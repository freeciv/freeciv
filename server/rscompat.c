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
#include "game.h"
#include "movement.h"
#include "requirements.h"
#include "unittype.h"

/* server */
#include "rssanity.h"
#include "ruleset.h"

#include "rscompat.h"

#define enough_new_user_flags(_new_flags_, _name_,                        \
                              _LAST_USER_FLAG_, _LAST_USER_FLAG_PREV_)    \
FC_STATIC_ASSERT((ARRAY_SIZE(_new_flags_)                                 \
                  <= _LAST_USER_FLAG_ - _LAST_USER_FLAG_PREV_),           \
                 not_enough_new_##_name_##_user_flags)

#define UTYF_LAST_USER_FLAG_3_0 UTYF_USER_FLAG_40
#define UCF_LAST_USER_FLAG_3_0 UCF_USER_FLAG_8
#define TER_LAST_USER_FLAG_3_0 TER_USER_8

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
                                struct rscompat_info *info)
{
  const char *datafile_options;
  bool ok = FALSE;
  int format;

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

  if (!secfile_lookup_int(file, &format, "datafile.format_version")) {
    log_error("\"%s\": lacking legal format_version field", filename);
    ruleset_error(LOG_ERROR, "%s", secfile_error());

    return 0;
  } else if (format == 0) {
    log_error("\"%s\": Illegal format_version value", filename);
    ruleset_error(LOG_ERROR, "Format version error");
  }

  return format;
}

/**********************************************************************//**
  Add all hard obligatory requirements to an action enabler or disable it.
  @param ae the action enabler to add requirements to.
  @return TRUE iff adding obligatory hard reqs for the enabler's action
               needs to restart - say if an enabler was added or removed.
**************************************************************************/
static bool
rscompat_enabler_add_obligatory_hard_reqs(struct action_enabler *ae)
{
  struct req_vec_problem *problem;

  struct action *paction = action_by_number(ae->action);
  /* Some changes requires starting to process an action's enablers from
   * the beginning. */
  bool needs_restart = FALSE;

  while ((problem = action_enabler_suggest_repair(ae)) != NULL) {
    /* A hard obligatory requirement is missing. */

    int i;

    if (problem->num_suggested_solutions == 0) {
      /* Didn't get any suggestions about how to solve this. */

      log_error("Dropping an action enabler for %s."
                " Don't know how to fix: %s.",
                action_rule_name(paction), problem->description);
      ae->disabled = TRUE;

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
         * requirements has been detected. Probably an old requirement that
         * contradicted a hard requirement that wasn't documented by making
         * it obligatory. In that case the enabler was never in use. The
         * action it self would have blocked it. */

        log_error("While adding hard obligatory reqs to action enabler"
                  " for %s: %s Dropping it.",
                  action_rule_name(paction), problem->description);
        ae->disabled = TRUE;
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
        log_error("Failed to apply solution %s for %s to action enabler"
                  " for %s. Dropping it.",
                  req_vec_change_translation(
                    &problem->suggested_solutions[i],
                    action_enabler_vector_by_number_name),
                  problem->description, action_rule_name(paction));
        new_enabler->disabled = TRUE;
        req_vec_problem_free(problem);
        return TRUE;
      }

      if (problem->num_suggested_solutions - 1 == i) {
        /* The last modification is to the original enabler. */
        ae->action = new_enabler->action;
        ae->disabled = new_enabler->disabled;
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
  action_iterate(act_id) {
    bool restart_enablers_for_action;
    do {
      restart_enablers_for_action = FALSE;
      action_enabler_list_iterate(action_enablers_for_action(act_id), ae) {
        if (ae->disabled) {
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
    } while (restart_enablers_for_action == TRUE);
  } action_iterate_end;
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

/**********************************************************************//**
  Find and return the first unused unit class user flag. If all unit class
  user flags are taken MAX_NUM_USER_UCLASS_FLAGS is returned.
**************************************************************************/
static int first_free_unit_class_user_flag(void)
{
  int flag;

  /* Find the first unused user defined unit class flag. */
  for (flag = 0; flag < MAX_NUM_USER_UCLASS_FLAGS; flag++) {
    if (unit_class_flag_id_name_cb(flag + UCF_USER_FLAG_1) == NULL) {
      return flag;
    }
  }

  /* All unit class user flags are taken. */
  return MAX_NUM_USER_UCLASS_FLAGS;
}

/**********************************************************************//**
  Find and return the first unused terrain user flag. If all terrain
  user flags are taken MAX_NUM_USER_TER_FLAGS is returned.
**************************************************************************/
static int first_free_terrain_user_flag(void)
{
  int flag;

  /* Find the first unused user defined terrain flag. */
  for (flag = 0; flag < MAX_NUM_USER_TER_FLAGS; flag++) {
    if (terrain_flag_id_name_cb(flag + TER_USER_1) == NULL) {
      return flag;
    }
  }

  /* All terrain user flags are taken. */
  return MAX_NUM_USER_TER_FLAGS;
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
  if (info->ver_units < 20) {
    /* Some unit type flags moved to the ruleset between 3.0 and 3.1.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_flags_31[] = {
      { N_("BeachLander"), N_("Won't lose all movement when moving from"
                              " non-native terrain to native terrain.") },
      { N_("Cant_Fortify"), NULL },
    };
    enough_new_user_flags(new_flags_31, unit_type,
                          UTYF_LAST_USER_FLAG, UTYF_LAST_USER_FLAG_3_0);

    /* Some unit class flags moved to the ruleset between 3.0 and 3.1.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_class_flags_31[] = {
      { N_("Missile"), N_("Unit is destroyed when it attacks") },
      { N_("CanPillage"), N_("Can pillage tile improvements.") },
      { N_("CanFortify"), N_("Gets a 50% defensive bonus while"
                             " in cities.") },
    };
    enough_new_user_flags(new_class_flags_31, unit_class,
                          UCF_LAST_USER_FLAG, UCF_LAST_USER_FLAG_3_0);

    int first_free;
    int i;

    /* Unit type flags. */
    first_free = first_free_unit_type_user_flag() + UTYF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_flags_31); i++) {
      if (UTYF_USER_FLAG_1 + MAX_NUM_USER_UNIT_FLAGS <= first_free + i) {
        /* Can't add the user unit type flags. */
        ruleset_error(LOG_ERROR,
                      "Can't upgrade the ruleset. Not enough free unit type "
                      "user flags to add user flags for the unit type flags "
                      "that used to be hardcoded.");
        return FALSE;
      }
      /* Shouldn't be possible for valid old ruleset to have flag names that
       * clash with these ones */
      if (unit_type_flag_id_by_name(new_flags_31[i].name, fc_strcasecmp)
          != unit_type_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user unit type flag '%s'",
                      new_flags_31[i].name);
        return FALSE;
      }
      set_user_unit_type_flag_name(first_free + i,
                                   new_flags_31[i].name,
                                   new_flags_31[i].helptxt);
    }

    /* Unit type class flags. */
    first_free = first_free_unit_class_user_flag() + UCF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_class_flags_31); i++) {
      if (UCF_USER_FLAG_1 + MAX_NUM_USER_UCLASS_FLAGS <= first_free + i) {
        /* Can't add the user unit type class flags. */
        ruleset_error(LOG_ERROR,
                      "Can't upgrade the ruleset. Not enough free unit "
                      "type class user flags to add user flags for the "
                      "unit type class flags that used to be hardcoded.");
        return FALSE;
      }
      /* Shouldn't be possible for valid old ruleset to have flag names that
       * clash with these ones */
      if (unit_class_flag_id_by_name(new_class_flags_31[i].name,
                                     fc_strcasecmp)
          != unit_class_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user unit class flag '%s'",
                      new_class_flags_31[i].name);
        return FALSE;
      }
      set_user_unit_class_flag_name(first_free + i,
                                    new_class_flags_31[i].name,
                                    new_class_flags_31[i].helptxt);
    }
  }

  if (info->ver_terrain < 20) {
    /* Some terrain flags moved to the ruleset between 3.0 and 3.1.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_flags_31[] = {
      { N_("NoFortify"), N_("No units can fortify on this terrain.") },
    };
    enough_new_user_flags(new_flags_31, terrain,
                          TER_USER_LAST, TER_LAST_USER_FLAG_3_0);

    int first_free;
    int i;

    /* Terrain flags. */
    first_free = first_free_terrain_user_flag() + TER_USER_1;

    for (i = 0; i < ARRAY_SIZE(new_flags_31); i++) {
      if (TER_USER_1 + MAX_NUM_USER_TER_FLAGS <= first_free + i) {
        /* Can't add the user terrain flags. */
        ruleset_error(LOG_ERROR,
                      "Can't upgrade the ruleset. Not enough free terrain "
                      "user flags to add user flags for the terrain flags "
                      "that used to be hardcoded.");
        return FALSE;
      }
      /* Shouldn't be possible for valid old ruleset to have flag names that
       * clash with these ones */
      if (terrain_flag_id_by_name(new_flags_31[i].name, fc_strcasecmp)
          != terrain_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user terrain flag '%s'",
                      new_flags_31[i].name);
        return FALSE;
      }
      set_user_terrain_flag_name(first_free + i,
                                 new_flags_31[i].name,
                                 new_flags_31[i].helptxt);
    }
  }

  /* No errors encountered. */
  return TRUE;
}

/**********************************************************************//**
  Handle a universal being separated from an original universal.

  A universal may be split into two new universals. An effect may mention
  the universal that now has been split in its requirement list. In that
  case two effect - one for the original and one for the universal being
  separated from it - are needed.

  Check if the original universal is mentioned in the requirement list of
  peffect. Handle creating one effect for the original and one for the
  universal that has been separated out if it is.
**************************************************************************/
static bool effect_handle_split_universal(struct effect *peffect,
                                          struct universal original,
                                          struct universal separated)
{
  if (universal_is_mentioned_by_requirements(&peffect->reqs, &original)) {
    /* Copy the old effect. */
    struct effect *peffect_copy = effect_copy(peffect);

    /* Replace the original requirement with the separated requirement. */
    return universal_replace_in_req_vec(&peffect_copy->reqs,
                                        &original, &separated);
  }

  return FALSE;
}

/**********************************************************************//**
  Adjust effects
**************************************************************************/
static bool effect_list_compat_cb(struct effect *peffect, void *data)
{
  struct rscompat_info *info = (struct rscompat_info *)data;

  if (info->ver_effects < 20) {
    /* Attack has been split in regular "Attack" and "Suicide Attack". */
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_ATTACK),
        universal_by_number(VUT_ACTION, ACTION_SUICIDE_ATTACK));

    /* "Nuke City" and "Nuke Units" has been split from "Explode Nuclear".
     * "Explode Nuclear" is now only about exploding at the current tile. */
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_NUKE),
        universal_by_number(VUT_ACTION, ACTION_NUKE_CITY));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_NUKE),
        universal_by_number(VUT_ACTION, ACTION_NUKE_UNITS));

    /* Production or building targeted actions have been split in one action
     * for each target. */
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_SPY_TARGETED_SABOTAGE_CITY),
        universal_by_number(VUT_ACTION, ACTION_SPY_SABOTAGE_CITY_PRODUCTION));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC),
        universal_by_number(VUT_ACTION, ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC));

    if (peffect->type == EFT_ILLEGAL_ACTION_MOVE_COST) {
      /* Boarding a transporter became action enabler controlled in
       * Freeciv 3.1. Old hard coded rules had no punishment for trying to
       * do this when it is illegal according to the rules. */
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, FALSE,
                                              "Transport Board"));
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, FALSE,
                                              "Transport Embark"));

      /* Disembarking became action enabler controlled in Freeciv 3.1. Old
       * hard coded rules had no punishment for trying to do those when it
       * is illegal according to the rules. */
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, FALSE,
                                              "Transport Disembark"));
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, FALSE,
                                              "Transport Disembark 2"));
    }
  }

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

  if (info->ver_effects < 20) {
    struct effect *peffect;

    /* Post successful action move fragment loss for "Bombard"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         MAX_MOVE_FRAGS, NULL);

    /* The reduction only applies to "Bombard". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Bombard"));

    /* Post successful action move fragment loss for "Heal Unit"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         MAX_MOVE_FRAGS, NULL);

    /* The reduction only applies to "Heal Unit". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Heal Unit"));

    /* Post successful action move fragment loss for "Expel Unit"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         SINGLE_MOVE, NULL);

    /* The reduction only applies to "Expel Unit". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Expel Unit"));

    /* Post successful action move fragment loss for "Capture Units"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         SINGLE_MOVE, NULL);

    /* The reduction only applies to "Capture Units". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Capture Units"));

    /* Post successful action move fragment loss for "Establish Embassy"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         1, NULL);

    /* The reduction only applies to "Establish Embassy". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Establish Embassy"));

    /* Post successful action move fragment loss for "Investigate City"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         1, NULL);

    /* The reduction only applies to "Investigate City". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Investigate City"));

    /* Post successful action move fragment loss for targets of "Expel Unit"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_TARGET_MOVE_COST,
                         MAX_MOVE_FRAGS, NULL);

    /* The reduction only applies to "Expel Unit". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Expel Unit"));

    /* Post successful action move fragment loss for targets of
     * "Paradrop Unit" has moved to the Action_Success_Actor_Move_Cost
     * effect. */
    unit_type_iterate(putype) {
      if (!utype_can_do_action(putype, ACTION_PARADROP)) {
        /* Not relevant */
        continue;
      }

      if (putype->rscompat_cache.paratroopers_mr_sub == 0) {
        /* Not relevant */
        continue;
      }

      /* Subtract the value via the Action_Success_Actor_Move_Cost effect */
      peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                           putype->rscompat_cache.paratroopers_mr_sub,
                           NULL);

      /* The reduction only applies to "Paradrop Unit". */
      effect_req_append(peffect,
                        req_from_str("Action", "Local", FALSE, TRUE, FALSE,
                                     "Paradrop Unit"));

      /* The reduction only applies to this unit type. */
      effect_req_append(peffect,
                        req_from_str("UnitType", "Local", FALSE, TRUE, FALSE,
                                     utype_rule_name(putype)));
    } unit_type_iterate_end;

    /* Fortifying rules have been unhardcoded to effects. */
    peffect = effect_new(EFT_FORTIFY_DEFENSE_BONUS, 50, NULL);

    /* Unit actually fortified. This does not need checks for unit class or
     * type flags for unit's ability to fortify as it would not be fortified
     * if it can't. */
    effect_req_append(peffect, req_from_str("Activity", "Local", FALSE, TRUE,
                                            FALSE, "Fortified"));

    /* Fortify bonus in cities */
    peffect = effect_new(EFT_FORTIFY_DEFENSE_BONUS, 50, NULL);

    /* City center */
    effect_req_append(peffect, req_from_str("CityTile", "Local", FALSE, TRUE,
                                            FALSE, "Center"));
    /* Not cumulative with regular fortified bonus */
    effect_req_append(peffect, req_from_str("Activity", "Local", FALSE, FALSE,
                                            FALSE, "Fortified"));
    /* Unit flags */
    effect_req_append(peffect, req_from_str("UnitClassFlag", "Local", FALSE, TRUE,
                                            FALSE, "CanFortify"));
    effect_req_append(peffect, req_from_str("UnitFlag", "Local", FALSE, FALSE,
                                            FALSE, "Cant_Fortify"));

    /* The probability that "Steal Maps" and "Steal Maps Escape" steals the
     * map of a tile has moved to the ruleset. */
    peffect = effect_new(EFT_MAPS_STOLEN_PCT, -50, NULL);

    /* The rule that "Recycle Unit"'s unit shield value is 50% has moved to
     * the ruleset. */
    peffect = effect_new(EFT_UNIT_SHIELD_VALUE_PCT, -50, NULL);
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            FALSE, "Recycle Unit"));

    /* The rule that "Upgrade Unit"'s current unit shield value is 50% when
     * calculating unit upgrade price has moved to the ruleset. */
    peffect = effect_new(EFT_UNIT_SHIELD_VALUE_PCT, -50, NULL);
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            FALSE, "Upgrade Unit"));
  }

  if (info->ver_game < 20) {
    /* New enablers */
    struct action_enabler *enabler;
    struct requirement e_req;

    enabler = action_enabler_new();
    enabler->action = ACTION_PILLAGE;
    e_req = req_from_str("UnitClassFlag", "Local", FALSE, TRUE, FALSE,
                         "CanPillage");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_CLEAN_FALLOUT;
    e_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL, FALSE, TRUE, FALSE,
                            UTYF_SETTLERS);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_CLEAN_POLLUTION;
    e_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL, FALSE, TRUE, FALSE,
                            UTYF_SETTLERS);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_FORTIFY;
    e_req = req_from_str("UnitClassFlag", "Local", FALSE, TRUE, TRUE,
                         "CanFortify");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    e_req = req_from_str("UnitFlag", "Local", FALSE, FALSE, TRUE,
                         "Cant_Fortify");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    e_req = req_from_str("TerrainFlag", "Local", FALSE, FALSE, TRUE,
                         "NoFortify");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_FORTIFY;
    e_req = req_from_str("UnitClassFlag", "Local", FALSE, TRUE, TRUE,
                         "CanFortify");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    e_req = req_from_str("UnitFlag", "Local", FALSE, FALSE, TRUE,
                         "Cant_Fortify");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    e_req = req_from_str("CityTile", "Local", FALSE, TRUE, TRUE,
                         "Center");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_ROAD;
    e_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL, FALSE, TRUE, FALSE,
                            UTYF_SETTLERS);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_CONVERT;
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_BASE;
    e_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL, FALSE, TRUE, FALSE,
                            UTYF_SETTLERS);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_TRANSPORT_ALIGHT;
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_TRANSPORT_BOARD;
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_TRANSPORT_EMBARK;
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_TRANSPORT_UNLOAD;
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_CONQUER_EXTRAS;
    e_req = req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                            FALSE, TRUE, TRUE, DS_WAR);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_CONQUER_EXTRAS;
    e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                            FALSE, FALSE, TRUE,
                            CITYT_EXTRAS_OWNED);
    requirement_vector_append(&enabler->target_reqs, e_req);
    action_enabler_add(enabler);

    /* Update action enablers. */
    rscompat_enablers_add_obligatory_hard_reqs();
    action_enablers_iterate(ae) {
      /* "Attack" is split in a unit consuming and a non unit consuming
       * version. */
      if (ae->action == ACTION_ATTACK) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);

        /* One allows regular attacks. */
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_str("UnitClassFlag", "Local",
                                               FALSE, FALSE, TRUE,
                                               "Missile"));

        /* The other allows suicide attacks. */
        enabler->action = ACTION_SUICIDE_ATTACK;
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_str("UnitClassFlag", "Local",
                                               FALSE, TRUE, TRUE,
                                               "Missile"));

        /* Add after the action was changed. */
        action_enabler_add(enabler);
      }

      /* "Explode Nuclear"'s adjacent tile attack is split to "Nuke City"
       * and "Nuke Units". */
      if (ae->action == ACTION_NUKE) {
        /* The old rule is represented with three action enablers:
         * 1) "Explode Nuclear" against the actors own tile.
         * 2) "Nuke City" against adjacent enemy cities.
         * 3) "Nuke Units" against adjacent enemy unit stacks. */

        struct action_enabler *city;
        struct action_enabler *units;

        /* Against city targets. */
        city = action_enabler_copy(ae);
        city->action = ACTION_NUKE_CITY;

        /* Against unit stack targets. */
        units = action_enabler_copy(ae);
        units->action = ACTION_NUKE_UNITS;

        /* "Explode Nuclear" required this to target an adjacent tile. */
        /* While this isn't a real move (because of enemy city/units) at
         * target tile it pretends to be one. */
        requirement_vector_append(&city->actor_reqs,
                                  req_from_values(VUT_MINMOVES,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, FALSE, 1));
        requirement_vector_append(&units->actor_reqs,
                                  req_from_values(VUT_MINMOVES,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, FALSE, 1));

        /* Be slightly stricter about the relationship to target unit stacks
         * than "Explode Nuclear" was before it would target an adjacent
         * tile. I think the intention was that you shouldn't nuke your
         * friends and allies. */
        requirement_vector_append(&city->actor_reqs,
                                  req_from_values(VUT_DIPLREL,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, FALSE,
                                                  DS_WAR));
        requirement_vector_append(&units->actor_reqs,
                                  req_from_values(VUT_DIPLREL,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, FALSE,
                                                  DS_WAR));

        /* Only display one nuke action at once. */
        requirement_vector_append(&units->target_reqs,
                                  req_from_values(VUT_CITYTILE,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, FALSE,
                                                  CITYT_CENTER));

        /* Add after the action was changed. */
        action_enabler_add(city);
        action_enabler_add(units);
      }

      /* "Targeted Sabotage City" is split in a production targeted and a
       * building targeted version. */
      if (ae->action == ACTION_SPY_TARGETED_SABOTAGE_CITY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);

        enabler->action = ACTION_SPY_SABOTAGE_CITY_PRODUCTION;

        /* Add after the action was changed. */
        action_enabler_add(enabler);
      }

      /* "Targeted Sabotage City Escape" is split in a production targeted
       * and a building targeted version. */
      if (ae->action == ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);

        enabler->action = ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC;

        /* Add after the action was changed. */
        action_enabler_add(enabler);
      }
    } action_enablers_iterate_end;

    /* The paratroopers_mr_req field has moved to the enabler for the
     * "Paradrop Unit" action. */
    {
      bool generic_in_use = FALSE;
      struct action_enabler_list *ae_custom = action_enabler_list_new();

      action_enabler_list_iterate(
            action_enablers_for_action(ACTION_PARADROP), ae) {
        unit_type_iterate(putype) {
          if (!requirement_fulfilled_by_unit_type(putype,
                                                  &(ae->actor_reqs))) {
            /* This action enabler isn't for this unit type at all. */
            continue;
          }

          requirement_vector_iterate(&ae->actor_reqs, preq) {
            if (preq->source.kind == VUT_MINMOVES) {
              if (!preq->present) {
                /* A max move fragments req has been found. Is it too
                 * large? */
                if (preq->source.value.minmoves
                    < putype->rscompat_cache.paratroopers_mr_req) {
                  /* Avoid self contradiciton */
                  continue;
                }
              }
            }
          } requirement_vector_iterate_end;

          if (putype->rscompat_cache.paratroopers_mr_req > 0) {
            /* This unit type needs a custom enabler */

            enabler = action_enabler_copy(ae);

            /* This enabler is specific to the unit type */
            e_req = req_from_values(VUT_UTYPE,
                                    REQ_RANGE_LOCAL,
                                    FALSE, TRUE, FALSE,
                                    utype_number(putype));
            requirement_vector_append(&enabler->actor_reqs, e_req);

            /* Add the minimum amout of move fragments */
            e_req = req_from_values(VUT_MINMOVES,
                                    REQ_RANGE_LOCAL,
                                    FALSE, TRUE, FALSE,
                                    putype->rscompat_cache
                                    .paratroopers_mr_req);
            requirement_vector_append(&enabler->actor_reqs, e_req);

            action_enabler_list_append(ae_custom, enabler);

            log_debug("paratroopers_mr_req upgrade: %s uses custom enabler",
                      utype_rule_name(putype));
          } else {
            /* The old one works just fine */

            generic_in_use = TRUE;

            log_debug("paratroopers_mr_req upgrade: %s uses generic enabler",
                      utype_rule_name(putype));
          }
        } unit_type_iterate_end;

        if (!generic_in_use) {
          /* The generic enabler isn't in use any more */
          action_enabler_remove(ae);
        }
      } action_enabler_list_iterate_end;

      action_enabler_list_iterate(ae_custom, ae) {
        /* Append the custom enablers. */
        action_enabler_add(ae);
      } action_enabler_list_iterate_end;

      action_enabler_list_destroy(ae_custom);
    }

    /* Enable all clause types */
    {
      int i;

      for (i = 0; i < CLAUSE_COUNT; i++) {
        struct clause_info *cinfo = clause_info_get(i);

        cinfo->enabled = TRUE;
      }
    }

    /* Used to live in unit_move() and new in 3.1 so they should be non
     * controversial enough. */
    action_iterate(act_id) {
      if (action_id_has_result_safe(act_id, ACTRES_TRANSPORT_DISEMBARK)
          || action_id_has_result_safe(act_id, ACTRES_CONQUER_EXTRAS)) {
        BV_SET(game.info.move_is_blocked_by, act_id);
      }
    } action_iterate_end;
  }

  /* The ruleset may need adjustments it didn't need before compatibility
   * post processing.
   *
   * If this isn't done a user of ruleset compatibility that ends up using
   * the rules risks bad rules. A user that saves the ruleset rather than
   * using it risks an unexpected change on the next load and save. */
  autoadjust_ruleset_data();
}
