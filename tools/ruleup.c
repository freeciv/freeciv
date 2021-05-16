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

#include <signal.h>

#ifdef FREECIV_MSWINDOWS
#include <windows.h>
#endif

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "registry.h"
#include "string_vector.h"

/* common */
#include "fc_cmdhelp.h"
#include "fc_interface.h"

/* server */
#include "ruleset.h"
#include "sernet.h"
#include "settings.h"

/* tools/shared */
#include "tools_fc_interface.h"

/* tools/ruleutil */
#include "comments.h"
#include "rulesave.h"

static char *rs_selected = NULL;
static char *od_selected = NULL;
static int fatal_assertions = -1;
static bool dirty = TRUE;

/**********************************************************************//**
  Parse freeciv-ruleup commandline parameters.
**************************************************************************/
static void rup_parse_cmdline(int argc, char *argv[])
{
  int i = 1;

  while (i < argc) {
    char *option = NULL;

    if (is_option("--help", argv[i])) {
      struct cmdhelp *help = cmdhelp_new(argv[0]);

      cmdhelp_add(help, "h", "help",
                  _("Print a summary of the options"));
#ifndef FREECIV_NDEBUG
      cmdhelp_add(help, "F",
                  /* TRANS: "Fatal" is exactly what user must type, do not translate. */
                  _("Fatal [SIGNAL]"),
                  _("Raise a signal on failed assertion or broken data"));
#endif /* FREECIV_NDEBUG */
      cmdhelp_add(help, "r",
                  /* TRANS: "ruleset" is exactly what user must type, do not translate. */
                  _("ruleset RULESET"),
                  _("Update RULESET"));
      cmdhelp_add(help, "o",
		  /* TRANS: "output" is exactly what user must type, do not translate. */
		  _("output DIRECTORY"),
		  _("Create directory DIRECTORY for output"));
      cmdhelp_add(help, "c", "clean",
                  _("Clean up the ruleset before saving it."));

      /* The function below prints a header and footer for the options.
       * Furthermore, the options are sorted. */
      cmdhelp_display(help, TRUE, FALSE, TRUE);
      cmdhelp_destroy(help);

      cmdline_option_values_free();

      exit(EXIT_SUCCESS);
    } else if ((option = get_option_malloc("--ruleset", argv, &i, argc, TRUE))) {
      if (rs_selected != NULL) {
        fc_fprintf(stderr,
                   _("Multiple rulesets requested. Only one ruleset at time supported.\n"));
      } else {
        rs_selected = option;
      }
    } else if ((option = get_option_malloc("--output", argv, &i, argc, TRUE))) {
      if (od_selected != NULL) {
	fc_fprintf(stderr,
		   _("Multiple output directories given.\n"));
      } else {
	od_selected = option;
      }
#ifndef FREECIV_NDEBUG
    } else if (is_option("--Fatal", argv[i])) {
      if (i + 1 >= argc || '-' == argv[i + 1][0]) {
        fatal_assertions = SIGABRT;
      } else if (str_to_int(argv[i + 1], &fatal_assertions)) {
        i++;
      } else {
        fc_fprintf(stderr, _("Invalid signal number \"%s\".\n"),
                   argv[i + 1]);
        fc_fprintf(stderr, _("Try using --help.\n"));
        exit(EXIT_FAILURE);
      }
#endif /* FREECIV_NDEBUG */
    } else if (is_option("--clean", argv[i])) {
      dirty = FALSE;
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[i]);
      cmdline_option_values_free();
      exit(EXIT_FAILURE);
    }

    i++;
  }
}

/**********************************************************************//**
  Conversion log callback
**************************************************************************/
static void conv_log(const char *msg)
{
  log_normal("%s", msg);
}

/**********************************************************************//**
  Main entry point for freeciv-ruleup
**************************************************************************/
int main(int argc, char **argv)
{
  enum log_level loglevel = LOG_NORMAL;
  int exit_status = EXIT_SUCCESS;

  /* Load Windows post-crash debugger */
#ifdef FREECIV_MSWINDOWS
# ifndef FREECIV_NDEBUG
  if (LoadLibrary("exchndl.dll") == NULL) {
#  ifdef FREECIV_DEBUG
    fprintf(stderr, "exchndl.dll could not be loaded, no crash debugger\n");
#  endif /* FREECIV_DEBUG */
  }
# endif /* FREECIV_NDEBUG */
#endif /* FREECIV_MSWINDOWS */

  init_nls();

  registry_module_init();
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  rup_parse_cmdline(argc, argv);
  
  log_init(NULL, loglevel, NULL, NULL, fatal_assertions);

  init_connections();

  settings_init(FALSE);

  game_init(FALSE);
  i_am_tool();

  /* Initialize the fc_interface functions needed to understand rules. */
  fc_interface_init_tool();

  /* Set ruleset user requested to use */
  if (rs_selected == NULL) {
    rs_selected = GAME_DEFAULT_RULESETDIR;
  }
  sz_strlcpy(game.server.rulesetdir, rs_selected);

  /* Reset aifill to zero */
  game.info.aifill = 0;

  if (load_rulesets(NULL, NULL, TRUE, conv_log, FALSE, TRUE, TRUE)) {
    struct rule_data data;
    char tgt_dir[2048];

    data.nationlist = game.server.ruledit.nationlist;

    if (od_selected != NULL) {
      fc_strlcpy(tgt_dir, od_selected, sizeof(tgt_dir));
    } else {
      fc_snprintf(tgt_dir, sizeof(tgt_dir), "%s.ruleup", rs_selected);
    }

    if (!comments_load()) {
      /* TRANS: 'Failed to load comments-x.y.txt' where x.y is
       * freeciv version */
      log_error(R__("Failed to load %s."), COMMENTS_FILE_NAME);

      /* Reuse fatal_assertions for failed comment loading. */
      if (0 <= fatal_assertions) {
        /* Emit a signal. */
        raise(fatal_assertions);
      }
    }

    /* Clean up unused entities added during the ruleset upgrade. */
    if (!dirty) {
      int purged = ruleset_purge_unused_entities();

      if (purged > 0) {
        log_normal("Purged %d unused entities after the ruleset upgrade",
                   purged);
      }

      purged = ruleset_purge_redundant_reqs();
      if (purged > 0) {
        log_normal("Purged %d redundant requirements after the ruleset"
                   " upgrade", purged);
      }
    }

    save_ruleset(tgt_dir, game.control.name, &data);
    log_normal("Saved %s", tgt_dir);
    comments_free();
  } else {
    log_error(_("Can't load ruleset %s"), rs_selected);

    /* Failed to upgrade the ruleset */
    exit_status = EXIT_FAILURE;
  }

  registry_module_close();
  log_close();
  free_libfreeciv();
  free_nls();
  cmdline_option_values_free();

  return exit_status;
}
