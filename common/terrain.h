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


enum special_river_move {
  RMV_NORMAL=0, RMV_FAST_STRICT=1, RMV_FAST_RELAXED=2, RMV_FAST_ALWAYS=3
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

enum terrain_class { TC_LAND, TC_OCEAN, TC_LAST };

/* S_LAST-terminated */
extern enum tile_special_type infrastructure_specials[];

BV_DEFINE(bv_special, S_LAST);

#define specials_iterate(special)					    \
{									    \
  enum tile_special_type special;					    \
									    \
  for (special = 0; special < S_LAST; special++) {

#define specials_iterate_end						    \
  }									    \
}


#define T_NONE (NULL) /* A special flag meaning no terrain type. */
#define T_UNKNOWN (NULL) /* An unknown terrain. */

/* The first terrain value and number of base terrains.  This is used in
 * loops.  T_COUNT may eventually be turned into a variable. */
#define T_FIRST 0
#define T_COUNT (game.control.terrain_count)

/* A hard limit on the number of terrains; useful for static arrays. */
#define MAX_NUM_TERRAINS  MAX_NUM_ITEMS
#define MAX_NUM_RESOURCES 100 /* Limited to half a byte in the net code. */

/* Must match with terrain_flag_from_str in terrain.c. */
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
  int index;
  const char *name; /* Translated string - doesn't need freeing. */
  char name_orig[MAX_LEN_NAME];	/* untranslated copy */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  /* Server-only. */
  char identifier; /* Single-character identifier used in savegames. */
#define UNKNOWN_TERRAIN_IDENTIFIER 'u'

  int movement_cost;
  int defense_bonus; /* % defense bonus - defaults to zero */

  int output[O_MAX];

  const struct resource **resources; /* NULL-terminated */

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

  /* These are special properties of the terrain used by mapgen.  Generally
   * for each property, if a tile is deemed to have that property then
   * the value gives the wieghted amount of tiles that will be assigned
   * this terrain.
   *
   * For instance if mountains have 70 and hills have 30 of MG_MOUNTAINOUS
   * then 70% of 'mountainous' tiles will be given mountains. */
  int property[MG_LAST];

  bv_unit_classes native_to;

  bv_terrain_flags flags;

  char *helptext;
};

struct resource {
  int index;
  const char *name; /* Translated string - doesn't need freeing. */
  char name_orig[MAX_LEN_NAME];
  char identifier; /* server-only, same as terrain->identifier */
  int output[O_MAX]; /* Amount added by this resource. */
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
};

#define RESOURCE_NULL_IDENTIFIER ' '

/* The first resource value and number of base resources.  This is used in
 * loops.  R_COUNT may eventually be turned into a variable. */
#define R_FIRST 0
#define R_COUNT (game.control.resource_count)

/* Terrain allocation functions. */
void terrains_init(void);

/* General terrain accessor functions. */
struct terrain *get_terrain(Terrain_type_id type);
struct terrain *get_terrain_by_name(const char *name);
const char *get_name(const struct terrain *pterrain);
enum terrain_flag_id terrain_flag_from_str(const char *s);
#define terrain_has_flag(terr, flag) BV_ISSET((terr)->flags, flag)
struct terrain *get_flag_terrain(enum terrain_flag_id flag);
void terrains_free(void);

struct resource *get_resource(Resource_type_id id);
struct resource *get_resource_by_name_orig(const char *name);

/* Functions to operate on a general terrain type. */
bool is_terrain_near_tile(const struct tile *ptile,
			  const struct terrain *pterrain);
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    const struct terrain *pterrain);
int count_terrain_property_near_tile(const struct tile *ptile,
				     bool cardinal_only, bool percentage,
				     enum mapgen_terrain_property prop);

/* General special accessor functions. */
enum tile_special_type get_special_by_name_orig(const char * name);
const char *get_special_name(enum tile_special_type type);
const char *get_special_name_orig(enum tile_special_type type);
void set_special(bv_special *set, enum tile_special_type to_set);
void clear_special(bv_special *set, enum tile_special_type to_clear);
void clear_all_specials(bv_special *set);
bool contains_special(bv_special all,
		      enum tile_special_type to_test_for);
bool contains_any_specials(bv_special all);

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

/* Functions to operate on a terrain class. */
bool terrain_belongs_to_class(const struct terrain *pterrain,
                              enum terrain_class class);
bool is_terrain_class_near_tile(const struct tile *ptile, enum terrain_class class);
enum terrain_class get_terrain_class_by_name(const char *name);
const char *terrain_class_name(enum terrain_class class);

/* Special helper functions */
const char *get_infrastructure_text(bv_special pset);
enum tile_special_type get_infrastructure_prereq(enum tile_special_type spe);
enum tile_special_type get_preferred_pillage(bv_special pset);

/* Terrain-specific functions. */
#define is_ocean(pterrain) ((pterrain) != T_UNKNOWN			    \
			    && terrain_has_flag((pterrain), TER_OCEANIC))
#define is_ocean_near_tile(ptile) \
  is_terrain_flag_near_tile(ptile, TER_OCEANIC)
#define count_ocean_near_tile(ptile, cardinal_only, percentage)		\
  count_terrain_flag_near_tile(ptile, cardinal_only, percentage, TER_OCEANIC)

/* This iterator iterates over all terrain types. */
#define terrain_type_iterate(pterrain)					    \
{                                                                           \
  Terrain_type_id _index;						    \
									    \
  for (_index = T_FIRST; _index < T_COUNT; _index++) {			    \
    struct terrain *pterrain = get_terrain(_index);
    

#define terrain_type_iterate_end                                            \
  }                                                                         \
}

#define resource_type_iterate(presource)				    \
{                                                                           \
  Resource_type_id _index;						    \
									    \
  for (_index = R_FIRST; _index < R_COUNT; _index++) {			    \
    struct resource *presource = get_resource(_index);
    

#define resource_type_iterate_end                                           \
  }                                                                         \
}

#endif  /* FC__TERRAIN_H */
