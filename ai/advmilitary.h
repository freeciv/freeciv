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
#ifndef FC__ADVMILITARY_H
#define FC__ADVMILITARY_H

struct city;
struct player;
struct unit;
struct ai_choice;

void military_advisor_choose_tech(struct player *pplayer,
				  struct ai_choice *choice);
void  military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				    struct ai_choice *choice);
void assess_danger_player(struct player *pplayer);
int assess_danger(struct city *pcity);
void establish_city_distances(struct player *pplayer, struct city *pcity);
int assess_defense_quadratic(struct city *pcity);
int assess_defense_unit(struct city *pcity, struct unit *punit, int igwall);
int assess_defense(struct city *pcity);
int unit_desirability(Unit_Type_id i, int def);
int unit_attack_desirability(Unit_Type_id i);

#endif  /* FC__ADVMILITARY_H */
