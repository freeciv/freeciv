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

typedef enum base_type_id Base_type_id;

enum base_flag_id {
  BF_NOT_AGGRESSIVE = 0, /* Unit inside are not considered aggressive
                          * if base is close to city */
  BF_DEFENSE_BONUS,      /* Base provides defense bonus for units inside */
  BF_NO_STACK_DEATH,     /* Units inside will not die all at once */
  BF_WATCHTOWER,         /* Base can act as watchtower */
  BF_CLAIM_TERRITORY,    /* Base claims tile ownership */
  BF_DIPLOMAT_DEFENSE,   /* Base provides bonus for defending diplomat */
  BF_REFUEL,             /* Base refuels units */
  BF_NO_HP_LOSS,         /* Units do not lose hitpoints when in base */
  BF_ATTACK_UNREACHABLE, /* Unreachable units inside base can be attacked */
  BF_PARADROP_FROM,      /* Paratroopers can use base for paradrop */
  BF_LAST                /* This has to be last */
};

BV_DEFINE(bv_base_flags, BF_LAST);

struct base_type {
  const char *name;
  char name_orig[MAX_LEN_NAME];
  int id;
  struct requirement_vector reqs;
  bv_base_flags flags;
};

bool base_flag(const struct base_type *pbase, enum base_flag_id flag);
const char *base_name(const struct base_type *pbase);

bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile);

struct base_type *base_type_get_from_special(bv_special spe);

enum base_flag_id base_flag_from_str(const char *s);
struct base_type *base_type_get_by_id(Base_type_id id);

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
