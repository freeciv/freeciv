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

/* utility */
#include "log.h"        /* enum log_level */
#include "netintf.h"

/* common */
#include "fc_types.h"

struct conn_list;

struct server_arguments {
  /* metaserver information */
  bool metaserver_no_send;
  char metaserver_addr[256];
  char metaserver_name[256];
  unsigned short int metaserver_port;
  /* address this server is to listen on (NULL => INADDR_ANY) */
  char *bind_addr;
  /* this server's listen port */
  int port;
  /* the log level */
  enum log_level loglevel;
  /* filenames */
  char *log_filename;
  char *ranklog_filename;
  char load_filename[512]; /* FIXME: may not be long enough? use MAX_PATH? */
  char *script_filename;
  char *saves_pathname;
  char *scenarios_pathname;
  char serverid[256];
  /* save a ppm of the map? */
  bool save_ppm;
  /* quit if there no players after a given time interval */
  int quitidle;
  /* exit the server on game ending */
  bool exit_on_end;
  /* authentication options */
  bool auth_enabled;            /* defaults to FALSE */
  char *auth_conf;              /* auth configuration file */
  bool auth_allow_guests;       /* defaults to TRUE */
  bool auth_allow_newusers;     /* defaults to TRUE */
  enum announce_type announce;
  int fatal_assertions;         /* default to -1 (disabled). */
};

/* used in savegame values */
#define SPECENUM_NAME server_states
#define SPECENUM_VALUE0 S_S_INITIAL
#define SPECENUM_VALUE1 S_S_RUNNING
#define SPECENUM_VALUE2 S_S_OVER
#include "specenum_gen.h"

/* Structure for holding global server data.
 *
 * TODO: Lots more variables could be added here. */
extern struct civserver {
  int playable_nations;
  int nbarbarians;

  /* this counter creates all the city and unit numbers used.
   * arbitrarily starts at 101, but at 65K wraps to 1.
   * use identity_number()
   */
#define IDENTITY_NUMBER_SKIP (100)
  unsigned short identity_number;

  char game_identifier[MAX_LEN_GAME_IDENTIFIER];
} server;


void init_game_seed(void);
void srv_init(void);
void srv_main(void);
void server_quit(void);
void save_game_auto(const char *save_reason, const char *reason_filename);

enum server_states server_state(void);
void set_server_state(enum server_states newstate);

void check_for_full_turn_done(void);
bool check_for_game_over(void);
bool game_was_started(void);

bool server_packet_input(struct connection *pconn, void *packet, int type);
void start_game(void);
void save_game(const char *orig_filename, const char *save_reason,
               bool scenario);
const char *pick_random_player_name(const struct nation_type *pnation);
void player_nation_defaults(struct player *pplayer, struct nation_type *pnation,
                            bool set_name);
void send_all_info(struct conn_list *dest);

void identity_number_release(int id);
void identity_number_reserve(int id);
int identity_number(void);
void server_game_init(void);
void server_game_free(void);
void aifill(int amount);

extern struct server_arguments srvarg;

extern bool force_end_of_sniff;

void init_available_nations(void);
#endif /* FC__SRV_MAIN_H */
