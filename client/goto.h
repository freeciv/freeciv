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
  char **drawn;
  int unit_id; /* The unit of the goto map */
  int src_x, src_y;
};

extern struct client_goto_map goto_map;
extern int line_dest_x;
extern int line_dest_y;

void create_goto_map(struct unit *punit, int src_x, int src_y,
		     enum goto_move_restriction restriction);
int transfer_route_to_stack(int dest_x, int dest_y);
int transfer_route_to_stack_circular(int dest_x, int dest_y);
void init_client_goto(void);

void draw_line(int dest_x, int dest_y);
void undraw_line(void);

void goto_array_clear(void);
void goto_array_insert(int x, int y);
void goto_array_send(struct unit *punit);

#endif /* FC__GOTO_H */
