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
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <game.h>
#include <gamehand.h>
#include <player.h>
#include <civserver.h>
#include <log.h>
#include <sernet.h>
#include <map.h>
#include <mapgen.h>
#include <registry.h>
#include <plrhand.h>
extern char metaserver_info_line[256];

void cut_player_connection(char *playername);
void quit_game(void);
void show_help(void);
void show_players(void);

struct proto_settings {
  char *name;
  char *help;
  int *value;
  int afterstart;
  int min_value, max_value, default_value;
};

struct proto_settings settings[] = {
  { "xsize", "Width of map in squares", 
    &map.xsize, 0,  
    MAP_MIN_WIDTH, MAP_MAX_HEIGHT, MAP_DEFAULT_WIDTH},

  { "ysize", "Height of map in squares", 
    &map.ysize, 0,
    MAP_MIN_HEIGHT, MAP_MAX_HEIGHT, MAP_DEFAULT_HEIGHT},

  { "seed", "This single number defines the random sequence that generate the map\nSame seed will always produce same map.", 
    &map.seed, 0,
    MAP_MIN_SEED,MAP_MAX_SEED, MAP_DEFAULT_SEED},
  
  { "landmass", "This number defines the percentage of the map that becomes land.", 
    &map.landpercent, 0,
    MAP_MIN_LANDMASS, MAP_MAX_LANDMASS, MAP_DEFAULT_LANDMASS},

  { "specials", "This number donates a percentage chance that a square is special.",
    &map.riches, 0,
    MAP_MIN_RICHES, MAP_MAX_RICHES, MAP_DEFAULT_RICHES},

  { "swamps", "How many swamps to create on the map.",
    &map.swampsize, 0,
    MAP_MIN_SWAMPS, MAP_MAX_SWAMPS, MAP_DEFAULT_SWAMPS},

  { "settlers", "How many settlers each player starts with.",
    &game.settlers, 0,
    GAME_MIN_SETTLERS, GAME_MAX_SETTLERS, GAME_DEFAULT_SETTLERS},

  { "explorer", "How many explorer units the player starts with.",
    &game.explorer, 0,
    GAME_MIN_EXPLORER, GAME_MAX_EXPLORER, GAME_DEFAULT_EXPLORER},

  { "deserts", "How many deserts to create on the map.",
    &map.deserts, 0,
    MAP_MIN_DESERTS, MAP_MAX_DESERTS, MAP_DEFAULT_DESERTS},

  { "rivers", "Denotes the total length of the rivers on the map.",
    &map.riverlength, 0,
    MAP_MIN_RIVERS, MAP_MAX_RIVERS, MAP_DEFAULT_RIVERS},

  { "mountains", "How flat/high is the map, higher values give more mountains.", 
    &map.mountains, 0,
    MAP_MIN_MOUNTAINS, MAP_MAX_MOUNTAINS, MAP_DEFAULT_MOUNTAINS},

  { "forests", "How much forest to create, higher values give more forest.", 
    &map.forestsize, 0,
    MAP_MIN_FORESTS, MAP_MAX_FORESTS, MAP_DEFAULT_FORESTS},

  { "huts", "how many 'bonus huts' should be created.",
    &map.huts, 0,
    MAP_MIN_HUTS, MAP_MAX_HUTS, MAP_DEFAULT_HUTS},

  { "generator", "made a more fair mapgenerator (2), but it only supports 7 players, and works on 80x50 maps only", 
    &map.generator, 0,
    MAP_MIN_GENERATOR, MAP_MAX_GENERATOR, MAP_DEFAULT_GENERATOR}, 

  { "gold", "how much gold does each players start with.",
    &game.gold, 0,
    GAME_MIN_GOLD, GAME_MAX_GOLD, GAME_DEFAULT_GOLD},

  { "techlevel", "How many initial advances does each player have.",
    &game.tech, 0,
    GAME_MIN_TECHLEVEL, GAME_MAX_TECHLEVEL, GAME_DEFAULT_TECHLEVEL},

  { "researchspeed", "How fast do players gain technology.",
    &game.techlevel, 0,
    GAME_MIN_RESEARCHLEVEL, GAME_MAX_RESEARCHLEVEL, GAME_DEFAULT_RESEARCHLEVEL},

  { "diplcost", "How many % of the price of researching a tech, does a tech cost when you exchange it in a diplomatic treaty.",
    &game.diplcost, 0,
    GAME_MIN_DIPLCOST, GAME_MAX_DIPLCOST, GAME_DEFAULT_DIPLCOST},

  { "freecost", "How many % of the price of researching a tech, does a tech cost when you get it for free.",
    &game.freecost, 0,
    GAME_MIN_FREECOST, GAME_MAX_FREECOST, GAME_DEFAULT_FREECOST},

  { "conquercost", "How many % of the price of researching a tech, does a tech cost when you get it by military force.",
    &game.conquercost, 0,
    GAME_MIN_CONQUERCOST, GAME_MAX_CONQUERCOST, GAME_DEFAULT_CONQUERCOST},
  
  { "unhappysize", "When do people get angry in a city.",
    &game.unhappysize, 0,
    GAME_MIN_UNHAPPYSIZE, GAME_MAX_UNHAPPYSIZE, GAME_DEFAULT_UNHAPPYSIZE},

  { "cityfactor", "How many cities will it take to increase unhappy faces in cities.",
    &game.cityfactor, 0,
    GAME_MIN_CITYFACTOR, GAME_MAX_CITYFACTOR, GAME_DEFAULT_CITYFACTOR},
  { "railfood", "How many % railroads modifies food production.",
     &game.rail_food, 0,
     GAME_MIN_RAILFOOD, GAME_MAX_RAILFOOD, GAME_DEFAULT_RAILFOOD},
 
   { "railprod", "How many % railroads modifies shield production.",
     &game.rail_prod, 0,
     GAME_MIN_RAILPROD, GAME_MAX_RAILPROD, GAME_DEFAULT_RAILPROD},
 
   { "railtrade", "How many % railroads modifies trade production.",
     &game.rail_trade, 0,
     GAME_MIN_RAILTRADE, GAME_MAX_RAILTRADE, GAME_DEFAULT_RAILTRADE},
  { "foodbox", "Size * this parameter is what it takes for a city to grow by 1"
    ,&game.foodbox, 0,
    GAME_MIN_FOODBOX, GAME_MAX_FOODBOX, GAME_DEFAULT_FOODBOX},
  { "techpenalty", "% penalty if you change tech default it's 100%",
    &game.techpenalty, 0,
    GAME_MIN_TECHPENALTY, GAME_MAX_TECHPENALTY, GAME_DEFAULT_TECHPENALTY},
  { "razechance", "% chance that each building in town is destroyed when conquered",
    &game.razechance, 0,
    GAME_MIN_RAZECHANCE, GAME_MAX_RAZECHANCE, GAME_DEFAULT_RAZECHANCE},
 
  { "civstyle", "1= civ 1 (units, techs, buildings), 2= civ 2 style",
    &game.civstyle, 0,
    GAME_MIN_CIVSTYLE, GAME_MAX_CIVSTYLE, GAME_DEFAULT_CIVSTYLE},

  { "endyear", "In what year is the game over.", 
    &game.end_year, 1,
    GAME_MIN_END_YEAR, GAME_MAX_END_YEAR, GAME_DEFAULT_END_YEAR},

  { "minplayers", "How many players are needed to start the game.",
    &game.min_players, 1,
    GAME_MIN_MIN_PLAYERS, GAME_MAX_MIN_PLAYERS, GAME_DEFAULT_MIN_PLAYERS},
  
  { "maxplayers", "How many players are maximally wanted in game.",
    &game.max_players, 1,
    GAME_MIN_MAX_PLAYERS, GAME_MAX_MAX_PLAYERS, GAME_DEFAULT_MAX_PLAYERS},

  { "saveturns", "How many turns between when the game is saved.",
    &game.save_nturns, 1,
    0, 200, 10},
  { "timeout", "How many seconds can a turn max take (0 is no timeout).",
    &game.timeout, 1,
    0, 999, GAME_DEFAULT_TIMEOUT},

  { "aifill", "Maximum number of AI players to create when game starts",
    &game.aifill, 1,
    GAME_MIN_AIFILL, GAME_MAX_AIFILL, GAME_DEFAULT_AIFILL},

  { NULL, NULL, NULL, 0, 0, 0}
};


/**************************************************************************
...
**************************************************************************/
void meta_command(char *arg)
{
  strncpy(metaserver_info_line, arg, 256);
  metaserver_info_line[256-1]='\0';
  send_server_info_to_metaserver(1);
}


/**************************************************************************
...
**************************************************************************/
void save_command(char *arg)
{
  struct section_file file;
  
  if (server_state==SELECT_RACES_STATE || 
      (server_state == PRE_GAME_STATE && !map_is_empty()))
    {
      printf("Please wait until the game has started to save.\n");
      return;
    }

  section_file_init(&file);
  
  game_save(&file);

  if(!section_file_save(&file, arg))
    printf("Failed saving game as %s\n", arg);
  else
    printf("Game saved as %s\n", arg);
}

/**************************************************************************
...
**************************************************************************/
void toggle_ai_player(char *arg)
{
  struct player *pplayer=find_player_by_name(arg);
  if (!pplayer) {
    puts("No player by that name.");
    return;
  }
  pplayer->ai.control = !pplayer->ai.control;
  if (pplayer->ai.control) {
    notify_player(0, "Option: %s is AI-controlled.", pplayer->name);
    printf("%s is now under AI control.\n",pplayer->name);
  } else {
    notify_player(0, "Option: %s is human.", pplayer->name);
    printf("%s is now under human control.\n",pplayer->name);
  }
  send_player_info(pplayer,0);
}

/**************************************************************************
...
**************************************************************************/
void create_ai_player(char *arg)
{
  struct player *pplayer;
   
  if (server_state==PRE_GAME_STATE) {
     if(game.nplayers==game.max_players) 
       puts ("Can't add more players, server is full.");
     else if ((pplayer=find_player_by_name(arg))) 
       puts("A player already exists by that name.");
     else {
	accept_new_player(arg, NULL);
	pplayer = find_player_by_name(arg);
	if (!pplayer) 
	  printf ("Error creating new ai player: %s\n", arg);
	else {
	   pplayer->ai.control = !pplayer->ai.control;
	   pplayer->is_connected=0;
	   notify_player(0, "Option: %s has been added as an AI-controlled.",
			 pplayer->name);
	}
     }
  } else
     puts ("Can't add AI players once the game has begun.");
}


/**************************************************************************
...
**************************************************************************/
void remove_player(char *arg)
{
  struct player *pplayer=find_player_by_name(arg);
  
  if(!pplayer) {
    puts("No player by that name.");
    return;
  }

  server_remove_player(pplayer);
}

int lookup_cmd(char *find)
{
  int i;
  for (i=0;settings[i].name!=NULL;i++) 
    if (!strcmp(find, settings[i].name)) return i;
  
  return -1;
}

void help_command(char *str)
{
  char command[512], *cptr_s, *cptr_d;
  int cmd,i;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command) {
    cmd=lookup_cmd(command);
    if (cmd==-1) {
      puts("No help on that (yet).");
      return;
    }
    printf("%s: is set to %d\nEffect: %s\nMinimum %d, Maximum %d, Default %d\n"
	   , settings[cmd].name, *settings[cmd].value, settings[cmd].help, 
	   settings[cmd].min_value, settings[cmd].max_value, 
	   settings[cmd].default_value);
  } else {
  puts("------------------------------------------------------------------------------");
      puts("Help are defined on the following variables:");
  puts("------------------------------------------------------------------------------");
    for (i=0;settings[i].name;i++) {
      printf("%-19s%c",settings[i].name, ((i+1)%4) ? ' ' : '\n'); 
    }
    if ((i)%4!=0) puts("");
  puts("------------------------------------------------------------------------------");
  }
}
  
void report_server_options(struct player *pplayer)
{
  int i;
  char buffer[4096];
  char buf2[4096];
  char title[128];
  buffer[0]=0;
  sprintf(title, "%-20svalue  (min , max)\n", "Variable");

  for (i=0;settings[i].name;i++) {
    sprintf(buf2, "%-20s%c%-6d (%d,%d)\n", settings[i].name,(*settings[i].value==settings[i].default_value) ? '*' : ' ',  *settings[i].value, settings[i].min_value, settings[i].max_value);
    strcat(buffer, buf2);
  }
  page_player(pplayer, title, buffer);
  
}

void show_command(char *str)
{
  int i;
  puts("------------------------------------------------------------------------------");
  printf("%-20svalue  (min , max)\n", "Variable");
  puts("------------------------------------------------------------------------------");
  for (i=0;settings[i].name;i++) {
    printf("%-20s%c%-6d (%d,%d)\n", settings[i].name,(*settings[i].value==settings[i].default_value) ? '*' : ' ',  *settings[i].value, settings[i].min_value, settings[i].max_value);
  }
  puts("------------------------------------------------------------------------------");
  puts("* means that it's the default for the variable");
  puts("------------------------------------------------------------------------------");
}

void set_command(char *str) 
{
  char command[512], arg[512], *cptr_s, *cptr_d;
  int val, cmd;
  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';
  
  for(; *cptr_s && (*cptr_s != '-' && !isalnum(*cptr_s)); cptr_s++);

  for(cptr_d=arg; *cptr_s && (*cptr_s == '-' || isalnum(*cptr_s)); cptr_s++ , cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  cmd=lookup_cmd(command);
  if (cmd==-1) {
    puts("Undefined argument. Usage: set <variable> <value>.");
    return;
  }
  if (!settings[cmd].afterstart && !map_is_empty()) {
    puts("This setting can't be modified after game has started");
    return;
  }

  val=atoi(arg);
  if (val>=settings[cmd].min_value && val<=settings[cmd].max_value) {
    *(settings[cmd].value)=val;
    notify_player(0, "Option: %s", str);
  } else
    puts("Value out of range. Usage: set <variable> <value>.");
}

/**************************************************************************
...
**************************************************************************/
void handle_stdin_input(char *str)
{
  char command[512], arg[512], *cptr_s, *cptr_d;
  int i;

  for(cptr_s=str; *cptr_s && !isalnum(*cptr_s); cptr_s++);

  for(cptr_d=command; *cptr_s && isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  for(; *cptr_s && !isalnum(*cptr_s); cptr_s++);
  strncpy(arg, cptr_s, 511);
  arg[511]='\0';

  i=strlen(arg)-1;
  while(i>0 && isspace(arg[i]))
    arg[i--]='\0';
  
  if(!strcmp("remove", command))
    remove_player(arg);
  else if(!strcmp("save", command))
    save_command(arg);
  else if(!strcmp("meta", command))
    meta_command(arg);
  else if(!strcmp("h", command))
    show_help();
  else if(!strcmp("l", command))
    show_players();
  else if (!strcmp("ai", command))
    toggle_ai_player(arg);
  else if (!strcmp("create", command))
    create_ai_player(arg);
  else if(!strcmp("q", command))
    quit_game();
  else if(!strcmp("c", command))
    cut_player_connection(arg);
  else if (!strcmp("show",command)) 
    show_command(arg);
  else if (!strcmp("help",command)) 
    help_command(arg);
  else if (!strcmp("set", command)) 
    set_command(arg);
  else if(!strcmp("score", command)) {
    if(server_state==RUN_GAME_STATE)
      show_ending();
    else
      printf("The game must be running, before you can see the score.\n");
  }
  else if(server_state==PRE_GAME_STATE) {
    int plrs=0;
    int i;
    if(!strcmp("s", command)) {
      for (i=0;i<game.nplayers;i++) {
	if (game.players[i].conn || game.players[i].ai.control) plrs++ ;
      }
      if (plrs<game.min_players) 
	printf("Not enough players, game will not start.\n");
      else 
	start_game();
    }
    else
      printf("Unknown Command, try 'h' for help.\n");
  }
  else
    printf("Unknown Command, try 'h' for help.\n");  
  
  printf(">");
  fflush(stdout);
}

/**************************************************************************
...
**************************************************************************/
void cut_player_connection(char *playername)
{
  struct player *pplayer;

  pplayer=find_player_by_name(playername);
  if(pplayer && pplayer->conn) {
    log(LOG_NORMAL, "cutting connection to %s", playername);
    close_connection(pplayer->conn);
    pplayer->conn=NULL;
  }
  else
    puts("uh, no such player connected");
}

/**************************************************************************
...
**************************************************************************/
void quit_game(void)
{
  int i;
  struct packet_generic_message gen_packet;
  gen_packet.message[0]='\0';
  
  for(i=0; i<game.nplayers; i++)
    send_packet_generic_message(game.players[i].conn, PACKET_SERVER_SHUTDOWN,
				&gen_packet);
  close_connections_and_socket();
  
  exit(1);
}

/**************************************************************************
...
**************************************************************************/
void show_help(void)
{
  puts("Available commands");
  puts("-------------------------------------");
  puts("c P      - cut connection to player");
  puts("h        - this help");
  puts("l        - list players");
  puts("q        - quit game and shutdown server");
  puts("remove P - fully remove player from game");
  puts("score    - show current score");
  puts("save F   - save game as file F");
  puts("show     - list server options");
  puts("help     - help on server options");
  puts("meta T   - Set meta-server infoline to T");
  puts("ai P     - toggles AI on player");
  puts("create P - creates an AI player");
  if(server_state==PRE_GAME_STATE) {
    puts("set      - set options");
    puts("s        - start game");
  }
  else {
    
  }
}

/**************************************************************************
...
**************************************************************************/
void show_players(void)
{
  int i;
  char aichar;
  
  puts("List of players:");
  puts("-------------------------------------");

  for(i=0; i<game.nplayers; i++) {
    if (game.players[i].ai.control) 
      aichar = '*';
    else
      aichar = ' ';
    printf("%c%s ", aichar, game.players[i].name);
    if(game.players[i].conn)
      printf("is connected from %s\n", game.players[i].addr); 
    else
      printf("is not connected\n");
  }
}
