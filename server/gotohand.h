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

/* 
 * The below GOTO result values are ordered by priority, e.g. if unit
 * fought and run out of movepoints, GR_OUT_OF_MOVEPOINTS should be
 * returned.  Not that it is of any importance...
 */
enum goto_result {
  GR_DIED,               /* pretty obvious that */ 
  GR_ARRIVED,            /* arrived to the destination */
  GR_OUT_OF_MOVEPOINTS,  /* either no moves left or plane refueling */ 
  GR_FOUGHT,             /* was stopped due to fighting, has moves */
  GR_FAILED              /* failed for some other reason, has moves */
};

enum goto_result do_unit_goto(struct unit *punit,
			      enum goto_move_restriction restriction,
			      int trigger_special_ability);
void generate_warmap(struct city *pcity, struct unit *punit);
void really_generate_warmap(struct city *pcity, struct unit *punit,
			    enum unit_move_type move_type);
int calculate_move_cost(struct unit *punit, int dest_x, int dest_y);
int air_can_move_between(int moves, int src_x, int src_y,
			 int dest_x, int dest_y, struct player *pplayer);

/* all other functions are internal */

#define THRESHOLD 12


int goto_is_sane(struct unit *punit, int x, int y, int omni);

struct move_cost_map {
  unsigned char *cost[MAP_MAX_WIDTH];
  unsigned char *seacost[MAP_MAX_WIDTH];
  unsigned char *vector[MAP_MAX_WIDTH];
  struct city *warcity; /* so we know what we're dealing with here */
  struct unit *warunit; /* so we know what we're dealing with here */
  int orig_x, orig_y;
};

extern struct move_cost_map warmap;

#endif  /* FC__GOTOHAND_H */
