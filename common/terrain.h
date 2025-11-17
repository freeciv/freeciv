/***********************************************************************
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

/* gen_headers/enums */
#include "terrain_enums_gen.h"

#define MAX_NUM_USER_TER_FLAGS (TER_USER_LAST - TER_USER_1 + 1)

struct base_type;
struct strvec;          /* Actually defined in "utility/string_vector.h". */
struct rgbcolor;

/* Used in the network protocol. */
enum special_river_move {
  RMV_NORMAL = 0,
  RMV_FAST_STRICT = 1,
  RMV_FAST_RELAXED = 2,
  RMV_FAST_ALWAYS = 3,
};

/* === */

struct resource_type {

  char id_old_save; /* Single-character identifier used in old savegames. */
#define RESOURCE_NULL_IDENTIFIER '\0'
#define RESOURCE_NONE_IDENTIFIER ' '

  int output[O_LAST]; /* Amount added by this resource. */

  struct extra_type *self;
};

/* === */

#define T_NONE (nullptr) /* A special flag meaning no terrain type. */
#define T_UNKNOWN (nullptr) /* An unknown terrain. */

/* The first terrain value. */
#define T_FIRST 0

/* A hard limit on the number of terrains; useful for static arrays.
 * Used in the network protocol. */
#define MAX_NUM_TERRAINS (96)

/*
 * This struct gives data about each terrain type. There are many ways
 * it could be extended.
 */
struct terrain {
  int item_number;
  struct name_translation name;
  bool ruledit_disabled; /* Does not really exist - hole in terrain array */
  void *ruledit_dlg;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char graphic_alt2[MAX_LEN_NAME];

  char identifier; /* Single-character identifier used in games saved. */
  char identifier_load; /* Single-character identifier that was used in the savegame
                         * loaded. */

#define TERRAIN_UNKNOWN_IDENTIFIER 'u'

  enum terrain_class tclass;

  int movement_cost; /* Whole MP, not scaled by SINGLE_MOVE */
  int defense_bonus; /* % defense bonus - defaults to zero */

  int output[O_LAST];

  struct extra_type **resources; /* nullptr-terminated */
  int *resource_freq; /* Same length as resources */

#define RESOURCE_FREQUENCY_MINIMUM (0)
#define RESOURCE_FREQUENCY_DEFAULT (1)
#define RESOURCE_FREQUENCY_MAXIMUM (255)

  int road_output_incr_pct[O_LAST];
  int base_time;
  int road_time;

  struct terrain *cultivate_result;
  int cultivate_time;

  struct terrain *plant_result;
  int plant_time;

  int irrigation_food_incr;
  int irrigation_time;

  int mining_shield_incr;
  int mining_time;

  int placing_time;

  struct terrain *transform_result;

  int transform_time;
  int pillage_time;

  /* Currently only clean times, but named for future */
  int extra_removal_times[MAX_EXTRA_TYPES];

  int num_animals;
  const struct unit_type **animals;

  /* May be nullptr if the transformation is impossible. */
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

bool is_terrain_flag_card_near(const struct civ_map *nmap,
                               const struct tile *ptile,
			       enum terrain_flag_id flag);
bool is_terrain_flag_near_tile(const struct civ_map *nmap,
                               const struct tile *ptile,
			       enum terrain_flag_id flag);
int count_terrain_flag_near_tile(const struct civ_map *nmap,
                                 const struct tile *ptile,
				 bool cardinal_only, bool percentage,
				 enum terrain_flag_id flag);
void user_terrain_flags_init(void);
void user_terrain_flags_free(void);
void set_user_terrain_flag_name(enum terrain_flag_id id, const char *name, const char *helptxt);
const char *terrain_flag_helptxt(enum terrain_flag_id id);

/* Terrain-specific functions. */
#define is_ocean(pterrain) ((pterrain) != T_UNKNOWN \
                            && terrain_type_terrain_class(pterrain) == TC_OCEAN)
#define is_ocean_tile(ptile) \
  is_ocean(tile_terrain(ptile))

bool terrain_has_resource(const struct terrain *pterrain,
                          const struct extra_type *presource);

/* Functions to operate on a general terrain type. */
bool is_terrain_card_near(const struct civ_map *nmap,
                          const struct tile *ptile,
			  const struct terrain *pterrain,
                          bool check_self);
bool is_terrain_near_tile(const struct civ_map *nmap,
                          const struct tile *ptile,
			  const struct terrain *pterrain,
                          bool check_self);
int count_terrain_property_near_tile(const struct civ_map *nmap,
                                     const struct tile *ptile,
                                     bool cardinal_only, bool percentage,
                                     enum mapgen_terrain_property prop);

bool is_resource_card_near(const struct civ_map *nmap,
                           const struct tile *ptile,
                           const struct extra_type *pres,
                           bool check_self);
bool is_resource_near_tile(const struct civ_map *nmap,
                           const struct tile *ptile,
                           const struct extra_type *pres,
                           bool check_self);

struct resource_type *resource_type_init(struct extra_type *pextra);
void resource_types_free(void);

struct extra_type *resource_extra_get(const struct resource_type *presource);

/* Special helper functions */
const char *get_infrastructure_text(bv_extras extras);
struct extra_type *get_preferred_pillage(bv_extras extras);

int terrain_extra_build_time(const struct terrain *pterrain,
                             enum unit_activity activity,
                             const struct extra_type *tgt);
int terrain_extra_removal_time(const struct terrain *pterrain,
                               enum unit_activity activity,
                               const struct extra_type *tgt)
  fc__attribute((nonnull (1, 3)));

/* Functions to operate on a terrain class. */
const char *terrain_class_name_translation(enum terrain_class tclass);

enum terrain_class terrain_type_terrain_class(const struct terrain *pterrain);
bool is_terrain_class_card_near(const struct civ_map *nmap,
                                const struct tile *ptile, enum terrain_class tclass);
bool is_terrain_class_near_tile(const struct civ_map *nmap,
                                  const struct tile *ptile, enum terrain_class tclass);
int count_terrain_class_near_tile(const struct civ_map *nmap,
                                  const struct tile *ptile,
                                  bool cardinal_only, bool percentage,
                                  enum terrain_class tclass);

/* Functions to deal with possible terrain alterations. */
bool terrain_can_support_alteration(const struct terrain *pterrain,
                                    enum terrain_alteration talter);

/* Initialization and iteration */
void terrains_init(void);
void terrains_free(void);

struct terrain *terrain_array_first(void);
const struct terrain *terrain_array_last(void);

#define terrain_type_iterate(_p)                                        \
{                                                                       \
  struct terrain *_p = terrain_array_first();                           \
  if (_p != nullptr) {                                                  \
    for (; _p <= terrain_array_last(); _p++) {

#define terrain_type_iterate_end                                        \
    }                                                                   \
  }                                                                     \
}

#define terrain_re_active_iterate(_p)                      \
  terrain_type_iterate(_p) {                               \
    if (!_p->ruledit_disabled) {

#define terrain_re_active_iterate_end                      \
    }                                                      \
  } terrain_type_iterate_end;

#define terrain_resources_iterate(pterrain, _res, _freq)                  \
  if (pterrain != nullptr && pterrain->resources != nullptr) {            \
    int _res##_index;                                                     \
    for (_res##_index = 0;                                                \
         pterrain->resources[_res##_index] != nullptr;                    \
         _res##_index++) {                                                \
      struct extra_type *_res = pterrain->resources[_res##_index];        \
      int _freq = pterrain->resource_freq[_res##_index];

#define terrain_resources_iterate_end                                     \
    }                                                                     \
  }

#define terrain_animals_iterate(pterrain, _animal)                  \
  if (NULL != pterrain && pterrain->num_animals > 0) {                  \
    int _animal##_index;                                                     \
    for (_animal##_index = 0;                                                \
         _animal##_index < pterrain->num_animals;                       \
         _animal##_index++) {                                                \
      const struct unit_type *_animal = pterrain->animals[_animal##_index];        \

#define terrain_animals_iterate_end                                     \
    }                                                                     \
  }

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__TERRAIN_H */
