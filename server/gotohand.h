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

enum goto_move_restriction {
  GOTO_MOVE_ANY, GOTO_MOVE_CARDINAL_ONLY, GOTO_MOVE_STRAIGHTEST
};

void do_unit_goto(struct unit *punit, enum goto_move_restriction restriction);
void generate_warmap(struct city *pcity, struct unit *punit);
void really_generate_warmap(struct city *pcity, struct unit *punit,
			    enum unit_move_type which);
int calculate_move_cost(struct unit *punit, int dest_x, int dest_y);
int air_can_move_between(int moves, int src_x, int src_y,
			 int dest_x, int dest_y, int playerid);

/* all other functions are internal */

#define THRESHOLD 12


int goto_is_sane(struct unit *punit, int x, int y, int omni);

struct move_cost_map {
  unsigned char *cost[MAP_MAX_WIDTH];
  unsigned char *seacost[MAP_MAX_WIDTH];
  unsigned char *vector[MAP_MAX_WIDTH];
  unsigned char *returned[MAP_MAX_WIDTH];
  struct city *warcity; /* so we know what we're dealing with here */
  struct unit *warunit; /* so we know what we're dealing with here */
  int orig_x, orig_y;
};

#endif  /* FC__GOTOHAND_H */
