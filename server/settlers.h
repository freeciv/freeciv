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

#include "map.h"

struct player;
struct unit;
struct city;

int auto_settler_do_goto(struct player *pplayer, struct unit *punit, 
			 int x, int y);
void auto_settlers(void);
int find_boat(struct player *pplayer, int *x, int *y, int cap);

#define MORT 24

int amortize(int benefit, int delay);
void ai_manage_settler(struct player *pplayer, struct unit *punit);

int is_ok_city_spot(int x, int y); /* laughable, really. */
void generate_minimap(void);
void remove_city_from_minimap(int x, int y);
void add_city_to_minimap(int x, int y);
void initialize_infrastructure_cache(struct city *pcity);

void contemplate_terrain_improvements(struct city *pcity);
void contemplate_new_city(struct city *pcity);

struct unit *other_passengers(struct unit *punit);

extern signed int minimap[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];

#endif   /* FC__SETTLERS_H */
