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
#include <config.h>
#endif

#include <string.h>
#include <math.h> /* pow */

/* utility */
#include "rand.h"

/* common */
#include "game.h"
#include "specialist.h"

/* server */
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "notify.h"
#include "srv_log.h"
#include "unithand.h"

/* server/advisors */
#include "advdata.h"
#include "autosettlers.h"
#include "advbuilding.h"

/* ai */
#include "advdomestic.h"
#include "advmilitary.h"
#include "aihand.h"
#include "aisettler.h"
#include "aitools.h"
#include "aiunit.h"
#include "defaultai.h"

#include "aicity.h"

#define LOG_BUY LOG_DEBUG
#define LOG_EMERGENCY LOG_VERBOSE
#define LOG_WANT LOG_VERBOSE

/* TODO:  AI_CITY_RECALC_SPEED should be configurable to ai difficulty.
          -kauf  */
#define AI_CITY_RECALC_SPEED 5

#define CITY_EMERGENCY(pcity)						\
 (pcity->surplus[O_SHIELD] < 0 || city_unhappy(pcity)			\
  || pcity->food_stock + pcity->surplus[O_FOOD] < 0)

#ifdef NDEBUG
#define ASSERT_CHOICE(c) /* Do nothing. */
#else
#define ASSERT_CHOICE(c)                                                 \
  do {                                                                   \
    if ((c).want > 0) {                                                  \
      fc_assert((c).type > CT_NONE && (c).type < CT_LAST);               \
      if ((c).type == CT_BUILDING) {                                     \
        int _iindex = improvement_index((c).value.building);             \
        fc_assert(_iindex >= 0 && _iindex < improvement_count());        \
      } else {                                                           \
        int _uindex = utype_index((c).value.utype);                      \
        fc_assert(_uindex >= 0 && _uindex < utype_count());              \
      }                                                                  \
    }                                                                    \
  } while(0);
#endif /* NDEBUG */

static void ai_sell_obsolete_buildings(struct city *pcity);
static void resolve_city_emergency(struct player *pplayer, struct city *pcity);

/**************************************************************************
  This calculates the usefulness of pcity to us. Note that you can pass
  another player's ai_data structure here for evaluation by different
  priorities.
**************************************************************************/
int ai_eval_calc_city(struct city *pcity, struct ai_data *ai)
{
  int i = (pcity->surplus[O_FOOD] * ai->food_priority
           + pcity->surplus[O_SHIELD] * ai->shield_priority
           + pcity->prod[O_LUXURY] * ai->luxury_priority
           + pcity->prod[O_GOLD] * ai->gold_priority
           + pcity->prod[O_SCIENCE] * ai->science_priority
           + pcity->feel[CITIZEN_HAPPY][FEELING_FINAL] * ai->happy_priority
           - pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] * ai->unhappy_priority
           - pcity->feel[CITIZEN_ANGRY][FEELING_FINAL] * ai->angry_priority
           - pcity->pollution * ai->pollution_priority);

  if (pcity->surplus[O_FOOD] < 0 || pcity->surplus[O_SHIELD] < 0) {
    /* The city is unmaintainable, it can't be good */
    i = MIN(i, 0);
  }

  return i;
}

/************************************************************************** 
  Increase want for a technology because of the value of that technology
  in providing an improvement effect.

  The input building_want gives the desire for the improvement;
  the value may be negative for technologies that produce undesirable
  effects.

  This function must convert from units of 'building want' to 'tech want'.
  We put this conversion in a function because the 'want' scales are
  unclear and kludged. Consequently, this conversion might require tweaking.
**************************************************************************/
void want_tech_for_improvement_effect(struct player *pplayer,
                                      const struct city *pcity,
                                      const struct impr_type *pimprove,
                                      const struct advance *tech,
                                      int building_want)
{
  /* The conversion factor was determined by experiment,
   * and might need adjustment.
   */
  const int tech_want = building_want * def_ai_city_data(pcity)->building_wait
                        * 14 / 8;
#if 0
  /* This logging is relatively expensive,
   * so activate it only while necessary. */
  TECH_LOG(LOG_DEBUG, pplayer, tech,
    "wanted by %s for building: %d -> %d",
    city_name(pcity), improvement_rule_name(pimprove),
    building_want, tech_want);
#endif
  if (tech) {
    pplayer->ai_common.tech_want[advance_index(tech)] += tech_want;
  }
}

/**************************************************************************
  Choose a build for the barbarian player.

  TODO: Move this into advmilitary.c
  TODO: It will be called for each city but doesn't depend on the city,
  maybe cache it?  Although barbarians don't normally have many cities, 
  so can be a bigger bother to cache it.
**************************************************************************/
static void ai_barbarian_choose_build(struct player *pplayer, 
                                      struct city *pcity,
				      struct ai_choice *choice)
{
  struct unit_type *bestunit = NULL;
  int i, bestattack = 0;

  /* Choose the best unit among the basic ones */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD); i++) {
    struct unit_type *iunit = get_role_unit(L_BARBARIAN_BUILD, i);

    if (iunit->attack_strength > bestattack
        && can_city_build_unit_now(pcity, iunit)) {
      bestunit = iunit;
      bestattack = iunit->attack_strength;
    }
  }

  /* Choose among those made available through other civ's research */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD_TECH); i++) {
    struct unit_type *iunit = get_role_unit(L_BARBARIAN_BUILD_TECH, i);

    if (iunit->attack_strength > bestattack
        && can_city_build_unit_now(pcity, iunit)) {
      bestunit = iunit;
      bestattack = iunit->attack_strength;
    }
  }

  /* If found anything, put it into the choice */
  if (bestunit) {
    choice->value.utype = bestunit;
    /* FIXME: 101 is the "overriding military emergency" indicator */
    choice->want   = 101;
    choice->type   = CT_ATTACKER;
  } else {
    log_base(LOG_WANT, "Barbarians don't know what to build!");
  }
}

/**************************************************************************
  Chooses what the city will build.  Is called after the military advisor
  put it's choice into pcity->server.ai.choice and "settler advisor" put
  settler want into pcity->founder_*.

  Note that AI cheats -- it suffers no penalty for switching from unit to 
  improvement, etc.
**************************************************************************/
static void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice newchoice;
  struct ai_data *ai = ai_data_get(pplayer);
  struct ai_city *city_data = def_ai_city_data(pcity);

  init_choice(&newchoice);

  if (ai_handicap(pplayer, H_AWAY)
      && city_built_last_turn(pcity)
      && city_data->urgency == 0) {
    /* Don't change existing productions unless we have to. */
    return;
  }

  if( is_barbarian(pplayer) ) {
    ai_barbarian_choose_build(pplayer, pcity, &(city_data->choice));
  } else {
    /* FIXME: 101 is the "overriding military emergency" indicator */
    if ((city_data->choice.want <= 100
         || city_data->urgency == 0)
        && !(ai_on_war_footing(pplayer) && city_data->choice.want > 0
             && pcity->id != ai->wonder_city)) {
      domestic_advisor_choose_build(pplayer, pcity, &newchoice);
      copy_if_better_choice(&newchoice, &(city_data->choice));
    }
  }

  /* Fallbacks */
  if (city_data->choice.want == 0) {
    /* Fallbacks do happen with techlevel 0, which is now default. -- Per */
    CITY_LOG(LOG_WANT, pcity, "Falling back - didn't want to build soldiers,"
	     " workers, caravans, settlers, or buildings!");
    city_data->choice.want = 1;
    if (best_role_unit(pcity, F_TRADE_ROUTE)) {
      city_data->choice.value.utype
        = best_role_unit(pcity, F_TRADE_ROUTE);
      city_data->choice.type = CT_CIVILIAN;
    } else if (best_role_unit(pcity, F_SETTLERS)) {
      city_data->choice.value.utype
        = best_role_unit(pcity, F_SETTLERS);
      city_data->choice.type = CT_CIVILIAN;
    } else {
      CITY_LOG(LOG_ERROR, pcity, "Cannot even build a fallback "
	       "(caravan/coinage/settlers). Fix the ruleset!");
      city_data->choice.want = 0;
    }
  }

  if (city_data->choice.want != 0) {
    ASSERT_CHOICE(city_data->choice);

    CITY_LOG(LOG_DEBUG, pcity, "wants %s with desire %d.",
	     ai_choice_rule_name(&city_data->choice),
	     city_data->choice.want);
    
    /* FIXME: parallel to citytools change_build_target() */
    if (VUT_IMPROVEMENT == pcity->production.kind
     && is_great_wonder(pcity->production.value.building)
     && (CT_BUILDING != city_data->choice.type
         || city_data->choice.value.building
            != pcity->production.value.building)) {
      notify_player(NULL, pcity->tile, E_WONDER_STOPPED, ftc_server,
		    _("The %s have stopped building The %s in %s."),
		    nation_plural_for_player(pplayer),
		    city_production_name_translation(pcity),
                    city_link(pcity));
    }
    if (CT_BUILDING == city_data->choice.type
      && is_great_wonder(city_data->choice.value.building)
      && (VUT_IMPROVEMENT != pcity->production.kind
          || pcity->production.value.building
             != city_data->choice.value.building)) {
      notify_player(NULL, pcity->tile, E_WONDER_STARTED, ftc_server,
		    _("The %s have started building The %s in %s."),
		    nation_plural_for_player(city_owner(pcity)),
		    city_improvement_name_translation(pcity,
                      city_data->choice.value.building),
                    city_link(pcity));
    }

    switch (city_data->choice.type) {
    case CT_CIVILIAN:
    case CT_ATTACKER:
    case CT_DEFENDER:
      pcity->production.kind = VUT_UTYPE;
      pcity->production.value.utype = city_data->choice.value.utype;
      break;
    case CT_BUILDING:
      pcity->production.kind = VUT_IMPROVEMENT;
      pcity->production.value.building 
        = city_data->choice.value.building;
      break;
    case CT_NONE:
      pcity->production.kind = VUT_NONE;
      break;
    case CT_LAST:
      pcity->production.kind = universals_n_invalid();
      break;
    };
  }
}

/************************************************************************** 
...
**************************************************************************/
static void try_to_sell_stuff(struct player *pplayer, struct city *pcity)
{
  improvement_iterate(pimprove) {
    if (can_city_sell_building(pcity, pimprove)
	&& !building_has_effect(pimprove, EFT_DEFEND_BONUS)) {
/* selling walls to buy defenders is counterproductive -- Syela */
      really_handle_city_sell(pplayer, pcity, pimprove);
      break;
    }
  } improvement_iterate_end;
}

/************************************************************************** 
  Increase maxbuycost.  This variable indicates (via ai_gold_reserve) to 
  the tax selection code how much money do we need for buying stuff.
**************************************************************************/
static void increase_maxbuycost(struct player *pplayer, int new_value)
{
  pplayer->ai_common.maxbuycost = MAX(pplayer->ai_common.maxbuycost, new_value);
}

/************************************************************************** 
  Try to upgrade a city's units. limit is the last amount of gold we can
  end up with after the upgrade. military is if we want to upgrade non-
  military or military units.
**************************************************************************/
static void ai_upgrade_units(struct city *pcity, int limit, bool military)
{
  struct player *pplayer = city_owner(pcity);
  int expenses;

  ai_calc_data(pplayer, NULL, &expenses);

  unit_list_iterate(pcity->tile->units, punit) {
    if (pcity->owner == punit->owner) {
      /* Only upgrade units you own, not allied ones */
      struct unit_type *punittype = can_upgrade_unittype(pplayer, unit_type(punit));

      if (military && !IS_ATTACKER(punit)) {
        /* Only upgrade military units this round */
        continue;
      } else if (!military && IS_ATTACKER(punit)) {
        /* Only civilians or tranports this round */
        continue;
      }
      if (punittype) {
        int cost = unit_upgrade_price(pplayer, unit_type(punit), punittype);
        int real_limit = limit;

        /* Triremes are DANGEROUS!! We'll do anything to upgrade 'em. */
        if (unit_has_type_flag(punit, F_TRIREME)) {
          real_limit = expenses;
        }
        if (pplayer->economic.gold - cost > real_limit) {
          CITY_LOG(LOG_BUY, pcity, "Upgraded %s to %s for %d (%s)",
                   unit_rule_name(punit),
                   utype_rule_name(punittype),
                   cost,
                   military ? "military" : "civilian");
          handle_unit_upgrade(city_owner(pcity), punit->id);
        } else {
          increase_maxbuycost(pplayer, cost);
        }
      }
    }
  } unit_list_iterate_end;
}

/************************************************************************** 
  Buy and upgrade stuff!
**************************************************************************/
static void ai_spend_gold(struct player *pplayer)
{
  struct ai_choice bestchoice;
  int cached_limit = ai_gold_reserve(pplayer);
  int expenses;
  bool war_footing = ai_on_war_footing(pplayer);

  /* Disband explorers that are at home but don't serve a purpose. 
   * FIXME: This is a hack, and should be removed once we
   * learn how to ferry explorers to new land. */
  city_list_iterate(pplayer->cities, pcity) {
    struct tile *ptile = pcity->tile;
    unit_list_iterate_safe(ptile->units, punit) {
      if (unit_has_type_role(punit, L_EXPLORER)
          && pcity->id == punit->homecity
          && def_ai_city_data(pcity)->urgency == 0) {
        CITY_LOG(LOG_BUY, pcity, "disbanding %s to increase production",
                 unit_rule_name(punit));
	handle_unit_disband(pplayer,punit->id);
      }
    } unit_list_iterate_safe_end;
  } city_list_iterate_end;

  ai_calc_data(pplayer, NULL, &expenses);

  do {
    bool expensive; /* don't buy when it costs x2 unless we must */
    int buycost;
    int limit = cached_limit; /* cached_limit is our gold reserve */
    struct city *pcity = NULL;
    struct ai_city *city_data;

    /* Find highest wanted item on the buy list */
    init_choice(&bestchoice);
    city_list_iterate(pplayer->cities, acity) {
      struct ai_city *acity_data = def_ai_city_data(acity);

      if (acity_data->choice.want
          > bestchoice.want && ai_fuzzy(pplayer, TRUE)) {
        bestchoice.value = acity_data->choice.value;
        bestchoice.want = acity_data->choice.want;
        bestchoice.type = acity_data->choice.type;
        pcity = acity;
      }
    } city_list_iterate_end;

    /* We found nothing, so we're done */
    if (bestchoice.want == 0) {
      break;
    }

    city_data = def_ai_city_data(pcity);

    /* Not dealing with this city a second time */
    city_data->choice.want = 0;

    ASSERT_CHOICE(bestchoice);

    /* Try upgrade units at danger location (high want is usually danger) */
    if (city_data->urgency > 1) {
      if (bestchoice.type == CT_BUILDING
       && is_wonder(bestchoice.value.building)) {
        CITY_LOG(LOG_BUY, pcity, "Wonder being built in dangerous position!");
      } else {
        /* If we have urgent want, spend more */
        int upgrade_limit = limit;

        if (city_data->urgency > 1) {
          upgrade_limit = expenses;
        }
        /* Upgrade only military units now */
        ai_upgrade_units(pcity, upgrade_limit, TRUE);
      }
    }

    if (pcity->anarchy != 0 && bestchoice.type != CT_BUILDING) {
      continue; /* Nothing we can do */
    }

    /* Cost to complete production */
    buycost = city_production_buy_gold_cost(pcity);

    if (buycost <= 0) {
      continue; /* Already completed */
    }

    if (is_unit_choice_type(bestchoice.type)
        && utype_has_flag(bestchoice.value.utype, F_CITIES)) {
      if (get_city_bonus(pcity, EFT_GROWTH_FOOD) == 0
          && pcity->size == 1
          && city_granary_size(pcity->size)
             > pcity->food_stock + pcity->surplus[O_FOOD]) {
        /* Don't buy settlers in size 1 cities unless we grow next turn */
        continue;
      } else if (city_list_size(pplayer->cities) > 6) {
        /* Don't waste precious money buying settlers late game
         * since this raises taxes, and we want science. Adjust this
         * again when our tax algorithm is smarter. */
        continue;
      } else if (war_footing) {
        continue;
      }
    } else {
      /* We are not a settler. Therefore we increase the cash need we
       * balance our buy desire with to keep cash at hand for emergencies 
       * and for upgrades */
      limit *= 2;
    }

    /* It costs x2 to buy something with no shields contributed */
    expensive = (pcity->shield_stock == 0)
                || (pplayer->economic.gold - buycost < limit);

    if (bestchoice.type == CT_ATTACKER
	&& buycost 
           > utype_build_shield_cost(bestchoice.value.utype) * 2
        && !war_footing) {
       /* Too expensive for an offensive unit */
       continue;
    }

    /* FIXME: Here Syela wanted some code to check if
     * pcity was doomed, and we should therefore attempt
     * to sell everything in it of non-military value */

    if (pplayer->economic.gold - expenses >= buycost
        && (!expensive 
            || (city_data->grave_danger != 0
                && assess_defense(pcity) == 0)
            || (bestchoice.want > 200 && city_data->urgency > 1))) {
      /* Buy stuff */
      CITY_LOG(LOG_BUY, pcity, "Crash buy of %s for %d (want %d)",
               ai_choice_rule_name(&bestchoice),
               buycost,
               bestchoice.want);
      really_handle_city_buy(pplayer, pcity);
    } else if (city_data->grave_danger != 0 
               && bestchoice.type == CT_DEFENDER
               && assess_defense(pcity) == 0) {
      /* We have no gold but MUST have a defender */
      CITY_LOG(LOG_BUY, pcity, "must have %s but can't afford it (%d < %d)!",
               ai_choice_rule_name(&bestchoice),
               pplayer->economic.gold,
               buycost);
      try_to_sell_stuff(pplayer, pcity);
      if (pplayer->economic.gold - expenses >= buycost) {
        CITY_LOG(LOG_BUY, pcity, "now we can afford it (sold something)");
        really_handle_city_buy(pplayer, pcity);
      }
      increase_maxbuycost(pplayer, buycost);
    }
  } while (TRUE);

  if (!war_footing) {
    /* Civilian upgrades now */
    city_list_iterate(pplayer->cities, pcity) {
      ai_upgrade_units(pcity, cached_limit, FALSE);
    } city_list_iterate_end;
  }

  log_base(LOG_BUY, "%s wants to keep %d in reserve (tax factor %d)", 
           player_name(pplayer), cached_limit, pplayer->ai_common.maxbuycost);
}

/**************************************************************************
  Calculates a unit's food upkeep (per turn).
**************************************************************************/
static int unit_food_upkeep(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  int upkeep = utype_upkeep_cost(unit_type(punit), pplayer, O_FOOD);
  if (punit->id != 0 && punit->homecity == 0)
    upkeep = 0; /* thanks, Peter */

  return upkeep;
}


/**************************************************************************
  Returns how much food a settler will consume out of the city's foodbox
  when created. If unit has id zero it is assumed to be a virtual unit
  inside a city.

  FIXME: This function should be generalised and then moved into 
  common/unittype.c - Per
**************************************************************************/
static int unit_foodbox_cost(struct unit *punit)
{
  int cost = 30;

  if (punit->id == 0) {
    /* It is a virtual unit, so must start in a city... */
    struct city *pcity = tile_city(punit->tile);

    /* The default is to lose 100%.  The growth bonus reduces this. */
    int foodloss_pct = 100 - get_city_bonus(pcity, EFT_GROWTH_FOOD);

    foodloss_pct = CLIP(0, foodloss_pct, 100);
    fc_assert_ret_val(pcity != NULL, -1);
    cost = city_granary_size(pcity->size);
    cost = cost * foodloss_pct / 100;
  }

  return cost;
}

/**************************************************************************
  Estimates the want for a terrain improver (aka worker) by creating a 
  virtual unit and feeding it to settler_evaluate_improvements.

  TODO: AI does not ship F_SETTLERS around, only F_CITIES - Per
**************************************************************************/
static void contemplate_terrain_improvements(struct city *pcity)
{
  struct unit *virtualunit;
  int want;
  enum unit_activity best_act;
  struct tile *best_tile = NULL; /* May be accessed by log_*() calls. */
  struct tile *pcenter = city_tile(pcity);
  struct player *pplayer = city_owner(pcity);
  struct ai_data *ai = ai_data_get(pplayer);
  struct unit_type *unit_type = best_role_unit(pcity, F_SETTLERS);
  Continent_id place = tile_continent(pcenter);

  if (unit_type == NULL) {
    log_debug("No F_SETTLERS role unit available");
    return;
  }

  /* Create a localized "virtual" unit to do operations with. */
  virtualunit = create_unit_virtual(pplayer, pcity, unit_type, 0);
  /* Advisors data space not allocated as it's not needed in the
     lifetime of the virtualunit. */
  virtualunit->tile = pcenter;
  want = settler_evaluate_improvements(virtualunit, &best_act, &best_tile,
                                       NULL, NULL);
  want = (want - unit_food_upkeep(virtualunit) * FOOD_WEIGHTING) * 100
         / (40 + unit_foodbox_cost(virtualunit));
  destroy_unit_virtual(virtualunit);

  /* Massage our desire based on available statistics to prevent
   * overflooding with worker type units if they come cheap in
   * the ruleset */
  want /= MAX(1, ai->stats.workers[place]
                 / (ai->stats.cities[place] + 1));
  want -= ai->stats.workers[place];
  want = MAX(want, 0);

  CITY_LOG(LOG_DEBUG, pcity, "wants %s with want %d to do %s at (%d,%d), "
           "we have %d workers and %d cities on the continent",
	   utype_rule_name(unit_type),
	   want,
	   get_activity_text(best_act),
	   TILE_XY(best_tile),
           ai->stats.workers[place], 
           ai->stats.cities[place]);
  fc_assert(want >= 0);

  def_ai_city_data(pcity)->settler_want = want;
}

/**************************************************************************
  One of the top level AI functions.  It does (by calling other functions):
  worker allocations,
  build choices,
  extra gold spending.
**************************************************************************/
void ai_manage_cities(struct player *pplayer)
{
  pplayer->ai_common.maxbuycost = 0;

  TIMING_LOG(AIT_EMERGENCY, TIMER_START);
  city_list_iterate(pplayer->cities, pcity) {
    if (CITY_EMERGENCY(pcity)) {
      auto_arrange_workers(pcity); /* this usually helps */
    }
    if (CITY_EMERGENCY(pcity)) {
      /* Fix critical shortages or unhappiness */
      resolve_city_emergency(pplayer, pcity);
    }
    ai_sell_obsolete_buildings(pcity);
    sync_cities();
  } city_list_iterate_end;
  TIMING_LOG(AIT_EMERGENCY, TIMER_STOP);

  TIMING_LOG(AIT_BUILDINGS, TIMER_START);
  building_advisor(pplayer);
  TIMING_LOG(AIT_BUILDINGS, TIMER_STOP);

  /* Initialize the infrastructure cache, which is used shortly. */
  initialize_infrastructure_cache(pplayer);
  city_list_iterate(pplayer->cities, pcity) {
    struct ai_city *city_data = def_ai_city_data(pcity);
    /* Note that this function mungs the seamap, but we don't care */
    TIMING_LOG(AIT_CITY_MILITARY, TIMER_START);
    military_advisor_choose_build(pplayer, pcity, &city_data->choice);
    TIMING_LOG(AIT_CITY_MILITARY, TIMER_STOP);
    if (ai_on_war_footing(pplayer) && city_data->choice.want > 0) {
      continue; /* Go, soldiers! */
    }
    /* Will record its findings in pcity->settler_want */ 
    TIMING_LOG(AIT_CITY_TERRAIN, TIMER_START);
    contemplate_terrain_improvements(pcity);
    TIMING_LOG(AIT_CITY_TERRAIN, TIMER_STOP);

    TIMING_LOG(AIT_CITY_SETTLERS, TIMER_START);
    if (city_data->founder_turn <= game.info.turn) {
      /* Will record its findings in pcity->founder_want */ 
      contemplate_new_city(pcity);
      /* Avoid recalculating all the time.. */
      city_data->founder_turn = 
        game.info.turn + fc_rand(AI_CITY_RECALC_SPEED) + AI_CITY_RECALC_SPEED;
    } else if (pcity->server.debug) {
      /* recalculate every turn */
      contemplate_new_city(pcity);
    }
    TIMING_LOG(AIT_CITY_SETTLERS, TIMER_STOP);
    ASSERT_CHOICE(city_data->choice);
  } city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity) {
    ai_city_choose_build(pplayer, pcity);
  } city_list_iterate_end;

  ai_spend_gold(pplayer);
}

/**************************************************************************
... 
**************************************************************************/
static bool building_unwanted(struct player *plr, struct impr_type *pimprove)
{
#if 0 /* This check will become more complicated now. */ 
  return (ai_wants_no_science(plr)
          && building_has_effect(pimprove, EFT_SCIENCE_BONUS));
#endif
  return FALSE;
}

/**************************************************************************
  Sell an obsolete building if there are any in the city.
**************************************************************************/
static void ai_sell_obsolete_buildings(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);

  city_built_iterate(pcity, pimprove) {
    if (can_city_sell_building(pcity, pimprove) 
       && !building_has_effect(pimprove, EFT_DEFEND_BONUS)
	      /* selling city walls is really, really dumb -- Syela */
       && (is_improvement_redundant(pcity, pimprove)
	   || building_unwanted(pplayer, pimprove))) {
      do_sell_building(pplayer, pcity, pimprove);
      notify_player(pplayer, pcity->tile, E_IMP_SOLD, ftc_server,
                    _("%s is selling %s (not needed) for %d."), 
                    city_link(pcity),
                    improvement_name_translation(pimprove), 
                    impr_sell_gold(pimprove));
      return; /* max 1 building each turn */
    }
  } city_built_iterate_end;
}

/**************************************************************************
  This function tries desperately to save a city from going under by
  revolt or starvation of food or resources. We do this by taking
  over resources held by nearby cities and disbanding units.

  TODO: Try to move units into friendly cities to reduce unhappiness
  instead of disbanding. Also rather starve city than keep it in
  revolt, as long as we don't lose settlers.

  TODO: Make function that tries to save units by moving them into
  cities that can upkeep them and change homecity rather than just
  disband. This means we'll have to move this function to beginning
  of AI turn sequence (before moving units).

  "I don't care how slow this is; it will very rarely be used." -- Syela

  Syela is wrong. It happens quite too often, mostly due to unhappiness.
  Also, most of the time we are unable to resolve the situation. 
**************************************************************************/
static void resolve_city_emergency(struct player *pplayer, struct city *pcity)
{
  struct tile *pcenter = city_tile(pcity);

  log_base(LOG_EMERGENCY,
           "Emergency in %s (%s, angry%d, unhap%d food%d, prod%d)",
           city_name(pcity),
           city_unhappy(pcity) ? "unhappy" : "content",
           pcity->feel[CITIZEN_ANGRY][FEELING_FINAL],
           pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
           pcity->surplus[O_FOOD],
           pcity->surplus[O_SHIELD]);

  city_tile_iterate(city_map_radius_sq_get(pcity), pcenter, atile) {
    struct city *acity = tile_worked(atile);

    if (acity && acity != pcity && city_owner(acity) == city_owner(pcity))  {
      log_base(LOG_EMERGENCY, "%s taking over %s square in (%d, %d)",
               city_name(pcity), city_name(acity), TILE_XY(atile));

      int ax, ay;
      fc_assert_action(city_base_to_city_map(&ax, &ay, acity, atile),
                       continue);

      if (is_free_worked(acity, atile)) {
        /* Can't remove a worker here. */
        continue;
      }

      city_map_update_empty(acity, atile);
      acity->specialists[DEFAULT_SPECIALIST]++;
      city_freeze_workers_queue(acity);
    }
  } city_tile_iterate_end;

  auto_arrange_workers(pcity);

  if (!CITY_EMERGENCY(pcity)) {
    log_base(LOG_EMERGENCY, "Emergency in %s resolved", city_name(pcity));
    goto cleanup;
  }

  unit_list_iterate_safe(pcity->units_supported, punit) {
    if (city_unhappy(pcity)
        && (utype_happy_cost(unit_type(punit), pplayer) > 0
            && (unit_being_aggressive(punit) || is_field_unit(punit)))
        && def_ai_unit_data(punit)->passenger == 0) {
      UNIT_LOG(LOG_EMERGENCY, punit, "is causing unrest, disbanded");
      handle_unit_disband(pplayer, punit->id);
      city_refresh(pcity);
    }
  } unit_list_iterate_safe_end;

  if (CITY_EMERGENCY(pcity)) {
    log_base(LOG_EMERGENCY, "Emergency in %s remains unresolved",
             city_name(pcity));
  } else {
    log_base(LOG_EMERGENCY,
             "Emergency in %s resolved by disbanding unit(s)",
             city_name(pcity));
  }

  cleanup:
  city_thaw_workers_queue();
  sync_cities();
}

/**************************************************************************
  Initialize city for use with default AI.
**************************************************************************/
void ai_city_alloc(struct city *pcity)
{
  struct ai_city *city_data = fc_calloc(1, sizeof(struct ai_city));

  city_data->building_wait = BUILDING_WAIT_MINIMUM;

  city_set_ai_data(pcity, default_ai_get_self(), city_data);
}

/**************************************************************************
  Free city from use with default AI.
**************************************************************************/
void ai_city_free(struct city *pcity)
{
  struct ai_city *city_data = def_ai_city_data(pcity);

  if (city_data != NULL) {
    city_set_ai_data(pcity, default_ai_get_self(), NULL);
    FC_FREE(city_data);
  }
}
