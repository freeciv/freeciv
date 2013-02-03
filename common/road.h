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
#ifndef FC__ROAD_H
#define FC__ROAD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPECENUM_NAME road_flag_id
/* Tile with this road is considered native for units traveling the road. */
#define SPECENUM_VALUE0 RF_NATIVE_TILE
#define SPECENUM_VALUE0NAME "NativeTile"
#define SPECENUM_VALUE1 RF_CONNECT_LAND
#define SPECENUM_VALUE1NAME "ConnectLand"
#define SPECENUM_VALUE2 RF_REQUIRES_BRIDGE
#define SPECENUM_VALUE2NAME "RequiresBridge"
#define SPECENUM_VALUE3 RF_CARDINAL_ONLY
#define SPECENUM_VALUE3NAME "CardinalOnly"
#define SPECENUM_VALUE4 RF_ALWAYS_ON_CITY_CENTER
#define SPECENUM_VALUE4NAME "AlwaysOnCityCenter"
#define SPECENUM_COUNT RF_COUNT
#include "specenum_gen.h"

#define SPECENUM_NAME road_move_mode
#define SPECENUM_VALUE0 RMM_NO_BONUS
#define SPECENUM_VALUE0NAME "NoBonus"
#define SPECENUM_VALUE1 RMM_CARDINAL
#define SPECENUM_VALUE1NAME "Cardinal"
#define SPECENUM_VALUE2 RMM_RELAXED
#define SPECENUM_VALUE2NAME "Relaxed"
#define SPECENUM_VALUE3 RMM_FAST_ALWAYS
#define SPECENUM_VALUE3NAME "FastAlways"
#include "specenum_gen.h"

BV_DEFINE(bv_road_flags, RF_COUNT);

struct road_type;

/* get 'struct road_type_list' and related functions: */
#define SPECLIST_TAG road_type
#define SPECLIST_TYPE struct road_type
#include "speclist.h"

#define road_type_list_iterate(roadlist, proad) \
    TYPED_LIST_ITERATE(struct road_type, roadlist, proad)
#define road_type_list_iterate_end LIST_ITERATE_END


struct road_type {
  int id;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char activity_gfx[MAX_LEN_NAME];
  char act_gfx_alt[MAX_LEN_NAME];

  int move_cost;
  enum road_move_mode move_mode;
  int build_time;
  int defense_bonus;
  bool buildable;
  bool pillageable;
  int tile_incr[O_LAST];
  int tile_bonus[O_LAST];
  enum road_compat compat;

  struct requirement_vector reqs;
  bv_unit_classes native_to;
  bv_roads hidden_by;
  bv_road_flags flags;

  /* Same information as in hidden_by, but iterating through this list is much
   * faster than through all road types to check which ones are hidin this one.
   * Only used client side. */
  struct road_type_list *hiders;

  struct strvec *helptext;
};

#define ROAD_NONE (-1)

/* General road type accessor functions. */
Road_type_id road_count(void);
Road_type_id road_index(const struct road_type *proad);
Road_type_id road_number(const struct road_type *proad);

struct road_type *road_by_number(Road_type_id id);

enum road_compat road_compat_special(const struct road_type *proad);
struct road_type *road_by_compat_special(enum road_compat compat);

const char *road_name_translation(struct road_type *road);
const char *road_rule_name(const struct road_type *road);
struct road_type *road_type_by_rule_name(const char *name);
struct road_type *road_type_by_translated_name(const char *name);

bool is_road_card_near(const struct tile *ptile, const struct road_type *proad);
bool is_road_near_tile(const struct tile *ptile, const struct road_type *proad);

bool road_has_flag(const struct road_type *proad, enum road_flag_id flag);

bool is_native_road_to_uclass(const struct road_type *proad,
                              const struct unit_class *pclass);

bool road_can_be_built(const struct road_type *proad, const struct tile *ptile);
bool can_build_road(struct road_type *proad,
		    const struct unit *punit,
		    const struct tile *ptile);
bool player_can_build_road(const struct road_type *proad,
			   const struct player *pplayer,
			   const struct tile *ptile);

bool is_native_tile_to_road(const struct road_type *proad,
                            const struct tile *ptile);

/* Initialization and iteration */
void road_types_init(void);
void road_types_free(void);

struct road_type *next_road_for_tile(struct tile *ptile, struct player *pplayer,
                                     struct unit *punit);

#define road_type_iterate(_p)                    \
{                                                \
  int _i_;                                       \
  for (_i_ = 0; _i_ < game.control.num_road_types ; _i_++) { \
    struct road_type *_p = road_by_number(_i_);

#define road_type_iterate_end                    \
  }}

#define road_deps_iterate(_reqs, _dep)                  \
{                                                       \
  requirement_vector_iterate(_reqs, preq) {             \
    if (preq->source.kind == VUT_ROAD) {                \
      struct road_type *_dep = preq->source.value.road;

#define road_deps_iterate_end                           \
    }                                                   \
  } requirement_vector_iterate_end;                     \
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__ROAD_H */
