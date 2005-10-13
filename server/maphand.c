/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"

#include "events.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "packets.h"
#include "unit.h"

#include "citytools.h"
#include "cityturn.h"
#include "plrhand.h"           /* notify_player */
#include "sernet.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "maphand.h"

#define MAXIMUM_CLAIMED_OCEAN_SIZE (20)

/* These arrays are indexed by continent number (or negative of the
 * ocean number) so the 0th element is unused and the array is 1 element
 * larger than you'd expect.
 *
 * The lake surrounders array tells how many land continents surround each
 * ocean (or -1 if the ocean touches more than one continent).
 *
 * The _sizes arrays give the sizes (in tiles) of each continent and
 * ocean.
 */
static Continent_id *lake_surrounders;
static int *continent_sizes, *ocean_sizes;

/**************************************************************************
  Number this tile and nearby tiles (recursively) with the specified
  continent number nr, using a flood-fill algorithm.

  is_land tells us whether we are assigning continent numbers or ocean 
  numbers.

  if skip_unsafe is specified then "unsafe" terrains are skipped.  This
  is useful for mapgen algorithms.
**************************************************************************/
static void assign_continent_flood(struct tile *ptile, bool is_land,
				   int nr, bool skip_unsafe)
{
  if (tile_get_continent(ptile) != 0) {
    return;
  }

  if (ptile->terrain == T_UNKNOWN) {
    return;
  }

  if (skip_unsafe && terrain_has_flag(tile_get_terrain(ptile), TER_UNSAFE)) {
    /* FIXME: This should check a specialized flag, not the TER_UNSAFE
     * flag which may not even be present. */
    return;
  }

  if (!XOR(is_land, is_ocean(tile_get_terrain(ptile)))) {
    return;
  }

  tile_set_continent(ptile, nr);
  
  /* count the tile */
  if (nr < 0) {
    ocean_sizes[-nr]++;
  } else {
    continent_sizes[nr]++;
  }

  adjc_iterate(ptile, tile1) {
    assign_continent_flood(tile1, is_land, nr, skip_unsafe);
  } adjc_iterate_end;
}

/**************************************************************************
  Calculate lake_surrounders[] array
**************************************************************************/
static void recalculate_lake_surrounders(void)
{
  const size_t size = (map.num_oceans + 1) * sizeof(*lake_surrounders);

  lake_surrounders = fc_realloc(lake_surrounders, size);
  memset(lake_surrounders, 0, size);
  
  whole_map_iterate(ptile) {
    Continent_id cont = tile_get_continent(ptile);
    if (ptile->terrain != T_UNKNOWN && !is_ocean(tile_get_terrain(ptile))) {
      adjc_iterate(ptile, tile2) {
        Continent_id cont2 = tile_get_continent(tile2);
	if (is_ocean(tile_get_terrain(tile2))) {
	  if (lake_surrounders[-cont2] == 0) {
	    lake_surrounders[-cont2] = cont;
	  } else if (lake_surrounders[-cont2] != cont) {
	    lake_surrounders[-cont2] = -1;
	  }
	}
      } adjc_iterate_end;
    }
  } whole_map_iterate_end;
}

/**************************************************************************
  Assigns continent and ocean numbers to all tiles, and set
  map.num_continents and map.num_oceans.  Recalculates continent and
  ocean sizes, and lake_surrounders[] arrays.

  Continents have numbers 1 to map.num_continents _inclusive_.
  Oceans have (negative) numbers -1 to -map.num_oceans _inclusive_.

  If skip_unsafe is specified then unsafe terrains are not used to
  connect continents.  This is useful for generator code so that polar
  regions don't connect landmasses.
**************************************************************************/
void assign_continent_numbers(bool skip_unsafe)
{
  
  /* Initialize */
  map.num_continents = 0;
  map.num_oceans = 0;

  whole_map_iterate(ptile) {
    tile_set_continent(ptile, 0);
  } whole_map_iterate_end;

  /* Assign new numbers */
  whole_map_iterate(ptile) {
    const struct terrain *pterrain = tile_get_terrain(ptile);

    if (tile_get_continent(ptile) != 0) {
      /* Already assigned. */
      continue;
    }

    if (ptile->terrain == T_UNKNOWN) {
      continue; /* Can't assign this. */
    }

    if (!skip_unsafe || !terrain_has_flag(pterrain, TER_UNSAFE)) {
      if (!is_ocean(pterrain)) {
	map.num_continents++;
	continent_sizes
	  = fc_realloc(continent_sizes,
		       (map.num_continents + 1) * sizeof(*continent_sizes));
	continent_sizes[map.num_continents] = 0;
	assign_continent_flood(ptile, TRUE, map.num_continents, skip_unsafe);
      } else {
	map.num_oceans++;
	ocean_sizes
	  = fc_realloc(ocean_sizes,
		       (map.num_oceans + 1) * sizeof(*ocean_sizes));
	ocean_sizes[map.num_oceans] = 0;
	assign_continent_flood(ptile, FALSE, -map.num_oceans, skip_unsafe);
      }
    }
  } whole_map_iterate_end;

  recalculate_lake_surrounders();

  freelog(LOG_VERBOSE, "Map has %d continents and %d oceans", 
	  map.num_continents, map.num_oceans);
}

static void player_tile_init(struct tile *ptile, struct player *pplayer);
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 struct tile *ptile);
static void shared_vision_change_seen(struct tile *ptile, struct player *pplayer, int change);
static int map_get_seen(const struct tile *ptile,
			const struct player *pplayer);
static void map_change_own_seen(struct tile *ptile, struct player *pplayer,
				int change);

/**************************************************************************
Used only in global_warming() and nuclear_winter() below.
**************************************************************************/
static bool is_terrain_ecologically_wet(struct tile *ptile)
{
  return (tile_has_special(ptile, S_RIVER)
	  || is_ocean_near_tile(ptile)
	  || is_special_near_tile(ptile, S_RIVER));
}

/**************************************************************************
...
**************************************************************************/
void global_warming(int effect)
{
  int k;

  freelog(LOG_NORMAL, "Global warming: %d", game.info.heating);

  k = map_num_tiles();
  while(effect > 0 && (k--) > 0) {
    struct terrain *old, *new;
    struct tile *ptile;

    ptile = rand_map_pos();
    old = tile_get_terrain(ptile);
    if (is_terrain_ecologically_wet(ptile)) {
      new = old->warmer_wetter_result;
    } else {
      new = old->warmer_drier_result;
    }
    if (new != T_NONE && old != new) {
      effect--;
      tile_change_terrain(ptile, new);
      check_terrain_change(ptile, old);
      update_tile_knowledge(ptile);
      unit_list_iterate(ptile->units, punit) {
	if (!can_unit_continue_current_activity(punit)) {
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
	}
      } unit_list_iterate_end;
    } else if (old == new) {
      /* This counts toward warming although nothing is changed. */
      effect--;
    }
  }

  notify_player(NULL, NULL, E_GLOBAL_ECO,
		   _("Global warming has occurred!"));
  notify_player(NULL, NULL, E_GLOBAL_ECO,
		_("Coastlines have been flooded and vast "
		  "ranges of grassland have become deserts."));
}

/**************************************************************************
...
**************************************************************************/
void nuclear_winter(int effect)
{
  int k;

  freelog(LOG_NORMAL, "Nuclear winter: %d", game.info.cooling);

  k = map_num_tiles();
  while(effect > 0 && (k--) > 0) {
    struct terrain *old, *new;
    struct tile *ptile;

    ptile = rand_map_pos();
    old = tile_get_terrain(ptile);
    if (is_terrain_ecologically_wet(ptile)) {
      new = old->cooler_wetter_result;
    } else {
      new = old->cooler_drier_result;
    }
    if (new != T_NONE && old != new) {
      effect--;
      tile_change_terrain(ptile, new);
      check_terrain_change(ptile, old);
      update_tile_knowledge(ptile);
      unit_list_iterate(ptile->units, punit) {
	if (!can_unit_continue_current_activity(punit)) {
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
	}
      } unit_list_iterate_end;
    } else if (old == new) {
      /* This counts toward winter although nothing is changed. */
      effect--;
    }
  }

  notify_player(NULL, NULL, E_GLOBAL_ECO,
		   _("Nuclear winter has occurred!"));
  notify_player(NULL, NULL, E_GLOBAL_ECO,
		_("Wetlands have dried up and vast "
		  "ranges of grassland have become tundra."));
}

/***************************************************************
To be called when a player gains the Railroad tech for the first
time.  Sends a message, and then upgrade all city squares to
railroads.  "discovery" just affects the message: set to
   1 if the tech is a "discovery",
   0 if otherwise acquired (conquer/trade/GLib).        --dwp
***************************************************************/
void upgrade_city_rails(struct player *pplayer, bool discovery)
{
  if (!(terrain_control.may_road)) {
    return;
  }

  conn_list_do_buffer(pplayer->connections);

  if (discovery) {
    notify_player(pplayer, NULL, E_TECH_GAIN,
		  _("New hope sweeps like fire through the country as "
		    "the discovery of railroad is announced.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  } else {
    notify_player(pplayer, NULL, E_TECH_GAIN,
		  _("The people are pleased to hear that your "
		    "scientists finally know about railroads.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  }
  
  city_list_iterate(pplayer->cities, pcity) {
    tile_set_special(pcity->tile, S_RAILROAD);
    update_tile_knowledge(pcity->tile);
  }
  city_list_iterate_end;

  conn_list_do_unbuffer(pplayer->connections);
}

/**************************************************************************
Return TRUE iff the player me really gives shared vision to player them.
**************************************************************************/
static bool really_gives_vision(struct player *me, struct player *them)
{
  return TEST_BIT(me->really_gives_vision, them->player_no);
}

/**************************************************************************
...
**************************************************************************/
static void buffer_shared_vision(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2))
      conn_list_do_buffer(pplayer2->connections);
  } players_iterate_end;
  conn_list_do_buffer(pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
static void unbuffer_shared_vision(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2))
      conn_list_do_unbuffer(pplayer2->connections);
  } players_iterate_end;
  conn_list_do_unbuffer(pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(ptile) {
    give_tile_info_from_player_to_player(pfrom, pdest, ptile);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(ptile) {
    if (is_ocean(tile_get_terrain(ptile))) {
      give_tile_info_from_player_to_player(pfrom, pdest, ptile);
    }
  } whole_map_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
...
**************************************************************************/
void give_citymap_from_player_to_player(struct city *pcity,
					struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  map_city_radius_iterate(pcity->tile, ptile) {
    give_tile_info_from_player_to_player(pfrom, pdest, ptile);
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
  Send all tiles known to specified clients.
  If dest is NULL means game.est_connections.
  
  Note for multiple connections this may change "sent" multiple times
  for single player.  This is ok, because "sent" data is just optimised
  calculations, so it will be correct before this, for each connection
  during this, and at end.
**************************************************************************/
void send_all_known_tiles(struct conn_list *dest)
{
  int tiles_sent;

  if (!dest) {
    dest = game.est_connections;
  }

  /* send whole map piece by piece to each player to balance the load
     of the send buffers better */
  tiles_sent = 0;
  conn_list_do_buffer(dest);

  whole_map_iterate(ptile) {
    tiles_sent++;
    if ((tiles_sent % map.xsize) == 0) {
      conn_list_do_unbuffer(dest);
      flush_packets();
      conn_list_do_buffer(dest);
    }

    send_tile_info(dest, ptile);
  } whole_map_iterate_end;

  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
  Send tile information to all the clients in dest which know and see
  the tile. If dest is NULL, sends to all clients (game.est_connections)
  which know and see tile.

  Note that this function does not update the playermap.  For that call
  update_tile_knowledge().
**************************************************************************/
void send_tile_info(struct conn_list *dest, struct tile *ptile)
{
  struct packet_tile_info info;

  if (!dest) {
    dest = game.est_connections;
  }

  info.x = ptile->x;
  info.y = ptile->y;
  if (ptile->spec_sprite) {
    sz_strlcpy(info.spec_sprite, ptile->spec_sprite);
  } else {
    info.spec_sprite[0] = '\0';
  }

  conn_list_iterate(dest, pconn) {
    struct player *pplayer = pconn->player;
    enum tile_special_type spe;

    if (!pplayer && !pconn->observer) {
      continue;
    }
    if (!pplayer || map_is_known_and_seen(ptile, pplayer)) {
      info.known = TILE_KNOWN;
      info.type = ptile->terrain->index;
      for (spe = 0; spe < S_LAST; spe++) {
	info.special[spe] = BV_ISSET(ptile->special, spe);
      }
      info.continent = ptile->continent;
      info.owner = ptile->owner ? ptile->owner->player_no : MAP_TILE_OWNER_NULL;
      send_packet_tile_info(pconn, &info);
    } else if (pplayer && map_is_known(ptile, pplayer)
	       && map_get_seen(ptile, pplayer) == 0) {
      struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

      info.known = TILE_KNOWN_FOGGED;
      info.type = plrtile->terrain->index;
      info.owner = plrtile->owner >= 0 ? plrtile->owner : MAP_TILE_OWNER_NULL;
      for (spe = 0; spe < S_LAST; spe++) {
	info.special[spe] = BV_ISSET(plrtile->special, spe);
      }
      info.continent = ptile->continent;
      send_packet_tile_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static int map_get_pending_seen(struct player *pplayer, struct tile *ptile)
{
  return map_get_player_tile(ptile, pplayer)->pending_seen;
}

/**************************************************************************
...
**************************************************************************/
static void map_set_pending_seen(struct player *pplayer, struct tile *ptile, int newv)
{
  map_get_player_tile(ptile, pplayer)->pending_seen = newv;
}

/**************************************************************************
...
**************************************************************************/
static void increment_pending_seen(struct player *pplayer, struct tile *ptile)
{
  map_get_player_tile(ptile, pplayer)->pending_seen += 1;
}

/**************************************************************************
...
**************************************************************************/
static void decrement_pending_seen(struct player *pplayer, struct tile *ptile)
{
  struct player_tile *plr_tile = map_get_player_tile(ptile, pplayer);
  assert(plr_tile->pending_seen != 0);
  plr_tile->pending_seen -= 1;
}

/**************************************************************************
...
**************************************************************************/
static void reveal_pending_seen(struct player *pplayer, struct tile *ptile, int len)
{
  square_iterate(ptile, len, tile1) {
    int pseen = map_get_pending_seen(pplayer, tile1);
    map_set_pending_seen(pplayer, tile1, 0);
    while (pseen > 0) {
      unfog_area(pplayer, tile1, 0);
      pseen--;
    }
  } square_iterate_end;
}

/*************************************************************************
 * Checks for hidden units around (x,y).  Such units can be invisible even
 * on a KNOWN_AND_SEEN tile, so unfogging might not reveal them.
 ************************************************************************/
void reveal_hidden_units(struct player *pplayer, struct tile *ptile)
{
  adjc_iterate(ptile, tile1) {
    unit_list_iterate(tile1->units, punit) {
      if (is_hiding_unit(punit)) {
        /* send_unit_info will check whether it is visible */
        send_unit_info(pplayer, punit);
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;
}

/*************************************************************************
  Checks for hidden units around (x,y).  Such units can be invisible even
  on a KNOWN_AND_SEEN tile, so fogging might not hide them.

  Note, this must be called after the unit/vision source at ptile has
  been removed, unlike remove_unit_sight_points.
************************************************************************/
void conceal_hidden_units(struct player *pplayer, struct tile *ptile)
{
  /* Remove vision of submarines.  This is extremely ugly and inefficient. */
  adjc_iterate(ptile, tile1) {
    unit_list_iterate(tile1->units, phidden_unit) {
      if (phidden_unit->transported_by == -1
	  && is_hiding_unit(phidden_unit)) {
	players_iterate(pplayer2) {
	  if ((pplayer2 == pplayer || really_gives_vision(pplayer, pplayer2))
	      && !can_player_see_unit(pplayer2, phidden_unit)) {
	    unit_goes_out_of_sight(pplayer2, phidden_unit);
	  }
	} players_iterate_end;
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void really_unfog_area(struct player *pplayer, struct tile *ptile)
{
  struct city *pcity;
  bool old_known = map_is_known(ptile, pplayer);

  freelog(LOG_DEBUG, "really unfogging %d,%d\n", TILE_XY(ptile));

  map_set_known(ptile, pplayer);

  /* send info about the tile itself 
   * It has to be sent first because the client needs correct
   * continent number before it can handle following packets
   */
  update_player_tile_knowledge(pplayer, ptile);
  send_tile_info(pplayer->connections, ptile);

  /* discover units */
  unit_list_iterate(ptile->units, punit)
    send_unit_info(pplayer, punit);
  unit_list_iterate_end;

  /* discover cities */ 
  reality_check_city(pplayer, ptile);
  if ((pcity=tile_get_city(ptile)))
    send_city_info(pplayer, pcity);

  /* If the tile was not known before we need to refresh the cities that
     can use the tile. */
  if (!old_known) {
    map_city_radius_iterate(ptile, tile1) {
      pcity = tile_get_city(tile1);
      if (pcity && city_owner(pcity) == pplayer) {
	update_city_tile_status_map(pcity, ptile);
      }
    } map_city_radius_iterate_end;
    sync_cities();
  }
}

/**************************************************************************
  Add an extra point of visibility to a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
  pplayer may not be NULL.
**************************************************************************/
void unfog_area(struct player *pplayer, struct tile *ptile, int len)
{
  /* Did the tile just become visible?
     - send info about units and cities and the tile itself */
  buffer_shared_vision(pplayer);
  square_iterate(ptile, len, tile1) {
    /* the player himself */
    shared_vision_change_seen(tile1, pplayer, +1);
    if (map_get_seen(tile1, pplayer) == 1
	|| !map_is_known(tile1, pplayer)) {
      really_unfog_area(pplayer, tile1);
    }

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      if (!really_gives_vision(pplayer, pplayer2)) {
	continue;
      }

      if (map_get_seen(tile1, pplayer2) == 1
	  || !map_is_known(tile1, pplayer2)) {
	really_unfog_area(pplayer2, tile1);
      }
      reveal_pending_seen(pplayer2, tile1, 0);
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, ptile, len);
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
...
**************************************************************************/
static void really_fog_area(struct player *pplayer, struct tile *ptile)
{
  freelog(LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.",
	  TILE_XY(ptile), map_get_seen(ptile, pplayer));
 
  assert(map_get_seen(ptile, pplayer) == 0);

  unit_list_iterate(ptile->units, punit)
    unit_goes_out_of_sight(pplayer,punit);
  unit_list_iterate_end;  

  update_player_tile_last_seen(pplayer, ptile);
  send_tile_info(pplayer->connections, ptile);
}

/**************************************************************************
  Remove a point of visibility from a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
**************************************************************************/
void fog_area(struct player *pplayer, struct tile *ptile, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(ptile, len, tile1) {
    if (map_is_known(tile1, pplayer)) {
      /* the player himself */
      shared_vision_change_seen(tile1, pplayer, -1);
      if (map_get_seen(tile1, pplayer) == 0) {
	really_fog_area(pplayer, tile1);
      }

      /* players (s)he gives shared vision */
      players_iterate(pplayer2) {
	if (!really_gives_vision(pplayer, pplayer2)) {
	  continue;
	}
	if (map_get_seen(tile1, pplayer2) == 0) {
	  really_fog_area(pplayer2, tile1);
	}
      } players_iterate_end;
    } else {
      decrement_pending_seen(pplayer, tile1);
    }
  } square_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
  Send basic map information: map size, topology, and is_earth.
**************************************************************************/
void send_map_info(struct conn_list *dest)
{
  struct packet_map_info minfo;

  minfo.xsize=map.xsize;
  minfo.ysize=map.ysize;
  minfo.topology_id = map.topology_id;
 
  lsend_packet_map_info(dest, &minfo);
}

/**************************************************************************
  Changes the vision range of the city.  The new range is given in
  the radius_sq.
**************************************************************************/
static void map_refog_city_area(struct city *pcity, int new_radius_sq)
{
  if (pcity) {
    map_refog_circle(city_owner(pcity), pcity->tile,
		     pcity->server.vision_radius_sq, new_radius_sq, TRUE);
    pcity->server.vision_radius_sq = new_radius_sq;
  } else {
    freelog(LOG_ERROR, "Attempting to change fog for non-existent city");
  }
}

/**************************************************************************
  Fogs the area visible by the city.  Unlike other unfog functions this
  one may be called multiple times in a row without any penalty.  Call it
  before destroying the city (or go straight to the source and use
  map_refog_circle by hand).
**************************************************************************/
void map_fog_city_area(struct city *pcity)
{
  map_refog_city_area(pcity, -1);
}

/**************************************************************************
  Unfogs the area visible by the city.  Unlike other unfog functions this
  one may be called multiple times in a row without any penalty.  Basically
  it should be called any time the city's vision range may have changed.
**************************************************************************/
void map_unfog_city_area(struct city *pcity)
{
  map_refog_city_area(pcity,
		      get_city_bonus(pcity, EFT_CITY_VISION_RADIUS_SQ));
}

/**************************************************************************
...
**************************************************************************/
static void shared_vision_change_seen(struct tile *ptile, struct player *pplayer, int change)
{
  map_change_seen(ptile, pplayer, change);
  map_change_own_seen(ptile, pplayer, change);

  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2))
      map_change_seen(ptile, pplayer2, change);
  } players_iterate_end;
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_refog_circle(struct player *pplayer, struct tile *ptile,
		      int old_radius_sq, int new_radius_sq, bool pseudo)
{
  if (old_radius_sq != new_radius_sq) {
    int max_radius = MAX(old_radius_sq, new_radius_sq);

    freelog(LOG_DEBUG, "Refogging circle at %d,%d from %d to %d",
	    TILE_XY(ptile), old_radius_sq, new_radius_sq);

    buffer_shared_vision(pplayer);
    circle_dxyr_iterate(ptile, max_radius, tile1, dx, dy, dr) {
      if (dr > old_radius_sq && dr <= new_radius_sq) {
	if (!pseudo || map_is_known(tile1, pplayer)) {
	  unfog_area(pplayer, tile1, 0);
	} else {
	  increment_pending_seen(pplayer, tile1);
	}
      } else if (dr > new_radius_sq && dr <= old_radius_sq) {
	if (!pseudo || map_is_known(tile1, pplayer)) {
	  fog_area(pplayer, tile1, 0);
	} else {
	  decrement_pending_seen(pplayer, tile1);
	}
      }
    } circle_dxyr_iterate_end;
    unbuffer_shared_vision(pplayer);
  }
}

/**************************************************************************
For removing a unit. The actual removal is done in server_remove_unit
**************************************************************************/
void remove_unit_sight_points(struct unit *punit)
{
  struct tile *ptile = punit->tile;
  struct player *pplayer = unit_owner(punit);

  freelog(LOG_DEBUG, "Removing unit sight points at  %i,%i",
	  TILE_XY(punit->tile));

  if (tile_has_special(punit->tile, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    fog_area(pplayer, ptile, get_watchtower_vision(punit));
  else
    fog_area(pplayer, ptile, unit_type(punit)->vision_range);
}

/**************************************************************************
Shows area even if still fogged. If the tile is not "seen" units are not
shown
**************************************************************************/
static void really_show_area(struct player *pplayer, struct tile *ptile)
{
  struct city *pcity;
  bool old_known = map_is_known(ptile, pplayer);

  freelog(LOG_DEBUG, "Showing %i,%i", TILE_XY(ptile));

  if (!map_is_known_and_seen(ptile, pplayer)) {
    map_set_known(ptile, pplayer);

    /* as the tile may be fogged send_tile_info won't always do this for us */
    update_player_tile_knowledge(pplayer, ptile);
    update_player_tile_last_seen(pplayer, ptile);

    send_tile_info(pplayer->connections, ptile);

    /* remove old cities that exist no more */
    reality_check_city(pplayer, ptile);
    if ((pcity = tile_get_city(ptile))) {
      /* as the tile may be fogged send_city_info won't do this for us */
      update_dumb_city(pplayer, pcity);
      send_city_info(pplayer, pcity);
    }

    if (map_get_seen(ptile, pplayer) != 0) {
      unit_list_iterate(ptile->units, punit)
	send_unit_info(pplayer, punit);
      unit_list_iterate_end;
    }

    /* If the tile was not known before we need to refresh the cities that
       can use the tile. */
    if (!old_known) {
      map_city_radius_iterate(ptile, tile1) {
	pcity = tile_get_city(tile1);
	if (pcity && city_owner(pcity) == pplayer) {
	  update_city_tile_status_map(pcity, ptile);
	}
      } map_city_radius_iterate_end;
      sync_cities();
    }
  }
}

/**************************************************************************
Shows area, ie send terrain etc., even if still fogged, sans units and cities.
**************************************************************************/
void show_area(struct player *pplayer, struct tile *ptile, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(ptile, len, tile1) {
    /* the player himself */
    really_show_area(pplayer, tile1);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      if (really_gives_vision(pplayer, pplayer2)) {
	really_show_area(pplayer2, tile1);
	reveal_pending_seen(pplayer2, tile1, 0);
      }
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, ptile, len);
  unbuffer_shared_vision(pplayer);
}

/****************************************************************************
  Return whether the player knows the tile.  Knowing a tile means you've
  seen it once (as opposed to seeing a tile which means you can see it now).
****************************************************************************/
bool map_is_known(const struct tile *ptile, const struct player *pplayer)
{
  return BV_ISSET(ptile->tile_known, pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
bool map_is_known_and_seen(const struct tile *ptile, struct player *pplayer)
{
  assert(BV_ISSET(ptile->tile_seen, pplayer->player_no)
	 == (map_get_player_tile(ptile, pplayer)->seen_count > 0));
  return (BV_ISSET(ptile->tile_known, pplayer->player_no)
	  && BV_ISSET(ptile->tile_seen, pplayer->player_no));
}

/****************************************************************************
  Return whether the player can see the tile.  Seeing a tile means you have
  vision of it now (as opposed to knowing a tile which means you've seen it
  before).  Note that a tile can be seen but not known (currently this only
  happens when a city is founded with some unknown tiles in its radius); in
  this case the tile is unknown (but map_get_seen will still return TRUE).
****************************************************************************/
static int map_get_seen(const struct tile *ptile,
			const struct player *pplayer)
{
  assert(BV_ISSET(ptile->tile_seen, pplayer->player_no)
	 == (map_get_player_tile(ptile, pplayer)->seen_count > 0));
  return map_get_player_tile(ptile, pplayer)->seen_count;
}

/***************************************************************
...
***************************************************************/
void map_change_seen(struct tile *ptile, struct player *pplayer, int change)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  assert(0 <= change || -change <= plrtile->seen_count); /* else underflow */

  plrtile->seen_count += change;
  if (plrtile->seen_count != 0) {
    BV_SET(ptile->tile_seen, pplayer->player_no);
  } else {
    BV_CLR(ptile->tile_seen, pplayer->player_no);
  }
  freelog(LOG_DEBUG, "%d,%d, p: %d, change %d, result %d\n", TILE_XY(ptile),
	  pplayer->player_no, change, plrtile->seen_count);
}

/***************************************************************
...
***************************************************************/
static int map_get_own_seen(struct tile *ptile, struct player *pplayer)
{
  int own_seen = map_get_player_tile(ptile, pplayer)->own_seen;
  if (own_seen != 0) {
    assert(map_is_known(ptile, pplayer));
  }
  return own_seen;
}

/***************************************************************
...
***************************************************************/
static void map_change_own_seen(struct tile *ptile, struct player *pplayer,
				int change)
{
  map_get_player_tile(ptile, pplayer)->own_seen += change;
}

/***************************************************************
...
***************************************************************/
void map_set_known(struct tile *ptile, struct player *pplayer)
{
  BV_SET(ptile->tile_known, pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_clear_known(struct tile *ptile, struct player *pplayer)
{
  BV_CLR(ptile->tile_known, pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_know_all(struct player *pplayer)
{
  whole_map_iterate(ptile) {
    show_area(pplayer, ptile, 0);
  } whole_map_iterate_end;
}

/***************************************************************
...
***************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  whole_map_iterate(ptile) {
    unfog_area(pplayer, ptile, 0);
  } whole_map_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void show_map_to_all(void)
{
  players_iterate(pplayer) {
    map_know_and_see_all(pplayer);
  } players_iterate_end;
}

/***************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
****************************************************************/
void player_map_allocate(struct player *pplayer)
{
  pplayer->private_map
    = fc_malloc(MAP_INDEX_SIZE * sizeof(*pplayer->private_map));
  whole_map_iterate(ptile) {
    player_tile_init(ptile, pplayer);
  } whole_map_iterate_end;
}

/***************************************************************
 frees a player's private map.
***************************************************************/
void player_map_free(struct player *pplayer)
{
  if (!pplayer->private_map) {
    return;
  }

  whole_map_iterate(ptile) {
    struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

    if (plrtile->city) {
      free(plrtile->city);
    }
  } whole_map_iterate_end;

  free(pplayer->private_map);
  pplayer->private_map = NULL;
}

/***************************************************************
We need to use use fogofwar_old here, so the player's tiles get
in the same state as the other players' tiles.
***************************************************************/
static void player_tile_init(struct tile *ptile, struct player *pplayer)
{
  struct player_tile *plrtile =
    map_get_player_tile(ptile, pplayer);

  plrtile->terrain = T_UNKNOWN;
  BV_CLR_ALL(plrtile->special);
  plrtile->city = NULL;

  plrtile->seen_count = 0;
  plrtile->pending_seen = 0;
  BV_CLR(ptile->tile_seen, pplayer->player_no);
  if (!game.fogofwar_old) {
    if (map_is_known(ptile, pplayer)) {
      plrtile->seen_count = 1;
      BV_SET(ptile->tile_seen, pplayer->player_no);
    } else {
      plrtile->pending_seen = 1;
    }
  }

  plrtile->last_updated = GAME_START_YEAR;
  plrtile->own_seen = plrtile->seen_count;
  plrtile->owner = -1;
}

/****************************************************************************
  Players' information of tiles is tracked so that fogged area can be kept
  consistent even when the client disconnects.  This function returns the
  player tile information for the given tile and player.
****************************************************************************/
struct player_tile *map_get_player_tile(const struct tile *ptile,
					const struct player *pplayer)
{
  return pplayer->private_map + ptile->index;
}

/****************************************************************************
  Give pplayer the correct knowledge about tile; return TRUE iff
  knowledge changed.

  Note that unlike update_tile_knowledge, this function will not send any
  packets to the client.  Callers may want to call send_tile_info() if this
  function returns TRUE.
****************************************************************************/
bool update_player_tile_knowledge(struct player *pplayer, struct tile *ptile)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  if (plrtile->terrain != ptile->terrain
      || memcmp(&plrtile->special, &ptile->special,
		sizeof(plrtile->special)) != 0
      || (ptile->owner && plrtile->owner != ptile->owner->player_no)
      || (!ptile->owner && plrtile->owner >= 0)) {
    plrtile->terrain = ptile->terrain;
    plrtile->special = ptile->special;
    if (ptile->owner) {
      plrtile->owner = ptile->owner->player_no;
    } else {
      plrtile->owner = -1;
    }
    return TRUE;
  }
  return FALSE;
}

/****************************************************************************
  Update playermap knowledge for everybody who sees the tile, and send a
  packet to everyone whose info is changed.

  Note this only checks for changing of the terrain or special for the
  tile, since these are the only values held in the playermap.
****************************************************************************/
void update_tile_knowledge(struct tile *ptile)
{
  /* Players */
  players_iterate(pplayer) {
    if (map_is_known_and_seen(ptile, pplayer)) {
      if (update_player_tile_knowledge(pplayer, ptile)) {
        send_tile_info(pplayer->connections, ptile);
      }
    }
  } players_iterate_end;

  /* Global observers */
  conn_list_iterate(game.est_connections, pconn) {
    struct player *pplayer = pconn->player;
    if (!pplayer && pconn->observer) {
      send_tile_info(pconn->self, ptile);
    }
  } conn_list_iterate_end;
}

/***************************************************************
...
***************************************************************/
void update_player_tile_last_seen(struct player *pplayer, struct tile *ptile)
{
  map_get_player_tile(ptile, pplayer)->last_updated = game.info.year;
}

/***************************************************************
...
***************************************************************/
static void really_give_tile_info_from_player_to_player(struct player *pfrom,
							struct player *pdest,
							struct tile *ptile)
{
  struct player_tile *from_tile, *dest_tile;
  if (!map_is_known_and_seen(ptile, pdest)) {
    /* I can just hear people scream as they try to comprehend this if :).
     * Let me try in words:
     * 1) if the tile is seen by pfrom the info is sent to pdest
     *  OR
     * 2) if the tile is known by pfrom AND (he has more recent info
     *     OR it is not known by pdest)
     */
    if (map_is_known_and_seen(ptile, pfrom)
	|| (map_is_known(ptile, pfrom)
	    && (((map_get_player_tile(ptile, pfrom)->last_updated
		 > map_get_player_tile(ptile, pdest)->last_updated))
	        || !map_is_known(ptile, pdest)))) {
      from_tile = map_get_player_tile(ptile, pfrom);
      dest_tile = map_get_player_tile(ptile, pdest);
      /* Update and send tile knowledge */
      map_set_known(ptile, pdest);
      dest_tile->terrain = from_tile->terrain;
      dest_tile->special = from_tile->special;
      dest_tile->owner = from_tile->owner;
      dest_tile->last_updated = from_tile->last_updated;
      send_tile_info(pdest->connections, ptile);
	
      /* update and send city knowledge */
      /* remove outdated cities */
      if (dest_tile->city) {
	if (!from_tile->city) {
	  /* As the city was gone on the newer from_tile
	     it will be removed by this function */
	  reality_check_city(pdest, ptile);
	} else /* We have a dest_city. update */
	  if (from_tile->city->id != dest_tile->city->id)
	    /* As the city was gone on the newer from_tile
	       it will be removed by this function */
	    reality_check_city(pdest, ptile);
      }
      /* Set and send new city info */
      if (from_tile->city) {
	if (!dest_tile->city) {
	  dest_tile->city = fc_malloc(sizeof(struct dumb_city));
	}
	*dest_tile->city = *from_tile->city;
	send_city_info_at_tile(pdest, pdest->connections, NULL, ptile);
      }

      reveal_pending_seen(pdest, ptile, 0);

      map_city_radius_iterate(ptile, tile1) {
	struct city *pcity = tile_get_city(tile1);
	if (pcity && city_owner(pcity) == pdest) {
	  update_city_tile_status_map(pcity, ptile);
	}
      } map_city_radius_iterate_end;
      sync_cities();
    }
  }
}

/***************************************************************
...
***************************************************************/
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 struct tile *ptile)
{
  really_give_tile_info_from_player_to_player(pfrom, pdest, ptile);

  players_iterate(pplayer2) {
    if (!really_gives_vision(pdest, pplayer2))
      continue;
    really_give_tile_info_from_player_to_player(pfrom, pplayer2, ptile);
  } players_iterate_end;
}

/***************************************************************
This updates all players' really_gives_vision field.
If p1 gives p2 shared vision and p2 gives p3 shared vision p1
should also give p3 shared vision.
***************************************************************/
static void create_vision_dependencies(void)
{
  int added;

  players_iterate(pplayer) {
    pplayer->really_gives_vision = pplayer->gives_shared_vision;
  } players_iterate_end;

  /* In words: This terminates when it has run a round without adding
     a dependency. One loop only propagates dependencies one level deep,
     which is why we keep doing it as long as changes occur. */
  do {
    added = 0;
    players_iterate(pplayer) {
      players_iterate(pplayer2) {
	if (really_gives_vision(pplayer, pplayer2)
	    && pplayer != pplayer2) {
	  players_iterate(pplayer3) {
	    if (really_gives_vision(pplayer2, pplayer3)
		&& !really_gives_vision(pplayer, pplayer3)
		&& pplayer != pplayer3) {
	      pplayer->really_gives_vision |= (1<<pplayer3->player_no);
	      added++;
	    }
	  } players_iterate_end;
	}
      } players_iterate_end;
    } players_iterate_end;
  } while (added > 0);
}

/***************************************************************
...
***************************************************************/
void give_shared_vision(struct player *pfrom, struct player *pto)
{
  int save_vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS];
  if (pfrom == pto) return;
  if (gives_shared_vision(pfrom, pto)) {
    freelog(LOG_ERROR, "Trying to give shared vision from %s to %s, "
	    "but that vision is already given!",
	    pfrom->name, pto->name);
    return;
  }

  players_iterate(pplayer) {
    save_vision[pplayer->player_no] = pplayer->really_gives_vision;
  } players_iterate_end;

  pfrom->gives_shared_vision |= 1<<pto->player_no;
  create_vision_dependencies();
  freelog(LOG_DEBUG, "giving shared vision from %s to %s\n",
	  pfrom->name, pto->name);

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (really_gives_vision(pplayer, pplayer2)
	  && !TEST_BIT(save_vision[pplayer->player_no], pplayer2->player_no)) {
	freelog(LOG_DEBUG, "really giving shared vision from %s to %s\n",
	       pplayer->name, pplayer2->name);
	whole_map_iterate(ptile) {
	  int change = map_get_own_seen(ptile, pplayer);
	  if (change != 0) {
	    map_change_seen(ptile, pplayer2, change);
	    if (map_get_seen(ptile, pplayer2) == change) {
	      really_unfog_area(pplayer2, ptile);
	      reveal_pending_seen(pplayer2, ptile, 0);
	    }
	  }
	} whole_map_iterate_end;

	/* squares that are not seen, but which pfrom may have more recent
	   knowledge of */
	give_map_from_player_to_player(pplayer, pplayer2);
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (server_state == RUN_GAME_STATE)
    send_player_info(pfrom, NULL);
}

/***************************************************************
...
***************************************************************/
void remove_shared_vision(struct player *pfrom, struct player *pto)
{
  int save_vision[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS];
  assert(pfrom != pto);
  if (!gives_shared_vision(pfrom, pto)) {
    freelog(LOG_ERROR, "Tried removing the shared vision from %s to %s, "
	    "but it did not exist in the first place!",
	    pfrom->name, pto->name);
    return;
  }

  players_iterate(pplayer) {
    save_vision[pplayer->player_no] = pplayer->really_gives_vision;
  } players_iterate_end;

  freelog(LOG_DEBUG, "removing shared vision from %s to %s\n",
	 pfrom->name, pto->name);

  pfrom->gives_shared_vision &= ~(1<<pto->player_no);
  create_vision_dependencies();

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (!really_gives_vision(pplayer, pplayer2)
	  && TEST_BIT(save_vision[pplayer->player_no], pplayer2->player_no)) {
	freelog(LOG_DEBUG, "really removing shared vision from %s to %s\n",
	       pplayer->name, pplayer2->name);
	whole_map_iterate(ptile) {
	  int change = map_get_own_seen(ptile, pplayer);
	  if (change > 0) {
	    map_change_seen(ptile, pplayer2, -change);
	    if (map_get_seen(ptile, pplayer2) == 0)
	      really_fog_area(pplayer2, ptile);
	  }
	} whole_map_iterate_end;
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (server_state == RUN_GAME_STATE) {
    send_player_info(pfrom, NULL);
  }
}

/*************************************************************************
...
*************************************************************************/
static void enable_fog_of_war_player(struct player *pplayer)
{
  whole_map_iterate(ptile) {
    if (map_is_known(ptile, pplayer)) {
      fog_area(pplayer, ptile, 0);
    } else {
      decrement_pending_seen(pplayer, ptile);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
void enable_fog_of_war(void)
{
  players_iterate(pplayer) {
    enable_fog_of_war_player(pplayer);
  } players_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
static void disable_fog_of_war_player(struct player *pplayer)
{
  whole_map_iterate(ptile) {
    if (map_is_known(ptile, pplayer)) {
      unfog_area(pplayer, ptile, 0);
    } else {
      increment_pending_seen(pplayer, ptile);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
...
*************************************************************************/
void disable_fog_of_war(void)
{
  players_iterate(pplayer) {
    disable_fog_of_war_player(pplayer);
  } players_iterate_end;
}

/**************************************************************************
  Set the tile to be a river if required.
  It's required if one of the tiles nearby would otherwise be part of a
  river to nowhere.
  For simplicity, I'm assuming that this is the only exit of the river,
  so I don't need to trace it across the continent.  --CJM
  Also, note that this only works for R_AS_SPECIAL type rivers.  --jjm
**************************************************************************/
static void ocean_to_land_fix_rivers(struct tile *ptile)
{
  /* clear the river if it exists */
  tile_clear_special(ptile, S_RIVER);

  cardinal_adjc_iterate(ptile, tile1) {
    if (tile_has_special(tile1, S_RIVER)) {
      bool ocean_near = FALSE;
      cardinal_adjc_iterate(tile1, tile2) {
        if (is_ocean(tile_get_terrain(tile2)))
          ocean_near = TRUE;
      } cardinal_adjc_iterate_end;
      if (!ocean_near) {
        tile_set_special(ptile, S_RIVER);
        return;
      }
    }
  } cardinal_adjc_iterate_end;
}

/****************************************************************************
  A helper function for check_terrain_change that moves units off of invalid
  terrain after it's been changed.
****************************************************************************/
static void bounce_units_on_terrain_change(struct tile *ptile)
{
  unit_list_iterate_safe(ptile->units, punit) {
    if (punit->tile == ptile
	&& punit->transported_by == -1
	&& !can_unit_exist_at_tile(punit, ptile)) {
      /* look for a nearby safe tile */
      adjc_iterate(ptile, ptile2) {
	if (can_unit_exist_at_tile(punit, ptile2)
	    && !is_non_allied_unit_tile(ptile2, unit_owner(punit))) {
	  freelog(LOG_VERBOSE,
		  "Moved %s's %s due to changing terrain at %d,%d.",
		  unit_owner(punit)->name, unit_name(punit->type),
		  punit->tile->x, punit->tile->y);
	  notify_player(unit_owner(punit),
			   punit->tile, E_UNIT_RELOCATED,
			   _("Moved your %s due to changing terrain."),
			   unit_name(punit->type));
	  (void) move_unit(punit, ptile2, 0);
	  if (punit->activity == ACTIVITY_SENTRY) {
	    handle_unit_activity_request(punit, ACTIVITY_IDLE);
	  }
	  break;
	}
      } adjc_iterate_end;
      if (punit->tile == ptile) {
	/* if we get here we could not move punit */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing land to sea at (%d, %d).",
		unit_owner(punit)->name, unit_name(punit->type),
		punit->tile->x, punit->tile->y);
	notify_player(unit_owner(punit),
			 punit->tile, E_UNIT_LOST,
			 _("Disbanded your %s due to changing terrain."),
			 unit_name(punit->type));
	wipe_unit(punit);
      }
    }
  } unit_list_iterate_safe_end;
}

/****************************************************************************
  Handles global side effects for a terrain change.  Call this in the
  server immediately after calling tile_change_terrain.
****************************************************************************/
void check_terrain_change(struct tile *ptile, struct terrain *oldter)
{
  struct terrain *newter = tile_get_terrain(ptile);
  bool ocean_toggled = FALSE;

  if (is_ocean(oldter) && !is_ocean(newter)) {
    /* ocean to land ... */
    ocean_to_land_fix_rivers(ptile);
    city_landlocked_sell_coastal_improvements(ptile);
    ocean_toggled = TRUE;
  } else if (!is_ocean(oldter) && is_ocean(newter)) {
    /* land to ocean ... */
    ocean_toggled = TRUE;
  }

  if (ocean_toggled) {
    bounce_units_on_terrain_change(ptile);
    assign_continent_numbers(FALSE);

    /* New continent numbers for all tiles to all players */
    send_all_known_tiles(NULL);
    
    map_update_borders_landmass_change(ptile);
  }
}

/*************************************************************************
  Return pointer to the oldest adjacent city to this tile.  If
  there is a city on the exact tile, that is returned instead.
*************************************************************************/
static struct city *map_get_adjc_city(struct tile *ptile)
{
  struct city *closest = NULL;   /* Closest city */

  if (ptile->city) {
    return ptile->city;
  }

  adjc_iterate(ptile, tile1) {
    if (tile1->city && 
         (!closest || tile1->city->turn_founded < closest->turn_founded)) {
      closest = tile1->city;
    }
  } adjc_iterate_end;

  return closest;
}

/*************************************************************************
  Ocean tile can be claimed iff one of the following conditions stands:
  a) it is an inland lake not larger than MAXIMUM_OCEAN_SIZE
  b) it is adjacent to only one continent and not more than two ocean tiles
  c) It is one tile away from a city (This function doesn't check it)
  The city, which claims the ocean has to be placed on the correct continent.
  in case a) The continent which surrounds the inland lake
  in case b) The only continent which is adjacent to the tile
  The correct continent is returned in *contp.
*************************************************************************/
static bool is_claimed_ocean(struct tile *ptile, Continent_id *contp)
{
  Continent_id cont = tile_get_continent(ptile);
  Continent_id cont2, other;
  int ocean_tiles;
  
  if (get_ocean_size(-cont) <= MAXIMUM_CLAIMED_OCEAN_SIZE &&
      lake_surrounders[-cont] > 0) {
    *contp = lake_surrounders[-cont];
    return TRUE;
  }
  
  other = 0;
  ocean_tiles = 0;
  adjc_iterate(ptile, tile2) {
    cont2 = tile_get_continent(tile2);
    if (cont2 == cont) {
      ocean_tiles++;
    } else {
      if (other == 0) {
        other = cont2;
      } else if (other != cont2) {
        return FALSE;
      }
    }
  } adjc_iterate_end;
  if (ocean_tiles <= 2) {
    *contp = other;
    return TRUE;
  } else {
    return FALSE;
  }
}

/*************************************************************************
  Return pointer to the closest city to this tile, which must be
  on the same continent if the city is not immediately adjacent.
  If two or more cities are equally distant, then return the
  oldest (i.e. the one with the lowest id). This also correctly
  works for water bases in SMAC mode, and allows coastal cities
  to claim one square of ocean. Inland lakes and long inlets act in
  the same way as the surrounding continent's land tiles. If no cities
  are within game.info.borders distance, returns NULL.

  NOTE: The behaviour of this function will eventually depend
  upon some planned ruleset options.
*************************************************************************/
static struct city *map_get_closest_city(struct tile *ptile)
{
  struct city *closest;  /* Closest city */

  closest = map_get_adjc_city(ptile);
  if (!closest) {
    int distsq;		/* Squared distance to city */
    /* integer arithmetic equivalent of (borders+0.5)**2 */
    int cldistsq = game.info.borders * (game.info.borders + 1);
    Continent_id cont = tile_get_continent(ptile);

    if (!is_ocean(tile_get_terrain(ptile)) || is_claimed_ocean(ptile, &cont)) {
      cities_iterate(pcity) {
	if (tile_get_continent(pcity->tile) == cont) {
          distsq = sq_map_distance(pcity->tile, ptile);
          if (distsq < cldistsq ||
               (distsq == cldistsq &&
                (!closest || closest->turn_founded > pcity->turn_founded))) {
            closest = pcity;
            cldistsq = distsq;
          } 
        }
      } cities_iterate_end;
    }
  }

  return closest;
}

/*************************************************************************
  Update tile worker states for all cities that have the given map tile
  within their radius. Does not sync with client.

  This function is inefficient and so should only be called when the
  owner actually changes.
*************************************************************************/
static void tile_update_owner(struct tile *ptile)
{
  /* This implementation is horribly inefficient, but this doesn't cause
   * problems since it's not called often. */
  cities_iterate(pcity) {
    update_city_tile_status_map(pcity, ptile);
  } cities_iterate_end;
}

/*************************************************************************
  Recalculate the borders around a given position.
*************************************************************************/
static void map_update_borders_recalculate_position(struct tile *ptile)
{
  struct city_list* cities_to_refresh = NULL;
  
  if (game.info.happyborders > 0) {
    cities_to_refresh = city_list_new();
  }
  
  if (game.info.borders > 0) {
    iterate_outward(ptile, game.info.borders, tile1) {
      struct city *pccity = map_get_closest_city(tile1);
      struct player *new_owner = pccity ? pccity->owner : NULL;

      if (new_owner != tile_get_owner(tile1)) {
	tile_set_owner(tile1, new_owner);
        update_tile_knowledge(tile1);
	tile_update_owner(tile1);
	/* Update happiness */
	if (game.info.happyborders > 0) {
	  unit_list_iterate(tile1->units, unit) {
	    struct city* homecity = find_city_by_id(unit->homecity);
	    bool already_listed = FALSE;
	    
	    if (!homecity) {
	      continue;
	    }
	    
	    city_list_iterate(cities_to_refresh, city2) {
	      if (city2 == homecity) {
	        already_listed = TRUE;
		break;
	      }
	    } city_list_iterate_end;
	    
	    if (!already_listed) {
	      city_list_prepend(cities_to_refresh, homecity);
	    }

	  } unit_list_iterate_end;
	}
      }
    } iterate_outward_end;
  }
 
  /* Update happiness in all homecities we have collected */ 
  if (game.info.happyborders > 0) {
    city_list_iterate(cities_to_refresh, to_refresh) {
      city_refresh(to_refresh);
      send_city_info(city_owner(to_refresh), to_refresh);
    } city_list_iterate_end;
    
    city_list_unlink_all(cities_to_refresh);
    city_list_free(cities_to_refresh);
  }
}

/*************************************************************************
  Modify national territories as resulting from a city being destroyed.
  x,y coords for (already deleted) city's location.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_city_destroyed(struct tile *ptile)
{
  map_update_borders_recalculate_position(ptile);
}

/*************************************************************************
  Modify national territories resulting from a change of landmass.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_landmass_change(struct tile *ptile)
{
  map_update_borders_recalculate_position(ptile);
}

/*************************************************************************
  Modify national territories resulting from new city or change of city
  ownership.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_city_change(struct city *pcity)
{
  map_update_borders_recalculate_position(pcity->tile);
}

/*************************************************************************
  Delete the territorial claims to all tiles.
*************************************************************************/
static void map_clear_borders(void)
{
  whole_map_iterate(ptile) {
    tile_set_owner(ptile, NULL);
  } whole_map_iterate_end;
}

/*************************************************************************
  Minimal code that calculates all national territories from scratch.
*************************************************************************/
static void map_calculate_territory(void)
{
  /* Clear any old territorial claims. */
  map_clear_borders();

  if (game.info.borders > 0) {
    /* Loop over all cities and claim territory. */
    cities_iterate(pcity) {
      /* Loop over all map tiles within this city's sphere of influence. */
      iterate_outward(pcity->tile, game.info.borders, ptile) {
	struct city *pccity = map_get_closest_city(ptile);

	if (pccity) {
	  tile_set_owner(ptile, pccity->owner);
	}
      } iterate_outward_end;
    } cities_iterate_end;
  }
}

/*************************************************************************
  Calculate all national territories from scratch.  This can be slow, but
  is only performed occasionally, i.e. after loading a saved game. Doesn't
  send any tile information to the clients. Tile worker states are updated
  as necessary, but not sync'd with client.
*************************************************************************/
void map_calculate_borders(void)
{
  if (game.info.borders > 0) {
    map_calculate_territory();

    /* Fix tile worker states. */
    cities_iterate(pcity) {
      map_city_radius_iterate(pcity->tile, tile1) {
        update_city_tile_status_map(pcity, tile1);
      } map_city_radius_iterate_end;
    } cities_iterate_end;
  }
}

/*************************************************************************
  Return size in tiles of the given continent(not ocean)
*************************************************************************/
int get_continent_size(Continent_id id)
{
  assert(id > 0);
  return continent_sizes[id];
}

/*************************************************************************
  Return size in tiles of the given ocean. You should use positive ocean
  number.
*************************************************************************/
int get_ocean_size(Continent_id id) 
{
  assert(id > 0);
  return ocean_sizes[id];
}
