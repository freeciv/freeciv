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

#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "rand.h"
#include "unit.h"
#include "unittype.h"

#include "citytools.h"
#include "settlers.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aidata.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "advdomestic.h"

/**************************************************************************
  Calculate desire for land defense. First look for potentially hostile 
  cities on our continent, indicating danger of being attacked by land. 
  Then check if we are on the shoreline because we want to stop invasions
  too - but only if a non-allied player has started using sea units yet. 
  If they are slow to go on the offensive, we should expand or attack.

  Returns a base value of desire for land defense building.

  FIXME: We should have a concept of "oceans" just like we do for
  continents. Too many times I've seen the AI build destroyers and 
  coastal defences in inlands cities with a small pond adjacent. - Per
**************************************************************************/
static int ai_eval_threat_land(struct player *pplayer, struct city *pcity)
{
  struct ai_data *ai = ai_data_get(pplayer);
  Continent_id continent;
  bool vulnerable;

  /* make easy AI dumb */
  if (ai_handicap(pplayer, H_DEFENSIVE)) {
    return 40;
  }

  continent = map_get_continent(pcity->x, pcity->y);
  vulnerable = ai->threats.continent[continent]
               || city_got_building(pcity, B_PALACE)
               || (ai->threats.invasions
                   && is_water_adjacent_to_tile(pcity->x, pcity->y));

  return vulnerable ? TRADE_WEIGHTING + 2 : 1; /* trump coinage, and sam */
}

/**************************************************************************
  Calculate desire for coastal defence building.
**************************************************************************/
static int ai_eval_threat_sea(struct player *pplayer, struct city *pcity)
{
  struct ai_data *ai = ai_data_get(pplayer);

  /* make easy AI stay dumb */
  if (ai_handicap(pplayer, H_DEFENSIVE)) {
    return 40;
  }

  /* trump coinage, and wall, and sam */
  return ai->threats.sea ? TRADE_WEIGHTING + 3 : 1;
}

/**************************************************************************
  Calculate desire for air defence building.
**************************************************************************/
static int ai_eval_threat_air(struct player *pplayer, struct city *pcity)
{
  struct ai_data *ai = ai_data_get(pplayer);
  Continent_id continent;
  bool vulnerable;

  /* make easy AI dumber */
  if (ai_handicap(pplayer, H_DEFENSIVE)) {
    return 50;
  }

  continent = map_get_continent(pcity->x, pcity->y);
  vulnerable = ai->threats.air
               && (ai->threats.continent[continent]
                   || is_water_adjacent_to_tile(pcity->x, pcity->y) 
                   || city_got_building(pcity, B_PALACE));

  return vulnerable ? TRADE_WEIGHTING + 1 : 1; /* trump coinage */
}

/**************************************************************************
  Calculate need for nuclear defence. Look for players capable of building
  nuclear weapons. Capable of building, not has, because at this stage in
  the game we can get nukes and use nukes faster than we can manage to
  build SDIs. We also look if players _have_ nukes, and increase our
  want for SDIs correspondingly. Lastly, we check if we are in a strategic
  position (ie near water or on a continent with non-allied cities).
**************************************************************************/
static int ai_eval_threat_nuclear(struct player *pplayer, struct city *pcity)
{
  struct ai_data *ai = ai_data_get(pplayer);
  Continent_id continent;
  int vulnerable = 1;

  /* make easy AI really dumb, like it was before */
  if (ai_handicap(pplayer, H_DEFENSIVE)) {
    return 50;
  }

  /* No non-allied player has nuclear capability yet. */
  if (ai->threats.nuclear == 0) { return 0; }

  continent = map_get_continent(pcity->x, pcity->y);
  if (ai->threats.continent[continent]
      || is_water_adjacent_to_tile(pcity->x, pcity->y)
      || city_got_building(pcity, B_PALACE)) {
    vulnerable++;
  }

  /* 15 is just a new magic number, which will be +15 if we are
     in a vulnerable spot, and +15 if someone we are not allied
     with really have built nukes. This might be overkill, but
     at least it is not as bad as it used to be. */
  return ((ai->threats.nuclear + vulnerable) * 15);
}

/**************************************************************************
  Calculate desire for missile defence.
**************************************************************************/
static int ai_eval_threat_missile(struct player *pplayer, struct city *pcity)
{
  struct ai_data *ai = ai_data_get(pplayer);
  Continent_id continent = map_get_continent(pcity->x, pcity->y);
  bool vulnerable = is_water_adjacent_to_tile(pcity->x, pcity->y)
                    || ai->threats.continent[continent]
                    || city_got_building(pcity, B_PALACE);

  /* Only build missile defence if opponent builds them, and we are in
     a vulnerable spot. Trump coinage but not wall or coastal. */
  return (ai->threats.missile && vulnerable) ? TRADE_WEIGHTING + 1 : 1;
}

/**************************************************************************
Returns the value (desire to build it) of the improvement for keeping
of order in the city.

Improvements which increase happiness have two benefits. They can prevent
cities from falling into disorder, and free the population changed into elvis
specialists (so you can work tiles with value like tileval with them).  Happy
is number of people the improvement would make happy.

This function lets you decide if the happiness improvement such as the Temple,
Cathedral or WoW is worth the cost of the elvises keeping your citizens content
or the cost of the potential disorder (basically how much citizens would be
unhappy * SADVAL).

Note that an elvis citizen is a parasite. The fewer you have, the more
food/prod/trade your city produces.
**************************************************************************/
static int impr_happy_val(struct city *pcity, int happy, int tileval)
{
  /* How much one rebeling citizen counts - 16 is debatable value */
#define SADVAL 16
  /* Number of elvises in the city */
  int elvis = pcity->specialists[SP_ELVIS];
  /* Raw number of unhappy people */
  int sad = pcity->ppl_unhappy[0];
  /* Final number of content people */
  int content = pcity->ppl_content[4];
  /* Resulting value */
  int value = 0;

  /* Count people made happy by luxury as content. */
  if (pcity->ppl_content[1] < pcity->ppl_content[0])
    content += pcity->ppl_content[0] - pcity->ppl_content[1];

  /* If units are making us unhappy, count that too. */
  sad += pcity->ppl_unhappy[3] - pcity->ppl_unhappy[2];
  /* Angry citizens have to be counted double, as you have to first make them
   * unhappy and then content. */
  sad += pcity->ppl_angry[0] * 2;
  
  freelog(LOG_DEBUG, "In %s, unh[0] = %d, unh[4] = %d, sad = %d",
	  pcity->name, pcity->ppl_unhappy[0], pcity->ppl_unhappy[4], sad);

  /* The cost of the elvises. */
  while (happy > 0 && elvis > 0) { happy--; elvis--; value += tileval; }
  /* The cost of the rebels. */
  while (happy > 0 && sad > 0) { happy--; sad--; value += SADVAL; }
  
  /* Desperately seeking Colosseum - we need happy improvements urgently. */
  if (city_unhappy(pcity))
    value += SADVAL * (sad + content);
  
  /* Usage of (happy && content) led to a lack of foresight, especially
   * re: Chapel -- Syela */
  while (happy > 0) { happy--; value += SADVAL; }
  
  freelog(LOG_DEBUG, "%s: %d elvis %d sad %d content %d size %d val",
	  pcity->name, pcity->specialists[SP_ELVIS], pcity->ppl_unhappy[4],
	  pcity->ppl_content[4], pcity->size, value);

  return value;
#undef SADVAL
}

/**************************************************************************
Return a weighted value for a city's Ocean tiles
(i.e. based on the total number and number actively worked).
**************************************************************************/
static int ocean_workers(struct city *pcity)
{
  int i = 0;

  city_map_checked_iterate(pcity->x, pcity->y, cx, cy, mx, my) {
    if (is_ocean(map_get_terrain(mx, my))) {
      /* This is a kluge; wasn't getting enough harbors because often
       * everyone was stuck farming grassland. */
      i++;

      if (is_worker_here(pcity, cx, cy))
	i++;
    }
  } city_map_checked_iterate_end;
  
  return i / 2;
}

/**************************************************************************
Number of road tiles actively worked.
**************************************************************************/
static int road_trade(struct city *pcity)
{
  int i = 0;

  city_map_checked_iterate(pcity->x, pcity->y, cx, cy, mx, my) {
    if (map_has_special(mx, my, S_ROAD)
	&& is_worker_here(pcity, cx, cy))
      i++;
  } city_map_checked_iterate_end;

  return i; 
}

/**************************************************************************
Number of farmland tiles actively worked.
**************************************************************************/
static int farmland_food(struct city *pcity)
{
  int i = 0;

  city_map_checked_iterate(pcity->x, pcity->y, cx, cy, mx, my) {
    if (map_has_special(mx, my, S_FARMLAND)
	&& is_worker_here(pcity, cx, cy))
      i++;
  } city_map_checked_iterate_end;

  return i; 
}

/**************************************************************************
Pollution benefit or cost of given improvement.

Positive return value: less pollution if this improvement would be built
Negative return value: more pollution if this improvement would be built
Bigger absolute value means greater effect, naturally.
**************************************************************************/
static int pollution_benefit(struct player *pplayer, struct city *pcity,
			     Impr_Type_id impr_type)
{
  /* Generated pollution */
  int pollution = 0;
  /* Production bonus of improvement */
  int prodbonus = 0;
  /* How seriously we should take it */
  int weight;
  /* Pollution cost */
  int cost = 0;

  /* Count total production of worked tiles, that's base of pollution value. */
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      pollution += city_get_shields_tile(x, y, pcity);
    }
  } city_map_iterate_end;

  /* Count production bonuses generated by various buildings. */
  if (city_got_building(pcity, B_FACTORY) || impr_type == B_FACTORY) {
    prodbonus += 50;
    if (city_got_building(pcity, B_MFG) || impr_type == B_MFG) {
      prodbonus *= 2;
    }
  }

  /* Count bonuses for that production bonuses ;-). */
  if (city_affected_by_wonder(pcity, B_HOOVER) || impr_type == B_HOOVER ||
      city_got_building(pcity, B_POWER) || impr_type == B_POWER ||
      city_got_building(pcity, B_HYDRO) || impr_type == B_HYDRO ||
      city_got_building(pcity, B_NUCLEAR) || impr_type == B_NUCLEAR) {
    prodbonus = (prodbonus * 3) / 2;
  }
  
  /* And now apply the bonuses. */
  pollution = pollution * (prodbonus + 100) / 100;

  /* Count buildings which reduce the pollution. */
  if (city_got_building(pcity, B_RECYCLING) || impr_type == B_RECYCLING)
    pollution /= 3;
  
  else if (city_got_building(pcity, B_HYDRO) || impr_type == B_HYDRO ||
           city_affected_by_wonder(pcity, B_HOOVER) || impr_type == B_HOOVER ||
           city_got_building(pcity, B_NUCLEAR) || impr_type == B_NUCLEAR)
    pollution /= 2;

  /* Count technologies which affect the pollution. */
  if (! city_got_building(pcity, B_MASS) && impr_type != B_MASS) {
    pollution += (num_known_tech_with_flag(pplayer,
					   TF_POPULATION_POLLUTION_INC) *
		  pcity->size) / 4;
  }
  
  /* Heuristic? */
  pollution -= 20;
  if (pollution < 0) pollution = 0;
  
  weight = (POLLUTION_WEIGHTING + pplayer->ai.warmth) * 64;
  
  /* Base is cost of actual pollution. */
  
  if (pcity->pollution > 0) {
    int amortized = amortize(weight, 100 / pcity->pollution);
    
    cost = ((amortized * weight) / (MAX(1, weight - amortized))) / 64;
    
    freelog(LOG_DEBUG, "City: %s, Pollu: %d, cost: %d, Id: %d, P: %d",
	    pcity->name, pcity->pollution, cost, impr_type, pollution);
  }
  
  /* Subtract cost of future pollution. */
  
  if (pollution > 0) {
    int amortized = amortize(weight, 100 / pollution);
  
    cost -= ((amortized * weight) / (MAX(1, weight - amortized))) / 64;
  }
  
  return cost;
}

/****************************************************************************
  Return TRUE if the given wonder is already being built by pcity owner in
  another city (if so we probably don't want to build it here, although
  we may want to start building it and switch later).
****************************************************************************/
static bool built_elsewhere(struct city *pcity, Impr_Type_id wonder)
{
  city_list_iterate(city_owner(pcity)->cities, acity) {
    if (pcity != acity && !acity->is_building_unit
	&& pcity->currently_building == wonder) {
      return TRUE;
    }
  } city_list_iterate_end;

  return FALSE;
}

/**************************************************************************
  Returns the city_tile_value of the worst tile worked by pcity 
  (not including the city center).  Returns 0 if no tiles are worked.
**************************************************************************/
static int worst_worker_tile_value(struct city *pcity)
{
  int worst = 0;

  city_map_iterate(x, y) {
    if (is_city_center(x, y)) {
      continue;
    }
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      int tmp = city_tile_value(pcity, x, y, 0, 0);

      if (tmp < worst || worst == 0) {
	worst = tmp;
      }
    }
  } city_map_iterate_end;

  return worst;
}

/**************************************************************************
  Get city_tile_value of best unused tile available to pcity.  Returns
  0 if no tiles are available.
**************************************************************************/
static int best_free_tile_value(struct city *pcity)
{
  int best = 0;

  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y) == C_TILE_EMPTY) {
      int tmp = city_tile_value(pcity, x, y, 0, 0);
      
      if (tmp > best) {
	best = tmp;
      }
    }
  } city_map_iterate_end;
  
  return best;
}

/**************************************************************************
  Returns the amount of food consumed by pcity's units.
**************************************************************************/
static int settler_eats(struct city *pcity)
{
  struct government *gov = get_gov_pcity(pcity);
  int free_food = citygov_free_food(pcity, gov);
  int eat = 0;

  unit_list_iterate(pcity->units_supported, this_unit) {
    int food_cost = utype_food_cost(unit_type(this_unit), gov);

    adjust_city_free_cost(&free_food, &food_cost);
    if (food_cost > 0) {
      eat += food_cost;
    }
  } unit_list_iterate_end;

  return eat;
}

/**************************************************************************
  Evaluate the current desirability of all city improvements for the given 
  city to update pcity->ai.building_want.
**************************************************************************/
void ai_eval_buildings(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  struct player *pplayer = city_owner(pcity);
  int bar, est_food, food, grana, hunger, needpower;
  int tprod, prod, sci, tax, t, val, wwtv;
  int j, k;
  int values[B_LAST];
  int nplayers = game.nplayers 
                 - team_count_members_alive(pplayer->team);

  /* --- initialize control parameters --- */
  t = pcity->ai.trade_want;  /* trade_weighting */
  sci = (pcity->trade_prod * pplayer->economic.science + 50) / 100;
  tax = pcity->trade_prod - sci;

  /* better might be a longterm weighted average, this is a quick-n-dirty fix 
  -- Syela */
  sci = ((sci + pcity->trade_prod) * t)/2;
  tax = ((tax + pcity->trade_prod) * t)/2;
  /* don't need libraries!! */
  if (ai_wants_no_science(pplayer)) {
    sci = 0;
  }

  est_food = (2 * pcity->specialists[SP_SCIENTIST]
	      + 2 * pcity->specialists[SP_TAXMAN]
	      + pcity->food_surplus);
  prod = 
    (pcity->shield_prod * SHIELD_WEIGHTING * 100) / city_shield_bonus(pcity);
  needpower = (city_got_building(pcity, B_MFG) ? 2 :
              (city_got_building(pcity, B_FACTORY) ? 1 : 0));
  val = best_free_tile_value(pcity);
  wwtv = worst_worker_tile_value(pcity);

  /* because the benefit doesn't come immediately, and to stop stupidity   */
  /* the AI used to really love granaries for nascent cities, which is OK  */
  /* as long as they aren't rated above Settlers and Laws is above Pottery */
  /* -- Syela */
  grana = MAX(2, pcity->size);
  food  = food_weighting(grana);
  grana = food_weighting(grana + 1);
  hunger = 1;
  j = (pcity->size * 2) + settler_eats(pcity) - pcity->food_prod;
  if (j >= 0
      && pcity->specialists[SP_SCIENTIST] <= 0
      && pcity->specialists[SP_TAXMAN] <= 0) {
    hunger += j + 1;
  }

  /* rationale: barracks effectively doubles prod if building military units */
  /* if half our production is military, effective gain is 1/2 city prod     */
  bar = ((!pcity->is_building_unit && pcity->currently_building == B_SUNTZU)
      || built_elsewhere(pcity, B_SUNTZU)) ? 1 : 0;
  j = 0; k = 0, tprod = 0;
  city_list_iterate(pplayer->cities, acity) {
    k++;
    tprod += acity->shield_prod;
    if (acity->is_building_unit) {
      if (!unit_type_flag(acity->currently_building, F_NONMIL) 
       && unit_types[acity->currently_building].move_type == LAND_MOVING)
        j += prod;
      else 
      if (unit_type_flag(acity->currently_building, F_HELP_WONDER) && bar != 0)
       j += prod;                              /* this also stops flip-flops */
    } else 
      if (acity->currently_building == B_BARRACKS   /* this stops flip-flops */
       || acity->currently_building == B_BARRACKS2
       || acity->currently_building == B_BARRACKS3
       || acity->currently_building == B_SUNTZU)
       j += prod;
  } city_list_iterate_end;
  bar = j > 0 ? j / k : 0;

  /* --- clear to initialize values ... and go --- */
  memset(values, 0, sizeof(values));

  /* Buildings first, values are used in Wonder section next */
  impr_type_iterate(id) {
    if ( is_wonder(id) 
      || !can_build_improvement(pcity, id))
      continue;

    switch (id) {
    
    /* food/growth production */
    case B_AQUEDUCT:
    case B_SEWER:
      /* Aqueducts and Sewers are similar */
      k = (id == B_AQUEDUCT ? game.aqueduct_size : game.sewer_size);
      if (pcity->size >= k-1 && est_food > 0) {
        /* need it to grow, but don't force */
        j = ((city_happy(pcity) || pcity->size > k) ? -100 : -66);
      }
      else {
        /* how long (== how much food) till we grow big enough? 
         * j = num_pop_rollover * biggest_granary_size - current_food */
        j = (k+1) - MIN(k, pcity->size);
	j *= city_granary_size(MAX(k, pcity->size));
        j-= pcity->food_stock;
  
        /* value = some odd factors / food_required */
        j = (k+1) * food * est_food * game.foodbox / MAX(1, j);
      }
      values[id] = j;
      break;
    case B_GRANARY:
      if (improvement_variant(B_PYRAMIDS) != 0
          || !built_elsewhere(pcity, B_PYRAMIDS))
        values[id] = grana * pcity->food_surplus;
      break;
    case B_HARBOUR:
      values[id] = ocean_workers(pcity) * food * hunger;
      break;
    case B_SUPERMARKET:
      if (player_knows_techs_with_flag(pplayer, TF_FARMLAND))
        values[id] = farmland_food(pcity) * food * hunger;
      break;
  
    /* science production */
    case B_RESEARCH:
      if (built_elsewhere(pcity, B_SETI))
        break;
    case B_UNIVERSITY:
    case B_LIBRARY:
      values[id] = sci/2;
      break;
  
    /* happiness generation - trade production */
    case B_MARKETPLACE:
    case B_BANK:
    case B_STOCK:
      values[id] = (tax + 3 * pcity->specialists[SP_TAXMAN]
		    + pcity->specialists[SP_ELVIS] * wwtv) / 2;
      break;
    case B_SUPERHIGHWAYS:
      values[id] = road_trade(pcity) * t;
      break;
    case B_CAPITAL:
      values[id] = TRADE_WEIGHTING; /* a kind of default */
      break;
  
    /* unhappiness relief */
    case B_TEMPLE:
      values[id] = impr_happy_val(pcity, get_temple_power(pcity), val);
      break;
    case B_CATHEDRAL:
      if (improvement_variant(B_MICHELANGELO) == 1 
          || !built_elsewhere(pcity, B_MICHELANGELO))
        values[id]= impr_happy_val(pcity, get_cathedral_power(pcity), val);
      else
      if (tech_exists(game.rtech.cathedral_plus)
        && get_invention(pplayer, game.rtech.cathedral_plus) != TECH_KNOWN)
        values[id]= impr_happy_val(pcity, 4, val) - impr_happy_val(pcity, 3, val);
      break;
    case B_COLOSSEUM:
      values[id] = impr_happy_val(pcity, get_colosseum_power(pcity), val);
      break;
    case B_COURTHOUSE:
      if (g->corruption_level == 0) {
        values[id] += impr_happy_val(pcity, 1, val);
      } else {
        /* The use of TRADE_WEIGHTING here is deliberate, since
         * t is corruption modified. */
        values[id] = (pcity->corruption * TRADE_WEIGHTING) / 2;
      }
      break;
    
    /* shield production and pollution */
    case B_OFFSHORE:  /* ignoring pollu. FIX? */
      values[B_OFFSHORE] = ocean_workers(pcity) * SHIELD_WEIGHTING;
      break;
    case B_FACTORY:
    case B_MFG:
      values[id] = 
        (( city_got_building(pcity, B_HYDRO)
        || city_got_building(pcity, B_NUCLEAR)
        || city_got_building(pcity, B_POWER)
        || city_affected_by_wonder(pcity, B_HOOVER))
        ?  (prod * 3)/4 : prod/2) 
        + pollution_benefit(pplayer, pcity, id);
      break;
    case B_HYDRO: 
    case B_NUCLEAR: 
    case B_POWER: 
      if ( !city_got_building(pcity, B_HYDRO)
        && !city_got_building(pcity, B_NUCLEAR)
        && !city_got_building(pcity, B_POWER)
        && !city_affected_by_wonder(pcity, B_HOOVER))
        values[id] = (needpower*prod)/4 + pollution_benefit(pplayer, pcity, id);
      break;
    case B_MASS:
    case B_RECYCLING:
      values[id] = pollution_benefit(pplayer, pcity, id);
      break;
  
    /* defense/protection */
    case B_BARRACKS:
    case B_BARRACKS2:
    case B_BARRACKS3:
      if (!built_elsewhere(pcity, B_SUNTZU)) 
        values[id] = bar;
      break;
  
    /* military advisor will deal with CITY and PORT */
  
    /* old wall code depended on danger, was a CPU hog and didn't really work 
     * anyway. It was so stupid, AI wouldn't start building walls until it was 
     * in danger and it would have no chance to finish them before it was too 
     * late */
    case B_CITY:
      /* if (!built_elsewhere(pcity, B_WALL)) was counterproductive -- Syela */
      values[id] = ai_eval_threat_land(pplayer, pcity);
      break;
    case B_COASTAL:
      values[id] = ai_eval_threat_sea(pplayer, pcity);
      break;
    case B_SAM:
      values[id] = ai_eval_threat_air(pplayer, pcity);
      break;
    case B_SDI:
      values[B_SDI] = MAX(ai_eval_threat_nuclear(pplayer, pcity),
			ai_eval_threat_missile(pplayer, pcity));
      break;
  
    /* spaceship production */
    case B_SCOMP:
      if ((game.spacerace)
        && pplayer->spaceship.components < NUM_SS_COMPONENTS) 
        values[id] = -90;
      break;
    case B_SMODULE:
      if ((game.spacerace)
        && pplayer->spaceship.modules < NUM_SS_MODULES) 
        values[id] = -90;
      break;
    case B_SSTRUCTURAL:
      if ((game.spacerace)
        && pplayer->spaceship.structurals < NUM_SS_STRUCTURALS)
        values[id] = -80;
      break;
    default:
      /* ignored: AIRPORT, PALACE, and POLICE and the rest. -- Syela*/
      break;
    } /* end switch */
  } impr_type_iterate_end;

  /* Miscellaneous building values */
  if ( values[B_COLOSSEUM] == 0
    && tech_exists(game.rtech.colosseum_plus)
    && get_invention(pplayer, game.rtech.colosseum_plus) != TECH_KNOWN)
    values[B_COLOSSEUM] = 
      impr_happy_val(pcity, 4, val) - impr_happy_val(pcity, 3, val);
    
  /* Wonders */
  impr_type_iterate(id) {
    if (!is_wonder(id) 
      || !can_build_improvement(pcity, id)
      || wonder_obsolete(id)
      || !is_wonder_useful(id)) 
      continue;

    switch (id) {
    case B_ASMITHS:
      impr_type_iterate(id2) {
        if (city_got_building(pcity, id2)
          && improvement_upkeep(pcity, id2) == 1)
          values[id] += t;
      } impr_type_iterate_end;
      break;
    case B_COLLOSSUS:
      /* probably underestimates the value */
      values[id] = (pcity->size + 1) * t;
      break;
    case B_COPERNICUS:
      values[id] = sci/2;
      break;
    case B_CURE:
      values[id] = impr_happy_val(pcity, 1, val);
      break;
    case B_DARWIN:
      /* this is a one-time boost, not constant */
      values[id] = ((total_bulbs_required(pplayer) * 2 + game.researchcost) * t 
                 - pplayer->research.bulbs_researched * t) / MORT;
      break;
    case B_GREAT:
      /* basically (100 - freecost)% of a free tech per 2 turns */
      values[id] = (total_bulbs_required(pplayer) * (100 - game.freecost) * t
                 * (nplayers - 2)) / (nplayers * 2 * 100);
      break;
    case B_WALL:
      if (!city_got_citywalls(pcity)) {
        /* allowing B_CITY when B_WALL exists, 
         * don't like B_WALL when B_CITY exists. */
        /* was 40, which led to the AI building WALL, not being able to build 
         * CITY, someone learning Metallurgy, and the AI collapsing.  
         * I hate the WALL. -- Syela */
        values[B_WALL] = 1; /* WAG */
      }
      break;
    case B_HANGING:
      /* will add the global effect to this. */
      values[id] = impr_happy_val(pcity, 3, val)
                 - impr_happy_val(pcity, 1, val);
      break;
    case B_HOOVER:
      if (!city_got_building(pcity, B_HYDRO)
        && !city_got_building(pcity, B_NUCLEAR)) {
        values[id] = (city_got_building(pcity, B_POWER) ? 0 :
          (needpower * prod)/4) + pollution_benefit(pplayer, pcity, B_HOOVER);
      }
      break;
    case B_ISAAC:
      values[id] = sci;
      break;
    case B_LEONARDO:
      unit_list_iterate(pcity->units_supported, punit) {
        Unit_Type_id unit_id = can_upgrade_unittype(pplayer, punit->type);
        if (unit_id >= 0) {
          /* this is probably wrong -- Syela */
          j = 8 * unit_upgrade_price(pplayer, punit->type, unit_id);
          values[id] = MAX(values[id], j);
        }
      } unit_list_iterate_end;
      break;
    case B_BACH:
      values[id] = impr_happy_val(pcity, 2, val);
      break;
    case B_RICHARDS:
      /* ignoring pollu, I don't think it matters here -- Syela */
      values[id] = (pcity->size + 1) * SHIELD_WEIGHTING;
      break;
    case B_MICHELANGELO:
      /* Note: Mich not built, so get_cathedral_power() doesn't include 
       * its effect. */
      if (improvement_variant(B_MICHELANGELO) == 0
        && !city_got_building(pcity, B_CATHEDRAL)) {
        /* Assumes Mich will act as the Cath that is not in this city. */
        values[id] = impr_happy_val(pcity, get_cathedral_power(pcity), val);
      }
      else
      if (improvement_variant(B_MICHELANGELO)==1
        && city_got_building(pcity, B_CATHEDRAL)) {
        /* Assumes Mich doubles the power of the Cath that is in this city. */
        values[id] = impr_happy_val(pcity, get_cathedral_power(pcity), val);
      }
      break;
    case B_ORACLE:
      /* The following is probably wrong if B_ORACLE req is
       * not the same as game.rtech.temple_plus (was A_MYSTICISM) --dwp */
      if (!city_got_building(pcity, B_TEMPLE)) {
        if (get_invention(pplayer, game.rtech.temple_plus) != TECH_KNOWN) {
          /* The += has nothing to do with oracle, 
           * just the tech_Want of mysticism! */
          values[id] = impr_happy_val(pcity, 2, val) 
                     - impr_happy_val(pcity, 1, val);
          values[id]+= impr_happy_val(pcity, 2, val)
                     - impr_happy_val(pcity, 1, val);
        }
      } else {
        if (get_invention(pplayer, game.rtech.temple_plus) != TECH_KNOWN) {
          /* The += has nothing to do with oracle, 
           * just the tech_Want of mysticism! */
          values[id] = impr_happy_val(pcity, 4, val) 
                     - impr_happy_val(pcity, 1, val);
          values[id]+= impr_happy_val(pcity, 2, val)
                     - impr_happy_val(pcity, 1, val);
        }
        else
          values[id] = impr_happy_val(pcity, 2, val);
      }
      break;
    case B_PYRAMIDS:
      if (improvement_variant(B_PYRAMIDS)==0
        && !city_got_building(pcity, B_GRANARY)) {
        /* different tech req's */
        values[id] = food * pcity->food_surplus;
      }
      break;
    case B_SETI:
      if (!city_got_building(pcity, B_RESEARCH))
        values[id] = sci/2;
      break;
    case B_SHAKESPEARE:
      values[id] = impr_happy_val(pcity, pcity->size, val);
      break;
    case B_SUNTZU:
      values[id] = bar;
      break;
    case B_WOMENS:
      unit_list_iterate(pcity->units_supported, punit) {
	if (punit->unhappiness != 0) {
	  values[id] += t * 2;
	}
      } unit_list_iterate_end;
      break;
    case B_APOLLO:
      if (game.spacerace) {
        values[id] = values[B_CAPITAL] + 1; /* trump coinage and defenses */
      }
      break;
    case B_UNITED:
      values[id] = values[B_CAPITAL] + 4; /* trump coinage and defenses */
      break;
    case B_LIGHTHOUSE:
      values[id] = values[B_CAPITAL] + 4; /* trump coinage and defenses */
      break;
    case B_MAGELLAN:
      values[id] = values[B_CAPITAL] + 4; /* trump coinage and defenses */
      break;
    default:
	/* also, MAGELLAN is special cased in ai_manage_buildings() */
	/* ignoring MANHATTAN and STATUE */
      break;
    } /* end switch */
  } impr_type_iterate_end;

  /* Final massage */
  impr_type_iterate(id) {
    if (values[id] >= 0) {
      if (!is_wonder(id)) {
        /* trying to buy fewer improvements */
        values[id]-= improvement_upkeep(pcity, id) * t;
        values[id] = (values[id] <= 0 
          ? 0 : (values[id] * SHIELD_WEIGHTING * 100) / MORT);
      } else {
        /* bias against wonders when total production is small */
        values[id] *= (tprod < 50 ? 100/(50-tprod): 100);
      }

      /* handle H_PROD here? -- Syela */
      j = impr_build_shield_cost(id);
      pcity->ai.building_want[id] = values[id] / j;
    } else {
      pcity->ai.building_want[id] = -values[id];
    }

    if (values[id] != 0) 
      CITY_LOG(LOG_DEBUG, pcity, "ai_eval_buildings: wants %s with desire %d.",
               get_improvement_name(id), pcity->ai.building_want[id]);
  } impr_type_iterate_end;

  return;
}

/***************************************************************************
 * Evaluate the need for units (like caravans) that aid wonder construction.
 * If another city is building wonder and needs help but pplayer is not
 * advanced enough to build caravans, the corresponding tech will be 
 * stimulated.
 ***************************************************************************/
static void ai_choose_help_wonder(struct city *pcity,
				  struct ai_choice *choice)
{
  struct player *pplayer = city_owner(pcity);
  /* Continent where the city is --- we won't be aiding any wonder 
   * construction on another continent */
  Continent_id continent = map_get_continent(pcity->x, pcity->y);
  /* Total count of caravans available or already being built 
   * on this continent */
  int caravans = 0;
  /* The type of the caravan */
  Unit_Type_id unit_type;

  if (num_role_units(F_HELP_WONDER) == 0) {
    /* No such units available in the ruleset */
    return;
  }

  /* Count existing caravans */
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_HELP_WONDER)
        && map_get_continent(punit->x, punit->y) == continent)
      caravans++;
  } unit_list_iterate_end;

  /* Count caravans being built */
  city_list_iterate(pplayer->cities, acity) {
    if (acity->is_building_unit
        && unit_type_flag(acity->currently_building, F_HELP_WONDER)
        && (acity->shield_stock
	    >= unit_build_shield_cost(acity->currently_building))
        && map_get_continent(acity->x, acity->y) == continent) {
      caravans++;
    }
  } city_list_iterate_end;

  /* Check all wonders in our cities being built, if one isn't worth a little
   * help */
  city_list_iterate(pplayer->cities, acity) {  
    unit_type = best_role_unit(pcity, F_HELP_WONDER);
    
    if (unit_type == U_LAST) {
      /* We cannot build such units yet
       * but we will consider it to stimulate science */
      unit_type = get_role_unit(F_HELP_WONDER, 0);
    }

    /* If we are building wonder there, the city is on same continent, we
     * aren't in that city (stopping building wonder in order to build caravan
     * to help it makes no sense) and we haven't already got enough caravans
     * to finish the wonder. */
    if (!acity->is_building_unit
        && is_wonder(acity->currently_building)
        && map_get_continent(acity->x, acity->y) == continent
        && acity != pcity
        && (build_points_left(acity)
	    > unit_build_shield_cost(unit_type) * caravans)) {
      
      /* Desire for the wonder we are going to help - as much as we want to
       * build it we want to help building it as well. */
      int want = pcity->ai.building_want[acity->currently_building];

      /* Distance to wonder city was established after ai_manage_buildings()
       * and before this.  If we just started building a wonder during
       * ai_city_choose_build(), the started_building notify comes equipped
       * with an update.  It calls generate_warmap(), but this is a lot less
       * warmap generation than there would be otherwise. -- Syela *
       * Value of 8 is a total guess and could be wrong, but it's still better
       * than 0. -- Syela */
      int dist = pcity->ai.distance_to_wonder_city * 8 / 
        get_unit_type(unit_type)->move_rate;

      want -= dist;
      
      if (can_build_unit_direct(pcity, unit_type)) {
        if (want > choice->want) {
          choice->want = want;
          choice->type = CT_NONMIL;
          ai_choose_role_unit(pplayer, pcity, choice, F_HELP_WONDER, dist / 2);
        }
      } else {
        int tech_req = get_unit_type(unit_type)->tech_requirement;

        /* XXX (FIXME): Had to add the scientist guess here too. -- Syela */
        pplayer->ai.tech_want[tech_req] += want;
      }
    }
  } city_list_iterate_end;
}

/************************************************************************** 
  This function should fill the supplied choice structure.

  If want is 0, this advisor doesn't want anything.
***************************************************************************/
void domestic_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  /* Government of the player */
  struct government *gov = get_gov_pplayer(pplayer);
  /* Unit type with certain role */
  Unit_Type_id unit_type;
  /* Food surplus assuming that workers and elvii are already accounted for
   * and properly balanced. */
  int est_food = pcity->food_surplus
                 + 2 * pcity->specialists[SP_SCIENTIST]
                 + 2 * pcity->specialists[SP_TAXMAN];

  init_choice(choice);

  /* Find out desire for settlers (terrain improvers) */
  unit_type = best_role_unit(pcity, F_SETTLERS);

  if (unit_type != U_LAST
      && est_food > utype_food_cost(get_unit_type(unit_type), gov)) {
    /* settler_want calculated in settlers.c called from ai_manage_cities() */
    int want = pcity->ai.settler_want;

    /* Allowing multiple settlers per city now. I think this is correct.
     * -- Syela */

    if (want > 0) {
      CITY_LOG(LOG_DEBUG, pcity, "desires terrain improvers with passion %d", 
               want);
      choice->want = want;
      choice->type = CT_NONMIL;
      ai_choose_role_unit(pplayer, pcity, choice, F_SETTLERS, want);
    }
    /* Terrain improvers don't use boats (yet) */
  }

  /* Find out desire for city founders */
  /* Basically, copied from above and adjusted. -- jjm */
  unit_type = best_role_unit(pcity, F_CITIES);

  if (unit_type != U_LAST
      && est_food >= utype_food_cost(get_unit_type(unit_type), gov)) {
    /* founder_want calculated in settlers.c, called from ai_manage_cities(). */
    int want = pcity->ai.founder_want;

    if (want > choice->want) {
      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d", want);
      choice->want = want;
      choice->need_boat = pcity->ai.founder_boat;
      choice->type = CT_NONMIL;
      ai_choose_role_unit(pplayer, pcity, choice, F_CITIES, want);
      
    } else if (want < -choice->want) {
      /* We need boats to colonize! */
      CITY_LOG(LOG_DEBUG, pcity, "desires founders with passion %d and asks"
	       " for a boat", want);
      choice->want = 0 - want;
      choice->type = CT_NONMIL;
      choice->choice = unit_type; /* default */
      choice->need_boat = TRUE;
      ai_choose_role_unit(pplayer, pcity, choice, L_FERRYBOAT, -want);
    }
  }

  {
    struct ai_choice cur;

    init_choice(&cur);
    /* Consider building caravan-type units to aid wonder construction */  
    ai_choose_help_wonder(pcity, &cur);
    copy_if_better_choice(&cur, choice);

    init_choice(&cur);
    /* Consider city improvments */
    ai_advisor_choose_building(pcity, &cur);
    copy_if_better_choice(&cur, choice);
  }

  if (choice->want >= 200) {
    /* If we don't do following, we buy caravans in city X when we should be
     * saving money to buy defenses for city Y. -- Syela */
    choice->want = 199;
  }

  return;
}
