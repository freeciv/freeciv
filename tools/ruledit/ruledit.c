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

/* ANSI */
#include <stdlib.h>

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "registry.h"

/* common */
#include "fc_cmdhelp.h"
#include "game.h"
#include "version.h"

/* server */
#include "ruleset.h"
#include "sernet.h"
#include "settings.h"


const char *source_rs = "classic";

static int re_parse_cmdline(int argc, char *argv[]);

/**************************************************************************
  Main entry point for freeciv-ruledit
**************************************************************************/
int main(int argc, char **argv)
{
  int loglevel = LOG_NORMAL;
  int ui_options;

  init_nls();
  registry_module_init();
  init_character_encodings(FC_DEFAULT_DATA_ENCODING, FALSE);

  log_init(NULL, loglevel, NULL, NULL, -1);

  ui_options = re_parse_cmdline(argc, argv);

  if (ui_options != -1) {
    init_connections();

    settings_init(FALSE);

    /* Reset aifill to zero */
    game.info.aifill = 0;

    game_init();
    i_am_server();

    if (source_rs !=  NULL) {
      sz_strlcpy(game.server.rulesetdir, source_rs);
    }

    if (load_rulesets(NULL, FALSE)) {
      log_normal("Terrains:     %d", game.control.terrain_count);
      log_normal("Resources:    %d", game.control.resource_count);
      log_normal("Techs:        %d", game.control.num_tech_types);
      log_normal("Unit classes: %d", game.control.num_unit_classes);
      log_normal("Unit types:   %d", game.control.num_unit_types);
      log_normal("Buildings:    %d", game.control.num_impr_types);
      log_normal("Nations:      %d", game.control.nation_count);
      log_normal("City Styles:  %d", game.control.styles_count);
      log_normal("Specialists:  %d", game.control.num_specialist_types);
      log_normal("Governments:  %d", game.control.government_count);
      log_normal("Disasters:    %d", game.control.num_disaster_types);
      log_normal("Achievements: %d", game.control.num_achievement_types);
      log_normal("Extras:       %d", game.control.num_extra_types);
      log_normal("Bases:        %d", game.control.num_base_types);
      log_normal("Roads:        %d", game.control.num_road_types);
    } else {
      log_error("Loading ruleset %s failed", game.server.rulesetdir);
    }
  }

  log_close();
  registry_module_close();

  return EXIT_SUCCESS;
}

/**************************************************************************
  Parse freeciv-ruledit commandline.
**************************************************************************/
static int re_parse_cmdline(int argc, char *argv[])
{
  int i = 1;
  bool ui_separator = FALSE;
  int ui_options = 0;
  const char *option = NULL;

  while (i < argc) {
    if (ui_separator) {
      argv[1 + ui_options] = argv[i];
      ui_options++;
    } else if (is_option("--help", argv[i])) {
      struct cmdhelp *help = cmdhelp_new(argv[0]);

      cmdhelp_add(help, "h", "help",
                  _("Print a summary of the options"));
      cmdhelp_add(help, "s",
                  /* TRANS: "source" is exactly what user must type, do not translate. */
                  _("source RULESET"),
                  _("Load given ruleset"));
      cmdhelp_add(help, "v", "version",
                  _("Print the version number"));
      /* The function below prints a header and footer for the options.
       * Furthermore, the options are sorted. */
      cmdhelp_display(help, TRUE, TRUE, TRUE);
      cmdhelp_destroy(help);

      exit(EXIT_SUCCESS);
    } else if ((option = get_option_malloc("--source", argv, &i, argc))) {
      source_rs = option;
    } else if (is_option("--version", argv[i])) {
      fc_fprintf(stderr, "%s \n", freeciv_name_version());

      exit(EXIT_SUCCESS);
    } else if (is_option("--", argv[i])) {
      ui_separator = TRUE;
    }

    i++;
  }

  return ui_options;
}
