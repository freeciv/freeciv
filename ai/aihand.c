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
#include <player.h>
#include <city.h>
#include <cityturn.h>
#include <citytools.h>
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
#include <sys/time.h>
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
- diplomati (ambassader/bytte tech/bytte kort med anden AI)


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

void ai_before_work(struct player *pplayer);
void ai_manage_taxes(struct player *pplayer); 
void ai_manage_government(struct player *pplayer);
void ai_manage_diplomacy(struct player *pplayer);
void ai_after_work(struct player *pplayer);


/**************************************************************************
 Main AI routine.
**************************************************************************/

void ai_do_first_activities(struct player *pplayer)
{
  ai_before_work(pplayer); 
  ai_manage_units(pplayer); /* STOP.  Everything else is at end of turn. */
}

void ai_do_last_activities(struct player *pplayer)
{
/*  ai_manage_units(pplayer);  very useful to include this! -- Syela */
/* I finally realized how stupid it was to call manage_units in update_player_ac
instead of right before it.  Managing units before end-turn reset now. -- Syela */
  calculate_tech_turns(pplayer); /* has to be here thanks to the above */
  ai_manage_cities(pplayer);
/* manage cities will establish our tech_wants. */
/* if I were upgrading units, which I'm not, I would do it here -- Syela */ 
/* printf("Managing %s's taxes.\n", pplayer->name); */
  ai_manage_taxes(pplayer); 
/* printf("Managing %s's government.\n", pplayer->name); */
  ai_manage_government(pplayer); 
  ai_manage_diplomacy(pplayer);
  ai_manage_tech(pplayer); 
  ai_after_work(pplayer);
/* printf("Done with %s.\n", pplayer->name); */
}

/**************************************************************************
 update advisors/structures
**************************************************************************/
  
void ai_before_work(struct player *pplayer)
{
  ; /* all instances of fnord and references thereto have been deleted. -- Syela */
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
{ /* obsoleted by Syela */
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
{ /* total rewrite by Syela */
  int gnow = pplayer->economic.gold;
  int sad = 0, trade = 0, m, n, i, expense = 0, tot;
  int waste[40]; /* waste with N elvises */
  int elvises[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int hhjj[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int cities = 0;
  struct packet_unit_request pack;
  struct city *incity; /* stay a while, until the night is over */
  struct unit *visitor; /* stay a while, stay forever */
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
    sad += pcity->ppl_unhappy[4];
    trade += pcity->trade_prod * city_tax_bonus(pcity) / 100;
/*    printf("%s has %d trade.\n", pcity->name, pcity->trade_prod); */
    for (i = 0; i < B_LAST; i++)
      if (city_got_building(pcity, i)) expense += improvement_upkeep(pcity,i);
  city_list_iterate_end;

  if (!trade) { /* can't return right away - thanks for the evidence, Muzz */
    city_list_iterate(pplayer->cities, pcity) 
      if (ai_fix_unhappy(pcity))
        ai_scientists_taxmen(pcity);
    city_list_iterate_end;
    return; /* damn division by zero! */
  }

  pplayer->economic.luxury = 0;

  city_list_iterate(pplayer->cities, pcity) 

/* this code must be ABOVE the elvises[] if SIMPLISTIC is off */
/* printf("Does %s want to be bigger? %d\n", pcity->name, wants_to_be_bigger(pcity)); */
    if (pcity->size > 4 && pplayer->government >= G_REPUBLIC &&
        pcity->food_surplus > 0 &&
        !pcity->ppl_unhappy[4] && wants_to_be_bigger(pcity)) {
/*      printf("%d happy people in %s\n", pcity->ppl_happy[4], pcity->name);*/
      n = (((pcity->size + 1)>>1) - pcity->ppl_happy[4]) * 20;
      if (n > pcity->ppl_content[1] * 20) n += (n - pcity->ppl_content[1] * 20);
      m = (((((city_got_building(pcity, B_GRANARY) || city_affected_by_wonder(pcity,
           B_PYRAMIDS)) ? 3 : 2) * pcity->size * game.foodbox)>>1) -
           pcity->food_stock) * food_weighting(pcity->size);
/*printf("Checking HHJJ for %s, m = %d\n", pcity->name, m);*/
      tot = 0;
      for (i = 0; i <= 10; i++) {
        if (pcity->trade_prod * i * city_tax_bonus(pcity) >= n * 100) {
/*if (!tot) printf("%s celebrates at %d.\n", pcity->name, i * 10);*/
          hhjj[i] += (pcity->was_happy ? m : m>>1);
          tot++;
        }
      }
    } /* hhjj[i] is (we think) the desirability of partying with lux = 10 * i */
/* end elevated code block */

    n = (pcity->ppl_unhappy[4] - pcity->ppl_happy[4]) * 20; /* need this much lux */

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
  city_list_iterate_end;
    
  for (i = 0; i <= 10; i++) elvises[i] += (trade * i) / 10 * TRADE_WEIGHTING;
  /* elvises[i] is the production + income lost to elvises with lux = i * 10 */
  n = 0; /* default to 0 lux */
  for (i = 1; i <= maxrate; i++) if (elvises[i] < elvises[n]) n = i;
/* two thousand zero zero party over it's out of time */
  for (i = 0; i <= 10; i++) {
    hhjj[i] -= (trade * i) / 10 * TRADE_WEIGHTING; /* hhjj is now our bonus */
/*    printf("hhjj[%d] = %d for %s.\n", i, hhjj[i], pplayer->name);  */
  }

  m = n; /* storing the lux we really need */
  pplayer->economic.luxury = n * 10; /* temporary */

/* Less-intelligent previous versions of the follow equation purged. -- Syela */
  n = ((expense - gnow + cities + pplayer->ai.maxbuycost) * 10 + trade - 1) / trade;
  if (n < 0) n = 0;
  while (n > 10 - (pplayer->economic.luxury / 10) || n > maxrate) n--;

/*printf("%s has %d trade and %d expense.  Min lux = %d, tax = %d\n",
pplayer->name, trade, expense, m, n);*/

  /* want to max the hhjj */
  for (i = m; i <= maxrate && i <= 10 - n; i++) if (hhjj[i] > hhjj[m]) m = i;
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
    n = ((ai_gold_reserve(pplayer) - gnow + expense + cities) * 20 + (trade<<1) - 1) / (trade<<1);
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
    if (ai_fix_unhappy(pcity))
      ai_scientists_taxmen(pcity);
    if (pcity->shield_surplus < 0 || city_unhappy(pcity) ||
        pcity->food_stock + pcity->food_surplus < 0) 
       emergency_reallocate_workers(pplayer, pcity);
    if (pcity->shield_surplus < 0) {
      visitor = 0;
      unit_list_iterate(pcity->units_supported, punit)
        incity = map_get_city(punit->x, punit->y);
        if (incity && pcity->shield_surplus < 0) {
          if (incity == pcity) {
            if (visitor) {
printf("Disbanding %s in %s\n", unit_types[punit->type].name, pcity->name);
              pack.unit_id = punit->id;
              handle_unit_disband(pplayer, &pack);
              city_refresh(pcity);
            } else visitor = punit;
          } else if (incity->shield_surplus > 0) {
            pack.unit_id = punit->id;
            pack.city_id = incity->id;
            handle_unit_change_homecity(pplayer, &pack);
            city_refresh(pcity);
printf("Reassigning %s from %s to %s\n", unit_types[punit->type].name, pcity->name, incity->name);
          }
        } /* end if */
      unit_list_iterate_end;
      if (pcity->shield_surplus < 0 && visitor) { /* AAAAAAAaaaaaaaAAAAAAAaaaaaaaAAAAAAA */
printf("Disbanding %s in %s\n", unit_types[visitor->type].name, pcity->name);
        pack.unit_id = visitor->id;
        handle_unit_disband(pplayer, &pack);
        city_refresh(pcity);
      }
    } /* end if we can't meet payroll */
    send_city_info(city_owner(pcity), pcity, 1);
  city_list_iterate_end;
}

/* --------------------------GOVERNMENT--------------------------------- */

/**************************************************************************
 change the government form, if it can and there is a good reason
**************************************************************************/

void ai_manage_government(struct player *pplayer)
{
  int government = get_race(pplayer)->goals.government;
  government = G_REPUBLIC; /* need to be REPUBLIC+ to love */
/* advantages of DEMOCRACY: partisans, no bribes, no corrup, +1 content if courthouse */
/* disadvantages of DEMOCRACY: doubled unhappiness from attacking units, anarchy */
/* realistically we should allow DEMOC in some circumstances but not yet -- Syela */
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
    else if (can_change_to_government(pplayer, G_MONARCHY)) 
      ai_government_change(pplayer, G_MONARCHY);
    break;
  case G_REPUBLIC:
    if (can_change_to_government(pplayer, G_MONARCHY)) 
      ai_government_change(pplayer, G_MONARCHY); /* better than despotism! -- Syela */
    break;
  }
}

