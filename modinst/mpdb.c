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
#include "capability.h"
#include "fcintl.h"
#include "mem.h"
#include "registry.h"

/* modinst */
#include "download.h"

#include "mpdb.h"

#define MPDB_CAPSTR "+mpdb"

/**************************************************************************
  Construct install info list from file.
**************************************************************************/
void load_install_info_list(const char *filename,
                            struct install_info_list *list)
{
  struct section_file *file;
  const char *caps;

  file = secfile_load(filename, FALSE);

  caps = secfile_lookup_str(file, "info.options");

  if (caps == NULL) {
    log_error("MPDB %s missing capability information", filename);
    secfile_destroy(file);
    return;
  }

  if (!has_capabilities(MPDB_CAPSTR, caps)) {
    log_error("Incompatible mpdb file %s:", filename);
    log_error("  file options:      %s", caps);
    log_error("  supported options: %s", MPDB_CAPSTR);

    secfile_destroy(file);

    return;
  }

  if (file != NULL) {
    bool all_read = FALSE;
    int i;

    for (i = 0 ; !all_read ; i++) {
      const char *str;
      char buf[80];

      fc_snprintf(buf, sizeof(buf), "modpacks.mp%d", i);

      str = secfile_lookup_str_default(file, NULL, "%s.name", buf);

      if (str != NULL) {
        struct install_info *ii;

        ii = fc_malloc(sizeof(*ii));

        strncpy(ii->name, str, sizeof(ii->name));
        str = secfile_lookup_str(file, "%s.type", buf);
        ii->type = modpack_type_by_name(str, fc_strcasecmp);
        str = secfile_lookup_str(file, "%s.version", buf);
        strncpy(ii->version, str, sizeof(ii->version));

        install_info_list_append(list, ii);
      } else {
        all_read = TRUE;
      }
    }

    secfile_destroy(file);
  }
}

/**************************************************************************
  Save install info list to file.
**************************************************************************/
void save_install_info_list(const char *filename,
                            struct install_info_list *list)
{
  int i = 0;
  struct section_file *file = secfile_new(TRUE);

  secfile_insert_str(file, MPDB_CAPSTR, "info.options");

  install_info_list_iterate(list, ii) {
    char buf[80];

    fc_snprintf(buf, sizeof(buf), "modpacks.mp%d", i);

    secfile_insert_str(file, ii->name, "%s.name", buf);
    secfile_insert_str(file, modpack_type_name(ii->type),
                       "%s.type", buf);
    secfile_insert_str(file, ii->version, "%s.version", buf);

    i++;
  } install_info_list_iterate_end;

  if (!secfile_save(file, filename, 0, FZ_PLAIN)) {
    log_error("Saving of install info to file %s failed.", filename);
  }

  secfile_destroy(file);
}
