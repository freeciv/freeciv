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

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "packets.h"
#include "rand.h"
#include "support.h"
#include "unit.h"

#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "plrhand.h"           /* notify_player */
#include "sernet.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "maphand.h"

static void player_tile_init(int x, int y, struct player *pplayer);
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 int x, int y);
static void send_tile_info_always(struct player *pplayer,
				  struct conn_list *dest, int x, int y);
static void shared_vision_change_seen(int x, int y, struct player *pplayer, int change);
static int map_get_seen(int x, int y, struct player *pplayer);
static void map_change_own_seen(int x, int y, struct player *pplayer,
				int change);

/**************************************************************************
Used only in global_warming() and nuclear_winter() below.
**************************************************************************/
static bool is_terrain_ecologically_wet(int x, int y)
{
  return (map_has_special(x, y, S_RIVER)
	  || is_ocean_near_tile(x, y)
	  || is_special_near_tile(x, y, S_RIVER));
}

/**************************************************************************
...
**************************************************************************/
void global_warming(int effect)
{
  int x, y, k;

  freelog(LOG_NORMAL, "Global warming: %d", game.heating);

  k = map_num_tiles();
  while(effect > 0 && (k--) > 0) {
    rand_map_pos(&x, &y);
    if (!is_ocean(map_get_terrain(x, y))) {
      if (is_terrain_ecologically_wet(x, y)) {
	switch (map_get_terrain(x, y)) {
	case T_FOREST:
	  effect--;
	  change_terrain(x, y, T_JUNGLE);
	  send_tile_info(NULL, x, y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  change_terrain(x, y, T_SWAMP);
	  send_tile_info(NULL, x, y);
	  break;
	default:
	  break;
	}
      } else {
	switch (map_get_terrain(x, y)) {
	case T_PLAINS:
	case T_GRASSLAND:
	case T_FOREST:
	  effect--;
	  change_terrain(x, y, T_DESERT);
	  send_tile_info(NULL, x, y);
	  break;
	default:
	  break;
	}
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	if (!can_unit_continue_current_activity(punit))
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } unit_list_iterate_end;
    }
  }

  notify_player_ex(NULL, -1, -1, E_GLOBAL_ECO,
		   _("Game: Global warming has occurred!"));
  notify_player(NULL, _("Game: Coastlines have been flooded and vast "
			"ranges of grassland have become deserts."));
}

/**************************************************************************
...
**************************************************************************/
void nuclear_winter(int effect)
{
  int x, y, k;

  freelog(LOG_NORMAL, "Nuclear winter: %d", game.cooling);

  k = map_num_tiles();
  while(effect > 0 && (k--) > 0) {
    rand_map_pos(&x, &y);
    if (!is_ocean(map_get_terrain(x, y))) {
      switch (map_get_terrain(x, y)) {
      case T_JUNGLE:
      case T_SWAMP:
      case T_PLAINS:
      case T_GRASSLAND:
	effect--;
	change_terrain(x, y,
		       is_terrain_ecologically_wet(x, y) ? T_DESERT : T_TUNDRA);
	send_tile_info(NULL, x, y);
	break;
      case T_TUNDRA:
	effect--;
	change_terrain(x, y, T_ARCTIC);
	send_tile_info(NULL, x, y);
	break;
      default:
	break;
      }
      unit_list_iterate(map_get_tile(x, y)->units, punit) {
	if (!can_unit_continue_current_activity(punit))
	  handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } unit_list_iterate_end;
    }
  }

  notify_player_ex(NULL, -1, -1, E_GLOBAL_ECO,
		   _("Game: Nuclear winter has occurred!"));
  notify_player(NULL, _("Game: Wetlands have dried up and vast "
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

  conn_list_do_buffer(&pplayer->connections);

  if (discovery) {
    notify_player(pplayer,
		  _("Game: New hope sweeps like fire through the country as "
		    "the discovery of railroad is announced.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  } else {
    notify_player(pplayer,
		  _("Game: The people are pleased to hear that your "
		    "scientists finally know about railroads.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  }
  
  city_list_iterate(pplayer->cities, pcity) {
    map_set_special(pcity->x, pcity->y, S_RAILROAD);
    send_tile_info(NULL, pcity->x, pcity->y);
  }
  city_list_iterate_end;

  conn_list_do_unbuffer(&pplayer->connections);
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
      conn_list_do_buffer(&pplayer2->connections);
  } players_iterate_end;
  conn_list_do_buffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
static void unbuffer_shared_vision(struct player *pplayer)
{
  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2))
      conn_list_do_unbuffer(&pplayer2->connections);
  } players_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(x, y) {
    give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);
  whole_map_iterate(x, y) {
    if (is_ocean(map_get_terrain(x, y))) {
      give_tile_info_from_player_to_player(pfrom, pdest, x, y);
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
  map_city_radius_iterate(pcity->x, pcity->y, x, y) {
    give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pdest);
}

/**************************************************************************
  Send all tiles known to specified clients.
  If dest is NULL means game.game_connections.
  
  Note for multiple connections this may change "sent" multiple times
  for single player.  This is ok, because "sent" data is just optimised
  calculations, so it will be correct before this, for each connection
  during this, and at end.
**************************************************************************/
void send_all_known_tiles(struct conn_list *dest)
{
  int tiles_sent;

  if (!dest) dest = &game.game_connections;

  /* send whole map piece by piece to each player to balance the load
     of the send buffers better */
  tiles_sent = 0;
  conn_list_do_buffer(dest);

  whole_map_iterate(x, y) {
    tiles_sent++;
    if ((tiles_sent % map.xsize) == 0) {
      conn_list_do_unbuffer(dest);
      flush_packets();
      conn_list_do_buffer(dest);
    }

    conn_list_iterate(*dest, pconn) {
      struct player *pplayer = pconn->player;

      if (!pplayer && !pconn->observer) {	/* no map needed */
        continue;
      }

      if (!pplayer) {
	send_tile_info_always(pplayer, &pconn->self, x, y);
      } else if (map_is_known(x, y, pplayer)) {
	send_tile_info_always(pplayer, &pconn->self, x, y);
      }
    } conn_list_iterate_end;
  } whole_map_iterate_end;

  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
  Send tile information to all the clients in dest which know and see
  the tile.  Also updates player knowledge.  If dest is NULL, sends to
  all clients (game.game_connections) which know and see tile.
**************************************************************************/
void send_tile_info(struct conn_list *dest, int x, int y)
{
  struct packet_tile_info info;
  struct tile *ptile;

  if (!dest) dest = &game.game_connections;

  ptile = map_get_tile(x, y);

  info.x = x;
  info.y = y;
  info.owner = ptile->owner ? ptile->owner->player_no : MAP_TILE_OWNER_NULL;
  if (ptile->spec_sprite) {
    sz_strlcpy(info.spec_sprite, ptile->spec_sprite);
  } else {
    info.spec_sprite[0] = '\0';
  }

  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (!pplayer && !pconn->observer) {
      continue;
    }
    if (!pplayer || map_is_known_and_seen(x, y, pplayer)) {
      info.known = TILE_KNOWN;
      info.type = ptile->terrain;
      info.special = ptile->special;
      info.continent = ptile->continent;
      if (pplayer) {
	update_tile_knowledge(pplayer,x,y);
      }
      send_packet_tile_info(pconn, &info);
    } else if (pplayer && map_is_known(x, y, pplayer)
	       && map_get_seen(x, y, pplayer) == 0) {
      /* Just update the owner */
      struct player_tile *plrtile = map_get_player_tile(x, y, pplayer);
      info.known = TILE_KNOWN_FOGGED;
      info.type = plrtile->terrain;
      info.special = plrtile->special;
      info.continent = ptile->continent;
      send_packet_tile_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  Send the tile information, as viewed by pplayer, to all specified
  connections.   The tile info is sent even if pplayer doesn't see or
  know the tile (setting appropriate info.known), as required for
  client drawing requirements in some cases (see doc/HACKING).
  Also updates pplayer knowledge if known and seen, else used old.
  pplayer==NULL means send "real" data, for observers
**************************************************************************/
static void send_tile_info_always(struct player *pplayer, struct conn_list *dest,
			   int x, int y)
{
  struct packet_tile_info info;
  struct tile *ptile;
  struct player_tile *plrtile;

  ptile = map_get_tile(x, y);

  info.x = x;
  info.y = y;
  info.owner = ptile->owner ? ptile->owner->player_no : MAP_TILE_OWNER_NULL;
  if (ptile->spec_sprite) {
    sz_strlcpy(info.spec_sprite, ptile->spec_sprite);
  } else {
    info.spec_sprite[0] = '\0';
  }

  if (!pplayer) {
    /* Observer sees all. */
    info.known=TILE_KNOWN;
    info.type = ptile->terrain;
    info.special = ptile->special;
    info.continent = ptile->continent;
  } else if (map_is_known(x, y, pplayer)) {
    if (map_get_seen(x, y, pplayer) != 0) {
      /* Known and seen. */
      /* Visible; update info. */
      update_tile_knowledge(pplayer,x,y);
      info.known = TILE_KNOWN;
    } else {
      /* Known but not seen. */
      info.known = TILE_KNOWN_FOGGED;
    }
    plrtile = map_get_player_tile(x, y, pplayer);
    info.type = plrtile->terrain;
    info.special = plrtile->special;
    info.continent = ptile->continent;
  } else {
    /* Unknown (the client needs these sometimes to draw correctly). */
    info.known = TILE_UNKNOWN;
    info.type = ptile->terrain;
    info.special = ptile->special;
    info.continent = ptile->continent;
  }
  lsend_packet_tile_info(dest, &info);
}

/**************************************************************************
...
**************************************************************************/
static int map_get_pending_seen(struct player *pplayer, int x, int y)
{
  return map_get_player_tile(x, y, pplayer)->pending_seen;
}

/**************************************************************************
...
**************************************************************************/
static void map_set_pending_seen(struct player *pplayer, int x, int y, int newv)
{
  map_get_player_tile(x, y, pplayer)->pending_seen = newv;
}

/**************************************************************************
...
**************************************************************************/
static void increment_pending_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(x, y, pplayer)->pending_seen += 1;
}

/**************************************************************************
...
**************************************************************************/
static void decrement_pending_seen(struct player *pplayer, int x, int y)
{
  struct player_tile *plr_tile = map_get_player_tile(x, y, pplayer);
  assert(plr_tile->pending_seen != 0);
  plr_tile->pending_seen -= 1;
}

/**************************************************************************
...
**************************************************************************/
static void reveal_pending_seen(struct player *pplayer, int x, int y, int len)
{
  square_iterate(x, y, len, x_itr, y_itr) {
    int pseen = map_get_pending_seen(pplayer, x_itr, y_itr);
    map_set_pending_seen(pplayer, x_itr, y_itr, 0);
    while (pseen > 0) {
      unfog_area(pplayer, x_itr, y_itr, 0);
      pseen--;
    }
  } square_iterate_end;
}

/*************************************************************************
 * Checks for hidden units around (x,y).  Such units can be invisible even
 * on a KNOWN_AND_SEEN tile, so unfogging might not reveal them.
 ************************************************************************/
void reveal_hidden_units(struct player *pplayer, int x, int y)
{
  adjc_iterate(x, y, x1, y1) {
    unit_list_iterate(map_get_tile(x1, y1)->units, punit) {
      if (is_hiding_unit(punit)) {
        /* send_unit_info will check whether it is visible */
        send_unit_info(pplayer, punit);
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
static void really_unfog_area(struct player *pplayer, int x, int y)
{
  struct city *pcity;
  bool old_known = map_is_known(x, y, pplayer);

  freelog(LOG_DEBUG, "really unfogging %d,%d\n", x, y);

  map_set_known(x, y, pplayer);

  /* discover units */
  unit_list_iterate(map_get_tile(x, y)->units, punit)
    send_unit_info(pplayer, punit);
  unit_list_iterate_end;

  /* discover cities */ 
  reality_check_city(pplayer, x, y);
  if ((pcity=map_get_city(x, y)))
    send_city_info(pplayer, pcity);

  /* send info about the tile itself */
  send_tile_info_always(pplayer, &pplayer->connections, x, y);

  /* If the tile was not known before we need to refresh the cities that
     can use the tile. */
  if (!old_known) {
    map_city_radius_iterate(x, y, x1, y1) {
      pcity = map_get_city(x1, y1);
      if (pcity && city_owner(pcity) == pplayer) {
	update_city_tile_status_map(pcity, x, y);
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
void unfog_area(struct player *pplayer, int x, int y, int len)
{
  /* Did the tile just become visible?
     - send info about units and cities and the tile itself */
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    shared_vision_change_seen(abs_x, abs_y, pplayer, +1);
    if (map_get_seen(abs_x, abs_y, pplayer) == 1
	|| !map_is_known(abs_x, abs_y, pplayer)) {
      really_unfog_area(pplayer, abs_x, abs_y);
    }

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      if (!really_gives_vision(pplayer, pplayer2)) {
	continue;
      }

      if (map_get_seen(abs_x, abs_y, pplayer2) == 1
	  || !map_is_known(abs_x, abs_y, pplayer2)) {
	really_unfog_area(pplayer2, abs_x, abs_y);
      }
      reveal_pending_seen(pplayer2, abs_x, abs_y, 0);
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, x, y, len);
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
...
**************************************************************************/
static void really_fog_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.",
	  x, y, map_get_seen(x, y, pplayer));
 
  assert(map_get_seen(x, y, pplayer) == 0);

  unit_list_iterate(map_get_tile(x, y)->units, punit)
    unit_goes_out_of_sight(pplayer,punit);
  unit_list_iterate_end;  

  update_player_tile_last_seen(pplayer, x, y);
  send_tile_info_always(pplayer, &pplayer->connections, x, y);
}

/**************************************************************************
  Remove a point of visibility from a square centered at x,y with
  sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
**************************************************************************/
void fog_area(struct player *pplayer, int x, int y, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    if (map_is_known(abs_x, abs_y, pplayer)) {
      /* the player himself */
      shared_vision_change_seen(abs_x, abs_y, pplayer, -1);
      if (map_get_seen(abs_x, abs_y, pplayer) == 0) {
	really_fog_area(pplayer, abs_x, abs_y);
      }

      /* players (s)he gives shared vision */
      players_iterate(pplayer2) {
	if (!really_gives_vision(pplayer, pplayer2)) {
	  continue;
	}
	if (map_get_seen(abs_x, abs_y, pplayer2) == 0) {
	  really_fog_area(pplayer2, abs_x, abs_y);
	}
      } players_iterate_end;
    } else {
      decrement_pending_seen(pplayer, abs_x, abs_y);
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
...
**************************************************************************/
void map_fog_city_area(struct city *pcity)
{
  if (!pcity) {
    freelog(LOG_ERROR, "Attempting to fog non-existent city");
    return;
  }

  map_fog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
void map_unfog_city_area(struct city *pcity)
{
  if (!pcity) {
    freelog(LOG_ERROR, "Attempting to unfog non-existent city");
    return;
  }

  map_unfog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
static void shared_vision_change_seen(int x, int y, struct player *pplayer, int change)
{
  map_change_seen(x, y, pplayer, change);
  map_change_own_seen(x, y, pplayer, change);

  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2))
      map_change_seen(x, y, pplayer2, change);
  } players_iterate_end;
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_unfog_pseudo_city_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Unfogging city area at %i,%i", x, y);

  buffer_shared_vision(pplayer);
  map_city_radius_iterate(x, y, x_itr, y_itr) {
    if (map_is_known(x_itr, y_itr, pplayer)) {
      unfog_area(pplayer, x_itr, y_itr, 0);
    } else {
      increment_pending_seen(pplayer, x_itr, y_itr);
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
There doesn't have to be a city.
**************************************************************************/
void map_fog_pseudo_city_area(struct player *pplayer, int x, int y)
{
  freelog(LOG_DEBUG, "Fogging city area at %i,%i", x, y);

  buffer_shared_vision(pplayer);
  map_city_radius_iterate(x, y, x_itr, y_itr) {
    if (map_is_known(x_itr, y_itr, pplayer)) {
      fog_area(pplayer, x_itr, y_itr, 0);
    } else {
      decrement_pending_seen(pplayer, x_itr, y_itr);
    }
  } map_city_radius_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
For removing a unit. The actual removal is done in server_remove_unit
**************************************************************************/
void remove_unit_sight_points(struct unit *punit)
{
  int x = punit->x, y = punit->y;
  struct player *pplayer = unit_owner(punit);

  freelog(LOG_DEBUG, "Removing unit sight points at  %i,%i", punit->x,
	  punit->y);

  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    fog_area(pplayer, x, y, get_watchtower_vision(punit));
  else
    fog_area(pplayer, x, y, unit_type(punit)->vision_range);
}

/**************************************************************************
Shows area even if still fogged. If the tile is not "seen" units are not
shown
**************************************************************************/
static void really_show_area(struct player *pplayer, int x, int y)
{
  struct city *pcity;
  bool old_known = map_is_known(x, y, pplayer);

  freelog(LOG_DEBUG, "Showing %i,%i", x, y);

  if (!map_is_known_and_seen(x, y, pplayer)) {
    map_set_known(x, y, pplayer);

    /* as the tile may be fogged send_tile_info won't always do this for us */
    update_tile_knowledge(pplayer, x, y);
    update_player_tile_last_seen(pplayer, x, y);

    send_tile_info_always(pplayer, &pplayer->connections, x, y);

    /* remove old cities that exist no more */
    reality_check_city(pplayer, x, y);
    if ((pcity = map_get_city(x, y))) {
      /* as the tile may be fogged send_city_info won't do this for us */
      update_dumb_city(pplayer, pcity);
      send_city_info(pplayer, pcity);
    }

    if (map_get_seen(x, y, pplayer) != 0) {
      unit_list_iterate(map_get_tile(x, y)->units, punit)
	send_unit_info(pplayer, punit);
      unit_list_iterate_end;
    }

    /* If the tile was not known before we need to refresh the cities that
       can use the tile. */
    if (!old_known) {
      map_city_radius_iterate(x, y, x1, y1) {
	pcity = map_get_city(x1, y1);
	if (pcity && city_owner(pcity) == pplayer) {
	  update_city_tile_status_map(pcity, x, y);
	}
      } map_city_radius_iterate_end;
      sync_cities();
    }
  }
}

/**************************************************************************
Shows area, ie send terrain etc., even if still fogged, sans units and cities.
**************************************************************************/
void show_area(struct player *pplayer, int x, int y, int len)
{
  buffer_shared_vision(pplayer);
  square_iterate(x, y, len, abs_x, abs_y) {
    /* the player himself */
    really_show_area(pplayer, abs_x, abs_y);

    /* players (s)he gives shared vision */
    players_iterate(pplayer2) {
      if (really_gives_vision(pplayer, pplayer2)) {
	really_show_area(pplayer2, abs_x, abs_y);
	reveal_pending_seen(pplayer2, abs_x, abs_y, 0);
      }
    } players_iterate_end;
  } square_iterate_end;

  reveal_pending_seen(pplayer, x, y, len);
  unbuffer_shared_vision(pplayer);
}

/***************************************************************
...
***************************************************************/
bool map_is_known(int x, int y, struct player *pplayer)
{
  return TEST_BIT(map_get_tile(x, y)->known, pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
bool map_is_known_and_seen(int x, int y, struct player *pplayer)
{
  int offset = map_pos_to_index(x, y);

  return TEST_BIT((map.tiles + offset)->known, pplayer->player_no)
      && ((pplayer->private_map + offset)->seen != 0);
}

/***************************************************************
Watch out - this can be true even if the tile is not known.
***************************************************************/
static int map_get_seen(int x, int y, struct player *pplayer)
{
  return map_get_player_tile(x, y, pplayer)->seen;
}

/***************************************************************
...
***************************************************************/
void map_change_seen(int x, int y, struct player *pplayer, int change)
{
  map_get_player_tile(x, y, pplayer)->seen += change;
  freelog(LOG_DEBUG, "%d,%d, p: %d, change %d, result %d\n", x, y,
	  pplayer->player_no, change, map_get_player_tile(x, y,
							 pplayer)->seen);
}

/***************************************************************
...
***************************************************************/
static int map_get_own_seen(int x, int y, struct player *pplayer)
{
  int own_seen = map_get_player_tile(x, y, pplayer)->own_seen;
  if (own_seen != 0) {
    assert(map_is_known(x, y, pplayer));
  }
  return own_seen;
}

/***************************************************************
...
***************************************************************/
static void map_change_own_seen(int x, int y, struct player *pplayer,
				int change)
{
  map_get_player_tile(x, y, pplayer)->own_seen += change;
}

/***************************************************************
...
***************************************************************/
void map_set_known(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->known |= (1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_clear_known(int x, int y, struct player *pplayer)
{
  map_get_tile(x, y)->known &= ~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_know_all(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    show_area(pplayer, x, y, 0);
  } whole_map_iterate_end;
}

/***************************************************************
...
***************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    unfog_area(pplayer, x, y, 0);
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
  pplayer->private_map =
    fc_malloc(map.xsize*map.ysize*sizeof(struct player_tile));
  whole_map_iterate(x, y) {
    player_tile_init(x, y, pplayer);
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

  whole_map_iterate(x, y) {
    struct player_tile *plrtile = map_get_player_tile(x, y, pplayer);

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
static void player_tile_init(int x, int y, struct player *pplayer)
{
  struct player_tile *plrtile =
    map_get_player_tile(x, y, pplayer);

  plrtile->terrain = T_UNKNOWN;
  plrtile->special = S_NO_SPECIAL;
  plrtile->city = NULL;

  plrtile->seen = 0;
  plrtile->pending_seen = 0;
  if (!game.fogofwar_old) {
    if (map_is_known(x, y, pplayer)) {
      plrtile->seen = 1;
    } else {
      plrtile->pending_seen = 1;
    }
  }

  plrtile->last_updated = GAME_START_YEAR;
  plrtile->own_seen = plrtile->seen;
}
 
/***************************************************************
...
***************************************************************/
struct player_tile *map_get_player_tile(int x, int y, struct player *pplayer)
{
  return pplayer->private_map + map_pos_to_index(x, y);
}

/***************************************************************
...
***************************************************************/
void update_tile_knowledge(struct player *pplayer, int x, int y)
{
  struct tile *ptile = map_get_tile(x, y);
  struct player_tile *plrtile = map_get_player_tile(x, y, pplayer);

  plrtile->terrain = ptile->terrain;
  plrtile->special = ptile->special;
}

/***************************************************************
...
***************************************************************/
void update_player_tile_last_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(x, y, pplayer)->last_updated = game.year;
}

/***************************************************************
...
***************************************************************/
static void really_give_tile_info_from_player_to_player(struct player *pfrom,
							struct player *pdest,
							int x, int y)
{
  struct player_tile *from_tile, *dest_tile;
  if (!map_is_known_and_seen(x, y, pdest)) {
    /* I can just hear people scream as they try to comprehend this if :).
     * Let me try in words:
     * 1) if the tile is seen by pdest the info is sent to pfrom
     *  OR
     * 2) if the tile is known by pfrom AND (he has more recent info
     *     OR it is not known by pdest)
     */
    if (map_is_known_and_seen(x, y, pfrom)
	|| (map_is_known(x,y,pfrom)
	    && (((map_get_player_tile(x, y, pfrom)->last_updated
		 > map_get_player_tile(x, y, pdest)->last_updated))
	        || !map_is_known(x, y, pdest)))) {
      from_tile = map_get_player_tile(x, y, pfrom);
      dest_tile = map_get_player_tile(x, y, pdest);
      /* Update and send tile knowledge */
      map_set_known(x, y, pdest);
      dest_tile->terrain = from_tile->terrain;
      dest_tile->special = from_tile->special;
      dest_tile->last_updated = from_tile->last_updated;
      send_tile_info_always(pdest, &pdest->connections, x, y);
	
      /* update and send city knowledge */
      /* remove outdated cities */
      if (dest_tile->city) {
	if (!from_tile->city) {
	  /* As the city was gone on the newer from_tile
	     it will be removed by this function */
	  reality_check_city(pdest, x, y);
	} else /* We have a dest_city. update */
	  if (from_tile->city->id != dest_tile->city->id)
	    /* As the city was gone on the newer from_tile
	       it will be removed by this function */
	    reality_check_city(pdest, x, y);
      }
      /* Set and send new city info */
      if (from_tile->city) {
	if (!dest_tile->city) {
	  dest_tile->city = fc_malloc(sizeof(struct dumb_city));
	}
	*dest_tile->city = *from_tile->city;
	send_city_info_at_tile(pdest, &pdest->connections, NULL, x, y);
      }

      reveal_pending_seen(pdest, x, y, 0);

      map_city_radius_iterate(x, y, x1, y1) {
	struct city *pcity = map_get_city(x1, y1);
	if (pcity && city_owner(pcity) == pdest) {
	  update_city_tile_status_map(pcity, x, y);
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
						 int x, int y)
{
  really_give_tile_info_from_player_to_player(pfrom, pdest, x, y);

  players_iterate(pplayer2) {
    if (!really_gives_vision(pdest, pplayer2))
      continue;
    really_give_tile_info_from_player_to_player(pfrom, pplayer2, x, y);
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
	whole_map_iterate(x, y) {
	  int change = map_get_own_seen(x, y, pplayer);
	  if (change != 0) {
	    map_change_seen(x, y, pplayer2, change);
	    if (map_get_seen(x, y, pplayer2) == change) {
	      really_unfog_area(pplayer2, x, y);
	      reveal_pending_seen(pplayer2, x, y, 0);
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
	whole_map_iterate(x, y) {
	  int change = map_get_own_seen(x, y, pplayer);
	  if (change > 0) {
	    map_change_seen(x, y, pplayer2, -change);
	    if (map_get_seen(x, y, pplayer2) == 0)
	      really_fog_area(pplayer2, x, y);
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
enum known_type map_get_known(int x, int y, struct player *pplayer)
{
  if (map_is_known(x, y, pplayer)) {
    if (map_get_seen(x, y, pplayer) > 0) {
      return TILE_KNOWN;
    } else {
      return TILE_KNOWN_FOGGED;
    }
  } else {
    return TILE_UNKNOWN;
  }
}

/*************************************************************************
...
*************************************************************************/
static void enable_fog_of_war_player(struct player *pplayer)
{
  whole_map_iterate(x, y) {
    if (map_is_known(x, y, pplayer)) {
      fog_area(pplayer, x, y, 0);
    } else {
      decrement_pending_seen(pplayer, x, y);
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
  whole_map_iterate(x, y) {
    if (map_is_known(x, y, pplayer)) {
      unfog_area(pplayer, x, y, 0);
    } else {
      increment_pending_seen(pplayer, x, y);
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
static void ocean_to_land_fix_rivers(int x, int y)
{
  /* clear the river if it exists */
  map_clear_special(x, y, S_RIVER);

  cardinal_adjc_iterate(x, y, x1, y1) {
    if (map_has_special(x1, y1, S_RIVER)) {
      bool ocean_near = FALSE;
      cardinal_adjc_iterate(x1, y1, x2, y2) {
        if (is_ocean(map_get_terrain(x2, y2)))
          ocean_near = TRUE;
      } cardinal_adjc_iterate_end;
      if (!ocean_near) {
        map_set_special(x, y, S_RIVER);
        return;
      }
    }
  } cardinal_adjc_iterate_end;
}

/**************************************************************************
  Checks for terrain change between ocean and land.  Handles side-effects.
  (Should be called after any potential ocean/land terrain changes.)
  Also, returns an enum ocean_land_change, describing the change, if any.

  if we did a land change, we try to avoid reassigning
  continent numbers.
**************************************************************************/
enum ocean_land_change check_terrain_ocean_land_change(int x, int y,
                                                enum tile_terrain_type oldter)
{
  enum tile_terrain_type newter = map_get_terrain(x, y);

  if (is_ocean(oldter) && !is_ocean(newter)) {
    /* ocean to land ... */
    ocean_to_land_fix_rivers(x, y);
    city_landlocked_sell_coastal_improvements(x, y);

    assign_continent_numbers();
    allot_island_improvs();
    send_all_known_tiles(NULL);
    
    map_update_borders_landmass_change(x, y);

    gamelog(GAMELOG_MAP, _("(%d,%d) land created from ocean"), x, y);
    return OLC_OCEAN_TO_LAND;
  } else if (!is_ocean(oldter) && is_ocean(newter)) {
    /* land to ocean ... */

    assign_continent_numbers();
    allot_island_improvs();
    send_all_known_tiles(NULL);

    map_update_borders_landmass_change(x, y);

    gamelog(GAMELOG_MAP, _("(%d,%d) ocean created from land"), x, y);
    return OLC_LAND_TO_OCEAN;
  }
  return OLC_NONE;
}

/*************************************************************************
  Return pointer to the oldest adjacent city to this tile.  If
  there is a city on the exact tile, that is returned instead.
*************************************************************************/
static struct city *map_get_adjc_city(int x, int y)
{
  struct city *closest = NULL;   /* Closest city */
  struct tile *ptile;

  ptile = map_get_tile(x, y);
  if (ptile->city) {
    return ptile->city;
  }

  adjc_iterate(x, y, xp, yp) {
    ptile = map_get_tile(xp, yp);
    if (ptile->city && 
         (!closest || ptile->city->turn_founded < closest->turn_founded)) {
      closest = ptile->city;
    }
  } adjc_iterate_end;

  return closest;
}

/*************************************************************************
  Returns TRUE if the given ocean tile is surrounded by at least 5 land
  tiles of the same continent (N.B. will probably need modifications to
  deal with topologies in which tiles do not have 8 neighbours). If this
  is the case, the continent number of the land tiles is returned in *contp.
  If multiple continents border the tile, FALSE is always returned.
  This enables small seas (usually long inlets or inland lakes) to be
  claimed by nations, rather than remaining as international waters. This
  should in the long run perhaps be replaced with more general detection
  of inland seas.
*************************************************************************/
static bool is_claimed_ocean(int x, int y, Continent_id *contp)
{
  Continent_id cont = 0, numland = 0;

  adjc_iterate(x, y, xp, yp) {
    if (!is_ocean(map_get_terrain(xp, yp))) {
      Continent_id thiscont = map_get_continent(xp, yp);

      if (cont == 0) {
	cont = thiscont;
      }
      if (cont == thiscont) {
	numland++;
      } else {
	return FALSE;
      }
    }
  } adjc_iterate_end;

  *contp = cont;
  return (numland >= 5);
}

/*************************************************************************
  Return pointer to the closest city to this tile, which must be
  on the same continent if the city is not immediately adjacent.
  If two or more cities are equally distant, then return the
  oldest (i.e. the one with the lowest id). This also correctly
  works for water bases in SMAC mode, and allows coastal cities
  to claim one square of ocean. Inland lakes and long inlets act in
  the same way as the surrounding continent's land tiles. If no cities
  are within game.borders distance, returns NULL.

  NOTE: The behaviour of this function will eventually depend
  upon some planned ruleset options.
*************************************************************************/
static struct city *map_get_closest_city(int x, int y)
{
  struct city *closest;  /* Closest city */

  closest = map_get_adjc_city(x, y);
  if (!closest) {
    int distsq;		/* Squared distance to city */
    /* integer arithmetic equivalent of (borders+0.5)**2 */
    int cldistsq = game.borders * (game.borders + 1);
    Continent_id cont = map_get_continent(x, y);

    if (!is_ocean(map_get_terrain(x, y)) || is_claimed_ocean(x, y, &cont)) {
      cities_iterate(pcity) {
	if (map_get_continent(pcity->x, pcity->y) == cont) {
          distsq = sq_map_distance(pcity->x, pcity->y, x, y);
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
static void tile_update_owner(int x, int y)
{
  /* This implementation is horribly inefficient, but this doesn't cause
   * problems since it's not called often. */
  cities_iterate(pcity) {
    update_city_tile_status_map(pcity, x, y);
  } cities_iterate_end;
}

/*************************************************************************
  Recalculate the borders around a given position.
*************************************************************************/
static void map_update_borders_recalculate_position(int x, int y)
{
  if (game.borders > 0) {
    iterate_outward(x, y, game.borders, xp, yp) {
      struct city *pccity = map_get_closest_city(xp, yp);
      struct player *new_owner = pccity ? get_player(pccity->owner) : NULL;

      if (new_owner != map_get_owner(xp, yp)) {
	map_set_owner(xp, yp, new_owner);
	send_tile_info(NULL, xp, yp);
	tile_update_owner(xp, yp);
      }
    } iterate_outward_end;
  }
}

/*************************************************************************
  Modify national territories as resulting from a city being destroyed.
  x,y coords for (already deleted) city's location.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_city_destroyed(int x, int y)
{
  map_update_borders_recalculate_position(x, y);
}

/*************************************************************************
  Modify national territories resulting from a change of landmass.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_landmass_change(int x, int y)
{
  map_update_borders_recalculate_position(x, y);
}

/*************************************************************************
  Modify national territories resulting from new city or change of city
  ownership.
  Tile worker states are updated as necessary, but not sync'd with client.
*************************************************************************/
void map_update_borders_city_change(struct city *pcity)
{
  map_update_borders_recalculate_position(pcity->x, pcity->y);
}

/*************************************************************************
  Delete the territorial claims to all tiles.
*************************************************************************/
static void map_clear_borders(void)
{
  whole_map_iterate(x, y) {
    map_set_owner(x, y, NULL);
  } whole_map_iterate_end;
}

/*************************************************************************
  Minimal code that calculates all national territories from scratch.
*************************************************************************/
static void map_calculate_territory(void)
{
  /* Clear any old territorial claims. */
  map_clear_borders();

  if (game.borders > 0) {
    /* Loop over all cities and claim territory. */
    cities_iterate(pcity) {
      /* Loop over all map tiles within this city's sphere of influence. */
      iterate_outward(pcity->x, pcity->y, game.borders, x, y) {
	struct city *pccity = map_get_closest_city(x, y);

	if (pccity) {
	  map_set_owner(x, y, get_player(pccity->owner));
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
  if (game.borders > 0) {
    map_calculate_territory();

    /* Fix tile worker states. */
    cities_iterate(pcity) {
      map_city_radius_iterate(pcity->x, pcity->y, map_x, map_y) {
        update_city_tile_status_map(pcity, map_x, map_y);
      } map_city_radius_iterate_end;
    } cities_iterate_end;
  }
}
