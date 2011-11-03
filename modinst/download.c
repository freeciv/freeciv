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

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <unistd.h>
#include <errno.h>

/* utility */
#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "netintf.h"
#include "netfile.h"
#include "registry.h"

/* modinst */
#include "download.h"

/**************************************************************************
  Return path to control directory
**************************************************************************/
static char *control_dir(const struct fcmp_params *fcmp)
{
  static char controld[500] = { '\0' };

  if (controld[0] != '\0') {
    return controld;
  }

  if (fcmp->inst_prefix == NULL) {
    return NULL;
  }

  fc_snprintf(controld, sizeof(controld),
              "%s/" DATASUBDIR "/control",
              fcmp->inst_prefix);

  return controld;
}

/**************************************************************************
  Message callback called by netfile module when downloading files.
**************************************************************************/
static void nf_cb(const char *msg, void *data)
{
  dl_msg_callback mcb = (dl_msg_callback) data;

  if (mcb != NULL) {
    mcb(msg);
  }
}

/**************************************************************************
  Download modpack from a given URL
**************************************************************************/
const char *download_modpack(const char *URL,
			     const struct fcmp_params *fcmp,
                             dl_msg_callback mcb,
                             dl_pb_callback pbcb)
{
  char *controld;
  char local_dir[2048];
  char local_name[2048];
  int start_idx;
  int filenbr, total_files;
  struct section_file *control;
  const char *control_capstr;
  const char *baseURL;
  enum modpack_type type;
  const char *typestr;
  char fileURL[2048];
  const char *src_name;
  bool partial_failure = FALSE;

  if (URL == NULL || URL[0] == '\0') {
    return _("No URL given");
  }

  if (strlen(URL) < strlen(MODPACK_SUFFIX)
      || strcmp(URL + strlen(URL) - strlen(MODPACK_SUFFIX), MODPACK_SUFFIX)) {
    return _("This does not look like modpack URL");
  }

  for (start_idx = strlen(URL) - strlen(MODPACK_SUFFIX);
       start_idx > 0 && URL[start_idx - 1] != '/';
       start_idx--) {
    /* Nothing */
  }

  log_normal(_("Installing modpack %s from %s"), URL + start_idx, URL);

  if (fcmp->inst_prefix == NULL) {
    return _("Cannot install to given directory hierarchy");
  }

  controld = control_dir(fcmp);

  if (controld == NULL) {
    return _("Cannot determine control directory");
  }

  if (!make_dir(controld)) {
    return _("Cannot create required directories");
  }

  if (mcb != NULL) {
    mcb(_("Downloading modpack control file."));
  }

  control = netfile_get_section_file(URL, nf_cb, mcb);

  if (control == NULL) {
    return _("Failed to get and parse modpack control file");
  }

  control_capstr = secfile_lookup_str(control, "info.options");
  if (control_capstr == NULL) {
    secfile_destroy(control);
    return _("Modpack control file has no capability string");
  }

  if (!has_capabilities(MODPACK_CAPSTR, control_capstr)) {
    log_error("Incompatible control file:");
    log_error("  control file options: %s", control_capstr);
    log_error("  supported options:    %s", MODPACK_CAPSTR);

    secfile_destroy(control);

    return _("Modpack control file is incompatible");  
  }

  typestr = secfile_lookup_str(control, "info.type");
  type = modpack_type_by_name(typestr, fc_strcasecmp);
  if (!modpack_type_is_valid(type)) {
    return _("Illegal modpack type");
  }

  if (type == MPT_SCENARIO) {
    fc_snprintf(local_dir, sizeof(local_dir),
                "%s/scenarios", fcmp->inst_prefix);
  } else {
    fc_snprintf(local_dir, sizeof(local_dir),
                "%s/" DATASUBDIR, fcmp->inst_prefix);
  }

  baseURL = secfile_lookup_str(control, "info.baseURL");

  total_files = 0;
  do {
    src_name = secfile_lookup_str_default(control, NULL,
                                          "files.list%d.src", total_files);

    if (src_name != NULL) {
      total_files++;
    }
  } while (src_name != NULL);

  if (pbcb != NULL) {
    /* Control file already downloaded */
    double fraction = (double) 1 / (total_files + 1);

    pbcb(fraction);
  }

  filenbr = 0;
  for (filenbr = 0; filenbr < total_files; filenbr++) {
    const char *dest_name;
    int i;
    bool illegal_filename = FALSE;

    src_name = secfile_lookup_str_default(control, NULL,
                                          "files.list%d.src", filenbr);

    dest_name = secfile_lookup_str_default(control, NULL,
                                           "files.list%d.dest", filenbr);

    if (dest_name == NULL || dest_name[0] == '\0') {
      /* Missing dest name is ok, we just default to src_name */
      dest_name = src_name;
    }

    for (i = 0; dest_name[i] != '\0'; i++) {
      if (dest_name[i] == '.' && dest_name[i+1] == '.') {
        if (mcb != NULL) {
          char buf[2048];

          fc_snprintf(buf, sizeof(buf), _("Illegal path for %s"),
                      dest_name);
          mcb(buf);
        }
        partial_failure = TRUE;
        illegal_filename = TRUE;
      }
    }

    if (!illegal_filename) {
      fc_snprintf(local_name, sizeof(local_name),
                  "%s/%s", local_dir, dest_name);
      for (i = strlen(local_name) - 1 ; local_name[i] != '/' ; i--) {
        /* Nothing */
      }
      local_name[i] = '\0';
      if (!make_dir(local_name)) {
        secfile_destroy(control);
        return _("Cannot create required directories");
      }
      local_name[i] = '/';

      if (mcb != NULL) {
        char buf[2048];

        fc_snprintf(buf, sizeof(buf), _("Downloading %s"), src_name);
        mcb(buf);
      }

      fc_snprintf(fileURL, sizeof(fileURL), "%s/%s", baseURL, src_name);
      if (!netfile_download_file(fileURL, local_name, nf_cb, mcb)) {
        if (mcb != NULL) {
          char buf[2048];

          fc_snprintf(buf, sizeof(buf), _("Failed to download %s"),
                      src_name);
          mcb(buf);
        }
        partial_failure = TRUE;
      }
    }

    if (pbcb != NULL) {
      /* Count download of control file also */
      double fraction = (double)(filenbr + 2) / (total_files + 1);

      pbcb(fraction);
    }
  }

  secfile_destroy(control);

  if (partial_failure) {
    return _("Some parts of the modpack failed to install.");
  }

  return NULL;
}

/**************************************************************************
  Download modpack list
**************************************************************************/
const char *download_modpack_list(const struct fcmp_params *fcmp,
                                  modpack_list_setup_cb cb,
                                  dl_msg_callback mcb)
{
  const char *controld = control_dir(fcmp);
  struct section_file *list_file;
  const char *list_capstr;
  int modpack_count;
  const char *msg;
  const char *mp_name;

  if (controld == NULL) {
    return _("Cannot determine control directory");
  }

  if (!make_dir(controld)) {
    return _("Cannot create required directories");
  }

  list_file = netfile_get_section_file(fcmp->list_url, nf_cb, mcb);

  if (list_file == NULL) {
    return _("Cannot fetch and parse modpack list");
  }

  list_capstr = secfile_lookup_str(list_file, "info.options");
  if (list_capstr == NULL) {
    secfile_destroy(list_file);
    return _("Modpack list has no capability string");
  }

  if (!has_capabilities(MODLIST_CAPSTR, list_capstr)) {
    log_error("Incompatible modpack list file:");
    log_error("  list file options: %s", list_capstr);
    log_error("  supported options: %s", MODLIST_CAPSTR);

    secfile_destroy(list_file);

    return _("Modpack list is incompatible");  
  }

  msg = secfile_lookup_str_default(list_file, NULL, "info.message");

  if (msg != NULL) {
    mcb(msg);
  }

  modpack_count = 0;
  do {
    const char *mpURL;
    const char *mpver;
    const char *mplic;
    const char *mp_type_str;

    mp_name = secfile_lookup_str_default(list_file, NULL,
                                         "modpacks.list%d.name", modpack_count);
    mpver = secfile_lookup_str_default(list_file, NULL,
                                       "modpacks.list%d.version",
                                       modpack_count);
    mplic = secfile_lookup_str_default(list_file, NULL,
                                       "modpacks.list%d.license",
                                       modpack_count);
    mp_type_str = secfile_lookup_str_default(list_file, NULL,
                                             "modpacks.list%d.type",
                                             modpack_count);
    mpURL = secfile_lookup_str_default(list_file, NULL,
                                       "modpacks.list%d.URL", modpack_count);

    if (mp_name != NULL && mpURL != NULL) {
      enum modpack_type type = modpack_type_by_name(mp_type_str, fc_strcasecmp);
      if (!modpack_type_is_valid(type)) {
        log_error("Illegal modpack type \"%s\"", mp_type_str ? mp_type_str : "NULL");
      }
      if (mpver == NULL) {
        mpver = "-";
      }
      cb(mp_name, mpURL, mpver, mplic, type);
      modpack_count++;
    }
  } while (mp_name != NULL);

  return NULL;
}
