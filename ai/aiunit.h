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
#ifndef FC__AIUNIT_H
#define FC__AIUNIT_H

#include "unittype.h"

struct player;
struct city;
struct unit;
struct ai_choice;

void ai_manage_units(struct player *pplayer); 
int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y);
int look_for_charge(struct player *pplayer, struct unit *punit,
                    struct unit **aunit, struct city **acity);

int ai_manage_explorer(struct unit *punit);

int find_something_to_kill(struct player *pplayer, struct unit *punit, 
                            int *x, int *y);
int find_beachhead(struct unit *punit, int dest_x, int dest_y, int *x, int *y);

int unit_belligerence_basic(struct unit *punit);
int unit_belligerence(struct unit *punit);
int unit_vulnerability_basic(struct unit *punit, struct unit *pdef);
int unit_vulnerability_virtual(struct unit *punit);
int unit_vulnerability(struct unit *punit, struct unit *pdef);
int military_amortize(int value, int delay, int build_cost);

int is_on_unit_upgrade_path(Unit_Type_id test, Unit_Type_id base);
int is_ai_simple_military(Unit_Type_id type);

Unit_Type_id ai_wants_role_unit(struct player *pplayer, struct city *pcity,
                                int role, int want);
void ai_choose_role_unit(struct player *pplayer, struct city *pcity,
			 struct ai_choice *choice, int role, int want);

#endif  /* FC__AIUNIT_H */
