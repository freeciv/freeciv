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
#include <string.h>
#include <ctype.h>

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "unit.h"

#include "map.h"

/* the very map */
struct civ_map map;

/* this is used for map yval out of range: */
static struct tile void_tile;

/* these are initialized from the terrain ruleset */
struct terrain_misc terrain_control;
struct tile_type tile_types[T_LAST];

/***************************************************************
  Return a (static) string with terrain name;
  eg: "Hills"
  eg: "Hills (Coals)"
  eg: "Polluted Hills (Coals)"
***************************************************************/
char *map_get_tile_info_text(int x, int y)
{
  static char s[64];
  struct tile *ptile=map_get_tile(x, y);
  
  sprintf(s, "%s%s",
	  (ptile->special&S_POLLUTION ? _("Polluted ") : ""),
	  tile_types[ptile->terrain].terrain_name);
  if(ptile->special&S_RIVER) 
    sprintf(s+strlen(s), "/%s", _("River"));
  if(ptile->special&S_SPECIAL_1) 
    sprintf(s+strlen(s), "(%s)", tile_types[ptile->terrain].special_1_name);
  else if(ptile->special&S_SPECIAL_2) 
    sprintf(s+strlen(s), "(%s)", tile_types[ptile->terrain].special_2_name);

  return s;
}

/***************************************************************
  Returns 1 if we are at a stage of the game where the map
  has not yet been generated/loaded.
  (To be precise, returns 1 if map_allocate() has not yet been
  called.)
***************************************************************/
int map_is_empty(void)
{
  return map.tiles==0;
}


/***************************************************************
 put some sensible values into the map structure
 Also initialize special void_tile.
***************************************************************/
void map_init(void)
{
  map.xsize=MAP_DEFAULT_WIDTH;
  map.ysize=MAP_DEFAULT_HEIGHT;
  map.seed=MAP_DEFAULT_SEED;
  map.riches=MAP_DEFAULT_RICHES;
  map.is_earth=0;
  map.huts=MAP_DEFAULT_HUTS;
  map.landpercent=MAP_DEFAULT_LANDMASS;
  map.grasssize=MAP_DEFAULT_GRASS;
  map.swampsize=MAP_DEFAULT_SWAMPS;
  map.deserts=MAP_DEFAULT_DESERTS;
  map.mountains=MAP_DEFAULT_MOUNTAINS;
  map.riverlength=MAP_DEFAULT_RIVERS;
  map.forestsize=MAP_DEFAULT_FORESTS;
  map.generator=MAP_DEFAULT_GENERATOR;
  map.tiles=0;
  map.num_start_positions=0;

  tile_init(&void_tile);
}

/**************************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
  Uses realloc, in case map was allocated before (eg, in client);
  should have called map_init() (via game_init()) before this,
  so map.tiles will be 0 on first call.
**************************************************************************/
void map_allocate(void)
{
  int x,y;

  freelog(LOG_DEBUG, "map_allocate (was %p) (%d,%d)",
	  map.tiles, map.xsize, map.ysize);
  
  map.tiles=fc_realloc(map.tiles, map.xsize*map.ysize*sizeof(struct tile));
  for(y=0; y<map.ysize; y++) {
    for(x=0; x<map.xsize; x++) {
      tile_init(map_get_tile(x, y));
    }
  }
}


/***************************************************************
...
***************************************************************/
struct tile_type *get_tile_type(enum tile_terrain_type type)
{
  return &tile_types[type];
}

/***************************************************************
...
***************************************************************/
enum tile_terrain_type get_terrain_by_name(char * name)
{
  enum tile_terrain_type tt;
  for (tt = T_FIRST; tt < T_COUNT; tt++)
    {
      if (0 == strcmp (tile_types[tt].terrain_name, name))
	{
	  break;
	}
    }
  return (tt);
}

/***************************************************************
...
***************************************************************/
int real_map_distance(int x0, int y0, int x1, int y1)
{
  int tmp;
  if(y0>y1)
    tmp=y0, y0=y1, y1=tmp;
  tmp=map_adjust_x(x0-x1);
  return MAX(y1 - y0, MIN(tmp, map.xsize-tmp));
}

/***************************************************************
...
***************************************************************/
int sq_map_distance(int x0, int y0, int x1, int y1)
{
  int tmp;
  tmp=map_adjust_x(x0-x1);
  tmp= MIN(tmp, map.xsize-tmp);
  return (((y1 - y0) * (y1 - y0)) + tmp * tmp);
}

/***************************************************************
...
***************************************************************/
int map_distance(int x0, int y0, int x1, int y1)
{
  int tmp;

  if(y0>y1)
    tmp=y0, y0=y1, y1=tmp;
  tmp = map_adjust_x(x0-x1);
  return MIN(tmp, map.xsize-tmp)+y1-y0;
}

/***************************************************************
 The following two function should always be used, such
 that hand optimizations need only be performed once.
 using isnt_ is faster than using is_ sometimes.
 is terrain close diagonally or gridwise ?
***************************************************************/
int is_terrain_near_tile(int x, int y, enum tile_terrain_type t)
{
  if (map_get_terrain(x-1, y+1)==t) return 1;
  if (map_get_terrain(x+1, y-1)==t) return 1;
  if (map_get_terrain(x-1, y-1)==t) return 1;
  if (map_get_terrain(x+1, y+1)==t) return 1;
  if (map_get_terrain(x, y+1)==t)   return 1;
  if (map_get_terrain(x-1, y)==t)   return 1;
  if (map_get_terrain(x+1, y)==t)   return 1;
  if (map_get_terrain(x, y-1)==t)   return 1;
  return 0;
}

/***************************************************************
 is no terrain close diagonally or gridwise ?
***************************************************************/
int isnt_terrain_near_tile(int x, int y, enum tile_terrain_type t)
{
  if (map_get_terrain(x-1, y+1)!=t) return 1;
  if (map_get_terrain(x+1, y-1)!=t) return 1;
  if (map_get_terrain(x-1, y-1)!=t) return 1;
  if (map_get_terrain(x+1, y+1)!=t) return 1;
  if (map_get_terrain(x, y+1)!=t)   return 1;
  if (map_get_terrain(x-1, y)!=t)   return 1;
  if (map_get_terrain(x+1, y)!=t)   return 1;
  if (map_get_terrain(x, y-1)!=t)   return 1;
  return 0;
}

/***************************************************************
  counts tiles close to x,y having terrain t
***************************************************************/
int count_terrain_near_tile(int x, int y, enum tile_terrain_type t)
{
  int rval= 0;
  if (map_get_terrain(x, y+1)==t)   rval++;
  if (map_get_terrain(x-1, y-1)==t) rval++;
  if (map_get_terrain(x-1, y)==t)   rval++;
  if (map_get_terrain(x-1, y+1)==t) rval++;
  if (map_get_terrain(x+1, y-1)==t) rval++;
  if (map_get_terrain(x+1, y)==t)   rval++;
  if (map_get_terrain(x+1, y+1)==t) rval++;
  if (map_get_terrain(x, y-1)==t)   rval++;
  return rval;
}

/***************************************************************
  determines if any tile close to x,y has special spe
***************************************************************/
int is_special_near_tile(int x, int y, enum tile_special_type spe)
{
  if (map_get_special(x-1, y+1)&spe) return 1;
  if (map_get_special(x+1, y-1)&spe) return 1;
  if (map_get_special(x-1, y-1)&spe) return 1;
  if (map_get_special(x+1, y+1)&spe) return 1;
  if (map_get_special(x, y+1)&spe)   return 1;
  if (map_get_special(x-1, y)&spe)   return 1;
  if (map_get_special(x+1, y)&spe)   return 1;
  if (map_get_special(x, y-1)&spe)   return 1;
  return 0;
}

/***************************************************************
  counts tiles close to x,y having special spe
***************************************************************/
int count_special_near_tile(int x, int y, enum tile_special_type spe)
{
  int rval= 0;
  if (map_get_special(x, y+1)&spe)   rval++;
  if (map_get_special(x-1, y-1)&spe) rval++;
  if (map_get_special(x-1, y)&spe)   rval++;
  if (map_get_special(x-1, y+1)&spe) rval++;
  if (map_get_special(x+1, y-1)&spe) rval++;
  if (map_get_special(x+1, y)&spe)   rval++;
  if (map_get_special(x+1, y+1)&spe) rval++;
  if (map_get_special(x, y-1)&spe)   rval++;
  return rval;
}

/***************************************************************
...
***************************************************************/
int is_at_coast(int x, int y)
{
  if (map_get_terrain(x-1,y)==T_OCEAN)   return 1;
  if (map_get_terrain(x,y-1)==T_OCEAN)   return 1;
  if (map_get_terrain(x,y+1)==T_OCEAN)   return 1;
  if (map_get_terrain(x+1,y)==T_OCEAN)   return 1;
  return 0;
  
}

/***************************************************************
...
***************************************************************/
int is_coastline(int x,int y) 
{
  /*if (map_get_terrain(x,y)!=T_OCEAN)   return 0;*/
  if (map_get_terrain(x-1,y)!=T_OCEAN)   return 1;
  if (map_get_terrain(x-1,y-1)!=T_OCEAN) return 1;
  if (map_get_terrain(x-1,y+1)!=T_OCEAN) return 1;
  if (map_get_terrain(x,y-1)!=T_OCEAN)   return 1;
  if (map_get_terrain(x,y+1)!=T_OCEAN)   return 1;
  if (map_get_terrain(x+1,y-1)!=T_OCEAN) return 1;
  if (map_get_terrain(x+1,y)!=T_OCEAN)   return 1;
  if (map_get_terrain(x+1,y+1)!=T_OCEAN) return 1;
  return 0;
}

/***************************************************************
...
***************************************************************/
int terrain_is_clean(int x, int y)
{
  int x1,y1;
  for (x1=x-3;x1<x+3;x1++)
    for (y1=y-3;y1<y+3;y1++) 
      if (map_get_terrain(x1,y1)!=T_GRASSLAND
	  && map_get_terrain(x1,y1)!=T_PLAINS) return 0;
  return 1;
}

/***************************************************************
...
***************************************************************/
int map_same_continent(int x1, int y1, int x2, int y2)
{
  return (map_get_continent(x1,y1) == map_get_continent(x2,y2));
}

/***************************************************************
  Returns 1 if (x,y) is _not_ a good position to start from;
  Bad places:
  - Non-suitable terrain;
  - On a hut;
  - On North/South pole continents
  - Too close to another starter on the same continent:
  'dist' is too close (real_map_distance)
  'nr' is the number of other start positions in
  map.start_positions to check for too closeness.
***************************************************************/
int is_starter_close(int x, int y, int nr, int dist) 
{
  int i;
  enum tile_terrain_type t = map_get_terrain(x, y);

  /* only start on clear terrain: */
  if (t!=T_PLAINS && t!=T_GRASSLAND && t!=T_RIVER)
    return 1;
  
  /* don't start on a hut: */
  if (map_get_tile(x, y)->special&S_HUT)
    return 1;
  
  /* don't start on a river: */
  if (map_get_tile(x, y)->special&S_RIVER)
    return 1;
  
  /* don't want them starting on the poles: */
  if (map_get_continent(x, y)<=2)
    return 1;

  /* don't start too close to someone else: */
  for (i=0;i<nr;i++) {
    int x1 = map.start_positions[i].x;
    int y1 = map.start_positions[i].y;
    if (map_same_continent(x, y, x1, y1)
	&& real_map_distance(x, y, x1, y1) < dist) {
      return 1;
    }
  }
  return 0;
}

/***************************************************************
...
***************************************************************/
int is_good_tile(int x, int y)
{
  int c;
  c=map_get_terrain(x,y);
  switch (c) {

    /* range 0 .. 5 , 2 standard */

  case T_FOREST:
    if (map_get_tile(x, y)->special) return 5;
    return 3;
  case T_RIVER:
    if (map_get_tile(x, y)->special) return 4;
    return 3;
  case T_GRASSLAND:
  case T_PLAINS:
  case T_HILLS:
    if (map_get_tile(x, y)->special) return 4;
    return 2;
  case T_DESERT:
  case T_OCEAN:/* must be called with usable seas */    
    if (map_get_tile(x, y)->special) return 3;
    return 1;
  case T_SWAMP:
  case T_JUNGLE:
  case T_MOUNTAINS:
    if (map_get_tile(x, y)->special) return 3;
    return 0;
  /* case T_ARCTIC: */
  /* case T_TUNDRA: */
  default:
    if (map_get_tile(x,y)->special) return 1;
    break;
  }
  return 0;
}


/***************************************************************
...
***************************************************************/
int is_water_adjacent(int x, int y)
{
  int x1,y1;
  for (y1=y-1;y1<y+2;y1++) 
    for (x1=x-1;x1<x+2;x1++) {
      if (map_get_terrain(x1,y1)==T_OCEAN
	  || map_get_terrain(x1, y1)==T_RIVER
	  || map_get_special(x1, y1)&S_RIVER)
	return 1;
    } 
  return 0;
}

/***************************************************************
...
***************************************************************/
int is_hut_close(int x, int y)
{
  int x1,y1;
  for (y1=y-3;y1<y+4;y1++) 
    for (x1=x-3;x1<x+4;x1++) {
      if (map_get_tile(x1,y1)->special&S_HUT)
	return 1;
    } 
  return 0;
}


/***************************************************************
...
***************************************************************/
int is_special_close(int x, int y)
{
  int x1,y1;
  for (x1=x-1;x1<x+2;x1++)
    for (y1=y-1;y1<=y+2;y1++) 
      if(map_get_tile(x1,y1)->special&(S_SPECIAL_1 | S_SPECIAL_2))
	return 1;
  return 0;
}

/***************************************************************
...
***************************************************************/
int is_special_type_close(int x, int y, enum tile_special_type spe)
{
  int x1,y1;
  for (x1=x-1;x1<x+2;x1++)
    for (y1=y-1;y1<=y+2;y1++) 
      if((map_get_tile(x1,y1)->special)&spe)
	return 1;
  return 0;
}

/***************************************************************
...
***************************************************************/
int is_sea_usable(int x, int y)
{
  int x1,y1;
  for (x1=x-2;x1<x+3;x1++)
    for (y1=y-2;y1<=y+3;y1++) 
      if( !( (x==x1-2||x==x1+2) && (y==y1-2||y==y1+2) ) )
	if(map_get_terrain(x1,y1)!=T_OCEAN)
	  return 1;
  return 0;
}

/***************************************************************
...
***************************************************************/
int get_tile_food_base(struct tile * ptile)
{
  if(ptile->special&S_SPECIAL_1) 
    return (tile_types[ptile->terrain].food_special_1);
  else if(ptile->special&S_SPECIAL_2) 
    return (tile_types[ptile->terrain].food_special_2);
  else
    return (tile_types[ptile->terrain].food);
}

/***************************************************************
...
***************************************************************/
int get_tile_shield_base(struct tile * ptile)
{
  if(ptile->special&S_SPECIAL_1) 
    return (tile_types[ptile->terrain].shield_special_1);
  else if(ptile->special&S_SPECIAL_2) 
    return (tile_types[ptile->terrain].shield_special_2);
  else
    return (tile_types[ptile->terrain].shield);
}

/***************************************************************
...
***************************************************************/
int get_tile_trade_base(struct tile * ptile)
{
  if(ptile->special&S_SPECIAL_1) 
    return (tile_types[ptile->terrain].trade_special_1);
  else if(ptile->special&S_SPECIAL_2) 
    return (tile_types[ptile->terrain].trade_special_2);
  else
    return (tile_types[ptile->terrain].trade);
}

/***************************************************************
...
***************************************************************/
int get_tile_infrastructure_set(struct tile * ptile)
{
  return
    ptile->special &
    (S_ROAD | S_RAILROAD | S_IRRIGATION | S_FARMLAND | S_MINE | S_FORTRESS | S_AIRBASE);
}

/***************************************************************
  Return a (static) string with special(s) name(s);
  eg: "Mine"
  eg: "Road/Irrigation/Farmland"
***************************************************************/
char *map_get_infrastructure_text(int spe)
{
  static char s[64];

  *s = '\0';

  if(spe&S_ROAD)
    sprintf(s+strlen(s), "%s/", _("Road"));
  if(spe&S_RAILROAD)
    sprintf(s+strlen(s), "%s/", _("Railroad"));
  if(spe&S_IRRIGATION)
    sprintf(s+strlen(s), "%s/", _("Irrigation"));
  if(spe&S_FARMLAND)
    sprintf(s+strlen(s), "%s/", _("Farmland"));
  if(spe&S_MINE)
    sprintf(s+strlen(s), "%s/", _("Mine"));
  if(spe&S_FORTRESS)
    sprintf(s+strlen(s), "%s/", _("Fortress"));
  if(spe&S_AIRBASE)
    sprintf(s+strlen(s), "%s/", _("Airbase"));

  if(*s)
    *(s+strlen(s)-1)='\0';

  return (s);
}

/***************************************************************
...
***************************************************************/
int map_get_infrastructure_prerequisite(int spe)
{
  int prereq = S_NO_SPECIAL;

  if (spe&S_RAILROAD)
    prereq|=S_ROAD;
  if (spe&S_FARMLAND)
    prereq|=S_IRRIGATION;

  return (prereq);
}

/***************************************************************
...
***************************************************************/
int get_preferred_pillage(int pset)
{
  if(pset&S_FARMLAND)
    return S_FARMLAND;
  if(pset&S_IRRIGATION)
    return S_IRRIGATION;
  if(pset&S_MINE)
    return S_MINE;
  if(pset&S_FORTRESS)
    return S_FORTRESS;
  if(pset&S_AIRBASE)
    return S_AIRBASE;
  if(pset&S_RAILROAD)
    return S_RAILROAD;
  if(pset&S_ROAD)
    return S_ROAD;
  return S_NO_SPECIAL;
}

/***************************************************************
...
***************************************************************/
int is_water_adjacent_to_tile(int x, int y)
{
  struct tile *ptile, *ptile_n, *ptile_e, *ptile_s, *ptile_w;

  ptile=map_get_tile(x, y);
  ptile_n=map_get_tile(x, y-1);
  ptile_e=map_get_tile(x+1, y);
  ptile_s=map_get_tile(x, y+1);
  ptile_w=map_get_tile(x-1, y);

  return (ptile->terrain==T_OCEAN   || ptile->terrain==T_RIVER
	  || ptile->special&S_RIVER || ptile->special&S_IRRIGATION ||
	  ptile_n->terrain==T_OCEAN || ptile_n->terrain==T_RIVER
	  || ptile_n->special&S_RIVER || ptile_n->special&S_IRRIGATION ||
	  ptile_e->terrain==T_OCEAN || ptile_e->terrain==T_RIVER
	  || ptile_e->special&S_RIVER || ptile_e->special&S_IRRIGATION ||
	  ptile_s->terrain==T_OCEAN || ptile_s->terrain==T_RIVER
	  || ptile_s->special&S_RIVER || ptile_s->special&S_IRRIGATION ||
	  ptile_w->terrain==T_OCEAN || ptile_w->terrain==T_RIVER
	  || ptile_w->special&S_RIVER || ptile_w->special&S_IRRIGATION);
}

/***************************************************************
..
***************************************************************/
int map_build_road_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].road_time;
}

/***************************************************************
...
***************************************************************/
int map_build_irrigation_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].irrigation_time;
}

/***************************************************************
...
***************************************************************/
int map_build_mine_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].mining_time;
}

/***************************************************************
...
***************************************************************/
int map_transform_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].transform_time;
}

/***************************************************************
...
***************************************************************/
void map_irrigate_tile(int x, int y)
{
  enum tile_terrain_type now, result;
  
  now=map_get_terrain(x, y);
  result=tile_types[now].irrigation_result;

  if(now==result) {
    if (map_get_special(x, y)&S_IRRIGATION) {
      if (improvement_exists(B_SUPERMARKET)) {
	map_set_special(x, y, S_FARMLAND);
      }
    } else {
      map_set_special(x, y, S_IRRIGATION);
    }
  }
  else if(result!=T_LAST) {
    map_set_terrain(x, y, result);
    reset_move_costs(x, y);
  }
  map_clear_special(x, y, S_MINE);
}

/***************************************************************
...
***************************************************************/
void map_mine_tile(int x, int y)
{
  enum tile_terrain_type now, result;
  
  now=map_get_terrain(x, y);
  result=tile_types[now].mining_result;
  
  if(now==result) 
    map_set_special(x, y, S_MINE);
  else if(result!=T_LAST) {
    map_set_terrain(x, y, result);
    reset_move_costs(x, y);
  }
  map_clear_special(x,y, S_FARMLAND);
  map_clear_special(x,y, S_IRRIGATION);
}

/***************************************************************
...
***************************************************************/
void map_transform_tile(int x, int y)
{
  enum tile_terrain_type now, result;
  
  now = map_get_terrain(x, y);
  result = tile_types[now].transform_result;
  
  if (result != T_LAST) {
    map_set_terrain(x, y, result);
    reset_move_costs(x, y);
  }

  /* Clear mining/irrigation if resulting terrain type cannot support
     that feature.  (With current rules, this should only clear mines,
     but I'm including both cases in the most general form for possible
     future ruleset expansion. -GJW) */
  if (tile_types[result].mining_result != result)
    map_clear_special(x, y, S_MINE);
  if (tile_types[result].irrigation_result != result) {
    map_clear_special(x, y, S_FARMLAND);
    map_clear_special(x, y, S_IRRIGATION);
  }
}

/***************************************************************
  (x,y) are map coords, assumed to be already adjusted;
  we calculate elements of xx[3], yy[3] for adjusted
  coordinates of adjacent tiles, offsets [0]=-1, [1]=0, [2]=+1
  Here we adjust yy[] values to stay inside [0,map.ysize-1]
***************************************************************/
void map_calc_adjacent_xy(int x, int y, int *xx, int *yy)
{
  if((xx[2]=x+1)==map.xsize) xx[2]=0;
  if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
  xx[1] = x;
  if ((yy[0]=y-1)==-1) yy[0] = 0;
  if ((yy[2]=y+1)==map.ysize) yy[2]=y;
  yy[1] = y;
}

/***************************************************************
  Like map_calc_adjacent_xy(), except don't adjust the yy[] values.
  Note that if y is out of range, then map_get_tile(x,y) returns
  &void_tile, hence the _void in the name of this function.
***************************************************************/
void map_calc_adjacent_xy_void(int x, int y, int *xx, int *yy)
{
  if((xx[2]=x+1)==map.xsize) xx[2]=0;
  if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
  xx[1] = x;
  yy[0] = y - 1;
  yy[1] = y;
  yy[2] = y + 1;
}

/***************************************************************
  The basic cost to move punit from tile t1 to tile t2.
  That is, tile_move_cost(), with pre-calculated tile pointers;
  the tiles are assumed to be adjacent, and the (x,y)
  values are used only to get the river bonus correct.

  May also be used with punit==NULL, in which case punit
  tests are not done (for unit-independent results).
***************************************************************/
static int tile_move_cost_ptrs(struct unit *punit, struct tile *t1,
			       struct tile *t2, int x1, int y1, int x2, int y2)
{
  int cardinal_move;

  if (punit && !is_ground_unit(punit))
    return 3;
  if( (t1->special&S_RAILROAD) && (t2->special&S_RAILROAD) )
    return 0;
  if (punit && unit_flag(punit->type, F_IGTER))
    return 1;
  if( (t1->special&S_ROAD) && (t2->special&S_ROAD) )
    return 1;

  if( ( (t1->terrain==T_RIVER) && (t2->terrain==T_RIVER) ) ||
      ( (t1->special&S_RIVER) && (t2->special&S_RIVER) ) ) {
    cardinal_move = (y1==y2 || map_adjust_x(x1)==map_adjust_x(x2));
    switch (terrain_control.river_move_mode) {
    case RMV_NORMAL:
      break;
    case RMV_FAST_STRICT:
      if (cardinal_move)
	return 1;
      break;
    case RMV_FAST_RELAXED:
      if (cardinal_move)
	return 1;
      else
	return 2;
    case RMV_FAST_ALWAYS:
      return 1;
    default:
      break;
    }
  }

  return(get_tile_type(t2->terrain)->movement_cost*3);
}

/***************************************************************
  Similar to tile_move_cost_ptrs, but for pre-calculating
  tile->move_cost[] values for use by ai (?)
  If either terrain is T_UNKNOWN (only void_tile, note this
  is different to tile->known), then return 'maxcost'.
  If both are ocean, or one is ocean and other city,
  return -3, else if either is ocean return maxcost.
  Otherwise, return normal cost (unit-independent).
***************************************************************/
static int tile_move_cost_ai(struct tile *tile0, struct tile *tile1,
			     int x, int y, int x1, int y1, int maxcost)
{
  if (tile0->terrain == T_UNKNOWN || tile1->terrain == T_UNKNOWN) {
    return maxcost;
  } else if (tile0->terrain == T_OCEAN) {
    return (tile1->city || tile1->terrain == T_OCEAN) ? -3 : maxcost;
  } else if (tile1->terrain == T_OCEAN) {
    return (tile0->city) ? -3 : maxcost;
  } else {
    return tile_move_cost_ptrs(NULL, tile0, tile1, x, y, x1, y1);
  }
}

/***************************************************************
 ...
***************************************************************/
static void debug_log_move_costs(char *str, int x, int y, struct tile *tile0)
{
  /* the %x don't work so well for oceans, where
     move_cost[]==-3 ,.. --dwp
  */
  freelog(LOG_DEBUG, "%s (%d, %d) [%x%x%x%x%x%x%x%x]", str, x, y,
	  tile0->move_cost[0], tile0->move_cost[1],
	  tile0->move_cost[2], tile0->move_cost[3],
	  tile0->move_cost[4], tile0->move_cost[5],
	  tile0->move_cost[6], tile0->move_cost[7]);
}

/***************************************************************
  Recalculate tile->move_cost[] for (x,y), and for adjacent
  tiles in direction back to (x,y).  That is, call this when
  something has changed on (x,y), eg roads, city, transform, etc.
***************************************************************/
void reset_move_costs(int x, int y)
{
  int k, x1, y1, xx[3], yy[3];
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int maxcost = 72; /* should be big enough without being TOO big */
  struct tile *tile0, *tile1;
 
  x = map_adjust_x(x);
  tile0 = map_get_tile(x, y);
  map_calc_adjacent_xy_void(x, y, xx, yy);
  debug_log_move_costs("Resetting move costs for", x, y, tile0);

  for (k = 0; k < 8; k++) {
    x1 = xx[ii[k]];
    y1 = yy[jj[k]];
    tile1 = map_get_tile(x1, y1);
    tile0->move_cost[k] = tile_move_cost_ai(tile0, tile1, x, y,
					    x1, y1, maxcost);
    /* reverse: not at all obfuscated now --dwp */
    /* this might muck with void_tile, but who cares? */
    tile1->move_cost[7 - k] = tile_move_cost_ai(tile1, tile0, x1, y1,
						x, y, maxcost);
  }
  debug_log_move_costs("Reset move costs for", x, y, tile0);
}

/***************************************************************
  Initialize tile->move_cost[] for all tiles, where move_cost[i]
  is the unit-independent cost to move _from_ that tile, to
  adjacent tile in direction specified by i.
***************************************************************/
void initialize_move_costs(void)
{
  int x, y, x1, y1, k, xx[3], yy[3];
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int maxcost = 72; /* should be big enough without being TOO big */
  struct tile *tile0, *tile1;

  for (k = 0; k < 8; k++) {
    void_tile.move_cost[k] = maxcost;
  }

  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      tile0 = map_get_tile(x, y);
      map_calc_adjacent_xy_void(x, y, xx, yy);
      for (k = 0; k < 8; k++) {
	x1 = xx[ii[k]];
	y1 = yy[jj[k]];
        tile1 = map_get_tile(x1, y1);
        tile0->move_cost[k] = tile_move_cost_ai(tile0, tile1, x, y,
						x1, y1, maxcost);
      }
    }
  }
}

/***************************************************************
  The cost to move punit from x1,y1 to x2,y2
  The tiles are assumed to be adjacent
***************************************************************/
int tile_move_cost(struct unit *punit, int x1, int y1, int x2, int y2)
{
  return tile_move_cost_ptrs(punit, map_get_tile(x1,y1),
			     map_get_tile(x2,y2), x1, y1, x2, y2);
}

/***************************************************************
  The cost to move punit from where it is to tile x1,y1.
  It is assumed the move is a valid one, e.g. the tiles are adjacent
***************************************************************/
int map_move_cost(struct unit *punit, int x1, int y1)
{
  return tile_move_cost(punit, punit->x, punit->y, x1, y1);
}

/***************************************************************
...
***************************************************************/
int is_tiles_adjacent(int x0, int y0, int x1, int y1)
{
  x0 = map_adjust_x(x0);
  x1 = map_adjust_x(x1);
  if (same_pos(x0, y0, x1, y1))
    return 1;
  return (((x0<=x1+1 && x0>=x1-1) || (x0==0 && x1==map.xsize-1) ||
	   (x0==map.xsize-1 && x1==0)) && (y0<=y1+1 && y0>=y1-1));
}



/***************************************************************
...
***************************************************************/
void tile_init(struct tile *ptile)
{
  ptile->terrain=T_UNKNOWN;
  ptile->special=S_NO_SPECIAL;
  ptile->known=0;
  ptile->city=NULL;
  unit_list_init(&ptile->units);
  ptile->worked = NULL; /* pointer to city working tile */
  ptile->assigned = 0; /* bitvector */
}


/***************************************************************
...
***************************************************************/
struct tile *map_get_tile(int x, int y)
{
  if(y<0 || y>=map.ysize)
    return &void_tile; /* accurate fix by Syela */
  else
    return map.tiles+map_adjust_x(x)+y*map.xsize;
}

/***************************************************************
...
***************************************************************/
char map_get_continent(int x, int y)
{
  if (y<0 || y>=map.ysize)
    return -1;
  else
    return (map.tiles + map_adjust_x(x) + y * map.xsize)->continent;
}

/***************************************************************
...
***************************************************************/
void map_set_continent(int x, int y, int val)
{
    (map.tiles + map_adjust_x(x) + y * map.xsize)->continent=val;
}


/***************************************************************
...
***************************************************************/
enum tile_terrain_type map_get_terrain(int x, int y)
{
  if(y<0 || y>=map.ysize)
    return T_UNKNOWN;
  else
    return (map.tiles+map_adjust_x(x)+y*map.xsize)->terrain;
}

/***************************************************************
...
***************************************************************/
enum tile_special_type map_get_special(int x, int y)
{
  if(y<0 || y>=map.ysize)
    return S_NO_SPECIAL;
  else
    return (map.tiles+map_adjust_x(x)+y*map.xsize)->special;
}

/***************************************************************
...
***************************************************************/
void map_set_terrain(int x, int y, enum tile_terrain_type ter)
{
  (map.tiles+map_adjust_x(x)+
   map_adjust_y(y)*map.xsize)->terrain=ter;
}

/***************************************************************
...
***************************************************************/
void map_set_special(int x, int y, enum tile_special_type spe)
{
  x = map_adjust_x(x); y = map_adjust_y(y);
  (map.tiles+x+y*map.xsize)->special|=spe;
  if (spe == S_ROAD || spe == S_RAILROAD) reset_move_costs(x, y);
}

/***************************************************************
...
***************************************************************/
void map_clear_special(int x, int y, enum tile_special_type spe)
{
  x = map_adjust_x(x); y = map_adjust_y(y);
  (map.tiles+x+y*map.xsize)->special&=~spe;
  if (spe == S_ROAD || spe == S_RAILROAD) reset_move_costs(x, y);
}


/***************************************************************
...
***************************************************************/
struct city *map_get_city(int x, int y)
{
  return (map.tiles+map_adjust_x(x)+map_adjust_y(y)*map.xsize)->city;
}


/***************************************************************
...
***************************************************************/
void map_set_city(int x, int y, struct city *pcity)
{
  (map.tiles+map_adjust_x(x)+map_adjust_y(y)*map.xsize)->city=pcity;
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
enum known_type tile_is_known(int x, int y)
{
  return ((map.tiles+map_adjust_x(x)+
	   map_adjust_y(y)*map.xsize)->known);
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
...
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
  Are (x1,y1) and (x2,y2) really the same when adjusted?
  This function might be necessary ALOT of places...
***************************************************************/
int same_pos(int x1, int y1, int x2, int y2)
{
  return (map_adjust_x(x1) == map_adjust_x(x2)
	  && map_adjust_y(y1) == map_adjust_y(y2)); 
}
