/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "capability.h"
#include "registry.h"
#include "section_file.h"
#include "shared.h"

#include "modpack.h"


/************************************************************************//**
  Check modpack file capabilties.
****************************************************************************/
bool modpack_check_capabilities(struct section_file *file, const char *us_capstr,
                                const char *filename, bool verbose)
{
  enum log_level level = verbose ? LOG_WARN : LOG_DEBUG;

  const char *file_capstr = secfile_lookup_str(file, "datafile.options");

  if (NULL == file_capstr) {
    log_base(level, "\"%s\": file doesn't have a capability string",
             filename);
    return FALSE;
  }
  if (!has_capabilities(us_capstr, file_capstr)) {
    log_base(level, "\"%s\": file appears incompatible:",
             filename);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }
  if (!has_capabilities(file_capstr, us_capstr)) {
    log_base(level, "\"%s\": file requires option(s) "
             "that freeciv doesn't support:", filename);
    log_base(level, "  datafile options: %s", file_capstr);
    log_base(level, "  supported options: %s", us_capstr);
    return FALSE;
  }

  return TRUE;
}

/************************************************************************//**
  Get list of modpack meta info files.
****************************************************************************/
struct fileinfo_list *get_modpacks_list(void)
{
  struct fileinfo_list *files;

  /* search for modpack files. */
  files = fileinfolist_infix(get_data_dirs(), MODPACK_SUFFIX, TRUE);

  return files;
}

/************************************************************************//**
  Return name of the modpack if contains a ruleset. If it does not contain
  ruleset, return NULL.
****************************************************************************/
const char *modpack_has_ruleset(struct section_file *sf)
{
  if (sf != NULL) {
    if (!modpack_check_capabilities(sf, MODPACK_CAPSTR, sf->name,
                                    FALSE)) {
      return NULL;
    }

    if (secfile_lookup_bool_default(sf, FALSE, "components.ruleset")) {
      return secfile_lookup_str_default(sf, NULL, "modpack.name");
    }
  }

  return NULL;
}

/************************************************************************//**
  Return .serv file name for the modpack, if any.
****************************************************************************/
const char *modpack_serv_file(struct section_file *sf)
{
  if (sf != NULL) {
    return secfile_lookup_str_default(sf, NULL, "ruleset.serv");
  }

  return NULL;
}

/************************************************************************//**
  Return rulesetdir for the modpack, if any.
****************************************************************************/
const char *modpack_rulesetdir(struct section_file *sf)
{
  if (sf != NULL) {
    return secfile_lookup_str_default(sf, NULL, "ruleset.rulesetdir");
  }

  return NULL;
}
