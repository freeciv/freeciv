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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "registry.h"
#include "support.h"

#include "cityhand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "plrhand.h"           /* notify_player */
#include "unitfunc.h"

#include "maphand.h"

static void player_tile_init(struct player_tile *ptile);
static void give_tile_info_from_player_to_player(struct player *pfrom,
						 struct player *pdest, int x, int y);

static char dec2hex[] = "0123456789abcdef";
static struct player_tile *player_tiles[MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS];
static char terrain_chars[] = "adfghjm prst";

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
          send_tile_info(0,x,y);
	  break;
	case T_DESERT:
	case T_PLAINS:
	case T_GRASSLAND:
	  effect--;
	  map_set_terrain(x, y, T_SWAMP);
	  map_clear_special(x, y, S_FARMLAND);
	  map_clear_special(x, y, S_IRRIGATION);
          reset_move_costs(x, y);
          send_tile_info(0,x,y);
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
          send_tile_info(0,x,y);
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
      give_tile_info_from_player_to_player(pfrom, pdest, x, y);
  connection_do_unbuffer(pdest->conn);
}

/**************************************************************************
...
**************************************************************************/
void give_seamap_from_player_to_player(struct player *pfrom, struct player *pdest)
{
  int x, y;

  connection_do_buffer(pdest->conn);
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      if(map_get_terrain(x, y)==T_OCEAN) {
	give_tile_info_from_player_to_player(pfrom, pdest, x, y);
      }
  connection_do_unbuffer(pdest->conn);
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
	    send_NODRAW_tiles(pplayer, x, y, 0);
            send_tile_info(pplayer, x, y);
          }
      connection_do_unbuffer(pplayer->conn);
    }
}

/**************************************************************************
dest can be NULL meaning all players
if(dest==NULL) we send to all the players that map_get_known_and_seen the tile

If the tile is seen we update the players private map. If it is known but
unseen we sent the info from the players private map
**************************************************************************/
void send_tile_info(struct player *dest, int x, int y)
{
  int o;
  struct packet_tile_info info;
  struct tile *ptile;
  struct player_tile *plrtile;

  ptile = map_get_tile(x, y);

  info.x = x;
  info.y = y;

  for(o=0; o<game.nplayers; o++) {          /* dests */
    if(!dest && map_get_known_and_seen(x,y,&game.players[o])) {
      info.known = TILE_KNOWN;
      info.type = ptile->terrain;
      info.special = ptile->special;
      update_tile_knowledge(&game.players[o],x,y);
      send_packet_tile_info(game.players[o].conn, &info);
      continue;
    }
    if(&game.players[o]==dest) { /* specific player */
      if (map_get_known(x,y,&game.players[o])) {
	if (map_get_seen(x,y,&game.players[o])) { /* known and seen */
	  update_tile_knowledge(&game.players[o],x,y); /* visible; update info */
	  info.known=TILE_KNOWN;
	} else /* known but not seen */
	  info.known = TILE_KNOWN_FOGGED;
	plrtile = map_get_player_tile(&game.players[o],x,y);
	info.type = plrtile->terrain;
	info.special = plrtile->special;
      } else { /* unknown (the client needs these sometimes to draw correctly) */
	info.known = TILE_UNKNOWN;
	info.type = ptile->terrain;
	info.special = ptile->special;
      }
      send_packet_tile_info(game.players[o].conn, &info);
    }
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
  for (dx = x-len;dx <= x+len;dx++) {
    abs_x = map_adjust_x(dx);
    for (dy = y-len;dy <= y+len;dy++) {
      if (is_real_tile(dx,dy)) {
	abs_y = map_adjust_y(dy);
	ptile = map_get_tile(abs_x,abs_y);
	
	freelog (LOG_DEBUG, "Unfogging %i,%i. Previous fog: %i.",
		 abs_x, abs_y, ptile->seen[pplayer->player_no]);
	
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
	  reality_check_city(pplayer, abs_x, abs_y);
	  if((pcity=map_get_city(abs_x, abs_y)))
	    send_city_info(pplayer, pcity);

	  /* send info about the tile itself */
	  send_tile_info(pplayer, abs_x, abs_y);
	} else 
	  ptile->seen[pplayer->player_no] += 1;
      }
    }
  }
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
send KNOWN_NODRAW tiles
We send only the unknown tiles around the square with lenght len
**************************************************************************/
void send_NODRAW_tiles(struct player *pplayer, int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  connection_do_buffer(pplayer->conn);
  for (dx = - len - 1;dx <= len + 1;dx += 2 * len + 2) {
    abs_x = map_adjust_x(x+dx);
    for (dy = - len - 1;dy <= len + 1;dy++) {
      abs_y = map_adjust_y(y+dy);
      if (!map_get_sent(abs_x,abs_y,pplayer)) {
	send_tile_info(pplayer,abs_x,abs_y);
	map_set_sent(abs_x,abs_y,pplayer);
      }
    }
  }

  for (dy = - len - 1;dy <= len + 1;dy += 2 * len + 2) {
    abs_y = map_adjust_y(y+dy);
    for (dx = - len;dx <= len;dx++) {
      abs_x = map_adjust_x(x+dx);
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
  for (dx = x-len;dx <= x+len;dx++) {
    abs_x = map_adjust_x(dx);
    for (dy = y-len;dy <= y+len;dy++) {
      if (is_real_tile(dx,dy)) {
	abs_y = map_adjust_y(dy);
	ptile = map_get_tile(abs_x,abs_y);
	freelog (LOG_DEBUG, "Fogging %i,%i. Previous fog: %i.",
		 x,y,ptile->seen[pplayer->player_no]);
	ptile->seen[pplayer->player_no]--;
	if (ptile->seen[pplayer->player_no] > 60000)
	  freelog(LOG_FATAL, "square %i,%i has a seen value > 60000 (wrap) for player %s",
		  abs_x, abs_y, pplayer->name);     
	if (ptile->seen[pplayer->player_no] == 0) {
	  update_player_tile_last_seen(pplayer,abs_x,abs_y);
	  send_tile_info(pplayer, abs_x, abs_y);
	}
      }
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
Now we don't know how many start positions there are nor how many
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

  if (i < MAX_NUM_PLAYERS) {
    freelog(LOG_FATAL, _("Too few starts %d (need at least %d)."
			 " Filling out with 0,0 start positions."),
	    i, MAX_NUM_PLAYERS);
  }

  while (i < MAX_NUM_PLAYERS) {
    map.start_positions[i].x = 0;
    map.start_positions[i].y = 0;
    i++;
  }

  map.num_start_positions = i; /* I really hope i is MAX_NUM_PLAYERS :) */
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
  if (!pcity) {
    freelog(LOG_DEBUG, "Attempting to fog non-existent city");
    return;
  }

  map_fog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
}

/**************************************************************************
...
**************************************************************************/
void map_unfog_city_area(struct city *pcity)
{
  if (!pcity) {
    freelog(LOG_DEBUG, "Attempting to unfog non-existent city");
    return;
  }

  map_unfog_pseudo_city_area(city_owner(pcity), pcity->x, pcity->y);
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
Shows area even if still fogged, sans units and cities.
**************************************************************************/
void show_area(struct player *pplayer,int x, int y, int len)
{
  int dx,dy,abs_x,abs_y;
  struct city *pcity;    

  freelog(LOG_DEBUG, "Showing %i,%i, len %i",x,y,len);

  connection_do_buffer(pplayer->conn);  
  send_NODRAW_tiles(pplayer, x, y, len);
  for (dx = - len; dx <= len; dx++) {
    abs_x = map_adjust_x(x + dx);
    for (dy = - len; dy <= len; dy++) {
      abs_y = map_adjust_y(y + dy);
      if (!map_get_known_and_seen(x,y,pplayer) && is_real_tile(x,y)) {
	map_set_known(abs_x,abs_y,pplayer);

	/* as the tile may be fogged send_tile_info won't do this for us */
	update_tile_knowledge(pplayer, abs_x, abs_y);
	update_player_tile_last_seen(pplayer, abs_x, abs_y);
	send_tile_info(pplayer,abs_x,abs_y);

	/* remove old cities that exist no more */
	reality_check_city(pplayer, abs_x, abs_y);
	if ((pcity = map_get_city(abs_x,abs_y))) {
	  /* as the tile may be fogged send_city_info won't do this for us */
	  update_dumb_city(pplayer, pcity);
	  send_city_info(pplayer, pcity);
	}

	if (map_get_seen(abs_x,abs_y,pplayer)) {
	  unit_list_iterate(map_get_tile(abs_x, abs_y)->units, punit)
	    send_unit_info(pplayer, punit);
	  unit_list_iterate_end;
	}
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
  struct city *pcity;
  for (x = 0; x < map.xsize; ++x) {
    for (y = 0; y < map.ysize; ++y) {
      map_set_known(x, y, pplayer);
      update_tile_knowledge(pplayer, x, y);
      send_tile_info(pplayer, x, y);
      reality_check_city(pplayer, x, y);
      if ((pcity = map_get_city(x, y))) {
	update_dumb_city(pplayer, pcity);
	send_city_info(pplayer, pcity);
      }
    }
  }
}

/***************************************************************
...
***************************************************************/
void map_know_and_see_all(struct player *pplayer)
{
  int x, y;
  struct city *pcity;
  for (x = 0; x < map.xsize; ++x) {
    for (y = 0; y < map.ysize; ++y) {
      map_set_known(x, y, pplayer);
      map_get_tile(x,y)->seen[pplayer->player_no]++;

      send_tile_info(pplayer, x, y);

      reality_check_city(pplayer, x, y);
      if ((pcity = map_get_city(x,y)))
	send_city_info(pplayer, pcity);

      unit_list_iterate(map_get_tile(x, y)->units, punit)
	send_unit_info(pplayer, punit);
      unit_list_iterate_end;
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

/***************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
****************************************************************/
void player_map_allocate(struct player *pplayer)
{
  int x,y;

  player_tiles[pplayer->player_no]=
    fc_malloc(map.xsize*map.ysize*sizeof(struct player_tile));
  for(y=0; y<map.ysize; y++)
    for(x=0; x<map.xsize; x++)
      player_tile_init(map_get_player_tile(pplayer,x, y));
}

/***************************************************************
...
***************************************************************/
void player_tile_init(struct player_tile *ptile)
{
  ptile->terrain = T_UNKNOWN;
  ptile->special = S_NO_SPECIAL;
  ptile->city = NULL;
  ptile->seen = 0;
  ptile->last_updated = GAME_START_YEAR;
}

/***************************************************************
...
***************************************************************/
struct player_tile *map_get_player_tile(struct player *pplayer,int x, int y)
{
  if(y<0 || y>=map.ysize) {
    freelog(LOG_FATAL, "Trying to get nonexistant tile at %i,%i", x,y);
    return player_tiles[pplayer->player_no]+map_adjust_x(x)+map_adjust_y(y)*map.xsize;
  } else
    return player_tiles[pplayer->player_no]+map_adjust_x(x)+y*map.xsize;
}

/***************************************************************
...
***************************************************************/
void update_tile_knowledge(struct player *pplayer,int x, int y)
{
  struct tile *ptile = map_get_tile(x,y);
  struct player_tile *plrtile = map_get_player_tile(pplayer,x,y);

  plrtile->terrain = ptile->terrain;
  plrtile->special = ptile->special;
}

/***************************************************************
...
***************************************************************/
void update_player_tile_last_seen(struct player *pplayer, int x, int y)
{
  map_get_player_tile(pplayer,x,y)->last_updated = game.year;
}

/***************************************************************
...
***************************************************************/
void give_tile_info_from_player_to_player(struct player *pfrom,
					  struct player *pdest, int x, int y)
{
  struct dumb_city *from_city, *dest_city;
  struct player_tile *from_tile, *dest_tile;
  if (!map_get_known_and_seen(x,y,pdest)) {
    /* I can just hear people scream as they try to comprehend this if :).
       Let me try in words:
       1) if the tile is seen by pdest the info is sent to pfrom
       OR
       2) if the tile is known by pfrom AND (he has more resent info
          OR it is not known by pdest) */
    if (map_get_known_and_seen(x, y, pfrom)
	|| (map_get_known(x,y,pfrom)
	    && (((map_get_player_tile(pfrom, x, y)->last_updated
		 > map_get_player_tile(pdest, x, y)->last_updated))
	        || !map_get_known(x, y, pdest)))) {
      from_tile = map_get_player_tile(pfrom, x, y);
      dest_tile = map_get_player_tile(pdest, x, y);
      /* Update and send tile knowledge */
      map_set_known(x, y, pdest);
      dest_tile->terrain = from_tile->terrain;
      dest_tile->special = from_tile->special;
      dest_tile->last_updated = from_tile->last_updated;
      send_NODRAW_tiles(pdest, x, y, 0);
      send_tile_info(pdest, x, y);
	
      /* update and send city knowledge */
      /* remove outdated cities */
      if (dest_tile->city) {
	if (!from_tile->city) {
	  /* As the city was gone on the newer from_tile
	     it will be removed by this function */
	  reality_check_city(pdest, x, y);
	} else /* We have a dest_city. update */
	  if (from_tile->city->id != dest_tile->city->id)
	    /* As the city was gone on the newer from_tile
	       it will be removed by this function */
	    reality_check_city(pdest, x, y);
      }
      /* Set and send new city info */
      if (from_tile->city && !dest_tile->city) {
	dest_tile->city = fc_malloc(sizeof(struct dumb_city));
      }
      if ((from_city = from_tile->city) && (dest_city = dest_tile->city)) {
	dest_city->id = from_city->id;
	sz_strlcpy(dest_city->name, from_city->name);
	dest_city->size = from_city->size;
	dest_city->has_walls = from_city->has_walls;
	dest_city->owner = from_city->owner;
	send_city_info_at_tile(pdest,x,y);
      }
    }
  }
}
