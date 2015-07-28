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
    /* Some unit type flags have moved to the ruleset. Add them back as
     * user flags. */

    int unit_flag_position = first_free_unit_type_user_flag();

    if (MAX_NUM_USER_UNIT_FLAGS <= unit_flag_position + 4) {
      /* Can't add the user unit type flags. */
      log_error("Can't upgrade the ruleset. Not enough free unit type "
                "user flags to add user flags for the unit type flags "
                "that used to be hard coded");
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

    /* Add the unit type flag Cities. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("Cities"),
                                 N_("Can found cities."));
    unit_flag_position++;

    /* Add the unit type flag AddToCity. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("AddToCity"),
                                 N_("Can join cities."));
    unit_flag_position++;

    /* The unit type flag Bombarder is no longer hard coded. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("Bombarder"),
                                 N_("Can do bombard attacks."));
    unit_flag_position++;

    /* The unit type flag Nuclear is no longer hard coded. */
    set_user_unit_type_flag_name(unit_flag_position + UTYF_USER_FLAG_1,
                                 N_("Nuclear"),
                                 N_("This unit's attack causes a nuclear"
                                    " explosion!"));
    unit_flag_position++;
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

    if (peffect->type == EFT_ILLEGAL_ACTION_MOVE_COST) {
      /* Founding and joining a city became action enabler controlled in
       * Freeciv 3.0. Old hard coded rules had no punishment for trying to
       * do those when it is illegal according to the rules. */
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, "Found City"));
      effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                              FALSE, "Join City"));
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

  if (info->ver_game < 10) {
    struct action_enabler *enabler;

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
                                           TRUE, "Capturer"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           TRUE, "War"));
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, "1"));

    /* The target unit(s) must all have the Capturable unit type flag and
     * can't be inside a city. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Capturable"));
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("CityTile", "Local", FALSE,
                                           FALSE, "Center"));

    action_enabler_add(enabler);

    /* City founding is now action enabler controlled. Add the old rule
     * that units with the Cities unit type flag can found a city.
     * Other requirements are still hard coded. */

    enabler = action_enabler_new();

    enabler->action = ACTION_FOUND_CITY;

    /* The actor unit must have the unit type flag Cities. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Cities"));

    /* The actor must be on native terrain. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           TRUE, "OnLivableTile"));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, "1"));

    action_enabler_add(enabler);

    /* City joining is now action enabler controlled. Add the old rule
     * that units with the AddToCity unit type flag can join a city.
     * Other requirements are still hard coded. */

    enabler = action_enabler_new();

    enabler->action = ACTION_JOIN_CITY;

    /* The actor unit must have the unit type flag AddToCity. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "AddToCity"));

    /* The actor unit must have moves left. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("MinMoveFrags", "Local", FALSE,
                                           TRUE, "1"));

    /* The target city must be domestic. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, "Is foreign"));

    action_enabler_add(enabler);

    /* The bombard attack is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_BOMBARD;

    /* The actor unit must have the unit type flag Bombarder. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Bombarder"));

    /* The actor unit can't be transported. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitState", "Local", FALSE,
                                           FALSE, "Transported"));

    /* The target can't be on an ocean tile. */
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("TerrainClass", "Local", FALSE,
                                           FALSE, "Oceanic"));

    action_enabler_add(enabler);

    /* The nuclear attack is now action enabler controlled. */

    enabler = action_enabler_new();

    enabler->action = ACTION_NUKE;

    /* The actor unit must have the unit type flag Nuclear. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("UnitFlag", "Local", FALSE,
                                           TRUE, "Nuclear"));

    action_enabler_add(enabler);
  }

  if (info->ver_effects < 10) {
    /* The reduced one time trade bonus of Enter Marketplace (compared to
     * Establish Trade Route) has moved to the ruleset. */
    struct effect *peffect = effect_new(EFT_TRADE_REVENUE_BONUS, -1585, NULL);

    /* The reduction only applies to Enter Marketplace. */
    effect_req_append(peffect, req_from_str("Action", "Local", FALSE, TRUE,
                                            "Enter Marketplace"));

    /* The fudge factor to more closely approximate Civ2 behavior has
     * moved to the ruleset. */
     peffect = effect_new(EFT_TRADE_REVENUE_BONUS, 1585, NULL);

     /* Diplomatic incident from getting caught stealing tech has moved to
      * the ruleset. */

     /* Start with the targeted steal tech. */
     peffect = effect_new(EFT_CASUS_BELLI_CAUGHT, 1, NULL);

     /* Should only apply to the targeted steal tech action. */
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Targeted Steal Tech"));

     /* No incident if stolen during war. */
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Getting caught trying to do the untargeted steal tech action would
      * also cause an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_CAUGHT, 1, NULL);

     /* Should only apply to the untargeted steal tech action. */
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Steal Tech"));

     /* No incident if stolen during war. */
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Diplomatic incident from successfully performing an action. */

     /* Stealing a specified tech during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Targeted Steal Tech"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Stealing a random tech during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Steal Tech"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Bribe unit during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Bribe Unit"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Sabotage unit during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Sabotage Unit"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Inciting a city during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Incite City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Poisoning a city during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Poison City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Sabotaging a random improvement in a city during peace causes an
      * incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Sabotage City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Sabotaging a specific improvement in a city during peace causes
      * an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
         TRUE, "Targeted Sabotage City"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Stealing gold during peace causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Steal Gold"));
     effect_req_append(peffect, req_from_str("DiplRel", "Local", FALSE,
                                            FALSE, "War"));

     /* Nuking someone causes an incident. */
     peffect = effect_new(EFT_CASUS_BELLI_SUCCESS, 1, NULL);
     effect_req_append(peffect, req_from_str("Action", "Local", FALSE,
                                             TRUE, "Explode Nuclear"));
  }

  /* Upgrade existing effects. */
  iterate_effect_cache(effect_list_compat_cb, info);
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
