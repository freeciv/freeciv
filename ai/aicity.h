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

#include "fc_types.h"
#include "aidata.h"

void ai_manage_cities(struct player *pplayer);
void ai_manage_buildings(struct player *pplayer);

int ai_eval_calc_city(struct city *pcity, struct ai_data *ai);

void ai_city_init(struct city *pcity);
void ai_city_update(struct city *pcity);
void ai_city_close(struct city *pcity);

void ai_city_worker_act_set(struct city *pcity, int city_tile_index,
                            enum unit_activity act_id, int value);
int ai_city_worker_act_get(const struct city *pcity, int city_tile_index,
                           enum unit_activity act_id);

#endif  /* FC__AICITY_H */
