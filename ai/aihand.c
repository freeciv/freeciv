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
#include <string.h>

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
#include "spacerace.h"
#include "unithand.h"

#include "aicity.h"
#include "aitech.h"
#include "aitools.h"
#include "aiunit.h"
#include "advspace.h"

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
- better city placement                                              TODO
- ability to explore island                                          DONE
- Barbarians                                                         ????
- better unit building management                                    DONE
- better improvement management                                      DONE
- taxcollecters/scientists                                           DONE
- spend spare trade on buying                                        DONE
- upgrade units                                                      DONE
- (Unit) wonders/caravans                                            DONE
- defense/city value                                                 ????
- Tax/science/unit producing cities                                  ????
3 Advanced:
- ZOC
- continent defense
- sea superiority
- air superiority
- continent attack
4 Superadvanced:
- Transporters (attack on other continents)
- diplomacy (embassies / tech trading / map trading with other AIs)


Step 1 is the basics, can be used for the blank seat people usually have when
they start a game, the AI will atleast do something other than just sit and 
wait, for the kill.
Step 2 will make it do it alot better, and hopefully the barbarians will be in
action.
3 if step 1 and 2 gives decent results, the AI will actually have survived the
building phase and we have to worry about units, step 3 is meant as defense.
So first in step 4 will we introduce attacking AI. (if it ever gets that far)

-- I don't know who wrote this, but I've gone a different direction -- Syela
 */

#ifdef UNUSED
/**************************************************************************
 update advisors/structures
**************************************************************************/
static void ai_before_work(struct player *pplayer)
{

}

/**************************************************************************
 well am not sure what will happend here yet, maybe some more analysis
**************************************************************************/
static void ai_after_work(struct player *pplayer)
{

}

/**************************************************************************
 Trade tech and stuff, this one will probably be blank for a long time.
**************************************************************************/
static void ai_manage_diplomacy(struct player *pplayer)
{

}
#endif /* UNUSED */

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
.. Set tax/science/luxury rates. Tax Rates > 40 indicates a crisis.
 total rewrite by Syela 
**************************************************************************/
static void ai_manage_taxes(struct player *pplayer) 
{
  struct government *g = get_gov_pplayer(pplayer);
  int gnow = pplayer->economic.gold;
  Impr_Type_id id;
  int trade = 0, m, n, i, expense = 0, tot;
  int waste[40]; /* waste with N elvises */
  int elvises[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int hhjj[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int cities = 0;
  struct packet_unit_request pack;
  struct city *incity; /* stay a while, until the night is over */
  struct unit *defender;
  int maxrate = 10;

  if (ai_handicap(pplayer, H_RATES))
    maxrate = get_government_max_rate(pplayer->government) / 10;

  pplayer->economic.science += pplayer->economic.luxury;
/* without the above line, auto_arrange does strange things we must avoid -- Syela */
  pplayer->economic.luxury = 0;
  city_list_iterate(pplayer->cities, pcity) 
    cities++;
    pcity->ppl_elvis = 0; pcity->ppl_taxman = 0; pcity->ppl_scientist = 0;
    add_adjust_workers(pcity); /* less wasteful than auto_arrange, required */
    city_refresh(pcity);
    trade += pcity->trade_prod * city_tax_bonus(pcity) / 100;
    freelog(LOG_DEBUG, "%s has %d trade.", pcity->name, pcity->trade_prod);
    for (id = 0; id < game.num_impr_types; id++)
      if (city_got_building(pcity, id)) expense += improvement_upkeep(pcity,id);
  city_list_iterate_end;

  pplayer->ai.est_upkeep = expense;

  if (!trade) { /* can't return right away - thanks for the evidence, Muzz */
    city_list_iterate(pplayer->cities, pcity) 
      if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer,1))
        ai_scientists_taxmen(pcity);
    city_list_iterate_end;
    return; /* damn division by zero! */
  }

  pplayer->economic.luxury = 0;

  city_list_iterate(pplayer->cities, pcity) {

    /* this code must be ABOVE the elvises[] if SIMPLISTIC is off */
    freelog(LOG_DEBUG, "Does %s want to be bigger? %d",
		  pcity->name, wants_to_be_bigger(pcity));
    if (government_has_flag(g, G_RAPTURE_CITY_GROWTH)
	&& pcity->size >= g->rapture_size && pcity->food_surplus > 0
	&& pcity->ppl_unhappy[4] == 0 && pcity->ppl_angry[4] == 0
	&& wants_to_be_bigger(pcity) && ai_fuzzy(pplayer, 1)) {
      freelog(LOG_DEBUG, "%d happy people in %s",
		    pcity->ppl_happy[4], pcity->name);
      n = ((pcity->size/2) - pcity->ppl_happy[4]) * 20;
      if (n > pcity->ppl_content[1] * 20) n += (n - pcity->ppl_content[1] * 20);
      m = ((((city_got_effect(pcity, B_GRANARY) ? 3 : 2) *
	     city_granary_size(pcity->size))/2) -
           pcity->food_stock) * food_weighting(pcity->size);
      freelog(LOG_DEBUG, "Checking HHJJ for %s, m = %d", pcity->name, m);
      tot = 0;
      for (i = 0; i <= 10; i++) {
        if (pcity->trade_prod * i * city_tax_bonus(pcity) >= n * 100) {
	  if (!tot) freelog(LOG_DEBUG, "%s celebrates at %d.",
			    pcity->name, i * 10);
          hhjj[i] += (pcity->was_happy ? m : m/2);
          tot++;
        }
      }
    } /* hhjj[i] is (we think) the desirability of partying with lux = 10 * i */
/* end elevated code block */

    /* need this much lux */
    n = (2 * pcity->ppl_angry[4] + pcity->ppl_unhappy[4] -
	 pcity->ppl_happy[4]) * 20;

/* this could be an unholy CPU glutton; it's only really useful when we need
   lots of luxury, like with pcity->size = 12 and only a temple */

    memset(waste, 0, sizeof(waste));
    tot = pcity->food_prod * food_weighting(pcity->size) +
            pcity->trade_prod * pcity->ai.trade_want +
            pcity->shield_prod * SHIELD_WEIGHTING;

    for (i = 1; i <= pcity->size; i++) {
      m = ai_make_elvis(pcity);
      if (m) {
        waste[i] = waste[i-1] + m;
      } else {
        while (i <= pcity->size) {
          waste[i++] = tot;
        }
        break;
      }
    }
    for (i = 0; i <= 10; i++) {
      m = ((n * 100 - pcity->trade_prod * city_tax_bonus(pcity) * i + 1999) / 2000);
      if (m >= 0) elvises[i] += waste[m];
    }
  }
  city_list_iterate_end;
    
  for (i = 0; i <= 10; i++) elvises[i] += (trade * i) / 10 * TRADE_WEIGHTING;
  /* elvises[i] is the production + income lost to elvises with lux = i * 10 */
  n = 0; /* default to 0 lux */
  for (i = 1; i <= maxrate; i++) if (elvises[i] < elvises[n]) n = i;
  /* two thousand zero zero party over it's out of time */
  for (i = 0; i <= 10; i++) {
    hhjj[i] -= (trade * i) / 10 * TRADE_WEIGHTING; /* hhjj is now our bonus */
    freelog(LOG_DEBUG, "hhjj[%d] = %d for %s.", i, hhjj[i], pplayer->name);
  }

  m = n; /* storing the lux we really need */
  pplayer->economic.luxury = n * 10; /* temporary */

/* Less-intelligent previous versions of the follow equation purged. -- Syela */
  n = ((expense - gnow + cities + pplayer->ai.maxbuycost) * 10 + trade - 1) / trade;
  if (n < 0) n = 0;
  while (n > maxrate) n--; /* Better to cheat on lux than on tax -- Syela */
  if (m > 10 - n) m = 10 - n;

  freelog(LOG_DEBUG, "%s has %d trade and %d expense."
	  "  Min lux = %d, tax = %d", pplayer->name, trade, expense, m, n);

/* Peter Schaefer points out (among other things) that in pathological cases
(like expense == 468) the AI will try to celebrate with m = 10 and then abort */

  /* want to max the hhjj */
  for (i = m; i <= maxrate && i <= 10 - n; i++)
    if (hhjj[i] > hhjj[m] && (trade * (10 - i) >= expense * 10)) m = i;
/* if the lux rate necessary to celebrate cannot be maintained, don't bother */
  pplayer->economic.luxury = 10 * m;

  if (pplayer->research.researching==A_NONE) {
    pplayer->economic.tax = 100 - pplayer->economic.luxury;
    while (pplayer->economic.tax > maxrate * 10) {
      pplayer->economic.tax -= 10;
      pplayer->economic.luxury += 10;
    }
  } else { /* have to balance things logically */
/* if we need 50 gold and we have trade = 100, need 50 % tax (n = 5) */
/*  n = ((ai_gold_reserve(pplayer) - gnow - expense) ... I hate typos. -- Syela */
    n = ((ai_gold_reserve(pplayer) - gnow + expense + cities) * 20 + (trade*2) - 1) / (trade*2);
/* same bug here as above, caused us not to afford city walls we needed. -- Syela */
    if (n < 0) n = 0; /* shouldn't allow 0 tax? */
    while (n > 10 - (pplayer->economic.luxury / 10) || n > maxrate) n--;
    pplayer->economic.tax = 10 * n;
  }

/* once we have tech_want established, can compare it to cash want here -- Syela */
  pplayer->economic.science = 100 - pplayer->economic.tax - pplayer->economic.luxury;
  while (pplayer->economic.science > maxrate * 10) {
    pplayer->economic.tax += 10;
    pplayer->economic.science -= 10;
  }

  city_list_iterate(pplayer->cities, pcity) 
    pcity->ppl_elvis = 0;
    city_refresh(pcity);
    add_adjust_workers(pcity);
    city_refresh(pcity);
    if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer,1))
      ai_scientists_taxmen(pcity);
    if (pcity->shield_surplus < 0 || city_unhappy(pcity) ||
        pcity->food_stock + pcity->food_surplus < 0) 
       emergency_reallocate_workers(pplayer, pcity);
    if (pcity->shield_surplus < 0) {
      defender = NULL;
      unit_list_iterate(pcity->units_supported, punit)
        incity = map_get_city(punit->x, punit->y);
        if (incity != NULL && pcity->shield_surplus < 0) {
	  /* Note that disbanding here is automatically safe (we don't
	   * need to use handle_unit_disband_safe()), because the unit is
	   * in a city, so there are no passengers to get disbanded. --dwp
	   */
          if (incity == pcity) {
            if (defender != NULL) {
              if (unit_vulnerability_virtual(punit) <
                  unit_vulnerability_virtual(defender)) {
		freelog(LOG_VERBOSE, "Disbanding %s in %s",
			unit_type(punit)->name, pcity->name);
                pack.unit_id = punit->id;
                handle_unit_disband(pplayer, &pack);
                city_refresh(pcity);
              } else {
		freelog(LOG_VERBOSE, "Disbanding %s in %s",
			unit_type(defender)->name, pcity->name);
                pack.unit_id = defender->id;
                handle_unit_disband(pplayer, &pack);
                city_refresh(pcity);
                defender = punit;
              }
            } else defender = punit;
          } else if (incity->shield_surplus > 0) {
            pack.unit_id = punit->id;
            pack.city_id = incity->id;
            handle_unit_change_homecity(pplayer, &pack);
            city_refresh(pcity);
	    freelog(LOG_VERBOSE, "Reassigning %s from %s to %s",
		    unit_type(punit)->name, pcity->name, incity->name);
          }
        } /* end if */
      unit_list_iterate_end;
      if (pcity->shield_surplus < 0) {
        unit_list_iterate(pcity->units_supported, punit)
          if (punit != defender && pcity->shield_surplus < 0) {
	    /* the defender MUST NOT be disbanded! -- Syela */
	    freelog(LOG_VERBOSE, "Disbanding %s's %s",
		    pcity->name, unit_type(punit)->name);
            pack.unit_id = punit->id;
            handle_unit_disband_safe(pplayer, &pack, &myiter);
            city_refresh(pcity);
          }
        unit_list_iterate_end;
      }
    } /* end if we can't meet payroll */
    /* FIXME: this shouldn't be here, but in the server... */
    send_city_info(city_owner(pcity), pcity);
  city_list_iterate_end;

  sync_cities();
}

/**************************************************************************
 change the government form, if it can and there is a good reason
**************************************************************************/
#ifndef NEW_GOV_EVAL
static void ai_manage_government(struct player *pplayer)
{
  int goal;
  int subgoal;
  int failsafe;
  
  goal = game.ai_goal_government;
  /* Was G_REPUBLIC; need to be REPUBLIC+ to love */
  
  /* advantages of DEMOCRACY:
        partisans, no bribes, no corrup, +1 content if courthouse;
     disadvantages of DEMOCRACY:
        doubled unhappiness from attacking units, anarchy 
     realistically we should allow DEMOC in some circumstances but
     not yet -- Syela
  */
  if (pplayer->government == goal) {
    freelog(LOG_DEBUG, "ai_man_gov (%s): there %d", pplayer->name, goal);
    return;
  }
  if (can_change_to_government(pplayer, goal)) {
    freelog(LOG_DEBUG, "ai_man_gov (%s): change %d", pplayer->name, goal);
    ai_government_change(pplayer, goal);
    return;
  }
  failsafe = 0;
  while((subgoal = get_government(goal)->subgoal) >= 0) {
    if (can_change_to_government(pplayer, subgoal)) {
      freelog(LOG_DEBUG, "ai_man_gov (%s): change sub %d (%d)",
	      pplayer->name, subgoal, goal);
      ai_government_change(pplayer, subgoal);
      break;
    }
    goal = subgoal;
    if (++failsafe > game.government_count) {
      freelog(LOG_ERROR, "Loop in ai_manage_government? (%s)",
	      pplayer->name);
      return;
    }
  }

  if (pplayer->government == game.government_when_anarchy) {
    /* if the ai ever intends to stay anarchy, */
    /* change condition to if( (pplayer->revolution==0) && */
    if( ((pplayer->revolution<=0) || (pplayer->revolution>5))
	&& can_change_to_government(pplayer, game.default_government)) {
      freelog(LOG_DEBUG, "ai_man_gov (%s): change from anarchy",
	      pplayer->name);
      ai_government_change(pplayer, game.default_government);
    }
  }
}

#else  /* following may need updating before enabled --dwp */

#define DEBUG_AMG_RATING        (0)
#define DEBUG_AMG_BOOST		(0)
#define DEBUG_AMG_CHANGED	(0)
#define DEBUG_AMG_CHOICE	(1)
static void ai_manage_government(struct player *pplayer)
{
  static int prev_rating[10][10];  /* these are for debugging only */
  static int prev_choice[10];      /* -- SKi*/
  int best_government = pplayer->government,
      best_rating = ai_evaluate_government(pplayer, get_gov_pplayer(pplayer));
  int i;

  for (i=0; i<game.government_count; ++i) {
    struct government *g = &governments[ i ];
    int rating;

    rating = ai_evaluate_government (pplayer, g);
    /* give bonus to current government */
/*    if (i == pplayer->government)
      rating += 2500; */

    if (DEBUG_AMG_RATING && prev_rating[pplayer->player_no][i] != rating && (rating > 0 || rating > prev_rating[pplayer->player_no][i]))
      freelog (LOG_DEBUG, "%d: ai_manage_government: %s: %s: %d (%+d)", game.year, pplayer->name, g->name, rating, rating - prev_rating[pplayer->player_no][i]);
    prev_rating[pplayer->player_no][i] = rating;
    /* compare this government with the best so far */
    if (rating > best_rating || best_government == -1) {
      if (can_change_to_government(pplayer, i)) {
        /* Use this thing! -- SKi */
        best_government = i;
        best_rating = rating;
      } else {
        if (get_invention(pplayer, g->required_tech) != TECH_KNOWN && rating > 0) {
          /* Research this thing! -- SKi */
	  if (DEBUG_AMG_BOOST)
            freelog (LOG_DEBUG, "%d: ai_manage_government: boosting %s for %s (%d -> %d)",
              game.year,
	      advances[ g->required_tech ].name, pplayer->name,
              pplayer->ai.tech_want[ g->required_tech ],
              pplayer->ai.tech_want[ g->required_tech ] + rating * 2);
          pplayer->ai.tech_want[ g->required_tech ] += 250 + rating / 2; /* need constant modifier? */
        }
      }
    }
  }
  if ((DEBUG_AMG_CHANGED && prev_choice[pplayer->player_no] != best_government) || DEBUG_AMG_CHOICE)
    freelog (LOG_DEBUG, "%d: %s chooses new government: %s", game.year, pplayer->name, governments[best_government].name);
  prev_choice[pplayer->player_no] = best_government;

  if (pplayer->government != best_government) {
    ai_government_change(pplayer, best_government);
  }
  return;
}
#endif /* NEW_GOV_EVAL */

/**************************************************************************
 Main AI routine.
**************************************************************************/
void ai_do_first_activities(struct player *pplayer)
{
#ifdef UNUSED
  ai_before_work(pplayer); 
#endif
  ai_manage_units(pplayer); /* STOP.  Everything else is at end of turn. */
}

void ai_do_last_activities(struct player *pplayer)
{
/*  ai_manage_units(pplayer);  very useful to include this! -- Syela */

  /* 
   * I finally realized how stupid it was to call manage_units in
   * update_player_ac instead of right before it.  Managing units
   * before end-turn reset now. -- Syela
   */

  ai_manage_cities(pplayer);
  /* manage cities will establish our tech_wants. */
  /* if I were upgrading units, which I'm not, I would do it here -- Syela */ 
  freelog(LOG_DEBUG, "Managing %s's taxes.", pplayer->name);
  ai_manage_taxes(pplayer); 
  freelog(LOG_DEBUG, "Managing %s's government.", pplayer->name);
  ai_manage_government(pplayer);
#ifdef UNUSED
  ai_manage_diplomacy(pplayer);
#endif
  ai_manage_tech(pplayer); 
  freelog(LOG_DEBUG, "Managing %s's spaceship.", pplayer->name);
  ai_manage_spaceship(pplayer);
  freelog(LOG_DEBUG, "Managing %s's taxes.", pplayer->name);
#ifdef UNUSED
  ai_after_work(pplayer);
#endif
  freelog(LOG_DEBUG, "Done with %s.", pplayer->name);
}


int is_unit_choice_type(enum choice_type type)
{
   return type == CT_NONMIL || type == CT_ATTACKER || type == CT_DEFENDER;
}
