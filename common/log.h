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
#ifndef __LOG_
#define __LOG_

#define LOG_FATAL  0
#define LOG_NORMAL 1
#define LOG_DEBUG  2

void log_init(char *filename);
int log(int level, char *message, ...);
void log_set_level(int level);
void log_kill(void);

#endif
