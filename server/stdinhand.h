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
#ifndef FC__STDINHAND_H
#define FC__STDINHAND_H

/* common */
#include "connection.h"         /* enum cmdlevel */
#include "fc_types.h"

/* server */
#include "commands.h"
#include "console.h"

#define SERVER_COMMAND_PREFIX '/'
  /* the character to mark chatlines as server commands */

void stdinhand_init(void);
void stdinhand_turn(void);
void stdinhand_free(void);

void cmd_reply(enum command_id cmd, struct connection *caller,
               enum rfc_status rfc_status, const char *format, ...)
               fc__attribute((__format__ (__printf__, 4, 5)));

bool handle_stdin_input(struct connection *caller, const char *str,
                        bool check);
void set_ai_level_direct(struct player *pplayer, enum ai_level level);
void set_ai_level_directer(struct player *pplayer, enum ai_level level);
bool read_init_script(struct connection *caller, char *script_filename,
                      bool from_cmdline, bool check);
void show_players(struct connection *caller);

enum rfc_status create_command_newcomer(const char *name,
                                        const char *ai,
                                        bool check,
                                        struct nation_type *pnation,
                                        struct player **newplayer,
                                        char *buf, size_t buflen);
enum rfc_status create_command_pregame(const char *name,
                                       const char *ai,
                                       bool check,
                                       struct player **newplayer,
                                       char *buf, size_t buflen);

bool load_command(struct connection *caller,
		  const char *filename, bool check);
bool start_command(struct connection *caller, bool check, bool notify);

void toggle_ai_player_direct(struct connection *caller,
			     struct player *pplayer);

/* for sernet.c in initing a new connection */
enum cmdlevel access_level_for_next_connection(void);

void notify_if_first_access_level_is_available(void);

bool conn_is_kicked(struct connection *pconn, int *time_remaining);

#ifdef HAVE_LIBREADLINE
#ifdef HAVE_NEWLIBREADLINE
char **freeciv_completion(const char *text, int start, int end);
#else
char **freeciv_completion(char *text, int start, int end);
#endif
#endif

#endif /* FC__STDINHAND_H */
