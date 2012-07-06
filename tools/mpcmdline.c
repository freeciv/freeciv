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

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "shared.h"
#include "support.h"

/* common */
#include "fc_cmdhelp.h"
#include "version.h"

/* modinst */
#include "modinst.h"

#include "mpcmdline.h"

extern struct fcmp_params fcmp;

/**************************************************************************
  Parse commandline parameters. Modified argv[] so it contains only
  ui-specific options afterwards. Number of those ui-specific options is
  returned.
  Currently this is implemented in a way that it never fails. Either it
  returns with success or exit()s. Implementation can be changhed so that
  this returns with value -1 in case program should be shut down instead
  of exiting itself. Callers are prepared for such implementation.
**************************************************************************/
int fcmp_parse_cmdline(int argc, char *argv[])
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
      cmdhelp_add(help, "L",
                  /* TRANS: "List" is exactly what user must type, do not translate. */
                  _("List URL"),
                  _("Load modpack list from given URL"));
      cmdhelp_add(help, "p",
                  /* TRANS: "prefix" is exactly what user must type, do not translate. */
                  _("prefix DIR"),
                  _("Install modpacks to given directory hierarchy"));
      cmdhelp_add(help, "v", "version",
                  _("Print the version number"));
      /* The function below prints a header and footer for the options.
       * Furthermore, the options are sorted. */
      cmdhelp_display(help, TRUE, TRUE, TRUE);
      cmdhelp_destroy(help);

      exit(EXIT_SUCCESS);
    } else if ((option = get_option_malloc("--List", argv, &i, argc))) {
      fcmp.list_url = option;
    } else if ((option = get_option_malloc("--prefix", argv, &i, argc))) {
      fcmp.inst_prefix = option;
    } else if (is_option("--version", argv[i])) {
      fc_fprintf(stderr, "%s \n", freeciv_name_version());

      exit(EXIT_SUCCESS);
    } else if (is_option("--", argv[i])) {
      ui_separator = TRUE;
    }

    i++;
  }

  if (fcmp.inst_prefix == NULL) {
    const char *home = user_home_dir();

    if (home == NULL) {
      log_error("Cannot determine user home directory");
    } else {
      static char pfx_buf[500];

      snprintf(pfx_buf, sizeof(pfx_buf), "%s/.freeciv", home);
      fcmp.inst_prefix = pfx_buf;
    }
  }

  return ui_options;
}
