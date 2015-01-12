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
#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* utility */
#include "capability.h"
#include "log.h"
#include "registry.h"

/* server */
#include "ruleset.h"

#include "rscompat.h"

/**************************************************************************
  Initialize rscompat information structure
**************************************************************************/
void rscompat_init_info(struct rscompat_info *info)
{
  memset(info, 0, sizeof(*info));
}

/**************************************************************************
  Ruleset files should have a capabilities string datafile.options
  This checks the string and that the required capabilities are satisified.
**************************************************************************/
int rscompat_check_capabilities(struct section_file *file,
                                const char *filename)
{
  const char *datafile_options;

  if (!(datafile_options = secfile_lookup_str(file, "datafile.options"))) {
    log_fatal("\"%s\": ruleset capability problem:", filename);
    ruleset_error(LOG_ERROR, "%s", secfile_error());

    return 0;
  }
  if (!has_capabilities(RULESET_CAPABILITIES, datafile_options)) {
    log_fatal("\"%s\": ruleset datafile appears incompatible:", filename);
    log_fatal("  datafile options: %s", datafile_options);
    log_fatal("  supported options: %s", RULESET_CAPABILITIES);
    ruleset_error(LOG_ERROR, "Capability problem");

    return 0;
  }
  if (!has_capabilities(datafile_options, RULESET_CAPABILITIES)) {
    log_fatal("\"%s\": ruleset datafile claims required option(s)"
              " that we don't support:", filename);
    log_fatal("  datafile options: %s", datafile_options);
    log_fatal("  supported options: %s", RULESET_CAPABILITIES);
    ruleset_error(LOG_ERROR, "Capability problem");

    return 0;
  }

  return secfile_lookup_int_default(file, 1, "datafile.format_version");
}

/**************************************************************************
  Do compatibility things after regular ruleset loading.
**************************************************************************/
void rscompat_postprocess(struct rscompat_info *info)
{
}
