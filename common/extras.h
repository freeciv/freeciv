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


#define EXTRA_NONE (-1)

struct extra_type
{
  int id;
  enum extra_type_id type;
  struct name_translation name;

  struct requirement_vector reqs;
  bool buildable;

  union
  {
    enum tile_special_type special;
    struct base_type base;
    struct road_type road;
  } data;
};

/* get 'struct extra_type_list' and related functions: */
#define SPECLIST_TAG extra_type
#define SPECLIST_TYPE struct extra_type
#include "speclist.h"

#define extra_type_list_iterate(extralist, pextra) \
    TYPED_LIST_ITERATE(struct extra_type, extralist, pextra)
#define extra_type_list_iterate_end LIST_ITERATE_END

void extras_init(void);
void extras_free(void);

int extra_count(void);
int extra_index(const struct extra_type *pextra);
int extra_number(const struct extra_type *pextra);
struct extra_type *extra_by_number(int id);

struct extra_type *extra_type_get(enum extra_type_id type, int subid);

const char *extra_name_translation(const struct extra_type *pextra);
const char *extra_rule_name(const struct extra_type *pextra);
struct extra_type *extra_type_by_rule_name(const char *name);
struct extra_type *extra_type_by_translated_name(const char *name);

struct base_type *extra_base_get(struct extra_type *pextra);
struct road_type *extra_road_get(struct extra_type *pextra);
const struct base_type *extra_base_get_const(const struct extra_type *pextra);
const struct road_type *extra_road_get_const(const struct extra_type *pextra);

void extra_to_caused_by_list(struct extra_type *pextra, enum extra_cause cause);
struct extra_type_list *extra_type_list_by_cause(enum extra_cause cause);
struct extra_type *rand_extra_type_by_cause(enum extra_cause cause);

bool is_extra_card_near(const struct tile *ptile, const struct extra_type *pextra);
bool is_extra_near_tile(const struct tile *ptile, const struct extra_type *pextra);

bool extra_can_be_built(const struct extra_type *pextra, const struct tile *ptile);
bool can_build_extra(struct extra_type *pextra,
                     const struct unit *punit,
                     const struct tile *ptile);
bool player_can_build_extra(const struct extra_type *pextra,
                            const struct player *pplayer,
                            const struct tile *ptile);

bool is_native_tile_to_extra(const struct extra_type *pextra,
                             const struct tile *ptile);

struct extra_type *next_extra_for_tile(struct tile *ptile, enum extra_cause cause,
                                       struct player *pplayer, struct unit *punit);

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
  extra_type_list_iterate(_etl_, _extra) {

#define extra_type_by_cause_iterate_end                    \
  } extra_type_list_iterate_end                            \
}

#define extra_deps_iterate(_reqs, _dep)                 \
{                                                       \
  requirement_vector_iterate(_reqs, preq) {             \
    if (preq->source.kind == VUT_EXTRA) {               \
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
