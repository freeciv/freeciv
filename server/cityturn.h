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

#ifndef FC__CITYTURN_H
#define FC__CITYTURN_H

struct city;
struct player;
struct unit;

int unit_being_aggressive(struct unit *punit);

int advisor_choose_build(struct city *pcity);  /* used by the AI */
int city_refresh(struct city *pcity);          /* call if city has changed */
void global_city_refresh(struct player *pplayer); /* tax/govt changed */

void worker_loop(struct city *pcity, int *foodneed, int *prodneed, int *workers);
void auto_arrange_workers(struct city *pcity); /* will arrange the workers */
int  add_adjust_workers(struct city *pcity);   /* will add workers */
void city_check_workers(struct player *pplayer, struct city *pcity);

/**** This part isn't meant to be used ****/  /* huh? which part?  --dwp */

void city_auto_remove_worker(struct city *pcity); 
void update_city_activities(struct player *pplayer);
void city_incite_cost(struct city *pcity);
void remove_obsolete_buildings(struct player *plr);

#endif  /* FC__CITYTURN_H */
