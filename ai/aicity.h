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
#ifndef __AICITY_H
#define __AICITY_H

void ai_manage_cities(struct player *pplayer);
int city_get_buildings(struct city *pcity);
int city_get_defenders(struct city *pcity);
int ai_in_initial_expand(struct player *pplayer);
int ai_choose_defender_versus(struct city *pcity, int v);
int ai_choose_defender_limited(struct city *pcity, int n);
int ai_choose_defender(struct city *pcity);

enum ai_city_task { AICITY_NONE, AICITY_TECH, AICITY_TAX, AICITY_PROD};

#endif
