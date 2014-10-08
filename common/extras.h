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
#ifndef FC__EXTRAS_H
#define FC__EXTRAS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "base.h"
#include "fc_types.h"
#include "road.h"

/* Used in the network protocol. */
#define SPECENUM_NAME extra_flag_id
/* Tile with this extra is considered native for units in tile. */
#define SPECENUM_VALUE0 EF_NATIVE_TILE
#define SPECENUM_VALUE0NAME "NativeTile"
/* Refuel native units */
#define SPECENUM_VALUE1 EF_REFUEL
#define SPECENUM_VALUE1NAME "Refuel"
#define SPECENUM_VALUE2 EF_TERR_CHANGE_REMOVES
#define SPECENUM_VALUE2NAME "TerrChangeRemoves"
/* Extra will be built in cities automatically */
#define SPECENUM_VALUE3 EF_AUTO_ON_CITY_CENTER
#define SPECENUM_VALUE3NAME "AutoOnCityCenter"
/* Extra is always present in cities */
#define SPECENUM_VALUE4 EF_ALWAYS_ON_CITY_CENTER
#define SPECENUM_VALUE4NAME "AlwaysOnCityCenter"
/* Road style gfx from ocean extra connects to nearby land */
#define SPECENUM_VALUE5 EF_CONNECT_LAND
#define SPECENUM_VALUE5NAME "ConnectLand"
/* Counts towards Global Warming */
#define SPECENUM_VALUE6 EF_GLOBAL_WARMING
#define SPECENUM_VALUE6NAME "GlobalWarming"
/* Counts towards Nuclear Winter */
#define SPECENUM_VALUE7 EF_NUCLEAR_WINTER
#define SPECENUM_VALUE7NAME "NuclearWinter"
#define SPECENUM_COUNT EF_COUNT
#define SPECENUM_BITVECTOR bv_extra_flags
#include "specenum_gen.h"

#define EXTRA_NONE (-1)

struct extra_type
{
  int id;
  struct name_translation name;
  enum extra_category category;
  enum extra_cause causes;
  enum extra_rmcause rmcauses;

  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char activity_gfx[MAX_LEN_NAME];
  char act_gfx_alt[MAX_LEN_NAME];
  char rmact_gfx[MAX_LEN_NAME];
  char rmact_gfx_alt[MAX_LEN_NAME];

  struct requirement_vector reqs;
  struct requirement_vector rmreqs;
  bool buildable;
  int build_time;
  int build_time_factor;
  int removal_time;
  int removal_time_factor;

  int defense_bonus;

  bv_unit_classes native_to;

  bv_extra_flags flags;
  bv_extras conflicts;
  bv_extras hidden_by;

  /* Same information as in hidden_by, but iterating through this list is much
   * faster than through all extra types to check which ones are hiding this one.
   * Only used client side. */
  struct extra_type_list *hiders;

  struct strvec *helptext;

  struct
  {
    int special_idx;
    struct base_type *base;
    struct road_type *road;
  } data;
};

/* get 'struct extra_type_list' and related functions: */
#define SPECLIST_TAG extra_type
#define SPECLIST_TYPE struct extra_type
#include "speclist.h"

#define extra_type_list_iterate(extralist, pextra) \
    TYPED_LIST_ITERATE(struct extra_type, extralist, pextra)
#define extra_type_list_iterate_end LIST_ITERATE_END

#define extra_type_list_iterate_rev(extralist, pextra) \
    TYPED_LIST_ITERATE_REV(struct extra_type, extralist, pextra)
#define extra_type_list_iterate_rev_end LIST_ITERATE_REV_END

void extras_init(void);
void extras_free(void);

int extra_count(void);
int extra_number(const struct extra_type *pextra);
struct extra_type *extra_by_number(int id);

/* For optimization purposes (being able to have it as macro instead of function
 * call) this is now same as extra_number(). extras.c does have sematically correct
 * implementation too. */
#define extra_index(_e_) (_e_)->id

struct extra_type *special_extra_get(int spe);

const char *extra_name_translation(const struct extra_type *pextra);
const char *extra_rule_name(const struct extra_type *pextra);
struct extra_type *extra_type_by_rule_name(const char *name);
struct extra_type *extra_type_by_translated_name(const char *name);

struct base_type *extra_base_get(const struct extra_type *pextra);
struct road_type *extra_road_get(const struct extra_type *pextra);
const struct base_type *extra_base_get_const(const struct extra_type *pextra);
const struct road_type *extra_road_get_const(const struct extra_type *pextra);

void extra_to_caused_by_list(struct extra_type *pextra, enum extra_cause cause);
struct extra_type_list *extra_type_list_by_cause(enum extra_cause cause);
struct extra_type *rand_extra_for_tile(struct tile *ptile, enum extra_cause cause);

bool is_extra_caused_by(const struct extra_type *pextra, enum extra_cause cause);
bool is_extra_caused_by_worker_action(const struct extra_type *pextra);

void extra_to_removed_by_list(struct extra_type *pextra, enum extra_rmcause rmcause);
struct extra_type_list *extra_type_list_by_rmcause(enum extra_rmcause rmcause);

bool is_extra_removed_by(const struct extra_type *pextra, enum extra_rmcause rmcause);

bool is_extra_card_near(const struct tile *ptile, const struct extra_type *pextra);
bool is_extra_near_tile(const struct tile *ptile, const struct extra_type *pextra);

bool extra_can_be_built(const struct extra_type *pextra, const struct tile *ptile);
bool can_build_extra(struct extra_type *pextra,
                     const struct unit *punit,
                     const struct tile *ptile);
bool player_can_build_extra(const struct extra_type *pextra,
                            const struct player *pplayer,
                            const struct tile *ptile);

bool can_remove_extra(struct extra_type *pextra,
                      const struct unit *punit,
                      const struct tile *ptile);
bool player_can_remove_extra(const struct extra_type *pextra,
                             const struct player *pplayer,
                             const struct tile *ptile);

bool is_native_extra_to_uclass(const struct extra_type *pextra,
                               const struct unit_class *pclass);
bool is_native_extra_to_utype(const struct extra_type *pextra,
                              const struct unit_type *punittype);
bool is_native_tile_to_extra(const struct extra_type *pextra,
                             const struct tile *ptile);

bool extra_has_flag(const struct extra_type *pextra, enum extra_flag_id flag);

bool extra_causes_env_upset(struct extra_type *pextra,
                            enum environment_upset_type upset);

bool can_extras_coexist(const struct extra_type *pextra1,
                        const struct extra_type *pextra2);

struct extra_type *next_extra_for_tile(const struct tile *ptile, enum extra_cause cause,
                                       const struct player *pplayer,
                                       const struct unit *punit);
struct extra_type *prev_extra_in_tile(const struct tile *ptile, enum extra_rmcause rmcause,
                                      const struct player *pplayer,
                                      const struct unit *punit);

enum extra_cause activity_to_extra_cause(enum unit_activity act);

#define extra_type_iterate(_p)                                \
{                                                             \
  int _i_;                                                    \
  for (_i_ = 0; _i_ < game.control.num_extra_types; _i_++) {  \
    struct extra_type *_p = extra_by_number(_i_);

#define extra_type_iterate_end                    \
  }                                               \
}

#define extra_type_by_cause_iterate(_cause, _extra)                 \
{                                                                   \
  struct extra_type_list *_etl_ = extra_type_list_by_cause(_cause); \
  if (_etl_ != NULL) {                                              \
    extra_type_list_iterate(_etl_, _extra) {

#define extra_type_by_cause_iterate_end                    \
    } extra_type_list_iterate_end                          \
  }                                                        \
}

#define extra_type_by_cause_iterate_rev(_cause, _extra)                 \
{                                                                      \
 struct extra_type_list *_etl_ = extra_type_list_by_cause(_cause);     \
  extra_type_list_iterate_rev(_etl_, _extra) {

#define extra_type_by_cause_iterate_rev_end                \
  } extra_type_list_iterate_rev_end                        \
}

#define extra_type_by_rmcause_iterate(_rmcause, _extra)                   \
{                                                                         \
  struct extra_type_list *_etl_ = extra_type_list_by_rmcause(_rmcause);   \
  extra_type_list_iterate_rev(_etl_, _extra) {

#define extra_type_by_rmcause_iterate_end                \
  } extra_type_list_iterate_rev_end                      \
}

#define extra_deps_iterate(_reqs, _dep)                 \
{                                                       \
  requirement_vector_iterate(_reqs, preq) {             \
    if (preq->source.kind == VUT_EXTRA                  \
        && preq->present) {                             \
      struct extra_type *_dep;                          \
      _dep = preq->source.value.extra;

#define extra_deps_iterate_end                          \
    }                                                   \
  } requirement_vector_iterate_end;                     \
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__EXTRAS_H */
