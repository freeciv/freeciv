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
#ifndef FC__SETTLERS_H
#define FC__SETTLERS_H

#include "unit.h"

int auto_settler_do_goto(struct player *pplayer, struct unit *punit, 
			 int x, int y);
int auto_settler_findwork(struct player *pplayer, struct unit *punit); 
void auto_settlers_player(struct player *pplayer); 
void auto_settlers(void);
int find_boat(struct player *pplayer, int *x, int *y, int cap);

#define MORT 24

const char *get_a_name(struct player *pplayer);
int amortize(int b, int d);
int city_desirability(struct player *pplayer, int x, int y);
void ai_manage_settler(struct player *pplayer, struct unit *punit);

int is_already_assigned(struct unit *myunit, struct player *pplayer, 
			int x, int y);
int ai_calc_pollution(struct city *pcity, struct player *pplayer, int i, int j);
int ai_calc_mine(struct city *pcity, struct player *pplayer, int i, int j);
int ai_calc_road(struct city *pcity, struct player *pplayer, int i, int j);
int ai_calc_railroad(struct city *pcity, struct player *pplayer, int i, int j);
int ai_calc_irrigate(struct city *pcity, struct player *pplayer, int i, int j);
int ai_calc_transform(struct city *pcity, struct player *pplayer, int i, int j);
int in_city_radius(int x, int y);
int is_ok_city_spot(int x, int y); /* laughable, really. */
int make_dy(int y1, int y2);
int make_dx(int x1, int x2);
void generate_minimap(void);
void remove_city_from_minimap(int x, int y);
void add_city_to_minimap(int x, int y);
void locally_zero_minimap(int x, int y); /* I should imp this someday -- Syela */
void initialize_infrastructure_cache(struct city *pcity);
void contemplate_settling(struct player *pplayer, struct city *pcity);
struct unit *other_passengers(struct unit *punit);

#endif   /* FC__SETTLERS_H */
