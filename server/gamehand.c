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
#include <string.h>

#include <civserver.h>
#include <game.h>
#include <gamehand.h>
#include <unitfunc.h>
#include <cityturn.h>
#include <maphand.h>
#include <plrhand.h>
#include <packets.h>
#include <map.h>
#include <meta.h>
#include <capability.h>
#include <time.h>

extern char metaserver_info_line[];

#define SAVEFILE_OPTIONS "1.7, scorelog"

/**************************************************************************
...
**************************************************************************/
void init_new_game(void)
{
  int i, x, y,j;
  if (!game.scenario) {
    for (i=0; i<game.nplayers;i++) { /* no advantage to the romans!! */
      j=myrand(game.nplayers);
      x=map.start_positions[j].x;
      y=map.start_positions[j].y;
      map.start_positions[j].x=map.start_positions[i].x;
      map.start_positions[j].y=map.start_positions[i].y;
      map.start_positions[i].x=x;
      map.start_positions[i].y=y;
    }
  }
  for(i=0; i<game.nplayers; i++) {
    x=map.start_positions[i].x;
    y=map.start_positions[i].y;
    light_square(&game.players[i], x, y, 1);
    for (j=0;j<game.settlers;j++) 
      create_unit(&game.players[i], x, y, U_SETTLERS, 0, 0, -1);
    for (j=0;j<game.explorer;j++) 
      create_unit(&game.players[i], x, y, U_EXPLORER, 0, 0, -1);
  }
}


/**************************************************************************
...
**************************************************************************/
void send_year_to_clients(int year)
{
  int i;
  struct packet_new_year apacket;
  apacket.year=year;

  for(i=0; i<game.nplayers; i++) {
    game.players[i].turn_done=0;
    game.players[i].nturns_idle++;
    send_packet_new_year(game.players[i].conn, &apacket);
  }
}


/**************************************************************************
...
**************************************************************************/
void send_game_state(struct player *dest, int state)
{
  int o;
  struct packet_generic_integer pack;

  pack.value=state;
  
  
  for(o=0; o<game.nplayers; o++)
    if(!dest || &game.players[o]==dest)
      send_packet_generic_integer(game.players[o].conn, 
				  PACKET_GAME_STATE, &pack);
}



/**************************************************************************
dest can be NULL meaning all players
**************************************************************************/
void send_game_info(struct player *dest)
{
  int i, o;
  struct packet_game_info ginfo;
  
  ginfo.gold=game.gold;
  ginfo.tech=game.tech;
  ginfo.techlevel=game.techlevel;
  ginfo.skill_level=game.skill_level;
  ginfo.timeout=game.timeout;
  ginfo.end_year=game.end_year;
  ginfo.year=game.year;
  ginfo.min_players=game.min_players;
  ginfo.max_players=game.max_players;
  ginfo.nplayers=game.nplayers;
  ginfo.globalwarming=game.globalwarming;
  ginfo.heating=game.heating;
  ginfo.techpenalty=game.techpenalty;
  ginfo.foodbox = game.foodbox;
  ginfo.civstyle=game.civstyle;
  ginfo.rail_food = game.rail_food;
  ginfo.rail_trade = game.rail_trade;
  ginfo.rail_prod = game.rail_prod;
  for(i=0; i<A_LAST; i++)
    ginfo.global_advances[i]=game.global_advances[i];
  for(i=0; i<B_LAST; i++)
    ginfo.global_wonders[i]=game.global_wonders[i];

  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest) {
      struct player *pplayer=&game.players[o];
      ginfo.player_idx=o;
      send_packet_game_info(pplayer->conn, &ginfo);
    }
}

/***************************************************************
...
***************************************************************/
int game_load(struct section_file *file)
{
  int i;
  enum server_states tmp_server_state;
  char *savefile_options=" ";

  if (section_file_lookup_internal(file, "game.version")) {
    game.version = secfile_lookup_int(file, "game.version");
  } else {
    game.version = 0;
  }
  if (section_file_lookup_internal(file, "game.server_state"))
    tmp_server_state  = (enum server_states) secfile_lookup_int(file, "game.server_state");
  else
    tmp_server_state = RUN_GAME_STATE;
  
  if(section_file_lookup_internal(file, "game.metastring")) {
    char *s=secfile_lookup_str(file, "game.metastring");
    strcpy(metaserver_info_line, s);
  }
  else
    strcpy(metaserver_info_line, DEFAULT_META_SERVER_INFO_STRING);
  
  game.gold          = secfile_lookup_int(file, "game.gold");
  game.tech          = secfile_lookup_int(file, "game.tech");
  game.skill_level   = secfile_lookup_int(file, "game.skill_level");
  game.timeout       = secfile_lookup_int(file, "game.timeout");
  game.end_year      = secfile_lookup_int(file, "game.end_year");
  game.techlevel     = secfile_lookup_int(file, "game.techlevel");
  game.year          = secfile_lookup_int(file, "game.year");
  game.min_players   = secfile_lookup_int(file, "game.min_players");
  game.max_players   = secfile_lookup_int(file, "game.max_players");
  game.nplayers      = secfile_lookup_int(file, "game.nplayers");
  game.globalwarming = secfile_lookup_int(file, "game.globalwarming");
  game.warminglevel  = secfile_lookup_int(file, "game.warminglevel");
  game.unhappysize   = secfile_lookup_int(file, "game.unhappysize");

  if (game.version >= 10100) {
    game.cityfactor = secfile_lookup_int(file, "game.cityfactor");
    game.diplcost   = secfile_lookup_int(file, "game.diplcost");
    game.freecost   = secfile_lookup_int(file, "game.freecost");
    game.conquercost   = secfile_lookup_int(file, "game.conquercost");
    game.rail_food  = secfile_lookup_int(file, "game.rail_food");
    game.rail_prod  = secfile_lookup_int(file, "game.rail_prod");
    game.rail_trade = secfile_lookup_int(file, "game.rail_trade");

    game.foodbox     = secfile_lookup_int(file, "game.foodbox");
    game.techpenalty = secfile_lookup_int(file, "game.techpenalty");
    game.razechance  = secfile_lookup_int(file, "game.razechance");
  }
  if (game.version >= 10300) {
    game.civstyle = secfile_lookup_int(file, "game.civstyle");
    game.save_nturns = secfile_lookup_int(file, "game.save_nturns");
  }

  if ((game.version == 10604 && section_file_lookup(file,"savefile.options"))
      || (game.version > 10604)) 
    savefile_options = secfile_lookup_str(file,"savefile.options");
  /* else leave savefile_options empty */

  /* Note -- as of v1.6.4 you should use savefile_options (instead of
     game.version) to determine which variables you can expect to 
     find in a savegame file */

  if (has_capability("1.6", savefile_options)) {
    game.aifill = secfile_lookup_int(file, "game.aifill");
  }

  if (has_capability("scorelog", savefile_options))
    game.scorelog = secfile_lookup_int(file, "game.scorelog");

  game.heating=0;
  if(tmp_server_state==PRE_GAME_STATE) {
    if (game.version >= 10300) {
      game.settlers = secfile_lookup_int(file, "game.settlers");
      game.explorer = secfile_lookup_int(file, "game.explorer");
      
      map.xsize = secfile_lookup_int(file, "map.xsize");
      map.ysize = secfile_lookup_int(file, "map.ysize");
      map.seed = secfile_lookup_int(file, "map.seed");
      map.landpercent = secfile_lookup_int(file, "map.landpercent");
      map.riches = secfile_lookup_int(file, "map.riches");
      map.swampsize = secfile_lookup_int(file, "map.swampsize");
      map.deserts = secfile_lookup_int(file, "map.deserts");
      map.riverlength = secfile_lookup_int(file, "map.riverlength");
      map.mountains = secfile_lookup_int(file, "map.mountains");
      map.forestsize = secfile_lookup_int(file, "map.forestsize");
      map.huts = secfile_lookup_int(file, "map.huts");
      map.generator = secfile_lookup_int(file, "map.generator");      
    }
    return 0;
  }
  map_load(file);

  for(i=0; i<game.nplayers; i++) {
    player_load(&game.players[i], i, file); 
  }
  initialize_globals();

  for(i=0; i<game.nplayers; i++) {
    /*    player_load(&game.players[i], i, file); */
    city_list_iterate(game.players[i].cities, pcity)
      city_refresh(pcity);
    city_list_iterate_end;
  }

  game.player_idx=0;
  game.player_ptr=&game.players[0];  

  return 1;
}



/***************************************************************
...
***************************************************************/
void game_save(struct section_file *file)
{
  int i;
  int version;
  version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION; 
  secfile_insert_int(file, version, "game.version");
  secfile_insert_int(file, (int) server_state, "game.server_state");
  secfile_insert_str(file, metaserver_info_line, "game.metastring");
  secfile_insert_str(file, SAVEFILE_OPTIONS, "savefile.options");

  
  secfile_insert_int(file, game.gold, "game.gold");
  secfile_insert_int(file, game.tech, "game.tech");
  secfile_insert_int(file, game.skill_level, "game.skill_level");
  secfile_insert_int(file, game.timeout, "game.timeout");
  secfile_insert_int(file, game.end_year, "game.end_year");
  secfile_insert_int(file, game.year, "game.year");
  secfile_insert_int(file, game.techlevel, "game.techlevel");
  secfile_insert_int(file, game.min_players, "game.min_players");
  secfile_insert_int(file, game.max_players, "game.max_players");
  secfile_insert_int(file, game.nplayers, "game.nplayers");
  secfile_insert_int(file, game.globalwarming, "game.globalwarming");
  secfile_insert_int(file, game.warminglevel, "game.warminglevel");
  secfile_insert_int(file, game.unhappysize, "game.unhappysize");
  secfile_insert_int(file, game.cityfactor, "game.cityfactor");
  secfile_insert_int(file, game.diplcost, "game.diplcost");
  secfile_insert_int(file, game.freecost, "game.freecost");
  secfile_insert_int(file, game.conquercost, "game.conquercost");
  secfile_insert_int(file, game.rail_food, "game.rail_food");
  secfile_insert_int(file, game.rail_prod, "game.rail_prod");
  secfile_insert_int(file, game.rail_trade, "game.rail_trade");
  secfile_insert_int(file, game.foodbox, "game.foodbox");
  secfile_insert_int(file, game.techpenalty, "game.techpenalty");
  secfile_insert_int(file, game.razechance, "game.razechance");
  secfile_insert_int(file, game.civstyle, "game.civstyle");
  secfile_insert_int(file, game.save_nturns, "game.save_nturns");
  secfile_insert_int(file, game.aifill, "game.aifill");
  secfile_insert_int(file, game.scorelog, "game.scorelog");

  if(server_state==PRE_GAME_STATE) {
    secfile_insert_int(file, game.settlers, "game.settlers");
    secfile_insert_int(file, game.explorer, "game.explorer");
    secfile_insert_int(file, map.xsize, "map.xsize");
    secfile_insert_int(file, map.ysize, "map.ysize");
    secfile_insert_int(file, map.seed, "map.seed");
    secfile_insert_int(file, map.landpercent, "map.landpercent");
    secfile_insert_int(file, map.riches, "map.riches");
    secfile_insert_int(file, map.swampsize, "map.swampsize");
    secfile_insert_int(file, map.deserts, "map.deserts");
    secfile_insert_int(file, map.riverlength, "map.riverlength");
    secfile_insert_int(file, map.mountains, "map.mountains");
    secfile_insert_int(file, map.forestsize, "map.forestsize");
    secfile_insert_int(file, map.huts, "map.huts");
    secfile_insert_int(file, map.generator, "map.generator");
    return;
  }

  for(i=0; i<game.nplayers; i++)
    player_save(&game.players[i], i, file);

  map_save(file);

}
