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
#ifndef FC__GOTOHAND_H
#define FC__GOTOHAND_H

#include "map.h"		/* MAP_MAX_ */

void do_unit_goto(struct player *pplayer, struct unit *punit);
void generate_warmap(struct city *pcity, struct unit *punit);
void really_generate_warmap(struct city *pcity, struct unit *punit,
			    enum unit_move_type which);
int calculate_move_cost(struct player *pplayer, struct unit *punit,
			int dest_x, int dest_y);

/* all other functions are internal */

#define THRESHOLD 12


int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y);
int goto_is_sane(struct player *pplayer, struct unit *punit, int x, int y, int omni);

struct move_cost_map {
  unsigned char cost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  unsigned char seacost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  unsigned char vector[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
  struct city *warcity; /* so we know what we're dealing with here */
  struct unit *warunit; /* so we know what we're dealing with here */
  int orig_x, orig_y;
};

#endif  /* FC__GOTOHAND_H */
