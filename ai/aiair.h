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
#ifndef FC__AIAIR_H
#define FC__AIAIR_H


void ai_manage_airunit(struct player *pplayer, struct unit *punit);
bool ai_choose_attacker_air(struct player *pplayer, struct city *pcity, 
			    struct ai_choice *choice);

int ai_evaluate_government(struct player *pplayer, struct government *g);
int ai_evaluate_tile_for_attack(struct unit *punit,
                                int dest_x, int dest_y);
bool find_something_to_bomb(struct unit *punit, int x, int y);
int find_nearest_airbase(int x, int y, struct unit *punit,
                         int *xref, int *yref);

#endif /* FC__AIAIR_H */
