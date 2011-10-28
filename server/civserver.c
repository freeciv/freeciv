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
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef GENERATING_MAC  /* mac header(s) */
#include <Controls.h>
#include <Dialogs.h>
#endif

#ifdef WIN32_NATIVE
#include <windows.h>
#endif

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"
#include "timing.h"

/* common */
#include "capstr.h"
#include "fc_cmdhelp.h"
#include "game.h"
#include "version.h"

/* server */
#include "aiiface.h"
#include "console.h"
#include "ggzserver.h"
#include "meta.h"
#include "sernet.h"
#include "srv_main.h"

#include "civserver.h"

#ifdef GENERATING_MAC
static void Mac_options(int argc);  /* don't need argv */
#endif

#ifdef HAVE_SIGNAL_H
#  define USE_INTERRUPT_HANDLERS
#endif

#ifdef USE_INTERRUPT_HANDLERS
#define save_and_exit(sig)              \
if (S_S_RUNNING == server_state()) {    \
  save_game_auto(#sig, AS_INTERRUPT);   \
}                                       \
exit(EXIT_SUCCESS);

/**************************************************************************
  This function is called when a SIGINT (ctrl-c) is received.  It will exit
  only if two SIGINTs are received within a second.
**************************************************************************/
static void signal_handler(int sig)
{
  static struct timer *timer = NULL;

  switch (sig) {
  case SIGINT:
    if (with_ggz) {
      save_and_exit(SIGINT);
    }
    if (timer && read_timer_seconds(timer) <= 1.0) {
      save_and_exit(SIGINT);
    } else {
      if (game.info.timeout == -1) {
        log_normal(_("Setting timeout to 0. Autogame will stop."));
        game.info.timeout = 0;
      }
      if (!timer) {
        log_normal(_("You must interrupt Freeciv twice "
                     "within one second to make it exit."));
      }
    }
    timer = renew_timer_start(timer, TIMER_USER, TIMER_ACTIVE);
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

/**************************************************************************
 Entry point for Freeciv server.  Basically, does two things:
  1. Parses command-line arguments (possibly dialog, on mac).
  2. Calls the main server-loop routine.
**************************************************************************/
int main(int argc, char *argv[])
{
  int inx;
  bool showhelp = FALSE;
  bool showvers = FALSE;
  char *option = NULL;

  /* Load win32 post-crash debugger */
#ifdef WIN32_NATIVE
# ifndef NDEBUG
  if (LoadLibrary("exchndl.dll") == NULL) {
#  ifdef DEBUG
    fprintf(stderr, "exchndl.dll could not be loaded, no crash debugger\n");
#  endif
  }
# endif
#endif

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

  /* initialize server */
  srv_init();

  /* parse command-line arguments... */

#ifdef GENERATING_MAC
  Mac_options(argc);
#endif
  srvarg.announce = ANNOUNCE_DEFAULT;

  /* no  we don't use GNU's getopt or even the "standard" getopt */
  /* yes we do have reasons ;)                                   */
  /* FIXME: and that are? */
  inx = 1;
  while (inx < argc) {
    if ((option = get_option_malloc("--file", argv, &inx, argc))) {
      sz_strlcpy(srvarg.load_filename, option);
      free(option);
    } else if (is_option("--help", argv[inx])) {
      showhelp = TRUE;
      break;
    } else if ((option = get_option_malloc("--log", argv, &inx, argc))) {
      srvarg.log_filename = option; /* Never freed. */
#ifndef NDEBUG
    } else if (is_option("--Fatal", argv[inx])) {
      if (inx + 1 >= argc || '-' == argv[inx + 1][0]) {
        srvarg.fatal_assertions = SIGABRT;
      } else if (str_to_int(argv[inx + 1], &srvarg.fatal_assertions)) {
        inx++;
      } else {
        fc_fprintf(stderr, _("Invalid signal number \"%s\".\n"),
                   argv[inx + 1]);
        inx++;
        showhelp = TRUE;
      }
#endif /* NDEBUG */
    } else if ((option = get_option_malloc("--Ranklog", argv, &inx, argc))) {
      srvarg.ranklog_filename = option; /* Never freed. */
    } else if (is_option("--nometa", argv[inx])) {
      fc_fprintf(stderr, _("Warning: the %s option is obsolete.  "
			   "Use -m to enable the metaserver.\n"), argv[inx]);
      showhelp = TRUE;
    } else if (is_option("--meta", argv[inx])) {
      srvarg.metaserver_no_send = FALSE;
    } else if ((option = get_option_malloc("--Metaserver",
					 argv, &inx, argc))) {
      sz_strlcpy(srvarg.metaserver_addr, option);
      free(option);
      srvarg.metaserver_no_send = FALSE;      /* --Metaserver implies --meta */
    } else if ((option = get_option_malloc("--identity",
					   argv, &inx, argc))) {
      sz_strlcpy(srvarg.metaserver_name, option);
      free(option);
    } else if ((option = get_option_malloc("--port", argv, &inx, argc))) {
      if (!str_to_int(option, &srvarg.port)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else if ((option = get_option_malloc("--bind", argv, &inx, argc))) {
      srvarg.bind_addr = option; /* Never freed. */
    } else if ((option = get_option_malloc("--Bind-meta", argv, &inx, argc))) {
      srvarg.bind_meta_addr = option; /* Never freed. */
    } else if ((option = get_option_malloc("--read", argv, &inx, argc)))
      srvarg.script_filename = option; /* Never freed. */
    else if ((option = get_option_malloc("--quitidle", argv, &inx, argc))) {
      if (!str_to_int(option, &srvarg.quitidle)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else if (is_option("--exit-on-end", argv[inx])) {
      srvarg.exit_on_end = TRUE;
    } else if ((option = get_option_malloc("--debug", argv, &inx, argc))) {
      if (!log_parse_level_str(option, &srvarg.loglevel)) {
        showhelp = TRUE;
        break;
      }
      free(option);
#ifdef HAVE_FCDB
    } else if ((option = get_option_malloc("--Database", argv, &inx, argc))) {
      srvarg.fcdb_enabled = TRUE;
      srvarg.fcdb_conf = option;
    } else if (is_option("--auth", argv[inx])) {
      srvarg.auth_enabled = TRUE;
    } else if (is_option("--Guests", argv[inx])) {
      srvarg.auth_allow_guests = TRUE;
    } else if (is_option("--Newusers", argv[inx])) {
      srvarg.auth_allow_newusers = TRUE;
#endif /* HAVE_FCDB */
    } else if ((option = get_option_malloc("--Serverid", argv, &inx, argc))) {
      sz_strlcpy(srvarg.serverid, option);
      free(option);
    } else if ((option = get_option_malloc("--saves", argv, &inx, argc))) {
      srvarg.saves_pathname = option; /* Never freed. */
    } else if ((option = get_option_malloc("--scenarios", argv, &inx, argc))) {
      srvarg.scenarios_pathname = option; /* Never freed */
    } else if (is_option("--version", argv[inx])) {
      showvers = TRUE;
    } else if ((option = get_option_malloc("--Announce", argv, &inx, argc))) {
      if (!strcasecmp(option, "ipv4")) {
        srvarg.announce = ANNOUNCE_IPV4;
      } else if(!strcasecmp(option, "none")) {
        srvarg.announce= ANNOUNCE_NONE;
#ifdef IPV6_SUPPORT
      } else if (!strcasecmp(option, "ipv6")) {
        srvarg.announce = ANNOUNCE_IPV6;
#endif /* IPv6 support */
      } else {
        log_error(_("Illegal value \"%s\" for --Announce"), option);
      }
      free(option);
#ifdef AI_MODULES
    } else if ((option = get_option_malloc("--LoadAI", argv, &inx, argc))) {
      if (!load_ai_module(option)) {
        fc_fprintf(stderr, _("Failed to load AI module \"%s\"\n"), option);
        exit(EXIT_FAILURE);
      }
      free(option);
#endif /* AI_MODULES */
    } else {
      fc_fprintf(stderr, _("Error: unknown option '%s'\n"), argv[inx]);
      showhelp = TRUE;
      break;
    }
    inx++;
  }

  if (showvers && !showhelp) {
    fc_fprintf(stderr, "%s \n", freeciv_name_version());
    exit(EXIT_SUCCESS);
  }
  con_write(C_VERSION, _("This is the server for %s"), freeciv_name_version());
  /* TRANS: No full stop after the URL, could cause confusion. */
  con_write(C_COMMENT, _("You can learn a lot about Freeciv at %s"),
	    WIKI_URL);

  if (showhelp) {
    struct cmdhelp *help = cmdhelp_new(argv[0]);

    cmdhelp_add(help, "A", "Announce PROTO",
                _("Announce game in LAN using protocol PROTO "
                  "(IPv4/IPv6/none)"));
#ifdef HAVE_FCDB
    cmdhelp_add(help, "D", "Database FILE",
                _("Enable database connection with configuration from "
                  "FILE."));
    cmdhelp_add(help, "a", "auth",
                _("Enable server authentication (requires --Database)."));
    cmdhelp_add(help, "G", "Guests",
                _("Allow guests to login if auth is enabled."));
    cmdhelp_add(help, "N", "Newusers",
                _("Allow new users to login if auth is enabled."));
#endif /* HAVE_FCDB */
    cmdhelp_add(help, "b", "bind ADDR",
                _("Listen for clients on ADDR"));
    cmdhelp_add(help, "B", "Bind-meta ADDR",
                _("Connect to metaserver from this address"));
#ifdef DEBUG
    cmdhelp_add(help, "d", "debug NUM",
                _("Set debug log level (%d to %d, or %d:file1,min,max:...)"),
                LOG_FATAL, LOG_DEBUG, LOG_DEBUG);
#else  /* DEBUG */
    cmdhelp_add(help, "d", "debug NUM",
                _("Set debug log level (%d to %d)"), LOG_FATAL, LOG_VERBOSE);
#endif /* DEBUG */
#ifndef NDEBUG
    cmdhelp_add(help, "F", "Fatal [SIGNAL]",
                _("Raise a signal on failed assertion"));
#endif
    cmdhelp_add(help, "f", "file FILE",
                _("Load saved game FILE"));
    cmdhelp_add(help, "h", "help",
                _("Print a summary of the options"));
    cmdhelp_add(help, "i", "identity ADDR",
                _("Be known as ADDR at metaserver"));
    cmdhelp_add(help, "l", "log FILE",
                _("Use FILE as logfile"));
    cmdhelp_add(help, "m", "meta",
                _("Notify metaserver and send server's info"));
    cmdhelp_add(help, "M", "Metaserver ADDR",
                _("Set ADDR as metaserver address"));
    cmdhelp_add(help, "p", "port PORT",
                _("Listen for clients on port PORT"));
    cmdhelp_add(help, "q", "quitidle TIME",
                _("Quit if no players for TIME seconds"));
    cmdhelp_add(help, "e", "exit-on-end",
                _("When a game ends, exit instead of restarting"));
    cmdhelp_add(help, "s", "saves DIR",
                _("Save games to directory DIR"));
    cmdhelp_add(help, "S", "Serverid ID",
                _("Sets the server id to ID"));
    cmdhelp_add(help, "r", "read FILE",
                _("Read startup script FILE"));
    cmdhelp_add(help, "R", "Ranklog FILE",
                _("Use FILE as ranking logfile"));
#ifdef AI_MODULES
    cmdhelp_add(help, "L", "LoadAI MODULE",
                _("Load ai module MODULE. Can appear multiple times"));
#endif /* AI_MODULES */
    cmdhelp_add(help, "v", "version",
                _("Print the version number"));

    /* The function below prints a header and footer for the options.
     * Furthermore, the options are sorted. */
    cmdhelp_display(help, TRUE, FALSE, TRUE);
    cmdhelp_destroy(help);

    exit(EXIT_SUCCESS);
  }

  /* disallow running as root -- too dangerous */
  dont_run_as_root(argv[0], "freeciv_server");

  ggz_initialize();
  init_our_capability();

  /* have arguments, call the main server loop... */
  srv_main();

  /* Technically, we won't ever get here. We exit via server_quit. */

  /* done */
  exit(EXIT_SUCCESS);
}

#ifdef GENERATING_MAC
/*************************************************************************
  generate an option dialog if no options have been passed in
*************************************************************************/
static void Mac_options(int argc)
{
#define HARDCODED_OPT
  /*temporary hack since GetNewDialog() doesn't want to work*/
#ifdef HARDCODED_OPT
  srvarg.log_filename="log.out";
  srvarg.loglevel=LOG_DEBUG;
#else  /* HARDCODED_OPT */
  if (argc == 0)
  {
    OSErr err;
    DialogPtr  optptr;
    Ptr storage;
    Handle ditl;
    Handle dlog;
    short the_type;
    Handle the_handle;
    Rect the_rect;
    short the_item, old_item=16;
    int done = false;
    Str255 the_string;
    /*load/init the stuff for the dialog*/
    storage =NewPtr(sizeof(DialogRecord));
    if (storage == 0)
    {
      exit(EXIT_FAILURE);
    }
    ditl=Get1Resource('DITL',200);
    if ((ditl == 0)||(ResError()))
    {
      exit(EXIT_FAILURE);
    }
    dlog=Get1Resource('DLOG',200);
    if ((dlog == 0)||(ResError()))
    {
      exit(EXIT_FAILURE);
    }
    /*make the dialog*/
    optptr=GetNewDialog(200, storage, (WindowPtr)-1L);
    /*setup the dialog*/
    err=SetDialogDefaultItem(optptr, 1);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    /*insert default highlight draw code?*/
    err=SetDialogCancelItem(optptr, 2);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    err=SetDialogTracksCursor(optptr, true);
    if (err != 0)
    {
      exit(EXIT_FAILURE);
    }
    GetDItem(optptr, 16/*normal radio button*/, &the_type, &the_handle, &the_rect);
    SetCtlValue((ControlHandle)the_handle, true);

    while(!done)/*loop*/
    {
      ModalDialog(0L, &the_item);/* don't feed 0 where a upp is expected? */
      	/* old book suggests using OL(NIL) as the first argument, but
           It doesn't include anything about UPPs either, so... */
      switch (the_item)
      {
        case 1:
          done = true;
        break;
        case 2:
          exit(EXIT_SUCCESS);
        break;
        case 13:
          GetDItem(optptr, 13, &the_type, &the_handle, &the_rect);
          srvarg.metaserver_no_send=GetCtlValue((ControlHandle)the_handle);
          SetCtlValue((ControlHandle)the_handle, !srvarg.metaserver_no_send);
        break;
        case 15:
        case 16:
        case 17:
          GetDItem(optptr, old_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, false);
          old_item=the_item;
          GetDItem(optptr, the_item, &the_type, &the_handle, &the_rect);
          SetCtlValue((ControlHandle)the_handle, true);
        break;
      }
    }
    /* now, load the dialog items into the correct variables interpritation */
    GetDItem( optptr, 4, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.load_filename);
    GetDItem( optptr, 8, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.log_filename);
    GetDItem( optptr, 12, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, the_string);
    sscanf(the_string, "%d", srvarg.port);
    GetDItem( optptr, 10, &the_type, &the_handle, &the_rect);
    GetIText( the_handle, (unsigned char *)srvarg.script_filename);
    GetDItem(optptr, 15, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_FATAL;
    }
    GetDItem(optptr, 16, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_NORMAL;
    }
    GetDItem(optptr, 17, &the_type, &the_handle, &the_rect);
    if(GetControlValue((ControlHandle)the_handle))
    {
      srvarg.loglevel=LOG_VERBOSE;
    }
    DisposeDialog(optptr);/*get rid of the dialog after sorting out the options*/
    DisposePtr(storage);/*clean up the allocated memory*/
  }
#endif /* HARDCODED_OPT */
#undef  HARDCODED_OPT
}
#endif /* GENERATING_MAC */
