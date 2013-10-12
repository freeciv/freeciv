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
#include "registry.h"

/* common */
#include "game.h"
#include "movement.h"
#include "unittype.h"

/* server */
#include "ruleset.h"

#include "rulesave.h"

/**************************************************************************
  Create new ruleset section file with common header.
**************************************************************************/
static struct section_file *create_ruleset_file(const char *rsname,
                                                const char *rstype)
{
  struct section_file *sfile = secfile_new(TRUE);
  char buf[500];

  if (rsname != NULL) {
    fc_snprintf(buf, sizeof(buf), "Template %s %s data for Freeciv", rsname, rstype);
  } else {
    fc_snprintf(buf, sizeof(buf), "Template %s data for Freeciv", rstype);
  }

  secfile_insert_str(sfile, buf, "datafile.description");
  secfile_insert_str(sfile, RULESET_CAPABILITIES, "datafile.options");

  return sfile;
}

/**************************************************************************
  Save ruleset file.
**************************************************************************/
static bool save_ruleset_file(struct section_file *sfile, const char *filename)
{
  return secfile_save(sfile, filename, 0, FZ_PLAIN);
}

/**************************************************************************
  Save buildings.ruleset
**************************************************************************/
static bool save_buildings_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "building");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save cities.ruleset
**************************************************************************/
static bool save_cities_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "cities");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save effects.ruleset
**************************************************************************/
static bool save_effects_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "effect");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save game.ruleset
**************************************************************************/
static bool save_game_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "game");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save governments.ruleset
**************************************************************************/
static bool save_governments_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "government");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save nations.ruleset
**************************************************************************/
static bool save_nations_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "nation");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save techs.ruleset
**************************************************************************/
static bool save_techs_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "tech");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save terrain.ruleset
**************************************************************************/
static bool save_terrain_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "terrain");

  if (sfile == NULL) {
    return FALSE;
  }

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save one veteran system.
**************************************************************************/
static bool save_veteran_system(struct section_file *sfile, const char *path,
                                struct veteran_system *vsystem)
{
  const char *vlist_name[vsystem->levels];
  int vlist_power[vsystem->levels];
  int vlist_raise[vsystem->levels];
  int vlist_wraise[vsystem->levels];
  int vlist_move[vsystem->levels];
  int i;

  for (i = 0; i < vsystem->levels; i++) {
    vlist_name[i] = rule_name(&(vsystem->definitions[i].name));
    vlist_power[i] = vsystem->definitions[i].power_fact;
    vlist_raise[i] = vsystem->definitions[i].raise_chance;
    vlist_wraise[i] = vsystem->definitions[i].work_raise_chance;
    vlist_move[i] = vsystem->definitions[i].move_bonus;
  }

  secfile_insert_str_vec(sfile, vlist_name, vsystem->levels,
                         "%s.veteran_names", path);
  secfile_insert_int_vec(sfile, vlist_power, vsystem->levels,
                         "%s.veteran_power_fact", path);
  secfile_insert_int_vec(sfile, vlist_raise, vsystem->levels,
                         "%s.veteran_raise_chance", path);
  secfile_insert_int_vec(sfile, vlist_wraise, vsystem->levels,
                         "%s.veteran_work_raise_chance", path);
  secfile_insert_int_vec(sfile, vlist_move, vsystem->levels,
                         "%s.veteran_move_bonus", path);

  return TRUE;
}

/**************************************************************************
  Save units.ruleset
**************************************************************************/
static bool save_units_ruleset(const char *filename, const char *name)
{
  struct section_file *sfile = create_ruleset_file(name, "unit");
  int i;
  int sect_idx;

  if (sfile == NULL) {
    return FALSE;
  }

  for (i = 0; i < MAX_NUM_USER_UNIT_FLAGS; i++) {
    const char *flagname = unit_type_flag_id_name(i + UTYF_USER_FLAG_1);
    const char *helptxt = unit_type_flag_helptxt(i + UTYF_USER_FLAG_1);

    if (flagname != NULL && helptxt != NULL) {
      secfile_insert_str(sfile, flagname, "control.flags%d.name", i);
      secfile_insert_str(sfile, helptxt, "control.flags%d.helptxt", i);
    }
  }

  save_veteran_system(sfile, "veteran_system", game.veteran);

  sect_idx = 0;
  unit_class_iterate(puc) {
    char path[512];
    char *hut_str = NULL;
    const char *flag_names[UCF_COUNT];
    int flagi;
    int set_count;

    fc_snprintf(path, sizeof(path), "unitclass_%d", sect_idx++);

    secfile_insert_str(sfile,
                       untranslated_name(&(puc->name)),
                       "%s.name", path);
    if (strcmp(skip_intl_qualifier_prefix(untranslated_name(&(puc->name))),
               rule_name(&(puc->name)))) {
      secfile_insert_str(sfile,
                         rule_name(&(puc->name)),
                         "%s.rule_name", path);
    }
    secfile_insert_int(sfile, puc->min_speed / SINGLE_MOVE,
                       "%s.min_speed", path);
    secfile_insert_int(sfile, puc->hp_loss_pct, "%s.hp_loss_pct", path);
    if (puc->non_native_def_pct != 100) {
      secfile_insert_int(sfile, puc->non_native_def_pct,
                         "%s.non_native_def_pct", path);
    }
    if (puc->hut_behavior != HUT_NORMAL) {
      switch (puc->hut_behavior) {
      case HUT_NORMAL:
        hut_str = "Normal";
        break;
      case HUT_NOTHING:
        hut_str = "Nothing";
        break;
      case HUT_FRIGHTEN:
        hut_str = "Frighten";
        break;
      }
      fc_assert(hut_str != NULL);
      secfile_insert_str(sfile, hut_str, "%s.hut_behavior", path);
    }

    set_count = 0;
    for (flagi = 0; flagi < UCF_COUNT; flagi++) {
      if (uclass_has_flag(puc, flagi)) {
        flag_names[set_count++] = unit_class_flag_id_name(flagi);
      }
    }

    if (set_count > 0) {
      secfile_insert_str_vec(sfile, flag_names, set_count,
                             "%s.flags", path);
    }

  } unit_class_iterate_end;

  return save_ruleset_file(sfile, filename);
}

/**************************************************************************
  Save ruleset to directory given.
**************************************************************************/
bool save_ruleset(const char *path, const char *name)
{
  if (make_dir(path)) {
    bool success = TRUE;
    char filename[500];

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/buildings.ruleset", path);
      success = save_buildings_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/cities.ruleset", path);
      success = save_cities_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/effects.ruleset", path);
      success = save_effects_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/game.ruleset", path);
      success = save_game_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/governments.ruleset", path);
      success = save_governments_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/nations.ruleset", path);
      success = save_nations_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/techs.ruleset", path);
      success = save_techs_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/terrain.ruleset", path);
      success = save_terrain_ruleset(filename, name);
    }

    if (success) {
      fc_snprintf(filename, sizeof(filename), "%s/units.ruleset", path);
      success = save_units_ruleset(filename, name);
    }

    return success;
  } else {
    log_error("Failed to create directory %s", path);
    return FALSE;
  }

  return TRUE;
}
