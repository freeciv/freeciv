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
#include "savegame.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "stdinhand.h"
#include "unitfunc.h"
#include "unithand.h"

#include "advmilitary.h"
#include "aihand.h"

#include "srv_main.h"


static void begin_turn(void);
static void before_end_year(void);
static int end_turn(void);
static void send_all_info(struct conn_list *dest);
static void ai_start_turn(void);
static int is_game_over(void);
static void save_game_auto(void);
static void generate_ai_players(void);
static int mark_nation_as_used(int nation);
static void announce_ai_player(struct player *pplayer);
static void reject_new_player(char *msg, struct connection *pconn);

static void handle_alloc_nation(struct player *pplayer,
				struct packet_alloc_nation *packet);
static void handle_request_join_game(struct connection *pconn, 
				     struct packet_req_join_game *request);
static void handle_turn_done(struct player *pplayer);
static void send_select_nation(struct player *pplayer);
static void enable_fog_of_war_player(struct player *pplayer);
static void disable_fog_of_war_player(struct player *pplayer);
static void enable_fog_of_war(void);
static void disable_fog_of_war(void);
static int check_for_full_turn_done(void);


/* this is used in strange places, and is 'extern'd where
   needed (hence, it is not 'extern'd in srv_main.h) */
int is_server = 1;

/* command-line arguments to server */
struct server_arguments srvarg;

/* server state information */
enum server_states server_state = PRE_GAME_STATE;
int nocity_send = 0;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
int force_end_of_sniff;


/* The next three variables make selecting nations for AI players cleaner */
static int *nations_avail;
static int *nations_used;
static int num_nations_avail;

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
static unsigned short global_id_counter=100;
static unsigned char used_ids[8192]={0};

/* server initialized flag */
static int has_been_srv_init = 0;

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

  *(srvarg.metaserver_servername) = '\0';

  /* mark as initialized */
  has_been_srv_init = 1;

  /* done */
  return;
}

/**************************************************************************
...
**************************************************************************/
void srv_main(void)
{
  int i;
  int save_counter = 0;
  struct timer *eot_timer;	/* time server processing at end-of-turn */

  /* make sure it's initialized */
  if (!has_been_srv_init) {
    srv_init();
  }

  con_log_init(srvarg.log_filename, srvarg.loglevel);
  gamelog_init(srvarg.gamelog_filename);
  gamelog_set_level(GAMELOG_FULL);
  gamelog(GAMELOG_NORMAL,"Starting new log");
  
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

  /* load a script file */

  if (srvarg.script_filename)
    read_init_script(srvarg.script_filename);

  /* load a saved game */
  
  if(srvarg.load_filename) {
    struct timer *loadtimer, *uloadtimer;
    struct section_file file;
    
    freelog(LOG_NORMAL, _("Loading saved game: %s"), srvarg.load_filename);
    loadtimer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
    uloadtimer = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    if(!section_file_load_nodup(&file, srvarg.load_filename)) { 
      freelog(LOG_FATAL, _("Couldn't load savefile: %s"), srvarg.load_filename);
      exit(1);
    }
    game_load(&file);
    section_file_check_unused(&file, srvarg.load_filename);
    section_file_free(&file);

    if (!game.is_new_game) {
      /* Is this right/necessary/the_right_place_for_this/etc? --dwp */
      while(is_id_allocated(global_id_counter++));
    }
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

  send_server_info_to_metaserver(1,0);
  
  /* accept new players, wait for serverop to start..*/
  freelog(LOG_NORMAL, _("Now accepting new client connections."));
  server_state=PRE_GAME_STATE;

  while(server_state==PRE_GAME_STATE)
    sniff_packets();

  send_server_info_to_metaserver(1,0);

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
    for (i=0;i<game.nplayers;i++) {
      struct player *pplayer = get_player(i);
      if (!pplayer->is_connected && !pplayer->ai.control) {
	toggle_ai_player_direct(NULL, pplayer);
      }
    }
  }

  /* Allow players to select a nation (case new game).
   * AI players may not yet have a nation; these will be selected
   * in generate_ai_players() later
   */
  server_state = RUN_GAME_STATE;
  for(i=0; i<game.nplayers; i++) {
    if (game.players[i].nation == MAX_NUM_NATIONS && !game.players[i].ai.control) {
      send_select_nation(&game.players[i]);
      server_state = SELECT_RACES_STATE;
    }
  }

  while(server_state==SELECT_RACES_STATE) {
    sniff_packets();
    for(i=0; i<game.nplayers; i++) {
      if (game.players[i].nation == MAX_NUM_NATIONS && !game.players[i].ai.control) {
	break;
      }
    }
    if(i==game.nplayers) {
      if(i>0) {
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

  /* When to start this timer the first time round is a bit arbitrary:
     here start straight after stop sniffing for select_races and
     before all following server initialization etc.
  */
  eot_timer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);

  if(!game.randseed) {
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
  send_server_info_to_metaserver(1,0);

  if(game.is_new_game) {
    /* Before the player map is allocated (and initiailized)! */
    game.fogofwar_old = game.fogofwar;

    for(i=0; i<game.nplayers; i++) {
      struct player *pplayer = &game.players[i];
      player_map_allocate(pplayer);
      init_tech(pplayer, game.tech);
      player_limit_to_government_rates(pplayer);
      pplayer->economic.gold=game.gold;
    }
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
    for (i=0;i<game.nplayers;i++) {
      struct player *pplayer = get_player(i);
      civ_score(pplayer);	/* if we don't, the AI gets really confused */
      if (pplayer->ai.control) {
	set_ai_level_direct(pplayer, pplayer->ai.skill_level);
	assess_danger_player(pplayer); /* a slowdown, but a necessary one */
      }
    }
  }
  
  send_all_info(&game.game_connections);
  
  if(game.is_new_game)
    init_new_game();

  game.is_new_game = 0;

  send_game_state(&game.est_connections, CLIENT_GAME_RUNNING_STATE);

  while(server_state==RUN_GAME_STATE) {
    /* absolute beginning of a turn */
    freelog(LOG_DEBUG, "Begin turn");
    begin_turn();

    force_end_of_sniff=0;

    freelog(LOG_DEBUG, "Shuffleplayers");
    shuffle_players();
    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_turn();

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
    while(sniff_packets()==1);

    /* After sniff, re-zero the timer: (read-out above on next loop) */
    clear_timer_start(eot_timer);
    
    conn_list_do_buffer(&game.game_connections);

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
    check_spaceship_arrivals();
    freelog(LOG_DEBUG, "Sendplayerinfo");
    send_player_info(0, 0);
    freelog(LOG_DEBUG, "Sendgameinfo");
    send_game_info(0);
    freelog(LOG_DEBUG, "Sendyeartoclients");
    send_year_to_clients(game.year);
    freelog(LOG_DEBUG, "Sendinfotometaserver");
    send_server_info_to_metaserver(0,0);

    conn_list_do_unbuffer(&game.game_connections);

    if (game.year>game.end_year || is_game_over()) 
      server_state=GAME_OVER_STATE;
  }

  report_scores(1);
  show_map_to_all();
  notify_player(0, _("Game: The game is over..."));

  while(server_state==GAME_OVER_STATE) {
    force_end_of_sniff=0;
    sniff_packets();
  }

  server_close_udp();

  return;
}

/**************************************************************************
...
**************************************************************************/
static int is_game_over(void)
{
  int barbs = 0;
  int alive = 0;
  int i;

  for (i=0;i<game.nplayers; i++) {
    if (is_barbarian(&(game.players[i])))
      barbs++;
  }
  if (game.nplayers == (barbs + 1))
    return 0;

  for (i=0;i<game.nplayers; i++) {
    if (game.players[i].is_alive && !(is_barbarian(&(game.players[i]))))
      alive++;
  }
  return (alive <= 1);
}

/**************************************************************************
  Send all information for when game starts or client reconnects.
  Ruleset information should have been sent before this.
**************************************************************************/
static void send_all_info(struct conn_list *dest)
{
  send_game_info(dest);
  send_map_info(dest);
  send_player_info_c(0, dest);
  send_conn_info(&game.est_connections, dest);
  send_spaceship_info(0, dest);
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
  int cityid;
  int i;
  struct player *pplayer;
  struct city *pcity;
  if ((cityid=game.global_wonders[B_APOLLO]) && 
      (pcity=find_city_by_id(cityid))) {
    pplayer=city_owner(pcity);
    if (game.civstyle == 1) {
      for(i=0; i<game.nplayers; i++) {
	city_list_iterate(game.players[i].cities, pcity)
	  show_area(pplayer, pcity->x, pcity->y, 0);
	city_list_iterate_end;
      }
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
  int cityid  = game.global_wonders[B_MARCO];
  int o;
  struct city *pcity;
  if (cityid && (pcity = find_city_by_id(cityid)))
    for (o = 0; o < game.nplayers; o++)
      make_contact(pcity->owner, o, pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
static void update_environmental_upset(enum tile_special_type cause,
				       int *current, int *accum, int *level,
				       void (*upset_action_fn)(int))
{
  int x, y, count;

  count = 0;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      if (map_get_special(x,y) & cause) {
	count++;
      }
    }
  }

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
      send_all_known_tiles(&game.game_connections);
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
  int p, p2;

  for(p = 0; p < game.nplayers; p++) {
    for(p2 = 0; p2 < game.nplayers; p2++) {
      game.players[p].diplstates[p2].has_reason_to_cancel =
	MAX(game.players[p].diplstates[p2].has_reason_to_cancel - 1, 0);

      if(game.players[p].diplstates[p2].type == DS_CEASEFIRE) {
	switch(--game.players[p].diplstates[p2].turns_left) {
	case 1:
	  notify_player(&game.players[p],
			_("Game: Concerned citizens point "
			  "out that the cease-fire with %s will run out soon."),
			game.players[p2].name);
	  break;
	case -1:
	  notify_player(&game.players[p],
			_("Game: The cease-fire with %s has "
			  "run out. You are now neutral towards the %s."),
			game.players[p2].name,
			get_nation_name_plural(game.players[p2].nation));
	  game.players[p].diplstates[p2].type = DS_NEUTRAL;
	  break;
	}
      }
      game.players[p].reputation =
	MIN(game.players[p].reputation + GAME_REPUTATION_INCR,
	    GAME_MAX_REPUTATION);
    }
  }
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
  int i;

  for (i = 0; i < game.nplayers; i++) {
    struct player *pplayer = shuffled_player(i);
    if (pplayer->ai.control) 
      ai_do_first_activities(pplayer);
  }
}

/**************************************************************************
Handle the beginning of each turn.
Note: This does not give "time" to any player;
      it is solely for updating turn-dependent data.
**************************************************************************/
static void begin_turn(void)
{
  int i;

  /* See if the value of fog of war has changed */
  if (game.fogofwar != game.fogofwar_old) {
    if (game.fogofwar == 1) {
      enable_fog_of_war();
      game.fogofwar_old = 1;
    } else {
      disable_fog_of_war();
      game.fogofwar_old = 0;
    }
  }

  conn_list_do_buffer(&game.game_connections);

  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = &game.players[i];
    freelog(LOG_DEBUG, "beginning player turn for #%d (%s)",
	    i, pplayer->name);
    begin_player_turn(pplayer);
  }
  for (i=0; i<game.nplayers;i++) {
    send_player_cities(&game.players[i]);
  }

  conn_list_do_unbuffer(&game.game_connections);
}

/**************************************************************************
...
**************************************************************************/
static int end_turn(void)
{
  int i;

  nocity_send = 1;
  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = shuffled_player(i);
    freelog(LOG_DEBUG, "updating player activities for #%d (%s)",
		  i, pplayer->name);
    update_player_activities(pplayer);
         /* ai unit activity has been moved UP -- Syela */
    pplayer->turn_done=0;
  }
  nocity_send = 0;
  for (i=0; i<game.nplayers;i++) {
    send_player_cities(&game.players[i]);
  }
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
  return 1;
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

  sz_strlcpy(filename, orig_filename);
  
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
	      "%s%d.sav", game.save_name, game.year);
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
  force_end_of_sniff=1;
  game.turn_start = time(NULL);
}

/**************************************************************************
...
**************************************************************************/
static void handle_report_request(struct connection *pconn,
				  enum report_type type)
{
  struct conn_list *dest = &pconn->self;
  
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
int is_id_allocated(int id)
{
  return (used_ids[id/8])&(1<<(id%8));
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
  while(is_id_allocated(++global_id_counter) || !global_id_counter) ;
  return global_id_counter;
}

/**************************************************************************
...
**************************************************************************/
void handle_packet_input(struct connection *pconn, char *packet, int type)
{
  struct player *pplayer;

  switch(type) {
  case PACKET_REQUEST_JOIN_GAME:
    handle_request_join_game(pconn, (struct packet_req_join_game *)packet);
    free(packet);
    return;
    break;
  }

  if (!pconn->established) {
    freelog(LOG_ERROR, "Received game packet from unaccepted connection %s",
	    conn_description(pconn));
    free(packet);
    return;
  }
  
  pplayer = pconn->player;

  if(pplayer == NULL) {
    /* don't support these yet */
    freelog(LOG_ERROR, "Received packet from non-player connection %s",
 	    conn_description(pconn));
    free(packet);
    return;
  }

  pplayer->nturns_idle=0;

  if((!pplayer->is_alive || pconn->observer)
     && !(type == PACKET_CHAT_MSG || type == PACKET_REPORT_REQUEST)) {
    free(packet);
    return;
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

  case PACKET_PLAYER_WORKLIST:
    handle_player_worklist(pplayer, (struct packet_player_request *)packet);
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
    handle_unit_establish_trade(pplayer, (struct packet_unit_request *)packet);
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
  default:
    freelog(LOG_ERROR, "Received unknown packet %d from %s",
	    type, conn_description(pconn));
  }

  free(packet);
  pplayer->current_conn = NULL;
}

/**************************************************************************
...
**************************************************************************/
static int check_for_full_turn_done(void)
{
  int i;

  /* fixedlength is only applicable if we have a timeout set */
  if (game.fixedlength && game.timeout)
    return 0;
  for(i=0; i<game.nplayers; i++) {
    struct player *pplayer = &game.players[i];
    if (game.turnblock) {
      if (!pplayer->ai.control && pplayer->is_alive && !pplayer->turn_done)
        return 0;
    } else {
      if(pplayer->is_connected && pplayer->is_alive && !pplayer->turn_done) {
        return 0;
      }
    }
  }
  force_end_of_sniff=1;
  return 1;
}

/**************************************************************************
...
(Hmm, how should "turn done" work for multi-connected non-observer players?)
**************************************************************************/
static void handle_turn_done(struct player *pplayer)
{
  pplayer->turn_done=1;

  check_for_full_turn_done();

  send_player_info(pplayer, 0);
}

/**************************************************************************
...
**************************************************************************/
static void handle_alloc_nation(struct player *pplayer,
				struct packet_alloc_nation *packet)
{
  int i, nation_used_count;
  struct packet_generic_values select_nation; 

  remove_leading_trailing_spaces(packet->name);

  if (strlen(packet->name)==0) {
    notify_player(pplayer, _("Please choose a non-blank name."));
    send_select_nation(pplayer);
    return;
  }
  
  for(i=0; i<game.nplayers; i++)
    if(game.players[i].nation==packet->nation_no) {
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
      if (i != pplayer->player_no
	  && mystrcasecmp(game.players[i].name,packet->name) == 0) {
	notify_player(pplayer,
		     _("Another player already has the name '%s'.  "
		       "Please choose another name."), packet->name);
       send_select_nation(pplayer);
       return;
    }

  freelog(LOG_NORMAL, _("%s is the %s ruler %s."), pplayer->name, 
      get_nation_name(packet->nation_no), packet->name);

  /* inform player his choice was ok */
  select_nation.value2=0xffff;
  lsend_packet_generic_values(&pplayer->connections, PACKET_SELECT_NATION,
			      &select_nation);

  pplayer->nation=packet->nation_no;
  sz_strlcpy(pplayer->name, packet->name);
  pplayer->is_male=packet->is_male;
  pplayer->city_style=packet->city_style;

  /* tell the other players, that the nation is now unavailable */
  nation_used_count = 0;
  for(i=0; i<game.nplayers; i++)
    if(game.players[i].nation==MAX_NUM_NATIONS) 
      send_select_nation(&game.players[i]);
    else
      nation_used_count++;                     /* count used nations */

  mark_nation_as_used(packet->nation_no);

  /* if there's no nation left, reject remaining players, sorry */
  if( nation_used_count == game.playable_nation_count ) {   /* barb */
    for(i=0; i<game.nplayers; i++) {
      if( game.players[i].nation == MAX_NUM_NATIONS ) {
	freelog(LOG_NORMAL, _("No nations left: Removing player %s."),
		game.players[i].name);
	notify_player(&game.players[i],
		      _("Game: Sorry, there are no nations left."));
	server_remove_player(&game.players[i]);
      }
    }
  }
}

/**************************************************************************
Send request to select nation, mask1, mask2 contains those that were
already selected and are not available
**************************************************************************/
static void send_select_nation(struct player *pplayer)
{
  int i;
  struct packet_generic_values select_nation;
                                           
  select_nation.value1=0;                     /* assume int is 32 bit, safe */
  select_nation.value2=0;

  /* set bits in mask corresponding to nations already selected by others */
  for( i=0; i<game.nplayers; i++)
    if(game.players[i].nation != MAX_NUM_NATIONS) {
      if( game.players[i].nation < 32)
        select_nation.value1 |= 1<<game.players[i].nation;
      else
        select_nation.value2 |= 1<<(game.players[i].nation-32);
    }

  lsend_packet_generic_values(&pplayer->connections, PACKET_SELECT_NATION,
			      &select_nation);
}

/**************************************************************************
 Send a join_game reply, accepting the connection.
 pconn->player should be set and non-NULL.
 'rejoin' is whether this is a new player, or re-connection.
 Prints a log message, and notifies other players.
**************************************************************************/
static void join_game_accept(struct connection *pconn, int rejoin)
{
  struct packet_join_game_reply packet;
  struct player *pplayer = pconn->player;

  assert(pplayer);
  packet.you_can_join = 1;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message),
	      "%s %s.", (rejoin?"Welcome back":"Welcome"), pplayer->name);
  send_packet_join_game_reply(pconn, &packet);
  pconn->established = 1;
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
  struct player *cplayer;
  int i;

  if (pconn == NULL) {
    return;
  }
  dest = &pconn->self;
  
  if (gethostname(hostname, 512)==0) {
    notify_conn(dest, _("Welcome to the %s Server running at %s."), 
		FREECIV_NAME_VERSION, hostname);
  } else {
    notify_conn(dest, _("Welcome to the %s Server."),
		FREECIV_NAME_VERSION);
  }

  /* tell who we're waiting on to end the game turn */
  if (game.turnblock) {
    for(i=0; i<game.nplayers;++i) {
      cplayer = &game.players[i];
      if (cplayer->is_alive
	  && !cplayer->ai.control
	  && !cplayer->turn_done
	  && cplayer != pconn->player) {  /* skip current player */
	notify_conn(dest, _("Turn-blocking game play: "
			    "waiting on %s to finish turn..."),
		    cplayer->name);
      }
    }
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

  server_player_init(pplayer, 0);
  /* sometimes needed if players connect/disconnect to avoid
   * inheriting stale AI status etc
   */
  
  sz_strlcpy(pplayer->name, name);
  sz_strlcpy(pplayer->username, name);

  if (pconn) {
    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, 0);
  } else {
    freelog(LOG_NORMAL, _("%s has been added as an AI-controlled player."),
	    name);
    notify_player(0, _("Game: %s has been added as an AI-controlled player."),
		  name);
  }

  game.nplayers++;
  if(server_state==PRE_GAME_STATE && game.max_players==game.nplayers)
    server_state=SELECT_RACES_STATE;

  introduce_game_to_connection(pconn);
  send_server_info_to_metaserver(1,0);
}

/**************************************************************************
...
**************************************************************************/
static void reject_new_player(char *msg, struct connection *pconn)
{
  struct packet_join_game_reply packet;
  
  packet.you_can_join=0;
  sz_strlcpy(packet.capability, our_capability);
  sz_strlcpy(packet.message, msg);
  send_packet_join_game_reply(pconn, &packet);
}
  
/**************************************************************************
  Try a single name as new connection name for set_unique_conn_name().
  If name is ok, copy to pconn->name and return 1, else return 0.
**************************************************************************/
static int try_conn_name(struct connection *pconn, const char *name)
{
  char save_name[MAX_LEN_NAME];

  /* avoid matching current name in find_conn_by_name() */
  sz_strlcpy(save_name, pconn->name);
  pconn->name[0] = '\0';
  
  if (!find_conn_by_name(name)) {
    sz_strlcpy(pconn->name, name);
    return 1;
  } else {
    sz_strlcpy(pconn->name, save_name);
    return 0;
  }
}

/**************************************************************************
  Set pconn->name based on reqested name req_name: either use req_name,
  if no other connection has that name, else a modified name based on
  req_name (req_name prefixed by digit and dash).
  Returns 0 if req_name used unchanged, else 1.
**************************************************************************/
static int set_unique_conn_name(struct connection *pconn, const char *req_name)
{
  char adjusted_name[MAX_LEN_NAME];
  int i;
  
  if (try_conn_name(pconn, req_name)) {
    return 0;
  }
  for(i=1; i<10000; i++) {
    my_snprintf(adjusted_name, sizeof(adjusted_name), "%d-%s", i, req_name);
    if (try_conn_name(pconn, adjusted_name)) {
      return 1;
    }
  }
  /* This should never happen */
  freelog(LOG_ERROR, "Too many failures in set_unique_conn_name(%s,%s)",
	  pconn->name, req_name);
  sz_strlcpy(pconn->name, req_name);
  return 0;
}

/**************************************************************************
...
**************************************************************************/
static void handle_request_join_game(struct connection *pconn, 
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
    close_connection(pconn);
    return;
  }

  if (set_unique_conn_name(pconn, req->name)==0) {
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
    close_connection(pconn);
    return;
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
    close_connection(pconn);
    return;
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
	close_connection(pconn);
	return;
      }
    } else if(!pplayer->is_alive) {
      if(!(allow = strchr(game.allow_connect, 'd'))) {
	reject_new_player(_("Sorry, no observation of dead players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No observation of dead players."),
		pconn->name);
	close_connection(pconn);
	return;
      }
    } else if(pplayer->ai.control) {
      if(!(allow = strchr(game.allow_connect, (game.is_new_game ? 'A' : 'a')))) {
	reject_new_player(_("Sorry, no observation of AI players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No observation of AI players."),
		pconn->name);
	close_connection(pconn);
	return;
      }
    } else {
      if(!(allow = strchr(game.allow_connect, (game.is_new_game ? 'H' : 'h')))) {
	reject_new_player(_("Sorry, no reconnections to human-mode players "
			    "in this game."), pconn);
	freelog(LOG_NORMAL,
		_("%s was rejected: No reconnection to human players."),
		pconn->name);
	close_connection(pconn);
	return;
      }
    }

    allow++;
    if (conn_list_size(&pplayer->connections) > 0
	&& !(*allow == '*' || *allow == '+')) {

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
	close_connection(pconn);
      
	return;
      }
      else {
	my_snprintf(msg, sizeof(msg), _("Sorry, %s is already connected."), 
		    pplayer->name);
	reject_new_player(msg, pconn);
	freelog(LOG_NORMAL, _("%s was rejected: %s already connected."),
		pconn->name, pplayer->name);
	close_connection(pconn);
	return;
      }
    }

    /* The player exists and not connected or multi-conn allowed: */
    if (pplayer->is_connected && (*allow != '*')) {
      pconn->observer = 1;
    }
    associate_player_connection(pplayer, pconn);
    join_game_accept(pconn, 1);
    introduce_game_to_connection(pconn);
    if(server_state==RUN_GAME_STATE) {
      send_rulesets(&pconn->self);
      send_all_info(&pconn->self);
      send_game_state(&pconn->self, CLIENT_GAME_RUNNING_STATE);
      send_player_info(NULL,NULL);
      send_diplomatic_meetings(pconn);
    }
    if (game.auto_ai_toggle && pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    return;
  }

  /* unknown name */

  if(server_state!=PRE_GAME_STATE) {
    reject_new_player(_("Sorry, the game is already running."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Game running and unknown name %s."),
	    pconn->name, req->name);
    close_connection(pconn);

    return;
  }

  if(game.nplayers==game.max_players) {
    reject_new_player(_("Sorry, the game is full."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: Maximum number of players reached."),
	    pconn->name);    
    close_connection(pconn);

    return;
  }

  if(!(allow = strchr(game.allow_connect, 'N'))) {
    reject_new_player(_("Sorry, no new players allowed in this game."), pconn);
    freelog(LOG_NORMAL, _("%s was rejected: No connections as new player."),
	    pconn->name);
    close_connection(pconn);
    return;
  }
  
  accept_new_player(req->name, pconn);
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
  notify_conn(&game.est_connections, _("Game: Lost connection: %s."), desc);
  
  if (pplayer == NULL) {
    /* This happens eg if the player has not yet joined properly. */
    return;
  }
  
  unassociate_player_connection(pplayer, pconn);

  /* Remove from lists so this conn is not included in broadcasts.
   * safe to do these even if not in lists:
   */
  conn_list_unlink(&game.all_connections, pconn);
  conn_list_unlink(&game.game_connections, pconn);
  pconn->established = 0;
  
  send_conn_info_remove(&pconn->self, &game.est_connections);
  send_player_info(pplayer, 0);

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
      pplayer->is_male = myrand(2);
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
    pplayer->ai.control = 1;
    pplayer->ai.skill_level = game.skill_level;
    if (check_nation_leader_name(nation, player_name)) {
      pplayer->is_male = get_nation_leader_sex(nation, player_name);
    } else {
      pplayer->is_male = myrand(2);
    }
    announce_ai_player(pplayer);
    set_ai_level_direct(pplayer, pplayer->ai.skill_level);
  }
  send_server_info_to_metaserver(1, 0);
}

/*************************************************************************
 Used in pick_ai_player_name() below; buf has size at least MAX_LEN_NAME;
*************************************************************************/
static int good_name(char *ptry, char *buf) {
  if (!find_player_by_name(ptry)) {
     mystrlcpy(buf, ptry, MAX_LEN_NAME);
     return 1;
  }
  return 0;
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
      exit(1);
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
   int i;

   freelog(LOG_NORMAL, _("AI is controlling the %s ruled by %s."),
                    get_nation_name_plural(pplayer->nation),
                    pplayer->name);

   for(i=0; i<game.nplayers; ++i)
     notify_player(&game.players[i],
  	     _("Game: %s rules the %s."), pplayer->name,
                    get_nation_name_plural(pplayer->nation));

}

/*************************************************************************
...
*************************************************************************/
static void enable_fog_of_war_player(struct player *pplayer)
{
  int x,y;
  int playerid = pplayer->player_no;
  struct tile *ptile;
  for (x = 0; x < map.xsize; x++)
    for (y = 0; y < map.ysize; y++) {
      ptile = map_get_tile(x,y);
      map_change_seen(x, y, playerid, -1);
      if (map_get_seen(x, y, playerid) == 0)
	update_player_tile_last_seen(pplayer, x, y);
    }
  send_all_known_tiles(&pplayer->connections);
}

/*************************************************************************
...
*************************************************************************/
static void enable_fog_of_war(void)
{
  int o;
  for (o = 0; o < game.nplayers; o++)
    enable_fog_of_war_player(&game.players[o]);
}

/*************************************************************************
...
*************************************************************************/
static void disable_fog_of_war_player(struct player *pplayer)
{
  int x,y;
  int playerid = pplayer->player_no;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      map_change_seen(x, y, playerid, +1);
      if (map_get_known(x, y, pplayer)) {
	struct city *pcity = map_get_city(x,y);
	reality_check_city(pplayer, x, y);
	if (pcity)
	  update_dumb_city(pplayer, pcity);
      }
    }
  }
  send_all_known_tiles(&pplayer->connections);
  send_all_known_units(&pplayer->connections);
  send_all_known_cities(&pplayer->connections);
}

/*************************************************************************
...
*************************************************************************/
static void disable_fog_of_war(void)
{
  int o;
  for (o = 0; o < game.nplayers; o++)
    disable_fog_of_war_player(&game.players[o]);
}
