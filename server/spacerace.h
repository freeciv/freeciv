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
#ifndef FC__SPACERACE_H
#define FC__SPACERACE_H

struct player;
struct player_spaceship;
struct packet_spaceship_action;

void spaceship_calc_derived(struct player_spaceship *ship);
void send_spaceship_info(struct player *src, struct player *dest);
void spaceship_lost(struct player *pplayer);
void check_spaceship_arrivals(void);

void handle_spaceship_action(struct player *pplayer, 
			     struct packet_spaceship_action *packet);
void handle_spaceship_launch(struct player *pplayer);

#endif /* FC__SPACERACE_H */
