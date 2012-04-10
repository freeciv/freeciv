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

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"

/* common */
#include "base.h"
#include "borders.h"
#include "events.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "unit.h"
#include "unitlist.h"
#include "vision.h"

/* generator */
#include "utilities.h"

/* server */
#include "citytools.h"
#include "cityturn.h"
#include "notify.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "sernet.h"
#include "srv_main.h"
#include "unithand.h"
#include "unittools.h"

#include "maphand.h"

#define MAXIMUM_CLAIMED_OCEAN_SIZE (20)

/* Suppress send_tile_info() during game_load() */
static bool send_tile_suppressed = FALSE;

static void player_tile_init(struct tile *ptile, struct player *pplayer);
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest,
						 struct tile *ptile);
static void shared_vision_change_seen(struct player *pplayer,
                                      struct tile *ptile,
                                      const v_radius_t change,
                                      bool can_reveal_tiles);
static void map_change_seen(struct player *pplayer,
                            struct tile *ptile,
                            const v_radius_t change,
                            bool can_reveal_tiles);
static void map_change_own_seen(struct player *pplayer,
                                struct tile *ptile,
                                const v_radius_t change);
static inline int map_get_seen(const struct player *pplayer,
                               const struct tile *ptile,
                               enum vision_layer vlayer);
static inline int map_get_own_seen(const struct player *pplayer,
                                   const struct tile *ptile,
                                   enum vision_layer vlayer);
static void climate_change(bool warming, int effect);

/**************************************************************************
Used only in global_warming() and nuclear_winter() below.
**************************************************************************/
static bool is_terrain_ecologically_wet(struct tile *ptile)
{
  return (is_ocean_near_tile(ptile)
	  || is_special_near_tile(ptile, S_RIVER, TRUE));
}

/**************************************************************************
  Wrapper for climate_change().
**************************************************************************/
void global_warming(int effect)
{
  climate_change(TRUE, effect);
}

/**************************************************************************
  Wrapper for climate_change().
**************************************************************************/
void nuclear_winter(int effect)
{
  climate_change(FALSE, effect);
}

/*****************************************************************************
  Do a climate change. Global warming occured if 'warming' is TRUE else there is
  a nuclear winter.
*****************************************************************************/
static void climate_change(bool warming, int effect)
{
  int k = map_num_tiles();
  bool used[k];
  memset(used, 0, sizeof(used));

  log_verbose("Climate change: %s (%d)",
              warming ? "Global warming" : "Nuclear winter",
              warming ? game.info.heating : game.info.cooling);

  while (effect > 0 && (k--) > 0) {
    struct terrain *old, *candidates[2], *new;
    struct tile *ptile;
    int i;

    do {
      /* We want to transform a tile at most once due to a climate change. */
      ptile = rand_map_pos();
    } while (used[tile_index(ptile)]);
    used[tile_index(ptile)] = TRUE;

    old = tile_terrain(ptile);
    /* Prefer the transformation that's appropriate to the ambient moisture,
     * but be prepared to fall back in exceptional circumstances */
    {
      struct terrain *wetter, *drier;
      wetter = warming ? old->warmer_wetter_result : old->cooler_wetter_result;
      drier  = warming ? old->warmer_drier_result  : old->cooler_drier_result;
      if (is_terrain_ecologically_wet(ptile)) {
        candidates[0] = wetter;
        candidates[1] = drier;
      } else {
        candidates[0] = drier;
        candidates[1] = wetter;
      }
    }

    /* If the preferred transformation is ruled out for some exceptional reason
     * specific to this tile, fall back to the other, rather than letting this
     * tile be immune to change. */
    for (i=0; i<2; i++) {
      new = candidates[i];

      /* If the preferred transformation simply hasn't been specified
       * for this terrain at all, don't fall back to the other. */
      if (new == T_NONE) {
        break;
      }

      if (tile_city(ptile) != NULL && terrain_has_flag(new, TER_NO_CITIES)) {
        /* do not change to a terrain with the flag TER_NO_CITIES if the tile
         * has a city */
        continue;
      }

      /* Only change between water and land at coastlines */
      if (!is_ocean(old) && is_ocean(new) && !can_channel_land(ptile)) {
        continue;
      } else if (is_ocean(old) && !is_ocean(new) && !can_reclaim_ocean(ptile)) {
        continue;
      }
      
      /* OK! */
      break;
    }
    if (i == 2) {
      /* Neither transformation was permitted. Give up. */
      continue;
    }

    if (new != T_NONE && old != new) {
      effect--;

      /* Really change the terrain. */
      tile_change_terrain(ptile, new);
      check_terrain_change(ptile, old);
      update_tile_knowledge(ptile);

      /* Check the unit activities. */
      unit_list_iterate(ptile->units, punit) {
        if (!can_unit_continue_current_activity(punit)) {
          unit_activity_handling(punit, ACTIVITY_IDLE);
        }
      } unit_list_iterate_end;
    } else if (old == new) {
      /* This counts toward a climate change although nothing is changed. */
      effect--;
    }
  }

  if (warming) {
    notify_player(NULL, NULL, E_GLOBAL_ECO, ftc_server,
                  _("Global warming has occurred!"));
    notify_player(NULL, NULL, E_GLOBAL_ECO, ftc_server,
                  _("Coastlines have been flooded and vast "
                    "ranges of grassland have become deserts."));
  } else {
    notify_player(NULL, NULL, E_GLOBAL_ECO, ftc_server,
                  _("Nuclear winter has occurred!"));
    notify_player(NULL, NULL, E_GLOBAL_ECO, ftc_server,
                  _("Wetlands have dried up and vast "
                    "ranges of grassland have become tundra."));
  }
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
    notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
                  _("New hope sweeps like fire through the country as "
                    "the discovery of railroad is announced.\n"
                    "      Workers spontaneously gather and upgrade all "
                    "cities with railroads."));
  } else {
    notify_player(pplayer, NULL, E_TECH_GAIN, ftc_server,
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
bool really_gives_vision(struct player *me, struct player *them)
{
  return BV_ISSET(me->server.really_gives_vision, player_index(them));
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
  city_thaw_workers_queue();
  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  buffer_shared_vision(pdest);

  whole_map_iterate(ptile) {
    if (is_ocean_tile(ptile)) {
      give_tile_info_from_player_to_player(pfrom, pdest, ptile);
    }
  } whole_map_iterate_end;

  unbuffer_shared_vision(pdest);
  city_thaw_workers_queue();
  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void give_citymap_from_player_to_player(struct city *pcity,
					struct player *pfrom, struct player *pdest)
{
  struct tile *pcenter = city_tile(pcity);

  buffer_shared_vision(pdest);

  city_tile_iterate(city_map_radius_sq_get(pcity), pcenter, ptile) {
    give_tile_info_from_player_to_player(pfrom, pdest, ptile);
  } city_tile_iterate_end;

  unbuffer_shared_vision(pdest);
  city_thaw_workers_queue();
  sync_cities();
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

    send_tile_info(dest, ptile, FALSE);
  } whole_map_iterate_end;

  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**************************************************************************
  Suppress send_tile_info() during game_load()
**************************************************************************/
bool send_tile_suppression(bool now)
{
  bool formerly = send_tile_suppressed;

  send_tile_suppressed = now;
  return formerly;
}

/**************************************************************************
  Send tile information to all the clients in dest which know and see
  the tile. If dest is NULL, sends to all clients (game.est_connections)
  which know and see tile.

  Note that this function does not update the playermap.  For that call
  update_tile_knowledge().
**************************************************************************/
void send_tile_info(struct conn_list *dest, struct tile *ptile,
                    bool send_unknown)
{
  struct packet_tile_info info;
  const struct player *owner;

  if (send_tile_suppressed) {
    return;
  }

  if (!dest) {
    dest = game.est_connections;
  }

  info.tile = tile_index(ptile);

  if (ptile->spec_sprite) {
    sz_strlcpy(info.spec_sprite, ptile->spec_sprite);
  } else {
    info.spec_sprite[0] = '\0';
  }

  info.special[S_OLD_FORTRESS] = FALSE;
  info.special[S_OLD_AIRBASE] = FALSE;

  conn_list_iterate(dest, pconn) {
    struct player *pplayer = pconn->playing;

    if (NULL == pplayer && !pconn->observer) {
      continue;
    }

    if (!pplayer || map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
      info.known = TILE_KNOWN_SEEN;
      info.continent = tile_continent(ptile);
      owner = tile_owner(ptile);
      info.owner = (owner ? player_number(owner) : MAP_TILE_OWNER_NULL);
      info.worked = (NULL != tile_worked(ptile))
                     ? tile_worked(ptile)->id
                     : IDENTITY_NUMBER_ZERO;

      info.terrain = (NULL != tile_terrain(ptile))
                      ? terrain_number(tile_terrain(ptile))
                      : terrain_count();
      info.resource = (NULL != tile_resource(ptile))
                       ? resource_number(tile_resource(ptile))
                       : resource_count();

      tile_special_type_iterate(spe) {
	info.special[spe] = BV_ISSET(ptile->special, spe);
      } tile_special_type_iterate_end;
      info.bases = ptile->bases;

      send_packet_tile_info(pconn, &info);
    } else if (pplayer && map_is_known(ptile, pplayer)) {
      struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);
      struct vision_site *psite = map_get_player_site(ptile, pplayer);

      info.known = TILE_KNOWN_UNSEEN;
      info.continent = tile_continent(ptile);
      owner = (game.server.foggedborders
               ? plrtile->owner
               : tile_owner(ptile));
      info.owner = (owner ? player_number(owner) : MAP_TILE_OWNER_NULL);
      info.worked = (NULL != psite)
                    ? psite->identity
                    : IDENTITY_NUMBER_ZERO;

      info.terrain = (NULL != plrtile->terrain)
                      ? terrain_number(plrtile->terrain)
                      : terrain_count();
      info.resource = (NULL != plrtile->resource)
                       ? resource_number(plrtile->resource)
                       : resource_count();

      tile_special_type_iterate(spe) {
	info.special[spe] = BV_ISSET(plrtile->special, spe);
      } tile_special_type_iterate_end;
      info.bases = plrtile->bases;

      send_packet_tile_info(pconn, &info);
    } else if (send_unknown) {
      info.known = TILE_UNKNOWN;
      info.continent = 0;
      info.owner = MAP_TILE_OWNER_NULL;
      info.worked = IDENTITY_NUMBER_ZERO;

      info.terrain = terrain_count();
      info.resource = resource_count();

      tile_special_type_iterate(spe) {
        info.special[spe] = FALSE;
      } tile_special_type_iterate_end;
      BV_CLR_ALL(info.bases);

      send_packet_tile_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/****************************************************************************
  Assumption: Each unit type is visible on only one layer.
****************************************************************************/
static bool unit_is_visible_on_layer(const struct unit *punit,
				     enum vision_layer vlayer)
{
  return XOR(vlayer == V_MAIN, is_hiding_unit(punit));
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

/****************************************************************************
  Change the seen count of a tile for a pplayer. It will automatically
  handle the shared visions.
****************************************************************************/
static void shared_vision_change_seen(struct player *pplayer,
                                      struct tile *ptile,
                                      const v_radius_t change,
                                      bool can_reveal_tiles)
{
  map_change_own_seen(pplayer, ptile, change);
  map_change_seen(pplayer, ptile, change, can_reveal_tiles);

  players_iterate(pplayer2) {
    if (really_gives_vision(pplayer, pplayer2)) {
      map_change_seen(pplayer2, ptile, change, can_reveal_tiles);
    }
  } players_iterate_end;
}

/**************************************************************************
  There doesn't have to be a city.
**************************************************************************/
void map_vision_update(struct player *pplayer, struct tile *ptile,
                       const v_radius_t old_radius_sq,
                       const v_radius_t new_radius_sq,
                       bool can_reveal_tiles)
{
  v_radius_t change;
  int max_radius;

  if (old_radius_sq[V_MAIN] == new_radius_sq[V_MAIN]
      && old_radius_sq[V_INVIS] == new_radius_sq[V_INVIS]) {
    return;
  }

  /* Determines 'max_radius' value. */
  max_radius = 0;
  vision_layer_iterate(v) {
    if (max_radius < old_radius_sq[v]) {
      max_radius = old_radius_sq[v];
    }
    if (max_radius < new_radius_sq[v]) {
      max_radius = new_radius_sq[v];
    }
  } vision_layer_iterate_end;

#ifdef DEBUG
  log_debug("Updating vision at (%d, %d) in a radius of %d.",
            TILE_XY(ptile), max_radius);
  vision_layer_iterate(v) {
    log_debug("  vision layer %d is changing from %d to %d.",
              v, old_radius_sq[v], new_radius_sq[v]);
  } vision_layer_iterate_end;
#endif /* DEBUG */

  buffer_shared_vision(pplayer);
  circle_dxyr_iterate(ptile, max_radius, tile1, dx, dy, dr) {
    vision_layer_iterate(v) {
      if (dr > old_radius_sq[v] && dr <= new_radius_sq[v]) {
        change[v] = 1;
      } else if (dr > new_radius_sq[v] && dr <= old_radius_sq[v]) {
        change[v] = -1;
      } else {
        change[v] = 0;
      }
    } vision_layer_iterate_end;
    shared_vision_change_seen(pplayer, tile1, change, can_reveal_tiles);
  } circle_dxyr_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/****************************************************************************
  Perform an actions on all units on 'ptile' seen by 'pplayer'.
****************************************************************************/
static inline void vision_update_units(struct player *pplayer,
                                       struct tile *ptile,
                                       void (*func) (struct player *,
                                                     struct unit *))
{
  /* Iterate vision layers. */
  vision_layer_iterate(v) {
    if (0 < map_get_seen(pplayer, ptile, v)) {
      unit_list_iterate(ptile->units, punit) {
        if (unit_is_visible_on_layer(punit, v)) {
          func(pplayer, punit);
        }
      } unit_list_iterate_end;
    }
  } vision_layer_iterate_end;
}

/****************************************************************************
  Shows the area to the player.  Unless the tile is "seen", it will remain
  fogged and units will be hidden.

  Callers may wish to buffer_shared_vision before calling this function.
****************************************************************************/
void map_show_tile(struct player *src_player, struct tile *ptile)
{
  static int recurse = 0;

  log_debug("Showing %i,%i to %s", TILE_XY(ptile), player_name(src_player));

  fc_assert(recurse == 0);
  recurse++;

  players_iterate(pplayer) {
    if (pplayer == src_player || really_gives_vision(src_player, pplayer)) {
      struct city *pcity;

      if (!map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
	map_set_known(ptile, pplayer);

	/* as the tile may be fogged send_tile_info won't always do this for us */
	update_player_tile_knowledge(pplayer, ptile);
	update_player_tile_last_seen(pplayer, ptile);

        send_tile_info(pplayer->connections, ptile, FALSE);

	/* remove old cities that exist no more */
	reality_check_city(pplayer, ptile);

	if ((pcity = tile_city(ptile))) {
	  /* as the tile may be fogged send_city_info won't do this for us */
	  update_dumb_city(pplayer, pcity);
	  send_city_info(pplayer, pcity);
	}

        vision_update_units(pplayer, ptile, send_unit_info);
      }
    }
  } players_iterate_end;

  recurse--;
}

/****************************************************************************
  Hides the area to the player.

  Callers may wish to buffer_shared_vision before calling this function.
****************************************************************************/
void map_hide_tile(struct player *src_player, struct tile *ptile)
{
  static int recurse = 0;

  log_debug("Hiding %d,%d to %s", TILE_XY(ptile), player_name(src_player));

  fc_assert(recurse == 0);
  recurse++;

  players_iterate(pplayer) {
    if (pplayer == src_player || really_gives_vision(src_player, pplayer)) {
      if (map_is_known(ptile, pplayer)) {
        if (0 < map_get_seen(pplayer, ptile, V_MAIN)) {
          update_player_tile_last_seen(pplayer, ptile);
        }

        /* Remove city. */
        remove_dumb_city(pplayer, ptile);

        /* Remove units. */
        vision_update_units(pplayer, ptile, unit_goes_out_of_sight);
      }

      map_clear_known(ptile, pplayer);

      send_tile_info(pplayer->connections, ptile, TRUE);
    }
  } players_iterate_end;

  recurse--;
}

/****************************************************************************
  Shows the area to the player.  Unless the tile is "seen", it will remain
  fogged and units will be hidden.
****************************************************************************/
void map_show_circle(struct player *pplayer, struct tile *ptile, int radius_sq)
{
  buffer_shared_vision(pplayer);

  circle_iterate(ptile, radius_sq, tile1) {
    map_show_tile(pplayer, tile1);
  } circle_iterate_end;

  unbuffer_shared_vision(pplayer);
}

/****************************************************************************
  Shows the area to the player.  Unless the tile is "seen", it will remain
  fogged and units will be hidden.
****************************************************************************/
void map_show_all(struct player *pplayer)
{
  buffer_shared_vision(pplayer);

  whole_map_iterate(ptile) {
    map_show_tile(pplayer, ptile);
  } whole_map_iterate_end;

  unbuffer_shared_vision(pplayer);
}

/****************************************************************************
  Return whether the player knows the tile.  Knowing a tile means you've
  seen it once (as opposed to seeing a tile which means you can see it now).
****************************************************************************/
bool map_is_known(const struct tile *ptile, const struct player *pplayer)
{
  return dbv_isset(&pplayer->tile_known, tile_index(ptile));
}

/****************************************************************************
  Returns whether the layer 'vlayer' of the tile 'ptile' is known and seen
  by the player 'pplayer'.
****************************************************************************/
bool map_is_known_and_seen(const struct tile *ptile,
                           const struct player *pplayer,
                           enum vision_layer vlayer)
{
  return (map_is_known(ptile, pplayer)
          && 0 < map_get_seen(pplayer, ptile, vlayer));
}

/****************************************************************************
  Return whether the player can see the tile.  Seeing a tile means you have
  vision of it now (as opposed to knowing a tile which means you've seen it
  before).  Note that a tile can be seen but not known (currently this only
  happens when a city is founded with some unknown tiles in its radius); in
  this case the tile is unknown (but map_get_seen will still return TRUE).
****************************************************************************/
static inline int map_get_seen(const struct player *pplayer,
                               const struct tile *ptile,
                               enum vision_layer vlayer)
{
  return map_get_player_tile(ptile, pplayer)->seen_count[vlayer];
}

/****************************************************************************
  This function changes the seen state of one player for all vision layers
  of a tile. It reveals the tiles if needed and controls the fog of war.

  See also map_change_own_seen(), shared_vision_change_seen().
****************************************************************************/
void map_change_seen(struct player *pplayer,
                     struct tile *ptile,
                     const v_radius_t change,
                     bool can_reveal_tiles)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);
  bool revealing_tile = FALSE;

#ifdef DEBUG
  log_debug("%s() for player %s (nb %d) at (%d, %d).",
            __FUNCTION__, player_name(pplayer), player_number(pplayer),
            TILE_XY(ptile));
  vision_layer_iterate(v) {
    log_debug("  vision layer %d is changing from %d to %d.",
              v, plrtile->seen_count[v], plrtile->seen_count[v] + change[v]);
  } vision_layer_iterate_end;
#endif /* DEBUG */

  vision_layer_iterate(v) {
    /* Avoid underflow. */
    fc_assert(0 <= change[v] || -change[v] <= plrtile->seen_count[v]);
    plrtile->seen_count[v] += change[v];
  } vision_layer_iterate_end;

  /* V_MAIN vision ranges must always be more than V_INVIS ranges
   * (see comment in common/vision.h), so we assume that the V_MAIN
   * seen count cannot be inferior to V_INVIS seen count.
   * Moreover, when the fog of war is disabled, V_MAIN has an extra
   * seen count point. */
  fc_assert(plrtile->seen_count[V_INVIS] + !game.info.fogofwar
            <= plrtile->seen_count[V_MAIN]);

  if (!map_is_known(ptile, pplayer)) {
    if (0 < plrtile->seen_count[V_MAIN] && can_reveal_tiles) {
      log_debug("(%d, %d): revealing tile to player %s (nb %d).",
                TILE_XY(ptile), player_name(pplayer),
                player_number(pplayer));

      map_set_known(ptile, pplayer);
      revealing_tile = TRUE;
    } else {
      return;
    }
  }

  /* Removes units out of vision. First, check V_INVIS layer because
   * we must remove all units before fog of war because clients expect
   * the tile is empty when it is fogged. */
  if (0 > change[V_INVIS] && 0 == plrtile->seen_count[V_INVIS]) {
    log_debug("(%d, %d): hiding invisible units to player %s (nb %d).",
              TILE_XY(ptile), player_name(pplayer), player_number(pplayer));

    unit_list_iterate(ptile->units, punit) {
      if (unit_is_visible_on_layer(punit, V_INVIS)) {
        unit_goes_out_of_sight(pplayer, punit);
      }
    } unit_list_iterate_end;
  }

  if (0 > change[V_MAIN] && 0 == plrtile->seen_count[V_MAIN]) {
    log_debug("(%d, %d): fogging tile for player %s (nb %d).",
              TILE_XY(ptile), player_name(pplayer), player_number(pplayer));

    unit_list_iterate(ptile->units, punit) {
      if (unit_is_visible_on_layer(punit, V_MAIN)) {
        unit_goes_out_of_sight(pplayer, punit);
      }
    } unit_list_iterate_end;

    /* Fog the tile. */
    update_player_tile_last_seen(pplayer, ptile);
    send_tile_info(pplayer->connections, ptile, FALSE);
    if (game.server.foggedborders) {
      plrtile->owner = tile_owner(ptile);
    }
  }

  if ((revealing_tile && 0 < plrtile->seen_count[V_MAIN])
      || (0 < change[V_MAIN]
          /* plrtile->seen_count[V_MAIN] Always set to 1
            * when the fog of war is disabled. */
          && (change[V_MAIN] + !game.info.fogofwar
              == (plrtile->seen_count[V_MAIN])))) {
    struct city *pcity;

    log_debug("(%d, %d): unfogging tile for player %s (nb %d).",
              TILE_XY(ptile), player_name(pplayer), player_number(pplayer));

    /* Send info about the tile itself.
     * It has to be sent first because the client needs correct
     * continent number before it can handle following packets
     */
    update_player_tile_knowledge(pplayer, ptile);
    send_tile_info(pplayer->connections, ptile, FALSE);

    /* Discover units. */
    unit_list_iterate(ptile->units, punit) {
      if (unit_is_visible_on_layer(punit, V_MAIN)) {
        send_unit_info(pplayer, punit);
      }
    } unit_list_iterate_end;

    /* Discover cities. */
    reality_check_city(pplayer, ptile);

    if (NULL != (pcity = tile_city(ptile))) {
      send_city_info(pplayer, pcity);
    }
  }

  if ((revealing_tile && 0 < plrtile->seen_count[V_INVIS])
      || (0 < change[V_INVIS]
          && change[V_INVIS] == plrtile->seen_count[V_INVIS])) {
    log_debug("(%d, %d): revealing invisible units to player %s (nb %d).",
              TILE_XY(ptile), player_name(pplayer),
              player_number(pplayer));
     /* Discover units. */
    unit_list_iterate(ptile->units, punit) {
      if (unit_is_visible_on_layer(punit, V_INVIS)) {
        send_unit_info(pplayer, punit);
      }
    } unit_list_iterate_end;
  }
}

/****************************************************************************
  Returns the own seen count of a tile for a player. It doesn't count the
  shared vision.

  See also map_get_seen().
****************************************************************************/
static inline int map_get_own_seen(const struct player *pplayer,
                                   const struct tile *ptile,
                                   enum vision_layer vlayer)
{
  return map_get_player_tile(ptile, pplayer)->own_seen[vlayer];
}

/***************************************************************
  Changes the own seen count of a tile for a player.
***************************************************************/
static void map_change_own_seen(struct player *pplayer,
                                struct tile *ptile,
                                const v_radius_t change)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  vision_layer_iterate(v) {
    plrtile->own_seen[v] += change[v];
  } vision_layer_iterate_end;
}

/***************************************************************
 Changes site information for player tile.
***************************************************************/
void change_playertile_site(struct player_tile *ptile,
                            struct vision_site *new_site)
{
  if (ptile->site == new_site) {
    /* Do nothing. */
    return;
  }

  if (ptile->site != NULL) {
    /* Releasing old site from tile */
    free_vision_site(ptile->site);
  }

  ptile->site = new_site;
}

/***************************************************************
...
***************************************************************/
void map_set_known(struct tile *ptile, struct player *pplayer)
{
  dbv_set(&pplayer->tile_known, tile_index(ptile));
}

/***************************************************************
...
***************************************************************/
void map_clear_known(struct tile *ptile, struct player *pplayer)
{
  dbv_clr(&pplayer->tile_known, tile_index(ptile));
}

/****************************************************************************
  Call this function to unfog all tiles.  This should only be called when
  a player dies or at the end of the game as it will result in permanent
  vision of the whole map.
****************************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  const v_radius_t radius_sq = V_RADIUS(1, 1);

  buffer_shared_vision(pplayer);
  whole_map_iterate(ptile) {
    map_change_seen(pplayer, ptile, radius_sq, TRUE);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pplayer);
}

/**************************************************************************
  Unfogs all tiles for all players.  See map_know_and_see_all.
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
void player_map_init(struct player *pplayer)
{
  pplayer->server.private_map
    = fc_realloc(pplayer->server.private_map,
                 MAP_INDEX_SIZE * sizeof(*pplayer->server.private_map));

  whole_map_iterate(ptile) {
    player_tile_init(ptile, pplayer);
  } whole_map_iterate_end;

  dbv_init(&pplayer->tile_known, MAP_INDEX_SIZE);
}

/***************************************************************
 frees a player's private map.
***************************************************************/
void player_map_free(struct player *pplayer)
{
  if (!pplayer->server.private_map) {
    return;
  }

  /* only after removing borders! */
  whole_map_iterate(ptile) {
    /* Clear player vision. */
    change_playertile_site(map_get_player_tile(ptile, pplayer), NULL);

    /* Clear other players knowledge about the removed one. */
    players_iterate(aplayer) {
      struct player_tile *aplrtile = map_get_player_tile(ptile, aplayer);

      if (aplrtile && aplrtile->site &&
          vision_owner(aplrtile->site) == pplayer) {
        change_playertile_site(aplrtile, NULL);
      }
    } players_iterate_end;

    /* clear players knowledge */
    map_clear_known(ptile, pplayer);

    /* Free all claimed tiles. */
    if (tile_owner(ptile) == pplayer) {
      tile_set_owner(ptile, NULL, NULL);
    }
  } whole_map_iterate_end;

  free(pplayer->server.private_map);
  pplayer->server.private_map = NULL;

  dbv_free(&pplayer->tile_known);
}

/***************************************************************
  We need to use fogofwar_old here, so the player's tiles get
  in the same state as the other players' tiles.
***************************************************************/
static void player_tile_init(struct tile *ptile, struct player *pplayer)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  plrtile->terrain = T_UNKNOWN;
  clear_all_specials(&plrtile->special);
  plrtile->resource = NULL;
  plrtile->owner = NULL;
  plrtile->site = NULL;
  BV_CLR_ALL(plrtile->bases);
  plrtile->last_updated = game.info.year;

  plrtile->seen_count[V_MAIN] = !game.server.fogofwar_old;
  plrtile->seen_count[V_INVIS] = 0;
  memcpy(plrtile->own_seen, plrtile->seen_count, sizeof(v_radius_t));
}

/****************************************************************************
  Returns city located at given tile from player map.
****************************************************************************/
struct vision_site *map_get_player_city(const struct tile *ptile,
					const struct player *pplayer)
{
  struct vision_site *psite = map_get_player_site(ptile, pplayer);

  fc_assert_ret_val(psite == NULL || psite->location == ptile, NULL);
 
  return psite;
}

/****************************************************************************
  Returns site located at given tile from player map.
****************************************************************************/
struct vision_site *map_get_player_site(const struct tile *ptile,
					const struct player *pplayer)
{
  return map_get_player_tile(ptile, pplayer)->site;
}

/****************************************************************************
  Players' information of tiles is tracked so that fogged area can be kept
  consistent even when the client disconnects.  This function returns the
  player tile information for the given tile and player.
****************************************************************************/
struct player_tile *map_get_player_tile(const struct tile *ptile,
					const struct player *pplayer)
{
  fc_assert_ret_val(pplayer->server.private_map, NULL);

  return pplayer->server.private_map + tile_index(ptile);
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
  struct player *owner = (game.server.foggedborders
                          && !map_is_known_and_seen(ptile, pplayer, V_MAIN)
                          ? plrtile->owner
                          : tile_owner(ptile));

  if (plrtile->terrain != ptile->terrain
      || !BV_ARE_EQUAL(plrtile->special, ptile->special)
      || plrtile->resource != ptile->resource
      || plrtile->owner != owner
      || !BV_ARE_EQUAL(plrtile->bases, ptile->bases)) {
    plrtile->terrain = ptile->terrain;
    plrtile->special = ptile->special;
    plrtile->resource = ptile->resource;
    plrtile->owner = owner;
    plrtile->bases = ptile->bases;
    return TRUE;
  }
  return FALSE;
}

/****************************************************************************
  Update playermap knowledge for everybody who sees the tile, and send a
  packet to everyone whose info is changed.

  Note this only checks for changing of the terrain, special, or resource
  for the tile, since these are the only values held in the playermap.

  A tile's owner always can see terrain changes in his or her territory.
****************************************************************************/
void update_tile_knowledge(struct tile *ptile)
{
  /* Players */
  players_iterate(pplayer) {
    if (map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
      if (update_player_tile_knowledge(pplayer, ptile)) {
        send_tile_info(pplayer->connections, ptile, FALSE);
      }
    }
  } players_iterate_end;

  /* Global observers */
  conn_list_iterate(game.est_connections, pconn) {
    struct player *pplayer = pconn->playing;

    if (NULL == pplayer && pconn->observer) {
      send_tile_info(pconn->self, ptile, FALSE);
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
  if (!map_is_known_and_seen(ptile, pdest, V_MAIN)) {
    /* I can just hear people scream as they try to comprehend this if :).
     * Let me try in words:
     * 1) if the tile is seen by pfrom the info is sent to pdest
     *  OR
     * 2) if the tile is known by pfrom AND (he has more recent info
     *     OR it is not known by pdest)
     */
    if (map_is_known_and_seen(ptile, pfrom, V_MAIN)
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
      dest_tile->resource = from_tile->resource;
      dest_tile->bases    = from_tile->bases;
      dest_tile->last_updated = from_tile->last_updated;
      send_tile_info(pdest->connections, ptile, FALSE);

      /* update and send city knowledge */
      /* remove outdated cities */
      if (dest_tile->site) {
	if (!from_tile->site) {
	  /* As the city was gone on the newer from_tile
	     it will be removed by this function */
	  reality_check_city(pdest, ptile);
	} else /* We have a dest_city. update */
	  if (from_tile->site->identity
              != dest_tile->site->identity) {
	    /* As the city was gone on the newer from_tile
	       it will be removed by this function */
	    reality_check_city(pdest, ptile);
          }
      }

      /* Set and send new city info */
      if (from_tile->site) {
	if (!dest_tile->site) {
          /* We cannot assign new vision site with change_playertile_site(),
           * since location is not yet set up for new site */
          dest_tile->site = create_vision_site(0, ptile, NULL);
          *dest_tile->site = *from_tile->site;
	}
        /* Note that we don't care if receiver knows vision source city
         * or not. */
	send_city_info_at_tile(pdest, pdest->connections, NULL, ptile);
      }

      city_map_update_tile_frozen(ptile);
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
    if (really_gives_vision(pdest, pplayer2)) {
      really_give_tile_info_from_player_to_player(pfrom, pplayer2, ptile);
    }
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
    pplayer->server.really_gives_vision = pplayer->gives_shared_vision;
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
              BV_SET(pplayer->server.really_gives_vision, player_index(pplayer3));
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
  bv_player save_vision[player_slot_count()];
  if (pfrom == pto) return;
  if (gives_shared_vision(pfrom, pto)) {
    log_error("Trying to give shared vision from %s to %s, "
              "but that vision is already given!",
              player_name(pfrom), player_name(pto));
    return;
  }

  players_iterate(pplayer) {
    save_vision[player_index(pplayer)] = pplayer->server.really_gives_vision;
  } players_iterate_end;

  BV_SET(pfrom->gives_shared_vision, player_index(pto));
  create_vision_dependencies();
  log_debug("giving shared vision from %s to %s",
            player_name(pfrom), player_name(pto));

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (really_gives_vision(pplayer, pplayer2)
          && !BV_ISSET(save_vision[player_index(pplayer)],
                       player_index(pplayer2))) {
        log_debug("really giving shared vision from %s to %s",
                  player_name(pplayer), player_name(pplayer2));
        whole_map_iterate(ptile) {
          const v_radius_t change =
              V_RADIUS(map_get_own_seen(pplayer, ptile, V_MAIN),
                       map_get_own_seen(pplayer, ptile, V_INVIS));

          if (0 < change[V_MAIN] || 0 < change[V_INVIS]) {
            map_change_seen(pplayer2, ptile, change,
                            map_is_known(ptile, pplayer));
          }
        } whole_map_iterate_end;

	/* squares that are not seen, but which pfrom may have more recent
	   knowledge of */
	give_map_from_player_to_player(pplayer, pplayer2);
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (S_S_RUNNING == server_state()) {
    send_player_info_c(pfrom, NULL);
  }
}

/***************************************************************
...
***************************************************************/
void remove_shared_vision(struct player *pfrom, struct player *pto)
{
  bv_player save_vision[player_slot_count()];

  fc_assert_ret(pfrom != pto);
  if (!gives_shared_vision(pfrom, pto)) {
    log_error("Tried removing the shared vision from %s to %s, "
              "but it did not exist in the first place!",
              player_name(pfrom), player_name(pto));
    return;
  }

  players_iterate(pplayer) {
    save_vision[player_index(pplayer)] = pplayer->server.really_gives_vision;
  } players_iterate_end;

  log_debug("removing shared vision from %s to %s",
            player_name(pfrom), player_name(pto));

  BV_CLR(pfrom->gives_shared_vision, player_index(pto));
  create_vision_dependencies();

  players_iterate(pplayer) {
    buffer_shared_vision(pplayer);
    players_iterate(pplayer2) {
      if (!really_gives_vision(pplayer, pplayer2)
          && BV_ISSET(save_vision[player_index(pplayer)], 
                      player_index(pplayer2))) {
        log_debug("really removing shared vision from %s to %s",
                  player_name(pplayer), player_name(pplayer2));
        whole_map_iterate(ptile) {
          const v_radius_t change =
              V_RADIUS(-map_get_own_seen(pplayer, ptile, V_MAIN),
                       -map_get_own_seen(pplayer, ptile, V_INVIS));

          if (0 > change[V_MAIN] || 0 > change[V_INVIS]) {
            map_change_seen(pplayer2, ptile, change, FALSE);
          }
        } whole_map_iterate_end;
      }
    } players_iterate_end;
    unbuffer_shared_vision(pplayer);
  } players_iterate_end;

  if (S_S_RUNNING == server_state()) {
    send_player_info_c(pfrom, NULL);
  }
}

/*************************************************************************
...
*************************************************************************/
void enable_fog_of_war_player(struct player *pplayer)
{
  const v_radius_t radius_sq = V_RADIUS(-1, 0);

  buffer_shared_vision(pplayer);
  whole_map_iterate(ptile) {
    map_change_seen(pplayer, ptile, radius_sq, FALSE);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pplayer);
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
void disable_fog_of_war_player(struct player *pplayer)
{
  const v_radius_t radius_sq = V_RADIUS(1, 0);

  buffer_shared_vision(pplayer);
  whole_map_iterate(ptile) {
    map_change_seen(pplayer, ptile, radius_sq, FALSE);
  } whole_map_iterate_end;
  unbuffer_shared_vision(pplayer);
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
  (Note that rivers-to-nowhere can still occur if a single-tile lake is
  transformed away, but this is relatively unlikely.)
  For simplicity, I'm assuming that this is the only exit of the river,
  so I don't need to trace it across the continent.  --CJM
**************************************************************************/
static void ocean_to_land_fix_rivers(struct tile *ptile)
{
  /* clear the river if it exists */
  tile_clear_special(ptile, S_RIVER);

  cardinal_adjc_iterate(ptile, tile1) {
    if (tile_has_special(tile1, S_RIVER)) {
      bool ocean_near = FALSE;
      cardinal_adjc_iterate(tile1, tile2) {
        if (is_ocean_tile(tile2))
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
  Helper function for bounce_units_on_terrain_change() that checks units
  on a single tile.
****************************************************************************/
static void check_units_single_tile(struct tile *ptile)
{
  unit_list_iterate_safe(ptile->units, punit) {
    bool unit_alive = TRUE;

    if (punit->tile == ptile
	&& punit->transported_by == -1
	&& !can_unit_exist_at_tile(punit, ptile)) {
      /* look for a nearby safe tile */
      adjc_iterate(ptile, ptile2) {
	if (can_unit_exist_at_tile(punit, ptile2)
            && !is_non_allied_unit_tile(ptile2, unit_owner(punit))
            && !is_non_allied_city_tile(ptile2, unit_owner(punit))) {
          log_verbose("Moved %s %s due to changing terrain at (%d,%d).",
                      nation_rule_name(nation_of_unit(punit)),
                      unit_rule_name(punit), TILE_XY(punit->tile));
          notify_player(unit_owner(punit), unit_tile(punit),
                        E_UNIT_RELOCATED, ftc_server,
                        _("Moved your %s due to changing terrain."),
                        unit_link(punit));
	  unit_alive = move_unit(punit, ptile2, 0);
	  if (unit_alive && punit->activity == ACTIVITY_SENTRY) {
	    unit_activity_handling(punit, ACTIVITY_IDLE);
	  }
	  break;
	}
      } adjc_iterate_end;
      if (unit_alive && punit->tile == ptile) {
        /* If we get here we could not move punit. */
        log_verbose("Disbanded %s %s due to changing land "
                    " to sea at (%d, %d).",
                    nation_rule_name(nation_of_unit(punit)),
                    unit_rule_name(punit), TILE_XY(unit_tile(punit)));
        notify_player(unit_owner(punit), unit_tile(punit),
                      E_UNIT_LOST_MISC, ftc_server,
                      _("Disbanded your %s due to changing terrain."),
                      unit_tile_link(punit));
        wipe_unit(punit);
      }
    }
  } unit_list_iterate_safe_end;
}

/****************************************************************************
  Check ptile and nearby tiles to see if all units can remain at their
  current locations, and move or disband any that cannot. Call this after
  terrain or specials change on ptile.
****************************************************************************/
void bounce_units_on_terrain_change(struct tile *ptile)
{
  /* Check this tile for direct effect on its units */
  check_units_single_tile(ptile);
  /* We have to check adjacent tiles too, in case units in cities are now
   * illegal (e.g., boat in a city that has become landlocked). */
  adjc_iterate(ptile, ptile2) {
    check_units_single_tile(ptile2);
  } adjc_iterate_end;
}

/****************************************************************************
  Returns TRUE if the terrain change from 'oldter' to 'newter' may require
  expensive reassignment of continents.
****************************************************************************/
bool need_to_reassign_continents(const struct terrain *oldter,
                                 const struct terrain *newter)
{
  bool old_is_ocean, new_is_ocean;
  
  if (!oldter || !newter) {
    return FALSE;
  }

  old_is_ocean = is_ocean(oldter);
  new_is_ocean = is_ocean(newter);

  return (old_is_ocean && !new_is_ocean)
    || (!old_is_ocean && new_is_ocean);
}

/****************************************************************************
  Handles local side effects for a terrain change (tile and its
  surroundings). Does *not* handle global side effects (such as reassigning
  continents).
  For in-game terrain changes 'extend_rivers' should be TRUE; for edits it
  should be FALSE.
****************************************************************************/
void fix_tile_on_terrain_change(struct tile *ptile,
                                struct terrain *oldter,
                                bool extend_rivers)
{
  if (is_ocean(oldter) && !is_ocean_tile(ptile)) {
    if (extend_rivers) {
      ocean_to_land_fix_rivers(ptile);
    }
    city_landlocked_sell_coastal_improvements(ptile);
  }

  /* Units may no longer be able to hold their current positions. */
  bounce_units_on_terrain_change(ptile);
}

/****************************************************************************
  Handles local and global side effects for a terrain change for a single
  tile.
  Call this in the server immediately after calling tile_change_terrain.
  Assumes an in-game terrain change (e.g., by workers/engineers).
****************************************************************************/
void check_terrain_change(struct tile *ptile, struct terrain *oldter)
{
  struct terrain *newter = tile_terrain(ptile);

  fix_tile_on_terrain_change(ptile, oldter, TRUE);
  if (need_to_reassign_continents(oldter, newter)) {
    assign_continent_numbers();
    send_all_known_tiles(NULL);
  }

  sanity_check_tile(ptile);
}

/*************************************************************************
  Ocean tile can be claimed iff one of the following conditions stands:
  a) it is an inland lake not larger than MAXIMUM_OCEAN_SIZE
  b) it is adjacent to only one continent and not more than two ocean tiles
  c) It is one tile away from a city
  The source which claims the ocean has to be placed on the correct continent.
  in case a) The continent which surrounds the inland lake
  in case b) The only continent which is adjacent to the tile
*************************************************************************/
static bool is_claimable_ocean(struct tile *ptile, struct tile *source)
{
  Continent_id cont = tile_continent(ptile);
  Continent_id source_cont = tile_continent(source);
  Continent_id cont2;
  int ocean_tiles;

  if (get_ocean_size(-cont) <= MAXIMUM_CLAIMED_OCEAN_SIZE
      && get_lake_surrounders(cont) == source_cont) {
    return TRUE;
  }

  if (ptile == source) {
    /* Source itself is always claimable. */
    return TRUE;
  }

  ocean_tiles = 0;
  adjc_iterate(ptile, tile2) {
    cont2 = tile_continent(tile2);
    if (tile2 == source) {
      return TRUE;
    }
    if (cont2 == cont) {
      ocean_tiles++;
    } else if (cont2 != source_cont) {
      return FALSE; /* two land continents adjacent, punt! */
    }
  } adjc_iterate_end;
  if (ocean_tiles <= 2) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/*************************************************************************
  For each unit at the tile, queue any unique home city.
*************************************************************************/
static void map_unit_homecity_enqueue(struct tile *ptile)
{
  unit_list_iterate(ptile->units, punit) {
    struct city *phome = game_city_by_number(punit->homecity);

    if (NULL == phome) {
      continue;
    }

    city_refresh_queue_add(phome);
  } unit_list_iterate_end;
}

/*************************************************************************
  Claim ownership of a single tile.  If ignore_loss is not NULL, then
  it won't remove the effect of this base from the former tile owner.
*************************************************************************/
static void map_claim_ownership_full(struct tile *ptile,
                                     struct player *powner,
                                     struct tile *psource,
                                     struct base_type *ignore_loss)
{
  struct player *ploser = tile_owner(ptile);

  if (BORDERS_SEE_INSIDE == game.info.borders
      || BORDERS_EXPAND == game.info.borders) {
    if (ploser != powner) {
      if (ploser) {
        const v_radius_t radius_sq = V_RADIUS(-1, 0);

        shared_vision_change_seen(ploser, ptile, radius_sq, FALSE);
      }
      if (powner) {
        const v_radius_t radius_sq = V_RADIUS(1, 0);

        shared_vision_change_seen(powner, ptile, radius_sq, TRUE);
      }
    }
  }

  if (ploser != powner) {
    base_type_iterate(pbase) {
      if (tile_has_base(ptile, pbase)) {
        /* Transfer base provided vision to new owner */
        if (powner) {
          const v_radius_t old_radius_sq = V_RADIUS(-1, -1);
          const v_radius_t new_radius_sq = V_RADIUS(pbase->vision_main_sq,
                                                    pbase->vision_invis_sq);

          map_vision_update(powner, ptile, old_radius_sq, new_radius_sq,
                            game.server.vision_reveal_tiles);
        }
        if (ploser && pbase != ignore_loss) {
          const v_radius_t old_radius_sq = V_RADIUS(pbase->vision_main_sq,
                                                    pbase->vision_invis_sq);
          const v_radius_t new_radius_sq = V_RADIUS(-1, -1);

          map_vision_update(ploser, ptile, old_radius_sq, new_radius_sq,
                            game.server.vision_reveal_tiles);
        }
      }
    } base_type_iterate_end;
  }

  tile_set_owner(ptile, powner, psource);

  /* Needed only when foggedborders enabled, but we do it unconditionally
   * in case foggedborders ever gets enabled later. Better to have correct
   * information in player map just in case. */
  update_tile_knowledge(ptile);

  if (ploser != powner) {
    if (S_S_RUNNING == server_state() && game.info.happyborders > 0) {
      map_unit_homecity_enqueue(ptile);
    }

    if (!city_map_update_tile_frozen(ptile)) {
      send_tile_info(NULL, ptile, FALSE);
    }
  }
}

/*************************************************************************
  Claim ownership of a single tile.
*************************************************************************/
void map_claim_ownership(struct tile *ptile, struct player *powner,
                         struct tile *psource)
{
  map_claim_ownership_full(ptile, powner, psource, NULL);
}

/*************************************************************************
  Remove border for this source.
*************************************************************************/
void map_clear_border(struct tile *ptile)
{
  int radius_sq = tile_border_source_radius_sq(ptile);

  circle_dxyr_iterate(ptile, radius_sq, dtile, dx, dy, dr) {
    struct tile *claimer = tile_claimer(dtile);

    if (claimer == ptile) {
      map_claim_ownership(dtile, NULL, NULL);
    }
  } circle_dxyr_iterate_end;
}

/*************************************************************************
  Update borders for this source. Call this for each new source.
*************************************************************************/
void map_claim_border(struct tile *ptile, struct player *owner)
{
  int radius_sq = tile_border_source_radius_sq(ptile);

  if (BORDERS_DISABLED == game.info.borders) {
    return;
  }

  circle_dxyr_iterate(ptile, radius_sq, dtile, dx, dy, dr) {
    struct tile *dclaimer = tile_claimer(dtile);

    if (dr != 0 && is_border_source(dtile)) {
      /* Do not claim border sources other than self */
      continue;
    }

    if (!map_is_known(dtile, owner) && BORDERS_EXPAND != game.info.borders) {
      continue;
    }

    if (NULL != dclaimer && dclaimer != ptile) {
      struct city *ccity = tile_city(dclaimer);
      int strength_old, strength_new;

      if (ccity != NULL) {
        /* Previously claimed by city */
        int city_x, city_y;

        map_distance_vector(&city_x, &city_y, ccity->tile, dtile);

        if (is_valid_city_coords(city_map_radius_sq_get(ccity),
            CITY_ABS2REL(city_x), CITY_ABS2REL(city_y))) {
          /* Tile is within squared city radius */
          continue;
        }
      }

      strength_old = tile_border_strength(dtile, dclaimer);
      strength_new = tile_border_strength(dtile, ptile);

      if (strength_new <= strength_old) {
        /* Stronger shall prevail,
         * in case of equel strength older shall prevail */
        continue;
      }
    }

    if (is_ocean_tile(dtile)) {
      if (is_claimable_ocean(dtile, ptile)) {
        map_claim_ownership(dtile, owner, ptile);
      }
    } else {
      if (tile_continent(dtile) == tile_continent(ptile)) {
        map_claim_ownership(dtile, owner, ptile);
      }
    }
  } circle_dxyr_iterate_end;
}

/*************************************************************************
  Update borders for all sources. Call this on turn end.
*************************************************************************/
void map_calculate_borders(void)
{
  if (BORDERS_DISABLED == game.info.borders) {
    return;
  }

  log_verbose("map_calculate_borders()");

  whole_map_iterate(ptile) {
    if (is_border_source(ptile)) {
      if (ptile->owner != NULL) {
	map_claim_border(ptile, ptile->owner);
      }
    }
  } whole_map_iterate_end;

  log_verbose("map_calculate_borders() workers");
  city_thaw_workers_queue();
  city_refresh_queue_processing();
}

/****************************************************************************
  Change the sight points for the vision source, fogging or unfogging tiles
  as needed.

  See documentation in vision.h.
****************************************************************************/
void vision_change_sight(struct vision *vision, const v_radius_t radius_sq)
{
  map_vision_update(vision->player, vision->tile, vision->radius_sq,
                    radius_sq, vision->can_reveal_tiles);
  memcpy(vision->radius_sq, radius_sq, sizeof(v_radius_t));
}

/****************************************************************************
  Clear all sight points from this vision source.

  See documentation in vision.h.
****************************************************************************/
void vision_clear_sight(struct vision *vision)
{
  const v_radius_t vision_radius_sq = V_RADIUS(-1, -1);

  vision_change_sight(vision, vision_radius_sq);
}

/****************************************************************************
  Create base to tile.
****************************************************************************/
void create_base(struct tile *ptile, struct base_type *pbase,
                 struct player *pplayer)
{
  bool done_new_vision = FALSE;
  bool bases_destroyed = FALSE;

  base_type_iterate(old_base) {
    if (tile_has_base(ptile, old_base)
        && !can_bases_coexist(old_base, pbase)) {
      destroy_base(ptile, old_base);
      bases_destroyed = TRUE;
    }
  } base_type_iterate_end;

  tile_add_base(ptile, pbase);

  /* Watchtower might become effective
   * FIXME: Reqs on other specials will not be updated immediately. */
  unit_list_refresh_vision(ptile->units);

  /* Claim base if it has "ClaimTerritory" flag */
  if (territory_claiming_base(pbase) && pplayer) {
    /* Normally map_claim_ownership will enact the new base's vision effect
     * as a side effect, except for the nasty special case where we already
     * own the tile. */
    if (pplayer != tile_owner(ptile))
      done_new_vision = TRUE;
    map_claim_ownership_full(ptile, pplayer, ptile, pbase);
    map_claim_border(ptile, pplayer);
    city_thaw_workers_queue();
    city_refresh_queue_processing();
  }
  if (!done_new_vision) {
    struct player *owner = tile_owner(ptile);

    if (NULL != owner
        && (0 < pbase->vision_main_sq || 0 < pbase->vision_invis_sq)) {
      const v_radius_t old_radius_sq = V_RADIUS(-1, -1);
      const v_radius_t new_radius_sq =
          V_RADIUS(0 < pbase->vision_main_sq ? pbase->vision_main_sq : -1,
                   0 < pbase->vision_invis_sq ? pbase->vision_invis_sq : -1);

      map_vision_update(owner, ptile, old_radius_sq, new_radius_sq,
                        game.server.vision_reveal_tiles);
    }
  }

  if (bases_destroyed) {
    /* Maybe conflicting base that was removed was the only thing
     * making tile native to some unit. */
    bounce_units_on_terrain_change(ptile);
  }
}

/****************************************************************************
  Remove base from tile.
****************************************************************************/
void destroy_base(struct tile *ptile, struct base_type *pbase)
{
  if (territory_claiming_base(pbase)) {
    /* Clearing borders will take care of the vision providing
     * bases as well. */
    map_clear_border(ptile);
  } else {
    struct player *owner = tile_owner(ptile);

    if (NULL != owner
        && (0 <= pbase->vision_main_sq || 0 <= pbase->vision_invis_sq)) {
      /* Base provides vision, but no borders. */
      const v_radius_t old_radius_sq =
          V_RADIUS(0 <= pbase->vision_main_sq ? pbase->vision_main_sq : -1,
                   0 <= pbase->vision_invis_sq ? pbase->vision_invis_sq : -1);
      const v_radius_t new_radius_sq = V_RADIUS(-1, -1);

      map_vision_update(owner, ptile, old_radius_sq, new_radius_sq,
                        game.server.vision_reveal_tiles);
    }
  }
  tile_remove_base(ptile, pbase);
}
