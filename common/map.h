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
#ifndef __MAP_H
#define __MAP_H

#include "player.h"
#include "genlist.h"
#include "unit.h"

enum tile_special_type {
  S_NONE=0, S_SPECIAL=1, S_ROAD=2, S_IRRIGATION=4, S_RAILROAD=8,
  S_MINE=16, S_POLLUTION=32, S_HUT=64, S_FORTRESS=128
};

enum tile_terrain_type {
  T_ARCTIC, T_DESERT, T_FOREST, T_GRASSLAND, T_HILLS, T_JUNGLE, 
  T_MOUNTAINS, T_OCEAN, T_PLAINS, T_RIVER, T_SWAMP, T_TUNDRA, T_UNKNOWN,
  T_LAST
};

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
  signed char worked; /* who defaults chars to unsigned?  Ugh. */
  unsigned char continent;
  signed char move_cost[8]; /* don't know if this helps! */
};


/****************************************************************
tile_type for each terrain type
expand with government bonuses??
*****************************************************************/
struct tile_type {
  char *terrain_name;
  char *special_name;

  int food;
  int shield;
  int trade;

  int food_special;
  int shield_special;
  int trade_special;
  
  int movement_cost;
  int defense_bonus;

  int road_time;
  
  enum tile_terrain_type irrigation_result;
  int irrigation_time;

  enum tile_terrain_type mining_result;
  int mining_time;
};

struct civ_map { 
  int xsize, ysize;
  int seed;
  int age;
  int riches;
  int is_earth;
  int huts;
  int landpercent;
  int swampsize;
  int deserts;
  int mountains;
  int riverlength;
  int forestsize;
  int generator;
  struct tile *tiles;
  struct map_position start_positions[R_LAST];
};

struct isledata {
  int x,y;                        /* upper left corner of the islands */
  int goodies;
  int starters;
};


char *map_get_tile_info_text(int x, int y);
void map_init(void);
int map_is_empty(void);
void map_fractal_create(void);
struct tile *map_get_tile(int x, int y);
int map_distance(int x0, int y0, int x1, int y1);
int real_map_distance(int x0, int y0, int x1, int y1);
int sq_map_distance(int x0, int y0, int x1, int y1);
void reset_move_costs(int x, int y);
void initialize_move_costs(void);

#define map_adjust_x(X) \
 (((X)<0) ?  (X)+map.xsize : (((X)>=map.xsize) ? (X)-map.xsize : (X)))
#define map_adjust_y(Y) \
  (((Y)<0) ? 0 : (((Y)>=map.ysize) ? map.ysize-1 : (Y)))

struct city *map_get_city(int x, int y);
void map_set_city(int x, int y, struct city *pcity);
enum tile_terrain_type map_get_terrain(int x, int y);
enum tile_special_type map_get_special(int x, int y);
void map_set_continent(int x, int y, int val);
char map_get_continent(int x, int y);
void map_set_terrain(int x, int y, enum tile_terrain_type ter);
void map_set_special(int x, int y, enum tile_special_type spe);
void map_clear_special(int x, int y, enum tile_special_type spe);
void tile_init(struct tile *ptile);
int map_get_known(int x, int y, struct player *pplayer);
enum known_type tile_is_known(int x, int y);
void map_set_known(int x, int y, struct player *pplayer);
void map_clear_known(int x, int y, struct player *pplayer);
void map_know_all(struct player *pplayer);

void send_full_tile_info(struct player *dest, int x, int y);
int is_water_adjacent_to_tile(int x, int y);
int is_tiles_adjacent(int x0, int y0, int x1, int y1);
int tile_move_cost(struct unit *punit, int x1, int y1, int x2, int y2);
int map_move_cost(struct unit *punit, int x1, int y1);
struct tile_type *get_tile_type(enum tile_terrain_type type);
int is_terrain_near_tile(int x, int y, enum tile_terrain_type t);
int is_coastline(int x,int y);
int terrain_is_clean(int x, int y);
int is_at_coast(int x, int y);
int is_water_adjacent(int x, int y);
int is_hut_close(int x, int y);
int same_island(int x1, int y1, int x2, int y2);
int is_starter_close(int x, int y, int nr, int dist); 
int is_good_tile(int x, int y);
int is_special_close(int x, int y);
void add_specials(int prob);
void reset_move_costs(int x, int y);

int same_pos(int x1, int y1, int x2, int y2);

void map_irrigate_tile(int x, int y);
void map_mine_tile(int x, int y);

extern struct civ_map map;
extern struct isledata islands[100];

int map_build_road_time(int x, int y);
int map_build_irrigation_time(int x, int y);
int map_build_mine_time(int x, int y);


#define MAP_DEFAULT_MOUNTAINS    40
#define MAP_MIN_MOUNTAINS        10
#define MAP_MAX_MOUNTAINS        100

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
#define MAP_MAX_SEED             50000

#define MAP_DEFAULT_LANDMASS     30
#define MAP_MIN_LANDMASS         20
#define MAP_MAX_LANDMASS         80

#define MAP_DEFAULT_RICHES       250
#define MAP_MIN_RICHES           0
#define MAP_MAX_RICHES           1000

#define MAP_DEFAULT_SWAMPS       10
#define MAP_MIN_SWAMPS           0
#define MAP_MAX_SWAMPS           100

#define MAP_DEFAULT_DESERTS      5
#define MAP_MIN_DESERTS          0
#define MAP_MAX_DESERTS          100

#define MAP_DEFAULT_RIVERS       50
#define MAP_MIN_RIVERS           0
#define MAP_MAX_RIVERS           1000

#define MAP_DEFAULT_FORESTS      25
#define MAP_MIN_FORESTS          0
#define MAP_MAX_FORESTS          100

#define MAP_DEFAULT_GENERATOR    1
#define MAP_MIN_GENERATOR        1
#define MAP_MAX_GENERATOR        2

#define GAME_DEFAULT_GOLD        50
#define GAME_MIN_GOLD            0
#define GAME_MAX_GOLD            5000

#define GAME_DEFAULT_SETTLERS    2
#define GAME_MIN_SETTLERS        1
#define GAME_MAX_SETTLERS        10

#define GAME_DEFAULT_EXPLORER    1
#define GAME_MIN_EXPLORER        0
#define GAME_MAX_EXPLORER        10

#define GAME_DEFAULT_TECHLEVEL   3
#define GAME_MIN_TECHLEVEL       0
#define GAME_MAX_TECHLEVEL       50

#define GAME_DEFAULT_UNHAPPYSIZE 4
#define GAME_MIN_UNHAPPYSIZE 1
#define GAME_MAX_UNHAPPYSIZE 6

#define GAME_DEFAULT_END_YEAR    2000
#define GAME_MIN_END_YEAR        -4000
#define GAME_MAX_END_YEAR        5000

#define GAME_DEFAULT_MIN_PLAYERS     1
#define GAME_MIN_MIN_PLAYERS         1
#define GAME_MAX_MIN_PLAYERS         MAX_PLAYERS

#define GAME_DEFAULT_MAX_PLAYERS     14
#define GAME_MIN_MAX_PLAYERS         1
#define GAME_MAX_MAX_PLAYERS         MAX_PLAYERS

#define GAME_DEFAULT_AIFILL          0
#define GAME_MIN_AIFILL              0
#define GAME_MAX_AIFILL              GAME_MAX_MAX_PLAYERS

#define GAME_DEFAULT_RESEARCHLEVEL   10
#define GAME_MIN_RESEARCHLEVEL       4
#define GAME_MAX_RESEARCHLEVEL       20

#define GAME_DEFAULT_DIPLCOST        100
#define GAME_MIN_DIPLCOST            0
#define GAME_MAX_DIPLCOST            100

#define GAME_DEFAULT_DIPLCHANCE      3
#define GAME_MIN_DIPLCHANCE          1
#define GAME_MAX_DIPLCHANCE          10

#define GAME_DEFAULT_FREECOST        0
#define GAME_MIN_FREECOST            0
#define GAME_MAX_FREECOST            100

#define GAME_DEFAULT_CONQUERCOST     0
#define GAME_MIN_CONQUERCOST         0
#define GAME_MAX_CONQUERCOST         100

#define GAME_DEFAULT_CITYFACTOR      14
#define GAME_MIN_CITYFACTOR          6
#define GAME_MAX_CITYFACTOR          100

#define GAME_DEFAULT_RAILFOOD        0
#define GAME_MIN_RAILFOOD            0
#define GAME_MAX_RAILFOOD            100

#define GAME_DEFAULT_RAILTRADE       0
#define GAME_MIN_RAILTRADE           0
#define GAME_MAX_RAILTRADE           100

#define GAME_DEFAULT_RAILPROD        50
#define GAME_MIN_RAILPROD            0
#define GAME_MAX_RAILPROD            100

#define GAME_DEFAULT_FOODBOX         10
#define GAME_MIN_FOODBOX             5
#define GAME_MAX_FOODBOX             30

#define GAME_DEFAULT_TECHPENALTY     100
#define GAME_MIN_TECHPENALTY         0
#define GAME_MAX_TECHPENALTY         100

#define GAME_DEFAULT_RAZECHANCE      20
#define GAME_MIN_RAZECHANCE          0
#define GAME_MAX_RAZECHANCE          100

#define GAME_DEFAULT_CIVSTYLE        2
#define GAME_MIN_CIVSTYLE            1
#define GAME_MAX_CIVSTYLE            2

#endif
