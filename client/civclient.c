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

#include <unistd.h>
#include <sys/types.h>

#include "capstr.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "version.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "climisc.h"
#include "clinet_g.h"
#include "connectdlg_g.h"
#include "dialogs_g.h"
#include "diplodlg_g.h"
#include "gui_main_g.h"
#include "helpdlg_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "packhand.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"

#include "civclient.h"

char server_host[512];
char name[512];
int server_port;
int is_server = 0;

enum client_states client_state=CLIENT_BOOT_STATE;

int seconds_to_turndone;

int last_turn_gold_amount;
int turn_gold_difference;

int did_advance_tech_this_turn;

/**************************************************************************
...
**************************************************************************/
char usage[] = 
"Usage: %s [-bhlpsv] [--bgcol] [--cmap] [--help] [--log] [--name]\n\t[--port] [--server] [--debug] [--version] [--tiles]\n";

/**************************************************************************
...
**************************************************************************/

int main(int argc, char *argv[])
{
  if(argc>1 && strstr(argv[1],"-help")) {
    fprintf(stderr, "This is the Freeciv client\n");
    fprintf(stderr, usage, argv[0]);
    fprintf(stderr, "  -help\t\tPrint a summary of the options\n");
    fprintf(stderr, "  -log F\tUse F as logfile\n");
    fprintf(stderr, "  -name N\tUse N as name\n");
    fprintf(stderr, "  -port N\tconnect to port N\n");
    fprintf(stderr, "  -server S\tConnect to the server at S\n");
#ifdef DEBUG
    fprintf(stderr, "  -debug N\tSet debug log level (0,1,2,3,"
	                          "or 3:file1,min,max:...)\n");
#else
    fprintf(stderr, "  -debug N\tSet debug log level (0,1,2)\n");
#endif
    fprintf(stderr, "  -version\tPrint the version number\n");
    fprintf(stderr, "  -tiles D\tLook in data subdirectory D for the tiles\n");
    fprintf(stderr, "Report bugs to <%s>.\n", BUG_EMAIL_ADDRESS);
    exit(0);
  }
  
  if(argc>1 && strstr(argv[1],"-version")) {
    fprintf(stderr, "%s\n", FREECIV_NAME_VERSION);
    exit(0);
  }

  /*  audio_init(); */
  init_messages_where();
  init_our_capability();
  game_init();

  /* This seed is not saved anywhere; randoms in the client should
     have cosmetic effects only (eg city name suggestions).  --dwp */
  mysrand(time(NULL));

  ui_main(argc, argv);
  return 0;
  /*  audio_term(); */
}




/**************************************************************************
...
**************************************************************************/
void handle_packet_input(char *packet, int type)
{
  switch(type) {
  case PACKET_JOIN_GAME_REPLY:
    handle_join_game_reply((struct packet_join_game_reply *)packet);
    break;

  case PACKET_SERVER_SHUTDOWN:
    freelog(LOG_VERBOSE, "server shutdown");
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

   case PACKET_MOVE_UNIT:
    handle_move_unit((struct packet_move_unit *)packet);
    break;
    
  case PACKET_TILE_INFO:
    handle_tile_info((struct packet_tile_info *)packet);
    break;

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
    handle_diplomacy_init_meeting((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_CANCEL_MEETING:
    handle_diplomacy_cancel_meeting((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    handle_diplomacy_create_clause((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    handle_diplomacy_remove_clause((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    handle_diplomacy_accept_treaty((struct packet_diplomacy_info *)packet);  
    break;

  case PACKET_REMOVE_PLAYER:
    handle_remove_player((struct packet_generic_integer *)packet);
    break;

  case PACKET_RULESET_UNIT:
    handle_ruleset_unit((struct packet_ruleset_unit *)packet);
    break;
    
  case PACKET_RULESET_TECH:
    handle_ruleset_tech((struct packet_ruleset_tech *)packet);
    break;
    
  case PACKET_RULESET_BUILDING:
    handle_ruleset_building((struct packet_ruleset_building *)packet);
    break;

  case PACKET_RULESET_TERRAIN:
    handle_ruleset_terrain((struct packet_ruleset_terrain *)packet);
    break;

  case PACKET_RULESET_TERRAIN_CONTROL:
    handle_ruleset_terrain_control((struct terrain_misc *)packet);
    break;

  case PACKET_INCITE_COST:
    handle_incite_cost((struct packet_generic_values *)packet);
    break;

  case PACKET_CITY_OPTIONS:
    handle_city_options((struct packet_generic_values *)packet);
    break;
    
  case PACKET_SPACESHIP_INFO:
    handle_spaceship_info((struct packet_spaceship_info *)packet);
    break;
    
  default:
    freelog(LOG_FATAL, "Received unknown packet from server!");
    exit(1);
    break;
  }

  free(packet);
}


/**************************************************************************
...
**************************************************************************/

void user_ended_turn(void)
{
  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';

  send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
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
void send_move_unit(struct unit *punit)
{
  struct packet_move_unit move;

  move.unid=punit->id;
  move.x=punit->x;
  move.y=punit->y;

  send_packet_move_unit(&aconnection, &move);
}

/**************************************************************************
...
**************************************************************************/
void send_report_request(enum report_type type)
{
 struct packet_generic_integer pa;
  
  pa.value=type;
  send_packet_generic_integer(&aconnection, PACKET_REPORT_REQUEST, &pa);
}

/**************************************************************************
...
**************************************************************************/
void set_client_state(enum client_states newstate)
{
  if(client_state!=newstate) {
    client_state=newstate;

    if(client_state==CLIENT_GAME_RUNNING_STATE) {
      update_research(game.player_ptr);
      role_unit_precalcs();
      boot_help_texts();	/* reboot */
      update_unit_focus();
    }
    else if(client_state==CLIENT_PRE_GAME_STATE) {
      popdown_all_city_dialogs();
      game_remove_all_players();
      set_unit_focus_no_center(0); /* thanks, David -- Syela */
      clear_notify_window();
/*      XtUnmapWidget(toplevel);
      XtMapWidget(toplevel);*/
    }
    
    update_menus();
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

void dealloc_id(int id); /* double kludge (suppress a possible warning) */
void dealloc_id(int id) { }/* kludge */
