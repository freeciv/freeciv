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

#include <stdarg.h>

#include "attribute.h"

#define LOG_FATAL   0
#define LOG_ERROR   1		/* non-fatal errors */
#define LOG_NORMAL  2
#define LOG_VERBOSE 3		/* not shown by default */
#define LOG_DEBUG   4		/* suppressed unless DEBUG defined;
				   may be enabled on file/line basis */

/* Some variables local to each file which includes log.h,
   to record whether LOG_DEBUG messages apply for that file
   and if so for which lines (min,max) :
*/
struct logdebug_afile_info {
  int tthis;
  int min;
  int max;
};
#ifdef DEBUG
static int logdebug_this_init;
static struct logdebug_afile_info logdebug_thisfile;
#endif

extern int logd_init_counter;   /* increment this to force re-init */

/* Return an updated struct logdebug_afile_info: */
struct logdebug_afile_info logdebug_update(const char *file);


/* A function type to enable custom output of log messages other than
 * via fputs(stderr).  Eg, to the server console while handling prompts,
 * rfcstyle, client notifications; Eg, to the client window output window?
 */
typedef void (*log_callback_fn)(int, char*);

int log_parse_level_str(char *level_str);
void log_init(char *filename, int initial_level, log_callback_fn callback);
void log_set_level(int level);

void real_freelog(int level, const char *message, ...)
                  fc__attribute((format (printf, 2, 3)));
void vreal_freelog(int level, const char *message, va_list ap);


/* A static (per-file) function to use/update the above per-file vars.
 * This should only be called for LOG_DEBUG messages.
 * It returns whether such a LOG_DEBUG message should be sent on
 * to real_freelog.
 */
#ifdef DEBUG
static int logdebug_check(const char *file, int line)
{
  if (logdebug_this_init < logd_init_counter) {  
    logdebug_thisfile = logdebug_update(file);
    logdebug_this_init = logd_init_counter;
  } 
  return (logdebug_thisfile.tthis && (logdebug_thisfile.max==0 
				      || (line >= logdebug_thisfile.min 
					  && line <= logdebug_thisfile.max))); 
}
/* Including log.h without calling freelog() can generate a
   warning that logdebug_check is never used; can use this to
   suppress that warning:
*/
#define logdebug_suppress_warning logdebug_check(__FILE__, __LINE__)
#else
#define logdebug_suppress_warning
#endif

/* For GCC we use a variadic macro; for others we take
   the performance hit of a function to get variadic args:
*/
#ifdef __GNUC__
#ifdef DEBUG
#define freelog(level, args...) do { \
  if ((level) != LOG_DEBUG || logdebug_check(__FILE__, __LINE__)) { \
    real_freelog((level), ##args); \
  } \
} while(0) 
#else
#define freelog(level, args...) do { \
  if ((level) != LOG_DEBUG) { \
    real_freelog((level), ##args); } \
} while(0) 
#endif  /* DEBUG */
#else
/* non-GCC: */
static void freelog(int level, char *message, ...)
{
  int log_this; 
#ifdef DEBUG
  log_this = (level != LOG_DEBUG || logdebug_check(__FILE__, __LINE__));
#else
  log_this = (level != LOG_DEBUG);
#endif
  if (log_this) {
    va_list args;
    va_start(args, message);
    vreal_freelog(level, message, args);
    va_end(args);
  }
}
#endif /* __GNUC__ */

#endif  /* FC__LOG_H */
