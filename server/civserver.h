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
#ifndef FC__CIVSERVER_H
#define FC__CIVSERVER_H

#include "game.h"
#include "packets.h"


struct connection;
struct unit;

void handle_alloc_race(int player_no, struct packet_alloc_race *packet);
void handle_packet_input(struct connection *pconn, char *packet, int type);
void lost_connection_to_player(struct connection *pconn);
void handle_request_join_game(struct connection *pconn, 
			      struct packet_req_join_game *request);
void handle_turn_done(int player_no);
void accept_new_player(char *name, struct connection *pconn);
int check_for_full_turn_done(void);
int send_server_info_to_metaserver(int do_send);
void start_game(void);
void send_select_race(struct player *pplayer);
void save_game(char *filename);
void save_game_auto(void);
void dealloc_id(int id);
int is_id_allocated(int id);
void alloc_id(int id);
int get_next_id_number(void);
void generate_ai_players(void);
void pick_ai_player_name(enum race_type race, char *newname);
int mark_race_as_used(int race);
void announce_ai_player(struct player *pplayer);

extern enum server_states server_state;
extern int nocity_send;

#endif /* FC__CIVSERVER_H */
