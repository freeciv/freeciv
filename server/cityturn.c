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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"

#include "citytools.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unittools.h"

#include "advdomestic.h"
#include "aicity.h"
#include "aitools.h"		/* for ai_advisor_choose_building/ai_choice */

#include "cityturn.h"

static void set_tax_income(struct city *pcity);
static void set_food_trade_shields(struct city *pcity);
static void add_buildings_effect(struct city *pcity);
static void set_pollution(struct city *pcity);
static void check_pollution(struct city *pcity);
static void city_support(struct city *pcity);

static void happy_copy(struct city *pcity, int i);
static void citizen_happy_size(struct city *pcity);
static void citizen_happy_luxury(struct city *pcity);
static void citizen_happy_units(struct city *pcity, int unhap);
static void citizen_happy_buildings(struct city *pcity);
static void citizen_happy_wonders(struct city *pcity);
static void unhappy_city_check(struct city *pcity);

static void city_populate(struct city *pcity);
static void city_increase_size(struct city *pcity);
static void city_reduce_size(struct city *pcity);

static int worklist_change_build_target(struct player *pplayer, 
					struct city *pcity);

static int city_build_stuff(struct player *pplayer, struct city *pcity);
static int improvement_upgrades_to(struct city *pcity, int imp);
static void upgrade_building_prod(struct city *pcity);
static Unit_Type_id unit_upgrades_to(struct city *pcity, Unit_Type_id id);
static void upgrade_unit_prod(struct city *pcity);
static void obsolete_building_test(struct city *pcity, int b1, int b2);
static void pay_for_buildings(struct player *pplayer, struct city *pcity);

static void sanity_check_city(struct city *pcity);

static int disband_city(struct city *pcity);

static void define_orig_production_values(struct city *pcity);
static int update_city_activity(struct player *pplayer, struct city *pcity);
static void nullify_caravan_and_disband_plus(struct city *pcity);

static void worker_loop(struct city *pcity, int *foodneed,
			int *prodneed, int *workers);

static int advisor_choose_build(struct player *pplayer, struct city *pcity);

/**************************************************************************
calculate the incomes according to the taxrates and # of specialists.
**************************************************************************/
static void set_tax_income(struct city *pcity)
{
  int sci = 0, tax = 0, lux = 0, rate;
  pcity->science_total = 0;
  pcity->luxury_total = 0;
  pcity->tax_total = 0;
  rate = pcity->trade_prod;
  while (rate) {
    if( get_gov_pcity(pcity)->index != game.government_when_anarchy ){
      tax += (100 - game.players[pcity->owner].economic.science - game.players[pcity->owner].economic.luxury);
      sci += game.players[pcity->owner].economic.science;
      lux += game.players[pcity->owner].economic.luxury;
    }else {/* ANARCHY */
      lux+= 100;
    }
    if (tax >= 100 && rate) {
      tax -= 100;
      pcity->tax_total++;
      rate--;
    }
    if (sci >= 100 && rate) {
      sci -= 100;
      pcity->science_total++;
      rate--;
    }
    if (lux >=100 && rate) {
      lux -= 100;
      pcity->luxury_total++;
      rate--;
    }
  }
  pcity->luxury_total+=(pcity->ppl_elvis*2); 
  pcity->science_total+=(pcity->ppl_scientist*3);
  pcity->tax_total+=(pcity->ppl_taxman*3);          
}

/**************************************************************************
Modify the incomes according to various buildings. 
**************************************************************************/
static void add_buildings_effect(struct city *pcity)
{
  int tax_bonus, science_bonus;
  int shield_bonus;

  tax_bonus = set_city_tax_bonus(pcity); /* this is the place to set them */
  science_bonus = set_city_science_bonus(pcity);
  shield_bonus = set_city_shield_bonus(pcity);
  
  pcity->shield_prod =(pcity->shield_prod*shield_bonus)/100;
  pcity->luxury_total=(pcity->luxury_total*tax_bonus)/100;
  pcity->tax_total=(pcity->tax_total*tax_bonus)/100;
  pcity->science_total=(pcity->science_total*science_bonus)/100;
  pcity->shield_surplus=pcity->shield_prod;
}

/**************************************************************************
...
**************************************************************************/
static void happy_copy(struct city *pcity, int i)
{  
  pcity->ppl_unhappy[i+1]=pcity->ppl_unhappy[i];
  pcity->ppl_content[i+1]=pcity->ppl_content[i];
  pcity->ppl_happy[i+1]=pcity->ppl_happy[i];
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_size(struct city *pcity)
{
  int workers, tmp;

  workers = pcity->size - city_specialists(pcity);
  tmp = content_citizens(&game.players[pcity->owner]);
  pcity->ppl_content[0] = MAX(0, MIN(workers, tmp));
  pcity->ppl_unhappy[0] = workers - pcity->ppl_content[0];
  pcity->ppl_happy[0] = 0; /* no one is born happy */
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_luxury(struct city *pcity)
{
  int x=pcity->luxury_total;
  happy_copy(pcity, 0);
  /* make people happy, content are made happy first, then unhappy content,
     etc.  each conversions costs 2 luxuries. */
  while (x>=2 && (pcity->ppl_content[1])) {
    pcity->ppl_content[1]--;
    pcity->ppl_happy[1]++;
    x-=2;
  }
  while (x>=4 && pcity->ppl_unhappy[1]) {
    pcity->ppl_unhappy[1]--;
    pcity->ppl_happy[1]++;
/*    x-=2; We can't seriously mean this, right? -- Syela */
    x-=4;
  }
  if (x>=2 && pcity->ppl_unhappy[1]) {
    pcity->ppl_unhappy[1]--;
    pcity->ppl_content[1]++;
    x-=2;
  }
}

/**************************************************************************
...
 Note Suffrage/Police is always dealt with elsewhere now --dwp 
**************************************************************************/
static void citizen_happy_units(struct city *pcity, int unhap)
{
  int step;         

  if (unhap>0) {                                                           
    step=MIN(unhap,pcity->ppl_content[3]);                          
    pcity->ppl_content[3]-=step;
    pcity->ppl_unhappy[3]+=step;
    unhap-=step;                     
    if (unhap>0) {                       
      step=MIN((unhap/2),pcity->ppl_happy[3]);    
      pcity->ppl_happy[3]-=step;
      pcity->ppl_unhappy[3]+=step;
      unhap -= step * 2;                                                
      if ((unhap > 0) && pcity->ppl_happy[3]) {                      
	pcity->ppl_happy[3]--;                                   
	pcity->ppl_content[3]++;                
	unhap--;              
      }                                                           
    }
  }
    /* MAKE VERY UNHAPPY CITIZENS WITH THE REST, but that is not documented */

}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_buildings(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  int faces=0;
  happy_copy(pcity, 1);
  
  if (city_got_building(pcity,B_TEMPLE)) { 
    faces+=get_temple_power(pcity);
  }
  if (city_got_building(pcity,B_COURTHOUSE) &&
      g->corruption_level == 0) {
    faces++;
  }

  if (city_got_building(pcity, B_COLOSSEUM)) 
    faces+=get_colosseum_power(pcity);
  if (city_got_effect(pcity, B_CATHEDRAL))
    faces+=get_cathedral_power(pcity);
  while (faces && pcity->ppl_unhappy[2]) {
    pcity->ppl_unhappy[2]--;
    pcity->ppl_content[2]++;
    faces--;
  }
/* no longer hijacking ppl_content[0]; seems no longer to be helpful -- Syela */
  /* TV doesn't make people happy just content...
 
  while (faces && pcity->ppl_content[2]) { 
    pcity->ppl_content[2]--;
    pcity->ppl_happy[2]++;
    faces--;
  }

  */
}

/**************************************************************************
...
**************************************************************************/
static void citizen_happy_wonders(struct city *pcity)
{
  int bonus=0;
  happy_copy(pcity, 3);
  bonus = 0;
  if (city_affected_by_wonder(pcity, B_HANGING)) {
    bonus += 1;
    if (city_got_building(pcity, B_HANGING)) 
      bonus +=2;
    while (bonus && pcity->ppl_content[4]) {
      pcity->ppl_content[4]--;
      pcity->ppl_happy[4]++;
      bonus--; 
      /* well i'm not sure what to do with the rest, 
	 will let it make unhappy content */
    }
  }
  if (city_affected_by_wonder(pcity, B_BACH)) 
    bonus+=2;
  if (city_affected_by_wonder(pcity, B_CURE))
    bonus+=1;
  while (bonus && pcity->ppl_unhappy[4]) {
    pcity->ppl_unhappy[4]--;
    pcity->ppl_content[4]++;
    bonus--;
  }
  if (city_affected_by_wonder(pcity, B_SHAKESPEARE)) {
    pcity->ppl_content[4]+=pcity->ppl_unhappy[4];
    pcity->ppl_unhappy[4]=0;
  }
}

/**************************************************************************
...
**************************************************************************/
static void unhappy_city_check(struct city *pcity)
{
  if (city_unhappy(pcity)) {
    pcity->food_surplus=MIN(0, pcity->food_surplus);
    pcity->tax_total=0;
    pcity->science_total=0;
    pcity->shield_surplus=MIN(0, pcity->shield_surplus);
  }  
}

/**************************************************************************
...
**************************************************************************/
static void set_pollution(struct city *pcity)
{
  int poppul=0;
  struct player *pplayer=&game.players[pcity->owner];
  pcity->pollution=pcity->shield_prod;
  if (city_got_building(pcity, B_RECYCLING))
    pcity->pollution/=3;
  else  if (city_got_building(pcity, B_HYDRO) ||
	   city_affected_by_wonder(pcity, B_HOOVER) ||
	    city_got_building(pcity, B_NUCLEAR))
    pcity->pollution/=2;
  
  if (!city_got_building(pcity, B_MASS)) {
    int mod = player_knows_techs_with_flag(pplayer, TF_POPULATION_POLLUTION_INC);
    /* was: A_INDUSTRIALIZATION, A_AUTOMOBILE, A_MASS, A_PLASTICS */
    poppul=(pcity->size*mod)/4;
    pcity->pollution+=poppul;
  }

  pcity->pollution=MAX(0, pcity->pollution-20);  
}

/**************************************************************************
...
**************************************************************************/
static void set_food_trade_shields(struct city *pcity)
{
  int i;
  pcity->food_prod=0;
  pcity->shield_prod=0;
  pcity->trade_prod=0;

  pcity->food_surplus=0;
  pcity->shield_surplus=0;
  pcity->corruption=0;

  city_map_iterate(x, y) {
    if(get_worker_city(pcity, x, y)==C_TILE_WORKER) {
      pcity->food_prod += city_get_food_tile(x, y, pcity);
      pcity->shield_prod += city_get_shields_tile(x, y, pcity);
      pcity->trade_prod += city_get_trade_tile(x, y, pcity);
    }
  } city_map_iterate_end;
  pcity->tile_trade=pcity->trade_prod;
  
  pcity->food_surplus=pcity->food_prod-pcity->size*2;

  for (i=0;i<4;i++) {
    pcity->trade_value[i]=trade_between_cities(pcity, find_city_by_id(pcity->trade[i]));
    pcity->trade_prod+=pcity->trade_value[i];
  }
  pcity->corruption = city_corruption(pcity, pcity->trade_prod);
  pcity->ai.trade_want = TRADE_WEIGHTING - city_corruption(pcity, TRADE_WEIGHTING);
/* AI would calculate this 1000 times otherwise; better to do it once -- Syela */
  pcity->trade_prod -= pcity->corruption;
}

/**************************************************************************
...
**************************************************************************/
int city_refresh(struct city *pcity)
{
  set_food_trade_shields(pcity);
  citizen_happy_size(pcity);
  set_tax_income(pcity);                  /* calc base luxury, tax & bulbs */
  add_buildings_effect(pcity);            /* marketplace, library wonders.. */
  set_pollution(pcity);
  citizen_happy_luxury(pcity);            /* with our new found luxuries */
  citizen_happy_buildings(pcity);         /* temple cathedral colosseum */
  city_support(pcity);                    /* manage settlers, and units */
  citizen_happy_wonders(pcity);           /* happy wonders */
  unhappy_city_check(pcity);
  return (city_happy(pcity) && pcity->was_happy);
}

/**************************************************************************
...
called on government change or wonder completion or stuff like that -- Syela
**************************************************************************/
void global_city_refresh(struct player *pplayer)
{
  conn_list_do_buffer(&pplayer->connections);
  city_list_iterate(pplayer->cities, pcity)
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  city_list_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
An "aggressive" unit is a unit which may cause unhappiness
under a Republic or Democracy.
A unit is *not* aggressive if one or more of following is true:
- zero attack strength
- inside a city
- ground unit inside a fortress within 3 squares of a friendly city
**************************************************************************/
int unit_being_aggressive(struct unit *punit)
{
  if (get_unit_type(punit->type)->attack_strength==0)
    return 0;
  if (map_get_city(punit->x,punit->y))
    return 0;
  if (is_ground_unit(punit) &&
      map_get_special(punit->x,punit->y)&S_FORTRESS) 
    return !is_unit_near_a_friendly_city (punit);
  
  return 1;
}

/**************************************************************************
...
**************************************************************************/
static void city_support(struct city *pcity)
{ 
  struct government *g = get_gov_pcity(pcity);

  int have_police = city_got_effect(pcity, B_POLICE);
  int variant = improvement_variant(B_WOMENS);

  int free_happy  = citygov_free_happy(pcity, g);
  int free_shield = citygov_free_shield(pcity, g);
  int free_food   = citygov_free_food(pcity, g);
  int free_gold   = citygov_free_gold(pcity, g);

  if (variant==0 && have_police) {
    /* ??  This does the right thing for normal Republic and Democ -- dwp */
    free_happy += g->unit_happy_cost_factor;
  }

  happy_copy(pcity, 2);

  /*
   * If you modify anything here these places might also need updating:
   * - ai/aitools.c : ai_assess_military_unhappiness
   *   Military discontentment evaluation for AI.
   *
   * P.S.  This list is by no means complete.
   * --SKi
   */

  /* military units in this city (need _not_ be home city) can make
     unhappy citizens content
  */
  if (g->martial_law_max > 0) {
    int city_units = 0;
    unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
      if (city_units < g->martial_law_max
	  && is_military_unit(punit)
	  && punit->owner == pcity->owner)
	city_units++;
    }
    unit_list_iterate_end;
    city_units *= g->martial_law_per;
    city_units = MIN(city_units, pcity->ppl_unhappy[3]);
    pcity->ppl_unhappy[3] -= city_units;
    pcity->ppl_content[3] += city_units;
  }
    
  /* loop over units, subtracting appropriate amounts of food, shields,
   * gold etc -- SKi */
  unit_list_iterate(pcity->units_supported, this_unit) {
    struct unit_type *ut = get_unit_type(this_unit->type);
    int shield_cost = utype_shield_cost(ut, g);
    int happy_cost = utype_happy_cost(ut, g);
    int food_cost = utype_food_cost(ut, g);
    int gold_cost = utype_gold_cost(ut, g);

    /* Save old values so ve can decide if the unit info should be resent */
    int old_unhappiness = this_unit->unhappiness;
    int old_upkeep      = this_unit->upkeep;
    int old_upkeep_food = this_unit->upkeep_food;
    int old_upkeep_gold = this_unit->upkeep_gold;

    /* set current upkeep on unit to zero */
    this_unit->unhappiness = 0;
    this_unit->upkeep      = 0;
    this_unit->upkeep_food = 0;
    this_unit->upkeep_gold = 0;

    /* This is how I think it should work (dwp)
     * Base happy cost (unhappiness) assumes unit is being aggressive;
     * non-aggressive units don't pay this, _except_ that field units
     * still pay 1.  Should this be always 1, or modified by other
     * factors?   Will treat as flat 1.
     */
    if (happy_cost > 0 && !unit_being_aggressive(this_unit)) {
      if (is_field_unit(this_unit)) {
	happy_cost = 1;
      } else {
	happy_cost = 0;
      }
    }
    if (happy_cost > 0 && variant==1 && have_police) {
      happy_cost--;
    }

    /* subtract values found above from city's resources -- SKi */
    if (happy_cost > 0) {
      adjust_city_free_cost(&free_happy, &happy_cost);
      if (happy_cost > 0) {
	citizen_happy_units (pcity, happy_cost);
        this_unit->unhappiness = happy_cost;
      }
    }
    if (shield_cost > 0) {
      adjust_city_free_cost(&free_shield, &shield_cost);
      if (shield_cost > 0) {
        pcity->shield_surplus -= shield_cost;
        this_unit->upkeep      = shield_cost;
      }
    }
    if (food_cost > 0) {
      adjust_city_free_cost(&free_food, &food_cost);
      if (food_cost > 0) {
        pcity->food_surplus -= food_cost;
        this_unit->upkeep_food = food_cost;
      }
    }
    if (gold_cost > 0) {
      adjust_city_free_cost(&free_gold, &gold_cost);
      if (gold_cost > 0) {
        /* FIXME: This is not implemented -- SKi */
        this_unit->upkeep_gold = gold_cost;
      }
    }
    
    /* Send unit info if anything has changed */
    if (this_unit->unhappiness != old_unhappiness
	|| this_unit->upkeep != old_upkeep
	|| this_unit->upkeep_food != old_upkeep_food
	|| this_unit->upkeep_gold != old_upkeep_gold)
      send_unit_info(unit_owner(this_unit), this_unit);
  } unit_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings_city(struct city *pcity, int refresh)
{
  struct player *pplayer = city_owner(pcity);
  int i, sold = 0;
  for (i=0;i<game.num_impr_types;i++) {
    if (city_got_building(pcity, i) 
	&& !is_wonder(i) 
	&& improvement_obsolete(pplayer, i)) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_SOLD, 
		       _("Game: %s is selling %s (obsolete) for %d."),
		       pcity->name, get_improvement_name(i), 
		       improvement_value(i));
      sold = 1;
    }
  }

  if (sold && refresh) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
    send_player_info(pplayer, 0); /* Send updated gold to all */
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    remove_obsolete_buildings_city(pcity, 0);
  } city_list_iterate_end;
}

/**************************************************************************
You need to call sync_cities for teh affected cities to be synced with the
client.
**************************************************************************/
static void worker_loop(struct city *pcity, int *foodneed,
			int *prodneed, int *workers)
{
  int bx, by, best, cur;
  int conflict[5][5];
  int e, pwr, luxneed = 0; /* I should have thought of this earlier, it is so simple */

  city_refresh(pcity);
  luxneed = 2 * (pcity->ppl_unhappy[4] - pcity->ppl_happy[4]);
  pwr = (2 * city_tax_bonus(pcity)) / 100;
  luxneed += pwr * pcity->ppl_elvis;
  if (luxneed < 0) luxneed = 0;

  e = (luxneed + pwr - 1) / pwr;
  if (e > (*workers - 1)) e = *workers - 1; /* stops the repeated emergencies. -- Syela */

/* If I were real clever, I would optimize trade by luxneed and tax_bonus -- Syela */

  *foodneed -= 2 * (*workers - 1 - e);
  *prodneed -= (*workers - 1 - e);

  freelog(LOG_DEBUG, "%s, %d workers, %d luxneed, %d e",
	  pcity->name, *workers, luxneed, e);
  freelog(LOG_DEBUG, "%s, u4 %d h4 %d pwr %d elv %d",
	  pcity->name, pcity->ppl_unhappy[4], pcity->ppl_happy[4],
	  pwr, pcity->ppl_elvis);

  if (city_happy(pcity) && wants_to_be_bigger(pcity) && pcity->size > 4)
    *foodneed += 1;

  freelog(LOG_DEBUG, "%s, foodneed %d prodneed %d",
	  pcity->name, *foodneed, *prodneed);

  /* better than nothing, not as good as a global worker allocation -- Syela */
  city_map_iterate(x, y) {
    int map_x = pcity->x + x - CITY_MAP_SIZE/2;
    int map_y = pcity->y + y - CITY_MAP_SIZE/2;

    if (normalize_map_pos(&map_x, &map_y)) {
      conflict[x][y] = -1 - minimap[map_x][map_y];
    } else {
      conflict[x][y] = 0;
    }
  } city_map_iterate_end;

  do {
    bx=0;
    by=0;
    best = 0;
    /* try to work near the city */
    city_map_iterate_outwards(x, y) {
      if(can_place_worker_here(pcity, x, y)) {
        cur = city_tile_value(pcity, x, y, *foodneed, *prodneed) - conflict[x][y];
        if (cur > best) {
          bx=x;
          by=y;
          best = cur;
	}
      }
    } city_map_iterate_outwards_end;
    if(bx || by) {
      server_set_worker_city(pcity, bx, by);
      (*workers)--; /* amazing what this did with no parens! -- Syela */
      *foodneed -= city_get_food_tile(bx,by,pcity) - 2;
      *prodneed -= city_get_shields_tile(bx,by,pcity) - 1;
    }
  } while(*workers && (bx || by));
  *foodneed += 2 * (*workers - 1 - e);
  *prodneed += (*workers - 1 - e);
  if (*prodneed > 0) {
    freelog(LOG_VERBOSE, "Ignored prodneed? in %s (%d)",
	 pcity->name, *prodneed);
  }
}

/**************************************************************************
You need to call sync_cities for teh affected cities to be synced with the
client.
**************************************************************************/
int add_adjust_workers(struct city *pcity)
{
  int workers=pcity->size;
  int iswork=0;
  int toplace;
  int foodneed;
  int prodneed = 0;

  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER) 
      iswork++;
  } city_map_iterate_end;
  iswork--; /* City center */

  if (iswork+city_specialists(pcity)>workers) {
    freelog(LOG_ERROR, "Encountered an inconsistency in "
	    "add_adjust_workers() for city %s", pcity->name);
    return 0;
  }

  if (iswork+city_specialists(pcity)==workers)
    return 1;

  toplace = workers-(iswork+city_specialists(pcity));
  foodneed = -pcity->food_surplus;
  prodneed = -pcity->shield_surplus;

  worker_loop(pcity, &foodneed, &prodneed, &toplace);

  pcity->ppl_elvis+=toplace;
  return 1;
}

/**************************************************************************
You need to call sync_cities for teh affected cities to be synced with the
client.
**************************************************************************/
void auto_arrange_workers(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  int workers = pcity->size;
  int taxwanted,sciwanted;
  int foodneed, prodneed;

  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER
	&& (x != CITY_MAP_SIZE/2 || y != CITY_MAP_SIZE/2))
      server_remove_worker_city(pcity, x, y);
  } city_map_iterate_end;
  
  foodneed=(pcity->size *2 -city_get_food_tile(2,2, pcity)) + settler_eats(pcity);
  prodneed = 0;
  prodneed -= city_get_shields_tile(2,2,pcity);
  prodneed -= citygov_free_shield(pcity, g);

  unit_list_iterate(pcity->units_supported, this_unit) {
    int shield_cost = utype_shield_cost(get_unit_type(this_unit->type), g);
    if (shield_cost > 0) {
      prodneed += shield_cost;
    }
  }
  unit_list_iterate_end;
  
  taxwanted=pcity->ppl_taxman;
  sciwanted=pcity->ppl_scientist;
  pcity->ppl_taxman=0;
  pcity->ppl_scientist=0;
  pcity->ppl_elvis=0;

  worker_loop(pcity, &foodneed, &prodneed, &workers);

  while (workers && (taxwanted ||sciwanted)) {
    if (taxwanted) {
      workers--;
      pcity->ppl_taxman++;
      taxwanted--;
    } 
    if (sciwanted && workers) {
      workers--;
      pcity->ppl_scientist++;
      sciwanted--;
    }
  }
  pcity->ppl_elvis=workers;

  city_refresh(pcity);
}

/**************************************************************************
Notices about cities that should be sent to all players.
**************************************************************************/
void send_global_city_turn_notifications(struct conn_list *dest)
{
  if (!dest)
    dest = &game.all_connections;

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      /* can_player_build_improvement() checks whether wonder is build
	 elsewhere (or destroyed) */
      if (!pcity->is_building_unit && is_wonder(pcity->currently_building)
	  && (city_turns_to_build(pcity, pcity->currently_building, 0) <= 1)
	  && can_player_build_improvement(city_owner(pcity), pcity->currently_building)) {
	notify_conn_ex(dest, pcity->x, pcity->y,
		       E_CITY_WONDER_WILL_BE_BUILT,
		       _("Game: Notice: Wonder %s in %s will be finished"
			 " next turn."), 
		       get_improvement_name(pcity->currently_building),
		       pcity->name);
      }
    } city_list_iterate_end;
  } players_iterate_end;
}

/**************************************************************************
  Send turn notifications for specified city to specified connections.
  Neither dest nor pcity may be NULL.
**************************************************************************/
void send_city_turn_notifications(struct conn_list *dest, struct city *pcity)
{
  int turns_growth, turns_granary;
  int can_grow;
 
  if (pcity->food_surplus > 0) {
    turns_growth = (city_granary_size(pcity->size) - pcity->food_stock - 1)
		   / pcity->food_surplus;

    if (!city_got_effect(pcity,B_GRANARY) && !pcity->is_building_unit &&
	(pcity->currently_building == B_GRANARY) && (pcity->shield_surplus > 0)) {
      turns_granary = (improvement_value(B_GRANARY) - pcity->shield_stock)
		      / pcity->shield_surplus;
      /* if growth and granary completion occur simultaneously, granary
	 preserves food.  -AJS */
      if ((turns_growth < 5) && (turns_granary < 5) &&
	  (turns_growth < turns_granary)) {
	notify_conn_ex(dest, pcity->x, pcity->y,
			 E_CITY_GRAN_THROTTLE,
			 _("Game: Suggest throttling growth in %s to use %s "
			   "(being built) more effectively."), pcity->name,
			 improvement_types[B_GRANARY].name);
      }
    }

    can_grow = (city_got_building(pcity, B_AQUEDUCT)
		|| pcity->size < game.aqueduct_size)
      && (city_got_building(pcity, B_SEWER)
	  || pcity->size < game.sewer_size);

    if ((turns_growth <= 0) && !city_celebrating(pcity) && can_grow) {
      notify_conn_ex(dest, pcity->x, pcity->y,
		       E_CITY_MAY_SOON_GROW,
		       _("Game: %s may soon grow to size %i."),
		       pcity->name, pcity->size + 1);
    }
  } else {
    if (pcity->food_stock + pcity->food_surplus <= 0 && pcity->food_surplus < 0) {
      notify_conn_ex(dest, pcity->x, pcity->y,
		     E_CITY_FAMINE_FEARED,
		     _("Game: Warning: Famine feared in %s."),
		     pcity->name);
    }
  }
  
}

/**************************************************************************
...
**************************************************************************/
void begin_cities_turn(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity)
     define_orig_production_values(pcity);
  city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void update_city_activities(struct player *pplayer)
{
  int gold;
  gold=pplayer->economic.gold;
  pplayer->got_tech=0;
  city_list_iterate(pplayer->cities, pcity)
     update_city_activity(pplayer, pcity);
  city_list_iterate_end;
  pplayer->ai.prev_gold = gold;
  if (gold-(gold-pplayer->economic.gold)*3<0) {
    notify_player_ex(pplayer, -1, -1, E_LOW_ON_FUNDS,
		     _("Game: WARNING, we're LOW on FUNDS sire."));  
  }
    /* uncomment to unbalance the game, like in civ1 (CLG)
      if (pplayer->got_tech && pplayer->research.researched > 0)    
        pplayer->research.researched=0;
    */
}

/**************************************************************************
...
**************************************************************************/
void city_auto_remove_worker(struct city *pcity)
{
  if(pcity->size<1) {      
    remove_city_from_minimap(pcity->x, pcity->y);
    remove_city(pcity);
    return;
  }
  if(city_specialists(pcity)) {
    if(pcity->ppl_taxman) {
      pcity->ppl_taxman--;
    } else if(pcity->ppl_scientist) {
      pcity->ppl_scientist--;
    } else {
      assert(pcity->ppl_elvis);
      pcity->ppl_elvis--; 
    }
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
    return;
  } 
  auto_arrange_workers(pcity);
  sync_cities();
}

/**************************************************************************
Note: We do not send info about the city to the clients as part of this function
**************************************************************************/
static void city_increase_size(struct city *pcity)
{
  struct player *powner = city_owner(pcity);
  int have_square;
  int has_granary = city_got_effect(pcity, B_GRANARY);
  int rapture_grow = city_rapture_grow(pcity); /* check before size increase! */
  int new_food;

  if (!city_got_building(pcity, B_AQUEDUCT)
      && pcity->size>=game.aqueduct_size) {/* need aqueduct */
    if (!pcity->is_building_unit && pcity->currently_building == B_AQUEDUCT) {
      notify_player_ex(powner, pcity->x, pcity->y, E_CITY_AQ_BUILDING,
		       _("Game: %s needs %s (being built) "
			 "to grow any further."), pcity->name,
		       improvement_types[B_AQUEDUCT].name);
    } else {
      notify_player_ex(powner, pcity->x, pcity->y, E_CITY_AQUEDUCT,
		       _("Game: %s needs %s to grow any further."),
		       pcity->name, improvement_types[B_AQUEDUCT].name);
    }
    /* Granary can only hold so much */
    new_food = (city_granary_size(pcity->size) *
		(100 - game.aqueductloss/(1+has_granary))) / 100;
    pcity->food_stock = MIN(pcity->food_stock, new_food);
    return;
  }

  if (!city_got_building(pcity, B_SEWER)
      && pcity->size>=game.sewer_size) {/* need sewer */
    if (!pcity->is_building_unit && pcity->currently_building == B_SEWER) {
      notify_player_ex(powner, pcity->x, pcity->y, E_CITY_AQ_BUILDING,
		       _("Game: %s needs %s (being built) "
			 "to grow any further."), pcity->name,
		       improvement_types[B_SEWER].name);
    } else {
      notify_player_ex(powner, pcity->x, pcity->y, E_CITY_AQUEDUCT,
		       _("Game: %s needs %s to grow any further."),
		       pcity->name, improvement_types[B_SEWER].name);
    }
    /* Granary can only hold so much */
    new_food = (city_granary_size(pcity->size) *
		(100 - game.aqueductloss/(1+has_granary))) / 100;
    pcity->food_stock = MIN(pcity->food_stock, new_food);
    return;
  }

  pcity->size++;
  /* Do not empty food stock if city is growing by celebrating */
  if (rapture_grow) {
    new_food = city_granary_size(pcity->size);
  } else {
    if (has_granary)
      new_food = city_granary_size(pcity->size) / 2;
    else
      new_food = 0;
  }
  pcity->food_stock = MIN(pcity->food_stock, new_food);

  /* If there is enough food, and the city is big enough,
   * make new citizens into scientists or taxmen -- Massimo */
  /* Ignore food if no square can be worked */
  have_square = FALSE;
  city_map_iterate(x, y) {
    if (can_place_worker_here(pcity, x, y)) {
      have_square = TRUE;
    }
  } city_map_iterate_end;
  if (((pcity->food_surplus >= 2) || !have_square)  &&  pcity->size >= 5  &&
      (pcity->city_options & ((1<<CITYO_NEW_EINSTEIN) | (1<<CITYO_NEW_TAXMAN)))){

    if (pcity->city_options & (1<<CITYO_NEW_EINSTEIN)) {
      pcity->ppl_scientist++;
    } else { /* now pcity->city_options & (1<<CITYO_NEW_TAXMAN) is true */
      pcity->ppl_taxman++;
    }

  } else {
    if (!add_adjust_workers(pcity))
      auto_arrange_workers(pcity);
  }

  city_refresh(pcity);

  if (powner->ai.control) /* don't know if we need this -- Syela */
    if (ai_fix_unhappy(pcity))
      ai_scientists_taxmen(pcity);

  notify_player_ex(powner, pcity->x, pcity->y, E_CITY_GROWTH,
                   _("Game: %s grows to size %d."), pcity->name, pcity->size);

  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
static void city_reduce_size(struct city *pcity)
{
  notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_CITY_FAMINE,
		   _("Game: Famine causes population loss in %s."), pcity->name);
  if (city_got_effect(pcity, B_GRANARY))
    pcity->food_stock=city_granary_size(pcity->size-1)/2;
  else
    pcity->food_stock=0;
  pcity->size--;

  city_auto_remove_worker(pcity);
}
 
/**************************************************************************
  Check whether the population can be increased or
  if the city is unable to support a 'settler'...
**************************************************************************/
static void city_populate(struct city *pcity)
{
  pcity->food_stock+=pcity->food_surplus;
  if(pcity->food_stock >= city_granary_size(pcity->size) 
     || city_rapture_grow(pcity)) {
    city_increase_size(pcity);
  }
  else if(pcity->food_stock<0) {
    /* FIXME: should this depend on units with ability to build
     * cities or on units that require food in uppkeep?
     * I'll assume citybuilders (units that 'contain' 1 pop) -- sjolie
     * The above may make more logical sense, but in game terms
     * you want to disband a unit that is draining your food
     * reserves.  Hence, I'll assume food upkeep > 0 units. -- jjm
     */
    unit_list_iterate(pcity->units_supported, punit) {
      if (get_unit_type(punit->type)->food_cost > 0) {
	char *utname = get_unit_type(punit->type)->name;
	wipe_unit_safe(punit, &myiter);
	notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_UNIT_LOST,
			 _("Game: Famine feared in %s, %s lost!"), 
			 pcity->name, utname);
	gamelog(GAMELOG_UNITFS, "%s lose %s (famine)",
		get_nation_name_plural(game.players[pcity->owner].nation),
		utname);
	if (city_got_effect(pcity, B_GRANARY))
	  pcity->food_stock=city_granary_size(pcity->size)/2;
	else
	  pcity->food_stock=0;
	return;
      }
    }
    unit_list_iterate_end;
    city_reduce_size(pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
static int advisor_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice choice;
  int i;
  int id=-1;
  int want=0;

  if (!game.players[pcity->owner].ai.control)
    ai_eval_buildings(pcity); /* so that ai_advisor is smart even for humans */
  ai_advisor_choose_building(pcity, &choice); /* much smarter version -- Syela */
  freelog(LOG_DEBUG, "Advisor_choose_build got %d/%d"
	  " from ai_advisor_choose_building.",
	  choice.choice, choice.want);
  id = choice.choice;
  want = choice.want;

  if (id!=-1 && id != B_LAST && want > 0) {
    change_build_target(pplayer, pcity, id, 0, E_IMP_AUTO);
    return 1; /* making something.  return value = 1 */
  }

  for (i=0;i<game.num_impr_types;i++)
    if(can_build_improvement(pcity, i) && i != B_PALACE) { /* build something random, undecided */
      pcity->currently_building=i;
      pcity->is_building_unit=0;
      return 0;
    }
  return 0;
}

/**************************************************************************
  Examine the worklist and change the build target.  Return 0 if no
  targets are available to change to.  Otherwise return non-zero.  Has
  the side-effect of removing from the worklist any no-longer-available
  targets as well as the target actually selected, if any.
**************************************************************************/
static int worklist_change_build_target(struct player *pplayer, struct city *pcity)
{
  int success = 0;
  int i;

  if (worklist_is_empty(pcity->worklist))
    /* Nothing in the worklist; bail now. */
    return 0;

  i = 0;
  while (1) {
    int target, is_unit;

    /* What's the next item in the worklist? */
    if (!worklist_peek_ith(pcity->worklist, &target, &is_unit, i))
      /* Nothing more in the worklist.  Ah, well. */
      break;

    i++;

    /* Sanity checks */
    if (is_unit &&
	!can_build_unit(pcity, target)) {
      int new_target = unit_upgrades_to(pcity, target);

      /* If the city can never build this unit or its descendants, drop it. */
      if (!can_eventually_build_unit(pcity, new_target)) {
	/* Nope, never in a million years. */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 /* Yes, warn about the targets that's actually
			    in the worklist, not its obsolete-closure
			    new_target. */
			 get_unit_type(target)->name);
	/* Purge this worklist item. */
	worklist_remove(pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      }

      /* Maybe we can just upgrade the target to what the city /can/ build. */
      if (new_target == target) {
	/* Nope, we're stuck.  Dump this item from the worklist. */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist; "
			   "tech not yet available.  Postponing..."),
			 pcity->name,
			 get_unit_type(target)->name);
	continue;
      } else {
	/* Yep, we can go after new_target instead.  Joy! */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_WORKLIST,
			 _("Game: Production of %s is upgraded to %s in %s."),
			 get_unit_type(target)->name, 
			 get_unit_type(new_target)->name,
			 pcity->name);
	target = new_target;
      }
    } else if (!is_unit && !can_build_improvement(pcity, target)) {
      int new_target = improvement_upgrades_to(pcity, target);

      /* If the city can never build this improvement, drop it. */
      if (!can_eventually_build_improvement(pcity, new_target)) {
	/* Nope, never in a million years. */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist.  "
			   "Purging..."),
			 pcity->name,
			 get_impr_name_ex(pcity, target));

	/* Purge this worklist item. */
	worklist_remove(pcity->worklist, i-1);
	/* Reset i to index to the now-next element. */
	i--;
	
	continue;
      }


      /* Maybe this improvement has been obsoleted by something that
	 we can build. */
      if (new_target == target) {
	/* Nope, no use.  *sigh*  */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
			 _("Game: %s can't build %s from the worklist; "
			   "tech not yet available.  Postponing..."),
			 pcity->name,
			 get_impr_name_ex(pcity, target));
	continue;
      } else {
	/* Hey, we can upgrade the improvement!  */
	notify_player_ex(pplayer, pcity->x, pcity->y, E_WORKLIST,
			 _("Game: Production of %s is upgraded to %s in %s."),
			 get_impr_name_ex(pcity, target), 
			 get_impr_name_ex(pcity, new_target),
			 pcity->name);
	target = new_target;
      }
    }

    /* All okay.  Switch targets. */
    change_build_target(pplayer, pcity, target, is_unit, E_WORKLIST);

    success = 1;
    break;
  }

  if (success) {
    /* i is the index immediately _after_ the item we're changing to.
       Remove the (i-1)th item from the worklist. */
    worklist_remove(pcity->worklist, i-1);
  }

  if (worklist_is_empty(pcity->worklist)) {
    /* There *was* something in the worklist, but it's empty now.  Bug the
       player about it. */
    notify_player_ex(pplayer, pcity->x, pcity->y, E_WORKLIST,
		     _("Game: %s's worklist is now empty."),
		     pcity->name);
  }

  return success;
}

/**************************************************************************
...
**************************************************************************/
static void obsolete_building_test(struct city *pcity, int b1, int b2)
{ 
  if (pcity->currently_building==b1 && 
      can_build_improvement(pcity, b2))
    pcity->currently_building=b2;
}

/**************************************************************************
  If imp is obsolete, return the improvement that _can_ be built that
  lead to imp's obsolesence.
  !!! Note:  I hear that the building ruleset code is going to be
  overhauled soon.  If this happens, then this function should be updated
  to follow the new model.  This function will probably look a lot like
  unit_upgrades_to().
**************************************************************************/
static int improvement_upgrades_to(struct city *pcity, int imp)
{
  if (imp == B_BARRACKS && can_build_improvement(pcity, B_BARRACKS3))
    return B_BARRACKS3;
  else if (imp == B_BARRACKS && can_build_improvement(pcity, B_BARRACKS2))
    return B_BARRACKS2;
  else if (imp == B_BARRACKS2 && can_build_improvement(pcity, B_BARRACKS3))
    return B_BARRACKS3;
  else
    return imp;
}

/**************************************************************************
...
**************************************************************************/
static void upgrade_building_prod(struct city *pcity)
{
  obsolete_building_test(pcity, B_BARRACKS,B_BARRACKS3);
  obsolete_building_test(pcity, B_BARRACKS,B_BARRACKS2);
  obsolete_building_test(pcity, B_BARRACKS2,B_BARRACKS3);
}

/**************************************************************************
  Follow the list of obsoleted_by units until we hit something that
  we can build.  Return id if we can't upgrade at all.  NB:  returning
  id doesn't guarantee that pcity really _can_ build id; just that
  pcity can't build whatever _obsoletes_ id.
**************************************************************************/
static Unit_Type_id unit_upgrades_to(struct city *pcity, Unit_Type_id id)
{
  Unit_Type_id latest_ok = id;

  if (!can_build_unit_direct(pcity, id))
    return -1;
  while(unit_type_exists(id = unit_types[id].obsoleted_by))
    if (can_build_unit_direct(pcity, id))
      latest_ok = id;

  return latest_ok;
}

/**************************************************************************
...
**************************************************************************/
static void upgrade_unit_prod(struct city *pcity)
{
  struct player *pplayer=&game.players[pcity->owner];
  int id = pcity->currently_building;
  int id2 = unit_upgrades_to(pcity, unit_types[id].obsoleted_by);

  if (can_build_unit_direct(pcity, id2)) {
    pcity->currently_building=id2;
    notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_UPGRADED, 
		  _("Game: Production of %s is upgraded to %s in %s."),
		  get_unit_type(id)->name, 
		  get_unit_type(id2)->name , 
		  pcity->name);
  }
}

/**************************************************************************
return 0 if the city is removed
return 1 otherwise
**************************************************************************/
static int city_build_stuff(struct player *pplayer, struct city *pcity)
{
  struct government *g = get_gov_pplayer(pplayer);
  int space_part;

  while (pcity->shield_surplus<0) {
    unit_list_iterate(pcity->units_supported, punit) {
      if (utype_shield_cost(get_unit_type(punit->type), g)) {
	notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_LOST,
			 _("Game: %s can't upkeep %s, unit disbanded."),
			 pcity->name, get_unit_type(punit->type)->name);
	wipe_unit_safe(punit, &myiter);
	break;
      }
    }
    unit_list_iterate_end;
  }

  /* Now we confirm changes made last turn. */
  pcity->shield_stock+=pcity->shield_surplus;
  pcity->before_change_shields=pcity->shield_stock;
  nullify_caravan_and_disband_plus(pcity);

  if (!pcity->is_building_unit) {
    if (pcity->currently_building==B_CAPITAL) {
      pplayer->economic.gold+=pcity->shield_surplus;
      pcity->before_change_shields=0;
      pcity->shield_stock=0;
    }    
    upgrade_building_prod(pcity);
    if (!can_build_improvement(pcity, pcity->currently_building)) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
		    _("Game: %s is building %s, which is no longer available."),
	pcity->name,get_impr_name_ex(pcity, pcity->currently_building));
      return 1;
    }
    if (pcity->shield_stock>=improvement_value(pcity->currently_building)) {
      if (pcity->currently_building==B_PALACE) {
	city_list_iterate(pplayer->cities, palace) 
	  if (city_got_building(palace, B_PALACE)) {
	    palace->improvements[B_PALACE]=0;
	    break;
	  }
	city_list_iterate_end;
      }

      space_part = 1;
      if (pcity->currently_building == B_SSTRUCTURAL) {
	pplayer->spaceship.structurals++;
      } else if (pcity->currently_building == B_SCOMP) {
	pplayer->spaceship.components++;
      } else if (pcity->currently_building == B_SMODULE) {
	pplayer->spaceship.modules++;
      } else {
	space_part = 0;
	pcity->improvements[pcity->currently_building]=1;
      }
      pcity->before_change_shields-=
			  improvement_value(pcity->currently_building); 
      pcity->shield_stock-=improvement_value(pcity->currently_building);
      pcity->turn_last_built = game.year;
      /* to eliminate micromanagement */
      if(is_wonder(pcity->currently_building)) {
	game.global_wonders[pcity->currently_building]=pcity->id;
	notify_player_ex(0, pcity->x, pcity->y, E_WONDER_BUILD,
		      _("Game: The %s have finished building %s in %s."),
		      get_nation_name_plural(pplayer->nation),
		      get_impr_name_ex(pcity, pcity->currently_building),
		      pcity->name);
        gamelog(GAMELOG_WONDER,"%s build %s in %s",
                get_nation_name_plural(pplayer->nation),
                get_impr_name_ex(pcity, pcity->currently_building),
                pcity->name);

      } else 
	gamelog(GAMELOG_IMP, "%s build %s in %s",
                get_nation_name_plural(pplayer->nation),
                get_impr_name_ex(pcity, pcity->currently_building),
                pcity->name);
      
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_BUILD,
		    _("Game: %s has finished building %s."), pcity->name, 
		    improvement_types[pcity->currently_building].name
		    );

      if (pcity->currently_building==B_DARWIN) {
	notify_player(pplayer, 
		      _("Game: %s boosts research, "
			"you gain 2 immediate advances."),
		      improvement_types[B_DARWIN].name);
	tech_researched(pplayer);
	tech_researched(pplayer);
      }
      if (space_part && pplayer->spaceship.state == SSHIP_NONE) {
	notify_player_ex(0, pcity->x, pcity->y, E_SPACESHIP,
			 _("Game: The %s have started building a spaceship!"),
			 get_nation_name_plural(pplayer->nation));
	pplayer->spaceship.state = SSHIP_STARTED;
      }
      if (space_part) {
	send_spaceship_info(pplayer, 0);
      } else {
	city_refresh(pcity);
      }
      /* If there's something in the worklist, change the build target.
       * Else if just built a spaceship part, keep building the same part.
       * (Fixme? - doesn't check whether spaceship part is still sensible.)
       * Else co-opt AI routines as "city advisor".
       */
      if (!worklist_change_build_target(pplayer, pcity) && !space_part) {
	/* Fall back to the good old ways. */
	freelog(LOG_DEBUG, "Trying advisor_choose_build.");
	advisor_choose_build(pplayer, pcity);
	freelog(LOG_DEBUG, "Advisor_choose_build didn't kill us.");
      }
    } 
  } else { /* is_building_unit */
    upgrade_unit_prod(pcity);

    /* FIXME: F_CITIES should be changed to any unit
     * that 'contains' 1 (or more) pop -- sjolie
     */
    if (pcity->shield_stock>=unit_value(pcity->currently_building)) {
      if (unit_flag(pcity->currently_building, F_CITIES)) {
	if (pcity->size==1) {

	  /* Should we disband the city? -- Massimo */
	  if (pcity->city_options & ((1<<CITYO_DISBAND))) {
	    return !disband_city(pcity);
	  } else {
	    notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
			     _("Game: %s can't build %s yet."),
			     pcity->name, unit_name(pcity->currently_building));
	    return 1;
	  }

	}
	pcity->size--;
	city_auto_remove_worker(pcity);
      }
      
      pcity->turn_last_built = game.year;
      /* don't update turn_last_built if we returned above for size==1 */

      create_unit(pplayer, pcity->x, pcity->y, pcity->currently_building,
		  do_make_unit_veteran(pcity, pcity->currently_building), 
		  pcity->id, -1);
      /* to eliminate micromanagement, we only subtract the unit's cost */
      pcity->before_change_shields-=unit_value(pcity->currently_building); 
      pcity->shield_stock-=unit_value(pcity->currently_building);

      notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_BUILD,
		       _("Game: %s is finished building %s."), 
		       pcity->name, 
		       unit_types[pcity->currently_building].name);

      gamelog(GAMELOG_UNIT, "%s build %s in %s (%i,%i)",
	      get_nation_name_plural(pplayer->nation), 
	      unit_types[pcity->currently_building].name,
	      pcity->name, pcity->x, pcity->y);

      /* If there's something in the worklist, change the build target. 
	 If there's nothing there, worklist_change_build_target won't
	 do anything. */
      worklist_change_build_target(pplayer, pcity);
    }
  }

  return 1;
}

/**************************************************************************
...
**************************************************************************/
static void pay_for_buildings(struct player *pplayer, struct city *pcity)
{
  int i;
  for (i=0;i<game.num_impr_types;i++) 
    if (city_got_building(pcity, i)) {
      if (is_wonder(i)) {
	if (wonder_obsolete(i)) {
	  switch (improvement_types[i].upkeep) {
	  case 1:
	    pplayer->economic.gold+=3;
	    break;
	  case 2:
	    update_tech(pplayer, 3);
	    break;
	  }
	}
      } else if( pplayer->government != game.government_when_anarchy ){
	if (pplayer->economic.gold-improvement_upkeep(pcity, i)<0) {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_AUCTIONED,
			   _("Game: Can't afford to maintain %s in %s, "
			     "building sold!"), 
			   improvement_types[i].name, pcity->name);
	  do_sell_building(pplayer, pcity, i);
	  city_refresh(pcity);
	} else
	  pplayer->economic.gold-=improvement_upkeep(pcity, i);
      }
    }
}

/**************************************************************************
 Add some Pollution if we have waste
**************************************************************************/
static void check_pollution(struct city *pcity)
{
  int x,y;
  int k=100;
  if (pcity->pollution && myrand(100)<=pcity->pollution) {
    while (k) {
      /* place pollution somewhere in city radius */
      x=myrand(5)-2;
      y=myrand(5)-2;
      if ( ( x != -2 && x != 2 ) || ( y != -2 && y != 2 ) ) {
	x=map_adjust_x(pcity->x+x); y=map_adjust_y(pcity->y+y);
	if ( (map_get_terrain(x,y)!=T_OCEAN && map_get_terrain(x,y)<=T_TUNDRA) &&
	     (!(map_get_special(x,y)&S_POLLUTION)) ) { 
	  map_set_special(x,y, S_POLLUTION);
	  send_tile_info(0, x, y);
	  notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_POLLUTION,
			   _("Game: Pollution near %s."), pcity->name);
	  return;
	}
      }
      k--;
    }
    freelog(LOG_DEBUG, "pollution not placed: city: %s", pcity->name);
  }
}

/**************************************************************************
...
**************************************************************************/
static void sanity_check_city(struct city *pcity)
{
  int size=pcity->size;
  int iswork=0;
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER) 
	iswork++;
  } city_map_iterate_end;
  iswork--;
  if (iswork+city_specialists(pcity)!=size) {
    freelog(LOG_ERROR,
	    "%s is bugged: size:%d workers:%d elvis: %d tax:%d sci:%d",
	    pcity->name, size, iswork, pcity->ppl_elvis,
	    pcity->ppl_taxman, pcity->ppl_scientist); 
    auto_arrange_workers(pcity);
    sync_cities();
  }
}

/**************************************************************************
...
**************************************************************************/
void city_incite_cost(struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  
  if (city_got_building(pcity, B_PALACE)) 
    pcity->incite_revolt_cost=1000000;
  else {
    pcity->incite_revolt_cost=city_owner(pcity)->economic.gold +1000;
    capital=find_palace(city_owner(pcity));
    if (capital) {
      int tmp = map_distance(capital->x, capital->y, pcity->x, pcity->y);
      dist=MIN(32, tmp);
    }
    else 
      dist=32;
    if (city_got_building(pcity, B_COURTHOUSE)) 
      dist/=2;
    if (g->fixed_corruption_distance)
      dist = MIN(g->fixed_corruption_distance, dist);
    pcity->incite_revolt_cost/=(dist + 3);
    pcity->incite_revolt_cost*=pcity->size;
    if (city_unhappy(pcity))
      pcity->incite_revolt_cost/=2;
    if (unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)==0)
      pcity->incite_revolt_cost/=2;
  }
}

/**************************************************************************
 Called every turn, at beginning of turn, for every city.
**************************************************************************/
static void define_orig_production_values(struct city *pcity)
{
  /* remember what this city is building at start of turn,
     so user can switch production back without penalty */
  pcity->changed_from_id = pcity->currently_building;
  pcity->changed_from_is_unit = pcity->is_building_unit;

  freelog(LOG_DEBUG,
	  "In %s, building %s.  Beg of Turn shields = %d",
	  pcity->name,
	  pcity->changed_from_is_unit ?
	    unit_types[pcity->changed_from_id].name :
	    improvement_types[pcity->changed_from_id].name,
	  pcity->before_change_shields
	  );
}

/**************************************************************************
...
**************************************************************************/
static void nullify_caravan_and_disband_plus(struct city *pcity)
{
  pcity->disbanded_shields=0;
  pcity->caravan_shields=0;
}

/**************************************************************************
...
**************************************************************************/
void nullify_prechange_production(struct city *pcity)
{
  nullify_caravan_and_disband_plus(pcity);
  pcity->before_change_shields=0;
}

/**************************************************************************
 Called every turn, at end of turn, for every city.
**************************************************************************/
static int update_city_activity(struct player *pplayer, struct city *pcity)
{
  struct government *g = get_gov_pcity(pcity);
  int got_tech = 0;

  city_refresh(pcity);

  /* the AI often has widespread disorder when the Gardens or Oracle
     become obsolete.  This is a quick hack to prevent this.  980805 -- Syela */
  while (pplayer->ai.control && city_unhappy(pcity)) {
    if (!ai_make_elvis(pcity)) break;
  } /* putting this lower in the routine would basically be cheating. -- Syela */

  /* reporting of celebrations rewritten, copying the treatment of disorder below,
     with the added rapture rounds count.  991219 -- Jing */
  if (city_build_stuff(pplayer, pcity)) {
    if (city_celebrating(pcity)) {
      pcity->rapture++;
      if (pcity->rapture == 1)
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_LOVE,
			 _("Game: We Love The %s Day celebrated in %s."), 
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
    }
    else {
      if (pcity->rapture)
	notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_NORMAL,
			 _("Game: We Love The %s Day canceled in %s."),
			 get_ruler_title(pplayer->government, pplayer->is_male,
					 pplayer->nation),
			 pcity->name);
      pcity->rapture=0;
    }
    pcity->was_happy=city_happy(pcity);

    /* City population updated here, after the rapture stuff above. --Jing */
    {
      int id=pcity->id;
      city_populate(pcity);
      if(!player_find_city_by_id(pplayer, id))
	return 0;
    }

    pcity->is_updated=1;

    pcity->did_sell=0;
    pcity->did_buy=0;
    if (city_got_building(pcity, B_AIRPORT))
      pcity->airlift=1;
    else
      pcity->airlift=0;
    if (update_tech(pplayer, pcity->science_total)) 
      got_tech = 1;
    pplayer->economic.gold+=pcity->tax_total;
    pay_for_buildings(pplayer, pcity);

    if(city_unhappy(pcity)) { 
      pcity->anarchy++;
      if (pcity->anarchy == 1) 
        notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_DISORDER,
	  	         _("Game: Civil disorder in %s."), pcity->name);
      else
        notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_DISORDER,
		         _("Game: CIVIL DISORDER CONTINUES in %s."),
			 pcity->name);
    }
    else {
      if (pcity->anarchy)
        notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_NORMAL,
	  	         _("Game: Order restored in %s."), pcity->name);
      pcity->anarchy=0;
    }
    check_pollution(pcity);
    city_incite_cost(pcity);

    send_city_info(0, pcity);
    if (pcity->anarchy>2 && government_has_flag(g, G_REVOLUTION_WHEN_UNHAPPY)) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_ANARCHY,
		       _("Game: The people have overthrown your %s, "
			 "your country is in turmoil."),
		       get_government_name(g->index));
      handle_player_revolution(pplayer);
    }
    sanity_check_city(pcity);
  }
  return got_tech;
}

/**************************************************************************
 disband a city into a settler, supported by the closest city -- Massimo
**************************************************************************/
static int disband_city(struct city *pcity)
{
  struct player *pplayer = get_player(pcity->owner);
  int x = pcity->x, y = pcity->y;
  struct city *rcity=NULL;

  /* find closest city other than pcity */
  rcity = find_closest_owned_city(pplayer, x, y, 0, pcity);

  if (!rcity) {
    /* What should we do when we try to disband our only city? */
    notify_player_ex(pplayer, x, y, E_CITY_CANTBUILD,
		     _("Game: %s can't build %s yet, "
		     "and we can't disband our only city."),
		     pcity->name, unit_name(pcity->currently_building));
    return 0;
  }

  create_unit(pplayer, x, y, pcity->currently_building,
	      do_make_unit_veteran(pcity, pcity->currently_building), 
	      pcity->id, -1);

  /* shift all the units supported by pcity (including the new settler) to rcity.
     transfer_city_units does not make sure no units are left floating without a
     transport, but since all units are transfered this is not a problem. */
  transfer_city_units(pplayer, pplayer, &pcity->units_supported, rcity, pcity, -1, 1);

  notify_player_ex(pplayer, x, y, E_UNIT_BUILD,
		   _("Game: %s is disbanded into %s."), 
		   pcity->name, unit_types[pcity->currently_building].name);
  gamelog(GAMELOG_UNIT, "%s (%i, %i) disbanded into %s by the %s", pcity->name, 
	  x,y, unit_types[pcity->currently_building].name,
	  get_nation_name_plural(pplayer->nation));

  remove_city(pcity);
  return 1;
}
