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

#include "player.h"		/* struct player; enum cmdlevel_id */

#define SERVER_COMMAND_PREFIX '/'
  /* the character to mark chatlines as server commands */

void handle_stdin_input(struct player *caller, char *str);
void report_server_options(struct player *pplayer, int which);
void set_ai_level_direct(struct player *pplayer, int level);

extern enum cmdlevel_id default_access_level;

#endif /* FC__STDINHAND_H */
