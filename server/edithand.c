/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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
#include <limits.h> /* USHRT_MAX */

#include "events.h"
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "terrain.h"
#include "unitlist.h"

#include "citytools.h"
#include "cityturn.h"
#include "gamehand.h"
#include "plrhand.h"
#include "unittools.h"
#include "hand_gen.h"
#include "maphand.h"
#include "utilities.h"

/* The number of tiles for which expensive checks have
 * been deferred after their terrains have been edited. */
static int unfixed_terrain_count;

/****************************************************************************
  Do the potentially slow checks required after some tile's terrain changes.
****************************************************************************/
static void check_edited_tile_terrains(void)
{
  if (unfixed_terrain_count > 0) {
    whole_map_iterate(ptile) {
      if (!ptile->editor.need_terrain_fix) {
        continue;
      }
      fix_tile_on_terrain_change(ptile, FALSE);
      ptile->editor.need_terrain_fix = FALSE;
    } whole_map_iterate_end;
    assign_continent_numbers();
    send_all_known_tiles(NULL);
  }
  unfixed_terrain_count = 0;
}

/****************************************************************************
  Do any necessary checks after leaving edit mode to ensure that the game
  is in a consistent state.
****************************************************************************/
static void check_leaving_edit_mode(void)
{
  conn_list_do_buffer(game.est_connections);
  players_iterate(pplayer) {
    if (pplayer->editor.fog_of_war_disabled
        && game.info.fogofwar) {
      enable_fog_of_war_player(pplayer);
    } else if (!pplayer->editor.fog_of_war_disabled
               && !game.info.fogofwar) {
      disable_fog_of_war_player(pplayer);
    }
    pplayer->editor.fog_of_war_disabled = FALSE;
  } players_iterate_end;

  check_edited_tile_terrains();
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Handles a request by the client to enter edit mode.
****************************************************************************/
void handle_edit_mode(struct connection *pc, bool is_edit_mode)
{
  if (!can_conn_enable_editing(pc)) {
    return;
  }
  if (!game.info.is_edit_mode && is_edit_mode) {
    /* Someone could be cheating! Warn people. */
    notify_conn(NULL, NULL, E_SETTING,
                _(" *** Server set to edit mode by %s! *** "),
                conn_description(pc));
  }
  if (game.info.is_edit_mode && !is_edit_mode) {
    notify_conn(NULL, NULL, E_SETTING,
                _(" *** Edit mode cancelled by %s. *** "),
                conn_description(pc));

    check_leaving_edit_mode();
  }
  if (game.info.is_edit_mode != is_edit_mode) {
    game.info.is_edit_mode = is_edit_mode;
    send_game_info(NULL);
  }
}

/****************************************************************************
  Handles a client request to change the terrain of the tile at the given
  x, y coordinates. The 'size' parameter indicates that all tiles in a
  square of "radius" 'size' should be affected. So size=1 corresponds to
  the single tile case.
****************************************************************************/
void handle_edit_tile_terrain(struct connection *pc, int x, int y,
                              Terrain_type_id terrain, int size)
{
  struct terrain *old_terrain;
  struct terrain *pterrain;
  struct tile *ptile_center;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit the tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  pterrain = terrain_by_number(terrain);
  if (!pterrain) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND,
                _("Cannot modify terrain for the tile (%d, %d) because "
                  "%d is not a valid terrain id."), x, y, terrain);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    old_terrain = tile_terrain(ptile);
    if (old_terrain == pterrain) {
      continue;
    }
    tile_change_terrain(ptile, pterrain);

    if (need_to_fix_terrain_change(old_terrain, pterrain)) {
      ptile->editor.need_terrain_fix = TRUE;
      unfixed_terrain_count++;
    }
    update_tile_knowledge(ptile);
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Handle a request to change one or more tiles' resources.
****************************************************************************/
void handle_edit_tile_resource(struct connection *pc, int x, int y,
                               Resource_type_id resource, int size)
{
  struct resource *presource;
  struct tile *ptile_center;
  

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit the tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }
  presource = resource_by_number(resource); /* May be NULL. */

  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    if (presource == tile_resource(ptile)) {
      continue;
    }
    tile_set_resource(ptile, presource);
    update_tile_knowledge(ptile);
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Handle a request to change one or more tiles' specials. The 'remove'
  argument controls whether to remove or add the given special of type
  'special' from the tile.
****************************************************************************/
void handle_edit_tile_special(struct connection *pc, int x, int y,
                              enum tile_special_type special,
                              bool remove, int size)
{
  struct tile *ptile_center;
  bool changed = FALSE;
  
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit the tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  if (!(0 <= special && special < S_LAST)) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND,
                _("Cannot modify specials for the tile (%d, %d) because "
                  "%d is not a valid terrain special id."), x, y, special);
    return;
  }
  
  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    if (remove) {
      if ((changed = tile_has_special(ptile, special))) {
        tile_remove_special(ptile, special);
      }
    } else {
      if ((changed = !tile_has_special(ptile, special))) {
        tile_add_special(ptile, special);
      }
    }

    if (changed) {
      update_tile_knowledge(ptile);
    }
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Handle a request to change the military base at one or more than one tile.
****************************************************************************/
void handle_edit_tile_base(struct connection *pc, int x, int y,
                           Base_type_id id, bool remove, int size)
{
  struct tile *ptile_center;
  struct base_type *pbase;
  bool changed = FALSE;
  
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit the tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  pbase = base_by_number(id);

  if (!pbase) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND,
                _("Cannot modify base for the tile (%d, %d) because "
                  "%d is not a valid base type id."), x, y, id);
    return;
  }
  
  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    if (remove) {
      if ((changed = tile_has_base(ptile, pbase))) {
        tile_remove_base(ptile, pbase);
      }
    } else {
      if ((changed = !tile_has_base(ptile, pbase))) {
        tile_add_base(ptile, pbase);
      }
    }

    if (changed) {
      update_tile_knowledge(ptile);
    }
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Handles tile information from the client, to make edits to tiles.
****************************************************************************/
void handle_edit_tile(struct connection *pc,
                      struct packet_edit_tile *packet)
{
  struct tile *ptile;
  int id;
  bool changed = FALSE;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  id = packet->id;
  ptile = index_to_tile(id);

  if (!ptile) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("No such tile (ID %d)."), id);
    return;
  }

  /* Handle changes in specials. */
  if (!BV_ARE_EQUAL(packet->specials, ptile->special)) {
    tile_special_type_iterate(spe) {
      if (BV_ISSET(packet->specials, spe)) {
        tile_add_special(ptile, spe);
      } else {
        tile_remove_special(ptile, spe);
      }
    } tile_special_type_iterate_end;
    changed = TRUE;
  }


  /* TODO: Handle more property edits. */


  /* Send the new state to all affected. */
  if (changed) {
    update_tile_knowledge(ptile);
    send_tile_info(NULL, ptile, FALSE);
  }
}

/****************************************************************************
  Handle a request to create 'count' units of type 'utid' at the tile given
  by the x, y coordinates and owned by player with number 'owner'.
****************************************************************************/
void handle_edit_unit_create(struct connection *pc, int owner,
                             int x, int y, Unit_type_id utid, int count)
{
  struct tile *ptile;
  struct unit_type *punittype;
  struct player *pplayer;
  struct city *homecity;
  struct unit *punit;
  bool coastal;
  int id, i;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile = map_pos_to_tile(x, y);
  if (!ptile) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot create units at tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  punittype = utype_by_number(utid);
  if (!punittype) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot create a unit at (%d, %d) because the "
                  "given unit type id %d is invalid."), x, y, utid);
    return;
  }

  pplayer = valid_player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot create a unit of type %s at (%d, %d) "
                  "because the given owner's player id %d is "
                  "invalid."), utype_name_translation(punittype),
                x, y, owner);
    return;
  }

  if (is_non_allied_unit_tile(ptile, pplayer)
      || (tile_city(ptile)
          && !pplayers_allied(pplayer, tile_owner(ptile)))) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot create unit of type %s on enemy tile "
                  "(%d, %d)."), utype_name_translation(punittype),
                x, y);
    return;
  }

  if (!can_exist_at_tile(punittype, ptile)) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot create a unit of type %s on the terrain "
                  "at (%d, %d)."),
                utype_name_translation(punittype), x, y);
    return;
  }

  if (count > 0 && !pplayer->is_alive) {
    pplayer->is_alive = TRUE;
    send_player_info(pplayer, NULL);
  }

  /* FIXME: Make this more general? */
  coastal = is_sailing_unittype(punittype);

  homecity = find_closest_owned_city(pplayer, ptile, coastal, NULL);
  id = homecity ? homecity->id : 0;

  conn_list_do_buffer(game.est_connections);
  for (i = 0; i < count; i++) {
    /* As far as I can see create_unit is guaranteed to
     * never return NULL. */
    punit = create_unit(pplayer, ptile, punittype, 0, id, -1);
  }
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Remove 'count' units of type 'utid' owned by player number 'owner' at
  tile (x, y).
****************************************************************************/
void handle_edit_unit_remove(struct connection *pc, int owner,
                             int x, int y, Unit_type_id utid, int count)
{
  struct tile *ptile;
  struct unit_type *punittype;
  struct player *pplayer;
  int i;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile = map_pos_to_tile(x, y);
  if (!ptile) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot remove units at tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  punittype = utype_by_number(utid);
  if (!punittype) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot remove a unit at (%d, %d) because the "
                  "given unit type id %d is invalid."), x, y, utid);
    return;
  }

  pplayer = valid_player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot remove a unit of type %s at (%d, %d) "
                  "because the given owner's player id %d is "
                  "invalid."), utype_name_translation(punittype),
                x, y, owner);
    return;
  }

  i = 0;
  unit_list_iterate_safe(ptile->units, punit) {
    if (i >= count) {
      break;
    }
    if (unit_type(punit) != punittype
        || unit_owner(punit) != pplayer) {
      continue;
    }
    wipe_unit(punit);
    i++;
  } unit_list_iterate_safe_end;
}


/****************************************************************************
  Handles unit information from the client, to make edits to units.
****************************************************************************/
void handle_edit_unit(struct connection *pc,
                      struct packet_edit_unit *packet)
{
  struct tile *ptile;
  struct unit_type *punittype;
  struct player *pplayer;
  struct unit *punit;
  int id;
  bool changed = FALSE;
  int moves_left;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  id = packet->id;
  punit = game_find_unit_by_number(id);
  if (!punit) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("No such unit (ID %d)."), id);
    return;
  }

  ptile = unit_tile(punit);
  punittype = unit_type(punit);
  pplayer = unit_owner(punit);

  /* Handle a change in the number of moves left. */
  moves_left = CLIP(0, packet->moves_left, punittype->move_rate);
  if (moves_left != punit->moves_left) {
    punit->moves_left = moves_left;
    changed = TRUE;
  }


  /* TODO: Handle more property edits. */


  /* Send the new state to all affected. */
  if (changed) {
    send_unit_info(NULL, punit);
  }
}

/****************************************************************************
  Allows the editing client to create a city at the given position and
  of size 'size'.
****************************************************************************/
void handle_edit_city_create(struct connection *pc,
			     int owner, int x, int y, int size)
{
  struct tile *ptile;
  struct city *pcity;
  struct player *pplayer;
  
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile = map_pos_to_tile(x, y);
  if (!ptile) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot create a city at (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  pplayer = player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("Cannot create a city at (%d, %d) because the "
                  "given owner's player id %d is invalid"), x, y,
                owner);
    return;

  }


  if (!city_can_be_built_here(ptile, NULL)) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("A city may not be built at (%d, %d)."), x, y);
    return;
  }

  if (!pplayer->is_alive) {
    pplayer->is_alive = TRUE;
    send_player_info(pplayer, NULL);
  }

  conn_list_do_buffer(game.est_connections);
  map_show_tile(pplayer, ptile);
  create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile));
  pcity = tile_city(ptile);

  if (size > 1) {
    /* FIXME: Slow and inefficient for large size changes. */
    city_change_size(pcity, CLIP(1, size, MAX_CITY_SIZE));
    send_city_info(NULL, pcity);
  }
  conn_list_do_unbuffer(game.est_connections);
}


/****************************************************************************
  Handle a request to change the internal state of a city.
****************************************************************************/
void handle_edit_city(struct connection *pc,
                      struct packet_edit_city *packet)
{
  struct tile *ptile;
  struct city *pcity, *oldcity;
  struct player *pplayer;
  char buf[1024];
  int id;
  bool changed = FALSE;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  pcity = game_find_city_by_number(packet->id);
  if (!pcity) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit city with invalid city ID %d."),
                packet->id);
    return;
  }

  pplayer = city_owner(pcity);
  ptile = city_tile(pcity);

  /* Handle name change. */
  if (0 != strcmp(pcity->name, packet->name)) {
    if (!is_allowed_city_name(pplayer, packet->name, buf, sizeof(buf))) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND,
                  _("Cannot edit city name: %s"), buf);
    } else {
      sz_strlcpy(pcity->name, packet->name);
      changed = TRUE;
    }
  }

  /* Handle size change. */
  if (packet->size != pcity->size) {
    if (!(0 < packet->size && packet->size <= MAX_CITY_SIZE)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND,
                  _("Invalid city size %d for city %s."),
                  packet->size, pcity->name);
    } else {
      /* FIXME: Slow and inefficient for large size changes. */
      city_change_size(pcity, packet->size);
      changed = TRUE;
    }
  }

  /* Handle city improvement changes. */
  improvement_iterate(pimprove) {
    oldcity = NULL;
    id = improvement_number(pimprove);

    if (is_special_improvement(pimprove)) {
      if (packet->built[id] >= 0) {
        notify_conn(pc->self, ptile, E_BAD_COMMAND,
                    _("It is impossible for a city to have %s!"),
                    improvement_name_translation(pimprove));
      }
      continue;
    }

    /* FIXME: game.info.great_wonders and pplayer->small_wonders
     * logic duplication with city_build_building. */

    if (city_has_building(pcity, pimprove) && packet->built[id] < 0) {

      if (is_great_wonder(pimprove)) {
        game.info.great_wonders[id] = 0;
      } else if (is_small_wonder(pimprove)) {
        pplayer->small_wonders[id] = 0;
      }
      city_remove_improvement(pcity, pimprove);
      changed = TRUE;

    } else if (!city_has_building(pcity, pimprove)
               && packet->built[id] >= 0) {

      if (is_great_wonder(pimprove)) {
        oldcity = game_find_city_by_number(game.info.great_wonders[id]);
        game.info.great_wonders[id] = pcity->id;
      } else if (is_small_wonder(pimprove)) {
        oldcity = player_find_city_by_id(pplayer,
                                         pplayer->small_wonders[id]);
        pplayer->small_wonders[id] = pcity->id;
      }

      if (oldcity) {
        city_remove_improvement(oldcity, pimprove);
        city_refresh_queue_add(oldcity);
      }

      city_add_improvement(pcity, pimprove);
      changed = TRUE;
    }
  } improvement_iterate_end;
 
  /* Handle food stock change. */
  if (packet->food_stock != pcity->food_stock) {
    int max = city_granary_size(pcity->size);
    if (!(0 <= packet->food_stock && packet->food_stock <= max)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND,
                  _("Invalid city food stock amount %d for city %s "
                    "(allowed range is %d to %d)."),
                  packet->food_stock, pcity->name, 0, max);
    } else {
      pcity->food_stock = packet->food_stock;
      changed = TRUE;
    }
  }

  /* Handle shield stock change. */
  if (packet->shield_stock != pcity->shield_stock) {
    int max = USHRT_MAX; /* Limited to uint16 by city info packet. */
    if (!(0 <= packet->shield_stock && packet->shield_stock <= max)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND,
                  _("Invalid city shield stock amount %d for city %s "
                    "(allowed range is %d to %d)."),
                  packet->shield_stock, pcity->name, 0, max);
    } else {
      pcity->shield_stock = packet->shield_stock;
      changed = TRUE;
    }
  }

  /* TODO: Handle more property edits. */


  if (changed) {
    city_refresh_queue_add(pcity);
    conn_list_do_buffer(game.est_connections);
    city_refresh_queue_processing();

    /* FIXME: city_refresh_queue_processing only sends to city owner? */
    send_city_info(NULL, pcity);  

    conn_list_do_unbuffer(game.est_connections);
  }
}

/**************************************************************************
  Handle editing of any or all player properties.
***************************************************************************/
void handle_edit_player(struct connection *pc, 
                        struct packet_edit_player *packet)
{
  struct player *pplayer;
  bool changed = FALSE, update_research = FALSE;
  struct nation_type *pnation;
  struct player_research *research;
  enum tech_state known;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  pplayer = valid_player_by_number(packet->id);
  if (!pplayer) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit player with invalid player ID %d."),
                packet->id);
    return;
  }

  research = get_player_research(pplayer);


  /* Handle player name change. */
  if (0 != strcmp(packet->name, player_name(pplayer))) {
    if (packet->name[0] == '\0') {
      notify_conn(pc->self, NULL, E_BAD_COMMAND,
                  _("Cannot set empty name for player (%d) '%s'."),
                  player_number(pplayer), player_name(pplayer));
    } else {
      bool valid = TRUE;

      players_iterate(other_player) {
        if (other_player == pplayer) {
          continue;
        }
        if (0 != mystrcasecmp(player_name(other_player), packet->name)) {
          continue;
        }
        notify_conn(pc->self, NULL, E_BAD_COMMAND,
                    _("Cannot change name of player (%d) '%s' to '%s': "
                      "another player (%d) already has that name."),
                    player_number(pplayer), player_name(pplayer),
                    packet->name, player_number(other_player));
        valid = FALSE;
        break;
      } players_iterate_end;

      if (valid) {
        sz_strlcpy(pplayer->name, packet->name);
        changed = TRUE;
      }
    }
  }

  /* Handle nation change. */
  pnation = nation_by_number(packet->nation);
  if (nation_of_player(pplayer) != pnation) {
    if (pnation == NULL) {
      notify_conn(pc->self, NULL, E_BAD_COMMAND,
                  _("Cannot change nation for player %d (%s) "
                    "because the given nation ID %d is invalid."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation);
    } else if (pnation->player != NULL) {
      notify_conn(pc->self, NULL, E_BAD_COMMAND,
                  _("Cannot change nation for player %d (%s) "
                    "to nation %d (%s) because that nation is "
                    "already assigned to player %d (%s)."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation, nation_plural_translation(pnation),
                  player_number(pnation->player),
                  player_name(pnation->player));
    } else {
      changed = player_set_nation(pplayer, pnation);
    }
  }

  /* Handle a change in known inventions. */
  /* FIXME: Modifies struct player_research directly. */
  advance_index_iterate(A_FIRST, tech) {
    known = player_invention_state(pplayer, tech);
    if ((packet->inventions[tech] && known == TECH_KNOWN)
        || (!packet->inventions[tech] && known != TECH_KNOWN)) {
      continue;
    }
    if (packet->inventions[tech]) {
      /* FIXME: Side-effect modifies game.info.global_advances. */
      player_invention_set(pplayer, tech, TECH_KNOWN);
      research->techs_researched++;
    } else {
      player_invention_set(pplayer, tech, TECH_UNKNOWN);
      research->techs_researched--;
    }
    changed = TRUE;
    update_research = TRUE;
  } advance_index_iterate_end;
  

  /* TODO: Handle more property edits. */


  if (update_research) {
    Tech_type_id current, goal;

    player_research_update(pplayer);

    /* FIXME: Modifies struct player_research directly. */

    current = research->researching;
    goal = research->tech_goal;

    if (current != A_UNSET) {
      known = player_invention_state(pplayer, current);
      if (known != TECH_PREREQS_KNOWN) {
        research->researching = A_UNSET;
      }
    }
    if (goal != A_UNSET) {
      known = player_invention_state(pplayer, goal);
      if (known == TECH_KNOWN) {
        research->tech_goal = A_UNSET;
      }
    }
    changed = TRUE;

    /* Inform everybody about global advances */
    send_game_info(NULL);
  }

  if (changed) {
    send_player_info(pplayer, NULL);
  }
}

/****************************************************************************
  Handles vision editing requests from client.
****************************************************************************/
void handle_edit_player_vision(struct connection *pc, int plr_no,
                               int x, int y, bool known, int size)
{
  struct player *pplayer;
  struct tile *ptile_center;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit vision for the tile at (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  pplayer = valid_player_by_number(plr_no);
  if (!pplayer) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND,
                _("Cannot edit vision for the tile at (%d, %d) because "
                  "given player id %d is invalid."), x, y, plr_no);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    if (known && map_is_known(ptile, pplayer)) {
      continue;
    }

    if (!known) {
      struct city *pcity = tile_city(ptile);
      bool cannot_make_unknown = FALSE;

      if (pcity && city_owner(pcity) == pplayer) {
        continue;
      }

      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) == pplayer
            || really_gives_vision(pplayer, unit_owner(punit))) {
          cannot_make_unknown = TRUE;
          break;
        }
      } unit_list_iterate_end;

      if (cannot_make_unknown) {
        continue;
      }

      /* The client expects tiles which become unseen to
       * contain no units (client/packhand.c +2368).
       * So here we tell it to remove units that do
       * not give it vision. */
      unit_list_iterate(ptile->units, punit) {
        conn_list_iterate(pplayer->connections, pconn) {
          dsend_packet_unit_remove(pconn, punit->id);
        } conn_list_iterate_end;
      } unit_list_iterate_end;
    }

    if (known) {
      map_set_known(ptile, pplayer);
      update_player_tile_knowledge(pplayer, ptile);
    } else {
      map_clear_known(ptile, pplayer);
    }

    send_tile_info(NULL, ptile, TRUE);
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Client editor requests us to recalculate borders. Note that this does
  not necessarily extend borders to their maximum due to the way the
  borders code is written. This may be considered a feature or limitation.
****************************************************************************/
void handle_edit_recalculate_borders(struct connection *pc)
{
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  map_calculate_borders();
}

/****************************************************************************
  Remove any city at the given location.
****************************************************************************/
void handle_edit_city_remove(struct connection *pc, int id)
{
  struct city *pcity;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  pcity = game_find_city_by_number(id);
  if (pcity == NULL) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("No such city (ID %d)."), id);
    return;
  }

  remove_city(pcity);
}

/****************************************************************************
  Run any pending tile checks.
****************************************************************************/
void handle_edit_check_tiles(struct connection *pc)
{
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  check_edited_tile_terrains();
}

/****************************************************************************
  Temporarily remove fog-of-war for the player with player number 'plr_no'.
  This will only stay in effect while the server is in edit mode and the
  connection is editing. Has no effect if fog-of-war is disabled globally.
****************************************************************************/
void handle_edit_toggle_fogofwar(struct connection *pc, int plr_no)
{
  struct player *pplayer;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  if (!game.info.fogofwar) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot toggle fog-of-war when it is already "
                  "disabled."));
    return;
  }

  pplayer = valid_player_by_number(plr_no);
  if (!pplayer) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot toggle fog-of-war for invalid player ID %d."),
                plr_no);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  if (pplayer->editor.fog_of_war_disabled) {
    enable_fog_of_war_player(pplayer);
    pplayer->editor.fog_of_war_disabled = FALSE;
  } else {
    disable_fog_of_war_player(pplayer);
    pplayer->editor.fog_of_war_disabled = TRUE;
  }
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Change the "ownership" of the tile(s) at the given coordinates.
****************************************************************************/
void handle_edit_territory(struct connection *pc, int x, int y, int owner,
                           int size)
{
  struct player *pplayer;
  struct tile *ptile_center;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile_center = map_pos_to_tile(x, y);
  if (!ptile_center) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot edit territory of tile (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  /* NULL is ok; represents "no owner". */
  pplayer = player_by_number(owner);

  conn_list_do_buffer(game.est_connections);
  square_iterate(ptile_center, size - 1, ptile) {
    if (tile_owner(ptile) == pplayer
        || tile_city(ptile) != NULL) {
      continue;
    }
    /* FIXME: This does not play well with border code
     * once edit mode is exited. */
    tile_set_owner(ptile, pplayer);
    send_tile_info(NULL, ptile, FALSE);
  } square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/****************************************************************************
  Set the given position to be the start position for the given nation.
****************************************************************************/
void handle_edit_startpos(struct connection *pc, int x, int y,
                          Nation_type_id nation)
{
  struct tile *ptile;
  bool changed = FALSE;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile = map_pos_to_tile(x, y);
  if (!ptile) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot place a start position at (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  if (nation == -1) {
    changed = ptile->editor.startpos_nation_id != -1;
    ptile->editor.startpos_nation_id = -1;
  } else {
    struct nation_type *pnation;

    pnation = nation_by_number(nation);
    if (pnation) {
      changed = ptile->editor.startpos_nation_id != nation;
      ptile->editor.startpos_nation_id = nation;
    }
  }

  if (changed) {
    send_tile_info(NULL, ptile, FALSE);
  }
}

/****************************************************************************
  Handle edit requests to the main game data structure.
****************************************************************************/
void handle_edit_game(struct connection *pc,
                      struct packet_edit_game *packet)
{
  bool changed = FALSE;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  if (packet->year != game.info.year) {

    /* 'year' is stored in a signed short. */
    const short min_year = -30000, max_year = 30000;

    if (!(min_year <= packet->year && packet->year <= max_year)) {
      notify_conn(pc->self, NULL, E_BAD_COMMAND,
                  _("Cannot set invalid game year %d. Valid year range "
                    "is from %d to %d."),
                  packet->year, min_year, max_year);
    } else {
      game.info.year = packet->year;
      changed = TRUE;
    }
  }

  if (changed) {
    send_game_info(NULL);
  }
}
