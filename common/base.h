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

/* Used in the network protocol. */
#define SPECENUM_NAME base_gui_type
#define SPECENUM_VALUE0 BASE_GUI_FORTRESS
#define SPECENUM_VALUE0NAME "Fortress"
#define SPECENUM_VALUE1 BASE_GUI_AIRBASE
#define SPECENUM_VALUE1NAME "Airbase"
#define SPECENUM_VALUE2 BASE_GUI_OTHER
#define SPECENUM_VALUE2NAME "Other"
#include "specenum_gen.h"

struct extra_type;

struct base_type {
  Base_type_id item_number;
  enum base_gui_type gui_type;
  int border_sq;
  int vision_main_sq;
  int vision_invis_sq;
  int vision_subs_sq;

  struct extra_type *self;
};

#define BASE_NONE       -1

/* General base accessor functions. */
Base_type_id base_count(void);
Base_type_id base_number(const struct base_type *pbase);

struct base_type *base_by_number(const Base_type_id id);

struct extra_type *base_extra_get(const struct base_type *pbase);

/* Ancillary functions */
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile);
bool player_can_build_base(const struct base_type *pbase,
                           const struct player *pplayer,
                           const struct tile *ptile);

struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile);

bool territory_claiming_base(const struct base_type *pbase);

/* Initialization and iteration */
void base_type_init(struct extra_type *pextra, int idx);
void base_types_free(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__BASE_H */
