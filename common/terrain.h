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
#ifndef FC__TERRAIN_H
#define FC__TERRAIN_H

#include "shared.h"

enum special_river_move {
  RMV_NORMAL=0, RMV_FAST_STRICT=1, RMV_FAST_RELAXED=2, RMV_FAST_ALWAYS=3
};

enum tile_special_type {
  S_NO_SPECIAL =    0,
  S_SPECIAL_1  =    1,
  S_ROAD       =    2,
  S_IRRIGATION =    4,
  S_RAILROAD   =    8,
  S_MINE       =   16,
  S_POLLUTION  =   32,
  S_HUT        =   64,
  S_FORTRESS   =  128,
  S_SPECIAL_2  =  256,
  S_RIVER      =  512,
  S_FARMLAND   = 1024,
  S_AIRBASE    = 2048,
  S_FALLOUT    = 4096
};

#define S_ALL    \
 (  S_SPECIAL_1  \
  | S_ROAD       \
  | S_IRRIGATION \
  | S_RAILROAD   \
  | S_MINE       \
  | S_POLLUTION  \
  | S_HUT        \
  | S_FORTRESS   \
  | S_SPECIAL_2  \
  | S_RIVER      \
  | S_FARMLAND   \
  | S_AIRBASE    \
  | S_FALLOUT)

#define S_INFRASTRUCTURE_MASK \
  (S_ROAD                   \
   | S_RAILROAD             \
   | S_IRRIGATION           \
   | S_FARMLAND             \
   | S_MINE                 \
   | S_FORTRESS             \
   | S_AIRBASE)

/* Changing this breaks savegame and network compatability. */
enum tile_terrain_type {
  T_ARCTIC, T_DESERT, T_FOREST, T_GRASSLAND, T_HILLS, T_JUNGLE, 
  T_MOUNTAINS, T_OCEAN, T_PLAINS, T_UNUSED, T_SWAMP, T_TUNDRA, T_UNKNOWN,
  T_LAST, /* last terrain type */
  T_ANY   /* A special flag that matches "any" terrain type. */
};
#define T_FIRST (T_ARCTIC)
#define T_COUNT (T_UNKNOWN)
#define MAX_NUM_TERRAINS (T_LAST)

/* Must match with terrain_flag_from_str in terrain.c. */
enum terrain_flag_id {
  TER_NO_BARBS, /* No barbarians summoned on this terrain. */
  TER_NO_POLLUTION, /* This terrain cannot be polluted. */
  TER_STARTER, /* Players will start on this terrain type. */
  TER_CAN_HAVE_RIVER, /* Terrains with this type can have S_RIVER on them. */
  TER_UNSAFE_COAST,/*this tile is not safe as coast, (all ocean / ice) */ 
  TER_UNSAFE,  /*unsafe for all units (ice,...) */
  TER_OCEANIC, /* This is an ocean terrain. */
  TER_LAST
};
#define TER_FIRST (TER_NO_BARBS)
#define TER_COUNT (TER_LAST)
#define TER_MAX 64 /* Changing this breaks network compatability. */

enum known_type {
 TILE_UNKNOWN, TILE_KNOWN_FOGGED, TILE_KNOWN
};

BV_DEFINE(bv_terrain_flags, TER_MAX);

extern struct tile_type tile_types[T_LAST];

/* General accessor functions. */
struct tile_type *get_tile_type(enum tile_terrain_type type);
enum tile_terrain_type get_terrain_by_name(const char * name);
const char *get_terrain_name(enum tile_terrain_type type);
enum terrain_flag_id terrain_flag_from_str(const char *s);
#define terrain_has_flag(terr, flag) BV_ISSET(tile_types[(terr)].flags, flag)
enum tile_terrain_type get_flag_terrain(enum terrain_flag_id flag);
void tile_types_free(void);

/* Functions to operate on a general terrain type. */
bool is_terrain_near_tile(int map_x, int map_y, enum tile_terrain_type t);
int count_terrain_near_tile(int map_x, int map_y, enum tile_terrain_type t);
int adjacent_terrain_tiles4(int map_x, int map_y, enum tile_terrain_type t);

/* Functions to operate on a terrain flag. */
bool is_terrain_flag_near_tile(int x, int y, enum terrain_flag_id flag);
int count_terrain_flag_near_tile(int x, int y, enum terrain_flag_id flag);
int adjacent_terrain_flag_tiles4(int x, int y, enum terrain_flag_id flag);

/* Terrain-specific functions. */
#define is_ocean(x) (terrain_has_flag((x), TER_OCEANIC))
#define is_ocean_near_tile(x, y) is_terrain_flag_near_tile(x, y, TER_OCEANIC)
#define adjacent_ocean_tiles4(x, y) adjacent_terrain_tiles4(x, y, T_OCEAN)
#define count_ocean_near_tile(x,y) count_terrain_near_tile(x,y, T_OCEAN)

/* This iterator iterates over all terrain types. */
#define terrain_type_iterate(id)                                            \
{                                                                           \
  enum tile_terrain_type id;                                                \
  for (id = T_FIRST; id < T_COUNT; id++) {

#define terrain_type_iterate_end                                            \
  }                                                                         \
}

#endif  /* FC__TERRAIN_H */
