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
#ifndef FC__PACKHAND_H
#define FC__PACKHAND_H

#include "packets.h"

void handle_join_game_reply(struct packet_join_game_reply *packet);

void handle_remove_city(struct packet_generic_integer *packet);
void handle_remove_unit(struct packet_generic_integer *packet);
void handle_incite_cost(struct packet_generic_values *packet);

void handle_city_options(struct packet_generic_values *preq);

void handle_spaceship_info(struct packet_spaceship_info *packet);

void popdown_races_dialog(void);

#endif /* FC__PACKHAND_H */
