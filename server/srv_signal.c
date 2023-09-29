/***********************************************************************
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
#endif

#include "fc_prehdrs.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

/* utility */
#include "fciconv.h"
#include "log.h"
#include "timing.h"

/* server */
#include "srv_main.h"

/* server/savegame */
#include "savemain.h"

#include "srv_signal.h"

#ifdef HAVE_SIGNAL_H
#define USE_INTERRUPT_HANDLERS
#endif

#ifdef USE_INTERRUPT_HANDLERS
#define save_and_exit(sig)              \
if (S_S_RUNNING == server_state()) {    \
  save_game_auto(#sig, AS_INTERRUPT);   \
  save_system_close();                  \
}                                       \
exit(EXIT_SUCCESS);

static struct timer *signal_timer = NULL;

/**********************************************************************//**
  This function is called when a SIGINT (ctrl-c) is received. It will exit
  only if two SIGINTs are received within a second.
**************************************************************************/
static void signal_handler(int sig)
{
  switch (sig) {
  case SIGINT:
    if (signal_timer != NULL && timer_read_seconds(signal_timer) <= 1.0) {
      save_and_exit(SIGINT);
    } else {
      if (game.info.timeout == -1) {
        log_normal(_("Setting timeout to 0. Autogame will stop."));
        game.info.timeout = 0;
      }
      if (signal_timer == NULL) {
        log_normal(_("You must interrupt Freeciv twice "
                     "within one second to make it exit."));
      }
    }
    signal_timer = timer_renew(signal_timer, TIMER_USER, TIMER_ACTIVE,
                               signal_timer != NULL ? NULL : "ctrlc");
    timer_start(signal_timer);
    break;

#ifdef SIGHUP
  case SIGHUP:
    save_and_exit(SIGHUP);
    break;
#endif /* SIGHUP */

  case SIGTERM:
    save_and_exit(SIGTERM);
    break;

#ifdef SIGPIPE
  case SIGPIPE:
    if (signal(SIGPIPE, signal_handler) == SIG_ERR) {
      /* Because the signal may have interrupted arbitrary code, we use
       * fprintf() and _exit() here instead of log_*() and exit() so
       * that we don't accidentally call any "unsafe" functions here
       * (see the manual page for the signal function). */
      fprintf(stderr, "\nFailed to reset SIGPIPE handler "
              "while handling SIGPIPE.\n");
      _exit(EXIT_FAILURE);
    }
    break;
#endif /* SIGPIPE */
  }
}

#endif /* USE_INTERRUPT_HANDLERS */

/**********************************************************************//**
  Sets interrupt handlers for the server.
**************************************************************************/
void setup_interrupt_handlers(void)
{
#ifdef USE_INTERRUPT_HANDLERS
  if (SIG_ERR == signal(SIGINT, signal_handler)) {
    fc_fprintf(stderr, _("Failed to install SIGINT handler: %s\n"),
               fc_strerror(fc_get_errno()));
    exit(EXIT_FAILURE);
  }

#ifdef SIGHUP
  if (SIG_ERR == signal(SIGHUP, signal_handler)) {
    fc_fprintf(stderr, _("Failed to install SIGHUP handler: %s\n"),
               fc_strerror(fc_get_errno()));
    exit(EXIT_FAILURE);
  }
#endif /* SIGHUP */

  if (SIG_ERR == signal(SIGTERM, signal_handler)) {
    fc_fprintf(stderr, _("Failed to install SIGTERM handler: %s\n"),
               fc_strerror(fc_get_errno()));
    exit(EXIT_FAILURE);
  }

#ifdef SIGPIPE
  /* Ignore SIGPIPE, the error is handled by the return value
   * of the write call. */
  if (SIG_ERR == signal(SIGPIPE, signal_handler)) {
    fc_fprintf(stderr, _("Failed to ignore SIGPIPE: %s\n"),
               fc_strerror(fc_get_errno()));
    exit(EXIT_FAILURE);
  }
#endif /* SIGPIPE */
#endif /* USE_INTERRUPT_HANDLERS */
}

/**********************************************************************//**
  Free signal timer
**************************************************************************/
void signal_timer_free(void)
{
  if (signal_timer != NULL) {
    timer_destroy(signal_timer);
    signal_timer = NULL;
  }
}
