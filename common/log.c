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
#include <stdio.h>
#include <stdarg.h>

#include <log.h>

int log_level;

char *log_filename;

/**************************************************************************/
void log_init(char *filename)
{
  log_level=LOG_NORMAL;
  log_filename=filename;
/*  flog(LOG_NORMAL, "log started");*/
}

/**************************************************************************/
void log_set_level(int level)
{
  log_level=level;
}

/**************************************************************************/
int flog(int level, char *message, ...)
{

  if(level<=log_level) {
    va_list args;
    char buf[512];
    FILE *fs;

    if(log_filename) {
      if(!(fs=fopen(log_filename, "a+b"))) {
	fprintf(stderr, "couldn't open logfile: %s for appending.\n", 
		log_filename);
	exit(1);
      }
    }
    else fs=stderr;

    va_start(args, message);
    vsprintf(buf, message, args);
    fprintf(fs, "%d: %s\n", level, buf);
    fflush(fs);
    if(log_filename)
      fclose(fs);
  }

  return 1;
}





