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
int ai_want_settlers(struct player *pplayer, int cont);

void ai_manage_military(struct player *pplayer,struct unit *punit);
void ai_military_findjob(struct player *pplayer,struct unit *punit);
void ai_military_gohome(struct player *pplayer,struct unit *punit);
void ai_military_attack(struct player *pplayer,struct unit *punit);

void ai_manage_unit(struct player *pplayer, struct unit *punit);
void ai_manage_settler(struct player *pplayer, struct unit *punit);
void ai_manage_caravan(struct player *pplayer, struct unit *punit);
void find_something_to_kill(struct player *pplayer, struct unit *punit, 
                            int *x, int *y);
int get_cities_on_island(struct player *pplayer, int cont);
int get_settlers_on_island(struct player *pplayer, int cont);
const char *get_a_name(struct player *pplayer);


#endif
