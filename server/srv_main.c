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
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_TERMIO_H
#include <sys/termio.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "capability.h"
#include "capstr.h"
#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "nation.h"
#include "netintf.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "timing.h"
#include "version.h"

#include "autoattack.h"
#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "console.h"
#include "diplhand.h"
#include "gamehand.h"
#include "gamelog.h"
#include "handchat.h"
#include "mapgen.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "stdinhand.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aihand.h"

#include "srv_main.h"


static void begin_turn(void);
static void before_end_year(void);
static void end_turn(void);
static void send_all_info(struct conn_list *dest);
static void ai_start_turn(void);
static bool is_game_over(void);
static void save_game_auto(void);
static void generate_ai_players(void);
static int mark_nation_as_used(int nation);
static void announce_ai_player(struct player *pplayer);
static void reject_new_player(char *msg, struct connection *pconn);

static void handle_alloc_nation(struct player *pplayer,
				struct packet_alloc_nation *packet);
static bool handle_request_join_game(struct connection *pconn, 
				    struct packet_req_join_game *req);
static void handle_turn_done(struct player *pplayer);
static void send_select_nation(struct player *pplayer);
static void check_for_full_turn_done(void);


/* this is used in strange places, and is 'extern'd where
   needed (hence, it is not 'extern'd in srv_main.h) */
bool is_server = TRUE;

/* command-line arguments to server */
struct server_arguments srvarg;

/* server state information */
enum server_states server_state = PRE_GAME_STATE;
bool nocity_send = FALSE;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
bool force_end_of_sniff;


/* The next three variables make selecting nations for AI players cleaner */
static int *nations_avail;
static int *nations_used;
static int num_nations_avail;

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
static unsigned short global_id_counter=100;
static unsigned char used_ids[8192]={0};

/* server initialized flag */
static bool has_been_srv_init = FALSE;

/**************************************************************************
...
**************************************************************************/
void srv_init(void)
{
  /* NLS init */
  init_nls();

  /* init server arguments... */

  srvarg.metaserver_no_send = DEFAULT_META_SERVER_NO_SEND;
  sz_strlcpy(srvarg.metaserver_info_line, default_meta_server_info_string());
  sz_strlcpy(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR);
  srvarg.metaserver_port = DEFAULT_META_SERVER_PORT;

  srvarg.port = DEFAULT_SOCK_PORT;

  srvarg.loglevel = LOG_NORMAL;

  srvarg.log_filename = NULL;
  srvarg.gamelog_filename = NULL;
  srvarg.load_filename = NULL;
  srvarg.script_filename = NULL;

  srvarg.quitidle = 0;

  srvarg.extra_metaserver_info[0] = '\0';

  /* mark as initialized */
  has_been_srv_init = TRUE;

  /* done */
  return;
}

/**************************************************************************
...
**************************************************************************/
static bool is_game_over(void)
{
  int barbs = 0;
  int alive = 0;

  if (game.year > game.end_year)
    return TRUE;

  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      barbs++;
    }
  } players_iterate_end;

  if (game.nplayers == (barbs + 1))
    return FALSE;

  players_iterate(pplayer) {
    if (pplayer->is_alive && !is_barbarian(pplayer)) {
      alive++;
    }
  } players_iterate_end;

  return (alive <= 1);
}

/**************************************************************************
  Send all information for when game starts or client reconnects.
  Ruleset information should have been sent before this.
**************************************************************************/
static void send_all_info(struct conn_list *dest)
{
  conn_list_iterate(*dest, pconn) {
      send_attribute_block(pconn->player,pconn);
  }
  conn_list_iterate_end;

  send_game_info(dest);
  send_map_info(dest);
  send_player_info_c(NULL, dest);
  send_conn_info(&game.est_connections, dest);
  send_spaceship_info(NULL, dest);
  send_all_known_tiles(dest);
  send_all_known_cities(dest);
  send_all_known_units(dest);
  send_player_turn_notifications(dest);
}

/**************************************************************************
...
**************************************************************************/
static void do_apollo_program(void)
{
  struct city *pcity = find_city_wonder(B_APOLLO);

  if (pcity) {
    struct player *pplayer = city_owner(pcity);

    if (game.civstyle == 1) {
      players_iterate(other_player) {
	city_list_iterate(other_player->cities, pcity) {
	  show_area(pplayer, pcity->x, pcity->y, 0);
	} city_list_iterate_end;
      } players_iterate_end;
    } else {
      map_know_all(pplayer);
      send_all_known_tiles(&pplayer->connections);
      send_all_known_cities(&pplayer->connections);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void marco_polo_make_contact(void)
{
  struct city *pcity = find_city_wonder(B_MARCO);

  if (pcity) {
    players_iterate(pplayer) {
      make_contact(city_owner(pcity), pplayer, pcity->x, pcity->y);
    } players_iterate_end;
  }
}

/**************************************************************************
...
**************************************************************************/
static void update_environmental_upset(enum tile_special_type cause,
				       int *current, int *accum, int *level,
				       void (*upset_action_fn)(int))
{
  int count;

  count = 0;
  whole_map_iterate(x, y) {
    if (map_has_special(x, y, cause)) {
      count++;
    }
  } whole_map_iterate_end;

  *current = count;
  *accum += count;
  if (*accum < *level) {
    *accum = 0;
  } else {
    *accum -= *level;
    if (myrand(200) <= *accum) {
      upset_action_fn((map.xsize / 10) + (map.ysize / 10) + ((*accum) * 5));
      *accum = 0;
      *level+=4;
    }
  }

  freelog(LOG_DEBUG,
	  "environmental_upset: cause=%-4d current=%-2d level=%-2d accum=%-2d",
	  cause, *current, *level, *accum);
}

/**************************************************************************
 check for cease-fires running out; update reputation; update cancelling
 reasons
**************************************************************************/
static void update_diplomatics(void)
{
  players_iterate(player1) {
    players_iterate(player2) {
      struct player_diplstate *pdiplstate =
	  &player1->diplstates[player2->player_no];

      pdiplstate->has_reason_to_cancel =
	  MAX(pdiplstate->has_reason_to_cancel - 1, 0);

      if(pdiplstate->type == DS_CEASEFIRE) {
	switch(--pdiplstate->turns_left) {
	case 1:
	  notify_player(player1,
			_("Game: Concerned citizens point "
  			  "out that the cease-fire with %s will run out soon."),
			player2->name);
  	  break;
  	case -1:
	  notify_player(player1,
  			_("Game: The cease-fire with %s has "
  			  "run out. You are now neutral towards the %s."),
			player2->name,
			get_nation_name_plural(player2->nation));
	  pdiplstate->type = DS_NEUTRAL;
	  check_city_workers(player1);
	  check_city_workers(player2);
  	  break;
  	}
        }
      player1->reputation = MIN(player1->reputation + GAME_REPUTATION_INCR,
				GAME_MAX_REPUTATION);
    } players_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Send packet which tells clients that the server is starting its
  "end year" calculations (and will be sending end-turn updates etc).
  (This is referred to as "before new year" in packet and client code.)
**************************************************************************/
static void before_end_year(void)
{
  lsend_packet_generic_empty(&game.est_connections, PACKET_BEFORE_NEW_YEAR);
}

/**************************************************************************
...
**************************************************************************/
static void ai_start_turn(void)
{
  int i;

  for (i = 0; i < game.nplayers; i++) {
    struct player *pplayer = shuffled_player(i);
    if (pplayer->ai.control) {
      ai_do_first_activities(pplayer);
      flush_packets();			/* AIs can be such spammers... */
    }
  }
}

/**************************************************************************
Handle the beginning of each turn.
Note: This does not give "time" to any player;
      it is solely for updating turn-dependent data.
**************************************************************************/
static void begin_turn(void)
{
  /* See if the value of fog of war has changed */
  if (game.fogofwar != game.fogofwar_old) {
    if (game.fogofwar) {
      enable_fog_of_war();
      game.fogofwar_old = TRUE;
    } else {
      disable_fog_of_war();
      game.fogofwar_old = FALSE;
    }
  }

  conn_list_do_buffer(&game.game_connections);

  players_iterate(pplayer) {
    freelog(LOG_DEBUG, "beginning player turn for #%d (%s)",
	    pplayer->player_no, pplayer->name);
    begin_player_turn(pplayer);
  } players_iterate_end;

  players_iterate(pplayer) {
    send_player_cities(pplayer);
  } players_iterate_end;

  flush_packets();			/* to curb major city spam */

  conn_list_do_unbuffer(&game.game_connections);
}

/**************************************************************************
...
**************************************************************************/
static void end_turn(void)
{
  int i;

  nocity_send = TRUE;
  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = shuffled_player(i);
    freelog(LOG_DEBUG, "updating player activities for #%d (%s)",
		  i, pplayer->name);
    update_player_activities(pplayer);
         /* ai unit activity has been moved UP -- Syela */

    flush_packets();
	 /* update_player_activities calls update_unit_activities which
	    causes *major* network traffic */
    pplayer->turn_done = FALSE;
  }
  nocity_send = FALSE;
  players_iterate(pplayer) {
    send_player_cities(pplayer);
  } players_iterate_end;
  flush_packets();			/* to curb major city spam */

  update_environmental_upset(S_POLLUTION, &game.heating,
			     &game.globalwarming, &game.warminglevel,
			     global_warming);
  update_environmental_upset(S_FALLOUT, &game.cooling,
			     &game.nuclearwinter, &game.coolinglevel,
			     nuclear_winter);
  update_diplomatics();
  do_apollo_program();
  marco_polo_make_contact();
  make_history_report();
  send_player_turn_notifications(NULL);
  freelog(LOG_DEBUG, "Turn ended.");
  game.turn_start = time(NULL);
}

/**************************************************************************
Unconditionally save the game, with specified filename.
Always prints a message: either save ok, or failed.

Note that if !HAVE_LIBZ, then game.save_compress_level should never
become non-zero, so no need to check HAVE_LIBZ explicitly here as well.
**************************************************************************/
void save_game(char *orig_filename)
{
  char filename[600];
  struct section_file file;
  struct timer *timer_cpu, *timer_user;

  if (orig_filename && orig_filename[0] != '\0'){
    sz_strlcpy(filename, orig_filename);
  } else { /* If orig_filename is NULL or empty, use "civgame<year>m.sav" */
    my_snprintf(filename, sizeof(filename),
		"%s%+05dm.sav", game.save_name, game.year);
  }
  
  timer_cpu = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
  timer_user = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    
  section_file_init(&file);
  game_save(&file);

  if (game.save_compress_level > 0) {
    /* Append ".gz" to filename if not there: */
    size_t len = strlen(filename);
    if (len < 3 || strcmp(filename+len-3, ".gz") != 0) {
      sz_strlcat(filename, ".gz");
    }
  }

  if(!section_file_save(&file, filename, game.save_compress_level))
    con_write(C_FAIL, _("Failed saving game as %s"), filename);
  else
    con_write(C_OK, _("Game saved as %s"), filename);

  section_file_free(&file);

  freelog(LOG_VERBOSE, "Save time: %g seconds (%g apparent)",
	  read_timer_seconds_free(timer_cpu),
	  read_timer_seconds_free(timer_user));
}

/**************************************************************************
Save game with autosave filename, and call gamelog_save().
**************************************************************************/
static void save_game_auto(void)
{
  char filename[512];

  assert(strlen(game.save_name)<256);
  
  my_snprintf(filename, sizeof(filename),
	      "%s%+05d.sav", game.save_name, game.year);
  save_game(filename);
  gamelog_save();		/* should this be in save_game()? --dwp */
}

/**************************************************************************
...
**************************************************************************/
void start_game(void)
{
  if(server_state!=PRE_GAME_STATE) {
    con_puts(C_SYNTAX, _("The game is already running."));
    return;
  }

  con_puts(C_OK, _("Starting game."));

  server_state=SELECT_RACES_STATE; /* loaded ??? */
  force_end_of_sniff = TRUE;
  game.turn_start = time(NULL);
}

/**************************************************************************
...
**************************************************************************/
static void handle_report_request(struct connection *pconn,
				  enum report_type type)
{
  struct conn_list *dest = &pconn->self;
  
  if (server_state != RUN_GAME_STATE && server_state != GAME_OVER_STATE
      && type != REPORT_SERVER_OPTIONS1 && type != REPORT_SERVER_OPTIONS2) {
    freelog(LOG_ERROR, "Got a report request %d before game start", type);
    return;
  }

  switch(type) {
   case REPORT_WONDERS_OF_THE_WORLD:
    report_wonders_of_the_world(dest);
    break;
   case REPORT_TOP_5_CITIES:
    report_top_five_cities(dest);
    break;
   case REPORT_DEMOGRAPHIC:
    report_demographics(pconn);
    break;
  case REPORT_SERVER_OPTIONS1:
    report_server_options(dest, 1);
    break;
  case REPORT_SERVER_OPTIONS2:
    report_server_options(dest, 2);
    break;
  case REPORT_SERVER_OPTIONS: /* obsolete */
  default:
    notify_conn(dest, "Game: request for unknown report (type %d)", type);
  }
}

/**************************************************************************
...
**************************************************************************/
void dealloc_id(int id)
{
  used_ids[id/8]&= 0xff ^ (1<<(id%8));
}

/**************************************************************************
...
**************************************************************************/
bool is_id_allocated(int id)
{
  return TEST_BIT(used_ids[id / 8], id % 8);
}

/**************************************************************************
...
**************************************************************************/
void alloc_id(int id)
{
  used_ids[id/8]|= (1<<(id%8));
}

/**************************************************************************
...
**************************************************************************/

int get_next_id_number(void)
{
  while (is_id_allocated(++global_id_counter) || global_id_counter == 0) {
    /* nothing */
  }
  return global_id_counter;
}

/**************************************************************************
Returns 0 if connection should be closed (because the clients was
rejected). Returns 1 else.
**************************************************************************/
bool handle_packet_input(struct connection *pconn, void *packet, int type)
{
  struct player *pplayer;

  /* a NULL packet can be returned from receive_packet_goto_route() */
  if (!packet)
    return TRUE;

  if (type == PACKET_REQUEST_JOIN_GAME) {
    bool result = handle_request_join_game(pconn,
					  (struct packet_req_join_game *) packet);
    free(packet);
    return result;
  }

  if (!pconn->established) {
    freelog(LOG_ERROR, "Received game packet from unaccepted connection %s",
	    conn_description(pconn));
    free(packet);
    return TRUE;
  }
  
  pplayer = pconn->player;

  if(!pplayer) {
    /* don't support these yet */
    freelog(LOG_ERROR, "Received packet from non-player connection %s",
 	    conn_description(pconn));
    free(packet);
    return TRUE;
  }

  if (server_state != RUN_GAME_STATE
      && type != PACKET_ALLOC_NATION
      && type != PACKET_CHAT_MSG
      && type != PACKET_CONN_PONG
      && type != PACKET_REPORT_REQUEST) {
    if (server_state == GAME_OVER_STATE) {
      /* This can happen by accident, so we don't want to print
	 out lots of error messages. Ie, we use LOG_DEBUG. */
      freelog(LOG_DEBUG, "got a packet of type %d "
			  "in GAME_OVER_STATE", type);
    } else {
      freelog(LOG_ERROR, "got a packet of type %d "
	                 "outside RUN_GAME_STATE", type);
    }
    free(packet);
    return TRUE;
  }

  pplayer->nturns_idle=0;

  if((!pplayer->is_alive || pconn->observer)
     && !(type == PACKET_CHAT_MSG || type == PACKET_REPORT_REQUEST
	  || type == PACKET_CONN_PONG)) {
    freelog(LOG_ERROR, _("Got a packet of type %d from a "
			 "dead or observer player"), type);
    free(packet);
    return TRUE;
  }
  
  /* Make sure to set this back to NULL before leaving this function: */
  pplayer->current_conn = pconn;
  
  switch(type) {
    
  case PACKET_TURN_DONE:
    handle_turn_done(pplayer);
    break;

  case PACKET_ALLOC_NATION:
    handle_alloc_nation(pplayer, (struct packet_alloc_nation *)packet);
    break;

  case PACKET_UNIT_INFO:
    handle_unit_info(pplayer, (struct packet_unit_info *)packet);
    break;

  case PACKET_MOVE_UNIT:
    handle_move_unit(pplayer, (struct packet_move_unit *)packet);
    break;
 
  case PACKET_CHAT_MSG:
    handle_chat_msg(pconn, (struct packet_generic_message *)packet);
    break;

  case PACKET_CITY_SELL:
    handle_city_sell(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_BUY:
    handle_city_buy(pplayer, (struct packet_city_request *)packet);
    break;
   
  case PACKET_CITY_CHANGE:
    handle_city_change(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_WORKLIST:
    handle_city_worklist(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_MAKE_SPECIALIST:
    handle_city_make_specialist(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_MAKE_WORKER:
    handle_city_make_worker(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_CHANGE_SPECIALIST:
    handle_city_change_specialist(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_CITY_RENAME:
    handle_city_rename(pplayer, (struct packet_city_request *)packet);
    break;

  case PACKET_PLAYER_RATES:
    handle_player_rates(pplayer, (struct packet_player_request *)packet);
    break;

  case PACKET_PLAYER_REVOLUTION:
    handle_player_revolution(pplayer);
    break;

  case PACKET_PLAYER_GOVERNMENT:
    handle_player_government(pplayer, (struct packet_player_request *)packet);
    break;

  case PACKET_PLAYER_RESEARCH:
    handle_player_research(pplayer, (struct packet_player_request *)packet);
    break;

  case PACKET_PLAYER_TECH_GOAL:
    handle_player_tech_goal(pplayer, (struct packet_player_request *)packet);
    break;

  case PACKET_UNIT_BUILD_CITY:
    handle_unit_build_city(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_DISBAND:
    handle_unit_disband(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_CHANGE_HOMECITY:
    handle_unit_change_homecity(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_AUTO:
    handle_unit_auto_request(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_UNLOAD:
    handle_unit_unload_request(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNITTYPE_UPGRADE:
    handle_upgrade_unittype_request(pplayer, (struct packet_unittype_info *)packet);
    break;

  case PACKET_UNIT_ESTABLISH_TRADE:
    (void) handle_unit_establish_trade(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_HELP_BUILD_WONDER:
    handle_unit_help_build_wonder(pplayer, (struct packet_unit_request *)packet);
    break;

  case PACKET_UNIT_GOTO_TILE:
    handle_unit_goto_tile(pplayer, (struct packet_unit_request *)packet);
    break;
    
  case PACKET_DIPLOMAT_ACTION:
    handle_diplomat_action(pplayer, (struct packet_diplomat_action *)packet);
    break;
  case PACKET_REPORT_REQUEST:
    handle_report_request(pconn,
			  ((struct packet_generic_integer *)packet)->value);
    break;
  case PACKET_DIPLOMACY_INIT_MEETING:
    handle_diplomacy_init(pplayer, (struct packet_diplomacy_info *)packet);
    break;
  case PACKET_DIPLOMACY_CANCEL_MEETING:
    handle_diplomacy_cancel_meeting(pplayer, (struct packet_diplomacy_info *)packet);  
    break;
  case PACKET_DIPLOMACY_CREATE_CLAUSE:
    handle_diplomacy_create_clause(pplayer, (struct packet_diplomacy_info *)packet);  
    break;
  case PACKET_DIPLOMACY_REMOVE_CLAUSE:
    handle_diplomacy_remove_clause(pplayer, (struct packet_diplomacy_info *)packet);  
    break;
  case PACKET_DIPLOMACY_ACCEPT_TREATY:
    handle_diplomacy_accept_treaty(pplayer, (struct packet_diplomacy_info *)packet);  
    break;
  case PACKET_CITY_REFRESH:
    handle_city_refresh(pplayer, (struct packet_generic_integer *)packet);
    break;
  case PACKET_INCITE_INQ:
    handle_incite_inq(pconn, (struct packet_generic_integer *)packet);
    break;
  case PACKET_UNIT_UPGRADE:
    handle_unit_upgrade_request(pplayer, (struct packet_unit_request *)packet);
    break;
  case PACKET_CITY_OPTIONS:
    handle_city_options(pplayer, (struct packet_generic_values *)packet);
    break;
  case PACKET_SPACESHIP_ACTION:
    handle_spaceship_action(pplayer, (struct packet_spaceship_action *)packet);
    break;
  case PACKET_UNIT_NUKE:
    handle_unit_nuke(pplayer, (struct packet_unit_request *)packet);
    break;
  case PACKET_CITY_NAME_SUGGEST_REQ:
    handle_city_name_suggest_req(pconn,
				 (struct packet_generic_integer *)packet);
    break;
  case PACKET_UNIT_PARADROP_TO:
    handle_unit_paradrop_to(pplayer, (struct packet_unit_request *)packet);
    break;
  case PACKET_PLAYER_CANCEL_PACT:
    handle_player_cancel_pact(pplayer,
			      ((struct packet_generic_integer *)packet)->value);
    break;
  case PACKET_UNIT_CONNECT:
    handle_unit_connect(pplayer, (struct packet_unit_connect *)packet);
    break;
  case PACKET_PLAYER_REMOVE_VISION:
    handle_player_remove_vision(pplayer, (struct packet_generic_integer *)packet);
    break;
  case PACKET_GOTO_ROUTE:
    handle_goto_route(pplayer, (struct packet_goto_route *)packet);
    break;
  case PACKET_PATROL_ROUTE:
    handle_patrol_route(pplayer, (struct packet_goto_route *)packet);
    break;
  case PACKET_CONN_PONG:
    pconn->ponged = TRUE;
    break;
  case PACKET_UNIT_AIRLIFT:
    handle_unit_airlift(pplayer, (struct packet_unit_request *)packet);
    break;
  case PACKET_ATTRIBUTE_CHUNK:
    handle_player_attribute_chunk(pplayer,
				  (struct packet_attribute_chunk *)
				  packet);
    break;
  case PACKET_PLAYER_ATTRIBUTE_BLOCK:
    handle_player_attribute_block(pplayer);
    break;

  default:
    freelog(LOG_ERROR, "Received unknown packet %d from %s",
	    type, conn_description(pconn));
  }

  free(packet);
  pplayer->current_conn = NULL;
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static void check_for_full_turn_done(void)
{
  /* fixedlength is only applicable if we have a timeout set */
  if (game.fixedlength && game.timeout != 0)
    return;

  players_iterate(pplayer) {
    if (game.turnblock) {
      if (!pplayer->ai.control && pplayer->is_alive && !pplayer->turn_done)
        return;
    } else {
      if(pplayer->is_connected && pplayer->is_alive && !pplayer->turn_done) {
        return;
      }
    }
  } players_iterate_end;

  force_end_of_sniff = TRUE;
}

/**************************************************************************
...
(Hmm, how should "turn done" work for multi-connected non-observer players?)
**************************************************************************/
static void handle_turn_done(struct player *pplayer)
{
  pplayer->turn_done = TRUE;

  check_for_full_turn_done();

  send_player_info(pplayer, NULL);
}

/**************************************************************************
...
**************************************************************************/
static void handle_alloc_nation(struct player *pplayer,
				struct packet_alloc_nation *packet)
{
  int nation_used_count;

  if (server_state != SELECT_RACES_STATE) {
    freelog(LOG_ERROR, _("Trying to alloc nation outside "
			 "of SELECT_RACES_STATE!"));
    return;
  }  

  remove_leading_trailing_spaces(packet->name);

  if (strlen(packet->name)==0) {
    notify_player(pplayer, _("Please choose a non-blank name."));
    send_select_nation(pplayer);
    return;
  }

  players_iterate(other_player) {
    if(other_player->nation==packet->nation_no) {
       send_select_nation(pplayer); /* it failed - nation taken */
       return;
    } else
      /* Check to see if name has been taken.
       * Ignore case because matches elsewhere are case-insenstive.
       * Don't limit this check to just players with allocated nation:
       * otherwise could end up with same name as pre-created AI player
       * (which have no nation yet, but will keep current player name).
       * Also want to keep all player names strictly distinct at all
       * times (for server commands etc), including during nation
       * allocation phase.
       */
      if (other_player->player_no != pplayer->player_no
	  && mystrcasecmp(other_player->name,packet->name) == 0) {
	notify_player(pplayer,
		     _("Another player already has the name '%s'.  "
		       "Please choose another name."), packet->name);
       send_select_nation(pplayer);
       return;
    }
  } players_iterate_end;

  notify_conn_ex(&game.est_connections, -1, -1, E_NATION_SELECTED,
		 _("Game: %s is the %s ruler %s."), pplayer->name,
		 get_nation_name(packet->nation_no), packet->name);

  /* inform player his choice was ok */
  lsend_packet_generic_empty(&pplayer->connections,
			     PACKET_SELECT_NATION_OK);

  pplayer->nation=packet->nation_no;
  sz_strlcpy(pplayer->name, packet->name);
  pplayer->is_male=packet->is_male;
  pplayer->city_style=packet->city_style;

  /* tell the other players, that the nation is now unavailable */
  nation_used_count = 0;

  players_iterate(other_player) {
    if (other_player->nation == MAX_NUM_NATIONS) {
      send_select_nation(other_player);
    } else {
      nation_used_count++;	/* count used nations */
    }
  } players_iterate_end;

  mark_nation_as_used(packet->nation_no);

  /* if there's no nation left, reject remaining players, sorry */
  if( nation_used_count == game.playable_nation_count ) {   /* barb */
    players_iterate(other_player) {
      if (other_player->nation == MAX_NUM_NATIONS) {
	freelog(LOG_NORMAL, _("No nations left: Removing player %s."),
		other_player->name);
	notify_player(other_player,
		      _("Game: Sorry, there are no nations left."));
	server_remove_player(other_player);
      }
    } players_iterate_end;
  }
}

/**************************************************************************
 Sends the currently collected selected nations to the given player.
**************************************************************************/
static void send_select_nation(struct player *pplayer)
{
  struct packet_nations_used packet;

  packet.num_nations_used = 0;

  players_iterate(other_player) {
    if (other_player->nation == MAX_NUM_NATIONS) {
      continue;
    }
    packet.nations_used[packet.num_nations_used] = other_player->nation;
    packet.num_nations_used++;
  } players_iterate_end;

  lsend_packet_nations_used(&pplayer->connections, &packet);
}

/**************************************************************************
 Send a join_game reply, accepting the connection.
 pconn->player should be set and non-NULL.
 'rejoin' is whether this is a new player, or re-connection.
 Prints a log message, and notifies other players.
**************************************************************************/
static void join_game_accept(struct connection *pconn, bool rejoin)
{
  struct packet_join_game_reply packet;
  struct player *pplayer = pconn->player;

  assert(pplayer != NULL);
  packet.you_can_join = TRUE;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message),
	      "%s %s.", (rejoin?"Welcome back":"Welcome"), pplayer->name);
  send_packet_join_game_reply(pconn, &packet);
  pconn->established = TRUE;
  conn_list_insert_back(&game.est_connections, pconn);
  conn_list_insert_back(&game.game_connections, pconn);

  send_conn_info(&pconn->self, &game.est_connections);

  freelog(LOG_NORMAL, _("%s has joined as player %s."),
	  pconn->name, pplayer->name);
  conn_list_iterate(game.est_connections, aconn) {
    if (aconn!=pconn) {
      notify_conn(&aconn->self,
		  _("Game: Connection %s from %s has joined as player %s."),
		  pconn->name, pconn->addr, pplayer->name);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
 Introduce connection/client/player to server, and notify of other players.
**************************************************************************/
static void introduce_game_to_connection(struct connection *pconn)
{
  struct conn_list *dest;
  char hostname[512];

  if (!pconn) {
    return;
  }
  dest = &pconn->self;
  
  if (my_gethostname(hostname, sizeof(hostname))==0) {
    notify_conn(dest, _("Welcome to the %s Server running at %s port %d."), 
		freeciv_name_version(), hostname, srvarg.port);
  } else {
    notify_conn(dest, _("Welcome to the %s Server at port %d."),
		freeciv_name_version(), srvarg.port);
  }

  /* tell who we're waiting on to end the game turn */
  if (game.turnblock) {
    players_iterate(cplayer) {
      if (cplayer->is_alive
	  && !cplayer->ai.control
	  && !cplayer->turn_done
	  && cplayer != pconn->player) {  /* skip current player */
	notify_conn(dest, _("Turn-blocking game play: "
			    "waiting on %s to finish turn..."),
		    cplayer->name);
      }
    } players_iterate_end;
  }

  if (server_state==RUN_GAME_STATE) {
    /* if the game is running, players can just view the Players menu?  --dwp */
    return;
  }

  /* Just use output of server command 'list' to inform about other
   * players and player-connections:
   */
  show_players(pconn);

  /* notify_conn(dest, "Waiting for the server to start the game."); */
  /* I would put this here, but then would need another message when
     the game is started. --dwp */
}

/**************************************************************************
  This is used when a new player joins a game, before the game
  has started.  If pconn is NULL, is an AI, else a client.
  ...
**************************************************************************/
void accept_new_player(char *name, struct connection *pconn)
{
  struct player *pplayer = &game.players[game.nplayers];

  server_player_init(pplayer, FALSE);
  /* sometimes needed if players connect/disconnect to avoid
   * inheriting stale AI status etc
   */
  
  sz_strlcpy(pplayer->name, name);
  sz_strlcpy(pplayer->username, name);

  if (pconn) {
    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, FALSE);
  } else {
    freelog(LOG_NORMAL, _("%s has been added as an AI-controlled player."),
	    name);
    notify_player(NULL,
		  _("Game: %s has been added as an AI-controlled player."),
		  name);
  }

  game.nplayers++;

  introduce_game_to_connection(pconn);
  (void) send_server_info_to_metaserver(TRUE, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void reject_new_player(char *msg, struct connection *pconn)
{
  struct packet_join_game_reply packet;
  
  packet.you_can_join = FALSE;
  sz_strlcpy(packet.capability, our_capability);
  sz_strlcpy(packet.message, msg);
  send_packet_join_game_reply(pconn, &packet);
}
  
/**************************************************************************
  Try a single name as new connection name for set_unique_conn_name().
  If name is ok, copy to pconn->name and return 1, else return 0.
**************************************************************************/
static bool try_conn_name(struct connection *pconn, const char *name)
{
  char save_name[MAX_LEN_NAME];

  /* avoid matching current name in find_conn_by_name() */
  sz_strlcpy(save_name, pconn->name);
  pconn->name[0] = '\0';
  
  if (!find_conn_by_name(name)) {
    sz_strlcpy(pconn->name, name);
    return TRUE;
  } else {
    sz_strlcpy(pconn->name, save_name);
    return FALSE;
  }
}

/**************************************************************************
  Set pconn->name based on reqested name req_name: either use req_name,
  if no other connection has that name, else a modified name based on
  req_name (req_name prefixed by digit and dash).
  Returns 0 if req_name used unchanged, else 1.
**************************************************************************/
static bool set_unique_conn_name(struct connection *pconn, const char *req_name)
{
  char adjusted_name[MAX_LEN_NAME];
  int i;
  
  if (try_conn_name(pconn, req_name)) {
    return FALSE;
  }
  for(i=1; i<10000; i++) {
    my_snprintf(adjusted_name, sizeof(adjusted_name), "%d-%s", i, req_name);
    if (try_conn_name(pconn, adjusted_name)) {
      return TRUE;
    }
  }
  /* This should never happen */
  freelog(LOG_ERROR, "Too many failures in set_unique_conn_name(%s,%s)",
	  pconn->name, req_name);
  sz_strlcpy(pconn->name, req_name);
  return FALSE;
}

/**************************************************************************
Returns 0 if the clients gets rejected and the connection should be
closed. Returns 1 if the client get accepted.
**************************************************************************/
static bool handle_request_join_game(struct connection *pconn, 
				    struct packet_req_join_game *req)
{
  struct player *pplayer;
  char msg[MAX_LEN_MSG];
  char orig_name[MAX_LEN_NAME];
  const char *allow;		/* pointer into game.allow_connect */
  
  sz_strlcpy(orig_name, req->name);
  remove_leading_trailing_spaces(req->name);

  /* Name-sanity check: could add more checks? */
  if (strlen(req->name)==0) {
    my_snprintf(msg, sizeof(msg), _("Invalid name '%s'"), orig_name);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("Rejected connection from %s with invalid name."),
	    pconn->addr);
    return FALSE;
  }

  if (!set_unique_conn_name(pconn, req->name)) {
    freelog(LOG_NORMAL, _("Connection request from %s from %s"),
	    req->name, pconn->addr);
  } else {
    freelog(LOG_NORMAL,
	    _("Connection request from %s from %s (connection named %s)"),
	    req->name, pconn->addr, pconn->name);
  }
  
  freelog(LOG_NORMAL, _("%s has client version %d.%d.%d%s"),
	  pconn->name, req->major_version, req->minor_version,
	  req->patch_version, req->version_label);
  freelog(LOG_VERBOSE, "Client caps: %s", req->capability);
  freelog(LOG_VERBOSE, "Server caps: %s", our_capability);
  sz_strlcpy(pconn->capability, req->capability);
  
  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(our_capability, req->capability)) {
    my_snprintf(msg, sizeof(msg),
		_("The client is missing a capability that this server needs.\n"
		   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
		   "  Upgrading may help!"),
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
	    req->major_version, req->minor_version,
	    req->patch_version, req->version_label);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
	    pconn->name);
    return FALSE;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, our_capability)) {
    my_snprintf(msg, sizeof(msg),
		_("The server is missing a capability that the client needs.\n"
		   "Server version: %d.%d.%d%s Client version: %d.%d.%d%s."
		   "  Upgrading may help!"),
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION, VERSION_LABEL,
	    req->major_version, req->minor_version,
	    req->patch_version, req->version_label);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Mismatched capabilities."),
	    pconn->name);
    return FALSE;
  }

  if( (pplayer=find_player_by_name(req->name)) || 
      (pplayer=find_player_by_user(req->name))     ) {
    if(is_barbarian(pplayer)) {
      if(!(allow = strchr(game.allow_connect, 'b'))) {
	reject_new_player(_("Sorry, no observation of barbarians "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No observation of barbarians."),
		pconn->name);
	return FALSE;
      }
    } else if(!pplayer->is_alive) {
      if(!(allow = strchr(game.allow_connect, 'd'))) {
	reject_new_player(_("Sorry, no observation of dead players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No observation of dead players."),
		pconn->name);
	return FALSE;
      }
    } else if(pplayer->ai.control) {
      if(!(allow = strchr(game.allow_connect, (game.is_new_game ? 'A' : 'a')))) {
	reject_new_player(_("Sorry, no observation of AI players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No observation of AI players."),
		pconn->name);
	return FALSE;
      }
    } else {
      if(!(allow = strchr(game.allow_connect, (game.is_new_game ? 'H' : 'h')))) {
	reject_new_player(_("Sorry, no reconnections to human-mode players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No reconnection to human players."),
		pconn->name);
	return FALSE;
      }
    }

    allow++;
    if (conn_list_size(&pplayer->connections) > 0
	&& !(*allow == '*' || *allow == '+' || *allow == '=')) {

      /* The player exists but is already connected and multiple
       * connections not permitted. */
      
      if(game.is_new_game && server_state==PRE_GAME_STATE) {
	/* Duplicate names cause problems even in PRE_GAME_STATE
	 * because they are used to identify players for various
	 * server commands. --dwp
	 */
	reject_new_player(_("Sorry, someone else already has that name."),
			  pconn);
	freelog(LOG_NORMAL, _("%s was rejected: Name %s already used."),
		pconn->name, req->name);
	return FALSE;
      }
      else {
	my_snprintf(msg, sizeof(msg), _("Sorry, %s is already connected."), 
		    pplayer->name);
	reject_new_player(msg, pconn);
	freelog(LOG_NORMAL, _("%s was rejected: %s already connected."),
		pconn->name, pplayer->name);
	return FALSE;
      }
    }

    /* The player exists and not connected or multi-conn allowed: */
    if ((*allow == '=') || (*allow == '-')
	|| (pplayer->is_connected && (*allow != '*'))) {
      pconn->observer = TRUE;
    }
    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, TRUE);
    introduce_game_to_connection(pconn);
    if(server_state==RUN_GAME_STATE) {
      send_rulesets(&pconn->self);
      send_all_info(&pconn->self);
      send_game_state(&pconn->self, CLIENT_GAME_RUNNING_STATE);
      send_player_info(NULL,NULL);
      send_diplomatic_meetings(pconn);
      send_packet_generic_empty(pconn, PACKET_START_TURN);
    }
    if (game.auto_ai_toggle && pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    return TRUE;
  }

  /* unknown name */

  if (server_state != PRE_GAME_STATE || !game.is_new_game) {
    reject_new_player(_("Sorry, the game is already running."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Game running and unknown name %s."),
	    pconn->name, req->name);
    return FALSE;
  }

  if(game.nplayers >= game.max_players) {
    reject_new_player(_("Sorry, the game is full."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Maximum number of players reached."),
	    pconn->name);    
    return FALSE;
  }

  if(!(allow = strchr(game.allow_connect, 'N'))) {
    reject_new_player(_("Sorry, no new players allowed in this game."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: No connections as new player."),
	    pconn->name);
    return FALSE;
  }
  
  accept_new_player(req->name, pconn);
  return TRUE;
}

/**************************************************************************
  High-level server stuff when connection to client is closed or lost.
  Reports loss to log, and to other players if the connection was a
  player.  Also removes player if in pregame, applies auto_toggle, and
  does check for turn done (since can depend on connection/ai status).
  Note caller should also call close_connection() after this, to do
  lower-level close stuff.
**************************************************************************/
void lost_connection_to_client(struct connection *pconn)
{
  struct player *pplayer = pconn->player;
  const char *desc = conn_description(pconn);

  freelog(LOG_NORMAL, _("Lost connection: %s."), desc);
  
  /* _Must_ avoid sending to pconn, in case pconn connection is
   * really lost (as opposed to server shutting it down) which would
   * trigger an error on send and recurse back to here.
   * Safe to unlink even if not in list:
   */
  conn_list_unlink(&game.est_connections, pconn);
  delayed_disconnect++;
  notify_conn(&game.est_connections, _("Game: Lost connection: %s."), desc);

  if (!pplayer) {
    /* This happens eg if the player has not yet joined properly. */
    delayed_disconnect--;
    return;
  }

  unassociate_player_connection(pplayer, pconn);

  /* Remove from lists so this conn is not included in broadcasts.
   * safe to do these even if not in lists:
   */
  conn_list_unlink(&game.all_connections, pconn);
  conn_list_unlink(&game.game_connections, pconn);
  pconn->established = FALSE;
  
  send_conn_info_remove(&pconn->self, &game.est_connections);
  send_player_info(pplayer, NULL);
  notify_if_first_access_level_is_available();

  /* Cancel diplomacy meetings */
  if (!pplayer->is_connected) { /* may be still true if multiple connections */
    players_iterate(other_player) {
      if (find_treaty(pplayer, other_player)) {
	struct packet_diplomacy_info packet;
	packet.plrno0 = pplayer->player_no;
	packet.plrno1 = other_player->player_no;
	handle_diplomacy_cancel_meeting(pplayer, &packet);
      }
    } players_iterate_end;
  }

  if (game.is_new_game
      && !pplayer->is_connected /* eg multiple controllers */
      && !pplayer->ai.control    /* eg created AI player */
      && (server_state==PRE_GAME_STATE || server_state==SELECT_RACES_STATE)) {
    server_remove_player(pplayer);
  }
  else {
    if (game.auto_ai_toggle
	&& !pplayer->ai.control
	&& !pplayer->is_connected /* eg multiple controllers */) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    check_for_full_turn_done();
  }
  delayed_disconnect--;
}

/**************************************************************************
generate_ai_players() - Selects a nation for players created with
   server's "create <PlayerName>" command.  If <PlayerName> matches
   one of the leader names for some nation, we choose that nation.
   (I.e. if we issue "create Shaka" then we will make that AI player's
   nation the Zulus if the Zulus have not been chosen by anyone else.
   If they have, then we pick an available nation at random.)

   After that, we check to see if the server option "aifill" is greater
   than the number of players currently connected.  If so, we create the
   appropriate number of players (game.aifill - game.nplayers) from
   scratch, choosing a random nation and appropriate name for each.

   If the AI player name is one of the leader names for the AI player's
   nation, the player sex is set to the sex for that leader, else it
   is chosen randomly.  (So if English are ruled by Elisabeth, she is
   female, but if "Player 1" rules English, may be male or female.)
**************************************************************************/
static void generate_ai_players(void)
{
  Nation_Type_id nation;
  char player_name[MAX_LEN_NAME];
  struct player *pplayer;
  int i, old_nplayers;

  /* Select nations for AI players generated with server
   * 'create <name>' command
   */
  for (i=0; i<game.nplayers; i++) {
    pplayer = &game.players[i];
    
    if (pplayer->nation != MAX_NUM_NATIONS)
      continue;

    if (num_nations_avail == 0) {
      freelog(LOG_NORMAL,
	      _("Ran out of nations.  AI controlled player %s not created."),
	      pplayer->name );
      server_remove_player(pplayer); 
      /*
       * Below decrement loop index 'i' so that the loop is redone with
       * the current index (if 'i' is still less than new game.nplayers).
       * This is because subsequent players in list will have been shifted
       * down one spot by the remove, and may need handling.
       */
      i--;  
      continue;
    }

    for (nation = 0; nation < game.playable_nation_count; nation++) {
      if (check_nation_leader_name(nation, pplayer->name)) {
        if (nations_used[nation] != -1) {
	  pplayer->nation = mark_nation_as_used(nation);
	  pplayer->city_style = get_nation_city_style(nation);
          pplayer->is_male = get_nation_leader_sex(nation, pplayer->name);
	  break;
        }
      }
    }

    if (nation == game.playable_nation_count) {
      pplayer->nation =
	mark_nation_as_used(nations_avail[myrand(num_nations_avail)]);
      pplayer->city_style = get_nation_city_style(pplayer->nation);
      pplayer->is_male = (myrand(2) == 1);
    }

    announce_ai_player(pplayer);
  }

  /* Create and pick nation and name for AI players needed to bring the
   * total number of players to equal game.aifill
   */

  if (game.playable_nation_count < game.aifill) {
    game.aifill = game.playable_nation_count;
    freelog(LOG_NORMAL,
	     _("Nation count smaller than aifill; aifill reduced to %d."),
             game.playable_nation_count);
  }

  if (game.max_players < game.aifill) {
    game.aifill = game.max_players;
    freelog(LOG_NORMAL,
	     _("Maxplayers smaller than aifill; aifill reduced to %d."),
             game.max_players);
  }

  for(;game.nplayers < game.aifill;) {
    nation = mark_nation_as_used(nations_avail[myrand(num_nations_avail)]);
    pick_ai_player_name(nation, player_name);

    old_nplayers = game.nplayers;
    accept_new_player(player_name, NULL);
    pplayer = get_player(old_nplayers);
     
    if (!((game.nplayers == old_nplayers+1)
	  && strcmp(player_name, pplayer->name)==0)) {
      con_write(C_FAIL, _("Error creating new ai player: %s\n"),
		player_name);
      break;			/* don't loop forever */
    }
      
    pplayer->nation = nation;
    pplayer->city_style = get_nation_city_style(nation);
    pplayer->ai.control = TRUE;
    pplayer->ai.skill_level = game.skill_level;
    if (check_nation_leader_name(nation, player_name)) {
      pplayer->is_male = get_nation_leader_sex(nation, player_name);
    } else {
      pplayer->is_male = (myrand(2) == 1);
    }
    announce_ai_player(pplayer);
    set_ai_level_direct(pplayer, pplayer->ai.skill_level);
  }
  (void) send_server_info_to_metaserver(TRUE, FALSE);
}

/*************************************************************************
 Used in pick_ai_player_name() below; buf has size at least MAX_LEN_NAME;
*************************************************************************/
static bool good_name(char *ptry, char *buf) {
  if (!find_player_by_name(ptry)) {
     (void) mystrlcpy(buf, ptry, MAX_LEN_NAME);
     return TRUE;
  }
  return FALSE;
}

/*************************************************************************
 pick_ai_player_name() - Returns a random ruler name picked from given nation
     ruler names, given that nation's number. If that player name is already 
     taken, iterates through all leader names to find unused one. If it fails
     it iterates through "Player 1", "Player 2", ... until an unused name
     is found.
 newname should point to a buffer of size at least MAX_LEN_NAME.
*************************************************************************/
void pick_ai_player_name(Nation_Type_id nation, char *newname) 
{
   int i, names_count;
   char **names;

   names = get_nation_leader_names(nation, &names_count);

   /* Try random names (scattershot), then all available,
    * then "Player 1" etc:
    */
   for(i=0; i<names_count; i++) {
     if (good_name(names[myrand(names_count)], newname)) return;
   }
   
   for(i=0; i<names_count; i++) {
     if (good_name(names[i], newname)) return;
   }
   
   for(i=1; /**/; i++) {
     char tempname[50];
     my_snprintf(tempname, sizeof(tempname), _("Player %d"), i);
     if (good_name(tempname, newname)) return;
   }
}

/*************************************************************************
 mark_nation_as_used() - shuffles the appropriate arrays to indicate that
 the specified nation number has been allocated to some player and is
 therefore no longer available to any other player.  We do things this way
 so that the process of determining which nations are available to AI players
 is more efficient.
*************************************************************************/
static int mark_nation_as_used (int nation) 
{
  if(num_nations_avail <= 0) {/* no more unused nation */
      freelog(LOG_FATAL, _("Argh! ran out of nations!"));
      exit(EXIT_FAILURE);
  }

   nations_used[nations_avail[num_nations_avail-1]]=nations_used[nation];
   nations_avail[nations_used[nation]]=nations_avail[--num_nations_avail];
   nations_used[nation]=-1;

   return nation;
}

/*************************************************************************
...
*************************************************************************/
static void announce_ai_player (struct player *pplayer) {
   freelog(LOG_NORMAL, _("AI is controlling the %s ruled by %s."),
                    get_nation_name_plural(pplayer->nation),
                    pplayer->name);

  players_iterate(other_player) {
    notify_player(other_player,
		  _("Game: %s rules the %s."), pplayer->name,
		  get_nation_name_plural(pplayer->nation));
  } players_iterate_end;
}

/**************************************************************************
Play the game! Returns when server_state == GAME_OVER_STATE.
**************************************************************************/
static void main_loop(void)
{
  struct timer *eot_timer;	/* time server processing at end-of-turn */
  int save_counter = 0;

  eot_timer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);

  while(server_state==RUN_GAME_STATE) {
    /* absolute beginning of a turn */
    freelog(LOG_DEBUG, "Begin turn");
    begin_turn();

#if (IS_DEVEL_VERSION || IS_BETA_VERSION)
    sanity_check();
#endif

    force_end_of_sniff = FALSE;

    freelog(LOG_DEBUG, "Shuffleplayers");
    shuffle_players();
    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_turn();
    send_start_turn_to_clients();

    /* Before sniff (human player activites), report time to now: */
    freelog(LOG_VERBOSE, "End/start-turn server/ai activities: %g seconds",
	    read_timer_seconds(eot_timer));

    /* Do auto-saves just before starting sniff_packets(), so that
     * autosave happens effectively "at the same time" as manual
     * saves, from the point of view of restarting and AI players.
     * Post-increment so we don't count the first loop.
     */
    if(save_counter >= game.save_nturns && game.save_nturns>0) {
      save_counter=0;
      save_game_auto();
    }
    save_counter++;
    
    freelog(LOG_DEBUG, "sniffingpackets");
    while (sniff_packets() == 1) {
      /* nothing */
    }

    /* After sniff, re-zero the timer: (read-out above on next loop) */
    clear_timer_start(eot_timer);
    
    conn_list_do_buffer(&game.game_connections);

#if (IS_DEVEL_VERSION || IS_BETA_VERSION)
    sanity_check();
#endif

    before_end_year();
    /* This empties the client Messages window; put this before
       everything else below, since otherwise any messages from
       the following parts get wiped out before the user gets a
       chance to see them.  --dwp
    */
    freelog(LOG_DEBUG, "Season of native unrests");
    summon_barbarians(); /* wild guess really, no idea where to put it, but
                            I want to give them chance to move their units */
    freelog(LOG_DEBUG, "Autosettlers");
    auto_settlers(); /* moved this after ai_start_turn for efficiency -- Syela */
    /* moved after sniff_packets for even more efficiency.
       What a guy I am. -- Syela */
    /* and now, we must manage our remaining units BEFORE the cities that are
       empty get to refresh and defend themselves.  How totally stupid. */
    ai_start_turn(); /* Misleading name for manage_units -- Syela */
    freelog(LOG_DEBUG, "Auto-Attack phase");
    auto_attack();
    freelog(LOG_DEBUG, "Endturn");
    end_turn();
    freelog(LOG_DEBUG, "Gamenextyear");
    game_advance_year();
    freelog(LOG_DEBUG, "Updatetimeout");
    update_timeout();
    check_spaceship_arrivals();
    freelog(LOG_DEBUG, "Sendplayerinfo");
    send_player_info(NULL, NULL);
    freelog(LOG_DEBUG, "Sendgameinfo");
    send_game_info(NULL);
    freelog(LOG_DEBUG, "Sendyeartoclients");
    send_year_to_clients(game.year);
    freelog(LOG_DEBUG, "Sendinfotometaserver");
    (void) send_server_info_to_metaserver(FALSE, FALSE);

    conn_list_do_unbuffer(&game.game_connections);

    if (is_game_over()) 
      server_state=GAME_OVER_STATE;
  }
}

/**************************************************************************
Server initialization.
**************************************************************************/
void srv_main(void)
{
  int i;

  /* make sure it's initialized */
  if (!has_been_srv_init) {
    srv_init();
  }

  my_init_network();

  con_log_init(srvarg.log_filename, srvarg.loglevel);
  gamelog_init(srvarg.gamelog_filename);
  gamelog_set_level(GAMELOG_FULL);
  gamelog(GAMELOG_NORMAL, _("Starting new log"));
  
#ifdef GENERATING_MAC	/* mac beta notice */
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, "This is an alpha/beta version of MacFreeciv.");
  con_puts(C_COMMENT, "Visit http://www.geocities.com/SiliconValley/Orchard/8738/MFC/index.html");
  con_puts(C_COMMENT, "for new versions of this project and information about it.");
  con_puts(C_COMMENT, "");
#endif
#if IS_BETA_VERSION
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, beta_message());
  con_puts(C_COMMENT, "");
#endif
  
  con_flush();

  init_our_capability();
  game_init();

  /* load a saved game */
  
  if(srvarg.load_filename) {
    struct timer *loadtimer, *uloadtimer;
    struct section_file file;
    
    freelog(LOG_NORMAL, _("Loading saved game: %s"), srvarg.load_filename);
    loadtimer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
    uloadtimer = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    if(!section_file_load_nodup(&file, srvarg.load_filename)) { 
      freelog(LOG_FATAL, _("Couldn't load savefile: %s"), srvarg.load_filename);
      exit(EXIT_FAILURE);
    }
    game_load(&file);
    section_file_check_unused(&file, srvarg.load_filename);
    section_file_free(&file);

    freelog(LOG_VERBOSE, "Load time: %g seconds (%g apparent)",
	    read_timer_seconds_free(loadtimer),
	    read_timer_seconds_free(uloadtimer));
  }

  /* init network */  
  init_connections(); 
  server_open_socket();

  if(!(srvarg.metaserver_no_send)) {
    freelog(LOG_NORMAL, _("Sending info to metaserver [%s]"),
	    meta_addr_port());
    server_open_udp(); /* open socket for meta server */ 
  }

  (void) send_server_info_to_metaserver(TRUE, FALSE);

  /* accept new players, wait for serverop to start..*/
  server_state=PRE_GAME_STATE;

  /* load a script file */
  if (srvarg.script_filename
      && !read_init_script(NULL, srvarg.script_filename)) {
    exit(EXIT_FAILURE);
  }

  freelog(LOG_NORMAL, _("Now accepting new client connections."));
  while(server_state==PRE_GAME_STATE)
    sniff_packets(); /* Accepting commands. */

  (void) send_server_info_to_metaserver(TRUE, FALSE);

  if(game.is_new_game) {
    load_rulesets();
    /* otherwise rulesets were loaded when savegame was loaded */
  }

  nations_avail = fc_calloc(game.playable_nation_count, sizeof(int));
  nations_used = fc_calloc(game.playable_nation_count, sizeof(int));

main_start_players:

  send_rulesets(&game.est_connections);

  num_nations_avail = game.playable_nation_count;
  for(i=0; i<game.playable_nation_count; i++) {
    nations_avail[i]=i;
    nations_used[i]=i;
  }

  if (game.auto_ai_toggle) {
    players_iterate(pplayer) {
      if (!pplayer->is_connected && !pplayer->ai.control) {
	toggle_ai_player_direct(NULL, pplayer);
      }
    } players_iterate_end;
  }

  /* Allow players to select a nation (case new game).
   * AI players may not yet have a nation; these will be selected
   * in generate_ai_players() later
   */
  server_state = RUN_GAME_STATE;
  players_iterate(pplayer) {
    if (pplayer->nation == MAX_NUM_NATIONS && !pplayer->ai.control) {
      send_select_nation(pplayer);
      server_state = SELECT_RACES_STATE;
    }
  } players_iterate_end;

  while(server_state==SELECT_RACES_STATE) {
    bool flag = FALSE;

    sniff_packets();

    players_iterate(pplayer) {
      if (pplayer->nation == MAX_NUM_NATIONS && !pplayer->ai.control) {
	flag = TRUE;
	break;
      }
    } players_iterate_end;

    if(!flag) {
      if (game.nplayers > 0) {
	server_state=RUN_GAME_STATE;
      } else {
	con_write(C_COMMENT,
		  _("Last player has disconnected: will need to restart."));
	server_state=PRE_GAME_STATE;
	while(server_state==PRE_GAME_STATE) {
	  sniff_packets();
	}
	goto main_start_players;
      }
    }
  }

  if(game.randseed == 0) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.randseed = time(NULL) & (MAX_UINT32 >> 1);
  }
 
  if(!myrand_is_init())
    mysrand(game.randseed);

#ifdef TEST_RANDOM		/* not defined anywhere, set it if you want it */
  test_random1(200);
  test_random1(2000);
  test_random1(20000);
  test_random1(200000);
#endif
    
  if(game.is_new_game)
    generate_ai_players();
   
  /* if we have a tile map, and map.generator==0, call map_fractal_generate
     anyway, to make the specials and huts */
  if(map_is_empty() || (map.generator == 0 && game.is_new_game))
    map_fractal_generate();

  if (map.num_continents==0)
    assign_continent_numbers();

  gamelog_map();
  /* start the game */

  server_state=RUN_GAME_STATE;
  (void) send_server_info_to_metaserver(TRUE, FALSE);

  if(game.is_new_game) {
    /* Before the player map is allocated (and initiailized)! */
    game.fogofwar_old = game.fogofwar;

    players_iterate(pplayer) {
      player_map_allocate(pplayer);
      init_tech(pplayer, game.tech);
      player_limit_to_government_rates(pplayer);
      pplayer->economic.gold=game.gold;
    } players_iterate_end;
    game.max_players=game.nplayers;

    /* we don't want random start positions in a scenario which already
       provides them.  -- Gudy */
    if(map.num_start_positions==0) {
      create_start_positions();
    }
  }

  initialize_move_costs(); /* this may be the wrong place to do this */
  generate_minimap(); /* for city_desire; saves a lot of calculations */

  if (!game.is_new_game) {
    players_iterate(pplayer) {
      civ_score(pplayer);	/* if we don't, the AI gets really confused */
      if (pplayer->ai.control) {
	set_ai_level_direct(pplayer, pplayer->ai.skill_level);
	assess_danger_player(pplayer); /* a slowdown, but a necessary one */
      }
    } players_iterate_end;
  }
  
  send_all_info(&game.game_connections);
  
  if(game.is_new_game)
    init_new_game();

  game.is_new_game = FALSE;

  send_game_state(&game.est_connections, CLIENT_GAME_RUNNING_STATE);

  /*** Where the action is. ***/
  main_loop();

  report_scores(TRUE);
  show_map_to_all();
  notify_player(NULL, _("Game: The game is over..."));
  gamelog(GAMELOG_NORMAL, _("The game is over!"));
  save_game_auto();

  while (server_state == GAME_OVER_STATE) {
    force_end_of_sniff = FALSE;
    sniff_packets();
  }

  close_connections_and_socket();
  free_rulesets();
}
