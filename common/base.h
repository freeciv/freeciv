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
#ifndef FC__BASE_H
#define FC__BASE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* utility */
#include "bitvector.h"

/* common */
#include "fc_types.h"
#include "requirements.h"

struct strvec;          /* Actually defined in "utility/string_vector.h". */

#define SPECENUM_NAME base_gui_type
#define SPECENUM_VALUE0 BASE_GUI_FORTRESS
#define SPECENUM_VALUE0NAME "Fortress"
#define SPECENUM_VALUE1 BASE_GUI_AIRBASE
#define SPECENUM_VALUE1NAME "Airbase"
#define SPECENUM_VALUE2 BASE_GUI_OTHER
#define SPECENUM_VALUE2NAME "Other"
#include "specenum_gen.h"

#define SPECENUM_NAME base_flag_id
/* Unit inside are not considered aggressive if base is close to city */
#define SPECENUM_VALUE0 BF_NOT_AGGRESSIVE
#define SPECENUM_VALUE0NAME "NoAggressive"
/* Units inside will not die all at once */
#define SPECENUM_VALUE1 BF_NO_STACK_DEATH
#define SPECENUM_VALUE1NAME "NoStackDeath"
/* Base provides bonus for defending diplomat */
#define SPECENUM_VALUE2 BF_DIPLOMAT_DEFENSE
#define SPECENUM_VALUE2NAME "DiplomatDefense"
/* Paratroopers can use base for paradrop */
#define SPECENUM_VALUE3 BF_PARADROP_FROM
#define SPECENUM_VALUE3NAME "ParadropFrom"
/* Makes tile native terrain for units */
#define SPECENUM_VALUE4 BF_NATIVE_TILE
#define SPECENUM_VALUE4NAME "NativeTile"
#define SPECENUM_COUNT BF_COUNT
#include "specenum_gen.h"

BV_DEFINE(bv_base_flags, BF_COUNT);

struct base_type {
  Base_type_id item_number;
  bool buildable;
  bool pillageable;
  struct name_translation name;
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char activity_gfx[MAX_LEN_NAME];
  char act_gfx_alt[MAX_LEN_NAME];
  struct requirement_vector reqs;
  enum base_gui_type gui_type;
  int build_time;
  int defense_bonus;
  int border_sq;
  int vision_main_sq;
  int vision_invis_sq;

  bv_unit_classes native_to;
  bv_base_flags flags;
  bv_bases conflicts;

  struct strvec *helptext;
};

#define BASE_NONE       -1

/* General base accessor functions. */
Base_type_id base_count(void);
Base_type_id base_index(const struct base_type *pbase);
Base_type_id base_number(const struct base_type *pbase);

struct base_type *base_by_number(const Base_type_id id);

const char *base_rule_name(const struct base_type *pbase);
const char *base_name_translation(const struct base_type *pbase);

struct base_type *base_type_by_rule_name(const char *name);
struct base_type *base_type_by_translated_name(const char *name);

bool is_base_card_near(const struct tile *ptile, const struct base_type *pbase);
bool is_base_near_tile(const struct tile *ptile, const struct base_type *pbase);

/* Functions to operate on a base flag. */
bool base_has_flag(const struct base_type *pbase, enum base_flag_id flag);
bool base_has_flag_for_utype(const struct base_type *pbase,
                             enum base_flag_id flag,
                             const struct unit_type *punittype);
bool is_native_base_to_uclass(const struct base_type *pbase,
                              const struct unit_class *pclass);
bool is_native_base_to_utype(const struct base_type *pbase,
                             const struct unit_type *punittype);
bool is_native_tile_to_base(const struct base_type *pbase,
                            const struct tile *ptile);

/* Ancillary functions */
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile);
bool player_can_build_base(const struct base_type *pbase,
                           const struct player *pplayer,
                           const struct tile *ptile);

struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile);

bool can_bases_coexist(const struct base_type *base1, const struct base_type *base2);

bool territory_claiming_base(const struct base_type *pbase);

/* Initialization and iteration */
void base_types_init(void);
void base_types_free(void);

struct base_type *base_array_first(void);
const struct base_type *base_array_last(void);

#define base_type_iterate(_p)						\
{									\
  struct base_type *_p = base_array_first();				\
  if (NULL != _p) {							\
    for (; _p <= base_array_last(); _p++) {

#define base_type_iterate_end						\
    }									\
  }									\
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__BASE_H */
