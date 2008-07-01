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

/* The number of tiles that we need deferred checking
 * after their terrains have been edited. */
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
  Handles new tile information from the client, to make local edits to
  the map.
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
  if (!EQ(game.info.is_edit_mode, is_edit_mode)) {
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
  Handle a request to change one or more tiles' specials. The 'remove'
  argument controls whether to remove or add the given special of type
  'special' from the tile.
****************************************************************************/
void handle_edit_tile_base(struct connection *pc, int x, int y,
                           enum base_type_id id, bool remove, int size)
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
      struct base_type *old_base;

      /* Unfortunately, at the moment only one base
       * type is allowed per tile, so we have to remove
       * any existing other base types before we add
       * a different one. :( */
      if ((old_base = tile_get_base(ptile))
          && old_base != pbase) {
        tile_remove_base(ptile, old_base);
        update_tile_knowledge(ptile);
      }

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
                _("Cannot edit the tile (%d, %d) because "
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

  /* FIXME: Make this more general? */
  coastal = is_sailing_unittype(punittype);

  homecity = find_closest_owned_city(pplayer, ptile, coastal, NULL);
  id = homecity ? homecity->id : 0;

  conn_list_do_buffer(game.est_connections);
  for (i = 0; i < count; i++) {
    /* As far as I can see create_unit is guaranteed to
     * never returns NULL. */
    punit = create_unit(pplayer, ptile, punittype, 0, id, -1);
  }
  update_tile_knowledge(ptile);
  conn_list_do_unbuffer(game.est_connections);
}

#if 0
/****************************************************************************
  Handles unit information from the client, to make edits to units.

  FIXME: We do some checking of input but not enough.
****************************************************************************/
void handle_edit_unit(struct connection *pc, struct packet_edit_unit *packet)
{
  struct tile *ptile = map_pos_to_tile(packet->x, packet->y);
  struct unit_type *punittype = utype_by_number(packet->type);
  struct player *pplayer = player_by_number(packet->owner);
  struct unit *punit;

  if (!can_conn_edit(pc)
      || !ptile || !punittype || !pplayer
      || (packet->create_new && packet->delete)) {
    return;
  }

  /* check if a unit with this id already exists on this tile, if
   * so, then this is an edit, otherwise, we create a new unit */
  if (packet->create_new) {
    struct city *homecity
      = player_find_city_by_id(pplayer, packet->homecity);

    if (is_non_allied_unit_tile(ptile, pplayer)
        || (tile_city(ptile)
            && !pplayers_allied(pplayer, tile_owner(ptile)))) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot create unit on enemy tile."));
      return;
    }
    /* FIXME: should use can_unit_exist_at_tile here. */
    if (!(tile_city(ptile)
	  && !(is_sailing_unittype(punittype)
	       && !is_ocean_near_tile(ptile)))
	&& !is_native_tile(punittype, ptile)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot create %s unit on this terrain."),
                    utype_name_translation(punittype));
      return;
    }

    punit = create_unit(pplayer, ptile, punittype,
                        packet->veteran,
			homecity ? homecity->id : 0,
			packet->movesleft);
  } else {
    punit = game_find_unit_by_number(packet->id);
    if (!punit) {
      freelog(LOG_ERROR, "can't find unit to edit!");
      return;
    } 
  }

  if (packet->delete) {
    wipe_unit(punit);
    return;
  }

  punit->hp = CLIP(0, packet->hp, punittype->hp);
  punit->activity_count = packet->activity_count;
  punit->fuel = CLIP(0, packet->fuel, punittype->fuel);
  punit->paradropped = BOOL_VAL(packet->paradropped);
  if (game_find_unit_by_number(packet->transported_by)) {
    punit->transported_by = packet->transported_by;
  } else {
    punit->transported_by = -1;
  }

  /* FIXME: resolve_unit_stacks? */
  /* FIXME: refresh homecity? */

  /* update playertiles and send updates to the clients */
  update_tile_knowledge(ptile);
  send_unit_info(NULL, punit);
}
#endif

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

  conn_list_do_buffer(game.est_connections);
  map_show_tile(pplayer, ptile);
  create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile));
  pcity = tile_city(ptile);

  if (size > 1) {
    city_change_size(pcity, CLIP(1, size, MAX_CITY_SIZE));
    send_city_info(NULL, pcity);
  }
  conn_list_do_unbuffer(game.est_connections);
}

#if 0
/****************************************************************************
  We do some checking of input but not enough.

  This function is deprecated and should be replaced by individual edit
  packets.  However this is incomplete so the code hasn't been cut out
  yet.
****************************************************************************/
void handle_edit_city(struct connection *pc, struct packet_edit_city *packet)
{
  struct tile *ptile = map_pos_to_tile(packet->x, packet->y);
  struct city *pcity;
  struct player *pplayer = player_by_number(packet->owner);
  int i;
  int old_traderoutes[NUM_TRADEROUTES];

  if (!can_conn_edit(pc) || !pplayer || !ptile) {
    return;
  }

  pcity = tile_city(ptile);
  if (!pcity) {
    if (!city_can_be_built_here(ptile, NULL)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot build city on this tile."));
      return;
    }

    /* new city */
    create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile));
    pcity = tile_city(ptile);

    if (!pcity) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
		    _("Could not create city."));
      return;
    }
  }

  if (!city_change_size(pcity, CLIP(0, packet->size, MAX_CITY_SIZE))) {
    /* City died. */
    return;
  }

  /* FIXME: should probably be a different packet_edit_trade_route. */
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    old_traderoutes[i] = pcity->trade[i];
  }
  for (i = 0; i < NUM_TRADEROUTES; i++) {
    struct city *oldcity = game_find_city_by_number(old_traderoutes[i]);
    struct city *newcity = game_find_city_by_number(packet->trade[i]);

    /*
     * This complicated bit of logic either deletes or creates trade routes.
     *
     * FIXME: What happens if (oldcity && newcity && oldcity != newcity) ?
     */
    if (oldcity && !newcity) {
      remove_trade_route(pcity, oldcity);
    } else if (newcity && !oldcity && can_cities_trade(pcity, newcity)) {
      establish_trade_route(pcity, newcity);
    }
  }

  pcity->food_stock = MAX(packet->food_stock, 0);
  pcity->shield_stock = MAX(packet->shield_stock, 0);

  pcity->did_buy = packet->did_buy;
  pcity->did_sell = packet->did_sell;
  pcity->was_happy = packet->was_happy;
  pcity->airlift = packet->airlift;

  pcity->turn_last_built = CLIP(0, packet->turn_last_built, game.info.turn);
  pcity->turn_founded = CLIP(0, packet->turn_founded, game.info.turn);
  pcity->before_change_shields = MAX(packet->before_change_shields, 0);
  pcity->disbanded_shields = MAX(packet->disbanded_shields, 0);
  pcity->caravan_shields = MAX(packet->caravan_shields, 0);
  pcity->last_turns_shield_surplus
    = MAX(packet->last_turns_shield_surplus, 0);

  /* FIXME: check these values! */
  pcity->changed_from.kind = packet->changed_from_is_unit ? VUT_UTYPE ? VUT_IMPROVEMENT;
  pcity->changed_from.value = packet->changed_from_id;

  /* make everything sane.  Note some refreshes may already have been
   * done above. */
  city_refresh(pcity);

  /* send update back to client */
  send_city_info(NULL, pcity);  
}
#endif

/****************************************************************************
  Allows the editor to change city size directly.
****************************************************************************/
void handle_edit_city_size(struct connection *pc,
			   int id, int size)
{
  struct city *pcity;
  
  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  pcity = game_find_city_by_number(id);

  if (!pcity) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot change size of unknown city with id %d."),
                id);
    return;
  }

  city_change_size(pcity, CLIP(1, size, MAX_CITY_SIZE));
  send_city_info(NULL, pcity);
}

/**************************************************************************
 right now there are no checks whatsoever in the server. beware.
***************************************************************************/
void handle_edit_player(struct connection *pc, 
                        struct packet_edit_player *packet)
{
  struct player *pplayer = player_by_number(packet->playerno);
  struct nation_type *pnation = nation_by_number(packet->nation);
  struct team *pteam = team_by_number(packet->team);
  struct government *pgov = government_by_number(packet->government);
#if 0 /* Unused: see below */
  struct player_research *research;
  int i;
#endif

  /* FIXME: Are NULL teams allowed? */
  if (!can_conn_edit(pc)
      || !pplayer || !pnation || !pteam || !pgov) {
    return;
  }

  sz_strlcpy(pplayer->name, packet->name);
  sz_strlcpy(pplayer->username, packet->username);

  pplayer->nation = pnation;
  pplayer->is_male = packet->is_male;
  pplayer->team = pteam;

  pplayer->economic.gold = MAX(packet->gold, 0);
  pplayer->economic.tax = CLIP(0, packet->tax, 100);
  pplayer->economic.science = CLIP(0, packet->science, 100 - packet->tax);
  pplayer->economic.luxury = 100 - packet->tax - packet->science;

  pplayer->government = pgov;
  pplayer->target_government = government_by_number(packet->target_government);

#if 0 /* FIXME: These values need to be checked */
  pplayer->embassy = packet->embassy;
  pplayer->gives_shared_vision = packet->gives_shared_vision;
  pplayer->city_style = packet->city_style;
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->ai.love[i] = packet->love[i];
  }

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->diplstates[i].type = packet->diplstates[i].type;
    pplayer->diplstates[i].turns_left = packet->diplstates[i].turns_left;
    pplayer->diplstates[i].has_reason_to_cancel
      = packet->diplstates[i].has_reason_to_cancel;
  }

  for (i = 0; i < B_LAST/*improvement_count()*/; i++) {
     pplayer->small_wonders[i] = packet->small_wonders[i];
  }
  
  /* FIXME: What's an AI value doing being set here? */
  pplayer->ai.science_cost = packet->science_cost;

  pplayer->bulbs_last_turn = packet->bulbs_last_turn;

  research = get_player_research(pplayer);
  research->bulbs_researched = packet->bulbs_researched;
  research->techs_researched = packet->techs_researched;
  research->researching = packet->researching;
  research->future_tech = packet->future_tech;
  research->tech_goal = packet->tech_goal;

  pplayer->is_alive = packet->is_alive;
  pplayer->ai.barbarian_type = packet->barbarian_type;
  pplayer->revolution_finishes = packet->revolution_finishes;
  pplayer->ai.control = packet->ai;
#endif

  /* TODO: and probably a bunch more stuff here */

  /* send update back to client */
  send_player_info(NULL, pplayer);  
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
  Edit techs known by player
****************************************************************************/
void handle_edit_player_tech(struct connection *pc,
                             int playerno, Tech_type_id tech,
                             enum editor_tech_mode mode)
{
  struct player *pplayer = player_by_number(playerno);
  struct player_research *research;

  if (!can_conn_edit(pc) || !pplayer || !valid_advance_by_number(tech)) {
    return;
  }

  research = get_player_research(pplayer);

  switch(mode) {
   case ETECH_ADD:
     player_invention_set(pplayer, tech, TECH_KNOWN);
     research->techs_researched++;
     break;
   case ETECH_REMOVE:
     player_invention_set(pplayer, tech, TECH_UNKNOWN);
     research->techs_researched--;
     break;
   case ETECH_TOGGLE:
     if (player_invention_state(pplayer, tech) == TECH_KNOWN) {
       player_invention_set(pplayer, tech, TECH_UNKNOWN);
       research->techs_researched--;
     } else {
       player_invention_set(pplayer, tech, TECH_KNOWN);
       research->techs_researched++;
     }
     break;
   default:
     break;
  }

  player_research_update(pplayer);

  if (research->researching != A_UNSET
      && player_invention_state(pplayer, research->researching) !=
           TECH_PREREQS_KNOWN) {
    research->researching = A_UNSET;
  }
  if (research->tech_goal != A_UNSET
      && player_invention_state(pplayer, research->tech_goal) == TECH_KNOWN) {
    research->tech_goal = A_UNSET;
  }

  /* Inform everybody about global advances */
  send_game_info(NULL);

  /* send update back to client */
  send_player_info(NULL, pplayer);
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
void handle_edit_city_remove(struct connection *pc, int x, int y)
{
  struct tile *ptile;
  struct city *pcity;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  ptile = map_pos_to_tile(x, y);
  if (ptile == NULL) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot remove a city at (%d, %d) because "
                  "it is not on the map!"), x, y);
    return;
  }

  pcity = tile_city(ptile);
  if (pcity == NULL) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
                _("There is no city to remove at (%d, %d)."), x, y);
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
  Remove a unit with the given id.
****************************************************************************/
void handle_edit_unit_remove(struct connection *pc, int id)
{
  struct unit *punit;

  if (!can_conn_edit(pc)) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("You are not allowed to edit."));
    return;
  }

  punit = game_find_unit_by_number(id);
  if (!punit) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
                _("Cannot remove unit with unknown id %d."), id);
    return;
  }

  wipe_unit(punit);
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
                _("Cannot toggle fog-of-war for invalid player id %d."),
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
    /* XXX This does not play well with border code
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
