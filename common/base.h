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

enum base_type_id { BASE_FORTRESS, BASE_AIRBASE };

typedef enum base_type_id Base_type_id;

enum base_flag_id {
  BF_NOT_AGGRESSIVE,     /* Unit inside are not considered aggressive
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

bool base_flag(Base_type_id base_type, enum base_flag_id flag);

#endif  /* FC__BASE_H */
