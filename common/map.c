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
#include <string.h>		/* strlen */

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "map.h"

/* the very map */
struct civ_map map;

/* these are initialized from the terrain ruleset */
struct terrain_misc terrain_control;

/* used to compute neighboring tiles.
 *
 * using
 *   x1 = x + DIR_DX[dir];
 *   y1 = y + DIR_DY[dir];
 * will give you the tile as shown below.
 *   -------
 *   |0|1|2|
 *   |-+-+-|
 *   |3| |4|
 *   |-+-+-|
 *   |5|6|7|
 *   -------
 * Note that you must normalize x1 and y1 yourself.
 */
const int DIR_DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
const int DIR_DY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

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

#define MAP_TILE(x,y)	(map.tiles + map_pos_to_index(x, y))
#define NAT_TILE(x, y)  (map.tiles + native_pos_to_index(x, y))

/****************************************************************************
  Return a bitfield of the specials on the tile that are infrastructure.
****************************************************************************/
enum tile_special_type get_tile_infrastructure_set(struct tile *ptile)
{
  return (ptile->special
	  & (S_ROAD | S_RAILROAD | S_IRRIGATION | S_FARMLAND | S_MINE
	     | S_FORTRESS | S_AIRBASE));
}

/***************************************************************
  Return a (static) string with terrain name;
  eg: "Hills"
  eg: "Hills (Coals)"
  eg: "Hills (Coals) [Pollution]"
***************************************************************/
const char *map_get_tile_info_text(int x, int y)
{
  static char s[64];
  struct tile *ptile=map_get_tile(x, y);
  bool first;

  sz_strlcpy(s, tile_types[ptile->terrain].terrain_name);
  if (tile_has_special(ptile, S_RIVER)) {
    sz_strlcat(s, "/");
    sz_strlcat(s, get_special_name(S_RIVER));
  }

  first = TRUE;
  if (tile_has_special(ptile, S_SPECIAL_1)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, tile_types[ptile->terrain].special_1_name);
  }
  if (tile_has_special(ptile, S_SPECIAL_2)) {
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
  if (tile_has_special(ptile, S_POLLUTION)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " [");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, get_special_name(S_POLLUTION));
  }
  if (tile_has_special(ptile, S_FALLOUT)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " [");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, get_special_name(S_FALLOUT));
  }
  if (!first) {
    sz_strlcat(s, "]");
  }

  return s;
}

/***************************************************************
  Return a (static) string with a tile's food/prod/trade
***************************************************************/
const char *map_get_tile_fpt_text(int x, int y)
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
  map.topology_id = MAP_DEFAULT_TOPO;
  map.size = MAP_DEFAULT_SIZE;

  /* The [xy]size values are set in map_init_topology.  It is initialized
   * to a non-zero value because some places erronously use these values
   * before they're initialized. */
  map.xsize = MAP_MIN_LINEAR_SIZE;  
  map.ysize = MAP_MIN_LINEAR_SIZE;

  map.seed                  = MAP_DEFAULT_SEED;
  map.riches                = MAP_DEFAULT_RICHES;
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
  map.alltemperate          = MAP_DEFAULT_ALLTEMPERATE;
  map.tiles                 = NULL;
  map.num_continents        = 0;
  map.num_start_positions   = 0;
  map.have_specials         = FALSE;
  map.have_rivers_overlay   = FALSE;
  map.have_huts             = FALSE;
}

/****************************************************************************
  Set the map xsize and ysize based on a base size and ratio (in natural
  coordinates).
****************************************************************************/
static void set_ratio(int size, int Xratio, int Yratio)
{
  /* Future topologies may require even dimensions (set this to 2). */
  const int even = 1;

  /* In TF_ISO we need to double the map.ysize factor, since xsize is
   * in native coordinates which are compressed 2x in the X direction. */ 
  const int iso = topo_has_flag(TF_ISO) ? 2 : 1;

  /* We have:
   *
   *   1000 * size = xsize * ysize
   *
   * And to satisfy the ratios and other constraints we set
   *
   *   xsize = isize * xratio * even
   *   ysize = isize * yratio * even * iso
   *
   * For any value of "isize".  So with some substitution
   *
   *   1000 * size = isize * isize * xratio * yratio * even * even * iso
   *   isize = sqrt(1000 * size / (xratio * yratio * even * even * iso))
   */
  const float i_size
    = sqrt((float)(1000 * size)
	   / (float)(Xratio * Yratio * iso * even * even));

  /* Now build xsize and ysize value as described above.  Make sure to
   * round off _before_ multiplying by even and iso so that we can end up
   * with even values if needed. */
  map.xsize = (int)(Xratio * i_size + 0.5) * even;
  map.ysize = (int)(Yratio * i_size + 0.5) * even * iso;

  /* The size and ratio must satisfy the minimum and maximum *linear*
   * restrictions on width.  If not then either these restrictions should
   * be lifted or the size should be more limited. */
  assert(MAP_WIDTH >= MAP_MIN_LINEAR_SIZE);
  assert(MAP_HEIGHT >= MAP_MIN_LINEAR_SIZE);
  assert(MAP_WIDTH <= MAP_MAX_LINEAR_SIZE);
  assert(MAP_HEIGHT <= MAP_MAX_LINEAR_SIZE);

  freelog(LOG_VERBOSE,
	  "Creating a map of size %d x %d = %d tiles (%d requested).",
	  map.xsize, map.ysize, map.xsize * map.ysize, size * 1000);
}

/*
 * The auto ratios for known topologies
 */
#define AUTO_RATIO_FLAT           {1, 1}
#define AUTO_RATIO_CLASSIC        {8, 5} 
#define AUTO_RATIO_URANUS         {5, 8} 
#define AUTO_RATIO_TORUS          {1, 1}

/****************************************************************************
  map_init_topology needs to be called after map.topology_id is changed.

  If map.size is changed, call map_init_topology(TRUE) to calculate map.xsize
  and map.ysize based on the default ratio.

  If map.xsize and map.ysize are changed, call map_init_topology(FALSE) to
  calculate map.size.  This should be done in the client or when loading
  savegames, since the [xy]size values are already known.
****************************************************************************/
void map_init_topology(bool set_sizes)
{
  enum direction8 dir;

  /* Changing or reordering the topo_flag enum will break this code. */
  const int default_ratios[4][2] =
      {AUTO_RATIO_FLAT, AUTO_RATIO_CLASSIC,
       AUTO_RATIO_URANUS, AUTO_RATIO_TORUS};
  const int id = 0x3 & map.topology_id;
  
  assert(TF_WRAPX == 0x1 && TF_WRAPY == 0x2);

  if (set_sizes) {
    /* Set map.xsize and map.ysize based on map.size. */
    set_ratio(map.size, default_ratios[id][0], default_ratios[id][1]);  
  } else {
    /* Set map.size based on map.xsize and map.ysize. */
    map.size = (float)(map.xsize * map.ysize) / 1000.0 + 0.5;
  }

  map.num_valid_dirs = map.num_cardinal_dirs = 0;
  for (dir = 0; dir < 8; dir++) {
    if (is_valid_dir(dir)) {
      map.valid_dirs[map.num_valid_dirs] = dir;
      map.num_valid_dirs++;
    }
    if (DIR_IS_CARDINAL(dir)) {
      map.cardinal_dirs[map.num_cardinal_dirs] = dir;
      map.num_cardinal_dirs++;
    }
  }
  assert(map.num_valid_dirs > 0 && map.num_valid_dirs <= 8);
  assert(map.num_cardinal_dirs > 0
	 && map.num_cardinal_dirs <= map.num_valid_dirs);
}

/***************************************************************
...
***************************************************************/
static void tile_init(struct tile *ptile)
{
  ptile->terrain  = T_UNKNOWN;
  ptile->special  = S_NO_SPECIAL;
  ptile->known    = 0;
  ptile->continent = 0;
  ptile->city     = NULL;
  unit_list_init(&ptile->units);
  ptile->worked   = NULL; /* pointer to city working tile */
  ptile->assigned = 0; /* bitvector */
  ptile->owner    = NULL; /* Tile not claimed by any nation. */
  if (!is_server) {
    ptile->client.hilite = HILITE_NONE; /* Area Selection in client. */
  }
  ptile->spec_sprite = NULL;
}

/**************************************************************************
  Return the player who owns this tile (or NULL if none).
**************************************************************************/
struct player *map_get_owner(int x, int y)
{
  return MAP_TILE(x, y)->owner;
}

/**************************************************************************
  Set the owner of a tile (may be NULL).
**************************************************************************/
void map_set_owner(int x, int y, struct player *owner)
{
  MAP_TILE(x, y)->owner = owner;
}

/***************************************************************
...
***************************************************************/
static void tile_free(struct tile *ptile)
{
  unit_list_unlink_all(&ptile->units);
  if (ptile->spec_sprite) {
    free(ptile->spec_sprite);
    ptile->spec_sprite = NULL;
  }
}

/**************************************************************************
  Allocate space for map, and initialise the tiles.
  Uses current map.xsize and map.ysize.
**************************************************************************/
void map_allocate(void)
{
  freelog(LOG_DEBUG, "map_allocate (was %p) (%d,%d)",
	  map.tiles, map.xsize, map.ysize);

  assert(map.tiles == NULL);
  map.tiles = fc_malloc(map.xsize * map.ysize * sizeof(struct tile));
  whole_map_iterate(x, y) {
    tile_init(map_get_tile(x, y));
  } whole_map_iterate_end;

  generate_city_map_indices();
}

/***************************************************************
  Frees the allocated memory of the map.
***************************************************************/
void map_free(void)
{
  if (map.tiles) {
    /* it is possible that map_init was called but not map_allocate */

    whole_map_iterate(x, y) {
      tile_free(map_get_tile(x, y));
    }
    whole_map_iterate_end;

    free(map.tiles);
    map.tiles = NULL;
  }
}

/***************************************************************
...
***************************************************************/
enum tile_special_type get_special_by_name(const char * name)
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
      return _(tile_special_type_names[i]);
    }
    type >>= 1;
  }

  return NULL;
}

/****************************************************************************
  Return the "distance" (which is really the Manhattan distance, and should
  rarely be used) for a given vector.
****************************************************************************/
static int map_vector_to_distance(int dx, int dy)
{
  return abs(dx) + abs(dy);
}

/****************************************************************************
  Return the "real" distance for a given vector.
****************************************************************************/
int map_vector_to_real_distance(int dx, int dy)
{
  return MAX(abs(dx), abs(dy));
}

/****************************************************************************
  Return the sq_distance for a given vector.
****************************************************************************/
int map_vector_to_sq_distance(int dx, int dy)
{
  return dx * dx + dy * dy;
}

/***************************************************************
...
***************************************************************/
int real_map_distance(int x0, int y0, int x1, int y1)
{
  int dx, dy;

  map_distance_vector(&dx, &dy, x0, y0, x1, y1);
  return map_vector_to_real_distance(dx, dy);
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
  return map_vector_to_sq_distance(dx, dy);
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
  return map_vector_to_distance(dx, dy);
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
  cardinal_adjc_iterate(x, y, x1, y1) {
    if (is_ocean(map_get_terrain(x1, y1))) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return TRUE if this ocean terrain is adjacent to a safe coastline.
****************************************************************************/
bool is_safe_ocean(int x, int y)
{
  adjc_iterate(x, y, x1, y1) {
    enum tile_terrain_type ter = map_get_terrain(x1, y1);
    if (!terrain_has_flag(ter, TER_UNSAFE_COAST) && ter != T_UNKNOWN) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/***************************************************************
Returns whether you can put a city on land near enough to use
the tile.
***************************************************************/
bool is_sea_usable(int x, int y)
{
  map_city_radius_iterate(x, y, x1, y1) {
    if (!is_ocean(map_get_terrain(x1, y1))) {
      return TRUE;
    }
  } map_city_radius_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
int get_tile_food_base(struct tile * ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1)) 
    return tile_types[ptile->terrain].food_special_1;
  else if (tile_has_special(ptile, S_SPECIAL_2))
    return tile_types[ptile->terrain].food_special_2;
  else
    return tile_types[ptile->terrain].food;
}

/***************************************************************
...
***************************************************************/
int get_tile_shield_base(struct tile * ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1))
    return tile_types[ptile->terrain].shield_special_1;
  else if(tile_has_special(ptile, S_SPECIAL_2))
    return tile_types[ptile->terrain].shield_special_2;
  else
    return tile_types[ptile->terrain].shield;
}

/***************************************************************
...
***************************************************************/
int get_tile_trade_base(struct tile * ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1))
    return tile_types[ptile->terrain].trade_special_1;
  else if (tile_has_special(ptile, S_SPECIAL_2))
    return tile_types[ptile->terrain].trade_special_2;
  else
    return tile_types[ptile->terrain].trade;
}

/***************************************************************
  Return a (static) string with special(s) name(s);
  eg: "Mine"
  eg: "Road/Farmland"
***************************************************************/
const char *map_get_infrastructure_text(enum tile_special_type spe)
{
  static char s[64];
  char *p;
  
  s[0] = '\0';

  /* Since railroad requires road, Road/Railroad is redundant */
  if (contains_special(spe, S_RAILROAD))
    cat_snprintf(s, sizeof(s), "%s/", _("Railroad"));
  else if (contains_special(spe, S_ROAD))
    cat_snprintf(s, sizeof(s), "%s/", _("Road"));

  /* Likewise for farmland on irrigation */
  if (contains_special(spe, S_FARMLAND))
    cat_snprintf(s, sizeof(s), "%s/", _("Farmland"));
  else if (contains_special(spe, S_IRRIGATION))
    cat_snprintf(s, sizeof(s), "%s/", _("Irrigation"));

  if (contains_special(spe, S_MINE))
    cat_snprintf(s, sizeof(s), "%s/", _("Mine"));

  if (contains_special(spe, S_FORTRESS))
    cat_snprintf(s, sizeof(s), "%s/", _("Fortress"));

  if (contains_special(spe, S_AIRBASE))
    cat_snprintf(s, sizeof(s), "%s/", _("Airbase"));

  p = s + strlen(s) - 1;
  if (*p == '/')
    *p = '\0';

  return s;
}

/****************************************************************************
  Return the prerequesites needed before building the given infrastructure.
****************************************************************************/
enum tile_special_type map_get_infrastructure_prerequisite(enum tile_special_type spe)
{
  enum tile_special_type prereq = S_NO_SPECIAL;

  if (contains_special(spe, S_RAILROAD)) {
    prereq |= S_ROAD;
  }
  if (contains_special(spe, S_FARMLAND)) {
    prereq |= S_IRRIGATION;
  }

  return prereq;
}

/***************************************************************
...
***************************************************************/
enum tile_special_type get_preferred_pillage(enum tile_special_type pset)
{
  if (contains_special(pset, S_FARMLAND))
    return S_FARMLAND;
  if (contains_special(pset, S_IRRIGATION))
    return S_IRRIGATION;
  if (contains_special(pset, S_MINE))
    return S_MINE;
  if (contains_special(pset, S_FORTRESS))
    return S_FORTRESS;
  if (contains_special(pset, S_AIRBASE))
    return S_AIRBASE;
  if (contains_special(pset, S_RAILROAD))
    return S_RAILROAD;
  if (contains_special(pset, S_ROAD))
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
  if (is_ocean(ptile->terrain)
      || tile_has_special(ptile, S_RIVER)
      || tile_has_special(ptile, S_IRRIGATION))
    return TRUE;

  cardinal_adjc_iterate(x, y, x1, y1) {
    ptile = map_get_tile(x1, y1);
    if (is_ocean(ptile->terrain)
	|| tile_has_special(ptile, S_RIVER)
	|| tile_has_special(ptile, S_IRRIGATION))
      return TRUE;
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
int map_build_road_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].road_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_build_irrigation_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].irrigation_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_build_mine_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].mining_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_transform_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].transform_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_build_rail_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].rail_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_build_airbase_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].airbase_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_build_fortress_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].fortress_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_clean_pollution_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].clean_pollution_time * 10;
}

/***************************************************************
...
***************************************************************/
int map_clean_fallout_time(int x, int y)
{
  return tile_types[map_get_terrain(x, y)].clean_fallout_time * 10;
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
    if (is_ocean(result)) {
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
    if (is_ocean(result)) {
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
  if (is_ocean(type)) {
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
    if (!is_ocean(map_get_tile(x1, y1)->terrain))
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
    if (is_ocean(map_get_tile(x1, y1)->terrain))
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

  if (game.slow_invasions
      && punit 
      && is_ground_unit(punit) 
      && is_ocean(t1->terrain)
      && !is_ocean(t2->terrain)) {
    /* Ground units moving from sea to land lose all their movement
     * if "slowinvasions" server option is turned on. */
    return punit->moves_left;
  }
  if (punit && !is_ground_unit(punit))
    return SINGLE_MOVE;
  if (tile_has_special(t1, S_RAILROAD) && tile_has_special(t2, S_RAILROAD))
    return MOVE_COST_RAIL;
/* return (unit_move_rate(punit)/RAIL_MAX) */
  if (punit && unit_flag(punit, F_IGTER))
    return SINGLE_MOVE/3;
  if (tile_has_special(t1, S_ROAD) && tile_has_special(t2, S_ROAD))
    return MOVE_COST_ROAD;

  if (tile_has_special(t1, S_RIVER) && tile_has_special(t2, S_RIVER)) {
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
  CHECK_MAP_POS(x, y);
  assert(!is_server
	 || (tile0->terrain != T_UNKNOWN && tile1->terrain != T_UNKNOWN));

  if (is_ocean(tile0->terrain) && is_ocean(tile1->terrain)) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (is_ocean(tile0->terrain)
      && (tile1->city || tile1->terrain == T_UNKNOWN)) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (is_ocean(tile1->terrain)
      && (tile0->city || tile0->terrain == T_UNKNOWN)) {
    return MOVE_COST_FOR_VALID_SEA_STEP;
  }

  if (is_ocean(tile0->terrain) || is_ocean(tile1->terrain)) {
    return maxcost;
  }

  return tile_move_cost_ptrs(NULL, tile0, tile1, x, y, x1, y1);
}

/***************************************************************
 ...
***************************************************************/
static void debug_log_move_costs(const char *str, int x, int y, struct tile *tile0)
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
struct tile *map_get_tile(int x, int y)
{
  return MAP_TILE(x, y);
}

/***************************************************************
...
***************************************************************/
unsigned short map_get_continent(int x, int y)
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

/****************************************************************************
  Return the terrain type of the given tile (in native coordinates).
****************************************************************************/
enum tile_terrain_type nat_get_terrain(int nat_x, int nat_y)
{
  return NAT_TILE(nat_x, nat_y)->terrain;
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
  return contains_special(MAP_TILE(x, y)->special, special);
}

/***************************************************************
 Returns TRUE iff the given tile has the given special.
***************************************************************/
bool tile_has_special(struct tile *ptile, enum tile_special_type special)
{
  return contains_special(ptile->special, special);
}
  
/***************************************************************
 Returns TRUE iff the given special is found in the given set.
***************************************************************/
bool contains_special(enum tile_special_type set,
		      enum tile_special_type to_test_for)
{
  enum tile_special_type masked = set & to_test_for;

  assert(0 == (int) S_NO_SPECIAL);

  /*
   * contains_special should only be called with one S_* in
   * to_test_for.
   */
  assert(masked == S_NO_SPECIAL || masked == to_test_for);

  return masked == to_test_for;
}

/***************************************************************
...
***************************************************************/
void map_set_terrain(int x, int y, enum tile_terrain_type ter)
{
  MAP_TILE(x, y)->terrain = ter;
}

/****************************************************************************
  Set the terrain of the given tile (in native coordinates).
****************************************************************************/
void nat_set_terrain(int nat_x, int nat_y, enum tile_terrain_type ter)
{
  NAT_TILE(nat_x, nat_y)->terrain = ter;
}

/***************************************************************
...
***************************************************************/
void map_set_special(int x, int y, enum tile_special_type spe)
{
  MAP_TILE(x, y)->special |= spe;

  if (contains_special(spe, S_ROAD) || contains_special(spe, S_RAILROAD)) {
    reset_move_costs(x, y);
  }
}

/***************************************************************
...
***************************************************************/
void map_clear_special(int x, int y, enum tile_special_type spe)
{
  MAP_TILE(x, y)->special &= ~spe;

  if (contains_special(spe, S_ROAD) || contains_special(spe, S_RAILROAD)) {
    reset_move_costs(x, y);
  }
}

/***************************************************************
  Remove any specials which may exist at these map co-ordinates.
***************************************************************/
void map_clear_all_specials(int x, int y)
{
  MAP_TILE(x, y)->special = S_NO_SPECIAL;
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

bool is_real_map_pos(int x, int y)
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
  If the position is real, it will be normalized and TRUE will be returned.
  If the position is unreal, it will be left unchanged and FALSE will be
  returned.

  Note, we need to leave x and y with sane values even in the unreal case.
  Some callers may for instance call nearest_real_pos on these values.
**************************************************************************/
bool normalize_map_pos(int *x, int *y)
{
  int nat_x, nat_y;

  /* Normalization is best done in native coordinatees. */
  map_to_native_pos(&nat_x, &nat_y, *x, *y);

  /* If the position is out of range in a non-wrapping direction, it is
   * unreal. */
  if (!((topo_has_flag(TF_WRAPX) || (nat_x >= 0 && nat_x < map.xsize))
	&& (topo_has_flag(TF_WRAPY) || (nat_y >= 0 && nat_y < map.ysize)))) {
    return FALSE;
  }

  /* Wrap in X and Y directions, as needed. */
  if (topo_has_flag(TF_WRAPX)) {
    nat_x = FC_WRAP(nat_x, map.xsize);
  }
  if (topo_has_flag(TF_WRAPY)) {
    nat_y = FC_WRAP(nat_y, map.ysize);
  }

  /* Now transform things back to map coordinates. */
  native_to_map_pos(x, y, nat_x, nat_y);
  return TRUE;
}

/**************************************************************************
Twiddle *x and *y to point the the nearest real tile, and ensure that the
position is normalized.
**************************************************************************/
void nearest_real_pos(int *x, int *y)
{
  int nat_x, nat_y;

  map_to_native_pos(&nat_x, &nat_y, *x, *y);
  if (!topo_has_flag(TF_WRAPX)) {
    nat_x = CLIP(0, nat_x, map.xsize - 1);
  }
  if (!topo_has_flag(TF_WRAPY)) {
    nat_y = CLIP(0, nat_y, map.ysize - 1);
  }
  native_to_map_pos(x, y, nat_x, nat_y);

  if (!normalize_map_pos(x, y)) {
    assert(FALSE);
  }
}

/**************************************************************************
Returns the total number of (real) positions (or tiles) on the map.
**************************************************************************/
int map_num_tiles(void)
{
  return map.xsize * map.ysize;
}

/****************************************************************************
  Topology function to find the vector which has the minimum "real"
  distance between the map positions (x0, y0) and (x1, y1).  If there is
  more than one vector with equal distance, no guarantee is made about
  which is found.

  Real distance is defined as the larger of the distances in the x and y
  direction; since units can travel diagonally this is the "real" distance
  a unit has to travel to get from point to point.

  (See also: real_map_distance, map_distance, and sq_map_distance.)

  With the standard topology the ranges of the return value are:
    -map.xsize/2 <= dx <= map.xsize/2
    -map.ysize   <  dy <  map.ysize
****************************************************************************/
void map_distance_vector(int *dx, int *dy, int x0, int y0, int x1, int y1)
{
  if (topo_has_flag(TF_WRAPX) || topo_has_flag(TF_WRAPY)) {
    /* Wrapping is done in native coordinates. */
    map_to_native_pos(&x0, &y0, x0, y0);
    map_to_native_pos(&x1, &y1, x1, y1);

    /* Find the "native" distance vector. This corresponds closely to the
     * map distance vector but is easier to wrap. */
    *dx = x1 - x0;
    *dy = y1 - y0;
    if (topo_has_flag(TF_WRAPX)) {
      /* Wrap dx to be in [-map.xsize/2, map.xsize/2). */
      *dx = FC_WRAP(*dx + map.xsize / 2, map.xsize) - map.xsize / 2;
    }
    if (topo_has_flag(TF_WRAPY)) {
      /* Wrap dy to be in [-map.ysize/2, map.ysize/2). */
      *dy = FC_WRAP(*dy + map.ysize / 2, map.ysize) - map.ysize / 2;
    }

    /* Convert the native delta vector back to a pair of map positions. */
    x1 = x0 + *dx;
    y1 = y0 + *dy;
    native_to_map_pos(&x0, &y0, x0, y0);
    native_to_map_pos(&x1, &y1, x1, y1);
  }

  /* Find the final (map) vector. */
  *dx = x1 - x0;
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

  CHECK_MAP_POS(x0, y0);

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
  int nat_x = myrand(map.xsize), nat_y = myrand(map.ysize);

  /* Don't pass non-deterministic expressions to native_to_map_pos! */
  native_to_map_pos(x, y, nat_x, nat_y);
  CHECK_MAP_POS(*x, *y);
}

/**************************************************************************
  Give a random tile anywhere on the map for which the 'filter' function
  returns TRUE.  Return FALSE if none can be found.  The filter may be
  NULL if any position is okay; if non-NULL it shouldn't have any side
  effects.
**************************************************************************/
bool rand_map_pos_filtered(int *x, int *y, void *data,
			   bool (*filter)(int x, int y, void *data))
{
  int tries = 0;
  const int max_tries = map.xsize * map.ysize / 10;

  /* First do a few quick checks to find a spot.  The limit on number of
   * tries could use some tweaking. */
  do {
    index_to_map_pos(x, y, myrand(map.xsize * map.ysize));
  } while (filter && !filter(*x, *y, data) && ++tries < max_tries);

  /* If that fails, count all available spots and pick one.
   * Slow but reliable. */
  if (tries == max_tries) {
    int count = 0, positions[map.xsize * map.ysize];

    whole_map_iterate(x1, y1) {
      if (filter(x1, y1, data)) {
	positions[count] = map_pos_to_index(x1, y1);
	count++;
      }
    } whole_map_iterate_end;

    if (count == 0) {
      return FALSE;
    }

    count = myrand(count);
    index_to_map_pos(x, y, positions[count]);
  }

  return TRUE;
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
    return "[Undef]";
  }
}

/**************************************************************************
  Returns the next direction clock-wise.
**************************************************************************/
enum direction8 dir_cw(enum direction8 dir)
{
  /* a switch statement is used so the ordering can be changed easily */
  switch (dir) {
  case DIR8_NORTH:
    return DIR8_NORTHEAST;
  case DIR8_NORTHEAST:
    return DIR8_EAST;
  case DIR8_EAST:
    return DIR8_SOUTHEAST;
  case DIR8_SOUTHEAST:
    return DIR8_SOUTH;
  case DIR8_SOUTH:
    return DIR8_SOUTHWEST;
  case DIR8_SOUTHWEST:
    return DIR8_WEST;
  case DIR8_WEST:
    return DIR8_NORTHWEST;
  case DIR8_NORTHWEST:
    return DIR8_NORTH;
  default:
    assert(0);
    return -1;
  }
}

/**************************************************************************
  Returns the next direction counter-clock-wise.
**************************************************************************/
enum direction8 dir_ccw(enum direction8 dir)
{
  /* a switch statement is used so the ordering can be changed easily */
  switch (dir) {
  case DIR8_NORTH:
    return DIR8_NORTHWEST;
  case DIR8_NORTHEAST:
    return DIR8_NORTH;
  case DIR8_EAST:
    return DIR8_NORTHEAST;
  case DIR8_SOUTHEAST:
    return DIR8_EAST;
  case DIR8_SOUTH:
    return DIR8_SOUTHEAST;
  case DIR8_SOUTHWEST:
    return DIR8_SOUTH;
  case DIR8_WEST:
    return DIR8_SOUTHWEST;
  case DIR8_NORTHWEST:
    return DIR8_WEST;
  default:
    assert(0);
    return -1;
  }
}

/**************************************************************************
  Returns TRUE iff the given direction is a valid one.
**************************************************************************/
bool is_valid_dir(enum direction8 dir)
{
  switch (dir) {
  case DIR8_NORTH:
  case DIR8_NORTHEAST:
  case DIR8_EAST:
  case DIR8_SOUTHEAST:
  case DIR8_SOUTH:
  case DIR8_SOUTHWEST:
  case DIR8_WEST:
  case DIR8_NORTHWEST:
    return TRUE;
  default:
    return FALSE;
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
    if (same_pos(x1, y1, end_x, end_y)) {
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

/****************************************************************************
  A "SINGULAR" position is any map position that has an abnormal number of
  tiles in the radius of dist.

  (map_x, map_y) must be normalized.

  dist is the "real" map distance.
****************************************************************************/
bool is_singular_map_pos(int map_x, int map_y, int dist)
{
  CHECK_MAP_POS(map_x, map_y);
  do_in_natural_pos(ntl_x, ntl_y, map_x, map_y) {
    /* Iso-natural coordinates are doubled in scale. */
    dist *= topo_has_flag(TF_ISO) ? 2 : 1;

    return ((!topo_has_flag(TF_WRAPX) 
	     && (ntl_x < dist || ntl_x >= NATURAL_WIDTH - dist))
	    || (!topo_has_flag(TF_WRAPY)
		&& (ntl_y < dist || ntl_y >= NATURAL_HEIGHT - dist)));
  } do_in_natural_pos_end;
}
