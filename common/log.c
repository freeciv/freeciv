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
#include <string.h>

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
  static char bufbuf[2][512];
  static int whichbuf=0;
  static unsigned int repeated=0; /* total times current message repeated */
  static unsigned int next=1;	/* next total to print update */
  static unsigned int prev=0;	/* total on last update */

  if(level<=log_level) {
    va_list args;
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
    vsprintf(bufbuf[whichbuf], message, args);
    if(0==strncmp(bufbuf[0],bufbuf[1],511)){
      repeated++;
      if(repeated==next){
	fprintf(fs, "%d: last message repeated %d times (total %d repeats)\n",
		level, repeated-prev, repeated);
	prev=repeated;
	next<<=1;
	if(next==2) next=4;
      }
    }else{
      if(repeated>0 && repeated!=prev){
	fprintf(fs, "%d: last message repeated %d times (total %d repeats)\n",
		level, repeated-prev, repeated);	
      }
      repeated=0;
      next=1;
      prev=0;
      fprintf(fs, "%d: %s\n", level, bufbuf[whichbuf]);
    }
    whichbuf= !whichbuf;
    fflush(fs);
    if(log_filename)
      fclose(fs);
  }

  return 1;
}





