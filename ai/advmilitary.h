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
#ifndef __ADVMILITARY_H
#define __ADVMILITARY_H

void military_advisor_choose_tech(struct player *pplayer,
				  struct ai_choice *choice);
void  military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				    struct ai_choice *choice);
int assess_danger(struct city *pcity);

#define THRESHOLD 12
#define DANGEROUS 1000

#include <map.h> /* just to allow MAP_MAX_ to be used */

struct move_cost_map {
  unsigned char cost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  unsigned char seacost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
/* I either need two arrays or >8 bits per element, so this is simplest -- Syela */
  struct city *warcity; /* so we know what we're dealing with here */
  struct unit *warunit; /* so we know what we're dealing with here */
};

#endif
