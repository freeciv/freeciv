/***********************************************************************
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
#include "effects.h"
#include "fc_types.h"
#include "game.h"
#include "movement.h"
#include "requirements.h"
#include "unittype.h"

/* server */
#include "rssanity.h"
#include "ruleset.h"
#include "settings.h"

#include "rscompat.h"

#define enough_new_user_flags(_new_flags_, _name_,                        \
                              _LAST_USER_FLAG_, _LAST_USER_FLAG_PREV_)    \
FC_STATIC_ASSERT((ARRAY_SIZE(_new_flags_)                                 \
                  <= _LAST_USER_FLAG_ - _LAST_USER_FLAG_PREV_),           \
                 not_enough_new_##_name_##_user_flags)

#define UTYF_LAST_USER_FLAG_3_2 UTYF_USER_FLAG_50

static int first_free_unit_type_user_flag(void);

/**********************************************************************//**
  Initialize rscompat information structure
**************************************************************************/
void rscompat_init_info(struct rscompat_info *info)
{
  memset(info, 0, sizeof(*info));
}

/**********************************************************************//**
  Ruleset files should have a capabilities string datafile.options
  This checks the string and that the required capabilities are satisfied.
**************************************************************************/
int rscompat_check_capabilities(struct section_file *file,
                                const char *filename,
                                const struct rscompat_info *info)
{
  const char *datafile_options;
  bool ok = FALSE;
  int format;

  if (!(datafile_options = secfile_lookup_str(file, "datafile.options"))) {
    log_fatal("\"%s\": ruleset capability problem:", filename);
    ruleset_error(NULL, LOG_ERROR, "%s", secfile_error());

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
      ruleset_error(NULL, LOG_ERROR, "Capability problem");

      return 0;
    }
    if (!has_capabilities(datafile_options, RULESET_CAPABILITIES)) {
      log_fatal("\"%s\": ruleset datafile claims required option(s)"
                " that we don't support:", filename);
      log_fatal("  datafile options: %s", datafile_options);
      log_fatal("  supported options: %s", RULESET_CAPABILITIES);
      ruleset_error(NULL, LOG_ERROR, "Capability problem");

      return 0;
    }
  }

  if (!secfile_lookup_int(file, &format, "datafile.format_version")) {
    log_error("\"%s\": lacking legal format_version field", filename);
    ruleset_error(NULL, LOG_ERROR, "%s", secfile_error());

    return 0;
  } else if (format == 0) {
    log_error("\"%s\": Illegal format_version value", filename);
    ruleset_error(NULL, LOG_ERROR, "Format version error");
  }

  return format;
}
/**********************************************************************//**
  Different ruleset files within a ruleset directory should all have
  identical datafile.format_version
  This checks the file version against the expected version.

  See also rscompat_check_capabilities
**************************************************************************/
bool rscompat_check_cap_and_version(struct section_file *file,
                                    const char *filename,
                                    const struct rscompat_info *info)
{
  int format_version;

  fc_assert_ret_val(info->version > 0, FALSE);

  format_version = rscompat_check_capabilities(file, filename, info);
  if (format_version <= 0) {
    /* Already logged in rscompat_check_capabilities */
    return FALSE;
  }

  if (format_version != info->version) {
    log_fatal("\"%s\": ruleset datafile format version differs from"
              " other ruleset datafile(s):", filename);
    log_fatal("  datafile format version: %d", format_version);
    log_fatal("  expected format version: %d", info->version);
    ruleset_error(NULL, LOG_ERROR, "Inconsistent format versions");

    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Add all hard obligatory requirements to an action enabler or disable it.

  @param ae the action enabler to add requirements to.
  @return TRUE if adding obligatory hard reqs for the enabler's action
               needs to restart - say if an enabler was added or removed.
**************************************************************************/
static bool
rscompat_enabler_add_obligatory_hard_reqs(struct action_enabler *ae)
{
  struct req_vec_problem *problem;

  struct action *paction = enabler_get_action(ae);
  /* Some changes requires starting to process an action's enablers from
   * the beginning. */
  bool needs_restart = FALSE;

  while ((problem = action_enabler_suggest_repair(ae)) != NULL) {
    /* A hard obligatory requirement is missing. */

    int i;

    if (problem->num_suggested_solutions == 0) {
      /* Didn't get any suggestions about how to solve this. */

      log_error("While adding hard obligatory reqs to action enabler"
                 " for %s: %s"
                 " Don't know how to fix it."
                 " Dropping it.",
                action_rule_name(paction), problem->description);
      ae->ruledit_disabled = TRUE;

      req_vec_problem_free(problem);
      return TRUE;
    }

    /* Sanity check. */
    fc_assert_ret_val(problem->num_suggested_solutions > 0,
                      needs_restart);

    /* Only append is supported for upgrade */
    for (i = 0; i < problem->num_suggested_solutions; i++) {
      if (problem->suggested_solutions[i].operation != RVCO_APPEND) {
        /* A problem that isn't caused by missing obligatory hard
         * requirements has been detected.
         *
         * Probably an old requirement that contradicted a hard requirement
         * that wasn't documented by making it obligatory. In that case all
         * suggested solutions has been applied to the enabler creating a
         * new copy for each possible fulfillment of the new obligatory hard
         * requirement.
         *
         * If another copy of the original enabler has survived this isn't
         * an error. It probably isn't event an indication of a potential
         * problem.
         *
         * If no possible solution survives the enabler was never in use
         * because the action it self would have blocked it. In that case
         * this is an error. */

        log_warn("While adding hard obligatory reqs to action enabler"
                 " for %s: %s"
                 " Dropping it.",
                 action_rule_name(paction), problem->description);
        ae->ruledit_disabled = TRUE;
        req_vec_problem_free(problem);
        return TRUE;
      }
    }

    for (i = 0; i < problem->num_suggested_solutions; i++) {
      struct action_enabler *new_enabler;

      /* There can be more than one suggestion to apply. In that case both
       * are applied to their own copy. The original should therefore be
       * kept for now. */
      new_enabler = action_enabler_copy(ae);

      /* Apply the solution. */
      if (!req_vec_change_apply(&problem->suggested_solutions[i],
                                action_enabler_vector_by_number,
                                new_enabler)) {
        log_error("While adding hard obligatory reqs to action enabler"
                  " for %s: %s"
                  "Failed to apply solution %s."
                  " Dropping it.",
                  action_rule_name(paction), problem->description,
                  req_vec_change_translation(
                    &problem->suggested_solutions[i],
                    action_enabler_vector_by_number_name));
        new_enabler->ruledit_disabled = TRUE;
        req_vec_problem_free(problem);
        return TRUE;
      }

      if (problem->num_suggested_solutions - 1 == i) {
        /* The last modification is to the original enabler. */
        ae->action = new_enabler->action;
        ae->ruledit_disabled = new_enabler->ruledit_disabled;
        requirement_vector_copy(&ae->actor_reqs,
                                &new_enabler->actor_reqs);
        requirement_vector_copy(&ae->target_reqs,
                                &new_enabler->target_reqs);
        FC_FREE(new_enabler);
      } else {
        /* Register the new enabler */
        action_enabler_add(new_enabler);

        /* This changes the number of action enablers. */
        needs_restart = TRUE;
      }
    }

    req_vec_problem_free(problem);

    if (needs_restart) {
      /* May need to apply future upgrades to the copies too. */
      return TRUE;
    }
  }

  return needs_restart;
}

/**********************************************************************//**
  Update existing action enablers for new hard obligatory requirements.
  Disable those that can't be upgraded.
**************************************************************************/
void rscompat_enablers_add_obligatory_hard_reqs(void)
{
  log_normal("action enablers: adding obligatory hard requirements.");
  log_warn("More than one way to fulfill a new obligatory hard requirement"
           " may exist."
           " In that case the enabler is copied so each alternative"
           " solution is applied to a copy of the enabler."
           " If an action enabler becomes self contradicting after applying"
           " a solution it is dropped."
           " Note that other copies of the original enabler may have"
           " survived even if one copy is dropped.");

  action_iterate(act_id) {
    bool restart_enablers_for_action;

    do {
      restart_enablers_for_action = FALSE;
      action_enabler_list_iterate(action_enablers_for_action(act_id), ae) {
        if (ae->ruledit_disabled) {
          /* Ignore disabled enablers */
          continue;
        }
        if (rscompat_enabler_add_obligatory_hard_reqs(ae)) {
          /* Something important, probably the number of action enablers
           * for this action, changed. Start over again on this action's
           * enablers. */
          restart_enablers_for_action = TRUE;
          break;
        }
      } action_enabler_list_iterate_end;
    } while (restart_enablers_for_action);
  } action_iterate_end;
}

/**********************************************************************//**
  Do compatibility things with names before they are referred to. Runs
  after names are loaded from the ruleset but before the ruleset objects
  that may refer to them are loaded.

  This is needed when previously hard coded items that are referred to in
  the ruleset them self becomes ruleset defined.

  Returns FALSE if an error occurs.
**************************************************************************/
bool rscompat_names(struct rscompat_info *info)
{
  if (info->version < RSFORMAT_3_3) {
    int first_free;
    int i;

    /* Some unit type flags moved to the ruleset between 3.2 and 3.3.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_flags_33[] = {
      { N_("NoVeteran"), N_("May acquire veteran status.") }
    };

    enough_new_user_flags(new_flags_33, unit_type,
                          UTYF_LAST_USER_FLAG, UTYF_LAST_USER_FLAG_3_2);

    /* Unit type flags. */
    first_free = first_free_unit_type_user_flag() + UTYF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_flags_33); i++) {
      if (UTYF_USER_FLAG_1 + MAX_NUM_USER_UNIT_FLAGS <= first_free + i) {
        /* Can't add the user unit type flags. */
        ruleset_error(NULL, LOG_ERROR,
                      "Can't upgrade the ruleset. Not enough free unit type "
                      "user flags to add user flags for the unit type flags "
                      "that used to be hardcoded.");
        return FALSE;
      }
      /* Shouldn't be possible for valid old ruleset to have flag names that
       * clash with these ones */
      if (unit_type_flag_id_by_name(new_flags_33[i].name, fc_strcasecmp)
          != unit_type_flag_id_invalid()) {
        ruleset_error(NULL, LOG_ERROR,
                      "Ruleset had illegal user unit type flag '%s'",
                      new_flags_33[i].name);
        return FALSE;
      }
      set_user_unit_type_flag_name(first_free + i,
                                   new_flags_33[i].name,
                                   new_flags_33[i].helptxt);
    }
  }

  /* No errors encountered. */
  return TRUE;
}

/**********************************************************************//**
  Adjust effects
**************************************************************************/
static bool effect_list_compat_cb(struct effect *peffect, void *data)
{
  /* Go to the next effect. */
  return TRUE;
}

/**********************************************************************//**
  Do compatibility things after regular ruleset loading.
**************************************************************************/
void rscompat_postprocess(struct rscompat_info *info)
{
  if (!info->compat_mode) {
    /* There isn't anything here that should be done outside of compat
     * mode. */
    return;
  }

  /* Upgrade existing effects. Done before new effects are added to prevent
   * the new effects from being upgraded by accident. */
  iterate_effect_cache(effect_list_compat_cb, info);

  /* Make sure that all action enablers added or modified by the
   * compatibility post processing fulfills all hard action requirements. */
  rscompat_enablers_add_obligatory_hard_reqs();

  /* The ruleset may need adjustments it didn't need before compatibility
   * post processing.
   *
   * If this isn't done a user of ruleset compatibility that ends up using
   * the rules risks bad rules. A user that saves the ruleset rather than
   * using it risks an unexpected change on the next load and save. */
  autoadjust_ruleset_data();
}

/**********************************************************************//**
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
