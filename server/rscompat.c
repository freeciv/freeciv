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
#include "settings.h"

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
                                const struct rscompat_info *info)
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
    ruleset_error(LOG_ERROR, "Inconsistent format versions");

    return FALSE;
  }

  return TRUE;
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
  if (info->version < RSFORMAT_3_1) {
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
      { N_("OneAttack"),    NULL },
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
      { N_("HutNothing"), N_("Does nothing to huts.") },
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

  if (info->version < RSFORMAT_3_1) {
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
  Copy the ui_name of an existing action to the ui_name of its copy by
  inserting it at the first %s in name_modification string.
**************************************************************************/
static void split_action_name_update(struct action *original,
                                     struct action *copy,
                                     const char *name_modification)
{
  fc_snprintf(copy->ui_name, MAX_LEN_NAME,
              name_modification, original->ui_name);
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

  if (info->version < RSFORMAT_3_1) {
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

    /* "Paradrop Unit" has been split by side effects. */
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_PARADROP),
        universal_by_number(VUT_ACTION, ACTION_PARADROP_CONQUER));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_PARADROP),
        universal_by_number(VUT_ACTION, ACTION_PARADROP_ENTER));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_PARADROP),
        universal_by_number(VUT_ACTION, ACTION_PARADROP_ENTER_CONQUER));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_PARADROP),
        universal_by_number(VUT_ACTION, ACTION_PARADROP_FRIGHTEN));
    effect_handle_split_universal(peffect,
        universal_by_number(VUT_ACTION, ACTION_PARADROP),
        universal_by_number(VUT_ACTION, ACTION_PARADROP_FRIGHTEN_CONQUER));
  }

  /* Go to the next effect. */
  return TRUE;
}

/**********************************************************************//**
  Turn paratroopers_mr_sub into the Action_Success_Actor_Move_Cost effect
**************************************************************************/
static void paratroopers_mr_sub_to_effect(struct unit_type *putype,
                                          struct action *paction)
{
  struct effect *peffect;

  /* Subtract the value via the Action_Success_Actor_Move_Cost effect */
  peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                       putype->rscompat_cache.paratroopers_mr_sub,
                       NULL);

  /* The reduction only applies to this action. */
  effect_req_append(peffect,
                    req_from_str("Action", "Local", FALSE, TRUE, FALSE,
                                 action_rule_name(paction)));

  /* The reduction only applies to this unit type. */
  effect_req_append(peffect,
                    req_from_str("UnitType", "Local", FALSE, TRUE, FALSE,
                                 utype_rule_name(putype)));
}

/**********************************************************************//**
  Helper for effect_to_enabler(). Is this requirement meant for target
  rather than actor?
**************************************************************************/
static bool ete_is_target_req(struct requirement *preq)
{
  switch (preq->source.kind) {
  /* World range only - listed in actor reqs */
  case VUT_MINYEAR:
  case VUT_TOPO:
  case VUT_MINCALFRAG:
  case VUT_SERVERSETTING:
    return FALSE;

  /* Actor ones */
  case VUT_ADVANCE:
  case VUT_GOVERNMENT:
  case VUT_NATION:
  case VUT_UTYPE:
  case VUT_UTFLAG:
  case VUT_UCLASS:
  case VUT_UCFLAG:
  case VUT_AI_LEVEL:
  case VUT_NATIONALITY:
  case VUT_TECHFLAG:
  case VUT_ACHIEVEMENT:
  case VUT_DIPLREL:
  case VUT_STYLE:
  case VUT_MINCULTURE:
  case VUT_UNITSTATE:
  case VUT_MINMOVES:
  case VUT_MINVETERAN:
  case VUT_MINHP:
  case VUT_NATIONGROUP:
  case VUT_ACTION:
  case VUT_MINTECHS:
  case VUT_ACTIVITY:
  case VUT_DIPLREL_UNITANY:   /* ? */
  case VUT_DIPLREL_UNITANY_O: /* ? */
    return FALSE;

  /* Target ones */
  case VUT_IMPROVEMENT:
  case VUT_TERRAIN:
  case VUT_MINSIZE:
  case VUT_TERRAINCLASS:
  case VUT_TERRAINALTER:
  case VUT_CITYTILE:
  case VUT_TERRFLAG:
  case VUT_ROADFLAG:
  case VUT_EXTRA:
  case VUT_MAXTILEUNITS:
  case VUT_EXTRAFLAG:
  case VUT_CITYSTATUS:
  case VUT_MINFOREIGNPCT:
  case VUT_DIPLREL_TILE:      /* ? */
  case VUT_DIPLREL_TILE_O:    /* ? */
    return TRUE;

  /* Ones that are actor reqs at some range, target reqs at another range */
  case VUT_AGE:
    if (preq->range == REQ_RANGE_CITY) {
      return TRUE;
    }
    return FALSE;

  /* Ones that make equally little sense both as actor and target req */
  case VUT_NONE:
  case VUT_OTYPE:
  case VUT_SPECIALIST:
  case VUT_GOOD:
  case VUT_IMPR_GENUS:
    return FALSE;
  case VUT_COUNT:
    fc_assert(preq->source.kind != VUT_COUNT);
    return FALSE;
  }

  fc_assert(FALSE);

  return FALSE;
}

/**********************************************************************//**
  Turn old effect to an action enabler.
**************************************************************************/
static void effect_to_enabler(action_id action, struct section_file *file,
                              const char *sec_name, struct rscompat_info *compat,
                              const char *type)
{
  int value = secfile_lookup_int_default(file, 1, "%s.value", sec_name);
  char buf[1024];

  if (value > 0) {
    /* It was an enabling effect. Add enabler */
    struct action_enabler *enabler;
    struct requirement_vector *reqs;
    struct requirement settler_req;

    enabler = action_enabler_new();
    enabler->action = action;

    reqs = lookup_req_list(file, compat, sec_name, "reqs", "old effect");

    requirement_vector_iterate(reqs, preq) {
      if (ete_is_target_req(preq)) {
        requirement_vector_append(&enabler->target_reqs, *preq);
      } else {
        requirement_vector_append(&enabler->actor_reqs, *preq);
      }
    } requirement_vector_iterate_end;

    settler_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL, FALSE, TRUE, FALSE,
                                  UTYF_SETTLERS);
    requirement_vector_append(&enabler->actor_reqs, settler_req);

    /* Add the enabler to the ruleset. */
    action_enabler_add(enabler);

    if (compat->log_cb != NULL) {
      fc_snprintf(buf, sizeof(buf),
                  "Converted effect %s in %s to an action enabler. Make sure requirements "
                  "are correctly divided to actor and target requirements.",
                  type, sec_name);
      compat->log_cb(buf);
    }
  } else if (value < 0) {
    if (compat->log_cb != NULL) {
      fc_snprintf(buf, sizeof(buf),
                  "%s effect with negative value in %s can't be automatically converted "
                  "to an action enabler. Do that manually.", type, sec_name);
      compat->log_cb(buf);
    }
  }
}

/**********************************************************************//**
  Check if effect name refers to one of the removed effects, and handle it
  if it does. Returns TRUE iff name was a valid old name.
**************************************************************************/
bool rscompat_old_effect_3_1(const char *type, struct section_file *file,
                             const char *sec_name, struct rscompat_info *compat)
{
  if (compat->version < RSFORMAT_3_1) {
    if (!fc_strcasecmp(type, "Transform_Possible")) {
      effect_to_enabler(ACTION_TRANSFORM_TERRAIN, file, sec_name, compat, type);
      return TRUE;
    }
    if (!fc_strcasecmp(type, "Irrig_TF_Possible")) {
      effect_to_enabler(ACTION_CULTIVATE, file, sec_name, compat, type);
      return TRUE;
    }
    if (!fc_strcasecmp(type, "Mining_TF_Possible")) {
      effect_to_enabler(ACTION_PLANT, file, sec_name, compat, type);
      return TRUE;
    }
    if (!fc_strcasecmp(type, "Mining_Possible")) {
      effect_to_enabler(ACTION_MINE, file, sec_name, compat, type);
      return TRUE;
    }
    if (!fc_strcasecmp(type, "Irrig_Possible")) {
      effect_to_enabler(ACTION_IRRIGATE, file, sec_name, compat, type);
      return TRUE;
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Do compatibility things after regular ruleset loading, but before
  sanity checking.
**************************************************************************/
void rscompat_adjust_pre_sanity(struct rscompat_info *info)
{
  if (!info->compat_mode) {
    /* There isn't anything here that should be done outside of compat
     * mode. */
    return;
  }

  if (info->version < RSFORMAT_3_1) {
    improvement_iterate(pimprove) {
      if (pimprove->upkeep != 0 && is_wonder(pimprove)) {
        pimprove->upkeep = 0;
      }
    } improvement_iterate_end;
  }
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

  if (info->version < RSFORMAT_3_1) {
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

    /* Post successful action move fragment loss for "OneAttack"
     * has moved to the ruleset. */
    peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                         MAX_MOVE_FRAGS, NULL);
    /* The reduction only applies to "Attack". */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Attack"));
    /* The reduction only applies to "OneAttack". */
    effect_req_append(peffect, req_from_str("UnitFlag", "Local", FALSE, TRUE,
                                            TRUE, "OneAttack"));

    action_by_result_iterate(paction, act_id, ACTRES_ATTACK) {
      if (paction->actor_consuming_always) {
        /* Not relevant. */
        continue;
      }

      peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                           SINGLE_MOVE, NULL);
      /* The reduction only applies to this action. */
      effect_req_append(peffect, req_from_str("Action", "Local",
                                              FALSE, TRUE, TRUE,
                                              action_rule_name(paction)));
      /* The reduction doesn't apply to "OneAttack". */
      effect_req_append(peffect, req_from_str("UnitFlag", "Local",
                                              FALSE, FALSE, TRUE,
                                              "OneAttack"));
    } action_by_result_iterate_end;

    /* Post successful action move fragment loss for spy post action escape
     * has moved to the ruleset. */
    action_iterate(act_id) {
      struct action *paction = action_by_number(act_id);

      if (paction->actor.is_unit.moves_actor != MAK_ESCAPE
          || paction->actor_consuming_always) {
        /* Not relevant. */
        continue;
      }

      peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                           MAX_MOVE_FRAGS, NULL);
      /* The reduction only applies to this action. */
      effect_req_append(peffect, req_from_str("Action", "Local",
                                              FALSE, TRUE, TRUE,
                                              action_rule_name(paction)));
    } action_iterate_end;

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

      action_by_result_iterate(paction, act_id, ACTRES_PARADROP) {
        paratroopers_mr_sub_to_effect(putype, paction);
      } action_by_result_iterate_end;
      action_by_result_iterate(paction, act_id, ACTRES_PARADROP_CONQUER) {
        paratroopers_mr_sub_to_effect(putype, paction);
      } action_by_result_iterate_end;
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

    /* The rule that "Heal Unit" heals up to 25% has moved to the
     * ruleset. */
    peffect = effect_new(EFT_HEAL_UNIT_PCT, -75, NULL);
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            FALSE, "Heal Unit"));

    /* The rule that unit in a city has at least 1/3 of its HP in the
     * beginning of the new turn has moved to the ruleset. */
    peffect = effect_new(EFT_MIN_HP_PCT, 33, NULL);
    effect_req_append(peffect, req_from_str("CityTile", "Tile", FALSE, TRUE,
                                            FALSE, "Center"));

    /* The base unit regeneration rule is in ruleset now. */
    peffect = effect_new(EFT_HP_REGEN_2, 10, NULL);
    /* Does not apply to any unit class with HP loss */
    unit_class_iterate(pclass) {
      if (pclass->hp_loss_pct) {
        effect_req_append(peffect, req_from_str("UnitClass", "Local", FALSE,
                                                FALSE, FALSE,
                                                uclass_rule_name(pclass)));
      }
    } unit_class_iterate_end;

    /* The rule that fortified unit regenerates extra 10% has been moved
     * to the ruleset. */
    peffect = effect_new(EFT_HP_REGEN_2, 10, NULL);
    effect_req_append(peffect, req_from_str("Activity", "Local", FALSE, TRUE,
                                            FALSE, "Fortified"));

    /* Help ruleset authors specify the new arguments to unit_move() and
     * unit_teleport() by introducing boolean effects */
    log_normal(_("Preparing user effects to help you port edit.unit_move()"
                 " and edit.unit_teleport() Lua calls."));
    log_normal(_("It is safe to delete the effects if you don't use"
                 " unit_move() or unit_teleport() or if you want to fill"
                 " in the new parameters with your own values."));
    log_normal(_("Use effects.unit_bonus() and effects.unit_vs_tile_bonus()"
                 " to get the value of the user effects"
                 " if you wish to use them."));

    /* EFT_USER_EFFECT_1 is if a unit may occupy a city */
    log_normal(_("User_Effect_1 is now if a unit can conquer a city."));
    peffect = effect_new(EFT_USER_EFFECT_1, 1, NULL);
    effect_req_append(peffect,
                      req_from_str("UnitClassFlag", "Local",
                                   FALSE, TRUE, FALSE, "CanOccupyCity"));
    effect_req_append(peffect,
                      req_from_str("UnitFlag", "Local",
                                   FALSE, FALSE, FALSE, "NonMil"));
    effect_req_append(peffect,
                      req_from_str("DiplRel", "Local",
                                   FALSE, TRUE, FALSE, "War"));
    nations_iterate(pnation) {
      if (nation_barbarian_type(pnation) == ANIMAL_BARBARIAN) {
        effect_req_append(peffect,
                          req_from_str("Nation", "Local",
                                       FALSE, FALSE, FALSE,
                                       nation_rule_name(pnation)));
      }
    } nations_iterate_end;

    /* EFT_USER_EFFECT_2 is if a unit may occupy an extra */
    log_normal(_("User_Effect_2 is now if a unit can conquer an extra."));
    peffect = effect_new(EFT_USER_EFFECT_2, 1, NULL);
    effect_req_append(peffect,
                      req_from_str("DiplRel", "Local",
                                   FALSE, TRUE, FALSE, "War"));

    peffect = effect_new(EFT_USER_EFFECT_2, 1, NULL);
    effect_req_append(peffect,
                      req_from_str("CityTile", "Local",
                                   FALSE, FALSE, FALSE, "Extras Owned"));

    /* EFT_USER_EFFECT_3 is if a unit may enter a hut */
    log_normal(_("User_Effect_3 is now if a unit can enter a hut."));
    peffect = effect_new(EFT_USER_EFFECT_3, 1, NULL);
    effect_req_append(peffect,
                      req_from_str("UnitClassFlag", "Local",
                                   FALSE, FALSE, FALSE, "HutFrighten"));
    effect_req_append(peffect,
                      req_from_str("UnitClassFlag", "Local",
                                   FALSE, FALSE, FALSE, "HutNothing"));

    /* EFT_USER_EFFECT_4 is if a unit may frighten a hut */
    log_normal(_("User_Effect_4 is now if a unit can frighten a hut."));
    peffect = effect_new(EFT_USER_EFFECT_4, 1, NULL);
    effect_req_append(peffect,
                      req_from_str("UnitClassFlag", "Local",
                                   FALSE, TRUE, FALSE, "HutFrighten"));
    effect_req_append(peffect,
                      req_from_str("UnitClassFlag", "Local",
                                   FALSE, FALSE, FALSE, "HutNothing"));
  }

  if (info->version < RSFORMAT_3_1) {
    /* New enablers */
    struct action_enabler *enabler;
    struct requirement e_req;
    struct action *paction;

    paction = action_by_number(ACTION_NUKE_CITY);
    paction->target_kind = ATK_CITY;
    paction->actor_consuming_always = TRUE;
    paction->min_distance = 1;
    paction->max_distance = RS_DEFAULT_ACTION_MAX_RANGE;

    paction = action_by_number(ACTION_NUKE);
    paction->actor_consuming_always = TRUE;
    paction->min_distance = 0;

    paction = action_by_number(ACTION_NUKE_UNITS);
    paction->actor_consuming_always = TRUE;
    paction->min_distance = 1;

    paction = action_by_number(ACTION_FOUND_CITY);
    paction->actor_consuming_always = TRUE;

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

    /* Assume the player knows that the unit is able to move */
    paction = action_by_number(ACTION_UNIT_MOVE);
    paction->quiet = TRUE;

    enabler = action_enabler_new();
    enabler->action = ACTION_UNIT_MOVE;
    e_req = req_from_values(VUT_MINMOVES, REQ_RANGE_LOCAL,
                            FALSE, TRUE, FALSE, 1);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    e_req = req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                            FALSE, FALSE, FALSE,
                            USP_TRANSPORTED);
    requirement_vector_append(&enabler->actor_reqs, e_req);
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
          || action_id_has_result_safe(act_id, ACTRES_CONQUER_EXTRAS)
          || action_id_has_result_safe(act_id, ACTRES_HUT_ENTER)
          || action_id_has_result_safe(act_id, ACTRES_HUT_FRIGHTEN)) {
        BV_SET(action_by_number(ACTION_UNIT_MOVE)->blocked_by, act_id);
      }
    } action_iterate_end;

    {
      struct action_auto_perf *auto_perf;
      int pos;

      /* The forced post successful action move action list has moved to the
       * ruleset. */

      /* "Bribe Unit" */
      auto_perf = action_auto_perf_slot_number(ACTION_AUTO_POST_BRIBE);
      auto_perf->alternatives[0] = ACTION_TRANSPORT_EMBARK;
      auto_perf->alternatives[1] = ACTION_TRANSPORT_EMBARK2;
      auto_perf->alternatives[2] = ACTION_TRANSPORT_EMBARK3;
      auto_perf->alternatives[3] = ACTION_TRANSPORT_DISEMBARK1;
      auto_perf->alternatives[4] = ACTION_TRANSPORT_DISEMBARK2;
      auto_perf->alternatives[5] = ACTION_TRANSPORT_DISEMBARK3;
      auto_perf->alternatives[6] = ACTION_TRANSPORT_DISEMBARK4;
      auto_perf->alternatives[7] = ACTION_CONQUER_EXTRAS;
      auto_perf->alternatives[8] = ACTION_CONQUER_EXTRAS2;
      auto_perf->alternatives[9] = ACTION_CONQUER_EXTRAS3;
      auto_perf->alternatives[10] = ACTION_CONQUER_EXTRAS4;
      auto_perf->alternatives[11] = ACTION_HUT_ENTER;
      auto_perf->alternatives[12] = ACTION_HUT_ENTER2;
      auto_perf->alternatives[13] = ACTION_HUT_ENTER3;
      auto_perf->alternatives[14] = ACTION_HUT_ENTER4;
      auto_perf->alternatives[15] = ACTION_HUT_FRIGHTEN;
      auto_perf->alternatives[16] = ACTION_HUT_FRIGHTEN2;
      auto_perf->alternatives[17] = ACTION_HUT_FRIGHTEN3;
      auto_perf->alternatives[18] = ACTION_HUT_FRIGHTEN4;
      auto_perf->alternatives[19] = ACTION_UNIT_MOVE;
      auto_perf->alternatives[20] = ACTION_UNIT_MOVE2;
      auto_perf->alternatives[21] = ACTION_UNIT_MOVE3;
      action_list_end(auto_perf->alternatives, 22);

      /* "Attack" */
      auto_perf = action_auto_perf_slot_number(ACTION_AUTO_POST_ATTACK);
      auto_perf->alternatives[0] = ACTION_CONQUER_CITY;
      auto_perf->alternatives[1] = ACTION_CONQUER_CITY2;
      auto_perf->alternatives[2] = ACTION_CONQUER_CITY3;
      auto_perf->alternatives[3] = ACTION_CONQUER_CITY4;
      auto_perf->alternatives[4] = ACTION_TRANSPORT_DISEMBARK1;
      auto_perf->alternatives[5] = ACTION_TRANSPORT_DISEMBARK2;
      auto_perf->alternatives[6] = ACTION_TRANSPORT_DISEMBARK3;
      auto_perf->alternatives[7] = ACTION_TRANSPORT_DISEMBARK4;
      auto_perf->alternatives[8] = ACTION_CONQUER_EXTRAS;
      auto_perf->alternatives[9] = ACTION_CONQUER_EXTRAS2;
      auto_perf->alternatives[10] = ACTION_CONQUER_EXTRAS3;
      auto_perf->alternatives[11] = ACTION_CONQUER_EXTRAS4;
      auto_perf->alternatives[12] = ACTION_HUT_ENTER;
      auto_perf->alternatives[13] = ACTION_HUT_ENTER2;
      auto_perf->alternatives[14] = ACTION_HUT_ENTER3;
      auto_perf->alternatives[15] = ACTION_HUT_ENTER4;
      auto_perf->alternatives[16] = ACTION_HUT_FRIGHTEN;
      auto_perf->alternatives[17] = ACTION_HUT_FRIGHTEN2;
      auto_perf->alternatives[18] = ACTION_HUT_FRIGHTEN3;
      auto_perf->alternatives[19] = ACTION_HUT_FRIGHTEN4;
      auto_perf->alternatives[20] = ACTION_UNIT_MOVE;
      auto_perf->alternatives[21] = ACTION_UNIT_MOVE2;
      auto_perf->alternatives[22] = ACTION_UNIT_MOVE3;
      action_list_end(auto_perf->alternatives, 23);


      /* The city that made the unit's current tile native is gone.
       * Evaluated against an adjacent tile. */
      auto_perf = action_auto_perf_slot_number(ACTION_AUTO_ESCAPE_CITY);
      auto_perf->alternatives[0] = ACTION_TRANSPORT_EMBARK;
      auto_perf->alternatives[1] = ACTION_TRANSPORT_EMBARK2;
      auto_perf->alternatives[2] = ACTION_TRANSPORT_EMBARK3;
      auto_perf->alternatives[3] = ACTION_HUT_ENTER;
      auto_perf->alternatives[4] = ACTION_HUT_ENTER2;
      auto_perf->alternatives[5] = ACTION_HUT_ENTER3;
      auto_perf->alternatives[6] = ACTION_HUT_ENTER4;
      auto_perf->alternatives[7] = ACTION_HUT_FRIGHTEN;
      auto_perf->alternatives[8] = ACTION_HUT_FRIGHTEN2;
      auto_perf->alternatives[9] = ACTION_HUT_FRIGHTEN3;
      auto_perf->alternatives[10] = ACTION_HUT_FRIGHTEN4;
      auto_perf->alternatives[11] = ACTION_UNIT_MOVE;
      auto_perf->alternatives[12] = ACTION_UNIT_MOVE2;
      auto_perf->alternatives[13] = ACTION_UNIT_MOVE3;
      action_list_end(auto_perf->alternatives, 14);


      /* The unit's stack has been defeated and is scheduled for execution
       * but the unit has the CanEscape unit type flag.
       * Evaluated against an adjacent tile. */
      pos = 0;
      auto_perf = action_auto_perf_slot_number(ACTION_AUTO_ESCAPE_STACK);
      action_list_add_all_by_result(auto_perf->alternatives, &pos,
                                    ACTRES_TRANSPORT_EMBARK);
      action_list_add_all_by_result(auto_perf->alternatives, &pos,
                                    ACTRES_CONQUER_EXTRAS);
      action_list_add_all_by_result(auto_perf->alternatives, &pos,
                                    ACTRES_HUT_ENTER);
      action_list_add_all_by_result(auto_perf->alternatives, &pos,
                                    ACTRES_HUT_FRIGHTEN);
      action_list_add_all_by_result(auto_perf->alternatives, &pos,
                                    ACTRES_UNIT_MOVE);
      action_list_end(auto_perf->alternatives, pos);
    }

    /* diplchance setting control over initial dice roll odds has moved to
     * the ruleset. */
    action_iterate(act_id) {
      struct action *act = action_by_number(act_id);

      if (action_has_result_safe(act, ACTRES_SPY_SPREAD_PLAGUE)
          || action_has_result_safe(act, ACTRES_SPY_STEAL_TECH)
          || action_has_result_safe(act, ACTRES_SPY_TARGETED_STEAL_TECH)
          || action_has_result_safe(act, ACTRES_SPY_INCITE_CITY)
          || action_has_result_safe(act, ACTRES_SPY_SABOTAGE_CITY)
          || action_has_result_safe(act, ACTRES_SPY_TARGETED_SABOTAGE_CITY)
          || action_has_result_safe(act,
                                    ACTRES_SPY_SABOTAGE_CITY_PRODUCTION)
          || action_has_result_safe(act, ACTRES_SPY_STEAL_GOLD)
          || action_has_result_safe(act, ACTRES_STEAL_MAPS)
          || action_has_result_safe(act, ACTRES_SPY_NUKE)) {
        BV_SET(game.info.diplchance_initial_odds, act->id);
      }
    } action_iterate_end;


    /* "Paradrop Unit" has been split by side effects. */
    split_action_name_update(action_by_number(ACTION_PARADROP),
                             action_by_number(ACTION_PARADROP_CONQUER),
                             "Contested %s");
    split_action_name_update(action_by_number(ACTION_PARADROP),
                             action_by_number(ACTION_PARADROP_ENTER),
                             "Enter Hut %s");
    split_action_name_update(action_by_number(ACTION_PARADROP),
                             action_by_number(
                               ACTION_PARADROP_ENTER_CONQUER),
                             "Enter Hut Contested %s");
    split_action_name_update(action_by_number(ACTION_PARADROP),
                             action_by_number(ACTION_PARADROP_FRIGHTEN),
                             "Frighten Hut %s");
    split_action_name_update(action_by_number(ACTION_PARADROP),
                             action_by_number(
                               ACTION_PARADROP_FRIGHTEN_CONQUER),
                             "Frighten Hut Contested %s");

    action_enabler_list_iterate(
          action_enablers_for_action(ACTION_PARADROP), ae) {
      struct action_enabler *edit;

      action_by_result_iterate(para_action, para_id,
                               ACTRES_PARADROP_CONQUER) {
        /* Conquer City and/or owned Extra during war if one is there */
        edit = action_enabler_copy(ae);
        edit->action = para_action->id;
        e_req = req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                FALSE, TRUE, TRUE, DS_WAR);
        requirement_vector_append(&edit->actor_reqs, e_req);
        e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                FALSE, TRUE, TRUE, CITYT_CLAIMED);
        if (!is_req_in_vec(&e_req, &ae->target_reqs)) {
          requirement_vector_append(&edit->target_reqs, e_req);
        }
        e_req = req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                                FALSE, TRUE, TRUE,
                                UCF_CAN_OCCUPY_CITY);
        requirement_vector_append(&edit->actor_reqs, e_req);
        e_req = req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                FALSE, FALSE, TRUE,
                                UTYF_CIVILIAN);
        requirement_vector_append(&edit->actor_reqs, e_req);
        action_enabler_add(edit);

        /* Conquer owned Extra during war if there */
        edit = action_enabler_copy(ae);
        edit->action = para_action->id;
        e_req = req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                FALSE, TRUE, TRUE, DS_WAR);
        requirement_vector_append(&edit->actor_reqs, e_req);
        e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                FALSE, TRUE, TRUE, CITYT_CLAIMED);
        if (!is_req_in_vec(&e_req, &ae->target_reqs)) {
          requirement_vector_append(&edit->target_reqs, e_req);
        }
        e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                FALSE, FALSE, TRUE, CITYT_CENTER);
        requirement_vector_append(&edit->target_reqs, e_req);
        action_enabler_add(edit);

        /* Unowned Extra conquest */
        extra_type_by_cause_iterate(EC_BASE, pextra) {
          if (!territory_claiming_base(extra_base_get(pextra))) {
            /* Only territory claiming bases can be conquered in 3.0 */
            continue;
          }

          edit = action_enabler_copy(ae);
          edit->action = para_action->id;
          e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                  FALSE, FALSE, TRUE, CITYT_CLAIMED);
          if (!is_req_in_vec(&e_req, &ae->target_reqs)) {
            requirement_vector_append(&edit->target_reqs, e_req);
          }
          e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                  FALSE, FALSE, TRUE, CITYT_CENTER);
          requirement_vector_append(&edit->target_reqs, e_req);
          e_req = req_from_values(VUT_EXTRA, REQ_RANGE_LOCAL,
                                  FALSE, TRUE, TRUE, pextra->id);
          requirement_vector_append(&edit->target_reqs, e_req);
          action_enabler_add(edit);
        } extra_type_by_cause_iterate_end;
      } action_by_result_iterate_end;

      action_by_result_iterate(para_action, para_id, ACTRES_PARADROP) {
        if (para_action->id == ACTION_PARADROP) {
          /* Use when not at war and against unclaimed tiles. */
          e_req = req_from_values(VUT_CITYTILE, REQ_RANGE_LOCAL,
                                  FALSE, FALSE, TRUE, CITYT_CLAIMED);
          if (is_req_in_vec(&e_req, &ae->target_reqs)) {
            /* Use Conquer version for unowned Extras */
            extra_type_by_cause_iterate(EC_BASE, pextra) {
              if (!territory_claiming_base(extra_base_get(pextra))) {
                /* Only territory claiming bases can be conquered in 3.0 */
                continue;
              }

              e_req = req_from_values(VUT_EXTRA, REQ_RANGE_LOCAL,
                                      FALSE, FALSE, TRUE, pextra->id);
              requirement_vector_append(&ae->target_reqs, e_req);
            } extra_type_by_cause_iterate_end;
          } else {
            /* Use Conquer version during war. */
            e_req = req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                    FALSE, FALSE, TRUE, DS_WAR);
            requirement_vector_append(&ae->actor_reqs, e_req);
          }
        } else {
          /* !War req requirement added before the copy. */
          edit = action_enabler_copy(ae);
          edit->action = para_action->id;
          action_enabler_add(edit);
        }
      } action_by_result_iterate_end;
    } action_enabler_list_iterate_end;

    /* The non hut popping version is only legal when hut_behavior is
     * "Nothing". */
    action_enablers_iterate(ae) {
      if (ae->action != ACTION_PARADROP
          && ae->action != ACTION_PARADROP_CONQUER) {
        /* Not relevant. */
        continue;
      }

      e_req = req_from_str("UnitClassFlag", "local",
                           FALSE, TRUE, FALSE, "HutNothing");
      requirement_vector_append(&ae->actor_reqs, e_req);
    } action_enablers_iterate_end;
  }

  if (info->version < RSFORMAT_3_1) {
    enum unit_class_flag_id nothing
        = unit_class_flag_id_by_name("HutNothing", fc_strcasecmp);

    unit_class_iterate(uc) {
      if (uc->rscompat_cache_from_3_0.hut_behavior == HUT_NOTHING) {
        BV_SET(uc->flags, nothing);
      } else if (uc->rscompat_cache_from_3_0.hut_behavior == HUT_FRIGHTEN) {
        BV_SET(uc->flags, UCF_HUT_FRIGHTEN);
      }
    } unit_class_iterate_end;
  }

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
  Replace deprecated auto_attack configuration.
**************************************************************************/
bool rscompat_auto_attack_3_1(struct rscompat_info *compat,
                              struct action_auto_perf *auto_perf,
                              size_t psize,
                              enum unit_type_flag_id *protecor_flag)
{
  int i;

  if (compat->version < RSFORMAT_3_1) {
    /* Auto attack happens during war. */
    requirement_vector_append(&auto_perf->reqs,
                              req_from_values(VUT_DIPLREL,
                                              REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, DS_WAR));

    /* Needs a movement point to auto attack. */
    requirement_vector_append(&auto_perf->reqs,
                              req_from_values(VUT_MINMOVES,
                                              REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, 1));

    for (i = 0; i < psize; i++) {
      /* Add each protecor_flag as a !present requirement. */
      requirement_vector_append(&auto_perf->reqs,
                                req_from_values(VUT_UTFLAG,
                                                REQ_RANGE_LOCAL,
                                                FALSE, FALSE, TRUE,
                                                protecor_flag[i]));
    }

    auto_perf->alternatives[0] = ACTION_CAPTURE_UNITS;
    auto_perf->alternatives[1] = ACTION_BOMBARD;
    auto_perf->alternatives[2] = ACTION_ATTACK;
    auto_perf->alternatives[3] = ACTION_SUICIDE_ATTACK;
  }

  return TRUE;
}

/**********************************************************************//**
  Slow invasion split existing action enablers for action_orig.
**************************************************************************/
static bool slow_invasion_enablers(action_id action_orig,
                                   action_id action_copy)
{
  struct action_enabler_list *to_upgrade;

  struct action_enabler *enabler;
  struct requirement e_req;

  /* Upgrade exisiting action enablers */
  to_upgrade = action_enabler_list_copy(
        action_enablers_for_action(action_orig));

  action_enabler_list_iterate(to_upgrade, ae) {
    /* City center counts as native. */
    enabler = action_enabler_copy(ae);
    e_req = req_from_values(VUT_CITYTILE,
                            REQ_RANGE_LOCAL,
                            FALSE, TRUE, TRUE,
                            CITYT_CENTER);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);
  } action_enabler_list_iterate_end;

  action_enabler_list_iterate(to_upgrade, ae) {
    /* No TerrainSpeed sees everything as native. */
    enabler = action_enabler_copy(ae);
    e_req = req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                            FALSE, FALSE, TRUE,
                            UCF_TERRAIN_SPEED);
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);
  } action_enabler_list_iterate_end;

  action_enabler_list_iterate(to_upgrade, ae) {
    /* "BeachLander" sees everything as native. */
    enabler = action_enabler_copy(ae);
    e_req = req_from_str("UnitFlag", "Local",
                         FALSE, TRUE, TRUE,
                         "BeachLander");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);
  } action_enabler_list_iterate_end;

  action_enabler_list_iterate(to_upgrade, ae) {
    /* Use the action_copy for acting from non native. */
    enabler = action_enabler_copy(ae);
    enabler->action = action_copy;

    /* Native terrain is native. */
    e_req = req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                            FALSE, FALSE, TRUE, USP_NATIVE_TILE);
    requirement_vector_append(&enabler->actor_reqs, e_req);

    /* City is native. */
    e_req = req_from_values(VUT_CITYTILE,
                            REQ_RANGE_LOCAL,
                            FALSE, FALSE, TRUE,
                            CITYT_CENTER);
    requirement_vector_append(&enabler->actor_reqs, e_req);

    /* No TerrainSpeed sees everything as native. */
    e_req = req_from_values(VUT_UCFLAG, REQ_RANGE_LOCAL,
                            FALSE, TRUE, TRUE,
                            UCF_TERRAIN_SPEED);
    requirement_vector_append(&enabler->actor_reqs, e_req);

    /* "BeachLander" sees everything as native. */
    e_req = req_from_str("UnitFlag", "Local",
                         FALSE, FALSE, TRUE,
                         "BeachLander");
    requirement_vector_append(&enabler->actor_reqs, e_req);

    action_enabler_add(enabler);
  } action_enabler_list_iterate_end;

  action_enabler_list_iterate(to_upgrade, ae) {
    /* Use for acting from native terrain so acting from
     * non native terain is handled by action_copy. */
    e_req = req_from_values(VUT_UNITSTATE, REQ_RANGE_LOCAL,
                            FALSE, TRUE, TRUE, USP_NATIVE_TILE);
    requirement_vector_append(&ae->actor_reqs, e_req);
  } action_enabler_list_iterate_end;

  action_enabler_list_destroy(to_upgrade);

  return TRUE;
}

/**********************************************************************//**
  Slow invasion movement punishment.
**************************************************************************/
static bool slow_invasion_effects(const char *action_rule_name)
{
  struct effect *peffect;

  /* Take movement for action from non native terrain */
  peffect = effect_new(EFT_ACTION_SUCCESS_MOVE_COST,
                       MAX_MOVE_FRAGS, NULL);
  /* The reduction only applies to this action. */
  effect_req_append(peffect, req_from_str("Action", "Local",
                                          FALSE, TRUE, TRUE,
                                          action_rule_name));
  /* No reduction here unless disembarking to native terrain. */
  effect_req_append(peffect, req_from_values(VUT_UNITSTATE,
                                             REQ_RANGE_LOCAL,
                                             FALSE, TRUE, TRUE,
                                             USP_NATIVE_TILE));
  /* Doesn't apply to non TerrainSpeed. */
  effect_req_append(peffect, req_from_str("UnitClassFlag", "Local",
                                          FALSE, TRUE, FALSE,
                                          "TerrainSpeed"));

  return TRUE;
}

/**********************************************************************//**
  Replace slow_invasions and friends.
**************************************************************************/
bool rscompat_old_slow_invasions_3_1(struct rscompat_info *compat,
                                     bool slow_invasions)
{
  if (compat->version < RSFORMAT_3_1 && compat->version < RSFORMAT_3_1) {
    /* BeachLander and slow_invasions has moved to the ruleset. Use a "fake
     * generalized" Transport Disembark, Conquer City, Enter Hut and
     * Frighten Hut to handle it. */

    struct action_enabler *enabler;
    struct requirement e_req;

    enabler = action_enabler_new();
    enabler->action = ACTION_TRANSPORT_DISEMBARK1;
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

    /* Hut entry and frightening is enabler controlled in 3.1. They can
     * involve disembarking. */
    enabler = action_enabler_new();
    enabler->action = ACTION_HUT_ENTER;
    e_req = req_from_str("UnitClassFlag", "Local", FALSE, FALSE, FALSE,
                         "HutNothing");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    enabler = action_enabler_new();
    enabler->action = ACTION_HUT_FRIGHTEN;
    e_req = req_from_str("UnitClassFlag", "Local", FALSE, FALSE, FALSE,
                         "HutNothing");
    requirement_vector_append(&enabler->actor_reqs, e_req);
    action_enabler_add(enabler);

    if (slow_invasions) {
      /* Make disembarking from non native terrain a different action. */

      struct action *paction;

      /* Add the enablers */
      /* The city conquest enablers are really from 3.0 */
      slow_invasion_enablers(ACTION_CONQUER_CITY, ACTION_CONQUER_CITY2);

      /* Enablers for entering and frightening huts and for disembarking
       * were added in a slow invasion neutral way.
       * Make them slow invasion friendly. */
      slow_invasion_enablers(ACTION_TRANSPORT_DISEMBARK1,
                             ACTION_TRANSPORT_DISEMBARK2);
      slow_invasion_enablers(ACTION_CONQUER_EXTRAS, ACTION_CONQUER_EXTRAS2);
      slow_invasion_enablers(ACTION_HUT_ENTER, ACTION_HUT_ENTER2);
      slow_invasion_enablers(ACTION_HUT_FRIGHTEN, ACTION_HUT_FRIGHTEN2);

      /* Add the actions */

      /* Use "Transport Disembark 2" for disembarking from non native. */
      paction = action_by_number(ACTION_TRANSPORT_DISEMBARK2);
      /* "Transport Disembark" and "Transport Disembark 2" won't appear in
       * the same action selection dialog given their opposite
       * requirements. */
      paction->quiet = TRUE;
      /* Make what is happening clear. */
      /* TRANS: _Disembark from non native (100% chance of success). */
      sz_strlcpy(paction->ui_name, N_("%sDisembark from non native%s"));


      /* Use "Conquer City 2" for conquring from non native. */
      paction = action_by_number(ACTION_CONQUER_CITY2);
      /* "Conquer City" and "Conquer City 2" won't appear in
       * the same action selection dialog given their opposite
       * requirements. */
      paction->quiet = TRUE;
      /* Make what is happening clear. */
      /* TRANS: _Conquer City from non native (100% chance of success). */
      sz_strlcpy(paction->ui_name, N_("%sConquer City from non native%s"));


      /* Use "Conquer Extras 2" for conquring from non native. */
      paction = action_by_number(ACTION_CONQUER_EXTRAS2);
      /* "Conquer Extras" and "Conquer Extras 2" won't appear in
       * the same action selection dialog given their opposite
       * requirements. */
      paction->quiet = TRUE;
      /* Make what is happening clear. */
      /* TRANS: _Enter Hut from non native (100% chance of success). */
      sz_strlcpy(paction->ui_name, N_("%sConquer Extras from non native%s"));


      /* Use "Enter Hut 2" for conquring from non native. */
      paction = action_by_number(ACTION_HUT_ENTER2);
      /* "Enter Hut" and "Enter Hut 2" won't appear in
       * the same action selection dialog given their opposite
       * requirements. */
      paction->quiet = TRUE;
      /* Make what is happening clear. */
      /* TRANS: _Enter Hut from non native (100% chance of success). */
      sz_strlcpy(paction->ui_name, N_("%sEnter Hut from non native%s"));

      /* Use "Frighten Hut 2" for conquring from non native. */
      paction = action_by_number(ACTION_HUT_FRIGHTEN2);
      /* "Frighten Hut" and "Frighten Hut 2" won't appear in
       * the same action selection dialog given their opposite
       * requirements. */
      paction->quiet = TRUE;
      /* Make what is happening clear. */
      /* TRANS: _Frighten Hut from non native (100% chance of success). */
      sz_strlcpy(paction->ui_name, N_("%sFrighten Hut from non native%s"));

      /* Take movement for disembarking, conquering and hut popping to
       * native terrain from non native terrain */
      slow_invasion_effects("Transport Disembark 2");
      slow_invasion_effects("Conquer City 2");
      slow_invasion_effects("Conquer Extras 2");
      slow_invasion_effects("Enter Hut 2");
      slow_invasion_effects("Frighten Hut 2");
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Replace deprecated requirement type names with currently valid ones.
**************************************************************************/
const char *rscompat_req_type_name_3_1(const char *old_type)
{
  if (!fc_strcasecmp("BaseFlag", old_type)) {
    /* Remaining BaseFlag has been turned to an extra flag */
    return "ExtraFlag";
  }

  return old_type;
}

/**********************************************************************//**
  Replace deprecated requirement type names with currently valid ones.

  The extra arguments are for situation where some, but not all, instances
  of a requirement type should become something else.
**************************************************************************/
const char *rscompat_req_name_3_1(const char *type,
                                  const char *old_name)
{
  if (!fc_strcasecmp("DiplRel", type)
      && !fc_strcasecmp("Is foreign", old_name)) {
    return "Foreign";
  }

  if (!fc_strcasecmp("AI", type)
      && !fc_strcasecmp("Handicapped", old_name)) {
    return "Restricted";
  }

  return old_name;
}

/**********************************************************************//**
  Modify requirement vectors in ways that require looking at the entire
  vector, rather than just individual requirements.
**************************************************************************/
void rscompat_req_vec_adjust_3_1(struct rscompat_info *compat,
                                 struct requirement_vector *preqs,
                                 int *reqs_count,
                                 const char *filename, const char *sec,
                                 const char *sub, const char *rfor)
{
  char buf[1024];

  /* Add a missing 'alltemperate' server setting requirement to any vector
   * including a 'singlepole' requirement.
   * See also sanity_check_req_vec_singlepole() in rssanity.c
   *
   * Note that this *does* change the semantics of the given vector, so we
   * must inform the user of this change. */
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {
    /* heuristic: the only non-conjunctive requirement vectors are
     * improvement obsoletion requirements */
    bool conjunctive = BOOL_VAL(fc_strcasecmp(sub, "obsolete_by"));
    bool need_alltemperate_req = FALSE;
    /* "wrong" here only applies when we do in fact have a
     * 'singlepole' requirement */
    bool has_wrong_alltemperate_req = FALSE;

    requirement_vector_iterate(preqs, preq) {
      server_setting_id id;
      struct setting *pset;

      if (preq->source.kind != VUT_SERVERSETTING) {
        continue;
      }

      id = ssetv_setting_get(preq->source.value.ssetval);
      fc_assert_ret(server_setting_exists(id));
      pset = setting_by_number(id);

      if (pset == setting_by_name("singlepole")) {
        need_alltemperate_req = TRUE;
      } else if (pset == setting_by_name("alltemperate")) {
        if (XOR(conjunctive, preq->present)) {
          need_alltemperate_req = FALSE;
          break;
        } else {
          has_wrong_alltemperate_req = TRUE;
        }
      }
    } requirement_vector_iterate_end;

    if (need_alltemperate_req) {
      if (has_wrong_alltemperate_req) {
        /* Can't do anything here - already a nonsensical situation */
        if (compat->log_cb != NULL) {
          fc_snprintf(buf, sizeof(buf),
                      "%s: Cannot update requirement vector %s.%s (for %s)"
                      " where a 'singlepole' requirement is only relevant"
                      " when 'alltemperate' is already active. Fix this"
                      " manually: drop the 'singlepole' requirement, or"
                      " flip the 'alltemperate' requirement.",
                      filename, sec, sub, rfor);
          compat->log_cb(buf);
        }
      } else {
        struct requirement new_req = req_from_values(VUT_SERVERSETTING,
            REQ_RANGE_WORLD, FALSE, !conjunctive, FALSE,
            ssetv_by_rule_name("alltemperate"));

        requirement_vector_append(preqs, new_req);
        (*reqs_count)++;

        if (compat->log_cb != NULL) {
          fc_snprintf(buf, sizeof(buf),
                      "%s: Added 'alltemperate' server setting requirement to"
                      " requirement vector %s.%s (for %s) so that current"
                      " 'singlepole' requirement is only relevant when"
                      " 'alltemperate' is disabled. This likely changes the"
                      " semantics; make sure the new rules are sensible.",
                      filename, sec, sub, rfor);
          compat->log_cb(buf);
        }
      }
    }
  }
}

/**********************************************************************//**
  Replace deprecated unit type flag names with currently valid ones.
**************************************************************************/
const char *rscompat_utype_flag_name_3_1(struct rscompat_info *compat,
                                         const char *old_type)
{
  if (compat->compat_mode) {
  }

  return old_type;
}

/**********************************************************************//**
  Replace deprecated combat bonus names with currently valid ones.
**************************************************************************/
const char *rscompat_combat_bonus_name_3_1(struct rscompat_info *compat,
                                           const char *old_type)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {
    if (!fc_strcasecmp("Firepower1", old_type)) {
      return combat_bonus_type_name(CBONUS_LOW_FIREPOWER);
    }
  }

  return old_type;
}

/**********************************************************************//**
  Set compatibility unit class flags.
**************************************************************************/
void rscompat_uclass_flags_3_1(struct rscompat_info *compat,
                               struct unit_class *pclass)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {
    /* Old hardcoded behavior was like all units having NonNatBombardTgt */
    BV_SET(pclass->flags, UCF_NONNAT_BOMBARD_TGT);
  }
}

/**********************************************************************//**
  Adjust freeciv-3.0 ruleset extra definitions to freeciv-3.1
**************************************************************************/
void rscompat_extra_adjust_3_1(struct rscompat_info *compat,
                               struct extra_type *pextra)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {

    /* Give remove cause ERM_ENTER for huts */
    if (is_extra_caused_by(pextra, EC_HUT)) {
      pextra->rmcauses |= (1 << ERM_ENTER);
      extra_to_removed_by_list(pextra, ERM_ENTER);
    }
  }
}

/**********************************************************************//**
  Determine freeciv-3.1 road gui_type from freeciv-3.0 compat_special
**************************************************************************/
enum road_gui_type rscompat_road_gui_type_3_1(struct road_type *proad)
{
  switch (proad->compat) {
  case ROCO_ROAD:
    return ROAD_GUI_ROAD;
  case ROCO_RAILROAD:
    return ROAD_GUI_RAILROAD;
  case ROCO_RIVER:
  case ROCO_NONE:
    break;
  }

  return ROAD_GUI_OTHER;
}

/**********************************************************************//**
  Adjust freeciv-3.0 ruleset action ui_name to freeciv-3.1
**************************************************************************/
const char *rscompat_action_ui_name_S3_1(struct rscompat_info *compat,
                                         int act_id)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {
    if (act_id == ACTION_DISBAND_UNIT_RECOVER) {
      return "ui_name_recycle_unit";
    }
  }

  return NULL;
}

/**********************************************************************//**
  Adjust freeciv-3.0 ruleset action max_range to freeciv-3.1
**************************************************************************/
const char *rscompat_action_max_range_name_S3_1(struct rscompat_info *compat,
                                                int act_id)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_1) {
    if (act_id == ACTION_DISBAND_UNIT_RECOVER) {
      return "recycle_unit_max_range";
    }
  }

  return NULL;
}
