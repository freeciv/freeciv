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
#include "fcintl.h"
#include "log.h"
#include "mem.h"

/* modinst */
#include "mpdb.h"

#include "modinst.h"

static struct install_info_list *main_ii_list;
static bool main_list_changed = FALSE;
static struct install_info_list *scenario_ii_list;
static bool scenario_list_changed = FALSE;

static char main_ii_filename[500];
static char scenario_ii_filename[500];

/**************************************************************************
  Load all required install info lists.
**************************************************************************/
void load_install_info_lists(struct fcmp_params *fcmp)
{
  main_ii_list = install_info_list_new();
  scenario_ii_list = install_info_list_new();

  fc_snprintf(main_ii_filename, sizeof(main_ii_filename),
              "%s/" DATASUBDIR "/" FCMP_CONTROLD "/modpacks.db", fcmp->inst_prefix);
  fc_snprintf(scenario_ii_filename, sizeof(scenario_ii_filename),
              "%s/scenarios/" FCMP_CONTROLD "/modpacks.db", fcmp->inst_prefix);

  load_install_info_list(main_ii_filename, main_ii_list);
  load_install_info_list(scenario_ii_filename, scenario_ii_list);
}

/**************************************************************************
  Save all changed install info lists.
**************************************************************************/
void save_install_info_lists(struct fcmp_params *fcmp)
{
  if (main_list_changed) {
    char controld[500];

    fc_snprintf(controld, sizeof(controld),
                "%s/" DATASUBDIR "/" FCMP_CONTROLD, fcmp->inst_prefix);

    if (make_dir(controld)) {
      save_install_info_list(main_ii_filename, main_ii_list);
    } else {
      log_error(_("Failed to create control directory \"%s\""),
                controld);
    }
  }

  if (scenario_list_changed) {
    char controld[500];

    fc_snprintf(controld, sizeof(controld),
                "%s/scenarios/" FCMP_CONTROLD, fcmp->inst_prefix);

    if (make_dir(controld)) {
      save_install_info_list(scenario_ii_filename, scenario_ii_list);
    } else {
      log_error(_("Failed to create control directory \"%s\""),
                controld);
    }
  }

  install_info_list_iterate(scenario_ii_list, ii) {
    FC_FREE(ii);
  } install_info_list_iterate_end;

  install_info_list_iterate(main_ii_list, ii) {
    FC_FREE(ii);
  } install_info_list_iterate_end;

  install_info_list_destroy(scenario_ii_list);
  install_info_list_destroy(main_ii_list);
}

/**************************************************************************
  Modpack successfully installed. Store information to appropriate list.
**************************************************************************/
void update_install_info_lists(const char *name,
                               enum modpack_type type,
                               const char *version)
{
  struct install_info *new_ii;
  struct install_info_list *ii_list;

  if (type == MPT_SCENARIO) {
    ii_list = scenario_ii_list;
    scenario_list_changed = TRUE;
  } else {
    ii_list = main_ii_list;
    main_list_changed = TRUE;
  }

  install_info_list_iterate(ii_list, ii) {
    if (!fc_strcasecmp(name, ii->name)) {
      if (type != ii->type) {
        /* TRANS: ... Ubermod ... Ruleset, not Scenario */
        log_normal(_("Earlier installation of %s found, but it seems to be %s, not %s"),
                    name, _(modpack_type_name(ii->type)), _(modpack_type_name(type)));
      } else {
        log_debug("Earlier installation of %s found", name);
      }

      ii->type = type;
      strncpy(ii->version, version, sizeof(ii->version));

      return;
    }
  } install_info_list_iterate_end;

  /* No existing entry with that name found, creating new one */
  new_ii = fc_malloc(sizeof(*new_ii));

  strncpy(new_ii->name, name, sizeof(new_ii->name));
  new_ii->type = type;
  strncpy(new_ii->version, version, sizeof(new_ii->version));

  install_info_list_append(ii_list, new_ii);
}

/**************************************************************************
  Get version number string of currently installed version, or NULL if not
  installed.
**************************************************************************/
const char *get_installed_version(const char *name, enum modpack_type type)
{
  struct install_info_list *ii_list;

  if (type == MPT_SCENARIO) {
    ii_list = scenario_ii_list;
  } else {
    ii_list = main_ii_list;
  }

  install_info_list_iterate(ii_list, ii) {
    if (!fc_strcasecmp(name, ii->name)) {
      return ii->version;
    }
  } install_info_list_iterate_end;

  return NULL;
}
