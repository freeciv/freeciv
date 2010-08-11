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
#ifndef FC__INFRACACHE_H
#define FC__INFRACACHE_H

struct player;

void initialize_infrastructure_cache(struct player *pplayer);

void ai_city_update(struct city *pcity);

int city_tile_value(struct city *pcity, struct tile *ptile,
                    int foodneed, int prodneed);

void ai_city_worker_act_set(struct city *pcity, int city_tile_index,
                            enum unit_activity act_id, int value);
int ai_city_worker_act_get(const struct city *pcity, int city_tile_index,
                           enum unit_activity act_id);

#endif   /* FC__INFRACACHE_H */
