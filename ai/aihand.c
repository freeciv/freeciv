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
#include "plrhand.h"
#include "spacerace.h"
#include "settlers.h" /* amortize */
#include "unithand.h"

#include "aicity.h"
#include "aidata.h"
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
    built_impr_iterate(pcity, id) {
      expense += improvement_upkeep(pcity, id);
    } built_impr_iterate_end;

  city_list_iterate_end;

  pplayer->ai.est_upkeep = expense;

  if (trade == 0) { /* can't return right away - thanks for the evidence, Muzz */
    city_list_iterate(pplayer->cities, pcity) 
      if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer, TRUE))
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
	&& wants_to_be_bigger(pcity) && ai_fuzzy(pplayer, TRUE)) {
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
	  if (tot == 0) freelog(LOG_DEBUG, "%s celebrates at %d.",
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
      if (m != 0) {
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

  if (ai_wants_no_science(pplayer)) {
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
    if (ai_fix_unhappy(pcity) && ai_fuzzy(pplayer, TRUE))
      ai_scientists_taxmen(pcity);
    if (pcity->shield_surplus < 0 || city_unhappy(pcity) ||
        pcity->food_stock + pcity->food_surplus < 0) 
       emergency_reallocate_workers(pplayer, pcity);
    if (pcity->shield_surplus < 0) {
      defender = NULL;
      unit_list_iterate(pcity->units_supported, punit)
        incity = map_get_city(punit->x, punit->y);
        if (incity && pcity->shield_surplus < 0) {
	  /* Note that disbanding here is automatically safe (we don't
	   * need to use handle_unit_disband_safe()), because the unit is
	   * in a city, so there are no passengers to get disbanded. --dwp
	   */
          if (incity == pcity) {
            if (defender) {
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
  Change the government form, if it can and there is a good reason.
**************************************************************************/
static void ai_manage_government(struct player *pplayer)
{
#undef ANALYSE
  struct ai_data *ai = ai_data_get(pplayer);
  int best_gov = 0;
  int best_val = 0;
  int really_best_val = 0;
  int really_best_req = A_UNSET;
  int i;
  int bonus = 0; /* in percentage */
  int current_gov = pplayer->government;

  if (ai_handicap(pplayer, H_AWAY)) {
    return;
  }

  for (i = 0; i < game.government_count; i++) {
    struct government *gov = &governments[i];
    int val = 0;
    int dist;

#ifdef ANALYSE
    int food_surplus = 0;
    int shield_surplus = 0;
    int luxury_total = 0;
    int tax_total = 0;
    int science_total = 0;
    int ppl_happy = 0;
    int ppl_unhappy = 0;
    int ppl_angry = 0;
    int pollution = 0;
#endif

    if (i == game.government_when_anarchy) {
      continue; /* pointless */
    }
    pplayer->government = gov->index;
    /* Ideally we should change tax rates here, but since
     * this is a rather big CPU operation, we'd rather not. */
    check_player_government_rates(pplayer);
    city_list_iterate(pplayer->cities, acity) {
      generic_city_refresh(acity, TRUE);
      auto_arrange_workers(acity);
      if (ai_fix_unhappy(acity)) {
        ai_scientists_taxmen(acity);
      }
    } city_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      val += ai_eval_calc_city(pcity, ai);
#ifdef ANALYSE
      food_surplus += pcity->food_surplus;
      shield_surplus += pcity->shield_surplus;
      luxury_total += pcity->luxury_total;
      tax_total += pcity->tax_total;
      science_total += pcity->science_total;
      ppl_happy += pcity->ppl_happy[4];
      ppl_unhappy += pcity->ppl_unhappy[4];
      ppl_angry += pcity->ppl_angry[4];
      pollution += pcity->pollution;
#endif
    } city_list_iterate_end;

    /* Bonuses for non-economic abilities */
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
    }
    if (government_has_flag(gov, G_FANATIC_TROOPS)) {
      bonus += 3; /* WAG */
    }

    val += (val * bonus) / 100;

    dist = MAX(1, num_unknown_techs_for_goal(pplayer, gov->required_tech));
    val = amortize(val, dist);
    if (val > best_val && can_change_to_government(pplayer, i)) {
      best_val = val;
      best_gov = i;
    }
    if (val > really_best_val) {
      really_best_val = val;
      really_best_req = gov->required_tech;
    }
#ifdef ANALYSE
    freelog(LOG_NORMAL, "%s govt eval %s (dist %d): %d [f%d|sh%d|l%d|g%d|sc%d|h%d|u%d|a%d|p%d]",
            pplayer->name, gov->name, dist, val, food_surplus,
            shield_surplus, luxury_total, tax_total, science_total,
            ppl_happy, ppl_unhappy, ppl_angry, pollution);
#endif
  }
  if (best_gov != current_gov) {
    ai_government_change(pplayer, best_gov); /* change */
  } else {
    pplayer->government = current_gov; /* reset */
  }

  /* Crank up tech want */
  if (get_invention(pplayer, really_best_req) == TECH_KNOWN) {
    return; /* already got it! */
  }
  pplayer->ai.tech_want[really_best_req] += (really_best_val - best_val);
  freelog(LOG_DEBUG, "%s wants %s with want %d", pplayer->name,
          advances[really_best_req].name, 
          pplayer->ai.tech_want[really_best_req]);
}

/**************************************************************************
 Main AI routine.
**************************************************************************/
void ai_do_first_activities(struct player *pplayer)
{
  ai_manage_units(pplayer); /* STOP.  Everything else is at end of turn. */
}

/**************************************************************************
  ...
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
