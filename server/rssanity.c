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

/* utility */
#include "astring.h"
#include "deprecations.h"

/* common */
#include "achievements.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "road.h"
#include "specialist.h"
#include "tech.h"

/* server */
#include "ruleset.h"
#include "settings.h"

#include "rssanity.h"

/**************************************************************************
  Is non-rule data in ruleset sane?
**************************************************************************/
static bool sanity_check_metadata(void)
{
  if (game.ruleset_summary != NULL &&
      strlen(game.ruleset_summary) > MAX_LEN_CONTENT) {
    log_error("Too long ruleset summary. It can be only %d bytes long. "
              "Put longer explanations to ruleset description.",
              MAX_LEN_CONTENT);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Does nation have tech initially?
**************************************************************************/
static bool nation_has_initial_tech(struct nation_type *pnation,
                                    struct advance *tech)
{
  int i;

  /* See if it's given as global init tech */
  for (i = 0; i < MAX_NUM_TECH_LIST
       && game.rgame.global_init_techs[i] != A_LAST; i++) {
    if (game.rgame.global_init_techs[i] == advance_number(tech)) {
      return TRUE;
    }
  }

  /* See if it's given as national init tech */
  for (i = 0;
       i < MAX_NUM_TECH_LIST && pnation->init_techs[i] != A_LAST;
       i++) {
    if (pnation->init_techs[i] == advance_number(tech)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**************************************************************************
  Sanity checks on a requirement in isolation.
  This will generally be things that could only not be checked at
  ruleset load time because they would have referenced things not yet
  loaded from the ruleset.
**************************************************************************/
static bool sanity_check_req_individual(struct requirement *preq,
                                        const char *list_for)
{
  switch (preq->source.kind) {
  case VUT_IMPROVEMENT:
    /* This check corresponds to what is_req_active() will support.
     * It can't be done in req_from_str(), as we may not have
     * loaded all building information at that time. */
    {
      struct impr_type *pimprove = preq->source.value.building;
      if (preq->range == REQ_RANGE_WORLD && !is_great_wonder(pimprove)) {
        log_error("%s: World-ranged requirement not supported for "
                  "%s (only great wonders supported)", list_for,
                  improvement_name_translation(pimprove));
        return FALSE;
      } else if (preq->range > REQ_RANGE_TRADEROUTE && !is_wonder(pimprove)) {
        log_error("%s: %s-ranged requirement not supported for "
                  "%s (only wonders supported)", list_for,
                  req_range_name(preq->range),
                  improvement_name_translation(pimprove));
        return FALSE;
      }
    }
    break;
  default:
    /* No other universals have checks that can't be done at ruleset
     * load time. See req_from_str(). */
    break;
  }
  return TRUE;
}

/**************************************************************************
  Helper function for sanity_check_req_list() and sanity_check_req_vec()
**************************************************************************/
static bool sanity_check_req_set(int reqs_of_type[], int local_reqs_of_type[],
                                 struct requirement *preq, bool conjunctive,
                                 int max_tiles, const char *list_for)
{
  int rc;

  fc_assert_ret_val(universals_n_is_valid(preq->source.kind), FALSE);

  if (!sanity_check_req_individual(preq, list_for)) {
    return FALSE;
  }

  if (!conjunctive) {
    /* All the checks below are only meaningful for conjunctive lists. */
    /* FIXME: we could add checks suitable for disjunctive lists. */
    return TRUE;
  }

  /* Add to counter for positive requirements. */
  if (preq->present) {
    reqs_of_type[preq->source.kind]++;
  }
  rc = reqs_of_type[preq->source.kind];

  if (preq->range == REQ_RANGE_LOCAL && preq->present) {
    local_reqs_of_type[preq->source.kind]++;

    switch (preq->source.kind) {
     case VUT_TERRAINCLASS:
       if (local_reqs_of_type[VUT_TERRAIN] > 0) {
         log_error("%s: Requirement list has both local terrain and terrainclass requirement",
                   list_for);
         return FALSE;
       }
       break;
     case VUT_TERRAIN:
       if (local_reqs_of_type[VUT_TERRAINCLASS] > 0) {
         log_error("%s: Requirement list has both local terrain and terrainclass requirement",
                   list_for);
         return FALSE;
       }
       break;
     default:
       break;
    }
  }

  if (rc > 1 && preq->present) {
    /* Multiple requirements of the same type */
    switch (preq->source.kind) {
     case VUT_GOVERNMENT:
     case VUT_UTYPE:
     case VUT_UCLASS:
     case VUT_OTYPE:
     case VUT_SPECIALIST:
     case VUT_MINSIZE: /* Breaks nothing, but has no sense either */
     case VUT_MINMOVES: /* Breaks nothing, but has no sense either */
     case VUT_MINVETERAN: /* Breaks nothing, but has no sense either */
     case VUT_MINHP: /* Breaks nothing, but has no sense either */
     case VUT_MINYEAR:
     case VUT_AI_LEVEL:
     case VUT_TERRAINALTER: /* Local range only */
     case VUT_CITYTILE:
     case VUT_STYLE:
       /* There can be only one requirement of these types (with current
        * range limitations)
        * Requirements might be identical, but we consider multiple
        * declarations error anyway. */

       log_error("%s: Requirement list has multiple %s requirements",
                 list_for, universal_type_rule_name(&preq->source));
       return FALSE;
       break;

     case VUT_TERRAIN:
     case VUT_RESOURCE:
       /* There can be only up to max_tiles requirements of these types */
       if (max_tiles != -1 && rc > max_tiles) {
         log_error("%s: Requirement list has more %s requirements than "
                   "can ever be fulfilled.", list_for,
                   universal_type_rule_name(&preq->source));
         return FALSE;
       }
       break;

     case VUT_TERRAINCLASS:
       if (rc > 2 || (max_tiles != -1 && rc > max_tiles)) {
         log_error("%s: Requirement list has more %s requirements than "
                   "can ever be fulfilled.", list_for,
                   universal_type_rule_name(&preq->source));
         return FALSE;
       }
       break;

    case VUT_AGE:
      /* There can be age of the city, unit, and player */
      if (rc > 3) {
        log_error("%s: Requirement list has more %s requirements than "
                  "can ever be fulfilled.", list_for,
                  universal_type_rule_name(&preq->source));
        return FALSE;
      }
      break;

     case VUT_TOPO:
       /* Can have multiple, since it's flag based (iso & wrapx & wrapy & hex) */
     case VUT_EXTRA:
       /* Note that there can be more than 1 extra / tile. */
     case VUT_MAXTILEUNITS:
       /* Can require different numbers on e.g. local/adjacent tiles. */
     case VUT_NATION:
       /* Can require multiple nations at Team/Alliance/World range. */
     case VUT_NATIONGROUP:
       /* Nations can be in multiple groups. */
     case VUT_NONE:
     case VUT_ADVANCE:
     case VUT_TECHFLAG:
     case VUT_IMPROVEMENT:
     case VUT_UNITSTATE:
       /* Can check different properties. */
     case VUT_UTFLAG:
     case VUT_UCFLAG:
     case VUT_TERRFLAG:
     case VUT_BASEFLAG:
     case VUT_ROADFLAG:
     case VUT_EXTRAFLAG:
     case VUT_NATIONALITY:
     case VUT_MINCULTURE:
     case VUT_ACHIEVEMENT:
     case VUT_DIPLREL:
       /* Can have multiple requirements of these types */
       break;
     case VUT_COUNT:
       /* Should never be in requirement vector */
       fc_assert(FALSE);
       return FALSE;
       break;
       /* No default handling here, as we want compiler warning
        * if new requirement type is added to enum and it's not handled
        * here. */
    }
  }

  return TRUE;
}

/**************************************************************************
  Sanity check requirement vector, including whether it's free of
  conflicting requirements.
  'conjunctive' should be TRUE if the vector is an AND vector (all requirements
  must be active), FALSE if it's a disjunctive (OR) vector.
  max_tiles is number of tiles that can provide requirement. Value -1
  disables checking based on number of tiles.

  Returns TRUE iff everything ok.

  TODO: This is based on current hardcoded range limitations.
        - There should be method of automatically determining these
          limitations for each requirement type
        - This function should check also problems caused by defining
          range to less than hardcoded max for requirement type
**************************************************************************/
static bool sanity_check_req_vec(const struct requirement_vector *preqs,
                                 bool conjunctive, int max_tiles,
                                 const char *list_for)
{
  int reqs_of_type[VUT_COUNT];
  int local_reqs_of_type[VUT_COUNT];

  /* Initialize requirement counters */
  memset(reqs_of_type, 0, sizeof(reqs_of_type));
  memset(local_reqs_of_type, 0, sizeof(local_reqs_of_type));

  requirement_vector_iterate(preqs, preq) {
    if (!sanity_check_req_set(reqs_of_type, local_reqs_of_type, preq,
                              conjunctive, max_tiles, list_for)) {
      return FALSE;
    }
    requirement_vector_iterate(preqs, nreq) {
      if (are_requirements_contradictions(preq, nreq)) {
        struct astring astr;
        struct astring nastr;

        log_error("%s: Requirements {%s} and {%s} contradict each other.",
                  list_for,
                  req_to_fstring(preq, &astr),
                  req_to_fstring(nreq, &nastr));

        astr_free(&astr);
        astr_free(&nastr);

        return FALSE;
      }
    } requirement_vector_iterate_end;
  } requirement_vector_iterate_end;

  return TRUE;
}

/**************************************************************************
  Sanity check callback for iterating effects cache.
**************************************************************************/
static bool effect_list_sanity_cb(const struct effect *peffect, void *data)
{
  int one_tile = -1; /* TODO: Determine correct value from effect.
                      *       -1 disables checking */

  return sanity_check_req_vec(&peffect->reqs, TRUE, one_tile,
                              effect_type_name(peffect->type));
}

/**************************************************************************
  Sanity check barbarian unit types
**************************************************************************/
static bool rs_barbarian_units(void)
{
  if (num_role_units(L_BARBARIAN) > 0) {
    if (num_role_units(L_BARBARIAN_LEADER) == 0) {
      ruleset_error(LOG_ERROR, "No role barbarian leader units");
      return FALSE;
    }
    if (num_role_units(L_BARBARIAN_BUILD) == 0) {
      ruleset_error(LOG_ERROR, "No role barbarian build units");
      return FALSE;
    }
    if (num_role_units(L_BARBARIAN_BOAT) == 0) {
      ruleset_error(LOG_ERROR, "No role barbarian ship units");
      return FALSE;
    } else if (num_role_units(L_BARBARIAN_BOAT) > 0) {
      bool sea_capable = FALSE;
      struct unit_type *u = get_role_unit(L_BARBARIAN_BOAT, 0);

      terrain_type_iterate(pterr) {
        if (is_ocean(pterr)
            && BV_ISSET(pterr->native_to, uclass_index(utype_class(u)))) {
          sea_capable = TRUE;
          break;
        }
      } terrain_type_iterate_end;

      if (!sea_capable) {
        ruleset_error(LOG_ERROR,
                      "Barbarian boat (%s) needs to be able to move at sea.",
                      utype_rule_name(u));
        return FALSE;
      }
    }
    if (num_role_units(L_BARBARIAN_SEA) == 0) {
      ruleset_error(LOG_ERROR, "No role sea raider barbarian units");
      return FALSE;
    }

    unit_type_iterate(ptype) {
      if (utype_has_role(ptype, L_BARBARIAN_BOAT)) {
        if (ptype->transport_capacity <= 1) {
          ruleset_error(LOG_ERROR,
                        "Barbarian boat %s has no capacity for both "
                        "leader and at least one man.",
                        utype_rule_name(ptype));
          return FALSE;
        }

        unit_type_iterate(pbarb) {
          if (utype_has_role(pbarb, L_BARBARIAN_SEA)
              || utype_has_role(pbarb, L_BARBARIAN_SEA_TECH)
              || utype_has_role(pbarb, L_BARBARIAN_LEADER)) {
            if (!can_unit_type_transport(ptype, utype_class(pbarb))) {
              ruleset_error(LOG_ERROR,
                            "Barbarian boat %s cannot transport "
                            "barbarian cargo %s.",
                            utype_rule_name(ptype),
                            utype_rule_name(pbarb));
              return FALSE;
            }
          }
        } unit_type_iterate_end;
      }
    } unit_type_iterate_end;
  }

  return TRUE;
}

/**************************************************************************
  Sanity check common unit types
**************************************************************************/
static bool rs_common_units(void)
{
  /* Check some required flags and roles etc: */
  if (num_role_units(UTYF_SETTLERS) == 0) {
    ruleset_error(LOG_ERROR, "No flag Settler units");
    return FALSE;
  }
  if (num_role_units(L_START_EXPLORER) == 0) {
    ruleset_error(LOG_ERROR, "No role Start Explorer units");
  }
  if (num_role_units(L_FERRYBOAT) == 0) {
    ruleset_error(LOG_ERROR, "No role Ferryboat units");
  }
  if (num_role_units(L_FIRSTBUILD) == 0) {
    ruleset_error(LOG_ERROR, "No role Firstbuild units");
  }

  if (num_role_units(L_FERRYBOAT) > 0) {
    bool sea_capable = FALSE;
    struct unit_type *u = get_role_unit(L_FERRYBOAT, 0);

    terrain_type_iterate(pterr) {
      if(is_ocean(pterr)
         && BV_ISSET(pterr->native_to, uclass_index(utype_class(u)))) {
        sea_capable = TRUE;
        break;
      }
    } terrain_type_iterate_end;

    if (!sea_capable) {
      ruleset_error(LOG_ERROR,
                    "Ferryboat (%s) needs to be able to move at sea.",
                    utype_rule_name(u));
      return FALSE;
    }
  }

  if (num_role_units(L_PARTISAN) == 0
      && effect_cumulative_max(EFT_INSPIRE_PARTISANS, NULL) > 0) {
    ruleset_error(LOG_ERROR,
                  "Inspire_Partisans effect present, but no units with partisan role.");
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Sanity check buildings
**************************************************************************/
static bool rs_buildings(void)
{
  /* Special Genus */
  improvement_iterate(pimprove) {
    if (improvement_has_flag(pimprove, IF_GOLD)
        && pimprove->genus != IG_SPECIAL) {
      ruleset_error(LOG_ERROR,
                    "Gold producing improvement %s with genus other than \"Special\"",
                    improvement_rule_name(pimprove));

      return FALSE;
    }

    if (is_wonder(pimprove) && pimprove->upkeep != 0) {
      /* Not treating this as a hard error for the sake of the rulesets
       * already in the wild. */
      log_deprecation("Ignoring nonzero upkeep value of %s as it's a wonder",
                      improvement_rule_name(pimprove));
      pimprove->upkeep = 0; /* Forced also in city_improvement_upkeep() */
    }
  } improvement_iterate_end;

  return TRUE;
}

/**************************************************************************
  Check that boolean effect types have sensible effects.
**************************************************************************/
static bool sanity_check_boolean_effects(void)
{
  enum effect_type boolean_effects[] =
    {
      EFT_ANY_GOVERNMENT,
      EFT_CAPITAL_CITY,
      EFT_ENABLE_NUKE,
      EFT_ENABLE_SPACE,
      EFT_HAVE_EMBASSIES,
      EFT_NO_ANARCHY,
      EFT_NUKE_PROOF,
      EFT_REVEAL_CITIES,
      EFT_REVEAL_MAP,
      EFT_SIZE_UNLIMIT,
      EFT_SS_STRUCTURAL,
      EFT_SS_COMPONENT,
      EFT_NO_UNHAPPY,
      EFT_RAPTURE_GROW,
      EFT_HAS_SENATE,
      EFT_INSPIRE_PARTISANS,
      EFT_HAPPINESS_TO_GOLD,
      EFT_FANATICS,
      EFT_NO_DIPLOMACY,
      EFT_IRRIG_POSSIBLE,
      EFT_GOV_CENTER,
      EFT_TRANSFORM_POSSIBLE,
      EFT_MINING_POSSIBLE,
      EFT_IRRIG_TF_POSSIBLE,
      EFT_MINING_TF_POSSIBLE,
      EFT_NOT_TECH_SOURCE,
      EFT_VICTORY,
      EFT_COUNT
    };
  int i;
  bool ret = TRUE;

  for (i = 0; boolean_effects[i] != EFT_COUNT; i++) {
    if (effect_cumulative_min(boolean_effects[i], NULL) < 0
        && effect_cumulative_max(boolean_effects[i], NULL) == 0) {
      ruleset_error(LOG_ERROR, "Boolean effect %s can get disabled, but it can't get "
                    "enabled before that.", effect_type_name(boolean_effects[i]));
      ret = FALSE;
    }
  }

  return ret;
}

/**************************************************************************
  Some more sanity checking once all rulesets are loaded. These check
  for some cross-referencing which was impossible to do while only one
  party was loaded in load_ruleset_xxx()

  Returns TRUE iff everything ok.
**************************************************************************/
bool sanity_check_ruleset_data(void)
{
  int num_utypes;
  int i;
  bool ok = TRUE; /* Store failures to variable instead of returning
                   * immediately so all errors get printed, not just first
                   * one. */
  bool default_gov_failed = FALSE;
  bool obsoleted_by_loop = FALSE;

  if (!sanity_check_metadata()) {
    ok = FALSE;
  }

  if (game.info.tech_cost_style == TECH_COST_CIV1CIV2
      && game.info.free_tech_method == FTM_CHEAPEST) {
    ruleset_error(LOG_ERROR, "Cost based free tech method, but tech cost style "
                  "\"Civ I|II\" so all techs cost the same.");
    ok = FALSE;
  }

  /* Advances. */
  advance_iterate(A_FIRST, padvance) {
    for (i = AR_ONE; i < AR_SIZE; i++) {
      const struct advance *preq;

      if (i == AR_ROOT) {
        /* Self rootreq is a feature. */
        continue;
      }

      preq = advance_requires(padvance, i);

      if (A_NEVER == preq) {
        continue;
      } else if (preq == padvance) {
        ruleset_error(LOG_ERROR, "Tech \"%s\" requires itself.",
                      advance_rule_name(padvance));
        ok = FALSE;
        continue;
      }

      advance_req_iterate(preq, preqreq) {
        if (preqreq == padvance) {
          ruleset_error(LOG_ERROR,
                        "Tech \"%s\" requires itself indirectly via \"%s\".",
                        advance_rule_name(padvance),
                        advance_rule_name(preq));
          ok = FALSE;
        }
      } advance_req_iterate_end;
    }

    if (padvance->bonus_message != NULL) {
      if (!formats_match(padvance->bonus_message, "%s")) {
        ruleset_error(LOG_ERROR,
                      "Tech \"%s\" bonus message is not format with %%s for a bonus tech name.",
                      advance_rule_name(padvance));
        ok = FALSE;
      }
    }
  } advance_iterate_end;

  if (game.default_government == game.government_during_revolution) {
    ruleset_error(LOG_ERROR,
                  "The government form %s reserved for revolution handling has been set as "
                  "default_government.",
                  government_rule_name(game.government_during_revolution));
    ok = FALSE;
    default_gov_failed = TRUE;
  }

  /* Check that all players can have their initial techs */
  nations_iterate(pnation) {
    int techi;

    /* Check global initial techs */
    for (techi = 0; techi < MAX_NUM_TECH_LIST
         && game.rgame.global_init_techs[techi] != A_LAST; techi++) {
      Tech_type_id tech = game.rgame.global_init_techs[techi];
      struct advance *a = valid_advance_by_number(tech);

      if (a == NULL) {
        ruleset_error(LOG_ERROR,
                      "Tech %s does not exist, but is initial "
                      "tech for everyone.",
                      advance_rule_name(advance_by_number(tech)));
        ok = FALSE;
      } else if (advance_by_number(A_NONE) != a->require[AR_ROOT]
          && !nation_has_initial_tech(pnation, a->require[AR_ROOT])) {
        /* Nation has no root_req for tech */
        ruleset_error(LOG_ERROR,
                      "Tech %s is initial for everyone, but %s has "
                      "no root_req for it.",
                      advance_rule_name(a),
                      nation_rule_name(pnation));
        ok = FALSE;
      }
    }

    /* Check national initial techs */
    for (techi = 0;
         techi < MAX_NUM_TECH_LIST && pnation->init_techs[techi] != A_LAST;
         techi++) {
      Tech_type_id tech = pnation->init_techs[techi];
      struct advance *a = valid_advance_by_number(tech);

      if (a == NULL) {
        ruleset_error(LOG_ERROR,
                      "Tech %s does not exist, but is initial tech for %s.",
                      advance_rule_name(advance_by_number(tech)),
                      nation_rule_name(pnation));
        ok = FALSE;
      } else if (advance_by_number(A_NONE) != a->require[AR_ROOT]
          && !nation_has_initial_tech(pnation, a->require[AR_ROOT])) {
        /* Nation has no root_req for tech */
        ruleset_error(LOG_ERROR,
                      "Tech %s is initial for %s, but they have "
                      "no root_req for it.",
                      advance_rule_name(a),
                      nation_rule_name(pnation));
        ok = FALSE;
      }
    }

    /* Check national initial buildings */
    if (nation_barbarian_type(pnation) != NOT_A_BARBARIAN
        && pnation->init_buildings[0] != B_LAST) {
      ruleset_error(LOG_ERROR,
                    "Barbarian nation %s has init_buildings set but will "
                    "never see them", nation_rule_name(pnation));
    }

    if (!default_gov_failed && pnation->init_government == game.government_during_revolution) {
      ruleset_error(LOG_ERROR,
                    "The government form %s reserved for revolution handling has been set as "
                    "initial government for %s.",
                    government_rule_name(game.government_during_revolution),
                    nation_rule_name(pnation));
      ok = FALSE;
    }
  } nations_iterate_end;

  /* Check against unit upgrade loops */
  num_utypes = game.control.num_unit_types;
  unit_type_iterate(putype) {
    int chain_length = 0;
    struct unit_type *upgraded = putype;

    while (upgraded != NULL && !obsoleted_by_loop) {
      upgraded = upgraded->obsoleted_by;
      chain_length++;
      if (chain_length > num_utypes) {
        ruleset_error(LOG_ERROR,
                      "There seems to be obsoleted_by loop in update "
                      "chain that starts from %s", utype_rule_name(putype));
        ok = FALSE;
        obsoleted_by_loop = TRUE;
      }
    }
  } unit_type_iterate_end;

  /* Some unit type properties depend on other unit type properties to work
   * properly. */
  unit_type_iterate(putype) {
    /* "Spy" is a better "Diplomat". Until all the places that assume that
     * "Diplomat" is set if "Spy" is set is changed this limitation must be
     * kept. */
    if (utype_has_flag(putype, UTYF_SPY)
        && !utype_has_flag(putype, UTYF_DIPLOMAT)) {
      ruleset_error(LOG_ERROR,
                    "The unit type '%s' has the 'Spy' unit type flag but "
                    "not the 'Diplomat' unit type flag.",
                    utype_rule_name(putype));
      ok = FALSE;
    }

    /* The ability to bombard requires a bombard_rate. */
    if (utype_has_flag(putype, UTYF_BOMBARDER)
        && putype->bombard_rate <= 0) {
      ruleset_error(LOG_ERROR,
                    "The unit type '%s' has the 'Bombarder' unit type flag"
                    " but no bombard_rate is set.",
                    utype_rule_name(putype));
      ok = FALSE;
    }
  } unit_type_iterate_end;

  /* Check that unit type fields are in range. */
  unit_type_iterate(putype) {
    if (putype->paratroopers_range < 0
        || putype->paratroopers_range > UNIT_MAX_PARADROP_RANGE) {
      /* Paradrop range is limited by the network protocol. */
      ruleset_error(LOG_ERROR,
                    "The paratroopers_range of the unit type '%s' is %d. "
                    "That is out of range. Max range is %d.",
                    utype_rule_name(putype),
                    putype->paratroopers_range, UNIT_MAX_PARADROP_RANGE);
      ok = FALSE;
    }
  } unit_type_iterate_end;

  /* Check requirement sets against conflicting requirements.
   * Effects use requirement lists */
  if (!iterate_effect_cache(effect_list_sanity_cb, NULL)) {
    ruleset_error(LOG_ERROR,
                  "Effects have conflicting or invalid requirements!");
    ok = FALSE;
  }

  if (!sanity_check_boolean_effects()) {
    ok = FALSE;
  }

  /* Others use requirement vectors */

  /* Disasters */
  disaster_type_iterate(pdis) {
    if (!sanity_check_req_vec(&pdis->reqs, TRUE, -1,
                              disaster_rule_name(pdis))) {
      ruleset_error(LOG_ERROR,
                    "Disasters have conflicting or invalid requirements!");
      ok = FALSE;
    }
  } disaster_type_iterate_end;

  /* Buildings */
  improvement_iterate(pimprove) {
    if (!sanity_check_req_vec(&pimprove->reqs, TRUE, -1,
                              improvement_rule_name(pimprove))) {
      ruleset_error(LOG_ERROR,
                    "Buildings have conflicting or invalid requirements!");
      ok = FALSE;
    }
    if (!sanity_check_req_vec(&pimprove->obsolete_by, FALSE, -1,
                              improvement_rule_name(pimprove))) {
      ruleset_error(LOG_ERROR,
                    "Buildings have conflicting or invalid obsolescence req!");
      ok = FALSE;
    }
  } improvement_iterate_end;

  /* Governments */
  governments_iterate(pgov) {
    if (!sanity_check_req_vec(&pgov->reqs, TRUE, -1,
                              government_rule_name(pgov))) {
      ruleset_error(LOG_ERROR,
                    "Governments have conflicting or invalid requirements!");
      ok = FALSE;
    }
  } governments_iterate_end;

  /* Specialists */
  specialist_type_iterate(sp) {
    struct specialist *psp = specialist_by_number(sp);

    if (!sanity_check_req_vec(&psp->reqs, TRUE, -1,
                              specialist_rule_name(psp))) {
      ruleset_error(LOG_ERROR,
                    "Specialists have conflicting or invalid requirements!");
      ok = FALSE;
    }
  } specialist_type_iterate_end;

  /* Extras */
  extra_type_iterate(pextra) {
    if (!sanity_check_req_vec(&pextra->reqs, TRUE, -1,
                              extra_rule_name(pextra))) {
      ruleset_error(LOG_ERROR,
                    "Extras have conflicting or invalid requirements!");
      ok = FALSE;
    }
    if (!sanity_check_req_vec(&pextra->rmreqs, TRUE, -1,
                              extra_rule_name(pextra))) {
      ruleset_error(LOG_ERROR,
                    "Extras have conflicting or invalid removal requirements!");
      ok = FALSE;
    }
  } extra_type_iterate_end;

  /* Roads */
  extra_type_by_cause_iterate(EC_ROAD, pextra) {
    struct road_type *proad = extra_road_get(pextra);

    extra_type_list_iterate(proad->integrators, iextra) {
      struct road_type *iroad = extra_road_get(iextra);

      if (road_index(proad) != road_index(iroad)
          && !BV_ISSET(iroad->integrates, road_index(proad))) {
        /* We don't support non-symmetric integrator relationships yet. */
        ruleset_error(LOG_ERROR,
                      "Road '%s' integrates with '%s' but not vice versa!",
                      extra_rule_name(pextra),
                      extra_rule_name(iextra));
        ok = FALSE;
      }
    } extra_type_list_iterate_end;
  } extra_type_by_cause_iterate_end;

  /* City styles */
  for (i = 0; i < game.control.styles_count; i++) {
    if (!sanity_check_req_vec(&city_styles[i].reqs, TRUE, -1,
                              city_style_rule_name(i))) {
      ruleset_error(LOG_ERROR,
                    "City styles have conflicting or invalid requirements!");
      ok = FALSE;
    }
  }

  /* Action enablers */
  action_iterate(act) {
    action_enabler_list_iterate(action_enablers_for_action(act), enabler) {
      if (!sanity_check_req_vec(&(enabler->actor_reqs), TRUE, -1,
                                "Action Enabler Actor Reqs")
          || !sanity_check_req_vec(&(enabler->target_reqs), TRUE, -1,
                                   "Action Enabler Target Reqs")) {
        ruleset_error(LOG_ERROR,
                      "Action enabler for %s has conflicting or invalid "
                      "requirements!", action_id_rule_name(act));
        ok = FALSE;
      }

      requirement_vector_iterate(&(enabler->target_reqs), preq) {
        if (preq->source.kind == VUT_DIPLREL
            && preq->range == REQ_RANGE_LOCAL) {
          struct astring astr;

          /* A Local DiplRel requirement can be expressed as a requirement
           * in actor_reqs. Demand that it is there. This avoids breaking
           * code that reasons about actions. */
          ruleset_error(LOG_ERROR,
                        "Action enabler for %s has a local DiplRel "
                        "requirement %s in target_reqs! Please read the "
                        "section \"Requirement vector rules\" in "
                        "doc/README.actions",
                        action_id_rule_name(act),
                        req_to_fstring(preq, &astr));
          astr_free(&astr);
          ok = FALSE;
        }
      } requirement_vector_iterate_end;
    } action_enabler_list_iterate_end;
  } action_iterate_end;

  /* There must be basic city style for each nation style to start with */
  styles_iterate(pstyle) {
    if (basic_city_style_for_style(pstyle) < 0) {
      ruleset_error(LOG_ERROR,
                    "There's no basic city style for nation style %s",
                    style_rule_name(pstyle));
      ok = FALSE;
    }
  } styles_iterate_end;

  /* Music styles */
  music_styles_iterate(pmus) {
    if (!sanity_check_req_vec(&pmus->reqs, TRUE, -1, "Music Style")) {
      ruleset_error(LOG_ERROR,
                    "Music Styles have conflicting or invalid requirements!");
      ok = FALSE;
    }
  } music_styles_iterate_end;

  terrain_type_iterate(pterr) {
    if (pterr->animal != NULL) {
      if (!is_native_to_class(utype_class(pterr->animal), pterr, NULL)) {
        ruleset_error(LOG_ERROR,
                      "%s has %s as animal to appear, but it's not native to the terrain.",
                      terrain_rule_name(pterr), utype_rule_name(pterr->animal));
        ok = FALSE;
      }
    }
  } terrain_type_iterate_end;

  /* Check that all unit classes can exist somewhere */
  unit_class_iterate(pclass) {
    if (!uclass_has_flag(pclass, UCF_BUILD_ANYWHERE)) {
      bool can_exist = FALSE;

      terrain_type_iterate(pterr) {
        if (BV_ISSET(pterr->native_to, uclass_index(pclass))) {
          can_exist = TRUE;
          break;
        }
      } terrain_type_iterate_end;

      if (!can_exist) {
        extra_type_iterate(pextra) {
          if (BV_ISSET(pextra->native_to, uclass_index(pclass))
              && extra_has_flag(pextra, EF_NATIVE_TILE)) {
            can_exist = TRUE;
            break;
          }
        } extra_type_iterate_end;
      }

      if (!can_exist) {
        ruleset_error(LOG_ERROR,
                      "Unit class %s cannot exist anywhere.",
                      uclass_rule_name(pclass));
        ok = FALSE;
      }
    }
  } unit_class_iterate_end;

  achievements_iterate(pach) {
    if (!pach->unique && pach->cons_msg == NULL) {
      ruleset_error(LOG_ERROR,
                    "Achievement %s has no message for consecutive gainers though "
                    "it's possible to be gained by multiple players",
                    achievement_rule_name(pach));
      ok = FALSE;
    }
  } achievements_iterate_end;

  if (game.server.ruledit.embedded_nations != NULL) {
    int nati;

    for (nati = 0; nati < game.server.ruledit.embedded_nations_count; nati++) {
      struct nation_type *pnat
        = nation_by_rule_name(game.server.ruledit.embedded_nations[nati]);

      if (pnat == NULL) {
        ruleset_error(LOG_ERROR,
                      "There's nation %s listed in embedded nations, but there's "
                      "no such nation.",
                      game.server.ruledit.embedded_nations[nati]);
        ok = FALSE;
      }
    }
  }

  if (ok) {
    ok = rs_common_units();
  }
  if (ok) {
    ok = rs_barbarian_units();
  }
  if (ok) {
    ok = rs_buildings();
  }

  return ok;
}

/**************************************************************************
  Set and lock settings that must have certain value.
**************************************************************************/
bool autolock_settings(void)
{
  bool ok = TRUE;

  if (num_role_units(L_BARBARIAN) == 0) {
    struct setting *pset = setting_by_name("barbarians");

    log_normal(_("Disabling 'barbarians' setting for lack of suitable "
                 "unit types."));
    setting_lock_set(pset, FALSE);
    if (!setting_enum_set(pset, "DISABLED", NULL, NULL, 0)) {
      ok = FALSE;
    }
    setting_lock_set(pset, TRUE);
  }

  return ok;
}
