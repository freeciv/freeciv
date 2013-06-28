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

struct extra_type
{
  int id;
  enum extra_type_id type;
  struct name_translation name;

  union
  {
    enum tile_special_type special;
    struct base_type base;
    struct road_type road;
  } data;
};

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

#define extra_type_iterate(_p)                    \
{                                                 \
  int _i_;                                        \
  int _ec_ = extra_count();                       \
  for (_i_ = 0; _i_ < _ec_; _i_++) {              \
    struct extra_type *_p = extra_by_number(_i_);

#define extra_type_iterate_end                    \
  }                                               \
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__EXTRAS_H */
