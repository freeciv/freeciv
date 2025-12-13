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
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"     /* ARRAY_SIZE */
#include "string_vector.h"
#include "support.h"

/* common */
#include "game.h"
#include "map.h"
#include "tech.h"
#include "victory.h"

#include "improvement.h"

/**************************************************************************
All the city improvements:
Use improvement_by_number(id) to access the array.
The improvement_types array is now setup in:
   server/ruleset.c (for the server)
   client/packhand.c (for the client)
**************************************************************************/
static struct impr_type improvement_types[B_LAST];

static struct user_flag user_impr_flags[MAX_NUM_USER_BUILDING_FLAGS];

/**********************************************************************//**
  Initialize building structures.
**************************************************************************/
void improvements_init(void)
{
  int i;

  /* Can't use improvement_iterate() or improvement_by_number() here
   * because num_impr_types isn't known yet. */
  for (i = 0; i < ARRAY_SIZE(improvement_types); i++) {
    struct impr_type *p = &improvement_types[i];

    p->item_number = i;
    requirement_vector_init(&p->reqs);
    requirement_vector_init(&p->obsolete_by);
    p->ruledit_disabled = FALSE;
    p->ruledit_dlg = nullptr;
  }
}

/**********************************************************************//**
  Frees the memory associated with this improvement.
**************************************************************************/
static void improvement_free(struct impr_type *p)
{
  if (p->helptext != nullptr) {
    strvec_destroy(p->helptext);
    p->helptext = nullptr;
  }

  requirement_vector_free(&p->reqs);
  requirement_vector_free(&p->obsolete_by);
}

/**********************************************************************//**
 Frees the memory associated with all improvements.
**************************************************************************/
void improvements_free(void)
{
  improvement_iterate(p) {
    improvement_free(p);
  } improvement_iterate_end;
}

/**********************************************************************//**
  Cache features of the improvement
**************************************************************************/
void improvement_feature_cache_init(void)
{
  improvement_iterate(pimprove) {
    pimprove->allows_units = FALSE;
    unit_type_iterate(putype) {
      if (requirement_needs_improvement(pimprove, &putype->build_reqs)) {
        pimprove->allows_units = TRUE;
        break;
      }
    } unit_type_iterate_end;

    pimprove->allows_extras = FALSE;
    extra_type_iterate(pextra) {
      if (requirement_needs_improvement(pimprove, &pextra->reqs)) {
        pimprove->allows_extras = TRUE;
        break;
      }
    } extra_type_iterate_end;

    pimprove->prevents_disaster = FALSE;
    disaster_type_iterate(pdis) {
      if (!requirement_fulfilled_by_improvement(pimprove, &pdis->reqs)) {
        pimprove->prevents_disaster = TRUE;
        break;
      }
    } disaster_type_iterate_end;

    pimprove->protects_vs_actions = FALSE;
    action_enablers_iterate(act) {
      if (!requirement_fulfilled_by_improvement(pimprove,
                                                &act->target_reqs)) {
        pimprove->protects_vs_actions = TRUE;
        break;
      }
    } action_enablers_iterate_end;

    pimprove->allows_actions = FALSE;
    action_enablers_iterate(act) {
      if (requirement_needs_improvement(pimprove, &act->actor_reqs)) {
        pimprove->allows_actions = TRUE;
        break;
      }
    } action_enablers_iterate_end;

  } improvement_iterate_end;
}

/**********************************************************************//**
  Return the first item of improvements.
**************************************************************************/
struct impr_type *improvement_array_first(void)
{
  if (game.control.num_impr_types > 0) {
    return improvement_types;
  }

  return nullptr;
}

/**********************************************************************//**
  Return the last item of improvements.
**************************************************************************/
const struct impr_type *improvement_array_last(void)
{
  if (game.control.num_impr_types > 0) {
    return &improvement_types[game.control.num_impr_types - 1];
  }

  return nullptr;
}

/**********************************************************************//**
  Return the number of improvements.
**************************************************************************/
Impr_type_id improvement_count(void)
{
  return game.control.num_impr_types;
}

/**********************************************************************//**
  Return the improvement index.

  Currently same as improvement_number(), paired with improvement_count()
  indicates use as an array index.
**************************************************************************/
Impr_type_id improvement_index(const struct impr_type *pimprove)
{
  fc_assert_ret_val(pimprove != nullptr, -1);

  return pimprove - improvement_types;
}

/**********************************************************************//**
  Return the improvement index.
**************************************************************************/
Impr_type_id improvement_number(const struct impr_type *pimprove)
{
  fc_assert_ret_val(pimprove != nullptr, -1);

  return pimprove->item_number;
}

/**********************************************************************//**
  Returns the improvement type for the given index/ID. Returns nullptr for
  an out-of-range index.
**************************************************************************/
struct impr_type *improvement_by_number(const Impr_type_id id)
{
  if (id < 0 || id >= improvement_count()) {
    return nullptr;
  }

  return &improvement_types[id];
}

/**********************************************************************//**
  Returns pointer when the improvement_type "exists" in this game,
  returns nullptr otherwise.

  An improvement_type doesn't exist for any of:
   - the improvement_type has been flagged as removed by setting its
     tech_req to A_LAST; [was not in current 2007-07-27]
   - it is a space part, and the spacerace is not enabled.
**************************************************************************/
const struct impr_type *valid_improvement(const struct impr_type *pimprove)
{
  if (pimprove == nullptr) {
    return nullptr;
  }

  if (!victory_enabled(VC_SPACERACE)
      && (building_has_effect(pimprove, EFT_SS_STRUCTURAL)
	  || building_has_effect(pimprove, EFT_SS_COMPONENT)
	  || building_has_effect(pimprove, EFT_SS_MODULE))) {
    /* This assumes that space parts don't have any other effects. */
    return nullptr;
  }

  return pimprove;
}

/**********************************************************************//**
  Returns pointer when the improvement_type "exists" in this game,
  returns nullptr otherwise.

  In addition to valid_improvement(), tests for id is out of range.
**************************************************************************/
const struct impr_type *valid_improvement_by_number(const Impr_type_id id)
{
  return valid_improvement(improvement_by_number(id));
}

/**********************************************************************//**
  Return the (translated) name of the given improvement.
  You don't have to free the return pointer.
**************************************************************************/
const char *improvement_name_translation(const struct impr_type *pimprove)
{
  return name_translation_get(&pimprove->name);
}

/**********************************************************************//**
  Return the (untranslated) rule name of the improvement.
  You don't have to free the return pointer.
**************************************************************************/
const char *improvement_rule_name(const struct impr_type *pimprove)
{
  return rule_name_get(&pimprove->name);
}

/**********************************************************************//**
  Returns the base number of shields it takes to build this improvement.
  This one does not take city specific bonuses in to account.
**************************************************************************/
int impr_base_build_shield_cost(const struct impr_type *pimprove)
{
  int base = pimprove->build_cost;

  return MAX(base * game.info.shieldbox / 100, 1);
}

/**********************************************************************//**
  Returns estimate of the number of shields it takes to build this improvement.
  pplayer and ptile can be nullptr, but that might reduce quality of the
  estimate.
**************************************************************************/
int impr_estimate_build_shield_cost(const struct player *pplayer,
                                    const struct tile *ptile,
                                    const struct impr_type *pimprove)
{
  int base = pimprove->build_cost
    * (100 +
       get_target_bonus_effects(nullptr,
                                &(const struct req_context) {
                                  .player = pplayer,
                                  .building = pimprove,
                                  .tile = ptile,
                                },
                                nullptr,
                                EFT_IMPR_BUILD_COST_PCT)) / 100;

  return MAX(base * game.info.shieldbox / 100, 1);
}

/**********************************************************************//**
  Returns the number of shields it takes to build this improvement.
**************************************************************************/
int impr_build_shield_cost(const struct city *pcity,
                           const struct impr_type *pimprove)
{
  int base = pimprove->build_cost
    * (100 + get_building_bonus(pcity, pimprove, EFT_IMPR_BUILD_COST_PCT)) / 100;

  return MAX(base * game.info.shieldbox / 100, 1);
}

/**********************************************************************//**
  Returns the amount of gold it takes to rush this improvement.
**************************************************************************/
int impr_buy_gold_cost(const struct city *pcity,
                       const struct impr_type *pimprove, int shields_in_stock)
{
  int cost = 0;
  const int missing = impr_build_shield_cost(pcity, pimprove) - shields_in_stock;

  if (is_convert_improvement(pimprove)) {
    /* Can't buy coinage-like improvements. */
    return 0;
  }

  if (missing > 0) {
    cost = 2 * missing;
  }

  if (shields_in_stock == 0) {
    cost *= 2;
  }

  cost = cost
    * (100 + get_building_bonus(pcity, pimprove, EFT_IMPR_BUY_COST_PCT)) / 100;

  return cost;
}

/**********************************************************************//**
  Returns the amount of gold received when this improvement is sold.
**************************************************************************/
int impr_sell_gold(const struct impr_type *pimprove)
{
  return MAX(pimprove->build_cost * game.info.shieldbox / 100, 1);
}

/**********************************************************************//**
  Returns whether improvement is some kind of wonder. Both great wonders
  and small wonders count.
**************************************************************************/
bool is_wonder(const struct impr_type *pimprove)
{
  return (is_great_wonder(pimprove) || is_small_wonder(pimprove));
}

/**********************************************************************//**
  Does a linear search of improvement_types[].name.translated
  Returns nullptr when none match.
**************************************************************************/
struct impr_type *improvement_by_translated_name(const char *name)
{
  improvement_iterate(pimprove) {
    if (0 == strcmp(improvement_name_translation(pimprove), name)) {
      return pimprove;
    }
  } improvement_iterate_end;

  return nullptr;
}

/**********************************************************************//**
  Does a linear search of improvement_types[].name.vernacular
  Returns nullptr when none match.
**************************************************************************/
struct impr_type *improvement_by_rule_name(const char *name)
{
  const char *qname = Qn_(name);

  improvement_iterate(pimprove) {
    if (0 == fc_strcasecmp(improvement_rule_name(pimprove), qname)) {
      return pimprove;
    }
  } improvement_iterate_end;

  return nullptr;
}

/**********************************************************************//**
  Return TRUE if the impr has this flag, otherwise FALSE
**************************************************************************/
bool improvement_has_flag(const struct impr_type *pimprove,
			  enum impr_flag_id flag)
{
  fc_assert_ret_val(impr_flag_id_is_valid(flag), FALSE);

  return BV_ISSET(pimprove->flags, flag);
}

/**********************************************************************//**
  Return TRUE if the improvement should be visible to others without spying
**************************************************************************/
bool is_improvement_visible(const struct impr_type *pimprove)
{
  return (is_wonder(pimprove)
          || improvement_has_flag(pimprove, IF_VISIBLE_BY_OTHERS));
}

/**********************************************************************//**
  Return TRUE if the improvement can ever go obsolete.
  Can be used for buildings or wonders.
**************************************************************************/
bool can_improvement_go_obsolete(const struct impr_type *pimprove)
{
  return requirement_vector_size(&pimprove->obsolete_by) > 0;
}

/**********************************************************************//**
  Returns TRUE if the improvement or wonder is obsolete
**************************************************************************/
bool improvement_obsolete(const struct player *pplayer,
			  const struct impr_type *pimprove,
                          const struct city *pcity)
{
  const struct req_context context = {
    .player = pplayer,
    .city = pcity,
    .tile = pcity ? city_tile(pcity) : nullptr,
    .building = pimprove,
  };

  requirement_vector_iterate(&pimprove->obsolete_by, preq) {
    if (is_req_active(&context, nullptr, preq, RPT_CERTAIN)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff improvement provides units buildable in city
**************************************************************************/
static bool impr_provides_buildable_units(const struct city *pcity,
                                          const struct impr_type *pimprove)
{
  const struct civ_map *nmap = &(wld.map);

  /* Fast check */
  if (!pimprove->allows_units) {
    return FALSE;
  }

  unit_type_iterate(ut) {
    if (requirement_needs_improvement(pimprove, &ut->build_reqs)
        && can_city_build_unit_now(nmap, pcity, ut)) {
      return TRUE;
    }
  } unit_type_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff improvement provides extras buildable in city
**************************************************************************/
static bool impr_provides_buildable_extras(const struct city *pcity,
                                           const struct impr_type *pimprove)
{
  const struct civ_map *nmap = &(wld.map);

  /* Fast check */
  if (!pimprove->allows_extras) {
    return FALSE;
  }

  extra_type_iterate(pextra) {
    if (requirement_needs_improvement(pimprove, &pextra->reqs)) {
      city_tile_iterate(nmap, city_map_radius_sq_get(pcity),
                        city_tile(pcity), ptile) {
        if (player_can_build_extra(pextra, city_owner(pcity), ptile)) {
          return TRUE;
        }
      } city_tile_iterate_end;
    }
  } extra_type_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff improvement prevents a disaster in city
**************************************************************************/
static bool impr_prevents_disaster(const struct city *pcity,
                                   const struct impr_type *pimprove)
{
  /* Fast check */
  if (!pimprove->prevents_disaster) {
    return FALSE;
  }

  disaster_type_iterate(pdis) {
    if (!requirement_fulfilled_by_improvement(pimprove, &pdis->reqs)
        && !can_disaster_happen(pdis, pcity)) {
      return TRUE;
    }
  } disaster_type_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff improvement protects against an action on the city
  FIXME: This is prone to false positives: for example, if one requires
         a special tech or unit to perform an action, and no other player
         has or can gain that tech or unit, protection is still claimed.
**************************************************************************/
static bool impr_protects_vs_actions(const struct city *pcity,
                                     const struct impr_type *pimprove)
{
  /* Fast check */
  if (!pimprove->protects_vs_actions) {
    return FALSE;
  }

  action_enablers_iterate(enabler) {
    if (!requirement_fulfilled_by_improvement(pimprove, &enabler->target_reqs)
        && !is_action_possible_on_city(enabler_get_action_id(enabler),
                                       nullptr, pcity)) {
      return TRUE;
    }
  } action_enablers_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff improvement allows its owner to perform an action.
**************************************************************************/
static bool impr_allows_actions(const struct city *pcity,
                                const struct impr_type *pimprove)
{
  const struct civ_map *nmap = &(wld.map);

  /* Fast check */
  if (!pimprove->allows_actions) {
    return FALSE;
  }

  action_enablers_iterate(enabler) {
    if (requirement_needs_improvement(pimprove, &enabler->actor_reqs)) {
      action_id act = enabler_get_action_id(enabler);

      switch (action_id_get_actor_kind(act)) {
      case AAK_UNIT:
        unit_type_iterate(ut) {
          if (!utype_can_do_action(ut, act)) {
            /* Not relevant */
            continue;
          }

          if (utype_player_already_has_this(city_owner(pcity), ut)) {
            /* The player has a unit that may use the building */
            return TRUE;
          }

          if (can_city_build_unit_now(nmap, pcity, ut)) {
            /* This city can build a unit that uses the building */
            return TRUE;
          }
        } unit_type_iterate_end;
        break;
      case AAK_CITY:
      case AAK_PLAYER:
        /* No traceable limitations */
        return TRUE;
      case AAK_COUNT:
        fc_assert(action_id_get_actor_kind(act) != AAK_COUNT);
        break;
      }
    }
  } action_enablers_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Check if an improvement has side effects for a city.  Side effects
  are any benefits that accrue that are not tracked by the effects
  system.

  Note that this function will always return FALSE if the improvement does
  not currently provide a benefit to the city (for example, if the improvement
  has not yet been built, or another city benefits from the improvement in
  this city (i.e. Wonders)).
**************************************************************************/
static bool improvement_has_side_effects(const struct city *pcity,
                                         const struct impr_type *pimprove)
{
    return (impr_provides_buildable_units(pcity, pimprove)
            || impr_provides_buildable_extras(pcity, pimprove)
            || impr_prevents_disaster(pcity, pimprove)
            || impr_allows_actions(pcity, pimprove)
            || impr_protects_vs_actions(pcity, pimprove));
}

/**********************************************************************//**
  Returns TRUE iff improvement provides some effect (good or bad).
**************************************************************************/
static bool improvement_has_effects(const struct city *pcity,
                                    const struct impr_type *pimprove)
{
  struct universal source = { .kind = VUT_IMPROVEMENT,
                              .value = { .building = pimprove } };
  struct effect_list *plist = get_req_source_effects(&source);

  if (!plist || improvement_obsolete(city_owner(pcity), pimprove, pcity)) {
    return FALSE;
  }

  effect_list_iterate(plist, peffect) {
    if (0 != get_potential_improvement_bonus(pimprove, pcity,
                                             peffect->type, RPT_CERTAIN, TRUE)) {
      return TRUE;
    }
  } effect_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if an improvement in a city is productive, in some way.

  Note that unproductive improvements may become productive later, if
  appropriate conditions are met (e.g. a special building that isn't
  producing units under the current government might under another).
**************************************************************************/
bool is_improvement_productive(const struct city *pcity,
                               const struct impr_type *pimprove)
{
    return (!improvement_obsolete(city_owner(pcity), pimprove, pcity)
            && (improvement_has_flag(pimprove, IF_GOLD)
                || improvement_has_flag(pimprove, IF_INFRA)
                || improvement_has_side_effects(pcity, pimprove)
                || improvement_has_effects(pcity, pimprove)));
}

/**********************************************************************//**
  Returns TRUE if an improvement in a city is redundant, that is, the
  city wouldn't lose anything by losing the improvement, or gain anything
  by building it. This means:
   - all of its effects (if any) are provided by other means, or it's
     obsolete (and thus assumed to have no effect); and
   - it's not enabling the city to build some kind of units; and
   - it's not convert production (IF_GOLD or IF_INFRA).
  (Note that it's not impossible that this improvement could become useful
  if circumstances changed, say if a new government enabled the building
  of its special units.)
**************************************************************************/
bool is_improvement_redundant(const struct city *pcity,
                              const struct impr_type *pimprove)
{
  /* A capitalization production is never redundant. */
  if (improvement_has_flag(pimprove, IF_GOLD)) {
    return FALSE;
  }
  if (improvement_has_flag(pimprove, IF_INFRA)) {
    return FALSE;
  }

  /* If an improvement has side effects, don't claim it's redundant. */
  if (improvement_has_side_effects(pcity, pimprove)) {
    return FALSE;
  }

  /* Otherwise, it's redundant if its effects are available by other means,
   * or if it's an obsolete wonder (great or small). */
  return is_building_replaced(pcity, pimprove, RPT_CERTAIN)
    || improvement_obsolete(city_owner(pcity), pimprove, pcity);
}

/**********************************************************************//**
   Whether player can build given building somewhere, ignoring whether it
   is obsolete.
**************************************************************************/
bool can_player_build_improvement_direct(const struct player *p,
                                         const struct impr_type *pimprove)
{
  const struct req_context context = { .player = p };

  bool space_part = FALSE;

  if (!valid_improvement(pimprove)) {
    return FALSE;
  }

  requirement_vector_iterate(&pimprove->reqs, preq) {
    if (preq->range >= REQ_RANGE_PLAYER
        && !is_req_active(&context, nullptr, preq, RPT_CERTAIN)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;

  /* Check for space part construction.  This assumes that space parts have
   * no other effects. */
  if (building_has_effect(pimprove, EFT_SS_STRUCTURAL)) {
    space_part = TRUE;
    if (p->spaceship.structurals >= NUM_SS_STRUCTURALS) {
      return FALSE;
    }
  }
  if (building_has_effect(pimprove, EFT_SS_COMPONENT)) {
    space_part = TRUE;
    if (p->spaceship.components >= NUM_SS_COMPONENTS) {
      return FALSE;
    }
  }
  if (building_has_effect(pimprove, EFT_SS_MODULE)) {
    space_part = TRUE;
    if (p->spaceship.modules >= NUM_SS_MODULES) {
      return FALSE;
    }
  }
  if (space_part
      && (get_player_bonus(p, EFT_ENABLE_SPACE) <= 0
          || p->spaceship.state >= SSHIP_LAUNCHED)) {
    return FALSE;
  }

  if (is_great_wonder(pimprove)) {
    /* Can't build wonder if already built */
    if (!great_wonder_is_available(pimprove)) {
      return FALSE;
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Whether player can build given building somewhere immediately.
  Returns FALSE if building is obsolete.
**************************************************************************/
bool can_player_build_improvement_now(const struct player *p,
				      struct impr_type *pimprove)
{
  if (!can_player_build_improvement_direct(p, pimprove)) {
    return FALSE;
  }
  if (improvement_obsolete(p, pimprove, nullptr)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Whether player can _eventually_ build given building somewhere -- i.e.,
  returns TRUE if building is available with current tech OR will be
  available with future tech. Returns FALSE if building is obsolete.
**************************************************************************/
bool can_player_build_improvement_later(const struct player *p,
                                        const struct impr_type *pimprove)
{
  const struct req_context context = { .player = p };

  if (!valid_improvement(pimprove)) {
    return FALSE;
  }
  if (improvement_obsolete(p, pimprove, nullptr)) {
    return FALSE;
  }
  if (is_great_wonder(pimprove) && !great_wonder_is_available(pimprove)) {
    /* Can't build wonder if already built */
    return FALSE;
  }

  /* Check for requirements that aren't met and that are unchanging (so
   * they can never be met). */
  requirement_vector_iterate(&pimprove->reqs, preq) {
    if (preq->range >= REQ_RANGE_PLAYER
        && is_req_preventing(&context, nullptr, preq, RPT_POSSIBLE)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;
  /* FIXME: should check some "unchanging" reqs here - like if there's
   * a nation requirement, we can go ahead and check it now. */

  return TRUE;
}

/**********************************************************************//**
  Is this building a great wonder?
**************************************************************************/
bool is_great_wonder(const struct impr_type *pimprove)
{
  return (pimprove->genus == IG_GREAT_WONDER);
}

/**********************************************************************//**
  Is this building a small wonder?
**************************************************************************/
bool is_small_wonder(const struct impr_type *pimprove)
{
  return (pimprove->genus == IG_SMALL_WONDER);
}

/**********************************************************************//**
  Is this building a regular improvement?
**************************************************************************/
bool is_improvement(const struct impr_type *pimprove)
{
  return (pimprove->genus == IG_IMPROVEMENT);
}

/**********************************************************************//**
  Returns TRUE if this is a "special" improvement. For example, spaceship
  parts and coinage in the default ruleset are considered special.
**************************************************************************/
bool is_special_improvement(const struct impr_type *pimprove)
{
  /* TODO: Find either a new term for traditional "special" improvements
   * (maybe "project"?), or a new umbrella term for all improvements that
   * can't be present in the city as a finished building (including special
   * and convert improvements). */
  return (pimprove->genus == IG_SPECIAL)
      || is_convert_improvement(pimprove);
}

/**********************************************************************//**
  Is this a coinage-like improvement that converts production into some
  other form of output?
**************************************************************************/
bool is_convert_improvement(const struct impr_type *pimprove)
{
  return (pimprove->genus == IG_CONVERT);
}

/**********************************************************************//**
  Build a wonder in the city.
**************************************************************************/
void wonder_built(const struct city *pcity, const struct impr_type *pimprove)
{
  struct player *pplayer;
  int windex = improvement_number(pimprove);

  fc_assert_ret(pcity != nullptr);
  fc_assert_ret(is_wonder(pimprove));

  pplayer = city_owner(pcity);
  pplayer->wonders[windex] = pcity->id;

  if (is_great_wonder(pimprove)) {
    game.info.great_wonder_owners[windex] = player_number(pplayer);
  }
}

/**********************************************************************//**
  Remove a wonder from a city and destroy it if it's a great wonder.  To
  transfer a great wonder, use great_wonder_transfer.
**************************************************************************/
void wonder_destroyed(const struct city *pcity,
                      const struct impr_type *pimprove)
{
  struct player *pplayer;
  int windex = improvement_number(pimprove);

  fc_assert_ret(pcity != nullptr);
  fc_assert_ret(is_wonder(pimprove));

  pplayer = city_owner(pcity);
  fc_assert_ret(pplayer->wonders[windex] == pcity->id);
  pplayer->wonders[windex] = WONDER_LOST;

  if (is_great_wonder(pimprove)) {
    fc_assert_ret(game.info.great_wonder_owners[windex]
                   == player_number(pplayer));
    game.info.great_wonder_owners[windex] = WONDER_DESTROYED;
  }
}

/**********************************************************************//**
  Returns whether the player has lost this wonder after having owned it
  (small or great).
**************************************************************************/
bool wonder_is_lost(const struct player *pplayer,
                    const struct impr_type *pimprove)
{
  fc_assert_ret_val(pplayer != nullptr, FALSE);
  fc_assert_ret_val(is_wonder(pimprove), FALSE);

  return pplayer->wonders[improvement_index(pimprove)] == WONDER_LOST;
}

/**********************************************************************//**
  Returns whether the player is currently in possession of this wonder
  (small or great).
**************************************************************************/
bool wonder_is_built(const struct player *pplayer,
                     const struct impr_type *pimprove)
{
  fc_assert_ret_val(pplayer != nullptr, FALSE);
  fc_assert_ret_val(is_wonder(pimprove), FALSE);

  return WONDER_BUILT(pplayer->wonders[improvement_index(pimprove)]);
}

/**********************************************************************//**
  Get the world city with this wonder (small or great).  This doesn't
  always succeed on the client side, and even when it does, it may
  return an "invisible" city whose members are unexpectedly nullptr;
  take care.
**************************************************************************/
struct city *city_from_wonder(const struct player *pplayer,
                              const struct impr_type *pimprove)
{
  int idx = improvement_index(pimprove);
  int city_id;

  if (idx < 0) {
    return nullptr;
  }

  city_id = pplayer->wonders[idx];

  fc_assert_ret_val(pplayer != nullptr, nullptr);
  fc_assert_ret_val(is_wonder(pimprove), nullptr);

  if (!WONDER_BUILT(city_id)) {
    return nullptr;
  }

#ifdef FREECIV_DEBUG
  if (is_server()) {
    /* On client side, this info is not always known. */
    struct city *pcity = player_city_by_number(pplayer, city_id);

    if (pcity == nullptr) {
      log_error("Player %s (nb %d) has outdated wonder info for "
                "%s (nb %d), it points to city nb %d.",
                player_name(pplayer), player_number(pplayer),
                improvement_rule_name(pimprove),
                improvement_number(pimprove), city_id);
    } else if (!city_has_building(pcity, pimprove)) {
      log_error("Player %s (nb %d) has outdated wonder info for "
                "%s (nb %d), the city %s (nb %d) doesn't have this wonder.",
                player_name(pplayer), player_number(pplayer),
                improvement_rule_name(pimprove),
                improvement_number(pimprove), city_name_get(pcity), pcity->id);
      return nullptr;
    }

    return pcity;
  }
#endif /* FREECIV_DEBUG */

  return player_city_by_number(pplayer, city_id);
}

/**********************************************************************//**
  Can the player see wonder owned by the other player?

  Embassy can be passed as parameter so that caller can make the somewhat
  costly embassy detection just once even when calling this multiple
  times (e.g. for number of different wonders)
**************************************************************************/
bool wonder_visible_to_player(const struct impr_type *wonder,
                              const struct player *pplayer,
                              const struct player *owner,
                              enum fc_tristate embassy)
{
  if (pplayer == owner) {
    /* Can see all own wonders,
     * even improvements if that matters. */
    return TRUE;
  }

  if (is_great_wonder(wonder)) {
    return TRUE;
  }

  if (is_small_wonder(wonder)) {
    switch (game.info.small_wonder_visibility) {
    case WV_ALWAYS:
      return TRUE;
    case WV_NEVER:
      return FALSE;
    case WV_EMBASSY:
      return embassy == TRI_YES
        || (embassy == TRI_MAYBE && team_has_embassy(pplayer->team, owner));
    }

    fc_assert(FALSE);

    return FALSE;
  }

  /* Now a wonder, but regular improvement */
  return FALSE;
}

/**********************************************************************//**
  Returns whether this wonder is currently built.
**************************************************************************/
bool great_wonder_is_built(const struct impr_type *pimprove)
{
  fc_assert_ret_val(is_great_wonder(pimprove), FALSE);

  return WONDER_OWNED(game.info.great_wonder_owners
                      [improvement_index(pimprove)]);
}

/**********************************************************************//**
  Returns whether this wonder has been destroyed.
**************************************************************************/
bool great_wonder_is_destroyed(const struct impr_type *pimprove)
{
  fc_assert_ret_val(is_great_wonder(pimprove), FALSE);

  return (WONDER_DESTROYED
          == game.info.great_wonder_owners[improvement_index(pimprove)]);
}

/**********************************************************************//**
  Returns whether this wonder can be currently built.
**************************************************************************/
bool great_wonder_is_available(const struct impr_type *pimprove)
{
  fc_assert_ret_val(is_great_wonder(pimprove), FALSE);

  return (WONDER_NOT_OWNED
          == game.info.great_wonder_owners[improvement_index(pimprove)]);
}

/**********************************************************************//**
  Get the world city with this great wonder.  This doesn't always success
  on the client side.
**************************************************************************/
struct city *city_from_great_wonder(const struct impr_type *pimprove)
{
  int player_id = game.info.great_wonder_owners[improvement_index(pimprove)];

  fc_assert_ret_val(is_great_wonder(pimprove), nullptr);

  if (WONDER_OWNED(player_id)) {
#ifdef FREECIV_DEBUG
    const struct player *pplayer = player_by_number(player_id);
    struct city *pcity = city_from_wonder(pplayer, pimprove);

    if (is_server() && pcity == nullptr) {
      log_error("Game has outdated wonder info for %s (nb %d), "
                "the player %s (nb %d) doesn't have this wonder.",
                improvement_rule_name(pimprove),
                improvement_number(pimprove),
                player_name(pplayer), player_number(pplayer));
    }

    return pcity;
#else
    return city_from_wonder(player_by_number(player_id), pimprove);
#endif /* FREECIV_DEBUG */
  } else {
    return nullptr;
  }
}

/**********************************************************************//**
  Get the player owning this small wonder.  This doesn't always success on
  the client side.
**************************************************************************/
struct player *great_wonder_owner(const struct impr_type *pimprove)
{
  int player_id = game.info.great_wonder_owners[improvement_index(pimprove)];

  fc_assert_ret_val(is_great_wonder(pimprove), nullptr);

  if (WONDER_OWNED(player_id)) {
    return player_by_number(player_id);
  } else {
    return nullptr;
  }
}

/**********************************************************************//**
  Returns whether the player has built this small wonder.
**************************************************************************/
bool small_wonder_is_built(const struct player *pplayer,
                           const struct impr_type *pimprove)
{
  fc_assert_ret_val(is_small_wonder(pimprove), FALSE);

  return (pplayer != nullptr
          && wonder_is_built(pplayer, pimprove));
}

/**********************************************************************//**
  Get the player city with this small wonder.
**************************************************************************/
struct city *city_from_small_wonder(const struct player *pplayer,
                                    const struct impr_type *pimprove)
{
  fc_assert_ret_val(is_small_wonder(pimprove), nullptr);

  if (pplayer == nullptr) {
    return nullptr; /* Used in some places in the client. */
  } else {
    return city_from_wonder(pplayer, pimprove);
  }
}

/**********************************************************************//**
  Return TRUE iff the improvement can be sold.
**************************************************************************/
bool can_sell_building(const struct impr_type *pimprove)
{
  return (valid_improvement(pimprove)
          && is_building_sellable(pimprove));
}

/**********************************************************************//**
  Return TRUE iff the city can sell the given improvement.
**************************************************************************/
bool can_city_sell_building(const struct city *pcity,
                            const struct impr_type *pimprove)
{
  return (city_has_building(pcity, pimprove)
          && is_building_sellable(pimprove));
}

/**********************************************************************//**
  Return TRUE iff the building is sellable one.
**************************************************************************/
bool is_building_sellable(const struct impr_type *pimprove)
{
  return is_improvement(pimprove);
}

/**********************************************************************//**
  Return TRUE iff the player can sell the given improvement from city.
  If pimprove is nullptr, returns iff city could sell some building type
  (this does not check if such building is in this city)
**************************************************************************/
enum test_result test_player_sell_building_now(struct player *pplayer,
                                               struct city *pcity,
                                               const struct impr_type *pimprove)
{
  /* Check if player can sell anything from this city */
  if (pcity->owner != pplayer) {
    return TR_OTHER_FAILURE;
  }

  if (pcity->did_sell) {
    return TR_ALREADY_SOLD;
  }

  /* Check if particular building can be solt */
  if (pimprove != nullptr
      && !can_city_sell_building(pcity, pimprove)) {
    return TR_OTHER_FAILURE;
  }

  return TR_SUCCESS;
}

/**********************************************************************//**
  Try to find a sensible replacement building, based on other buildings
  that may have caused this one to become obsolete.
**************************************************************************/
const struct impr_type *improvement_replacement(const struct impr_type *pimprove)
{
  requirement_vector_iterate(&pimprove->obsolete_by, pobs) {
    if (pobs->source.kind == VUT_IMPROVEMENT && pobs->present) {
      return pobs->source.value.building;
    }
  } requirement_vector_iterate_end;

  return nullptr;
}

/************************************************************************//**
  Initialize user building flags.
****************************************************************************/
void user_impr_flags_init(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_BUILDING_FLAGS; i++) {
    user_flag_init(&user_impr_flags[i]);
  }
}

/************************************************************************//**
  Frees the memory associated with all building flags
****************************************************************************/
void impr_flags_free(void)
{
  int i;

  for (i = 0; i < MAX_NUM_USER_BUILDING_FLAGS; i++) {
    user_flag_free(&user_impr_flags[i]);
  }
}

/************************************************************************//**
  Sets user defined name for building flag.
****************************************************************************/
void set_user_impr_flag_name(enum impr_flag_id id, const char *name,
                             const char *helptxt)
{
  int bfid = id - IF_USER_FLAG_1;

  fc_assert_ret(id >= IF_USER_FLAG_1 && id <= IF_LAST_USER_FLAG);

  if (user_impr_flags[bfid].name != nullptr) {
    FC_FREE(user_impr_flags[bfid].name);
    user_impr_flags[bfid].name = nullptr;
  }

  if (name && name[0] != '\0') {
    user_impr_flags[bfid].name = fc_strdup(name);
  }

  if (user_impr_flags[bfid].helptxt != nullptr) {
    free(user_impr_flags[bfid].helptxt);
    user_impr_flags[bfid].helptxt = nullptr;
  }

  if (helptxt && helptxt[0] != '\0') {
    user_impr_flags[bfid].helptxt = fc_strdup(helptxt);
  }
}

/************************************************************************//**
  Building flag name callback, called from specenum code.
****************************************************************************/
const char *impr_flag_id_name_cb(enum impr_flag_id flag)
{
  if (flag < IF_USER_FLAG_1 || flag > IF_LAST_USER_FLAG) {
    return nullptr;
  }

  return user_impr_flags[flag - IF_USER_FLAG_1].name;
}

/************************************************************************//**
  Return the (untranslated) help text of the user building flag.
****************************************************************************/
const char *impr_flag_helptxt(enum impr_flag_id id)
{
  fc_assert(id >= IF_USER_FLAG_1 && id <= IF_LAST_USER_FLAG);

  return user_impr_flags[id - IF_USER_FLAG_1].helptxt;
}
