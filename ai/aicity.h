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
#ifndef FC__AICITY_H
#define FC__AICITY_H

#include "unit.h"		/* enum unit_move_type */

struct player;
struct city;
struct ai_choice;
struct ai_data;

int ai_eval_calc_city(struct city *pcity, struct ai_data *ai);

void ai_manage_cities(struct player *pplayer);

enum ai_city_task { AICITY_NONE, AICITY_TECH, AICITY_TAX, AICITY_PROD};
/* These are not used (well, except AICITY_NONE)  --dwp */

#endif  /* FC__AICITY_H */
