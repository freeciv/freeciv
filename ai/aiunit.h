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
#ifndef __AIUNIT_H
#define __AIUNIT_H

enum ai_unit_task { AIUNIT_NONE, AIUNIT_AUTO_SETTLER, AIUNIT_BUILD_CITY,
                    AIUNIT_DEFEND_HOME, AIUNIT_ATTACK, AIUNIT_FORTIFY,
                    AIUNIT_RUNAWAY, AIUNIT_ESCORT, AIUNIT_EXPLORE};

void ai_manage_units(struct player *pplayer); 

int look_for_charge(struct player *pplayer, struct unit *punit,
                    struct unit **aunit, struct city **acity);

void ai_manage_explorer(struct player *pplayer,struct unit *punit);
void ai_manage_military(struct player *pplayer,struct unit *punit);
void ai_military_findjob(struct player *pplayer,struct unit *punit);
void ai_military_gohome(struct player *pplayer,struct unit *punit);
void ai_military_attack(struct player *pplayer,struct unit *punit);

void ai_manage_unit(struct player *pplayer, struct unit *punit);
void ai_manage_caravan(struct player *pplayer, struct unit *punit);
int find_something_to_kill(struct player *pplayer, struct unit *punit, 
                            int *x, int *y);
int find_beachhead(struct unit *punit, int dest_x, int dest_y, int *x, int *y);

int unit_move_turns(struct unit *punit, int x, int y);
int unit_belligerence_basic(struct unit *punit);
int unit_belligerence(struct unit *punit);
int unit_vulnerability_basic(struct unit *punit, struct unit *pdef);
int unit_vulnerability_virtual(struct unit *punit);
int unit_vulnerability(struct unit *punit, struct unit *pdef);
int unit_can_defend(int type);

int is_ai_simple_military(int type);

#endif
