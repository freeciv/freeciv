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
#ifndef FC__CIVCLIENT_H
#define FC__CIVCLIENT_H

#include "packets.h"		/* enum report_type */
#include "game.h"		/* enum client_states */

void handle_packet_input(char *packet, int type);

void send_unit_info(struct unit *punit);
void send_move_unit(struct unit *punit);
void send_goto_unit(struct unit *punit, int dest_x, int dest_y);
void send_report_request(enum report_type type);
void send_attribute_block_request(void);
void send_turn_done(void);

void user_ended_turn(void);

void set_client_state(enum client_states newstate);
enum client_states get_client_state(void);

void client_remove_cli_conn(struct connection *pconn);

extern int turn_gold_difference;
extern int seconds_to_turndone;
extern int last_turn_gold_amount;
extern int did_advance_tech_this_turn;

extern char metaserver[256];
extern char server_host[512];
extern char player_name[512];
extern int server_port;
extern int auto_connect;

#endif  /* FC__CIVCLIENT_H */
