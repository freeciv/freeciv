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
#ifndef FC__AITOOLS_H
#define FC__AITOOLS_H

struct ai_choice;
struct city;
struct government;
struct player;

struct city *dist_nearest_city(struct player *pplayer, int x, int y,
                               int everywhere, int enemy);

void ai_government_change(struct player *pplayer, int gov);

int ai_gold_reserve(struct player *pplayer);

void adjust_choice(int value, struct ai_choice *choice);
void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best);
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice);
int ai_assess_military_unhappiness(struct city *pcity, struct government *g);

int ai_evaluate_government(struct player *pplayer, struct government *g);

#endif  /* FC__AITOOLS_H */
