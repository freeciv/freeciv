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
                               bool everywhere, bool enemy)
{ 
  struct city *pc=NULL;
  int dist = MAX(map.xsize / 2, map.ysize);
  int con = map_get_continent(x, y);

  players_iterate(pplay) {
    if ((enemy) && (pplayer) && (!pplayers_at_war(pplayer,pplay))) continue;

    city_list_iterate(pplay->cities, pcity)
      if (real_map_distance(x, y, pcity->x, pcity->y) < dist &&
         (everywhere || con == 0 || con == map_get_continent(pcity->x, pcity->y)) &&
         (!pplayer || map_get_known(pcity->x, pcity->y, pplayer))) {
        dist = real_map_distance(x, y, pcity->x, pcity->y);
        pc = pcity;
      }
    city_list_iterate_end;
  } players_iterate_end;

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

/**************************************************************************
... Credits the AI wants to have in reserves.
**************************************************************************/
int ai_gold_reserve(struct player *pplayer)
{
  int i = total_player_citizens(pplayer)*2;
  return MAX(pplayer->ai.maxbuycost, i);
/* I still don't trust this function -- Syela */
}

/**************************************************************************
...
**************************************************************************/
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

/**************************************************************************
...
**************************************************************************/
void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice)
{ /* I prefer the ai_choice as a return value; gcc prefers it as an arg -- Syela */
  Impr_Type_id id = B_LAST;
  int danger = 0, downtown = 0, cities = 0;
  int want=0;
  struct player *plr;
        
  plr = city_owner(pcity);
     
  /* too bad plr->score isn't kept up to date. */
  city_list_iterate(plr->cities, acity)
    danger += acity->ai.danger;
    downtown += acity->ai.downtown;
    cities++;
  city_list_iterate_end;

  impr_type_iterate(i) {
    if (!is_wonder(i) ||
       (!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
       pcity->shield_stock >= improvement_value(i) / 2) ||
       (!is_building_other_wonder(pcity) &&
        pcity->ai.grave_danger == 0 && /* otherwise caravans will be killed! */
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
  } impr_type_iterate_end;

  if (want != 0) {
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
bool ai_assess_military_unhappiness(struct city *pcity,
				   struct government *g)
{
  int free_happy;
  bool have_police;
  int variant;
  int unhap = 0;

  /* bail out now if happy_cost is 0 */
  if (g->unit_happy_cost_factor == 0) {
    return FALSE;
  }
  
  free_happy  = citygov_free_happy(pcity, g);
  have_police = city_got_effect(pcity, B_POLICE);
  variant = improvement_variant(B_WOMENS);

  if (variant==0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }
  
  unit_list_iterate(pcity->units_supported, punit) {
    int happy_cost = utype_happy_cost(unit_type(punit), g);

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
  return unhap > 0;
}

/**********************************************************************
  Evaluate a government form, still pretty sketchy.

  This evaluation should be more dynamic (based on players current
  needs, like expansion, at war, etc, etc). -SKi

  0 is my first attempt at government evaluation
  1 is new evaluation based on patch from rizos -SKi
**********************************************************************/
int ai_evaluate_government (struct player *pplayer, struct government *g)
{
  int current_gov      = pplayer->government;
  int shield_surplus   = 0;
  int food_surplus     = 0;
  int trade_prod       = 0;
  int shield_need      = 0;
  int food_need        = 0;
  bool gov_overthrown   = FALSE;
  int score;

  pplayer->government = g->index;

  city_list_iterate(pplayer->cities, pcity) {
    city_refresh(pcity);

    /* the lines that follow are copied from ai_manage_city -
       we don't need the sell_obsolete_buildings */
    auto_arrange_workers(pcity);
    if (ai_fix_unhappy (pcity))
      ai_scientists_taxmen(pcity);

    trade_prod     += pcity->trade_prod;
    if (pcity->shield_prod > 0)
      shield_surplus += pcity->shield_surplus;
    else
      shield_need    += pcity->shield_surplus;
    if (pcity->food_surplus > 0)
      food_surplus   += pcity->food_surplus;
    else
      food_need      += pcity->food_surplus;

    if (city_unhappy(pcity)) {
      /* the following is essential to prevent falling into anarchy */
      if (pcity->anarchy > 0
	  && government_has_flag(g, G_REVOLUTION_WHEN_UNHAPPY)) 
        gov_overthrown = TRUE;
    }
  } city_list_iterate_end;

  pplayer->government = current_gov;

  /* Restore all cities. */
  city_list_iterate(pplayer->cities, pcity) {
    city_refresh(pcity);

    /* the lines that follow are copied from ai_manage_city -
       we don't need the sell_obsolete_buildings */
    auto_arrange_workers(pcity);
    if (ai_fix_unhappy (pcity))
      ai_scientists_taxmen(pcity);
  } city_list_iterate_end;
  sync_cities();

  score =
    3 * trade_prod
  + 2 * shield_surplus + 2 * food_surplus
  - 4 * shield_need - 4 * food_need;

  score = gov_overthrown ? 0 : MAX(score, 0);

  freelog(LOG_DEBUG, "a_e_g (%12s) = score=%3d; trade=%3d; shield=%3d/%3d; "
                     "food=%3d/%3d;", g->name, score, trade_prod, 
                     shield_surplus, shield_need, food_surplus, food_need);
  return score;
}
