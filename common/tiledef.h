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
#ifndef FC__TILEDEF_H
#define FC__TILEDEF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "extras.h"

struct tiledef
{
  int id;
  struct name_translation name;

  struct extra_type_list *extras;
};

void tiledefs_init(void);
void tiledefs_free(void);

int tiledef_count(void);
int tiledef_number(const struct tiledef *td);
struct tiledef *tiledef_by_number(int id);

/* For optimization purposes (being able to have it as macro instead of function
 * call) this is now same as tiledef_number(). tiledef.c does have semantically
 * correct implementation too. */
#define tiledef_index(_td_) (_td_)->id

const char *tiledef_name_translation(const struct tiledef *td);
const char *tiledef_rule_name(const struct tiledef *td);
struct tiledef *tiledef_by_rule_name(const char *name);
struct tiledef *tiledef_by_translated_name(const char *name);

#define tiledef_iterate(_p)                                                 \
{                                                                           \
  int _i_##_p;                                                              \
  for (_i_##_p = 0; _i_##_p < game.control.num_tiledef_types; _i_##_p++) {  \
    struct tiledef *_p = tiledef_by_number(_i_##_p);

#define tiledef_iterate_end                       \
  }                                               \
}

bool tile_matches_tiledef(const struct tiledef *td, const struct tile *ptile)
  fc__attribute((nonnull (1, 2)));

bool is_tiledef_card_near(const struct civ_map *nmap, const struct tile *ptile,
                          const struct tiledef *ptd);
bool is_tiledef_near_tile(const struct civ_map *nmap, const struct tile *ptile,
                          const struct tiledef *ptd);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__TILEDEF_H */
