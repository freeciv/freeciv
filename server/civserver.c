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
#include "packets.h"
#include "player.h"
#include "registry.h"
#include "shared.h"
#include "tech.h"
#include "timing.h"
#include "version.h"

#include "autoattack.h"
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
static void read_init_script(char *script_filename);
static void save_game_auto(void);
static void generate_ai_players(void);
static int mark_race_as_used(int race);
static void announce_ai_player(struct player *pplayer);

static void handle_alloc_race(int player_no, struct packet_alloc_race *packet);
static void handle_request_join_game(struct connection *pconn, 
				     struct packet_req_join_game *request);
static void handle_turn_done(int player_no);
static void send_select_race(struct player *pplayer);

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
/* server name for metaserver to use for us */
char metaserver_servername[64]="";

struct player *shuffled[MAX_NUM_PLAYERS];

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
unsigned short global_id_counter=100;
unsigned char used_ids[8192]={0};
int is_server = 1;

int is_new_game=1;

/* The following is unused, AFAICT.  --dwp */
char usage[] = 
"Usage: %s [-fhlpgv] [--file] [--help] [--log] [--port]\n\t[--gamelog] [--version]\n";

int port=DEFAULT_SOCK_PORT;
int nocity_send=0;

/* The next three variables make selecting races for AI players cleaner */
int races_avail[R_LAST];
int races_used[R_LAST];
int num_races_avail=R_LAST;

int rand_init=0;

/**************************************************************************
...
**************************************************************************/
int main(int argc, char *argv[])
{
  int h=0, v=0, no_meta=1;
  char *log_filename=NULL;
  char *gamelog_filename=NULL;
  char *load_filename=NULL;
  char *script_filename=NULL;
  char *option=NULL;
  int i;
  int save_counter=0;
  int loglevel=LOG_NORMAL;

  init_nls();
  dont_run_as_root(argv[0], "freeciv_server");

  strcpy(metaserver_info_line, DEFAULT_META_SERVER_INFO_STRING);
  strcpy(metaserver_addr, DEFAULT_META_SERVER_ADDR);

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
      fprintf(stderr, "Warning: the %s option is obsolete.  "
	              "Use -m to enable the metaserver.\n", argv[i]);
      h=1;
    }
    else if(is_option("--meta", argv[i]))
        no_meta=0;
    else if ((option = get_option("--Metaserver",argv,&i,argc)) != NULL)
    {
	strcpy(metaserver_addr,argv[i]);
	no_meta = 0; /* implies --meta */
    }
    else if ((option = get_option("--port",argv,&i,argc)) != NULL)
	port=atoi(option);
    else if ((option = get_option("--read",argv,&i,argc)) != NULL)
	script_filename=option;
    else if ((option = get_option("--server",argv,&i,argc)) != NULL)
      	strcpy(metaserver_servername,option);
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
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
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
  con_write(C_COMMENT, "You can learn a lot about Freeciv at %s",
	    WEBSITE_URL);

  if(h) {
    fprintf(stderr, "  -f, --file F\t\tLoad saved game F\n");
    fprintf(stderr, "  -g, --gamelog F\tUse F as game logfile\n");
    fprintf(stderr, "  -h, --help\t\tPrint a summary of the options\n");
    fprintf(stderr, "  -l, --log F\t\tUse F as logfile\n");
    fprintf(stderr, "  -m, --meta\t\tSend info to Metaserver\n");
    fprintf(stderr, "  -M, --Metaserver\tSet Metaserver address.\n");
    fprintf(stderr, "  -p, --port N\t\tconnect to port N\n");
    fprintf(stderr, "  -r, --read\t\tRead startup script\n");
    fprintf(stderr, "  -s, --server H\tList this server as host H\n");
#ifdef DEBUG
    fprintf(stderr, "  -d, --debug N\t\tSet debug log level (0,1,2,3,"
	                                "or 3:file1,min,max:...)\n");
#else
    fprintf(stderr, "  -d, --debug N\t\tSet debug log level (0,1,2)\n");
#endif
    fprintf(stderr, "  -v, --version\t\tPrint the version number\n");
    fprintf(stderr, "Report bugs to <%s>.\n", BUG_EMAIL_ADDRESS);
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
  con_write(C_COMMENT, "Freeciv 1.8 will be released "
	    "third week of March at %s", WEBSITE_URL);
#endif
  
  con_flush();

  init_our_capability();
  game_init();
  initialize_city_cache();

  /* load a saved game */
  
  if(load_filename) {
    struct timer *loadtimer, *uloadtimer;
    struct section_file file;
    
    freelog(LOG_NORMAL,"Loading saved game: %s", load_filename);
    loadtimer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
    uloadtimer = new_timer_start(TIMER_USER, TIMER_ACTIVE);
    if(!section_file_load(&file, load_filename)) { 
      freelog(LOG_FATAL, "Couldn't load savefile: %s", load_filename);
      exit(1);
    }
    game.scenario=game_load(&file);
    section_file_check_unused(&file, load_filename);
    section_file_free(&file);

   /* game.scenario: 0=normal savegame, 1=everything but players,
       2=just tile map and startpositions, 3=just tile map
   */
    if (game.scenario) { /* we may have a scenario here */
      if(game.nplayers) { /* no, it's just a normal savegame */
	is_new_game=0;
	game.scenario=0;
      }
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
    freelog(LOG_NORMAL, "Sending info to metaserver[%s %d]",
	    metaserver_addr, METASERVER_PORT);
    server_open_udp(); /* open socket for meta server */ 
  }

  send_server_info_to_metaserver(1,0);
  
  /* accept new players, wait for serverop to start..*/
  freelog(LOG_NORMAL, "Now accepting new client connections");
  server_state=PRE_GAME_STATE;

  if (script_filename)
    read_init_script(script_filename);

  while(server_state==PRE_GAME_STATE)
    sniff_packets();

  send_server_info_to_metaserver(1,0);

  if(is_new_game) {
    load_rulesets();
    /* otherwise rulesets were loaded when savegame was loaded */
  }
  
  for(i=0; i<R_LAST;i++) {
      races_avail[i]=i;
      races_used[i]=i;
  }

  /* Allow players to select a race (case new game).
   * AI players may not yet have a race; these will be selected
   * in generate_ai_players() later
   */
  server_state = RUN_GAME_STATE;
  for(i=0; i<game.nplayers; i++) {
    if (game.players[i].race == R_LAST && !game.players[i].ai.control) {
      send_select_race(&game.players[i]);
      server_state = SELECT_RACES_STATE;
    }
  }
  
  while(server_state==SELECT_RACES_STATE) {
    sniff_packets();
    for(i=0; i<game.nplayers; i++) {
      if (game.players[i].race == R_LAST && !game.players[i].ai.control) {
	break;
      }
    }
    if(i==game.nplayers)
      server_state=RUN_GAME_STATE;
  }

  if(!game.randseed) {
    /* We strip the high bit for now because neither game file nor
       server options can handle unsigned ints yet. - Cedric */
    game.randseed = time(NULL) & (MAX_UINT32 >> 1);
  }
 
  if(!rand_init)
    mysrand(game.randseed);

  if(is_new_game)
    generate_ai_players();
   
  /* if we have a tile map, and map.generator==0, call map_fractal_generate
     anyway, to make the specials and huts */
  if(map_is_empty() || (map.generator == 0 && is_new_game))
    map_fractal_generate();
  else 
    flood_it(1);
  gamelog_map();
  /* start the game */

  server_state=RUN_GAME_STATE;
  send_server_info_to_metaserver(1,0);

  if(is_new_game) {
    int i;
    for(i=0; i<game.nplayers; i++) {
      init_tech(&game.players[i], game.tech); 
      game.players[i].economic.gold=game.gold;
    }
    game.max_players=game.nplayers;

    /* we don't want random start positions in a scenario which already
       provides them.  -- Gudy */
    if(game.scenario==1 || game.scenario==2)
      flood_it(1);
    else {
      flood_it(0);
      create_start_positions();
    }
  }

  initialize_move_costs(); /* this may be the wrong place to do this */
  generate_minimap(); /* for city_desire; saves a lot of calculations */

  if (!is_new_game) {
    for (i=0;i<game.nplayers;i++) {
      civ_score(&game.players[i]);  /* if we don't, the AI gets really confused */
      if (game.players[i].ai.control) {
	set_ai_level_direct(&game.players[i], game.players[i].ai.skill_level);
        city_list_iterate(game.players[i].cities, pcity)
          assess_danger(pcity); /* a slowdown, but a necessary one */
        city_list_iterate_end;
      }
    }
  }
  
  send_all_info(0);

  if(is_new_game) 
    init_new_game();
    
  send_game_state(0, CLIENT_GAME_RUNNING_STATE);
  
  /* from here on, treat scenarios as normal games, as this is what
     they have become (nothing special about them anymore)*/
  game.scenario=0;
  
  while(server_state==RUN_GAME_STATE) {
    force_end_of_sniff=0;
    freelog(LOG_DEBUG, "Shuffleplayers");
    shuffle_players();
    freelog(LOG_DEBUG, "Aistartturn");
    ai_start_turn();

    freelog(LOG_DEBUG, "sniffingpackets");
    while(sniff_packets()==1);
    
    for(i=0;i<game.nplayers;i++)
      connection_do_buffer(game.players[i].conn);
    freelog(LOG_DEBUG, "Autosettlers");
    auto_settlers(); /* moved this after ai_start_turn for efficiency -- Syela */
    /* moved after sniff_packets for even more efficiency.
       What a guy I am. -- Syela */
    before_end_year(); /* resetting David P's message window -- Syela */
    /* and now, we must manage our remaining units BEFORE the cities that are
       empty get to refresh and defend themselves.  How totally stupid. */
    ai_start_turn(); /* Misleading name for manage_units -- Syela */
    freelog(LOG_DEBUG, "Auto-Attack phase");
    auto_attack();
    freelog(LOG_DEBUG, "Endturn");
    end_turn();
    freelog(LOG_DEBUG, "Gamenextyear");
    game_next_year();
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
  
  notify_player(0, "Game: The game is over..");

  while(server_state==GAME_OVER_STATE) {
    force_end_of_sniff=0;
    sniff_packets();
  }

  server_close_udp();

  return 0;
}

static int is_game_over(void)
{
  int alive = 0;
  int i;
  if (game.nplayers == 1)
    return 0;
  for (i=0;i<game.nplayers; i++) {
    if (game.players[i].is_alive)
      alive ++;
  }
  return (alive <= 1);
}

/**************************************************************************
...
**************************************************************************/
static void read_init_script(char *script_filename)
{
  FILE *script_file;
  char buffer[512];

  script_file = fopen(script_filename,"r");
  if (script_file)
    {
      /* the size 511 is set as to not overflow buffer in handle_stdin_input */
      while(fgets(buffer,511,script_file))
	handle_stdin_input((struct player *)NULL, buffer);
      fclose(script_file);
    }
  else
    freelog(LOG_NORMAL, "Could not open script file '%s'.",script_filename);
}

/**************************************************************************
...
**************************************************************************/

int send_server_info_to_metaserver(int do_send,int reset_timer)
{
  static struct timer *time_since_last_send = NULL;
  char desc[4096], info[4096];
  int i;

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

  /* build description block */
  desc[0]='\0';
  
  sprintf(desc+strlen(desc), "Freeciv\n");
  sprintf(desc+strlen(desc), VERSION_STRING"\n");
  switch(server_state) {
  case PRE_GAME_STATE:
    sprintf(desc+strlen(desc), "Pregame\n");
    break;
  case RUN_GAME_STATE:
    sprintf(desc+strlen(desc), "Running\n");
    break;
  case GAME_OVER_STATE:
    sprintf(desc+strlen(desc), "Game over\n");
    break;
  default:
    sprintf(desc+strlen(desc), "Waiting\n");
  }
  sprintf(desc+strlen(desc), "%s\n",metaserver_servername);
  sprintf(desc+strlen(desc), "%d\n", port);
  sprintf(desc+strlen(desc), "%d\n", game.nplayers);
  sprintf(desc+strlen(desc), "%s", metaserver_info_line);
    
  /* now build the info block */
  info[0]='\0';
  sprintf(info+strlen(info), 
	  "Players:%d  Min players:%d  Max players:%d\n",
	  game.nplayers, game.min_players, game.max_players);
  sprintf(info+strlen(info), 
	  "Timeout:%d  Year: %s\n",
	  game.timeout, textyear(game.year));
    
    
  sprintf(info+strlen(info), "NO:  NAME:               HOST:\n");
  sprintf(info+strlen(info), "----------------------------------------\n");
  for(i=0; i<game.nplayers; ++i) {
    sprintf(info+strlen(info), "%2d   %-20s %s\n", 
	    i, game.players[i].name, game.players[i].addr);
  }

  clear_timer_start(time_since_last_send);
  return send_to_metaserver(desc, info);
}

/**************************************************************************
dest can be NULL meaning all players
**************************************************************************/
static void send_all_info(struct player *dest)
{
  send_rulesets(dest);
  send_game_info(dest);
  send_map_info(dest);
  send_player_info(0, dest);
  send_spaceship_info(0, dest);
  send_all_known_tiles(dest);
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
    
    for(i=0; i<game.nplayers; i++) {
      city_list_iterate(game.players[i].cities, pcity)
	
	light_square(pplayer, pcity->x, pcity->y, 0);
      city_list_iterate_end;
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
      notify_player_ex(0, 0,0, E_WARMING, "Game: Global warming has occurred!");
      notify_player(0, "Game: Coastlines have been flooded and vast ranges of grassland have become deserts.");
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
  
  section_file_init(&file);
  game_save(&file);
  
  if(!section_file_save(&file, filename))
    con_write(C_FAIL, "Failed saving game as %s", filename);
  else
    con_write(C_OK, "Game saved as %s", filename);

  section_file_free(&file);
}

/**************************************************************************
Save game with autosave filename, and call gamelog_save().
**************************************************************************/
static void save_game_auto(void)
{
  char filename[512];

  assert(strlen(game.save_name)<256);
  
  sprintf(filename, "%s%d.sav", game.save_name, game.year);
  save_game(filename);
  gamelog_save();		/* should this be in save_game()? --dwp */
}

/**************************************************************************
...
**************************************************************************/

void start_game(void)
{
  if(server_state!=PRE_GAME_STATE) {
    con_puts(C_SYNTAX, "the game is already running.");
    return;
  }

  con_puts(C_OK, "starting game.");

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
void handle_city_name_suggest_req(struct player *pplayer,
				  struct packet_generic_integer *packet)
{
  struct packet_city_name_suggestion reply;
  reply.id = packet->value;
  strcpy(reply.name, city_name_suggestion(pplayer));
  send_packet_city_name_suggestion(pplayer->conn, &reply);
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

  case PACKET_ALLOC_RACE:
    handle_alloc_race(i, (struct packet_alloc_race *)packet);
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
  
  /* if someone is still moving, then let the other know that this play moved */ 
  if(!check_for_full_turn_done())
    send_player_info(&game.players[player_no], 0);
}

/**************************************************************************
...
**************************************************************************/
static void handle_alloc_race(int player_no, struct packet_alloc_race *packet)
{
  int i;
  struct packet_generic_integer retpacket;

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].race==packet->race_no) {
       send_select_race(&game.players[player_no]); /* it failed - race taken */
       return;
    } else /* check to see if name has been taken */
       if (!strcmp(game.players[i].name,packet->name) && 
            game.players[i].race != R_LAST) { 
       notify_player(&game.players[player_no],
		     "Another player named '%s' has already joined the game.  "
		     "Please choose another name.", packet->name);
       send_select_race(&game.players[player_no]);
       return;
    }

  freelog(LOG_NORMAL, "%s is the %s ruler %s", game.players[player_no].name, 
      get_race_name(packet->race_no), packet->name);

  /* inform player his choice was ok */
  retpacket.value=-1;
  send_packet_generic_integer(game.players[player_no].conn,
			      PACKET_SELECT_RACE, &retpacket);

  game.players[player_no].race=packet->race_no;
  strcpy(game.players[player_no].name, packet->name);

  /* tell the other players, that the race is now unavailable */
  for(i=0; i<game.nplayers; i++)
    if(game.players[i].race==R_LAST)
      send_select_race(&game.players[i]);

  mark_race_as_used(packet->race_no);
}

/**************************************************************************
...
**************************************************************************/
static void send_select_race(struct player *pplayer)
{
  int i;
  struct packet_generic_integer genint;

  genint.value=0;

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].race!=R_LAST)
      genint.value|=1<<game.players[i].race;

  send_packet_generic_integer(pplayer->conn, PACKET_SELECT_RACE, &genint);
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
  strcpy(packet.capability, our_capability);
  sprintf(packet.message, "%s %s.", (rejoin?"Welcome back":"Welcome"),
				     pplayer->name);
  send_packet_join_game_reply(pplayer->conn, &packet);
  freelog(LOG_NORMAL, "<%s@%s> has %sjoined the game.",
	  pplayer->name, pplayer->addr, (rejoin?"re":""));
  for(i=0; i<game.nplayers; ++i) {
    if (pplayer != &game.players[i]) {
      notify_player(&game.players[i],
	  	    "Game: Player <%s@%s> has %sconnected.",
		    pplayer->name, pplayer->addr, (rejoin?"re":""));
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
  int i, nconn, nother;
  
  if (!pplayer->conn)
    return;
  
  if (gethostname(hostname, 512)==0) {
    notify_player(pplayer, "Welcome to the %s Server running at %s", 
		  FREECIV_NAME_VERSION, hostname);
  } else {
    notify_player(pplayer, "Welcome to the %s Server",
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
                       "turn-blocking game play: waiting on %s to finish turn...",
                       game.players[i].name);
      }
    }
  }

  if (server_state==RUN_GAME_STATE) {
    /* if the game is running, players can just view the Players menu?  --dwp */
    return;
  }
  for(nconn=0, i=0; i<game.nplayers; ++i) {
    nconn += (game.players[i].is_connected);
  }
  if (nconn != 1) {
    notify_player(pplayer, "There are currently %d players connected:", nconn);
  } else {
    notify_player(pplayer, "There is currently 1 player connected:");
  }
  for(i=0; i<game.nplayers; ++i) {
    cplayer = &game.players[i];
    if (cplayer->is_connected) {
      notify_player(pplayer, "  <%s@%s>%s", cplayer->name, cplayer->addr,
		    ((!cplayer->is_alive) ? " (R.I.P.)"
		     :cplayer->ai.control ? " (AI mode)" : ""));
    }
  }
  nother = game.nplayers - nconn;
  if (nother > 0) {
    if (nother == 1) {
      notify_player(pplayer, "There is 1 other player:");
    } else {
      notify_player(pplayer, "There are %d other players:", nother);
    }
    for(i=0; i<game.nplayers; ++i) {
      cplayer = &game.players[i];
      if (!cplayer->is_connected) {
	notify_player(pplayer, "  %s%s", cplayer->name, 
		    ((!cplayer->is_alive) ? " (R.I.P.)"
		     :cplayer->ai.control ? " (an AI player)" : ""));
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

  player_init(pplayer);
  /* sometimes needed if players connect/disconnect to avoid
   * inheriting stale AI status etc
   */
  
  strcpy(pplayer->name, name);
  pplayer->conn = pconn;
  pplayer->is_connected = (pconn!=NULL);
  if (pconn) {
    strcpy(pplayer->addr, pconn->addr); 
    join_game_accept(pplayer, 0);
  } else {
    freelog(LOG_NORMAL, "%s has been added as an AI-controlled player.", name);
    notify_player(0, "Game: %s has been added as an AI-controlled player.",
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
  strcpy(packet.capability, our_capability);
  strcpy(packet.message, msg);
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
  
  freelog(LOG_NORMAL, "Connection request from %s with client version %d.%d.%d",
       req->name, req->major_version, req->minor_version, req->patch_version);
  freelog(LOG_VERBOSE, "Client caps: %s Server Caps: %s", req->capability,
       our_capability);
  strcpy(pconn->capability, req->capability);
  
  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(our_capability, req->capability)) {
    sprintf(msg, "The client is missing a capability that this server needs.\n"
	    "Server version: %d.%d.%d Client version: %d.%d.%d.  Upgrading may help!",
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION,
	    req->major_version, req->minor_version, req->patch_version);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, "%s was rejected: mismatched capabilities", req->name);
    close_connection(pconn);
    return;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, our_capability)) {
    sprintf(msg, "The server is missing a capability that the client needs.\n"
	    "Server version: %d.%d.%d Client version: %d.%d.%d.  Upgrading may help!",
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION,
	    req->major_version, req->minor_version, req->patch_version);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, "%s was rejected: mismatched capabilities", req->name);
    close_connection(pconn);
    return;
  }

  if((pplayer=find_player_by_name(req->name))) {
    if(!pplayer->conn) {
      pplayer->conn=pconn;
      pplayer->is_connected=1;
      strcpy(pplayer->addr, pconn->addr); 
      join_game_accept(pplayer, 1);
      introduce_game_to_player(pplayer);
      if(server_state==RUN_GAME_STATE) {
	send_all_info(pplayer);
        send_game_state(pplayer, CLIENT_GAME_RUNNING_STATE);
	send_player_info(NULL,NULL);
      }
      return;
    }

    if(server_state==PRE_GAME_STATE) {
      if(game.nplayers==game.max_players) {
	reject_new_player("Sorry you can't join. The game is full.", pconn);
	freelog(LOG_NORMAL, "game full - %s was rejected.", req->name);    
	close_connection(pconn);
        return;
      }
      else {
	/* Used to have here: accept_new_player(req->name, pconn); 
	 * but duplicate names cause problems even in PRE_GAME_STATE
	 * because they are used to identify players for various
	 * server commands. --dwp
	 */
	reject_new_player("Sorry, someone else already has that name.",
			  pconn);
	freelog(LOG_NORMAL, "%s was rejected, name already used.", req->name);
	close_connection(pconn);

        return;
      }
    }

    sprintf(msg, "You can't join the game. %s is already connected.", 
	    pplayer->name);
    reject_new_player(msg, pconn);
    freelog(LOG_NORMAL, "%s was rejected.", pplayer->name);
    close_connection(pconn);

    return;
  }

  /* unknown name */

  if(server_state!=PRE_GAME_STATE) {
    reject_new_player("Sorry you can't join. The game is already running.",
		      pconn);
    freelog(LOG_NORMAL, "game running - %s was rejected.", req->name);
    lost_connection_to_player(pconn);
    close_connection(pconn);

    return;
  }

  if(game.nplayers==game.max_players) {
    reject_new_player("Sorry you can't join. The game is full.", pconn);
    freelog(LOG_NORMAL, "game full - %s was rejected.", req->name);    
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
  int i;

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].conn==pconn) {
      game.players[i].conn=NULL;
      game.players[i].is_connected=0;
      strcpy(game.players[i].addr, "---.---.---.---");
      freelog(LOG_NORMAL, "lost connection to %s", game.players[i].name);
      send_player_info(&game.players[i], 0);
      notify_player(0, "Game: Lost connection to %s", game.players[i].name);

      if(is_new_game && (server_state==PRE_GAME_STATE ||
			 server_state==SELECT_RACES_STATE))
	 server_remove_player(&game.players[i]);
      check_for_full_turn_done();
      return;
    }

  freelog(LOG_FATAL, "lost connection to unknown");
}

/**************************************************************************
generate_ai_players() - Selects a race for players created with server's
   "create <PlayerName>" command.  If <PlayerName> is the default leader
   name for some race, we choose the corresponding race for that name
   (i.e. if we issue "create Shaka" then we will make that AI player's
   race the Zulus if the Zulus have not been chosen by anyone else.  If they
   have, then we pick an available race at random.

   After that, we check to see if the
   server option "aifill" is greater than the number of players currently
   connected.  If so, we create the appropriate number of players
   (game.aifill - game.nplayers) from scratch, choosing a random race
   and appropriate name for each.
**************************************************************************/

static void generate_ai_players(void)
{
  int i,player,race;
  char player_name[MAX_LEN_NAME];

  /* Select races for AI players generated with server 'create <name>' command */

  for(player=0;player<game.nplayers;player++) {
    if(game.players[player].race != R_LAST)
       continue;

    for (race = 0; race < R_LAST; race++) {
      if (!strcmp(game.players[player].name, default_race_leader_names[race]))
        if(races_used[race] != -1) {
           game.players[player].race=mark_race_as_used(race);
            break;
        }
    }

    if(race == R_LAST)
           game.players[player].race=
              mark_race_as_used(races_avail[myrand(num_races_avail)]);

    announce_ai_player(&game.players[player]);

  }

  /* Create and pick race and name for AI players needed to bring the
     total number of players == game.aifill */

  for(;game.nplayers < game.aifill;) {
     race = mark_race_as_used(races_avail[myrand(num_races_avail)]);
     pick_ai_player_name(race,player_name);
     i=game.nplayers;
     accept_new_player(player_name, NULL);
     if ((game.nplayers == i+1) && 
         !strcmp(player_name,game.players[i].name)) {
           game.players[i].race=race;
	   game.players[i].ai.control = !game.players[i].ai.control;
	   game.players[i].ai.skill_level = game.skill_level;
           announce_ai_player(&game.players[i]);
	   set_ai_level_direct(&game.players[i], game.players[i].ai.skill_level);
        } else
	  con_write(C_FAIL, "Error creating new ai player: %s\n", player_name);
   }

  send_server_info_to_metaserver(1,0);

}



/*************************************************************************
 pick_ai_player_name() - Returns the default ruler name for a given race
     given that race's number.  If that player name is already taken,
     iterates through "Player 1", "Player 2", ... until an unused name
     is found.
*************************************************************************/
void pick_ai_player_name (enum race_type race, char *newname) 
{
   int playernumber=1;
   char tempname[50];

   strcpy(tempname,default_race_leader_names[race]);

   while(find_player_by_name(tempname)) {
       sprintf(tempname,"Player %d",playernumber++);
   }

   strcpy(newname,tempname);
}

/*************************************************************************
 mark_race_as_used() - shuffles the appropriate arrays to indicate that
 the specified race number has been allocated to some player and is
 therefore no longer available to any other player.  We do things this way
 so that the process of determining which races are available to AI players
 is more efficient.
*************************************************************************/
static int mark_race_as_used (int race) {

  if(num_races_avail <= 0) {/* no more unused races */
      freelog(LOG_FATAL, "Argh! ran out of races!");
      exit(1);
  }

   races_used[races_avail[num_races_avail-1]]=races_used[race];
   races_avail[races_used[race]]=races_avail[--num_races_avail];
   races_used[race]=-1;

   return race;
}

/*************************************************************************
...
*************************************************************************/
static void announce_ai_player (struct player *pplayer) {
   int i;

   freelog(LOG_NORMAL, "AI is controlling the %s ruled by %s",
                    races[pplayer->race].name_plural,
                    pplayer->name);

   for(i=0; i<game.nplayers; ++i)
     notify_player(&game.players[i],
  	     "Game: %s rules the %s.", pplayer->name,
                    races[pplayer->race].name_plural);

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
