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
#include "plrhand.h"
#include "settlers.h" /* amortize */
#include "spacerace.h"
#include "unithand.h"

#include "advmilitary.h"
#include "advspace.h"
#include "aicity.h"
#include "aidata.h"
#include "aitech.h"
#include "aitools.h"
#include "aiunit.h"

#include "aihand.h"

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

/**************************************************************************
 handle spaceship related stuff
**************************************************************************/
static void ai_manage_spaceship(struct player *pplayer)
{
  if (game.spacerace) {
    if (pplayer->spaceship.state == SSHIP_STARTED) {
      ai_spaceship_autoplace(pplayer, &pplayer->spaceship);
      /* if we have built the best possible spaceship  -- AJS 19990610 */
      if ((pplayer->spaceship.structurals == NUM_SS_STRUCTURALS) &&
        (pplayer->spaceship.components == NUM_SS_COMPONENTS) &&
        (pplayer->spaceship.modules == NUM_SS_MODULES))
        handle_spaceship_launch(pplayer);
    }
  }
}

/**************************************************************************
  Refresh all cities of the given player.  This function is only used by 
  AI so we keep it here. 

  Do not send_unit_info because it causes client to blink.
**************************************************************************/
static void ai_player_cities_refresh(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    generic_city_refresh(pcity, TRUE, NULL);
  } city_list_iterate_end;
}

/**************************************************************************
  Set tax/science/luxury rates.

  TODO: Add support for luxuries: select the luxury rate at which all 
  cities are content and the trade output (minus what is consumed by 
  luxuries) is maximal.

  TODO: Audit the use of pplayer->ai.maxbuycost in the code elsewhere,
  then add support for it here.

  TODO: Add support for rapture, needs to be coordinated for entire
  empire.
**************************************************************************/
static void ai_manage_taxes(struct player *pplayer) 
{
  int maxrate = (ai_handicap(pplayer, H_RATES) 
                 ? get_government_max_rate(pplayer->government) : 100);

  /* Otherwise stupid problems arise */
  assert(maxrate >= 50);

  /* Add proper support for luxury here */
  pplayer->economic.luxury = 0;
  /* After this moment don't touch luxury, it's optimal! */

  if (ai_wants_no_science(pplayer)) {
    /* Maximum tax, leftovers into science */
    pplayer->economic.tax = MIN(maxrate, 100 - pplayer->economic.luxury);
    pplayer->economic.science = (100 - pplayer->economic.tax
                                 - pplayer->economic.luxury);
    ai_player_cities_refresh(pplayer);
  } else {
    /* Set tax to the bare minimum which allows positive balance */

    /* First set tax to the minimal available number */
    pplayer->economic.science = MIN(maxrate, 100 - pplayer->economic.luxury);
    pplayer->economic.tax = (100 - pplayer->economic.science
                             - pplayer->economic.luxury);
    ai_player_cities_refresh(pplayer);

    /* Now find the minimum tax with positive balance */
    while(pplayer->economic.tax < maxrate 
          && pplayer->economic.science > 10) {

      if (player_get_expected_income(pplayer) < 0) {
        pplayer->economic.tax += 10;
        pplayer->economic.science -= 10;
        ai_player_cities_refresh(pplayer);
      } else {
        /* Ok, got positive balance */
        if (pplayer->economic.gold < ai_gold_reserve(pplayer)) {
          /* Need to refill coffers, increase tax a bit */
          pplayer->economic.tax += 10;
          pplayer->economic.science -= 10;
          ai_player_cities_refresh(pplayer);
        }
        /* Done! Break the while loop */
        break;
      }

    }

  }

  freelog(LOG_DEBUG, "%s rates: Sci %d Lux%d Tax %d NetIncome %d",
          pplayer->name, pplayer->economic.science,
          pplayer->economic.luxury, pplayer->economic.tax,
          player_get_expected_income(pplayer));
}

/**************************************************************************
  Find best government to aim for.
  We do it by setting our government to all possible values and calculating
  our GDP (total ai_eval_calc_city) under this government.  If the very
  best of the governments is not available to us (it is not yet discovered),
  we record it in the goal.gov structure with the aim of wanting the
  necessary tech more.  The best of the available governments is recorded 
  in goal.revolution.  We record the want of each government, and only
  recalculate this data every ai->govt_reeval_turns turns.
**************************************************************************/
void ai_best_government(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);
  int best_val = 0;
  int bonus = 0; /* in percentage */
  int current_gov = pplayer->government;

  ai->goal.govt.idx = pplayer->government;
  ai->goal.govt.val = 0;
  ai->goal.govt.req = A_UNSET;
  ai->goal.revolution = pplayer->government;

  if (ai_handicap(pplayer, H_AWAY) || !pplayer->is_alive) {
    return;
  }

  if (ai->govt_reeval == 0) {
    government_iterate(gov) {
      int val = 0;
      int dist;

      if (gov->index == game.government_when_anarchy) {
        continue; /* pointless */
      }
      pplayer->government = gov->index;
      /* Ideally we should change tax rates here, but since
       * this is a rather big CPU operation, we'd rather not. */
      check_player_government_rates(pplayer);
      city_list_iterate(pplayer->cities, acity) {
        generic_city_refresh(acity, TRUE, NULL);
        auto_arrange_workers(acity);
      } city_list_iterate_end;
      city_list_iterate(pplayer->cities, pcity) {
        val += ai_eval_calc_city(pcity, ai);
      } city_list_iterate_end;

      /* Bonuses for non-economic abilities. We increase val by
       * a very small amount here to choose govt in cases where
       * we have no cities yet. */
      if (government_has_flag(gov, G_BUILD_VETERAN_DIPLOMAT)) {
        bonus += 3; /* WAG */
      }
      if (government_has_flag(gov, G_REVOLUTION_WHEN_UNHAPPY)) {
        bonus -= 3; /* Not really a problem for us */ /* WAG */
      }
      if (government_has_flag(gov, G_UNBRIBABLE)) {
        bonus += 5; /* WAG */
      }
      if (government_has_flag(gov, G_INSPIRES_PARTISANS)) {
        bonus += 3; /* WAG */
      }
      if (government_has_flag(gov, G_RAPTURE_CITY_GROWTH)) {
        bonus += 5; /* WAG */
        val += 1;
      }
      if (government_has_flag(gov, G_FANATIC_TROOPS)) {
        bonus += 3; /* WAG */
      }
      val += gov->trade_bonus + gov->shield_bonus + gov->food_bonus;

      val += (val * bonus) / 100;

      dist = MAX(1, num_unknown_techs_for_goal(pplayer, gov->required_tech));
      val = amortize(val, dist);
      ai->government_want[gov->index] = val; /* Save want */
    } government_iterate_end;
    /* Now reset our gov to it's real state. */
    pplayer->government = current_gov;
    city_list_iterate(pplayer->cities, acity) {
      generic_city_refresh(acity, FALSE, NULL);
      auto_arrange_workers(acity);
    } city_list_iterate_end;
    ai->govt_reeval = CLIP(5, city_list_size(&pplayer->cities), 20);
  }
  ai->govt_reeval--;

  /* Figure out which government is the best for us this turn. */
  government_iterate(gov) {
    if (ai->government_want[gov->index] > best_val 
        && can_change_to_government(pplayer, gov->index)) {
      best_val = ai->government_want[gov->index];
      ai->goal.revolution = gov->index;
    }
    if (ai->government_want[gov->index] > ai->goal.govt.val) {
      ai->goal.govt.idx = gov->index;
      ai->goal.govt.val = ai->government_want[gov->index];
      ai->goal.govt.req = gov->required_tech;
    }
  } government_iterate_end;
  /* Goodness of the ideal gov is calculated relative to the goodness of the
   * best of the available ones. */
  ai->goal.govt.val -= best_val;
}

/**************************************************************************
  Change the government form, if it can and there is a good reason.
**************************************************************************/
static void ai_manage_government(struct player *pplayer)
{
  struct ai_data *ai = ai_data_get(pplayer);

  if (!pplayer->is_alive || ai_handicap(pplayer, H_AWAY)) {
    return;
  }

  if (ai->goal.revolution != pplayer->government) {
    ai_government_change(pplayer, ai->goal.revolution); /* change */
  }

  /* Crank up tech want */
  if (get_invention(pplayer, ai->goal.govt.req) == TECH_KNOWN) {
    return; /* already got it! */
  }
  pplayer->ai.tech_want[ai->goal.govt.req] += ai->goal.govt.val;
  freelog(LOG_DEBUG, "%s wants %s with want %d", pplayer->name,
          advances[ai->goal.govt.req].name, 
          pplayer->ai.tech_want[ai->goal.govt.req]);
}

/**************************************************************************
  Activities to be done by AI _before_ human turn.  Here we just move the
  units intelligently.
**************************************************************************/
void ai_do_first_activities(struct player *pplayer)
{
  assess_danger_player(pplayer);
  /* TODO: Make assess_danger save information on what is threatening
   * us and make ai_mange_units and Co act upon this information, trying
   * to eliminate the source of danger */

  ai_manage_units(pplayer); 
  /* STOP.  Everything else is at end of turn. */
}

/**************************************************************************
  Activities to be done by AI _after_ human turn.  Here we respond to 
  dangers created by human and AI opposition by ordering defenders in 
  cities and setting taxes accordingly.  We also do other duties.  

  We do _not_ move units here, otherwise humans complain that AI moves 
  twice.
**************************************************************************/
void ai_do_last_activities(struct player *pplayer)
{
  ai_manage_government(pplayer);
  ai_manage_taxes(pplayer); 
  ai_manage_cities(pplayer);
  ai_manage_tech(pplayer); 
  ai_manage_spaceship(pplayer);
}

/**************************************************************************
  ...
**************************************************************************/
bool is_unit_choice_type(enum choice_type type)
{
   return type == CT_NONMIL || type == CT_ATTACKER || type == CT_DEFENDER;
}
