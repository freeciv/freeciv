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
#ifndef FC__MAP_H
#define FC__MAP_H

#include <assert.h>
#include <math.h>

#include "fc_types.h"

#include "game.h"
#include "player.h"
#include "terrain.h"
#include "unit.h"

/*
 * The value of MOVE_COST_FOR_VALID_SEA_STEP has no particular
 * meaning. The value is only used for comparison. The value must be
 * <0.
 */
#define MOVE_COST_FOR_VALID_SEA_STEP	(-3)
#define MOVE_COST_FOR_VALID_AIR_STEP	(-3)

/* For client Area Selection */
enum tile_hilite {
  HILITE_NONE = 0, HILITE_CITY
};

/* Convenience macro for accessing tile coordinates.  This should only be
 * used for debugging. */
#define TILE_XY(ptile) ((ptile) ? (ptile)->x : -1), \
                       ((ptile) ? (ptile)->y : -1)

struct tile {
  const int x, y; /* Cartesian (map) coordinates of the tile. */
  const int nat_x, nat_y; /* Native coordinates of the tile. */
  const int index; /* Index coordinate of the tile. */
  Terrain_type_id terrain;
  enum tile_special_type special;
  struct city *city;
  struct unit_list units;
  unsigned int known;   /* A bitvector on the server side, an
			   enum known_type on the client side.
			   Player_no is index */
  int assigned; /* these can save a lot of CPU usage -- Syela */
  struct city *worked;      /* city working tile, or NULL if none */
  Continent_id continent;
  signed char move_cost[8]; /* don't know if this helps! */
  struct player *owner;     /* Player owning this tile, or NULL. */
  struct {
    /* Area Selection in client. */
    enum tile_hilite hilite;
  } client;
  char *spec_sprite;
};


/****************************************************************
miscellaneous terrain information
*****************************************************************/
#define terrain_misc packet_ruleset_terrain_control

/****************************************************************
tile_type for each terrain type
expand with government bonuses??
*****************************************************************/
struct tile_type {
  const char *terrain_name; /* Translated string - doesn't need freeing. */
  char terrain_name_orig[MAX_LEN_NAME];	/* untranslated copy */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  /* Server-only. */
  char identifier; /* Single-character identifier used in savegames. */
#define UNKNOWN_TERRAIN_IDENTIFIER 'u'

  int movement_cost;
  int defense_bonus;

  int food;
  int shield;
  int trade;

  const char *special_1_name; /* Translated string - doesn't need freeing. */
  char special_1_name_orig[MAX_LEN_NAME]; /* untranslated copy */
  int food_special_1;
  int shield_special_1;
  int trade_special_1;

  const char *special_2_name; /* Translated string - doesn't need freeing. */
  char special_2_name_orig[MAX_LEN_NAME]; /* untranslated copy */
  int food_special_2;
  int shield_special_2;
  int trade_special_2;

  /* I would put the above special data in this struct too,
     but don't want to make unnecessary changes right now --dwp */
  struct {
    char graphic_str[MAX_LEN_NAME];
    char graphic_alt[MAX_LEN_NAME];
  } special[2];

  int road_trade_incr;
  int road_time;

  Terrain_type_id irrigation_result;
  int irrigation_food_incr;
  int irrigation_time;

  Terrain_type_id mining_result;
  int mining_shield_incr;
  int mining_time;

  Terrain_type_id transform_result;
  int transform_time;
  int rail_time;
  int airbase_time;
  int fortress_time;
  int clean_pollution_time;
  int clean_fallout_time;

  Terrain_type_id warmer_wetter_result, warmer_drier_result;
  Terrain_type_id cooler_wetter_result, cooler_drier_result;

  bv_terrain_flags flags;

  char *helptext;
};

/* The direction8 gives the 8 possible directions.  These may be used in
 * a number of ways, for instance as an index into the DIR_DX/DIR_DY
 * arrays.  Not all directions may be valid; see is_valid_dir and
 * is_cardinal_dir. */
enum direction8 {
  /* The DIR8/direction8 naming system is used to avoid conflict with
   * DIR4/direction4 in client/tilespec.h
   *
   * Changing the order of the directions will break network compatability.
   *
   * Some code assumes that the first 4 directions are the reverses of the
   * last 4 (in no particular order).  See client/goto.c. */
  DIR8_NORTHWEST = 0,
  DIR8_NORTH = 1,
  DIR8_NORTHEAST = 2,
  DIR8_WEST = 3,
  DIR8_EAST = 4,
  DIR8_SOUTHWEST = 5,
  DIR8_SOUTH = 6,
  DIR8_SOUTHEAST = 7
};
#define DIR8_LAST 8
#define DIR8_COUNT DIR8_LAST

struct civ_map {
  int topology_id;
  enum direction8 valid_dirs[8], cardinal_dirs[8];
  int num_valid_dirs, num_cardinal_dirs;
  struct iter_index *iterate_outwards_indices;
  int num_iterate_outwards_indices;
  int size; /* used to calculate [xy]size */
  int xsize, ysize; /* native dimensions */
  int seed;
  int riches;
  int huts;
  int landpercent;
  int generator;
  int startpos;
  bool tinyisles;
  bool separatepoles;
  bool alltemperate;
  int temperature;
  int wetness;
  int steepness;
  int num_start_positions;
  bool have_specials;
  bool have_huts;
  bool have_rivers_overlay;	/* only applies if !have_specials */
  int num_continents;
  int num_oceans;               /* not updated at the client */
  struct tile *tiles;

  /* Only used by server. */
  struct start_position {
    struct tile *tile;
    Nation_Type_id nation; /* May be NO_NATION_SELECTED. */
  } *start_positions;	/* allocated at runtime */
};

enum topo_flag {
  /* Bit-values. */
  /* Changing these values will break map_init_topology. */
  TF_WRAPX = 1,
  TF_WRAPY = 2,
  TF_ISO = 4,
  TF_HEX = 8
};

#define MAP_IS_ISOMETRIC (topo_has_flag(TF_ISO) || topo_has_flag(TF_HEX))

#define CURRENT_TOPOLOGY (map.topology_id)

#define topo_has_flag(flag) ((CURRENT_TOPOLOGY & (flag)) != 0)

bool map_is_empty(void);
void map_init(void);
void map_init_topology(bool set_sizes);
void map_allocate(void);
void map_free(void);

const char *map_get_tile_info_text(const struct tile *ptile);

int map_vector_to_real_distance(int dx, int dy);
int map_vector_to_sq_distance(int dx, int dy);
int map_distance(const struct tile *tile0, const struct tile *tile1);
int real_map_distance(const struct tile *tile0, const struct tile *tile1);
int sq_map_distance(const struct tile *tile0,const  struct tile *tile1);

bool same_pos(const struct tile *tile0, const struct tile *tile1);
bool base_get_direction_for_step(const struct tile *src_tile,
				 const struct tile *dst_tile,
				 enum direction8 *dir);
int get_direction_for_step(const struct tile *src_tile,
			   const struct tile *dst_tile);

void map_set_continent(struct tile *ptile, Continent_id val);
Continent_id map_get_continent(const struct tile *ptile);

void initialize_move_costs(void);
void reset_move_costs(struct tile *ptile);

/* Maximum value of index (for sanity checks and allocations) */
#define MAX_MAP_INDEX (map.xsize * map.ysize)

#ifdef DEBUG
#define CHECK_MAP_POS(x,y) assert(is_normal_map_pos((x),(y)))
#define CHECK_NATIVE_POS(x, y) assert((x) >= 0 && (x) < map.xsize \
				      && (y) >= 0 && (y) < map.ysize)
#define CHECK_INDEX(index) assert((index) >= 0 && (index) < MAX_MAP_INDEX)
#else
#define CHECK_MAP_POS(x,y) ((void)0)
#define CHECK_NATIVE_POS(x, y) ((void)0)
#define CHECK_INDEX(index) ((void)0)
#endif

#define native_pos_to_index(nat_x, nat_y)                                   \
  (CHECK_NATIVE_POS((nat_x), (nat_y)),					    \
   (nat_x) + (nat_y) * map.xsize)
#define index_to_native_pos(pnat_x, pnat_y, index)                          \
  (*(pnat_x) = (index) % map.xsize,                                         \
   *(pnat_y) = (index) / map.xsize)

/* Obscure math.  See explanation in doc/HACKING. */
#define NATIVE_TO_MAP_POS(pmap_x, pmap_y, nat_x, nat_y)                     \
  (MAP_IS_ISOMETRIC							    \
   ? (*(pmap_x) = ((nat_y) + ((nat_y) & 1)) / 2 + (nat_x),                  \
      *(pmap_y) = (nat_y) - *(pmap_x) + map.xsize)                          \
   : (*(pmap_x) = (nat_x), *(pmap_y) = (nat_y)))

#define MAP_TO_NATIVE_POS(pnat_x, pnat_y, map_x, map_y)                     \
  (MAP_IS_ISOMETRIC							    \
   ? (*(pnat_y) = (map_x) + (map_y) - map.xsize,                            \
      *(pnat_x) = (2 * (map_x) - *(pnat_y) - (*(pnat_y) & 1)) / 2)          \
   : (*(pnat_x) = (map_x), *(pnat_y) = (map_y)))

#define NATURAL_TO_MAP_POS(pmap_x, pmap_y, nat_x, nat_y)                    \
  (MAP_IS_ISOMETRIC							    \
   ? (*(pmap_x) = ((nat_y) + (nat_x)) / 2,                                  \
      *(pmap_y) = (nat_y) - *(pmap_x) + map.xsize)                          \
   : (*(pmap_x) = (nat_x), *(pmap_y) = (nat_y)))

#define MAP_TO_NATURAL_POS(pnat_x, pnat_y, map_x, map_y)                    \
  (MAP_IS_ISOMETRIC							    \
   ? (*(pnat_y) = (map_x) + (map_y) - map.xsize,                            \
      *(pnat_x) = 2 * (map_x) - *(pnat_y))                                  \
   : (*(pnat_x) = (map_x), *(pnat_y) = (map_y)))


/* Provide a block to convert from map to native coordinates.  This allows
 * you to use a native version of the map position within the block.  Note
 * that the native position is declared as const and can't be changed
 * inside the block. */
#define do_in_native_pos(nat_x, nat_y, map_x, map_y)                        \
{                                                                           \
  int _nat_x, _nat_y;                                                       \
  MAP_TO_NATIVE_POS(&_nat_x, &_nat_y, map_x, map_y);			    \
  {                                                                         \
    const int nat_x = _nat_x, nat_y = _nat_y;

#define do_in_native_pos_end                                                \
  }                                                                         \
}

/* Provide a block to convert from map to natural coordinates. This allows
 * you to use a natural version of the map position within the block.  Note
 * that the natural position is declared as const and can't be changed
 * inside the block. */
#define do_in_natural_pos(ntl_x, ntl_y, map_x, map_y)                        \
{                                                                           \
  int _ntl_x, _ntl_y;                                                       \
  MAP_TO_NATURAL_POS(&_ntl_x, &_ntl_y, map_x, map_y);			    \
  {                                                                         \
    const int ntl_x = _ntl_x, ntl_y = _ntl_y;

#define do_in_natural_pos_end                                                \
  }                                                                         \
}

/* Width and height of the map, in native coordinates. */
#define NATIVE_WIDTH map.xsize
#define NATIVE_HEIGHT map.ysize

/* Width and height of the map, in natural coordinates. */
#define NATURAL_WIDTH (MAP_IS_ISOMETRIC ? 2 * map.xsize : map.xsize)
#define NATURAL_HEIGHT map.ysize

#define MAP_WIDTH  \
  (MAP_IS_ISOMETRIC ? (map.xsize + map.ysize / 2) : map.xsize)
#define MAP_HEIGHT \
  (MAP_IS_ISOMETRIC ? (map.xsize + map.ysize / 2) : map.ysize)
  
static inline int map_pos_to_index(int map_x, int map_y);

/* index_to_map_pos(int *, int *, int) inverts map_pos_to_index */
#define index_to_map_pos(pmap_x, pmap_y, index) \
  (CHECK_INDEX(index),                          \
   index_to_native_pos(pmap_x, pmap_y, index),  \
   NATIVE_TO_MAP_POS(pmap_x, pmap_y, *(pmap_x), *(pmap_y)))

#define DIRSTEP(dest_x, dest_y, dir)	\
(    (dest_x) = DIR_DX[(dir)],      	\
     (dest_y) = DIR_DY[(dir)])

/*
 * Steps from the tile in the given direction, yielding a new tile (or NULL).
 *
 * Direct calls to DIR_DXY should be avoided and DIRSTEP should be
 * used. But to allow dest and src to be the same, as in
 *    MAPSTEP(x, y, x, y, dir)
 * we bend this rule here.
 */
struct tile *mapstep(const struct tile *ptile, enum direction8 dir);

struct tile *map_pos_to_tile(int x, int y);
struct tile *native_pos_to_tile(int nat_x, int nat_y);
struct tile *index_to_tile(int index);

struct player *map_get_owner(const struct tile *ptile);
void map_set_owner(struct tile *ptile, struct player *pplayer);
struct city *map_get_city(const struct tile *ptile);
void map_set_city(struct tile *ptile, struct city *pcity);
Terrain_type_id map_get_terrain(const struct tile *ptile);
enum tile_special_type map_get_special(const struct tile *ptile);
void map_set_terrain(struct tile *ptile, Terrain_type_id ter);
void map_set_special(struct tile *ptile, enum tile_special_type spe);
void map_clear_special(struct tile *ptile, enum tile_special_type spe);
void map_clear_all_specials(struct tile *ptile);

bool is_real_map_pos(int x, int y);
bool is_normal_map_pos(int x, int y);

/* implemented in server/maphand.c and client/climisc.c */
enum known_type map_get_known(const struct tile *ptile,
			      struct player *pplayer);

/* special testing */
bool map_has_special(const struct tile *ptile,
		     enum tile_special_type to_test_for);
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type to_test_for);
bool contains_special(enum tile_special_type all,
		      enum tile_special_type to_test_for);


bool is_singular_tile(const struct tile *ptile, int dist);
bool normalize_map_pos(int *x, int *y);
struct tile *nearest_real_tile(int x, int y);
void base_map_distance_vector(int *dx, int *dy,
			      int x0, int y0, int x1, int y1);
void map_distance_vector(int *dx, int *dy, const struct tile *ptile0,
			 const struct tile *ptile1);
int map_num_tiles(void);

struct tile *rand_neighbour(const struct tile *ptile);
struct tile *rand_map_pos(void);
struct tile *rand_map_pos_filtered(void *data,
				   bool (*filter)(const struct tile *ptile,
						  const void *data));

bool is_water_adjacent_to_tile(const struct tile *ptile);
bool is_tiles_adjacent(const struct tile *ptile0, const struct tile *ptile1);
bool is_move_cardinal(const struct tile *src_tile,
		      const struct tile *dst_tile);
int map_move_cost(struct unit *punit, const struct tile *ptile);
enum tile_special_type get_special_by_name(const char * name);
const char *get_special_name(enum tile_special_type type);
bool is_safe_ocean(const struct tile *ptile);
bool is_cardinally_adj_to_ocean(const struct tile *ptile);
bool is_sea_usable(const struct tile *ptile);
int get_tile_food_base(const struct tile *ptile);
int get_tile_shield_base(const struct tile *ptile);
int get_tile_trade_base(const struct tile *ptile);
enum tile_special_type get_tile_infrastructure_set(const struct tile *ptile);
const char *map_get_infrastructure_text(enum tile_special_type spe);
enum tile_special_type map_get_infrastructure_prerequisite(enum tile_special_type spe);
enum tile_special_type get_preferred_pillage(enum tile_special_type pset);

void map_irrigate_tile(struct tile *ptile);
void map_mine_tile(struct tile *ptile);
void change_terrain(struct tile *ptile, Terrain_type_id type);
void map_transform_tile(struct tile *ptile);

int map_build_road_time(const struct tile *ptile);
int map_build_irrigation_time(const struct tile *ptile);
int map_build_mine_time(const struct tile *ptile);
int map_transform_time(const struct tile *ptile);
int map_build_rail_time(const struct tile *ptile);
int map_build_airbase_time(const struct tile *ptile);
int map_build_fortress_time(const struct tile *ptile);
int map_clean_pollution_time(const struct tile *ptile);
int map_clean_fallout_time(const struct tile *ptile);
int map_activity_time(enum unit_activity activity, const struct tile *ptile);

bool can_channel_land(const struct tile *ptile);
bool can_reclaim_ocean(const struct tile *ptile);

extern struct civ_map map;

extern struct terrain_misc terrain_control;

/* This iterates outwards from the starting point. Every tile within max_dist
 * will show up exactly once, in an outward (based on real map distance)
 * order.  The returned values are always real and are normalized.  The
 * starting position must be normal.
 *
 * See also iterate_outward() */
#define iterate_outward_dxy(start_tile, max_dist, tile_itr, dx_itr, dy_itr) \
{									    \
  const struct tile *_start_tile = (start_tile);			    \
  struct tile *tile_itr;						    \
  int _max_dist = (max_dist), _x_itr, _y_itr, dx_itr, dy_itr, _index;	    \
  bool _is_border = is_border_tile(_start_tile, _max_dist);		    \
									    \
  for (_index = 0; _index < map.num_iterate_outwards_indices; _index++) {   \
    if (map.iterate_outwards_indices[_index].dist > _max_dist) {	    \
      break;								    \
    }									    \
    dx_itr = map.iterate_outwards_indices[_index].dx;			    \
    dy_itr = map.iterate_outwards_indices[_index].dy;			    \
    _x_itr = dx_itr + _start_tile->x;					    \
    _y_itr = dy_itr + _start_tile->y;					    \
    if (_is_border && !normalize_map_pos(&_x_itr, &_y_itr)) {		    \
      continue;								    \
    }									    \
    tile_itr = map.tiles + map_pos_to_index(_x_itr, _y_itr);

#define iterate_outward_dxy_end						    \
  }                                                                         \
}

/* See iterate_outward_dxy() */
#define iterate_outward(start_tile, max_dist, itr_tile)			    \
  iterate_outward_dxy(start_tile, max_dist, itr_tile, _dx_itr, _dy_itr)

#define iterate_outward_end iterate_outward_dxy_end

/* 
 * Iterate through all tiles in a square with given center and radius.
 * The position (x_itr, y_itr) that is returned will be normalized;
 * unreal positions will be automatically discarded. (dx_itr, dy_itr)
 * is the standard distance vector between the position and the center
 * position. Note that when the square is larger than the map the
 * distance vector may not be the minimum distance vector.
 */
#define square_dxy_iterate(center_tile, radius, tile_itr, dx_itr, dy_itr)   \
  iterate_outward_dxy(center_tile, radius, tile_itr, dx_itr, dy_itr)

#define square_dxy_iterate_end iterate_outward_dxy_end

/*
 * Iterate through all tiles in a square with given center and radius.
 * Positions returned will have adjusted x, and positions with illegal
 * y will be automatically discarded.
 */
#define square_iterate(center_tile, radius, tile_itr)			    \
  square_dxy_iterate(center_tile, radius, tile_itr, _dummy_x, dummy_y)

#define square_iterate_end  square_dxy_iterate_end

/* 
 * Iterate through all tiles in a circle with given center and squared
 * radius.  Positions returned will have adjusted (x, y); unreal
 * positions will be automatically discarded. 
 */
#define circle_iterate(center_tile, sq_radius, tile_itr)		    \
{                                                                           \
  int _sq_radius = (sq_radius);						    \
  int _cr_radius = (int)sqrt((double)_sq_radius);			    \
  square_dxy_iterate(center_tile, _cr_radius, tile_itr, _dx, _dy) {	    \
    if (map_vector_to_sq_distance(_dx, _dy) <= _sq_radius) {

#define circle_iterate_end                                                  \
    }                                                                       \
  } square_dxy_iterate_end;                                                 \
}									       

/* Iterate through all map positions adjacent to the given center map
 * position, with normalization.  The order of positions is unspecified. */
#define adjc_iterate(center_tile, itr_tile)				    \
{									    \
  /* Written as a wrapper to adjc_dir_iterate since it's the cleanest and   \
   * most efficient. */							    \
  adjc_dir_iterate(center_tile, itr_tile, ADJC_ITERATE_dir_itr) {

#define adjc_iterate_end                                                    \
  } adjc_dir_iterate_end;                                                   \
}

#define adjc_dir_iterate(center_tile, itr_tile, dir_itr)		    \
  adjc_dirlist_iterate(center_tile, itr_tile, dir_itr,			    \
		       map.valid_dirs, map.num_valid_dirs)

#define adjc_dir_iterate_end adjc_dirlist_iterate_end

#define cardinal_adjc_iterate(center_tile, itr_tile)			    \
  adjc_dirlist_iterate(center_tile, itr_tile, _dir_itr,			    \
		       map.cardinal_dirs, map.num_cardinal_dirs)

#define cardinal_adjc_iterate_end adjc_dirlist_iterate_end

#define cardinal_adjc_dir_iterate(center_tile, itr_tile, dir_itr)	    \
  adjc_dirlist_iterate(center_tile, itr_tile, dir_itr,			    \
		       map.cardinal_dirs, map.num_cardinal_dirs)

#define cardinal_adjc_dir_iterate_end adjc_dirlist_iterate_end

/* Iterate through all tiles adjacent to a tile using the given list of
 * directions.  dir_itr is the directional value, (center_x, center_y) is
 * the center tile (which must be normalized), and (x_itr, y_itr) is the
 * position corresponding to dir_itr.
 *
 * This macro should not be used directly.  Instead, use adjc_dir_iterate
 * or cartesian_adjacent_iterate. */
#define adjc_dirlist_iterate(center_tile, itr_tile, dir_itr,		    \
			     dirlist, dircount)				    \
{									    \
  const struct tile *_center_tile = (center_tile);			    \
  struct tile *itr_tile;						    \
  int _dir_index, _x_itr, _y_itr;					    \
  enum direction8 dir_itr;						    \
  bool _is_border = is_border_tile(_center_tile, 1);			    \
									    \
  for (_dir_index = 0; _dir_index < (dircount); _dir_index++) {		    \
    dir_itr = dirlist[_dir_index];					    \
    DIRSTEP(_x_itr, _y_itr, dir_itr);					    \
    _x_itr += _center_tile->x;						    \
    _y_itr += _center_tile->y;						    \
    if (_is_border && !normalize_map_pos(&_x_itr, &_y_itr)) {		    \
      continue;								    \
    }									    \
    itr_tile = map.tiles + map_pos_to_index(_x_itr, _y_itr);

#define adjc_dirlist_iterate_end					    \
    }									    \
}

/* Iterate over all positions on the globe. */
#define whole_map_iterate(ptile)					    \
{                                                                           \
  int _index; /* We use index positions for cache efficiency. */	    \
  for (_index = 0; _index < MAX_MAP_INDEX; _index++) {			    \
    struct tile *ptile = map.tiles + _index;				    \

#define whole_map_iterate_end                                               \
  }                                                                         \
}

BV_DEFINE(dir_vector, 8);

struct unit_order {
  enum unit_orders order;
  enum direction8 dir;		/* Only valid for ORDER_MOVE. */
  enum unit_activity activity;	/* Only valid for ORDER_ACTIVITY. */
};

/* return the reverse of the direction */
#define DIR_REVERSE(dir) (7 - (dir))

enum direction8 dir_cw(enum direction8 dir);
enum direction8 dir_ccw(enum direction8 dir);
const char* dir_get_name(enum direction8 dir);
bool is_valid_dir(enum direction8 dir);
bool is_cardinal_dir(enum direction8 dir);

extern const int DIR_DX[8];
extern const int DIR_DY[8];

/* Used for network transmission; do not change. */
#define MAP_TILE_OWNER_NULL	 MAX_UINT8

#define MAP_DEFAULT_HUTS         50
#define MAP_MIN_HUTS             0
#define MAP_MAX_HUTS             500

/* Size of the map in thousands of tiles */
#define MAP_DEFAULT_SIZE         4
#define MAP_MIN_SIZE             1
#define MAP_MAX_SIZE             29

/* This defines the maximum linear size in _map_ coordinates.
 * This must be smaller than 255 because of the way coordinates are sent
 * across the network. */
#define MAP_MAX_LINEAR_SIZE      254
#define MAP_MIN_LINEAR_SIZE      8
#define MAP_MAX_WIDTH            MAP_MAX_LINEAR_SIZE
#define MAP_MAX_HEIGHT           MAP_MAX_LINEAR_SIZE

#define MAP_ORIGINAL_TOPO        TF_WRAPX
#define MAP_DEFAULT_TOPO         TF_WRAPX
#define MAP_MIN_TOPO             0
#define MAP_MAX_TOPO             15

#define MAP_DEFAULT_SEED         0
#define MAP_MIN_SEED             0
#define MAP_MAX_SEED             (MAX_UINT32 >> 1)

#define MAP_DEFAULT_LANDMASS     30
#define MAP_MIN_LANDMASS         15
#define MAP_MAX_LANDMASS         85

#define MAP_DEFAULT_RICHES       250
#define MAP_MIN_RICHES           0
#define MAP_MAX_RICHES           1000

#define MAP_DEFAULT_STEEPNESS    30
#define MAP_MIN_STEEPNESS        0
#define MAP_MAX_STEEPNESS        100

#define MAP_DEFAULT_WETNESS      50
#define MAP_MIN_WETNESS          0
#define MAP_MAX_WETNESS          100

#define MAP_DEFAULT_GENERATOR    1
#define MAP_MIN_GENERATOR        1
#define MAP_MAX_GENERATOR        3

#define MAP_DEFAULT_STARTPOS     0
#define MAP_MIN_STARTPOS         0
#define MAP_MAX_STARTPOS         4

#define MAP_DEFAULT_TINYISLES    FALSE
#define MAP_MIN_TINYISLES        FALSE
#define MAP_MAX_TINYISLES        TRUE

#define MAP_DEFAULT_SEPARATE_POLES   TRUE
#define MAP_MIN_SEPARATE_POLES       FALSE
#define MAP_MAX_SEPARATE_POLES       TRUE

#define MAP_DEFAULT_ALLTEMPERATE   FALSE
#define MAP_MIN_ALLTEMPERATE       FALSE
#define MAP_MAX_ALLTEMPERATE       TRUE

#define MAP_DEFAULT_TEMPERATURE   50
#define MAP_MIN_TEMPERATURE       0
#define MAP_MAX_TEMPERATURE       100

/*
 * Inline function definitions.  These are at the bottom because they may use
 * elements defined above.
 */

static inline int map_pos_to_index(int map_x, int map_y)
{
  /* Note: writing this as a macro is hard; it needs temp variables. */
  int nat_x, nat_y;

  CHECK_MAP_POS(map_x, map_y);
  MAP_TO_NATIVE_POS(&nat_x, &nat_y, map_x, map_y);
  return native_pos_to_index(nat_x, nat_y);
}

/****************************************************************************
  A "border position" is any map position that _may have_ positions within
  real map distance dist that are non-normal.  To see its correctness,
  consider the case where dist is 1 or 0.
****************************************************************************/
static inline bool is_border_tile(const struct tile *ptile, int dist)
{
  /* HACK: An iso-map compresses the value in the X direction but not in
   * the Y direction.  Hence (x+1,y) is 1 tile away while (x,y+2) is also
   * one tile away. */
  int xdist = dist;
  int ydist = (MAP_IS_ISOMETRIC ? (2 * dist) : dist);

  return (ptile->nat_x < xdist 
	  || ptile->nat_y < ydist
	  || ptile->nat_x >= map.xsize - xdist
	  || ptile->nat_y >= map.ysize - ydist);
}

/* An arbitrary somewhat integer value.  Activity times are multiplied by
 * this amount, and divided by them later before being used.  This may
 * help to avoid rounding errors; however it should probably be removed. */
#define ACTIVITY_FACTOR 10

#endif  /* FC__MAP_H */
