#include <stdio.h>
#include <stdlib.h>

#include <packets.h>
#include <climisc.h>
#include <game.h>
#include <mapview.h>
#include <mapctrl.h>
#include <map.h>
#include <dialogs.h>
#include <citydlg.h>
#include <repodlgs.h>
#include <xmain.h>
#include <log.h>
#include <meswindlg.h>
#include <chatline.h>
#include <plrdlg.h>
#include <civclient.h>
#include <graphics.h>

extern int seconds_to_turndone;
extern int turn_gold_difference;
extern int last_turn_gold_amount;
extern int did_advance_tech_this_turn;
extern int ai_popup_windows;
extern int ai_manual_turn_done;
extern char name[512];
extern struct Sprite *intro_gfx_sprite;
extern struct Sprite *radar_gfx_sprite;


/**************************************************************************
...
**************************************************************************/
void handle_join_game_reply(struct packet_join_game_reply *packet)
{
  char msg[MSG_SIZE];

  strcpy(s_capability, packet->capability);

  if (packet->you_can_join) {
    log(LOG_DEBUG, "join game accept:%s", packet->message);
  } else {
    sprintf(msg, "You were rejected from the game: %s", packet->message);
    append_output_window(msg);
  }
  if (strcmp(s_capability, c_capability)==0)
    return;
  sprintf(msg, "Client capability string: %s", c_capability);
  append_output_window(msg);
  sprintf(msg, "Server capability string: %s", s_capability);
  append_output_window(msg);
}

/**************************************************************************
...
**************************************************************************/
void handle_remove_city(struct packet_generic_integer *packet)
{
  client_remove_city(packet->value);
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
     game.player_ptr->race == R_LAST) {
    popdown_races_dialog();
  }
  
  set_client_state(packet->value);

  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
    refresh_overview_canvas();
    refresh_overview_viewrect();
    enable_turn_done_button();
    player_set_unit_focus_status(game.player_ptr);
    
    update_unit_focus();
    update_unit_info_label(get_unit_in_focus());

    if(get_unit_in_focus())
      center_tile_mapcanvas(get_unit_in_focus()->x, get_unit_in_focus()->y);
    if(intro_gfx_sprite) { free_sprite(intro_gfx_sprite); intro_gfx_sprite=NULL; };
    if(radar_gfx_sprite) { free_sprite(radar_gfx_sprite); radar_gfx_sprite=NULL; };
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
    pcity=(struct city *)malloc(sizeof(struct city));
  }
  else
    city_is_new=0;
  
  pcity->id=packet->id;
  pcity->owner=packet->owner;
  pcity->x=packet->x;
  pcity->y=packet->y;
  strcpy(pcity->name, packet->name);
  
  pcity->size=packet->size;
  pcity->ppl_happy[4]=packet->ppl_happy;
  pcity->ppl_content[4]=packet->ppl_content;
  pcity->ppl_unhappy[4]=packet->ppl_unhappy;
  pcity->ppl_elvis=packet->ppl_elvis;
  pcity->ppl_scientist=packet->ppl_scientist;
  pcity->ppl_taxman=packet->ppl_taxman;
  for (i=0;i<4;i++)
    pcity->trade[i]=packet->trade[i];
  
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
  pcity->incite_revolt_cost=packet->incite_revolt_cost;
    
  pcity->is_building_unit=packet->is_building_unit;
  pcity->currently_building=packet->currently_building;
  pcity->did_buy=packet->did_buy;
  pcity->did_sell=packet->did_sell;
  pcity->was_happy=packet->was_happy;
  pcity->airlift=packet->airlift;
  i=0;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++)
      pcity->city_map[x][y]=packet->city_map[i++]-'0';
    
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
    
  refresh_tile_mapcanvas(pcity->x, pcity->y, 1);
  
  if(city_is_new && get_client_state()==CLIENT_GAME_RUNNING_STATE && 
     pcity->owner==game.player_idx &&
     (!game.player_ptr->ai.control || ai_popup_windows))
    popup_city_dialog(pcity, 0);

  if(!city_is_new)
    refresh_city_dialog(pcity);
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
  update_report_dialogs();

  if(game.player_ptr->ai.control && !ai_manual_turn_done) user_ended_turn();
}

void handle_before_new_year()
{
  clear_notify_window();
}

/**************************************************************************
...
**************************************************************************/
void handle_chat_msg(struct packet_generic_message *packet)
{
  append_output_window(packet->message);
  if (packet->event > -1) add_notify_window(packet);
}
 
/**************************************************************************
...
**************************************************************************/
void handle_page_msg(struct packet_generic_message *packet)
{
  int i;
  char title[512];
  
  for(i=0; packet->message[i]!='\n'; i++)
    title[i]=packet->message[i];
  title[i]='\0';
  
  if (!game.player_ptr->ai.control || ai_popup_windows || 
       packet->event != BROADCAST_EVENT)
    popup_notify_dialog(title, packet->message+i+1);
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
  int repaint_city;
  
  repaint_unit=0;
  repaint_city=0;
  punit=unit_list_find(&game.players[packet->owner].units, packet->id);
  
  if(punit) {
    if(punit->activity!=packet->activity) { /* change activity */
      punit->activity=packet->activity;
  
      repaint_unit=1;
      
      /*      refresh_tile_mapcanvas(punit->x, punit->y, 1);
      update_unit_pix_label(punit);
      refresh_unit_city_dialogs(punit);
      update_unit_focus(); */
    }
    
    if(punit->homecity!=packet->homecity) { /* change homecity */
      struct city *pcity;
      if((pcity=game_find_city_by_id(punit->homecity))) {
	unit_list_unlink(&pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
      
      punit->homecity=packet->homecity;
      if((pcity=game_find_city_by_id(punit->homecity))) {
	unit_list_insert(&pcity->units_supported, punit);
	refresh_city_dialog(pcity);
      }
    }

    if(punit->hp!=packet->hp) {                      /* hp changed */
      punit->hp=packet->hp;
      repaint_unit=1;
    }
    if (punit->type!=packet->type) {
      punit->type=packet->type;
      repaint_unit=1;
      repaint_city=1;
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
	refresh_tile_mapcanvas(punit->x, punit->y, 1);
	free(punit);
        return;
      }
      if(pcity)
	refresh_city_dialog(pcity);
      
      if((pcity=map_get_city(punit->x, punit->y)))
	refresh_city_dialog(pcity);
      
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
    if (repaint_city) {
      if((pcity=game_find_city_by_id(punit->homecity))) {
	refresh_city_dialog(pcity);
      }
    }

    punit->veteran=packet->veteran;
    punit->moves_left=packet->movesleft;
    punit->bribe_cost=packet->bribe_cost;
    punit->ai.control=packet->ai;
    punit->fuel=packet->fuel;
    punit->goto_dest_x=packet->goto_dest_x;
    punit->goto_dest_y=packet->goto_dest_y;
  }
  
  else {      /* create new unit */
    punit=(struct unit *)malloc(sizeof(struct unit));
    
    punit->id=packet->id;
    punit->owner=packet->owner;
    punit->x=packet->x;
    punit->y=packet->y;
    punit->veteran=packet->veteran;
    punit->homecity=packet->homecity;
    punit->type=packet->type;
    punit->moves_left=packet->movesleft;
    punit->hp=packet->hp;
    punit->unhappiness=0;
    punit->activity=packet->activity;
    punit->activity_count=0;
    punit->upkeep=0;
    punit->hp=packet->hp;
    punit->bribe_cost=packet->bribe_cost;
    punit->fuel=0;
    punit->goto_dest_x=packet->goto_dest_x;
    punit->goto_dest_y=packet->goto_dest_y;
    punit->focus_status=FOCUS_AVAIL;
    punit->ai.control=0;
    
    unit_list_insert(&game.players[packet->owner].units, punit);
    unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);
    
    if((pcity=game_find_city_by_id(punit->homecity)))
      unit_list_insert(&pcity->units_supported, punit);
    
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
  int x, y;

  map.xsize=pinfo->xsize;
  map.ysize=pinfo->ysize;
  map.is_earth=pinfo->is_earth;

  if(!(map.tiles=(struct tile*)malloc(map.xsize*map.ysize*
				      sizeof(struct tile)))) {
    log(LOG_FATAL, "malloc failed in handle_map_info");
    exit(1);
  }
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      tile_init(map_get_tile(x, y));

  set_overview_dimensions(map.xsize, map.ysize);
}

/**************************************************************************
...
**************************************************************************/
void handle_game_info(struct packet_game_info *pinfo)
{
  int i;
  static int boottime=1;
  game.gold=pinfo->gold;
  game.tech=pinfo->tech;
  game.techlevel=pinfo->techlevel;
  game.skill_level=pinfo->skill_level;
  game.timeout=pinfo->timeout;
  
  game.rail_food = pinfo->rail_food;
  game.rail_trade = pinfo->rail_trade;
  game.rail_prod = pinfo->rail_prod;

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
  for(i=0; i<A_LAST; i++)
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
  if (boottime) {
    boottime=0;
    set_civ_style(game.civstyle);
    update_research(game.player_ptr);
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
  char msg[MSG_SIZE];
  struct player *pplayer=&game.players[pinfo->playerno];
  
  strcpy(pplayer->name, pinfo->name);
  pplayer->race=pinfo->race;
  
  pplayer->economic.gold=pinfo->gold;
  pplayer->economic.tax=pinfo->tax;
  pplayer->economic.science=pinfo->science;
  pplayer->economic.luxury=pinfo->luxury;
  pplayer->government=pinfo->government;
  pplayer->embassy=pinfo->embassy;

  for(i=0; i<A_LAST; i++)
    pplayer->research.inventions[i]=pinfo->inventions[i]-'0';
  update_research(pplayer);

  poptechup = (pplayer->research.researching!=pinfo->researching);
  pplayer->research.researched=pinfo->researched;
  pplayer->research.researchpoints=pinfo->researchpoints;
  pplayer->research.researching=pinfo->researching;
  pplayer->future_tech=pinfo->future_tech;
  pplayer->ai.tech_goal=pinfo->tech_goal;

  if(get_client_state()==CLIENT_GAME_RUNNING_STATE && pplayer==game.player_ptr) {
    strcpy(name, pplayer->name);
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
  strcpy(pplayer->addr, pinfo->addr);

  pplayer->revolution=pinfo->revolution;
  if(pplayer->ai.control!=pinfo->ai)  {
    pplayer->ai.control=pinfo->ai;
    if(pplayer==game.player_ptr)  {
      sprintf(msg,"AI Mode is now %s.",game.player_ptr->ai.control?"ON":"OFF");
      append_output_window(msg);
    }
  }
  
  if(pplayer==game.player_ptr && pplayer->revolution==0 && 
     pplayer->government==G_ANARCHY &&
     (!game.player_ptr->ai.control || ai_popup_windows))
    popup_government_dialog();
  
  update_players_dialog();

  if(pplayer==game.player_ptr) {
    if(get_client_state()==CLIENT_GAME_RUNNING_STATE &&
       !game.player_ptr->turn_done)
      enable_turn_done_button();
    update_info_label();
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
void handle_select_race(struct packet_generic_integer *packet)
{
  if(get_client_state()==CLIENT_SELECT_RACE_STATE) {
    if(packet->value==-1) {
      set_client_state(CLIENT_WAITING_FOR_GAME_START_STATE);
      popdown_races_dialog();
    }
    else
      races_toggles_set_sensitive(packet->value);
  }
  else if(get_client_state()==CLIENT_PRE_GAME_STATE) {
    set_client_state(CLIENT_SELECT_RACE_STATE);
    popup_races_dialog();
    races_toggles_set_sensitive(packet->value);
  }
  else
    log(LOG_DEBUG, "got a select race packet in an incompatible state");
}
