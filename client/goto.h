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

struct client_goto_map {
  short **move_cost;
  char **vector;
  char **returned;
  unsigned char **drawn; /* Should not be modified directly. */
  int unit_id; /* The unit of the goto map */
  int src_x, src_y;
};

extern struct client_goto_map goto_map;

void init_client_goto(void);
void enter_goto_state(struct unit *punit);
void exit_goto_state(void);
int goto_is_active(void);
void get_line_dest(int *x, int *y);
void goto_add_waypoint(void);
int goto_pop_waypoint(void);

void draw_line(int dest_x, int dest_y);
int get_drawn(int x, int y, int dir);
void increment_drawn(int x, int y, int dir);
void decrement_drawn(int x, int y, int dir);

void send_patrol_route(struct unit *punit);
void send_goto_route(struct unit *punit);

#endif /* FC__GOTO_H */
