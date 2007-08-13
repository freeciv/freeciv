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
#include "unitlist.h"

void init_client_goto(void);
void free_client_goto(void);
void enter_goto_state(struct unit_list *punits);
void exit_goto_state(void);
bool goto_is_active(void);
struct tile *get_line_dest(void);
void goto_get_turns(int *min, int *max);
void goto_add_waypoint(void);
bool goto_pop_waypoint(void);
bool is_valid_goto_destination(struct tile *ptile);

void draw_line(struct tile *dest_tile);
bool is_drawn_line(struct tile *dest_tile, int dir);
 
bool is_endpoint(struct tile *ptile);

void request_orders_cleared(struct unit *punit);
void send_goto_path(struct unit *punit, struct pf_path *path,
		    struct unit_order *last_order);
bool send_goto_tile(struct unit *punit, struct tile *ptile);
void send_patrol_route(void);
void send_goto_route(void);
void send_connect_route(enum unit_activity activity);

struct pf_path *path_to_nearest_allied_city(struct unit *punit);

#endif /* FC__GOTO_H */
