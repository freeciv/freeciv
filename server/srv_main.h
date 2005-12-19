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

#include "fc_types.h"
#include "game.h"
#include "packets.h"

struct connection;

BV_DEFINE(bv_draw, MAX_NUM_PLAYERS);

struct server_arguments {
  /* metaserver information */
  bool metaserver_no_send;
  char metaserver_addr[256];
  unsigned short int metaserver_port;
  /* address this server is to listen on (NULL => INADDR_ANY) */
  char *bind_addr;
  /* this server's listen port */
  int port;
  /* the log level */
  int loglevel;
  /* filenames */
  char *log_filename;
  char *gamelog_filename;
  char load_filename[512]; /* FIXME: may not be long enough? use MAX_PATH? */
  char *script_filename;
  char *saves_pathname;
  char serverid[256];
  /* save a ppm of the map? */
  bool save_ppm;
  /* quit if there no players after a given time interval */
  int quitidle;
  /* exit the server on game ending */
  bool exit_on_end;
  /* what kind of end game we should use */
  bv_draw draw;
  /* authentication options */
  bool auth_enabled;            /* defaults to FALSE */
  bool auth_allow_guests;       /* defaults to TRUE */
  bool auth_allow_newusers;     /* defaults to TRUE */
};

void init_game_seed(void);
void srv_init(void);
void srv_main(void);
void server_quit(void);

void save_game_auto(void);

bool handle_packet_input(struct connection *pconn, void *packet, int type);
void start_game(void);
void save_game(char *orig_filename);
void pick_ai_player_name(Nation_Type_id nation, char *newname);
void send_all_info(struct conn_list *dest);
void check_for_full_turn_done(void);

void dealloc_id(int id);
void alloc_id(int id);
int get_next_id_number(void);
void server_game_free(void);
void check_for_full_turn_done(void);

extern struct server_arguments srvarg;

extern enum server_states server_state;
extern bool nocity_send;

extern bool force_end_of_sniff;

#endif /* FC__SRV_MAIN_H */
