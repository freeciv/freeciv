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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"
#include "shared.h"

/* common */
#include "fc_types.h"
#include "name_translation.h"
#include "unittype.h"

struct base_type;
struct strvec;          /* Actually defined in "utility/string_vector.h". */
struct rgbcolor;

enum special_river_move {
  RMV_NORMAL = 0,
  RMV_FAST_STRICT = 1,
  RMV_FAST_RELAXED = 2,
  RMV_FAST_ALWAYS = 3,
};

/* S_LAST-terminated */
extern enum tile_special_type infrastructure_specials[];

BV_DEFINE(bv_special, S_LAST);

/* NB: This does not include S_FORTRESS and S_AIRBASE.
 * You must use base_type_iterate and related accessors
 * in base.h for those. */
#define tile_special_type_iterate(special)                                 \
{                                                                          \
  enum tile_special_type special = 0;                                      \
  for (; special < S_LAST; special++) {                                    \
    
#define tile_special_type_iterate_end                                      \
  }                                                                        \
}

/* === */

struct resource {
  int item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];

  char identifier; /* Single-character identifier used in savegames. */
#define RESOURCE_NULL_IDENTIFIER '\0'
#define RESOURCE_NONE_IDENTIFIER ' '

  int output[O_LAST]; /* Amount added by this resource. */
};

/* === */

#define T_NONE (NULL) /* A special flag meaning no terrain type. */
#define T_UNKNOWN (NULL) /* An unknown terrain. */

/* The first terrain value. */
#define T_FIRST 0

/* A hard limit on the number of terrains; useful for static arrays. */
#define MAX_NUM_TERRAINS (96)
/* Reflect reality; but theoretically could be larger than terrains! */
#define MAX_NUM_RESOURCES (MAX_NUM_TERRAINS/2)

#define SPECENUM_NAME terrain_class
#define SPECENUM_VALUE0 TC_LAND
/* TRANS: terrain class: used adjectivally */
#define SPECENUM_VALUE0NAME N_("Land")
#define SPECENUM_VALUE1 TC_OCEAN
/* TRANS: terrain class: used adjectivally */
#define SPECENUM_VALUE1NAME N_("Oceanic")
#include "specenum_gen.h"

/* Types of alterations available to terrain.
 * This enum is only used in the effects system; the relevant information
 * is encoded in other members of the terrain structure. */
#define SPECENUM_NAME terrain_alteration
/* Can build irrigation without changing terrain */
#define SPECENUM_VALUE0 TA_CAN_IRRIGATE
#define SPECENUM_VALUE0NAME "CanIrrigate"
/* Can build mine without changing terrain */
#define SPECENUM_VALUE1 TA_CAN_MINE
#define SPECENUM_VALUE1NAME "CanMine"
/* Can build roads and/or railroads */
#define SPECENUM_VALUE2 TA_CAN_ROAD
#define SPECENUM_VALUE2NAME "CanRoad"
#include "specenum_gen.h"

#define SPECENUM_NAME terrain_flag_id
/* No barbarians summoned on this terrain. */
#define SPECENUM_VALUE0 TER_NO_BARBS
#define SPECENUM_VALUE0NAME "NoBarbs"
/* This terrain cannot be polluted. */
#define SPECENUM_VALUE1 TER_NO_POLLUTION
#define SPECENUM_VALUE1NAME "NoPollution"
/* No cities on this terrain. */
#define SPECENUM_VALUE2 TER_NO_CITIES
#define SPECENUM_VALUE2NAME "NoCities"
/* Players will start on this terrain type. */
#define SPECENUM_VALUE3 TER_STARTER
#define SPECENUM_VALUE3NAME "Starter"
/* Terrains with this type can have S_RIVER on them. */
#define SPECENUM_VALUE4 TER_CAN_HAVE_RIVER
#define SPECENUM_VALUE4NAME "CanHaveRiver"
/*this tile is not safe as coast, (all ocean / ice) */
#define SPECENUM_VALUE5 TER_UNSAFE_COAST
#define SPECENUM_VALUE5NAME "UnsafeCoast"
/* Fresh water terrain */
#define SPECENUM_VALUE6 TER_FRESHWATER
#define SPECENUM_VALUE6NAME "FreshWater"
#include "specenum_gen.h"

#define TER_MAX 64 /* Changing this breaks network compatability. */

BV_DEFINE(bv_terrain_flags, TER_MAX);

#define SPECENUM_NAME mapgen_terrain_property
#define SPECENUM_VALUE0 MG_MOUNTAINOUS
#define SPECENUM_VALUE0NAME "mountainous"
#define SPECENUM_VALUE1 MG_GREEN
#define SPECENUM_VALUE1NAME "green"
#define SPECENUM_VALUE2 MG_FOLIAGE
#define SPECENUM_VALUE2NAME "foliage"
#define SPECENUM_VALUE3 MG_TROPICAL
#define SPECENUM_VALUE3NAME "tropical"
#define SPECENUM_VALUE4 MG_TEMPERATE
#define SPECENUM_VALUE4NAME "temperate"
#define SPECENUM_VALUE5 MG_COLD
#define SPECENUM_VALUE5NAME "cold"
#define SPECENUM_VALUE6 MG_FROZEN
#define SPECENUM_VALUE6NAME "frozen"
#define SPECENUM_VALUE7 MG_WET
#define SPECENUM_VALUE7NAME "wet"
#define SPECENUM_VALUE8 MG_DRY
#define SPECENUM_VALUE8NAME "dry"
#define SPECENUM_VALUE9 MG_OCEAN_DEPTH
#define SPECENUM_VALUE9NAME "ocean_depth"
#define SPECENUM_COUNT MG_COUNT
#include "specenum_gen.h"

/*
 * This struct gives data about each terrain type.  There are many ways
 * it could be extended.
 */
struct terrain {
  int item_number;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];	/* add tile_ prefix */
  char graphic_alt[MAX_LEN_NAME];

  char identifier; /* Single-character identifier used in savegames. */

#define TERRAIN_UNKNOWN_IDENTIFIER 'u'

  enum terrain_class tclass;

  int movement_cost;
  int defense_bonus; /* % defense bonus - defaults to zero */

  int output[O_LAST];

  struct resource **resources; /* NULL-terminated */

  int road_output_incr_pct[O_LAST];
  int base_time;
  int road_time;

  struct terrain *irrigation_result;
  int irrigation_food_incr;
  int irrigation_time;

  struct terrain *mining_result;
  int mining_shield_incr;
  int mining_time;

  struct terrain *transform_result;
  int transform_time;
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
  int property[MG_COUNT];
#define TERRAIN_OCEAN_DEPTH_MINIMUM (1)
#define TERRAIN_OCEAN_DEPTH_MAXIMUM (100)

  bv_unit_classes native_to;

  bv_terrain_flags flags;

  struct rgbcolor *rgb;

  struct strvec *helptext;
};

/* General terrain accessor functions. */
Terrain_type_id terrain_count(void);
Terrain_type_id terrain_index(const struct terrain *pterrain);
Terrain_type_id terrain_number(const struct terrain *pterrain);

struct terrain *terrain_by_number(const Terrain_type_id type);

struct terrain *terrain_by_identifier(const char identifier);
struct terrain *terrain_by_rule_name(const char *name);
struct terrain *terrain_by_translated_name(const char *name);
struct terrain *rand_terrain_by_flag(enum terrain_flag_id flag);

char terrain_identifier(const struct terrain *pterrain);
const char *terrain_rule_name(const struct terrain *pterrain);
const char *terrain_name_translation(const struct terrain *pterrain);

/* Functions to operate on a terrain flag. */
#define terrain_has_flag(terr, flag) BV_ISSET((terr)->flags, flag)

int terrains_by_flag(enum terrain_flag_id flag, struct terrain **buffer, int bufsize);

bool is_terrain_flag_card_near(const struct tile *ptile,
			       enum terrain_flag_id flag);
bool is_terrain_flag_near_tile(const struct tile *ptile,
			       enum terrain_flag_id flag);
int count_terrain_flag_near_tile(const struct tile *ptile,
				 bool cardinal_only, bool percentage,
				 enum terrain_flag_id flag);

/* Terrain-specific functions. */
#define is_ocean(pterrain) ((pterrain) != T_UNKNOWN \
                            && terrain_type_terrain_class(pterrain) == TC_OCEAN)
#define is_ocean_tile(ptile) \
  is_ocean(tile_terrain(ptile))

bool terrain_has_resource(const struct terrain *pterrain,
			  const struct resource *presource);

/* Functions to operate on a general terrain type. */
bool is_terrain_card_near(const struct tile *ptile,
			  const struct terrain *pterrain,
                          bool check_self);
bool is_terrain_near_tile(const struct tile *ptile,
			  const struct terrain *pterrain,
                          bool check_self);
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    const struct terrain *pterrain);
int count_terrain_property_near_tile(const struct tile *ptile,
				     bool cardinal_only, bool percentage,
				     enum mapgen_terrain_property prop);

bool is_resource_card_near(const struct tile *ptile,
                           const struct resource *pres,
                           bool check_self);
bool is_resource_near_tile(const struct tile *ptile,
                           const struct resource *pres,
                           bool check_self);

/* General resource accessor functions. */
Resource_type_id resource_count(void);
Resource_type_id resource_index(const struct resource *presource);
Resource_type_id resource_number(const struct resource *presource);

struct resource *resource_by_number(const Resource_type_id id);
struct resource *resource_by_identifier(const char identifier);
struct resource *resource_by_rule_name(const char *name);

const char *resource_rule_name(const struct resource *presource);
const char *resource_name_translation(const struct resource *presource);

/* General special accessor functions. */
enum tile_special_type special_by_rule_name(const char *name);
const char *special_rule_name(enum tile_special_type type);
const char *special_name_translation(enum tile_special_type type);

void set_special(bv_special *set, enum tile_special_type to_set);
void clear_special(bv_special *set, enum tile_special_type to_clear);
void clear_all_specials(bv_special *set);
bool contains_special(bv_special all,
		      enum tile_special_type to_test_for);
bool contains_any_specials(bv_special all);

bool is_native_terrain_to_special(enum tile_special_type special,
                                  const struct terrain *pterrain);
bool is_native_tile_to_special(enum tile_special_type special,
                               const struct tile *ptile);

/* Special helper functions */
const char *get_infrastructure_text(bv_special pset, bv_bases bases, bv_roads roads);
enum tile_special_type get_infrastructure_prereq(enum tile_special_type spe);
bool get_preferred_pillage(struct act_tgt *tgt,
                           bv_special pset,
                           bv_bases bases,
                           bv_roads roads);

int terrain_base_time(const struct terrain *pterrain,
                      Base_type_id base);

int terrain_road_time(const struct terrain *pterrain,
                      Road_type_id road);

/* Functions to operate on a terrain special. */
bool is_special_card_near(const struct tile *ptile,
                          enum tile_special_type spe,
                          bool check_self);
bool is_special_near_tile(const struct tile *ptile,
			  enum tile_special_type spe,
                          bool check_self);
int count_special_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    enum tile_special_type spe);

/* Functions to operate on a terrain class. */
const char *terrain_class_name_translation(enum terrain_class tclass);

enum terrain_class terrain_type_terrain_class(const struct terrain *pterrain);
bool is_terrain_class_card_near(const struct tile *ptile, enum terrain_class tclass);
bool is_terrain_class_near_tile(const struct tile *ptile, enum terrain_class tclass);
int count_terrain_class_near_tile(const struct tile *ptile,
                                  bool cardinal_only, bool percentage,
                                  enum terrain_class tclass);

/* Functions to deal with possible terrain alterations. */
const char *terrain_alteration_name_translation(enum terrain_alteration talter);
bool terrain_can_support_alteration(const struct terrain *pterrain,
                                    enum terrain_alteration talter);

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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__TERRAIN_H */
