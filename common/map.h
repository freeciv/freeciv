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

#include "player.h"
#include "terrain.h"
#include "unit.h"
#include "game.h"

struct Sprite;			/* opaque; client-gui specific */

#define NUM_DIRECTION_NSEW 		16

/*
 * The value of MOVE_COST_FOR_VALID_SEA_STEP has no particular
 * meaning. The value is only used for comparison. The value must be
 * <0.
 */
#define MOVE_COST_FOR_VALID_SEA_STEP	(-3)

struct map_position {
  int x,y;
};

struct goto_route {
  int first_index; /* first valid tile pos */
  int last_index; /* point to the first non_legal pos. Note that the pos
		   is always alloced in the pos array (for coding reasons) */
  int length; /* length of pos array (use this as modulus when iterating)
		 Note that this is always at least 1 greater than the number
		 of valid positions, to make comparing first_index with
		 last_index during wrapped iteration easier. */
  struct map_position *pos;
};

struct tile {
  enum tile_terrain_type terrain;
  enum tile_special_type special;
  struct city *city;
  struct unit_list units;
  unsigned int known;   /* A bitvector on the server side, an
			   enum known_type on the client side.
			   Player_no is index */
  unsigned int sent;    /* Indicates if  the client know the tile
			   as TILE_KNOWN_NODRAW. A bitvector like known.
			   Not used on the client side. */
  int assigned; /* these can save a lot of CPU usage -- Syela */
  struct city *worked;      /* city working tile, or NULL if none */
  signed short continent;
  signed char move_cost[8]; /* don't know if this helps! */
};


/****************************************************************
miscellaneous terrain information
*****************************************************************/

struct terrain_misc
{
  /* options */
  enum terrain_river_type river_style;
  int may_road;       /* boolean: may build roads/railroads */
  int may_irrigate;   /* boolean: may build irrigation/farmland */
  int may_mine;       /* boolean: may build mines */
  int may_transform;  /* boolean: may transform terrain */
  /* parameters */
  int ocean_reclaim_requirement;       /* # adjacent land tiles for reclaim */
  int land_channel_requirement;        /* # adjacent ocean tiles for channel */
  enum special_river_move river_move_mode;
  int river_defense_bonus;             /* % added to defense if Civ2 river */
  int river_trade_incr;                /* added to trade if Civ2 river */
  char *river_help_text;	       /* help for Civ2-style rivers */
  int fortress_defense_bonus;          /* % added to defense if fortress */
  int road_superhighway_trade_bonus;   /* % added to trade if road/s-highway */
  int rail_food_bonus;                 /* % added to food if railroad */
  int rail_shield_bonus;               /* % added to shield if railroad */
  int rail_trade_bonus;                /* % added to trade if railroad */
  int farmland_supermarket_food_bonus; /* % added to food if farm/s-market */
  int pollution_food_penalty;          /* % subtr. from food if polluted */
  int pollution_shield_penalty;        /* % subtr. from shield if polluted */
  int pollution_trade_penalty;         /* % subtr. from trade if polluted */
  int fallout_food_penalty;            /* % subtr. from food if fallout */
  int fallout_shield_penalty;          /* % subtr. from shield if fallout */
  int fallout_trade_penalty;           /* % subtr. from trade if fallout */
};

/****************************************************************
tile_type for each terrain type
expand with government bonuses??
*****************************************************************/

struct tile_type {
  char terrain_name[MAX_LEN_NAME];     /* "" if unused */
  char terrain_name_orig[MAX_LEN_NAME];	/* untranslated copy */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  struct Sprite *sprite[NUM_DIRECTION_NSEW];

  int movement_cost;
  int defense_bonus;

  int food;
  int shield;
  int trade;

  char special_1_name[MAX_LEN_NAME];   /* "" if none */
  char special_1_name_orig[MAX_LEN_NAME]; /* untranslated copy */
  int food_special_1;
  int shield_special_1;
  int trade_special_1;

  char special_2_name[MAX_LEN_NAME];   /* "" if none */
  char special_2_name_orig[MAX_LEN_NAME]; /* untranslated copy */
  int food_special_2;
  int shield_special_2;
  int trade_special_2;

  /* I would put the above special data in this struct too,
     but don't want to make unnecessary changes right now --dwp */
  struct {
    char graphic_str[MAX_LEN_NAME];
    char graphic_alt[MAX_LEN_NAME];
    struct Sprite *sprite;
  } special[2];

  int road_trade_incr;
  int road_time;

  enum tile_terrain_type irrigation_result;
  int irrigation_food_incr;
  int irrigation_time;

  enum tile_terrain_type mining_result;
  int mining_shield_incr;
  int mining_time;

  enum tile_terrain_type transform_result;
  int transform_time;

  char *helptext;
};

struct civ_map { 
  int xsize, ysize;
  int seed;
  int riches;
  int is_earth;
  int huts;
  int landpercent;
  int grasssize;
  int swampsize;
  int deserts;
  int mountains;
  int riverlength;
  int forestsize;
  int generator;
  int tinyisles;
  int separatepoles;
  int num_start_positions;
  int fixed_start_positions;
  int have_specials;
  int have_huts;
  int have_rivers_overlay;	/* only applies if !have_specials */
  int num_continents;
  struct tile *tiles;
  struct map_position start_positions[MAX_NUM_NATIONS];
};

int map_is_empty(void);
void map_init(void);
void map_allocate(void);

char *map_get_tile_info_text(int x, int y);
char *map_get_tile_fpt_text(int x, int y);
struct tile *map_get_tile(int x, int y);

int map_distance(int x0, int y0, int x1, int y1);
int real_map_distance(int x0, int y0, int x1, int y1);
int sq_map_distance(int x0, int y0, int x1, int y1);
int same_pos(int x1, int y1, int x2, int y2);
int base_get_direction_for_step(int start_x, int start_y, int end_x,
				int end_y, int *dir);
int get_direction_for_step(int start_x, int start_y, int end_x, int end_y);

void map_set_continent(int x, int y, int val);
signed short map_get_continent(int x, int y);

void initialize_move_costs(void);
void reset_move_costs(int x, int y);

#define CHECK_MAP_POS(x,y) assert(is_normal_map_pos((x),(y)))

#define map_adjust_x(X)            \
  ((X) < 0                         \
   ? ((X) % map.xsize != 0 ? (X) % map.xsize + map.xsize : 0) \
   : ((X) >= map.xsize             \
      ? (X) % map.xsize            \
      : (X)))

#define map_adjust_y(Y) \
  (((Y)<0) ? 0 : (((Y)>=map.ysize) ? map.ysize-1 : (Y)))

#define map_inx(x,y) \
  (CHECK_MAP_POS((x),(y)), (x)+(y)*map.xsize)

#define DIRSTEP(dest_x, dest_y, dir)	\
(    (dest_x) = DIR_DX[(dir)],      	\
     (dest_y) = DIR_DY[(dir)])

/*
 * Returns true if the step yields a new valid map position. If yes
 * (dest_x, dest_y) is set to the new map position.
 */
#define MAPSTEP(dest_x, dest_y, src_x, src_y, dir)	\
(    DIRSTEP(dest_x, dest_y, dir),			\
     (dest_x) += (src_x),		   		\
     (dest_y) += (src_y),   				\
     normalize_map_pos(&(dest_x), &(dest_y)))

/*
 * Sets (dest_x, dest_y) to the new map position or to (src_x, src_y)
 * if the step would end in an unreal map position. In most cases it
 * is better to use MAPSTEP instead of SAFE_MAPSTEP.
 */
#define SAFE_MAPSTEP(dest_x, dest_y, src_x, src_y, dir)	\
   do {                                                 \
     DIRSTEP(dest_x, dest_y, dir);			\
     (dest_x) += (src_x);		   		\
     (dest_y) += (src_y);   				\
     if (!normalize_map_pos(&(dest_x), &(dest_y))) {    \
       (dest_x) = (src_x);                              \
       (dest_y) = (src_y);                              \
       CHECK_MAP_POS((dest_x), (dest_y));               \
     }                                                  \
   } while(0)

/*
 * Returns the next direction clock-wise
 */
#define DIR_CW(dir) \
  ((dir)==DIR8_WEST ? DIR8_NORTHWEST : \
   ((dir)==DIR8_EAST ? DIR8_SOUTHEAST : \
    ((dir)==DIR8_NORTH ? DIR8_NORTHEAST : \
     ((dir)==DIR8_SOUTH ? DIR8_SOUTHWEST : \
      ((dir)==DIR8_NORTHWEST ? DIR8_NORTH : \
       ((dir)==DIR8_NORTHEAST ? DIR8_EAST : \
        ((dir)==DIR8_SOUTHWEST ? DIR8_WEST : \
         ((dir)==DIR8_SOUTHEAST ? DIR8_SOUTH : \
         (dir)/0))))))))

/*
 * Returns the next direction counter-clock-wise
 */
#define DIR_CCW(dir) \
  ((dir)==DIR8_WEST ? DIR8_SOUTHWEST : \
   ((dir)==DIR8_EAST ? DIR8_NORTHEAST : \
    ((dir)==DIR8_NORTH ? DIR8_NORTHWEST : \
     ((dir)==DIR8_SOUTH ? DIR8_SOUTHEAST : \
      ((dir)==DIR8_NORTHWEST ? DIR8_WEST : \
       ((dir)==DIR8_NORTHEAST ? DIR8_NORTH : \
        ((dir)==DIR8_SOUTHWEST ? DIR8_SOUTH : \
         ((dir)==DIR8_SOUTHEAST ? DIR8_EAST : \
         (dir)/0))))))))

struct city *map_get_city(int x, int y);
void map_set_city(int x, int y, struct city *pcity);
enum tile_terrain_type map_get_terrain(int x, int y);
enum tile_special_type map_get_special(int x, int y);
void map_set_terrain(int x, int y, enum tile_terrain_type ter);
void map_set_special(int x, int y, enum tile_special_type spe);
void map_clear_special(int x, int y, enum tile_special_type spe);
void tile_init(struct tile *ptile);
int is_real_tile(int x, int y);
int is_normal_map_pos(int x, int y);

/* 
 * Determines whether the position is normal given that it's in the
 * range 0<=x<map.xsize and 0<=y<map.ysize.  It's primarily a faster
 * version of is_normal_map_pos since such checks are pretty common.
 */
#define regular_map_pos_is_normal(x, y) (1)

/*
 * A "border position" is any one that has adjacent positions that are
 * not normal/proper.
 */
#define IS_BORDER_MAP_POS(x, y)              \
  ((y) == 0 || (x) == 0 ||                   \
   (y) == map.ysize-1 || (x) == map.xsize-1)

int normalize_map_pos(int *x, int *y);
void nearest_real_pos(int *x, int *y);
void map_distance_vector(int *dx, int *dy, int x0, int y0, int x1, int y1);
int map_num_tiles(void);

void rand_neighbour(int x0, int y0, int *x, int *y);
void rand_map_pos(int *x, int *y);

int is_water_adjacent_to_tile(int x, int y);
int is_tiles_adjacent(int x0, int y0, int x1, int y1);
int is_move_cardinal(int start_x, int start_y, int end_x, int end_y);
int map_move_cost(struct unit *punit, int x, int y);
struct tile_type *get_tile_type(enum tile_terrain_type type);
enum tile_terrain_type get_terrain_by_name(char * name);
char *get_terrain_name(enum tile_terrain_type type);
enum tile_special_type get_special_by_name(char * name);
const char *get_special_name(enum tile_special_type type);
int is_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int count_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int is_special_near_tile(int x, int y, enum tile_special_type spe);
int count_special_near_tile(int x, int y, enum tile_special_type spe);
int is_coastline(int x,int y);
int terrain_is_clean(int x, int y);
int is_at_coast(int x, int y);
int is_hut_close(int x, int y);
int is_starter_close(int x, int y, int nr, int dist); 
int is_good_tile(int x, int y);
int is_special_close(int x, int y);
int is_sea_usable(int x, int y);
int get_tile_food_base(struct tile * ptile);
int get_tile_shield_base(struct tile * ptile);
int get_tile_trade_base(struct tile * ptile);
int get_tile_infrastructure_set(struct tile * ptile);
char *map_get_infrastructure_text(int spe);
int map_get_infrastructure_prerequisite(int spe);
int get_preferred_pillage(int pset);

void map_irrigate_tile(int x, int y);
void map_mine_tile(int x, int y);
void change_terrain(int x, int y, enum tile_terrain_type type);
void map_transform_tile(int x, int y);

int map_build_road_time(int x, int y);
int map_build_irrigation_time(int x, int y);
int map_build_mine_time(int x, int y);
int map_transform_time(int x, int y);
int map_build_rail_time(int x, int y);
int map_build_airbase_time(int x, int y);
int map_build_fortress_time(int x, int y);
int map_clean_pollution_time(int x, int y);
int map_clean_fallout_time(int x, int y);
int map_activity_time(enum unit_activity activity, int x, int y);

int can_channel_land(int x, int y);
int can_reclaim_ocean(int x, int y);

extern struct civ_map map;

extern struct terrain_misc terrain_control;
extern struct tile_type tile_types[T_LAST];

/* This iterates outwards from the starting point (Duh?).
   Every tile within max_dist will show up exactly once. (even takes
   into account wrap). All positions given correspond to real tiles.
   The values given are adjusted.
   You should make sure that the arguments passed to the macro are adjusted,
   or you could have some very nasty intermediate errors.

   Internally it works by for a given distance
   1) assume y positive and iterate over x
   2) assume y negative and iterate over x
   3) assume x positive and iterate over y
   4) assume x negative and iterate over y
   Where in this we are is decided by the variables xcycle and positive.
   each of there distances give a box of tiles; to ensure each tile is only
   returned once we only return the corner when iterating over x.
   As a special case positive is initialized as 0 (ie we start in step 2) ),
   as the center tile would else be returned by both step 1) and 2).
*/
#define iterate_outward(ARG_start_x, ARG_start_y, ARG_max_dist, ARG_x_itr, ARG_y_itr) \
{                                                                             \
  int ARG_x_itr, ARG_y_itr;                                                   \
  int MACRO_max_dx = map.xsize/2;                                             \
  int MACRO_min_dx = -(MACRO_max_dx - (map.xsize%2 ? 0 : 1));                 \
  int MACRO_xcycle = 1;                                                       \
  int MACRO_positive = 0;                                                     \
  int MACRO_dxy = 0, MACRO_do_xy;                                             \
  CHECK_MAP_POS(ARG_start_x, ARG_start_y);                                    \
  while(MACRO_dxy <= (ARG_max_dist)) {                                        \
    for (MACRO_do_xy = -MACRO_dxy; MACRO_do_xy <= MACRO_dxy; MACRO_do_xy++) { \
      if (MACRO_xcycle) {                                                     \
	ARG_x_itr = (ARG_start_x) + MACRO_do_xy;                              \
	if (MACRO_positive)                                                   \
	  ARG_y_itr = (ARG_start_y) + MACRO_dxy;                              \
	else                                                                  \
	  ARG_y_itr = (ARG_start_y) - MACRO_dxy;                              \
      } else { /* ! MACRO_xcycle */                                           \
        if (MACRO_dxy == MACRO_do_xy || MACRO_dxy == -MACRO_do_xy)            \
          continue;                                                           \
	ARG_y_itr = (ARG_start_y) + MACRO_do_xy;                              \
	if (MACRO_positive)                                                   \
	  ARG_x_itr = (ARG_start_x) + MACRO_dxy;                              \
	else                                                                  \
	  ARG_x_itr = (ARG_start_x) - MACRO_dxy;                              \
      }                                                                       \
      {                                                                       \
	int MACRO_dx = (ARG_start_x) - ARG_x_itr;                             \
	if (MACRO_dx > MACRO_max_dx || MACRO_dx < MACRO_min_dx)               \
	  continue;                                                           \
      }                                                                       \
      if (!normalize_map_pos(&ARG_x_itr, &ARG_y_itr))                         \
	continue;

#define iterate_outward_end                                                   \
    }                                                                         \
    if (!MACRO_positive) {                                                    \
      if (!MACRO_xcycle)                                                      \
	MACRO_dxy++;                                                          \
      MACRO_xcycle = !MACRO_xcycle;                                           \
    }                                                                         \
    MACRO_positive = !MACRO_positive;                                         \
  }                                                                           \
}

/* 
 * Iterate through all tiles in a square with given center and radius.
 * The position (x_itr, y_itr) that is returned will be normalized;
 * unreal positions will be automatically discarded. (dx_itr, dy_itr)
 * is the standard distance vector between the position and the center
 * position. Note that when the square is larger than the map the
 * distance vector may not be the minimum distance vector.
 */
#define square_dxy_iterate(center_x, center_y, radius, x_itr, y_itr,          \
                           dx_itr, dy_itr)                                    \
{                                                                             \
  int dx_itr, dy_itr;                                                         \
  CHECK_MAP_POS((center_x), (center_y));                                      \
  for (dy_itr = -(radius); dy_itr <= (radius); dy_itr++) {                    \
    for (dx_itr = -(radius); dx_itr <= (radius); dx_itr++) {                  \
      int x_itr = dx_itr + (center_x), y_itr = dy_itr + (center_y);           \
      if (normalize_map_pos(&x_itr, &y_itr)) {

#define square_dxy_iterate_end                                                \
      }                                                                       \
    }                                                                         \
  }                                                                           \
}

/*
 * Iterate through all tiles in a square with given center and radius.
 * Positions returned will have adjusted x, and positions with illegal
 * y will be automatically discarded.
 */
#define square_iterate(center_x, center_y, radius, x_itr, y_itr)              \
{                                                                             \
  square_dxy_iterate(center_x, center_y, radius, x_itr, y_itr,                \
                     _dummy_x, _dummy_y);

#define square_iterate_end  square_dxy_iterate_end                            \
}

/* 
 * Iterate through all tiles in a circle with given center and squared
 * radius.  Positions returned will have adjusted (x, y); unreal
 * positions will be automatically discarded. 
 */
#define circle_iterate(center_x, center_y, sq_radius, x_itr, y_itr)           \
{                                                                             \
  int _cr_radius = (int)sqrt(sq_radius);                                     \
  square_dxy_iterate(center_x, center_y, _cr_radius,                          \
		     x_itr, y_itr, _dx, _dy) {                                \
    if (_dy * _dy + _dx * _dx <= (sq_radius)) {

#define circle_iterate_end                                                    \
    }                                                                         \
  } square_dxy_iterate_end;                                                   \
}									       

/* Iterate through all tiles adjacent to a tile */
#define adjc_iterate(RI_center_x, RI_center_y, RI_x_itr, RI_y_itr)            \
{                                                                             \
  int RI_x_itr, RI_y_itr;                                                     \
  int RI_x_itr1, RI_y_itr1;                                                   \
  CHECK_MAP_POS(RI_center_x, RI_center_y);                                    \
  for (RI_y_itr1 = RI_center_y - 1;                                           \
       RI_y_itr1 <= RI_center_y + 1; RI_y_itr1++) {                           \
    for (RI_x_itr1 = RI_center_x - 1;                                         \
	 RI_x_itr1 <= RI_center_x + 1; RI_x_itr1++) {                         \
      RI_x_itr = RI_x_itr1;                                                   \
      RI_y_itr = RI_y_itr1;                                                   \
      if (!normalize_map_pos(&RI_x_itr, &RI_y_itr))                           \
        continue;                                                             \
      if (RI_x_itr == RI_center_x && RI_y_itr == RI_center_y)                 \
        continue; 

#define adjc_iterate_end                                                      \
    }                                                                         \
  }                                                                           \
}

/* Iterate through all tiles adjacent to a tile.  dir_itr is the
   directional value (see DIR_D[XY]).  This assumes that center_x and
   center_y are normalized. --JDS */
#define adjc_dir_iterate(center_x, center_y, x_itr, y_itr, dir_itr)           \
{                                                                             \
  int x_itr, y_itr, dir_itr, MACRO_border;                                    \
  int MACRO_center_x = (center_x);                                            \
  int MACRO_center_y = (center_y);                                            \
  CHECK_MAP_POS(MACRO_center_x, MACRO_center_y);                              \
  MACRO_border = IS_BORDER_MAP_POS(MACRO_center_x, MACRO_center_y);           \
  for (dir_itr = 0; dir_itr < 8; dir_itr++) {                                 \
    DIRSTEP(x_itr, y_itr, dir_itr);                                           \
    x_itr += MACRO_center_x;                                                  \
    y_itr += MACRO_center_y;                                                  \
    if (MACRO_border) {                                                       \
      if (!normalize_map_pos(&x_itr, &y_itr))                                 \
	continue;                                                             \
    }

#define adjc_dir_iterate_end                                                  \
  }                                                                           \
}

/* iterating y, x for cache efficiency */
#define whole_map_iterate(WMI_x_itr, WMI_y_itr)                               \
{                                                                             \
  int WMI_x_itr, WMI_y_itr;                                                   \
  for (WMI_y_itr = 0; WMI_y_itr < map.ysize; WMI_y_itr++)                     \
    for (WMI_x_itr = 0; WMI_x_itr < map.xsize; WMI_x_itr++)                   \
      if (regular_map_pos_is_normal(WMI_x_itr, WMI_y_itr)) {

#define whole_map_iterate_end                                                 \
      }                                                                       \
}

/*
used to compute neighboring tiles:
using
x1 = x + DIR_DX[dir];
y1 = y + DIR_DX[dir];
will give you the tile as shown below.
-------
|0|1|2|
|-+-+-|
|3| |4|
|-+-+-|
|5|6|7|
-------
Note that you must normalize x1 and y1 yourself.
*/

enum direction8 {
  /* FIXME: DIR8 is used to avoid conflict with
   * enum Directions in client/tilespec.h */
  DIR8_NORTHWEST = 0,
  DIR8_NORTH = 1,
  DIR8_NORTHEAST = 2,
  DIR8_WEST = 3,
  DIR8_EAST = 4,
  DIR8_SOUTHWEST = 5,
  DIR8_SOUTH = 6,
  DIR8_SOUTHEAST = 7
};

/* return the reverse of the direction */
#define DIR_REVERSE(dir) (7 - (dir))

/* is the direction "cardinal"?  Cardinal directions
 * (also called cartesian) are the four main ones */
#define DIR_IS_CARDINAL(dir)                           \
  ((dir) == DIR8_NORTH || (dir) == DIR8_EAST ||        \
   (dir) == DIR8_WEST || (dir) == DIR8_SOUTH)

const char* dir_get_name(enum direction8 dir);

extern const int DIR_DX[8];
extern const int DIR_DY[8];

/* like DIR_DX[] and DIR_DY[], only cartesian */
extern const int CAR_DIR_DX[4];
extern const int CAR_DIR_DY[4];

#define cartesian_adjacent_iterate(x, y, IAC_x, IAC_y) \
{                                                      \
  int IAC_i;                                           \
  int IAC_x, IAC_y;                                    \
  CHECK_MAP_POS(x, y);                                 \
  for (IAC_i = 0; IAC_i < 4; IAC_i++) {                \
    IAC_x = x + CAR_DIR_DX[IAC_i];                     \
    IAC_y = y + CAR_DIR_DY[IAC_i];                     \
                                                       \
    if (normalize_map_pos(&IAC_x, &IAC_y)) {

#define cartesian_adjacent_iterate_end                 \
    }                                                  \
  }                                                    \
}

#define MAP_DEFAULT_HUTS         50
#define MAP_MIN_HUTS             0
#define MAP_MAX_HUTS             500

#define MAP_DEFAULT_WIDTH        80
#define MAP_MIN_WIDTH            40
#define MAP_MAX_WIDTH            200

#define MAP_DEFAULT_HEIGHT       50
#define MAP_MIN_HEIGHT           25
#define MAP_MAX_HEIGHT           100

#define MAP_DEFAULT_SEED         0
#define MAP_MIN_SEED             0
#define MAP_MAX_SEED             (MAX_UINT32 >> 1)

#define MAP_DEFAULT_LANDMASS     30
#define MAP_MIN_LANDMASS         15
#define MAP_MAX_LANDMASS         85

#define MAP_DEFAULT_RICHES       250
#define MAP_MIN_RICHES           0
#define MAP_MAX_RICHES           1000

#define MAP_DEFAULT_MOUNTAINS    30
#define MAP_MIN_MOUNTAINS        10
#define MAP_MAX_MOUNTAINS        100

#define MAP_DEFAULT_GRASS       35
#define MAP_MIN_GRASS           20
#define MAP_MAX_GRASS           100

#define MAP_DEFAULT_SWAMPS       5
#define MAP_MIN_SWAMPS           0
#define MAP_MAX_SWAMPS           100

#define MAP_DEFAULT_DESERTS      5
#define MAP_MIN_DESERTS          0
#define MAP_MAX_DESERTS          100

#define MAP_DEFAULT_RIVERS       50
#define MAP_MIN_RIVERS           0
#define MAP_MAX_RIVERS           1000

#define MAP_DEFAULT_FORESTS      20
#define MAP_MIN_FORESTS          0
#define MAP_MAX_FORESTS          100

#define MAP_DEFAULT_GENERATOR    1
#define MAP_MIN_GENERATOR        1
#define MAP_MAX_GENERATOR        4

#define MAP_DEFAULT_TINYISLES    0
#define MAP_MIN_TINYISLES        0
#define MAP_MAX_TINYISLES        1

#define MAP_DEFAULT_SEPARATE_POLES   1
#define MAP_MIN_SEPARATE_POLES       0
#define MAP_MAX_SEPARATE_POLES       1

#endif  /* FC__MAP_H */
