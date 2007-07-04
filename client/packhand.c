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
#include <string.h>

#include "capability.h"
#include "capstr.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "spaceship.h"
#include "support.h"
#include "unit.h"
#include "unitlist.h"
#include "worklist.h"

#include "agents.h"
#include "attribute.h"
#include "audio.h"
#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "clinet.h"		/* aconnection */
#include "connectdlg_common.h"
#include "connectdlg_g.h"
#include "control.h"
#include "dialogs_g.h"
#include "ggzclient.h"
#include "goto.h"               /* client_goto_init() */
#include "graphics_g.h"
#include "gui_main_g.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "inteldlg_g.h"
#include "mapctrl_g.h"		/* popup_newcity_dialog() */
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "overview_common.h"
#include "pages_g.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"
#include "spaceshipdlg_g.h"
#include "tilespec.h"
#include "wldlg_g.h"

#include "packhand.h"

static void handle_city_packet_common(struct city *pcity, bool is_new,
                                      bool popup, bool investigate);
static bool handle_unit_packet_common(struct unit *packet_unit);
static int *reports_thaw_requests = NULL;
static int reports_thaw_requests_size = 0;

/**************************************************************************
  Unpackage the unit information into a newly allocated unit structure.
**************************************************************************/
static struct unit * unpackage_unit(struct packet_unit_info *packet)
{
  struct unit *punit = create_unit_virtual(get_player(packet->owner), NULL,
					   utype_by_number(packet->type),
					   packet->veteran);

  /* Owner, veteran, and type fields are already filled in by
   * create_unit_virtual. */
  punit->id = packet->id;
  punit->tile = map_pos_to_tile(packet->x, packet->y);
  punit->homecity = packet->homecity;
  punit->moves_left = packet->movesleft;
  punit->hp = packet->hp;
  punit->activity = packet->activity;
  punit->activity_count = packet->activity_count;
  punit->ai.control = packet->ai;
  punit->fuel = packet->fuel;
  if (is_normal_map_pos(packet->goto_dest_x, packet->goto_dest_y)) {
    punit->goto_tile = map_pos_to_tile(packet->goto_dest_x,
				       packet->goto_dest_y);
  } else {
    punit->goto_tile = NULL;
  }
  punit->activity_target = packet->activity_target;
  punit->activity_base = packet->activity_base;
  punit->paradropped = packet->paradropped;
  punit->done_moving = packet->done_moving;
  punit->occupy = packet->occupy;
  if (packet->transported) {
    punit->transported_by = packet->transported_by;
  } else {
    punit->transported_by = -1;
  }
  punit->battlegroup = packet->battlegroup;
  punit->has_orders = packet->has_orders;
  punit->orders.length = packet->orders_length;
  punit->orders.index = packet->orders_index;
  punit->orders.repeat = packet->orders_repeat;
  punit->orders.vigilant = packet->orders_vigilant;
  if (punit->has_orders) {
    int i;

    punit->orders.list
      = fc_malloc(punit->orders.length * sizeof(*punit->orders.list));
    for (i = 0; i < punit->orders.length; i++) {
      punit->orders.list[i].order = packet->orders[i];
      punit->orders.list[i].dir = packet->orders_dirs[i];
      punit->orders.list[i].activity = packet->orders_activities[i];
      punit->orders.list[i].base = packet->orders_bases[i];
    }
  }
  return punit;
}

/**************************************************************************
  Unpackage a short_unit_info packet.  This extracts a limited amount of
  information about the unit, and is sent for units we shouldn't know
  everything about (like our enemies' units).
**************************************************************************/
static struct unit *unpackage_short_unit(struct packet_unit_short_info *packet)
{
  struct unit *punit = create_unit_virtual(get_player(packet->owner), NULL,
					   utype_by_number(packet->type),
					   FALSE);

  /* Owner and type fields are already filled in by create_unit_virtual. */
  punit->id = packet->id;
  punit->tile = map_pos_to_tile(packet->x, packet->y);
  punit->veteran = packet->veteran;
  punit->hp = packet->hp;
  punit->activity = packet->activity;
  punit->activity_base = packet->activity_base;
  punit->occupy = (packet->occupied ? 1 : 0);
  if (packet->transported) {
    punit->transported_by = packet->transported_by;
  } else {
    punit->transported_by = -1;
  }

  return punit;
}

/****************************************************************************
  After we send a join packet to the server we receive a reply.  This
  function handles the reply.
****************************************************************************/
void handle_server_join_reply(bool you_can_join, char *message,
                              char *capability, char *challenge_file,
                              int conn_id)
{
  char msg[MAX_LEN_MSG];
  char *s_capability = aconnection.capability;

  sz_strlcpy(aconnection.capability, capability);
  close_connection_dialog();

  if (you_can_join) {
    freelog(LOG_VERBOSE, "join game accept:%s", message);
    aconnection.established = TRUE;
    aconnection.id = conn_id;
    agents_game_joined();
    update_menus();
    
    
    if (get_client_page() == PAGE_MAIN
	|| get_client_page() == PAGE_NETWORK
	|| get_client_page() == PAGE_GGZ) {
      set_client_page(PAGE_START);
    }

    /* we could always use hack, verify we're local */ 
    send_client_wants_hack(challenge_file);
  } else {
    my_snprintf(msg, sizeof(msg),
		_("You were rejected from the game: %s"), message);
    append_output_window(msg);
    aconnection.id = 0;
    if (auto_connect) {
      freelog(LOG_NORMAL, "%s", msg);
    }
    gui_server_connect();
    if (!with_ggz) {
      set_client_page(in_ggz ? PAGE_MAIN : PAGE_GGZ);
    }
  }
  if (strcmp(s_capability, our_capability) == 0) {
    return;
  }
  my_snprintf(msg, sizeof(msg),
	      _("Client capability string: %s"), our_capability);
  append_output_window(msg);
  my_snprintf(msg, sizeof(msg),
	      _("Server capability string: %s"), s_capability);
  append_output_window(msg);
}

/****************************************************************************
  Handles a remove-city packet, used by the server to tell us any time a
  city is no longer there.
****************************************************************************/
void handle_city_remove(int city_id)
{
  struct city *pcity = find_city_by_id(city_id);
  struct tile *ptile;

  if (!pcity)
    return;

  agents_city_remove(pcity);

  ptile = pcity->tile;
  client_remove_city(pcity);

  /* update menus if the focus unit is on the tile. */
  if (get_num_units_in_focus() > 0) {
    update_menus();
  }
}

/**************************************************************************
  Handle a remove-unit packet, sent by the server to tell us any time a
  unit is no longer there.
**************************************************************************/
void handle_unit_remove(int unit_id)
{
  struct unit *punit = find_unit_by_id(unit_id);
  struct player *powner;

  if (!punit) {
    return;
  }
  
  /* Close diplomat dialog if the diplomat is lost */
  if (diplomat_handled_in_diplomat_dialog() == punit->id) {
    close_diplomat_dialog();
    /* Open another diplomat dialog if there are other diplomats waiting */
    process_diplomat_arrival(NULL, 0);
  }

  powner = unit_owner(punit);

  agents_unit_remove(punit);
  client_remove_unit(punit);

  if (powner == game.player_ptr) {
    activeunits_report_dialog_update();
  }
}

/****************************************************************************
  The tile (x,y) has been nuked!
****************************************************************************/
void handle_nuke_tile_info(int x, int y)
{
  put_nuke_mushroom_pixmaps(map_pos_to_tile(x, y));
}

/****************************************************************************
  A combat packet.  The server tells us the attacker and defender as well
  as both of their hitpoints after the combat is over (in most combat, one
  unit always dies and their HP drops to zero).  If make_winner_veteran is
  set then the surviving unit becomes veteran.
****************************************************************************/
void handle_unit_combat_info(int attacker_unit_id, int defender_unit_id,
			     int attacker_hp, int defender_hp,
			     bool make_winner_veteran)
{
  bool show_combat = FALSE;
  struct unit *punit0 = find_unit_by_id(attacker_unit_id);
  struct unit *punit1 = find_unit_by_id(defender_unit_id);

  if (punit0 && punit1) {
    if (tile_visible_mapcanvas(punit0->tile) &&
	tile_visible_mapcanvas(punit1->tile)) {
      show_combat = TRUE;
    } else if (auto_center_on_combat) {
      if (punit0->owner == game.player_ptr)
	center_tile_mapcanvas(punit0->tile);
      else
	center_tile_mapcanvas(punit1->tile);
      show_combat = TRUE;
    }

    if (show_combat) {
      int hp0 = attacker_hp, hp1 = defender_hp;

      audio_play_sound(unit_type(punit0)->sound_fight,
		       unit_type(punit0)->sound_fight_alt);
      audio_play_sound(unit_type(punit1)->sound_fight,
		       unit_type(punit1)->sound_fight_alt);

      if (do_combat_animation) {
	decrease_unit_hp_smooth(punit0, hp0, punit1, hp1);
      } else {
	punit0->hp = hp0;
	punit1->hp = hp1;

	set_units_in_combat(NULL, NULL);
	refresh_unit_mapcanvas(punit0, punit0->tile, TRUE, FALSE);
	refresh_unit_mapcanvas(punit1, punit1->tile, TRUE, FALSE);
      }
    }
    if (make_winner_veteran) {
      struct unit *pwinner = (defender_hp == 0 ? punit0 : punit1);

      pwinner->veteran++;
      refresh_unit_mapcanvas(pwinner, pwinner->tile, TRUE, FALSE);
    }
  }
}

/**************************************************************************
  Updates a city's list of improvements from packet data. "impr" identifies
  the improvement, and "have_impr" specifies whether the improvement should
  be added (TRUE) or removed (FALSE).
**************************************************************************/
static void update_improvement_from_packet(struct city *pcity,
					   Impr_type_id impr, bool have_impr)
{
  if (have_impr) {
    if (pcity->improvements[impr] == I_NONE) {
      city_add_improvement(pcity, impr);
    }
  } else {
    if (pcity->improvements[impr] != I_NONE) {
      city_remove_improvement(pcity, impr);
    }
  }
}

/****************************************************************************
  Handles a game-state packet from the server.  The server sends these to
  us regularly to inform the client of state changes.
****************************************************************************/
void handle_game_state(int value)
{
  bool changed = (get_client_state() != value);

  if (get_client_state() == CLIENT_PRE_GAME_STATE
      && value == CLIENT_GAME_RUNNING_STATE) {
    popdown_races_dialog();
  }
  
  set_client_state(value);

  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    refresh_overview_canvas();

    update_info_label();	/* get initial population right */
    update_unit_focus();
    update_unit_info_label(get_units_in_focus());

    /* Find something sensible to display instead of the intro gfx. */
    center_on_something();
    
    free_intro_radar_sprites();
    agents_game_start();
  }

  if (get_client_state() == CLIENT_GAME_OVER_STATE) {
    refresh_overview_canvas();

    update_info_label();
    update_unit_focus();
    update_unit_info_label(NULL); 
  }

  if (changed && can_client_change_view()) {
    update_map_canvas_visible();
  }
}

/****************************************************************************
  A city-info packet contains all information about a city.  If we receive
  this packet then we know everything about the city internals.
****************************************************************************/
void handle_city_info(struct packet_city_info *packet)
{
  int i;
  bool city_is_new, city_has_changed_owner = FALSE;
  bool need_units_dialog_update = FALSE;
  struct city *pcity;
  bool popup, update_descriptions = FALSE, name_changed = FALSE;
  bool shield_stock_changed = FALSE;
  bool production_changed = FALSE;
  struct unit_list *pfocus_units = get_units_in_focus();
  int caravan_city_id;

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner->player_no != packet->owner)) {
    client_remove_city(pcity);
    pcity = NULL;
    city_has_changed_owner = TRUE;
  }

  if(!pcity) {
    city_is_new = TRUE;
    pcity = create_city_virtual(get_player(packet->owner),
				map_pos_to_tile(packet->x, packet->y),
				packet->name);
    pcity->id=packet->id;
    idex_register_city(pcity);
    update_descriptions = TRUE;
  }
  else {
    city_is_new = FALSE;

    name_changed = (strcmp(pcity->name, packet->name) != 0);

    /* Check if city desciptions should be updated */
    if (draw_city_names && name_changed) {
      update_descriptions = TRUE;
    } else if (DRAW_CITY_PRODUCTIONS
	       && (pcity->production.is_unit != packet->production_is_unit
		   || pcity->production.value != packet->production_value
		   || pcity->surplus[O_SHIELD] != packet->surplus[O_SHIELD]
		   || pcity->shield_stock != packet->shield_stock)) {
      update_descriptions = TRUE;
    } else if (draw_city_names && DRAW_CITY_GROWTH &&
	       (pcity->food_stock != packet->food_stock ||
		pcity->surplus[O_FOOD] != packet->surplus[O_FOOD])) {
      /* If either the food stock or surplus have changed, the time-to-grow
	 is likely to have changed as well. */
      update_descriptions = TRUE;
    }
    assert(pcity->id == packet->id);
  }
  
  pcity->owner = get_player(packet->owner);
  pcity->tile = map_pos_to_tile(packet->x, packet->y);
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size = packet->size;
  for (i = 0; i < 5; i++) {
    pcity->ppl_happy[i] = packet->ppl_happy[i];
    pcity->ppl_content[i] = packet->ppl_content[i];
    pcity->ppl_unhappy[i] = packet->ppl_unhappy[i];
    pcity->ppl_angry[i] = packet->ppl_angry[i];
  }
  specialist_type_iterate(sp) {
    pcity->specialists[sp] = packet->specialists[sp];
  } specialist_type_iterate_end;

  pcity->city_options = packet->city_options;

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    pcity->trade[i]=packet->trade[i];
    pcity->trade_value[i]=packet->trade_value[i];
  }

  output_type_iterate(o) {
    pcity->surplus[o] = packet->surplus[o];
    pcity->waste[o] = packet->waste[o];
    pcity->unhappy_penalty[o] = packet->unhappy_penalty[o];
    pcity->prod[o] = packet->prod[o];
    pcity->citizen_base[o] = packet->citizen_base[o];
    pcity->usage[o] = packet->usage[o];
  } output_type_iterate_end;

  pcity->food_stock=packet->food_stock;
  if (pcity->shield_stock != packet->shield_stock) {
    shield_stock_changed = TRUE;
    pcity->shield_stock = packet->shield_stock;
  }
  pcity->pollution=packet->pollution;

  if (city_is_new
      || pcity->production.is_unit != packet->production_is_unit
      || pcity->production.value != packet->production_value) {
    need_units_dialog_update = TRUE;
  }
  if (pcity->production.is_unit != packet->production_is_unit
      || pcity->production.value != packet->production_value) {
    production_changed = TRUE;
  }
  pcity->production.is_unit = packet->production_is_unit;
  pcity->production.value = packet->production_value;
  if (city_is_new) {
    init_worklist(&pcity->worklist);

    for (i = 0; i < ARRAY_SIZE(pcity->improvements); i++) {
      pcity->improvements[i] = I_NONE;
    }
  }
  copy_worklist(&pcity->worklist, &packet->worklist);
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;

  pcity->turn_last_built=packet->turn_last_built;
  pcity->turn_founded = packet->turn_founded;
  pcity->changed_from.value = packet->changed_from_id;
  pcity->changed_from.is_unit = packet->changed_from_is_unit;
  pcity->before_change_shields=packet->before_change_shields;
  pcity->disbanded_shields=packet->disbanded_shields;
  pcity->caravan_shields=packet->caravan_shields;
  pcity->last_turns_shield_surplus = packet->last_turns_shield_surplus;

  for (i = 0; i < CITY_MAP_SIZE * CITY_MAP_SIZE; i++) {
    const int x = i % CITY_MAP_SIZE, y = i / CITY_MAP_SIZE;

    if (city_is_new) {
      /* Need to pre-initialize before set_worker_city()  -- dwp */
      pcity->city_map[x][y] =
	is_valid_city_coords(x, y) ? C_TILE_EMPTY : C_TILE_UNAVAILABLE;
    }
    if (is_valid_city_coords(x, y)) {
      set_worker_city(pcity, x, y, packet->city_map[i]);
    }
  }
  
  impr_type_iterate(i) {
    if (pcity->improvements[i] == I_NONE
	&& BV_ISSET(packet->improvements, i)
	&& !city_is_new) {
      audio_play_sound(improvement_by_number(i)->soundtag,
		       improvement_by_number(i)->soundtag_alt);
    }
    update_improvement_from_packet(pcity, i,
				   BV_ISSET(packet->improvements, i));
  } impr_type_iterate_end;

  /* We should be able to see units in the city.  But for a diplomat
   * investigating an enemy city we can't.  In that case we don't update
   * the occupied flag at all: it's already been set earlier and we'll
   * get an update if it changes. */
  if (can_player_see_units_in_city(game.player_ptr, pcity)) {
    pcity->client.occupied
      = (unit_list_size(pcity->tile->units) > 0);
  }

  pcity->client.happy = city_happy(pcity);
  pcity->client.unhappy = city_unhappy(pcity);

  popup = (city_is_new && can_client_change_view()
           && pcity->owner == game.player_ptr && popup_new_cities)
          || packet->diplomat_investigate;

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  pcity->client.walls = packet->walls;

  handle_city_packet_common(pcity, city_is_new, popup,
			    packet->diplomat_investigate);

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }

  /* Update focus unit info label if necessary. */
  if (name_changed) {
    unit_list_iterate(pfocus_units, pfocus_unit) {
      if (pfocus_unit->homecity == pcity->id) {
	update_unit_info_label(pfocus_units);
	break;
      }
    } unit_list_iterate_end;
  }

  /* Update the units dialog if necessary. */
  if (need_units_dialog_update) {
    activeunits_report_dialog_update();
  }

  /* Update the panel text (including civ population). */
  update_info_label();
  
  /* update caravan dialog */
  if ((production_changed || shield_stock_changed)
      && caravan_dialog_is_open(NULL, &caravan_city_id)
      && caravan_city_id == pcity->id) {
    caravan_dialog_update();
  }
}

/****************************************************************************
  A helper function for handling city-info and city-short-info packets.
  Naturally, both require many of the same operations to be done on the
  data.
****************************************************************************/
static void handle_city_packet_common(struct city *pcity, bool is_new,
                                      bool popup, bool investigate)
{
  int i;

  if(is_new) {
    pcity->units_supported = unit_list_new();
    pcity->info_units_supported = unit_list_new();
    pcity->info_units_present = unit_list_new();
    city_list_prepend(city_owner(pcity)->cities, pcity);
    tile_set_city(pcity->tile, pcity);
    if (pcity->owner == game.player_ptr) {
      city_report_dialog_update();
    }

    for(i=0; i<game.info.nplayers; i++) {
      unit_list_iterate(game.players[i].units, punit) 
	if(punit->homecity==pcity->id)
	  unit_list_prepend(pcity->units_supported, punit);
      unit_list_iterate_end;
    }
  } else {
    if (pcity->owner == game.player_ptr) {
      city_report_dialog_update_city(pcity);
    }
  }


  if (can_client_change_view()) {
    refresh_city_mapcanvas(pcity, pcity->tile, FALSE, FALSE);
  }

  if (city_workers_display==pcity)  {
    city_workers_display=NULL;
  }

  if (popup
      && can_client_issue_orders()
      && game.player_ptr
      && !game.player_ptr->ai.control) {
    update_menus();
    if (!city_dialog_is_open(pcity)) {
      popup_city_dialog(pcity);
    }
  }

  if (!is_new
      && (can_player_see_city_internals(game.player_ptr, pcity) || popup)) {
    refresh_city_dialog(pcity);
  }

  /* update menus if the focus unit is on the tile. */
  if (get_focus_unit_on_tile(pcity->tile)) {
    update_menus();
  }

  if(is_new) {
    freelog(LOG_DEBUG, "New %s city %s id %d (%d %d)",
	    nation_rule_name(nation_of_city(pcity)),
	    pcity->name, pcity->id, TILE_XY(pcity->tile));
  }
}

/****************************************************************************
  A city-short-info packet is sent to tell us about any cities we can't see
  the internals of.  Most of the time this includes any cities owned by
  someone else.
****************************************************************************/
void handle_city_short_info(struct packet_city_short_info *packet)
{
  struct city *pcity;
  bool city_is_new, city_has_changed_owner = FALSE;
  bool update_descriptions = FALSE;

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner->player_no != packet->owner)) {
    client_remove_city(pcity);
    pcity = NULL;
    city_has_changed_owner = TRUE;
  }

  if(!pcity) {
    city_is_new = TRUE;
    pcity = create_city_virtual(get_player(packet->owner),
				map_pos_to_tile(packet->x, packet->y),
				packet->name);
    pcity->id=packet->id;
    idex_register_city(pcity);
  }
  else {
    city_is_new = FALSE;

    /* Check if city desciptions should be updated */
    if (draw_city_names && strcmp(pcity->name, packet->name) != 0) {
      update_descriptions = TRUE;
    }

    pcity->owner = get_player(packet->owner);
    sz_strlcpy(pcity->name, packet->name);
    
    assert(pcity->id == packet->id);
  }
  
  pcity->size=packet->size;

  /* HACK: special case for trade routes */
  pcity->citizen_base[O_TRADE] = packet->tile_trade;

  /* We can't actually see the internals of the city, but the server tells
   * us this much. */
  pcity->client.occupied = packet->occupied;
  pcity->client.happy = packet->happy;
  pcity->client.unhappy = packet->unhappy;

  pcity->ppl_happy[4] = 0;
  pcity->ppl_content[4] = 0;
  pcity->ppl_unhappy[4] = 0;
  pcity->ppl_angry[4] = 0;
  if (packet->happy) {
    pcity->ppl_happy[4] = pcity->size;
  } else if (packet->unhappy) {
    pcity->ppl_unhappy[4] = pcity->size;
  } else {
    pcity->ppl_content[4] = pcity->size;
  }

  if (city_is_new) {
    int i;

    for (i = 0; i < ARRAY_SIZE(pcity->improvements); i++) {
      pcity->improvements[i] = I_NONE;
    }
  }

  impr_type_iterate(i) {
    update_improvement_from_packet(pcity, i,
				   BV_ISSET(packet->improvements, i));
  } impr_type_iterate_end;

  /* This sets dumb values for everything else. This is not really required,
     but just want to be at the safe side. */
  {
    int i;
    int x, y;

    specialist_type_iterate(sp) {
      pcity->specialists[sp] = 0;
    } specialist_type_iterate_end;
    for (i = 0; i < NUM_TRADEROUTES; i++) {
      pcity->trade[i] = 0;
      pcity->trade_value[i] = 0;
    }
    memset(pcity->surplus, 0, O_COUNT * sizeof(*pcity->surplus));
    memset(pcity->waste, 0, O_COUNT * sizeof(*pcity->waste));
    memset(pcity->prod, 0, O_COUNT * sizeof(*pcity->prod));
    pcity->food_stock         = 0;
    pcity->shield_stock       = 0;
    pcity->pollution          = 0;
    BV_CLR_ALL(pcity->city_options);
    pcity->production.is_unit   = FALSE;
    pcity->production.value = 0;
    init_worklist(&pcity->worklist);
    pcity->airlift            = FALSE;
    pcity->did_buy            = FALSE;
    pcity->did_sell           = FALSE;
    pcity->was_happy          = FALSE;

    for (y = 0; y < CITY_MAP_SIZE; y++) {
      for (x = 0; x < CITY_MAP_SIZE; x++) {
	pcity->city_map[x][y] = C_TILE_EMPTY;
      }
    }
  } /* Dumb values */

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  pcity->client.walls = packet->walls;

  handle_city_packet_common(pcity, city_is_new, FALSE, FALSE);

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_new_year(int year, int turn)
{
  game.info.year = year;
  /*
   * The turn was increased in handle_before_new_year()
   */
  assert(game.info.turn == turn);
  update_info_label();

  update_unit_focus();
  auto_center_on_focus_unit();

  update_unit_info_label(get_units_in_focus());
  update_menus();

  set_seconds_to_turndone(game.info.timeout);

#if 0
  /* This information shouldn't be needed, but if it is this is the only
   * way we can get it. */
  if (game.player_ptr) {
    turn_gold_difference
      = game.player_ptr->economic.gold - last_turn_gold_amount;
    last_turn_gold_amount = game.player_ptr->economic.gold;
  }
#endif

  update_city_descriptions();

  if (sound_bell_at_new_turn) {
    create_event(NULL, E_TURN_BELL, _("Start of turn %d"), game.info.turn);
  }

  agents_new_turn();
}

/**************************************************************************
  Called by the network code when an end-phase packet is received.  This
  signifies the end of our phase (it's not sent for other player's
  phases).
**************************************************************************/
void handle_end_phase(void)
{
  clear_notify_window();
  /*
   * The local idea of the game.info.turn is increased here since the
   * client will get unit updates (reset of move points for example)
   * between handle_before_new_year() and handle_new_year(). These
   * unit updates will look like they did take place in the old turn
   * which is incorrect. If we get the authoritative information about
   * the game.info.turn in handle_new_year() we will check it.
   */
  game.info.turn++;
  agents_before_new_turn();
}

/**************************************************************************
  Called by the network code when an start-phase packet is received.  This
  may be the start of our phase or someone else's phase.
**************************************************************************/
void handle_start_phase(int phase)
{
  game.info.phase = phase;

  if (game.player_ptr && is_player_phase(game.player_ptr, phase)) {
    /* HACK: this is updated by the player packet too; we update it here
     * so the turn done button state will be set properly. */
    game.player_ptr->phase_done = FALSE;

    agents_start_turn();
    non_ai_unit_focus = FALSE;

    turn_done_sent = FALSE;
    update_turn_done_button_state();

    if(game.player_ptr->ai.control && !ai_manual_turn_done) {
      user_ended_turn();
    }

    player_set_unit_focus_status(game.player_ptr);

    city_list_iterate(game.player_ptr->cities, pcity) {
      pcity->client.colored = FALSE;
    } city_list_iterate_end;
    unit_list_iterate(game.player_ptr->units, punit) {
      punit->client.colored = FALSE;
    } unit_list_iterate_end;
    update_map_canvas_visible();
  }

  update_info_label();
}

/**************************************************************************
...
**************************************************************************/
void play_sound_for_event(enum event_type type)
{
  const char *sound_tag = get_event_sound_tag(type);

  if (sound_tag) {
    audio_play_sound(sound_tag, NULL);
  }
}  
  
/**************************************************************************
  Handle a message packet.  This includes all messages - both
  in-game messages and chats from other players.
**************************************************************************/
void handle_chat_msg(char *message, int x, int y,
		     enum event_type event, int conn_id)
{
  struct tile *ptile = NULL;

  if (is_normal_map_pos(x, y)) {
    ptile = map_pos_to_tile(x, y);
  }

  handle_event(message, ptile, event, conn_id);
}
 
/**************************************************************************
...
**************************************************************************/
void handle_page_msg(char *message, enum event_type event)
{
  char *caption;
  char *headline;
  char *lines;

  caption = message;
  headline = strchr (caption, '\n');
  if (headline) {
    *(headline++) = '\0';
    lines = strchr (headline, '\n');
    if (lines) {
      *(lines++) = '\0';
    } else {
      lines = "";
    }
  } else {
    headline = "";
    lines = "";
  }

  if (!game.player_ptr
      || !game.player_ptr->ai.control
      || event != E_BROADCAST_REPORT) {
    popup_notify_dialog(caption, headline, lines);
    play_sound_for_event(event);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct packet_unit_info *packet)
{
  struct unit *punit;

  punit = unpackage_unit(packet);
  if (handle_unit_packet_common(punit)) {
    free(punit);
  }
}

/**************************************************************************
  Called to do basic handling for a unit_info or short_unit_info packet.

  Both owned and foreign units are handled; you may need to check unit
  owner, or if unit equals focus unit, depending on what you are doing.

  Note: Normally the server informs client about a new "activity" here.
  For owned units, the new activity can be a result of:
  - The player issued a command (a request) with the client.
  - The server side AI did something.
  - An enemy encounter caused a sentry to idle. (See "Wakeup Focus").

  Depending on what caused the change, different actions may be taken.
  Therefore, this function is a bit of a jungle, and it is advisable
  to read thoroughly before changing.

  Exception: When the client puts a unit in focus, it's status is set to
  idle immediately, before informing the server about the new status. This
  is because the server can never deny a request for idle, and should not
  be concerned about which unit the client is focusing on.
**************************************************************************/
static bool handle_unit_packet_common(struct unit *packet_unit)
{
  struct city *pcity;
  struct unit *punit;
  bool need_update_menus = FALSE;
  bool repaint_unit = FALSE;
  bool repaint_city = FALSE;	/* regards unit's homecity */
  struct tile *old_tile = NULL;
  bool check_focus = FALSE;     /* conservative focus change */
  bool moved = FALSE;
  bool ret = FALSE;
  
  punit = player_find_unit_by_id(packet_unit->owner, packet_unit->id);
  if (!punit && find_unit_by_id(packet_unit->id)) {
    /* This means unit has changed owner. We deal with this here
     * by simply deleting the old one and creating a new one. */
    handle_unit_remove(packet_unit->id);
  }

  if (punit) {
    ret = TRUE;
    punit->activity_count = packet_unit->activity_count;
    unit_change_battlegroup(punit, packet_unit->battlegroup);
    if (punit->ai.control != packet_unit->ai.control) {
      punit->ai.control = packet_unit->ai.control;
      repaint_unit = TRUE;
      /* AI is set:     may change focus */
      /* AI is cleared: keep focus */
      if (packet_unit->ai.control && unit_is_in_focus(punit)) {
        check_focus = TRUE;
      }
    }

    if (punit->activity != packet_unit->activity
	|| punit->activity_target != packet_unit->activity_target
        || punit->activity_base != packet_unit->activity_base
	|| punit->transported_by != packet_unit->transported_by
	|| punit->occupy != packet_unit->occupy
	|| punit->has_orders != packet_unit->has_orders
	|| punit->orders.repeat != packet_unit->orders.repeat
	|| punit->orders.vigilant != packet_unit->orders.vigilant
	|| punit->orders.index != packet_unit->orders.index) {

      /*** Change in activity or activity's target. ***/

      /* May change focus if focus unit gets a new activity.
       * But if new activity is Idle, it means user specifically selected
       * the unit */
      if (unit_is_in_focus(punit)
	  && (packet_unit->activity != ACTIVITY_IDLE
	      || packet_unit->has_orders)) {
        check_focus = TRUE;
      }

      repaint_unit = TRUE;

      /* Wakeup Focus */
      if (wakeup_focus 
	  && game.player_ptr
          && !game.player_ptr->ai.control
          && punit->owner == game.player_ptr
          && punit->activity == ACTIVITY_SENTRY
          && packet_unit->activity == ACTIVITY_IDLE
	  && is_player_phase(game.player_ptr, game.info.phase)
              /* only 1 wakeup focus per tile is useful */
          && !get_focus_unit_on_tile(packet_unit->tile)) {
        set_unit_focus(punit);
        check_focus = FALSE; /* and keep it */

        /* Autocenter on Wakeup, regardless of the local option 
         * "auto_center_on_unit". */
        if (!tile_visible_and_not_on_border_mapcanvas(punit->tile)) {
          center_tile_mapcanvas(punit->tile);
        }
      }

      punit->activity = packet_unit->activity;
      punit->activity_target = packet_unit->activity_target;
      punit->activity_base = packet_unit->activity_base;

      punit->transported_by = packet_unit->transported_by;
      if (punit->occupy != packet_unit->occupy
	  && get_focus_unit_on_tile(packet_unit->tile)) {
	/* Special case: (un)loading a unit in a transporter on the
	 * same tile as the focus unit may (dis)allow the focus unit to be
	 * loaded.  Thus the orders->(un)load menu item needs updating. */
	need_update_menus = TRUE;
      }
      punit->occupy = packet_unit->occupy;
    
      punit->has_orders = packet_unit->has_orders;
      punit->orders.length = packet_unit->orders.length;
      punit->orders.index = packet_unit->orders.index;
      punit->orders.repeat = packet_unit->orders.repeat;
      punit->orders.vigilant = packet_unit->orders.vigilant;

      /* We cheat by just stealing the packet unit's list. */
      if (punit->orders.list) {
	free(punit->orders.list);
      }
      punit->orders.list = packet_unit->orders.list;
      packet_unit->orders.list = NULL;

      if (!game.player_ptr || punit->owner == game.player_ptr) {
        refresh_unit_city_dialogs(punit);
      }
    } /*** End of Change in activity or activity's target. ***/

    /* These two lines force the menus to be updated as appropriate when
     * the focus unit changes. */
    if (unit_is_in_focus(punit)) {
      need_update_menus = TRUE;
    }

    if (punit->homecity != packet_unit->homecity) {
      /* change homecity */
      struct city *pcity;
      if ((pcity=find_city_by_id(punit->homecity))) {
	unit_list_unlink(pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
      
      punit->homecity = packet_unit->homecity;
      if ((pcity=find_city_by_id(punit->homecity))) {
	unit_list_prepend(pcity->units_supported, punit);
	repaint_city = TRUE;
      }
    }

    if (punit->hp != packet_unit->hp) {
      /* hp changed */
      punit->hp = packet_unit->hp;
      repaint_unit = TRUE;
    }

    if (punit->utype != unit_type(packet_unit)) {
      /* Unit type has changed (been upgraded) */
      struct city *pcity = tile_get_city(punit->tile);
      
      punit->utype = unit_type(packet_unit);
      repaint_unit = TRUE;
      repaint_city = TRUE;
      if (pcity && (pcity->id != punit->homecity)) {
	refresh_city_dialog(pcity);
      }
      if (unit_is_in_focus(punit)) {
        /* Update the orders menu -- the unit might have new abilities */
	need_update_menus = TRUE;
      }
    }

    /* May change focus if an attempted move or attack exhausted unit */
    if (punit->moves_left != packet_unit->moves_left
        && unit_is_in_focus(punit)) {
      check_focus = TRUE;
    }

    if (!same_pos(punit->tile, packet_unit->tile)) { 
      /*** Change position ***/
      struct city *pcity = tile_get_city(punit->tile);

      old_tile = punit->tile;
      moved = TRUE;

      /* Show where the unit is going. */
      do_move_unit(punit, packet_unit);

      if(pcity)  {
	if (can_player_see_units_in_city(game.player_ptr, pcity)) {
	  /* Unit moved out of a city - update the occupied status. */
	  bool new_occupied =
	    (unit_list_size(pcity->tile->units) > 0);

	  if (pcity->client.occupied != new_occupied) {
	    pcity->client.occupied = new_occupied;
	    refresh_city_mapcanvas(pcity, pcity->tile, FALSE, FALSE);
	    update_city_description(pcity);
	  }
	}

        if(pcity->id==punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
      }
      
      if((pcity=tile_get_city(punit->tile)))  {
	if (can_player_see_units_in_city(game.player_ptr, pcity)) {
	  /* Unit moved into a city - obviously it's occupied. */
	  if (!pcity->client.occupied) {
	    pcity->client.occupied = TRUE;
	    refresh_city_mapcanvas(pcity, pcity->tile, FALSE, FALSE);
	  }
	}

        if(pcity->id == punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
	
        if((unit_has_type_flag(punit, F_TRADE_ROUTE) || unit_has_type_flag(punit, F_HELP_WONDER))
	   && game.player_ptr
	   && !game.player_ptr->ai.control
	   && punit->owner == game.player_ptr
	   && !unit_has_orders(punit)
	   && can_client_issue_orders()
	   && (unit_can_help_build_wonder_here(punit)
	       || unit_can_est_traderoute_here(punit))) {
	  process_caravan_arrival(punit);
	}
      }

    }  /*** End of Change position. ***/

    if (repaint_city || repaint_unit) {
      /* We repaint the city if the unit itself needs repainting or if
       * there is a special city-only redrawing to be done. */
      if((pcity=find_city_by_id(punit->homecity))) {
	refresh_city_dialog(pcity);
      }
      if (repaint_unit && punit->tile->city && punit->tile->city != pcity) {
	/* Refresh the city we're occupying too. */
	refresh_city_dialog(punit->tile->city);
      }
    }

    punit->veteran = packet_unit->veteran;
    punit->moves_left = packet_unit->moves_left;
    punit->bribe_cost = 0;
    punit->fuel = packet_unit->fuel;
    punit->goto_tile = packet_unit->goto_tile;
    punit->paradropped = packet_unit->paradropped;
    if (punit->done_moving != packet_unit->done_moving) {
      punit->done_moving = packet_unit->done_moving;
      check_focus = TRUE;
    }

    /* This won't change punit; it enqueues the call for later handling. */
    agents_unit_changed(punit);
  } else {
    /*** Create new unit ***/
    punit = packet_unit;
    idex_register_unit(punit);

    unit_list_prepend(punit->owner->units, punit);
    unit_list_prepend(punit->tile->units, punit);

    unit_register_battlegroup(punit);

    if((pcity=find_city_by_id(punit->homecity))) {
      unit_list_prepend(pcity->units_supported, punit);
    }

    freelog(LOG_DEBUG, "New %s %s id %d (%d %d) hc %d %s", 
	    nation_rule_name(nation_of_unit(punit)),
	    unit_rule_name(punit),
	    TILE_XY(punit->tile),
	    punit->id,
	    punit->homecity,
	    (pcity ? pcity->name : "(unknown)"));

    repaint_unit = (punit->transported_by == -1);
    agents_unit_new(punit);

    if ((pcity = tile_get_city(punit->tile))) {
      /* The unit is in a city - obviously it's occupied. */
      pcity->client.occupied = TRUE;
    }
  } /*** End of Create new unit ***/

  assert(punit != NULL);

  if (unit_is_in_focus(punit)
      || get_focus_unit_on_tile(punit->tile)
      || (moved && get_focus_unit_on_tile(old_tile))) {
    update_unit_info_label(get_units_in_focus());
  }

  if (repaint_unit) {
    refresh_unit_mapcanvas(punit, punit->tile, TRUE, FALSE);
  }

  if ((check_focus || get_num_units_in_focus() == 0)
      && game.player_ptr
      && !game.player_ptr->ai.control
      && is_player_phase(game.player_ptr, game.info.phase)) {
    update_unit_focus();
  }

  if (need_update_menus) {
    update_menus();
  }

  return ret;
}

/**************************************************************************
  Receive a short_unit info packet.
**************************************************************************/
void handle_unit_short_info(struct packet_unit_short_info *packet)
{
  struct city *pcity;
  struct unit *punit;

  if (packet->goes_out_of_sight) {
    punit = find_unit_by_id(packet->id);
    if (punit) {
      client_remove_unit(punit);
    }
    return;
  }

  /* Special case for a diplomat/spy investigating a city: The investigator
   * needs to know the supported and present units of a city, whether or not
   * they are fogged. So, we send a list of them all before sending the city
   * info. */
  if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED
      || packet->packet_use == UNIT_INFO_CITY_PRESENT) {
    static int last_serial_num = 0;

    /* fetch city -- abort if not found */
    pcity = find_city_by_id(packet->info_city_id);
    if (!pcity) {
      return;
    }

    /* New serial number -- clear (free) everything */
    if (last_serial_num != packet->serial_num) {
      last_serial_num = packet->serial_num;
      unit_list_iterate(pcity->info_units_supported, psunit) {
	destroy_unit_virtual(psunit);
      } unit_list_iterate_end;
      unit_list_unlink_all(pcity->info_units_supported);
      unit_list_iterate(pcity->info_units_present, ppunit) {
	destroy_unit_virtual(ppunit);
      } unit_list_iterate_end;
      unit_list_unlink_all(pcity->info_units_present);
    }

    /* Okay, append a unit struct to the proper list. */
    punit = unpackage_short_unit(packet);
    if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED) {
      unit_list_prepend(pcity->info_units_supported, punit);
    } else {
      assert(packet->packet_use == UNIT_INFO_CITY_PRESENT);
      unit_list_prepend(pcity->info_units_present, punit);
    }

    /* Done with special case. */
    return;
  }

  if (packet->owner == game.info.player_idx) {
    freelog(LOG_ERROR, "Got packet_short_unit for own unit.");
  }

  punit = unpackage_short_unit(packet);
  if (handle_unit_packet_common(punit)) {
    free(punit);
  }
}

/****************************************************************************
  Receive information about the map size and topology from the server.  We
  initialize some global variables at the same time.
****************************************************************************/
void handle_map_info(int xsize, int ysize, int topology_id)
{
  if (!map_is_empty()) {
    map_free();
  }

  map.xsize = xsize;
  map.ysize = ysize;
  map.topology_id = topology_id;

  /* Parameter is FALSE so that sizes are kept unchanged. */
  map_init_topology(FALSE);

  map_allocate();
  init_client_goto();
  init_mapview_decorations();

  generate_citydlg_dimensions();

  calculate_overview_dimensions();
}

/**************************************************************************
...
**************************************************************************/
void handle_game_info(struct packet_game_info *pinfo)
{
  bool boot_help;
  bool update_aifill_button = FALSE;


  if (game.info.aifill != pinfo->aifill) {
    update_aifill_button = TRUE;
  }
  
  if (game.info.is_edit_mode != pinfo->is_edit_mode) {
    popdown_all_city_dialogs();
  }

  game.info = *pinfo;

  game.government_when_anarchy
    = government_by_number(game.info.government_when_anarchy_id);
  game.player_ptr = get_player(game.info.player_idx);
  if (get_client_state() == CLIENT_PRE_GAME_STATE) {
    popdown_races_dialog();
  }
  boot_help = (can_client_change_view()
	       && game.info.spacerace != pinfo->spacerace);
  if (game.info.timeout != 0 && pinfo->seconds_to_phasedone >= 0) {
    set_seconds_to_turndone(pinfo->seconds_to_phasedone);
  }
  if (boot_help) {
    boot_help_texts();		/* reboot, after setting game.spacerace */
  }
  update_unit_focus();
  update_menus();
  update_players_dialog();
  if (update_aifill_button) {
    update_start_page();
  }
}

/**************************************************************************
...
**************************************************************************/
static bool read_player_info_techs(struct player *pplayer,
				   char *inventions)
{
  bool need_effect_update = FALSE;

  tech_type_iterate(i) {
    enum tech_state oldstate
      = get_player_research(pplayer)->inventions[i].state;
    enum tech_state newstate = inventions[i] - '0';

    get_player_research(pplayer)->inventions[i].state = newstate;
    if (newstate != oldstate
	&& (newstate == TECH_KNOWN || oldstate == TECH_KNOWN)) {
      need_effect_update = TRUE;
    }
  } tech_type_iterate_end;

  if (need_effect_update) {
    update_menus();
  }

  update_research(pplayer);
  return need_effect_update;
}

/**************************************************************************
  Sets the target government.  This will automatically start a revolution
  if the target government differs from the current one.
**************************************************************************/
void set_government_choice(struct government *government)
{
  if (can_client_issue_orders()
      && game.player_ptr
      && government != government_of_player(game.player_ptr)) {
    dsend_packet_player_change_government(&aconnection, government->index);
  }
}

/**************************************************************************
  Begin a revolution by telling the server to start it.  This also clears
  the current government choice.
**************************************************************************/
void start_revolution(void)
{
  dsend_packet_player_change_government(&aconnection,
					game.info.government_when_anarchy_id);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_info(struct packet_player_info *pinfo)
{
  int i;
  bool poptechup, new_tech = FALSE;
  char msg[MAX_LEN_MSG];
  struct player *pplayer = &game.players[pinfo->playerno];
  struct player_research* research;
  bool is_new_nation;

  sz_strlcpy(pplayer->name, pinfo->name);

  is_new_nation = player_set_nation(pplayer, nation_by_number(pinfo->nation));
  pplayer->is_male=pinfo->is_male;
  team_add_player(pplayer, team_get_by_id(pinfo->team));
  pplayer->score.game = pinfo->score;

  pplayer->economic.gold=pinfo->gold;
  pplayer->economic.tax=pinfo->tax;
  pplayer->economic.science=pinfo->science;
  pplayer->economic.luxury=pinfo->luxury;
  pplayer->government = government_by_number(pinfo->government);
  pplayer->target_government = government_by_number(pinfo->target_government);
  BV_CLR_ALL(pplayer->embassy);
  players_iterate(pother) {
    if (pinfo->embassy[pother->player_no]) {
      BV_SET(pplayer->embassy, pother->player_no);
    }
  } players_iterate_end;
  pplayer->gives_shared_vision = pinfo->gives_shared_vision;
  pplayer->city_style=pinfo->city_style;
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->ai.love[i] = pinfo->love[i];
  }

  /* Check if we detect change to armistice with us. If so,
   * ready all units for movement out of the territory in
   * question; otherwise they will be disbanded. */
  if (game.player_ptr
      && pplayer->diplstates[game.player_ptr->player_no].type
      != DS_ARMISTICE
      && pinfo->diplstates[game.player_ptr->player_no].type
      == DS_ARMISTICE) {
    unit_list_iterate(game.player_ptr->units, punit) {
      if (!punit->tile->owner || punit->tile->owner != pplayer) {
        continue;
      }
      if (punit->focus_status == FOCUS_WAIT) {
        punit->focus_status = FOCUS_AVAIL;
      }
      if (punit->activity != ACTIVITY_IDLE) {
        request_new_unit_activity(punit, ACTIVITY_IDLE);
      }
    } unit_list_iterate_end;
  }

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->diplstates[i].type =
      pinfo->diplstates[i].type;
    pplayer->diplstates[i].turns_left =
      pinfo->diplstates[i].turns_left;
    pplayer->diplstates[i].contact_turns_left =
      pinfo->diplstates[i].contact_turns_left;
    pplayer->diplstates[i].has_reason_to_cancel =
      pinfo->diplstates[i].has_reason_to_cancel;
  }
  pplayer->is_connected = pinfo->is_connected;

  for (i = 0; i < B_LAST/*game.control.num_impr_types*/; i++) {
     pplayer->small_wonders[i] = pinfo->small_wonders[i];
  }

  /* We need to set ai.control before read_player_info_techs */
  if(pplayer->ai.control!=pinfo->ai)  {
    pplayer->ai.control=pinfo->ai;
    if(pplayer==game.player_ptr)  {
      my_snprintf(msg, sizeof(msg), _("AI Mode is now %s."),
		  game.player_ptr->ai.control?_("ON"):_("OFF"));
      append_output_window(msg);
    }
  }

  pplayer->ai.science_cost = pinfo->science_cost;

  /* If the server sends out player information at the wrong time, it is
   * likely to give us inconsistent player tech information, causing a
   * sanity-check failure within this function.  Fixing this at the client
   * end is very tricky; it's hard to figure out when to read the techs
   * and when to ignore them.  The current solution is that the server should
   * only send the player info out at appropriate times - e.g., while the
   * game is running. */
  new_tech = read_player_info_techs(pplayer, pinfo->inventions);
  
  research = get_player_research(pplayer);

  poptechup = (research->researching != pinfo->researching
               || research->tech_goal != pinfo->tech_goal);
  pplayer->bulbs_last_turn = pinfo->bulbs_last_turn;
  research->bulbs_researched = pinfo->bulbs_researched;
  research->techs_researched = pinfo->techs_researched;
  research->researching=pinfo->researching;
  research->future_tech = pinfo->future_tech;
  research->tech_goal=pinfo->tech_goal;
  
  if (can_client_change_view() && pplayer == game.player_ptr) {
    if (poptechup || new_tech) {
      science_dialog_update();
    }
    if (poptechup) {
      if (game.player_ptr && !game.player_ptr->ai.control) {
	popup_science_dialog(FALSE);
      }
    }
    if (new_tech) {
      /* If we just learned bridge building and focus is on a settler
	 on a river the road menu item will remain disabled unless we
	 do this. (applys in other cases as well.) */
      if (get_num_units_in_focus() > 0) {
	update_menus();
      }
    }
    economy_report_dialog_update();
    activeunits_report_dialog_update();
    city_report_dialog_update();
  }

  pplayer->is_ready = pinfo->is_ready;

  if (pplayer == game.player_ptr
      && pplayer->phase_done != pinfo->phase_done) {
    update_turn_done_button_state();
  }
  pplayer->phase_done = pinfo->phase_done;

  pplayer->nturns_idle=pinfo->nturns_idle;
  pplayer->is_alive=pinfo->is_alive;

  pplayer->ai.barbarian_type = pinfo->barbarian_type;
  pplayer->revolution_finishes = pinfo->revolution_finishes;
  pplayer->ai.skill_level = pinfo->ai_skill_level;

  update_players_dialog();
  update_worklist_report_dialog();
  upgrade_canvas_clipboard();

  if (pplayer == game.player_ptr && can_client_change_view()) {
    update_info_label();
  }

  /* if the server requests that the client reset, then information about
   * connections to this player are lost. If this is the case, insert the
   * correct conn back into the player->connections list */
  if (conn_list_size(pplayer->connections) == 0) {
    conn_list_iterate(game.est_connections, pconn) {
      if (pconn->player == pplayer) {
        /* insert the controller into first position */
        if (pconn->observer) {
          conn_list_append(pplayer->connections, pconn);
        } else {
          conn_list_prepend(pplayer->connections, pconn);
        }
      }
    } conn_list_iterate_end;
  }

  sz_strlcpy(pplayer->username, pinfo->username);

  if (is_new_nation) {
    races_toggles_set_sensitive();

    /* When changing nation during a running game, some refreshing is needed.
     * This may not be the only one! */
    update_map_canvas_visible();
  }
  if (can_client_change_view()) {
    /* Just about any changes above require an update to the intelligence
     * dialog. */
    update_intel_dialog(pplayer);
  }
  update_conn_list_dialog();
}

/**************************************************************************
  Remove, add, or update dummy connection struct representing some
  connection to the server, with info from packet_conn_info.
  Updates player and game connection lists.
  Calls update_players_dialog() in case info for that has changed.
**************************************************************************/
void handle_conn_info(struct packet_conn_info *pinfo)
{
  struct connection *pconn = find_conn_by_id(pinfo->id);

  freelog(LOG_DEBUG, "conn_info id%d used%d est%d plr%d obs%d acc%d",
	  pinfo->id, pinfo->used, pinfo->established, pinfo->player_num,
	  pinfo->observer, (int)pinfo->access_level);
  freelog(LOG_DEBUG, "conn_info \"%s\" \"%s\" \"%s\"",
	  pinfo->username, pinfo->addr, pinfo->capability);
  
  if (!pinfo->used) {
    /* Forget the connection */
    if (!pconn) {
      freelog(LOG_VERBOSE, "Server removed unknown connection %d", pinfo->id);
      return;
    }
    client_remove_cli_conn(pconn);
    pconn = NULL;
  } else {
    /* Add or update the connection.  Note the connection may refer to
     * a player we don't know about yet. */
    struct player *pplayer =
      ((pinfo->player_num >= 0 
        && pinfo->player_num < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS)
       ? get_player(pinfo->player_num) : NULL);
    
    if (!pconn) {
      freelog(LOG_VERBOSE, "Server reports new connection %d %s",
	      pinfo->id, pinfo->username);

      pconn = fc_calloc(1, sizeof(struct connection));
      pconn->buffer = NULL;
      pconn->send_buffer = NULL;
      pconn->ping_time = -1.0;
      if (pplayer) {
	conn_list_append(pplayer->connections, pconn);
      }
      conn_list_append(game.all_connections, pconn);
      conn_list_append(game.est_connections, pconn);
    } else {
      freelog(LOG_DEBUG, "Server reports updated connection %d %s",
	      pinfo->id, pinfo->username);
      if (pplayer != pconn->player) {
	if (pconn->player) {
	  conn_list_unlink(pconn->player->connections, pconn);
	}
	if (pplayer) {
	  conn_list_append(pplayer->connections, pconn);
	}
      }
    }
    pconn->id = pinfo->id;
    pconn->established = pinfo->established;
    pconn->observer = pinfo->observer;
    pconn->access_level = pinfo->access_level;
    pconn->player = pplayer;
    sz_strlcpy(pconn->username, pinfo->username);
    sz_strlcpy(pconn->addr, pinfo->addr);
    sz_strlcpy(pconn->capability, pinfo->capability);

    if (pinfo->id == aconnection.id) {
      aconnection.established = pconn->established;
      aconnection.observer = pconn->observer;
      aconnection.access_level = pconn->access_level;
      aconnection.player = pplayer;
    }
  }
  update_players_dialog();
  update_conn_list_dialog();
}

/*************************************************************************
  Handles a conn_ping_info packet from the server.  This packet contains
  ping times for each connection.
**************************************************************************/
void handle_conn_ping_info(int connections, int *conn_id, float *ping_time)
{
  int i;

  for (i = 0; i < connections; i++) {
    struct connection *pconn = find_conn_by_id(conn_id[i]);

    if (!pconn) {
      continue;
    }

    pconn->ping_time = ping_time[i];
    freelog(LOG_DEBUG, "conn-id=%d, ping=%fs", pconn->id,
	    pconn->ping_time);
  }
  /* The old_ping_time data is ignored. */

  update_players_dialog();
}

/**************************************************************************
Ideally the client should let the player choose which type of
modules and components to build, and (possibly) where to extend
structurals.  The protocol now makes this possible, but the
client is not yet that good (would require GUI improvements)
so currently the client choices stuff automatically if there
is anything unplaced.

This function makes a choice (sends spaceship_action) and
returns 1 if we placed something, else 0.

Do things one at a time; the server will send us an updated
spaceship_info packet, and we'll be back here to do anything
which is left.
**************************************************************************/
static bool spaceship_autoplace(struct player *pplayer,
			       struct player_spaceship *ship)
{
  int i, num;
  enum spaceship_place_type type;
  
  if (ship->modules > (ship->habitation + ship->life_support
		       + ship->solar_panels)) {
    /* "nice" governments prefer to keep success 100%;
     * others build habitation first (for score?)  (Thanks Massimo.)
     */
    type =
      (ship->habitation==0)   ? SSHIP_PLACE_HABITATION :
      (ship->life_support==0) ? SSHIP_PLACE_LIFE_SUPPORT :
      (ship->solar_panels==0) ? SSHIP_PLACE_SOLAR_PANELS :
      ((ship->habitation < ship->life_support)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_PLACE_HABITATION :
      (ship->solar_panels*2 < ship->habitation + ship->life_support)
                              ? SSHIP_PLACE_SOLAR_PANELS :
      (ship->life_support<ship->habitation)
                              ? SSHIP_PLACE_LIFE_SUPPORT :
      ((ship->life_support <= ship->habitation)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_PLACE_LIFE_SUPPORT :
                                SSHIP_PLACE_SOLAR_PANELS;

    if (type == SSHIP_PLACE_HABITATION) {
      num = ship->habitation + 1;
    } else if(type == SSHIP_PLACE_LIFE_SUPPORT) {
      num = ship->life_support + 1;
    } else {
      num = ship->solar_panels + 1;
    }
    assert(num <= NUM_SS_MODULES / 3);

    dsend_packet_spaceship_place(&aconnection, type, num);
    return TRUE;
  }
  if (ship->components > ship->fuel + ship->propulsion) {
    if (ship->fuel <= ship->propulsion) {
      type = SSHIP_PLACE_FUEL;
      num = ship->fuel + 1;
    } else {
      type = SSHIP_PLACE_PROPULSION;
      num = ship->propulsion + 1;
    }
    dsend_packet_spaceship_place(&aconnection, type, num);
    return TRUE;
  }
  if (ship->structurals > num_spaceship_structurals_placed(ship)) {
    /* Want to choose which structurals are most important.
       Else we first want to connect one of each type of module,
       then all placed components, pairwise, then any remaining
       modules, or else finally in numerical order.
    */
    int req = -1;
    
    if (!ship->structure[0]) {
      /* if we don't have the first structural, place that! */
      type = SSHIP_PLACE_STRUCTURAL;
      num = 0;
      dsend_packet_spaceship_place(&aconnection, type, num);
      return TRUE;
    }
    
    if (ship->habitation >= 1
	&& !ship->structure[modules_info[0].required]) {
      req = modules_info[0].required;
    } else if (ship->life_support >= 1
	       && !ship->structure[modules_info[1].required]) {
      req = modules_info[1].required;
    } else if (ship->solar_panels >= 1
	       && !ship->structure[modules_info[2].required]) {
      req = modules_info[2].required;
    } else {
      int i;
      for(i=0; i<NUM_SS_COMPONENTS; i++) {
	if ((i%2==0 && ship->fuel > (i/2))
	    || (i%2==1 && ship->propulsion > (i/2))) {
	  if (!ship->structure[components_info[i].required]) {
	    req = components_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_MODULES; i++) {
	if ((i%3==0 && ship->habitation > (i/3))
	    || (i%3==1 && ship->life_support > (i/3))
	    || (i%3==2 && ship->solar_panels > (i/3))) {
	  if (!ship->structure[modules_info[i].required]) {
	    req = modules_info[i].required;
	    break;
	  }
	}
      }
    }
    if (req == -1) {
      for(i=0; i<NUM_SS_STRUCTURALS; i++) {
	if (!ship->structure[i]) {
	  req = i;
	  break;
	}
      }
    }
    /* sanity: */
    assert(req!=-1);
    assert(!ship->structure[req]);
    
    /* Now we want to find a structural we can build which leads to req.
       This loop should bottom out, because everything leads back to s0,
       and we made sure above that we do s0 first.
     */
    while(!ship->structure[structurals_info[req].required]) {
      req = structurals_info[req].required;
    }
    type = SSHIP_PLACE_STRUCTURAL;
    num = req;
    dsend_packet_spaceship_place(&aconnection, type, num);
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
void handle_spaceship_info(struct packet_spaceship_info *p)
{
  int i;
  struct player *pplayer = &game.players[p->player_num];
  struct player_spaceship *ship = &pplayer->spaceship;
  
  ship->state        = p->sship_state;
  ship->structurals  = p->structurals;
  ship->components   = p->components;
  ship->modules      = p->modules;
  ship->fuel         = p->fuel;
  ship->fuel         = p->fuel;
  ship->propulsion   = p->propulsion;
  ship->habitation   = p->habitation;
  ship->life_support = p->life_support;
  ship->solar_panels = p->solar_panels;
  ship->launch_year  = p->launch_year;
  ship->population   = p->population;
  ship->mass         = p->mass;
  ship->support_rate = p->support_rate;
  ship->energy_rate  = p->energy_rate;
  ship->success_rate = p->success_rate;
  ship->travel_time  = p->travel_time;
  
  for(i=0; i<NUM_SS_STRUCTURALS; i++) {
    if (p->structure[i] == '0') {
      ship->structure[i] = FALSE;
    } else if (p->structure[i] == '1') {
      ship->structure[i] = TRUE;
    } else {
      freelog(LOG_ERROR,
	      "handle_spaceship_info()"
	      " invalid spaceship structure '%c' (%d).",
	      p->structure[i], p->structure[i]);
      ship->structure[i] = FALSE;
    }
  }

  if (pplayer != game.player_ptr) {
    refresh_spaceship_dialog(pplayer);
    return;
  }
  update_menus();

  if (!spaceship_autoplace(pplayer, ship)) {
    refresh_spaceship_dialog(pplayer);
  }
}

/**************************************************************************
This was once very ugly...
**************************************************************************/
void handle_tile_info(struct packet_tile_info *packet)
{
  struct tile *ptile = map_pos_to_tile(packet->x, packet->y);
  enum known_type old_known = client_tile_get_known(ptile);
  int old_resource;
  bool tile_changed = FALSE;
  bool known_changed = FALSE;
  enum tile_special_type spe;

  if (!ptile->terrain || ptile->terrain->index != packet->type) {
    tile_changed = TRUE;
    ptile->terrain = terrain_by_number(packet->type);
  }
  for (spe = 0; spe < S_LAST; spe++) {
    if (packet->special[spe]) {
      if (!tile_has_special(ptile, spe)) {
	tile_set_special(ptile, spe);
	tile_changed = TRUE;
      }
    } else {
      if (tile_has_special(ptile, spe)) {
	tile_clear_special(ptile, spe);
	tile_changed = TRUE;
      }
    }
  }

  if (ptile->resource) {
    old_resource = ptile->resource->index;
  } else {
    old_resource = -1;
  }

  if (old_resource != packet->resource) {
    tile_changed = TRUE;
  }

  ptile->resource = resource_by_number(packet->resource);
  if (packet->owner == MAP_TILE_OWNER_NULL) {
    if (ptile->owner) {
      ptile->owner = NULL;
      tile_changed = TRUE;
    }
  } else {
    struct player *newowner = get_player(packet->owner);

    if (ptile->owner != newowner) {
      ptile->owner = newowner;
      tile_changed = TRUE;
    }
  }
  if (old_known != packet->known) {
    known_changed = TRUE;
  }
  if (game.player_ptr) {
    BV_CLR(ptile->tile_known, game.info.player_idx);
    vision_layer_iterate(v) {
      BV_CLR(ptile->tile_seen[v], game.info.player_idx);
    } vision_layer_iterate_end;
    switch (packet->known) {
    case TILE_KNOWN:
      BV_SET(ptile->tile_known, game.info.player_idx);
      vision_layer_iterate(v) {
	BV_SET(ptile->tile_seen[v], game.info.player_idx);
      } vision_layer_iterate_end;
      break;
    case TILE_KNOWN_FOGGED:
      BV_SET(ptile->tile_known, game.info.player_idx);
      break;
    case TILE_UNKNOWN:
      break;
    default:
      freelog(LOG_NORMAL, "Unknown tile value %d.", packet->known);
      break;
    }
  }

  if (packet->spec_sprite[0] != '\0') {
    if (!ptile->spec_sprite
	|| strcmp(ptile->spec_sprite, packet->spec_sprite) != 0) {
      if (ptile->spec_sprite) {
	free(ptile->spec_sprite);
      }
      ptile->spec_sprite = mystrdup(packet->spec_sprite);
      tile_changed = TRUE;
    }
  } else {
    if (ptile->spec_sprite) {
      free(ptile->spec_sprite);
      ptile->spec_sprite = NULL;
      tile_changed = TRUE;
    }
  }

  if (client_tile_get_known(ptile) <= TILE_KNOWN_FOGGED
      && old_known == TILE_KNOWN) {
    /* This is an error.  So first we log the error, then make an assertion.
     * But for NDEBUG clients we fix the error. */
    unit_list_iterate(ptile->units, punit) {
      freelog(LOG_ERROR, "%p %d %s at (%d,%d) %s",
              punit,
              punit->id,
              unit_rule_name(punit),
              TILE_XY(punit->tile),
              unit_owner(punit)->name);
    } unit_list_iterate_end;
    assert(unit_list_size(ptile->units) == 0);
    unit_list_unlink_all(ptile->units);
  }

  /* update continents */
  if (ptile->continent != packet->continent && ptile->continent != 0
      && packet->continent > 0) {
    /* We're renumbering continents, somebody did a transform.
     * But we don't care about renumbering oceans since 
     * num_oceans is not kept at the client. */
    map.num_continents = 0;
  }

  ptile->continent = packet->continent;

  if (ptile->continent > map.num_continents) {
    map.num_continents = ptile->continent;
  }

  if (known_changed || tile_changed) {
    /* 
     * A tile can only change if it was known before and is still
     * known. In the other cases the tile is new or removed.
     */
    if (known_changed && client_tile_get_known(ptile) == TILE_KNOWN) {
      agents_tile_new(ptile);
    } else if (known_changed && client_tile_get_known(ptile) == TILE_KNOWN_FOGGED) {
      agents_tile_remove(ptile);
    } else {
      agents_tile_changed(ptile);
    }
  }

  /* refresh tiles */
  if (can_client_change_view()) {
    /* the tile itself (including the necessary parts of adjacent tiles) */
    if (tile_changed || old_known != client_tile_get_known(ptile)) {
      refresh_tile_mapcanvas(ptile, TRUE, FALSE);
    }
  }

  /* update menus if the focus unit is on the tile. */
  if (tile_changed) {
    if (get_focus_unit_on_tile(ptile)) {
      update_menus();
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_player_remove(int player_id)
{
  client_remove_player(player_id);
  update_conn_list_dialog();
}

/**************************************************************************
  Take arrival of ruleset control packet to indicate that
  current allocated governments should be free'd, and new
  memory allocated for new size. The same for nations.
**************************************************************************/
void handle_ruleset_control(struct packet_ruleset_control *packet)
{
  int i;

  ruleset_data_free();

  ruleset_cache_init();

  game.control = *packet;
  governments_alloc(packet->government_count);
  nations_alloc(packet->nation_count);
  city_styles_alloc(packet->styles_count);

  /* We are in inconsistent state. Players point to nations,
   * which do not point to players. Fix */
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    game.players[i].nation = NULL;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_unit_class(struct packet_ruleset_unit_class *p)
{
  struct unit_class *c;

  if(p->id < 0 || p->id >= game.control.num_unit_classes || p->id >= UCL_LAST) {
    freelog(LOG_ERROR,
            "Received bad unit_class id %d in handle_ruleset_unit_class()",
	    p->id);
    return;
  }

  c = uclass_by_number(p->id);

  sz_strlcpy(c->name.vernacular, p->name);
  c->name.translated = NULL;	/* unittype.c uclass_name_translation */
  c->move_type   = p->move_type;
  c->min_speed   = p->min_speed;
  c->hp_loss_pct = p->hp_loss_pct;
  c->hut_behavior = p->hut_behavior;
  c->flags       = p->flags;
}


/**************************************************************************
...
**************************************************************************/
void handle_ruleset_unit(struct packet_ruleset_unit *p)
{
  struct unit_type *u;
  int i;

  if(p->id < 0 || p->id >= game.control.num_unit_types || p->id >= U_LAST) {
    freelog(LOG_ERROR,
	    "handle_ruleset_unit() bad unit_type %d.",
	    p->id);
    return;
  }
  u = utype_by_number(p->id);

  sz_strlcpy(u->name.vernacular, p->name);
  u->name.translated = NULL;	/* unittype.c utype_name_translation */
  sz_strlcpy(u->graphic_str, p->graphic_str);
  sz_strlcpy(u->graphic_alt, p->graphic_alt);
  sz_strlcpy(u->sound_move, p->sound_move);
  sz_strlcpy(u->sound_move_alt, p->sound_move_alt);
  sz_strlcpy(u->sound_fight, p->sound_fight);
  sz_strlcpy(u->sound_fight_alt, p->sound_fight_alt);

  u->uclass             = uclass_by_number(p->unit_class_id);
  u->build_cost         = p->build_cost;
  u->pop_cost           = p->pop_cost;
  u->attack_strength    = p->attack_strength;
  u->defense_strength   = p->defense_strength;
  u->move_rate          = p->move_rate;
  u->tech_requirement   = p->tech_requirement;
  u->impr_requirement   = p->impr_requirement;
  u->gov_requirement = government_by_number(p->gov_requirement);
  u->vision_radius_sq = p->vision_radius_sq;
  u->transport_capacity = p->transport_capacity;
  u->hp                 = p->hp;
  u->firepower          = p->firepower;
  u->obsoleted_by = utype_by_number(p->obsoleted_by);
  u->fuel               = p->fuel;
  u->flags              = p->flags;
  u->roles              = p->roles;
  u->happy_cost         = p->happy_cost;
  output_type_iterate(o) {
    u->upkeep[o] = p->upkeep[o];
  } output_type_iterate_end;
  u->paratroopers_range = p->paratroopers_range;
  u->paratroopers_mr_req = p->paratroopers_mr_req;
  u->paratroopers_mr_sub = p->paratroopers_mr_sub;
  u->bombard_rate       = p->bombard_rate;
  u->cargo              = p->cargo;

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    sz_strlcpy(u->veteran[i].name, p->veteran_name[i]);
    u->veteran[i].power_fact = p->power_fact[i];
    u->veteran[i].move_bonus = p->move_bonus[i];
  }

  u->helptext = mystrdup(p->helptext);

  tileset_setup_unit_type(tileset, u);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_tech(struct packet_ruleset_tech *p)
{
  struct advance *a;

  if(p->id < 0 || p->id >= game.control.num_tech_types || p->id >= A_LAST) {
    freelog(LOG_ERROR,
	    "handle_ruleset_tech() bad advance %d.",
	    p->id);
    return;
  }
  a = &advances[p->id];

  sz_strlcpy(a->name.vernacular, p->name);
  a->name.translated = NULL;	/* tech.c advance_name_translation */
  sz_strlcpy(a->graphic_str, p->graphic_str);
  sz_strlcpy(a->graphic_alt, p->graphic_alt);
  a->req[0] = p->req[0];
  a->req[1] = p->req[1];
  a->root_req = p->root_req;
  a->flags = p->flags;
  a->preset_cost = p->preset_cost;
  a->num_reqs = p->num_reqs;
  a->helptext = mystrdup(p->helptext);
  
  tileset_setup_tech_type(tileset, p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_building(struct packet_ruleset_building *p)
{
  struct impr_type *b = improvement_by_number(p->id);
  int i;

  if (!b) {
    freelog(LOG_ERROR,
	    "handle_ruleset_building() bad improvement %d.",
	    p->id);
    return;
  }

  b->genus = p->genus;
  sz_strlcpy(b->name.vernacular, p->name);
  b->name.translated = NULL;	/* improvement.c improvement_name_translation */
  sz_strlcpy(b->graphic_str, p->graphic_str);
  sz_strlcpy(b->graphic_alt, p->graphic_alt);
  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&b->reqs, &p->reqs[i]);
  }
  assert(b->reqs.size == p->reqs_count);
  b->obsolete_by = p->obsolete_by;
  b->build_cost = p->build_cost;
  b->upkeep = p->upkeep;
  b->sabotage = p->sabotage;
  b->flags = p->flags;
  b->helptext = mystrdup(p->helptext);
  sz_strlcpy(b->soundtag, p->soundtag);
  sz_strlcpy(b->soundtag_alt, p->soundtag_alt);

#ifdef DEBUG
  if(p->id == game.control.num_impr_types-1) {
    impr_type_iterate(id) {
      b = improvement_by_number(id);
      freelog(LOG_DEBUG, "Impr: %s...",
	      b->name);
      if (tech_exists(b->obsolete_by)) {
	freelog(LOG_DEBUG, "  obsolete_by %2d/%s",
		b->obsolete_by,
		advance_rule_name(b->obsolete_by));
      } else {
	freelog(LOG_DEBUG, "  obsolete_by %2d/Never", b->obsolete_by);
      }
      freelog(LOG_DEBUG, "  build_cost %3d", b->build_cost);
      freelog(LOG_DEBUG, "  upkeep      %2d", b->upkeep);
      freelog(LOG_DEBUG, "  sabotage   %3d", b->sabotage);
      freelog(LOG_DEBUG, "  helptext    %s", b->helptext);
    } impr_type_iterate_end;
  }
#endif
  
  tileset_setup_impr_type(tileset, p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_government(struct packet_ruleset_government *p)
{
  struct government *gov;
  int j;

  if (p->id < 0 || p->id >= game.control.government_count) {
    freelog(LOG_ERROR,
	    "handle_ruleset_government() bad government %d.",
	    p->id);
    return;
  }
  gov = &governments[p->id];

  gov->index             = p->id;

  for (j = 0; j < p->reqs_count; j++) {
    requirement_vector_append(&gov->reqs, &p->reqs[j]);
  }
  assert(gov->reqs.size == p->reqs_count);

  gov->num_ruler_titles    = p->num_ruler_titles;
    
  sz_strlcpy(gov->name.vernacular, p->name);
  gov->name.translated = NULL;	/* government.c government_name_translation */
  sz_strlcpy(gov->graphic_str, p->graphic_str);
  sz_strlcpy(gov->graphic_alt, p->graphic_alt);

  gov->ruler_titles = fc_calloc(gov->num_ruler_titles,
				sizeof(struct ruler_title));

  gov->helptext = mystrdup(p->helptext);
  
  tileset_setup_government(tileset, p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_government_ruler_title
  (struct packet_ruleset_government_ruler_title *p)
{
  struct government *gov;

  if(p->gov < 0 || p->gov >= game.control.government_count) {
    freelog(LOG_ERROR,
            "handle_ruleset_government_ruler_title()"
            " bad government %d.",
            p->gov);
    return;
  }
  gov = &governments[p->gov];
  if(p->id < 0 || p->id >= gov->num_ruler_titles) {
    freelog(LOG_ERROR,
            "handle_ruleset_government_ruler_title()"
            " bad ruler title %d for government \"%s\".",
            p->id, gov->name.vernacular);
    return;
  }
  gov->ruler_titles[p->id].nation = nation_by_number(p->nation);
  /* government.c ruler_title_translation */
  sz_strlcpy(gov->ruler_titles[p->id].male.vernacular, p->male_title);
  gov->ruler_titles[p->id].male.translated = NULL;
  sz_strlcpy(gov->ruler_titles[p->id].female.vernacular, p->female_title);
  gov->ruler_titles[p->id].female.translated = NULL;
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_terrain(struct packet_ruleset_terrain *p)
{
  struct terrain *pterrain = terrain_by_number(p->id);
  int j;

  if (!pterrain) {
    freelog(LOG_ERROR,
	    "handle_ruleset_terrain() bad terrain %d.",
	    p->id);
    return;
  }

  pterrain->native_to = p->native_to;
  sz_strlcpy(pterrain->name.vernacular, p->name_orig);
  pterrain->name.translated = NULL;	/* terrain.c terrain_name_translation */
  sz_strlcpy(pterrain->graphic_str, p->graphic_str);
  sz_strlcpy(pterrain->graphic_alt, p->graphic_alt);
  pterrain->movement_cost = p->movement_cost;
  pterrain->defense_bonus = p->defense_bonus;

  output_type_iterate(o) {
    pterrain->output[o] = p->output[o];
  } output_type_iterate_end;

  pterrain->resources = fc_calloc(p->num_resources + 1,
				  sizeof(*pterrain->resources));
  for (j = 0; j < p->num_resources; j++) {
    pterrain->resources[j] = resource_by_number(p->resources[j]);
    if (!pterrain->resources[j]) {
      freelog(LOG_ERROR,
              "handle_ruleset_terrain()"
              " Mismatched resource %d for terrain \"%s\".",
              p->resources[j],
              terrain_rule_name(pterrain));
    }
  }
  pterrain->resources[p->num_resources] = NULL;

  pterrain->road_time = p->road_time;
  pterrain->road_trade_incr = p->road_trade_incr;
  pterrain->irrigation_result = terrain_by_number(p->irrigation_result);
  pterrain->irrigation_food_incr = p->irrigation_food_incr;
  pterrain->irrigation_time = p->irrigation_time;
  pterrain->mining_result = terrain_by_number(p->mining_result);
  pterrain->mining_shield_incr = p->mining_shield_incr;
  pterrain->mining_time = p->mining_time;
  pterrain->transform_result = terrain_by_number(p->transform_result);
  pterrain->transform_time = p->transform_time;
  pterrain->rail_time = p->rail_time;
  pterrain->airbase_time = p->airbase_time;
  pterrain->fortress_time = p->fortress_time;
  pterrain->clean_pollution_time = p->clean_pollution_time;
  pterrain->clean_fallout_time = p->clean_fallout_time;
  
  pterrain->flags = p->flags;

  pterrain->helptext = mystrdup(p->helptext);
  
  tileset_setup_tile_type(tileset, pterrain);
}

/****************************************************************************
  Handle a packet about a particular terrain resource.
****************************************************************************/
void handle_ruleset_resource(struct packet_ruleset_resource *p)
{
  struct resource *presource = resource_by_number(p->id);

  if (!presource) {
    freelog(LOG_ERROR,
	    "handle_ruleset_resource() bad resource %d.",
	    p->id);
    return;
  }

  sz_strlcpy(presource->name.vernacular, p->name_orig);
  presource->name.translated = NULL;	/* terrain.c resource_name_translation */
  sz_strlcpy(presource->graphic_str, p->graphic_str);
  sz_strlcpy(presource->graphic_alt, p->graphic_alt);

  output_type_iterate(o) {
    presource->output[o] = p->output[o];
  } output_type_iterate_end;

  tileset_setup_resource(tileset, presource);
}

/****************************************************************************
  Handle a packet about a particular base type.
****************************************************************************/
void handle_ruleset_base(struct packet_ruleset_base *p)
{
  struct base_type *pbase;
  int i;

  assert(p->id < game.control.num_base_types);

  pbase = base_type_get_by_id(p->id);

  if (!pbase) {
    freelog(LOG_ERROR,
            "Received bad base id %d in handle_ruleset_base",
            p->id);
    return;
  }

  sz_strlcpy(pbase->name_orig, p->name);
  pbase->name = Q_(pbase->name_orig);
  sz_strlcpy(pbase->graphic_str, p->graphic_str);
  sz_strlcpy(pbase->graphic_alt, p->graphic_alt);
  sz_strlcpy(pbase->activity_gfx, p->activity_gfx);

  for (i = 0; i < p->reqs_count; i++) {
    requirement_vector_append(&pbase->reqs, &p->reqs[i]);
  }
  assert(pbase->reqs.size == p->reqs_count);

  pbase->native_to = p->native_to;

  pbase->gui_type = p->gui_type;

  pbase->flags = p->flags;

  tileset_setup_base(tileset, pbase);
}

/**************************************************************************
  Handle the terrain control ruleset packet sent by the server.
**************************************************************************/
void handle_ruleset_terrain_control(struct packet_ruleset_terrain_control *p)
{
  /* Since terrain_control is the same as packet_ruleset_terrain_control
   * we can just copy the data directly. */
  terrain_control = *p;
}

/**************************************************************************
  Handle the list of groups, sent by the ruleset.
**************************************************************************/
void handle_ruleset_nation_groups(struct packet_ruleset_nation_groups *packet)
{
  int i;

  nation_groups_free();
  for (i = 0; i < packet->ngroups; i++) {
    struct nation_group *group = add_new_nation_group(packet->groups[i]);

    assert(group != NULL && group->index == i);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_nation(struct packet_ruleset_nation *p)
{
  int i;
  struct nation_type *pl;

  if (p->id < 0 || p->id >= game.control.nation_count) {
    freelog(LOG_ERROR,
	    "handle_ruleset_nation() bad nation %d.",
	    p->id);
    return;
  }
  pl = nation_by_number(p->id);

  sz_strlcpy(pl->name_single.vernacular, p->name);
  pl->name_single.translated = NULL;
  sz_strlcpy(pl->name_plural.vernacular, p->name_plural);
  pl->name_plural.translated = NULL;
  sz_strlcpy(pl->flag_graphic_str, p->graphic_str);
  sz_strlcpy(pl->flag_graphic_alt, p->graphic_alt);
  pl->leader_count = p->leader_count;
  pl->leaders = fc_malloc(sizeof(*pl->leaders) * pl->leader_count);
  for (i = 0; i < pl->leader_count; i++) {
    pl->leaders[i].name = mystrdup(p->leader_name[i]);
    pl->leaders[i].is_male = p->leader_sex[i];
  }
  pl->city_style = p->city_style;

  pl->is_playable = p->is_playable;
  pl->barb_type = p->barbarian_type;

  memcpy(pl->init_techs, p->init_techs, sizeof(pl->init_techs));
  memcpy(pl->init_buildings, p->init_buildings, 
         sizeof(pl->init_buildings));
  memcpy(pl->init_units, p->init_units, 
         sizeof(pl->init_units));
  pl->init_government = government_by_number(p->init_government);

  if (p->legend[0] != '\0') {
    pl->legend = mystrdup(_(p->legend));
  } else {
    pl->legend = mystrdup("");
  }

  pl->num_groups = p->ngroups;
  pl->groups = fc_malloc(sizeof(*(pl->groups)) * pl->num_groups);
  for (i = 0; i < p->ngroups; i++) {
    pl->groups[i] = nation_group_by_number(p->groups[i]);
    if (!pl->groups[i]) {
      freelog(LOG_FATAL, "Nation %s: Unknown group %d.",
		nation_rule_name(pl),
		p->groups[i]);
      exit(EXIT_FAILURE);
    }
  }

  pl->is_available = p->is_available;

  tileset_setup_nation_flag(tileset, p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_city(struct packet_ruleset_city *packet)
{
  int id, j;
  struct citystyle *cs;

  id = packet->style_id;
  if (id < 0 || id >= game.control.styles_count) {
    freelog(LOG_ERROR,
	    "handle_ruleset_city() bad citystyle %d.",
	    id);
    return;
  }
  cs = &city_styles[id];
  
  for (j = 0; j < packet->reqs_count; j++) {
    requirement_vector_append(&cs->reqs, &packet->reqs[j]);
  }
  assert(cs->reqs.size == packet->reqs_count);
  cs->replaced_by = packet->replaced_by;

  sz_strlcpy(cs->name.vernacular, packet->name);
  cs->name.translated = NULL;
  sz_strlcpy(cs->graphic, packet->graphic);
  sz_strlcpy(cs->graphic_alt, packet->graphic_alt);
  sz_strlcpy(cs->citizens_graphic, packet->citizens_graphic);
  sz_strlcpy(cs->citizens_graphic_alt, packet->citizens_graphic_alt);

  tileset_setup_city_tiles(tileset, id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_game(struct packet_ruleset_game *packet)
{
  int i;

  /* Must set num_specialist_types before iterating over them. */
  DEFAULT_SPECIALIST = packet->default_specialist;

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    game.work_veteran_chance[i] = packet->work_veteran_chance[i];
    game.veteran_chance[i] = packet->work_veteran_chance[i];
  }
}

/**************************************************************************
   Handle info about a single specialist.
**************************************************************************/
void handle_ruleset_specialist(struct packet_ruleset_specialist *p)
{
  int j;
  struct specialist *s;

  if (p->id < 0 || p->id >= game.control.num_specialist_types) {
    freelog(LOG_ERROR,
	    "handle_ruleset_specialist() bad specialist %d.",
	    p->id);
  }

  s = get_specialist(p->id);

  sz_strlcpy(s->name, p->name);
  sz_strlcpy(s->short_name, p->short_name);

  for (j = 0; j < p->reqs_count; j++) {
    requirement_vector_append(&s->reqs, &p->reqs[j]);
  }
  assert(s->reqs.size == p->reqs_count);

  tileset_setup_specialist_type(tileset, p->id);
}

/**************************************************************************
  ...
**************************************************************************/
void handle_unit_bribe_info(int unit_id, int cost)
{
  struct unit *punit = find_unit_by_id(unit_id);

  if (punit) {
    punit->bribe_cost = cost;
    if (game.player_ptr && !game.player_ptr->ai.control) {
      popup_bribe_dialog(punit);
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
void handle_city_incite_info(int city_id, int cost)
{
  struct city *pcity = find_city_by_id(city_id);

  if (pcity) {
    pcity->incite_revolt_cost = cost;
    if (game.player_ptr && !game.player_ptr->ai.control) {
      popup_incite_dialog(pcity);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_name_suggestion_info(int unit_id, char *name)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, unit_id);

  if (!can_client_issue_orders()) {
    return;
  }

  if (punit) {
    if (ask_city_name) {
      popup_newcity_dialog(punit, name);
    } else {
      dsend_packet_unit_build_city(&aconnection, unit_id,name);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_diplomat_popup_dialog(int diplomat_id, int target_id)
{
  struct unit *pdiplomat =
      player_find_unit_by_id(game.player_ptr, diplomat_id);

  if (pdiplomat && can_client_issue_orders()) {
    process_diplomat_arrival(pdiplomat, target_id);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_sabotage_list(int diplomat_id, int city_id,
			       bv_imprs improvements)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, diplomat_id);
  struct city *pcity = find_city_by_id(city_id);

  if (punit && pcity && can_client_issue_orders()) {
    impr_type_iterate(i) {
      pcity->improvements[i] = BV_ISSET(improvements, i) ? I_ACTIVE : I_NONE;
    } impr_type_iterate_end;

    popup_sabotage_dialog(pcity);
  }
}

/**************************************************************************
 Pass the packet on to be displayed in a gui-specific endgame dialog. 
**************************************************************************/
void handle_endgame_report(struct packet_endgame_report *packet)
{
  popup_endgame_report_dialog(packet);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_attribute_chunk(struct packet_player_attribute_chunk *packet)
{
  if (!game.player_ptr) {
    return;
  }
  generic_handle_player_attribute_chunk(game.player_ptr, packet);

  if (packet->offset + packet->chunk_length == packet->total_length) {
    /* We successful received the last chunk. The attribute block is
       now complete. */
      attribute_restore();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_processing_started(void)
{
  agents_processing_started();

  assert(aconnection.client.request_id_of_currently_handled_packet == 0);
  aconnection.client.request_id_of_currently_handled_packet =
      get_next_request_id(aconnection.
			  client.last_processed_request_id_seen);

  freelog(LOG_DEBUG, "start processing packet %d",
	  aconnection.client.request_id_of_currently_handled_packet);
}

/**************************************************************************
...
**************************************************************************/
void handle_processing_finished(void)
{
  int i;

  freelog(LOG_DEBUG, "finish processing packet %d",
	  aconnection.client.request_id_of_currently_handled_packet);

  assert(aconnection.client.request_id_of_currently_handled_packet != 0);

  aconnection.client.last_processed_request_id_seen =
      aconnection.client.request_id_of_currently_handled_packet;

  aconnection.client.request_id_of_currently_handled_packet = 0;

  for (i = 0; i < reports_thaw_requests_size; i++) {
    if (reports_thaw_requests[i] != 0 &&
	reports_thaw_requests[i] ==
	aconnection.client.last_processed_request_id_seen) {
      reports_thaw();
      reports_thaw_requests[i] = 0;
    }
  }

  agents_processing_finished();
}

/**************************************************************************
...
**************************************************************************/
void notify_about_incoming_packet(struct connection *pc,
				   int packet_type, int size)
{
  assert(pc == &aconnection);
  freelog(LOG_DEBUG, "incoming packet={type=%d, size=%d}", packet_type,
	  size);
}

/**************************************************************************
...
**************************************************************************/
void notify_about_outgoing_packet(struct connection *pc,
				  int packet_type, int size,
				  int request_id)
{
  assert(pc == &aconnection);
  freelog(LOG_DEBUG, "outgoing packet={type=%d, size=%d, request_id=%d}",
	  packet_type, size, request_id);

  assert(request_id);
}

/**************************************************************************
  ...
**************************************************************************/
void set_reports_thaw_request(int request_id)
{
  int i;

  for (i = 0; i < reports_thaw_requests_size; i++) {
    if (reports_thaw_requests[i] == 0) {
      reports_thaw_requests[i] = request_id;
      return;
    }
  }

  reports_thaw_requests_size++;
  reports_thaw_requests =
      fc_realloc(reports_thaw_requests,
		 reports_thaw_requests_size * sizeof(int));
  reports_thaw_requests[reports_thaw_requests_size - 1] = request_id;
}

/**************************************************************************
...
**************************************************************************/
void handle_freeze_hint(void)
{
  freelog(LOG_DEBUG, "handle_freeze_hint");

  reports_freeze();

  agents_freeze_hint();
}

/**************************************************************************
...
**************************************************************************/
void handle_thaw_hint(void)
{
  freelog(LOG_DEBUG, "handle_thaw_hint");

  reports_thaw();

  agents_thaw_hint();
  update_turn_done_button_state();
}

/**************************************************************************
...
**************************************************************************/
void handle_conn_ping(void)
{
  send_packet_conn_pong(&aconnection);
}

/**************************************************************************
...
**************************************************************************/
void handle_server_shutdown(void)
{
  freelog(LOG_VERBOSE, "server shutdown");
}

/**************************************************************************
  Add effect data to ruleset cache.  
**************************************************************************/
void handle_ruleset_effect(struct packet_ruleset_effect *packet)
{
  recv_ruleset_effect(packet);
}

/**************************************************************************
  Add effect requirement data to ruleset cache.  
**************************************************************************/
void handle_ruleset_effect_req(struct packet_ruleset_effect_req *packet)
{
  recv_ruleset_effect_req(packet);
}

