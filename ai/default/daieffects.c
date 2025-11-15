/***********************************************************************
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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

/* common */
#include "city.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "multipliers.h"
#include "player.h"
#include "research.h"
#include "specialist.h"
#include "traderoutes.h"
#include "victory.h"

/* server */
#include "plrhand.h"

/* server/advisors */
#include "advdata.h"
#include "advtools.h"

/* ai */
#include "aitraits.h"
#include "handicaps.h"


#include "daieffects.h"

/**********************************************************************//**
  Return the number of "luxury specialists".  This is the number of
  specialists who provide at least HAPPY_COST luxury, being the number of
  luxuries needed to make one citizen content or happy.

  The AI assumes that for any specialist that provides HAPPY_COST luxury,
  if we can get that luxury from some other source it allows the specialist
  to become a worker.  The benefits from an extra worker are weighed against
  the losses from acquiring the two extra luxury.

  This is a very bad model if the abilities of specialists are changed.
  But as long as the civ2 model of specialists is used it will continue
  to work okay.
**************************************************************************/
static int get_entertainers(const struct city *pcity)
{
  int providers = 0;

  normal_specialist_type_iterate(i) {
    if (get_specialist_output(pcity, i, O_LUXURY) >= game.info.happy_cost) {
      providers += pcity->specialists[i];
    }
  } normal_specialist_type_iterate_end;

  return providers;
}

/**********************************************************************//**
  How desirable particular effect making people content is for a
  particular city?
**************************************************************************/
adv_want dai_content_effect_value(const struct player *pplayer,
                                  const struct city *pcity,
                                  int amount,
                                  int num_cities,
                                  int happiness_step)
{
  adv_want v = 0;

  if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
    int i;
    int max_converted = pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL];

    /* See if some step of happiness calculation gets capped */
    for (i = happiness_step; i < FEELING_FINAL; i++) {
      max_converted = MIN(max_converted, pcity->feel[CITIZEN_UNHAPPY][i]);
    }

    v = MIN(amount, max_converted + get_entertainers(pcity)) * 35;
  }

  if (num_cities > 1) {
    int factor = 2;

    /* Try to build wonders to offset empire size unhappiness */
    if (city_list_size(pplayer->cities)
        > get_player_bonus(pplayer, EFT_EMPIRE_SIZE_BASE)) {
      if (get_player_bonus(pplayer, EFT_EMPIRE_SIZE_BASE) > 0) {
        int step_bonus = get_player_bonus(pplayer, EFT_EMPIRE_SIZE_STEP);

        factor += city_list_size(pplayer->cities)
          / MAX(step_bonus, 1);
      }
      factor += 2;
    }
    v += factor * num_cities * amount;
  }

  return v;
}

/**********************************************************************//**
  Number of AI stats units affected by effect
**************************************************************************/
static int num_affected_units(const struct effect *peffect,
                              const struct adv_data *ai)
{
  int unit_count = 0;

  unit_class_iterate(pclass) {
    if (requirement_fulfilled_by_unit_class(pclass, &peffect->reqs)) {
      unit_count += ai->stats.units.byclass[uclass_index(pclass)];
    }
  } unit_class_iterate_end;

  return unit_count;
}

/**********************************************************************//**
  How desirable is a particular effect for a particular city,
  given the number of cities in range (c).
**************************************************************************/
adv_want dai_effect_value(struct player *pplayer,
                          const struct adv_data *adv, const struct city *pcity,
                          const bool capital, int turns,
                          const struct effect *peffect, const int c,
                          const int nplayers)
{
  int amount = peffect->value;
  bool affects_sea_capable_units = FALSE;
  bool affects_land_capable_units = FALSE;
  int num;
  int trait;
  adv_want v = 0;
  enum effect_type value_as;

  if (peffect->multiplier) {
    if (pplayer) {
      amount = (player_multiplier_effect_value(pplayer, peffect->multiplier)
                * amount) / 100;
    } else {
      amount = 0;
    }
  }

  if (amount == 0) {
    /* We could prune such effects in ruleset loading already,
     * but we allow people tuning their rulesets to temporarily disable
     * the effect by setting value to 0 without need to completely
     * remove the effect.
     * Shortcutting these effects here is not only for performance,
     * more importantly it makes sure code below assuming amount to
     * be positive does not assign positive value. */
    return 0;
  }

  value_as = user_effect_ai_valued_as(peffect->type);

  switch (value_as) {
  /* These effects have already been evaluated in base_want() */
  case EFT_CAPITAL_CITY:
  case EFT_GOV_CENTER:
  case EFT_UPKEEP_FREE:
  case EFT_IMPR_UPKEEP_REDUCTION:
  case EFT_TECH_UPKEEP_FREE:
  case EFT_POLLU_POP_PCT:
  case EFT_POLLU_POP_PCT_2:
  case EFT_POLLU_PROD_PCT:
  case EFT_OUTPUT_BONUS:
  case EFT_OUTPUT_BONUS_ABSOLUTE:
  case EFT_OUTPUT_BONUS_2:
  case EFT_OUTPUT_ADD_TILE:
  case EFT_OUTPUT_INC_TILE:
  case EFT_OUTPUT_PER_TILE:
  case EFT_OUTPUT_WASTE:
  case EFT_OUTPUT_WASTE_BY_DISTANCE:
  case EFT_OUTPUT_WASTE_BY_REL_DISTANCE:
  case EFT_OUTPUT_WASTE_PCT:
  case EFT_SURPLUS_WASTE_PCT:
  case EFT_SURPLUS_WASTE_PCT_BY_REL_DISTANCE:
  case EFT_SPECIALIST_OUTPUT:
  case EFT_ENEMY_CITIZEN_UNHAPPY_PCT:
  case EFT_IRRIGATION_PCT:
  case EFT_MINING_PCT:
  case EFT_OUTPUT_TILE_PUNISH_PCT:
    break;

  case EFT_CITY_VISION_RADIUS_SQ:
  case EFT_UNIT_VISION_RADIUS_SQ:
    /* Wild guess. "Amount" is the number of tiles (on average) that
     * will be revealed by the effect. Note that with an omniscient
     * AI this effect is actually not useful at all. */
    v += c * amount;
    break;

  case EFT_TURN_YEARS:
  case EFT_TURN_FRAGMENTS:
  case EFT_SLOW_DOWN_TIMELINE:
    /* AI doesn't care about these. */
    break;

    /* WAG evaluated effects */
  case EFT_INCITE_COST_PCT:
    v += c * amount / 100;
    break;
  case EFT_MAKE_HAPPY:
    v += (get_entertainers(pcity) + pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]) * 5 * amount;
    if (city_list_size(pplayer->cities)
	> get_player_bonus(pplayer, EFT_EMPIRE_SIZE_BASE)) {
      v += c * amount; /* Offset large empire size */
    }
    v += c * amount;
    break;
  case EFT_UNIT_RECOVER:
    /* TODO */
    break;
  case EFT_NO_UNHAPPY:
    v += (get_entertainers(pcity) + pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]) * 30;
    break;
  case EFT_FORCE_CONTENT:
    v += dai_content_effect_value(pplayer, pcity, amount, c, FEELING_FINAL);
    break;
  case EFT_MAKE_CONTENT:
    v += dai_content_effect_value(pplayer, pcity, amount, c, FEELING_EFFECT);
    break;
  case EFT_MAKE_CONTENT_MIL_PER:
    if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
      v += MIN(pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] + get_entertainers(pcity),
	       amount) * 25;
      v += MIN(amount, 5) * c;
    }
    break;
  case EFT_MAKE_CONTENT_MIL:
    if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
      v += pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL] * amount
        * MAX(unit_list_size(pcity->units_supported), 0) * 2;
      v += c * MAX(amount + 2, 1);
    }
    break;
  case EFT_TECH_PARASITE_PCT_MAX:
    amount = (amount * research_count()) / 100;
    fc__fallthrough;
  case EFT_TECH_PARASITE:
    {
      int bulbs;
      int value;

      if (nplayers <= amount) {
        break;
      }

      bulbs = 0;
      players_iterate_alive(aplayer) {
        if (aplayer != pplayer
            && (!game.info.team_pooled_research
                || !players_on_same_team(aplayer, pplayer))) {
          bulbs += (aplayer->server.bulbs_last_turn
                    + city_list_size(aplayer->cities) + 1);
        }
      } players_iterate_alive_end;

      /* For some number of turns we will be receiving bulbs for free
       * Bulbs should be amortized properly for each turn.
       * We use formula for the sum of geometric series:
       */
      value = bulbs * (1.0 - pow(1.0 - (1.0 / MORT), turns)) * MORT;

      value = value  * (100 - game.server.freecost)
        * (nplayers - amount) / (nplayers * amount * 100);

      /* WAG */
      value /= 3;

      v += value;
      break;
    }
  case EFT_CONQUEST_TECH_PCT:
    /* Compare to EFT_GIVE_IMM_TECH which gives game.info.sciencebox * num_techs */
    v +=  game.info.sciencebox * (100 - game.server.conquercost) / 200
      * amount / 100;
    break;
  case EFT_GROWTH_FOOD:
    /* FIXME: As total value is clipped 0 ... 100, single
     *        effect should have no value at all in some cases. */
    v += c * 4 + (amount / 10) * MAX(pcity->surplus[O_FOOD], 0);
    break;
  case EFT_SHRINK_FOOD:
    v += c * 4 + (amount / 15) * MAX(-pcity->surplus[O_FOOD], 1);
    break;
  case EFT_POPCOST_FREE:
    v += amount * city_granary_size(pcity->size) / 10;
    break;
  case EFT_HEALTH_PCT:
    /* Is plague possible */
    if (game.info.illness_on) {
      v += c * 5 + (amount / 5) * pcity->server.illness;
    }
    break;
  case EFT_AIRLIFT:
    /* FIXME: We need some smart algorithm here. The below is
     * totally braindead. */
    v += c + MIN(adv->stats.units.airliftable, 13);
    break;
  case EFT_ANY_GOVERNMENT:
    if (!can_change_to_government(pplayer, adv->goal.govt.gov)) {
      v += MIN(MIN(adv->goal.govt.val, 65),
               research_goal_unknown_techs(research_get(pplayer),
                                           adv->goal.govt.req) * 10);
    }
    break;
  case EFT_ENABLE_NUKE:
    /* Treat nuke as a Cruise Missile upgrade */
    v += 20 + adv->stats.units.suicide_attackers * 5;
    break;
  case EFT_ENABLE_SPACE:
    if (victory_enabled(VC_SPACERACE)) {
      v += 10;
      if (adv->dipl.production_leader == pplayer
          || adv->dipl.tech_leader == pplayer) {
        v += 150;
      }
    }
    break;
  case EFT_VICTORY:
    v += 250;
    break;
  case EFT_GIVE_IMM_TECH:
    if (adv_wants_science(pplayer)) {
      v += amount * (game.info.sciencebox + 1);
    }
    break;
  case EFT_HAVE_CONTACTS:
    {
      int new_contacts = 0;

      players_iterate_alive(theother) {
        if (player_diplstate_get(pplayer, theother)->contact_turns_left <= 0) {
          new_contacts++;
        }
      } players_iterate_alive_end;

      v += 30 * new_contacts;
    }
    break;
  case EFT_HAVE_EMBASSIES:
    v += 2 * nplayers;
    break;
  case EFT_REVEAL_CITIES:
  case EFT_NO_ANARCHY:
    break;  /* Useless for AI */
  case EFT_NUKE_PROOF:
    if (adv->threats.nuclear) {
      v += city_size_get(pcity) * unit_list_size(pcity->tile->units)
           * (capital + 1) * amount / 100;
    }
    break;
  case EFT_REVEAL_MAP:
    if (!adv->explore.land_done || !adv->explore.sea_done) {
      v += 10;
    }
    break;
  case EFT_UNIT_SLOTS:
    v += 8 * c;
    break;
  case EFT_SIZE_UNLIMIT:
    /* Note we look up the SIZE_UNLIMIT again right below.  This could
     * be avoided... */
    if (amount > 0) {
      if (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) <= 0) {
        amount = 20; /* really big city */
      }
    } else {
      /* Effect trying to remove unlimit. */
      v -= 30 * c * adv->food_priority;
      break;
    }
    fc__fallthrough; /* there not being a break here is deliberate, mind you */
  case EFT_SIZE_ADJ:
    if (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) <= 0) {
      const int aqueduct_size = get_city_bonus(pcity, EFT_SIZE_ADJ);
      int extra_food = pcity->surplus[O_FOOD];

      if (city_granary_size(city_size_get(pcity)) == pcity->food_stock) {
        /* The idea being that if we have a full granary, we have an
         * automatic surplus of our granary excess in addition to anything
         * collected by city workers. */
        extra_food += pcity->food_stock -
                      city_granary_size(city_size_get(pcity) - 1);
      }

      if (amount > 0 && !city_can_grow_to(pcity, city_size_get(pcity) + 1)) {
	v += extra_food * adv->food_priority * amount;
	if (city_size_get(pcity) == aqueduct_size) {
	  v += 30 * extra_food;
	}
      }
      v += c * amount * 4 / aqueduct_size;
    }
    break;
  case EFT_SS_STRUCTURAL:
  case EFT_SS_COMPONENT:
  case EFT_SS_MODULE:
    if (victory_enabled(VC_SPACERACE)
        /* If someone has started building spaceship already or
         * we have chance to win a spacerace */
        && (adv->dipl.spacerace_leader
            || adv->dipl.production_leader == pplayer
            || adv->dipl.tech_leader == pplayer)) {
      int space_want;

      if (pcity->id == adv->wonder_city) {
        /* adjust_improvement_wants_by_effects() lowers want for any non-wonder
         * target on wonder city. We want space part want reduced even more than
         * regular buildings, as buildings are at least helping the specific city
         * when finished. */
        space_want = 120;
      } else {
        space_want = 210;
      }

      if (pplayer->spaceship.state == SSHIP_STARTED) {
        /* We are already running. Let's not make it a half-hearted attempt. */
        space_want *= 3;
      }

      v += space_want;
    }
    break;
  case EFT_SPY_RESISTANT:
  case EFT_SABOTEUR_RESISTANT:
    /* Uhm, problem: City Wall has -50% here!! */
    break;
  case EFT_MOVE_BONUS:
    num = num_affected_units(peffect, adv);
    v += (8 * v * amount + num);
    break;
  case EFT_ACTIVITY_TIME:
    /* TODO: As an override, this can be either good or bad,
     *       depending on the value being overridden. */
    break;
  case EFT_UNIT_NO_LOSE_POP:
    v += unit_list_size(pcity->tile->units) * 2;
    break;
  case EFT_HP_REGEN:
  case EFT_HP_REGEN_2:
    num = num_affected_units(peffect, adv);
    v += (5 * c + num);
    break;
  case EFT_MIN_HP_PCT:
    num = num_affected_units(peffect, adv);
    /* Lesser value than EFT_HP_REGEN as this does not add health
     * with full effect value, but only up to the effect value.
     * Only badly wounded units affected at all. */
    v += (3 * c + num);
    break;
  case EFT_VETERAN_COMBAT:
    num = num_affected_units(peffect, adv);
    v += amount * (2 * c + num) / 50;
    break;
  case EFT_VETERAN_WORK:
    num = num_affected_units(peffect, adv);
    v += amount * (2 * c + num) / 70;
    break;
  case EFT_VETERAN_BUILD:
    /* FIXME: check other reqs (e.g., unitflag) */
    num = num_affected_units(peffect, adv);
    v += amount * (3 * c + num);
    break;
  case EFT_UPGRADE_UNIT:
    if (amount == 1) {
      v += adv->stats.units.upgradeable * 2;
    } else if (amount == 2) {
      v += adv->stats.units.upgradeable * 3;
    } else {
      v += adv->stats.units.upgradeable * 4;
    }
    break;
  case EFT_UNIT_BRIBE_COST_PCT:
    num = num_affected_units(peffect, adv);
    v += ((2 * c + num) * amount) / 400;
    break;
  case EFT_ATTACK_BONUS:
    num = num_affected_units(peffect, adv);
    v += (num + 4) * amount / 200;
    break;
  case EFT_DEFEND_BONUS:
    if (has_handicap(pplayer, H_DEFENSIVE)) {
      v += amount / 10; /* make AI slow */
    }

    /* TODO: Really should consider how many affected enemy units there is. */
    unit_class_iterate(pclass) {
      if (requirement_fulfilled_by_unit_class(pclass, &peffect->reqs)) {
        if (pclass->adv.sea_move != MOVE_NONE) {
          affects_sea_capable_units = TRUE;
        }
        if (pclass->adv.land_move != MOVE_NONE) {
          affects_land_capable_units = TRUE;
        }
      }
      if (affects_sea_capable_units && affects_land_capable_units) {
        /* Don't bother searching more if we already know enough. */
        break;
      }
    } unit_class_iterate_end;

    if (affects_sea_capable_units) {
      if (is_ocean_tile(pcity->tile)) {
        v += adv->oceans[-tile_continent(pcity->tile)].threat
          ? amount / 6 : amount / 25;
      } else {
        adjc_iterate(&(wld.map), pcity->tile, tile2) {
          if (is_ocean_tile(tile2)) {
            if (adv->oceans[-tile_continent(tile2)].threat) {
              v += amount / 6;
              break;
            }
          }
        } adjc_iterate_end;
      }
    }
    v += (amount / 25 + adv->threats.invasions - 1) * c; /* For wonder */
    if (capital || affects_land_capable_units) {
      Continent_id place = tile_continent(pcity->tile);

      if ((place > 0 && adv->continents[place].threat)
          || capital
          || (adv->threats.invasions
              /* FIXME: This ignores riverboats on some rulesets.
                        We should analyze rulesets when game starts
                        and have relevant checks here. */
              && is_terrain_class_near_tile(&(wld.map), pcity->tile, TC_OCEAN))) {
        if (place > 0 && adv->continents[place].threat) {
          v += amount * 4 / 5;
        } else {
          v += amount / (!adv->threats.igwall ? (18 - capital * 6) : 18);
        }
      }
    }
    break;
  case EFT_FORTIFY_DEFENSE_BONUS:
    num = num_affected_units(peffect, adv);
    v += (num + 4) * amount / 250; /* Divisor 250 is a bit bigger than one for
                                    * EFT_ATTACK_BONUS that is always active.
                                    * Fortify bonus applies only in special case that
                                    * unit is fortified. */
    break;
  case EFT_GAIN_AI_LOVE:
    players_iterate(aplayer) {
      if (is_ai(aplayer)) {
	if (has_handicap(pplayer, H_DEFENSIVE)) {
	  v += amount / 10;
	} else {
	  v += amount / 20;
	}
      }
    } players_iterate_end;
    break;
  case EFT_UPGRADE_PRICE_PCT:
    /* This is based on average base upgrade price of 50. */
    v -= adv->stats.units.upgradeable * amount / 2;
    break;
  case EFT_BORDER_STRENGTH_PCT:
    v += amount / 4;
    break;
  case EFT_CIVIL_WAR_CITY_BONUS:
    /* Relatively low, since we don't expect ever to lose our capital,
     * and otherwise this doesn't matter. */
    v += amount / 10;
    break;

  /* Currently not supported for building AI - wait for modpack users */
  case EFT_CITY_UNHAPPY_SIZE:
  case EFT_UNHAPPY_FACTOR:
  case EFT_UPKEEP_PCT:
  case EFT_UNIT_UPKEEP_FREE_PER_CITY:
  case EFT_CIVIL_WAR_CHANCE:
  case EFT_EMPIRE_SIZE_BASE:
  case EFT_EMPIRE_SIZE_STEP:
  case EFT_MAX_RATES:
  case EFT_MARTIAL_LAW_BY_UNIT:
  case EFT_MARTIAL_LAW_MAX:
  case EFT_RAPTURE_GROW:
  case EFT_REVOLUTION_UNHAPPINESS:
  case EFT_HAS_SENATE:
  case EFT_INSPIRE_PARTISANS:
  case EFT_HAPPINESS_TO_GOLD:
  case EFT_FANATICS:
  case EFT_NO_DIPLOMACY:
  case EFT_NOT_TECH_SOURCE:
  case EFT_OUTPUT_PENALTY_TILE:
  case EFT_OUTPUT_INC_TILE_CELEBRATE:
  case EFT_TRADE_REVENUE_BONUS:
  case EFT_TILE_WORKABLE:
  case EFT_COMBAT_ROUNDS:
  case EFT_ILLEGAL_ACTION_MOVE_COST:
  case EFT_ILLEGAL_ACTION_HP_COST:
  case EFT_CASUS_BELLI_CAUGHT:
  case EFT_CASUS_BELLI_SUCCESS:
  case EFT_CASUS_BELLI_COMPLETE:
  case EFT_ACTION_ODDS_PCT:
  case EFT_ACTION_RESIST_PCT:
  case EFT_BORDER_VISION:
  case EFT_STEALINGS_IGNORE:
  case EFT_MAPS_STOLEN_PCT:
  case EFT_UNIT_SHIELD_VALUE_PCT:
  case EFT_NUKE_BLAST_RADIUS_1_SQ:
  case EFT_HEAL_UNIT_PCT:
  case EFT_TILE_CLAIMABLE:
    break;
    /* This has no effect for AI */
  case EFT_VISIBLE_WALLS:
  case EFT_CITY_IMAGE:
  case EFT_SHIELD2GOLD_PCT:
    break;
  case EFT_PERFORMANCE:
  case EFT_NATION_PERFORMANCE:
    /* Consider each culture point worth 1/10 point, minimum of 1 point... */
    v += amount / 10 + 1;
    break;
  case EFT_HISTORY:
  case EFT_NATION_HISTORY:
    /* ...and history effect to accumulate those points for 50 turns. */
    v += amount * 5;
    break;
  case EFT_CULTURE_PCT:
    /* Assume that this multiplies accumulation of 5 history points / turn */
    v += amount * 5 * 5 / 100;
    break;
  case EFT_TECH_COST_FACTOR:
    v -= amount * 50;
    break;
  case EFT_TECH_LEAKAGE:
    {
      int leak_val = 0;

      switch (game.info.tech_leakage) {
      case TECH_LEAKAGE_NONE:
        break;
      case TECH_LEAKAGE_EMBASSIES:
        leak_val = (normal_player_count() - 1) * 2;
        break;
      case TECH_LEAKAGE_PLAYERS:
        leak_val = (normal_player_count() - 1) * 5;
        break;
      case TECH_LEAKAGE_NO_BARBS:
        leak_val = (normal_player_count() - 1) * 5 + 2 * 3;
        break;
      }

      if (amount > 0 && get_player_bonus(pplayer, EFT_TECH_LEAKAGE) <= 0) {
        v += leak_val;
      } else if (amount < 0) {
        v -= leak_val;
      }
    }
    break;
  case EFT_IMPR_BUILD_COST_PCT:
  case EFT_UNIT_BUILD_COST_PCT:
    v -= amount * 30;
    break;
  case EFT_IMPR_BUY_COST_PCT:
  case EFT_UNIT_BUY_COST_PCT:
    v -= amount * 25;
    break;
  case EFT_CITY_RADIUS_SQ:
    v += amount * 10; /* AI wants bigger city radii */
    break;
  case EFT_CITY_BUILD_SLOTS:
    v += amount * 10;
    break;
  case EFT_MIGRATION_PCT:
    /* Consider all foreign cities within set distance */
    iterate_outward(&(wld.map), city_tile(pcity),
                    game.server.mgr_distance + (int)sqrt(MAX(city_map_radius_sq_get(pcity), 0)),
                    ptile) {
      struct city *acity = tile_city(ptile);

      if (!acity || acity == pcity || city_owner(acity) == pplayer) {
        /* No city, the city in the center, or our own city */
        continue;
      }

      v += amount; /* AI wants migration into its cities! */
    } iterate_outward_end;
    break;
  case EFT_MAX_TRADE_ROUTES:
    {
      /* We may know no caravans yet, then consider primary one
       * (Supposed that if this effect is defined then some unit
       * can establish trade routes) */
      int trr = action_id_get_role(ACTION_TRADE_ROUTE);
      const struct unit_type *van = best_role_unit(pcity, trr);
      int bonus;

      if (NULL == van){
        if (0 < num_role_units(trr)) {
          van = get_role_unit(trr, 0);
        }
      }
      /* NOTE: if you have "DiplRel" in reqs, becomes 0 */
      bonus =
        get_target_bonus_effects(NULL,
                                 &(const struct req_context) {
                                   .player = city_owner(pcity),
                                   .city = pcity,
                                   .tile = city_tile(pcity),
                                   .unittype = van
                                 }, NULL, EFT_TRADE_REVENUE_BONUS);

      trait = ai_trait_get_value(TRAIT_TRADER, pplayer);
      v += amount
        * (pow(2.0,
               (double) bonus / 1000.0)
           + c)
        * trait
        / TRAIT_DEFAULT_VALUE;
      if (city_num_trade_routes(pcity) >= max_trade_routes(pcity)
          && amount > 0) {
        /* Has no free trade routes before this */
        v += trait;
      }
    }
    break;
  case EFT_TRADE_ROUTE_PCT:
    {
      int trade = 0;

      trait = ai_trait_get_value(TRAIT_TRADER, pplayer);

      trade_partners_iterate(pcity, tgt) {
        trade += trade_base_between_cities(pcity, tgt);
      } trade_partners_iterate_end;

      v += trade * amount * trait / 100 / TRAIT_DEFAULT_VALUE;

      if (city_num_trade_routes(pcity) < max_trade_routes(pcity)
          && amount > 0) {
        /* Space for future routes */
        v += trait * 5 / TRAIT_DEFAULT_VALUE;
      }
    }
    break;
  case EFT_MAX_STOLEN_GOLD_PM:
    v -= amount / 40;
    break;
  case EFT_THIEFS_SHARE_PM:
    v -= amount / 80;
    break;
  case EFT_RETIRE_PCT:
    num = num_affected_units(peffect, adv);
    v -= amount * num / 20;
    break;
  case EFT_ACTION_SUCCESS_MOVE_COST:
  case EFT_ACTION_SUCCESS_TARGET_MOVE_COST:
    {
      /* Taking MAX_MOVE_FRAGS takes all the move fragments. */
      adv_want move_fragment_cost = MIN(MAX_MOVE_FRAGS, amount);

      /* Lose all movement => 1. */
      v -= move_fragment_cost / (adv_want)MAX_MOVE_FRAGS;
    }
    break;
  case EFT_INFRA_POINTS:
    v += amount * adv->infra_priority;
    break;
  case EFT_USER_EFFECT_1:
  case EFT_USER_EFFECT_2:
  case EFT_USER_EFFECT_3:
  case EFT_USER_EFFECT_4:
    /* The AI has no idea what this does. */
    break;
  case EFT_COUNT:
    log_error("Bad effect type.");
    break;
  }

  return v;
}

/**********************************************************************//**
  Checks recursively to see if the player already has a better government
**************************************************************************/
static bool have_better_government(const struct player *pplayer,
                                   const struct government *pgov)
{
    if (pgov->ai.better) {
      if (pplayer->government == pgov->ai.better) {
        return TRUE;
      } else {
        return have_better_government(pplayer, pgov->ai.better);
      }
    }
    return FALSE;
}
/**********************************************************************//**
  Does the AI expect to ever be able to meet this requirement.

  The return value of this function is unreliable for requirements
  that are currently active: the caller should only call this
  function to determine if a currently *inactive* requirement could
  be met in the future.

  City may be NULL, so request pplayer separately.
**************************************************************************/
bool dai_can_requirement_be_met_in_city(const struct requirement *preq,
                                        const struct player *pplayer,
                                        const struct city *pcity)
{
  switch (preq->source.kind) {
  case VUT_GOVERNMENT:
    /* We can't meet a government requirement if we have a better one. */
    return !have_better_government(pplayer, preq->source.value.govern);

  case VUT_GOVFLAG:
    break;

  case VUT_IMPROVEMENT:
  case VUT_SITE:
  {
    const struct impr_type *pimprove = preq->source.value.building;

    if (preq->present
        && preq->source.kind == VUT_IMPROVEMENT
        && improvement_obsolete(pplayer, pimprove, pcity)) {
      /* Would need to unobsolete a building, which is too hard. */
      return FALSE;
    } else if (!preq->present && pcity != NULL
               && I_NEVER < pcity->built[improvement_index(pimprove)].turn
               && (preq->source.kind != VUT_IMPROVEMENT
                   || !can_improvement_go_obsolete(pimprove))) {
      /* Would need to unbuild an unobsoleteable building, which is too hard. */
      return FALSE;
    } else if (preq->present) {
      requirement_vector_iterate(&pimprove->reqs, ireq) {
        if (!dai_can_requirement_be_met_in_city(ireq, pplayer, pcity)) {
          return FALSE;
        }
      } requirement_vector_iterate_end;
    }
    break;
  } /* VUT_IMPROVEMENT inline block */

  case VUT_SPECIALIST:
    if (preq->present) {
      if (is_super_specialist(preq->source.value.specialist)
          && pcity->specialists[specialist_index(preq->source.value.specialist)] > 0) {
        /* The superspecialist won't leave */
        break;
      }
      requirement_vector_iterate(&(preq->source.value.specialist)->reqs,
                                 sreq) {
        if (!dai_can_requirement_be_met_in_city(sreq, pplayer, pcity)) {
          return FALSE;
        }
      } requirement_vector_iterate_end;
    } /* Almost always there can be a specialist other than given one */
    break;

  case VUT_NATIONALITY:
    /* Crude, but the right answer needs to consider civil wars. */
    return nation_is_in_current_set(preq->source.value.nation);

  case VUT_ORIGINAL_OWNER:
    return FALSE;

  case VUT_CITYSTATUS:
    if (pcity != NULL) {
      if (preq->source.value.citystatus == CITYS_OWNED_BY_ORIGINAL
          && pcity->original != NULL) {
        if (preq->present) {
          return city_owner(pcity) == pcity->original;
        } else {
          return city_owner(pcity) != pcity->original;
        }
      } else if (preq->source.value.citystatus == CITYS_TRANSFERRED) {
        if ((preq->present && pcity->acquire_t == CACQ_FOUNDED)
            || (!preq->present && pcity->acquire_t != CACQ_FOUNDED)) {
          /* Would change only when the AI loses the city */
          return FALSE;
        }
      }
    }
    break;
  case VUT_TERRAIN:
  case VUT_TERRAINCLASS:
  case VUT_TERRAINALTER:
  case VUT_TERRFLAG:
  case VUT_ROADFLAG:
  case VUT_EXTRAFLAG:
  case VUT_EXTRA:
  case VUT_TILEDEF:
    /* TODO: These could be determined by building a map of all
     *       possible futures (e.g. terrain transformations, etc.),
     *       and traversing it for all tiles in largest possible range
     *       of city, and using that to check requirements. */
    break;

  case VUT_ADVANCE:
  case VUT_MINSIZE:
  case VUT_MINYEAR:
  case VUT_TOPO:
  case VUT_WRAP:
  case VUT_AGE:
  case VUT_TECHFLAG:
  case VUT_ACHIEVEMENT:
  case VUT_MINCULTURE:
  case VUT_MINTECHS:
  case VUT_FUTURETECHS:
    /* No way to remove once present. */
    return preq->present;

  case VUT_FORM_AGE:
    /* FIXME: Sometimes it would be possible to convert back and forth */
    return preq->present;

  case VUT_MINFOREIGNPCT:
    /* No way to add once lost. */
    return !preq->present;

  case VUT_NATION:
  case VUT_NATIONGROUP:
  case VUT_AI_LEVEL:
  case VUT_SERVERSETTING:
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
  case VUT_PLAYER_FLAG:
  case VUT_PLAYER_STATE:
    /* Beyond player control. */
    return FALSE;

  case VUT_OTYPE:
  case VUT_IMPR_GENUS:
    /* Can always be achieved. */
    return TRUE;

  case VUT_MINCITIES:
    /* We don't WANT to lose cities */
    return preq->present;

  case VUT_IMPR_FLAG:
    /* TODO: Have at least some checks for this. */
    break;

  case VUT_NONE:
  case VUT_COUNTER:
  case VUT_UTYPE:
  case VUT_UTFLAG:
  case VUT_UCLASS:
  case VUT_UCFLAG:
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
  case VUT_MAXTILETOTALUNITS:
  case VUT_MAXTILETOPUNITS:
  case VUT_STYLE:
  case VUT_UNITSTATE:
  case VUT_ACTIVITY:
  case VUT_CITYTILE:
  case VUT_MINMOVES:
  case VUT_MINVETERAN:
  case VUT_MINHP:
  case VUT_ACTION:
  case VUT_GOOD:
  case VUT_MINCALFRAG:
  case VUT_MAX_DISTANCE_SQ:
  case VUT_MAX_REGION_TILES:
  case VUT_TILE_REL:
  case VUT_COUNT:
    /* No sensible implementation possible with data available. */
    break;
  }

  return TRUE;
}
