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

void ai_manage_cities(struct player *pplayer);
int city_get_buildings(struct city *pcity);
int city_get_defenders(struct city *pcity);
int city_get_settlers(struct city *pcity);
int ai_in_initial_expand(struct player *pplayer);
void ai_choose_ferryboat(struct player *pplayer, struct city *pcity, struct ai_choice *choice);
int ai_choose_defender_versus(struct city *pcity, int v);
int ai_choose_defender_limited(struct city *pcity, int n, enum unit_move_type which);
int ai_choose_defender_by_type(struct city *pcity, enum unit_move_type which);
int ai_choose_defender(struct city *pcity);
int ai_choose_attacker_ground(struct city *pcity);
int ai_choose_attacker_sailing(struct city *pcity);
int unit_attack_desirability(int i);
int ai_make_elvis(struct city *pcity);
void ai_scientists_taxmen(struct city *pcity);
int ai_fix_unhappy(struct city *pcity);
int has_a_normal_defender(struct city *pcity);
void emergency_reallocate_workers(struct player *pplayer, struct city *pcity);

enum ai_city_task { AICITY_NONE, AICITY_TECH, AICITY_TAX, AICITY_PROD};

#endif  /* FC__AICITY_H */
