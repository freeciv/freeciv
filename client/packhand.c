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
#include <assert.h>

#include "capability.h"
#include "capstr.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "packets.h"
#include "spaceship.h"
#include "support.h"
#include "unit.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"		/* aconnection */
#include "control.h"
#include "dialogs_g.h"
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

#include "packhand.h"

extern int seconds_to_turndone;
extern int last_turn_gold_amount;
extern int did_advance_tech_this_turn;
extern char name[512];

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
  } else {
    my_snprintf(msg, sizeof(msg),
		_("You were rejected from the game: %s"), packet->message);
    append_output_window(msg);
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
  struct city *pcity=find_city_by_id(packet->value);
  
  if(pcity!=NULL) client_remove_city(pcity);
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
this piece of code below is about as bad as it can get
**************************************************************************/
void handle_unit_combat(struct packet_unit_combat *packet)
{
  struct unit *punit0=game_find_unit_by_id(packet->attacker_unit_id);
  struct unit *punit1=game_find_unit_by_id(packet->defender_unit_id);

  if(punit0 && tile_visible_mapcanvas(punit0->x, punit0->y) &&
     punit1 && tile_visible_mapcanvas(punit1->x, punit1->y)) {
    struct unit *pfocus;
    pfocus=get_unit_in_focus();
  
    decrease_unit_hp_smooth(punit0, packet->attacker_hp,
			    punit1, packet->defender_hp);
    set_unit_focus(pfocus);
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

  pcity=city_list_find_id(&game.players[packet->owner].cities, packet->id);

  if(!pcity) {
    city_is_new=1;
    pcity=fc_malloc(sizeof(struct city));
  }
  else
    city_is_new=0;
  
  pcity->id=packet->id;
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
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;
  
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
    
  for(i=0; i<B_LAST; i++)
    pcity->improvements[i]=(packet->improvements[i]=='1') ? 1 : 0;
  
  if(city_is_new) {
    unit_list_init(&pcity->units_supported);
    city_list_insert(&game.players[packet->owner].cities, pcity);
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
     city_is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE) {
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

  if(((city_is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE && 
      pcity->owner==game.player_idx) || packet->diplomat_investigate) &&
     (!game.player_ptr->ai.control || ai_popup_windows)) {
    update_menus();
    popup_city_dialog(pcity, 0);
  }

  if(!city_is_new && pcity->owner==game.player_idx) {
    struct unit *punit = get_unit_in_focus();
    if (punit && (punit->x == pcity->x) && (punit->y == pcity->y)) {
      update_menus();
    }
    refresh_city_dialog(pcity);
  }

  if(city_is_new) {
    freelog(LOG_DEBUG, "New %s city %s id %d (%d %d)",
	 get_nation_name(city_owner(pcity)->nation),
	 pcity->name, pcity->id, pcity->x, pcity->y);
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
    
  update_unit_info_label(get_unit_in_focus());

  seconds_to_turndone=game.timeout;

  turn_gold_difference=game.player_ptr->economic.gold-last_turn_gold_amount;
  last_turn_gold_amount=game.player_ptr->economic.gold;

  report_update_delay_off();
  update_report_dialogs();

  meswin_update_delay_off();
  update_meswin_dialog();

  if(game.player_ptr->ai.control && !ai_manual_turn_done) user_ended_turn();
}

void handle_before_new_year(void)
{
  clear_notify_window();
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
    freelog(LOG_NORMAL,"Unknown event type %d!", packet->event);
  } else if (packet->event >= 0)  {
    where = messages_where[packet->event];
  }
  if (where & MW_OUTPUT)
    append_output_window(packet->message);
  if (where & MW_MESSAGES)
    add_notify_window(packet);
  if (where & MW_POPUP)
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
  
  repaint_unit=0;
  repaint_city=0;
  punit=unit_list_find(&game.players[packet->owner].units, packet->id);
  
  if(punit) {
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
      update_unit_pix_label(punit);
      update_unit_focus(); */

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
      
      if(tile_is_known(packet->x, packet->y)) {
	do_move_unit(punit, packet);
	update_unit_focus();
      }
      else {
	unit_list_unlink(&game.players[packet->owner].units, punit);
	unit_list_unlink(&map_get_tile(punit->x, punit->y)->units, punit);
	if(punit->homecity && (pcity=find_city_by_id(punit->homecity))) {
	  unit_list_unlink(&pcity->units_supported, punit);
	}
	refresh_tile_mapcanvas(punit->x, punit->y, 1);
	free(punit);
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
  }
  
  else {      /* create new unit */
    punit=fc_malloc(sizeof(struct unit));
    
    punit->id=packet->id;
    punit->owner=packet->owner;
    punit->x=packet->x;
    punit->y=packet->y;
    punit->veteran=packet->veteran;
    punit->homecity=packet->homecity;
    punit->type=packet->type;
    punit->moves_left=packet->movesleft;
    punit->hp=packet->hp;
    punit->unhappiness=packet->unhappiness;
    punit->activity=packet->activity;
    punit->upkeep=packet->upkeep;
    punit->upkeep_food=packet->upkeep_food;
    punit->upkeep_gold=packet->upkeep_gold;
    punit->fuel=packet->fuel;
    punit->goto_dest_x=packet->goto_dest_x;
    punit->goto_dest_y=packet->goto_dest_y;
    punit->activity_target=packet->activity_target;
    punit->ai.control=packet->ai;
    punit->paradropped=packet->paradropped;
    punit->connecting=packet->connecting;
    
    punit->activity_count=0;	/* never used in client/ or common/  --dwp */
    punit->bribe_cost=0;	/* done by handle_incite_cost() */
    
    punit->focus_status=FOCUS_AVAIL;
    
    unit_list_insert(&game.players[packet->owner].units, punit);
    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);
    
    if((pcity=find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);

    freelog(LOG_DEBUG, "New %s %s id %d (%d %d) hc %d %s", 
	   get_nation_name(get_player(punit->owner)->nation),
	   unit_name(punit->type), punit->x, punit->y, punit->id,
	   punit->homecity, (pcity ? pcity->name : _("(unknown)")));
    
    /* this is ugly - prevent unit from being drawn if it's moved into
     * screen by a transporter - only works for ground_units.. yak */
    if(!is_ground_unit(punit) || map_get_terrain(punit->x, punit->y)!=T_OCEAN)
      repaint_unit=1;
    else
      repaint_unit=0;
  }

  if(punit && punit==get_unit_in_focus())
    update_unit_info_label(punit);
  else if(get_unit_in_focus() && get_unit_in_focus()->x==punit->x &&
	  get_unit_in_focus()->y==punit->y) {
    update_unit_info_label(get_unit_in_focus());
  }

  if(repaint_unit)
    refresh_tile_mapcanvas(punit->x, punit->y, 1);

  update_unit_focus(); 
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
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    game.player_idx=pinfo->player_idx;
    game.player_ptr=&game.players[game.player_idx];
  }
  for(i=0; i<A_LAST/*game.num_tech_types*/; i++)
    game.global_advances[i]=pinfo->global_advances[i];
  for(i=0; i<B_LAST; i++)
    game.global_wonders[i]=pinfo->global_wonders[i];
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) {
    if(get_client_state()==CLIENT_SELECT_RACE_STATE)
      popdown_races_dialog();
  }
  game.techpenalty=pinfo->techpenalty;
  game.foodbox=pinfo->foodbox;
  game.civstyle=pinfo->civstyle;

  boot_help = (get_client_state() == CLIENT_GAME_RUNNING_STATE
	       && game.spacerace != pinfo->spacerace);
  game.spacerace=pinfo->spacerace;
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
  pplayer->city_style=pinfo->city_style;

  for(i=0; i<game.num_tech_types; i++)
    pplayer->research.inventions[i]=pinfo->inventions[i]-'0';
  update_research(pplayer);

  poptechup = (pplayer->research.researching!=pinfo->researching);
  pplayer->research.researched=pinfo->researched;
  pplayer->research.researchpoints=pinfo->researchpoints;
  pplayer->research.researching=pinfo->researching;
  pplayer->future_tech=pinfo->future_tech;
  pplayer->ai.tech_goal=pinfo->tech_goal;

  if(!pplayer->conn){
    /* It is only the client that does this */

    pplayer->conn = fc_malloc(sizeof(struct connection));
    pplayer->conn->sock = 0;
    pplayer->conn->used = 0;
    pplayer->conn->player = NULL;
  }

  sz_strlcpy(pplayer->conn->capability, pinfo->capability);
  
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
  sz_strlcpy(pplayer->addr, pinfo->addr);

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

  if(pplayer==game.player_ptr) {
    if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
      if (!game.player_ptr->turn_done) {
	enable_turn_done_button();
      }
      update_info_label();
    }
  }
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
...
**************************************************************************/
void handle_tile_info(struct packet_tile_info *packet)
{
  int old_known;

  struct tile *ptile=map_get_tile(packet->x, packet->y);

  old_known=ptile->known;

  ptile->terrain=packet->type;
  ptile->special=packet->special;

  ptile->known=packet->known;

  if(packet->known >= TILE_KNOWN && old_known < TILE_KNOWN
     && ptile->terrain != T_OCEAN)
    climap_update_continents(packet->x, packet->y);
  
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE && packet->known>=TILE_KNOWN) {
    refresh_tile_mapcanvas(packet->x, packet->y, 1); 
    if(old_known<TILE_KNOWN) { 
      int known;
      
      known=tile_is_known(packet->x-1, packet->y);
      if(known>=TILE_KNOWN && known!=ptile->known)
	refresh_tile_mapcanvas(packet->x-1, packet->y, 1); 

      known=tile_is_known(packet->x+1, packet->y);
      if(known>=TILE_KNOWN && known!=ptile->known)
	refresh_tile_mapcanvas(packet->x+1, packet->y, 1); 

      known=tile_is_known(packet->x, packet->y-1);
      if(known>=TILE_KNOWN && known!=ptile->known)
	refresh_tile_mapcanvas(packet->x, packet->y-1, 1); 

      known=tile_is_known(packet->x, packet->y+1);
      if(known>=TILE_KNOWN && known!=ptile->known)
	refresh_tile_mapcanvas(packet->x, packet->y+1, 1); 
    }
    else { /* happens so seldom(new roads etc) so refresh'em all */
      refresh_tile_mapcanvas(packet->x-1, packet->y, 1); 
      refresh_tile_mapcanvas(packet->x+1, packet->y, 1); 
      refresh_tile_mapcanvas(packet->x, packet->y-1, 1); 
      refresh_tile_mapcanvas(packet->x, packet->y+1, 1); 
      /* jjm@codewell.com 30dec1998a */
      refresh_tile_mapcanvas(packet->x-1, packet->y+1, 1); 
      refresh_tile_mapcanvas(packet->x+1, packet->y+1, 1); 
      refresh_tile_mapcanvas(packet->x+1, packet->y-1, 1); 
      refresh_tile_mapcanvas(packet->x-1, packet->y-1, 1); 
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
    freelog(LOG_VERBOSE, "got a select nation packet in an incompatible state");
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
  game.num_tech_types = packet->num_tech_types;

  governments = fc_calloc(game.government_count, sizeof(struct government));
  for(i=0; i<game.government_count; i++) {
    get_government(i)->ruler_titles = NULL;
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
    freelog(LOG_NORMAL, "Received bad unit_type id %d in handle_ruleset_unit()",
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
    freelog(LOG_NORMAL, "Received bad advance id %d in handle_ruleset_tech()",
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
  struct improvement_type *b;

  if(p->id < 0 || p->id >= B_LAST) {
    freelog(LOG_NORMAL,
	    "Received bad building id %d in handle_ruleset_building()",
	    p->id);
    return;
  }
  b = &improvement_types[p->id];

  sz_strlcpy(b->name, p->name);
  b->is_wonder        = p->is_wonder;
  b->tech_requirement = p->tech_requirement;
  b->build_cost       = p->build_cost;      
  b->shield_upkeep    = p->shield_upkeep;   
  b->obsolete_by      = p->obsolete_by;     
  b->variant          = p->variant;         
 
  free(b->helptext);
  b->helptext = p->helptext;	/* pointer assignment */
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_government(struct packet_ruleset_government *p)
{
  struct government *gov;

  if (p->id < 0 || p->id >= game.government_count) {
    freelog(LOG_NORMAL,
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
  
  free(gov->helptext);
  gov->helptext = p->helptext;	/* pointer assignment */
  
  tilespec_setup_government(p->id);
}
void handle_ruleset_government_ruler_title
  (struct packet_ruleset_government_ruler_title *p)
{
  struct government *gov;

  if(p->gov < 0 || p->gov >= game.government_count) {
    freelog(LOG_NORMAL, "Received bad government num %d for title", p->gov);
    return;
  }
  gov = &governments[p->gov];
  if(p->id < 0 || p->id >= gov->num_ruler_titles) {
    freelog(LOG_NORMAL, "Received bad ruler title num %d for %s title",
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
    freelog(LOG_NORMAL,
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
  terrain_control.river_move_mode = p->river_move_mode;
  terrain_control.river_defense_bonus = p->river_defense_bonus;
  terrain_control.river_trade_incr = p->river_trade_incr;
  terrain_control.river_help_text =
    p->river_help_text ? mystrdup(p->river_help_text) : NULL;
  terrain_control.fortress_defense_bonus = p->fortress_defense_bonus;
  terrain_control.road_superhighway_trade_bonus = p->road_superhighway_trade_bonus;
  terrain_control.rail_food_bonus = p->rail_food_bonus;
  terrain_control.rail_shield_bonus = p->rail_shield_bonus;
  terrain_control.rail_trade_bonus = p->rail_trade_bonus;
  terrain_control.farmland_supermarket_food_bonus = p->farmland_supermarket_food_bonus;
  terrain_control.pollution_food_penalty = p->pollution_food_penalty;
  terrain_control.pollution_shield_penalty = p->pollution_shield_penalty;
  terrain_control.pollution_trade_penalty = p->pollution_trade_penalty;
}

/**************************************************************************
...
**************************************************************************/
void handle_ruleset_nation(struct packet_ruleset_nation *p)
{
  int i;
  struct nation_type *pl;

  if(p->id < 0 || p->id >= game.nation_count || p->id >= MAX_NUM_NATIONS) {
    freelog(LOG_NORMAL, "Received bad nation id %d in handle_ruleset_nation()",
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
    freelog(LOG_NORMAL, "Received bad citystyle id %d in handle_ruleset_city()",
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
void handle_incite_cost(struct packet_generic_values *packet)
{
  struct city *pcity=find_city_by_id(packet->id);
  struct unit *punit=find_unit_by_id(packet->id);

  if(pcity)  {
    pcity->incite_revolt_cost = packet->value1;
    popup_incite_dialog(pcity);
    return;
  }

  if(punit) {
    punit->bribe_cost = packet->value1;
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
  
  punit = unit_list_find(&game.player_ptr->units, packet->id);
  if (punit) {
    popup_newcity_dialog(punit, packet->name);
    return;
  }
  /* maybe unit died; ignore */
}
