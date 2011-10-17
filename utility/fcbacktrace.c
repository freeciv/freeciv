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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

/* utility */
#include "log.h"
#include "shared.h"

#include "fcbacktrace.h"

#if defined(DEBUG) && defined(HAVE_BACKTRACE)
#define BACKTRACE_ACTIVE 1
#endif

#ifdef BACKTRACE_ACTIVE

#define MAX_NUM_FRAMES 64

static log_callback_fn previous = NULL;

static void backtrace_log(enum log_level level, const char *msg, bool file_too);
#endif

/************************************************************************
  Take backtrace log callback to use
************************************************************************/
void backtrace_init(void)
{
#ifdef BACKTRACE_ACTIVE
  previous = log_set_callback(backtrace_log);
#endif
}

/************************************************************************
  Remove backtrace log callback from use
************************************************************************/
void backtrace_deinit(void)
{
#ifdef BACKTRACE_ACTIVE
  log_callback_fn active;

  active = log_set_callback(previous);

  if (active != backtrace_log) {
    /* We were not the active callback!
     * Restore the active callback and log error */
    log_set_callback(active);
    log_error("Backtrace log callback cannot be removed");
  }
#endif /* BACKTRACE_ACTIVE */
}

#ifdef BACKTRACE_ACTIVE
/************************************************************************
  Write one line of backtrace
************************************************************************/
static void write_backtrace_line(enum log_level level, const char *msg)
{
  /* Current behavior of this function is to write to chained callback,
   * nothing more, nothing less. */
  if (previous != NULL) {
    previous(level, msg, FALSE);
  }
}

/************************************************************************
  Main backtrace callback called from logging code.
************************************************************************/
static void backtrace_log(enum log_level level, const char *msg, bool file_too)
{
  if (previous != NULL) {
    /* Call chained callback first */
    previous(level, msg, file_too);
  }

  if (level <= LOG_ERROR) {
    void *buffer[MAX_NUM_FRAMES];
    int frames;
    char **names;

    frames = backtrace(buffer, sizeof(buffer));
    names = backtrace_symbols(buffer, frames);

    if (names == NULL) {
      write_backtrace_line(LOG_NORMAL, "No backtrace");
    } else {
      int i;

      write_backtrace_line(LOG_NORMAL, "Backtrace:");

      for (i = 0; i < MIN(frames, MAX_NUM_FRAMES); i++) {
	char linestr[100];

	fc_snprintf(linestr, sizeof(linestr), " %d: %s", i, names[i]);

        /* We write always in level LOG_NORMAL and not in higher one
         * since those interact badly with server callback to send error
         * messages to local client. */
        write_backtrace_line(LOG_NORMAL, linestr);
      }

      free(names);
    }
  }
}

#endif /* BACKTRACE_ACTIVE */
