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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#ifdef __EMX__
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/termio.h>
#include <termios.h>
#endif

#include <time.h>
#include <registry.h>
#include <game.h>
#include <gamehand.h>
#include <civserver.h>
#include <map.h>
#include <shared.h>
#include <city.h>
#include <tech.h>
#include <sernet.h>
#include <log.h>
#include <packets.h>
#include <unithand.h>
#include <unitfunc.h>
#include <cityhand.h>
#include <cityturn.h>
#include <handchat.h>
#include <maphand.h>
#include <player.h>
#include <plrhand.h>
#include <mapgen.h>
#include <diplhand.h>
#include <packets.h>
#include <stdinhand.h>
#include <meta.h>
#include <advmilitary.h>
#include <aihand.h>
#include <capability.h>
#include <settlers.h>

void show_ending();
void end_game();
void before_end_year(); /* main() belongs at the bottom of files!! -- Syela */
int end_turn();
void start_new_game(void);
void init_new_game(void);
void send_game_state(struct player *dest, int state);
int find_highest_used_id(void);
void send_all_info(struct player *dest);
void shuffle_players(void);
void ai_start_turn(void);
int is_game_over();
void read_init_script(char *script_filename);

extern struct connection connections[];

enum server_states server_state;

/* this global is checked deep down the netcode. 
   packets handling functions can set it to none-zero, to
   force end-of-tick asap
*/
int force_end_of_sniff;
char metaserver_info_line[256];
/* server name for metaserver to use for us */
char metaserver_servername[64]="";

struct player *shuffled[MAX_PLAYERS];

/* this counter creates all the id numbers used */
/* use get_next_id_number()                     */
unsigned short global_id_counter=100;
unsigned char used_ids[8192]={0};

int is_new_game=1;

char usage[] = 
"Usage: %s [-fhlpv] [--file] [--help] [--log] [--port]\n\t[--version]\n";

int port=DEFAULT_SOCK_PORT;
int nocity_send=0;

/* These are the capability strings for the client and the server. */
char c_capability[MSG_SIZE]="";
char s_capability[MSG_SIZE]="";

/* The next three variables make selecting races for AI players cleaner */
int races_avail[R_LAST];
int races_used[R_LAST];
int num_races_avail=R_LAST;

/**************************************************************************
...
**************************************************************************/
int main(int argc, char *argv[])
{
  int h=0, v=0, n=0;
  char *log_filename=NULL;
  char *load_filename=NULL;
  char *script_filename=NULL;
  int i;
  int save_counter;

  strcpy(metaserver_info_line, DEFAULT_META_SERVER_INFO_STRING);

  /* no  we don't use GNU's getopt or even the "standard" getopt */
  /* yes we do have reasons ;)                                   */
  i=1;
  while(i<argc) {
    if(!strcmp("-f", argv[i]) || !strcmp("--file", argv[i])) { 
      if(++i<argc) 
	load_filename=argv[i];
      else {
	fprintf(stderr, "Error: filename not specified.\n");
	h=1;
	break;
      }
    }
    else if(!strcmp("-h", argv[i]) || !strcmp("--help", argv[i])) { 
      h=1;
      break;
    }
    else if(!strcmp("-l", argv[i]) || !strcmp("--log", argv[i])) { 
      if(++i<argc) 
	log_filename=argv[i];
      else {
	fprintf(stderr, "Error: filename not specified.\n");
	h=1;
	break;
      }
    }
    else if(!strcmp("-n", argv[i]) || !strcmp("--nometa", argv[i])) { 
      n=1;
    }
    else if(!strcmp("-p", argv[i]) || !strcmp("--port", argv[i])) { 
      if(++i<argc) 
	port=atoi(argv[i]);
      else {
	fprintf(stderr, "Error: port not specified.\n");
	h=1;
	break;
      }
    }
    else if(!strcmp("-r", argv[i]) || !strcmp("--read", argv[i])) { 
      if (++i<argc)
	script_filename=argv[i];
      else {
	fprintf(stderr, "Error: read script not specified.\n");
	h=1;
	break;
      }
    }
    else if(!strcmp("-s", argv[i]) || !strcmp("--server", argv[i])) { 
      if(++i<argc) 
      	strcpy(metaserver_servername,argv[i]);
      else {
	fprintf(stderr, "Error: no name specified.\n");
	h=1;
	break;
      }
    }
    else if(!strcmp("-v", argv[i]) || !strcmp("--version", argv[i])) { 
      v=1;
    }
    else {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      h=1;
      break;
    }
    i++;
  }
    
  if(h) {
    fprintf(stderr, "This is the Freeciv server\n");
    fprintf(stderr, "  -f, --file F\t\t\tLoad saved game F\n");
    fprintf(stderr, "  -h, --help\t\t\tPrint a summary of the options\n");
    fprintf(stderr, "  -l, --log F\t\t\tUse F as logfile\n");
    fprintf(stderr, "  -n, --nometa\t\t\tDon't send info to Metaserver\n");
    fprintf(stderr, "  -p, --port N\t\t\tconnect to port N\n");
    fprintf(stderr, "  -r, --read\t\t\tRead startup script\n");
    fprintf(stderr, "  -s, --server H\t\tList this server as host H\n");
    fprintf(stderr, "  -v, --version\t\t\tPrint the version number\n");
    exit(0);
  }

  if(v) {
    fprintf(stderr, FREECIV_NAME_VERSION "\n");
    exit(0);
  }

  log_init(log_filename);
  log_set_level(LOG_NORMAL);
  
  printf(FREECIV_NAME_VERSION " server\n> ");
#if MINOR_VERSION < 7
  printf("Freeciv 1.7 will be released on August 5 from http://www.freeciv.org\n");
#endif
  fflush(stdout);

  strcpy(s_capability, CAPABILITY);
  if (getenv("FREECIV_CAPS"))
    strcpy(s_capability, getenv("FREECIV_CAPS"));

  game_init();
  initialize_city_cache();

  /* load a saved game */
  
  if(load_filename) {
    clock_t loadtime;
    struct section_file file;
    flog(LOG_NORMAL,"Loading saved game: %s", load_filename);
    loadtime = clock();
    if(!section_file_load(&file, load_filename)) { 
      flog(LOG_FATAL, "Couldn't load savefile: %s", load_filename);
      exit(1);
    }
    if (game_load(&file)) {
      section_file_free(&file);
      if(game.nplayers)
	is_new_game=0;
      else
	game.scenario=1;
      while(is_id_allocated(global_id_counter++));
    } else 
      section_file_free(&file);
    loadtime = clock() - loadtime;
    flog(LOG_DEBUG,"Load time: %g seconds", (float)loadtime/CLOCKS_PER_SEC);
  }
  
  /* init network */  
  init_connections(); 
  server_open_socket();
  if(n==0) {
    flog(LOG_NORMAL, "Sending info to metaserver[%s %d]", METASERVER_ADDR, METASERVER_PORT);
    server_open_udp(); /* open socket for meta server */ 
  }

  send_server_info_to_metaserver(1);
  
  /* accept new players, wait for serverop to start..*/
  flog(LOG_NORMAL, "Now accepting new client connections");
  server_state=PRE_GAME_STATE;

  if (script_filename)
    read_init_script(script_filename);

  while(server_state==PRE_GAME_STATE)
    sniff_packets();

  send_server_info_to_metaserver(1);
  
  for(i=0; i<R_LAST;i++) {
      races_avail[i]=i;
      races_used[i]=i;
  }

  /* allow players to select a race(case new game} */
/* come on, this can't be right!
  for(i=0; i<game.nplayers && game.players[i].race!=R_LAST
      && !game.players[i].ai.control; i++); */
  for(i=0; i<game.nplayers && game.players[i].race!=R_LAST; i++);
  
  /* at least one player is missing a race */
  if(i<game.nplayers) { 
/* this is bizarre!  Who wrote this and how long has it been this way?? -- Syela */
    send_select_race(&game.players[i]);
    for(i=0; i<game.nplayers; i++)
      send_select_race(&game.players[i]);

    while(server_state==SELECT_RACES_STATE) {
      sniff_packets();
      for(i=0; i<game.nplayers; i++)
	if((game.players[i].race==R_LAST) && !game.players[i].ai.control)
	  break;
      if(i==game.nplayers)
	server_state=RUN_GAME_STATE;
    }
  }

  generate_ai_players();
   
  if(map_is_empty())
    map_fractal_generate();
  else 
    flood_it(1);
  /* start the game */

  set_civ_style(game.civstyle);

  server_state=RUN_GAME_STATE;
  send_server_info_to_metaserver(1);

  if(is_new_game) {
    int i;
    for(i=0; i<game.nplayers; i++) {
      init_tech(&game.players[i], game.tech); 
      game.players[i].economic.gold=game.gold;
    }
    game.max_players=game.nplayers;
    flood_it(game.scenario);
    choose_start_positions();
  }

  initialize_move_costs(); /* this may be the wrong place to do this */
  generate_minimap(); /* for city_desire; saves a lot of calculations */

  if (!is_new_game) {
    set_ai_level("", game.skill_level);
    for (i=0;i<game.nplayers;i++) {
      civ_score(&game.players[i]);  /* if we don't, the AI gets really confused */
      if (game.players[i].ai.control) {
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
  
  save_counter=game.save_nturns;
  
  while(server_state==RUN_GAME_STATE) {
    force_end_of_sniff=0;
/* printf("Shuffleplayers\n"); */
    shuffle_players();
/* printf("Aistartturn\n"); */
    ai_start_turn();
/* printf("sniffingpackets\n"); */
    while(sniff_packets()==1);
    for(i=0;i<game.nplayers;i++)
      connection_do_buffer(game.players[i].conn);
/* printf("Autosettlers\n"); */
    auto_settlers(); /* moved this after ai_start_turn for efficiency -- Syela */
/* moved after sniff_packets for even more efficiency.  What a guy I am. -- Syela */
    before_end_year(); /* resetting David P's message window -- Syela */
/* and now, we must manage our remaining units BEFORE the cities that are
empty get to refresh and defend themselves.  How totally stupid. */
    ai_start_turn(); /* Misleading name for manage_units -- Syela */
/* printf("Endturn\n"); */
    end_turn();
/* printf("Gamenextyear\n"); */
    game_next_year();
/* printf("Sendplayerinfo\n"); */
    send_player_info(0, 0);
/* printf("Sendgameinfo\n"); */
    send_game_info(0);
/* printf("Sendyeartoclients\n"); */
    send_year_to_clients(game.year);
/* printf("Sendinfotometaserver\n"); */
    send_server_info_to_metaserver(0);
    for(i=0;i<game.nplayers;i++)
      connection_do_unbuffer(game.players[i].conn);
      
    if(--save_counter==0) {
      save_counter=game.save_nturns;
      save_game();
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

int is_game_over()
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
void read_init_script(char *script_filename)
{
  FILE *script_file;
  char buffer[512];

  script_file = fopen(script_filename,"r");
  if (script_file)
    {
      /* the size 511 is set as to not overflow buffer in handle_stdin_input */
      while(fgets(buffer,511,script_file))
	handle_stdin_input(buffer);
      fclose(script_file);
    }
  else
    fprintf(stderr,"Could not open script file '%s'.\n",script_filename);
}

/**************************************************************************
...
**************************************************************************/

void send_server_info_to_metaserver(int do_send)
{
  static int time_last_send;
  int time_now;
  
  time_now=time(NULL);
  
  if(do_send || time_now-time_last_send>METASERVER_UPDATE_INTERVAL) {
    char desc[4096], info[4096];
    int i;
  
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

    time_last_send=time_now;
    send_to_metaserver(desc, info);
  }
    
}

/**************************************************************************
dest can be NULL meaning all players
**************************************************************************/
void send_all_info(struct player *dest)
{
  send_game_info(dest);
  send_map_info(dest);
  send_player_info(0, dest);
  send_all_known_tiles(dest);
}

/**************************************************************************
...
**************************************************************************/
int find_highest_used_id(void)
{
  int i, max_id;
  struct genlist_iterator myiter;

  max_id=0;
  for(i=0; i<game.nplayers; i++) {
    genlist_iterator_init(&myiter, &game.players[i].cities.list, 0);
    for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
      max_id=MAX(max_id, ((struct city *)ITERATOR_PTR(myiter))->id);
    genlist_iterator_init(&myiter, &game.players[i].units.list, 0);
    for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
      max_id=MAX(max_id, ((struct unit *)ITERATOR_PTR(myiter))->id);
  }

  return max_id;
}
/**************************************************************************
...
**************************************************************************/
void do_apollo_program()
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
void update_pollution()
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
      fprintf(stderr, "Global warming:%d\n", count);
      global_warming(map.xsize/10+map.ysize/10+game.globalwarming*5);
      game.globalwarming=0;
      send_all_known_tiles(0);
      notify_player(0, "Game: Global warming has occurred! Coastlines have been flooded\nand vast ranges of grassland have become deserts.");
      game.warminglevel+=4;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void before_end_year()
{
  int i;
  for (i=0; i<game.nplayers; i++) {
    send_packet_before_end_year(game.players[i].conn);
  }
}

/**************************************************************************
...
**************************************************************************/
void shuffle_players()
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

void ai_start_turn()
{
  int i;
  for (i = 0; i < game.nplayers; i++) {
    if (shuffled[i]->ai.control) ai_do_first_activities(shuffled[i]);
  }
}

int end_turn()
{
  int i;
  nocity_send = 1;

  for(i=0; i<game.nplayers; i++) {
/*    printf("updating player activities for #%d\n", i); */
    update_player_activities(shuffled[i]); /* ai unit activity has been moved UP -- Syela */
    shuffled[i]->turn_done=0;
  }
  nocity_send = 0;
  for (i=0; i<game.nplayers;i++) {
    send_player_cities(&game.players[i]);
  }
  update_pollution();
  do_apollo_program();
  make_history_report();
/*  printf("Turn ended.\n"); */
  return 1;
}


/**************************************************************************
...
**************************************************************************/

void end_game()
{
}

/**************************************************************************
...
**************************************************************************/

void save_game(void)
{
  char filename[512];
  struct section_file file;
  
  sprintf(filename, "%s%d.sav", game.save_name, game.year);
  
  section_file_init(&file);
  
  game_save(&file);
  
  if(!section_file_save(&file, filename))
    printf("Failed saving game as %s\n", filename);
  else
    printf("Game saved as %s\n", filename);

  section_file_free(&file);
}

/**************************************************************************
...
**************************************************************************/

void start_game(void)
{
  if(server_state!=PRE_GAME_STATE) {
    puts("the game is already running.");
    return;
  }

  puts("starting game.");

  server_state=SELECT_RACES_STATE; /* loaded ??? */
  force_end_of_sniff=1;
}


/**************************************************************************
...
**************************************************************************/
void handle_report_request(struct player *pplayer, enum report_type type)
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
  case REPORT_SERVER_OPTIONS:
    report_server_options(pplayer);
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
    flog(LOG_DEBUG, "got game packet from unaccepted connection");
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
  default:
    flog(LOG_NORMAL, "uh got an unknown packet from %s", game.players[i].name);
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

  for(i=0; i<game.nplayers; i++)
    if(game.players[i].conn && game.players[i].is_alive && 
       !game.players[i].turn_done) {
      return 0;
    }
  force_end_of_sniff=1;
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void handle_turn_done(int player_no)
{
  game.players[player_no].turn_done=1;
  
  /* if someone is still moving, then let the other know that this play moved */ 
  if(!check_for_full_turn_done())
    send_player_info(&game.players[player_no], 0);
}

/**************************************************************************
...
**************************************************************************/
void handle_alloc_race(int player_no, struct packet_alloc_race *packet)
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

  flog(LOG_NORMAL, "%s is the %s ruler %s", game.players[player_no].name, 
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
void send_select_race(struct player *pplayer)
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
...
**************************************************************************/

void accept_new_player(char *name, struct connection *pconn)
{
  int i;
  struct packet_join_game_reply packet;
  char hostname[512];

  packet.you_can_join=1;
  strcpy(packet.capability, CAPABILITY);

  strcpy(game.players[game.nplayers].name, name);
  game.players[game.nplayers].conn=pconn;
  game.players[game.nplayers].is_connected=1;
  if (pconn) {
    strcpy(game.players[game.nplayers].addr, pconn->addr); 
    sprintf(packet.message, "Welcome %s.", name);
    send_packet_join_game_reply(pconn, &packet);
    flog(LOG_NORMAL, "%s[%s] has joined the game.", name, pconn->addr);
    for(i=0; i<game.nplayers; ++i)
      notify_player(&game.players[i],
	  	    "Game: Player %s[%s] has connected.", name, pconn->addr);
  } else {
     flog(LOG_NORMAL, "%s[ai-player] has joined the game.", name, pconn->addr);
     for(i=0; i<game.nplayers; ++i)
       notify_player(&game.players[i],
		     "Game: Player %s[ai-player] has connected.", name, pconn->addr);
  }
  game.nplayers++;
  if(server_state==PRE_GAME_STATE && game.max_players==game.nplayers)
    server_state=SELECT_RACES_STATE;

  /* now notify the player about the server etc */
  if (pconn && (gethostname(hostname, 512)==0)) {
     notify_player(&game.players[game.nplayers-1],
		   "Welcome to the %s Server running at %s", 
		   FREECIV_NAME_VERSION, hostname);
     notify_player(&game.players[game.nplayers-1],
		   "There's currently %d player%s connected:", game.nplayers,
		   game.nplayers>1 ? "s" : "");
     
     for(i=0; i<game.nplayers; ++i)
       notify_player(&game.players[game.nplayers-1], "%s[%s]", 
		     game.players[i].name, game.players[i].addr);
  }
  send_server_info_to_metaserver(1);
}
  
/**************************************************************************
...
**************************************************************************/

void reject_new_player(char *msg, struct connection *pconn)
{
  struct packet_join_game_reply packet;
  
  packet.you_can_join=0;
  strcpy(packet.capability, s_capability);
  strcpy(packet.message, msg);
  send_packet_join_game_reply(pconn, &packet);
}
  
/**************************************************************************
...
**************************************************************************/

void handle_request_join_game(struct connection *pconn, 
			      struct packet_req_join_game *req)
{
  struct player *pplayer;
  char msg[MSG_SIZE];
  
  flog(LOG_NORMAL, "Connection from %s with client version %d.%d.%d", req->name,
      req->major_version, req->minor_version, req->patch_version);
#if 0
  flog(LOG_NORMAL, "Client caps: %s Server Caps: %s", req->capability, s_capability);
#endif
  /* Make sure the server has every capability the client needs */
  if (!has_capabilities(s_capability, req->capability)) {
    sprintf(msg, "The server is missing a capability that this client needs.\n"
	    "Server version: %d.%d.%d Client version: %d.%d.%d.  Upgrading may help!",
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION,
	    req->major_version, req->minor_version, req->patch_version);
    reject_new_player(msg, pconn);
    flog(LOG_NORMAL, "%s was rejected: mismatched capabilities", req->name);
    close_connection(pconn);
    return;
  }

  /* Make sure the client has every capability the server needs */
  if (!has_capabilities(req->capability, s_capability)) {
    sprintf(msg, "The client is missing a capability that the server needs.\n"
	    "Server version: %d.%d.%d Client version: %d.%d.%d.  Upgrading may help!",
	    MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION,
	    req->major_version, req->minor_version, req->patch_version);
    reject_new_player(msg, pconn);
    flog(LOG_NORMAL, "%s was rejected: mismatched capabilities", req->name);
    close_connection(pconn);
    return;
  }

  if((pplayer=find_player_by_name(req->name))) {
    if(!pplayer->conn) {
      /* can I use accept_new_player here?? mjd */
      struct packet_join_game_reply packet;

      pplayer->conn=pconn;
      pplayer->is_connected=1;
      strcpy(pplayer->addr, pconn->addr); 
      sprintf(packet.message, "Welcome back %s.", pplayer->name);
      packet.you_can_join=1;
      strcpy(packet.capability, s_capability);
      send_packet_join_game_reply(pconn, &packet);
      flog(LOG_NORMAL, "%s has reconnected.", pplayer->name);
      if(server_state==RUN_GAME_STATE) {
	send_all_info(pplayer);
        send_game_state(pplayer, CLIENT_GAME_RUNNING_STATE);
      }

      return;
    }

    if(server_state==PRE_GAME_STATE) {
      if(game.nplayers==game.max_players) {
	reject_new_player("Sorry you can't join. The game is full.", pconn);
	flog(LOG_NORMAL, "game full - %s was rejected.", req->name);    
	close_connection(pconn);

        return;
      }
      accept_new_player(req->name, pconn);

      return;
    }

    sprintf(msg, "You can't join the game. %s is already connected.", 
	    pplayer->name);
    reject_new_player(msg, pconn);
    flog(LOG_NORMAL, "%s was rejected.", pplayer->name);
    close_connection(pconn);

    return;
  }

  /* unknown name */

  if(server_state!=PRE_GAME_STATE) {
    reject_new_player("Sorry you can't join. The game is already running.",
		      pconn);
    flog(LOG_NORMAL, "game running - %s was rejected.", req->name);
    lost_connection_to_player(pconn);
    close_connection(pconn);

    return;
  }

  if(game.nplayers==game.max_players) {
    reject_new_player("Sorry you can't join. The game is full.", pconn);
    flog(LOG_NORMAL, "game full - %s was rejected.", req->name);    
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
      flog(LOG_NORMAL, "lost connection to %s", game.players[i].name);
      send_player_info(&game.players[i], 0);
      notify_player(0, "Game: Lost connection to %s", game.players[i].name);

      if(is_new_game && (server_state==PRE_GAME_STATE ||
			 server_state==SELECT_RACES_STATE))
	 server_remove_player(&game.players[i]);
      check_for_full_turn_done();
      return;
    }

  flog(LOG_FATAL, "lost connection to unknown");
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

void generate_ai_players()
{
  int i,j,player,race;
  char player_name[MAX_LENGTH_NAME];

  /* My attempt to randomly seed the PRNG.
     Without this we get the same AI players in every game -- at least
     under Linux. Maybe this should be near the beginning of main()? 
     - Cedric */

  j=time(NULL)%60;
  for(i=0;i<j;i++)
      myrand(1);

/* Editorial comment: Seeding the PRNG hinders development in many cases,
but since this only happens at the start of a brand-new game, I don't
see it as particularly harmful and am therefore leaving it intact. -- Syela */

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
	   game.players[i].is_connected=0;
           announce_ai_player(&game.players[i]);
        } else
	  printf ("Error creating new ai player: %s\n", player_name);
   }

  send_server_info_to_metaserver(1);

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
int mark_race_as_used (int race) {

  if(num_races_avail <= 0) {/* no more unused races */
      flog(LOG_FATAL, "Argh! ran out of races!");
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
void announce_ai_player (struct player *pplayer) {
   int i;

   flog(LOG_NORMAL, "AI is controlling the %s ruled by %s",
                    races[pplayer->race].name_plural,
                    pplayer->name);

   for(i=0; i<game.nplayers; ++i)
     notify_player(&game.players[i],
  	     "Option: %s rules the %s.", pplayer->name,
                    races[pplayer->race].name_plural);

}
