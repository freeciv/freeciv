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

#ifdef GENERATING_MAC  /* mac header(s) */
#include <Dialogs.h>
#include <Controls.h>
#include <bool.h> /*from STL, but works w/ c*/
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
#include "ruleset.h"
#include "sernet.h"
#include "settlers.h"
#include "spacerace.h"
#include "stdinhand.h"
#include "unitfunc.h"
#include "unithand.h"

#include "advmilitary.h"
#include "aihand.h"

#include "civserver.h"

/* main() belongs at the bottom of files!! -- Syela */
static void before_end_year(void);
static int end_turn(void);
static void send_all_info(struct player *dest);
static void shuffle_players(void);
static void ai_start_turn(void);
static int is_game_over(void);
static void save_game_auto(void);
static void generate_ai_players(void);
static int mark_nation_as_used(int nation);
static void announce_ai_player(struct player *pplayer);
static void reject_new_player(char *msg, struct connection *pconn);

static void handle_alloc_nation(int player_no, struct packet_alloc_nation *packet);
static void handle_request_join_game(struct connection *pconn, 
				     struct packet_req_join_game *request);
static void handle_turn_done(int player_no);
static void send_select_nation(struct player *pplayer);
static void enable_fog_of_war_player(struct player *pplayer);
static void disable_fog_of_war_player(struct player *pplayer);
static void enable_fog_of_war(void);
static void disable_fog_of_war(void);

#ifdef GENERATING_MAC
static void Mac_options(int *argc, char *argv[]);
#endif

extern struct connection connections[];

enum server_states server_state;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
int force_end_of_sniff;
char metaserver_info_line[256];
char metaserver_addr[256];
unsigned short int metaserver_port;

/* server name for metaserver to use for us */
char metaserver_servername[64]="";

struct player *shuffled[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
unsigned short global_id_counter=100;
unsigned char used_ids[8192]={0};
int is_server = 1;

/* The following is unused, AFAICT.  --dwp */
char usage[] = 
"Usage: %s [-fhlpgv] [--file] [--help] [--log] [--port]\n\t[--gamelog] [--version]\n";

int port=DEFAULT_SOCK_PORT;
int nocity_send=0;

/* The next three variables make selecting nations for AI players cleaner */
int *nations_avail;
int *nations_used;
int num_nations_avail;

/**************************************************************************
...
**************************************************************************/
int main(int argc, char *argv[])
{
  int h=0, v=0, no_meta=1;
  int xitr, yitr,i,notfog;
  char *log_filename=NULL;
  char *gamelog_filename=NULL;
  char *load_filename=NULL;
  char *script_filename=NULL;
  char *option=NULL;
  int save_counter=0;
  int loglevel=LOG_NORMAL;
  struct timer *eot_timer;	/* time server processing at end-of-turn */

  init_nls();
  dont_run_as_root(argv[0], "freeciv_server");

  sz_strlcpy(metaserver_info_line, DEFAULT_META_SERVER_INFO_STRING);
  sz_strlcpy(metaserver_addr, DEFAULT_META_SERVER_ADDR);
  metaserver_port = DEFAULT_META_SERVER_PORT;

#ifdef GENERATING_MAC
  Mac_options(&argc, argv);
#endif	
  /* no  we don't use GNU's getopt or even the "standard" getopt */
  /* yes we do have reasons ;)                                   */
  i=1;
  while(i<argc) {
    if ((option = get_option("--file",argv,&i,argc)) != NULL)
	load_filename=option;
    else if (is_option("--help", argv[i])) {
      h=1;
      break;
    }
    else if ((option = get_option("--log",argv,&i,argc)) != NULL)
	log_filename=option;
    else if ((option = get_option("--gamelog",argv,&i,argc)) != NULL)
        gamelog_filename=option;
    else if(is_option("--nometa", argv[i])) { 
      fprintf(stderr, _("Warning: the %s option is obsolete.  "
	              "Use -m to enable the metaserver.\n"), argv[i]);
      h=1;
    }
    else if(is_option("--meta", argv[i]))
        no_meta=0;
    else if ((option = get_option("--Metaserver",argv,&i,argc)) != NULL)
    {
	sz_strlcpy(metaserver_addr, argv[i]);
	meta_addr_split();

	no_meta = 0; /* implies --meta */
    }
    else if ((option = get_option("--port",argv,&i,argc)) != NULL)
	port=atoi(option);
    else if ((option = get_option("--read",argv,&i,argc)) != NULL)
	script_filename=option;
    else if ((option = get_option("--server",argv,&i,argc)) != NULL)
      	sz_strlcpy(metaserver_servername, option);
    else if ((option = get_option("--debug",argv,&i,argc)) != NULL)
      {
	loglevel = log_parse_level_str(option);
	if (loglevel == -1) {
	  loglevel = LOG_NORMAL;
	  h=1;
	  break;
	}
      }
    else if(is_option("--version",argv[i])) 
      v=1;
    else {
      fprintf(stderr, _("Error: unknown option '%s'\n"), argv[i]);
      h=1;
      break;
    }
    i++;
  }
  
  if(v && !h) {
    fprintf(stderr, FREECIV_NAME_VERSION "\n");
    exit(0);
  }

  con_write(C_VERSION, _("This is the server for %s"), FREECIV_NAME_VERSION);
  con_write(C_COMMENT, _("You can learn a lot about Freeciv at %s"),
	    WEBSITE_URL);

  if(h) {
    fprintf(stderr, _("Usage: %s [option ...]\nValid options are:\n"), argv[0]);
    fprintf(stderr, _("  -h, --help\t\tPrint a summary of the options\n"));
    fprintf(stderr, _("  -r, --read FILE\tRead startup script FILE\n"));
    fprintf(stderr, _("  -f, --file FILE\tLoad saved game FILE\n"));
    fprintf(stderr, _("  -p, --port PORT\tListen for clients on port PORT\n"));
    fprintf(stderr, _("  -g, --gamelog FILE\tUse FILE as game logfile\n"));
    fprintf(stderr, _("  -l, --log FILE\tUse FILE as logfile\n"));
    fprintf(stderr, _("  -m, --meta\t\tSend info to metaserver\n"));
    fprintf(stderr, _("  -M, --Metaserver ADDR\tSet ADDR as metaserver address\n"));
    fprintf(stderr, _("  -s, --server HOST\tList this server as host HOST\n"));
#ifdef DEBUG
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0,1,2,3,"
	                                "or 3:file1,min,max:...)\n"));
#else
    fprintf(stderr, _("  -d, --debug NUM\tSet debug log level (0,1,2)\n"));
#endif
    fprintf(stderr, _("  -v, --version\t\tPrint the version number\n"));
    fprintf(stderr, _("Report bugs to <%s>.\n"), BUG_EMAIL_ADDRESS);
    exit(0);
  }

  con_log_init(log_filename, loglevel);
  gamelog_init(gamelog_filename);
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

  /* load a saved game */
  
  if(load_filename) {
    struct timer *loadtimer, *uloadtimer;
    struct section_file file;
    
    freelog(LOG_NORMAL, _("Loading saved game: %s"), load_filename);
    loadtimer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
    uloadtimer = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    if(!section_file_load_nodup(&file, load_filename)) { 
      freelog(LOG_FATAL, _("Couldn't load savefile: %s"), load_filename);
      exit(1);
    }
    game_load(&file);
    section_file_check_unused(&file, load_filename);
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
  if(no_meta==0) {
    freelog(LOG_NORMAL, _("Sending info to metaserver [%s]"),
	    meta_addr_port());
    server_open_udp(); /* open socket for meta server */ 
  }

  send_server_info_to_metaserver(1,0);
  
  /* accept new players, wait for serverop to start..*/
  freelog(LOG_NORMAL, _("Now accepting new client connections."));
  server_state=PRE_GAME_STATE;

  if (script_filename)
    read_init_script(script_filename);

  while(server_state==PRE_GAME_STATE)
    sniff_packets();

  send_server_info_to_metaserver(1,0);

  if(game.is_new_game) {
    load_rulesets();
    /* otherwise rulesets were loaded when savegame was loaded */
  }

  nations_avail = fc_calloc( game.playable_nation_count, sizeof(int));
  nations_used = fc_calloc( game.playable_nation_count, sizeof(int));

main_start_players:

  send_rulesets(0);

  num_nations_avail = game.playable_nation_count;
  for(i=0; i<game.playable_nation_count; i++) {
      nations_avail[i]=i;
      nations_used[i]=i;
  }

  if (game.auto_ai_toggle) {
    for (i=0;i<game.nplayers;i++) {
      struct player *pplayer = get_player(i);
      if (!pplayer->conn && !pplayer->ai.control) {
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
  
  /* Initialize fog of war. */
  notfog = !game.fogofwar;
  for(i=0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++)
    for (xitr = 0; xitr < map.xsize; xitr++)
      for (yitr = 0; yitr < map.ysize; yitr++)
	map_get_tile(xitr,yitr)->seen[i] += notfog;
  game.fogofwar_old = game.fogofwar;
  
  send_all_info(0);
  
  if(game.is_new_game)
    init_new_game();

  game.is_new_game = 0;

  send_game_state(0, CLIENT_GAME_RUNNING_STATE);

  while(server_state==RUN_GAME_STATE) {
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

    force_end_of_sniff=0;
    freelog(LOG_DEBUG, "Shuffleplayers");
    shuffle_players();
    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_turn();

    /* Before sniff (human player activites), report time to now: */
    freelog(LOG_VERBOSE, "End/start-turn server/ai activities: %g seconds",
	    read_timer_seconds(eot_timer));
	    
    freelog(LOG_DEBUG, "sniffingpackets");
    while(sniff_packets()==1);

    /* After sniff, re-zero the timer: (read-out above on next loop) */
    clear_timer_start(eot_timer);
    
    for(i=0;i<game.nplayers;i++)
      connection_do_buffer(game.players[i].conn);

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

    for(i=0;i<game.nplayers;i++)
      connection_do_unbuffer(game.players[i].conn);

    if(++save_counter>=game.save_nturns && game.save_nturns>0) {
      save_counter=0;
      save_game_auto();
    }
    if (game.year>game.end_year || is_game_over()) 
      server_state=GAME_OVER_STATE;
  }

  show_ending();
  show_map_to_all();
  notify_player(0, _("Game: The game is over..."));

  while(server_state==GAME_OVER_STATE) {
    force_end_of_sniff=0;
    sniff_packets();
  }

  server_close_udp();

  return 0;
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
...
**************************************************************************/
int send_server_info_to_metaserver(int do_send,int reset_timer)
{
  static struct timer *time_since_last_send = NULL;
  char desc[4096], info[4096];
  int i;
  int num_nonbarbarians;

  if (reset_timer && time_since_last_send)
  {
    free_timer(time_since_last_send);
    time_since_last_send = NULL;
    return 1;
     /* use when we close the connection to a metaserver */
  }

  if(!do_send && (time_since_last_send!=NULL)
     && (read_timer_seconds(time_since_last_send)
	 < METASERVER_UPDATE_INTERVAL)) {
    return 0;
  }
  if (time_since_last_send == NULL) {
    time_since_last_send = new_timer(TIMER_USER, TIMER_ACTIVE);
  }

  for (num_nonbarbarians=0, i=0; i<game.nplayers; ++i) {
    if (!is_barbarian(&game.players[i])) {
      ++num_nonbarbarians;
    }
  }

  /* build description block */
  desc[0]='\0';
  
  cat_snprintf(desc, sizeof(desc), "Freeciv\n");
  cat_snprintf(desc, sizeof(desc), VERSION_STRING"\n");
  switch(server_state) {
  case PRE_GAME_STATE:
    cat_snprintf(desc, sizeof(desc), "Pregame\n");
    break;
  case RUN_GAME_STATE:
    cat_snprintf(desc, sizeof(desc), "Running\n");
    break;
  case GAME_OVER_STATE:
    cat_snprintf(desc, sizeof(desc), "Game over\n");
    break;
  default:
    cat_snprintf(desc, sizeof(desc), "Waiting\n");
  }
  cat_snprintf(desc, sizeof(desc), "%s\n",metaserver_servername);
  cat_snprintf(desc, sizeof(desc), "%d\n", port);
  cat_snprintf(desc, sizeof(desc), "%d\n", num_nonbarbarians);
  cat_snprintf(desc, sizeof(desc), "%s", metaserver_info_line);

  /* now build the info block */
  info[0]='\0';
  cat_snprintf(info, sizeof(info),
	  "Players:%d  Min players:%d  Max players:%d\n",
	  num_nonbarbarians,  game.min_players, game.max_players);
  cat_snprintf(info, sizeof(info),
	  "Timeout:%d  Year: %s\n",
	  game.timeout, textyear(game.year));
    
    
  cat_snprintf(info, sizeof(info),
	       "NO:  NAME:               HOST:\n");
  cat_snprintf(info, sizeof(info),
	       "----------------------------------------\n");
  for(i=0; i<game.nplayers; ++i) {
    if (!is_barbarian(&game.players[i])) {
      cat_snprintf(info, sizeof(info), "%2d   %-20s %s\n", 
		 i, game.players[i].name, game.players[i].addr);
    }
  }

  clear_timer_start(time_since_last_send);
  return send_to_metaserver(desc, info);
}

/**************************************************************************
dest can be NULL meaning all players
**************************************************************************/
static void send_all_info(struct player *dest)
{
/*  send_rulesets(dest); */
  send_game_info(dest);
  send_map_info(dest);
  send_player_info(0, dest);
  send_spaceship_info(0, dest);
  send_all_known_tiles(dest);
  send_all_known_cities(dest);
  send_all_known_units(dest);
}

#ifdef UNUSED
/**************************************************************************
...
**************************************************************************/
int find_highest_used_id(void)
{
  int i, max_id;

  max_id=0;
  for(i=0; i<game.nplayers; i++) {
    city_list_iterate(game.players[i].cities, pcity) {
      max_id=MAX(max_id, pcity->id);
    }
    city_list_iterate_end;
    unit_list_iterate(game.players[i].units, punit) {
      max_id=MAX(max_id, punit->id);
    }
    unit_list_iterate_end;
  }
  return max_id;
}
#endif /* UNUSED */


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
      send_all_known_tiles(pplayer);
      send_all_known_cities(pplayer);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void update_pollution(void)
{
  int x,y,count=0;
  
  for (x=0;x<map.xsize;x++) 
    for (y=0;y<map.ysize;y++) 
      if (map_get_special(x,y)&S_POLLUTION) {
	count++;
      }
  game.heating=count;
  game.globalwarming+=count;
  if (game.globalwarming<game.warminglevel) 
    game.globalwarming=0;
  else {
    game.globalwarming-=game.warminglevel;
    if (myrand(200)<=game.globalwarming) {
      freelog(LOG_NORMAL, "Global warming:%d\n", count);
      global_warming(map.xsize/10+map.ysize/10+game.globalwarming*5);
      game.globalwarming=0;
      send_all_known_tiles(0);
      notify_player_ex(0, 0,0, E_WARMING,
		       _("Game: Global warming has occurred!"));
      notify_player(0, _("Game: Coastlines have been flooded and vast ranges "
			 "of grassland have become deserts."));
      game.warminglevel+=4;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void before_end_year(void)
{
  int i;
  for (i=0; i<game.nplayers; i++) {
    send_packet_before_end_year(game.players[i].conn);
  }
}

/**************************************************************************
...
**************************************************************************/
static void shuffle_players(void)
{
  int i, pos;
  struct player *tmpplr;
  /* shuffle players */

  for(i=0; i<game.nplayers; i++) {
    shuffled[i] = &game.players[i];
  }

  for(i=0; i<game.nplayers; i++) {
    pos = myrand(game.nplayers);
    tmpplr = shuffled[i]; 
    shuffled[i] = shuffled[pos];
    shuffled[pos] = tmpplr;
  }
}

/**************************************************************************
...
**************************************************************************/
static void ai_start_turn(void)
{
  int i;

  if(!shuffled[game.nplayers-1])
    shuffle_players();

  for (i = 0; i < game.nplayers; i++) {
    if(shuffled[i] && shuffled[i]->ai.control) 
      ai_do_first_activities(shuffled[i]);
  }
}

static int end_turn(void)
{
  int i;
  
  nocity_send = 1;

  for(i=0; i<game.nplayers; i++) {
    /* game.nplayers may increase during this loop, due to
       goto-ing units causing a civil war.  Thus shuffled[i]
       may be NULL; we do any new players in non-shuffled order
       at the end.  --dwp
    */
    struct player *pplayer = shuffled[i];
    if (pplayer==NULL) pplayer = &game.players[i];
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
  update_pollution();
  do_apollo_program();
  make_history_report();
  freelog(LOG_DEBUG, "Turn ended.");
  return 1;
}


/**************************************************************************
Unconditionally save the game, with specified filename.
Always prints a message: either save ok, or failed.
**************************************************************************/
void save_game(char *filename)
{
  struct section_file file;
  struct timer *timer_cpu, *timer_user;
  
  timer_cpu = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
  timer_user = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    
  section_file_init(&file);
  game_save(&file);
  
  if(!section_file_save(&file, filename))
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
}


/**************************************************************************
...
**************************************************************************/
static void handle_report_request(struct player *pplayer, enum report_type type)
{
  switch(type) {
   case REPORT_WONDERS_OF_THE_WORLD:
    wonders_of_the_world(pplayer);
    break;
   case REPORT_TOP_5_CITIES:
    top_five_cities(pplayer);
    break;
   case REPORT_DEMOGRAPHIC:
    demographics_report(pplayer);
    break;
  case REPORT_SERVER_OPTIONS1:
    report_server_options(pplayer, 1);
    break;
  case REPORT_SERVER_OPTIONS2:
    report_server_options(pplayer, 2);
    break;
  case REPORT_SERVER_OPTIONS: /* obsolete */
  default:
    notify_player(pplayer, "Game: request for unknown report (type %d)", type);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_packet_input(struct connection *pconn, char *packet, int type)
{
  int i;
  struct player *pplayer=NULL;

  switch(type) {
  case PACKET_REQUEST_JOIN_GAME:
    handle_request_join_game(pconn, (struct packet_req_join_game *)packet);
    free(packet);
    return;
    break;
  }

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].conn==pconn) {
      pplayer=&game.players[i];
      break;
    }

  if(i==game.nplayers) {
    freelog(LOG_VERBOSE, "got game packet from unaccepted connection");
    free(packet);
    return;
  }

  pplayer->nturns_idle=0;

  if(!pplayer->is_alive && type!=PACKET_CHAT_MSG)
    return;
  
  switch(type) {
    
  case PACKET_TURN_DONE:
    handle_turn_done(i);
    break;

  case PACKET_ALLOC_NATION:
    handle_alloc_nation(i, (struct packet_alloc_nation *)packet);
    break;

  case PACKET_UNIT_INFO:
    handle_unit_info(pplayer, (struct packet_unit_info *)packet);
    break;

  case PACKET_MOVE_UNIT:
    handle_move_unit(pplayer, (struct packet_move_unit *)packet);
    break;
 
  case PACKET_CHAT_MSG:
    handle_chat_msg(pplayer, (struct packet_generic_message *)packet);
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
    handle_report_request(pplayer, 
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
    handle_incite_inq(pplayer, (struct packet_generic_integer *)packet);
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
    handle_city_name_suggest_req(pplayer,
				 (struct packet_generic_integer *)packet);
    break;
  case PACKET_UNIT_PARADROP_TO:
    handle_unit_paradrop_to(pplayer, (struct packet_unit_request *)packet);
    break;
  case PACKET_UNIT_CONNECT:
    handle_unit_connect(pplayer, (struct packet_unit_connect *)packet);
    break;
  default:
    freelog(LOG_NORMAL, "uh got an unknown packet from %s", game.players[i].name);
  }

  free(packet);
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
int check_for_full_turn_done(void)
{
  int i;

  for(i=0; i<game.nplayers; i++) {
    if (game.turnblock) {
      if (!game.players[i].ai.control && game.players[i].is_alive &&
          !game.players[i].turn_done)
        return 0;
    } else {
      if(game.players[i].conn && game.players[i].is_alive && 
         !game.players[i].turn_done) {
        return 0;
      }
    }
  }
  force_end_of_sniff=1;
  return 1;
}

/**************************************************************************
...
**************************************************************************/
static void handle_turn_done(int player_no)
{
  game.players[player_no].turn_done=1;

  check_for_full_turn_done();

  send_player_info(&game.players[player_no], 0);
}

/**************************************************************************
...
**************************************************************************/
static void handle_alloc_nation(int player_no, struct packet_alloc_nation *packet)
{
  int i, nations_used;
  struct packet_generic_values select_nation; 

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].nation==packet->nation_no) {
       send_select_nation(&game.players[player_no]); /* it failed - nation taken */
       return;
    } else /* check to see if name has been taken */
       if (!strcmp(game.players[i].name,packet->name) && 
            game.players[i].nation != MAX_NUM_NATIONS) { 
       notify_player(&game.players[player_no],
		     _("Another player named '%s' has already joined the game.  "
		       "Please choose another name."), packet->name);
       send_select_nation(&game.players[player_no]);
       return;
    }

  freelog(LOG_NORMAL, _("%s is the %s ruler %s."), game.players[player_no].name, 
      get_nation_name(packet->nation_no), packet->name);

  /* inform player his choice was ok */
  select_nation.value2=0xffff;
  send_packet_generic_values(game.players[player_no].conn, PACKET_SELECT_NATION, 
                             &select_nation);

  game.players[player_no].nation=packet->nation_no;
  sz_strlcpy(game.players[player_no].name, packet->name);
  game.players[player_no].is_male=packet->is_male;
  game.players[player_no].city_style=packet->city_style;

  /* tell the other players, that the nation is now unavailable */
  nations_used = 0;
  for(i=0; i<game.nplayers; i++)
    if(game.players[i].nation==MAX_NUM_NATIONS) 
      send_select_nation(&game.players[i]);
    else
      nations_used++;                     /* count used nations */

  mark_nation_as_used(packet->nation_no);

  /* if there's no nation left, reject remaining players, sorry */
  if( nations_used == game.playable_nation_count ) {   /* barb */
    for(i=0; i<game.nplayers; i++) {
      if( game.players[i].nation == MAX_NUM_NATIONS ) {
        reject_new_player(_("Sorry, you can't play."
			    "  There are no nations left."),
			  game.players[i].conn);
	freelog(LOG_NORMAL, _("Game full - %s was rejected."),
		game.players[i].name);    
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

  send_packet_generic_values(pplayer->conn, PACKET_SELECT_NATION, &select_nation);
}


/**************************************************************************
 Send a join_game reply, accepting the player.
 (Send to pplayer->conn, which should be set, as should pplayer->addr)
 'rejoin' is whether this is a new player, or re-connection.
 Prints a log message, and notifies other players.
**************************************************************************/
static void join_game_accept(struct player *pplayer, int rejoin)
{
  struct packet_join_game_reply packet;
  int i;

  assert(pplayer);
  assert(pplayer->conn);
  packet.you_can_join = 1;
  sz_strlcpy(packet.capability, our_capability);
  my_snprintf(packet.message, sizeof(packet.message),
	      "%s %s.", (rejoin?"Welcome back":"Welcome"), pplayer->name);
  send_packet_join_game_reply(pplayer->conn, &packet);
  if (rejoin) {
    freelog(LOG_NORMAL, _("<%s@%s> has rejoined the game."),
	    pplayer->name, pplayer->addr);
  } else {
    freelog(LOG_NORMAL, _("<%s@%s> has joined the game."),
	    pplayer->name, pplayer->addr);
  }
  for(i=0; i<game.nplayers; ++i) {
    if (pplayer != &game.players[i]) {
      if (rejoin) {
	notify_player(&game.players[i],
		      _("Game: Player <%s@%s> has reconnected."),
		      pplayer->name, pplayer->addr);
      } else {
	notify_player(&game.players[i],
		      _("Game: Player <%s@%s> has connected."),
		      pplayer->name, pplayer->addr);
      }
    }
  }
}

/**************************************************************************
 Introduce player to server, and notify of other players.
**************************************************************************/
static void introduce_game_to_player(struct player *pplayer)
{
  char hostname[512];
  struct player *cplayer;
  int i, nconn, nother, nbarb;
  
  if (!pplayer->conn)
    return;
  
  if (gethostname(hostname, 512)==0) {
    notify_player(pplayer, _("Welcome to the %s Server running at %s."), 
		  FREECIV_NAME_VERSION, hostname);
  } else {
    notify_player(pplayer, _("Welcome to the %s Server."),
		  FREECIV_NAME_VERSION);
  }

  /* tell who we're waiting on to end the game turn */
  if (game.turnblock) {
    for(i=0; i<game.nplayers;++i) {
      if (game.players[i].is_alive &&
          !game.players[i].ai.control &&
          !game.players[i].turn_done) {

         /* skip current player */
         if (&game.players[i] == pplayer) {
           continue;
         }

         notify_player(pplayer,
                       _("Turn-blocking game play: "
			 "waiting on %s to finish turn..."),
                       game.players[i].name);
      }
    }
  }

  if (server_state==RUN_GAME_STATE) {
    /* if the game is running, players can just view the Players menu?  --dwp */
    return;
  }
  for(nbarb=0, nconn=0, i=0; i<game.nplayers; ++i) {
    nconn += (game.players[i].is_connected);
    nbarb += is_barbarian(&game.players[i]);
  }
  if (nconn != 1) {
    notify_player(pplayer, _("There are currently %d players connected:"),
		  nconn);
  } else {
    notify_player(pplayer, _("There is currently 1 player connected:"));
  }
  for(i=0; i<game.nplayers; ++i) {
    cplayer = &game.players[i];
    if (cplayer->is_connected) {
      notify_player(pplayer, "  <%s@%s>%s", cplayer->name, cplayer->addr,
		    ((!cplayer->is_alive) ? _(" (R.I.P.)")
		     :cplayer->ai.control ? _(" (AI mode)") : ""));
    }
  }
  nother = game.nplayers - nconn - nbarb;
  if (nother > 0) {
    if (nother == 1) {
      notify_player(pplayer, _("There is 1 other player:"));
    } else {
      notify_player(pplayer, _("There are %d other players:"), nother);
    }
    for(i=0; i<game.nplayers; ++i) {
      cplayer = &game.players[i];
      if( is_barbarian(cplayer) ) continue;
      if (!cplayer->is_connected) {
	notify_player(pplayer, "  %s%s", cplayer->name, 
		    ((!cplayer->is_alive) ? _(" (R.I.P.)")
		     :cplayer->ai.control ? _(" (an AI player)") : ""));
      }
    }
  }
  /* notify_player(pplayer, "Waiting for the server to start the game."); */
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
  pplayer->conn = pconn;
  pplayer->is_connected = (pconn!=NULL);
  if (pconn) {
    sz_strlcpy(pplayer->addr, pconn->addr); 
    join_game_accept(pplayer, 0);
  } else {
    freelog(LOG_NORMAL, _("%s has been added as an AI-controlled player."),
	    name);
    notify_player(0, _("Game: %s has been added as an AI-controlled player."),
		  name);
  }

  game.nplayers++;
  if(server_state==PRE_GAME_STATE && game.max_players==game.nplayers)
    server_state=SELECT_RACES_STATE;

  introduce_game_to_player(pplayer);
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
...
**************************************************************************/
static void handle_request_join_game(struct connection *pconn, 
				     struct packet_req_join_game *req)
{
  struct player *pplayer;
  char msg[MAX_LEN_MSG];
  
  freelog(LOG_NORMAL,
	  _("Connection request from %s with client version %d.%d.%d%s"),
	  req->name, req->major_version, req->minor_version,
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
    freelog(LOG_NORMAL, _("%s was rejected: mismatched capabilities."),
	    req->name);
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
    freelog(LOG_NORMAL, _("%s was rejected: mismatched capabilities"),
	    req->name);
    close_connection(pconn);
    return;
  }

  if( (pplayer=find_player_by_name(req->name)) || 
      (pplayer=find_player_by_user(req->name))     ) {
    if(!pplayer->conn) {
      if(is_barbarian(pplayer)) {
        reject_new_player(_("Sorry, you can't observe a barbarian."), pconn);
        freelog(LOG_NORMAL,
		_("No observation of barbarians - %s was rejected."),
		req->name);
        close_connection(pconn);
        return;
      }

      pplayer->conn=pconn;
      pplayer->is_connected=1;
      sz_strlcpy(pplayer->addr, pconn->addr); 
      join_game_accept(pplayer, 1);
      introduce_game_to_player(pplayer);
      if(server_state==RUN_GAME_STATE) {
        send_rulesets(pplayer);
	send_all_info(pplayer);
        send_game_state(pplayer, CLIENT_GAME_RUNNING_STATE);
	send_player_info(NULL,NULL);
      }
      if (game.auto_ai_toggle && pplayer->ai.control) {
	toggle_ai_player_direct(NULL, pplayer);
      }
      return;
    }

    if(server_state==PRE_GAME_STATE) {
      if(game.nplayers==game.max_players) {
	reject_new_player(_("Sorry, you can't join.  The game is full."), pconn);
	freelog(LOG_NORMAL, _("Game full - %s was rejected."), req->name);    
	close_connection(pconn);
        return;
      }
      else {
	/* Used to have here: accept_new_player(req->name, pconn); 
	 * but duplicate names cause problems even in PRE_GAME_STATE
	 * because they are used to identify players for various
	 * server commands. --dwp
	 */
	reject_new_player(_("Sorry, someone else already has that name."),
			  pconn);
	freelog(LOG_NORMAL, _("%s was rejected, name already used."), req->name);
	close_connection(pconn);

        return;
      }
    }

    my_snprintf(msg, sizeof(msg),
		_("You can't join the game.  %s is already connected."), 
		pplayer->name);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, _("%s was rejected."), pplayer->name);
    close_connection(pconn);

    return;
  }

  /* unknown name */

  if(server_state!=PRE_GAME_STATE) {
    reject_new_player(_("Sorry, you can't join.  The game is already running."),
		      pconn);
    freelog(LOG_NORMAL, _("Game running - %s was rejected."), req->name);
    close_connection(pconn);

    return;
  }

  if(game.nplayers==game.max_players) {
    reject_new_player(_("Sorry, you can't join.  The game is full."), pconn);
    freelog(LOG_NORMAL, _("game full - %s was rejected."), req->name);    
    close_connection(pconn);

    return;
  }

  accept_new_player(req->name, pconn);
}

/**************************************************************************
...
**************************************************************************/
void lost_connection_to_player(struct connection *pconn)
{
  struct player *pplayer = NULL;
  int i;

  /* Find the player: */
  for(i=0; i<game.nplayers; i++) {
    if (game.players[i].conn == pconn) {
      pplayer = &game.players[i];
      break;
    }
  }
  if (pplayer == NULL) {
    /* This happens eg if the player has not yet joined properly. */
    freelog(LOG_FATAL, _("Lost connection to <unknown>."));
    return;
  }
  
  freelog(LOG_NORMAL, _("Lost connection to %s."), pplayer->name);
  
  pplayer->conn = NULL;
  pplayer->is_connected = 0;
  sz_strlcpy(pplayer->addr, "---.---.---.---");

  send_player_info(&game.players[i], 0);
  notify_player(0, _("Game: Lost connection to %s."), pplayer->name);

  if (game.is_new_game && (server_state==PRE_GAME_STATE ||
			   server_state==SELECT_RACES_STATE)) {
    server_remove_player(&game.players[i]);
  }
  else if (game.auto_ai_toggle && !pplayer->ai.control) {
    toggle_ai_player_direct(NULL, pplayer);
  }
  check_for_full_turn_done();
}

/**************************************************************************
generate_ai_players() - Selects a nation for players created with server's
   "create <PlayerName>" command.  If <PlayerName> is the default leader
   name for some nation, we choose the corresponding nation for that name
   (i.e. if we issue "create Shaka" then we will make that AI player's
   nation the Zulus if the Zulus have not been chosen by anyone else.  If they
   have, then we pick an available nation at random.

   After that, we check to see if the
   server option "aifill" is greater than the number of players currently
   connected.  If so, we create the appropriate number of players
   (game.aifill - game.nplayers) from scratch, choosing a random nation
   and appropriate name for each.

   If AI player name is the default leader name for AI player nation,
   then player sex is defualt nation leader sex.
   (so if English are ruled by Elisabeth, she is female, but if "Player 1"
   rules English, he's a male).
**************************************************************************/

static void generate_ai_players(void)
{
  int i, player;
  Nation_Type_id nation;
  char player_name[MAX_LEN_NAME];

  /* Select nations for AI players generated with server 'create <name>' command */

  for(player=0;player<game.nplayers;player++) {
    if(game.players[player].nation != MAX_NUM_NATIONS)
       continue;

    if( num_nations_avail == 0 ) {
      freelog( LOG_NORMAL,
	       _("Ran out of nations.  AI controlled player %s not created."),
               game.players[player].name );
      server_remove_player(&game.players[player]);
      continue;
    }

    for (nation = 0; nation < game.playable_nation_count; nation++) {
      if ( check_nation_leader_name( nation, game.players[player].name ) )
        if(nations_used[nation] != -1) {
           game.players[player].nation=mark_nation_as_used(nation);
           game.players[player].city_style = get_nation_city_style(nation);
           game.players[player].is_male=get_nation_leader_sex(nation, game.players[player].name);
           break;
        }
    }

    if(nation == game.playable_nation_count) {
      game.players[player].nation=
              mark_nation_as_used(nations_avail[myrand(num_nations_avail)]);
      game.players[player].is_male=myrand(2);
    }

    announce_ai_player(&game.players[player]);

  }

  /* Create and pick nation and name for AI players needed to bring the
     total number of players == game.aifill */

  if( game.playable_nation_count < game.aifill ) {
    game.aifill = game.playable_nation_count;
    freelog( LOG_NORMAL,
	     _("Nation count smaller than aifill; aifill reduced to %d."),
             game.playable_nation_count);
  }

  for(;game.nplayers < game.aifill;) {
     nation = mark_nation_as_used(nations_avail[myrand(num_nations_avail)]);
     pick_ai_player_name(nation,player_name);
     i=game.nplayers;
     accept_new_player(player_name, NULL);
     if ((game.nplayers == i+1) && 
         !strcmp(player_name,game.players[i].name)) {
           game.players[i].nation=nation;
           game.players[i].city_style = get_nation_city_style(nation);
	   game.players[i].ai.control = !game.players[i].ai.control;
	   game.players[i].ai.skill_level = game.skill_level;
           if( check_nation_leader_name(nation,player_name) )
             game.players[i].is_male = get_nation_leader_sex(nation,player_name);
           else
             game.players[i].is_male = myrand(2);
           announce_ai_player(&game.players[i]);
	   set_ai_level_direct(&game.players[i], game.players[i].ai.skill_level);
        } else
	  con_write(C_FAIL, _("Error creating new ai player: %s\n"),
		    player_name);
   }

  send_server_info_to_metaserver(1,0);

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
  struct tile *ptile;
  for (x = 0; x < map.xsize; x++)
    for (y = 0; y < map.ysize; y++) {
      ptile = map_get_tile(x,y);
      ptile->seen[pplayer->player_no]--;
      if (ptile->seen[pplayer->player_no] == 0)
	update_player_tile_last_seen(pplayer, x, y);
    }
  send_all_known_tiles(pplayer);
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
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      map_get_tile(x,y)->seen[pplayer->player_no] += 1;
      if (map_get_known(x, y, pplayer)) {
	struct city *pcity = map_get_city(x,y);
	reality_check_city(pplayer, x, y);
	if (pcity)
	  update_dumb_city(pplayer, pcity);
      }
    }
  }
  send_all_known_tiles(pplayer);
  send_all_known_units(pplayer);
  send_all_known_cities(pplayer);
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


/********************************************************************** 
The initmap option is used because we don't want to initialize the map
before the x and y sizes have been determined
***********************************************************************/
void server_player_init(struct player *pplayer, int initmap)
{
  if (initmap)
    player_map_allocate(pplayer);
  player_init(pplayer);
}

#ifdef GENERATING_MAC
/*************************************************************************
  generate an option dialog if no options have been passed in
*************************************************************************/
static void Mac_options(int *argc, char *argv[])
{
  if (argc == 0) /* always true curently, but... */
  {
    OSErr err;
    DialogPtr  optptr;
    short the_type;
    Handle the_handle;
    Rect the_rect;
    bool done, meta;
    char * str='\0';
    Str255 the_string;
    optptr=GetNewDialog(128,nil,(WindowPtr)-1);
    err=SetDialogDefaultItem(optptr, 2);
    if (err != 0)
    {
      exit(1); /*we don't have an error log yet so I just bomb.  Is this correct?*/
    }
    err=SetDialogTracksCursor(optptr, true);
    if (err != 0)
    {
      exit(1);
    }
    GetDItem(optptr, 16/*normal radio button*/, &the_type, &the_handle, &the_rect);
    SetCtlValue((ControlHandle)the_handle, true);

    while(!done)
    {
      short the_item, old_item;
      ModalDialog(0, &the_item);
      switch (the_item)
      {
        case 1:
          done = true;
        break;
        case 2:
          exit(0);
        break;
        break;
        case 13:
          GetDItem(optptr, 13, &the_type, &the_handle, &the_rect);
          meta=!GetCtlValue((ControlHandle)the_handle);
          SetCtlValue((ControlHandle)the_handle, meta);
        break;
        case 15:
        case 16:
        case 17:
          GetDItem(optptr, old_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, false);
          old_item=the_item;
          GetDItem(optptr, the_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, true);
        break;
      }
    }
    /*now, bundle the data into the argc/argv storage for interpritation*/
    GetDItem( optptr, 4, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)str);
    if (str)
    {
      strcpy(argv[++(*argc)],"--file");
      strcpy(argv[++(*argc)], str);
    }
    GetDItem( optptr, 6, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)str);
    if (str)
    {
      strcpy(argv[++(*argc)],"--gamelog");
      strcpy(argv[++(*argc)], str);
    }
    GetDItem( optptr, 8, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)str);
    if (str)
    {
      strcpy(argv[++(*argc)],"--log");
      strcpy(argv[++(*argc)], str);
    }
    if (meta)
      strcpy(argv[++(*argc)], "--meta");
    GetDItem( optptr, 12, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, the_string);
    if (atoi((char*)the_string)>0)
    {
      strcpy(argv[++(*argc)],"--port");
      strcpy(argv[++(*argc)], (char *)the_string);
    }
    GetDItem( optptr, 10, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)str);
    if (str)
    {
      strcpy(argv[++(*argc)],"--read");
      strcpy(argv[++(*argc)], str);
    }
    GetDItem(optptr, 15, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      strcpy(argv[++(*argc)],"--debug");
      strcpy(argv[++(*argc)],"0");
    }
    GetDItem(optptr, 16, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      strcpy(argv[++(*argc)],"--debug");
      strcpy(argv[++(*argc)],"1");
    }
    GetDItem(optptr, 17, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      strcpy(argv[++(*argc)],"--debug");
      strcpy(argv[++(*argc)],"2");
    }
    DisposeDialog(optptr);/*get rid of the dialog after sorting out the options*/
  }
}
/*  --file	F 	implemented
    --gamelog F implemented
    --help		not needed?
    --log F		implemented
    --meta		implemented
    --port N	implemented
    --read F	implemented
    --server H	**not implemented***
    --debug N	implemented
    --version	not needed?
    --Metaserver A not implemented 
*/

#endif
