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

#include "player.h"
#include "terrain.h"
#include "unit.h"
#include "game.h"

struct Sprite;			/* opaque; client-gui specific */

#define NUM_DIRECTION_NSEW 16

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

int xdist(int x0, int x1);
int ydist(int y0, int y1);
int map_distance(int x0, int y0, int x1, int y1);
int real_map_distance(int x0, int y0, int x1, int y1);
int sq_map_distance(int x0, int y0, int x1, int y1);
int same_pos(int x1, int y1, int x2, int y2);
int get_direction_for_step(int start_x, int start_y, int end_x, int end_y);

void map_set_continent(int x, int y, int val);
signed short map_get_continent(int x, int y);
int map_same_continent(int x1, int y1, int x2, int y2);

void initialize_move_costs(void);
void reset_move_costs(int x, int y);

#define map_adjust_x(X)            \
  ((X) < 0                         \
   ? ((X) % map.xsize != 0 ? (X) % map.xsize + map.xsize : 0) \
   : ((X) >= map.xsize             \
      ? (X) % map.xsize            \
      : (X)))

#define map_adjust_y(Y) \
  (((Y)<0) ? 0 : (((Y)>=map.ysize) ? map.ysize-1 : (Y)))

#define map_inx(x,y) \
  ((x)+(y)*map.xsize)

struct city *map_get_city(int x, int y);
void map_set_city(int x, int y, struct city *pcity);
enum tile_terrain_type map_get_terrain(int x, int y);
enum tile_special_type map_get_special(int x, int y);
void map_set_terrain(int x, int y, enum tile_terrain_type ter);
void map_set_special(int x, int y, enum tile_special_type spe);
void map_clear_special(int x, int y, enum tile_special_type spe);
void tile_init(struct tile *ptile);
enum known_type tile_is_known(int x, int y);
int check_coords(int *x, int *y);
int is_real_tile(int x, int y);
int normalize_map_pos(int *x, int *y);
void nearest_real_pos(int *x, int *y);

void rand_neighbour(int x0, int y0, int *x, int *y);

int is_water_adjacent_to_tile(int x, int y);
int is_tiles_adjacent(int x0, int y0, int x1, int y1);
int is_move_cardinal(int x0, int y0, int x1, int y1);
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

/* Iterate through all tiles in a square with given center and radius.
   Positions returned will have adjusted x, and positions with illegal y will be
   automatically discarded.
 */
#define square_iterate(SI_center_x, SI_center_y, radius, SI_x_itr, SI_y_itr)  \
{                                                                             \
  int SI_x_itr, SI_y_itr;                                                     \
  int SI_x_itr1, SI_y_itr1;                                                   \
  for (SI_y_itr1 = (SI_center_y) - (radius);                                  \
       SI_y_itr1 <= (SI_center_y) + (radius); SI_y_itr1++) {                  \
    for (SI_x_itr1 = (SI_center_x) - (radius);                                \
	 SI_x_itr1 <= (SI_center_x) + (radius); SI_x_itr1++) {                \
      SI_x_itr = SI_x_itr1;                                                   \
      SI_y_itr = SI_y_itr1;                                                   \
      if (!normalize_map_pos(&SI_x_itr, &SI_y_itr)) continue;

#define square_iterate_end                                                    \
    }                                                                         \
  }                                                                           \
}

/* Iterate through all tiles adjacent to a tile */
#define adjc_iterate(RI_center_x, RI_center_y, RI_x_itr, RI_y_itr)            \
{                                                                             \
  int RI_x_itr, RI_y_itr;                                                     \
  int RI_x_itr1, RI_y_itr1;                                                   \
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
  assert(0 <= MACRO_center_y && MACRO_center_y < map.ysize                    \
         && 0 <= MACRO_center_x && MACRO_center_x < map.xsize);               \
  MACRO_border = (MACRO_center_y == 0                                         \
                  || MACRO_center_x == 0                                      \
                  || MACRO_center_y == map.ysize-1                            \
                  || MACRO_center_x == map.xsize-1);                          \
  for (dir_itr = 0; dir_itr < 8; dir_itr++) {                                 \
    y_itr = MACRO_center_y + DIR_DY[dir_itr];                                 \
    x_itr = MACRO_center_x + DIR_DX[dir_itr];                                 \
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
    for (WMI_x_itr = 0; WMI_x_itr < map.xsize; WMI_x_itr++)

#define whole_map_iterate_end                                                 \
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
  for (IAC_i = 0; IAC_i < 4; IAC_i++) {                \
    switch (IAC_i) {                                   \
    case 0:                                            \
      IAC_x = x + 1;                                   \
      IAC_y = y;                                       \
      if (!normalize_map_pos(&IAC_x, &IAC_y))          \
	continue;                                      \
      break;                                           \
    case 1:                                            \
      IAC_x = x;                                       \
      IAC_y = y + 1;                                   \
      if (!normalize_map_pos(&IAC_x, &IAC_y))          \
	continue;                                      \
      break;                                           \
    case 2:                                            \
      IAC_x = x - 1;                                   \
      IAC_y = y;                                       \
      if (!normalize_map_pos(&IAC_x, &IAC_y))          \
	continue;                                      \
      break;                                           \
    case 3:                                            \
      IAC_x = x;                                       \
      IAC_y = y - 1;                                   \
      if (!normalize_map_pos(&IAC_x, &IAC_y))          \
	continue;                                      \
      break;                                           \
    default:                                           \
      abort();                                         \
    }

#define cartesian_adjacent_iterate_end                 \
  }                                                    \
}


/*
used to compute neighboring tiles:
using
x1 = x + DIR_DX2[dir];
y1 = y + DIR_DY2[dir];
will give you the tile as shown below.
-------
|7|0|1|
|-+-+-|
|6| |2|
|-+-+-|
|5|4|3|
-------
 */
extern const int DIR_DX2[8];
extern const int DIR_DY2[8];

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

#endif  /* FC__MAP_H */
