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
#include <stdlib.h>
#include <time.h>

#include <gtk/gtk.h>

#include <civclient.h>
#include <clinet.h>
#include <gmain.h>
#include <packets.h>
#include <game.h>
#include <help.h>
#include <log.h>
#include <main.h>
#include <connect.h>
#include <events.h>
#include <packhand.h>

char	server_host	[512];
char	name		[512];
int	server_port;

enum client_states	client_state			= CLIENT_BOOT_STATE;
int			use_solid_color_behind_units;
int			sound_bell_at_new_turn;
int			smooth_move_units		= 1;
int			flags_are_transparent		= 0;

int			seconds_to_turndone;

int			last_turn_gold_amount;
int			turn_gold_difference;
int			did_advance_tech_this_turn;


int message_values[E_LAST];
void handle_move_unit(struct packet_move_unit *packet);
void handle_new_year(struct packet_new_year *ppacket);
void handle_city_info(struct packet_city_info *packet);
void handle_unit_combat(struct packet_unit_combat *packet);
void handle_game_state(struct packet_generic_integer *packet);
void handle_nuke_tile(struct packet_nuke_tile *packet);
void handle_page_msg(struct packet_generic_message *packet);
void handle_before_new_year();
void handle_remove_player(struct packet_generic_integer *packet);





/**************************************************************************
...
**************************************************************************/

int main( int argc, char **argv )
{
    game_init();
    boot_help_texts();
    g_main( argc, argv );
    return 0;
}

/**************************************************************************
...
**************************************************************************/
void handle_packet_input(char *packet, int type)
{
  switch(type) {
  case PACKET_REPLY_JOIN_GAME_ACCEPT:
    log_string(LOG_DEBUG, "join game accept:%s", ((struct packet_generic_message *)
	packet)->message);
    break;

  case PACKET_REPLY_JOIN_GAME_REJECT:
    append_output_window("You were rejected from the game:");
    append_output_window(((struct packet_generic_message *)packet)->message);
    break;

  case PACKET_SERVER_SHUTDOWN:
    log_string(LOG_DEBUG, "server shutdown");
    break;
  case PACKET_BEFORE_NEW_YEAR:
    handle_before_new_year();
    break;
  case PACKET_NEW_YEAR:
    handle_new_year((struct packet_new_year *)packet);
    break;

  case PACKET_UNIT_INFO:
    handle_unit_info((struct packet_unit_info *)packet);
    break;
/*100%*/
   case PACKET_MOVE_UNIT:
    handle_move_unit((struct packet_move_unit *)packet);
    break;
    
  case PACKET_TILE_INFO:
    handle_tile_info((struct packet_tile_info *)packet);
    break;

/*100%*/
  case PACKET_SELECT_RACE:
    handle_select_race((struct packet_generic_integer *)packet);
    break;

  case PACKET_PLAYER_INFO:
    handle_player_info((struct packet_player_info *)packet);
    break;
    
  case PACKET_GAME_INFO:
    handle_game_info((struct packet_game_info *)packet);
    break;

  case PACKET_MAP_INFO:
    handle_map_info((struct packet_map_info *)packet);
    break;
    
  case PACKET_CHAT_MSG:
  handle_chat_msg((struct packet_generic_message *)packet);
  break;
/*100%*/
  case PACKET_PAGE_MSG:
  handle_page_msg((struct packet_generic_message *)packet);
  break;
    
  case PACKET_CITY_INFO:
  handle_city_info((struct packet_city_info *)packet);
  break;

  case PACKET_REMOVE_UNIT:
  handle_remove_unit((struct packet_generic_integer *)packet);
  break;

  case PACKET_REMOVE_CITY:
  handle_remove_city((struct packet_generic_integer *)packet);
  break;
    
  case PACKET_UNIT_COMBAT:
  handle_unit_combat((struct packet_unit_combat *)packet);
  break;

  case PACKET_GAME_STATE:
  handle_game_state(((struct packet_generic_integer *)packet));
  break;

  case PACKET_NUKE_TILE:
  handle_nuke_tile(((struct packet_nuke_tile *)packet));
  break;

  case PACKET_DIPLOMACY_INIT_MEETING:
//    handle_diplomacy_init_meeting((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_CANCEL_MEETING:
//    handle_diplomacy_cancel_meeting((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
//    handle_diplomacy_create_clause((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
//    handle_diplomacy_remove_clause((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
//    handle_diplomacy_accept_treaty((struct packet_diplomacy_info *)packet);  
    break;


  case PACKET_REMOVE_PLAYER:
    handle_remove_player((struct packet_generic_integer *)packet);
    break;
    
  default:
    log_string(LOG_FATAL, "Received unknown packet from server!");
    gtk_main_quit();
    break;
  }

  free(packet);
}



/**************************************************************************
...
**************************************************************************/
void send_unit_info(struct unit *punit)
{
  struct packet_unit_info info;

  info.id=punit->id;
  info.owner=punit->owner;
  info.x=punit->x;
  info.y=punit->y;
  info.homecity=punit->homecity;
  info.veteran=punit->veteran;
  info.type=punit->type;
  info.movesleft=punit->moves_left;
  info.activity=punit->activity;

  send_packet_unit_info(&aconnection, &info);
}

/**************************************************************************
...
**************************************************************************/
void send_report_request(GtkWidget *widget, enum report_type type)
{
 struct packet_generic_integer pa;

  pa.value=type;
  send_packet_generic_integer(&aconnection, PACKET_REPORT_REQUEST, &pa);
}


/**************************************************************************
...
**************************************************************************/

void user_ended_turn( void )
{
  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';

  send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
}

/**************************************************************************
...
**************************************************************************/
void set_client_state( enum client_states newstate )
{
  if(client_state!=newstate) {
    client_state=newstate;

    if(client_state==CLIENT_PRE_GAME_STATE) {
      game_remove_all_players();
    }
    
/*    update_menus();*/
  }
  if(client_state==CLIENT_PRE_GAME_STATE)
    gui_server_connect();
}


/**************************************************************************
...
**************************************************************************/
enum client_states get_client_state(void)
{
  return client_state;
}
