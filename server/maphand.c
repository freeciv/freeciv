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

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "registry.h"

#include "cityhand.h"
#include "mapgen.h"		/* assign_continent_numbers */
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
          update_tile_if_seen(x, y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  map_set_terrain(x, y, T_SWAMP);
	  map_clear_special(x, y, S_FARMLAND);
	  map_clear_special(x, y, S_IRRIGATION);
          reset_move_costs(x, y);
          update_tile_if_seen(x, y);
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
          update_tile_if_seen(x, y);
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

  connection_do_buffer(pdest->conn);
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      if(map_get_known(x, y, pfrom) && !map_get_known(x, y, pdest)) {
	show_area(pdest, x, y, 0);
      }
  connection_do_unbuffer(pdest->conn);
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
	show_area(pdest, x, y, 0);
      }
}

/**************************************************************************
...
**************************************************************************/
void give_citymap_from_player_to_player(struct city *pcity,
					struct player *pfrom, struct player *pdest)
{
  int cx, cy, mx, my;

  city_map_iterate(cx, cy) {
    mx = map_adjust_x(pcity->x+cx-(CITY_MAP_SIZE/2));
    my = map_adjust_y(pcity->y+cy-(CITY_MAP_SIZE/2));
    if(map_get_known(mx, my, pfrom) && !map_get_known(mx, my, pdest)) {
      show_area(pdest, mx, my, 0);
    }
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
      set_unknown_tiles_to_unsent(pplayer);      
      connection_do_buffer(pplayer->conn);
      for(y=0; y<map.ysize; y++)
        for(x=0; x<map.xsize; x++)
          if(map_get_known(x, y, pplayer)) {
            show_area(pplayer, x, y, 0);
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
void send_tile_info(struct player *dest, int x, int y)
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
      if (map_get_known(x,y,&game.players[o])) {
	if (map_get_known_and_seen(x,y,&game.players[o]))
	  info.known=TILE_KNOWN;
	else
	  info.known=TILE_KNOWN_FOGGED;
      }
      else
	info.known=TILE_UNKNOWN;;
      send_packet_tile_info(game.players[o].conn, &info);
    }
    else
      if(&game.players[o]==dest) {
	if (map_get_known(x,y,&game.players[o])) {
	  if (map_get_known_and_seen(x,y,&game.players[o]))
	    info.known=TILE_KNOWN;
	  else
	    info.known=TILE_KNOWN_FOGGED;
	} else
	  info.known=TILE_UNKNOWN;
	send_packet_tile_info(game.players[o].conn, &info);
      }
}

/**************************************************************************
Add an exstra point of visibility to a square centered at x,y with
sidelength 1+2*len, ie length 1 is normal sightrange for a unit.
**************************************************************************/
void unfog_area(struct player *pplayer, int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  struct tile *ptile;
  struct city *pcity;
  send_NODRAW_tiles(pplayer,x,y,len);
  connection_do_buffer(pplayer->conn);
  for (dx = x-len;dx <= x+len;dx++)
    for (dy = y-len;dy <= y+len;dy++) {
      if (is_real_tile(dx,dy)) {
	abs_x = map_adjust_x(dx);
	abs_y = map_adjust_y(dy);
	ptile = map_get_tile(abs_x,abs_y);
	
	freelog (LOG_DEBUG, "Unfogging %i,%i. Previous fog: %i.\n",
		 x,y,ptile->seen[pplayer->player_no]);
	
	/* Did the tile just become visible?
	   - send info about units and cities and the tile itself */
	if (!(ptile->seen[pplayer->player_no]) ||
	    !map_get_known(abs_x,abs_y,pplayer)) {
	  ptile->seen[pplayer->player_no] += 1;
	  
	  map_set_known(abs_x, abs_y, pplayer);
	  
	  /*discover units*/
	  unit_list_iterate(map_get_tile(abs_x, abs_y)->units, punit)
	    send_unit_info(pplayer, punit);
	  unit_list_iterate_end;
	  /*discover cities*/
	  
	  if((pcity=map_get_city(abs_x, abs_y))) {
	    send_city_info(pplayer, pcity, 1);
	  }
	  
	  /* send info about the tile itself */
	  send_tile_info(pplayer, abs_x, abs_y);
	} else 
	  ptile->seen[pplayer->player_no] += 1;
      }
    }
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
send KNOWN_NODRAW tiles
**************************************************************************/
void send_NODRAW_tiles(struct player *pplayer, int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  struct tile *ptile;
  connection_do_buffer(pplayer->conn);
  for (dx = - len - 1;dx <= len + 1;dx++)
    for (dy = - len - 1;dy <= len + 1;dy++) {
      abs_x = map_adjust_x(x+dx);
      abs_y = map_adjust_y(y+dy);
      ptile = map_get_tile(abs_x,abs_y);
      if (map_get_known(abs_x,abs_y,pplayer) == 0) {
	if (!map_get_sent(abs_x,abs_y,pplayer)) {
	  send_tile_info(pplayer,abs_x,abs_y);
	  map_set_sent(abs_x,abs_y,pplayer);
	}
      }
    }
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
Remove a point of visibility from a square centered at x,y with
sidelenght 1+2*len, ie lenght 1 is normal sightrange for a unit.
**************************************************************************/
void fog_area(struct player *pplayer, int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  struct tile *ptile;
  connection_do_buffer(pplayer->conn);
  for (dx = x-len;dx <= x+len;dx++)
    for (dy = y-len;dy <= y+len;dy++) {
      if (is_real_tile(dx,dy)) {
	abs_x = map_adjust_x(dx);
	abs_y = map_adjust_y(dy);
	ptile = map_get_tile(abs_x,abs_y);
	freelog (LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.\n",
		 x,y,ptile->seen[pplayer->player_no]);
	ptile->seen[pplayer->player_no] -= 1;
	if (ptile->seen[pplayer->player_no] > 60000)
	  freelog(LOG_FATAL, "square %i,%i has a seen value > 60000 (wrap)",
		  abs_x,abs_y);     
	if (ptile->seen[pplayer->player_no] == 0)
	  send_tile_info(pplayer, abs_x, abs_y);
      }
    }
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
    notify_player(pplayer,
		  _("Game: New hope sweeps like fire through the country as "
		    "the discovery of railroad is announced.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  } else {
    notify_player(pplayer,
		  _("Game: The people are pleased to hear that your "
		    "scientists finally know about railroads.\n"
		    "      Workers spontaneously gather and upgrade all "
		    "cities with railroads."));
  }
  
  city_list_iterate(pplayer->cities, pcity) {
    map_set_special(pcity->x, pcity->y, S_RAILROAD);
    send_tile_info(0, pcity->x, pcity->y);
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

  if(map.num_start_positions>0) {
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

  if(!map.have_specials) {
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
    freelog(LOG_FATAL, _("Too few starts %d (need at least %d)."),
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

  assign_continent_numbers();
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
      map_get_tile(x,y)->sent=0;
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
  map.have_specials = 1;

  /* Should be handled as part of send_all_know_tiles,
     but do it here too for safety */
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      map_get_tile(x,y)->sent = 0;
}

/***************************************************************
load the rivers overlay map from a savegame file

(This does not need to be called from map_load(), because
 map_load() loads the rivers overlay along with the rest of
 the specials.  Call this only if you've already called
 map_tiles_load(), and want to overlay rivers defined as
 specials, rather than as terrain types.)
***************************************************************/
void map_rivers_overlay_load(struct section_file *file)
{
  int x, y;

  /* get "next" 4 bits of special flags;
     extract the rivers overlay from them */
  for(y=0; y<map.ysize; y++) {
    char *terline=secfile_lookup_str_default(file, NULL, "map.n%03d", y);

    if (terline) {
      for(x=0; x<map.xsize; x++) {
	char ch=terline[x];

	if(isxdigit(ch)) {
	  map_get_tile(x, y)->special |=
	    ((ch-(isdigit(ch) ? '0' : 'a'-10))<<8) & S_RIVER;
	} else if(ch!=' ') {
	  freelog(LOG_FATAL, "unknown rivers overlay flag (map.n) in map "
		  "at position(%d,%d): %d '%c'", x, y, ch, ch);
	  exit(1);
	}
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void teleport_unit_sight_points(int src_x, int src_y, int dest_x, int dest_y,
				struct unit* punit)
{
  struct player *pplayer = &game.players[punit->owner];
  int range = get_unit_type(punit->type)->vision_range;
  unfog_area(pplayer,dest_x,dest_y,range);
  fog_area(pplayer,src_x,src_y,range);
}

/**************************************************************************
...
**************************************************************************/
void map_fog_city_area(struct city *pcity)
{
  int x,y;
  struct player *pplayer;
  x = pcity->x;
  y = pcity->y;

  if (!pcity) {
    freelog(LOG_DEBUG, "Attempting to fog non-existent city at %i,%i.", x,y);
    return;
  }

  pplayer = &game.players[pcity->owner];

  map_fog_pseudo_city_area(pplayer, x, y);
}

/**************************************************************************
...
**************************************************************************/
void map_unfog_city_area(struct city *pcity)
{
  int x,y;
  struct player *pplayer;
  x = pcity->x;
  y = pcity->y;

  if (!pcity) {
    freelog(LOG_DEBUG, "Attempting to unfog non-existent city at %i,%i.",x,y);
    return;
  }

  pplayer = &game.players[pcity->owner];

  map_unfog_pseudo_city_area(pplayer, x, y);
}

/**************************************************************************
There don't have to be a city.
**************************************************************************/
void map_unfog_pseudo_city_area(struct player *pplayer, int x,int y)
{
  int i;

  freelog(LOG_DEBUG, "Unfogging city area at %i,%i",x,y);

  unfog_area(pplayer,x,y,1);
  for (i = x-1;i<=x+1;i++)
    if (is_real_tile(i,y+2))
      unfog_area(pplayer,map_adjust_x(i),y+2,0);
  for (i = x-1;i<=x+1;i++)
    if (is_real_tile(i,y-2))
      unfog_area(pplayer,map_adjust_x(i),y-2,0);
  for (i = y-1;i<=y+1;i++)
    if (is_real_tile(x+2,i))
      unfog_area(pplayer,map_adjust_x(x+2),i,0);
  for (i = y-1;i<=y+1;i++)
    if (is_real_tile(x+2,i))
      unfog_area(pplayer,map_adjust_x(x-2),i,0);
}

/**************************************************************************
There don't have to be a city.
**************************************************************************/
void map_fog_pseudo_city_area(struct player *pplayer, int x,int y)
{
  int i;

  freelog(LOG_DEBUG, "Fogging city area at %i,%i",x,y);

  fog_area(pplayer,x,y,1);
  for (i = x-1;i<=x+1;i++)
    if (is_real_tile(i,y+2))
      fog_area(pplayer,map_adjust_x(i),y+2,0);
  for (i = x-1;i<=x+1;i++)
    if (is_real_tile(i,y-2))
      fog_area(pplayer,map_adjust_x(i),y-2,0);
  for (i = y-1;i<=y+1;i++)
    if (is_real_tile(x+2,i))
      fog_area(pplayer,map_adjust_x(x+2),i,0);
  for (i = y-1;i<=y+1;i++)
    if (is_real_tile(x-2,i))
      fog_area(pplayer,map_adjust_x(x-2),i,0);
}

/**************************************************************************
For removing a unit. The actual removal is done in server_remove_unit
**************************************************************************/
void remove_unit_sight_points(struct unit *punit)
{
  int x = punit->x,y = punit->y,range = get_unit_type(punit->type)->vision_range;
  struct player *pplayer = &game.players[punit->owner];

  freelog(LOG_DEBUG, "Removing unit sight points at  %i,%i",punit->x,punit->y);

  fog_area(pplayer,x,y,range);
}


/**************************************************************************
sends changes about a tile. Used for roads, irrigation, etc
**************************************************************************/
void update_tile_if_seen(int x, int y)
{
  send_tile_info(0,x,y);
}

/**************************************************************************
Shows area even if still fogged, sans units and cities.
**************************************************************************/
void show_area(struct player *pplayer,int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  struct city *pcity;    

  freelog(LOG_DEBUG, "Showing %i,%i, len %i",x,y,len);

  send_NODRAW_tiles(pplayer, x, y, len);
  connection_do_buffer(pplayer->conn);  
  for (dx = - len; dx <= len; dx++) {
    abs_x = map_adjust_x(x + dx);
    for (dy = - len; dy <= len; dy++) {
      abs_y = map_adjust_y(y + dy);
      map_set_known(abs_x,abs_y,pplayer);
      send_tile_info(pplayer,abs_x,abs_y); 
      if((pcity=map_get_city(abs_x, abs_y))) {
	send_city_info(pplayer, pcity, 1);
      }
    }
  }
  connection_do_unbuffer(pplayer->conn);
}

/***************************************************************
...
***************************************************************/
int map_get_known(int x, int y, struct player *pplayer)
{
  return ((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->known)&(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
int map_get_known_and_seen(int x, int y, struct player *pplayer)
{
  return ((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->known)&(1u<<pplayer->player_no) &&
    map_get_tile(x,y)->seen[pplayer->player_no];
}

/***************************************************************
Watch out - this can be true even if the tile is not known.
***************************************************************/
int map_get_seen(int x, int y, struct player *pplayer)
{
  return map_get_tile(x,y)->seen[pplayer->player_no];
}

/***************************************************************
...
***************************************************************/
void map_set_known(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->known|=(1u<<pplayer->player_no);
}

/***************************************************************
Not used
***************************************************************/
void map_clear_known(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->known&=~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_know_all(struct player *pplayer)
{
  int x, y;
  for (x = 0; x < map.xsize; ++x) {
    for (y = 0; y < map.ysize; ++y) {
      map_set_known(x, y, pplayer);
    }
  }
}

/***************************************************************
...
***************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  int x, y;
  for (x = 0; x < map.xsize; ++x) {
    for (y = 0; y < map.ysize; ++y) {
      map_set_known(x, y, pplayer);
      map_get_tile(x,y)->seen[pplayer->player_no]++;
    }
  }
}

/***************************************************************
...
***************************************************************/
void map_set_sent(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->sent|=(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void map_clear_sent(int x, int y, struct player *pplayer)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->sent&=~(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
int map_get_sent(int x, int y, struct player *pplayer)
{
  return ((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->sent)&(1u<<pplayer->player_no);
}

/***************************************************************
...
***************************************************************/
void set_unknown_tiles_to_unsent(struct player *pplayer)
{
  int x,y;
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      map_clear_sent(x,y,pplayer);
}
