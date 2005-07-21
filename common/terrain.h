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

#include "fc_types.h"

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

#define T_NONE (-3) /* A special flag meaning no terrain type. */
#define T_ANY (-2) /* A special flag that matches "any" terrain type. */
#define T_UNKNOWN (-1) /* An unknown terrain. */

/* The first terrain value and number of base terrains.  This is used in
 * loops.  T_COUNT may eventually be turned into a variable. */
#define T_FIRST 0
#define T_COUNT (game.terrain_count)

/* A hard limit on the number of terrains; useful for static arrays. */
#define MAX_NUM_TERRAINS MAX_NUM_ITEMS

/* Must match with terrain_flag_from_str in terrain.c. */
enum terrain_flag_id {
  TER_NO_BARBS, /* No barbarians summoned on this terrain. */
  TER_NO_POLLUTION, /* This terrain cannot be polluted. */
  TER_NO_CITIES, /* No cities on this terrain. */
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

/* General accessor functions. */
struct tile_type *get_tile_type(Terrain_type_id type);
Terrain_type_id get_terrain_by_name(const char * name);
const char *get_terrain_name(Terrain_type_id type);
enum terrain_flag_id terrain_flag_from_str(const char *s);
#define terrain_has_flag(terr, flag)		\
  BV_ISSET(get_tile_type(terr)->flags, flag)
Terrain_type_id get_flag_terrain(enum terrain_flag_id flag);
void tile_types_free(void);

/* Functions to operate on a general terrain type. */
bool is_terrain_near_tile(const struct tile *ptile, Terrain_type_id t);
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    Terrain_type_id t);

/* Functions to operate on a terrain special. */
bool is_special_near_tile(const struct tile *ptile,
			  enum tile_special_type spe);
int count_special_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    enum tile_special_type spe);

/* Functions to operate on a terrain flag. */
bool is_terrain_flag_near_tile(const struct tile *ptile,
			       enum terrain_flag_id flag);
int count_terrain_flag_near_tile(const struct tile *ptile,
				 bool cardinal_only, bool percentage,
				 enum terrain_flag_id flag);

/* Terrain-specific functions. */
#define is_ocean(x) (terrain_has_flag((x), TER_OCEANIC))
#define is_ocean_near_tile(ptile) \
  is_terrain_flag_near_tile(ptile, TER_OCEANIC)
#define count_ocean_near_tile(ptile, cardinal_only, percentage)		\
  count_terrain_flag_near_tile(ptile, cardinal_only, percentage, TER_OCEANIC)

/* This iterator iterates over all terrain types. */
#define terrain_type_iterate(id)                                            \
{                                                                           \
  Terrain_type_id id;                                                \
  for (id = T_FIRST; id < T_COUNT; id++) {

#define terrain_type_iterate_end                                            \
  }                                                                         \
}

#endif  /* FC__TERRAIN_H */
