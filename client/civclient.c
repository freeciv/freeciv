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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "capstr.h"
#include "fcintl.h"
#include "game.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "support.h"
#include "version.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "climisc.h"
#include "clinet.h"
#include "connectdlg_g.h"
#include "control.h" 
#include "dialogs_g.h"
#include "diplodlg_g.h"
#include "gui_main_g.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"
#include "messagewin_g.h"
#include "options.h"
#include "packhand.h"
#include "plrdlg_g.h"
#include "repodlgs_g.h"
#include "tilespec.h"

#include "civclient.h"

char metaserver[256]="";
char server_host[512];
char name[512];
int server_port;
int is_server = 0;

enum client_states client_state=CLIENT_BOOT_STATE;

static char *tile_set_name = NULL;

int seconds_to_turndone;

int last_turn_gold_amount;
int turn_gold_difference;

int did_advance_tech_this_turn;

/**************************************************************************
...
**************************************************************************/

int main(int argc, char *argv[])
{
  int i;
  int loglevel;
  char *logfile=NULL;
  char *option=NULL;

  init_nls();
  dont_run_as_root(argv[0], "freeciv_client");

  /* set default argument values */
  loglevel=LOG_NORMAL;
  server_port=DEFAULT_SOCK_PORT;
  mystrlcpy(server_host, "localhost", 512);
  mystrlcpy(metaserver, METALIST_ADDR, 256);
  name[0] = '\0';

  i = 1;

  while (i < argc) {
   if (is_option("--help", argv[i])) {
    fprintf(stderr, _("Usage: %s [option ...]\nValid options are:\n"), argv[0]);
    fprintf(stderr, _("  -h, --help\t\tPrint a summary of the options\n"));
    fprintf(stderr, _("  -l, --log FILE\tUse FILE as logfile\n"));
    fprintf(stderr, _("  -n, --name NAME\tUse NAME as name\n"));
    fprintf(stderr, _("  -p, --port PORT\tConnect to server port PORT\n"));
    fprintf(stderr, _("  -s, --server HOST\tConnect to the server at HOST\n"));
    fprintf(stderr, _("  -t, --tiles FILE\tUse data file FILE.tilespec for tiles\n"));
#ifdef SOUND
    fprintf(stderr, _("  -s, --sound FILE\tRead sound information from FILE\n"));
#endif
    fprintf(stderr, _("  -m, --meta HOST\tConnect to the metaserver at HOST\n"));
#ifdef DEBUG
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0 to 4,"
                                  " or 4:file1,min,max:...)\n"));
#else
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0 to 3)\n"));
#endif
    fprintf(stderr, _("  -v, --version\t\tPrint the version number\n"));
   } else if (is_option("--version",argv[i])) {
    fprintf(stderr, "%s\n", FREECIV_NAME_VERSION);
    exit(0);
   } else if ((option = get_option("--log",argv,&i,argc)) != NULL)
      logfile = mystrdup(option); /* never free()d */
   else if ((option = get_option("--name",argv,&i,argc)) != NULL)
      mystrlcpy(name, option, 512);
   else if ((option = get_option("--metaserver",argv,&i,argc)) != NULL)
      mystrlcpy(metaserver, option, 256);
   else if ((option = get_option("--port",argv,&i,argc)) != NULL)
     server_port=atoi(option);
   else if ((option = get_option("--server",argv,&i,argc)) != NULL)
      mystrlcpy(server_host, option, 512);
   else if ((option = get_option("--debug",argv,&i,argc)) != NULL) {
      loglevel=log_parse_level_str(option);
      if (loglevel==-1) {
        exit(1);
      }
   } else if ((option = get_option("--tiles",argv,&i,argc)) != NULL) {
      tile_set_name=option;
   } else { 
      fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[i]);
      exit(1);
   }
   i++;
  } /* of while */

  log_init(logfile, loglevel, NULL);

  /* after log_init: */
  if (name[0] == '\0') {
    mystrlcpy(name, user_username(), 512);
  }

  init_messages_where();
  init_our_capability();
  game_init();

  /* This seed is not saved anywhere; randoms in the client should
     have cosmetic effects only (eg city name suggestions).  --dwp */
  mysrand(time(NULL));

  boot_help_texts();
  tilespec_read_toplevel(tile_set_name); /* get tile sizes etc */

  ui_main(argc, argv);
  return 0;
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

  case PACKET_SELECT_NATION:
    handle_select_nation((struct packet_generic_values *)packet);
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

  case PACKET_RULESET_CONTROL:
    handle_ruleset_control((struct packet_ruleset_control *)packet);
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

  case PACKET_RULESET_GOVERNMENT:
    handle_ruleset_government((struct packet_ruleset_government *)packet);
    break;
  
  case PACKET_RULESET_GOVERNMENT_RULER_TITLE:
    handle_ruleset_government_ruler_title((struct packet_ruleset_government_ruler_title *)packet);
    break;
  
  case PACKET_RULESET_NATION:
    handle_ruleset_nation((struct packet_ruleset_nation *)packet);
    break;

  case PACKET_RULESET_CITY:
    handle_ruleset_city((struct packet_ruleset_city *)packet);
    break;

  case PACKET_RULESET_GAME:
    handle_ruleset_game((struct packet_ruleset_game *)packet);
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
    
  case PACKET_CITY_NAME_SUGGESTION:
    handle_city_name_suggestion((struct packet_city_name_suggestion *)packet);
    break;

  case PACKET_SABOTAGE_LIST:
    handle_sabotage_list((struct packet_sabotage_list *)packet);
    break;

  case PACKET_DIPLOMAT_ACTION:
    handle_diplomat_action((struct packet_diplomat_action *)packet);
    break;

  case PACKET_ADVANCE_FOCUS:
    handle_advance_focus((struct packet_generic_integer *)packet);
    break;
    
  default:
    freelog(LOG_FATAL, _("Received unknown packet from server!"));
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
  info.activity_target=punit->activity_target;
  info.select_it=0;
  info.packet_use = UNIT_INFO_IDENTITY;

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

    /* If changing from pre-game state to _either_ select race
       or running state, then we have finished getting ruleset data,
       and should translate data, for joining running game or for
       selecting nations.  (Want translated nation names in nation
       select dialog.)
    */
    if (client_state==CLIENT_PRE_GAME_STATE
	&& (newstate==CLIENT_SELECT_RACE_STATE
	    || newstate==CLIENT_GAME_RUNNING_STATE)) {
      translate_data_names();
    }
      
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
      set_unit_focus_no_center(0);
      clear_notify_window();
      idex_init();		/* clear old data */
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
