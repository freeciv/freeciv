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
#ifndef __SERVER_H
#define __SERVER_H

#include "game.h"
#include "packets.h"


struct connection;
struct unit;

void handle_stdin_input(char *str);
void handle_alloc_race(int player_no, struct packet_alloc_race *packet);
void handle_packet_input(struct connection *pconn, char *packet, int type);
void lost_connection_to_player(struct connection *pconn);
void handle_request_move_unit(int player_no, struct packet_move_unit *packet);
void handle_request_join_game(struct connection *pconn, 
			      struct packet_req_join_game *request);
void handle_turn_done(int player_no);
void accept_new_player(char *name, struct connection *pconn);

void send_year_to_clients(int year);
void send_game_info(struct player *dest);
int check_for_full_turn_done(void);
void send_server_info_to_metaserver(int do_send);


void show_players(void);
void show_help(void);
void quit_game(void);
void start_game(void);
void cut_player_connection(char *playername);
void send_select_race(struct player *pplayer);
void lighten_area(struct player *pplayer, int x, int y);
void notify_player(struct player *p, char *format, ...);
void update_pollution();
void save_game(void);

void dealloc_id(int id);
int is_id_allocated(int id);
void alloc_id(int id);
int get_next_id_number(void);

extern enum server_states server_state;
extern int nocity_send;

/* Cedric's stuff ... */
void generate_ai_players();
void pick_ai_player_name(enum race_type race, char *newname);
int mark_race_as_used(int race);
void announce_ai_player(struct player *pplayer);

#endif


