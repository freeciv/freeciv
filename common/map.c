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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "map.h"

/* the very map */
struct civ_map map;

/* these are initialized from the terrain ruleset */
struct terrain_misc terrain_control;
struct tile_type tile_types[T_LAST];

/* used to compute neighboring tiles */
const int DIR_DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
const int DIR_DY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

/* like DIR_DX[] and DIR_DY[], only cartesian */
const int CAR_DIR_DX[4] = {1, 0, -1, 0};
const int CAR_DIR_DY[4] = {0, 1, 0, -1};

/* Names of specials.
 * (These must correspond to enum tile_special_type in terrain.h.)
 */
static const char *tile_special_type_names[] =
{
  N_("Special1"),
  N_("Road"),
  N_("Irrigation"),
  N_("Railroad"),
  N_("Mine"),
  N_("Pollution"),
  N_("Hut"),
  N_("Fortress"),
  N_("Special2"),
  N_("River"),
  N_("Farmland"),
  N_("Airbase"),
  N_("Fallout")
};

extern bool is_server;

#define MAP_TILE(x,y)	(map.tiles + map_inx(x, y))

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
  Return a (static) string with terrain name;
  eg: "Hills"
  eg: "Hills (Coals)"
  eg: "Hills (Coals) [Pollution]"
***************************************************************/
char *map_get_tile_info_text(int x, int y)
{
  static char s[64];
  struct tile *ptile=map_get_tile(x, y);
  bool first;

  sz_strlcpy(s, tile_types[ptile->terrain].terrain_name);
  if (BOOL_VAL(ptile->special & S_RIVER)) {
    sz_strlcat(s, "/");
    sz_strlcat(s, _(get_special_name(S_RIVER)));
  }

  first = TRUE;
  if (BOOL_VAL(ptile->special & S_SPECIAL_1)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, tile_types[ptile->terrain].special_1_name);
  }
  if (BOOL_VAL(ptile->special & S_SPECIAL_2)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, tile_types[ptile->terrain].special_2_name);
  }
  if (!first) {
    sz_strlcat(s, ")");
  }

  first = TRUE;
  if (BOOL_VAL(ptile->special & S_POLLUTION)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " [");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, _(get_special_name(S_POLLUTION)));
  }
  if (BOOL_VAL(ptile->special & S_FALLOUT)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " [");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, _(get_special_name(S_FALLOUT)));
  }
  if (!first) {
    sz_strlcat(s, "]");
  }

  return s;
}

/***************************************************************
  Return a (static) string with a tile's food/prod/trade
***************************************************************/
char *map_get_tile_fpt_text(int x, int y)
{
  static char s[64];
  
  my_snprintf(s, sizeof(s), "%d/%d/%d",
	      get_food_tile(x, y),
	      get_shields_tile(x, y),
	      get_trade_tile(x, y));
  return s;
}

/***************************************************************
  Returns 1 if we are at a stage of the game where the map
  has not yet been generated/loaded.
  (To be precise, returns 1 if map_allocate() has not yet been
  called.)
***************************************************************/
bool map_is_empty(void)
{
  return !map.tiles;
}

/***************************************************************
 put some sensible values into the map structure
***************************************************************/
void map_init(void)
{
  map.xsize                 = MAP_DEFAULT_WIDTH;
  map.ysize                 = MAP_DEFAULT_HEIGHT;
  map.seed                  = MAP_DEFAULT_SEED;
  map.riches                = MAP_DEFAULT_RICHES;
  map.is_earth              = FALSE;
  map.huts                  = MAP_DEFAULT_HUTS;
  map.landpercent           = MAP_DEFAULT_LANDMASS;
  map.grasssize             = MAP_DEFAULT_GRASS;
  map.swampsize             = MAP_DEFAULT_SWAMPS;
  map.deserts               = MAP_DEFAULT_DESERTS;
  map.mountains             = MAP_DEFAULT_MOUNTAINS;
  map.riverlength           = MAP_DEFAULT_RIVERS;
  map.forestsize            = MAP_DEFAULT_FORESTS;
  map.generator             = MAP_DEFAULT_GENERATOR;
  map.tinyisles             = MAP_DEFAULT_TINYISLES;
  map.separatepoles         = MAP_DEFAULT_SEPARATE_POLES;
  map.tiles                 = NULL;
  map.num_continents        = 0;
  map.num_start_positions   = 0;
  map.fixed_start_positions = 0;
  map.have_specials         = FALSE;
  map.have_rivers_overlay   = FALSE;
  map.have_huts             = FALSE;
}

/**************************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
  Uses fc_realloc, in case map was allocated before (eg, in client);
  should have called map_init() (via game_init()) before this,
  so map.tiles will be 0 on first call.
**************************************************************************/
void map_allocate(void)
{
  freelog(LOG_DEBUG, "map_allocate (was %p) (%d,%d)",
	  map.tiles, map.xsize, map.ysize);
  
  map.tiles = fc_realloc(map.tiles, map.xsize*map.ysize*sizeof(struct tile));
  whole_map_iterate(x, y) {
    tile_init(map_get_tile(x, y));
  } whole_map_iterate_end;
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
  for (tt = T_FIRST; tt < T_COUNT; tt++) {
    if (0 == strcmp (tile_types[tt].terrain_name, name)) {
      break;
    }
  }
  return tt;
}

/***************************************************************
...
***************************************************************/
char *get_terrain_name(enum tile_terrain_type type)
{
  assert(type < T_COUNT);
  return tile_types[type].terrain_name;
}

/***************************************************************
...
***************************************************************/
enum tile_special_type get_special_by_name(char * name)
{
  int i;
  enum tile_special_type st = 1;

  for (i = 0; i < ARRAY_SIZE(tile_special_type_names); i++) {
    if (0 == strcmp(name, tile_special_type_names[i]))
      return st;
      
    st <<= 1;
  }

  return S_NO_SPECIAL;
}

/***************************************************************
...
***************************************************************/
const char *get_special_name(enum tile_special_type type)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(tile_special_type_names); i++) {
    if ((type & 0x1) == 1) {
      return tile_special_type_names[i];
    }
    type >>= 1;
  }

  return NULL;
}

/***************************************************************
...
***************************************************************/
int real_map_distance(int x0, int y0, int x1, int y1)
{
  int dx, dy;

  map_distance_vector(&dx, &dy, x0, y0, x1, y1);

  return MAX(abs(dx), abs(dy));
}

/***************************************************************
...
***************************************************************/
int sq_map_distance(int x0, int y0, int x1, int y1)
{
  /* We assume map_distance_vector gives us the vector with the
     minimum squared distance. Right now this is true. */
  int dx, dy;

  map_distance_vector(&dx, &dy, x0, y0, x1, y1);

  return (dx*dx + dy*dy);
}

/***************************************************************
...
***************************************************************/
int map_distance(int x0, int y0, int x1, int y1)
{
  /* We assume map_distance_vector gives us the vector with the
     minimum map distance. Right now this is true. */
  int dx, dy;

  map_distance_vector(&dx, &dy, x0, y0, x1, y1);

  return abs(dx) + abs(dy);
}

/***************************************************************
...
***************************************************************/
bool is_terrain_near_tile(int x, int y, enum tile_terrain_type t)
{
  adjc_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) == t)
      return TRUE;
  } adjc_iterate_end;

  return FALSE;
}

/***************************************************************
  counts tiles close to x,y having terrain t
***************************************************************/
int count_terrain_near_tile(int x, int y, enum tile_terrain_type t)
{
  int count = 0;

  adjc_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) == t)
      count++;
  } adjc_iterate_end;

  return count;
}

/***************************************************************
  determines if any tile close to x,y has special spe
***************************************************************/
bool is_special_near_tile(int x, int y, enum tile_special_type spe)
{
  adjc_iterate(x, y, x1, y1) {
    if (map_has_special(x1, y1, spe))
      return TRUE;
  } adjc_iterate_end;

  return FALSE;
}

/***************************************************************
  counts tiles close to x,y having special spe
***************************************************************/
int count_special_near_tile(int x, int y, enum tile_special_type spe)
{
  int count = 0;

  adjc_iterate(x, y, x1, y1) {
    if (map_has_special(x1, y1, spe))
      count++;
  } adjc_iterate_end;

  return count;
}

/***************************************************************
...
***************************************************************/
bool is_at_coast(int x, int y)
{
  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) == T_OCEAN)
      return TRUE;
  } cartesian_adjacent_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
bool is_coastline(int x, int y)
{
  adjc_iterate(x, y, x1, y1) {
    enum tile_terrain_type ter = map_get_terrain(x1, y1);
    if (ter != T_OCEAN
	&& ter != T_UNKNOWN)
      return TRUE;
  } adjc_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
bool terrain_is_clean(int x, int y)
{
  square_iterate(x, y, 2, x1, y1) {
    if (map_get_terrain(x1,y1) != T_GRASSLAND
	&& map_get_terrain(x1,y1) != T_PLAINS)
      return FALSE;
  } square_iterate_end;

  return TRUE;
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
bool is_starter_close(int x, int y, int nr, int dist) 
{
  int i;
  enum tile_terrain_type t = map_get_terrain(x, y);

  /* only start on clear terrain: */
  if (t!=T_PLAINS && t!=T_GRASSLAND && t!=T_RIVER)
    return TRUE;
  
  /* don't start on a hut: */
  if (map_has_special(x, y, S_HUT))
    return TRUE;
  
  /* don't want them starting on the poles unless the poles are
     connected to more land: */
  if (map_get_continent(x, y) <= 2 && map.generator != 0
      && map.separatepoles) {
    return TRUE;
  }

  /* don't start too close to someone else: */
  for (i=0;i<nr;i++) {
    int x1 = map.start_positions[i].x;
    int y1 = map.start_positions[i].y;
    if (map_get_continent(x, y) == map_get_continent(x1, y1)
	&& real_map_distance(x, y, x1, y1) < dist) {
      return TRUE;
    }
  }
  return FALSE;
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
bool is_hut_close(int x, int y)
{
  square_iterate(x, y, 3, x1, y1) {
    if (map_has_special(x1, y1, S_HUT))
      return TRUE;
  } square_iterate_end;

  return FALSE;
}


/***************************************************************
...
***************************************************************/
bool is_special_close(int x, int y)
{
  square_iterate(x, y, 1, x1, y1) {
    if (map_has_special(x1, y1, (S_SPECIAL_1 | S_SPECIAL_2)))
      return TRUE;
  } square_iterate_end;

  return FALSE;
}

/***************************************************************
Returns whether you can put a city on land near enough to use
the tile.
***************************************************************/
bool is_sea_usable(int x, int y)
{
  map_city_radius_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) != T_OCEAN)
      return TRUE;
  } map_city_radius_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
int get_tile_food_base(struct tile * ptile)
{
  if (BOOL_VAL(ptile->special & S_SPECIAL_1)) 
    return tile_types[ptile->terrain].food_special_1;
  else if (BOOL_VAL(ptile->special & S_SPECIAL_2))
    return tile_types[ptile->terrain].food_special_2;
  else
    return tile_types[ptile->terrain].food;
}

/***************************************************************
...
***************************************************************/
int get_tile_shield_base(struct tile * ptile)
{
  if (BOOL_VAL(ptile->special & S_SPECIAL_1))
    return tile_types[ptile->terrain].shield_special_1;
  else if(BOOL_VAL(ptile->special&S_SPECIAL_2))
    return tile_types[ptile->terrain].shield_special_2;
  else
    return tile_types[ptile->terrain].shield;
}

/***************************************************************
...
***************************************************************/
int get_tile_trade_base(struct tile * ptile)
{
  if (BOOL_VAL(ptile->special & S_SPECIAL_1))
    return tile_types[ptile->terrain].trade_special_1;
  else if (BOOL_VAL(ptile->special & S_SPECIAL_2))
    return tile_types[ptile->terrain].trade_special_2;
  else
    return tile_types[ptile->terrain].trade;
}

/***************************************************************
  Return a (static) string with special(s) name(s);
  eg: "Mine"
  eg: "Road/Farmland"
***************************************************************/
char *map_get_infrastructure_text(int spe)
{
  static char s[64];
  char *p;
  
  s[0] = '\0';

  /* Since railroad requires road, Road/Railroad is redundant */
  if (BOOL_VAL(spe & S_RAILROAD))
    cat_snprintf(s, sizeof(s), "%s/", _("Railroad"));
  else if (BOOL_VAL(spe & S_ROAD))
    cat_snprintf(s, sizeof(s), "%s/", _("Road"));

  /* Likewise for farmland on irrigation */
  if (BOOL_VAL(spe & S_FARMLAND))
    cat_snprintf(s, sizeof(s), "%s/", _("Farmland"));
  else if (BOOL_VAL(spe & S_IRRIGATION))
    cat_snprintf(s, sizeof(s), "%s/", _("Irrigation"));

  if (BOOL_VAL(spe & S_MINE))
    cat_snprintf(s, sizeof(s), "%s/", _("Mine"));

  if (BOOL_VAL(spe & S_FORTRESS))
    cat_snprintf(s, sizeof(s), "%s/", _("Fortress"));

  if (BOOL_VAL(spe & S_AIRBASE))
    cat_snprintf(s, sizeof(s), "%s/", _("Airbase"));

  p = s + strlen(s) - 1;
  if (*p == '/')
    *p = '\0';

  return s;
}

/***************************************************************
...
***************************************************************/
int map_get_infrastructure_prerequisite(int spe)
{
  int prereq = S_NO_SPECIAL;

  if (BOOL_VAL(spe & S_RAILROAD))
    prereq |= S_ROAD;
  if (BOOL_VAL(spe & S_FARMLAND))
    prereq |= S_IRRIGATION;

  return prereq;
}

/***************************************************************
...
***************************************************************/
int get_preferred_pillage(int pset)
{
  if (BOOL_VAL(pset & S_FARMLAND))
    return S_FARMLAND;
  if (BOOL_VAL(pset & S_IRRIGATION))
    return S_IRRIGATION;
  if (BOOL_VAL(pset & S_MINE))
    return S_MINE;
  if (BOOL_VAL(pset & S_FORTRESS))
    return S_FORTRESS;
  if (BOOL_VAL(pset & S_AIRBASE))
    return S_AIRBASE;
  if (BOOL_VAL(pset & S_RAILROAD))
    return S_RAILROAD;
  if (BOOL_VAL(pset & S_ROAD))
    return S_ROAD;
  return S_NO_SPECIAL;
}

/***************************************************************
...
***************************************************************/
bool is_water_adjacent_to_tile(int x, int y)
{
  struct tile *ptile;

  ptile = map_get_tile(x, y);
  if (ptile->terrain == T_OCEAN
      || ptile->terrain == T_RIVER
      || BOOL_VAL(ptile->special & S_RIVER)
      || BOOL_VAL(ptile->special & S_IRRIGATION))
    return TRUE;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    ptile = map_get_tile(x1, y1);
    if (ptile->terrain == T_OCEAN
	|| ptile->terrain == T_RIVER
	|| BOOL_VAL(ptile->special & S_RIVER)
	|| BOOL_VAL(ptile->special & S_IRRIGATION))
      return TRUE;
  } cartesian_adjacent_iterate_end;

  return FALSE;
}

/***************************************************************
...
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
int map_build_rail_time(int x, int y)
{
  return 3;
}

/***************************************************************
...
***************************************************************/
int map_build_airbase_time(int x, int y)
{
  return 3;
}

/***************************************************************
...
***************************************************************/
int map_build_fortress_time(int x, int y)
{
  return 3;
}

/***************************************************************
...
***************************************************************/
int map_clean_pollution_time(int x, int y)
{
  return 3;
}

/***************************************************************
...
***************************************************************/
int map_clean_fallout_time(int x, int y)
{
  return 3;
}

/***************************************************************
  Time to complete given activity on given tile.
***************************************************************/
int map_activity_time(enum unit_activity activity, int x, int y)
{
  switch (activity) {
  case ACTIVITY_POLLUTION:
    return map_clean_pollution_time(x, y);
  case ACTIVITY_ROAD:
    return map_build_road_time(x, y);
  case ACTIVITY_MINE:
    return map_build_mine_time(x, y);
  case ACTIVITY_IRRIGATE:
    return map_build_irrigation_time(x, y);
  case ACTIVITY_FORTRESS:
    return map_build_fortress_time(x, y);
  case ACTIVITY_RAILROAD:
    return map_build_rail_time(x, y);
  case ACTIVITY_TRANSFORM:
    return map_transform_time(x, y);
  case ACTIVITY_AIRBASE:
    return map_build_airbase_time(x, y);
  case ACTIVITY_FALLOUT:
    return map_clean_fallout_time(x, y);
  default:
    return 0;
  }
}

/***************************************************************
...
***************************************************************/
static void clear_infrastructure(int x, int y)
{
  map_clear_special(x, y, S_INFRASTRUCTURE_MASK);
}

/***************************************************************
...
***************************************************************/
static void clear_dirtiness(int x, int y)
{
  map_clear_special(x, y, S_POLLUTION | S_FALLOUT);
}

/***************************************************************
...
***************************************************************/
void map_irrigate_tile(int x, int y)
{
  enum tile_terrain_type now, result;
  
  now = map_get_terrain(x, y);
  result = tile_types[now].irrigation_result;

  if (now == result) {
    if (map_has_special(x, y, S_IRRIGATION)) {
      map_set_special(x, y, S_FARMLAND);
    } else {
      map_set_special(x, y, S_IRRIGATION);
    }
  }
  else if (result != T_LAST) {
    map_set_terrain(x, y, result);
    if (result == T_OCEAN) {
      clear_infrastructure(x, y);
      clear_dirtiness(x, y);
      map_clear_special(x, y, S_RIVER);	/* FIXME: When rest of code can handle
					   rivers in oceans, don't clear this! */
    }
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
  
  now = map_get_terrain(x, y);
  result = tile_types[now].mining_result;
  
  if (now == result) 
    map_set_special(x, y, S_MINE);
  else if (result != T_LAST) {
    map_set_terrain(x, y, result);
    if (result == T_OCEAN) {
      clear_infrastructure(x, y);
      clear_dirtiness(x, y);
      map_clear_special(x, y, S_RIVER);	/* FIXME: When rest of code can handle
					   rivers in oceans, don't clear this! */
    }
    reset_move_costs(x, y);
  }
  map_clear_special(x, y, S_FARMLAND);
  map_clear_special(x, y, S_IRRIGATION);
}

/***************************************************************
...
***************************************************************/
void change_terrain(int x, int y, enum tile_terrain_type type)
{
  map_set_terrain(x, y, type);
  if (type == T_OCEAN) {
    clear_infrastructure(x, y);
    clear_dirtiness(x, y);
    map_clear_special(x, y, S_RIVER);	/* FIXME: When rest of code can handle
					   rivers in oceans, don't clear this! */
  }

  reset_move_costs(x, y);

  /* Clear mining/irrigation if resulting terrain type cannot support
     that feature.  (With current rules, this should only clear mines,
     but I'm including both cases in the most general form for possible
     future ruleset expansion. -GJW) */
  
  if (tile_types[type].mining_result != type)
    map_clear_special(x, y, S_MINE);

  if (tile_types[type].irrigation_result != type)
    map_clear_special(x, y, S_FARMLAND | S_IRRIGATION);
}

/***************************************************************
...
***************************************************************/
void map_transform_tile(int x, int y)
{
  enum tile_terrain_type now, result;
  
  now = map_get_terrain(x, y);
  result = tile_types[now].transform_result;
  
  if (result != T_LAST)
    change_terrain(x, y, result);
}

/**************************************************************************
This function returns true if the tile at the given location can be
"reclaimed" from ocean into land.  This is the case only when there are
a sufficient number of adjacent tiles that are not ocean.
**************************************************************************/
bool can_reclaim_ocean(int x, int y)
{
  int landtiles = terrain_control.ocean_reclaim_requirement;

  if (landtiles >= 9)
    return FALSE;
  if (landtiles <= 0)
    return TRUE;

  adjc_iterate(x, y, x1, y1) {
    if (map_get_tile(x1, y1)->terrain != T_OCEAN)
      if (--landtiles == 0)
	return TRUE;	
  } adjc_iterate_end;

  return FALSE;
}

/**************************************************************************
This function returns true if the tile at the given location can be
"channeled" from land into ocean.  This is the case only when there are
a sufficient number of adjacent tiles that are ocean.
**************************************************************************/
bool can_channel_land(int x, int y)
{
  int oceantiles = terrain_control.land_channel_requirement;

  if (oceantiles >= 9)
    return FALSE;
  if (oceantiles <= 0)
    return TRUE;

  adjc_iterate(x, y, x1, y1) {
    if (map_get_tile(x1, y1)->terrain == T_OCEAN)
      if (--oceantiles == 0)
	return TRUE;
  } adjc_iterate_end;

  return FALSE;
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
  bool cardinal_move;

  if (punit && !is_ground_unit(punit))
    return SINGLE_MOVE;
  if (BOOL_VAL(t1->special & S_RAILROAD) && BOOL_VAL(t2->special & S_RAILROAD))
    return MOVE_COST_RAIL;
/* return (unit_move_rate(punit)/RAIL_MAX) */
  if (punit && unit_flag(punit, F_IGTER))
    return SINGLE_MOVE/3;
  if (BOOL_VAL(t1->special & S_ROAD) && BOOL_VAL(t2->special & S_ROAD))
    return MOVE_COST_ROAD;

  if (((t1->terrain == T_RIVER) && (t2->terrain == T_RIVER)) ||
      (BOOL_VAL(t1->special & S_RIVER) && BOOL_VAL(t2->special & S_RIVER))) {
    cardinal_move = is_move_cardinal(x1, y1, x2, y2);
    switch (terrain_control.river_move_mode) {
    case RMV_NORMAL:
      break;
    case RMV_FAST_STRICT:
      if (cardinal_move)
	return MOVE_COST_RIVER;
      break;
    case RMV_FAST_RELAXED:
      if (cardinal_move)
	return MOVE_COST_RIVER;
      else
	return 2 * MOVE_COST_RIVER;
    case RMV_FAST_ALWAYS:
      return MOVE_COST_RIVER;
    default:
      break;
    }
  }

  return(get_tile_type(t2->terrain)->movement_cost*SINGLE_MOVE);
}

/***************************************************************
  tile_move_cost_ai is used to fill the move_cost array of struct
  tile. The cached values of this array are used in server/gotohand.c
  and client/goto.c. tile_move_cost_ai returns the move cost as
  calculated by tile_move_cost_ptrs (with no unit pointer to get
  unit-independent results) EXCEPT if either of the source or
  destination tile is an ocean tile. Then the result of the method
  shows if a ship can take the step from the source position to the
  destination position (return value is MOVE_COST_FOR_VALID_SEA_STEP)
  or not (return value is maxcost).

  A ship can take the step if:
    - both tiles are ocean or
    - one of the tiles is ocean and the other is a city or is unknown
***************************************************************/
static int tile_move_cost_ai(struct tile *tile0, struct tile *tile1,
			     int x, int y, int x1, int y1, int maxcost)
{
  assert(is_real_tile(x, y));
  assert(!is_server
	 || (tile0->terrain != T_UNKNOWN && tile1->terrain != T_UNKNOWN));

  if (tile0->terrain == T_OCEAN && tile1->terrain == T_OCEAN) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (tile0->terrain == T_OCEAN
      && (tile1->city || tile1->terrain == T_UNKNOWN)) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (tile1->terrain == T_OCEAN
      && (tile0->city || tile0->terrain == T_UNKNOWN)) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (tile0->terrain == T_OCEAN || tile1->terrain == T_OCEAN) {
    return maxcost;
  }

  return tile_move_cost_ptrs(NULL, tile0, tile1, x, y, x1, y1);
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
  int maxcost = 72; /* should be big enough without being TOO big */
  struct tile *tile0, *tile1;

  tile0 = map_get_tile(x, y);
  debug_log_move_costs("Resetting move costs for", x, y, tile0);

  /* trying to move off the screen is the default */
  memset(tile0->move_cost, maxcost, sizeof(tile0->move_cost));

  adjc_dir_iterate(x, y, x1, y1, dir) {
    tile1 = map_get_tile(x1, y1);
    tile0->move_cost[dir] = tile_move_cost_ai(tile0, tile1, x, y,
					      x1, y1, maxcost);
    /* reverse: not at all obfuscated now --dwp */
    tile1->move_cost[DIR_REVERSE(dir)] =
	tile_move_cost_ai(tile1, tile0, x1, y1, x, y, maxcost);
  } adjc_dir_iterate_end;
  debug_log_move_costs("Reset move costs for", x, y, tile0);
}

/***************************************************************
  Initialize tile->move_cost[] for all tiles, where move_cost[i]
  is the unit-independent cost to move _from_ that tile, to
  adjacent tile in direction specified by i.
***************************************************************/
void initialize_move_costs(void)
{
  int maxcost = 72; /* should be big enough without being TOO big */

  whole_map_iterate(x, y) {
    struct tile *tile0, *tile1;
    tile0 = map_get_tile(x, y);

    /* trying to move off the screen is the default */
    memset(tile0->move_cost, maxcost, sizeof(tile0->move_cost));

    adjc_dir_iterate(x, y, x1, y1, dir) {
      tile1 = map_get_tile(x1, y1);
      tile0->move_cost[dir] = tile_move_cost_ai(tile0, tile1, x, y,
						x1, y1, maxcost);
    }
    adjc_dir_iterate_end;
  } whole_map_iterate_end;
}

/***************************************************************
  The cost to move punit from where it is to tile x,y.
  It is assumed the move is a valid one, e.g. the tiles are adjacent.
***************************************************************/
int map_move_cost(struct unit *punit, int x, int y)
{
  return tile_move_cost_ptrs(punit, map_get_tile(punit->x,punit->y),
			     map_get_tile(x, y), punit->x, punit->y, x, y);
}

/***************************************************************
...
***************************************************************/
bool is_tiles_adjacent(int x0, int y0, int x1, int y1)
{
  return real_map_distance(x0, y0, x1, y1) == 1;
}

/***************************************************************
...
***************************************************************/
void tile_init(struct tile *ptile)
{
  ptile->terrain  = T_UNKNOWN;
  ptile->special  = S_NO_SPECIAL;
  ptile->known    = 0;
  ptile->sent     = 0;
  ptile->city     = NULL;
  unit_list_init(&ptile->units);
  ptile->worked   = NULL; /* pointer to city working tile */
  ptile->assigned = 0; /* bitvector */
}

/***************************************************************
...
***************************************************************/
struct tile *map_get_tile(int x, int y)
{
  return MAP_TILE(x, y);
}

/***************************************************************
...
***************************************************************/
signed short map_get_continent(int x, int y)
{
  return MAP_TILE(x, y)->continent;
}

/***************************************************************
...
***************************************************************/
void map_set_continent(int x, int y, int val)
{
  MAP_TILE(x, y)->continent = val;
}


/***************************************************************
...
***************************************************************/
enum tile_terrain_type map_get_terrain(int x, int y)
{
  return MAP_TILE(x, y)->terrain;
}

/***************************************************************
...
***************************************************************/
enum tile_special_type map_get_special(int x, int y)
{
  return MAP_TILE(x, y)->special;
}

/***************************************************************
 Returns TRUE iff the given special is found at the given map
 position.
***************************************************************/
bool map_has_special(int x, int y, enum tile_special_type special)
{
  return BOOL_VAL(MAP_TILE(x, y)->special & special);
}

/***************************************************************
...
***************************************************************/
void map_set_terrain(int x, int y, enum tile_terrain_type ter)
{
  MAP_TILE(x, y)->terrain = ter;
}

/***************************************************************
...
***************************************************************/
void map_set_special(int x, int y, enum tile_special_type spe)
{
  MAP_TILE(x, y)->special |= spe;

  if (BOOL_VAL(spe & (S_ROAD | S_RAILROAD)))
    reset_move_costs(x, y);
}

/***************************************************************
...
***************************************************************/
void map_clear_special(int x, int y, enum tile_special_type spe)
{
  MAP_TILE(x, y)->special &= ~spe;

  if (BOOL_VAL(spe & (S_ROAD | S_RAILROAD)))
    reset_move_costs(x, y);
}

/***************************************************************
...
***************************************************************/
struct city *map_get_city(int x, int y)
{
  return MAP_TILE(x, y)->city;
}

/***************************************************************
...
***************************************************************/
void map_set_city(int x, int y, struct city *pcity)
{
  MAP_TILE(x, y)->city = pcity;
}

/***************************************************************
  Are (x1,y1) and (x2,y2) really the same when adjusted?
  This function might be necessary ALOT of places...
***************************************************************/
bool same_pos(int x1, int y1, int x2, int y2)
{
  CHECK_MAP_POS(x1, y1);
  CHECK_MAP_POS(x2, y2);
  return (x1 == x2 && y1 == y2);
}

bool is_real_tile(int x, int y)
{
  return normalize_map_pos(&x, &y);
}

/**************************************************************************
Returns TRUE iff the map position is normal. "Normal" here means that
it is both a real/valid coordinate set and that the coordinates are in
their canonical/proper form. In plain English: the coordinates must be
on the map.
**************************************************************************/
bool is_normal_map_pos(int x, int y)
{
  int x1 = x, y1 = y;

  return (normalize_map_pos(&x1, &y1) && (x1 == x) && (y1 == y));
}

/**************************************************************************
Normalizes the map position. Returns TRUE if it is real, FALSE otherwise.
**************************************************************************/
bool normalize_map_pos(int *x, int *y)
{
  while (*x < 0)
    *x += map.xsize;
  while (*x >= map.xsize)
    *x -= map.xsize;

  return (0 <= *y && *y < map.ysize);
}

/**************************************************************************
Twiddle *x and *y to point the the nearest real tile, and ensure that the
position is normalized.
**************************************************************************/
void nearest_real_pos(int *x, int *y)
{
  if (*y < 0)
    *y = 0;
  else if (*y >= map.ysize)
    *y = map.ysize - 1;
  
  while (*x < 0)
    *x += map.xsize;
  while (*x >= map.xsize)
    *x -= map.xsize;
}

/**************************************************************************
Returns the total number of (real) positions (or tiles) on the map.
**************************************************************************/
int map_num_tiles(void)
{
  return map.xsize * map.ysize;
}

/**************************************************************************
Topology function to find the vector which has the minimum "real"
distance between the map positions (x0, y0) and (x1, y1).  If there is
more than one vector with equal distance, no guarantee is made about
which is found.

Real distance is defined as the larger of the distances in the x and y
direction; since units can travel diagonally this is the "real" distance
a unit has to travel to get from point to point.

(See also: real_map_distance, map_distance, and sq_map_distance.)

The ranges of the return values are currently:
-map.xsize/2 < dx <= map.xsize/2
-map.ysize   < dy <  map.ysize
**************************************************************************/
void map_distance_vector(int *dx, int *dy, int x0, int y0, int x1, int y1)
{
  CHECK_MAP_POS(x0, y0);
  CHECK_MAP_POS(x1, y1);

  *dx = x1 - x0;

  if (*dx > map.xsize / 2) {
    *dx -= map.xsize;
  } else if (*dx <= -map.xsize / 2) {
    *dx += map.xsize;
  }

  *dy = y1 - y0;
}

/**************************************************************************
Random neighbouring square.
**************************************************************************/
void rand_neighbour(int x0, int y0, int *x, int *y)
{
  int n;
  /* 
   * list of all 8 directions 
   */
  enum direction8 dirs[8] = {
    DIR8_NORTHWEST, DIR8_NORTH, DIR8_NORTHEAST, DIR8_WEST, DIR8_EAST,
    DIR8_SOUTHWEST, DIR8_SOUTH, DIR8_SOUTHEAST
  };

  assert(is_real_tile(x0, y0));

  /* This clever loop by Trent Piepho will take no more than
   * 8 tries to find a valid direction. */
  for (n = 8; n > 0; n--) {
    enum direction8 choice = (enum direction8) myrand(n);

    /* this neighbour's OK */
    if (MAPSTEP(*x, *y, x0, y0, dirs[choice]))
      return;

    /* Choice was bad, so replace it with the last direction in the list.
     * On the next iteration, one fewer choices will remain. */
    dirs[choice] = dirs[n - 1];
  }

  assert(0);			/* Are we on a 1x1 map with no wrapping??? */
}

/**************************************************************************
 Random square anywhere on the map.  Only normal positions (for which
 is_normal_map_pos returns true) will be found.
**************************************************************************/
void rand_map_pos(int *x, int *y)
{
  do {
    *x = myrand(map.xsize);
    *y = myrand(map.ysize);
  } while (!is_normal_map_pos(*x, *y));
}

/**************************************************************************
Return the debugging name of the direction.
**************************************************************************/
const char *dir_get_name(enum direction8 dir)
{
  /* a switch statement is used so the ordering can be changed easily */
  switch (dir) {
  case DIR8_NORTH:
    return "N";
  case DIR8_NORTHEAST:
    return "NE";
  case DIR8_EAST:
    return "E";
  case DIR8_SOUTHEAST:
    return "SE";
  case DIR8_SOUTH:
    return "S";
  case DIR8_SOUTHWEST:
    return "SW";
  case DIR8_WEST:
    return "W";
  case DIR8_NORTHWEST:
    return "NW";
  default:
    assert(0);
    return "[Bad Direction]";
  }
}

/**************************************************************************
Return true and sets dir to the direction of the step if (end_x,
end_y) can be reached from (start_x, start_y) in one step. Return
false otherwise (value of dir is unchanged in this case).
**************************************************************************/
bool base_get_direction_for_step(int start_x, int start_y, int end_x,
				int end_y, int *dir)
{
  adjc_dir_iterate(start_x, start_y, x1, y1, dir2) {
    if (x1 == end_x && y1 == end_y) {
      *dir = dir2;
      return TRUE;
    }
  } adjc_dir_iterate_end;

  return FALSE;
}

/**************************************************************************
Return the direction which is needed for a step on the map from
(start_x, start_y) to (end_x, end_y).
**************************************************************************/
int get_direction_for_step(int start_x, int start_y, int end_x, int end_y)
{
  int dir;

  if (base_get_direction_for_step(start_x, start_y, end_x, end_y, &dir)) {
    return dir;
  }

  assert(0);
  return -1;
}

/**************************************************************************
Returns 1 if the move from the position (start_x,start_y) to
(end_x,end_y) is a cardinal move. Else returns 0.
**************************************************************************/
bool is_move_cardinal(int start_x, int start_y, int end_x, int end_y)
{
  int diff_x, diff_y;

  assert(is_tiles_adjacent(start_x, start_y, end_x, end_y));

  map_distance_vector(&diff_x, &diff_y, start_x, start_y, end_x, end_y);
  return (diff_x == 0) || (diff_y == 0);
}
