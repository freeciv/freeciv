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

/****************************************************************************
  Return a bitfield of the specials on the tile that are infrastructure.
****************************************************************************/
enum tile_special_type get_tile_infrastructure_set(const struct tile *ptile)
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
const char *map_get_tile_info_text(const struct tile *ptile)
{
  static char s[64];
  bool first;
  struct tile_type *ptype = get_tile_type(ptile->terrain);

  sz_strlcpy(s, ptype->terrain_name);
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
    sz_strlcat(s, ptype->special_1_name);
  }
  if (tile_has_special(ptile, S_SPECIAL_2)) {
    if (first) {
      first = FALSE;
      sz_strlcat(s, " (");
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, ptype->special_2_name);
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

  map.seed = MAP_DEFAULT_SEED;
  map.riches                = MAP_DEFAULT_RICHES;
  map.huts                  = MAP_DEFAULT_HUTS;
  map.landpercent           = MAP_DEFAULT_LANDMASS;
  map.wetness               = MAP_DEFAULT_WETNESS;
  map.steepness             = MAP_DEFAULT_STEEPNESS;
  map.generator             = MAP_DEFAULT_GENERATOR;
  map.startpos              = MAP_DEFAULT_STARTPOS;
  map.tinyisles             = MAP_DEFAULT_TINYISLES;
  map.separatepoles         = MAP_DEFAULT_SEPARATE_POLES;
  map.alltemperate          = MAP_DEFAULT_ALLTEMPERATE;
  map.temperature           = MAP_DEFAULT_TEMPERATURE;
  map.tiles                 = NULL;
  map.num_continents        = 0;
  map.num_oceans            = 0;
  map.num_start_positions   = 0;
  map.have_specials         = FALSE;
  map.have_rivers_overlay   = FALSE;
  map.have_huts             = FALSE;
}

/**************************************************************************
  Fill the iterate_outwards_indices array.  This may depend on the topology.
***************************************************************************/
static void generate_map_indices(void)
{
  int i = 0, nat_x, nat_y, tiles;
  struct iter_index *array = map.iterate_outwards_indices;
  int nat_center_x, nat_center_y, nat_min_x, nat_min_y, nat_max_x, nat_max_y;
  int map_center_x, map_center_y;

  /* These caluclations are done via tricky native math.  We need to make
   * sure that when "exploring" positions in the iterate_outward we hit each
   * position within the distance exactly once.
   *
   * To do this we pick a center position (at the center of the map, for
   * convenience).  Then we iterate over all of the positions around it,
   * accounting for wrapping, in native coordinates.  Note that some of the
   * positions iterated over before will not even be real; the point is to
   * use the native math so as to get the right behavior under different
   * wrapping conditions.
   *
   * Thus the "center" position below is just an arbitrary point.  We choose
   * the center of the map to make the min/max values (below) simpler. */
  nat_center_x = map.xsize / 2;
  nat_center_y = map.ysize / 2;
  NATIVE_TO_MAP_POS(&map_center_x, &map_center_y,
		    nat_center_x, nat_center_y);

  /* If we wrap in a particular direction (X or Y) we only need to explore a
   * half of a map-width in that direction before we hit the wrap point.  If
   * not we need to explore the full width since we have to account for the
   * worst-case where we start at one edge of the map.  Of course if we try
   * to explore too far the extra steps will just be skipped by the
   * normalize check later on.  So the purpose at this point is just to
   * get the right set of positions, relative to the start position, that
   * may be needed for the iteration.
   *
   * If the map does wrap, we go map.Nsize / 2 in each direction.  This
   * gives a min value of 0 and a max value of Nsize-1, because of the
   * center position chosen above.  This also avoids any off-by-one errors.
   *
   * If the map doesn't wrap, we go map.Nsize-1 in each direction.  In this
   * case we're not concerned with going too far and wrapping around, so we
   * just have to make sure we go far enough if we're at one edge of the
   * map. */
  nat_min_x = (topo_has_flag(TF_WRAPX) ? 0 : (nat_center_x - map.xsize + 1));
  nat_min_y = (topo_has_flag(TF_WRAPY) ? 0 : (nat_center_y - map.ysize + 1));

  nat_max_x = (topo_has_flag(TF_WRAPX)
	       ? (map.xsize - 1)
	       : (nat_center_x + map.xsize - 1));
  nat_max_y = (topo_has_flag(TF_WRAPY)
	       ? (map.ysize - 1)
	       : (nat_center_y + map.ysize - 1));
  tiles = (nat_max_x - nat_min_x + 1) * (nat_max_y - nat_min_y + 1);

  array = fc_realloc(array, tiles * sizeof(*array));

  for (nat_x = nat_min_x; nat_x <= nat_max_x; nat_x++) {
    for (nat_y = nat_min_y; nat_y <= nat_max_y; nat_y++) {
      int map_x, map_y, dx, dy;

      /* Now for each position, we find the vector (in map coordinates) from
       * the center position to that position.  Then we calculate the
       * distance between the two points.  Wrapping is ignored at this
       * point since the use of native positions means we should always have
       * the shortest vector. */
      NATIVE_TO_MAP_POS(&map_x, &map_y, nat_x, nat_y);
      dx = map_x - map_center_x;
      dy = map_y - map_center_y;

      array[i].dx = dx;
      array[i].dy = dy;
      array[i].dist = map_vector_to_real_distance(dx, dy);
      i++;
    }
  }
  assert(i == tiles);

  qsort(array, tiles, sizeof(*array), compare_iter_index);

#if 0
  for (i = 0; i < tiles; i++) {
    freelog(LOG_DEBUG, "%5d : (%3d,%3d) : %d",
	    i, array[i].dx, array[i].dy, array[i].dist);
  }
#endif

  map.num_iterate_outwards_indices = tiles;
  map.iterate_outwards_indices = array;
}

/****************************************************************************
  map_init_topology needs to be called after map.topology_id is changed.

  If map.size is changed, map.xsize and map.ysize must be set before
  calling map_init_topology(TRUE).  This is done by the mapgen code
  (server) and packhand code (client).

  If map.xsize and map.ysize are changed, call map_init_topology(FALSE) to
  calculate map.size.  This should be done in the client or when loading
  savegames, since the [xy]size values are already known.
****************************************************************************/
void map_init_topology(bool set_sizes)
{
  enum direction8 dir;

  if (!set_sizes) {
    /* Set map.size based on map.xsize and map.ysize. */
    map.size = (float)(map.xsize * map.ysize) / 1000.0 + 0.5;
  }
  
  /* sanity check for iso topologies*/
  assert(!MAP_IS_ISOMETRIC || (map.ysize % 2) == 0);

  /* The size and ratio must satisfy the minimum and maximum *linear*
   * restrictions on width */
  assert(MAP_WIDTH >= MAP_MIN_LINEAR_SIZE);
  assert(MAP_HEIGHT >= MAP_MIN_LINEAR_SIZE);
  assert(MAP_WIDTH <= MAP_MAX_LINEAR_SIZE);
  assert(MAP_HEIGHT <= MAP_MAX_LINEAR_SIZE);

  map.num_valid_dirs = map.num_cardinal_dirs = 0;
  for (dir = 0; dir < 8; dir++) {
    if (is_valid_dir(dir)) {
      map.valid_dirs[map.num_valid_dirs] = dir;
      map.num_valid_dirs++;
    }
    if (is_cardinal_dir(dir)) {
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
  ptile->client.hilite = HILITE_NONE; /* Area Selection in client. */
  ptile->spec_sprite = NULL;
}

/****************************************************************************
  Step from the given tile in the given direction.  The new tile is returned,
  or NULL if the direction is invalid or leads off the map.
****************************************************************************/
struct tile *mapstep(const struct tile *ptile, enum direction8 dir)
{
  int x, y;

  if (!is_valid_dir(dir)) {
    return NULL;
  }

  DIRSTEP(x, y, dir);
  x += ptile->x;
  y += ptile->y;

  return map_pos_to_tile(x, y);
}

/****************************************************************************
  Return the tile for the given native position, with wrapping.

  This is a backend function used by map_pos_to_tile and native_pos_to_tile.
  It is called extremely often so it is made inline.
****************************************************************************/
static inline struct tile *base_native_pos_to_tile(int nat_x, int nat_y)
{
  /* If the position is out of range in a non-wrapping direction, it is
   * unreal. */
  if (!((topo_has_flag(TF_WRAPX) || (nat_x >= 0 && nat_x < map.xsize))
	&& (topo_has_flag(TF_WRAPY) || (nat_y >= 0 && nat_y < map.ysize)))) {
    return NULL;
  }

  /* Wrap in X and Y directions, as needed. */
  if (topo_has_flag(TF_WRAPX)) {
    nat_x = FC_WRAP(nat_x, map.xsize);
  }
  if (topo_has_flag(TF_WRAPY)) {
    nat_y = FC_WRAP(nat_y, map.ysize);
  }

  return map.tiles + native_pos_to_index(nat_x, nat_y);
}

/****************************************************************************
  Return the tile for the given cartesian (map) position.
****************************************************************************/
struct tile *map_pos_to_tile(int map_x, int map_y)
{
  int nat_x, nat_y;

  if (!map.tiles) {
    return NULL;
  }

  /* Normalization is best done in native coordinates. */
  MAP_TO_NATIVE_POS(&nat_x, &nat_y, map_x, map_y);
  return base_native_pos_to_tile(nat_x, nat_y);
}

/****************************************************************************
  Return the tile for the given native position.
****************************************************************************/
struct tile *native_pos_to_tile(int nat_x, int nat_y)
{
  if (!map.tiles) {
    return NULL;
  }

  return base_native_pos_to_tile(nat_x, nat_y);
}

/****************************************************************************
  Return the tile for the given index position.
****************************************************************************/
struct tile *index_to_tile(int index)
{
  if (!map.tiles) {
    return NULL;
  }

  if (index >= 0 && index < MAX_MAP_INDEX) {
    return map.tiles + index;
  } else {
    /* Unwrapped index coordinates are impossible, so the best we can do is
     * return NULL. */
    return NULL;
  }
}

/**************************************************************************
  Return the player who owns this tile (or NULL if none).
**************************************************************************/
struct player *map_get_owner(const struct tile *ptile)
{
  return ptile->owner;
}

/**************************************************************************
  Set the owner of a tile (may be NULL).
**************************************************************************/
void map_set_owner(struct tile *ptile, struct player *owner)
{
  ptile->owner = owner;
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
  whole_map_iterate(ptile) {
    int index, nat_x, nat_y, map_x, map_y;

    index = ptile - map.tiles;
    index_to_native_pos(&nat_x, &nat_y, index);
    index_to_map_pos(&map_x, &map_y, index);
    CHECK_INDEX(index);
    CHECK_MAP_POS(map_x, map_y);
    CHECK_NATIVE_POS(nat_x, nat_y);

    /* HACK: these fields are declared const to keep anyone from changing
     * them.  But we have to set them somewhere!  This should be the only
     * place. */
    *(int *)&ptile->index = index;
    *(int *)&ptile->x = map_x;
    *(int *)&ptile->y = map_y;
    *(int *)&ptile->nat_x = nat_x;
    *(int *)&ptile->nat_y = nat_y;

    tile_init(ptile);
  } whole_map_iterate_end;

  generate_city_map_indices();
  generate_map_indices();
}

/***************************************************************
  Frees the allocated memory of the map.
***************************************************************/
void map_free(void)
{
  if (map.tiles) {
    /* it is possible that map_init was called but not map_allocate */

    whole_map_iterate(ptile) {
      tile_free(ptile);
    } whole_map_iterate_end;

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
  if (topo_has_flag(TF_HEX)) {
    /* Hex: all directions are cardinal so the distance is equivalent to
     * the real distance. */
    return map_vector_to_real_distance(dx, dy);
  } else {
    return abs(dx) + abs(dy);
  }
}

/****************************************************************************
  Return the "real" distance for a given vector.
****************************************************************************/
int map_vector_to_real_distance(int dx, int dy)
{
  if (topo_has_flag(TF_HEX)) {
    if (topo_has_flag(TF_ISO)) {
      /* Iso-hex: you can't move NE or SW. */
      if ((dx < 0 && dy > 0)
	  || (dx > 0 && dy < 0)) {
	/* Diagonal moves in this direction aren't allowed, so it will take
	 * the full number of moves. */
	return abs(dx) + abs(dy);
      } else {
	/* Diagonal moves in this direction *are* allowed. */
	return MAX(abs(dx), abs(dy));
      }
    } else {
      /* Hex: you can't move SE or NW. */
      if ((dx > 0 && dy > 0)
	  || (dx < 0 && dy < 0)) {
	/* Diagonal moves in this direction aren't allowed, so it will take
	 * the full number of moves. */
	return abs(dx) + abs(dy);
      } else {
	/* Diagonal moves in this direction *are* allowed. */
	return MAX(abs(dx), abs(dy));
      }
    }
  } else {
    return MAX(abs(dx), abs(dy));
  }
}

/****************************************************************************
  Return the sq_distance for a given vector.
****************************************************************************/
int map_vector_to_sq_distance(int dx, int dy)
{
  if (topo_has_flag(TF_HEX)) {
    /* Hex: The square distance is just the square of the real distance; we
     * don't worry about pythagorean calculations. */
    int dist = map_vector_to_real_distance(dx, dy);

    return dist * dist;
  } else {
    return dx * dx + dy * dy;
  }
}

/***************************************************************
...
***************************************************************/
int real_map_distance(const struct tile *tile0, const struct tile *tile1)
{
  int dx, dy;

  map_distance_vector(&dx, &dy, tile0, tile1);
  return map_vector_to_real_distance(dx, dy);
}

/***************************************************************
...
***************************************************************/
int sq_map_distance(const struct tile *tile0, const struct tile *tile1)
{
  /* We assume map_distance_vector gives us the vector with the
     minimum squared distance. Right now this is true. */
  int dx, dy;

  map_distance_vector(&dx, &dy, tile0, tile1);
  return map_vector_to_sq_distance(dx, dy);
}

/***************************************************************
...
***************************************************************/
int map_distance(const struct tile *tile0, const struct tile *tile1)
{
  /* We assume map_distance_vector gives us the vector with the
     minimum map distance. Right now this is true. */
  int dx, dy;

  map_distance_vector(&dx, &dy, tile0, tile1);
  return map_vector_to_distance(dx, dy);
}

/*************************************************************************
  This is used in mapgen for rivers going into ocen.  The name is 
  intentionally made awkward to prevent people from using it in place of
  is_ocean_near_tile
*************************************************************************/
bool is_cardinally_adj_to_ocean(const struct tile *ptile)
{
  cardinal_adjc_iterate(ptile, tile1) {
    if (is_ocean(map_get_terrain(tile1))) {
      return TRUE;
    }
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return TRUE if this ocean terrain is adjacent to a safe coastline.
****************************************************************************/
bool is_safe_ocean(const struct tile *ptile)
{
  adjc_iterate(ptile, tile1) {
    Terrain_type_id ter = map_get_terrain(tile1);
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
bool is_sea_usable(const struct tile *ptile)
{
  map_city_radius_iterate(ptile, tile1) {
    if (!is_ocean(map_get_terrain(tile1))) {
      return TRUE;
    }
  } map_city_radius_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
int get_tile_food_base(const struct tile *ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1)) 
    return get_tile_type(ptile->terrain)->food_special_1;
  else if (tile_has_special(ptile, S_SPECIAL_2))
    return get_tile_type(ptile->terrain)->food_special_2;
  else
    return get_tile_type(ptile->terrain)->food;
}

/***************************************************************
...
***************************************************************/
int get_tile_shield_base(const struct tile *ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1))
    return get_tile_type(ptile->terrain)->shield_special_1;
  else if(tile_has_special(ptile, S_SPECIAL_2))
    return get_tile_type(ptile->terrain)->shield_special_2;
  else
    return get_tile_type(ptile->terrain)->shield;
}

/***************************************************************
...
***************************************************************/
int get_tile_trade_base(const struct tile *ptile)
{
  if (tile_has_special(ptile, S_SPECIAL_1))
    return get_tile_type(ptile->terrain)->trade_special_1;
  else if (tile_has_special(ptile, S_SPECIAL_2))
    return get_tile_type(ptile->terrain)->trade_special_2;
  else
    return get_tile_type(ptile->terrain)->trade;
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
bool is_water_adjacent_to_tile(const struct tile *ptile)
{
  if (is_ocean(ptile->terrain)
      || tile_has_special(ptile, S_RIVER)
      || tile_has_special(ptile, S_IRRIGATION))
    return TRUE;

  cardinal_adjc_iterate(ptile, tile1) {
    if (is_ocean(tile1->terrain)
	|| tile_has_special(tile1, S_RIVER)
	|| tile_has_special(tile1, S_IRRIGATION))
      return TRUE;
  } cardinal_adjc_iterate_end;

  return FALSE;
}

/***************************************************************
...
***************************************************************/
int map_build_road_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->road_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_build_irrigation_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->irrigation_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_build_mine_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->mining_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_transform_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->transform_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_build_rail_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->rail_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_build_airbase_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->airbase_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_build_fortress_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->fortress_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_clean_pollution_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->clean_pollution_time * ACTIVITY_FACTOR;
}

/***************************************************************
...
***************************************************************/
int map_clean_fallout_time(const struct tile *ptile)
{
  return get_tile_type(ptile->terrain)->clean_fallout_time * ACTIVITY_FACTOR;
}

/***************************************************************
  Time to complete given activity on given tile.
***************************************************************/
int map_activity_time(enum unit_activity activity, const struct tile *ptile)
{
  switch (activity) {
  case ACTIVITY_POLLUTION:
    return map_clean_pollution_time(ptile);
  case ACTIVITY_ROAD:
    return map_build_road_time(ptile);
  case ACTIVITY_MINE:
    return map_build_mine_time(ptile);
  case ACTIVITY_IRRIGATE:
    return map_build_irrigation_time(ptile);
  case ACTIVITY_FORTRESS:
    return map_build_fortress_time(ptile);
  case ACTIVITY_RAILROAD:
    return map_build_rail_time(ptile);
  case ACTIVITY_TRANSFORM:
    return map_transform_time(ptile);
  case ACTIVITY_AIRBASE:
    return map_build_airbase_time(ptile);
  case ACTIVITY_FALLOUT:
    return map_clean_fallout_time(ptile);
  default:
    return 0;
  }
}

/***************************************************************
...
***************************************************************/
static void clear_infrastructure(struct tile *ptile)
{
  map_clear_special(ptile, S_INFRASTRUCTURE_MASK);
}

/***************************************************************
...
***************************************************************/
static void clear_dirtiness(struct tile *ptile)
{
  map_clear_special(ptile, S_POLLUTION | S_FALLOUT);
}

/***************************************************************
...
***************************************************************/
void map_irrigate_tile(struct tile *ptile)
{
  Terrain_type_id now, result;
  
  now = ptile->terrain;
  result = get_tile_type(now)->irrigation_result;

  if (now == result) {
    if (map_has_special(ptile, S_IRRIGATION)) {
      map_set_special(ptile, S_FARMLAND);
    } else {
      map_set_special(ptile, S_IRRIGATION);
    }
  } else if (result != T_NONE) {
    map_set_terrain(ptile, result);
    if (is_ocean(result)) {
      clear_infrastructure(ptile);
      clear_dirtiness(ptile);

      /* FIXME: When rest of code can handle
       * rivers in oceans, don't clear this! */
      map_clear_special(ptile, S_RIVER);
    }
    reset_move_costs(ptile);
  }
  map_clear_special(ptile, S_MINE);
}

/***************************************************************
...
***************************************************************/
void map_mine_tile(struct tile *ptile)
{
  Terrain_type_id now, result;
  
  now = ptile->terrain;
  result = get_tile_type(now)->mining_result;
  
  if (now == result) {
    map_set_special(ptile, S_MINE);
  } else if (result != T_NONE) {
    map_set_terrain(ptile, result);
    if (is_ocean(result)) {
      clear_infrastructure(ptile);
      clear_dirtiness(ptile);

      /* FIXME: When rest of code can handle
       * rivers in oceans, don't clear this! */
      map_clear_special(ptile, S_RIVER);
    }
    reset_move_costs(ptile);
  }
  map_clear_special(ptile, S_FARMLAND);
  map_clear_special(ptile, S_IRRIGATION);
}

/***************************************************************
...
***************************************************************/
void change_terrain(struct tile *ptile, Terrain_type_id type)
{
  map_set_terrain(ptile, type);
  if (is_ocean(type)) {
    clear_infrastructure(ptile);
    clear_dirtiness(ptile);
    map_clear_special(ptile, S_RIVER);	/* FIXME: When rest of code can handle
					   rivers in oceans, don't clear this! */
  }

  reset_move_costs(ptile);

  /* Clear mining/irrigation if resulting terrain type cannot support
     that feature.  (With current rules, this should only clear mines,
     but I'm including both cases in the most general form for possible
     future ruleset expansion. -GJW) */
  
  if (get_tile_type(type)->mining_result != type)
    map_clear_special(ptile, S_MINE);

  if (get_tile_type(type)->irrigation_result != type)
    map_clear_special(ptile, S_FARMLAND | S_IRRIGATION);
}

/***************************************************************
...
***************************************************************/
void map_transform_tile(struct tile *ptile)
{
  Terrain_type_id now, result;
  
  now = ptile->terrain;
  result = get_tile_type(now)->transform_result;
  
  if (result != T_NONE) {
    change_terrain(ptile, result);
  }
}

/**************************************************************************
This function returns true if the tile at the given location can be
"reclaimed" from ocean into land.  This is the case only when there are
a sufficient number of adjacent tiles that are not ocean.
**************************************************************************/
bool can_reclaim_ocean(const struct tile *ptile)
{
  int land_tiles = 100 - count_ocean_near_tile(ptile, FALSE, TRUE);

  return land_tiles >= terrain_control.ocean_reclaim_requirement_pct;
}

/**************************************************************************
This function returns true if the tile at the given location can be
"channeled" from land into ocean.  This is the case only when there are
a sufficient number of adjacent tiles that are ocean.
**************************************************************************/
bool can_channel_land(const struct tile *ptile)
{
  int ocean_tiles = count_ocean_near_tile(ptile, FALSE, TRUE);

  return ocean_tiles >= terrain_control.land_channel_requirement_pct;
}

/***************************************************************
  The basic cost to move punit from tile t1 to tile t2.
  That is, tile_move_cost(), with pre-calculated tile pointers;
  the tiles are assumed to be adjacent, and the (x,y)
  values are used only to get the river bonus correct.

  May also be used with punit==NULL, in which case punit
  tests are not done (for unit-independent results).
***************************************************************/
static int tile_move_cost_ptrs(struct unit *punit,
			       const struct tile *t1, const struct tile *t2)
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
    cardinal_move = is_move_cardinal(t1, t2);
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
			     int maxcost)
{
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

  return tile_move_cost_ptrs(NULL, tile0, tile1);
}

/***************************************************************
 ...
***************************************************************/
static void debug_log_move_costs(const char *str, struct tile *tile0)
{
  /* the %x don't work so well for oceans, where
     move_cost[]==-3 ,.. --dwp
  */
  freelog(LOG_DEBUG, "%s (%d, %d) [%x%x%x%x%x%x%x%x]", str,
	  tile0->x, tile0->y,
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
void reset_move_costs(struct tile *ptile)
{
  int maxcost = 72; /* should be big enough without being TOO big */

  debug_log_move_costs("Resetting move costs for", ptile);

  /* trying to move off the screen is the default */
  memset(ptile->move_cost, maxcost, sizeof(ptile->move_cost));

  adjc_dir_iterate(ptile, tile1, dir) {
    ptile->move_cost[dir] = tile_move_cost_ai(ptile, tile1, maxcost);
    /* reverse: not at all obfuscated now --dwp */
    tile1->move_cost[DIR_REVERSE(dir)] =
	tile_move_cost_ai(tile1, ptile, maxcost);
  } adjc_dir_iterate_end;
  debug_log_move_costs("Reset move costs for", ptile);
}

/***************************************************************
  Initialize tile->move_cost[] for all tiles, where move_cost[i]
  is the unit-independent cost to move _from_ that tile, to
  adjacent tile in direction specified by i.
***************************************************************/
void initialize_move_costs(void)
{
  int maxcost = 72; /* should be big enough without being TOO big */

  whole_map_iterate(ptile) {
    /* trying to move off the screen is the default */
    memset(ptile->move_cost, maxcost, sizeof(ptile->move_cost));

    adjc_dir_iterate(ptile, tile1, dir) {
      ptile->move_cost[dir] = tile_move_cost_ai(ptile, tile1, maxcost);
    }
    adjc_dir_iterate_end;
  } whole_map_iterate_end;
}

/***************************************************************
  The cost to move punit from where it is to tile x,y.
  It is assumed the move is a valid one, e.g. the tiles are adjacent.
***************************************************************/
int map_move_cost(struct unit *punit, const struct tile *ptile)
{
  return tile_move_cost_ptrs(punit, punit->tile, ptile);
}

/***************************************************************
...
***************************************************************/
bool is_tiles_adjacent(const struct tile *tile0, const struct tile *tile1)
{
  return real_map_distance(tile0, tile1) == 1;
}

/***************************************************************
...
***************************************************************/
Continent_id map_get_continent(const struct tile *ptile)
{
  return ptile->continent;
}

/***************************************************************
...
***************************************************************/
void map_set_continent(struct tile *ptile, Continent_id val)
{
  ptile->continent = val;
}

/***************************************************************
...
***************************************************************/
Terrain_type_id map_get_terrain(const struct tile *ptile)
{
  return ptile->terrain;
}

/***************************************************************
...
***************************************************************/
void map_set_terrain(struct tile *ptile, Terrain_type_id ter)
{
  ptile->terrain = ter;
}

/***************************************************************
...
***************************************************************/
enum tile_special_type map_get_special(const struct tile *ptile)
{
  return ptile->special;
}

/***************************************************************
 Returns TRUE iff the given special is found at the given map
 position.
***************************************************************/
bool map_has_special(const struct tile *ptile, enum tile_special_type special)
{
  return contains_special(ptile->special, special);
}

/***************************************************************
 Returns TRUE iff the given tile has the given special.
***************************************************************/
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type special)
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
void map_set_special(struct tile *ptile, enum tile_special_type spe)
{
  ptile->special |= spe;

  if (contains_special(spe, S_ROAD) || contains_special(spe, S_RAILROAD)) {
    reset_move_costs(ptile);
  }
}

/***************************************************************
...
***************************************************************/
void map_clear_special(struct tile *ptile, enum tile_special_type spe)
{
  ptile->special &= ~spe;

  if (contains_special(spe, S_ROAD) || contains_special(spe, S_RAILROAD)) {
    reset_move_costs(ptile);
  }
}

/***************************************************************
  Remove any specials which may exist at these map co-ordinates.
***************************************************************/
void map_clear_all_specials(struct tile *ptile)
{
  ptile->special = S_NO_SPECIAL;
}

/***************************************************************
...
***************************************************************/
struct city *map_get_city(const struct tile *ptile)
{
  return ptile->city;
}

/***************************************************************
...
***************************************************************/
void map_set_city(struct tile *ptile, struct city *pcity)
{
  ptile->city = pcity;
}

/***************************************************************
  Are (x1,y1) and (x2,y2) really the same when adjusted?
  This function might be necessary ALOT of places...
***************************************************************/
bool same_pos(const struct tile *tile1, const struct tile *tile2)
{
  assert(tile1 != NULL && tile2 != NULL);
  return (tile1 == tile2);
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
  int nat_x, nat_y;

  MAP_TO_NATIVE_POS(&nat_x, &nat_y, x, y);
  return nat_x >= 0 && nat_x < map.xsize && nat_y >= 0 && nat_y < map.ysize;
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
  struct tile *ptile = map_pos_to_tile(*x, *y);

  if (ptile) {
    *x = ptile->x;
    *y = ptile->y;
    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
Twiddle *x and *y to point the the nearest real tile, and ensure that the
position is normalized.
**************************************************************************/
struct tile *nearest_real_tile(int x, int y)
{
  int nat_x, nat_y;

  MAP_TO_NATIVE_POS(&nat_x, &nat_y, x, y);
  if (!topo_has_flag(TF_WRAPX)) {
    nat_x = CLIP(0, nat_x, map.xsize - 1);
  }
  if (!topo_has_flag(TF_WRAPY)) {
    nat_y = CLIP(0, nat_y, map.ysize - 1);
  }
  NATIVE_TO_MAP_POS(&x, &y, nat_x, nat_y);

  return map_pos_to_tile(x, y);
}

/**************************************************************************
Returns the total number of (real) positions (or tiles) on the map.
**************************************************************************/
int map_num_tiles(void)
{
  return map.xsize * map.ysize;
}

/****************************************************************************
  Finds the difference between the two (unnormalized) positions, in
  cartesian (map) coordinates.  Most callers should use map_distance_vector
  instead.
****************************************************************************/
void base_map_distance_vector(int *dx, int *dy,
			      int x0, int y0, int x1, int y1)
{
  if (topo_has_flag(TF_WRAPX) || topo_has_flag(TF_WRAPY)) {
    /* Wrapping is done in native coordinates. */
    MAP_TO_NATIVE_POS(&x0, &y0, x0, y0);
    MAP_TO_NATIVE_POS(&x1, &y1, x1, y1);

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
    NATIVE_TO_MAP_POS(&x0, &y0, x0, y0);
    NATIVE_TO_MAP_POS(&x1, &y1, x1, y1);
  }

  /* Find the final (map) vector. */
  *dx = x1 - x0;
  *dy = y1 - y0;
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
void map_distance_vector(int *dx, int *dy,
			 const struct tile *tile0,
			 const struct tile *tile1)
{
  base_map_distance_vector(dx, dy,
			   tile0->x, tile0->y, tile1->x, tile1->y);
}

/**************************************************************************
Random neighbouring square.
**************************************************************************/
struct tile *rand_neighbour(const struct tile *ptile)
{
  int n;
  struct tile *tile1;

  /* 
   * list of all 8 directions 
   */
  enum direction8 dirs[8] = {
    DIR8_NORTHWEST, DIR8_NORTH, DIR8_NORTHEAST, DIR8_WEST, DIR8_EAST,
    DIR8_SOUTHWEST, DIR8_SOUTH, DIR8_SOUTHEAST
  };

  /* This clever loop by Trent Piepho will take no more than
   * 8 tries to find a valid direction. */
  for (n = 8; n > 0; n--) {
    enum direction8 choice = (enum direction8) myrand(n);

    /* this neighbour's OK */
    tile1 = mapstep(ptile, dirs[choice]);
    if (tile1) {
      return tile1;
    }

    /* Choice was bad, so replace it with the last direction in the list.
     * On the next iteration, one fewer choices will remain. */
    dirs[choice] = dirs[n - 1];
  }

  assert(0);			/* Are we on a 1x1 map with no wrapping??? */
  return NULL;
}

/**************************************************************************
 Random square anywhere on the map.  Only normal positions (for which
 is_normal_map_pos returns true) will be found.
**************************************************************************/
struct tile *rand_map_pos(void)
{
  int nat_x = myrand(map.xsize), nat_y = myrand(map.ysize);

  return native_pos_to_tile(nat_x, nat_y);
}

/**************************************************************************
  Give a random tile anywhere on the map for which the 'filter' function
  returns TRUE.  Return FALSE if none can be found.  The filter may be
  NULL if any position is okay; if non-NULL it shouldn't have any side
  effects.
**************************************************************************/
struct tile *rand_map_pos_filtered(void *data,
				   bool (*filter)(const struct tile *ptile,
						  const void *data))
{
  struct tile *ptile;
  int tries = 0;
  const int max_tries = map.xsize * map.ysize / ACTIVITY_FACTOR;

  /* First do a few quick checks to find a spot.  The limit on number of
   * tries could use some tweaking. */
  do {
    ptile = map.tiles + myrand(map.xsize * map.ysize);
  } while (filter && !filter(ptile, data) && ++tries < max_tries);

  /* If that fails, count all available spots and pick one.
   * Slow but reliable. */
  if (tries == max_tries) {
    int count = 0, positions[map.xsize * map.ysize];

    whole_map_iterate(ptile) {
      if (filter(ptile, data)) {
	positions[count] = ptile->index;
	count++;
      }
    } whole_map_iterate_end;

    if (count == 0) {
      return NULL;
    }

    return map.tiles + positions[myrand(count)];
  } else {
    return ptile;
  }
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
  case DIR8_SOUTHEAST:
  case DIR8_NORTHWEST:
    /* These directions are invalid in hex topologies. */
    return !(topo_has_flag(TF_HEX) && !topo_has_flag(TF_ISO));
  case DIR8_NORTHEAST:
  case DIR8_SOUTHWEST:
    /* These directions are invalid in iso-hex topologies. */
    return !(topo_has_flag(TF_HEX) && topo_has_flag(TF_ISO));
  case DIR8_NORTH:
  case DIR8_EAST:
  case DIR8_SOUTH:
  case DIR8_WEST:
    return TRUE;
  default:
    return FALSE;
  }
}

/**************************************************************************
  Returns TRUE iff the given direction is a cardinal one.

  Cardinal directions are those in which adjacent tiles share an edge not
  just a vertex.
**************************************************************************/
bool is_cardinal_dir(enum direction8 dir)
{
  switch (dir) {
  case DIR8_NORTH:
  case DIR8_SOUTH:
  case DIR8_EAST:
  case DIR8_WEST:
    return TRUE;
  case DIR8_SOUTHEAST:
  case DIR8_NORTHWEST:
    /* These directions are cardinal in iso-hex topologies. */
    return topo_has_flag(TF_HEX) && topo_has_flag(TF_ISO);
  case DIR8_NORTHEAST:
  case DIR8_SOUTHWEST:
    /* These directions are cardinal in hexagonal topologies. */
    return topo_has_flag(TF_HEX) && !topo_has_flag(TF_ISO);
  }
  return FALSE;
}

/**************************************************************************
Return true and sets dir to the direction of the step if (end_x,
end_y) can be reached from (start_x, start_y) in one step. Return
false otherwise (value of dir is unchanged in this case).
**************************************************************************/
bool base_get_direction_for_step(const struct tile *start_tile,
				 const struct tile *end_tile,
				 enum direction8 *dir)
{
  adjc_dir_iterate(start_tile, test_tile, test_dir) {
    if (same_pos(end_tile, test_tile)) {
      *dir = test_dir;
      return TRUE;
    }
  } adjc_dir_iterate_end;

  return FALSE;
}

/**************************************************************************
Return the direction which is needed for a step on the map from
(start_x, start_y) to (end_x, end_y).
**************************************************************************/
int get_direction_for_step(const struct tile *start_tile,
			   const struct tile *end_tile)
{
  enum direction8 dir;

  if (base_get_direction_for_step(start_tile, end_tile, &dir)) {
    return dir;
  }

  assert(0);
  return -1;
}

/**************************************************************************
  Returns TRUE iff the move from the position (start_x,start_y) to
  (end_x,end_y) is a cardinal one.
**************************************************************************/
bool is_move_cardinal(const struct tile *start_tile,
		      const struct tile *end_tile)
{
  return is_cardinal_dir(get_direction_for_step(start_tile, end_tile));
}

/****************************************************************************
  A "SINGULAR" position is any map position that has an abnormal number of
  tiles in the radius of dist.

  (map_x, map_y) must be normalized.

  dist is the "real" map distance.
****************************************************************************/
bool is_singular_tile(const struct tile *ptile, int dist)
{
  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    /* Iso-natural coordinates are doubled in scale. */
    dist *= MAP_IS_ISOMETRIC ? 2 : 1;

    return ((!topo_has_flag(TF_WRAPX) 
	     && (ntl_x < dist || ntl_x >= NATURAL_WIDTH - dist))
	    || (!topo_has_flag(TF_WRAPY)
		&& (ntl_y < dist || ntl_y >= NATURAL_HEIGHT - dist)));
  } do_in_natural_pos_end;
}
