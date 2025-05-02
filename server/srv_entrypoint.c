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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

/* utility */
#include "deprecations.h"
#include "executable.h"
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "capstr.h"
#include "fc_cmdhelp.h"
#include "game.h"
#include "version.h"

/* server */
#include "aiiface.h"
#include "console.h"
#include "meta.h"
#include "sernet.h"
#include "srv_main.h"
#include "srv_signal.h"

/**********************************************************************//**
 Entry point for Freeciv server. Basically, does two things:
  1. Parses command-line arguments
  2. Calls the main server-loop routine.
**************************************************************************/
int main(int argc, char *argv[])
{
  int inx;
  bool showhelp = FALSE;
  bool showvers = FALSE;
  char *option = NULL;

  executable_init();
  setup_interrupt_handlers();

  /* Initialize server */
  srv_init();

  /* Parse command-line arguments... */

  srvarg.announce = ANNOUNCE_DEFAULT;

  game.server.meta_info.type[0] = '\0';

  /* No, we don't use GNU's getopt or even the "standard" getopt */
  /* Yes, we do have reasons ;)                                  */
  /* FIXME: and that are? */
  inx = 1;
  while (inx < argc) {
    if ((option = get_option_malloc("--file", argv, &inx, argc,
                                    FALSE))) {
      sz_strlcpy(srvarg.load_filename, option);
      free(option);
    } else if (is_option("--help", argv[inx])) {
      showhelp = TRUE;
      break;
    } else if ((option = get_option_malloc("--log", argv, &inx, argc, TRUE))) {
      srvarg.log_filename = option;
#ifndef FREECIV_NDEBUG
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
#endif /* FREECIV_NDEBUG */
    } else if ((option = get_option_malloc("--Ranklog", argv, &inx, argc, TRUE))) {
      srvarg.ranklog_filename = option;
    } else if (is_option("--keep", argv[inx])) {
      srvarg.metaconnection_persistent = TRUE;
      /* Implies --meta */
      srvarg.metaserver_no_send = FALSE;
    } else if (is_option("--nometa", argv[inx])) {
      fc_fprintf(stderr, _("Warning: the %s option is obsolete.  "
                           "Use -m to enable the metaserver.\n"), argv[inx]);
      showhelp = TRUE;
    } else if (is_option("--meta", argv[inx])) {
      srvarg.metaserver_no_send = FALSE;
    } else if ((option = get_option_malloc("--Metaserver",
                                           argv, &inx, argc, FALSE))) {
      sz_strlcpy(srvarg.metaserver_addr, option);
      free(option);
      srvarg.metaserver_no_send = FALSE;      /* --Metaserver implies --meta */
    } else if ((option = get_option_malloc("--identity",
                                           argv, &inx, argc, FALSE))) {
      sz_strlcpy(srvarg.identity_name, option);
      free(option);
    } else if ((option = get_option_malloc("--port", argv, &inx, argc, FALSE))) {
      if (!str_to_int(option, &srvarg.port)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else if ((option = get_option_malloc("--bind", argv, &inx, argc, TRUE))) {
      srvarg.bind_addr = option;
    } else if ((option = get_option_malloc("--Bind-meta", argv, &inx, argc, TRUE))) {
      srvarg.bind_meta_addr = option;
#ifdef FREECIV_WEB
    } else if ((option = get_option_malloc("--type", argv, &inx, argc, FALSE))) {
      sz_strlcpy(game.server.meta_info.type, option);
      free(option);
#endif /* FREECIV_WEB */
    } else if ((option = get_option_malloc("--read", argv, &inx, argc, TRUE)))
      srvarg.script_filename = option;
    else if ((option = get_option_malloc("--quitidle", argv, &inx, argc, FALSE))) {
      if (!str_to_int(option, &srvarg.quitidle)) {
        showhelp = TRUE;
        break;
      }
      free(option);
    } else if (is_option("--exit-on-end", argv[inx])) {
      srvarg.exit_on_end = TRUE;
    } else if ((option = get_option_malloc("--debug", argv, &inx, argc, FALSE))) {
      if (!log_parse_level_str(option, &srvarg.loglevel)) {
        showhelp = TRUE;
        break;
      }
      free(option);
#ifdef HAVE_FCDB
    } else if ((option = get_option_malloc("--Database", argv, &inx, argc, FALSE))) {
      /* Freed after file has been loaded - not here nor in server quit */
      srvarg.fcdb_enabled = TRUE;
      srvarg.fcdb_conf = option;
    } else if (is_option("--auth", argv[inx])) {
      srvarg.auth_enabled = TRUE;
    } else if (is_option("--Guests", argv[inx])) {
      srvarg.auth_allow_guests = TRUE;
    } else if (is_option("--Newusers", argv[inx])) {
      srvarg.auth_allow_newusers = TRUE;
#endif /* HAVE_FCDB */
    } else if ((option = get_option_malloc("--Serverid", argv, &inx, argc, FALSE))) {
      sz_strlcpy(srvarg.serverid, option);
      free(option);
    } else if ((option = get_option_malloc("--saves", argv, &inx, argc, TRUE))) {
      srvarg.saves_pathname = option;
    } else if ((option = get_option_malloc("--scenarios", argv, &inx, argc, TRUE))) {
      srvarg.scenarios_pathname = option;
    } else if ((option = get_option_malloc("--ruleset", argv, &inx, argc, TRUE))) {
      srvarg.ruleset = option;
    } else if (is_option("--version", argv[inx])) {
      showvers = TRUE;
    } else if ((option = get_option_malloc("--Announce", argv, &inx, argc, FALSE))) {
      if (!fc_strcasecmp(option, "ipv4")) {
        srvarg.announce = ANNOUNCE_IPV4;
      } else if (!fc_strcasecmp(option, "none")) {
        srvarg.announce = ANNOUNCE_NONE;
#ifdef FREECIV_IPV6_SUPPORT
      } else if (!fc_strcasecmp(option, "ipv6")) {
        srvarg.announce = ANNOUNCE_IPV6;
#endif /* IPv6 support */
      } else {
        log_error(_("Illegal value \"%s\" for --Announce"), option);
      }
      free(option);
    } else if (is_option("--warnings", argv[inx])) {
      deprecation_warnings_enable();
#ifdef AI_MODULES
    } else if ((option = get_option_malloc("--LoadAI", argv, &inx, argc, FALSE))) {
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
	    HOMEPAGE_URL);

  if (showhelp) {
    struct cmdhelp *help = cmdhelp_new(argv[0]);

    cmdhelp_add(help, "A",
                /* TRANS: "Announce" is exactly what user must type, do not translate. */
                _("Announce PROTO"),
                _("Announce game in LAN using protocol PROTO "
                  "(IPv4/IPv6/none)"));
#ifdef HAVE_FCDB
    cmdhelp_add(help, "D",
                /* TRANS: "Database" is exactly what user must type, do not translate. */
                _("Database FILE"),
                _("Enable database connection with configuration from "
                  "FILE."));
    cmdhelp_add(help, "a", "auth",
                _("Enable server authentication (requires --Database)."));
    cmdhelp_add(help, "G", "Guests",
                _("Allow guests to login if auth is enabled."));
    cmdhelp_add(help, "N", "Newusers",
                _("Allow new users to login if auth is enabled."));
#endif /* HAVE_FCDB */
    cmdhelp_add(help, "b",
                /* TRANS: "bind" is exactly what user must type, do not translate. */
                _("bind ADDR"),
                _("Listen for clients on ADDR"));
    cmdhelp_add(help, "B", "Bind-meta ADDR",
                _("Connect to metaserver from this address"));
#ifdef FREECIV_DEBUG
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v,d, or "
                  "d:file1,min,max:...)"));
#else  /* FREECIV_DEBUG */
    cmdhelp_add(help, "d",
                /* TRANS: "debug" is exactly what user must type, do not translate. */
                _("debug LEVEL"),
                _("Set debug log level (one of f,e,w,n,v)"));
#endif /* FREECIV_DEBUG */
#ifndef FREECIV_NDEBUG
    cmdhelp_add(help, "F",
                /* TRANS: "Fatal" is exactly what user must type, do not translate. */
                _("Fatal [SIGNAL]"),
                _("Raise a signal on failed assertion"));
#endif /* FREECIV_NDEBUG */
    cmdhelp_add(help, "f",
                /* TRANS: "file" is exactly what user must type, do not translate. */
                _("file FILE"),
                _("Load saved game FILE"));
    cmdhelp_add(help, "h", "help",
                _("Print a summary of the options"));
    cmdhelp_add(help, "i",
                /* TRANS: "identity" is exactly what user must type, do not translate. */
                _("identity ADDR"),
                _("Be known as ADDR at metaserver or LAN client"));
    cmdhelp_add(help, "l",
                /* TRANS: "log" is exactly what user must type, do not translate. */
                _("log FILE"),
                _("Use FILE as logfile"));
    cmdhelp_add(help, "m", "meta",
                _("Notify metaserver and send server's info"));
    cmdhelp_add(help, "M",
                /* TRANS: "Metaserver" is exactly what user must type, do not translate. */
                _("Metaserver ADDR"),
                _("Set ADDR as metaserver address"));
#ifdef FREECIV_WEB
    cmdhelp_add(help, "t",
                /* TRANS: "type" is exactly what user must type, do not translate. */
                _("type TYPE"),
                _("Set TYPE as server type in metaserver"));
#endif /* FREECIV_WEB */
    cmdhelp_add(help, "k", "keep",
                _("Keep updating game information on metaserver even after "
                  "failure")),
    cmdhelp_add(help, "p",
                /* TRANS: "port" is exactly what user must type, do not translate. */
                _("port PORT"),
                _("Listen for clients on port PORT"));
    cmdhelp_add(help, "q",
                /* TRANS: "quitidle" is exactly what user must type, do not translate. */
                _("quitidle TIME"),
                _("Quit if no players for TIME seconds"));
    cmdhelp_add(help, "e", "exit-on-end",
                _("When a game ends, exit instead of restarting"));
    cmdhelp_add(help, "s",
                /* TRANS: "saves" is exactly what user must type, do not translate. */
                _("saves DIR"),
                _("Save games to directory DIR"));
    cmdhelp_add(help, NULL,
                /* TRANS: "scenarios" is exactly what user must type, do not translate. */
                _("scenarios DIR"),
                _("Save scenarios to directory DIR"));
    cmdhelp_add(help, "S",
                /* TRANS: "Serverid" is exactly what user must type, do not translate. */
                _("Serverid ID"),
                _("Sets the server id to ID"));
    cmdhelp_add(help, "r",
                /* TRANS: "read" is exactly what user must type, do not translate. */
                _("read FILE"),
                _("Read startup script FILE"));
    cmdhelp_add(help, "R",
                /* TRANS: "Ranklog" is exactly what user must type, do not translate. */
                _("Ranklog FILE"),
                _("Use FILE as ranking logfile"));
    cmdhelp_add(help, NULL,
                /* TRANS: "ruleset" is exactly what user must type, do not translate. */
                _("ruleset RULESET"),
                _("Load ruleset RULESET"));
#ifdef AI_MODULES
    cmdhelp_add(help, "L",
                /* TRANS: "LoadAI" is exactly what user must type, do not translate. */
                _("LoadAI MODULE"),
                _("Load ai module MODULE. Can appear multiple times"));
#endif /* AI_MODULES */
    cmdhelp_add(help, "v", "version",
                _("Print the version number"));
    cmdhelp_add(help, "w", "warnings",
                _("Warn about deprecated modpack constructs"));

    /* The function below prints a header and footer for the options.
     * Furthermore, the options are sorted. */
    cmdhelp_display(help, TRUE, FALSE, TRUE);
    cmdhelp_destroy(help);

    exit(EXIT_SUCCESS);
  }

#ifdef HAVE_FCDB
  if (srvarg.auth_enabled && !srvarg.fcdb_enabled) {
    fc_fprintf(stderr,
               _("Requested authentication with --auth, "
                 "but no --Database given\n"));
    exit(EXIT_FAILURE);
  }
#endif /* HAVE_FCDB */

  /* disallow running as root -- too dangerous */
  dont_run_as_root(argv[0], "freeciv_server");

  init_our_capability();

  /* have arguments, call the main server loop... */
  srv_main();

  /* Technically, we won't ever get here. We exit via server_quit. */

  /* done */
  exit(EXIT_SUCCESS);
}
