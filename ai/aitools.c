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

#include <stdio.h>
#include <stdlib.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "unit.h"

#include "citytools.h"
#include "cityturn.h"
#include "maphand.h"
#include "plrhand.h"
#include "unittools.h"

#include "aicity.h"
#include "aiunit.h"

#include "aitools.h"

/* dist_nearest_enemy_* are no longer ever used.  This is
   dist_nearest_enemy_city, respaced so I can read it and therefore
   debug it into something useful. -- Syela
*/

/**************************************************************************
This looks for the nearest city:
If (x,y) is the land, it looks for cities only on the same continent
unless (everywhere != 0)
If (enemy != 0) it looks only for enemy cities
If (pplayer != NULL) it looks for cities known to pplayer
**************************************************************************/

struct city *dist_nearest_city(struct player *pplayer, int x, int y,
                               int everywhere, int enemy)
{ 
  struct player *pplay;
  struct city *pc=NULL;
  int i;
  int dist = MAX(map.xsize / 2, map.ysize);
  int con = map_get_continent(x, y);

  for(i = 0; i < game.nplayers; i++) {
    pplay = &game.players[i];
    if(enemy && pplay == pplayer) continue;

    city_list_iterate(pplay->cities, pcity)
      if (real_map_distance(x, y, pcity->x, pcity->y) < dist &&
         (everywhere || !con || con == map_get_continent(pcity->x, pcity->y)) &&
         (!pplayer || map_get_known(pcity->x, pcity->y, pplayer))) {
        dist = real_map_distance(x, y, pcity->x, pcity->y);
        pc = pcity;
      }
    city_list_iterate_end;
  }
  return(pc);
}

/**************************************************************************
.. change government,pretty fast....
**************************************************************************/
void ai_government_change(struct player *pplayer, int gov)
{
  struct packet_player_request preq;
  if (gov == pplayer->government)
    return;
  preq.government=gov;
  pplayer->revolution=0;
  pplayer->government=game.government_when_anarchy;
  handle_player_government(pplayer, &preq);
  pplayer->revolution = -1; /* yes, I really mean this. -- Syela */
}

/* --------------------------------TAX---------------------------------- */


/**************************************************************************
... Credits the AI wants to have in reserves.
**************************************************************************/
int ai_gold_reserve(struct player *pplayer)
{
  int i = total_player_citizens(pplayer)*2;
  return MAX(pplayer->ai.maxbuycost, i);
/* I still don't trust this function -- Syela */
}

/* --------------------------------------------------------------------- */

void adjust_choice(int value, struct ai_choice *choice)
{
  choice->want = (choice->want *value)/100;
}

/**************************************************************************
...
**************************************************************************/

void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best)
{
  if (cur->want > best->want) {
    best->choice =cur->choice;
    best->want = cur->want;
    best->type = cur->type;
  }
}

void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice)
{ /* I prefer the ai_choice as a return value; gcc prefers it as an arg -- Syela */
  Impr_Type_id i;
  Impr_Type_id id = B_LAST;
  int danger = 0, downtown = 0, cities = 0;
  int want=0;
  struct player *plr;
        
  plr = &game.players[pcity->owner];
     
  /* too bad plr->score isn't kept up to date. */
  city_list_iterate(plr->cities, acity)
    danger += acity->ai.danger;
    downtown += acity->ai.downtown;
    cities++;
  city_list_iterate_end;
 
  for(i=0; i<game.num_impr_types; i++) {
    if (!is_wonder(i) ||
       (!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
       pcity->shield_stock >= improvement_value(i) / 2) ||
       (!is_building_other_wonder(pcity) &&
        !pcity->ai.grave_danger && /* otherwise caravans will be killed! */
        pcity->ai.downtown * cities >= downtown &&
        pcity->ai.danger * cities <= danger)) { /* too many restrictions? */
/* trying to keep wonders in safe places with easy caravan access -- Syela */
      if(pcity->ai.building_want[i]>want) {
/* we have to do the can_build check to avoid Built Granary.  Now Building Granary. */
        if (can_build_improvement(pcity, i)) {
          want=pcity->ai.building_want[i];
          id=i;
        } else {
	  freelog(LOG_DEBUG, "%s can't build %s", pcity->name,
		  get_improvement_name(i));
	}
      } /* id is the building we like the best */
    }
  }

  if (want) {
    freelog(LOG_DEBUG, "AI_Chosen: %s with desire = %d for %s",
	    get_improvement_name(id), want, pcity->name);
  } else {
    freelog(LOG_DEBUG, "AI_Chosen: None for %s", pcity->name);
  }
  choice->want = want;
  choice->choice = id;
  choice->type = CT_BUILDING;
}


/**********************************************************************
The following evaluates the unhappiness caused by military units
in the field (or aggressive) at a city when at Republic or Democracy

Now generalised somewhat for government rulesets, though I'm not sure
whether it is fully general for all possible parameters/combinations.
 --dwp
**********************************************************************/
int ai_assess_military_unhappiness(struct city *pcity,
				   struct government *g)
{
  int free_happy, have_police, variant;
  int unhap = 0;

  /* bail out now if happy_cost is 0 */
  if (g->unit_happy_cost_factor == 0) {
    return 0;
  }
  
  free_happy  = citygov_free_happy(pcity, g);
  have_police = city_got_effect(pcity, B_POLICE);
  variant = improvement_variant(B_WOMENS);

  if (variant==0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }
  
  unit_list_iterate(pcity->units_supported, punit) {
    int happy_cost = utype_happy_cost(get_unit_type(punit->type), g);

    if (happy_cost<=0)
      continue;

    /* See discussion/rules in cityturn.c:city_support() --dwp */
    if (!unit_being_aggressive(punit)) {
      if (is_field_unit(punit)) {
	happy_cost = 1;
      } else {
	happy_cost = 0;
      }
    }
    if (happy_cost<=0)
      continue;

    if (variant==1 && have_police) {
      happy_cost--;
    }
    adjust_city_free_cost(&free_happy, &happy_cost);
    
    if (happy_cost>0)
      unhap += happy_cost;
  }
  unit_list_iterate_end;
 
  if (unhap < 0) unhap = 0;
  return unhap;
}

#ifndef NEW_GOV_EVAL		/* needs updating before enabled --dwp */

/**********************************************************************
  Evaluate a government form, still pretty sketchy.
**********************************************************************/

/* 
 * This evaluation should be more dynamic (based on players current
 * needs, like expansion, at war, etc, etc).
 * -SKi
 */

/*
 * 0 is my first attempt at government evaluation
 * 1 is new evaluation based on patch from rizos
 * -SKi
 */
#if 1

#define DEBUG_AEG_PRODUCTION (1)

int ai_evaluate_government (struct player *pplayer, struct government *g)
{
  int current_gov      = pplayer->government;
  int shield_surplus   = 0;
  int food_surplus     = 0;
  int trade_prod       = 0;
  int shield_need      = 0;
  int food_need        = 0;
  int gov_overthrown   = 0;
  int score;

  pplayer->government = g->index;

  city_list_iterate(pplayer->cities, pcity) {
    struct city tmp_city = *pcity; 
    int x, y;

    city_refresh (&tmp_city);
     
    /* the 4 lines that follow are copied from ai_manage_city -
       we don't need the sell_obsolete_buildings */
    city_check_workers(&tmp_city, 0);
    auto_arrange_workers(&tmp_city);
    if (ai_fix_unhappy (&tmp_city))
       ai_scientists_taxmen (&tmp_city);

    trade_prod     += tmp_city.trade_prod;
    if (tmp_city.shield_prod > 0)
      shield_surplus += tmp_city.shield_surplus;
    else
      shield_need    += tmp_city.shield_surplus;
    if (tmp_city.food_surplus > 0)
      food_surplus   += tmp_city.food_surplus;
    else
      food_need      += tmp_city.food_surplus;

    if (city_unhappy (&tmp_city)) {
      /* the following is essential to prevent falling into anarchy */
      if (tmp_city.anarchy > 0
	  && government_has_flag(g, G_REVOLUTION_WHEN_UNHAPPY)) 
        gov_overthrown = 1;
    }

    /* the following is essential because the above may change the ptile->worked */
    city_map_iterate(x, y) {
       struct tile *ptile = map_get_tile(pcity->x+x-2, pcity->y+y-2);
       if (ptile->worked == &tmp_city)
           ptile->worked = NULL;
       set_worker_city(pcity, x, y, get_worker_city(pcity, x, y));
    }
  } city_list_iterate_end;

  pplayer->government = current_gov;

  score =
    3 * trade_prod
  + 2 * shield_surplus + 2 * food_surplus
  - 4 * shield_need - 4 * food_need
  ;
  score = gov_overthrown ? 0 : MAX(score, 0);

  if (DEBUG_AEG_PRODUCTION)
    freelog (LOG_DEBUG, "a_e_g (%12s) = score=%3d; trade=%3d; shield=%3d/%3d; food=%3d/%3d;", g->name, score, trade_prod, shield_surplus, shield_need, food_surplus, food_need);
  return score;
}

#else

#define DEBUG_AEG_EMPIRE	(0)
#define DEBUG_AEG_CORRUPTION	(0)
#define DEBUG_AEG_EXTRA_FOOD	(0)
#define DEBUG_AEG_EXTRA_SHIELD	(0)
#define DEBUG_AEG_EXTRA_HAPPY	(0)
#define DEBUG_AEG_BONUS_TRADE	(0)
#define DEBUG_AEG_PENALTY	(0)
#define DEBUG_AEG_FREE		(0)

int ai_evaluate_government (struct player *pplayer, struct government *g)
{
  static struct player *p_last_player;
  static int    last_year;

  struct city *capital = find_palace(pplayer);
  int rating = 25000, rating_before;
  int avg_city_size = 0;
  int empire_size = 0;
  int aggressive_units = 0;
  int free_aggressive_units = 0;
  int upkept_units = 0;
  int free_upkept_units = 0;
  int eating_units = 0;
  int free_eating_units = 0;
  int trade_fields = 0;
  int total_distance = 0;

  city_list_iterate (pplayer->cities, this_city) {
    int free_shield = citygov_free_shield(this_city, g);
    int free_happy  = citygov_free_happy(this_city, g);
    int free_food   = citygov_free_food(this_city, g);

    ++empire_size;
    avg_city_size += this_city->size;
    trade_fields += this_city->tile_trade;

    total_distance += g->extra_corruption_distance;
    if (g->fixed_corruption_distance) {
      total_distance += g->fixed_corruption_distance;
    } else if (capital) {
      total_distance += min(36, map_distance(capital->x, capital->y, this_city->x, this_city->y)); 
    } else {
      total_distance += 36;
    }

    unit_list_iterate (this_city->units_supported, this_unit) {
      struct unit_type *ut = &unit_types[ this_unit->type ];
      if (unit_being_aggressive (this_unit)) {
        if (free_happy) {
	  --free_happy;
	  ++free_aggressive_units;
	}
        ++aggressive_units;
      }
      if (ut->shield_cost) {
        if (free_shield) {
	  --free_shield;
	  ++free_upkept_units;
	}
        ++upkept_units;
      }
      if (ut->food_cost) {
        if (free_food) {
	  --free_food;
	  ++free_eating_units;
	}
        ++eating_units;
      }
    } unit_list_iterate_end;
  } city_list_iterate_end;
  if (empire_size)
    avg_city_size /= empire_size;
  else
    avg_city_size = 0;
  
  if (DEBUG_AEG_EMPIRE && (p_last_player != pplayer || last_year != game.year)) {
    freelog (LOG_DEBUG, "%s:\tEmpire of %s (%d): s=%d,c=%d,a=%d,u=%d,e=%d,t=%d",
      g->name, pplayer->name, game.year,
      empire_size, avg_city_size, aggressive_units, upkept_units, eating_units,
      trade_fields
    );
  }

  rating_before = rating;
  if (government_has_flag(g, G_BUILD_VETERAN_DIPLOMAT)) {
    rating += 100; /* ignore this advantage for now -- SKi */
  }
  if (government_has_flag(g, G_REVOLUTION_WHEN_UNHAPPY)) {
    rating -= 50000; /* very bad! */
  }
  if (government_has_flag(g, G_HAS_SENATE)) {
    rating -= 300; /* undesirable */
  }
  if (government_has_flag(g, G_UNBRIBABLE)) {
    rating += 100; /* nice */
  }
  if (government_has_flag(g, G_INSPIRES_PARTISANS)) {
    rating += 100; /* nice */
  }

  /* punish governments for their extra upkeep costs */
  rating_before = rating;
#if 0 /* FIXME --dwp */
  if (g->extra_happy_cost != G_NO_UPKEEP)
    rating -= g->extra_happy_cost * 150 * aggressive_units;
  if (DEBUG_AEG_EXTRA_HAPPY && rating != rating_before) freelog (LOG_DEBUG, "extra_happy_cost changed rating for %s by %d", g->name, rating - rating_before);
  rating_before = rating;
  if (g->extra_shield_cost != G_NO_UPKEEP)
    rating -= g->extra_shield_cost * 150 * upkept_units;
  if (DEBUG_AEG_EXTRA_SHIELD && rating != rating_before) freelog (LOG_DEBUG, "extra_shield_cost changed rating for %s by %d", g->name, rating - rating_before);
  rating_before = rating;
  if (g->extra_food_cost != G_NO_UPKEEP)
    rating -= g->extra_food_cost * 150 * eating_units;
  if (DEBUG_AEG_EXTRA_FOOD && rating != rating_before) freelog (LOG_DEBUG, "extra_food_cost changed rating for %s by %d", g->name, rating - rating_before);
#endif

  /* reward governments that give us some upkeep for free */
  rating_before= rating;
  rating += free_upkept_units * 250;
  if (DEBUG_AEG_FREE && rating != rating_before) freelog (LOG_DEBUG, "free upkeep changed rating for %s by %d", g->name, rating - rating_before);

  /* Punish governments for their production penalties. */
  rating_before = rating;
  if (g->trade_before_penalty)
    rating -= max(g->trade_before_penalty - 2, 2) * 400;
  /* trade bonuses */
  rating_before = rating;
  if (g->trade_bonus)
    rating += g->trade_bonus * 350 * trade_fields;
  if (DEBUG_AEG_BONUS_TRADE && rating != rating_before) freelog (LOG_DEBUG, "trade bonus changed rating for %s by %d", g->name, rating - rating_before);
  /* corruption level */
  rating_before = rating;
  rating += (100 - g->corruption_level) * 50;
  rating -= g->corruption_level * max(0, 100 - g->corruption_modifier) * trade_fields / 45;
  rating -= total_distance * g->corruption_distance_factor * trade_fields / 3;
  if (DEBUG_AEG_CORRUPTION) freelog(LOG_DEBUG, "corruption changed rating for %s by %d", g->name, rating - rating_before);
  /* empire_size_factor */
  rating_before = rating;
  if (g->empire_size_factor > empire_size)
    rating += 500;

  p_last_player = pplayer;
  last_year = game.year;

  return rating;
}
#endif

#endif /* NEW_GOV_EVAL */
