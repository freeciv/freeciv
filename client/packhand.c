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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

#include "capstr.h"
#include "capability.h"
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
#include "spaceship.h"
#include "support.h"
#include "unit.h"
#include "worklist.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"		/* aconnection */
#include "control.h"
#include "dialogs_g.h"
#include "goto.h"               /* client_goto_init() */
#include "graphics_g.h"
#include "gui_main_g.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "mapctrl_g.h"		/* popup_newcity_dialog() */
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"
#include "spaceshipdlg_g.h"
#include "tilespec.h"
#include "wldlg_g.h"
#include "attribute.h"
#include "agents.h"
#include "audio.h"

#include "packhand.h"

static void handle_city_packet_common(struct city *pcity, bool is_new,
                                      bool popup, bool investigate);
static int *reports_thaw_requests = NULL;
static int reports_thaw_requests_size = 0;

/**************************************************************************
...
**************************************************************************/
static void unpackage_unit(struct unit *punit, struct packet_unit_info *packet)
{
  punit->id = packet->id;
  punit->owner = packet->owner;
  punit->x = packet->x;
  punit->y = packet->y;
  punit->homecity = packet->homecity;
  punit->veteran = packet->veteran;
  punit->type = packet->type;
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
  punit->goto_dest_x = packet->goto_dest_x;
  punit->goto_dest_y = packet->goto_dest_y;
  punit->activity_target = packet->activity_target;
  punit->paradropped = packet->paradropped;
  punit->connecting = packet->connecting;
  /* not in packet, only in unit struct */
  punit->focus_status = FOCUS_AVAIL;
  punit->bribe_cost = 0;	/* done by handle_incite_cost() */
  punit->foul = FALSE;		/* never used in client/ */
  punit->ord_map = 0;		/* never used in client/ */
  punit->ord_city = 0;		/* never used in client/ */
  punit->moved = FALSE;		/* never used in client/ */
  punit->transported_by = 0;	/* never used in client/ */
}

/**************************************************************************
...
**************************************************************************/
void handle_join_game_reply(struct packet_join_game_reply *packet)
{
  char msg[MAX_LEN_MSG];
  char *s_capability = aconnection.capability;

  sz_strlcpy(aconnection.capability, packet->capability);

  if (packet->you_can_join) {
    freelog(LOG_VERBOSE, "join game accept:%s", packet->message);
    aconnection.established = TRUE;
    game.conn_id = packet->conn_id;
    agents_game_joined();
  } else {
    my_snprintf(msg, sizeof(msg),
		_("You were rejected from the game: %s"), packet->message);
    append_output_window(msg);
    game.conn_id = 0;
    if (auto_connect) {
      freelog(LOG_NORMAL, "%s", msg);
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

/**************************************************************************
...
**************************************************************************/
void handle_remove_city(struct packet_generic_integer *packet)
{
  struct city *pcity = find_city_by_id(packet->value);
  int x, y;

  if (!pcity)
    return;

  agents_city_remove(pcity);

  x = pcity->x; y = pcity->y;
  client_remove_city(pcity);
  reset_move_costs(x, y);

  /* update menus if the focus unit is on the tile. */
  if (get_unit_in_focus()) {
    update_menus();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_remove_unit(struct packet_generic_integer *packet)
{
  agents_unit_remove(find_unit_by_id(packet->value));
  client_remove_unit(packet->value);
}

/**************************************************************************
...
**************************************************************************/
void handle_nuke_tile(struct packet_nuke_tile *packet)
{
  put_nuke_mushroom_pixmaps(packet->x, packet->y);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_combat(struct packet_unit_combat *packet)
{
  bool show_combat = FALSE;
  struct unit *punit0 = find_unit_by_id(packet->attacker_unit_id);
  struct unit *punit1 = find_unit_by_id(packet->defender_unit_id);

  if (punit0 && punit1) {
    if (tile_visible_mapcanvas(punit0->x, punit0->y) &&
	tile_visible_mapcanvas(punit1->x, punit1->y)) {
      show_combat = TRUE;
    } else if (auto_center_on_combat) {
      if (punit0->owner == game.player_idx)
	center_tile_mapcanvas(punit0->x, punit0->y);
      else
	center_tile_mapcanvas(punit1->x, punit1->y);
      show_combat = TRUE;
    }

    if (show_combat) {
      int hp0 = packet->attacker_hp, hp1 = packet->defender_hp;

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
	refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
	refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
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
    update_all_effects();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_game_state(struct packet_generic_integer *packet)
{
  if(get_client_state()==CLIENT_SELECT_RACE_STATE && 
     packet->value==CLIENT_GAME_RUNNING_STATE &&
     game.player_ptr->nation == MAX_NUM_NATIONS) {
    popdown_races_dialog();
  }
  
  set_client_state(packet->value);

  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
    refresh_overview_canvas();
    refresh_overview_viewrect();
    if (!client_is_observer()) {
      enable_turn_done_button();
    }
    player_set_unit_focus_status(game.player_ptr);

    update_info_label();	/* get initial population right */
    update_unit_focus();
    update_unit_info_label(get_unit_in_focus());

    /* Find something sensible to display instead of the intro gfx. */
    center_on_something();
    
    free_intro_radar_sprites();
    agents_game_start();
  } else if(get_client_state() == CLIENT_GAME_OVER_STATE) {
    reports_thaw();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_info(struct packet_city_info *packet)
{
  int i, x, y;
  bool city_is_new, city_has_changed_owner = FALSE, need_effect_update = FALSE;
  struct city *pcity;
  bool popup;

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
    bool update_descriptions = FALSE;

    city_is_new = FALSE;

    /* Check if city desciptions should be updated */
    if (draw_city_names && strcmp(pcity->name, packet->name) != 0) {
      update_descriptions = TRUE;
    } else if (draw_city_productions &&
	       (pcity->is_building_unit != packet->is_building_unit ||
		pcity->currently_building != packet->currently_building ||
		pcity->shield_surplus != packet->shield_surplus ||
		pcity->shield_stock != packet->shield_stock)) {
      update_descriptions = TRUE;
    }

    /* update the descriptions if necessary */
    if (update_descriptions && tile_visible_mapcanvas(packet->x, packet->y)) {
      queue_mapview_update();
    }

    assert(pcity->id == packet->id);
  }
  
  pcity->owner=packet->owner;
  pcity->x=packet->x;
  pcity->y=packet->y;
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;
  for (i=0;i<5;i++) {
    pcity->ppl_happy[i]=packet->ppl_happy[i];
    pcity->ppl_content[i]=packet->ppl_content[i];
    pcity->ppl_unhappy[i]=packet->ppl_unhappy[i];
    pcity->ppl_angry[i] = packet->ppl_angry[i];
  }
  pcity->ppl_elvis=packet->ppl_elvis;
  pcity->ppl_scientist=packet->ppl_scientist;
  pcity->ppl_taxman=packet->ppl_taxman;

  pcity->city_options=packet->city_options;

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
  
  pcity->luxury_total=packet->luxury_total;
  pcity->tax_total=packet->tax_total;
  pcity->science_total=packet->science_total;
  
  pcity->food_stock=packet->food_stock;
  pcity->shield_stock=packet->shield_stock;
  pcity->pollution=packet->pollution;

  pcity->is_building_unit=packet->is_building_unit;
  pcity->currently_building=packet->currently_building;
  if (city_is_new) {
    init_worklist(&pcity->worklist);

    /* Initialise list of improvements with city/building wide equiv_range. */
    improvement_status_init(pcity->improvements,
			    ARRAY_SIZE(pcity->improvements));

    /* Initialise city's vector of improvement effects. */
    ceff_vector_init(&pcity->effects);
  }
  copy_worklist(&pcity->worklist, &packet->worklist);
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;

  pcity->turn_last_built=packet->turn_last_built;
  pcity->turn_changed_target=packet->turn_changed_target;
  pcity->turn_founded = packet->turn_founded;
  pcity->changed_from_id=packet->changed_from_id;
  pcity->changed_from_is_unit=packet->changed_from_is_unit;
  pcity->before_change_shields=packet->before_change_shields;
  pcity->disbanded_shields=packet->disbanded_shields;
  pcity->caravan_shields=packet->caravan_shields;

  i=0;
  for(y=0; y<CITY_MAP_SIZE; y++) {
    for(x=0; x<CITY_MAP_SIZE; x++) {
      if (city_is_new) {
	/* Need to pre-initialize before set_worker_city()  -- dwp */
	pcity->city_map[x][y] =
	    is_valid_city_coords(x, y) ? C_TILE_EMPTY : C_TILE_UNAVAILABLE;
      }
      if (is_valid_city_coords(x, y)) {
	set_worker_city(pcity, x, y, packet->city_map[i] - '0');
      }
      i++;
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

  popup = (city_is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE && 
           pcity->owner==game.player_idx) || packet->diplomat_investigate;

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  handle_city_packet_common(pcity, city_is_new, popup, packet->diplomat_investigate);

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
    map_set_city(pcity->x, pcity->y, pcity);
    if(pcity->owner==game.player_idx)
      city_report_dialog_update();

    for(i=0; i<game.nplayers; i++) {
      unit_list_iterate(game.players[i].units, punit) 
	if(punit->homecity==pcity->id)
	  unit_list_insert(&pcity->units_supported, punit);
      unit_list_iterate_end;
    }
  }

  if (draw_map_grid && get_client_state() == CLIENT_GAME_RUNNING_STATE) {
    queue_mapview_update();
  } else {
    refresh_tile_mapcanvas(pcity->x, pcity->y, TRUE);
  }

  if (city_workers_display==pcity)  {
    put_city_workers(pcity, -1);
    city_workers_display=NULL;
  }

  if (popup &&
      (!game.player_ptr->ai.control || ai_popup_windows)) {
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
    if (punit && same_pos(punit->x, punit->y, pcity->x, pcity->y)) {
      update_menus();
    }
  }

  if(is_new) {
    freelog(LOG_DEBUG, "New %s city %s id %d (%d %d)",
	 get_nation_name(city_owner(pcity)->nation),
	 pcity->name, pcity->id, pcity->x, pcity->y);
  }

  reset_move_costs(pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
void handle_short_city(struct packet_short_city *packet)
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
  pcity->x=packet->x;
  pcity->y=packet->y;
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;
  pcity->tile_trade = packet->tile_trade;

  if (packet->happy) {
    pcity->ppl_happy[4]   = pcity->size;
    pcity->ppl_unhappy[4] = 0;
    pcity->ppl_angry[4]   = 0;
  } else {
    pcity->ppl_happy[4]   = 0;
    pcity->ppl_unhappy[4] = pcity->size;
    pcity->ppl_angry[4]   = 0;
  }

  if (city_is_new) {
    /* Initialise list of improvements with city/building wide equiv_range. */
    improvement_status_init(pcity->improvements,
			    ARRAY_SIZE(pcity->improvements));

    /* Initialise city's vector of improvement effects. */
    ceff_vector_init(&pcity->effects);
  }

  update_improvement_from_packet(pcity, B_PALACE, packet->capital,
                                 &need_effect_update);
  update_improvement_from_packet(pcity, B_CITY, packet->walls,
                                 &need_effect_update);

  if (city_is_new) {
    init_worklist(&pcity->worklist);
  }

  /* This sets dumb values for everything else. This is not really required,
     but just want to be at the safe side. */
  {
    int i;
    int x, y;

    pcity->ppl_elvis          = pcity->size;
    pcity->ppl_scientist      = 0;
    pcity->ppl_taxman         = 0;
    for (i = 0; i < NUM_TRADEROUTES; i++) {
      pcity->trade[i]=0;
      pcity->trade_value[i]     = 0;
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

    for(y=0; y<CITY_MAP_SIZE; y++)
      for(x=0; x<CITY_MAP_SIZE; x++)
	pcity->city_map[x][y] = C_TILE_EMPTY;
  } /* Dumb values */

  if (city_is_new && !city_has_changed_owner) {
    agents_city_new(pcity);
  } else {
    agents_city_changed(pcity);
  }

  handle_city_packet_common(pcity, city_is_new, FALSE, FALSE);

  /* update the descriptions if necessary */
  if (update_descriptions && tile_visible_mapcanvas(pcity->x,pcity->y)) {
    update_city_descriptions();
  }

  try_update_effects(need_effect_update);
}

/**************************************************************************
...
**************************************************************************/
void handle_new_year(struct packet_new_year *ppacket)
{
  update_turn_done_button(1);
  if (!client_is_observer()) {
    enable_turn_done_button();
  }

  game.year = ppacket->year;
  /*
   * The turn was increased in handle_before_new_year()
   */
  assert(game.turn == ppacket->turn);
  update_info_label();

  player_set_unit_focus_status(game.player_ptr);
  update_unit_focus();
  auto_center_on_focus_unit();

  update_unit_info_label(get_unit_in_focus());

  seconds_to_turndone=game.timeout;

  turn_gold_difference=game.player_ptr->economic.gold-last_turn_gold_amount;
  last_turn_gold_amount=game.player_ptr->economic.gold;

  update_city_descriptions();

  if (sound_bell_at_new_turn &&
      (!game.player_ptr->ai.control || ai_manual_turn_done)) {
    create_event(-1, -1, E_TURN_BELL, _("Start of turn %d"), game.turn);
  }

  agents_new_turn();
}

/**************************************************************************
...
**************************************************************************/
void handle_before_new_year(void)
{
  clear_notify_window();
  reports_freeze();
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
  reports_thaw();

  agents_start_turn();

  if(game.player_ptr->ai.control && !ai_manual_turn_done) {
    user_ended_turn();
  }
}

/**************************************************************************
...
**************************************************************************/
static void play_sound_for_event(enum event_type type)
{
  const char *sound_tag = get_sound_tag_for_event(type);

  if (sound_tag) {
    audio_play_sound(sound_tag, NULL);
  }
}  
  
/**************************************************************************
...
**************************************************************************/
void handle_chat_msg(struct packet_generic_message *packet)
{
  int where = MW_OUTPUT;	/* where to display the message */
  
  if (packet->event >= E_LAST)  {
    /* Server may have added a new event; leave as MW_OUTPUT */
    freelog(LOG_NORMAL, "Unknown event type %d!", packet->event);
  } else if (packet->event >= 0)  {
    where = messages_where[packet->event];
  }
  if (BOOL_VAL(where & MW_OUTPUT))
    append_output_window(packet->message);
  if (BOOL_VAL(where & MW_MESSAGES))
    add_notify_window(packet);
  if (BOOL_VAL(where & MW_POPUP) &&
      (!game.player_ptr->ai.control || ai_popup_windows))
    popup_notify_goto_dialog(_("Popup Request"), packet->message, 
			     packet->x, packet->y);
  play_sound_for_event(packet->event);
}
 
/**************************************************************************
...
**************************************************************************/
void handle_page_msg(struct packet_generic_message *packet)
{
  char *caption;
  char *headline;
  char *lines;

  caption = packet->message;
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
      packet->event != E_BROADCAST_REPORT) {
    popup_notify_dialog(caption, headline, lines);
    play_sound_for_event(packet->event);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_move_unit()
{
  /* this packet should never get sent to a client */
  assert(0);
  exit(EXIT_FAILURE);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct packet_unit_info *packet)
{
  struct city *pcity;
  struct unit *punit;
  bool repaint_unit;
  bool repaint_city;		/* regards unit's homecity */
  /* Special case for a diplomat/spy investigating a city:
     The investigator needs to know the supported and present
     units of a city, whether or not they are fogged. So, we
     send a list of them all before sending the city info. */
  if ((packet->packet_use == UNIT_INFO_CITY_SUPPORTED) ||
      (packet->packet_use == UNIT_INFO_CITY_PRESENT)) {
    static int last_serial_num = 0;
    /* fetch city -- abort if not found */
    pcity = find_city_by_id(packet->info_city_id);
    if (!pcity) {
      return;
    }
    /* new serial number -- clear everything */
    if (last_serial_num != packet->serial_num) {
      last_serial_num = packet->serial_num;
      unit_list_iterate(pcity->info_units_supported, psunit) {
	free(psunit);
      } unit_list_iterate_end;
      unit_list_unlink_all(&(pcity->info_units_supported));
      unit_list_iterate(pcity->info_units_present, ppunit) {
	free(ppunit);
      } unit_list_iterate_end;
      unit_list_unlink_all(&(pcity->info_units_present));
    }
    /* okay, append a unit struct to the proper list */
    if (packet->packet_use == UNIT_INFO_CITY_SUPPORTED) {
      punit = fc_malloc(sizeof(struct unit));
      unpackage_unit(punit, packet);
      unit_list_insert(&(pcity->info_units_supported), punit);
    } else if (packet->packet_use == UNIT_INFO_CITY_PRESENT) {
      punit = fc_malloc(sizeof(struct unit));
      unpackage_unit(punit, packet);
      unit_list_insert(&(pcity->info_units_present), punit);
    }
    /* done with special case */
    return;
  }

  repaint_unit = FALSE;
  repaint_city = FALSE;
  punit = player_find_unit_by_id(get_player(packet->owner), packet->id);

  if(punit) {
    int dest_x,dest_y;
    punit->activity_count = packet->activity_count;
    if((punit->activity!=packet->activity)         /* change activity */
       || (punit->activity_target!=packet->activity_target)) { /*   or act's target */
      repaint_unit = TRUE;
      if(wakeup_focus && (punit->owner==game.player_idx)
                      && (punit->activity==ACTIVITY_SENTRY)) {
        set_unit_focus(punit);
        /* RP: focus on (each) activated unit (e.g. when unloading a ship) */
      }

      punit->activity=packet->activity;
      punit->activity_target=packet->activity_target;

      if(punit->owner==game.player_idx) 
        refresh_unit_city_dialogs(punit);
      /*      refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
       *      update_unit_pix_label(punit);
       *      update_unit_focus();
       */

      /* These two lines force the menus to be updated as appropriate when
	 the unit activity changes. */
      if(punit == get_unit_in_focus())
         update_menus();
    }
    
    if(punit->homecity!=packet->homecity) { /* change homecity */
      struct city *pcity;
      if((pcity=find_city_by_id(punit->homecity))) {
	unit_list_unlink(&pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
      
      punit->homecity=packet->homecity;
      if((pcity=find_city_by_id(punit->homecity))) {
	unit_list_insert(&pcity->units_supported, punit);
	repaint_city = TRUE;
      }
    }

    if(punit->hp!=packet->hp) {                      /* hp changed */
      punit->hp=packet->hp;
      repaint_unit = TRUE;
    }
    if (punit->type!=packet->type) {
      struct city *pcity = map_get_city(punit->x, punit->y);
      
      punit->type=packet->type;
      repaint_unit = TRUE;
      repaint_city = TRUE;
      if (pcity && (pcity->id != punit->homecity)) {
	refresh_city_dialog(pcity);
      }
    }
    if (punit->ai.control!=packet->ai) {
      punit->ai.control=packet->ai;
      repaint_unit = TRUE;
    }

    if(punit->x!=packet->x || punit->y!=packet->y) { /* change position */
      struct city *pcity;
      pcity=map_get_city(punit->x, punit->y);
      
      if(tile_get_known(packet->x, packet->y) == TILE_KNOWN) {
	do_move_unit(punit, packet);
	update_unit_focus();
      }
      else {
	do_move_unit(punit, packet); /* nice to see where a unit is going */
	client_remove_unit(punit->id);
	refresh_tile_mapcanvas(packet->x, packet->y, TRUE);
        return;
      }
      if(pcity)  {
        if(pcity->id==punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
      }
      
      if((pcity=map_get_city(punit->x, punit->y)))  {
        if(pcity->id == punit->homecity)
	  repaint_city = TRUE;
	else
	  refresh_city_dialog(pcity);
	
        if((unit_flag(punit, F_TRADE_ROUTE) || unit_flag(punit, F_HELP_WONDER))
	   && (!game.player_ptr->ai.control || ai_popup_windows)
	   && punit->owner==game.player_idx
	   && (punit->activity!=ACTIVITY_GOTO ||
	       same_pos(punit->goto_dest_x, punit->goto_dest_y,
			pcity->x, pcity->y))
	   && (unit_can_help_build_wonder_here(punit)
	       || unit_can_est_traderoute_here(punit))) {
	  process_caravan_arrival(punit);
	}
      }
      
      repaint_unit = FALSE;
    }
    if (punit->unhappiness!=packet->unhappiness) {
      punit->unhappiness=packet->unhappiness;
      repaint_city = TRUE;
    }
    if (punit->upkeep!=packet->upkeep) {
      punit->upkeep=packet->upkeep;
      repaint_city = TRUE;
    }
    if (punit->upkeep_food!=packet->upkeep_food) {
      punit->upkeep_food=packet->upkeep_food;
      repaint_city = TRUE;
    }
    if (punit->upkeep_gold!=packet->upkeep_gold) {
      punit->upkeep_gold=packet->upkeep_gold;
      repaint_city = TRUE;
    }
    if (repaint_city) {
      if((pcity=find_city_by_id(punit->homecity))) {
	refresh_city_dialog(pcity);
      }
    }

    punit->veteran=packet->veteran;
    punit->moves_left=packet->movesleft;
    punit->bribe_cost=0;
    punit->fuel=packet->fuel;
    punit->goto_dest_x=packet->goto_dest_x;
    punit->goto_dest_y=packet->goto_dest_y;
    punit->paradropped=packet->paradropped;
    punit->connecting=packet->connecting;
  
    dest_x = packet->x;
    dest_y = packet->y;
    /*fog of war*/
    if (!(tile_get_known(punit->x,punit->y) == TILE_KNOWN)) {
      client_remove_unit(packet->id);
      refresh_tile_mapcanvas(dest_x, dest_y, TRUE);
    }
    agents_unit_changed(punit);
  }

  else {      /* create new unit */
    punit=fc_malloc(sizeof(struct unit));
    unpackage_unit(punit, packet);
    idex_register_unit(punit);

    unit_list_insert(&get_player(packet->owner)->units, punit);
    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);

    freelog(LOG_DEBUG, "New %s %s id %d (%d %d) hc %d %s", 
	   get_nation_name(unit_owner(punit)->nation),
	   unit_name(punit->type), punit->x, punit->y, punit->id,
	   punit->homecity, (pcity ? pcity->name : _("(unknown)")));

    repaint_unit = !packet->carried;
    agents_unit_new(punit);
  }

  if(punit && punit==get_unit_in_focus())
    update_unit_info_label(punit);
  else if(get_unit_in_focus() && get_unit_in_focus()->x==punit->x &&
	  get_unit_in_focus()->y==punit->y) {
    update_unit_info_label(get_unit_in_focus());
  }

  if(repaint_unit)
    refresh_tile_mapcanvas(punit->x, punit->y, TRUE);

  if(packet->select_it && (punit->owner==game.player_idx)) {
    set_unit_focus_and_select(punit);
  } else {
    update_unit_focus(); 
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_map_info(struct packet_map_info *pinfo)
{
  map.xsize=pinfo->xsize;
  map.ysize=pinfo->ysize;
  map.is_earth=pinfo->is_earth;

  map_allocate();
  climap_init_continents();
  init_client_goto();
  
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
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    improvement_status_init(game.improvements,
			    ARRAY_SIZE(game.improvements));

    /* Free vector of effects with a worldwide range. */
    geff_vector_free(&game.effects);
    /* Free vector of destroyed effects. */
    ceff_vector_free(&game.destroyed_effects);

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

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(get_client_state()==CLIENT_SELECT_RACE_STATE)
      popdown_races_dialog();
  }
  game.techpenalty=pinfo->techpenalty;
  game.foodbox=pinfo->foodbox;
  game.civstyle=pinfo->civstyle;
  game.unhappysize = pinfo->unhappysize;
  game.cityfactor = pinfo->cityfactor;

  boot_help = (get_client_state() == CLIENT_GAME_RUNNING_STATE
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
static void read_player_info_techs(struct player *pplayer,
				   unsigned char *inventions)
{
  int i;
  bool need_effect_update = FALSE;

  for (i = 0; i < game.num_tech_types; i++) {
    enum tech_state oldstate = pplayer->research.inventions[i].state;
    enum tech_state newstate = inventions[i] - '0';

    pplayer->research.inventions[i].state = newstate;
    if (newstate != oldstate
	&& (newstate == TECH_KNOWN || oldstate == TECH_KNOWN)) {
      need_effect_update = TRUE;
    }
  }
  if (need_effect_update) {
    update_all_effects();
  }
  update_research(pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_player_info(struct packet_player_info *pinfo)
{
  int i;
  bool poptechup;
  char msg[MAX_LEN_MSG];
  struct player *pplayer = &game.players[pinfo->playerno];

  sz_strlcpy(pplayer->name, pinfo->name);

  if (!pplayer->island_improv) {   /* initialise new player */
    client_init_player(pplayer);
  }

  pplayer->nation=pinfo->nation;
  pplayer->is_male=pinfo->is_male;

  pplayer->economic.gold=pinfo->gold;
  pplayer->economic.tax=pinfo->tax;
  pplayer->economic.science=pinfo->science;
  pplayer->economic.luxury=pinfo->luxury;
  pplayer->government=pinfo->government;
  pplayer->embassy=pinfo->embassy;
  pplayer->gives_shared_vision = pinfo->gives_shared_vision;
  pplayer->city_style=pinfo->city_style;

  for (i = 0; i < MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS; i++) {
    pplayer->diplstates[i].type =
      pinfo->diplstates[i].type;
    pplayer->diplstates[i].turns_left =
      pinfo->diplstates[i].turns_left;
    pplayer->diplstates[i].has_reason_to_cancel =
      pinfo->diplstates[i].has_reason_to_cancel;
  }
  pplayer->reputation = pinfo->reputation;

  read_player_info_techs(pplayer, pinfo->inventions);

  poptechup = (pplayer->research.researching!=pinfo->researching);
  pplayer->research.bulbs_researched = pinfo->bulbs_researched;
  pplayer->research.techs_researched = pinfo->techs_researched;
  pplayer->research.researching=pinfo->researching;
  pplayer->future_tech=pinfo->future_tech;
  pplayer->ai.tech_goal=pinfo->tech_goal;
  
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE && pplayer==game.player_ptr) {
    if(poptechup) {
      if(!game.player_ptr->ai.control || ai_popup_windows)
	popup_science_dialog(TRUE);
      science_dialog_update();

      /* If we just learned bridge building and focus is on a settler
	 on a river the road menu item will remain disabled unless we
	 do this. (applys in other cases as well.) */
      if (get_unit_in_focus())
	update_menus();
    } 
  }

  pplayer->turn_done=pinfo->turn_done;
  pplayer->nturns_idle=pinfo->nturns_idle;
  pplayer->is_alive=pinfo->is_alive;
  
  pplayer->is_connected=pinfo->is_connected;

  pplayer->ai.barbarian_type = pinfo->barbarian_type;
  pplayer->revolution=pinfo->revolution;
  if(pplayer->ai.control!=pinfo->ai)  {
    pplayer->ai.control=pinfo->ai;
    if(pplayer==game.player_ptr)  {
      my_snprintf(msg, sizeof(msg), _("AI Mode is now %s."),
		  game.player_ptr->ai.control?_("ON"):_("OFF"));
      append_output_window(msg);
    }
  }
  
  if (pplayer == game.player_ptr &&
      (pplayer->revolution < 1 || pplayer->revolution > 5) &&
      pplayer->government == game.government_when_anarchy &&
      (!game.player_ptr->ai.control || ai_popup_windows) &&
      (get_client_state() == CLIENT_GAME_RUNNING_STATE)) {
    create_event(-1, -1, E_REVOLT_DONE, _("Game: Revolution finished"));
    popup_government_dialog();
  }
  
  update_players_dialog();
  update_worklist_report_dialog();

  if(pplayer==game.player_ptr) {
    if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
      if (!game.player_ptr->turn_done) {
	if (!client_is_observer()) {
	  enable_turn_done_button();
	}
      } else {
	update_turn_done_button(1);
      }
      update_info_label();
    }
  }
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
	  pinfo->name, pinfo->addr, pinfo->capability);
  
  if (!pinfo->used) {
    /* Forget the connection */
    if (!pconn) {
      freelog(LOG_VERBOSE, "Server removed unknown connection %d", pinfo->id);
      return;
    }
    client_remove_cli_conn(pconn);
    pconn = NULL;
  }
  else {
    /* Add or update the connection */
    struct player *pplayer =
      ((pinfo->player_num >= 0 && pinfo->player_num < game.nplayers)
       ? get_player(pinfo->player_num) : NULL);
    
    if (!pconn) {
      freelog(LOG_VERBOSE, "Server reports new connection %d %s",
	      pinfo->id, pinfo->name);
      pconn = fc_calloc(1, sizeof(struct connection));
      pconn->buffer = NULL;
      pconn->send_buffer = NULL;
      if (pplayer) {
	conn_list_insert_back(&pplayer->connections, pconn);
      }
      conn_list_insert_back(&game.all_connections, pconn);
      conn_list_insert_back(&game.est_connections, pconn);
      conn_list_insert_back(&game.game_connections, pconn);
    } else {
      freelog(LOG_DEBUG, "Server reports updated connection %d %s",
	      pinfo->id, pinfo->name);
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
    sz_strlcpy(pconn->name, pinfo->name);
    sz_strlcpy(pconn->addr, pinfo->addr);
    sz_strlcpy(pconn->capability, pinfo->capability);
  }
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
  struct government *g = get_gov_pplayer(pplayer);
  struct packet_spaceship_action packet;
  int i;
  
  if (ship->modules > (ship->habitation + ship->life_support
		       + ship->solar_panels)) {
    
    bool nice = government_has_hint(g, G_IS_NICE);
    /* "nice" governments prefer to keep success 100%;
     * others build habitation first (for score?)  (Thanks Massimo.)
     */

    packet.action =
      (ship->habitation==0)   ? SSHIP_ACT_PLACE_HABITATION :
      (ship->life_support==0) ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (ship->solar_panels==0) ? SSHIP_ACT_PLACE_SOLAR_PANELS :
      ((ship->habitation < ship->life_support)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_ACT_PLACE_HABITATION :
      (ship->solar_panels*2 < ship->habitation + ship->life_support)
                              ? SSHIP_ACT_PLACE_SOLAR_PANELS :
      (ship->life_support<ship->habitation)
                              ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (nice && (ship->life_support <= ship->habitation)
       && (ship->solar_panels*2 >= ship->habitation + ship->life_support + 1))
                              ? SSHIP_ACT_PLACE_LIFE_SUPPORT :
      (nice)                  ? SSHIP_ACT_PLACE_SOLAR_PANELS :
                                SSHIP_ACT_PLACE_HABITATION;

    if (packet.action == SSHIP_ACT_PLACE_HABITATION) {
      packet.num = ship->habitation + 1;
    } else if(packet.action == SSHIP_ACT_PLACE_LIFE_SUPPORT) {
      packet.num = ship->life_support + 1;
    } else {
      packet.num = ship->solar_panels + 1;
    }
    assert(packet.num<=NUM_SS_MODULES/3);

    send_packet_spaceship_action(&aconnection, &packet);
    return TRUE;
  }
  if (ship->components > ship->fuel + ship->propulsion) {
    if (ship->fuel <= ship->propulsion) {
      packet.action = SSHIP_ACT_PLACE_FUEL;
      packet.num = ship->fuel + 1;
    } else {
      packet.action = SSHIP_ACT_PLACE_PROPULSION;
      packet.num = ship->propulsion + 1;
    }
    send_packet_spaceship_action(&aconnection, &packet);
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
      packet.action = SSHIP_ACT_PLACE_STRUCTURAL;
      packet.num = 0;
      send_packet_spaceship_action(&aconnection, &packet);
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
    packet.action = SSHIP_ACT_PLACE_STRUCTURAL;
    packet.num = req;
    send_packet_spaceship_action(&aconnection, &packet);
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
  struct tile *ptile = map_get_tile(packet->x, packet->y);
  enum tile_terrain_type old_terrain = ptile->terrain;
  enum known_type old_known = ptile->known;
  bool tile_changed = FALSE;

  if (ptile->terrain != packet->type) { /*terrain*/
    tile_changed = TRUE;
    ptile->terrain = packet->type;
  }
  if (ptile->special != packet->special) { /*add-on*/
    tile_changed = TRUE;
    ptile->special = packet->special;
  }
  ptile->known = packet->known;

  reset_move_costs(packet->x, packet->y);

  /* fog of war remove units */
  if (ptile->known <= TILE_KNOWN_FOGGED && old_known == TILE_KNOWN) {
    unit_list_iterate(ptile->units, punit) {
      client_remove_unit(punit->id);
    }
    unit_list_iterate_end;
  }

  /* update continents */
  if ((packet->known >= TILE_KNOWN_FOGGED &&
       old_known == TILE_UNKNOWN && ptile->terrain != T_OCEAN) ||
      ((old_terrain == T_OCEAN) && (ptile->terrain != T_OCEAN))) {
    /* new knowledge or new land -- update can handle incrementally */
    climap_update_continents(packet->x, packet->y);
  } else if (old_known >= TILE_KNOWN_FOGGED &&
	     ((old_terrain != T_OCEAN) && (ptile->terrain == T_OCEAN))) {
    /* land changed into ocean -- rebuild continents map from scratch */
    whole_map_iterate(x, y) {
      map_set_continent(x, y, 0);
    }
    whole_map_iterate_end;
    climap_init_continents();
    whole_map_iterate(x, y) {
      if ((tile_get_known(x, y) >= TILE_KNOWN_FOGGED) &&
	  (map_get_terrain(x, y) != T_OCEAN))
	climap_update_continents(x, y);
    }
    whole_map_iterate_end;
  }

  /* refresh tiles */
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
    int x = packet->x, y = packet->y;

    /* the tile itself */
    if (tile_changed || old_known!=ptile->known)
      refresh_tile_mapcanvas(x, y, TRUE);

    /* if the terrain or the specials of the tile
       have changed it affects the adjacent tiles */
    if (tile_changed) {
      adjc_iterate(x, y, x1, y1) {
	if (tile_get_known(x1, y1) >= TILE_KNOWN_FOGGED)
	  refresh_tile_mapcanvas(x1, y1, TRUE);
      }
      adjc_iterate_end;
      return;
    }

    /* the "furry edges" on tiles adjacent to an TILE_UNKNOWN tile are
       removed here */
    if (old_known == TILE_UNKNOWN && packet->known >= TILE_KNOWN_FOGGED) {     
      cartesian_adjacent_iterate(x, y, x1, y1) {
	if (tile_get_known(x1, y1) >= TILE_KNOWN_FOGGED)
	  refresh_tile_mapcanvas(x1, y1, TRUE);
      }
      cartesian_adjacent_iterate_end;
    }
  }

  /* update menus if the focus unit is on the tile. */
  if (tile_changed) {
    struct unit *punit = get_unit_in_focus();
    if (punit && same_pos(punit->x, punit->y, packet->x, packet->y)) {
      update_menus();
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_remove_player(struct packet_generic_integer *packet)
{
  client_remove_player(packet->value);
}

/**************************************************************************
...
**************************************************************************/
void handle_select_nation(struct packet_nations_used *packet)
{
  if (get_client_state() == CLIENT_SELECT_RACE_STATE) {
    if (!packet) {
      set_client_state(CLIENT_WAITING_FOR_GAME_START_STATE);
      popdown_races_dialog();
    } else {
      races_toggles_set_sensitive(packet);
    }
  } else if (get_client_state() == CLIENT_PRE_GAME_STATE) {
    set_client_state(CLIENT_SELECT_RACE_STATE);
    popup_races_dialog();
    assert(packet != NULL);
    races_toggles_set_sensitive(packet);
  } else {
    freelog(LOG_ERROR,
	    "got a select nation packet in an incompatible state");
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

  reports_freeze();

  tilespec_free_city_tiles(game.styles_count);
  ruleset_data_free();

  game.aqueduct_size = packet->aqueduct_size;
  game.sewer_size = packet->sewer_size;
  game.add_to_size_limit = packet->add_to_size_limit;
  game.notradesize = packet->notradesize;
  game.fulltradesize = packet->fulltradesize;
  
  game.rtech.cathedral_plus = packet->rtech.cathedral_plus;
  game.rtech.cathedral_minus = packet->rtech.cathedral_minus;
  game.rtech.colosseum_plus = packet->rtech.colosseum_plus;
  game.rtech.temple_plus = packet->rtech.temple_plus;

  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    game.rtech.partisan_req[i]  = packet->rtech.partisan_req[i];
    freelog(LOG_DEBUG, "techl %d: %d", i, game.rtech.partisan_req[i]);
  }

  game.government_when_anarchy = packet->government_when_anarchy;
  game.default_government = packet->default_government;

  game.num_unit_types = packet->num_unit_types;
  game.num_impr_types = packet->num_impr_types;
  game.num_tech_types = packet->num_tech_types;

  governments_alloc(packet->government_count);

  nations_alloc(packet->nation_count);
  game.playable_nation_count = packet->playable_nation_count;

  city_styles_alloc(packet->style_count);
  tilespec_alloc_city_tiles(game.styles_count);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_unit(struct packet_ruleset_unit *p)
{
  struct unit_type *u;

  if(p->id < 0 || p->id >= game.num_unit_types || p->id >= U_LAST) {
    freelog(LOG_ERROR, "Received bad unit_type id %d in handle_ruleset_unit()",
	    p->id);
    return;
  }
  u = get_unit_type(p->id);

  sz_strlcpy(u->name, p->name);
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

  u->helptext = p->helptext;	/* pointer assignment */

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

  sz_strlcpy(a->name, p->name);
  a->req[0] = p->req[0];
  a->req[1] = p->req[1];
  a->flags = p->flags;
  a->preset_cost = p->preset_cost;
  a->num_reqs = p->num_reqs;
  
  a->helptext = p->helptext;	/* pointer assignment */
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_building(struct packet_ruleset_building *p)
{
  struct impr_type *b;

  if(p->id < 0 || p->id >= game.num_impr_types || p->id >= B_LAST) {
    freelog(LOG_ERROR,
	    "Received bad building id %d in handle_ruleset_building()",
	    p->id);
    return;
  }
  b = &improvement_types[p->id];

  sz_strlcpy(b->name, p->name);
  b->tech_req = p->tech_req;
  b->bldg_req = p->bldg_req;
  b->terr_gate = p->terr_gate;		/* pointer assignment */
  b->spec_gate = p->spec_gate;		/* pointer assignment */
  b->equiv_range = p->equiv_range;
  b->equiv_dupl = p->equiv_dupl;	/* pointer assignment */
  b->equiv_repl = p->equiv_repl;	/* pointer assignment */
  b->obsolete_by = p->obsolete_by;
  b->is_wonder = p->is_wonder;
  b->build_cost = p->build_cost;
  b->upkeep = p->upkeep;
  b->sabotage = p->sabotage;
  b->effect = p->effect;		/* pointer assignment */
  b->variant = p->variant;	/* FIXME: remove when gen-impr obsoletes */
  b->helptext = p->helptext;		/* pointer assignment */
  sz_strlcpy(b->soundtag, p->soundtag);
  sz_strlcpy(b->soundtag_alt, p->soundtag_alt);

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
	      "Never" :
	      advances[b->tech_req].name);
      freelog(LOG_DEBUG, "  bldg_req    %2d/%s",
	      b->bldg_req,
	      (b->bldg_req == B_LAST) ?
	      "None" :
	      improvement_types[b->bldg_req].name);
      freelog(LOG_DEBUG, "  terr_gate...");
      for (inx = 0; b->terr_gate[inx] != T_LAST; inx++) {
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
      freelog(LOG_DEBUG, "  obsolete_by %2d/%s",
	      b->obsolete_by, advances[b->obsolete_by].name);
      freelog(LOG_DEBUG, "  is_wonder   %2d", b->is_wonder);
      freelog(LOG_DEBUG, "  build_cost %3d", b->build_cost);
      freelog(LOG_DEBUG, "  upkeep      %2d", b->upkeep);
      freelog(LOG_DEBUG, "  sabotage   %3d", b->sabotage);
      freelog(LOG_DEBUG, "  effect...");
      for (inx = 0; b->effect[inx].type != EFT_LAST; inx++) {
	char buf[1024], *ptr;
	ptr = buf;
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " %d/%s",
		    b->effect[inx].type,
		    effect_type_name(b->effect[inx].type));
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " range=%d/%s",
		    b->effect[inx].range,
		    effect_range_name(b->effect[inx].range));
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " amount=%d",
		    b->effect[inx].amount);
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " survives=%d",
		    b->effect[inx].survives);
	ptr = strchr(ptr, '\0');
	freelog(LOG_DEBUG, "   %2d. %s", inx, buf);
	ptr = buf;
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " cond_bldg=%d/%s",
		    b->effect[inx].cond_bldg,
		    (b->effect[inx].cond_bldg == B_LAST) ?
		    "Uncond." :
		    improvement_types[b->effect[inx].cond_bldg].name);
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " cond_gov=%d/%s",
		    b->effect[inx].cond_gov,
		    (b->effect[inx].cond_gov == game.government_count) ?
		    "Uncond." :
		    get_government_name(b->effect[inx].cond_gov));
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " cond_adv=%d/%s",
		    b->effect[inx].cond_adv,
		    (b->effect[inx].cond_adv == A_NONE) ?
		    "Uncond." :
		    (b->effect[inx].cond_adv == A_LAST) ?
		    "Never" :
		    advances[b->effect[inx].cond_adv].name);
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " cond_eff=%d/%s",
		    b->effect[inx].cond_eff,
		    (b->effect[inx].cond_eff == EFT_LAST) ?
		    "Uncond." :
		    effect_type_name(b->effect[inx].cond_eff));
	ptr = strchr(ptr, '\0');
	freelog(LOG_DEBUG, "       %s", buf);
	ptr = buf;
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " aff_unit=%d/%s",
		    b->effect[inx].aff_unit,
		    (b->effect[inx].aff_unit == UCL_LAST) ?
		    "All" :
		    unit_class_name(b->effect[inx].aff_unit));
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " aff_terr=%d/%s",
		    b->effect[inx].aff_terr,
		    (b->effect[inx].aff_terr == T_LAST) ?
		    "None" :
		    (b->effect[inx].aff_terr == T_UNKNOWN) ?
		    "All" :
		    get_terrain_name(b->effect[inx].aff_terr));
	ptr = strchr(ptr, '\0');
	my_snprintf(ptr, sizeof(buf)-(ptr-buf), " aff_spec=%04X/%s",
		    b->effect[inx].aff_spec,
		    (b->effect[inx].aff_spec == 0) ?
		    "None" :
		    (b->effect[inx].aff_spec == S_ALL) ?
		    "All" :
		    get_special_name(b->effect[inx].aff_spec));
	ptr = strchr(ptr, '\0');
	freelog(LOG_DEBUG, "       %s", buf);
      }
      freelog(LOG_DEBUG, "  variant     %2d", b->variant);	/* FIXME: remove when gen-impr obsoletes */
      freelog(LOG_DEBUG, "  helptext    %s", b->helptext);
    } impr_type_iterate_end;
  }
#endif
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
  gov->corruption_modifier = p->corruption_modifier;
  gov->fixed_corruption_distance = p->fixed_corruption_distance;
  gov->corruption_distance_factor = p->corruption_distance_factor;
  gov->extra_corruption_distance = p->extra_corruption_distance;

  gov->flags               = p->flags;
  gov->hints               = p->hints;
  gov->num_ruler_titles    = p->num_ruler_titles;
    
  sz_strlcpy(gov->name, p->name);
  sz_strlcpy(gov->graphic_str, p->graphic_str);
  sz_strlcpy(gov->graphic_alt, p->graphic_alt);

  gov->ruler_titles = fc_calloc(gov->num_ruler_titles,
				sizeof(struct ruler_title));
  
  gov->helptext = p->helptext;	/* pointer assignment */
  
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
  sz_strlcpy(gov->ruler_titles[p->id].male_title, p->male_title);
  sz_strlcpy(gov->ruler_titles[p->id].female_title, p->female_title);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_terrain(struct packet_ruleset_terrain *p)
{
  struct tile_type *t;
  int j;

  if (p->id < T_FIRST || p->id >= T_COUNT) {
    freelog(LOG_ERROR,
	    "Received bad terrain id %d in handle_ruleset_terrain",
	    p->id);
    return;
  }
  t = &(tile_types[p->id]);

  sz_strlcpy(t->terrain_name, p->terrain_name);
  sz_strlcpy(t->graphic_str, p->graphic_str);
  sz_strlcpy(t->graphic_alt, p->graphic_alt);
  t->movement_cost = p->movement_cost;
  t->defense_bonus = p->defense_bonus;
  t->food = p->food;
  t->shield = p->shield;
  t->trade = p->trade;
  sz_strlcpy(t->special_1_name, p->special_1_name);
  t->food_special_1 = p->food_special_1;
  t->shield_special_1 = p->shield_special_1;
  t->trade_special_1 = p->trade_special_1;
  sz_strlcpy(t->special_2_name, p->special_2_name);
  t->food_special_2 = p->food_special_2;
  t->shield_special_2 = p->shield_special_2;
  t->trade_special_2 = p->trade_special_2;

  for(j=0; j<2; j++) {
    sz_strlcpy(t->special[j].graphic_str, p->special[j].graphic_str);
    sz_strlcpy(t->special[j].graphic_alt, p->special[j].graphic_alt);
  }

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
  
  t->helptext = p->helptext;	/* pointer assignment */
  
  tilespec_setup_tile_type(p->id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_terrain_control(struct terrain_misc *p)
{
  terrain_control.river_style = p->river_style;
  terrain_control.may_road = p->may_road;
  terrain_control.may_irrigate = p->may_irrigate;
  terrain_control.may_mine = p->may_mine;
  terrain_control.may_transform = p->may_transform;
  terrain_control.ocean_reclaim_requirement = p->ocean_reclaim_requirement;
  terrain_control.land_channel_requirement = p->land_channel_requirement;
  terrain_control.river_move_mode = p->river_move_mode;
  terrain_control.river_defense_bonus = p->river_defense_bonus;
  terrain_control.river_trade_incr = p->river_trade_incr;
  free(terrain_control.river_help_text);
  terrain_control.river_help_text = p->river_help_text; /* malloc'ed string */
  terrain_control.fortress_defense_bonus = p->fortress_defense_bonus;
  terrain_control.road_superhighway_trade_bonus = p->road_superhighway_trade_bonus;
  terrain_control.rail_food_bonus = p->rail_food_bonus;
  terrain_control.rail_shield_bonus = p->rail_shield_bonus;
  terrain_control.rail_trade_bonus = p->rail_trade_bonus;
  terrain_control.farmland_supermarket_food_bonus = p->farmland_supermarket_food_bonus;
  terrain_control.pollution_food_penalty = p->pollution_food_penalty;
  terrain_control.pollution_shield_penalty = p->pollution_shield_penalty;
  terrain_control.pollution_trade_penalty = p->pollution_trade_penalty;
  terrain_control.fallout_food_penalty = p->fallout_food_penalty;
  terrain_control.fallout_shield_penalty = p->fallout_shield_penalty;
  terrain_control.fallout_trade_penalty = p->fallout_trade_penalty;
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_nation(struct packet_ruleset_nation *p)
{
  int i;
  struct nation_type *pl;

  if(p->id < 0 || p->id >= game.nation_count || p->id >= MAX_NUM_NATIONS) {
    freelog(LOG_ERROR, "Received bad nation id %d in handle_ruleset_nation()",
	    p->id);
    return;
  }
  pl = get_nation_by_idx(p->id);

  sz_strlcpy(pl->name, p->name);
  sz_strlcpy(pl->name_plural, p->name_plural);
  sz_strlcpy(pl->flag_graphic_str, p->graphic_str);
  sz_strlcpy(pl->flag_graphic_alt, p->graphic_alt);
  pl->leader_count = p->leader_count;
  for( i=0; i<pl->leader_count; i++) {
    pl->leader_name[i] = mystrdup(p->leader_name[i]);
    pl->leader_is_male[i] = p->leader_sex[i];
  }
  pl->city_style = p->city_style;

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

  sz_strlcpy(cs->name, packet->name);
  sz_strlcpy(cs->graphic, packet->graphic);
  sz_strlcpy(cs->graphic_alt, packet->graphic_alt);

  tilespec_setup_city_tiles(id);
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_game(struct packet_ruleset_game *packet)
{
  game.rgame.min_city_center_food = packet->min_city_center_food;
  game.rgame.min_city_center_shield = packet->min_city_center_shield;
  game.rgame.min_city_center_trade = packet->min_city_center_trade;
  game.rgame.min_dist_bw_cities = packet->min_dist_bw_cities;
  game.rgame.init_vis_radius_sq = packet->init_vis_radius_sq;
  game.rgame.hut_overflight = packet->hut_overflight;
  game.rgame.pillage_select = packet->pillage_select;
  game.rgame.nuke_contamination = packet->nuke_contamination;
  game.rgame.granary_food_ini = packet->granary_food_ini;
  game.rgame.granary_food_inc = packet->granary_food_inc;
  game.rgame.tech_cost_style = packet->tech_cost_style;
  game.rgame.tech_leakage = packet->tech_leakage;
}

/**************************************************************************
...
**************************************************************************/
void handle_incite_cost(struct packet_generic_values *packet)
{
  struct city *pcity=find_city_by_id(packet->id);
  struct unit *punit=find_unit_by_id(packet->id);

  if(pcity) {
    pcity->incite_revolt_cost = packet->value1;
    if(!game.player_ptr->ai.control || ai_popup_windows)
      popup_incite_dialog(pcity);
    return;
  }

  if(punit) {
    punit->bribe_cost = packet->value1;
    if(!game.player_ptr->ai.control || ai_popup_windows)
      popup_bribe_dialog(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_options(struct packet_generic_values *preq)
{
  struct city *pcity = find_city_by_id(preq->value1);
  
  if (!pcity || pcity->owner != game.player_idx) return;
  pcity->city_options = preq->value2;
}

/**************************************************************************
...
**************************************************************************/
void handle_city_name_suggestion(struct packet_city_name_suggestion *packet)
{
  struct unit *punit;
  
  punit = player_find_unit_by_id(game.player_ptr, packet->id);
  if (punit) {
    popup_newcity_dialog(punit, packet->name);
    return;
  }
  /* maybe unit died; ignore */
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomat_action(struct packet_diplomat_action *packet)
{
  struct unit *pdiplomat=player_find_unit_by_id(game.player_ptr, packet->diplomat_id);

  if (!pdiplomat) {
    freelog(LOG_ERROR, "Received bad diplomat id %d in handle_diplomat_action()",
	    packet->diplomat_id);
    return;
  }

  switch(packet->action_type) {
  case DIPLOMAT_CLIENT_POPUP_DIALOG:
    process_diplomat_arrival(pdiplomat, packet->target_id);
    break;
  default:
    freelog(LOG_ERROR, "Received bad action %d in handle_diplomat_action()",
	    packet->action_type);
    break;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_sabotage_list(struct packet_sabotage_list *packet)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, packet->diplomat_id);
  struct city *pcity = find_city_by_id(packet->city_id);

  if (punit && pcity) {
    impr_type_iterate(i) {
      pcity->improvements[i] = (packet->improvements[i]=='1') ? I_ACTIVE : I_NONE;
    } impr_type_iterate_end;

    popup_sabotage_dialog(pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_player_attribute_chunk(struct packet_attribute_chunk *chunk)
{
  generic_handle_attribute_chunk(game.player_ptr, chunk);

  if (chunk->offset + chunk->chunk_length == chunk->total_length) {
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
