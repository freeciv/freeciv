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
#ifndef FC__LOG_H
#define FC__LOG_H

#include "attribute.h"

#define LOG_FATAL  0
#define LOG_NORMAL 1
#define LOG_DEBUG  2

/* A function type to enable custom output of log messages other than
 * via fputs(stderr).  Eg, to the server console while handling prompts,
 * rfcstyle, client notifications; Eg, to the client window output window?
 */
typedef void (*log_callback_fn)(int, char*);

void log_init(char *filename, int initial_level, log_callback_fn callback);
void log_set_level(int level);

int freelog(int level, char *message, ...)
            fc__attribute((format (printf, 2, 3)));

#endif
