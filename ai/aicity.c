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

#include <assert.h>
#include <string.h>

#include "city.h"
#include "combat.h"
#include "effects.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"
#include "unitlist.h"

#include "path_finding.h"
#include "pf_tools.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unithand.h"
#include "unittools.h"

#include "advdomestic.h"
#include "advmilitary.h"
#include "aidata.h"
#include "aihand.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "aicity.h"

/* Iterate over cities within a certain range around a given city
 * (city_here) that exist within a given city list. */
#define city_range_iterate(city_here, list, range, city)            \
{                                                                   \
  Continent_id continent = tile_get_continent(city_here->tile);	    \
  city_list_iterate(list, city) {                                   \
    if ((range == REQ_RANGE_CITY && city == city_here)              \
        || (range == REQ_RANGE_LOCAL && city == city_here)          \
        || (range == REQ_RANGE_CONTINENT                            \
            && tile_get_continent(city->tile) == continent)	    \
        || (range == REQ_RANGE_PLAYER)) {
#define city_range_iterate_end \
  } } city_list_iterate_end; }

#define CITY_EMERGENCY(pcity)                        \
 (pcity->surplus[O_SHIELD] < 0 || city_unhappy(pcity)   \
  || pcity->food_stock + pcity->surplus[O_FOOD] < 0)
#define LOG_BUY LOG_DEBUG

static void resolve_city_emergency(struct player *pplayer, struct city *pcity);
static void ai_sell_obsolete_buildings(struct city *pcity);

/**************************************************************************
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

  specialist_type_iterate(i) {
    if (get_specialist_output(pcity, i, O_LUXURY) >= game.info.happy_cost) {
      providers += pcity->specialists[i];
    }
  } specialist_type_iterate_end;

  return providers;
}

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
           + pcity->ppl_happy[4] * ai->happy_priority
           - pcity->ppl_unhappy[4] * ai->unhappy_priority
           - pcity->ppl_angry[4] * ai->angry_priority
           - pcity->pollution * ai->pollution_priority);

  if (pcity->surplus[O_FOOD] < 0 || pcity->surplus[O_SHIELD] < 0) {
    /* The city is unmaintainable, it can't be good */
    i = MIN(i, 0);
  }

  return i;
}

/**************************************************************************
  Calculates city want from some input values.  Set id to B_LAST if
  nothing in the city has changed, and you just want to know the
  base want of a city.
**************************************************************************/
static inline int city_want(struct player *pplayer, struct city *acity, 
                            struct ai_data *ai, int id)
{
  int want = 0, prod[O_COUNT], bonus[O_COUNT], waste[O_COUNT], i;

  memset(prod, 0, O_COUNT * sizeof(*prod));
  if (id != B_LAST && ai->impr_calc[id] == AI_IMPR_CALCULATE_FULL) {
    bool celebrating = base_city_celebrating(acity);

    /* The below calculation mostly duplicates get_citizen_output(). 
     * We do this only for buildings that we know may change tile
     * outputs. */
    city_map_checked_iterate(acity->tile, x, y, ptile) {
      if (acity->city_map[x][y] == C_TILE_WORKER) {
        output_type_iterate(o) {
          prod[o] += base_city_get_output_tile(x, y, acity, celebrating, o);
        } output_type_iterate_end;
      }
    } city_map_checked_iterate_end;
    add_specialist_output(acity, prod);
  } else {
    assert(sizeof(*prod) == sizeof(*acity->citizen_base));
    memcpy(prod, acity->citizen_base, O_COUNT * sizeof(*prod));
  }

  for (i = 0; i < NUM_TRADEROUTES; i++) {
    prod[O_TRADE] += acity->trade_value[i];
  }
  prod[O_GOLD] += get_city_tithes_bonus(acity);
  output_type_iterate(o) {
    bonus[o] = get_final_city_output_bonus(acity, o);
    waste[o] = city_waste(acity, o, prod[o] * bonus[o] / 100);
  } output_type_iterate_end;
  add_tax_income(pplayer,
		 prod[O_TRADE] * bonus[O_TRADE] / 100 - waste[O_TRADE],
		 prod);
  output_type_iterate(o) {
    prod[o] = prod[o] * bonus[o] / 100 - waste[o];
  } output_type_iterate_end;

  built_impr_iterate(acity, i) {
    prod[O_GOLD] -= improvement_upkeep(acity, i);
  } built_impr_iterate_end;
  /* Unit upkeep isn't handled here.  Unless we do a full city_refresh it
   * won't be changed anyway. */

  want += prod[O_FOOD] * ai->food_priority;
  if (prod[O_SHIELD] != 0) {
    want += prod[O_SHIELD] * ai->shield_priority;
    want -= city_pollution(acity, prod[O_SHIELD]) * ai->pollution_priority;
  }
  want += prod[O_LUXURY] * ai->luxury_priority;
  want += prod[O_SCIENCE] * ai->science_priority;
  want += prod[O_GOLD] * ai->gold_priority;

  return want;
}

/**************************************************************************
  Calculates want for some buildings by actually adding the building and
  measuring the effect.
**************************************************************************/
static int base_want(struct player *pplayer, struct city *pcity, 
                     Impr_type_id id)
{
  struct ai_data *ai = ai_data_get(pplayer);
  int final_want = 0;
  int great_wonders_tmp = 0, small_wonders_tmp = 0;

  if (ai->impr_calc[id] == AI_IMPR_ESTIMATE) {
    return 0; /* Nothing to calculate here. */
  }

  if (!can_build_improvement(pcity, id)) {
    return 0;
  }

  /* Add the improvement */
  city_add_improvement(pcity, id);
  if (is_great_wonder(id)) {
    great_wonders_tmp = game.info.great_wonders[id];
    game.info.great_wonders[id] = pcity->id;
  } else if (is_small_wonder(id)) {
    small_wonders_tmp = pplayer->small_wonders[id];
    pplayer->small_wonders[id] = pcity->id;
  }

  /* Stir, then compare notes */
  city_range_iterate(pcity, pplayer->cities, ai->impr_range[id], acity) {
    final_want += city_want(pplayer, acity, ai, id) - acity->ai.worth;
  } city_range_iterate_end;

  /* Restore */
  city_remove_improvement(pcity, id);
  if (is_great_wonder(id)) {
    game.info.great_wonders[id] = great_wonders_tmp;
  } else if (is_small_wonder(id)) {
    pplayer->small_wonders[id] = small_wonders_tmp;
  }

  return final_want;
}

/************************************************************************** 
  Calculate effects. A few base variables:
    c - number of cities we have in current range
    u - units we have of currently affected type
    v - the want for the improvement we are considering

  This function contains a whole lot of WAGs. We ignore cond_* for now,
  thinking that one day we may fulfill the cond_s anyway. In general, we
  first add bonus for city improvements, then for wonders.

  IDEA: Calculate per-continent aggregates of various data, and use this
  for wonders below for better wonder placements.
**************************************************************************/
static void adjust_building_want_by_effects(struct city *pcity, 
                                            Impr_type_id id)
{
  struct player *pplayer = city_owner(pcity);
  struct impr_type *pimpr = get_improvement_type(id);
  int v = 0;
  int cities[REQ_RANGE_LAST];
  int nplayers = game.info.nplayers - game.info.nbarbarians;
  struct ai_data *ai = ai_data_get(pplayer);
  struct tile *ptile = pcity->tile;
  bool capital = is_capital(pcity);
  struct req_source source = {
    .type = REQ_BUILDING,
    .value = {.building = id}
  };

  /* Remove team members from the equation */
  players_iterate(aplayer) {
    if (aplayer->team
        && aplayer->team == pplayer->team
        && aplayer != pplayer) {
      nplayers--;
    }
  } players_iterate_end;

  if (impr_flag(id, IF_GOLD)) {
    /* Since coinage contains some entirely spurious ruleset values,
     * we need to return here with some spurious want. */
    pcity->ai.building_want[id] = TRADE_WEIGHTING;
    return;
  }

  /* Base want is calculated above using a more direct approach. */
  v += base_want(pplayer, pcity, id);
  if (v != 0) {
    CITY_LOG(LOG_DEBUG, pcity, "%s base_want is %d (range=%d)", 
             get_improvement_name(id), v, ai->impr_range[id]);
  }

  /* Find number of cities per range.  */
  cities[REQ_RANGE_PLAYER] = city_list_size(pplayer->cities);
  cities[REQ_RANGE_WORLD] = cities[REQ_RANGE_PLAYER]; /* kludge. */

  cities[REQ_RANGE_CONTINENT] = ai->stats.cities[ptile->continent];

  cities[REQ_RANGE_CITY] = 1;
  cities[REQ_RANGE_LOCAL] = 0;

  effect_list_iterate(get_req_source_effects(&source), peffect) {
    struct requirement *mypreq;
    bool useful;

    if (is_effect_disabled(pplayer, pcity, pimpr,
			   NULL, NULL, NULL, NULL,
			   peffect)) {
      CITY_LOG(LOG_DEBUG, pcity, "%s has a disabled effect: %s", 
               get_improvement_name(id), effect_type_name(peffect->type));
      continue;
    }

    mypreq = NULL;
    useful = TRUE;
    requirement_list_iterate(peffect->reqs, preq) {
      /* Check if all the requirements for the currently evaluated effect
       * are met, except for having the building that we are evaluating. */
      if (preq->source.type == REQ_BUILDING
	  && preq->source.value.building == id) {
	mypreq = preq;
        continue;
      }
      if (!is_req_active(pplayer, pcity, pimpr, NULL, NULL, NULL, NULL,
			 preq)) {
	useful = FALSE;
	break;
      }
    } requirement_list_iterate_end;

    if (useful) {
      int amount = peffect->value, c = cities[mypreq->range];

      switch (peffect->type) {
      /* These have already been evaluated in base_want() */
      case EFT_CAPITAL_CITY:
      case EFT_UPKEEP_FREE:
      case EFT_POLLU_POP_PCT:
      case EFT_POLLU_PROD_PCT:
      case EFT_OUTPUT_BONUS:
      case EFT_OUTPUT_BONUS_2:
      case EFT_OUTPUT_ADD_TILE:
      case EFT_OUTPUT_INC_TILE:
      case EFT_OUTPUT_PER_TILE:
      case EFT_OUTPUT_WASTE:
      case EFT_OUTPUT_WASTE_BY_DISTANCE:
      case EFT_OUTPUT_WASTE_PCT:
      case EFT_SPECIALIST_OUTPUT:
	  break;

      case EFT_CITY_VISION_RADIUS_SQ:
      case EFT_UNIT_VISION_RADIUS_SQ:
	/* Wild guess.  "Amount" is the number of tiles (on average) that
	 * will be revealed by the effect.  Note that with an omniscient
	 * AI this effect is actually not useful at all. */
	v += c * amount;
	break;

      case EFT_SLOW_DOWN_TIMELINE:
	/* AI doesn't care about these. */
	break;

	/* WAG evaluated effects */
	case EFT_INCITE_COST_PCT:
	  v += c * amount / 100;
	  break;
	case EFT_MAKE_HAPPY:
	  v += (get_entertainers(pcity) + pcity->ppl_unhappy[4]) * 5 * amount;
          if (city_list_size(pplayer->cities)
                > game.info.cityfactor 
                  + get_player_bonus(pplayer, EFT_EMPIRE_SIZE_MOD)) {
            v += c * amount; /* offset large empire size */
          }
          v += c * amount;
	  break;
	case EFT_UNIT_RECOVER:
	  /* TODO */
	  break;
	case EFT_NO_UNHAPPY:
	  v += (get_entertainers(pcity) + pcity->ppl_unhappy[4]) * 30;
	  break;
	case EFT_FORCE_CONTENT:
	case EFT_MAKE_CONTENT:
	  if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
            int factor = 2;

	    v += MIN(amount, pcity->ppl_unhappy[4] + get_entertainers(pcity)) * 35;

            /* Try to build wonders to offset empire size unhappiness */
            if (city_list_size(pplayer->cities) 
                > game.info.cityfactor 
                  + get_player_bonus(pplayer, EFT_EMPIRE_SIZE_MOD)) {
              if (get_player_bonus(pplayer, EFT_EMPIRE_SIZE_MOD) > 0) {
                factor += city_list_size(pplayer->cities) 
                          / get_player_bonus(pplayer, EFT_EMPIRE_SIZE_STEP);
              }
              factor += 2;
            }
	    v += factor * c * amount;
	  }
	  break;
	case EFT_MAKE_CONTENT_MIL_PER:
          if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
	    v += MIN(pcity->ppl_unhappy[4] + get_entertainers(pcity),
		     amount) * 25;
	    v += MIN(amount, 5) * c;
	  }
	  break;
	case EFT_MAKE_CONTENT_MIL:
          if (get_city_bonus(pcity, EFT_NO_UNHAPPY) <= 0) {
	    v += pcity->ppl_unhappy[4] * amount
	      * MAX(unit_list_size(pcity->units_supported), 0) * 2;
	    v += c * MAX(amount + 2, 1);
	  }
	  break;
	case EFT_TECH_PARASITE:
	{
	  int turns;
	  int bulbs;
	  int value;
	  
	  if (nplayers <= amount) {
	    break;
	  }
          
	  turns = 9999;
	  bulbs = 0;
	  players_iterate(aplayer) {
	    int potential = aplayer->bulbs_last_turn
	                    + city_list_size(aplayer->cities) + 1;
	    if (tech_exists(pimpr->obsolete_by)) {
	      turns = MIN(turns, 
	        total_bulbs_required_for_goal(aplayer, pimpr->obsolete_by)
		/ (potential + 1));
	    }
	    if (players_on_same_team(aplayer, pplayer)) {
	      continue;
	    }
	    bulbs += potential;
	  } players_iterate_end;
  
  	  /* For some number of turns we will be receiving bulbs for free
	   * Bulbs should be amortized properly for each turn.
	   * We use formula for the sum of geometric series:
	   */
	  value = bulbs * (1.0 - pow(1.0 - (1.0 / MORT), turns)) * MORT;
	  
	  value = value  * (100 - game.info.freecost)	  
	          * (nplayers - amount) / (nplayers * amount * 100);
	  
	  /* WAG */
	  value /= 3;

          CITY_LOG(LOG_DEBUG, pcity,
	           "%s parasite effect: bulbs %d, turns %d, value %d", 
                   get_improvement_name(id), bulbs, turns, value);
	
	  v += value;
	  break;
	}
	case EFT_GROWTH_FOOD:
	  v += c * 4 + (amount / 7) * pcity->surplus[O_FOOD];
	  break;
	case EFT_AIRLIFT:
	  /* FIXME: We need some smart algorithm here. The below is 
	   * totally braindead. */
	  v += c + MIN(ai->stats.units.land, 13);
	  break;
	case EFT_ANY_GOVERNMENT:
	  if (!can_change_to_government(pplayer, ai->goal.govt.gov)) {
	    v += MIN(MIN(ai->goal.govt.val, 65),
		num_unknown_techs_for_goal(pplayer, ai->goal.govt.req) * 10);
	  }
	  break;
	case EFT_ENABLE_NUKE:
	  /* Treat nuke as a Cruise Missile upgrade */
	  v += 20 + ai->stats.units.missiles * 5;
	  break;
	case EFT_ENABLE_SPACE:
	  if (game.info.spacerace) {
	    v += 5;
	    if (ai->diplomacy.production_leader == pplayer) {
	      v += 100;
	    }
	  }
	  break;
	case EFT_GIVE_IMM_TECH:
 	  if (!ai_wants_no_science(pplayer)) {
 	    v += amount * (game.info.sciencebox + 1);
          }
	  break;
	case EFT_HAVE_EMBASSIES:
	  v += 5 * nplayers;
	  break;
	case EFT_REVEAL_CITIES:
	case EFT_NO_ANARCHY:
	  break;  /* Useless for AI */
	case EFT_NO_SINK_DEEP:
	  v += 15 + ai->stats.units.triremes * 5;
	  break;
	case EFT_NUKE_PROOF:
	  if (ai->threats.nuclear) {
	    v += pcity->size * unit_list_size(ptile->units) * (capital + 1)
	         * amount / 100;
	  }
	  break;
	case EFT_REVEAL_MAP:
	  if (!ai->explore.land_done || !ai->explore.sea_done) {
	    v += 10;
	  }
	  break;
	case EFT_SIZE_UNLIMIT:
	  /* Note we look up the SIZE_UNLIMIT again right below.  This could
	   * be avoided... */
	  if (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) == 0) {
	    amount = 20; /* really big city */
	  }
	  /* there not being a break here is deliberate, mind you */
	case EFT_SIZE_ADJ:
	  if (get_city_bonus(pcity, EFT_SIZE_UNLIMIT) == 0) {
	    const int aqueduct_size = get_city_bonus(pcity, EFT_SIZE_ADJ);

	    if (!city_can_grow_to(pcity, pcity->size + 1)) {
	      v += pcity->surplus[O_FOOD] * ai->food_priority * amount;
	      if (pcity->size == aqueduct_size) {
		v += 30 * pcity->surplus[O_FOOD];
	      }
	    }
	    v += c * amount * 4 / aqueduct_size;
	  }
	  break;
	case EFT_SS_STRUCTURAL:
	case EFT_SS_COMPONENT:
	case EFT_SS_MODULE:
	  if (game.info.spacerace
	      /* If someone has started building spaceship already or
	       * we have chance to win a spacerace */
	      && (ai->diplomacy.spacerace_leader
		  || ai->diplomacy.production_leader == pplayer)) {
	    v += 95;
	  }
	  break;
	case EFT_SPY_RESISTANT:
	  /* Uhm, problem: City Wall has -50% here!! */
	  break;
	case EFT_MOVE_BONUS:
	  /* FIXME: check other reqs (e.g., unitclass) */
	  v += (8 * v * amount + ai->stats.units.land
		+ ai->stats.units.sea + ai->stats.units.air);
	  break;
	case EFT_UNIT_NO_LOSE_POP:
	  v += unit_list_size(ptile->units) * 2;
	  break;
	case EFT_HP_REGEN:
	  /* FIXME: check other reqs (e.g., unitclass) */
	  v += (5 * c + ai->stats.units.land
		+ ai->stats.units.sea + ai->stats.units.air);
	  break;
	case EFT_VETERAN_COMBAT:
	  /* FIXME: check other reqs (e.g., unitclass) */
	  v += (2 * c + ai->stats.units.land + ai->stats.units.sea
		+ ai->stats.units.air);
	  break;
	case EFT_VETERAN_BUILD:
	  /* FIXME: check other reqs (e.g., unitclass, unitflag) */
	  v += (3 * c + ai->stats.units.land + ai->stats.units.sea
		+ ai->stats.units.air);
	  break;
	case EFT_UPGRADE_UNIT:
	  v += ai->stats.units.upgradeable;
	  if (amount == 1) {
	    v *= 2;
	  } else if (amount == 2) {
	    v *= 3;
	  } else {
	    v *= 4;
	  }
	  break;
	case EFT_DEFEND_BONUS:
	  if (ai_handicap(pplayer, H_DEFENSIVE)) {
	    v += amount / 10; /* make AI slow */
	  }
	  if (is_ocean(tile_get_terrain(pcity->tile))) {
	    v += ai->threats.ocean[-tile_get_continent(pcity->tile)]
		 ? amount/5 : amount/20;
	  } else {
	    adjc_iterate(pcity->tile, tile2) {
	      if (is_ocean(tile_get_terrain(tile2))) {
		if (ai->threats.ocean[-tile_get_continent(tile2)]) {
		  v += amount/5;
		  break;
		}
	      }
	    } adjc_iterate_end;
	  }
	  v += (amount/20 + ai->threats.invasions - 1) * c; /* for wonder */
	  if (ai->threats.continent[ptile->continent]
	      || capital
	      || (ai->threats.invasions
		  && is_water_adjacent_to_tile(pcity->tile))) {
	    if (ai->threats.continent[ptile->continent]) {
	      v += amount;
	    } else {
	      v += amount / (!ai->threats.igwall ? (15 - capital * 5) : 15);
	    }
	  }
	  break;
	case EFT_NO_INCITE:
	  if (get_city_bonus(pcity, EFT_NO_INCITE) <= 0) {
	    v += MAX((game.info.diplchance * 2 
                      - game.info.incite_total_factor) / 2
		- game.info.incite_improvement_factor * 5
		- game.info.incite_unit_factor * 5, 0);
	  }
	  break;
	case EFT_GAIN_AI_LOVE:
	  players_iterate(aplayer) {
	    if (aplayer->ai.control) {
	      if (ai_handicap(pplayer, H_DEFENSIVE)) {
		v += amount / 10;
	      } else {
		v += amount / 20;
	      }
	    }
	  } players_iterate_end;
	  break;
        /* Currently not supported for building AI - wait for modpack users */
        case EFT_UNHAPPY_FACTOR:
        case EFT_UPKEEP_FACTOR:
        case EFT_UNIT_UPKEEP_FREE_PER_CITY:
        case EFT_CIVIL_WAR_CHANCE:
        case EFT_EMPIRE_SIZE_MOD:
        case EFT_EMPIRE_SIZE_STEP:
        case EFT_MAX_RATES:
        case EFT_MARTIAL_LAW_EACH:
        case EFT_MARTIAL_LAW_MAX:
        case EFT_RAPTURE_GROW:
        case EFT_UNBRIBABLE_UNITS:
        case EFT_REVOLUTION_WHEN_UNHAPPY:
        case EFT_HAS_SENATE:
        case EFT_INSPIRE_PARTISANS:
        case EFT_HAPPINESS_TO_GOLD:
        case EFT_FANATICS:
        case EFT_NO_DIPLOMACY:
        case EFT_OUTPUT_PENALTY_TILE:
        case EFT_OUTPUT_INC_TILE_CELEBRATE:
	case EFT_TRADE_REVENUE_BONUS:
          break;
	case EFT_LAST:
	  freelog(LOG_ERROR, "Bad effect type.");
	  break;
      }
    }
  } effect_list_iterate_end;

  /* Reduce want if building gets obsoleted soon */
  if (tech_exists(pimpr->obsolete_by)) {
    v -= v / MAX(1, num_unknown_techs_for_goal(pplayer, pimpr->obsolete_by));
  }

  /* Adjust by building cost */
  v -= (impr_build_shield_cost(pimpr->index)
	/ (pcity->surplus[O_SHIELD] * 10 + 1));

  /* Would it mean losing shields? */
  if ((pcity->production.is_unit 
       || (is_wonder(pcity->production.value) 
           && !is_wonder(id))
       || (!is_wonder(pcity->production.value)
           && is_wonder(id)))
      && pcity->turn_last_built != game.info.turn) {
    v -= (pcity->shield_stock / 2) * (SHIELD_WEIGHTING / 2);
  }

  /* Are we wonder city? Try to avoid building non-wonders very much. */
  if (pcity->id == ai->wonder_city && !is_wonder(id)) {
    v /= 5;
  }

  /* Set */
  pcity->ai.building_want[id] = v;
}

/**************************************************************************
  Calculate walking distance to nearest friendly cities from every city.

  The hidden assumption here is that a F_HELP_WONDER unit is like any
  other unit that will use this data.

  pcity->ai.downtown is set to the number of cities within 4 turns of
  the best help wonder unit we can currently produce.
**************************************************************************/
static void calculate_city_clusters(struct player *pplayer)
{
  struct unit_type *punittype;
  struct unit *ghost;
  int range;

  city_list_iterate(pplayer->cities, pcity) {
    pcity->ai.downtown = 0;
  } city_list_iterate_end;

  if (num_role_units(F_HELP_WONDER) == 0) {
    return; /* ruleset has no help wonder unit */
  }

  punittype = best_role_unit_for_player(pplayer, F_HELP_WONDER);
  if (!punittype) {
    punittype = get_role_unit(F_HELP_WONDER, 0); /* simulate future unit */
  }
  ghost = create_unit_virtual(pplayer, NULL, punittype, 0);
  range = unit_move_rate(ghost) * 4;

  city_list_iterate(pplayer->cities, pcity) {
    struct pf_parameter parameter;
    struct pf_map *map;

    ghost->tile = pcity->tile;
    pft_fill_unit_parameter(&parameter, ghost);
    map = pf_create_map(&parameter);

    pf_iterator(map, pos) {
      struct city *acity = tile_get_city(pos.tile);

      if (pos.total_MC > range) {
        break;
      }
      if (!acity) {
        continue;
      }
      if (city_owner(acity) == pplayer) {
        pcity->ai.downtown++;
      }
    } pf_iterator_end;

    pf_destroy_map(map);
  } city_list_iterate_end;

  destroy_unit_virtual(ghost);
}

/**************************************************************************
  Calculate walking distances to wonder city from nearby cities.
**************************************************************************/
static void calculate_wonder_helpers(struct player *pplayer, 
                                     struct ai_data *ai)
{
  struct pf_map *map;
  struct pf_parameter parameter;
  struct unit_type *punittype;
  struct unit *ghost;
  int maxrange;
  struct city *wonder_city = find_city_by_id(ai->wonder_city);

  city_list_iterate(pplayer->cities, acity) {
    acity->ai.distance_to_wonder_city = 0; /* unavailable */
  } city_list_iterate_end;

  if (wonder_city == NULL) {
    return;
  }
  if (wonder_city->owner != pplayer) {
    ai->wonder_city = 0;
    return;
  }

  punittype = best_role_unit_for_player(pplayer, F_HELP_WONDER);
  if (!punittype) {
    return;
  }
  ghost = create_unit_virtual(pplayer, wonder_city, punittype, 0);
  maxrange = unit_move_rate(ghost) * 7;

  pft_fill_unit_parameter(&parameter, ghost);
  map = pf_create_map(&parameter);

  pf_iterator(map, pos) {
    struct city *acity = tile_get_city(pos.tile);

    if (pos.total_MC > maxrange) {
      break;
    }
    if (!acity) {
      continue;
    }
    if (city_owner(acity) == pplayer) {
      acity->ai.distance_to_wonder_city = pos.total_MC;
    }
  } pf_iterator_end;

  pf_destroy_map(map);
  destroy_unit_virtual(ghost);
}

/************************************************************************** 
  Prime pcity->ai.building_want[]
**************************************************************************/
void ai_manage_buildings(struct player *pplayer)
/* TODO:  RECALC_SPEED should be configurable to ai difficulty. -kauf  */
#define RECALC_SPEED 5
{
  struct ai_data *ai = ai_data_get(pplayer);
  struct city *wonder_city = find_city_by_id(ai->wonder_city);

  if (wonder_city && wonder_city->owner != pplayer) {
    /* We lost it to the enemy! */
    ai->wonder_city = 0;
    wonder_city = NULL;
  }

  /* Preliminary analysis - find our Wonder City. Also check if it
   * is sane to continue building the wonder in it. If either does
   * not check out, make a Wonder City. */
  if (!(wonder_city != NULL
        && wonder_city->surplus[O_SHIELD] > 0
        && !wonder_city->production.is_unit
        && is_wonder(wonder_city->production.value)
        && can_build_improvement(wonder_city, 
                                 wonder_city->production.value)
        && !improvement_obsolete(pplayer, wonder_city->production.value)
        && !is_building_replaced(wonder_city, 
                                 wonder_city->production.value))
      || wonder_city == NULL) {
    /* Find a new wonder city! */
    int best_candidate_value = 0;
    struct city *best_candidate = NULL;
    /* Whether ruleset has a help wonder unit type */
    bool has_help = (num_role_units(F_HELP_WONDER) > 0);

    calculate_city_clusters(pplayer);

    city_list_iterate(pplayer->cities, pcity) {
      int value = pcity->surplus[O_SHIELD];

      if (pcity->ai.grave_danger > 0) {
        continue;
      }
      if (is_ocean_near_tile(pcity->tile)) {
        value /= 2;
      }
      /* Downtown is the number of cities within a certain pf range.
       * These may be able to help with caravans. Also look at the whole
       * continent. */
      if (first_role_unit_for_player(pplayer, F_HELP_WONDER)) {
        value += pcity->ai.downtown;
        value += ai->stats.cities[pcity->tile->continent] / 8;
      }
      if (ai->threats.continent[pcity->tile->continent] > 0) {
        /* We have threatening neighbours: -25% */
        value -= value / 4;
      }
      /* Require that there is at least some neighbors for wonder helpers,
       * if ruleset supports it. */
      if (value > best_candidate_value
          && (!has_help || ai->stats.cities[pcity->tile->continent] > 5)
          && (!has_help || pcity->ai.downtown > 3)) {
        best_candidate = pcity;
        best_candidate_value = value;
      }
    } city_list_iterate_end;
    if (best_candidate) {
      CITY_LOG(LOG_DEBUG, best_candidate, "chosen as wonder-city!");
      ai->wonder_city = best_candidate->id;
      wonder_city = best_candidate;
    }
  }
  calculate_wonder_helpers(pplayer, ai);

  /* First find current worth of cities and cache this. */
  city_list_iterate(pplayer->cities, acity) {
    acity->ai.worth = city_want(pplayer, acity, ai, B_LAST);
  } city_list_iterate_end;

  impr_type_iterate(id) {
    if (!can_player_build_improvement(pplayer, id)
        || improvement_obsolete(pplayer, id)) {
      continue;
    }
    city_list_iterate(pplayer->cities, pcity) {
      if (pcity != wonder_city && is_wonder(id)) {
        /* Only wonder city should build wonders! */
        continue;
      }
      if (pplayer->ai.control && pcity->ai.next_recalc > game.info.turn) {
        continue; /* do not recalc yet */
      } else {
        pcity->ai.building_want[id] = 0; /* do recalc */
      }
      if (city_got_building(pcity, id)
          || !can_build_improvement(pcity, id)
          || is_building_replaced(pcity, id)) {
        continue; /* Don't build redundant buildings */
      }
      adjust_building_want_by_effects(pcity, id);
      CITY_LOG(LOG_DEBUG, pcity, "want to build %s with %d", 
               get_improvement_name(id), pcity->ai.building_want[id]);
    } city_list_iterate_end;
  } impr_type_iterate_end;

  /* Reset recalc counter */
  city_list_iterate(pplayer->cities, pcity) {
    if (pcity->ai.next_recalc <= game.info.turn) {
      /* This will spread recalcs out so that no one turn end is 
       * much longer than others */
      pcity->ai.next_recalc = game.info.turn + myrand(RECALC_SPEED) 
                              + RECALC_SPEED;
    }
  } city_list_iterate_end;
}

/**************************************************************************
  Choose a build for the barbarian player.

  TODO: Move this into advmilitary.c
  TODO: It will be called for each city but doesn't depend on the city,
  maybe cache it?  Although barbarians don't normally have many cities, 
  so can be a bigger bother to cache it.
**************************************************************************/
static void ai_barbarian_choose_build(struct player *pplayer, 
				      struct ai_choice *choice)
{
  struct unit_type *bestunit = NULL;
  int i, bestattack = 0;

  /* Choose the best unit among the basic ones */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD); i++) {
    struct unit_type *iunit = get_role_unit(L_BARBARIAN_BUILD, i);

    if (iunit->attack_strength > bestattack) {
      bestunit = iunit;
      bestattack = iunit->attack_strength;
    }
  }

  /* Choose among those made available through other civ's research */
  for(i = 0; i < num_role_units(L_BARBARIAN_BUILD_TECH); i++) {
    struct unit_type *iunit = get_role_unit(L_BARBARIAN_BUILD_TECH, i);

    if (game.info.global_advances[iunit->tech_requirement]
	&& iunit->attack_strength > bestattack) {
      bestunit = iunit;
      bestattack = iunit->attack_strength;
    }
  }

  /* If found anything, put it into the choice */
  if (bestunit) {
    choice->choice = bestunit->index;
    /* FIXME: 101 is the "overriding military emergency" indicator */
    choice->want   = 101;
    choice->type   = CT_ATTACKER;
  } else {
    freelog(LOG_VERBOSE, "Barbarians don't know what to build!");
  }
}

/************************************************************************** 
  Chooses what the city will build.  Is called after the military advisor
  put it's choice into pcity->ai.choice and "settler advisor" put settler
  want into pcity->founder_*.

  Note that AI cheats -- it suffers no penalty for switching from unit to 
  improvement, etc.
**************************************************************************/
static void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice newchoice;
  struct ai_data *ai = ai_data_get(pplayer);

  init_choice(&newchoice);

  if (ai_handicap(pplayer, H_AWAY)
      && city_built_last_turn(pcity)
      && pcity->ai.urgency == 0) {
    /* Don't change existing productions unless we have to. */
    return;
  }

  if( is_barbarian(pplayer) ) {
    ai_barbarian_choose_build(pplayer, &(pcity->ai.choice));
  } else {
    /* FIXME: 101 is the "overriding military emergency" indicator */
    if ((pcity->ai.choice.want <= 100 || pcity->ai.urgency == 0)
        && !(ai_on_war_footing(pplayer) && pcity->ai.choice.want > 0
             && pcity->id != ai->wonder_city)) {
      domestic_advisor_choose_build(pplayer, pcity, &newchoice);
      copy_if_better_choice(&newchoice, &(pcity->ai.choice));
    }
  }

  /* Fallbacks */
  if (pcity->ai.choice.want == 0) {
    /* Fallbacks do happen with techlevel 0, which is now default. -- Per */
    CITY_LOG(LOG_ERROR, pcity, "Falling back - didn't want to build soldiers,"
	     " workers, caravans, settlers, or buildings!");
    pcity->ai.choice.want = 1;
    if (best_role_unit(pcity, F_TRADE_ROUTE)) {
      pcity->ai.choice.choice = best_role_unit(pcity, F_TRADE_ROUTE)->index;
      pcity->ai.choice.type = CT_NONMIL;
    } else if (best_role_unit(pcity, F_SETTLERS)) {
      pcity->ai.choice.choice = best_role_unit(pcity, F_SETTLERS)->index;
      pcity->ai.choice.type = CT_NONMIL;
    } else {
      CITY_LOG(LOG_ERROR, pcity, "Cannot even build a fallback "
	       "(caravan/coinage/settlers). Fix the ruleset!");
      pcity->ai.choice.want = 0;
    }
  }

  if (pcity->ai.choice.want != 0) { 
    ASSERT_REAL_CHOICE_TYPE(pcity->ai.choice.type);

    CITY_LOG(LOG_DEBUG, pcity, "wants %s with desire %d.",
	     (is_unit_choice_type(pcity->ai.choice.type) ?
	      unit_name(get_unit_type(pcity->ai.choice.choice)) :
	      get_improvement_name(pcity->ai.choice.choice)),
	     pcity->ai.choice.want);
    
    if (!pcity->production.is_unit && is_great_wonder(pcity->production.value) 
	&& (is_unit_choice_type(pcity->ai.choice.type) 
	    || pcity->ai.choice.choice != pcity->production.value))
      notify_player(NULL, pcity->tile, E_WONDER_STOPPED,
		       _("The %s have stopped building The %s in %s."),
		       get_nation_name_plural(pplayer->nation),
		       get_impr_name_ex(pcity, pcity->production.value),
		       pcity->name);
    
    if (pcity->ai.choice.type == CT_BUILDING 
	&& is_wonder(pcity->ai.choice.choice)
	&& (pcity->production.is_unit 
	    || pcity->production.value != pcity->ai.choice.choice)) {
      if (is_great_wonder(pcity->ai.choice.choice)) {
	notify_player(NULL, pcity->tile, E_WONDER_STARTED,
			 _("The %s have started building The %s in %s."),
			 get_nation_name_plural(city_owner(pcity)->nation),
			 get_impr_name_ex(pcity, pcity->ai.choice.choice),
			 pcity->name);
      }
      pcity->production.value = pcity->ai.choice.choice;
      pcity->production.is_unit = is_unit_choice_type(pcity->ai.choice.type);
    } else {
      pcity->production.value = pcity->ai.choice.choice;
      pcity->production.is_unit   = is_unit_choice_type(pcity->ai.choice.type);
    }
  }
}

/************************************************************************** 
...
**************************************************************************/
static void try_to_sell_stuff(struct player *pplayer, struct city *pcity)
{
  impr_type_iterate(id) {
    if (can_city_sell_building(pcity, id)
	&& !building_has_effect(id, EFT_DEFEND_BONUS)) {
/* selling walls to buy defenders is counterproductive -- Syela */
      really_handle_city_sell(pplayer, pcity, id);
      break;
    }
  } impr_type_iterate_end;
}

/************************************************************************** 
  Increase maxbuycost.  This variable indicates (via ai_gold_reserve) to 
  the tax selection code how much money do we need for buying stuff.
**************************************************************************/
static void increase_maxbuycost(struct player *pplayer, int new_value)
{
  pplayer->ai.maxbuycost = MAX(pplayer->ai.maxbuycost, new_value);
}

/************************************************************************** 
  Try to upgrade a city's units. limit is the last amount of gold we can
  end up with after the upgrade. military is if we want to upgrade non-
  military or military units.
**************************************************************************/
static void ai_upgrade_units(struct city *pcity, int limit, bool military)
{
  struct player *pplayer = city_owner(pcity);

  unit_list_iterate(pcity->tile->units, punit) {
    struct unit_type *punittype = can_upgrade_unittype(pplayer, punit->type);

    if (military && !IS_ATTACKER(punit)) {
      /* Only upgrade military units this round */
      continue;
    } else if (!military && IS_ATTACKER(punit)) {
      /* Only civilians or tranports this round */
      continue;
    }
    if (punittype) {
      int cost = unit_upgrade_price(pplayer, punit->type, punittype);
      int real_limit = limit;

      /* Triremes are DANGEROUS!! We'll do anything to upgrade 'em. */
      if (unit_flag(punit, F_TRIREME)) {
        real_limit = pplayer->ai.est_upkeep;
      }
      if (pplayer->economic.gold - cost > real_limit) {
        CITY_LOG(LOG_BUY, pcity, "Upgraded %s to %s for %d (%s)",
                 unit_type(punit)->name, punittype->name, cost,
                 military ? "military" : "civilian");
        handle_unit_upgrade(city_owner(pcity), punit->id);
      } else {
        increase_maxbuycost(pplayer, cost);
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
  bool war_footing = ai_on_war_footing(pplayer);

  /* Disband explorers that are at home but don't serve a purpose. 
   * FIXME: This is a hack, and should be removed once we
   * learn how to ferry explorers to new land. */
  city_list_iterate(pplayer->cities, pcity) {
    struct tile *ptile = pcity->tile;
    unit_list_iterate_safe(ptile->units, punit) {
      if (unit_has_role(punit->type, L_EXPLORER)
          && pcity->id == punit->homecity
          && pcity->ai.urgency == 0) {
        CITY_LOG(LOG_BUY, pcity, "disbanding %s to increase production",
                 unit_name(punit->type));
	handle_unit_disband(pplayer,punit->id);
      }
    } unit_list_iterate_safe_end;
  } city_list_iterate_end;
  
  do {
    int limit = cached_limit; /* cached_limit is our gold reserve */
    struct city *pcity = NULL;
    bool expensive; /* don't buy when it costs x2 unless we must */
    int buycost;

    /* Find highest wanted item on the buy list */
    init_choice(&bestchoice);
    city_list_iterate(pplayer->cities, acity) {
      if (acity->ai.choice.want > bestchoice.want && ai_fuzzy(pplayer, TRUE)) {
        bestchoice.choice = acity->ai.choice.choice;
        bestchoice.want = acity->ai.choice.want;
        bestchoice.type = acity->ai.choice.type;
        pcity = acity;
      }
    } city_list_iterate_end;

    /* We found nothing, so we're done */
    if (bestchoice.want == 0) {
      break;
    }

    /* Not dealing with this city a second time */
    pcity->ai.choice.want = 0;

    ASSERT_REAL_CHOICE_TYPE(bestchoice.type);

    /* Try upgrade units at danger location (high want is usually danger) */
    if (pcity->ai.urgency > 1) {
      if (bestchoice.type == CT_BUILDING && is_wonder(bestchoice.choice)) {
        CITY_LOG(LOG_BUY, pcity, "Wonder being built in dangerous position!");
      } else {
        /* If we have urgent want, spend more */
        int upgrade_limit = limit;

        if (pcity->ai.urgency > 1) {
          upgrade_limit = pplayer->ai.est_upkeep;
        }
        /* Upgrade only military units now */
        ai_upgrade_units(pcity, upgrade_limit, TRUE);
      }
    }

    if (pcity->anarchy != 0 && bestchoice.type != CT_BUILDING) {
      continue; /* Nothing we can do */
    }

    /* Cost to complete production */
    buycost = city_buy_cost(pcity);

    if (buycost <= 0) {
      continue; /* Already completed */
    }

    if (bestchoice.type != CT_BUILDING
        && unit_type_flag(get_unit_type(bestchoice.choice), F_CITIES)) {
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
           > unit_build_shield_cost(get_unit_type(bestchoice.choice)) * 2
        && !war_footing) {
       /* Too expensive for an offensive unit */
       continue;
    }

    /* FIXME: Here Syela wanted some code to check if
     * pcity was doomed, and we should therefore attempt
     * to sell everything in it of non-military value */

    if (pplayer->economic.gold - pplayer->ai.est_upkeep >= buycost
        && (!expensive 
            || (pcity->ai.grave_danger != 0 && assess_defense(pcity) == 0)
            || (bestchoice.want > 200 && pcity->ai.urgency > 1))) {
      /* Buy stuff */
      CITY_LOG(LOG_BUY, pcity, "Crash buy of %s for %d (want %d)",
               bestchoice.type != CT_BUILDING
	       ? unit_name(get_unit_type(bestchoice.choice))
               : get_improvement_name(bestchoice.choice), buycost,
               bestchoice.want);
      really_handle_city_buy(pplayer, pcity);
    } else if (pcity->ai.grave_danger != 0 
               && bestchoice.type == CT_DEFENDER
               && assess_defense(pcity) == 0) {
      /* We have no gold but MUST have a defender */
      CITY_LOG(LOG_BUY, pcity, "must have %s but can't afford it (%d < %d)!",
	       unit_name(get_unit_type(bestchoice.choice)),
	       pplayer->economic.gold, buycost);
      try_to_sell_stuff(pplayer, pcity);
      if (pplayer->economic.gold - pplayer->ai.est_upkeep >= buycost) {
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

  freelog(LOG_BUY, "%s wants to keep %d in reserve (tax factor %d)", 
          pplayer->name, cached_limit, pplayer->ai.maxbuycost);
}

/**************************************************************************
  One of the top level AI functions.  It does (by calling other functions):
  worker allocations,
  build choices,
  extra gold spending.
**************************************************************************/
void ai_manage_cities(struct player *pplayer)
{
  pplayer->ai.maxbuycost = 0;

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
  ai_manage_buildings(pplayer);
  TIMING_LOG(AIT_BUILDINGS, TIMER_STOP);

  /* Initialize the infrastructure cache, which is used shortly. */
  initialize_infrastructure_cache(pplayer);
  city_list_iterate(pplayer->cities, pcity) {
    /* Note that this function mungs the seamap, but we don't care */
    TIMING_LOG(AIT_CITY_MILITARY, TIMER_START);
    military_advisor_choose_build(pplayer, pcity, &pcity->ai.choice);
    TIMING_LOG(AIT_CITY_MILITARY, TIMER_STOP);
    if (ai_on_war_footing(pplayer) && pcity->ai.choice.want > 0) {
      continue; /* Go, soldiers! */
    }
    /* Will record its findings in pcity->settler_want */ 
    TIMING_LOG(AIT_CITY_TERRAIN, TIMER_START);
    contemplate_terrain_improvements(pcity);
    TIMING_LOG(AIT_CITY_TERRAIN, TIMER_STOP);

    TIMING_LOG(AIT_CITY_SETTLERS, TIMER_START);
    if (pcity->ai.next_founder_want_recalc <= game.info.turn) {
      /* Will record its findings in pcity->founder_want */ 
      contemplate_new_city(pcity);
      /* Avoid recalculating all the time.. */
      pcity->ai.next_founder_want_recalc = 
        game.info.turn + myrand(RECALC_SPEED) + RECALC_SPEED;
    }
    TIMING_LOG(AIT_CITY_SETTLERS, TIMER_STOP);
  } city_list_iterate_end;

  city_list_iterate(pplayer->cities, pcity) {
    ai_city_choose_build(pplayer, pcity);
  } city_list_iterate_end;

  ai_spend_gold(pplayer);
}

/**************************************************************************
... 
**************************************************************************/
static bool building_unwanted(struct player *plr, Impr_type_id i)
{
#if 0 /* This check will become more complicated now. */ 
  return (ai_wants_no_science(plr)
          && building_has_effect(i, EFT_SCIENCE_BONUS));
#endif
  return FALSE;
}

/**************************************************************************
  Sell an obsolete building if there are any in the city.
**************************************************************************/
static void ai_sell_obsolete_buildings(struct city *pcity)
{
  struct player *pplayer = city_owner(pcity);

  built_impr_iterate(pcity, i) {
    if(can_city_sell_building(pcity, i) 
       && !building_has_effect(i, EFT_DEFEND_BONUS)
	      /* selling city walls is really, really dumb -- Syela */
       && (is_building_replaced(pcity, i)
	   || building_unwanted(city_owner(pcity), i))) {
      do_sell_building(pplayer, pcity, i);
      notify_player(pplayer, pcity->tile, E_IMP_SOLD,
		       _("%s is selling %s (not needed) for %d."), 
		       pcity->name, get_improvement_name(i), 
		       impr_sell_gold(i));
      return; /* max 1 building each turn */
    }
  } built_impr_iterate_end;
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
#define LOG_EMERGENCY LOG_DEBUG
{
  struct city_list *minilist;

  freelog(LOG_EMERGENCY,
          "Emergency in %s (%s, angry%d, unhap%d food%d, prod%d)",
          pcity->name, city_unhappy(pcity) ? "unhappy" : "content",
          pcity->ppl_angry[4], pcity->ppl_unhappy[4],
          pcity->surplus[O_FOOD], pcity->surplus[O_SHIELD]);

  minilist = city_list_new();
  map_city_radius_iterate(pcity->tile, ptile) {
    struct city *acity = ptile->worked;
    int city_map_x, city_map_y;
    bool is_valid;

    if (acity && acity != pcity && acity->owner == pcity->owner)  {
      freelog(LOG_DEBUG, "%s taking over %s's square in (%d, %d)",
              pcity->name, acity->name, ptile->x, ptile->y);
      is_valid = map_to_city_map(&city_map_x, &city_map_y, acity, ptile);
      assert(is_valid);
      if (!is_valid || is_free_worked_tile(city_map_x, city_map_y)) {
	/* Can't remove a worker here. */
        continue;
      }
      server_remove_worker_city(acity, city_map_x, city_map_y);
      acity->specialists[DEFAULT_SPECIALIST]++;
      if (!city_list_find_id(minilist, acity->id)) {
	city_list_prepend(minilist, acity);
      }
    }
  } map_city_radius_iterate_end;
  auto_arrange_workers(pcity);

  if (!CITY_EMERGENCY(pcity)) {
    freelog(LOG_EMERGENCY, "Emergency in %s resolved", pcity->name);
    goto cleanup;
  }

  unit_list_iterate_safe(pcity->units_supported, punit) {
    if (city_unhappy(pcity)
        && punit->unhappiness != 0
        && punit->ai.passenger == 0) {
      UNIT_LOG(LOG_EMERGENCY, punit, "is causing unrest, disbanded");
      handle_unit_disband(pplayer, punit->id);
      city_refresh(pcity);
    }
  } unit_list_iterate_safe_end;

  if (CITY_EMERGENCY(pcity)) {
    freelog(LOG_EMERGENCY, "Emergency in %s remains unresolved", 
            pcity->name);
  } else {
    freelog(LOG_EMERGENCY, 
            "Emergency in %s resolved by disbanding unit(s)", pcity->name);
  }

  cleanup:
  city_list_iterate(minilist, acity) {
    /* otherwise food total and stuff was wrong. -- Syela */
    city_refresh(acity);
    auto_arrange_workers(pcity);
  } city_list_iterate_end;

  city_list_unlink_all(minilist);
  city_list_free(minilist);

  sync_cities();
}
#undef LOG_EMERGENCY
