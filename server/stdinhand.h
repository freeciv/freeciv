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

#include "connection.h"		/* enum cmdlevel_id */

struct player;

#define SERVER_COMMAND_PREFIX '/'
  /* the character to mark chatlines as server commands */

void handle_stdin_input(struct connection *caller, char *str);
void report_server_options(struct conn_list *dest, int which);
void set_ai_level_direct(struct player *pplayer, int level);
void set_ai_level_directer(struct player *pplayer, int level);
void read_init_script(char *script_filename);
void show_players(struct connection *caller);

void toggle_ai_player_direct(struct connection *caller, struct player *subject);

#ifdef HAVE_LIBREADLINE
char **freeciv_completion(char *text, int start, int end);
#endif

extern enum cmdlevel_id default_access_level;  /* for sernet.c in */
extern enum cmdlevel_id   first_access_level;  /* initing a new connection */

#endif /* FC__STDINHAND_H */
