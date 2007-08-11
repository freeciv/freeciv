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
					   packet->type, packet->veteran);

  /* Owner, veteran, and type fields are already filled in by
   * create_unit_virtual. */
  punit->id = packet->id;
  punit->tile = map_pos_to_tile(packet->x, packet->y);
  punit->homecity = packet->homecity;
  punit->moves_left = packet->movesleft;
  punit->hp = packet->hp;
  punit->activity = packet->activity;
  punit->activity_count = packet->activity_count;
  punit->unhappiness = packet->unhappiness;
  punit->upkeep = packet->upkeep;
  punit->upkeep_food = packet->upkeep_food;
  punit->upkeep_gold = packet->upkeep_gold;
  punit->ai.control = packet->ai;
  punit->fuel = packet->fuel;
  if (is_normal_map_pos(packet->goto_dest_x, packet->goto_dest_y)) {
    punit->goto_tile = map_pos_to_tile(packet->goto_dest_x,
				       packet->goto_dest_y);
  } else {
    punit->goto_tile = NULL;
  }
  punit->activity_target = packet->activity_target;
  punit->paradropped = packet->paradropped;
  punit->done_moving = packet->done_moving;
  punit->occupy = packet->occupy;
  if (packet->transported) {
    punit->transported_by = packet->transported_by;
  } else {
    punit->transported_by = -1;
  }
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
					   packet->type, FALSE);

  /* Owner and type fields are already filled in by create_unit_virtual. */
  punit->id = packet->id;
  punit->tile = map_pos_to_tile(packet->x, packet->y);
  punit->veteran = packet->veteran;
  punit->hp = packet->hp;
  punit->activity = packet->activity;
  punit->occupy = (packet->occupied ? 1 : 0);
  if (packet->transported) {
    punit->transported_by = packet->transported_by;
  } else {
    punit->transported_by = -1;
  }

  return punit;
}

/**************************************************************************
...
**************************************************************************/
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
    set_client_page(PAGE_START);

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
    set_client_page(PAGE_MAIN);
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

/**************************************************************************
...
**************************************************************************/
void handle_city_remove(int city_id)
{
  struct city *pcity = find_city_by_id(city_id);
  struct tile *ptile;

  if (!pcity)
    return;

  agents_city_remove(pcity);

  ptile = pcity->tile;
  client_remove_city(pcity);
  reset_move_costs(ptile);

  /* update menus if the focus unit is on the tile. */
  if (get_unit_in_focus()) {
    update_menus();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_remove(int unit_id)
{
  struct unit *punit = find_unit_by_id(unit_id);
  struct player *powner;

  if (!punit) {
    return;
  }

  powner = unit_owner(punit);

  agents_unit_remove(punit);
  client_remove_unit(punit);

  if (powner == game.player_ptr) {
    activeunits_report_dialog_update();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_nuke_tile_info(int x, int y)
{
  flush_dirty();
  redraw_selection_rectangle();
  put_nuke_mushroom_pixmaps(map_pos_to_tile(x, y));
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_combat_info(int attacker_unit_id, int defender_unit_id,
			     int attacker_hp, int defender_hp,
			     bool make_winner_veteran)
{
  bool show_combat = FALSE;
  struct unit *punit0 = find_unit_by_id(attacker_unit_id);
  struct unit *punit1 = find_unit_by_id(defender_unit_id);

  if (punit0 && punit1) {
    struct unit *pwinner = (defender_hp == 0 ? punit0 : punit1);

    if (tile_visible_mapcanvas(punit0->tile) &&
	tile_visible_mapcanvas(punit1->tile)) {
      show_combat = TRUE;
    } else if (auto_center_on_combat) {
      if (punit0->owner == game.player_idx)
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
	flush_dirty();
	redraw_selection_rectangle();
	decrease_unit_hp_smooth(punit0, hp0, punit1, hp1);
	if (make_winner_veteran) {
	  pwinner->veteran++;
	  refresh_tile_mapcanvas(pwinner->tile, FALSE);
	}
      } else {
	punit0->hp = hp0;
	punit1->hp = hp1;

	set_units_in_combat(NULL, NULL);
	if (make_winner_veteran) {
	  pwinner->veteran++;
	}
	refresh_tile_mapcanvas(punit0->tile, FALSE);
	refresh_tile_mapcanvas(punit1->tile, FALSE);
      }
    } else {
      if (make_winner_veteran) {
	pwinner->veteran++;
	refresh_tile_mapcanvas(pwinner->tile, FALSE);
      }
    }
  }
}

/**************************************************************************
  Updates a city's list of improvements from packet data. "impr" identifies
  the improvement, and "have_impr" specifies whether the improvement should
  be added (TRUE) or removed (FALSE). "impr_changed" is set TRUE only if
  the existing improvement status was changed by this call.
**************************************************************************/
static void update_improvement_from_packet(struct city *pcity,
					   Impr_Type_id impr, bool have_impr,
					   bool *impr_changed)
{
  if (have_impr && pcity->improvements[impr] == I_NONE) {
    city_add_improvement(pcity, impr);

    if (impr_changed) {
      *impr_changed = TRUE;
    }
  } else if (!have_impr && pcity->improvements[impr] != I_NONE) {
    city_remove_improvement(pcity, impr);

    if (impr_changed) {
      *impr_changed = TRUE;
    }
  }
}

/**************************************************************************
  Possibly update city improvement effects.
**************************************************************************/
static void try_update_effects(bool need_update)
{
  if (need_update) {
    /* nothing yet... */
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_game_state(int value)
{
  bool changed = (get_client_state() != value);

  if (get_client_state() == CLIENT_SELECT_RACE_STATE
      && value == CLIENT_GAME_RUNNING_STATE
      && game.player_ptr->nation == NO_NATION_SELECTED) {
    popdown_races_dialog();
  }
  
  set_client_state(value);

  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    refresh_overview_canvas();
    player_set_unit_focus_status(game.player_ptr);

    update_info_label();	/* get initial population right */
    update_unit_focus();
    update_unit_info_label(get_unit_in_focus());

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

/**************************************************************************
...
**************************************************************************/
void handle_city_info(struct packet_city_info *packet)
{
  int i;
  bool city_is_new, city_has_changed_owner = FALSE, need_effect_update = FALSE;
  bool need_units_dialog_update = FALSE;
  struct city *pcity;
  bool popup, update_descriptions = FALSE, name_changed = FALSE;
  struct unit *pfocus_unit = get_unit_in_focus();

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner != packet->owner)) {
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
    } else if (draw_city_productions &&
	       (pcity->is_building_unit != packet->is_building_unit ||
		pcity->currently_building != packet->currently_building ||
		pcity->shield_surplus != packet->shield_surplus ||
		pcity->shield_stock != packet->shield_stock)) {
      update_descriptions = TRUE;
    } else if (draw_city_names && draw_city_growth &&
	       (pcity->food_stock != packet->food_stock ||
		pcity->food_surplus != packet->food_surplus)) {
      /* If either the food stock or surplus have changed, the time-to-grow
	 is likely to have changed as well. */
      update_descriptions = TRUE;
    }
    assert(pcity->id == packet->id);
  }
  
  pcity->owner=packet->owner;
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
  
  pcity->food_prod=packet->food_prod;
  pcity->food_surplus=packet->food_surplus;
  pcity->shield_prod=packet->shield_prod;
  pcity->shield_surplus=packet->shield_surplus;
  pcity->trade_prod=packet->trade_prod;
  pcity->tile_trade=packet->tile_trade;
  pcity->corruption=packet->corruption;
  pcity->shield_waste=packet->shield_waste;
    
  pcity->luxury_total=packet->luxury_total;
  pcity->tax_total=packet->tax_total;
  pcity->science_total=packet->science_total;
  
  pcity->food_stock=packet->food_stock;
  pcity->shield_stock=packet->shield_stock;
  pcity->pollution=packet->pollution;

  if (city_is_new
      || pcity->is_building_unit != packet->is_building_unit
      || pcity->currently_building != packet->currently_building) {
    need_units_dialog_update = TRUE;
  }
  pcity->is_building_unit=packet->is_building_unit;
  pcity->currently_building=packet->currently_building;
  if (city_is_new) {
    init_worklist(&pcity->worklist);

    /* Initialise list of improvements with city/building wide equiv_range. */
    improvement_status_init(pcity->improvements,
			    ARRAY_SIZE(pcity->improvements));
  }
  copy_worklist(&pcity->worklist, &packet->worklist);
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;

  pcity->turn_last_built=packet->turn_last_built;
  pcity->turn_founded = packet->turn_founded;
  pcity->changed_from_id=packet->changed_from_id;
  pcity->changed_from_is_unit=packet->changed_from_is_unit;
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
    if (pcity->improvements[i] == I_NONE && packet->improvements[i] == '1'
	&& !city_is_new) {
      audio_play_sound(get_improvement_type(i)->soundtag,
		       get_improvement_type(i)->soundtag_alt);
    }
    update_improvement_from_packet(pcity, i, packet->improvements[i] == '1',
                                   &need_effect_update);
  } impr_type_iterate_end;

  /* We should be able to see units in the city.  But for a diplomat
   * investigating an enemy city we can't.  In that case we don't update
   * the occupied flag at all: it's already been set earlier and we'll
   * get an update if it changes. */
  if (can_player_see_units_in_city(game.player_ptr, pcity)) {
    pcity->client.occupied
      = (unit_list_size(&pcity->tile->units) > 0);
  }

  pcity->client.happy = city_happy(pcity);
  pcity->client.unhappy = city_unhappy(pcity);

  popup = (city_is_new && can_client_change_view()
           && pcity->owner == game.player_idx && popup_new_cities)
          || packet->diplomat_investigate;

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  handle_city_packet_common(pcity, city_is_new, popup,
			    packet->diplomat_investigate);

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }

  /* Update focus unit info label if necessary. */
  if (name_changed && pfocus_unit && pfocus_unit->homecity == pcity->id) {
    update_unit_info_label(pfocus_unit);
  }

  /* Update the units dialog if necessary. */
  if (need_units_dialog_update) {
    activeunits_report_dialog_update();
  }

  /* Update the panel text (including civ population). */
  update_info_label();

  try_update_effects(need_effect_update);
}

/**************************************************************************
  ...
**************************************************************************/
static void handle_city_packet_common(struct city *pcity, bool is_new,
                                      bool popup, bool investigate)
{
  int i;

  if(is_new) {
    unit_list_init(&pcity->units_supported);
    unit_list_init(&pcity->info_units_supported);
    unit_list_init(&pcity->info_units_present);
    city_list_insert(&city_owner(pcity)->cities, pcity);
    map_set_city(pcity->tile, pcity);
    if(pcity->owner==game.player_idx)
      city_report_dialog_update();

    for(i=0; i<game.nplayers; i++) {
      unit_list_iterate(game.players[i].units, punit) 
	if(punit->homecity==pcity->id)
	  unit_list_insert(&pcity->units_supported, punit);
      unit_list_iterate_end;
    }
  } else {
    if(pcity->owner == game.player_idx) {
      city_report_dialog_update_city(pcity);
    }
  }

  if ((draw_map_grid || draw_borders) && can_client_change_view()) {
    /* We have to make sure we update any workers on the map grid, then
     * redraw the city descriptions on top of them.  So we calculate the
     * rectangle covered by the city's map, and update that.  Then we
     * queue up a city description redraw for later.
     *
     * HACK: The +2 below accounts for grid lines that may actually be on a
     * tile outside of the city radius. */
    int canvas_x, canvas_y;
    int width = get_citydlg_canvas_width() + 2;
    int height = get_citydlg_canvas_height() + 2;

    (void) tile_to_canvas_pos(&canvas_x, &canvas_y, pcity->tile);

    update_map_canvas(canvas_x - (width - NORMAL_TILE_WIDTH) / 2,
		      canvas_y - (height - NORMAL_TILE_HEIGHT) / 2,
		      width, height);
    overview_update_tile(pcity->tile);
  } else {
    refresh_tile_mapcanvas(pcity->tile, FALSE);
  }

  if (city_workers_display==pcity)  {
    city_workers_display=NULL;
  }

  if (popup
      && (!game.player_ptr->ai.control || ai_popup_windows)
      && can_client_issue_orders()) {
    update_menus();
    if (!city_dialog_is_open(pcity)) {
      popup_city_dialog(pcity, FALSE);
    }
  }

  if (!is_new && (pcity->owner==game.player_idx
		  || popup)) {
    refresh_city_dialog(pcity);
  }

  /* update menus if the focus unit is on the tile. */
  {
    struct unit *punit = get_unit_in_focus();
    if (punit && same_pos(punit->tile, pcity->tile)) {
      update_menus();
    }
  }

  if(is_new) {
    freelog(LOG_DEBUG, "New %s city %s id %d (%d %d)",
	    get_nation_name(city_owner(pcity)->nation),
	    pcity->name, pcity->id, TILE_XY(pcity->tile));
  }

  reset_move_costs(pcity->tile);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_short_info(struct packet_city_short_info *packet)
{
  struct city *pcity;
  bool city_is_new, city_has_changed_owner = FALSE, need_effect_update = FALSE;
  bool update_descriptions = FALSE;

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner != packet->owner)) {
    client_remove_city(pcity);
    pcity = NULL;
    city_has_changed_owner = TRUE;
  }

  if(!pcity) {
    city_is_new = TRUE;
    pcity=fc_malloc(sizeof(struct city));
    pcity->id=packet->id;
    idex_register_city(pcity);
  }
  else {
    city_is_new = FALSE;

    /* Check if city desciptions should be updated */
    if (draw_city_names && strcmp(pcity->name, packet->name) != 0) {
      update_descriptions = TRUE;
    }
    
    assert(pcity->id == packet->id);
  }

  pcity->owner=packet->owner;
  pcity->tile = map_pos_to_tile(packet->x, packet->y);
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;
  pcity->tile_trade = packet->tile_trade;

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
    /* Initialise list of improvements with city/building wide equiv_range. */
    improvement_status_init(pcity->improvements,
			    ARRAY_SIZE(pcity->improvements));
  }

  update_improvement_from_packet(pcity, game.palace_building,
				 packet->capital, &need_effect_update);
  update_improvement_from_packet(pcity, game.land_defend_building,
				 packet->walls, &need_effect_update);

  if (city_is_new) {
    init_worklist(&pcity->worklist);
  }

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
    pcity->food_prod          = 0;
    pcity->food_surplus       = 0;
    pcity->shield_prod        = 0;
    pcity->shield_surplus     = 0;
    pcity->trade_prod         = 0;
    pcity->corruption         = 0;
    pcity->luxury_total       = 0;
    pcity->tax_total          = 0;
    pcity->science_total      = 0;
    pcity->food_stock         = 0;
    pcity->shield_stock       = 0;
    pcity->pollution          = 0;
    pcity->city_options       = 0;
    pcity->is_building_unit   = FALSE;
    pcity->currently_building = 0;
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

  handle_city_packet_common(pcity, city_is_new, FALSE, FALSE);

  /* Update the description if necessary. */
  if (update_descriptions) {
    update_city_description(pcity);
  }

  try_update_effects(need_effect_update);
}

/**************************************************************************
...
**************************************************************************/
void handle_new_year(int year, int turn)
{
  game.year = year;
  /*
   * The turn was increased in handle_before_new_year()
   */
  assert(game.turn == turn);
  update_info_label();

  player_set_unit_focus_status(game.player_ptr);
  city_list_iterate(game.player_ptr->cities, pcity) {
    pcity->client.colored = FALSE;
  } city_list_iterate_end;
  unit_list_iterate(game.player_ptr->units, punit) {
    punit->client.colored = FALSE;
  } unit_list_iterate_end;
  update_unit_focus();
  auto_center_on_focus_unit();

  update_unit_info_label(get_unit_in_focus());
  update_menus();

  seconds_to_turndone=game.timeout;

#if 0
  /* This information shouldn't be needed, but if it is this is the only
   * way we can get it. */
  turn_gold_difference=game.player_ptr->economic.gold-last_turn_gold_amount;
  last_turn_gold_amount=game.player_ptr->economic.gold;
#endif

  queue_mapview_update(UPDATE_MAP_CANVAS_VISIBLE);

  if (sound_bell_at_new_turn &&
      (!game.player_ptr->ai.control || ai_manual_turn_done)) {
    create_event(NULL, E_TURN_BELL, _("Start of turn %d"), game.turn);
  }

  agents_new_turn();
}

/**************************************************************************
...
**************************************************************************/
void handle_before_new_year(void)
{
  clear_notify_window();
  /*
   * The local idea of the game turn is increased here since the
   * client will get unit updates (reset of move points for example)
   * between handle_before_new_year() and handle_new_year(). These
   * unit updates will look like they did take place in the old turn
   * which is incorrect. If we get the authoritative information about
   * the game turn in handle_new_year() we will check it.
   */
  game.turn++;
  agents_before_new_turn();
}

/**************************************************************************
...
**************************************************************************/
void handle_start_turn(void)
{
  agents_start_turn();
  non_ai_unit_focus = FALSE;

  turn_done_sent = FALSE;
  update_turn_done_button_state();

  if(game.player_ptr->ai.control && !ai_manual_turn_done) {
    user_ended_turn();
  }
}

/**************************************************************************
...
**************************************************************************/
void play_sound_for_event(enum event_type type)
{
  const char *sound_tag = get_sound_tag_for_event(type);

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

  if (!game.player_ptr->ai.control || ai_popup_windows ||
      event != E_BROADCAST_REPORT) {
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

  if (packet->owner != game.player_idx ) {
    freelog(LOG_ERROR, "Got packet_unit_info for unit of %s.",
            game.players[packet->owner].name);
  }

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
  struct unit *focus_unit = get_unit_in_focus();
  
  punit = player_find_unit_by_id(get_player(packet_unit->owner),
				 packet_unit->id);

  if (punit) {
    ret = TRUE;
    punit->activity_count = packet_unit->activity_count;
    if (punit->ai.control != packet_unit->ai.control) {
      punit->ai.control = packet_unit->ai.control;
      repaint_unit = TRUE;
      /* AI is set:     may change focus */
      /* AI is cleared: keep focus */
      if (packet_unit->ai.control && punit == get_unit_in_focus()) {
        check_focus = TRUE;
      }
    }

    if (punit->activity != packet_unit->activity
	|| punit->activity_target != packet_unit->activity_target
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
      if (punit == get_unit_in_focus()
	  && (packet_unit->activity != ACTIVITY_IDLE
	      || packet_unit->has_orders)) {
        check_focus = TRUE;
      }

      repaint_unit = TRUE;

      /* Wakeup Focus */
      if (wakeup_focus 
          && !game.player_ptr->ai.control
          && punit->owner == game.player_idx
          && punit->activity == ACTIVITY_SENTRY
          && packet_unit->activity == ACTIVITY_IDLE
          && (!get_unit_in_focus()
              /* only 1 wakeup focus per tile is useful */
              || !same_pos(packet_unit->tile, get_unit_in_focus()->tile))) {
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

      if (punit->occupy != packet_unit->occupy
	  && focus_unit && focus_unit->tile == packet_unit->tile) {
	/* Special case: (un)loading a unit in a transporter on the
	 * same tile as the focus unit may (dis)allow the focus unit to be
	 * loaded.  Thus the orders->(un)load menu item needs updating. */
	need_update_menus = TRUE;
      }
      punit->occupy = packet_unit->occupy;
      punit->transported_by = packet_unit->transported_by;

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

      if (punit->owner == game.player_idx) {
        refresh_unit_city_dialogs(punit);
      }
    } /*** End of Change in activity or activity's target. ***/

    /* These two lines force the menus to be updated as appropriate when
     * the focus unit changes. */
    if (punit == get_unit_in_focus()) {
      need_update_menus = TRUE;
    }

    if (punit->homecity != packet_unit->homecity) {
      /* change homecity */
      struct city *pcity;
      if ((pcity=find_city_by_id(punit->homecity))) {
	unit_list_unlink(&pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
      
      punit->homecity = packet_unit->homecity;
      if ((pcity=find_city_by_id(punit->homecity))) {
	unit_list_insert(&pcity->units_supported, punit);
	repaint_city = TRUE;
      }
    }

    if (punit->hp != packet_unit->hp) {
      /* hp changed */
      punit->hp = packet_unit->hp;
      repaint_unit = TRUE;
    }

    if (punit->type != packet_unit->type) {
      /* Unit type has changed (been upgraded) */
      struct city *pcity = map_get_city(punit->tile);
      
      punit->type = packet_unit->type;
      repaint_unit = TRUE;
      repaint_city = TRUE;
      if (pcity && (pcity->id != punit->homecity)) {
	refresh_city_dialog(pcity);
      }
      if(punit == get_unit_in_focus()) {
        /* Update the orders menu -- the unit might have new abilities */
	need_update_menus = TRUE;
      }
    }

    /* May change focus if an attempted move or attack exhausted unit */
    if (punit->moves_left != packet_unit->moves_left
        && punit == get_unit_in_focus()) {
      check_focus = TRUE;
    }

    if (!same_pos(punit->tile, packet_unit->tile)) { 
      /*** Change position ***/
      struct city *pcity = map_get_city(punit->tile);

      old_tile = punit->tile;
      moved = TRUE;

      /* Show where the unit is going. */
      do_move_unit(punit, packet_unit);
      if (punit->transported_by == -1) {
	/* Repaint if the unit isn't transported.  do_move_unit erases the
	 * unit's old position and animates, but doesn't update the unit's
	 * new position. */
	repaint_unit = TRUE;
      }

      if(pcity)  {
	if (can_player_see_units_in_city(game.player_ptr, pcity)) {
	  /* Unit moved out of a city - update the occupied status. */
	  bool new_occupied =
	    (unit_list_size(&pcity->tile->units) > 0);

	  if (pcity->client.occupied != new_occupied) {
	    pcity->client.occupied = new_occupied;
	    refresh_tile_mapcanvas(pcity->tile, FALSE);
	  }
	}

        if(pcity->id==punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
      }
      
      if((pcity=map_get_city(punit->tile)))  {
	if (can_player_see_units_in_city(game.player_ptr, pcity)) {
	  /* Unit moved into a city - obviously it's occupied. */
	  if (!pcity->client.occupied) {
	    pcity->client.occupied = TRUE;
	    refresh_tile_mapcanvas(pcity->tile, FALSE);
	  }
	}

        if(pcity->id == punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
	
        if((unit_flag(punit, F_TRADE_ROUTE) || unit_flag(punit, F_HELP_WONDER))
	   && (!game.player_ptr->ai.control || ai_popup_windows)
	   && punit->owner==game.player_idx
	   && !unit_has_orders(punit)
	   && can_client_issue_orders()
	   && (unit_can_help_build_wonder_here(punit)
	       || unit_can_est_traderoute_here(punit))) {
	  process_caravan_arrival(punit);
	}
      }

    }  /*** End of Change position. ***/

    if (punit->unhappiness != packet_unit->unhappiness) {
      punit->unhappiness = packet_unit->unhappiness;
      repaint_city = TRUE;
    }
    if (punit->upkeep != packet_unit->upkeep) {
      punit->upkeep = packet_unit->upkeep;
      repaint_city = TRUE;
    }
    if (punit->upkeep_food != packet_unit->upkeep_food) {
      punit->upkeep_food = packet_unit->upkeep_food;
      repaint_city = TRUE;
    }
    if (punit->upkeep_gold != packet_unit->upkeep_gold) {
      punit->upkeep_gold = packet_unit->upkeep_gold;
      repaint_city = TRUE;
    }
    if (repaint_city || repaint_unit) {
      /* We repaint the city if the unit itself needs repainting or if
       * there a special city-only redrawing to be done. */
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

    unit_list_insert(&get_player(punit->owner)->units, punit);
    unit_list_insert(&punit->tile->units, punit);

    if((pcity=find_city_by_id(punit->homecity))) {
      unit_list_insert(&pcity->units_supported, punit);
    }

    freelog(LOG_DEBUG, "New %s %s id %d (%d %d) hc %d %s", 
	    get_nation_name(unit_owner(punit)->nation),
	    unit_name(punit->type), TILE_XY(punit->tile), punit->id,
	    punit->homecity, (pcity ? pcity->name : _("(unknown)")));

    repaint_unit = (punit->transported_by == -1);
    agents_unit_new(punit);

    if ((pcity = map_get_city(punit->tile))) {
      /* The unit is in a city - obviously it's occupied. */
      pcity->client.occupied = TRUE;
    }
  } /*** End of Create new unit ***/

  assert(punit != NULL);

  if (punit == get_unit_in_focus()) {
    update_unit_info_label(punit);
  } else if (get_unit_in_focus()
	     && (same_pos(get_unit_in_focus()->tile, punit->tile)
		 || (moved
		     && same_pos(get_unit_in_focus()->tile, old_tile)))) {
    update_unit_info_label(get_unit_in_focus());
  }

  if (repaint_unit) {
    if (unit_type_flag(punit->type, F_CITIES)) {
      int width = get_citydlg_canvas_width();
      int height = get_citydlg_canvas_height();
      int canvas_x, canvas_y;

      tile_to_canvas_pos(&canvas_x, &canvas_y, punit->tile);
      update_map_canvas(canvas_x - (width - NORMAL_TILE_WIDTH) / 2,
			canvas_y - (height - NORMAL_TILE_HEIGHT) / 2,
			width, height);
    } else {
      refresh_tile_mapcanvas(punit->tile, FALSE);
    }
  }

  if ((check_focus || get_unit_in_focus() == NULL) &&
      !game.player_ptr->ai.control) {
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
      unit_list_unlink_all(&(pcity->info_units_supported));
      unit_list_iterate(pcity->info_units_present, ppunit) {
	destroy_unit_virtual(ppunit);
      } unit_list_iterate_end;
      unit_list_unlink_all(&(pcity->info_units_present));
    }

    /* Okay, append a unit struct to the proper list. */
    punit = unpackage_short_unit(packet);
    if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED) {
      unit_list_insert(&(pcity->info_units_supported), punit);
    } else {
      assert(packet->packet_use == UNIT_INFO_CITY_PRESENT);
      unit_list_insert(&(pcity->info_units_present), punit);
    }

    /* Done with special case. */
    return;
  }

  if (packet->owner == game.player_idx ) {
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
  map.xsize = xsize;
  map.ysize = ysize;
  map.topology_id = topology_id;

  /* Parameter is FALSE so that sizes are kept unchanged. */
  map_init_topology(FALSE);

  map_allocate();
  init_client_goto();

  generate_citydlg_dimensions();

  set_overview_dimensions(map.xsize, map.ysize);
}

/**************************************************************************
...
**************************************************************************/
void handle_game_info(struct packet_game_info *pinfo)
{
  int i;
  bool boot_help, need_effect_update = FALSE;

  game.gold=pinfo->gold;
  game.tech=pinfo->tech;
  game.researchcost=pinfo->researchcost;
  game.skill_level=pinfo->skill_level;
  game.timeout=pinfo->timeout;
  game.diplomacy = pinfo->diplomacy;

  game.end_year=pinfo->end_year;
  game.year=pinfo->year;
  game.turn=pinfo->turn;
  game.min_players=pinfo->min_players;
  game.max_players=pinfo->max_players;
  game.nplayers=pinfo->nplayers;
  game.globalwarming=pinfo->globalwarming;
  game.heating=pinfo->heating;
  game.nuclearwinter=pinfo->nuclearwinter;
  game.cooling=pinfo->cooling;
  if (!can_client_change_view()) {
    /*
     * Hack to allow code that explicitly checks for Palace or City Walls
     * to work.
     */
    game.palace_building = get_building_for_effect(EFT_CAPITAL_CITY);
    if (game.palace_building == B_LAST) {
      freelog(LOG_FATAL, "Cannot find any palace building");
    }

    game.land_defend_building = get_building_for_effect(EFT_LAND_DEFEND);
    if (game.land_defend_building == B_LAST) {
      freelog(LOG_FATAL, "Cannot find any land defend building");
    }

    improvement_status_init(game.improvements,
			    ARRAY_SIZE(game.improvements));

    game.player_idx = pinfo->player_idx;
    game.player_ptr = &game.players[game.player_idx];
  }
  for(i=0; i<A_LAST/*game.num_tech_types*/; i++)
    game.global_advances[i]=pinfo->global_advances[i];
  for(i=0; i<B_LAST/*game.num_impr_types*/; i++) {
     game.global_wonders[i]=pinfo->global_wonders[i];
/* Only add in the improvement if it's in a "foreign" (i.e. unknown) city
   and has equiv_range==World - otherwise we deal with it in its home
   city anyway */
    if (is_wonder(i) && improvement_types[i].equiv_range==EFR_WORLD &&
        !find_city_by_id(game.global_wonders[i])) {
      if (game.global_wonders[i] <= 0 && game.improvements[i] != I_NONE) {
        game.improvements[i] = I_NONE;
        need_effect_update = TRUE;
      } else if (game.global_wonders[i] > 0 && game.improvements[i] == I_NONE) {
        game.improvements[i] = I_ACTIVE;
        need_effect_update = TRUE;
      }
    }
  }

  /* Only update effects if a new wonder appeared or was destroyed */
  try_update_effects(need_effect_update);

  if (get_client_state() == CLIENT_SELECT_RACE_STATE) {
    popdown_races_dialog();
  }
  game.techpenalty=pinfo->techpenalty;
  game.foodbox=pinfo->foodbox;
  game.civstyle=pinfo->civstyle;
  game.unhappysize = pinfo->unhappysize;
  game.cityfactor = pinfo->cityfactor;

  boot_help = (can_client_change_view()
	       && game.spacerace != pinfo->spacerace);
  game.spacerace=pinfo->spacerace;
  if (game.timeout != 0) {
    if (pinfo->seconds_to_turndone != 0)
      seconds_to_turndone = pinfo->seconds_to_turndone;
  } else
    seconds_to_turndone = 0;
  if (boot_help) {
    boot_help_texts();		/* reboot, after setting game.spacerace */
  }

  update_unit_focus();
}

/**************************************************************************
...
**************************************************************************/
static bool read_player_info_techs(struct player *pplayer,
				   char *inventions)
{
  bool need_effect_update = FALSE;

  tech_type_iterate(i) {
    enum tech_state oldstate = pplayer->research.inventions[i].state;
    enum tech_state newstate = inventions[i] - '0';

    pplayer->research.inventions[i].state = newstate;
    if (newstate != oldstate
	&& (newstate == TECH_KNOWN || oldstate == TECH_KNOWN)) {
      need_effect_update = TRUE;
    }
  } tech_type_iterate_end;

  if (need_effect_update) {
    improvements_update_obsolete();
    update_menus();
  }

  update_research(pplayer);
  return need_effect_update;
}

/**************************************************************************
  Sets the target government.  This will automatically start a revolution
  if the target government differs from the current one.
**************************************************************************/
void set_government_choice(int government)
{
  if (government != game.player_ptr->government
      && can_client_issue_orders()) {
    dsend_packet_player_change_government(&aconnection, government);
  }
}

/**************************************************************************
  Begin a revolution by telling the server to start it.  This also clears
  the current government choice.
**************************************************************************/
void start_revolution(void)
{
  dsend_packet_player_change_government(&aconnection,
					game.government_when_anarchy);
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

  sz_strlcpy(pplayer->name, pinfo->name);

  pplayer->nation=pinfo->nation;
  pplayer->is_male=pinfo->is_male;
  pplayer->team = pinfo->team;

  pplayer->economic.gold=pinfo->gold;
  pplayer->economic.tax=pinfo->tax;
  pplayer->economic.science=pinfo->science;
  pplayer->economic.luxury=pinfo->luxury;
  pplayer->government=pinfo->government;
  pplayer->target_government = pinfo->target_government;
  pplayer->embassy=pinfo->embassy;
  pplayer->gives_shared_vision = pinfo->gives_shared_vision;
  pplayer->city_style=pinfo->city_style;
  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->ai.love[i] = pinfo->love[i];
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
  pplayer->reputation = pinfo->reputation;

  pplayer->is_connected = pinfo->is_connected;

  /* If the server sends out player information at the wrong time, it is
   * likely to give us inconsistent player tech information, causing a
   * sanity-check failure within this function.  Fixing this at the client
   * end is very tricky; it's hard to figure out when to read the techs
   * and when to ignore them.  The current solution is that the server should
   * only send the player info out at appropriate times - e.g., while the
   * game is running. */
  new_tech = read_player_info_techs(pplayer, pinfo->inventions);

  poptechup = (pplayer->research.researching != pinfo->researching
               || pplayer->ai.tech_goal != pinfo->tech_goal);
  pplayer->research.bulbs_last_turn = pinfo->bulbs_last_turn;
  pplayer->research.bulbs_researched = pinfo->bulbs_researched;
  pplayer->research.techs_researched = pinfo->techs_researched;
  pplayer->research.researching=pinfo->researching;
  pplayer->future_tech=pinfo->future_tech;
  pplayer->ai.tech_goal=pinfo->tech_goal;
  
  if (can_client_change_view() && pplayer == game.player_ptr) {
    if (poptechup || new_tech) {
      science_dialog_update();
    }
    if (poptechup) {
      if (!game.player_ptr->ai.control || ai_popup_windows) {
	popup_science_dialog(FALSE);
      }
    }
    if (new_tech) {
      /* If we just learned bridge building and focus is on a settler
	 on a river the road menu item will remain disabled unless we
	 do this. (applys in other cases as well.) */
      if (get_unit_in_focus()) {
	update_menus();
      }
    }
    economy_report_dialog_update();
    activeunits_report_dialog_update();
    city_report_dialog_update();
  }

  if (pplayer == game.player_ptr && pplayer->turn_done != pinfo->turn_done) {
    update_turn_done_button_state();
  }
  pplayer->turn_done=pinfo->turn_done;

  pplayer->nturns_idle=pinfo->nturns_idle;
  pplayer->is_alive=pinfo->is_alive;

  pplayer->ai.barbarian_type = pinfo->barbarian_type;
  pplayer->revolution_finishes = pinfo->revolution_finishes;
  if(pplayer->ai.control!=pinfo->ai)  {
    pplayer->ai.control=pinfo->ai;
    if(pplayer==game.player_ptr)  {
      my_snprintf(msg, sizeof(msg), _("AI Mode is now %s."),
		  game.player_ptr->ai.control?_("ON"):_("OFF"));
      append_output_window(msg);
    }
  }

  update_players_dialog();
  update_worklist_report_dialog();
  upgrade_canvas_clipboard();

  if (pplayer == game.player_ptr && can_client_change_view()) {
    update_info_label();
  }

  /* if the server requests that the client reset, then information about
   * connections to this player are lost. If this is the case, insert the
   * correct conn back into the player->connections list */
  if (conn_list_size(&pplayer->connections) == 0) {
    conn_list_iterate(game.est_connections, pconn) {
      if (pconn->player == pplayer) {
        /* insert the controller into first position */
        if (pconn->observer) {
          conn_list_insert_back(&pplayer->connections, pconn);
        } else {
          conn_list_insert(&pplayer->connections, pconn);
        }
      }
    } conn_list_iterate_end;
  }

  if (has_capability("username_info", aconnection.capability)) {
    sz_strlcpy(pplayer->username, pinfo->username);
  } else {
    conn_list_iterate(game.est_connections, pconn) {
      if (pconn->player == pplayer && !pconn->observer) {
        sz_strlcpy(pplayer->username, pconn->username);
      }
    } conn_list_iterate_end;
  }

  /* Just about any changes above require an update to the intelligence
   * dialog. */
  update_intel_dialog(pplayer);
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
	conn_list_insert_back(&pplayer->connections, pconn);
      }
      conn_list_insert_back(&game.all_connections, pconn);
      conn_list_insert_back(&game.est_connections, pconn);
      conn_list_insert_back(&game.game_connections, pconn);
    } else {
      freelog(LOG_DEBUG, "Server reports updated connection %d %s",
	      pinfo->id, pinfo->username);
      if (pplayer != pconn->player) {
	if (pconn->player) {
	  conn_list_unlink(&pconn->player->connections, pconn);
	}
	if (pplayer) {
	  conn_list_insert_back(&pplayer->connections, pconn);
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
...
**************************************************************************/
void handle_conn_ping_info(struct packet_conn_ping_info *packet)
{
  int i;

  for (i = 0; i < packet->connections; i++) {
    struct connection *pconn = find_conn_by_id(packet->conn_id[i]);

    if (!pconn) {
      continue;
    }

    pconn->ping_time = packet->ping_time[i];
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
      freelog(LOG_ERROR, "invalid spaceship structure '%c' %d",
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
  enum known_type old_known = ptile->known;
  bool tile_changed = FALSE;
  bool known_changed = FALSE;

  if (ptile->terrain != packet->type) { /*terrain*/
    tile_changed = TRUE;
    ptile->terrain = packet->type;
  }
  if (ptile->special != packet->special) { /*add-on*/
    tile_changed = TRUE;
    ptile->special = packet->special;
  }
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
  if (ptile->known != packet->known) {
    known_changed = TRUE;
  }
  ptile->known = packet->known;

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

  reset_move_costs(ptile);

  if (ptile->known <= TILE_KNOWN_FOGGED && old_known == TILE_KNOWN) {
    /* This is an error.  So first we log the error, then make an assertion.
     * But for NDEBUG clients we fix the error. */
    unit_list_iterate(ptile->units, punit) {
      freelog(LOG_ERROR, "%p %s at (%d,%d) %s", punit,
	      unit_type(punit)->name, TILE_XY(punit->tile),
	      unit_owner(punit)->name);
    } unit_list_iterate_end;
    unit_list_unlink_all(&ptile->units);
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
    allot_island_improvs();
  }

  if (known_changed || tile_changed) {
    /* 
     * A tile can only change if it was known before and is still
     * known. In the other cases the tile is new or removed.
     */
    if (known_changed && ptile->known == TILE_KNOWN) {
      agents_tile_new(ptile);
    } else if (known_changed && ptile->known == TILE_KNOWN_FOGGED) {
      agents_tile_remove(ptile);
    } else {
      agents_tile_changed(ptile);
    }
  }

  /* refresh tiles */
  if (can_client_change_view()) {
    /* the tile itself */
    if (tile_changed || old_known!=ptile->known)
      refresh_tile_mapcanvas(ptile, FALSE);

    /* if the terrain or the specials of the tile
       have changed it affects the adjacent tiles */
    if (tile_changed) {
      adjc_iterate(ptile, tile1) {
	if (tile_get_known(tile1) >= TILE_KNOWN_FOGGED)
	  refresh_tile_mapcanvas(tile1, FALSE);
      }
      adjc_iterate_end;
      return;
    }

    /* the "furry edges" on tiles adjacent to an TILE_UNKNOWN tile are
       removed here */
    if (old_known == TILE_UNKNOWN && packet->known >= TILE_KNOWN_FOGGED) {     
      cardinal_adjc_iterate(ptile, tile1) {
	if (tile_get_known(tile1) >= TILE_KNOWN_FOGGED)
	  refresh_tile_mapcanvas(tile1, FALSE);
      } cardinal_adjc_iterate_end;
    }
  }

  /* update menus if the focus unit is on the tile. */
  if (tile_changed) {
    struct unit *punit = get_unit_in_focus();
    if (punit && same_pos(punit->tile, ptile)) {
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
}

/**************************************************************************
...
**************************************************************************/
void handle_nation_select_ok(void)
{
  if (get_client_state() == CLIENT_SELECT_RACE_STATE) {
    set_client_state(CLIENT_WAITING_FOR_GAME_START_STATE);
    popdown_races_dialog();
  } else {
    freelog(LOG_ERROR,
	    "got a select nation packet in an incompatible state");
  }
}

static bool *nations_used;

/**************************************************************************
  Mark a nation as unavailable, after we've entered the select-race state.
**************************************************************************/
void handle_nation_unavailable(Nation_Type_id nation)
{
  if (get_client_state() == CLIENT_SELECT_RACE_STATE
      && nation >= 0 && nation < game.playable_nation_count) {
    if (!nations_used[nation]) {
      nations_used[nation] = TRUE;
      races_toggles_set_sensitive(nations_used);
    }
  } else {
    freelog(LOG_ERROR,
	    "got a select nation packet in an incompatible state");
  }
}

/**************************************************************************
  Enter the select races state.
**************************************************************************/
void handle_select_races(void)
{
  if (get_client_state() == CLIENT_PRE_GAME_STATE) {
    /* First set the state. */
    set_client_state(CLIENT_SELECT_RACE_STATE);

    /* Then clear the nations used.  They are filled by a
     * PACKET_NATION_UNAVAILABLE packet that follows. */
    nations_used = fc_realloc(nations_used,
			      game.playable_nation_count
			      * sizeof(nations_used));
    memset(nations_used, 0,
	   game.playable_nation_count * sizeof(nations_used));

    if (!client_is_observer()) {
      /* Now close the conndlg and popup the races dialog. */
      really_close_connection_dialog();
      popup_races_dialog();
    }
  }
}

/**************************************************************************
  Take arrival of ruleset control packet to indicate that
  current allocated governments should be free'd, and new
  memory allocated for new size. The same for nations.
**************************************************************************/
void handle_ruleset_control(struct packet_ruleset_control *packet)
{
  int i;

  tilespec_free_city_tiles(game.styles_count);
  ruleset_data_free();

  ruleset_cache_init();

  game.aqueduct_size = packet->aqueduct_size;
  game.add_to_size_limit = packet->add_to_size_limit;
  game.notradesize = packet->notradesize;
  game.fulltradesize = packet->fulltradesize;
  
  game.rtech.cathedral_plus = packet->rtech_cathedral_plus;
  game.rtech.cathedral_minus = packet->rtech_cathedral_minus;
  game.rtech.colosseum_plus = packet->rtech_colosseum_plus;
  game.rtech.temple_plus = packet->rtech_temple_plus;

  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    game.rtech.partisan_req[i]  = packet->rtech_partisan_req[i];
    freelog(LOG_DEBUG, "techl %d: %d", i, game.rtech.partisan_req[i]);
  }

  game.government_when_anarchy = packet->government_when_anarchy;
  game.default_government = packet->default_government;

  game.num_unit_types = packet->num_unit_types;
  game.num_impr_types = packet->num_impr_types;
  game.num_tech_types = packet->num_tech_types;

  game.borders = packet->borders;
  game.happyborders = packet->happyborders;
  game.slow_invasions = packet->slow_invasions;

  governments_alloc(packet->government_count);

  nations_alloc(packet->nation_count);
  game.playable_nation_count = packet->playable_nation_count;

  city_styles_alloc(packet->style_count);
  tilespec_alloc_city_tiles(game.styles_count);

  game.terrain_count = packet->terrain_count;

  for(i = 0; i < MAX_NUM_TEAMS; i++) {
    mystrlcpy(team_get_by_id(i)->name, packet->team_name[i],
              MAX_LEN_NAME);
  }

  game.default_building = packet->default_building;
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_unit(struct packet_ruleset_unit *p)
{
  struct unit_type *u;
  int i;

  if(p->id < 0 || p->id >= game.num_unit_types || p->id >= U_LAST) {
    freelog(LOG_ERROR, "Received bad unit_type id %d in handle_ruleset_unit()",
	    p->id);
    return;
  }
  u = get_unit_type(p->id);

  sz_strlcpy(u->name_orig, p->name);
  u->name = u->name_orig;
  sz_strlcpy(u->graphic_str, p->graphic_str);
  sz_strlcpy(u->graphic_alt, p->graphic_alt);
  sz_strlcpy(u->sound_move, p->sound_move);
  sz_strlcpy(u->sound_move_alt, p->sound_move_alt);
  sz_strlcpy(u->sound_fight, p->sound_fight);
  sz_strlcpy(u->sound_fight_alt, p->sound_fight_alt);

  u->move_type          = p->move_type;
  u->build_cost         = p->build_cost;
  u->pop_cost           = p->pop_cost;
  u->attack_strength    = p->attack_strength;
  u->defense_strength   = p->defense_strength;
  u->move_rate          = p->move_rate;
  u->tech_requirement   = p->tech_requirement;
  u->impr_requirement   = p->impr_requirement;
  u->vision_range       = p->vision_range;
  u->transport_capacity = p->transport_capacity;
  u->hp                 = p->hp;
  u->firepower          = p->firepower;
  u->obsoleted_by       = p->obsoleted_by;
  u->fuel               = p->fuel;
  u->flags              = p->flags;
  u->roles              = p->roles;
  u->happy_cost         = p->happy_cost;
  u->shield_cost        = p->shield_cost;
  u->food_cost          = p->food_cost;
  u->gold_cost          = p->gold_cost;
  u->paratroopers_range = p->paratroopers_range;
  u->paratroopers_mr_req = p->paratroopers_mr_req;
  u->paratroopers_mr_sub = p->paratroopers_mr_sub;
  u->bombard_rate       = p->bombard_rate;

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    sz_strlcpy(u->veteran[i].name, p->veteran_name[i]);
    u->veteran[i].power_fact = p->power_fact[i];
    u->veteran[i].move_bonus = p->move_bonus[i];
  }

  u->helptext = mystrdup(p->helptext);

  tilespec_setup_unit_type(p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_tech(struct packet_ruleset_tech *p)
{
  struct advance *a;

  if(p->id < 0 || p->id >= game.num_tech_types || p->id >= A_LAST) {
    freelog(LOG_ERROR, "Received bad advance id %d in handle_ruleset_tech()",
	    p->id);
    return;
  }
  a = &advances[p->id];

  sz_strlcpy(a->name_orig, p->name);
  a->name = a->name_orig;
  sz_strlcpy(a->graphic_str, p->graphic_str);
  sz_strlcpy(a->graphic_alt, p->graphic_alt);
  a->req[0] = p->req[0];
  a->req[1] = p->req[1];
  a->root_req = p->root_req;
  a->flags = p->flags;
  a->preset_cost = p->preset_cost;
  a->num_reqs = p->num_reqs;
  a->helptext = mystrdup(p->helptext);
  
  tilespec_setup_tech_type(p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_building(struct packet_ruleset_building *p)
{
  struct impr_type *b;
  int i;

  if(p->id < 0 || p->id >= game.num_impr_types || p->id >= B_LAST) {
    freelog(LOG_ERROR,
	    "Received bad building id %d in handle_ruleset_building()",
	    p->id);
    return;
  }
  b = &improvement_types[p->id];

  sz_strlcpy(b->name_orig, p->name);
  b->name = b->name_orig;
  sz_strlcpy(b->graphic_str, p->graphic_str);
  sz_strlcpy(b->graphic_alt, p->graphic_alt);
  b->tech_req = p->tech_req;
  b->bldg_req = p->bldg_req;
  b->equiv_range = p->equiv_range;
  b->obsolete_by = p->obsolete_by;
  b->is_wonder = p->is_wonder;
  b->build_cost = p->build_cost;
  b->upkeep = p->upkeep;
  b->sabotage = p->sabotage;
  b->helptext = mystrdup(p->helptext);
  sz_strlcpy(b->soundtag, p->soundtag);
  sz_strlcpy(b->soundtag_alt, p->soundtag_alt);

#define T(elem,count,last) \
  b->elem = fc_malloc(sizeof(*b->elem) * (p->count + 1)); \
  for (i = 0; i < p->count; i++) { \
    b->elem[i] = p->elem[i]; \
  } \
  b->elem[p->count] = last;

  T(terr_gate, terr_gate_count, T_NONE);
  T(spec_gate, spec_gate_count, S_NO_SPECIAL);
  T(equiv_dupl, equiv_dupl_count, B_LAST);
  T(equiv_repl, equiv_repl_count, B_LAST);
#undef T

#ifdef DEBUG
  if(p->id == game.num_impr_types-1) {
    impr_type_iterate(id) {
      int inx;
      b = &improvement_types[id];
      freelog(LOG_DEBUG, "Impr: %s...",
	      b->name);
      freelog(LOG_DEBUG, "  tech_req    %2d/%s",
	      b->tech_req,
	      (b->tech_req == A_LAST) ?
	      "Never" : get_tech_name(game.player_ptr, b->tech_req));
      freelog(LOG_DEBUG, "  bldg_req    %2d/%s",
	      b->bldg_req,
	      (b->bldg_req == B_LAST) ?
	      "None" :
	      improvement_types[b->bldg_req].name);
      freelog(LOG_DEBUG, "  terr_gate...");
      for (inx = 0; b->terr_gate[inx] != T_NONE; inx++) {
	freelog(LOG_DEBUG, "    %2d/%s",
		b->terr_gate[inx], get_terrain_name(b->terr_gate[inx]));
      }
      freelog(LOG_DEBUG, "  spec_gate...");
      for (inx = 0; b->spec_gate[inx] != S_NO_SPECIAL; inx++) {
	freelog(LOG_DEBUG, "    %2d/%s",
		b->spec_gate[inx], get_special_name(b->spec_gate[inx]));
      }
      freelog(LOG_DEBUG, "  equiv_range %2d/%s",
	      b->equiv_range, effect_range_name(b->equiv_range));
      freelog(LOG_DEBUG, "  equiv_dupl...");
      for (inx = 0; b->equiv_dupl[inx] != B_LAST; inx++) {
	freelog(LOG_DEBUG, "    %2d/%s",
		b->equiv_dupl[inx], improvement_types[b->equiv_dupl[inx]].name);
      }
      freelog(LOG_DEBUG, "  equiv_repl...");
      for (inx = 0; b->equiv_repl[inx] != B_LAST; inx++) {
	freelog(LOG_DEBUG, "    %2d/%s",
		b->equiv_repl[inx], improvement_types[b->equiv_repl[inx]].name);
      }
      if (tech_exists(b->obsolete_by)) {
	freelog(LOG_DEBUG, "  obsolete_by %2d/%s",
		b->obsolete_by,
		get_tech_name(game.player_ptr, b->obsolete_by));
      } else {
	freelog(LOG_DEBUG, "  obsolete_by %2d/Never", b->obsolete_by);
      }
      freelog(LOG_DEBUG, "  is_wonder   %2d", b->is_wonder);
      freelog(LOG_DEBUG, "  build_cost %3d", b->build_cost);
      freelog(LOG_DEBUG, "  upkeep      %2d", b->upkeep);
      freelog(LOG_DEBUG, "  sabotage   %3d", b->sabotage);
      freelog(LOG_DEBUG, "  helptext    %s", b->helptext);
    } impr_type_iterate_end;
  }
#endif
  
  tilespec_setup_impr_type(p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_government(struct packet_ruleset_government *p)
{
  struct government *gov;

  if (p->id < 0 || p->id >= game.government_count) {
    freelog(LOG_ERROR,
	    "Received bad government id %d in handle_ruleset_government",
	    p->id);
    return;
  }
  gov = &governments[p->id];

  gov->index             = p->id;

  gov->required_tech     = p->required_tech;
  gov->max_rate          = p->max_rate;
  gov->civil_war         = p->civil_war;
  gov->martial_law_max   = p->martial_law_max;
  gov->martial_law_per   = p->martial_law_per;
  gov->empire_size_mod   = p->empire_size_mod;
  gov->empire_size_inc   = p->empire_size_inc;
  gov->rapture_size      = p->rapture_size;
  
  gov->unit_happy_cost_factor  = p->unit_happy_cost_factor;
  gov->unit_shield_cost_factor = p->unit_shield_cost_factor;
  gov->unit_food_cost_factor   = p->unit_food_cost_factor;
  gov->unit_gold_cost_factor   = p->unit_gold_cost_factor;
  
  gov->free_happy          = p->free_happy;
  gov->free_shield         = p->free_shield;
  gov->free_food           = p->free_food;
  gov->free_gold           = p->free_gold;

  gov->trade_before_penalty   = p->trade_before_penalty;
  gov->shields_before_penalty = p->shields_before_penalty;
  gov->food_before_penalty    = p->food_before_penalty;

  gov->celeb_trade_before_penalty   = p->celeb_trade_before_penalty;
  gov->celeb_shields_before_penalty = p->celeb_shields_before_penalty;
  gov->celeb_food_before_penalty    = p->celeb_food_before_penalty;

  gov->trade_bonus         = p->trade_bonus;
  gov->shield_bonus        = p->shield_bonus;
  gov->food_bonus          = p->food_bonus;

  gov->celeb_trade_bonus   = p->celeb_trade_bonus;
  gov->celeb_shield_bonus  = p->celeb_shield_bonus;
  gov->celeb_food_bonus    = p->celeb_food_bonus;

  gov->corruption_level    = p->corruption_level;
  gov->fixed_corruption_distance = p->fixed_corruption_distance;
  gov->corruption_distance_factor = p->corruption_distance_factor;
  gov->extra_corruption_distance = p->extra_corruption_distance;
  gov->corruption_max_distance_cap = p->corruption_max_distance_cap;
  
  gov->waste_level           = p->waste_level;
  gov->fixed_waste_distance  = p->fixed_waste_distance;
  gov->waste_distance_factor = p->waste_distance_factor;
  gov->extra_waste_distance  = p->extra_waste_distance;
  gov->waste_max_distance_cap = p->waste_max_distance_cap;
  
  gov->flags               = p->flags;
  gov->num_ruler_titles    = p->num_ruler_titles;
    
  sz_strlcpy(gov->name_orig, p->name);
  gov->name = gov->name_orig;
  sz_strlcpy(gov->graphic_str, p->graphic_str);
  sz_strlcpy(gov->graphic_alt, p->graphic_alt);

  gov->ruler_titles = fc_calloc(gov->num_ruler_titles,
				sizeof(struct ruler_title));

  gov->helptext = mystrdup(p->helptext);
  
  tilespec_setup_government(p->id);
}

void handle_ruleset_government_ruler_title
  (struct packet_ruleset_government_ruler_title *p)
{
  struct government *gov;

  if(p->gov < 0 || p->gov >= game.government_count) {
    freelog(LOG_ERROR, "Received bad government num %d for title", p->gov);
    return;
  }
  gov = &governments[p->gov];
  if(p->id < 0 || p->id >= gov->num_ruler_titles) {
    freelog(LOG_ERROR, "Received bad ruler title num %d for %s title",
	    p->id, gov->name);
    return;
  }
  gov->ruler_titles[p->id].nation = p->nation;
  sz_strlcpy(gov->ruler_titles[p->id].male_title_orig, p->male_title);
  gov->ruler_titles[p->id].male_title
    = gov->ruler_titles[p->id].male_title_orig;
  sz_strlcpy(gov->ruler_titles[p->id].female_title_orig, p->female_title);
  gov->ruler_titles[p->id].female_title
    = gov->ruler_titles[p->id].female_title_orig;
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_terrain(struct packet_ruleset_terrain *p)
{
  struct tile_type *t;

  if (p->id < T_FIRST || p->id >= T_COUNT) {
    freelog(LOG_ERROR,
	    "Received bad terrain id %d in handle_ruleset_terrain",
	    p->id);
    return;
  }
  t = get_tile_type(p->id);

  sz_strlcpy(t->terrain_name_orig, p->terrain_name);
  t->terrain_name = t->terrain_name_orig;
  sz_strlcpy(t->graphic_str, p->graphic_str);
  sz_strlcpy(t->graphic_alt, p->graphic_alt);
  t->movement_cost = p->movement_cost;
  t->defense_bonus = p->defense_bonus;
  t->food = p->food;
  t->shield = p->shield;
  t->trade = p->trade;
  sz_strlcpy(t->special_1_name_orig, p->special_1_name);
  t->special_1_name = t->special_1_name_orig;
  t->food_special_1 = p->food_special_1;
  t->shield_special_1 = p->shield_special_1;
  t->trade_special_1 = p->trade_special_1;
  sz_strlcpy(t->special_2_name_orig, p->special_2_name);
  t->special_2_name = t->special_2_name_orig;
  t->food_special_2 = p->food_special_2;
  t->shield_special_2 = p->shield_special_2;
  t->trade_special_2 = p->trade_special_2;

  sz_strlcpy(t->special[0].graphic_str, p->graphic_str_special_1);
  sz_strlcpy(t->special[0].graphic_alt, p->graphic_alt_special_1);

  sz_strlcpy(t->special[1].graphic_str, p->graphic_str_special_2);
  sz_strlcpy(t->special[1].graphic_alt, p->graphic_alt_special_2);

  t->road_time = p->road_time;
  t->road_trade_incr = p->road_trade_incr;
  t->irrigation_result = p->irrigation_result;
  t->irrigation_food_incr = p->irrigation_food_incr;
  t->irrigation_time = p->irrigation_time;
  t->mining_result = p->mining_result;
  t->mining_shield_incr = p->mining_shield_incr;
  t->mining_time = p->mining_time;
  t->transform_result = p->transform_result;
  t->transform_time = p->transform_time;
  t->rail_time = p->rail_time;
  t->airbase_time = p->airbase_time;
  t->fortress_time = p->fortress_time;
  t->clean_pollution_time = p->clean_pollution_time;
  t->clean_fallout_time = p->clean_fallout_time;
  
  t->flags = p->flags;

  t->helptext = mystrdup(p->helptext);
  
  tilespec_setup_tile_type(p->id);
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
...
**************************************************************************/
void handle_ruleset_nation(struct packet_ruleset_nation *p)
{
  int i;
  struct nation_type *pl;

  if (p->id < 0 || p->id >= game.nation_count) {
    freelog(LOG_ERROR, "Received bad nation id %d in handle_ruleset_nation()",
	    p->id);
    return;
  }
  pl = get_nation_by_idx(p->id);

  sz_strlcpy(pl->name_orig, p->name);
  pl->name = pl->name_orig;
  sz_strlcpy(pl->name_plural_orig, p->name_plural);
  pl->name_plural = pl->name_plural_orig;
  sz_strlcpy(pl->flag_graphic_str, p->graphic_str);
  sz_strlcpy(pl->flag_graphic_alt, p->graphic_alt);
  pl->leader_count = p->leader_count;
  pl->leaders = fc_malloc(sizeof(*pl->leaders) * pl->leader_count);
  for (i = 0; i < pl->leader_count; i++) {
    pl->leaders[i].name = mystrdup(p->leader_name[i]);
    pl->leaders[i].is_male = p->leader_sex[i];
  }
  pl->city_style = p->city_style;

  if (p->class[0] != '\0') {
    pl->class = mystrdup(p->class);
  } else {
    pl->class = mystrdup(N_("Other"));
  }

  if (p->legend[0] != '\0') {
    pl->legend = mystrdup(_(p->legend));
  } else {
    pl->legend = mystrdup("");
  }

  tilespec_setup_nation_flag(p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_city(struct packet_ruleset_city *packet)
{
  int id;
  struct citystyle *cs;

  id = packet->style_id;
  if (id < 0 || id >= game.styles_count) {
    freelog(LOG_ERROR, "Received bad citystyle id %d in handle_ruleset_city()",
	    id);
    return;
  }
  cs = &city_styles[id];
  
  cs->techreq = packet->techreq;
  cs->replaced_by = packet->replaced_by;

  sz_strlcpy(cs->name_orig, packet->name);
  cs->name = cs->name_orig;
  sz_strlcpy(cs->graphic, packet->graphic);
  sz_strlcpy(cs->graphic_alt, packet->graphic_alt);
  sz_strlcpy(cs->citizens_graphic, packet->citizens_graphic);
  sz_strlcpy(cs->citizens_graphic_alt, packet->citizens_graphic_alt);

  tilespec_setup_city_tiles(id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_game(struct packet_ruleset_game *packet)
{
  int i;

  specialist_type_iterate(sp) {
    sz_strlcpy(game.rgame.specialists[sp].name, packet->specialist_name[sp]);
    game.rgame.specialists[sp].min_size = packet->specialist_min_size[sp];
    game.rgame.specialists[sp].bonus = packet->specialist_bonus[sp];
  } specialist_type_iterate_end;
  tilespec_setup_specialist_types();

  game.rgame.changable_tax = packet->changable_tax;
  game.rgame.forced_science = packet->forced_science;
  game.rgame.forced_luxury = packet->forced_luxury;
  game.rgame.forced_gold = packet->forced_gold;
  game.rgame.min_city_center_food = packet->min_city_center_food;
  game.rgame.min_city_center_shield = packet->min_city_center_shield;
  game.rgame.min_city_center_trade = packet->min_city_center_trade;
  game.rgame.min_dist_bw_cities = packet->min_dist_bw_cities;
  game.rgame.init_vis_radius_sq = packet->init_vis_radius_sq;
  game.rgame.hut_overflight = packet->hut_overflight;
  game.rgame.pillage_select = packet->pillage_select;
  game.rgame.nuke_contamination = packet->nuke_contamination;
  for (i = 0; i < MAX_GRANARY_INIS; i++) {
    game.rgame.granary_food_ini[i] = packet->granary_food_ini[i];
  }
  game.rgame.granary_num_inis = packet->granary_num_inis;
  game.rgame.granary_food_inc = packet->granary_food_inc;
  game.rgame.tech_cost_style = packet->tech_cost_style;
  game.rgame.tech_leakage = packet->tech_leakage;
  game.rgame.tech_cost_double_year = packet->tech_cost_double_year;
  game.rgame.killstack = packet->killstack;

  for (i = 0; i < MAX_VET_LEVELS; i++) {
    game.trireme_loss_chance[i] = packet->trireme_loss_chance[i];
    game.work_veteran_chance[i] = packet->work_veteran_chance[i];
    game.veteran_chance[i] = packet->work_veteran_chance[i];
  }
}

/**************************************************************************
  ...
**************************************************************************/
void handle_unit_bribe_info(int unit_id, int cost)
{
  struct unit *punit = find_unit_by_id(unit_id);

  if (punit) {
    punit->bribe_cost = cost;
    if (!game.player_ptr->ai.control || ai_popup_windows) {
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
    if (!game.player_ptr->ai.control || ai_popup_windows) {
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

  if (pdiplomat) {
    process_diplomat_arrival(pdiplomat, target_id);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_sabotage_list(int diplomat_id, int city_id,
			       char *improvements)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, diplomat_id);
  struct city *pcity = find_city_by_id(city_id);

  if (punit && pcity) {
    impr_type_iterate(i) {
      pcity->improvements[i] = (improvements[i]=='1') ? I_ACTIVE : I_NONE;
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
  We have received PACKET_FREEZE_HINT. It is packet internal to network code
**************************************************************************/
void handle_freeze_hint(void)
{
  freelog(LOG_DEBUG, "handle_freeze_hint");
}

/**************************************************************************
  We have received PACKET_FREEZE_CLIENT.
**************************************************************************/
void handle_freeze_client(void)
{
  freelog(LOG_DEBUG, "handle_freeze_client");

  reports_freeze();

  agents_freeze_hint();
}

/**************************************************************************
  We have received PACKET_THAW_HINT. It is packet internal to network code
**************************************************************************/
void handle_thaw_hint(void)
{
  freelog(LOG_DEBUG, "handle_thaw_hint");
}

/**************************************************************************
  We have received PACKET_THAW_CLIENT
**************************************************************************/
void handle_thaw_client(void)
{
  freelog(LOG_DEBUG, "handle_thaw_client");

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
  Add group data to ruleset cache.  
**************************************************************************/
void handle_ruleset_cache_group(struct packet_ruleset_cache_group *packet)
{
  struct effect_group *pgroup;
  int i;

  pgroup = effect_group_new(packet->name);

  for (i = 0; i < packet->num_elements; i++) {
    effect_group_add_element(pgroup, packet->source_buildings[i],
			     packet->ranges[i], packet->survives[i]);
  }
}

/**************************************************************************
  Add effect data to ruleset cache.  
**************************************************************************/
void handle_ruleset_cache_effect(struct packet_ruleset_cache_effect *packet)
{
  ruleset_cache_add(packet->id, packet->effect_type, packet->range,
		    packet->survives, packet->eff_value,
		    packet->req_type, packet->req_value, packet->group_id);
}

