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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_TERMIO_H
#include <sys/termio.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "capability.h"
#include "capstr.h"
#include "city.h"
#include "dataio.h"
#include "effects.h"
#include "events.h"
#include "fciconv.h"
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

#include "auth.h"
#include "autoattack.h"
#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "console.h"
#include "diplhand.h"
#include "gamehand.h"
#include "gamelog.h"
#include "handchat.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "score.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "stdinhand.h"
#include "unithand.h"
#include "unittools.h"

#include "advdiplomacy.h"
#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "aihand.h"
#include "aisettler.h"
#include "citymap.h"

#include "mapgen.h"

#include "srv_main.h"


static void after_game_advance_year(void);
static void before_end_year(void);
static void end_turn(void);
static void ai_start_turn(void);
static bool is_game_over(void);
static void generate_ai_players(void);
static void mark_nation_as_used(Nation_Type_id nation);
static void announce_ai_player(struct player *pplayer);
static void send_select_nation(struct player *pplayer);
static void srv_loop(void);

static void freeze_clients(void);
static void thaw_clients(void);

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

/* List of which nations are available. */
static bool *nations_available;

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
static unsigned short global_id_counter=100;
static unsigned char used_ids[8192]={0};

/* server initialized flag */
static bool has_been_srv_init = FALSE;

/**************************************************************************
  Initialize the game seed.  This may safely be called multiple times.
**************************************************************************/
void init_game_seed(void)
{
  if (game.seed == 0) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.seed = time(NULL) & (MAX_UINT32 >> 1);
  }
 
  if (!myrand_is_init()) {
    mysrand(game.seed);
  }
}

/**************************************************************************
...
**************************************************************************/
void srv_init(void)
{
  /* NLS init */
  init_nls();

  /* init server arguments... */

  srvarg.metaserver_no_send = DEFAULT_META_SERVER_NO_SEND;
  sz_strlcpy(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR);

  srvarg.bind_addr = NULL;
  srvarg.port = DEFAULT_SOCK_PORT;

  srvarg.loglevel = LOG_NORMAL;

  srvarg.log_filename = NULL;
  srvarg.gamelog_filename = NULL;
  srvarg.load_filename[0] = '\0';
  srvarg.script_filename = NULL;
  srvarg.saves_pathname = "";

  srvarg.quitidle = 0;
  BV_CLR_ALL(srvarg.draw);

  srvarg.auth_enabled = FALSE;
  srvarg.auth_allow_guests = FALSE;
  srvarg.auth_allow_newusers = FALSE;

  /* initialize teams */
  team_init();

  srvarg.save_ppm = FALSE;

  /* mark as initialized */
  has_been_srv_init = TRUE;

  /* init character encodings. */
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  /* done */
  return;
}

/**************************************************************************
  Returns TRUE if any one game end condition is fulfilled, FALSE otherwise
**************************************************************************/
static bool is_game_over(void)
{
  int barbs = 0, alive = 0, observers = 0;
  bool all_allied;
  struct player *victor = NULL;

  /* quit if we are past the year limit */
  if (game.year > game.end_year) {
    notify_conn_ex(&game.est_connections, NULL, E_GAME_END, 
		   _("Game ended in a draw as end year exceeded"));
    gamelog(GAMELOG_JUDGE, GL_DRAW, 
            "Game ended in a draw as end year exceeded");
    return TRUE;
  }

  /* count barbarians and observers */
  players_iterate(pplayer) {
    if (is_barbarian(pplayer)) {
      barbs++;
    }
    if (pplayer->is_observer) {
      observers++;
    }
  } players_iterate_end;

  /* the game does not quit if we are playing solo */
  if (game.nplayers == (observers + barbs + 1)) {
    return FALSE;
  } 

  /* count the living */
  players_iterate(pplayer) {
    if (pplayer->is_alive && !is_barbarian(pplayer)) {
      alive++;
      victor = pplayer;
    }
  } players_iterate_end;

  /* quit if we have team victory */
  team_iterate(pteam) {
    if (team_count_members_alive(pteam->id) == alive) {
      notify_conn_ex(&game.est_connections, NULL, E_GAME_END,
		     _("Team victory to %s"), pteam->name);
      gamelog(GAMELOG_JUDGE, GL_TEAMWIN, pteam);
      return TRUE;
    }
  } team_iterate_end;

  /* quit if only one player is left alive */
  if (alive == 1) {
    notify_conn_ex(&game.est_connections, NULL, E_GAME_END,
		   _("Game ended in victory for %s"), victor->name);
    gamelog(GAMELOG_JUDGE, GL_LONEWIN, victor);
    return TRUE;
  } else if (alive == 0) {
    notify_conn_ex(&game.est_connections, NULL, E_GAME_END, 
		   _("Game ended in a draw"));
    gamelog(GAMELOG_JUDGE, GL_DRAW);
    return TRUE;
  }

  /* quit if all remaining players are allied to each other */
  all_allied = TRUE;
  players_iterate(pplayer) {
    if (!pplayer->is_alive) {
      continue;
    }
    players_iterate(aplayer) {
      if (!pplayers_allied(pplayer, aplayer) && aplayer->is_alive) {
        all_allied = FALSE;
        break;
      }
    } players_iterate_end;
    if (!all_allied) {
      break;
    }
  } players_iterate_end;
  if (all_allied) {
    notify_conn_ex(&game.est_connections, NULL, E_GAME_END, 
		   _("Game ended in allied victory"));
    gamelog(GAMELOG_JUDGE, GL_ALLIEDWIN);
    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
  Send all information for when game starts or client reconnects.
  Ruleset information should have been sent before this.
**************************************************************************/
void send_all_info(struct conn_list *dest)
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
  Give map information to players with EFT_REVEAL_CITIES or
  EFT_REVEAL_MAP effects (traditionally from the Apollo Program).
**************************************************************************/
static void do_reveal_effects(void)
{
  players_iterate(pplayer) {
    if (get_player_bonus(pplayer, EFT_REVEAL_CITIES) > 0) {
      players_iterate(other_player) {
	city_list_iterate(other_player->cities, pcity) {
	  show_area(pplayer, pcity->tile, 0);
	} city_list_iterate_end;
      } players_iterate_end;
    }
    if (get_player_bonus(pplayer, EFT_REVEAL_MAP) > 0) {
      /* map_know_all will mark all unknown tiles as known and send
       * tile, unit, and city updates as necessary.  No other actions are
       * needed. */
      map_know_all(pplayer);
    }
  } players_iterate_end;
}

/**************************************************************************
  Give contact to players with the EFT_HAVE_EMBASSIES effect (traditionally
  from Marco Polo's Embassy).
**************************************************************************/
static void do_have_embassies_effect(void)
{
  players_iterate(pplayer) {
    if (get_player_bonus(pplayer, EFT_HAVE_EMBASSIES) > 0) {
      players_iterate(pother) {
	/* Note this gives pplayer contact with pother, but doesn't give
	 * pother contact with pplayer.  This may cause problems in other
	 * parts of the code if we're not careful. */
	make_contact(pplayer, pother, NULL);
      } players_iterate_end;
    }
  } players_iterate_end;
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
  whole_map_iterate(ptile) {
    if (map_has_special(ptile, cause)) {
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

      pdiplstate->contact_turns_left =
	  MAX(pdiplstate->contact_turns_left - 1, 0);

      if(pdiplstate->type == DS_CEASEFIRE) {
	switch(--pdiplstate->turns_left) {
	case 1:
	  notify_player(player1,
			_("Game: Concerned citizens point "
  			  "out that the cease-fire with %s will run out soon."),
			player2->name);
  	  break;
  	case 0:
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
    } players_iterate_end;
    player1->reputation = 
      MIN((get_player_bonus(player1, EFT_REGEN_REPUTATION) * 
           GAME_MAX_REPUTATION / 1000) + 
	  player1->reputation + GAME_REPUTATION_INCR,
          GAME_MAX_REPUTATION);
  } players_iterate_end;
}

/**************************************************************************
  Send packet which tells clients that the server is starting its
  "end year" calculations (and will be sending end-turn updates etc).
  (This is referred to as "before new year" in packet and client code.)
**************************************************************************/
static void before_end_year(void)
{
  lsend_packet_before_new_year(&game.est_connections);
}

/**************************************************************************
...
**************************************************************************/
static void ai_start_turn(void)
{
  shuffled_players_iterate(pplayer) {
    if (pplayer->ai.control) {
      ai_do_first_activities(pplayer);
      flush_packets();			/* AIs can be such spammers... */
    }
  } shuffled_players_iterate_end;
  kill_dying_players();
}

/**************************************************************************
Handle the beginning of each turn.
Note: This does not give "time" to any player;
      it is solely for updating turn-dependent data.
**************************************************************************/
static void begin_turn(bool is_new_turn)
{
  freelog(LOG_DEBUG, "Begin turn");

  if (is_new_turn) {
    /* We build scores at the beginning and end of every turn.  We have to
     * build them at the beginning so that the AI can use the data. */
    players_iterate(pplayer) {
      calc_civ_score(pplayer);
    } players_iterate_end;
  }

  /* See if the value of fog of war has changed */
  if (is_new_turn && game.fogofwar != game.fogofwar_old) {
    if (game.fogofwar) {
      enable_fog_of_war();
      game.fogofwar_old = TRUE;
    } else {
      disable_fog_of_war();
      game.fogofwar_old = FALSE;
    }
  }

  if (is_new_turn) {
    freelog(LOG_DEBUG, "Shuffleplayers");
    shuffle_players();
  }

  sanity_check();
}

/**************************************************************************
  Begin a phase of movement.  This handles all beginning-of-phase actions
  for one or more players.
**************************************************************************/
static void begin_phase(bool is_new_phase)
{
  freelog(LOG_DEBUG, "Begin phase");

  conn_list_do_buffer(&game.game_connections);

  players_iterate(pplayer) {
    freelog(LOG_DEBUG, "beginning player turn for #%d (%s)",
	    pplayer->player_no, pplayer->name);
    /* human players also need this for building advice */
    ai_data_turn_init(pplayer);
    ai_manage_buildings(pplayer);
  } players_iterate_end;

  players_iterate(pplayer) {
    send_player_cities(pplayer);
  } players_iterate_end;

  flush_packets();  /* to curb major city spam */
  conn_list_do_unbuffer(&game.game_connections);

  shuffled_players_iterate(pplayer) {
    update_revolution(pplayer);
  } shuffled_players_iterate_end;

  if (is_new_phase) {
    /* Try to avoid hiding events under a diplomacy dialog */
    players_iterate(pplayer) {
      if (pplayer->ai.control && !is_barbarian(pplayer)) {
	ai_diplomacy_actions(pplayer);
      }
    } players_iterate_end;

    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_turn();
  }

  send_start_turn_to_clients();

  sanity_check();
}

/**************************************************************************
  End a phase of movement.  This handles all end-of-phase actions
  for one or more players.
**************************************************************************/
static void end_phase(void)
{
  freelog(LOG_DEBUG, "Endphase");
 
  /* 
   * This empties the client Messages window; put this before
   * everything else below, since otherwise any messages from the
   * following parts get wiped out before the user gets a chance to
   * see them.  --dwp
   */
  before_end_year();
  players_iterate(pplayer) {
    if (pplayer->research.researching == A_UNSET) {
      if (choose_goal_tech(pplayer) == A_UNSET) {
        choose_random_tech(pplayer);
      }
      update_tech(pplayer, 0);
    }
  } players_iterate_end;

  /* Freeze sending of cities. */
  nocity_send = TRUE;

  /* AI end of turn activities */
  auto_settlers_init();
  players_iterate(pplayer) {
    if (pplayer->ai.control) {
      ai_settler_init(pplayer);
    }
    auto_settlers_player(pplayer);
    if (pplayer->ai.control) {
      ai_do_last_activities(pplayer);
    }
  } players_iterate_end;

  /* Refresh cities */
  shuffled_players_iterate(pplayer) {
    do_tech_parasite_effect(pplayer);
    player_restore_units(pplayer);
    update_city_activities(pplayer);
    pplayer->research.changed_from=-1;
    flush_packets();
  } shuffled_players_iterate_end;

  kill_dying_players();

  /* Unfreeze sending of cities. */
  nocity_send = FALSE;
  players_iterate(pplayer) {
    send_player_cities(pplayer);
    ai_data_turn_done(pplayer);
  } players_iterate_end;
  flush_packets();  /* to curb major city spam */

  do_reveal_effects();
  do_have_embassies_effect();

  freelog(LOG_DEBUG, "Auto-Attack phase");
  auto_attack();
}

/**************************************************************************
  Handle the end of each turn.
**************************************************************************/
static void end_turn(void)
{
  freelog(LOG_DEBUG, "Endturn");

  /* Output some ranking and AI debugging info here. */
  if (game.turn % 10 == 0) {
    players_iterate(pplayer) {
      gamelog(GAMELOG_INFO, pplayer);
    } players_iterate_end;
  }

  /* We build scores at the beginning and end of every turn.  We have to
   * build them at the end so that the history report can be built. */
  players_iterate(pplayer) {
    calc_civ_score(pplayer);
  } players_iterate_end;

  freelog(LOG_DEBUG, "Season of native unrests");
  summon_barbarians(); /* wild guess really, no idea where to put it, but
			  I want to give them chance to move their units */

  update_environmental_upset(S_POLLUTION, &game.heating,
			     &game.globalwarming, &game.warminglevel,
			     global_warming);
  update_environmental_upset(S_FALLOUT, &game.cooling,
			     &game.nuclearwinter, &game.coolinglevel,
			     nuclear_winter);
  update_diplomatics();
  make_history_report();
  stdinhand_turn();
  send_player_turn_notifications(NULL);

  freelog(LOG_DEBUG, "Turn ended.");
  game.turn_start = time(NULL);

  freelog(LOG_DEBUG, "Gamenextyear");
  game_advance_year();
  after_game_advance_year();

  freelog(LOG_DEBUG, "Updatetimeout");
  update_timeout();

  check_spaceship_arrivals();

  freelog(LOG_DEBUG, "Sendplayerinfo");
  send_player_info(NULL, NULL);

  freelog(LOG_DEBUG, "Sendgameinfo");
  send_game_info(NULL);

  freelog(LOG_DEBUG, "Sendyeartoclients");
  send_year_to_clients(game.year);
}

/**************************************************************************
  After game advance year stuff.
**************************************************************************/
static void after_game_advance_year(void)
{
  /* Unit end of turn activities */
  shuffled_players_iterate(pplayer) {
    update_unit_activities(pplayer); /* major network traffic */
    flush_packets();
    pplayer->turn_done = FALSE;
  } shuffled_players_iterate_end;
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
  char *dot;
  struct section_file file;
  struct timer *timer_cpu, *timer_user;

  if (!orig_filename) {
    filename[0] = '\0';
  } else {
    sz_strlcpy(filename, orig_filename);
  }

  /* Strip extension. */
  if ((dot = strchr(filename, '.'))) {
    *dot = '\0';
  }

  /* If orig_filename is NULL or empty, use "civgame<year>m". */
  if (filename[0] == '\0'){
    my_snprintf(filename, sizeof(filename),
	"%s%+05dm", game.save_name, game.year);
  }
  
  timer_cpu = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
  timer_user = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    
  section_file_init(&file);
  game_save(&file);

  /* Append ".sav" to filename. */
  sz_strlcat(filename, ".sav");

  if (game.save_compress_level > 0) {
    /* Append ".gz" to filename. */
    sz_strlcat(filename, ".gz");
  }

  if (!path_is_absolute(filename)) {
    char tmpname[600];

    /* Ensure the saves directory exists. */
    make_dir(srvarg.saves_pathname);

    sz_strlcpy(tmpname, srvarg.saves_pathname);
    if (tmpname[0] != '\0') {
      sz_strlcat(tmpname, "/");
    }
    sz_strlcat(tmpname, filename);
    sz_strlcpy(filename, tmpname);
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
void save_game_auto(void)
{
  char filename[512];

  assert(strlen(game.save_name)<256);
  
  my_snprintf(filename, sizeof(filename),
	      "%s%+05d.sav", game.save_name, game.year);
  save_game(filename);
  save_ppm();
  gamelog(GAMELOG_STATUS);
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
}

/**************************************************************************
 Quit the server and exit.
**************************************************************************/
void server_quit(void)
{
  server_game_free();
  close_connections_and_socket();
  exit(EXIT_SUCCESS);
}

/**************************************************************************
...
**************************************************************************/
void handle_report_req(struct connection *pconn, enum report_type type)
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
    report_settable_server_options(pconn, 1);
    break;
  case REPORT_SERVER_OPTIONS2:
    report_settable_server_options(pconn, 2);
    break;
  case REPORT_SERVER_OPTIONS: /* obsolete */
  default:
    notify_conn(dest, _("Game: request for unknown report (type %d)"), type);
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
static bool is_id_allocated(int id)
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

  /* 
   * Old pre-delta clients (before 2003-11-28) send a
   * PACKET_LOGIN_REQUEST (type 0) to the server. We catch this and
   * reply with an old reject packet. Since there is no struct for
   * this old packet anymore we build it by hand.
   */
  if (type == 0) {
    unsigned char buffer[4096];
    struct data_out dout;

    freelog(LOG_ERROR,
	    _("Warning: rejecting old client %s"), conn_description(pconn));

    dio_output_init(&dout, buffer, sizeof(buffer));
    dio_put_uint16(&dout, 0);

    /* 1 == PACKET_LOGIN_REPLY in the old client */
    dio_put_uint8(&dout, 1);

    dio_put_bool32(&dout, FALSE);
    dio_put_string(&dout, _("Your client is too old. To use this server "
			    "please upgrade your client to a "
			    "Freeciv 2.0 or later."));
    dio_put_string(&dout, "");

    {
      size_t size = dio_output_used(&dout);
      dio_output_rewind(&dout);
      dio_put_uint16(&dout, size);

      /* 
       * Use send_connection_data instead of send_packet_data to avoid
       * compression.
       */
      send_connection_data(pconn, buffer, size);
    }

    return FALSE;
  }

  if (type == PACKET_SERVER_JOIN_REQ) {
    return handle_login_request(pconn,
				(struct packet_server_join_req *) packet);
  }

  /* May be received on a non-established connection. */
  if (type == PACKET_AUTHENTICATION_REPLY) {
    return handle_authentication_reply(pconn,
				((struct packet_authentication_reply *)
				 packet)->password);
  }

  if (type == PACKET_CONN_PONG) {
    handle_conn_pong(pconn);
    return TRUE;
  }

  if (!pconn->established) {
    freelog(LOG_ERROR, "Received game packet from unaccepted connection %s",
	    conn_description(pconn));
    return TRUE;
  }
  
  /* valid packets from established connections but non-players */
  if (type == PACKET_CHAT_MSG_REQ) {
    handle_chat_msg_req(pconn,
			((struct packet_chat_msg_req *) packet)->message);
    return TRUE;
  }

  if (type == PACKET_SINGLE_WANT_HACK_REQ) {
    handle_single_want_hack_req(pconn,
		                (struct packet_single_want_hack_req *) packet);
    return TRUE;
  }

  pplayer = pconn->player;

  if(!pplayer) {
    /* don't support these yet */
    freelog(LOG_ERROR, "Received packet from non-player connection %s",
 	    conn_description(pconn));
    return TRUE;
  }

  if (server_state != RUN_GAME_STATE
      && type != PACKET_NATION_SELECT_REQ
      && type != PACKET_CONN_PONG
      && type != PACKET_REPORT_REQ) {
    if (server_state == GAME_OVER_STATE) {
      /* This can happen by accident, so we don't want to print
	 out lots of error messages. Ie, we use LOG_DEBUG. */
      freelog(LOG_DEBUG, "got a packet of type %d "
			  "in GAME_OVER_STATE", type);
    } else {
      freelog(LOG_ERROR, "got a packet of type %d "
	                 "outside RUN_GAME_STATE", type);
    }
    return TRUE;
  }

  pplayer->nturns_idle=0;

  if((!pplayer->is_alive || pconn->observer)
     && !(type == PACKET_REPORT_REQ || type == PACKET_CONN_PONG)) {
    freelog(LOG_ERROR, _("Got a packet of type %d from a "
			 "dead or observer player"), type);
    return TRUE;
  }
  
  /* Make sure to set this back to NULL before leaving this function: */
  pplayer->current_conn = pconn;

  if (!server_handle_packet(type, packet, pplayer, pconn)) {
    freelog(LOG_ERROR, "Received unknown packet %d from %s",
	    type, conn_description(pconn));
  }

  if (server_state == RUN_GAME_STATE) {
    kill_dying_players();
  }

  pplayer->current_conn = NULL;
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void check_for_full_turn_done(void)
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
  Checks if the player name belongs to the default player names of a
  particular player.
**************************************************************************/
static bool is_default_nation_name(const char *name,
				   Nation_Type_id nation_id)
{
  const struct nation_type *nation = get_nation_by_idx(nation_id);

  int choice;

  for (choice = 0; choice < nation->leader_count; choice++) {
    if (mystrcasecmp(name, nation->leaders[choice].name) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**************************************************************************
  Check if this name is allowed for the player.  Fill out the error message
  (a translated string to be sent to the client) if not.
**************************************************************************/
static bool is_allowed_player_name(struct player *pplayer,
				   Nation_Type_id nation,
				   const char *name,
				   char *error_buf, size_t bufsz)
{
  struct connection *pconn = find_conn_by_user(pplayer->username);

  /* An empty name is surely not allowed. */
  if (strlen(name) == 0) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz, _("Please choose a non-blank name."));
    }
    return FALSE;
  }

  /* Any name already taken is not allowed. */
  players_iterate(other_player) {
    if (other_player->nation == nation) {
      if (error_buf) {
	my_snprintf(error_buf, bufsz, _("That nation is already in use."));
      }
      return FALSE;
    } else {
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
	  && mystrcasecmp(other_player->name, name) == 0) {
	if (error_buf) {
	  my_snprintf(error_buf, bufsz,
		      _("Another player already has the name '%s'.  Please "
			"choose another name."), name);
	}
	return FALSE;
      }
    }
  } players_iterate_end;

  /* Any name from the default list is always allowed. */
  if (is_default_nation_name(name, nation)) {
    return TRUE;
  }

  /* To prevent abuse, only players with HACK access (usually local
   * connections) can use non-ascii names.  Otherwise players could use
   * confusing garbage names in multi-player games. */
    /* FIXME: is there a better way to determine if a *player* has hack
     * access? */
  if (!is_ascii_name(name)
      && (!pconn || pconn->access_level != ALLOW_HACK)) {
    if (error_buf) {
      my_snprintf(error_buf, bufsz, _("Please choose a name containing "
				      "only ASCII characters."));
    }
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void handle_nation_select_req(struct player *pplayer,
			      Nation_Type_id nation_no, bool is_male,
			      char *name, int city_style)
{
  int nation_used_count;
  char message[1024];

  if (server_state != SELECT_RACES_STATE) {
    freelog(LOG_ERROR, _("Trying to alloc nation outside "
			 "of SELECT_RACES_STATE!"));
    return;
  }  
  
  /* check sanity of the packet sent by client */
  if (nation_no < 0 || nation_no >= game.nation_count ||
      city_style < 0 || city_style >= game.styles_count ||
      city_styles[city_style].techreq != A_NONE
      || !nations_available[nation_no]) {
    return;
  }

  remove_leading_trailing_spaces(name);

  if (!is_allowed_player_name(pplayer, nation_no, name,
			      message, sizeof(message))) {
    notify_player(pplayer, "%s", message);
    send_select_nation(pplayer);
    return;
  }

  name[0] = my_toupper(name[0]);

  notify_conn_ex(&game.game_connections, NULL, E_NATION_SELECTED,
		 _("Game: %s is the %s ruler %s."), pplayer->username,
		 get_nation_name(nation_no), name);

  /* inform player his choice was ok */
  lsend_packet_nation_select_ok(&pplayer->connections);

  pplayer->nation = nation_no;
  sz_strlcpy(pplayer->name, name);
  pplayer->is_male = is_male;
  pplayer->city_style = city_style;

  /* tell the other players, that the nation is now unavailable */
  nation_used_count = 0;

  players_iterate(other_player) {
    if (other_player->nation == NO_NATION_SELECTED) {
      send_select_nation(other_player);
    } else {
      nation_used_count++;	/* count used nations */
    }
  } players_iterate_end;

  mark_nation_as_used(nation_no);

  /* if there's no nation left, reject remaining players, sorry */
  if( nation_used_count == game.playable_nation_count ) {   /* barb */
    players_iterate(other_player) {
      if (other_player->nation == NO_NATION_SELECTED) {
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
  struct packet_nation_unavailable packet;
  Nation_Type_id nation;

  lsend_packet_select_races(&pplayer->connections);

  for (nation = 0; nation < game.playable_nation_count; nation++) {
    if (!nations_available[nation]) {
      packet.nation = nation;
      lsend_packet_nation_unavailable(&pplayer->connections, &packet);
    }
  }
}

/**************************************************************************
  If all players have chosen the same nation class, return
  this class, otherwise return NULL.
**************************************************************************/  
static char* find_common_class(void) 
{
  char* class = NULL;
  struct nation_type* nation;

  players_iterate(pplayer) {
    if (pplayer->nation == NO_NATION_SELECTED) {
      /* still undecided */
      continue;  
    }
    nation = get_nation_by_idx(pplayer->nation);
    assert(nation->class != NULL);
    if (class == NULL) {
       /* Set the class. */
      class = nation->class;
    } else if (strcmp(nation->class, class) != 0) {
      /* Multiple classes are already being used. */
      return NULL;
    }
  } players_iterate_end;

  return class;
}

/**************************************************************************
  Select a random available nation.  If 'class' is non-NULL, then choose
  a nation from that class if possible.
**************************************************************************/
static Nation_Type_id select_random_nation(const char* class)
{
  Nation_Type_id i, available[game.playable_nation_count];
  int count = 0;

  /* Determine which nations are available. */
  for (i = 0; i < game.playable_nation_count; i++) {
    struct nation_type *nation = get_nation_by_idx(i);

    if (nations_available[i]
	&& (class == NULL || strcmp(nation->class, class) == 0)) {
      available[count] = i;
      count++;
    }
  }

  /* Handle the case where no nations are possible. */
  if (count == 0) {
    if (class) {
      /* Try other classes. */
      return select_random_nation(NULL);
    }

    /* Or else return an invalid value. */
    return NO_NATION_SELECTED;
  }

  /* Then pick one. */
  return available[myrand(count)];
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
   
   When we choose a nation randomly we try to consider only nations
   that are in the same class as nations choosen by other players.
   (I.e., if human player decides to play English, AI won't use Mordorians.)

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
  char* common_class;

  /* Select nations for AI players generated with server
   * 'create <name>' command
   */
  common_class = find_common_class();
  for (i=0; i<game.nplayers; i++) {
    pplayer = &game.players[i];
    
    if (pplayer->nation != NO_NATION_SELECTED) {
      continue;
    }

    /* See if the AI player matches a known leader name. */
    for (nation = 0; nation < game.playable_nation_count; nation++) {
      if (check_nation_leader_name(nation, pplayer->name)
	  && nations_available[nation]) {
	mark_nation_as_used(nation);
	pplayer->nation = nation;
	pplayer->city_style = get_nation_city_style(nation);
	pplayer->is_male = get_nation_leader_sex(nation, pplayer->name);
	break;
      }
    }
    if (pplayer->nation != NO_NATION_SELECTED) {
      continue;
    }

    nation = select_random_nation(common_class);
    if (nation == NO_NATION_SELECTED) {
      freelog(LOG_NORMAL,
	      _("Ran out of nations.  AI controlled player %s not created."),
	      pplayer->name);
      server_remove_player(pplayer); 
      /*
       * Below decrement loop index 'i' so that the loop is redone with
       * the current index (if 'i' is still less than new game.nplayers).
       * This is because subsequent players in list will have been shifted
       * down one spot by the remove, and may need handling.
       */
      i--;  
      continue;
    } else {
      mark_nation_as_used(nation);
      pplayer->nation = nation;
      pplayer->city_style = get_nation_city_style(nation);
      pplayer->is_male = (myrand(2) == 1);
    }

    announce_ai_player(pplayer);
  }
  
  /* We do this again, because user could type:
   * >create Hammurabi
   * >set aifill 5
   * Now we are sure that all AI-players will use historical class
   */
  common_class = find_common_class();

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

  /* we don't want aifill to count global observers unless 
   * aifill = MAX_NUM_PLAYERS */
  i = 0;
  players_iterate(pplayer) {
    if (pplayer->is_observer) {
      i++;
    }
  } players_iterate_end;
  if (game.aifill == MAX_NUM_PLAYERS) {
    i = 0;
  }

  for(;game.nplayers < game.aifill + i;) {
    nation = select_random_nation(common_class);
    assert(nation != NO_NATION_SELECTED);
    mark_nation_as_used(nation);
    pick_ai_player_name(nation, player_name);

    old_nplayers = game.nplayers;
    pplayer = get_player(old_nplayers);
     
    sz_strlcpy(pplayer->name, player_name);
    sz_strlcpy(pplayer->username, ANON_USER_NAME);

    freelog(LOG_NORMAL, _("%s has been added as an AI-controlled player."),
            player_name);
    notify_conn(NULL,
		_("Game: %s has been added as an AI-controlled player."),
		player_name);

    game.nplayers++;

    if (!((game.nplayers == old_nplayers+1)
	  && strcmp(player_name, pplayer->name)==0)) {
      con_write(C_FAIL, _("Error creating new AI player: %s\n"),
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
  (void) send_server_info_to_metaserver(META_INFO);
}

/*************************************************************************
 Used in pick_ai_player_name() below; buf has size at least MAX_LEN_NAME;
*************************************************************************/
static bool good_name(char *ptry, char *buf) {
  if (!(find_player_by_name(ptry) || find_player_by_user(ptry))) {
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
   struct leader *leaders;

   leaders = get_nation_leaders(nation, &names_count);

   /* Try random names (scattershot), then all available,
    * then "Player 1" etc:
    */
   for(i=0; i<names_count; i++) {
     if (good_name(leaders[myrand(names_count)].name, newname)) {
       return;
     }
   }
   
   for(i=0; i<names_count; i++) {
     if (good_name(leaders[i].name, newname)) {
       return;
     }
   }
   
   for(i=1; /**/; i++) {
     char tempname[50];
     my_snprintf(tempname, sizeof(tempname), _("Player %d"), i);
     if (good_name(tempname, newname)) return;
   }
}

/*************************************************************************
  Simply mark the nation as unavailable.
*************************************************************************/
static void mark_nation_as_used (Nation_Type_id nation) 
{
  assert(nations_available[nation]);
  nations_available[nation] = FALSE;
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
  Send PACKET_FREEZE_CLIENT to all clients capable of handling it.
**************************************************************************/
static void freeze_clients(void)
{
  conn_list_iterate(game.game_connections, pconn) {
    if (has_capability("ReportFreezeFix", pconn->capability)) {
      send_packet_freeze_client(pconn);
    }
  } conn_list_iterate_end;
}

/**************************************************************************
  Send PACKET_THAW_CLIENT to all clients capable of handling it.
**************************************************************************/
static void thaw_clients(void)
{
  conn_list_iterate(game.game_connections, pconn) {
    if (has_capability("ReportFreezeFix", pconn->capability)) {
      send_packet_thaw_client(pconn);
    }
  } conn_list_iterate_end;
}

/**************************************************************************
Play the game! Returns when server_state == GAME_OVER_STATE.
**************************************************************************/
static void main_loop(void)
{
  struct timer *eot_timer;	/* time server processing at end-of-turn */
  int save_counter = 0;
  bool is_new_turn = game.is_new_game;

  /* We may as well reset is_new_game now. */
  game.is_new_game = FALSE;

  eot_timer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);

  /* 
   * This will freeze the reports and agents at the client.
   * 
   * Do this before the body so that the PACKET_THAW_CLIENT packet is
   * balanced.
   */
  freeze_clients();

  while(server_state==RUN_GAME_STATE) {
    /* The beginning of a turn.
     *
     * We have to initialize data as well as do some actions.  However when
     * loading a game we don't want to do these actions (like AI unit
     * movement and AI diplomacy). */
    begin_turn(is_new_turn);
    begin_phase(is_new_turn);
    is_new_turn = TRUE;

    force_end_of_sniff = FALSE;

    /* 
     * This will thaw the reports and agents at the client.
     */
    thaw_clients();

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

    sanity_check();

    /* 
     * This will freeze the reports and agents at the client.
     */
    freeze_clients();

    end_phase();
    end_turn();
    freelog(LOG_DEBUG, "Sendinfotometaserver");
    (void) send_server_info_to_metaserver(META_REFRESH);

    conn_list_do_unbuffer(&game.game_connections);

    if (is_game_over()) {
      server_state=GAME_OVER_STATE;
    }
  }

  /* 
   * This will thaw the reports and agents at the client.
   */
  thaw_clients();

  free_timer(eot_timer);
}

/**************************************************************************
  Server initialization.
**************************************************************************/
void srv_main(void)
{
  /* make sure it's initialized */
  if (!has_been_srv_init) {
    srv_init();
  }

  my_init_network();

  con_log_init(srvarg.log_filename, srvarg.loglevel);
  gamelog_init(srvarg.gamelog_filename);
  gamelog_set_level(GAMELOG_FULL);
  gamelog(GAMELOG_BEGIN);
  
#if IS_BETA_VERSION
  con_puts(C_COMMENT, "");
  con_puts(C_COMMENT, beta_message());
  con_puts(C_COMMENT, "");
#endif
  
  con_flush();

  game_init();
  stdinhand_init();
  diplhand_init();

  /* init network */  
  init_connections(); 
  server_open_socket();

  /* load a saved game */
  if (srvarg.load_filename[0] != '\0') {
    (void) load_command(NULL, srvarg.load_filename, FALSE);
  } 

  if(!(srvarg.metaserver_no_send)) {
    freelog(LOG_NORMAL, _("Sending info to metaserver [%s]"),
	    meta_addr_port());
    server_open_meta(); /* open socket for meta server */ 
  }

  (void) send_server_info_to_metaserver(META_INFO);

  /* accept new players, wait for serverop to start..*/
  server_state = PRE_GAME_STATE;

  /* load a script file */
  if (srvarg.script_filename
      && !read_init_script(NULL, srvarg.script_filename)) {
    exit(EXIT_FAILURE);
  }

  /* Run server loop */
  while (TRUE) {
    srv_loop();

    send_game_state(&game.game_connections, CLIENT_GAME_OVER_STATE);
    report_final_scores();
    show_map_to_all();
    notify_conn(NULL, _("Game: The game is over..."));
    gamelog(GAMELOG_JUDGE, GL_NONE);
    send_server_info_to_metaserver(META_INFO);
    if (game.save_nturns > 0) {
      save_game_auto();
    }
    gamelog(GAMELOG_END);

    /* Remain in GAME_OVER_STATE until players log out */
    while (conn_list_size(&game.est_connections) > 0) {
      (void) sniff_packets();
    }

    if (game.timeout == -1 || srvarg.exit_on_end) {
      /* For autogames or if the -e option is specified, exit the server. */
      server_quit();
    }

    /* Reset server */
    server_game_free();
    game_init();
    game.is_new_game = TRUE;
    server_state = PRE_GAME_STATE;
  }

  /* Technically, we won't ever get here. We exit via server_quit. */
}

/**************************************************************************
  Server loop, run to set up one game.
**************************************************************************/
static void srv_loop(void)
{
  int i;
  bool start_nations;

  freelog(LOG_NORMAL, _("Now accepting new client connections."));
  while(server_state == PRE_GAME_STATE) {
    sniff_packets(); /* Accepting commands. */
  }

  (void) send_server_info_to_metaserver(META_INFO);

  if (game.is_new_game) {
    load_rulesets();
    /* otherwise rulesets were loaded when savegame was loaded */
  }

  nations_available
    = fc_realloc(nations_available,
		 game.playable_nation_count * sizeof(*nations_available));

main_start_players:

  send_rulesets(&game.game_connections);

  if (map.num_start_positions > 0) {
    start_nations = TRUE;

    for (i = 0; i < map.num_start_positions; i++) {
      if (map.start_positions[i].nation == NO_NATION_SELECTED) {
	start_nations = FALSE;
	break;
      }
    }
  } else {
    start_nations = FALSE;
  }

  if (start_nations) {
    for (i = 0; i < game.playable_nation_count; i++) {
      nations_available[i] = FALSE;
    }
    for (i = 0; i < map.num_start_positions; i++) {
      nations_available[map.start_positions[i].nation] = TRUE;
    }
    
  } else {
    for (i = 0; i < game.playable_nation_count; i++) {
      nations_available[i] = TRUE;
    }
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
    ai_data_analyze_rulesets(pplayer);
    if (pplayer->is_observer) {
      pplayer->nation = OBSERVER_NATION;
    } else if (pplayer->nation == NO_NATION_SELECTED && !pplayer->ai.control) {
      send_select_nation(pplayer);
      server_state = SELECT_RACES_STATE;
    }
  } players_iterate_end;

  while(server_state == SELECT_RACES_STATE) {
    bool flag = FALSE;

    sniff_packets();

    players_iterate(pplayer) {
      if (pplayer->nation == NO_NATION_SELECTED && !pplayer->ai.control) {
	flag = TRUE;
	break;
      }
    } players_iterate_end;

    if (!flag) {
      if (game.nplayers > 0) {
	server_state = RUN_GAME_STATE;
      } else {
	con_write(C_COMMENT,
		  _("Last player has disconnected: will need to restart."));
	server_state = PRE_GAME_STATE;
	while(server_state == PRE_GAME_STATE) {
	  sniff_packets();
	}
	goto main_start_players;
      }
    }
  }

  init_game_seed();

#ifdef TEST_RANDOM /* not defined anywhere, set it if you want it */
  test_random1(200);
  test_random1(2000);
  test_random1(20000);
  test_random1(200000);
#endif

  if (game.is_new_game) {
    generate_ai_players();
  }
   
  /* If we have a tile map, and map.generator==0, call map_fractal_generate
   * anyway to make the specials, huts and continent numbers. */
  if (map_is_empty() || (map.generator == 0 && game.is_new_game)) {
    map_fractal_generate(TRUE);
  }

  gamelog(GAMELOG_MAP);
  /* start the game */

  server_state = RUN_GAME_STATE;
  (void) send_server_info_to_metaserver(META_INFO);

  if(game.is_new_game) {
    /* Before the player map is allocated (and initiailized)! */
    game.fogofwar_old = game.fogofwar;

    allot_island_improvs();

    players_iterate(pplayer) {
      player_map_allocate(pplayer);
      init_tech(pplayer);
      player_limit_to_government_rates(pplayer);
      pplayer->economic.gold = game.gold;
    } players_iterate_end;
    
    players_iterate(pplayer) {
      int i;
      bool free_techs_already_given = FALSE;

      give_initial_techs(pplayer);
    
      players_iterate(eplayer) {
        if (players_on_same_team(eplayer, pplayer) &&
            eplayer->player_no < pplayer->player_no) {
	  free_techs_already_given = TRUE;
	  break;
        }
      } players_iterate_end;
      
      if (free_techs_already_given) {
        break;
      }
      for (i = 0; i < game.tech; i++) {
        give_random_initial_tech(pplayer);
      }
    } players_iterate_end;
    
    if(game.is_new_game) {
      /* If we're starting a new game, reset the max_players to be the
       * number of players currently in the game.  But when loading a game
       * we don't want to change it. */
      game.max_players = game.nplayers;
    }
  }

  /* Set up alliances based on team selections */
  if (game.is_new_game) {
   players_iterate(pplayer) {
     players_iterate(pdest) {
      if (players_on_same_team(pplayer, pdest)
          && pplayer->player_no != pdest->player_no) {
        pplayer->diplstates[pdest->player_no].type = DS_TEAM;
        give_shared_vision(pplayer, pdest);
        pplayer->embassy |= (1 << pdest->player_no);
      }
    } players_iterate_end;
   } players_iterate_end;
  }

  /* tell the gamelog about the players */
  players_iterate(pplayer) {
    gamelog(GAMELOG_PLAYER, pplayer);
  } players_iterate_end;

  /* tell the gamelog who is whose team */
  team_iterate(pteam) {
    gamelog(GAMELOG_TEAM, pteam);
  } team_iterate_end;

  initialize_move_costs(); /* this may be the wrong place to do this */
  init_settlers(); /* create minimap and other settlers.c data */

  if (!game.is_new_game) {
    players_iterate(pplayer) {
      if (pplayer->ai.control) {
	set_ai_level_direct(pplayer, pplayer->ai.skill_level);
      }
    } players_iterate_end;
  } else {
    players_iterate(pplayer) {
      ai_data_init(pplayer); /* Initialize this at last moment */
    } players_iterate_end;
  }
  
  /* We want to reset the timer as late as possible but before the info is
   * sent to the clients */
  game.turn_start = time(NULL);

  lsend_packet_freeze_hint(&game.game_connections);
  send_all_info(&game.game_connections);
  lsend_packet_thaw_hint(&game.game_connections);
  
  if(game.is_new_game) {
    init_new_game();

    /* give global observers the entire map */
    players_iterate(pplayer) {
      if (pplayer->is_observer) {
        map_know_and_see_all(pplayer);
      }
    } players_iterate_end;
  }

  send_game_state(&game.game_connections, CLIENT_GAME_RUNNING_STATE);

  /*** Where the action is. ***/
  main_loop();
}

/**************************************************************************
 ...
**************************************************************************/
void server_game_free()
{
  players_iterate(pplayer) {
    player_map_free(pplayer);
  } players_iterate_end;
  diplhand_free();
  game_free();
  stdinhand_free();
  ruleset_cache_free();
  BV_CLR_ALL(srvarg.draw);
}
