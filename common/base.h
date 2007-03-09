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

#include "fc_types.h"
#include "requirements.h"
#include "terrain.h"

enum base_type_id { BASE_FORTRESS = 0, BASE_AIRBASE, BASE_LAST };

/* This must correspond to base_gui_type_names[] in base.c */
enum base_gui_type { BASE_GUI_FORTRESS = 0, BASE_GUI_AIRBASE, BASE_GUI_OTHER,
                     BASE_GUI_LAST };

typedef enum base_type_id Base_type_id;

enum base_flag_id {
  BF_NOT_AGGRESSIVE = 0, /* Unit inside are not considered aggressive
                          * if base is close to city */
  BF_DEFENSE_BONUS,      /* Base provides defense bonus for units inside */
  BF_NO_STACK_DEATH,     /* Units inside will not die all at once */
  BF_CLAIM_TERRITORY,    /* Base claims tile ownership */
  BF_DIPLOMAT_DEFENSE,   /* Base provides bonus for defending diplomat */
  BF_PARADROP_FROM,      /* Paratroopers can use base for paradrop */
  BF_LAST                /* This has to be last */
};

BV_DEFINE(bv_base_flags, BF_LAST);

struct base_type {
  int id;
  const char *name;
  char name_orig[MAX_LEN_NAME];
  char graphic_str[MAX_LEN_NAME];
  char graphic_alt[MAX_LEN_NAME];
  char activity_gfx[MAX_LEN_NAME];
  struct requirement_vector reqs;
  enum base_gui_type gui_type;
  bv_unit_classes native_to;

  bv_base_flags flags;
};

bool base_flag(const struct base_type *pbase, enum base_flag_id flag);
bool is_native_base(const struct unit_type *punittype,
                    const struct base_type *pbase);
bool is_native_base_to_class(const struct unit_class *pclass,
                             const struct base_type *pbase);
bool base_flag_affects_unit(const struct unit_type *punittype,
                            const struct base_type *pbase,
                            enum base_flag_id flag);
const char *base_name(const struct base_type *pbase);

bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile);

struct base_type *base_type_get_from_special(bv_special spe);

enum base_flag_id base_flag_from_str(const char *s);
struct base_type *base_type_get_by_id(Base_type_id id);

enum base_gui_type base_gui_type_from_str(const char *s);
struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile);

void base_types_init(void);
void base_types_free(void);

#define base_type_iterate(pbase)                                            \
{                                                                           \
  int _index;                                                               \
                                                                            \
  for (_index = 0; _index < game.control.num_base_types; _index++) {        \
    struct base_type *pbase = base_type_get_by_id(_index);

#define base_type_iterate_end                                               \
  }                                                                         \
}


#endif  /* FC__BASE_H */
