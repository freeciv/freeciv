/*
1.5.1

- altered the automatic build advisor, chooses more intelligent now. 
- fixed a bug in the automatic worker assignment scheme. the
  message cityname is bugged: etc... shouldn't happend again.
- the ai will only build 1 wonder on a continent at a given time.
- caravan control added, when ai is building wonders, "idle" cities  
  will help by building caravans and send them to the aid.

 */




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
#include <player.h>
#include <city.h>
#include <game.h>
#include <unit.h>
#include <unithand.h>
#include <shared.h>
#include <cityhand.h>
#include <packets.h>
#include <map.h>
#include <mapgen.h>
#include <aitools.h>
#include <aihand.h>
#include <aiunit.h>
#include <aicity.h>
#include <aitech.h>
/****************************************************************************
  A man builds a city
  With banks and cathedrals
  A man melts the sand so he can 
  See the world outside
  A man makes a car 
  And builds a road to run them on
  A man dreams of leaving
  but he always stays behind
  And these are the days when our work has come assunder
  And these are the days when we look for something other
  /U2 Lemon.
******************************************************************************/
/*
1 Basic:
- (serv) AI <player> server command toggles AI on off                DONE
- (Unit) settler capable of building city                            DONE
- (City) cities capable of building units/buildings                  DONE
- (Hand) adjustments of tax/luxuries/science                         DONE
- (City) happiness control                                           DONE
- (Hand) change government                                           DONE
- (Tech) tech management                                             DONE
2 Medium:
- better city placement
- ability to explore island 
- Barbarians
- better unit building management
- better improvement management 
- taxcollecters/scientists
- spend spare trade on buying
- upgrade units
- (Unit) wonders/caravans                                            DONE
- defense/city value
- Tax/science/unit producing cities 
3 Advanced:
- ZOC
- continent defense
- sea superiority
- air superiority
- continent attack
4 Superadvanced:
- Transporters (attack on other continents)
- diplomati (ambassader/bytte tech/bytte kort med anden AI)


Step 1 is the basics, can be used for the blank seat people usually have when
they start a game, the AI will atleast do something other than just sit and 
wait, for the kill.
Step 2 will make it do it alot better, and hopefully the barbarians will be in
action.
3 if step 1 and 2 gives decent results, the AI will actually have survived the
building phase and we have to worry about units, step 3 is meant as defense.
So first in step 4 will we introduce attacking AI. (if it ever gets that far)
 */

void ai_before_work(struct player *pplayer);
void ai_manage_taxes(struct player *pplayer); 
void ai_manage_government(struct player *pplayer);
void ai_manage_diplomacy(struct player *pplayer);
void ai_after_work(struct player *pplayer);


/**************************************************************************
 Main AI routine.
**************************************************************************/

void ai_do_activities(struct player *pplayer)
{
  ai_before_work(pplayer); 
  ai_manage_units(pplayer); 
  ai_manage_cities(pplayer); 
  ai_manage_taxes(pplayer); 
  ai_manage_government(pplayer); 
  ai_manage_diplomacy(pplayer);
  ai_manage_tech(pplayer); 
  ai_after_work(pplayer);
}

/**************************************************************************
 update advisors/structures
**************************************************************************/
  
void ai_before_work(struct player *pplayer)
{
  ai_update_player_island_info(pplayer);
}


/**************************************************************************
 Trade tech and stuff, this one will probably be blank for a long time.
**************************************************************************/

void ai_manage_diplomacy(struct player *pplayer)
{

}

/**************************************************************************
 well am not sure what will happend here yet, maybe some more analysis
**************************************************************************/

void ai_after_work(struct player *pplayer)
{

}


/**************************************************************************
...
**************************************************************************/

int ai_calc_city_buy(struct city *pcity)
{
  int val;
  if (pcity->is_building_unit) {   /* add wartime stuff here */
    return ((pcity->size*10)/(2*city_get_defenders(pcity)+1))
;
  } else {                         /* crude function, add some value stuff */
    if (pcity->currently_building==B_CAPITAL)
      return 0;
    val = ((pcity->size*20)/(city_get_buildings(pcity)+1));
    val = val * (30 - pcity->shield_prod); /* yes it'll become negative if > 30 */
    return val;
  }
}

/**************************************************************************
.. Spend money
**************************************************************************/
void ai_spend_gold(struct player *pplayer, int gold)
{
  struct city *pc2=NULL;
  int maxwant, curwant;
  maxwant = 0;
  city_list_iterate(pplayer->cities, pcity) 
    if ((curwant = ai_calc_city_buy(pcity)) > maxwant) {
      maxwant = curwant;
      pc2 = pcity;
    }
  city_list_iterate_end;

  if (!pc2) return;
  if (pc2->is_building_unit) {
    if (city_buy_cost(pc2) > gold)
      return;
    pplayer->economic.gold -= city_buy_cost(pc2);
    pc2->shield_stock = unit_value(pc2->currently_building);
  } else { 
    if (city_buy_cost(pc2) < gold) {
      pplayer->economic.gold -= city_buy_cost(pc2);
      pc2->shield_stock = improvement_value(pc2->currently_building);
      return;
    }
    /* we don't have to end the build, we just pool in some $ */
    if (is_wonder(pc2->currently_building)) 
      pc2->shield_stock +=(gold/4);
    else
      pc2->shield_stock +=(gold/2);
    pplayer->economic.gold -= gold;
  }
}
 


/**************************************************************************
.. Set tax/science/luxury rates. Tax Rates > 40 indicates a crisis.
**************************************************************************/

void ai_manage_taxes(struct player *pplayer) 
{
  int gnow=pplayer->economic.gold;
  int gthen=pplayer->ai.prev_gold;
  if (pplayer->government == G_REPUBLIC || pplayer->government == G_DEMOCRACY)
    pplayer->economic.luxury = 20;
  else
    pplayer->economic.luxury = 0;
  
  /* do president sale here */

  if (pplayer->research.researching==A_NONE) {
    pplayer->economic.tax+=pplayer->economic.science;
    pplayer->economic.science=0;
    if (gnow > ai_gold_reserve(pplayer))
      ai_spend_gold(pplayer, gnow - ai_gold_reserve(pplayer));
    return;
  } else if (gnow>gthen && gnow>1.5*ai_gold_reserve(pplayer)) { 
    if (pplayer->economic.tax>20) 
      pplayer->economic.tax-=10;
    
  }
  if (gnow<gthen || gnow < ai_gold_reserve(pplayer)) {
    pplayer->economic.tax+=10;
  }
  if (pplayer->economic.tax > 80) 
    pplayer->economic.tax = 80;
  pplayer->economic.science = 100 - (pplayer->economic.tax +
									     pplayer->economic.luxury);
  if (gnow > ai_gold_reserve(pplayer))
    ai_spend_gold(pplayer, gnow - ai_gold_reserve(pplayer));
}

/* --------------------------GOVERNMENT--------------------------------- */

/**************************************************************************
 change the government form, if it can and there is a good reason
**************************************************************************/

void ai_manage_government(struct player *pplayer)
{
  int government = get_race(pplayer)->goals.government;
  if (pplayer->government == government)
    return;
  if (can_change_to_government(pplayer, government)) {
    ai_government_change(pplayer, government);
    return;
  }
  switch (government) {
  case G_COMMUNISM:
    if (can_change_to_government(pplayer, G_MONARCHY)) 
      ai_government_change(pplayer, G_MONARCHY);
    break;
  case G_DEMOCRACY:
    if (can_change_to_government(pplayer, G_REPUBLIC)) 
      ai_government_change(pplayer, G_REPUBLIC);
    break;
  }
}














