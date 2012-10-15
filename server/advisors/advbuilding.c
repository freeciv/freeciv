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

/* utility */
#include "rand.h"

/* common */
#include "city.h"
#include "effects.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "player.h"
#include "specialist.h"

/* common/aicore */
#include "path_finding.h"
#include "pf_tools.h"

/* server */
#include "citytools.h"
#include "plrhand.h"
#include "srv_log.h"

/* server/advisors */
#include "advdata.h"
#include "advtools.h"

/* ai */
#include "aicity.h"
#include "aitools.h"
#include "defaultai.h"

#include "advbuilding.h"

#define BA_RECALC_SPEED 5

#define SPECVEC_TAG tech
#define SPECVEC_TYPE struct advance *
#include "specvec.h"

#define SPECVEC_TAG impr
#define SPECVEC_TYPE struct impr_type *
#include "specvec.h"

/* Iterate over cities within a certain range around a given city
 * (city_here) that exist within a given city list. */
#define city_range_iterate(city_here, list, range, city)		\
{									\
  city_list_iterate(list, city) {					\
    if (range == REQ_RANGE_PLAYER					\
     || ((range == REQ_RANGE_CITY || range == REQ_RANGE_LOCAL)		\
      && city == city_here)						\
     || (range == REQ_RANGE_CONTINENT					\
      && tile_continent(city->tile) ==					\
	 tile_continent(city_here->tile))) {

#define city_range_iterate_end						\
    }									\
  } city_list_iterate_end;						\
}

/**************************************************************************
  Calculate walking distance to nearest friendly cities from every city.

  The hidden assumption here is that a F_HELP_WONDER unit is like any
  other unit that will use this data.

  pcity->server.ai.downtown is set to the number of cities within 4 turns of
  the best help wonder unit we can currently produce.
**************************************************************************/
static void calculate_city_clusters(struct player *pplayer)
{
  struct unit_type *punittype;
  struct unit *ghost;
  int range;

  city_list_iterate(pplayer->cities, pcity) {
    def_ai_city_data(pcity)->downtown = 0;
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
    struct pf_map *pfm;
    struct ai_city *city_data = def_ai_city_data(pcity);

    ghost->tile = pcity->tile;
    pft_fill_unit_parameter(&parameter, ghost);
    pfm = pf_map_new(&parameter);

    pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
      struct city *acity = tile_city(ptile);

      if (move_cost > range) {
        break;
      }
      if (!acity) {
        continue;
      }
      if (city_owner(acity) == pplayer) {
        city_data->downtown++;
      }
    } pf_map_move_costs_iterate_end;

    pf_map_destroy(pfm);
  } city_list_iterate_end;

  destroy_unit_virtual(ghost);
}

/**************************************************************************
  Calculate walking distances to wonder city from nearby cities.
**************************************************************************/
static void calculate_wonder_helpers(struct player *pplayer, 
                                     struct ai_data *ai)
{
  struct pf_map *pfm;
  struct pf_parameter parameter;
  struct unit_type *punittype;
  struct unit *ghost;
  int maxrange;
  struct city *wonder_city = game_city_by_number(ai->wonder_city);

  city_list_iterate(pplayer->cities, acity) {
    def_ai_city_data(acity)->distance_to_wonder_city = 0; /* unavailable */
  } city_list_iterate_end;

  if (wonder_city == NULL) {
    return;
  }
  if (city_owner(wonder_city) != pplayer) {
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
  pfm = pf_map_new(&parameter);

  pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
    struct city *acity = tile_city(ptile);

    if (move_cost > maxrange) {
      break;
    }
    if (!acity) {
      continue;
    }
    if (city_owner(acity) == pplayer) {
      def_ai_city_data(acity)->distance_to_wonder_city = move_cost;
    }
  } pf_map_move_costs_iterate_end;

  pf_map_destroy(pfm);
  destroy_unit_virtual(ghost);
}


/**************************************************************************
  Calculates city want from some input values.  Set pimprove to NULL when
  nothing in the city has changed, and you just want to know the
  base want of a city.
**************************************************************************/
static inline int city_want(struct player *pplayer, struct city *acity, 
                            struct ai_data *ai, struct impr_type *pimprove)
{
  int want = 0, prod[O_LAST], bonus[O_LAST], waste[O_LAST], i;

  memset(prod, 0, O_LAST * sizeof(*prod));
  if (NULL != pimprove
   && ai->impr_calc[improvement_index(pimprove)] == AI_IMPR_CALCULATE_FULL) {
    struct tile *acenter = city_tile(acity);
    bool celebrating = base_city_celebrating(acity);

    /* The below calculation mostly duplicates get_worked_tile_output().
     * We do this only for buildings that we know may change tile
     * outputs. */
    city_tile_iterate(city_map_radius_sq_get(acity), acenter, ptile) {
      if (tile_worked(ptile) == acity) {
        output_type_iterate(o) {
          prod[o] += city_tile_output(acity, ptile, celebrating, o);
        } output_type_iterate_end;
      }
    } city_tile_iterate_end;

    add_specialist_output(acity, prod);
  } else {
    fc_assert(sizeof(*prod) == sizeof(*acity->citizen_base));
    memcpy(prod, acity->citizen_base, O_LAST * sizeof(*prod));
  }

  for (i = 0; i < NUM_TRADE_ROUTES; i++) {
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

  city_built_iterate(acity, pimprove) {
    prod[O_GOLD] -= city_improvement_upkeep(acity, pimprove);
  } city_built_iterate_end;
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
                     struct impr_type *pimprove)
{
  struct ai_data *ai = ai_data_get(pplayer);
  int final_want = 0;
  int wonder_player_id = WONDER_NOT_OWNED;
  int wonder_city_id = WONDER_NOT_BUILT;

  if (ai->impr_calc[improvement_index(pimprove)] == AI_IMPR_ESTIMATE) {
    return 0; /* Nothing to calculate here. */
  }

  if (!can_city_build_improvement_now(pcity, pimprove)
      || (is_small_wonder(pimprove)
          && NULL != city_from_small_wonder(pplayer, pimprove))) {
    return 0;
  }

  if (is_wonder(pimprove)) {
    if (is_great_wonder(pimprove)) {
      wonder_player_id =
          game.info.great_wonder_owners[improvement_index(pimprove)];
    }
    wonder_city_id = pplayer->wonders[improvement_index(pimprove)];
  }
  /* Add the improvement */
  city_add_improvement(pcity, pimprove);

  /* Stir, then compare notes */
  city_range_iterate(pcity, pplayer->cities,
                     ai->impr_range[improvement_index(pimprove)], acity) {
    final_want += city_want(pplayer, acity, ai, pimprove)
      - def_ai_city_data(acity)->worth;
  } city_range_iterate_end;

  /* Restore */
  city_remove_improvement(pcity, pimprove);
  if (is_wonder(pimprove)) {
    if (is_great_wonder(pimprove)) {
      game.info.great_wonder_owners[improvement_index(pimprove)] =
          wonder_player_id;
    }

    pplayer->wonders[improvement_index(pimprove)] = wonder_city_id;
  }

  return final_want;
}

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
  How desirable particular effect making people content is for a
  particular city?
**************************************************************************/
static int content_effect_value(const struct player *pplayer,
                                const struct city *pcity,
                                int amount,
                                int num_cities,
                                int happiness_step)
{
  int v = 0;

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
        factor += city_list_size(pplayer->cities) 
          / MAX(get_player_bonus(pplayer, EFT_EMPIRE_SIZE_STEP), 1);
      }
      factor += 2;
    }
    v += factor * num_cities * amount;
  }

  return v;
}

/**************************************************************************
  Unit class affected by this effect.
**************************************************************************/
static struct unit_class *affected_unit_class(const struct effect *peffect)
{
  requirement_list_iterate(peffect->reqs, preq) {
    if (preq->source.kind == VUT_UCLASS) {
      return preq->source.value.uclass;
    }
  } requirement_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Number of AI stats units affected by effect
**************************************************************************/
static int num_affected_units(const struct effect *peffect,
                              const struct ai_data *ai)
{
  struct unit_class *uclass;
  enum unit_move_type move;

  uclass = affected_unit_class(peffect);
  if (uclass) {
    move = uclass_move_type(uclass);
    switch (move) {
     case LAND_MOVING:
         return ai->stats.units.land;
     case SEA_MOVING:
       return ai->stats.units.sea;
     case BOTH_MOVING:
       return ai->stats.units.amphibious;
     case MOVETYPE_LAST:
       break;
    }
  }
  return ai->stats.units.land + ai->stats.units.sea
         + ai->stats.units.amphibious;
}

/**************************************************************************
  How desirable is a particular effect for a particular city?
  Expressed as an adjustment of the base value (v)
  given the number of cities in range (c).
**************************************************************************/
static int improvement_effect_value(struct player *pplayer,
				    struct government *gov,
				    const struct ai_data *ai,
				    const struct city *pcity,
				    const bool capital, 
				    const struct impr_type *pimprove,
				    const struct effect *peffect,
				    const int c,
				    const int nplayers,
				    int v)
{
  int amount = peffect->value;
  struct unit_class *uclass;
  enum unit_move_type move = MOVETYPE_LAST;
  int num;

  switch (peffect->type) {
  /* These (Wonder) effects have already been evaluated in base_want() */
  case EFT_CAPITAL_CITY:
  case EFT_UPKEEP_FREE:
  case EFT_TECH_UPKEEP_FREE:
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

  case EFT_TURN_YEARS:
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
      v += c * amount; /* offset large empire size */
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
    v += content_effect_value(pplayer, pcity, amount, c, FEELING_FINAL);
    break;
  case EFT_MAKE_CONTENT:
    v += content_effect_value(pplayer, pcity, amount, c, FEELING_EFFECT);
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

        if (potential > 0) {
          if (valid_advance(pimprove->obsolete_by)) {
            turns = MIN(turns, 
                        total_bulbs_required_for_goal(aplayer, advance_number(pimprove->obsolete_by))
                        / (potential + 1));
          }
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
	  
      value = value  * (100 - game.server.freecost)	  
	* (nplayers - amount) / (nplayers * amount * 100);
	  
      /* WAG */
      value /= 3;

      CITY_LOG(LOG_DEBUG, pcity,
	       "%s parasite effect: bulbs %d, turns %d, value %d", 
	       improvement_rule_name(pimprove),
	       bulbs,
	       turns,
	       value);
	
      v += value;
      break;
    }
  case EFT_GROWTH_FOOD:
    v += c * 4 + (amount / 7) * pcity->surplus[O_FOOD];
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
  case EFT_NUKE_PROOF:
    if (ai->threats.nuclear) {
      v += pcity->size * unit_list_size(pcity->tile->units) * (capital + 1)
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
    num = num_affected_units(peffect, ai);
    v += (8 * v * amount + num);
    break;
  case EFT_UNIT_NO_LOSE_POP:
    v += unit_list_size(pcity->tile->units) * 2;
    break;
  case EFT_HP_REGEN:
    num = num_affected_units(peffect, ai);
    v += (5 * c + num);
    break;
  case EFT_VETERAN_COMBAT:
    num = num_affected_units(peffect, ai);
    v += (2 * c + num);
    break;
  case EFT_VETERAN_BUILD:
    /* FIXME: check other reqs (e.g., unitflag) */
    num = num_affected_units(peffect, ai);
    v += (3 * c + num);
    break;
  case EFT_UPGRADE_UNIT:
    if (amount == 1) {
      v += ai->stats.units.upgradeable * 2;
    } else if (amount == 2) {
      v += ai->stats.units.upgradeable * 3;
    } else {
      v += ai->stats.units.upgradeable * 4;
    }
    break;
  case EFT_DEFEND_BONUS:
    if (ai_handicap(pplayer, H_DEFENSIVE)) {
      v += amount / 10; /* make AI slow */
    }
    uclass = affected_unit_class(peffect);
    if (uclass) {
      move = uclass_move_type(uclass);
    }

    if (uclass == NULL || move == SEA_MOVING) {
      /* Helps against sea units */
      if (is_ocean_tile(pcity->tile)) {
        v += ai->threats.ocean[-tile_continent(pcity->tile)]
          ? amount/5 : amount/20;
      } else {
        adjc_iterate(pcity->tile, tile2) {
          if (is_ocean_tile(tile2)) {
            if (ai->threats.ocean[-tile_continent(tile2)]) {
              v += amount/5;
              break;
            }
          }
        } adjc_iterate_end;
      }
    }
    v += (amount/20 + ai->threats.invasions - 1) * c; /* for wonder */
    if (capital || uclass == NULL || move != SEA_MOVING) {
      if (ai->threats.continent[tile_continent(pcity->tile)]
          || capital
          || (ai->threats.invasions
              && is_ocean_near_tile(pcity->tile))) {
        if (ai->threats.continent[tile_continent(pcity->tile)]) {
          v += amount;
        } else {
          v += amount / (!ai->threats.igwall ? (15 - capital * 5) : 15);
        }
      }
    }
    break;
  case EFT_NO_INCITE:
    if (get_city_bonus(pcity, EFT_NO_INCITE) <= 0) {
      v += MAX((game.server.diplchance * 2
                - game.server.incite_total_factor) / 2
               - game.server.incite_improvement_factor * 5
               - game.server.incite_unit_factor * 5, 0);
    }
    break;
  case EFT_GAIN_AI_LOVE:
    players_iterate(aplayer) {
      if (aplayer->ai_controlled) {
	if (ai_handicap(pplayer, H_DEFENSIVE)) {
	  v += amount / 10;
	} else {
	  v += amount / 20;
	}
      }
    } players_iterate_end;
    break;
  case EFT_UPGRADE_PRICE_PCT:
    /* This is based on average base upgrade price of 50. */
    v -= ai->stats.units.upgradeable * amount / 2;
    break;
  /* Currently not supported for building AI - wait for modpack users */
  case EFT_CITY_UNHAPPY_SIZE:
  case EFT_UNHAPPY_FACTOR:
  case EFT_UPKEEP_FACTOR:
  case EFT_UNIT_UPKEEP_FREE_PER_CITY:
  case EFT_CIVIL_WAR_CHANCE:
  case EFT_EMPIRE_SIZE_BASE:
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
  case EFT_TILE_WORKABLE:
    break;
    /* This has no effect for AI */
  case EFT_VISIBLE_WALLS:
  case EFT_SHIELD2GOLD_FACTOR:
    break;
  case EFT_TECH_COST_FACTOR:
    v -= amount * 50;
    break;
  case EFT_CITY_RADIUS_SQ:
    v += amount * 10; /* AI wants bigger city radii */
    break;
  case EFT_CITY_BUILD_SLOTS:
    v += amount * 10;
    break;
  case EFT_MIGRATION_PCT:
    /* consider all foreign cities within the set distance */
    iterate_outward(city_tile(pcity), game.server.mgr_distance + 1, ptile) {
      struct city *acity = tile_city(ptile);

      if (!acity || acity == pcity || city_owner(acity) == pplayer) {
        /* no city, the city in the center or own city */
        continue;
      }

      v += amount; /* AI wants migration into its cities! */
    } iterate_outward_end;
    break;
  case EFT_LAST:
    log_error("Bad effect type.");
    break;
  }

  return v;
}

/************************************************************************** 
  Increase the degree to which we want to meet a set of requirements,
  because they will enable construction of an improvement
  with desirable effects.

  v is the desire for the improvement.

  Returns whether all the requirements are met.
**************************************************************************/
static bool adjust_wants_for_reqs(struct player *pplayer,
                                  struct city *pcity, 
                                  struct impr_type *pimprove,
                                  const int v)
{
  bool all_met = TRUE;
  int n_needed_techs = 0;
  int n_needed_improvements = 0;
  struct tech_vector needed_techs;
  struct impr_vector needed_improvements;

  tech_vector_init(&needed_techs);
  impr_vector_init(&needed_improvements);

  requirement_vector_iterate(&pimprove->reqs, preq) {
    const bool active = is_req_active(pplayer, pcity, pimprove,
                                      pcity->tile, NULL, NULL, NULL, preq,
                                      RPT_POSSIBLE);

    if (VUT_ADVANCE == preq->source.kind && !active) {
      /* Found a missing technology requirement for this improvement. */
      tech_vector_append(&needed_techs, preq->source.value.advance);
    } else if (VUT_IMPROVEMENT == preq->source.kind && !active) {
      /* Found a missing improvement requirement for this improvement.
       * For example, in the default ruleset a city must have a Library
       * before it can have a University. */
      impr_vector_append(&needed_improvements, preq->source.value.building);
    }
    all_met = all_met && active;
  } requirement_vector_iterate_end;

  /* If v is negative, the improvement is not worth building,
   * but there is no need to punish research of the technologies
   * that would make it available.
   */
  n_needed_techs = tech_vector_size(&needed_techs);
  if (0 < v && 0 < n_needed_techs) {
    /* Because we want this improvements,
     * we want the techs that will make it possible */
    const int dv = v / n_needed_techs;
    int t;

    for (t = 0; t < n_needed_techs; t++) {
      want_tech_for_improvement_effect(pplayer, pcity, pimprove,
                                       *tech_vector_get(&needed_techs, t), dv);
    }
  }

  /* If v is negative, the improvement is not worth building,
   * but there is no need to punish building the improvements
   * that would make it available.
   */
  n_needed_improvements = impr_vector_size(&needed_improvements);
  if (0 < v && 0 < n_needed_improvements) {
    /* Because we want this improvement,
     * we want the improvements that will make it possible */
    const int dv = v / (n_needed_improvements * 4); /* WAG */
    int i;

    for (i = 0; i < n_needed_improvements; i++) {
      struct impr_type *needed_impr = *impr_vector_get(&needed_improvements, i);
      /* TODO: increase the want for the needed_impr,
       * if we can build it now */
      /* Recurse */
      (void)adjust_wants_for_reqs(pplayer, pcity, needed_impr, dv);
    }
  }

  /* TODO: use a similar method to increase wants for governments
   * that will make this improvement possible? */

  tech_vector_free(&needed_techs);
  impr_vector_free(&needed_improvements);

  return all_met;
}

/************************************************************************** 
  Calculate effects of possible improvements and extra effects of existing
  improvements. Consequently adjust the desirability of those improvements
  or the technologies that would make them possible.

  This function may (indeed, should) be called even for improvements that a city
  already has, or can not (yet) build. For existing improvements,
  it will discourage research of technologies that would make the improvement
  obsolete or reduce its effectiveness, and encourages technologies that would
  improve its effectiveness. For improvements that the city can not yet build
  it will encourage research of the techs and building of the improvements
  that will make the improvement possible.

  A complexity is that there are two sets of requirements to consider:
  the requirements for the building itself, and the requirements for
  the effects for the building.

  A few base variables:
    c - number of cities we have in current range
    u - units we have of currently affected type
    v - the want for the improvement we are considering

  This function contains a whole lot of WAGs. We ignore cond_* for now,
  thinking that one day we may fulfil the cond_s anyway. In general, we
  first add bonus for city improvements, then for wonders.

  IDEA: Calculate per-continent aggregates of various data, and use this
  for wonders below for better wonder placements.
**************************************************************************/
static void adjust_improvement_wants_by_effects(struct player *pplayer,
                                                struct city *pcity, 
                                                struct impr_type *pimprove,
                                                const bool already)
{
  int v = 0;
  int cities[REQ_RANGE_COUNT];
  int nplayers = normal_player_count();
  struct ai_data *ai = ai_data_get(pplayer);
  bool capital = is_capital(pcity);
  bool can_build = TRUE;
  struct government *gov = government_of_player(pplayer);
  struct universal source = {
    .kind = VUT_IMPROVEMENT,
    .value = {.building = pimprove}
  };
  const bool is_coinage = improvement_has_flag(pimprove, IF_GOLD);

  /* Remove team members from the equation */
  players_iterate(aplayer) {
    if (aplayer->team
        && aplayer->team == pplayer->team
        && aplayer != pplayer) {
      nplayers--;
    }
  } players_iterate_end;

  if (is_coinage) {
    /* Since coinage contains some entirely spurious ruleset values,
     * we need to hard-code a sensible want.
     * We must otherwise handle the special IF_GOLD improvement
     * like the others, so the AI will research techs that make it available,
     * for rulesets that do not provide it from the start.
     */
    v += TRADE_WEIGHTING;
  } else {
    /* Base want is calculated above using a more direct approach. */
    v += base_want(pplayer, pcity, pimprove);
    if (v != 0) {
      CITY_LOG(LOG_DEBUG, pcity, "%s base_want is %d (range=%d)", 
               improvement_rule_name(pimprove),
               v,
               ai->impr_range[improvement_index(pimprove)]);
    }
  }

  if (!is_coinage) {
    /* Adjust by building cost */
    /* FIXME: ought to reduce by upkeep cost and amortise by building cost */
    v -= (impr_build_shield_cost(pimprove)
         / (pcity->surplus[O_SHIELD] * 10 + 1));
  }

  /* Find number of cities per range.  */
  cities[REQ_RANGE_PLAYER] = city_list_size(pplayer->cities);
  cities[REQ_RANGE_WORLD] = cities[REQ_RANGE_PLAYER]; /* kludge. */

  cities[REQ_RANGE_CONTINENT] = ai->stats.cities[tile_continent(pcity->tile)];

  cities[REQ_RANGE_CITY] = 1;
  cities[REQ_RANGE_LOCAL] = 0;

  effect_list_iterate(get_req_source_effects(&source), peffect) {
    struct requirement *mypreq = NULL;
    bool active = TRUE;
    int n_needed_techs = 0;
    struct tech_vector needed_techs;

    if (is_effect_disabled(pplayer, pcity, pimprove,
			   NULL, NULL, NULL, NULL,
			   peffect, RPT_CERTAIN)) {
      /* We believe that effect if disabled only if there is no change that it
       * is not. This should lead AI using wider spectrum of improvements.
       *
       * TODO: Select between RPT_POSSIBLE and RPT_CERTAIN dynamically
       * depending how much AI can take risks. */
      continue;
    }

    tech_vector_init(&needed_techs);

    requirement_list_iterate(peffect->reqs, preq) {
      /* Check if all the requirements for the currently evaluated effect
       * are met, except for having the building that we are evaluating. */
      if (VUT_IMPROVEMENT == preq->source.kind
	  && preq->source.value.building == pimprove) {
	mypreq = preq;
        continue;
      }
      if (!is_req_active(pplayer, pcity, pimprove, NULL, NULL, NULL, NULL,
			 preq, RPT_POSSIBLE)) {
	active = FALSE;
	if (VUT_ADVANCE == preq->source.kind) {
	  /* This missing requirement is a missing tech requirement.
	   * This will be for some additional effect
	   * (For example, in the default ruleset, Mysticism increases
	   * the effect of Temples). */
          tech_vector_append(&needed_techs, preq->source.value.advance);
	}
      }
    } requirement_list_iterate_end;

    n_needed_techs = tech_vector_size(&needed_techs);
    if (active || n_needed_techs) {
      const int v1 = improvement_effect_value(pplayer, gov, ai,
					      pcity, capital, 
					      pimprove, peffect,
					      cities[mypreq->range],
					      nplayers, v);
      /* v1 could be negative (the effect could be undesirable),
       * although it is usually positive.
       * For example, in the default ruleset, Communism decreases the
       * effectiveness of a Cathedral. */

      if (active) {
	v = v1;
      } else { /* n_needed_techs */
	/* We might want the technology that will enable this
	 * (additional) effect.
	 * The better the effect, the more we want the technology.
	 * Use (v1 - v) rather than v1 in case there are multiple
	 * effects that have technology requirements and to eliminate the
	 * 'base' effect of the building.
         * We are more interested in (additional) effects that enhance
	 * buildings we already have.
	 */
        const int a = already? 5: 4; /* WAG */
        const int dv = (v1 - v) * a / (4 * n_needed_techs);
	int t;
	for (t = 0; t < n_needed_techs; t++) {
	  want_tech_for_improvement_effect(pplayer, pcity, pimprove,
                                           *tech_vector_get(&needed_techs, t),
                                           dv);
	}
      }
    }

    tech_vector_free(&needed_techs);
  } effect_list_iterate_end;

  if (already && valid_advance(pimprove->obsolete_by)) {
    /* Discourage research of the technology that would make this building
     * obsolete. The bigger the desire for this building, the more
     * we want to discourage the technology. */
    want_tech_for_improvement_effect(pplayer, pcity, pimprove,
				     pimprove->obsolete_by, -v);
  } else if (!already) {
    /* Increase the want for technologies that will enable
     * construction of this improvement, if necessary.
     */
    const bool all_met = adjust_wants_for_reqs(pplayer, pcity, pimprove, v);
    can_build = can_build && all_met;
  }

  if (is_coinage && can_build) {
    /* Could have a negative want for coinage,
     * if we have some stock in a building already. */
    def_ai_city_data(pcity)->building_want[improvement_index(pimprove)] += v;
  } else if (!already && can_build) {
    /* Convert the base 'want' into a building want
     * by applying various adjustments */

    /* Would it mean losing shields? */
    if ((VUT_UTYPE == pcity->production.kind 
        || (is_wonder(pcity->production.value.building)
          && !is_wonder(pimprove))
        || (!is_wonder(pcity->production.value.building)
          && is_wonder(pimprove)))
     && pcity->turn_last_built != game.info.turn) {
      v -= (pcity->shield_stock / 2) * (SHIELD_WEIGHTING / 2);
    }

    /* Reduce want if building gets obsoleted soon */
    if (valid_advance(pimprove->obsolete_by)) {
      v -= v / MAX(1, num_unknown_techs_for_goal(pplayer, advance_number(pimprove->obsolete_by)));
    }

    /* Are we wonder city? Try to avoid building non-wonders very much. */
    if (pcity->id == ai->wonder_city && !is_wonder(pimprove)) {
      v /= 5;
    }

    /* Set */
    def_ai_city_data(pcity)->building_want[improvement_index(pimprove)] += v;
  }
  /* Else we either have the improvement already,
   * or we can not build it (yet) */
}

/************************************************************************** 
  Whether the AI should calculate the building wants for this city
  this turn, ahead of schedule.

  Always recalculate if the city just finished building,
  so we can make a sensible choice for the next thing to build.
  Perhaps the improvement we were building has become obsolete,
  or a different player built the Great Wonder we were building.
**************************************************************************/
static bool should_force_recalc(struct city *pcity)
{
  return city_built_last_turn(pcity)
      || (VUT_IMPROVEMENT == pcity->production.kind
       && !improvement_has_flag(pcity->production.value.building, IF_GOLD)
       && !can_city_build_improvement_later(pcity, pcity->production.value.building));
}

/************************************************************************** 
  Calculate how much an AI player should want to build particular
  improvements, because of the effects of those improvements, and
  increase the want for technologies that will enable buildings with
  desirable effects.
**************************************************************************/
static void adjust_wants_by_effects(struct player *pplayer,
                                    struct city *wonder_city)
{
  /* Clear old building wants.
   * Do this separately from the iteration over improvement types
   * because each iteration could actually update more than one improvement,
   * if improvements have improvements as requirements.
   */
  city_list_iterate(pplayer->cities, pcity) {
    struct ai_city *city_data = def_ai_city_data(pcity);

    if (!pplayer->ai_controlled) {
      /* For a human player, any building is worth building until discarded */
      improvement_iterate(pimprove) {
        city_data->building_want[improvement_index(pimprove)] = 1;
      } improvement_iterate_end;
    } else if (city_data->building_turn <= game.info.turn) {
      /* Do a scheduled recalculation this turn */
      improvement_iterate(pimprove) {
        city_data->building_want[improvement_index(pimprove)] = 0;
      } improvement_iterate_end;
    } else if (should_force_recalc(pcity)) {
      /* Do an emergency recalculation this turn. */
      city_data->building_wait = city_data->building_turn
                                        - game.info.turn;
      city_data->building_turn = game.info.turn;

      improvement_iterate(pimprove) {
        city_data->building_want[improvement_index(pimprove)] = 0;
      } improvement_iterate_end;
    }
  } city_list_iterate_end;

  improvement_iterate(pimprove) {
    const bool is_coinage = improvement_has_flag(pimprove, IF_GOLD);

    /* Handle coinage specially because you can never complete coinage */
    if (is_coinage
     || can_player_build_improvement_later(pplayer, pimprove)) {
      city_list_iterate(pplayer->cities, pcity) {
        struct ai_city *city_data = def_ai_city_data(pcity);

        if (pcity != wonder_city && is_wonder(pimprove)) {
          /* Only wonder city should build wonders! */
          city_data->building_want[improvement_index(pimprove)] = 0;
        } else if ((!is_coinage
                    && !can_city_build_improvement_later(pcity, pimprove))
                   || is_improvement_redundant(pcity, pimprove)) {
          /* Don't consider impossible or redundant buildings */
          city_data->building_want[improvement_index(pimprove)] = 0;
        } else if (pplayer->ai_controlled
                   && city_data->building_turn <= game.info.turn) {
          /* Building wants vary relatively slowly, so not worthwhile
           * recalculating them every turn.
           * We DO want to calculate (tech) wants because of buildings
           * we already have. */
          const bool already = city_has_building(pcity, pimprove);

          adjust_improvement_wants_by_effects(pplayer, pcity, 
                                              pimprove, already);

          fc_assert(!(already
                      && 0 < city_data->building_want
                      [improvement_index(pimprove)]));
        } else if (city_has_building(pcity, pimprove)) {
          /* Never want to build something we already have. */
          city_data->building_want[improvement_index(pimprove)] = 0;
        }
        /* else wait until a later turn */
      } city_list_iterate_end;
    } else {
      /* An impossible improvement */
      city_list_iterate(pplayer->cities, pcity) {
        def_ai_city_data(pcity)->building_want[improvement_index(pimprove)] = 0;
      } city_list_iterate_end;
    }
  } improvement_iterate_end;

#ifdef DEBUG
  /* This logging is relatively expensive, so activate only if necessary */
  city_list_iterate(pplayer->cities, pcity) {
    struct ai_city *city_data = def_ai_city_data(pcity);
    improvement_iterate(pimprove) {
      if (city_data->building_want[improvement_index(pimprove)] != 0) {
        CITY_LOG(LOG_DEBUG, pcity, "want to build %s with %d", 
                 improvement_rule_name(pimprove),
                 city_data->building_want[improvement_index(pimprove)]);
      }
    } improvement_iterate_end;
  } city_list_iterate_end;
#endif /* DEBUG */
}

/************************************************************************** 
  Prime pcity->server.ai.building_want[]
**************************************************************************/
void building_advisor(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);
  struct city *wonder_city = game_city_by_number(ai->wonder_city);

  if (wonder_city && city_owner(wonder_city) != pplayer) {
    /* We lost it to the enemy! */
    ai->wonder_city = 0;
    wonder_city = NULL;
  }

  /* Preliminary analysis - find our Wonder City. Also check if it
   * is sane to continue building the wonder in it. If either does
   * not check out, make a Wonder City. */
  if (NULL == wonder_city
   || 0 >= wonder_city->surplus[O_SHIELD]
   || VUT_UTYPE == wonder_city->production.kind /* changed to defender? */
   || !is_wonder(wonder_city->production.value.building)
   || !can_city_build_improvement_now(wonder_city, 
                                      wonder_city->production.value.building)
   || is_improvement_redundant(wonder_city, 
                               wonder_city->production.value.building)
      ) {
    /* Find a new wonder city! */
    int best_candidate_value = 0;
    struct city *best_candidate = NULL;
    /* Whether ruleset has a help wonder unit type */
    bool has_help = (num_role_units(F_HELP_WONDER) > 0);

    calculate_city_clusters(pplayer);

    city_list_iterate(pplayer->cities, pcity) {
      int value = pcity->surplus[O_SHIELD];
      Continent_id place = tile_continent(pcity->tile);
      struct ai_city *city_data = def_ai_city_data(pcity);

      if (city_data->grave_danger > 0) {
        continue;
      }
      if (is_ocean_near_tile(pcity->tile)) {
        value /= 2;
      }
      /* Downtown is the number of cities within a certain pf range.
       * These may be able to help with caravans. Also look at the whole
       * continent. */
      if (first_role_unit_for_player(pplayer, F_HELP_WONDER)) {
        value += city_data->downtown;
        value += ai->stats.cities[place] / 8;
      }
      if (ai->threats.continent[place] > 0) {
        /* We have threatening neighbours: -25% */
        value -= value / 4;
      }
      /* Require that there is at least some neighbors for wonder helpers,
       * if ruleset supports it. */
      if (value > best_candidate_value
          && (!has_help || ai->stats.cities[place] > 5)
          && (!has_help || city_data->downtown > 3)) {
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
    def_ai_city_data(acity)->worth = city_want(pplayer, acity, ai, NULL);
  } city_list_iterate_end;

  adjust_wants_by_effects(pplayer, wonder_city);

  /* Reset recalc counter */
  city_list_iterate(pplayer->cities, pcity) {
    struct ai_city *city_data = def_ai_city_data(pcity);

    if (city_data->building_turn <= game.info.turn) {
      /* This will spread recalcs out so that no one turn end is 
       * much longer than others */
      city_data->building_wait = fc_rand(BA_RECALC_SPEED) + BA_RECALC_SPEED;
      city_data->building_turn = game.info.turn
        + city_data->building_wait;
    }
  } city_list_iterate_end;
}
