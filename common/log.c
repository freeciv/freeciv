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
log_callback_fn log_callback;

/**************************************************************************
Initialise the log module.
Either 'filename' or 'callback' may be NULL.
If both are NULL, print to stderr.
If both are non-NULL, both callback, and fprintf to file.
**************************************************************************/
void log_init(char *filename, int initial_level, log_callback_fn callback)
{
  log_level=initial_level;
  log_filename=filename;
  log_callback=callback;
  /* freelog(LOG_NORMAL, "log started"); */
}

/**************************************************************************
Adjust the logging level after initial log_init().
**************************************************************************/
void log_set_level(int level)
{
  log_level=level;
}

/**************************************************************************
Unconditionally print a simple string.
Let the callback do its own level formating and add a '\n' if it wants.
**************************************************************************/
static void log_write(FILE *fs, int level, char *message)
{
  if (log_callback) {
    log_callback(level, message);
  }
  if (log_filename || (!log_callback)) {
    fprintf(fs, "%d: %s\n", level, message);
  }
}

/**************************************************************************
Print a log message.
Only prints if level <= log_level.
For repeat message, may wait and print instead
"last message repeated ..." at some later time.
Calls log_callback if non-null, else prints to stderr.
**************************************************************************/
int freelog(int level, char *message, ...)
{
  static char bufbuf[2][512];
  char buf[512];
  static int whichbuf=0;
  static unsigned int repeated=0; /* total times current message repeated */
  static unsigned int next=2;	/* next total to print update */
  static unsigned int prev=0;	/* total on last update */
  static int prev_level=-1;	/* only count as repeat if same level  */

  if(level<=log_level) {
    va_list args;
    FILE *fs;

    if(log_filename) {
      if(!(fs=fopen(log_filename, "a"))) {
	fprintf(stderr, "couldn't open logfile: %s for appending.\n", 
		log_filename);
	exit(1);
      }
    }
    else fs=stderr;

    va_start(args, message);
    vsprintf(bufbuf[whichbuf], message, args);
    va_end(args);
    
    if(level==prev_level && 0==strncmp(bufbuf[0],bufbuf[1],511)){
      repeated++;
      if(repeated==next){
	sprintf(buf, "last message repeated %d times", repeated-prev);
	if (repeated>2) {
	  sprintf(buf+strlen(buf), " (total %d repeats)", repeated);
	}
	log_write(fs, level, buf);
	prev=repeated;
	next*=2;
      }
    }else{
      if(repeated>0 && repeated!=prev){
	if(repeated==1) {
	  /* just repeat the previous message: */
	  log_write(fs, level, bufbuf[!whichbuf]);
	} else {
	  if(repeated-prev==1) {
	    strcpy(buf, "last message repeated once");
	  } else {
	    sprintf(buf, "last message repeated %d times",  repeated-prev);
	  }
	  if (repeated>2) {
	    sprintf(buf+strlen(buf), " (total %d repeats)", repeated);
	  }
	  log_write(fs, level, buf);
	}
      }
      prev_level=level;
      repeated=0;
      next=2;
      prev=0;
      log_write(fs, level, bufbuf[whichbuf]);
    }
    whichbuf= !whichbuf;
    fflush(fs);
    if(log_filename)
      fclose(fs);
  }

  return 1;
}
