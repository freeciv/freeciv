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
  S_SPECIAL_1,
  S_ROAD,
  S_IRRIGATION,
  S_RAILROAD,
  S_MINE,
  S_POLLUTION,
  S_HUT,
  S_FORTRESS,
  S_SPECIAL_2,
  S_RIVER,
  S_FARMLAND,
  S_AIRBASE,
  S_FALLOUT,
  S_LAST
};

/* S_LAST-terminated */
extern enum tile_special_type infrastructure_specials[];

BV_DEFINE(bv_special, S_LAST);

#define T_NONE (-3) /* A special flag meaning no terrain type. */
#define T_ANY (-2) /* A special flag that matches "any" terrain type. */
#define T_UNKNOWN (-1) /* An unknown terrain. */

/* The first terrain value and number of base terrains.  This is used in
 * loops.  T_COUNT may eventually be turned into a variable. */
#define T_FIRST 0
#define T_COUNT (game.control.terrain_count)

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

enum mapgen_terrain_property {
  MG_MOUNTAINOUS,
  MG_GREEN,
  MG_FOLIAGE,

  MG_TROPICAL,
  MG_TEMPERATE,
  MG_COLD,
  MG_FROZEN,

  MG_WET,
  MG_DRY,

  MG_OCEAN_DEPTH,

  MG_LAST
};

/*
 * This struct gives data about each terrain type.  There are many ways
 * it could be extended.
 */
struct tile_type {
  int index;
  const char *terrain_name; /* Translated string - doesn't need freeing. */
  char terrain_name_orig[MAX_LEN_NAME];	/* untranslated copy */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  /* Server-only. */
  char identifier; /* Single-character identifier used in savegames. */
#define UNKNOWN_TERRAIN_IDENTIFIER 'u'

  int movement_cost;
  int defense_bonus; /* % defense bonus - defaults to zero */

  int output[O_MAX];

#define MAX_NUM_SPECIALS 2
  struct {
    const char *name; /* Translated string - doesn't need freeing. */
    char name_orig[MAX_LEN_NAME];
    int output[O_MAX];
    char graphic_str[MAX_LEN_NAME];
    char graphic_alt[MAX_LEN_NAME];
  } special[MAX_NUM_SPECIALS];

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

  /* These are special properties of the terrain used by mapgen.  Generally
   * for each property, if a tile is deemed to have that property then
   * the value gives the wieghted amount of tiles that will be assigned
   * this terrain.
   *
   * For instance if mountains have 70 and hills have 30 of MG_MOUNTAINOUS
   * then 70% of 'mountainous' tiles will be given mountains. */
  int property[MG_LAST];

  bv_terrain_flags flags;

  char *helptext;
};

extern struct tile_type tile_types[MAX_NUM_TERRAINS];

/* Terrain allocation functions. */
void terrains_init(void);

/* General terrain accessor functions. */
struct tile_type *get_tile_type(Terrain_type_id type);
Terrain_type_id get_terrain_by_name(const char * name);
const char *get_terrain_name(Terrain_type_id type);
enum terrain_flag_id terrain_flag_from_str(const char *s);
#define terrain_has_flag(terr, flag) BV_ISSET(tile_types[(terr)].flags, flag)
Terrain_type_id get_flag_terrain(enum terrain_flag_id flag);
void tile_types_free(void);

/* Functions to operate on a general terrain type. */
bool is_terrain_near_tile(const struct tile *ptile, Terrain_type_id t);
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    Terrain_type_id t);
int count_terrain_property_near_tile(const struct tile *ptile,
				     bool cardinal_only, bool percentage,
				     enum mapgen_terrain_property prop);

/* General special accessor functions. */
enum tile_special_type get_special_by_name(const char * name);
const char *get_special_name(enum tile_special_type type);
void set_special(bv_special *set, enum tile_special_type to_set);
void clear_special(bv_special *set, enum tile_special_type to_clear);
void clear_all_specials(bv_special *set);
bool contains_special(bv_special all,
		      enum tile_special_type to_test_for);

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

/* Special helper functions */
const char *get_infrastructure_text(bv_special pset);
enum tile_special_type get_infrastructure_prereq(enum tile_special_type spe);
enum tile_special_type get_preferred_pillage(bv_special pset);

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
