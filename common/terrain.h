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

#include "unittype.h"

struct base_type;

enum special_river_move {
  RMV_NORMAL = 0,
  RMV_FAST_STRICT = 1,
  RMV_FAST_RELAXED = 2,
  RMV_FAST_ALWAYS = 3,
};

enum tile_special_type {
  S_ROAD,
  S_IRRIGATION,
  S_RAILROAD,
  S_MINE,
  S_POLLUTION,
  S_HUT,
  S_FORTRESS,
  S_RIVER,
  S_FARMLAND,
  S_AIRBASE,
  S_FALLOUT,
  S_LAST
};

/* Special value for pillaging bases */
#define S_PILLAGE_BASE (S_LAST + 1)

/* S_LAST-terminated */
extern enum tile_special_type infrastructure_specials[];

BV_DEFINE(bv_special, S_LAST);

/* currently only used in edithand.c */
#define tile_special_type_iterate(special)				    \
{									    \
  enum tile_special_type special;					    \
									    \
  for (special = 0; special < S_LAST; special++) {

#define tile_special_type_iterate_end					    \
  }									    \
}

/* === */

struct resource {
  int item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  char identifier; /* Single-character identifier used in savegames. */
#define RESOURCE_NULL_IDENTIFIER ' '

  int output[O_MAX]; /* Amount added by this resource. */
};

/* === */

#define T_NONE (NULL) /* A special flag meaning no terrain type. */
#define T_UNKNOWN (NULL) /* An unknown terrain. */

/* The first terrain value. */
#define T_FIRST 0

/* A hard limit on the number of terrains; useful for static arrays. */
#define MAX_NUM_TERRAINS  MAX_NUM_ITEMS
#define MAX_NUM_RESOURCES 100 /* Limited to half a byte in the net code. */

enum terrain_class {
  TC_LAND,
  TC_OCEAN,
  TC_LAST
};

/* Must match with find_terrain_flag_by_rule_name in terrain.c. */
enum terrain_flag_id {
  TER_NO_BARBS, /* No barbarians summoned on this terrain. */
  TER_NO_POLLUTION, /* This terrain cannot be polluted. */
  TER_NO_CITIES, /* No cities on this terrain. */
  TER_STARTER, /* Players will start on this terrain type. */
  TER_CAN_HAVE_RIVER, /* Terrains with this type can have S_RIVER on them. */
  TER_UNSAFE_COAST,/*this tile is not safe as coast, (all ocean / ice) */ 
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
struct terrain {
  int item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];	/* add tile_ prefix */
  char graphic_alt[MAX_LEN_NAME];	/* TODO: retire, never used! */

  char identifier; /* Single-character identifier used in savegames. */
#define TERRAIN_WATER_IDENTIFIER ' '
#define TERRAIN_LAKE_IDENTIFIER '+'
#define TERRAIN_SEA_IDENTIFIER '-'
#define TERRAIN_COAST_IDENTIFIER '.'
#define TERRAIN_SHELF_IDENTIFIER ','
#define TERRAIN_FLOOR_IDENTIFIER ':'
#define TERRAIN_RIDGE_IDENTIFIER '^'
#define TERRAIN_GLACIER_IDENTIFIER 'a'
#define TERRAIN_UNKNOWN_IDENTIFIER 'u'

  int movement_cost;
  int defense_bonus; /* % defense bonus - defaults to zero */

  int output[O_MAX];

  struct resource **resources; /* NULL-terminated */

  int road_trade_incr;
  int road_time;

  struct terrain *irrigation_result;
  int irrigation_food_incr;
  int irrigation_time;

  struct terrain *mining_result;
  int mining_shield_incr;
  int mining_time;

  struct terrain *transform_result;
  int transform_time;
  int rail_time;
  int airbase_time;
  int fortress_time;
  int clean_pollution_time;
  int clean_fallout_time;

  /* May be NULL if the transformation is impossible. */
  struct terrain *warmer_wetter_result, *warmer_drier_result;
  struct terrain *cooler_wetter_result, *cooler_drier_result;

  /* These are special properties of the terrain used by mapgen.  If a tile
   * has a property, then the value gives the weighted amount of tiles that
   * will be assigned this terrain.
   *
   * For instance if mountains have 70 and hills have 30 of MG_MOUNTAINOUS
   * then 70% of 'mountainous' tiles will be given mountains.
   *
   * Ocean_depth is different.  Instead of a percentage, the depth of the
   * tile in the range 0 (never chosen) to 100 (deepest) is used.
   */
  int property[MG_LAST];
#define TERRAIN_OCEAN_DEPTH_MINIMUM (1)
#define TERRAIN_OCEAN_DEPTH_MAXIMUM (100)

  bv_unit_classes native_to;

  bv_terrain_flags flags;

  char *helptext;
};

/* General terrain accessor functions. */
Terrain_type_id terrain_count(void);
Terrain_type_id terrain_index(const struct terrain *pterrain);
Terrain_type_id terrain_number(const struct terrain *pterrain);

struct terrain *terrain_by_number(const Terrain_type_id type);

struct terrain *find_terrain_by_identifier(const char identifier);
struct terrain *find_terrain_by_rule_name(const char *name);
struct terrain *find_terrain_by_translated_name(const char *name);

char terrain_identifier(const struct terrain *pterrain);
const char *terrain_rule_name(const struct terrain *pterrain);
const char *terrain_name_translation(struct terrain *pterrain);

/* Functions to operate on a terrain flag. */
enum terrain_flag_id find_terrain_flag_by_rule_name(const char *s);
#define terrain_has_flag(terr, flag) BV_ISSET((terr)->flags, flag)

bool is_terrain_flag_near_tile(const struct tile *ptile,
			       enum terrain_flag_id flag);
int count_terrain_flag_near_tile(const struct tile *ptile,
				 bool cardinal_only, bool percentage,
				 enum terrain_flag_id flag);

/* Terrain-specific functions. */
#define is_ocean(pterrain) ((pterrain) != T_UNKNOWN			\
			    && terrain_has_flag((pterrain), TER_OCEANIC))
#define is_ocean_near_tile(ptile) \
  is_terrain_flag_near_tile(ptile, TER_OCEANIC)
#define count_ocean_near_tile(ptile, cardinal_only, percentage)		\
  count_terrain_flag_near_tile(ptile, cardinal_only, percentage, TER_OCEANIC)

/* Functions to operate on a general terrain type. */
bool is_terrain_near_tile(const struct tile *ptile,
			  const struct terrain *pterrain);
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    const struct terrain *pterrain);
int count_terrain_property_near_tile(const struct tile *ptile,
				     bool cardinal_only, bool percentage,
				     enum mapgen_terrain_property prop);

/* General resource accessor functions. */
Resource_type_id resource_count(void);
Resource_type_id resource_index(const struct resource *presource);
Resource_type_id resource_number(const struct resource *presource);

struct resource *resource_by_number(const Resource_type_id id);
struct resource *find_resource_by_rule_name(const char *name);

const char *resource_rule_name(const struct resource *presource);
const char *resource_name_translation(struct resource *presource);

/* General special accessor functions. */
enum tile_special_type find_special_by_rule_name(const char *name);
const char *special_rule_name(enum tile_special_type type);
const char *special_name_translation(enum tile_special_type type);

void set_special(bv_special *set, enum tile_special_type to_set);
void clear_special(bv_special *set, enum tile_special_type to_clear);
void clear_all_specials(bv_special *set);
bool contains_special(bv_special all,
		      enum tile_special_type to_test_for);
bool contains_any_specials(bv_special all);

/* Special helper functions */
const char *get_infrastructure_text(bv_special pset);
enum tile_special_type get_infrastructure_prereq(enum tile_special_type spe);
enum tile_special_type get_preferred_pillage(bv_special pset,
                                             struct base_type *pbase);

/* Functions to operate on a terrain special. */
bool is_special_near_tile(const struct tile *ptile,
			  enum tile_special_type spe);
int count_special_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    enum tile_special_type spe);

/* Functions to operate on a terrain class. */
enum terrain_class find_terrain_class_by_rule_name(const char *name);
const char *terrain_class_name_translation(enum terrain_class tclass);

bool terrain_belongs_to_class(const struct terrain *pterrain,
                              enum terrain_class tclass);
bool is_terrain_class_near_tile(const struct tile *ptile, enum terrain_class tclass);

/* Initialization and iteration */
struct resource *resource_array_first(void);
const struct resource *resource_array_last(void);

#define resource_type_iterate(_p)					\
{									\
   struct resource *_p = resource_array_first();			\
  if (NULL != _p) {							\
    for (; _p <= resource_array_last(); _p++) {

#define resource_type_iterate_end					\
    }									\
  }									\
}

/* Initialization and iteration */
void terrains_init(void);
void terrains_free(void);

struct terrain *terrain_array_first(void);
const struct terrain *terrain_array_last(void);

#define terrain_type_iterate(_p)					\
{									\
  struct terrain *_p = terrain_array_first();				\
  if (NULL != _p) {							\
    for (; _p <= terrain_array_last(); _p++) {

#define terrain_type_iterate_end					\
    }									\
  }									\
}

#endif  /* FC__TERRAIN_H */
