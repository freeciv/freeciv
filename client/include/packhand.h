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

void handle_move_unit(struct packet_move_unit *packet);
void handle_new_year(struct packet_new_year *ppacket);
void handle_city_info(struct packet_city_info *packet);
void handle_unit_combat(struct packet_unit_combat *packet);
void handle_game_state(struct packet_generic_integer *packet);
void handle_nuke_tile(struct packet_nuke_tile *packet);
void handle_page_msg(struct packet_generic_message *packet);
void handle_before_new_year(void);
void handle_remove_player(struct packet_generic_integer *packet);
void handle_ruleset_unit(struct packet_ruleset_unit *packet);
void handle_ruleset_tech(struct packet_ruleset_tech *packet);
void handle_ruleset_building(struct packet_ruleset_building *packet);
void handle_ruleset_terrain(struct packet_ruleset_terrain *packet);
void handle_ruleset_terrain_control(struct terrain_misc *packet);

#endif /* FC__PACKHAND_H */
