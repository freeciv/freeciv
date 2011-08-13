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
#include <config.h>
#endif

/* utility */
#include "fciconv.h"
#include "shared.h"
#include "support.h"

/* common */
#include "version.h"

/* modinst */
#include "mpcmdline.h"

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

  while (i < argc) {
    if (ui_separator) {
      argv[1 + ui_options] = argv[i];
      ui_options++;
    } else if (is_option("--help", argv[i])) {
      fc_fprintf(stderr, _("Usage: %s [option ...]\n"
			   "Valid option are:\n"), argv[0]);
      fc_fprintf(stderr, _("  -h, --help\t\tPrint a summary of the options\n"));
      fc_fprintf(stderr, _("  -v, --version\t\tPrint the version number\n"));
      fc_fprintf(stderr, _("      --\t\t"
			   "Pass any following options to the UI.\n"));

      /* TRANS: No full stop after the URL, could cause confusion. */
      fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);

      exit(EXIT_SUCCESS);
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
