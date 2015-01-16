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

/* common */
#include "actions.h"
#include "requirements.h"
#include "unittype.h"

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
                                const char *filename,
                                struct rscompat_info *info)
{
  const char *datafile_options;
  bool ok = FALSE;

  if (!(datafile_options = secfile_lookup_str(file, "datafile.options"))) {
    log_fatal("\"%s\": ruleset capability problem:", filename);
    ruleset_error(LOG_ERROR, "%s", secfile_error());

    return 0;
  }

  if (info->compat_mode) {
    /* Check alternative capstr first, so that when we do the main capstr check,
     * we already know that failures there are fatal (error message correct, can return
     * immediately) */

    if (has_capabilities(RULESET_COMPAT_CAP, datafile_options)
        && has_capabilities(datafile_options, RULESET_COMPAT_CAP)) {
      ok = TRUE;
    }
  }

  if (!ok) {
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
  }

  return secfile_lookup_int_default(file, 1, "datafile.format_version");
}

/**************************************************************************
  Find and return the first unused unit type user flag. If all unit type
  user flags are taken MAX_NUM_USER_UNIT_FLAGS is returned.
**************************************************************************/
static int first_free_unit_type_user_flag(void)
{
  int flag;

  /* Find the first unused user defined unit type flag. */
  for (flag = 0; flag < MAX_NUM_USER_UNIT_FLAGS; flag++) {
    if (unit_type_flag_id_name_cb(flag + UTYF_USER_FLAG_1) == NULL) {
      return flag;
    }
  }

  /* All unit type user flags are taken. */
  return MAX_NUM_USER_UNIT_FLAGS;
}

/**************************************************************************
  Do compatibility things with names before they are referred to. Runs
  after names are loaded from the ruleset but before the ruleset objects
  that may refer to them are loaded.

  This is needed when previously hard coded items that are referred to in
  the ruleset them self becomes ruleset defined.

  Returns FALSE if an error occurs.
**************************************************************************/
bool rscompat_names(struct rscompat_info *info)
{
  if (info->ver_units < 10) {
    /* The unit type flags Capturer and Capturable have moved to the
     * ruleset. Add them. */

    int unit_flag_position = first_free_unit_type_user_flag();

    if (MAX_NUM_USER_UNIT_FLAGS <= unit_flag_position + 2) {
      /* Can't add the user unit type flags. */
      log_error("Can't upgrade the ruleset. Not enough free unit type "
                "user flags to add the user unit type flags Capturer and "
                "Capturable.");
      return FALSE;
    }

    /* Add the unit type flag Capturer. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("Capturer"),
                                 N_("Can capture some enemy units."));
    unit_flag_position++;

    /* Add the unit type flag Capturable. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("Capturable"),
                                 N_("Can be captured by some enemy "
                                    "units."));
    unit_flag_position++;
  }

  /* No errors encountered. */
  return TRUE;
}

/**************************************************************************
  Do compatibility things after regular ruleset loading.
**************************************************************************/
void rscompat_postprocess(struct rscompat_info *info)
{
  if (!info->compat_mode) {
    /* There isn't anything here yet that should be done outside of compat
     * mode. */
    return;
  }

  if (info->ver_game < 10) {
    /* Unit capture is now action enabler controlled. Add the old rule that
     * units with the Capturer unit type flag can capture units with the
     * Capturable unit type flag. */

    struct action_enabler *capture;

    capture = action_enabler_new();

    capture->action = ACTION_CAPTURE_UNITS;

    /* The actor unit must have the unit type flag Capturer, belong to a
     * player that is at war with each player that owns a target unit and
     * have at least one move fragment left. */
    requirement_vector_init(&(capture->actor_reqs));
    requirement_vector_append(&capture->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Capturer"));
    requirement_vector_append(&capture->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           TRUE, "War"));
    requirement_vector_append(&capture->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, "1"));

    /* The target unit(s) must all have the Capturable unit type flag and
     * can't be inside a city. */
    requirement_vector_init(&(capture->target_reqs));
    requirement_vector_append(&capture->target_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Capturable"));
    requirement_vector_append(&capture->target_reqs,
                              req_from_str("CityTile", "Local", FALSE,
                                           FALSE, "Center"));

    action_enabler_add(capture);
  }
}
