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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "registry.h"

#include "cityhand.h"
#include "plrhand.h"           /* notify_player */
#include "unitfunc.h"

#include "maphand.h"


static char terrain_chars[] = "adfghjm prst";
static char dec2hex[] = "0123456789abcdef";


/**************************************************************************
...
**************************************************************************/
void global_warming(int effect)
{
  int x, y;
  int k=map.xsize*map.ysize;
  while(effect && k--) {
    x=myrand(map.xsize);
    y=myrand(map.ysize);
    if (map_get_terrain(x, y)!=T_OCEAN) {
      if(is_water_adjacent(x, y)) {
	switch (map_get_terrain(x, y)) {
	case T_FOREST:
	  effect--;
	  map_set_terrain(x, y, T_JUNGLE);
          reset_move_costs(x, y);
          relight_square_if_known(0, x, y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  map_set_terrain(x, y, T_SWAMP);
	  map_clear_special(x, y, S_FARMLAND);
	  map_clear_special(x, y, S_IRRIGATION);
          reset_move_costs(x, y);
          relight_square_if_known(0, x, y);
	  break;
	default:
	  break;
	}
      } else {
	switch (map_get_terrain(x, y)) {
	case T_PLAINS:
	case T_GRASSLAND:
	case T_FOREST:
	  effect--;
	  map_set_terrain(x, y, T_DESERT);
          reset_move_costs(x, y);
          relight_square_if_known(0, x, y);
	  break;
	default:
	  break;
	}
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void give_map_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  int x, y;

  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      if(map_get_known(x, y, pfrom) && !map_get_known(x, y, pdest)) {
	light_square(pdest, x, y, 0);
      }
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  int x, y;

  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      if(map_get_known(x, y, pfrom) && !map_get_known(x, y, pdest) &&
	 map_get_terrain(x, y)==T_OCEAN) {
	light_square(pdest, x, y, 0);
      }
}

/**************************************************************************
dest can be NULL meaning all players
**************************************************************************/
void send_all_known_tiles(struct player *dest)
{
  int o;
 
  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest) {
      int x, y;
      struct player *pplayer=&game.players[o];
      
      connection_do_buffer(pplayer->conn);
      for(y=0; y<map.ysize; y++)
        for(x=0; x<map.xsize; x++)
          if(map_get_known(x, y, pplayer)) {
            map_clear_known(x, y, pplayer);
            light_square(pplayer, x, y, 0);
          }
      connection_do_unbuffer(pplayer->conn);
    }
}

/**************************************************************************
dest can be NULL meaning all players
if(dest==NULL) only_send_tile_info_to_client_that_know_the_tile()
else send_tile_info
notice: the tile isn't lighten up on the client by this func
**************************************************************************/
void send_tile_info(struct player *dest, int x, int y, enum known_type type)
{
  int o;
  struct packet_tile_info info;
  struct tile *ptile;

  ptile=map_get_tile(x, y);

  info.x=x;
  info.y=y;
  info.type=ptile->terrain;
  info.special=ptile->special;

  for(o=0; o<game.nplayers; o++)           /* dests */
     if(!dest) {
       if(ptile->known & (1<<o)) {
	 info.known=TILE_KNOWN;
	 send_packet_tile_info(game.players[o].conn, &info);
       }
     }
     else if(&game.players[o]==dest) {
       info.known=type;
       send_packet_tile_info(game.players[o].conn, &info);
     }
}

/**************************************************************************
...
**************************************************************************/
void relight_square_if_known(struct player *pplayer, int x, int y)
{
  int o;
  for(o=0; o<game.nplayers; o++) {
    struct player *pplayer=&game.players[o];
    if(map_get_known(x, y, pplayer)) {
      connection_do_buffer(pplayer->conn);
      map_clear_known(x, y, pplayer);
      light_square(pplayer, x, y, 0);
      connection_do_unbuffer(pplayer->conn);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void light_square(struct player *pplayer, int x, int y, int len)
{
  int dx, dy;
  static int known_count=TILE_KNOWN;
  
  known_count++;
  if(known_count>255) known_count=TILE_KNOWN+1;

  connection_do_buffer(pplayer->conn);
  for(dy=-len-1; dy<=len+1; ++dy)
    for(dx=-len-1; dx<=len+1; ++dx) {
      int abs_x=map_adjust_x(x+dx);
      int abs_y=map_adjust_y(y+dy);
      if(!map_get_known(abs_x, abs_y, pplayer)) {
	if(dx>=-len && dx<=len && dy>=-len && dy<=len)
	  send_tile_info(pplayer, abs_x, abs_y, TILE_KNOWN_NODRAW);
	else
	  send_tile_info(pplayer, abs_x, abs_y, TILE_UNKNOWN);
      }
    }

  for(dy=-len; dy<=len; ++dy)
    for(dx=-len; dx<=len; ++dx) {
      int abs_x=map_adjust_x(x+dx);
      int abs_y=map_adjust_y(y+dy);
      if(!map_get_known(abs_x, abs_y, pplayer)) {
	unit_list_iterate(map_get_tile(abs_x, abs_y)->units, punit)
	  send_unit_info(pplayer, punit, 1);
        unit_list_iterate_end;
      }
    }
    
  
  for(dy=-len; dy<=len; ++dy)
    for(dx=-len; dx<=len; ++dx) {
      int abs_x=map_adjust_x(x+dx);
      int abs_y=map_adjust_y(y+dy);
      if(!map_get_known(abs_x, abs_y, pplayer)) {
	struct city *pcity;
	
	if((pcity=map_get_city(abs_x, abs_y))) {
	  send_city_info(pplayer, pcity, 1);
	}
      }
    }
  
  for(dy=-len; dy<=len; ++dy)
    for(dx=-len; dx<=len; ++dx) {
      int abs_x=map_adjust_x(x+dx);
      int abs_y=map_adjust_y(y+dy);

      if(!map_get_known(abs_x, abs_y, pplayer)) {
	map_set_known(abs_x, abs_y, pplayer);
	send_tile_info(pplayer, abs_x, abs_y, known_count);
      }
    }
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
...
**************************************************************************/
void lighten_area(struct player *pplayer, int x, int y)
{
  connection_do_buffer(pplayer->conn);

  light_square(pplayer, x, y, 1);
  
  connection_do_unbuffer(pplayer->conn);
}

/***************************************************************
To be called when a player gains the Railroad tech for the first
time.  Sends a message, and then upgrade all city squares to
railroads.  "discovery" just affects the message: set to
   1 if the tech is a "discovery",
   0 if otherwise aquired (conquer/trade/GLib).        --dwp
***************************************************************/
void upgrade_city_rails(struct player *pplayer, int discovery)
{
  if (!(terrain_control.may_road)) {
    return;
  }

  connection_do_buffer(pplayer->conn);

  if (discovery) {
    notify_player(pplayer, "Game: New hope sweeps like fire through the country as the discovery of railroad is announced.\n      Workers spontaneously gather and upgrade all cities with railroads.");
  } else {
    notify_player(pplayer, "Game: The people are pleased to hear that your scientists finally know about railroads.\n      Workers spontaneously gather and upgrade all cities with railroads.");
  }
  
  city_list_iterate(pplayer->cities, pcity) {
    map_set_special(pcity->x, pcity->y, S_RAILROAD);
    send_tile_info(0, pcity->x, pcity->y, TILE_KNOWN);
  }
  city_list_iterate_end;

  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
...
**************************************************************************/
void send_map_info(struct player *dest)
{
  int o;
  struct packet_map_info minfo;

  minfo.xsize=map.xsize;
  minfo.ysize=map.ysize;
  minfo.is_earth=map.is_earth;
 
  for(o=0; o<game.nplayers; o++)           /* dests */
    if(!dest || &game.players[o]==dest) {
      struct player *pplayer=&game.players[o];
      send_packet_map_info(pplayer->conn, &minfo);
    }
}

/***************************************************************
...
***************************************************************/
void map_save(struct section_file *file)
{
  int i, x, y;
  char *pbuf=fc_malloc(map.xsize+1);

  /* map.xsize and map.ysize (saved as map.width and map.height)
   * are now always saved in game_save()
   */
  secfile_insert_int(file, map.is_earth, "map.is_earth");

  /* don't save start positions in a type 3 scenario */
  if(game.scenario!=3) {
    for(i=0; i<game.nation_count; i++) {
      secfile_insert_int(file, map.start_positions[i].x, "map.r%dsx", i);
      secfile_insert_int(file, map.start_positions[i].y, "map.r%dsy", i);
    }
  }
    
  /* put the terrain type */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=terrain_chars[map_get_tile(x, y)->terrain];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.t%03d", y);
  }

  /* don't save specials in scenarios of types 2 and 3 */
  if((game.scenario==3) || (game.scenario==2)) {
    free(pbuf);
    return;
  }

  /* put lower 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[map_get_tile(x, y)->special&0xf];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.l%03d", y);
  }

  /* put upper 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf0)>>4];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.u%03d", y);
  }

  /* put "next" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->special&0xf00)>>8];
    pbuf[x]='\0';

    secfile_insert_str(file, pbuf, "map.n%03d", y);
  }

  /* put bit 0-3 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[(map_get_tile(x, y)->known&0xf)];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.a%03d", y);
  }

  /* put bit 4-7 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf0))>>4];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.b%03d", y);
  }

  /* put bit 8-11 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf00))>>8];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.c%03d", y);
  }

  /* put bit 12-15 of known bits */
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++)
      pbuf[x]=dec2hex[((map_get_tile(x, y)->known&0xf000))>>12];
    pbuf[x]='\0';
    secfile_insert_str(file, pbuf, "map.d%03d", y);
  }
  
  free(pbuf);
}

/***************************************************************
load starting positions for the players from a savegame file
Now we don't know how many start positions are nor how many
should be because rulesets are loaded later. So try to load as
many as they are; there should be at least enough for every
player.  This could be changed/improved in future.
***************************************************************/
void map_startpos_load(struct section_file *file)
{
  int i=0, pos;

  while( (pos = secfile_lookup_int_default(file, -1, "map.r%dsx", i)) != -1) {
    map.start_positions[i].x = pos;
    map.start_positions[i].y = secfile_lookup_int(file, "map.r%dsy", i);
    i++;
  }
  map.num_start_positions = i;

  if (map.num_start_positions < MAX_NUM_PLAYERS) {
    freelog(LOG_FATAL, "Too few starts %d (need at least %d)",
	    map.num_start_positions, MAX_NUM_PLAYERS);
    exit(1);
  }
}

/***************************************************************
load the tile map from a savegame file
***************************************************************/
void map_tiles_load(struct section_file *file)
{
  int x, y;

  map.is_earth=secfile_lookup_int(file, "map.is_earth");

  /* In some cases we read these before, but not always, and
   * its safe to read them again:
   */
  map.xsize=secfile_lookup_int(file, "map.width");
  map.ysize=secfile_lookup_int(file, "map.height");

  map_allocate();

  /* get the terrain type */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.t%03d", y);
    for(x=0; x<map.xsize; x++) {
      char *pch;
      if(!(pch=strchr(terrain_chars, terline[x]))) {
 	freelog(LOG_FATAL, "unknown terrain type (map.t) in map "
		"at position (%d,%d): %d '%c'", x, y, terline[x], terline[x]);
	exit(1);
      }
      map_get_tile(x, y)->terrain=pch-terrain_chars;
    }
  }
}
/***************************************************************
load a complete map from a savegame file
***************************************************************/
void map_load(struct section_file *file)
{
  int x ,y;

  /* map_init();
   * This is already called in game_init(), and calling it
   * here stomps on map.huts etc.  --dwp
   */

  map_tiles_load(file);
  map_startpos_load(file);

  /* get lower 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.l%03d", y);

    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->special=ch-(isdigit(ch) ? '0' : ('a'-10));
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown special flag(lower) (map.l) in map "
 	    "at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
      else
	map_get_tile(x, y)->special=S_NO_SPECIAL;
    }
  }

  /* get upper 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.u%03d", y);

    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown special flag(upper) (map.u) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get "next" 4 bits of special flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str_default(file, NULL, "map.n%03d", y);

    if (terline) {
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];

	if(isxdigit(ch)) {
	  map_get_tile(x, y)->special|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown special flag(next) (map.n) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
  }

  /* get bits 0-3 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.a%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known=ch-(isdigit(ch) ? '0' : ('a'-10));
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.a) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
      else
	map_get_tile(x, y)->known=0;
    }
  }

  /* get bits 4-7 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.b%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<4;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.b) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get bits 8-11 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.c%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];
      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<8;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.c) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }

  /* get bits 12-15 of known flags */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str(file, "map.d%03d", y);
    for(x=0; x<map.xsize; x++) {
      char ch=terline[x];

      if(isxdigit(ch)) {
	map_get_tile(x, y)->known|=(ch-(isdigit(ch) ? '0' : 'a'-10))<<12;
      } else if(ch!=' ') {
 	freelog(LOG_FATAL, "unknown known flag (map.d) in map "
		"at position(%d,%d): %d '%c'", x, y, ch, ch);
	exit(1);
      }
    }
  }
}
