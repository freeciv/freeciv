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
#include "unit.h"

enum terrain_river_type {
  R_AS_TERRAIN=1, R_AS_SPECIAL=2
};

enum special_river_move {
  RMV_NORMAL=0, RMV_FAST_STRICT=1, RMV_FAST_RELAXED=2, RMV_FAST_ALWAYS=3
};

enum tile_special_type {
  S_NO_SPECIAL=0, S_SPECIAL_1=1, S_ROAD=2, S_IRRIGATION=4, S_RAILROAD=8,
  S_MINE=16, S_POLLUTION=32, S_HUT=64, S_FORTRESS=128, S_SPECIAL_2=256,
  S_RIVER=512, S_FARMLAND=1024
};

enum tile_terrain_type {
  T_ARCTIC, T_DESERT, T_FOREST, T_GRASSLAND, T_HILLS, T_JUNGLE, 
  T_MOUNTAINS, T_OCEAN, T_PLAINS, T_RIVER, T_SWAMP, T_TUNDRA, T_UNKNOWN,
  T_LAST
};
#define T_FIRST (T_ARCTIC)
#define T_COUNT (T_UNKNOWN)

enum known_type {
 TILE_UNKNOWN, TILE_KNOWN_NODRAW, TILE_KNOWN
};

struct map_position {
  int x,y;
};

struct tile {
  enum tile_terrain_type terrain;
  enum tile_special_type special;
  struct city *city;
  struct unit_list units;
  unsigned short known;
  short assigned; /* these can save a lot of CPU usage -- Syela */
  struct city *worked;      /* city working tile, or NULL if none */
  unsigned char continent;
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
  enum special_river_move river_move_mode;
  int river_defense_bonus;             /* % added to defense if Civ2 river */
  int river_trade_incr;                /* added to trade if Civ2 river */
  int fortress_defense_bonus;          /* % added to defense if fortress */
  int road_superhighway_trade_bonus;   /* % added to trade if road/s-highway */
  int rail_food_bonus;                 /* % added to food if railroad */
  int rail_shield_bonus;               /* % added to shield if railroad */
  int rail_trade_bonus;                /* % added to trade if railroad */
  int farmland_supermarket_food_bonus; /* % added to food if farm/s-market */
  int pollution_food_penalty;          /* % subtr. from food if polluted */
  int pollution_shield_penalty;        /* % subtr. from shield if polluted */
  int pollution_trade_penalty;         /* % subtr. from trade if polluted */
  /* graphics */
  int border_base;    /* NO_SUCH_GRAPHIC (999) means no such tiles */
  int corner_base;    /* NO_SUCH_GRAPHIC (999) means no such tiles */
  int river_base;     /* NO_SUCH_GRAPHIC (999) means no such tiles */
  int outlet_base;    /* NO_SUCH_GRAPHIC (999) means no such tiles */
  int denmark_base;   /* NO_SUCH_GRAPHIC (999) means no such tiles */
};

#define NO_SUCH_GRAPHIC (999)

/****************************************************************
tile_type for each terrain type
expand with government bonuses??
*****************************************************************/

struct tile_type {
  char terrain_name[MAX_LEN_NAME];     /* "" if unused */
  int graphic_base;
  int graphic_count;

  int movement_cost;
  int defense_bonus;

  int food;
  int shield;
  int trade;

  char special_1_name[MAX_LEN_NAME];   /* "" if none */
  int graphic_special_1;
  int food_special_1;
  int shield_special_1;
  int trade_special_1;

  char special_2_name[MAX_LEN_NAME];   /* "" if none */
  int graphic_special_2;
  int food_special_2;
  int shield_special_2;
  int trade_special_2;

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
  struct tile *tiles;
  struct map_position start_positions[R_LAST];
};

int map_is_empty(void);
void map_init(void);
void map_allocate(void);

char *map_get_tile_info_text(int x, int y);
struct tile *map_get_tile(int x, int y);

int map_distance(int x0, int y0, int x1, int y1);
int real_map_distance(int x0, int y0, int x1, int y1);
int sq_map_distance(int x0, int y0, int x1, int y1);
int same_pos(int x1, int y1, int x2, int y2);

void map_set_continent(int x, int y, int val);
char map_get_continent(int x, int y);
int map_same_continent(int x1, int y1, int x2, int y2);

void initialize_move_costs(void);
void reset_move_costs(int x, int y);

#define map_adjust_x(X) \
 (((X)<0) ?  (X)+map.xsize : (((X)>=map.xsize) ? (X)-map.xsize : (X)))
#define map_adjust_y(Y) \
  (((Y)<0) ? 0 : (((Y)>=map.ysize) ? map.ysize-1 : (Y)))

#define map_inx(x,y) \
  ((x)+(y)*map.xsize)

void map_calc_adjacent_xy(int x, int y, int *xx, int *yy);
void map_calc_adjacent_xy_void(int x, int y, int *xx, int *yy);

struct city *map_get_city(int x, int y);
void map_set_city(int x, int y, struct city *pcity);
enum tile_terrain_type map_get_terrain(int x, int y);
enum tile_special_type map_get_special(int x, int y);
void map_set_terrain(int x, int y, enum tile_terrain_type ter);
void map_set_special(int x, int y, enum tile_special_type spe);
void map_clear_special(int x, int y, enum tile_special_type spe);
void tile_init(struct tile *ptile);
enum known_type tile_is_known(int x, int y);
int map_get_known(int x, int y, struct player *pplayer);
void map_set_known(int x, int y, struct player *pplayer);
void map_clear_known(int x, int y, struct player *pplayer);
void map_know_all(struct player *pplayer);

int is_water_adjacent_to_tile(int x, int y);
int is_tiles_adjacent(int x0, int y0, int x1, int y1);
int tile_move_cost(struct unit *punit, int x1, int y1, int x2, int y2);
int map_move_cost(struct unit *punit, int x1, int y1);
struct tile_type *get_tile_type(enum tile_terrain_type type);
enum tile_terrain_type get_terrain_by_name(char * name);
int is_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int isnt_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int count_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int is_special_near_tile(int x, int y, enum tile_special_type spe);
int count_special_near_tile(int x, int y, enum tile_special_type spe);
int is_coastline(int x,int y);
int terrain_is_clean(int x, int y);
int is_at_coast(int x, int y);
int is_water_adjacent(int x, int y);
int is_hut_close(int x, int y);
int is_starter_close(int x, int y, int nr, int dist); 
int is_good_tile(int x, int y);
int is_special_close(int x, int y);
int is_special_type_close(int x, int y, enum tile_special_type spe);
int is_sea_usable(int x, int y);
int get_tile_food_base(struct tile * ptile);
int get_tile_shield_base(struct tile * ptile);
int get_tile_trade_base(struct tile * ptile);

void map_irrigate_tile(int x, int y);
void map_mine_tile(int x, int y);
void map_transform_tile(int x, int y);

int map_build_road_time(int x, int y);
int map_build_irrigation_time(int x, int y);
int map_build_mine_time(int x, int y);
int map_transform_time(int x, int y);

extern struct civ_map map;

extern struct terrain_misc terrain_control;
extern struct tile_type tile_types[T_LAST];

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
