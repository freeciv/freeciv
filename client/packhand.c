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

#include "packhand.h"

extern int seconds_to_turndone;
extern int last_turn_gold_amount;
extern int did_advance_tech_this_turn;
extern char name[512];


static void handle_city_packet_common(struct city *pcity, int is_new,
                                      int popup);

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
  punit->foul = 0;		/* never used in client/ */
  punit->ord_map = 0;		/* never used in client/ */
  punit->ord_city = 0;		/* never used in client/ */
  punit->moved = 0;		/* never used in client/ */
  punit->transported_by = 0;	/* never used in client/ */
}

/**************************************************************************
...
**************************************************************************/
void handle_join_game_reply(struct packet_join_game_reply *packet)
{
  char msg[MAX_LEN_MSG];
  char *s_capability = aconnection.capability;

  mystrlcpy(s_capability, packet->capability, sizeof(aconnection.capability));

  if (packet->you_can_join) {
    freelog(LOG_VERBOSE, "join game accept:%s", packet->message);
    aconnection.established = 1;
    game.conn_id = packet->conn_id;
  } else {
    my_snprintf(msg, sizeof(msg),
		_("You were rejected from the game: %s"), packet->message);
    append_output_window(msg);
    game.conn_id = 0;
  }
  if (strcmp(s_capability, our_capability)==0)
    return;
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

  if (pcity==NULL)
    return;

  x = pcity->x; y = pcity->y;
  client_remove_city(pcity);
  reset_move_costs(x, y);
}

/**************************************************************************
...
**************************************************************************/
void handle_remove_unit(struct packet_generic_integer *packet)
{
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
  int show_combat = FALSE;
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
      decrease_unit_hp_smooth(punit0, packet->attacker_hp,
			      punit1, packet->defender_hp);
    }
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
    enable_turn_done_button();
    player_set_unit_focus_status(game.player_ptr);

    update_info_label();	/* get initial population right */
    update_unit_focus();
    update_unit_info_label(get_unit_in_focus());

    if(get_unit_in_focus())
      center_tile_mapcanvas(get_unit_in_focus()->x, get_unit_in_focus()->y);
    
    free_intro_radar_sprites();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_info(struct packet_city_info *packet)
{
  int i, x, y, city_is_new;
  struct city *pcity;
  int popup;
  int update_descriptions = 0;

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner != packet->owner)) {
    /* With current server, this shouldn't happen: when city changes
     * owner it is re-created with a new id.  Unclear what to do here;
     * try to cope...
     */
    freelog(LOG_ERROR,
	    "Got existing city id (%d %s) with wrong owner (%d %s, old %d %s)",
	    packet->id, pcity->name, packet->owner,
	    get_player(packet->owner)->name, pcity->owner,
	    get_player(pcity->owner)->name);
    client_remove_city(pcity);
    pcity = 0;
  }

  if(!pcity) {
    city_is_new=1;
    pcity=fc_malloc(sizeof(struct city));
    pcity->id=packet->id;
    idex_register_city(pcity);
  }
  else {
    city_is_new=0;

    /* Check if city desciptions should be updated */
    if (draw_city_names && strcmp(pcity->name,packet->name)) {
      update_descriptions = 1;
    }
    if (draw_city_productions &&
        ((pcity->is_building_unit != packet->is_building_unit) ||
         (pcity->currently_building != packet->currently_building))) {
      update_descriptions = 1;
    }

    assert(pcity->id == packet->id);
  }
  
  pcity->owner=packet->owner;
  pcity->x=packet->x;
  pcity->y=packet->y;
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;
  pcity->ppl_happy[4]=packet->ppl_happy;
  pcity->ppl_content[4]=packet->ppl_content;
  pcity->ppl_unhappy[4]=packet->ppl_unhappy;
  pcity->ppl_elvis=packet->ppl_elvis;
  pcity->ppl_scientist=packet->ppl_scientist;
  pcity->ppl_taxman=packet->ppl_taxman;

  pcity->city_options=packet->city_options;
  
  for (i=0;i<4;i++)  {
    pcity->trade[i]=packet->trade[i];
    pcity->trade_value[i]=packet->trade_value[i];
  }
  
  pcity->food_prod=packet->food_prod;
  pcity->food_surplus=packet->food_surplus;
  pcity->shield_prod=packet->shield_prod;
  pcity->shield_surplus=packet->shield_surplus;
  pcity->trade_prod=packet->trade_prod;
  pcity->corruption=packet->corruption;
  
  pcity->luxury_total=packet->luxury_total;
  pcity->tax_total=packet->tax_total;
  pcity->science_total=packet->science_total;
  
  pcity->food_stock=packet->food_stock;
  pcity->shield_stock=packet->shield_stock;
  pcity->pollution=packet->pollution;

  pcity->is_building_unit=packet->is_building_unit;
  pcity->currently_building=packet->currently_building;
  if (city_is_new)
    pcity->worklist = create_worklist();
  copy_worklist(pcity->worklist, &packet->worklist);
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;

  pcity->turn_last_built=packet->turn_last_built;
  pcity->turn_changed_target=packet->turn_changed_target;
  pcity->changed_from_id=packet->changed_from_id;
  pcity->changed_from_is_unit=packet->changed_from_is_unit;
  pcity->before_change_shields=packet->before_change_shields;

  i=0;
  for(y=0; y<CITY_MAP_SIZE; y++) {
    for(x=0; x<CITY_MAP_SIZE; x++) {
      if (city_is_new) {
	/* Need to pre-initialize before set_worker_city()  -- dwp */
	pcity->city_map[x][y] = C_TILE_EMPTY;
      }
      set_worker_city(pcity,x,y,packet->city_map[i++]-'0');
    }
  }
    
  for(i=0; i<game.num_impr_types; i++)
    pcity->improvements[i]=(packet->improvements[i]=='1') ? 1 : 0;

  popup = (city_is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE && 
           pcity->owner==game.player_idx) || packet->diplomat_investigate;

  handle_city_packet_common(pcity, city_is_new, popup);

  /* update the descriptions if necessary */
  if (update_descriptions && tile_visible_mapcanvas(pcity->x,pcity->y)) {
    /* update it only every second (because of ChangeAll), this is not
       a perfect solution at all! */
    static int timer_initialized;
    static time_t timer;
    int really_update = 1;
    time_t new_timer = time(NULL);

    if (timer_initialized) {
      double diff;
      timer = time(NULL);
      diff = difftime(new_timer,timer);
      if (diff < 1.0) {
	really_update = 0;
      }
    }

    if (really_update) {
      update_city_descriptions();
      timer = new_timer;
      timer_initialized = 1;
    }
  }
}

/**************************************************************************
  ...
**************************************************************************/
static void handle_city_packet_common(struct city *pcity, int is_new,
                                      int popup)
{
  int i;

  if(is_new) {
    unit_list_init(&pcity->units_supported);
    unit_list_init(&pcity->info_units_supported);
    unit_list_init(&pcity->info_units_present);
    city_list_insert(&game.players[pcity->owner].cities, pcity);
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

  if(draw_map_grid &&
     is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE) {
    /* just to update grid; slow, but doesn't happen very often --jjm */
    int r=((CITY_MAP_SIZE+1)/2);
    int d=(2*r)+1;
    int x=map_adjust_x(pcity->x - r);
    int y=map_adjust_y(pcity->y - r);
    update_map_canvas
    (
     map_canvas_adjust_x(x), map_canvas_adjust_y(y),
     d, d,
     1
    );
  } else {
    refresh_tile_mapcanvas(pcity->x, pcity->y, 1);
  }

  if(city_workers_display==pcity)  {
    put_city_workers(pcity, -1);
    city_workers_display=NULL;
  }

  if( popup &&
     (!game.player_ptr->ai.control || ai_popup_windows)) {
    update_menus();
    popup_city_dialog(pcity, 0);
  }

  if(!is_new && pcity->owner==game.player_idx) {
    struct unit *punit = get_unit_in_focus();
    if (punit && (punit->x == pcity->x) && (punit->y == pcity->y)) {
      update_menus();
    }
    refresh_city_dialog(pcity);
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
  int city_is_new;
  int update_descriptions = 0;

  pcity=find_city_by_id(packet->id);

  if (pcity && (pcity->owner != packet->owner)) {
    /* With current server, this shouldn't happen: when city changes
     * owner it is re-created with a new id.  Unclear what to do here;
     * try to cope...
     */
    freelog(LOG_ERROR,
	    "Got existing city id (%d %s) with wrong owner (%d %s, old %d %s)",
	    packet->id, pcity->name, packet->owner,
	    get_player(packet->owner)->name, pcity->owner,
	    get_player(pcity->owner)->name);
    client_remove_city(pcity);
    pcity = 0;
  }

  if(!pcity) {
    city_is_new=1;
    pcity=fc_malloc(sizeof(struct city));
    pcity->id=packet->id;
    idex_register_city(pcity);
  }
  else {
    city_is_new=0;

    /* Check if city desciptions should be updated */
    if (draw_city_names && strcmp(pcity->name,packet->name)) {
      update_descriptions = 1;
    }
    
    assert(pcity->id == packet->id);
  }

  pcity->owner=packet->owner;
  pcity->x=packet->x;
  pcity->y=packet->y;
  sz_strlcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;

  if (packet->happy) {
    pcity->ppl_happy[4]   = pcity->size;
    pcity->ppl_unhappy[4] = 0;
  } else {
    pcity->ppl_unhappy[4] = pcity->size;
    pcity->ppl_happy[4]   = 0;
  }

  pcity->improvements[B_PALACE] = packet->capital;
  pcity->improvements[B_CITY]   = packet->walls;

  if (city_is_new)
    pcity->worklist = create_worklist();

  /* This sets dumb values for everything else. This is not really required,
     but just want to be at the safe side. */
  {
    int i;
    int x, y;

    pcity->ppl_elvis          = pcity->size;
    pcity->ppl_scientist      = 0;
    pcity->ppl_taxman         = 0;
    for (i=0;i<4;i++)  {
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
    pcity->is_building_unit    = 0;
    pcity->currently_building = 0;
    init_worklist(pcity->worklist);
    pcity->airlift            = 0;
    pcity->did_buy            = 0;
    pcity->did_sell           = 0;
    pcity->was_happy          = 0;
    for(i=0; i<game.num_impr_types; i++) {
      if (i != B_PALACE && i != B_CITY)
        pcity->improvements[i] = 0;
    }
    for(y=0; y<CITY_MAP_SIZE; y++)
      for(x=0; x<CITY_MAP_SIZE; x++)
	pcity->city_map[x][y] = C_TILE_EMPTY;
  } /* Dumb values */

  handle_city_packet_common(pcity, city_is_new, 0);

  /* update the descriptions if necessary */
  if (update_descriptions && tile_visible_mapcanvas(pcity->x,pcity->y)) {
    update_city_descriptions();
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_new_year(struct packet_new_year *ppacket)
{
  update_turn_done_button(1);
  enable_turn_done_button();
  
  game.year=ppacket->year;
  update_info_label();

  player_set_unit_focus_status(game.player_ptr);
  update_unit_focus();
  auto_center_on_focus_unit();

  update_unit_info_label(get_unit_in_focus());

  seconds_to_turndone=game.timeout;

  turn_gold_difference=game.player_ptr->economic.gold-last_turn_gold_amount;
  last_turn_gold_amount=game.player_ptr->economic.gold;

  plrdlg_update_delay_off();
  update_players_dialog();

  report_update_delay_off();
  update_report_dialogs();

  meswin_update_delay_off();
  update_meswin_dialog();

  update_city_descriptions();

  if(game.player_ptr->ai.control && !ai_manual_turn_done)
    user_ended_turn();

  if(sound_bell_at_new_turn &&
     (!game.player_ptr->ai.control || ai_manual_turn_done))
    sound_bell();
}

/**************************************************************************
...
**************************************************************************/
void handle_before_new_year(void)
{
  clear_notify_window();
  plrdlg_update_delay_on();
  report_update_delay_on();
  meswin_update_delay_on();
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
  if (where & MW_OUTPUT)
    append_output_window(packet->message);
  if (where & MW_MESSAGES)
    add_notify_window(packet);
  if ((where & MW_POPUP) &&
      (!game.player_ptr->ai.control || ai_popup_windows))
    popup_notify_goto_dialog("Popup Request", packet->message, 
			     packet->x, packet->y);
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
       packet->event != BROADCAST_EVENT)
    popup_notify_dialog(caption, headline, lines);
}

/**************************************************************************
...
**************************************************************************/
void handle_move_unit(struct packet_move_unit *packet)
{
  /* should there be something here? - mjd */
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct packet_unit_info *packet)
{
  struct city *pcity;
  struct unit *punit;
  int repaint_unit;
  int repaint_city;		/* regards unit's homecity */

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

  repaint_unit = 0;
  repaint_city = 0;
  punit = player_find_unit_by_id(&game.players[packet->owner], packet->id);

  if(punit) {
    int dest_x,dest_y;
    if((punit->activity!=packet->activity)         /* change activity */
       || (punit->activity_target!=packet->activity_target)) { /*   or act's target */
      repaint_unit=1;
      if(wakeup_focus && (punit->owner==game.player_idx)
                      && (punit->activity==ACTIVITY_SENTRY)) {
        set_unit_focus(punit);
        /* RP: focus on (each) activated unit (e.g. when unloading a ship) */
      }

      punit->activity=packet->activity;
      punit->activity_target=packet->activity_target;

      if(punit->owner==game.player_idx) 
        refresh_unit_city_dialogs(punit);
      /*      refresh_tile_mapcanvas(punit->x, punit->y, 1);
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
	repaint_city=1;
      }
    }

    if(punit->hp!=packet->hp) {                      /* hp changed */
      punit->hp=packet->hp;
      repaint_unit=1;
    }
    if (punit->type!=packet->type) {
      struct city *pcity = map_get_city(punit->x, punit->y);
      
      punit->type=packet->type;
      repaint_unit=1;
      repaint_city=1;
      if (pcity && (pcity->id != punit->homecity)) {
	refresh_city_dialog(pcity);
      }
    }
    if (punit->ai.control!=packet->ai) {
      punit->ai.control=packet->ai;
      repaint_unit = 1;
    }

    if(punit->x!=packet->x || punit->y!=packet->y) { /* change position */
      struct city *pcity;
      pcity=map_get_city(punit->x, punit->y);
      
      if(tile_is_known(packet->x, packet->y) == TILE_KNOWN) {
	do_move_unit(punit, packet);
	update_unit_focus();
      }
      else {
	do_move_unit(punit, packet); /* nice to see where a unit is going */
	client_remove_unit(punit->id);
	refresh_tile_mapcanvas(packet->x, packet->y, 1);
        return;
      }
      if(pcity)  {
        if(pcity->id==punit->homecity)
	  repaint_city=1;
	else
	  refresh_city_dialog(pcity);
      }
      
      if((pcity=map_get_city(punit->x, punit->y)))  {
        if(pcity->id == punit->homecity)
	  repaint_city=1;
	else
	  refresh_city_dialog(pcity);
	
	if(unit_flag(punit->type, F_CARAVAN)
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
      
      repaint_unit=0;
    }
    if (punit->unhappiness!=packet->unhappiness) {
      punit->unhappiness=packet->unhappiness;
      repaint_city=1;
    }
    if (punit->upkeep!=packet->upkeep) {
      punit->upkeep=packet->upkeep;
      repaint_city=1;
    }
    if (punit->upkeep_food!=packet->upkeep_food) {
      punit->upkeep_food=packet->upkeep_food;
      repaint_city=1;
    }
    if (punit->upkeep_gold!=packet->upkeep_gold) {
      punit->upkeep_gold=packet->upkeep_gold;
      repaint_city=1;
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
    if (!(tile_is_known(punit->x,punit->y) == TILE_KNOWN)) {
      client_remove_unit(packet->id);
      refresh_tile_mapcanvas(dest_x, dest_y, 1);
    }
  }

  else {      /* create new unit */
    punit=fc_malloc(sizeof(struct unit));
    unpackage_unit(punit, packet);
    idex_register_unit(punit);

    punit->activity_count=0;	/* never used in client/ or common/  --dwp */

    unit_list_insert(&game.players[packet->owner].units, punit);
    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);

    freelog(LOG_DEBUG, "New %s %s id %d (%d %d) hc %d %s", 
	   get_nation_name(get_player(punit->owner)->nation),
	   unit_name(punit->type), punit->x, punit->y, punit->id,
	   punit->homecity, (pcity ? pcity->name : _("(unknown)")));

    if (packet->carried)
      repaint_unit=0;
    else
      repaint_unit=1;
  }

  if(punit && punit==get_unit_in_focus())
    update_unit_info_label(punit);
  else if(get_unit_in_focus() && get_unit_in_focus()->x==punit->x &&
	  get_unit_in_focus()->y==punit->y) {
    update_unit_info_label(get_unit_in_focus());
  }

  if(repaint_unit)
    refresh_tile_mapcanvas(punit->x, punit->y, 1);

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
  int i, boot_help;
  game.gold=pinfo->gold;
  game.tech=pinfo->tech;
  game.techlevel=pinfo->techlevel;
  game.skill_level=pinfo->skill_level;
  game.timeout=pinfo->timeout;

  game.end_year=pinfo->end_year;
  game.year=pinfo->year;
  game.min_players=pinfo->min_players;
  game.max_players=pinfo->max_players;
  game.nplayers=pinfo->nplayers;
  game.globalwarming=pinfo->globalwarming;
  game.heating=pinfo->heating;
  game.nuclearwinter=pinfo->nuclearwinter;
  game.cooling=pinfo->cooling;
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    game.player_idx=pinfo->player_idx;
    game.player_ptr=&game.players[game.player_idx];
  }
  for(i=0; i<A_LAST/*game.num_tech_types*/; i++)
    game.global_advances[i]=pinfo->global_advances[i];
  for(i=0; i<B_LAST/*game.num_impr_types*/; i++)
    game.global_wonders[i]=pinfo->global_wonders[i];
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(get_client_state()==CLIENT_SELECT_RACE_STATE)
      popdown_races_dialog();
  }
  game.techpenalty=pinfo->techpenalty;
  game.foodbox=pinfo->foodbox;
  game.civstyle=pinfo->civstyle;
/* when removing "game_ruleset" capability,
remove this *entire* piece of code (to REMOVE TO HERE, below) */
if (!has_capability("game_ruleset", aconnection.capability)) {
if (game.civstyle == 1) {
game.rgame.min_city_center_food = 0;
game.rgame.min_city_center_shield = 0;
game.rgame.min_city_center_trade = 0;
game.rgame.min_dist_bw_cities = 1;
game.rgame.init_vis_radius_sq = 2;
game.rgame.hut_overflight = OVERFLIGHT_NOTHING;
game.rgame.pillage_select = 0;
} else {
game.rgame.min_city_center_food = 1;
game.rgame.min_city_center_shield = 1;
game.rgame.min_city_center_trade = 0;
game.rgame.min_dist_bw_cities = 2;
game.rgame.init_vis_radius_sq = 5;
game.rgame.hut_overflight = OVERFLIGHT_FRIGHTEN;
game.rgame.pillage_select = 1;
}
game.rgame.nuke_contamination = CONTAMINATION_POLLUTION;
}
/* REMOVE TO HERE */

  boot_help = (get_client_state() == CLIENT_GAME_RUNNING_STATE
	       && game.spacerace != pinfo->spacerace);
  game.spacerace=pinfo->spacerace;
  if (game.timeout) {
    if (pinfo->seconds_to_turndone)
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
void handle_player_info(struct packet_player_info *pinfo)
{
  int i;
  int poptechup;
  char msg[MAX_LEN_MSG];
  struct player *pplayer=&game.players[pinfo->playerno];

  sz_strlcpy(pplayer->name, pinfo->name);
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

  for (i = 0; i < MAX_NUM_WORKLISTS; i++)
    copy_worklist(&pplayer->worklists[i], &pinfo->worklists[i]);

  for(i=0; i<game.num_tech_types; i++)
    pplayer->research.inventions[i]=pinfo->inventions[i]-'0';
  update_research(pplayer);

  poptechup = (pplayer->research.researching!=pinfo->researching);
  pplayer->research.researched=pinfo->researched;
  pplayer->research.researchpoints=pinfo->researchpoints;
  pplayer->research.researching=pinfo->researching;
  pplayer->future_tech=pinfo->future_tech;
  pplayer->ai.tech_goal=pinfo->tech_goal;

  /* Remove this when "conn_info" capability removed: */
  if (!has_capability("conn_info", aconnection.capability)) {
    /* Fake things for old servers; old server sends blank_addr_str
     * for non-connected players.
     */
    struct connection *pconn;
    if (strcmp(pinfo->addr, blank_addr_str)==0) {
      while (conn_list_size(&pplayer->connections)) { /* only expect one */
	pconn = conn_list_get(&pplayer->connections, 0);
	client_remove_cli_conn(pconn);
      }
    } else {
      if (conn_list_size(&pplayer->connections)) {
	pconn = conn_list_get(&pplayer->connections, 0);
      } else {
	pconn = fc_calloc(1, sizeof(struct connection));
	pconn->buffer = NULL;
	pconn->send_buffer = NULL;
	conn_list_insert_back(&pplayer->connections, pconn);
	conn_list_insert_back(&game.all_connections, pconn);
	conn_list_insert_back(&game.est_connections, pconn);
	conn_list_insert_back(&game.game_connections, pconn);
      }
      sz_strlcpy(pconn->capability, pinfo->capability);
      sz_strlcpy(pconn->addr, pinfo->addr);
      sz_strlcpy(pconn->name, pplayer->name);
      pconn->player = pplayer;
      pconn->established = 1;
    }
  }
  /* Remove to here */
  
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE && pplayer==game.player_ptr) {
    sz_strlcpy(name, pplayer->name);
    if(poptechup) {
      if(!game.player_ptr->ai.control || ai_popup_windows)
	popup_science_dialog(1);
      did_advance_tech_this_turn=game.year;
      science_dialog_update();
    } 
  }

  pplayer->turn_done=pinfo->turn_done;
  pplayer->nturns_idle=pinfo->nturns_idle;
  pplayer->is_alive=pinfo->is_alive;
  
  pplayer->is_connected=pinfo->is_connected;

  pplayer->ai.is_barbarian=pinfo->is_barbarian;
  pplayer->revolution=pinfo->revolution;
  if(pplayer->ai.control!=pinfo->ai)  {
    pplayer->ai.control=pinfo->ai;
    if(pplayer==game.player_ptr)  {
      my_snprintf(msg, sizeof(msg), _("AI Mode is now %s."),
		  game.player_ptr->ai.control?_("ON"):_("OFF"));
      append_output_window(msg);
    }
  }
  
  if(pplayer==game.player_ptr && 
     (pplayer->revolution < 1 || pplayer->revolution > 5) && 
     pplayer->government==game.government_when_anarchy &&
     (!game.player_ptr->ai.control || ai_popup_windows) &&
     (get_client_state()==CLIENT_GAME_RUNNING_STATE))
    popup_government_dialog();
  
  update_players_dialog();
  update_worklist_report_dialog();

  if(pplayer==game.player_ptr) {
    if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
      if (!game.player_ptr->turn_done) {
	enable_turn_done_button();
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
  
  if (pinfo->used==0) {
    /* Forget the connection */
    if (!pconn) {
      freelog(LOG_VERBOSE, "Server removed unknown connection %d", pinfo->id);
      return;
    }
    client_remove_cli_conn(pconn);
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
static int spaceship_autoplace(struct player *pplayer,
			       struct player_spaceship *ship)
{
  struct government *g = get_gov_pplayer(pplayer);
  struct packet_spaceship_action packet;
  int i;
  
  if (ship->modules > (ship->habitation + ship->life_support
		       + ship->solar_panels)) {
    
    int nice = government_has_hint(g, G_IS_NICE);
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
    return 1;
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
    return 1;
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
      return 1;
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
    return 1;
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void handle_spaceship_info(struct packet_spaceship_info *p)
{
  int i;
  struct player *pplayer=&game.players[p->player_num];
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
    ship->structure[i] = p->structure[i]-'0';
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
  int tile_changed = 0;

  if (ptile->terrain != packet->type) { /*terrain*/
    tile_changed = 1;
    ptile->terrain = packet->type;
  }
  if (ptile->special != packet->special) { /*add-on*/
    tile_changed = 1;
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
    int x, y;
    for (y = 0; y < map.ysize; y++)
      for (x = 0; x < map.xsize; x++)
	map_set_continent(x, y, 0);
    climap_init_continents();
    for (y = 0; y < map.ysize; y++)
      for (x = 0; x < map.xsize; x++)
	if ((tile_is_known(x, y) >= TILE_KNOWN_FOGGED) &&
	    (map_get_terrain(x, y) != T_OCEAN))
	  climap_update_continents(x, y);
  }

  /* refresh tiles */
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
    int x = packet->x, y = packet->y;

    /* the tile itself */
    if (tile_changed || old_known!=ptile->known)
      refresh_tile_mapcanvas(x, y, 1);

    /* if the terrain or the specials of the tile
       have changed it affects the adjacent tiles */
    if (tile_changed) {
      if (tile_is_known(x-1, y) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x-1, y, 1);
      if (tile_is_known(x+1, y) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x+1, y, 1);
      if (tile_is_known(x, y-1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x, y-1, 1);
      if (tile_is_known(x, y+1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x, y+1, 1);
      if (tile_is_known(x-1, y+1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x-1, y+1, 1);
      if (tile_is_known(x+1, y+1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x+1, y+1, 1);
      if (tile_is_known(x+1, y-1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x+1, y-1, 1);
      if (tile_is_known(x-1, y-1) >= TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x-1, y-1, 1);
      return;
    }

    /* the "furry edges" on tiles adjacent to an TILE_UNKNOWN tile are removed here */
    if (old_known == TILE_UNKNOWN && packet->known >= TILE_KNOWN_FOGGED) {     
      if(tile_is_known(x-1, y)>=TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x-1, y, 1);
      if(tile_is_known(x+1, y)>=TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x+1, y, 1);
      if(tile_is_known(x, y-1)>=TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x, y-1, 1);
      if(tile_is_known(x, y+1)>=TILE_KNOWN_FOGGED)
	refresh_tile_mapcanvas(x, y+1, 1);
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
void handle_select_nation(struct packet_generic_values *packet)
{
  if(get_client_state()==CLIENT_SELECT_RACE_STATE) {
    if(packet->value2 == 0xffff) {
      set_client_state(CLIENT_WAITING_FOR_GAME_START_STATE);
      popdown_races_dialog();
    }
    else
      races_toggles_set_sensitive( packet->value1, packet->value2);
  }
  else if(get_client_state()==CLIENT_PRE_GAME_STATE) {
    set_client_state(CLIENT_SELECT_RACE_STATE);
    popup_races_dialog();
    races_toggles_set_sensitive( packet->value1, packet->value2);
  }
  else
    freelog(LOG_ERROR, "got a select nation packet in an incompatible state");
}

/**************************************************************************
  Take arrival of ruleset control packet to indicate that
  current allocated governments should be free'd, and new
  memory allocated for new size. The same for nations.
**************************************************************************/
void handle_ruleset_control(struct packet_ruleset_control *packet)
{
  int i;
  
  for(i=0; i<game.government_count; i++) {
    free(get_government(i)->ruler_titles);
    free(get_government(i)->helptext);
  }
  free(governments);
      
  game.aqueduct_size = packet->aqueduct_size;
  game.sewer_size = packet->sewer_size;
  game.add_to_size_limit = packet->add_to_size_limit;
  
  game.rtech.get_bonus_tech = packet->rtech.get_bonus_tech;
  game.rtech.cathedral_plus = packet->rtech.cathedral_plus;
  game.rtech.cathedral_minus = packet->rtech.cathedral_minus;
  game.rtech.colosseum_plus = packet->rtech.colosseum_plus;
  game.rtech.temple_plus = packet->rtech.temple_plus;

  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    game.rtech.partisan_req[i]  = packet->rtech.partisan_req[i];
    freelog(LOG_DEBUG, "techl %d: %d", i, game.rtech.partisan_req[i]);
  }

  game.government_count = packet->government_count;
  game.government_when_anarchy = packet->government_when_anarchy;
  game.default_government = packet->default_government;

  game.num_unit_types = packet->num_unit_types;
  game.num_impr_types = packet->num_impr_types;
  game.num_tech_types = packet->num_tech_types;

  governments = fc_calloc(game.government_count, sizeof(struct government));
  for(i=0; i<game.government_count; i++) {
    get_government(i)->ruler_titles = NULL;
    get_government(i)->helptext = NULL;
  }

  free_nations(game.nation_count);
  game.nation_count = packet->nation_count;
  alloc_nations(game.nation_count);
  game.playable_nation_count = packet->playable_nation_count;

  tilespec_free_city_tiles(game.styles_count);
  free(city_styles);
  game.styles_count = packet->style_count;
  city_styles = fc_calloc( game.styles_count, sizeof(struct citystyle) );
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
  u->move_type          = p->move_type;
  u->build_cost         = p->build_cost;
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

  free(u->helptext);
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
  
  free(a->helptext);
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
  free(b->terr_gate);
  b->terr_gate = p->terr_gate;		/* pointer assignment */
  free(b->spec_gate);
  b->spec_gate = p->spec_gate;		/* pointer assignment */
  b->equiv_range = p->equiv_range;
  free(b->equiv_dupl);
  b->equiv_dupl = p->equiv_dupl;	/* pointer assignment */
  free(b->equiv_repl);
  b->equiv_repl = p->equiv_repl;	/* pointer assignment */
  b->obsolete_by = p->obsolete_by;
  b->is_wonder = p->is_wonder;
  b->build_cost = p->build_cost;
  b->upkeep = p->upkeep;
  b->sabotage = p->sabotage;
  free(b->effect);
  b->effect = p->effect;		/* pointer assignment */
  b->variant = p->variant;	/* FIXME: remove when gen-impr obsoletes */
  free(b->helptext);
  b->helptext = p->helptext;		/* pointer assignment */

#ifdef DEBUG
  if(p->id == game.num_impr_types-1) {
    int id;
    for (id = 0; id < game.num_impr_types; id++) {
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
    }
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
  
  free(t->helptext);
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
    int i;
    for(i=0; i<game.num_impr_types; i++)
      pcity->improvements[i] = (packet->improvements[i]=='1') ? 1 : 0;

    popup_sabotage_dialog(pcity);
  }
}
