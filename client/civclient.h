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

void user_ended_turn(void);

void set_client_state(enum client_states newstate);
enum client_states get_client_state(void);

void client_remove_cli_conn(struct connection *pconn);

extern int turn_gold_difference;

#endif  /* FC__CIVCLIENT_H */
