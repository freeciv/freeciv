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
#ifndef FC__GAMEHAND_H
#define FC__GAMEHAND_H

struct player;
struct section_file;
struct conn_list;

void init_new_game(void);
void send_year_to_clients(int year);
void send_game_info(struct player *dest);
void send_game_state(struct conn_list *dest, int state);

void game_load(struct section_file *file);
void game_save(struct section_file *file);

#endif  /* FC__GAMEHAND_H */
