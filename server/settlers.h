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

#include "fc_types.h"
#include "map.h"

void auto_settlers_init(void);
void auto_settlers_player(struct player *pplayer);
int find_boat(struct player *pplayer, struct tile **boat_tile, int cap);

#define MORT 24

int amortize(int benefit, int delay);
void ai_manage_settler(struct player *pplayer, struct unit *punit);

void init_settlers(void);

int city_tile_value(struct city *pcity, int x, int y,
		    int foodneed, int prodneed);
void initialize_infrastructure_cache(struct player *pplayer);

void contemplate_terrain_improvements(struct city *pcity);
void contemplate_new_city(struct city *pcity);

struct unit *other_passengers(struct unit *punit);

extern signed int *minimap;
#define MINIMAP(ptile) minimap[(ptile)->index]

#endif   /* FC__SETTLERS_H */
