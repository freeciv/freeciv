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
#ifndef FC__SRV_MAIN_H
#define FC__SRV_MAIN_H

#include "game.h"
#include "packets.h"

struct connection;
struct unit;

struct server_arguments {
  /* metaserver information */
  int metaserver_no_send;
  char metaserver_info_line[256];
  char metaserver_addr[256];
  unsigned short int metaserver_port;
  /* this server's listen port */
  int port;
  /* the log level */
  int loglevel;
  /* filenames */
  char *log_filename;
  char *gamelog_filename;
  char *load_filename;
  char *script_filename;
  /* server name for metaserver to use for us */
  char metaserver_servername[64];
};

void srv_init(void);
void srv_main(void);

void handle_packet_input(struct connection *pconn, char *packet, int type);
void lost_connection_to_client(struct connection *pconn);
void accept_new_player(char *name, struct connection *pconn);
void start_game(void);
void save_game(char *filename);
void pick_ai_player_name(Nation_Type_id nation, char *newname);

void dealloc_id(int id);
int is_id_allocated(int id);
void alloc_id(int id);
int get_next_id_number(void);

extern struct server_arguments srvarg;

extern enum server_states server_state;
extern int nocity_send;

extern int force_end_of_sniff;

#endif /* FC__SRV_MAIN_H */
