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

#ifndef __CITYTURN_H
#define __CITYTURN_H

#include "packets.h"
#include "city.h"

int advisor_choose_build(struct city *pcity);  /* used by the AI */
int city_refresh(struct city *pcity);          /* call if city has changed */
					       
void auto_arrange_workers(struct city *pcity); /* will arrange the workers */
int  add_adjust_workers(struct city *pcity);   /* will add workers */
void city_check_workers(struct player *pplayer, struct city *pcity);

/**** This part isn't meant to be used ****/

void city_auto_remove_worker(struct city *pcity); 
void set_worker_city(struct city *pcity, int x, int y, 
		     enum city_tile_type type); 
void update_city_activities(struct player *pplayer);
void city_incite_cost(struct city *pcity);
void remove_obsolete_buildings(struct player *plr);

#endif








