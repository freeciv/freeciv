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
#ifndef FC__GOTO_H
#define FC__GOTO_H

#include "map.h"

#include "path_finding.h"

void init_client_goto(void);
void free_client_goto(void);
void enter_goto_state(struct unit *punit);
void exit_goto_state(void);
bool goto_is_active(void);
void get_line_dest(int *x, int *y);
int get_goto_turns(void);
void goto_add_waypoint(void);
bool goto_pop_waypoint(void);

void draw_line(int dest_x, int dest_y);
bool is_drawn_line(int x, int y, int dir);
 
bool is_endpoint(int x, int y);

void request_orders_cleared(struct unit *punit);
void send_goto_path(struct unit *punit, struct pf_path *path,
		    enum unit_activity final_activity);
void send_patrol_route(struct unit *punit);
void send_goto_route(struct unit *punit);

struct pf_path *path_to_nearest_allied_city(struct unit *punit);

#endif /* FC__GOTO_H */
