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
#ifndef __CIVCLIENT_H
#define __CIVCLIENT_H

#include <packets.h>
#include <game.h>

void handle_packet_input(char *packet, int type);
void user_ended_turn(void);
void handle_select_race(struct packet_generic_integer *packet);
void handle_player_info(struct packet_player_info *packet);
void handle_game_info(struct packet_game_info *packet);
void handle_map_info(struct packet_map_info *pinfo);
void handle_unit_info(struct packet_unit_info *packet);
void handle_tile_info(struct packet_tile_info *packet);
void handle_chat_msg(struct packet_generic_message *packet);
void send_unit_info(struct unit *punit);
void send_report_request(GtkWidget *widget, enum report_type type);

void set_client_state(enum client_states newstate);
enum client_states get_client_state(void);
extern int turn_gold_difference;

#endif
