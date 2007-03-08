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
#include "terrain.h"
#include "unitlist.h"

#include "citytools.h"
#include "cityturn.h"
#include "gamehand.h"
#include "plrhand.h"
#include "unittools.h"
#include "hand_gen.h"
#include "maphand.h"

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
		_(" *** Server set to edit mode! *** "));
  }
  if (game.info.is_edit_mode && !is_edit_mode) {
    notify_conn(NULL, NULL, E_SETTING,
		_(" *** Server is leaving edit mode. *** "));
  }
  if (!EQ(game.info.is_edit_mode, is_edit_mode)) {
    game.info.is_edit_mode = is_edit_mode;
    send_game_info(NULL);
  }
}

/****************************************************************************
  Handles new tile information from the client, to make local edits to
  the map.
****************************************************************************/
void handle_edit_tile(struct connection *pc, int x, int y,
                      Terrain_type_id terrain, Resource_type_id resource,
		      bv_special special)
{
  struct tile *ptile = map_pos_to_tile(x, y);
  struct terrain *pterrain = get_terrain(terrain), *old_terrain;
  struct resource *presource = get_resource(resource);

  if (!can_conn_edit(pc) || !ptile || !pterrain) {
    return;
  }

  old_terrain = ptile->terrain;

  specials_iterate(s) {
    if (contains_special(special, s) && !tile_has_special(ptile, s)) {
      tile_add_special(ptile, s);
    } else if (!contains_special(special, s) && tile_has_special(ptile, s)) {
      tile_remove_special(ptile, s);
    }
  } specials_iterate_end;

  tile_set_resource(ptile, presource); /* May be NULL. */

  tile_change_terrain(ptile, pterrain);

  /* Handle global side effects. */
  check_terrain_change(ptile, old_terrain);

  /* update playertiles and send updates to the clients */
  update_tile_knowledge(ptile);
  send_tile_info(NULL, ptile, FALSE);
}

/****************************************************************************
  Handles unit information from the client, to make edits to units.

  FIXME: We do some checking of input but not enough.
****************************************************************************/
void handle_edit_unit(struct connection *pc, struct packet_edit_unit *packet)
{
  struct tile *ptile = map_pos_to_tile(packet->x, packet->y);
  struct unit_type *punittype = get_unit_type(packet->type);
  struct player *pplayer = get_player(packet->owner);
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
        || (ptile->city
            && !pplayers_allied(pplayer, city_owner(ptile->city)))) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot create unit on enemy tile."));
      return;
    }
    /* FIXME: should use can_unit_exist_at_tile here. */
    if (!(ptile->city
	  && !(is_sailing_unittype(punittype)
	       && !is_ocean_near_tile(ptile)))
	&& !is_native_tile(punittype, ptile)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot create %s unit on this terrain."),
                    punittype->name);
      return;
    }

    punit = create_unit(pplayer, ptile, punittype,
                        packet->veteran,
			homecity ? homecity->id : 0,
			packet->movesleft);
  } else {
    punit = find_unit_by_id(packet->id);
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
  if (find_unit_by_id(packet->transported_by)) {
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

/****************************************************************************
  Allows the editing client to create a city and the given position.
****************************************************************************/
void handle_edit_create_city(struct connection *pc,
			     int owner, int x, int y)
{
  struct tile *ptile = map_pos_to_tile(x, y);
  struct city *pcity;
  struct player *pplayer = get_player(owner);

  if (!can_conn_edit(pc) || !pplayer || !ptile) {
    return;
  }

  if (!city_can_be_built_here(ptile, NULL)) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
		_("Cannot build city on this tile."));
    return;
  }

  /* Reveal tile to city owner */
  map_show_tile(pplayer, ptile);

  /* new city */
  create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile));
  pcity = tile_get_city(ptile);

  if (!pcity) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND,
		_("Could not create city."));
    return;
  }
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
  struct player *pplayer = get_player(packet->owner);
  int i;
  int old_traderoutes[NUM_TRADEROUTES];

  if (!can_conn_edit(pc) || !pplayer || !ptile) {
    return;
  }

  pcity = tile_get_city(ptile);
  if (!pcity) {
    if (!city_can_be_built_here(ptile, NULL)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND,
                    _("Cannot build city on this tile."));
      return;
    }

    /* new city */
    create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile));
    pcity = tile_get_city(ptile);

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
    struct city *oldcity = find_city_by_id(old_traderoutes[i]);
    struct city *newcity = find_city_by_id(packet->trade[i]);

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

  /* FIXME: Might want to check these values. */
  pcity->changed_from.is_unit = packet->changed_from_is_unit;
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
  struct city *pcity = find_city_by_id(id);

  if (!can_conn_edit(pc) || !pcity) {
    return;
  }

  city_change_size(pcity, CLIP(1, size, MAX_CITY_SIZE));
  send_city_info(NULL, pcity);
#if 0 /* city_change_size already sends notification */
  if (pcity->size != size) {
    notify_conn(pc->self, NULL, E_BAD_COMMAND,
		_("Could not change city size."));
  }
#endif
}

/**************************************************************************
 right now there are no checks whatsoever in the server. beware.
***************************************************************************/
void handle_edit_player(struct connection *pc, 
                        struct packet_edit_player *packet)
{
  struct player *pplayer = get_player(packet->playerno);
  struct nation_type *pnation = get_nation_by_idx(packet->nation);
  struct team *pteam = team_get_by_id(packet->team);
  struct government *pgov = get_government(packet->government);
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
  pplayer->target_government = get_government(packet->target_government);

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
    pplayer->diplstates[i].contact_turns_left
      = packet->diplstates[i].contact_turns_left;
    pplayer->diplstates[i].has_reason_to_cancel
      = packet->diplstates[i].has_reason_to_cancel;
  }

  for (i = 0; i < B_LAST/*game.num_impr_types*/; i++) {
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
  Handles vision editing requests from client
****************************************************************************/
void handle_edit_vision(struct connection *pc, int plr_no, int x, int y,
                        int mode)
{
  struct player *pplayer = get_player(plr_no);
  struct tile *ptile = map_pos_to_tile(x, y);
  bool remove_knowledge = FALSE;

  if (!can_conn_edit(pc) || !pplayer || !ptile) {
    return;
  }

  if (mode == EVISION_REMOVE
      || (mode == EVISION_TOGGLE && map_is_known(ptile, pplayer))) {
    remove_knowledge = TRUE;
  }

  if (remove_knowledge) {
    struct city *pcity = tile_get_city(ptile);

    if (pcity && pcity->owner == pplayer) {
      notify_conn(pc->self, NULL, E_BAD_COMMAND,
                  _("Cannot remove knowledde about own city."));
      return;
    }

    unit_list_iterate(ptile->units, punit) {
      if (punit->owner == pplayer) {
        notify_conn(pc->self, NULL, E_BAD_COMMAND,
                    _("Cannot remove knowledde about own unit."));
        return;
      }
    } unit_list_iterate_end;
  }

  if (!remove_knowledge) {
    map_set_known(ptile, pplayer);
    update_player_tile_knowledge(pplayer, ptile);
  } else {
    map_clear_known(ptile, pplayer);
  }

  send_tile_info(pplayer->connections, ptile, TRUE);
}

/****************************************************************************
  Client editor requests us to recalculate borders. Note that this does
  not necessarily extend borders to their maximum due to the way the
  borders code is written. This may be considered a feature or limitation.
****************************************************************************/
void handle_edit_recalculate_borders(struct connection *pc)
{
  if (can_conn_edit(pc)) {
    map_calculate_borders();
  }
}
