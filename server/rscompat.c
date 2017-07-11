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
#include "effects.h"
#include "game.h"
#include "requirements.h"
#include "unittype.h"

/* server */
#include "rssanity.h"
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
  Find and return the first unused unit class user flag. If all unit class
  user flags are taken MAX_NUM_USER_UCLASS_FLAGS is returned.
**************************************************************************/
static int first_free_unit_class_user_flag(void)
{
  int flag;

  /* Find the first unused user defined unit type flag. */
  for (flag = 0; flag < MAX_NUM_USER_UCLASS_FLAGS; flag++) {
    if (unit_class_flag_id_name_cb(flag + UCF_USER_FLAG_1) == NULL) {
      return flag;
    }
  }

  /* All unit type user flags are taken. */
  return MAX_NUM_USER_UCLASS_FLAGS;
}

/**************************************************************************
  Find and return the first unused extra user flag. If all extra user
  flags are taken MAX_NUM_USER_EXTRA_FLAGS is returned.
**************************************************************************/
static int first_free_extra_user_flag(void)
{
  int flag;

  /* Find the first unused user defined extra flag. */
  for (flag = 0; flag < MAX_NUM_USER_EXTRA_FLAGS; flag++) {
    if (extra_flag_id_name_cb(flag + EF_USER_FLAG_1) == NULL) {
      return flag;
    }
  }

  /* All unit type user flags are taken. */
  return MAX_NUM_USER_EXTRA_FLAGS;
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
    /* Some unit type flags moved to the ruleset between 2.6 and 3.0.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_flags_30[] = {
      { N_("Capturer"), N_("Can capture some enemy units.") },
      { N_("Capturable"), N_("Can be captured by some enemy units.") },
      { N_("Cities"), N_("Can found cities.") },
      { N_("AddToCity"), N_("Can join cities.") },
      { N_("Bombarder"), N_("Can do bombard attacks.") },
      { N_("Nuclear"), N_("This unit's attack causes a nuclear explosion!") },
      { N_("Paratroopers"),
          N_("Can be paradropped from a friendly city or suitable base.") },
      { N_("Marines"), N_("Can launch attack from non-native tiles.") },
    };

    /* Some unit class flags moved to the ruleset between 2.6 and 3.0.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_class_flags_30[] = {
      { N_("Airliftable"), N_("Can be airlifted from a suitable city.") },
      { N_("AttFromNonNative"), N_("Can launch attack from non-native tiles.") },
    };

    int first_free;
    int i;

    /* Unit type flags. */
    first_free = first_free_unit_type_user_flag() + UTYF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_flags_30); i++) {
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
      if (unit_type_flag_id_by_name(new_flags_30[i].name, fc_strcasecmp)
          != unit_type_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user unit type flag '%s'",
                      new_flags_30[i].name);
        return FALSE;
      }
      set_user_unit_type_flag_name(first_free + i,
                                   new_flags_30[i].name,
                                   new_flags_30[i].helptxt);
    }

    /* Unit type class flags. */
    first_free = first_free_unit_class_user_flag() + UCF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_class_flags_30); i++) {
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
      if (unit_class_flag_id_by_name(new_class_flags_30[i].name, fc_strcasecmp)
          != unit_class_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user unit class flag '%s'",
                      new_class_flags_30[i].name);
        return FALSE;
      }
      set_user_unit_class_flag_name(first_free + i,
                                    new_class_flags_30[i].name,
                                    new_class_flags_30[i].helptxt);
    }
  }

  if (info->ver_terrain < 10) {
    /* Some extra flags moved to the ruleset between 2.6 and 3.0.
     * Add them back as user flags.
     * XXX: ruleset might not need all of these, and may have enough
     * flags of its own that these additional ones prevent conversion. */
    const struct {
      const char *name;
      const char *helptxt;
    } new_extra_flags_30[] = {
      { N_("ParadropFrom"), N_("Units can paradrop from this tile.") },
      { N_("DiplomatDefense"), N_("Diplomatic units get a 25% defense "
                                  "bonus in diplomatic fights.") },
    };

    int first_free;
    int i;

    /* Extra flags. */
    first_free = first_free_extra_user_flag() + EF_USER_FLAG_1;

    for (i = 0; i < ARRAY_SIZE(new_extra_flags_30); i++) {
      if (EF_USER_FLAG_1 + MAX_NUM_USER_EXTRA_FLAGS <= first_free + i) {
        /* Can't add the user extra flags. */
        ruleset_error(LOG_ERROR,
                      "Can't upgrade the ruleset. Not enough free extra "
                      "user flags to add user flags for the extra flags "
                      "that used to be hardcoded.");
        return FALSE;
      }

      /* Shouldn't be possible for valid old ruleset to have flag names that
       * clash with these ones */
      if (extra_flag_id_by_name(new_extra_flags_30[i].name, fc_strcasecmp)
          != extra_flag_id_invalid()) {
        ruleset_error(LOG_ERROR,
                      "Ruleset had illegal user extra flag '%s'",
                      new_extra_flags_30[i].name);
        return FALSE;
      }
      set_user_extra_flag_name(first_free + i,
                               new_extra_flags_30[i].name,
                               new_extra_flags_30[i].helptxt);
    }
  }

  /* No errors encountered. */
  return TRUE;
}

/**************************************************************************
  Adjust effects
**************************************************************************/
static bool effect_list_compat_cb(struct effect *peffect, void *data)
{
  struct rscompat_info *info = (struct rscompat_info *)data;

  if (info->ver_effects < 10) {
    if (peffect->type == EFT_HAVE_EMBASSIES) {
      /* Create "Have_Contacts" effect matching each "Have_Embassies" */
      struct effect *contacts = effect_copy(peffect);

      contacts->type = EFT_HAVE_CONTACTS;
    }

    if (peffect->type == EFT_SPY_RESISTANT) {
      /* Create "Building_Saboteur_Resistant" effect matching each
       * "Spy_Resistant" */
      struct effect *contacts = effect_copy(peffect);

      contacts->type = EFT_SABOTEUR_RESISTANT;
    }

    if (peffect->type == EFT_ILLEGAL_ACTION_MOVE_COST) {
      /* Founding and joining a city became action enabler controlled in
       * Freeciv 3.0. Old hard coded rules had no punishment for trying to
       * do those when it is illegal according to the rules. */
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, TRUE, "Found City"));
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, TRUE, "Join City"));
    }
  }

  /* Go to the next effect. */
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

  /* Upgrade existing effects. Done before new effects are added to prevent
   * the new effects from being upgraded by accident. */
  iterate_effect_cache(effect_list_compat_cb, info);

  if (info->ver_cities < 10) {
    struct action_auto_perf *auto_perf;

    /* Missing unit upkeep. */

    /* Can't pay food upkeep! */
    auto_perf = action_auto_perf_slot_number(ACTION_AUTO_UPKEEP_FOOD);

    /* The actor unit can't have the unit type flag EvacuateFirst. */
    requirement_vector_append(&auto_perf->reqs,
                              req_from_str("UnitFlag", "Local",
                                           FALSE, FALSE, TRUE,
                                           "EvacuateFirst"));

    game.info.muuk_food_wipe = TRUE;

    /* Can't pay gold upkeep! */
    auto_perf = action_auto_perf_slot_number(ACTION_AUTO_UPKEEP_GOLD);

    /* TODO: Should missing gold upkeep really be able to kill units with
     * the EvacuateFirst unit type flag? */
    game.info.muuk_gold_wipe = TRUE;

    /* Can't pay shield upkeep! */
    auto_perf = action_auto_perf_slot_number(ACTION_AUTO_UPKEEP_SHIELD);

    /* The actor unit can't have the unit type flag EvacuateFirst. */
    requirement_vector_append(&auto_perf->reqs,
                              req_from_str("UnitFlag", "Local",
                                           FALSE, FALSE, TRUE,
                                           "EvacuateFirst"));

    /* Only disbanding because of missing shield upkeep will try to disband
     * via a forced action. */
    auto_perf->alternatives[0] = ACTION_HELP_WONDER;
    auto_perf->alternatives[1] = ACTION_RECYCLE_UNIT;
    auto_perf->alternatives[2] = ACTION_DISBAND_UNIT;

    game.info.muuk_shield_wipe = FALSE;
  }

  if (info->ver_units < 10) {
    unit_type_iterate(ptype) {
      if (utype_has_flag(ptype, UTYF_SETTLERS)) {
        int flag;

        flag = unit_type_flag_id_by_name("Infra", fc_strcasecmp);
        fc_assert(unit_type_flag_id_is_valid(flag));
        BV_SET(ptype->flags, flag);
      }

      if (utype_can_do_action(ptype, ACTION_SPY_INVESTIGATE_CITY)
          || utype_can_do_action(ptype, ACTION_INV_CITY_SPEND)
          || utype_can_do_action(ptype, ACTION_SPY_POISON)
          || utype_can_do_action(ptype, ACTION_SPY_STEAL_GOLD)
          || utype_can_do_action(ptype, ACTION_SPY_STEAL_GOLD_ESC)
          || utype_can_do_action(ptype, ACTION_SPY_SABOTAGE_CITY)
          || utype_can_do_action(ptype, ACTION_SPY_SABOTAGE_CITY_ESC)
          || utype_can_do_action(ptype, ACTION_SPY_TARGETED_SABOTAGE_CITY)
          || utype_can_do_action(ptype, ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC)
          || utype_can_do_action(ptype, ACTION_SPY_STEAL_TECH)
          || utype_can_do_action(ptype, ACTION_SPY_TARGETED_STEAL_TECH)
          || utype_can_do_action(ptype, ACTION_SPY_INCITE_CITY)
          || utype_can_do_action(ptype, ACTION_SPY_INCITE_CITY_ESC)
          || utype_can_do_action(ptype, ACTION_SPY_BRIBE_UNIT)
          || utype_can_do_action(ptype, ACTION_SPY_SABOTAGE_UNIT)
          || 0 < ptype->transport_capacity) {
        BV_SET(ptype->flags, UTYF_PROVOKING);
      }
    } unit_type_iterate_end;
  }

  if (info->ver_game < 10) {
    struct action_enabler *enabler;
    struct action_auto_perf *auto_perf;

    /* Unit capture is now action enabler controlled. Add the old rule that
     * units with the Capturer unit type flag can capture units with the
     * Capturable unit type flag. */

    enabler = action_enabler_new();

    enabler->action = ACTION_CAPTURE_UNITS;

    /* The actor unit must have the unit type flag Capturer, belong to a
     * player that is at war with each player that owns a target unit and
     * have at least one move fragment left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Capturer"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           TRUE, TRUE, "War"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target unit(s) must all have the Capturable unit type flag and
     * can't be inside a city. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Capturable"));
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("CityTile", "Local", FALSE,
                                           FALSE, TRUE, "Center"));

    action_enabler_add(enabler);

    /* City founding is now action enabler controlled. Add the old rules.
     * The rule that a city can't be founded on a tile claimed by another
     * player has to be translated to "a city can be founded on an
     * unclaimed tile OR on a tile owned by the actor player. That results
     * in two action enablers. */

    enabler = action_enabler_new();

    enabler->action = ACTION_FOUND_CITY;

    /* The actor unit must have the unit type flag Cities. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Cities"));

    /* The actor must be on native terrain. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           TRUE, TRUE, "OnLivableTile"));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target tile can't be claimed. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("CityTile", "Local", FALSE,
                                           FALSE, TRUE, "Claimed"));

    action_enabler_add(enabler);

    enabler = action_enabler_new();

    enabler->action = ACTION_FOUND_CITY;

    /* The actor unit must have the unit type flag Cities. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Cities"));

    /* The actor must be on native terrain. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           TRUE, TRUE, "OnLivableTile"));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target tile must be domestic. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Is foreign"));

    action_enabler_add(enabler);

    /* City joining is now action enabler controlled. Add the old rule
     * that units with the AddToCity unit type flag can join a city.
     * Other requirements are still hard coded. */

    enabler = action_enabler_new();

    enabler->action = ACTION_JOIN_CITY;

    /* The actor unit must have the unit type flag AddToCity. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "AddToCity"));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target city must be domestic. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Is foreign"));

    action_enabler_add(enabler);

    /* The bombard attack is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_BOMBARD;

    /* The actor unit must have the unit type flag Bombarder. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Bombarder"));

    /* The actor unit can't be transported. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           FALSE, TRUE, "Transported"));

    /* The actor unit must have a move fragment left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* Must be at war with each target unit. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, DS_WAR));

    /* The target can't be on an ocean tile. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("TerrainClass", "Local", FALSE,
                                           FALSE, TRUE, "Oceanic"));

    action_enabler_add(enabler);

    /* The nuclear attack is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_NUKE;

    /* The actor unit must have the unit type flag Nuclear. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Nuclear"));

    action_enabler_add(enabler);

    /* Disbanding a unit in a city to have 50% of its shields added to the
     * production is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_RECYCLE_UNIT;

    /* The actor unit can't have the unit type flag EvacuateFirst. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local",
                                           FALSE, FALSE, TRUE,
                                           "EvacuateFirst"));

    /* The target city must be domestic, allied or on our team. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "War"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Cease-fire"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Armistice"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Peace"));

    action_enabler_add(enabler);

    /* Disbanding a unit (without getting anything in return) is now action
     * enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_DISBAND_UNIT;

    /* The actor unit can't have the unit type flag EvacuateFirst. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local",
                                           FALSE, FALSE, TRUE,
                                           "EvacuateFirst"));

    action_enabler_add(enabler);

    /* Changing a unit's home city is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_HOME_CITY;

    /* The actor unit can't have the unit type flag NoHome. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_NOHOME));

    /* The actor unit has a home city. (This is a feature since being
     * homeless is a big benefit. Unless the killunhomed setting is above
     * 0.) */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           TRUE, TRUE, "HasHomeCity"));

    /* The target city must be domestic. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Is foreign"));

    action_enabler_add(enabler);

    /* User initiated unit upgrade is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_UPGRADE_UNIT;

    /* The target city must be domestic. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, TRUE, "Is foreign"));

    action_enabler_add(enabler);

    /* Paradrop is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_PARADROP;

    /* The actor unit must have the unit type flag Paratroopers. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Paratroopers"));

    /* The actor unit isn't transporting another unit. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UNITSTATE,
                                              REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              USP_TRANSPORTING));

    /* The actor unit must be inside a city. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("CityTile", "Local", FALSE,
                                           TRUE, TRUE, "Center"));

    action_enabler_add(enabler);

    enabler = action_enabler_new();

    enabler->action = ACTION_PARADROP;

    /* The actor unit must have the unit type flag Paratroopers. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Paratroopers"));

    /* The actor unit isn't transporting another unit. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UNITSTATE,
                                              REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              USP_TRANSPORTING));

    /* The actor unit must be in an extra it can paradrop from. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("ExtraFlag", "Local", FALSE,
                                           TRUE, TRUE, "ParadropFrom"));

    action_enabler_add(enabler);

    /* Airlift is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_AIRLIFT;

    /* The actor unit must have the unit class flag Airliftable. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "Airliftable"));

    /* The actor unit isn't transporting another unit. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UNITSTATE,
                                              REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              USP_TRANSPORTING));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    action_enabler_add(enabler);

    /* Regular attack is now action enabler controlled. */

    /* Regular attack from native tile */

    enabler = action_enabler_new();

    enabler->action = ACTION_ATTACK;

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The actor unit must be on a native tile. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UNITSTATE,
                                              REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE,
                                              USP_NATIVE_TILE));

    action_enabler_add(enabler);

    /* Regular attack as Marines. */

    enabler = action_enabler_new();

    enabler->action = ACTION_ATTACK;

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The actor unit must have the unit type flag Marines. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Marines"));

    action_enabler_add(enabler);

    /* Regular attack as AttFromNonNative. */

    enabler = action_enabler_new();

    enabler->action = ACTION_ATTACK;

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The actor unit must have the unit class flag AttFromNonNative. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "AttFromNonNative"));

    action_enabler_add(enabler);

    /* City occupation is now action enabler controlled. */

    /* City occupation from livable tile. */

    enabler = action_enabler_new();

    enabler->action = ACTION_CONQUER_CITY;

    /* The actor unit must have the unit class flag CanOccupyCity. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "CanOccupyCity"));

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* Must be at war with the target city. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, DS_WAR));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target city must be empty. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("MaxUnitsOnTile", "Local", FALSE,
                                           TRUE, TRUE, "0"));

    /* The actor unit must be on a livable tile. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UNITSTATE,
                                              REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE,
                                              USP_LIVABLE_TILE));

    action_enabler_add(enabler);

    /* City occupation as Marines. */

    enabler = action_enabler_new();

    enabler->action = ACTION_CONQUER_CITY;

    /* The actor unit must have the unit class flag CanOccupyCity. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "CanOccupyCity"));

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* Must be at war with the target city. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, DS_WAR));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target city must be empty. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("MaxUnitsOnTile", "Local", FALSE,
                                           TRUE, TRUE, "0"));

    /* The actor unit must have the unit type flag Marines. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, TRUE, "Marines"));

    action_enabler_add(enabler);

    /* City occupation as AttFromNonNative. */

    enabler = action_enabler_new();

    enabler->action = ACTION_CONQUER_CITY;

    /* The actor unit must have the unit class flag CanOccupyCity. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "CanOccupyCity"));

    /* The actor unit can't have the unit type flag NonMil. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_UTFLAG, REQ_RANGE_LOCAL,
                                              FALSE, FALSE, TRUE,
                                              UTYF_CIVILIAN));

    /* Must be at war with the target city. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_values(VUT_DIPLREL, REQ_RANGE_LOCAL,
                                              FALSE, TRUE, TRUE, DS_WAR));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, TRUE, "1"));

    /* The target city must be empty. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("MaxUnitsOnTile", "Local", FALSE,
                                           TRUE, TRUE, "0"));

    /* The actor unit must have the unit class flag AttFromNonNative. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitClassFlag", "Local", FALSE,
                                           TRUE, TRUE, "AttFromNonNative"));

    action_enabler_add(enabler);

    /* Update action enablers. */
    action_enablers_iterate(ae) {
      /* The rule that Help Wonder only can help wonders now lives in the
       * ruleset. */
      if (ae->action == ACTION_HELP_WONDER) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows doing "Help Wonder" to great wonders. */
        requirement_vector_append(&ae->target_reqs,
                                  req_from_str("BuildingGenus", "Local", FALSE,
                                               TRUE, TRUE, "GreatWonder"));

        /* The other allows doing "Help Wonder" to small wonders. */
        requirement_vector_append(&enabler->target_reqs,
                                  req_from_str("BuildingGenus", "Local", FALSE,
                                               TRUE, TRUE, "SmallWonder"));

      }

      /* Investigate city is split in a unit consuming and a non unit
       * consuming version. */
      if (ae->action == ACTION_SPY_INVESTIGATE_CITY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Investigate City". */
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do
         * "Investigate City Spend Unit". */
        enabler->action = ACTION_INV_CITY_SPEND;
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      /* Establish Embassy is split in a unit consuming and a non unit
       * consuming version. */
      if (ae->action == ACTION_ESTABLISH_EMBASSY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Establish Embassy". */
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do "Establish Embassy Stay". */
        enabler->action = ACTION_ESTABLISH_EMBASSY_STAY;
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      /* Incite City is split in a unit consuming and a "try to escape"
       * version. */
      if (ae->action == ACTION_SPY_INCITE_CITY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Incite City Escape". */
        ae->action = ACTION_SPY_INCITE_CITY_ESC;
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do "Incite City". */
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      /* Steal Gold is split in a unit consuming and a "try to escape"
       * version. */
      if (ae->action == ACTION_SPY_STEAL_GOLD) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Steal Gold Escape". */
        ae->action = ACTION_SPY_STEAL_GOLD_ESC;
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do "Steal Gold". */
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      /* "Sabotage City" is split in a unit consuming and a "try to escape"
       * version. */
      if (ae->action == ACTION_SPY_SABOTAGE_CITY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Sabotage City Escape". */
        ae->action = ACTION_SPY_SABOTAGE_CITY_ESC;
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do "Sabotage City". */
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      /* "Targeted Sabotage City" is split in a unit consuming and a "try to
       * escape" version. */
      if (ae->action == ACTION_SPY_TARGETED_SABOTAGE_CITY) {
        /* The old rule is represented with two action enablers. */
        enabler = action_enabler_copy(ae);
        action_enabler_add(enabler);

        /* One allows spies to do "Targeted Sabotage City Escape". */
        ae->action = ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC;
        requirement_vector_append(&ae->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, TRUE, TRUE,
                                                  UTYF_SPY));

        /* The other allows non spies to do "Targeted Sabotage City". */
        requirement_vector_append(&enabler->actor_reqs,
                                  req_from_values(VUT_UTFLAG,
                                                  REQ_RANGE_LOCAL,
                                                  FALSE, FALSE, TRUE,
                                                  UTYF_SPY));

        /* Add previously implicit obligatory hard requirement(s) to the
         * newly created copy. (Not done below.) */
        action_enabler_obligatory_reqs_add(enabler);
      }

      if (action_enabler_obligatory_reqs_missing(ae)) {
        /* Add previously implicit obligatory hard requirement(s). */
        action_enabler_obligatory_reqs_add(ae);
      }
    } action_enablers_iterate_end;

    /* Auto attack. */
    auto_perf = action_auto_perf_slot_number(ACTION_AUTO_MOVED_ADJ);

    /* Not a good idea to nuke our own area. */
    requirement_vector_append(&auto_perf->reqs,
                              req_from_str("UnitFlag", "Local",
                                           FALSE, FALSE, TRUE,
                                           "Nuclear"));
  }

  if (info->ver_effects < 10) {
    /* The reduced one time trade bonus of Enter Marketplace (compared to
     * Establish Trade Route) has moved to the ruleset. */
    struct effect *peffect = effect_new(EFT_TRADE_REVENUE_BONUS, -1585, NULL);

    /* The reduction only applies to Enter Marketplace. */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            TRUE, "Enter Marketplace"));

    /* The fudge factor to more closely approximate Civ2 behavior has
     * moved to the ruleset. */
     peffect = effect_new(EFT_TRADE_REVENUE_BONUS, 1585, NULL);

     /* Diplomatic incident from getting caught stealing tech has moved to
      * the ruleset. */

     /* Start with the targeted steal tech. */
     peffect = effect_new(EFT_CASUS_BELLI_CAUGHT, 1, NULL);

     /* Should only apply to the targeted steal tech action. */
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE,
                                             "Targeted Steal Tech"));

     /* No incident if stolen during war. */
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Getting caught trying to do the untargeted steal tech action would
      * also cause an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_CAUGHT, 1, NULL);

     /* Should only apply to the untargeted steal tech action. */
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Steal Tech"));

     /* No incident if stolen during war. */
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Diplomatic incident from successfully performing an action. */

     /* Stealing a specified tech during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE,
                                             "Targeted Steal Tech"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE,
                                             "War"));

     /* Stealing a random tech during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Steal Tech"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Bribe unit during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Bribe Unit"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Sabotage unit during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Sabotage Unit"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Inciting a city during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Incite City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Incite City Escape"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Poisoning a city during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Poison City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Sabotaging a random improvement in a city during peace causes an
      * incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Sabotage City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Sabotaging a random improvement in a city and escaping during peace
      * causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE,
                                             "Sabotage City Escape"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Sabotaging a specific improvement in a city during peace causes
      * an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE,
                                             "Targeted Sabotage City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Sabotaging a specific improvement in a city and escaping during
      * peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE,
                                             "Targeted Sabotage City Escape"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Stealing gold during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Steal Gold"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                             FALSE, TRUE, "War"));

     /* Nuking someone causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, TRUE, "Explode Nuclear"));

     /* City sabotage is twice as difficult if target is specified. */
     peffect = effect_new(EFT_ACTION_ODDS_PCT, -50, NULL);
     effect_req_append(peffect,
                       req_from_str("Action", "Local", FALSE,
                                    TRUE, TRUE, "Targeted Sabotage City"));

     /* City sabotage and escaping is twice as difficult if target is
      * specified. */
     peffect = effect_new(EFT_ACTION_ODDS_PCT, -50, NULL);
     effect_req_append(peffect,
                       req_from_str("Action", "Local", FALSE,
                                    TRUE, TRUE,
                                    "Targeted Sabotage City Escape"));

     /* The DiplomatDefense flag effect now lives in the ruleset. */
     peffect = effect_new(EFT_SPY_RESISTANT, 25, NULL);
     effect_req_append(peffect,
                       req_from_str("ExtraFlag", "Local", FALSE,
                                    TRUE, TRUE, "DiplomatDefense"));
  }

  if (info->ver_terrain < 10) {
    /* Map from retired base flag to current extra flag. */
    const struct {
      enum base_flag_id from;
      enum extra_flag_id to;
    } base_to_extra_flag_map_3_0[] = {
      /* ParadropFrom has moved to the ruleset as an extra user flag. */
      { BF_RETIRED_PARADROP_FROM,
          extra_flag_id_by_name("ParadropFrom", fc_strcasecmp) },
      /* DiplomatDefense has moved to the ruleset as an extra user flag. */
      { BF_RETIRED_DIPLOMAT_DEFENSE,
          extra_flag_id_by_name("DiplomatDefense", fc_strcasecmp) },
      /* NoStackDeath is now an extra flag. */
      { BF_RETIRED_NO_STACK_DEATH, EF_NO_STACK_DEATH },
    };

    extra_type_by_cause_iterate(EC_BASE, pextra) {
      int i;
      struct base_type *pbase = extra_base_get(pextra);

      for (i = 0; i < ARRAY_SIZE(base_to_extra_flag_map_3_0); i++) {
        if (BV_ISSET(pbase->flags, base_to_extra_flag_map_3_0[i].from)) {
          /* Set the *extra* user flag */
          BV_SET(pextra->flags, base_to_extra_flag_map_3_0[i].to);
          /* Remove the retired *base* flag. */
          BV_CLR(pbase->flags, base_to_extra_flag_map_3_0[i].from);
        }
      }
    } extra_type_by_cause_iterate_end;
  }

  /* The ruleset may need adjustments it didn't need before compatibility
   * post processing.
   *
   * If this isn't done a user of ruleset compatibility that ends up using
   * the rules risks bad rules. A user that saves the ruleset rather than
   * using it risks an unexpected change on the next load and save. */
  autoadjust_ruleset_data();
}

/**************************************************************************
  Created one required good for rulesets lacking one.
**************************************************************************/
void rscompat_goods_3_0(void)
{
  struct goods_type *pgood;

  game.control.num_goods_types = 1;

  pgood = goods_by_number(0);

  names_set(&pgood->name, NULL, "Goods", "Goods");
}

/**************************************************************************
  Create extra equivalent for resource
**************************************************************************/
struct extra_type *rscompat_extra_from_resource_3_0(struct section_file *sfile,
                                                    const char *sec_name)
{
  if (game.control.num_extra_types >= MAX_EXTRA_TYPES) {
    ruleset_error(LOG_ERROR, "Can't convert resource from %s to an extra. No free slots.",
                  sec_name);
  } else {
    struct extra_type *pextra = extra_by_number(game.control.num_extra_types++);

    pextra->category = ECAT_RESOURCE;
    extra_to_caused_by_list(pextra, EC_RESOURCE);
    pextra->causes |= (1 << EC_RESOURCE);

    strcpy(pextra->graphic_str, secfile_lookup_str_default(sfile, "None", "%s.graphic", sec_name));
    strcpy(pextra->graphic_alt, secfile_lookup_str_default(sfile, "-", "%s.graphic_alt", sec_name));

    strcpy(pextra->activity_gfx, "None");
    strcpy(pextra->act_gfx_alt, "-");
    strcpy(pextra->rmact_gfx, "None");
    strcpy(pextra->rmact_gfx_alt, "-");

    return pextra;
  }

  return NULL;
}

/**************************************************************************
  Replace deprecated resource names with currently valid ones.

  The extra arguments are for situation where some, but not all, instances
  of a requirement type should become something else.
**************************************************************************/
const char *rscompat_req_type_name_3_0(const char *old_type,
                                       const char *old_range,
                                       bool old_survives, bool old_present,
                                       bool old_quiet,
                                       const char *old_value)
{
  if (!fc_strcasecmp("Resource", old_type)) {
    return "Extra";
  }

  if (!fc_strcasecmp("BaseFlag", old_type)
      && base_flag_is_retired(base_flag_id_by_name(old_value,
                                                   fc_strcasecmp))) {
    return "ExtraFlag";
  }

  return old_type;
}

/**************************************************************************
  Replace deprecated unit type flag names with currently valid ones.
**************************************************************************/
const char *rscompat_utype_flag_name_3_0(struct rscompat_info *compat,
                                         const char *old_type)
{
  if (compat->compat_mode) {
    if (!fc_strcasecmp("Trireme", old_type)) {
      return "CoastStrict";
    }

    if (!fc_strcasecmp("Undisbandable", old_type)) {
      return "EvacuateFirst";
    }
  }

  return old_type;
}
