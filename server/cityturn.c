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

#include <game.h>
#include <player.h>
#include <unitfunc.h>
#include <civserver.h>
#include <map.h>
#include <maphand.h>
#include <mapgen.h>
#include <cityhand.h>
#include <cityturn.h>
#include <citytools.h>
#include <unit.h>
#include <unittools.h>
#include <city.h>
#include <player.h>
#include <tech.h>
#include <shared.h>
#include <plrhand.h>
#include <events.h>
#include <aicity.h>
#include <aitools.h> /* for ai_advisor_choose_building/ai_choice */
#include <settlers.h>
#include <advdomestic.h>

extern signed short int minimap[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];

static void set_trade_prod(struct city *pcity);
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
static void city_settlersupport(struct city *pcity);
static void city_increase_size(struct city *pcity);
static void city_reduce_size(struct city *pcity);

static void city_build_stuff(struct player *pplayer, struct city *pcity);
static void upgrade_building_prod(struct city *pcity);
static void upgrade_unit_prod(struct city *pcity);
static void obsolete_building_test(struct city *pcity, int b1, int b2);
static void pay_for_buildings(struct player *pplayer, struct city *pcity);

static void sanity_check_city(struct city *pcity);

static int update_city_activity(struct player *pplayer, struct city *pcity);

void set_trade_prod(struct city *pcity)
{
  int i;
  for (i=0;i<4;i++) {
    pcity->trade_value[i]=trade_between_cities(pcity, find_city_by_id(pcity->trade[i]));
    pcity->trade_prod+=pcity->trade_value[i];
  }
  pcity->corruption = city_corruption(pcity, pcity->trade_prod);
  pcity->ai.trade_want = 8 - city_corruption(pcity, 8);
/* AI would calculate this 1000 times otherwise; better to do it once -- Syela */
  pcity->trade_prod -= pcity->corruption;
}


/**************************************************************************
calculate the incomes according to the taxrates and # of specialists.
**************************************************************************/
void set_tax_income(struct city *pcity)
{
  int sci = 0, tax = 0, lux = 0, rate;
  pcity->science_total = 0;
  pcity->luxury_total = 0;
  pcity->tax_total = 0;
  rate = pcity->trade_prod;
  while (rate) {
    if( game.players[pcity->owner].government!= G_ANARCHY ){
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
void add_buildings_effect(struct city *pcity)
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
void happy_copy(struct city *pcity, int i)
{  
  pcity->ppl_unhappy[i+1]=pcity->ppl_unhappy[i];
  pcity->ppl_content[i+1]=pcity->ppl_content[i];
  pcity->ppl_happy[i+1]=pcity->ppl_happy[i];
}

/**************************************************************************
...
**************************************************************************/
void citizen_happy_size(struct city *pcity)
{
  int citizens;
  citizens = min(pcity->size, content_citizens(&game.players[pcity->owner])); 
  pcity->ppl_content[0] = max(0, (citizens - city_specialists(pcity)));
  pcity->ppl_unhappy[0] = max(0, (pcity->size - (pcity->ppl_content[0] + city_specialists(pcity))));
  pcity->ppl_happy[0]=0;
}

/**************************************************************************
...
**************************************************************************/
void citizen_happy_luxury(struct city *pcity)
{
  int x=pcity->luxury_total;
  happy_copy(pcity, 0);
  /* make people happy, content are made happy first, then unhappy content etc.   each conversions costs 2 luxuries. */
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
**************************************************************************/
void citizen_happy_units(struct city *pcity, int unhap)
{
  if (city_got_effect(pcity, B_POLICE)) {
    if (get_government(pcity->owner)==G_DEMOCRACY)
      unhap-=2;
    else
      unhap--;
  }
  if (unhap>0 && (pcity->ppl_happy[3] + pcity->ppl_content[3])) { 
    while (unhap> 0  && pcity->ppl_content[3]) {
      pcity->ppl_content[3]--;
      pcity->ppl_unhappy[3]++;
      unhap--;
    }
    while (unhap > 2 && pcity->ppl_happy[3]) {
      pcity->ppl_happy[3]--;
      pcity->ppl_unhappy[3]++;
      unhap -= 2;
    } 
    if (unhap > 0 && pcity->ppl_happy[3]) {
      pcity->ppl_happy[3]--;
      pcity->ppl_content[3]++;
      unhap--;
    } 
  }
    /* MAKE VERY UNHAPPY CITIZENS WITH THE REST, but that is not documented */

}

/**************************************************************************
...
**************************************************************************/
void citizen_happy_buildings(struct city *pcity)
{
  int faces=0;
  happy_copy(pcity, 1);
  
  if (city_got_building(pcity,B_TEMPLE)) { 
    faces+=get_temple_power(pcity);
  }
  if (city_got_building(pcity,B_COURTHOUSE) &&
      get_government(pcity->owner) == G_DEMOCRACY ) {
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
void citizen_happy_wonders(struct city *pcity)
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
void unhappy_city_check(struct city *pcity)
{
  if (city_unhappy(pcity)) {
    pcity->food_surplus=min(0, pcity->food_surplus);
    pcity->tax_total=0;
    pcity->science_total=0;
    pcity->shield_surplus=min(0, pcity->shield_surplus);
  }  
}

/**************************************************************************
...
**************************************************************************/
void set_pollution(struct city *pcity)
{
  int mod=0;
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
    if (get_invention(pplayer, A_INDUSTRIALIZATION)==TECH_KNOWN)  mod=1;
    if (get_invention(pplayer, A_AUTOMOBILE)==TECH_KNOWN) mod=2;
    if (get_invention(pplayer, A_MASS)==TECH_KNOWN) mod=3;
    if (get_invention(pplayer, A_PLASTICS)==TECH_KNOWN) mod=4;
    poppul=(pcity->size*mod)/4;
    pcity->pollution+=poppul;
  }

  pcity->pollution=max(0, pcity->pollution-20);  
}

/**************************************************************************
...
**************************************************************************/
void set_food_trade_shields(struct city *pcity)
{
  int x,y;
  pcity->food_prod=0;
  pcity->shield_prod=0;
  pcity->trade_prod=0;

  pcity->food_surplus=0;
  pcity->shield_surplus=0;
  pcity->corruption=0;

  city_map_iterate(x, y) {
    if(get_worker_city(pcity, x, y)==C_TILE_WORKER) {
      pcity->food_prod+=get_food_tile(x, y, pcity);
      pcity->shield_prod+=get_shields_tile(x, y, pcity);
      pcity->trade_prod+=get_trade_tile(x, y, pcity);
    }
  }
  pcity->tile_trade=pcity->trade_prod;
  
  pcity->food_surplus=pcity->food_prod-pcity->size*2;
  set_trade_prod(pcity);
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

void global_city_refresh(struct player *pplayer)
{ /* called on government change or wonder completion or stuff like that -- Syela */
  connection_do_buffer(pplayer->conn);
  city_list_iterate(pplayer->cities, pcity)
    city_refresh(pcity);
    send_city_info(pplayer, pcity, 1);
  city_list_iterate_end;
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
...
**************************************************************************/
void city_settlersupport(struct city *pcity)
{
  unit_list_iterate(pcity->units_supported, punit) {
    if (unit_flag(punit->type, F_SETTLERS)) {
      pcity->food_surplus--;
      punit->upkeep=1;
      if (get_government(pcity->owner)>=G_COMMUNISM) {
	pcity->food_surplus--;
	punit->upkeep=2;
      }
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
An "aggressive" unit is a unit which may cause unhappiness
under a Republic or Democracy.
A unit is *not* aggressive if one or more of following is true:
- zero attack strength
- inside a city
- inside a fortress within 3 squares of a friendly city (new)
**************************************************************************/
int unit_being_aggressive(struct unit *punit)
{
  if (get_unit_type(punit->type)->attack_strength==0)
    return 0;
  if (map_get_city(punit->x,punit->y))
    return 0;
  if (map_get_special(punit->x,punit->y)&S_FORTRESS) {
    city_list_iterate(get_player(punit->owner)->cities, pcity) {
      if (real_map_distance(punit->x, punit->y, pcity->x, pcity->y)<=3)
	return 0;
    }
    city_list_iterate_end;
  }
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void city_support(struct city *pcity)
{ 
  int milunits=0;
  int city_units=0;
  int unhap=0;
  int gov=get_government(pcity->owner);
  happy_copy(pcity, 2);
  city_settlersupport(pcity);
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, this_unit) {
    if (is_military_unit(this_unit))
      city_units++;
  }
  unit_list_iterate_end;
  unit_list_iterate(pcity->units_supported, this_unit) {
    this_unit->unhappiness=0;
    if (!unit_flag(this_unit->type, F_SETTLERS))
      this_unit->upkeep=0;
    if (is_military_unit(this_unit)) {
      milunits++;
      switch (gov) {
      case G_ANARCHY:
      case G_DESPOTISM:
	if (milunits>pcity->size) {
	  pcity->shield_surplus--;
	  this_unit->upkeep=1;
	} 
	break;
      case G_MONARCHY:
  	if (milunits>3) {
 	  pcity->shield_surplus--;
	  this_unit->upkeep=1;
	} 
	break;
      case G_COMMUNISM:
	if (milunits>3) {
	  pcity->shield_surplus--;
	  this_unit->upkeep=1;
	} 
	break;
      case G_REPUBLIC:
	pcity->shield_surplus--;
	this_unit->upkeep=1;
	if (unit_being_aggressive(this_unit)) {
	  if (unhap)
	    this_unit->unhappiness=1;
	  unhap++;
	} else if (is_field_unit(this_unit)) {
	  if (unhap)
	    this_unit->unhappiness=1;
	  unhap++;
	}
	break;
      case G_DEMOCRACY:
	pcity->shield_surplus--;
	this_unit->upkeep=1;
	if (unit_being_aggressive(this_unit)) {
	  unhap+=2;
	  this_unit->unhappiness=2;
	} else if (is_field_unit(this_unit)) {
	  this_unit->unhappiness=1;
	  unhap+=1;
	}
	break;
      default:
	break;
      }
    } 
  }
  unit_list_iterate_end;
  
  switch (gov) {
  case G_ANARCHY:
  case G_DESPOTISM:
    city_units = min(city_units, pcity->ppl_unhappy[3]);
    pcity->ppl_unhappy[3]-= city_units;
    pcity->ppl_content[3]+= city_units;
    break;
  case G_MONARCHY:
    city_units = min(3, city_units);
    city_units = min(pcity->ppl_unhappy[3], city_units);
    pcity->ppl_unhappy[3]-= city_units;
    pcity->ppl_content[3]+= city_units;
    break;
  case G_COMMUNISM:
    city_units = min(3, city_units);
    city_units = min(pcity->ppl_unhappy[3], city_units*2);
    pcity->ppl_unhappy[3]-= city_units;
    pcity->ppl_content[3]+= city_units;
    break;
  case G_REPUBLIC:
    unhap--;
  case G_DEMOCRACY:
    citizen_happy_units(pcity, unhap);
  }

}


/**************************************************************************
...
**************************************************************************/
void remove_obsolete_buildings(struct player *pplayer)
{
  int i;
  city_list_iterate(pplayer->cities, pcity) {
    for (i=0;i<B_LAST;i++) {
      if(city_got_building(pcity, i) 
	 && !is_wonder(i) 
	 && improvement_obsolete(pplayer, i)) {
	do_sell_building(pplayer, pcity, i);
	notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_SOLD, 
			 "Game: %s is selling %s (obsolete) for %d", 
			 pcity->name, get_improvement_name(i), 
			 improvement_value(i)/2);
      }
    }
  }
  city_list_iterate_end;
}

void worker_loop(struct city *pcity, int *foodneed, int *prodneed, int *workers)
{
  int x, y, bx, by, best, cur;
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

/*printf("%s, %d workers, %d luxneed, %d e\n", pcity->name, *workers, luxneed, e);*/

  if (city_happy(pcity) && wants_to_be_bigger(pcity) && pcity->size > 4) *foodneed += 1;

  city_map_iterate(x, y) {
    conflict[x][y] = -1 - minimap[map_adjust_x(pcity->x+x-2)][map_adjust_y(pcity->y+y-2)];
  } /* better than nothing, not as good as a global worker allocation -- Syela */

  do {
    bx=0;
    by=0;
    best = 0;
    city_map_iterate(x, y) {
      if(can_place_worker_here(pcity, x, y)) {
        cur = city_tile_value(pcity, x, y, *foodneed, *prodneed) - conflict[x][y];
        if (cur > best) {
          bx=x;
          by=y;
          best = cur;
	}
      }
    }
    if(bx || by) {
      set_worker_city(pcity, bx, by, C_TILE_WORKER);
      (*workers)--; /* amazing what this did with no parens! -- Syela */
      *foodneed -= get_food_tile(bx,by,pcity) - 2;
      *prodneed -= get_shields_tile(bx,by,pcity) - 1;
    }
  } while(*workers && (bx || by));
  *foodneed += 2 * (*workers - 1 - e);
  *prodneed += (*workers - 1 - e);
  if (*prodneed > 0) printf("Ignored prodneed? in %s (%d)\n", pcity->name, *prodneed);
}

/**************************************************************************
...
**************************************************************************/
int  add_adjust_workers(struct city *pcity)
{
  int workers=pcity->size;
  int iswork=0;
  int toplace;
  int foodneed;
  int prodneed = 0;
  int x,y;
  city_map_iterate(x, y) 
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER) 
      iswork++;
  
  iswork--;
  if (iswork+city_specialists(pcity)>workers)
    return 0;
  if (iswork+city_specialists(pcity)==workers)
    return 1;
  toplace=workers-(iswork+city_specialists(pcity));
  foodneed = -pcity->food_surplus;
  prodneed = -pcity->shield_surplus;

  worker_loop(pcity, &foodneed, &prodneed, &toplace);

  pcity->ppl_elvis+=toplace;
  return 1;
}

/**************************************************************************
...
**************************************************************************/

void auto_arrange_workers(struct city *pcity)
{
  int workers=pcity->size;
  int taxwanted,sciwanted;
  int x,y;
  int foodneed, prodneed, gov;

  city_map_iterate(x, y)
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER) 
      set_worker_city(pcity, x, y, C_TILE_EMPTY);
  
  set_worker_city(pcity, 2, 2, C_TILE_WORKER); 
  foodneed=(pcity->size *2 -get_food_tile(2,2, pcity)) + settler_eats(pcity);
  prodneed = 0;
  prodneed -= get_shields_tile(2,2,pcity);
  unit_list_iterate(pcity->units_supported, punit)
    if (is_military_unit(punit)) prodneed++;
  unit_list_iterate_end;
  gov = get_government(pcity->owner);
  if (gov == G_DESPOTISM) prodneed -= pcity->size;
  if (gov == G_MONARCHY || gov == G_COMMUNISM) prodneed -= 3;
  
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
  send_city_info(city_owner(pcity), pcity, 1);
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
    notify_player_ex(pplayer, 0, 0, E_LOW_ON_FUNDS, "Game: WARNING, we're LOW on FUNDS sire.");  
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
      return;
    } else if(pcity->ppl_scientist) {
      pcity->ppl_scientist--;
      return;
    } else if(pcity->ppl_elvis) {
      pcity->ppl_elvis--; 
      return;
    }
  } 
  auto_arrange_workers(pcity);
  city_refresh(pcity);
  send_city_info(&game.players[pcity->owner], pcity, 0);
}

/**************************************************************************
...
**************************************************************************/
void city_increase_size(struct city *pcity)
{
  int has_granary = city_got_effect(pcity, B_GRANARY);
  
  if (!city_got_building(pcity, B_AQUEDUCT)
      && pcity->size>=game.aqueduct_size) {/* need aqueduct */
    notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_CITY_AQUEDUCT,
	  "Game: %s needs Aqueducts to grow any further", pcity->name);
    /* Granary can only hold so much */
    pcity->food_stock = (pcity->size * game.foodbox *
			 (100 - game.aqueductloss/(1+has_granary))) / 100;
    return;
  }

  if (!city_got_building(pcity, B_SEWER)
      && pcity->size>=game.sewer_size) {/* need sewer */
    notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_CITY_AQUEDUCT,
      "Game: %s needs Sewer system to grow any further", pcity->name);
    /* Granary can only hold so much */
    pcity->food_stock = (pcity->size * game.foodbox *
			 (100 - game.aqueductloss/(1+has_granary))) / 100; 
    return;
  }

  if (has_granary)
    pcity->food_stock = (pcity->size + 1) * (game.foodbox / 2);
  else
    pcity->food_stock = 0;
  
  pcity->size++;
  if (!add_adjust_workers(pcity))
    auto_arrange_workers(pcity);

  city_refresh(pcity);

  if (game.players[pcity->owner].ai.control) /* don't know if we need this -- Syela */
    if (ai_fix_unhappy(pcity))
      ai_scientists_taxmen(pcity);

  connection_do_buffer(city_owner(pcity)->conn);
  send_city_info(&game.players[pcity->owner], pcity, 0);
  notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_CITY_GROWTH,
                   "Game: %s grows to size %d", pcity->name, pcity->size);
  connection_do_unbuffer(city_owner(pcity)->conn);
}

/**************************************************************************
...
**************************************************************************/
void city_reduce_size(struct city *pcity)
{
  pcity->size--;
  notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_CITY_FAMINE,
		   "Game: Famine feared in %s", pcity->name);
  if (city_got_effect(pcity, B_GRANARY))
    pcity->food_stock=(pcity->size+1)*(game.foodbox/2);
  else
    pcity->food_stock=0;

  city_auto_remove_worker(pcity);
}
 
/**************************************************************************
...
**************************************************************************/
void city_populate(struct city *pcity)
{
  pcity->food_stock+=pcity->food_surplus;
  if(pcity->food_stock >= pcity->size*game.foodbox) 
    city_increase_size(pcity);
  else if(pcity->food_stock<0) {
    unit_list_iterate(pcity->units_supported, punit) {
      if (unit_flag(punit->type, F_SETTLERS)) {
	wipe_unit(0, punit);
	notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_UNIT_LOST, "Game: Famine feared in %s, Settlers dies!", 
			 pcity->name);
	if (city_got_effect(pcity, B_GRANARY))
	  pcity->food_stock=(pcity->size+1)*(game.foodbox/2);
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
int advisor_choose_build(struct city *pcity)
{ /* Old stuff that I obsoleted deleted. -- Syela */
  struct ai_choice choice;
  int i;
  int id=-1;
  int want=0;

  if (!game.players[pcity->owner].ai.control)
    ai_eval_buildings(pcity); /* so that ai_advisor is smart even for humans */
  ai_advisor_choose_building(pcity, &choice); /* much smarter version -- Syela */
/*printf("Advisor_choose_build got %d/%d from ai_advisor_choose_building.\n", 
  choice.choice, choice.want);*/
  id = choice.choice;
  want = choice.want;

  if (id!=-1 && id != B_LAST && want > 0) {
    if(is_wonder(id)) {
      notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STARTED,
		    "Game: The %s have started building The %s in %s.",
		    get_race_name_plural(city_owner(pcity)->race),
		    get_imp_name_ex(pcity, id), pcity->name);
    }
    pcity->currently_building=id;
    pcity->is_building_unit=0;
    return 1; /* making something.  return value = 1 */
  }

  for (i=0;i<B_LAST;i++)
    if(can_build_improvement(pcity, i) && i != B_PALACE) { /* build something random, undecided */
      pcity->currently_building=i;
      pcity->is_building_unit=0;
      return 0;
    }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void obsolete_building_test(struct city *pcity, int b1, int b2)
{ 
  if (pcity->currently_building==b1 && 
      can_build_improvement(pcity, b2))
    pcity->currently_building=b2;
}

/**************************************************************************
...
**************************************************************************/
void upgrade_building_prod(struct city *pcity)
{
  obsolete_building_test(pcity, B_BARRACKS,B_BARRACKS3);
  obsolete_building_test(pcity, B_BARRACKS,B_BARRACKS2);
  obsolete_building_test(pcity, B_BARRACKS2,B_BARRACKS3);
}

/**************************************************************************
...
**************************************************************************/
void upgrade_unit_prod(struct city *pcity)
{
  struct player *pplayer=&game.players[pcity->owner];
  int id = pcity->currently_building;
  int id2= unit_types[id].obsoleted_by;
  if (can_build_unit_direct(pcity, id2)) {
    pcity->currently_building=id2;
    notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_UPGRADED, 
		  "Game: production of %s is upgraded to %s in %s",
		  get_unit_type(id)->name, 
		  get_unit_type(id2)->name , 
		  pcity->name);
  }
}

/**************************************************************************
...
**************************************************************************/
void city_build_stuff(struct player *pplayer, struct city *pcity)
{
  if (pcity->shield_surplus<0) {
    unit_list_iterate(pcity->units_supported, punit) {
      if (is_military_unit(punit)) {
	notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_LOST,
			 "Game: %s can't upkeep %s, unit disbanded",
			 pcity->name, get_unit_type(punit->type)->name);
	 wipe_unit(pplayer, punit);
	 break;
      }
    }
    unit_list_iterate_end;
  }
  
  if(pcity->shield_surplus<=0 && !city_unhappy(pcity)) 
    pcity->shield_surplus=1;
  pcity->shield_stock+=pcity->shield_surplus;
  if (!pcity->is_building_unit) {
    if (pcity->currently_building==B_CAPITAL) {
      pplayer->economic.gold+=pcity->shield_surplus;
      pcity->shield_stock=0;
    }    
    upgrade_building_prod(pcity);
    if (!can_build_improvement(pcity, pcity->currently_building)) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_CANTBUILD,
		    "Game: %s is building %s, which is no longer available",
	pcity->name,get_imp_name_ex(pcity, pcity->currently_building));
      return;
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
      

      pcity->improvements[pcity->currently_building]=1;
      pcity->shield_stock-=improvement_value(pcity->currently_building); 
      /* to eliminate micromanagement */
      if(is_wonder(pcity->currently_building)) {
	game.global_wonders[pcity->currently_building]=pcity->id;
	notify_player_ex(0, pcity->x, pcity->y, E_WONDER_BUILD,
		      "Game: The %s have finished building %s in %s.",
		      get_race_name_plural(pplayer->race),
		      get_imp_name_ex(pcity, pcity->currently_building),
		      pcity->name);
      }
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_BUILD,
		    "Game: %s has finished building %s", pcity->name, 
		    improvement_types[pcity->currently_building].name
		    );

      if (pcity->currently_building==B_DARWIN) {
	notify_player(pplayer, 
		      "Game: Darwin's Voyage boost research, you gain 2 immediate advances.");
	update_tech(pplayer, 1000000); 
	update_tech(pplayer, 1000000); 
      }
      city_refresh(pcity);
/* printf("Trying advisor_choose_build.\n"); */
      advisor_choose_build(pcity);
/* printf("Advisor_choose_build didn't kill us.\n"); */
      notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_AUTO,
		    "Game: %s is now building %s", pcity->name, 
		    improvement_types[pcity->currently_building].name
		    );
    } 
  } else {
    upgrade_unit_prod(pcity);
    if(pcity->shield_stock>=unit_value(pcity->currently_building)) {
      if (unit_flag(pcity->currently_building, F_SETTLERS)) {
	if (pcity->size==1) {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			"Game: %s can't build settler yet", pcity->name);
	  return;
	}
	pcity->size--;
	city_auto_remove_worker(pcity);
      }

      create_unit(pplayer, pcity->x, pcity->y, pcity->currently_building,
		  do_make_unit_veteran(pcity, pcity->currently_building), 
		  pcity->id, -1);
      /* to eliminate micromanagement, we only subtract the unit's cost */
      pcity->shield_stock-=unit_value(pcity->currently_building); 
      notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_BUILD,
		       "Game: %s is finished building %s", 
		       pcity->name, 
		       unit_types[pcity->currently_building].name);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void pay_for_buildings(struct player *pplayer, struct city *pcity)
{
  int i;
  for (i=0;i<B_LAST;i++) 
    if (city_got_building(pcity, i)) {
      if (is_wonder(i)) {
	if (wonder_obsolete(i)) {
	  switch (improvement_types[i].shield_upkeep) {
	  case 1:
	    pplayer->economic.gold+=3;
	    break;
	  case 2:
	    update_tech(pplayer, 3);
	    break;
	  }
	}
      } else if( pplayer->government != G_ANARCHY ){
	if (pplayer->economic.gold-improvement_upkeep(pcity, i)<0) {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_AUCTIONED,
			   "Game: Can't afford to maintain %s in %s, building sold!", 
			   improvement_types[i].name, pcity->name);
	  do_sell_building(pplayer, pcity, i);
	  city_refresh(pcity);
	} else
	  pplayer->economic.gold-=improvement_upkeep(pcity, i);
      }
    }
}

/**************************************************************************
1) check for enemy units on citymap tiles
2) withdraw workers from such tiles
3) mark citymap tiles accordingly empty/unavailable  
**************************************************************************/
void city_check_workers(struct player *pplayer, struct city *pcity)
{
  int x, y;
  
  city_map_iterate(x, y) {
    struct tile *ptile=map_get_tile(pcity->x+x-2, pcity->y+y-2);
    
    if(unit_list_size(&ptile->units)>0) {
      struct unit *punit=unit_list_get(&ptile->units, 0);
      if(pplayer->player_no!=punit->owner) {
	if(get_worker_city(pcity, x, y)==C_TILE_WORKER) {
/*	  pcity->ppl_elvis++; -- this is really not polite -- Syela */
  	  set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
          add_adjust_workers(pcity); /* will place the displaced */
          city_refresh(pcity); /* may be unnecessary; can't hurt */
        } else	set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
	continue;
      }
    }
    if(get_worker_city(pcity, x, y)==C_TILE_UNAVAILABLE)
      set_worker_city(pcity, x, y, C_TILE_EMPTY);
    if(get_worker_city(pcity, x, y)!=C_TILE_WORKER &&
       !can_place_worker_here(pcity, x, y))
      set_worker_city(pcity, x, y, C_TILE_UNAVAILABLE);
  }
}

/**************************************************************************
 Add some Pollution if we have waste
**************************************************************************/
void check_pollution(struct city *pcity)
{
  int x,y;
  int k=100;
  if (pcity->pollution && myrand(100)<=pcity->pollution) {
    while (k) {
      x=pcity->x+myrand(5)-2;
      y=pcity->y+myrand(5)-2;
      x=map_adjust_x(x); y=map_adjust_y(y);
      if ( (x!=pcity->x || y!=pcity->x) && 
	   (map_get_terrain(x,y)!=T_OCEAN && map_get_terrain(x,y)<=T_TUNDRA) &&
	   (!(map_get_special(x,y)&S_POLLUTION)) ) { 
	map_set_special(x,y, S_POLLUTION);
	send_tile_info(0, x, y, TILE_KNOWN);
	notify_player_ex(city_owner(pcity), pcity->x, pcity->y, E_POLLUTION,
			 "Game: Pollution near %s", pcity->name);
	return;
      }
      k--;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void sanity_check_city(struct city *pcity)
{
  int size=pcity->size;
  int x,y;
  int iswork=0;
  city_map_iterate(x, y) {
    if (get_worker_city(pcity, x, y)==C_TILE_WORKER) 
	iswork++;
  }
  iswork--;
  if (iswork+city_specialists(pcity)!=size) {
    printf("%s is bugged: size:%d workers:%d elvis: %d tax:%d sci:%d\n", pcity->name,size,iswork,  pcity->ppl_elvis, pcity->ppl_taxman, pcity->ppl_scientist); 
    auto_arrange_workers(pcity);
  }
}

/**************************************************************************
...
**************************************************************************/
void city_incite_cost(struct city *pcity)
{

  struct city *capital;
  int dist;
  
  if (city_got_building(pcity, B_PALACE)) 
    pcity->incite_revolt_cost=1000000;
  else {
    pcity->incite_revolt_cost=city_owner(pcity)->economic.gold +1000;
    capital=find_palace(city_owner(pcity));
    if (capital)
      dist=min(32, map_distance(capital->x, capital->y, pcity->x, pcity->y)); 
    else 
      dist=32;
    if (city_got_building(pcity, B_COURTHOUSE)) 
      dist/=2;
    if (get_government(city_owner(pcity)->player_no)==G_COMMUNISM)
      dist = min(10, dist);
    pcity->incite_revolt_cost/=(dist + 3);
    pcity->incite_revolt_cost*=pcity->size;
    if (city_unhappy(pcity))
      pcity->incite_revolt_cost/=2;
    if (unit_list_size(&map_get_tile(pcity->x,pcity->y)->units)==0)
      pcity->incite_revolt_cost/=2;
  }
}

/**************************************************************************
 called every turn, for every city. 
**************************************************************************/
int update_city_activity(struct player *pplayer, struct city *pcity)
{
  int got_tech = 0;
  city_check_workers(pplayer, pcity);
  if (city_refresh(pcity) && 
      get_government(pcity->owner)>=G_REPUBLIC &&
      pcity->food_surplus>0 && pcity->size>4) {
    pcity->food_stock=pcity->size*game.foodbox+1; 
  }

/* the AI often has widespread disorder when the Gardens or Oracle
become obsolete.  This is a quick hack to prevent this.  980805 -- Syela */
  while (pplayer->ai.control && city_unhappy(pcity)) {
    if (!ai_make_elvis(pcity)) break;
  } /* putting this lower in the routine would basically be cheating. -- Syela */

  city_build_stuff(pplayer, pcity);
  if (!pcity->was_happy && city_happy(pcity) && pcity->size>4) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_LOVE,
		  "Game: We Love The %s Day celebrated in %s", 
		  get_ruler_title(pplayer->government),
		  pcity->name);
  }
  if (!city_happy(pcity) && pcity->was_happy && pcity->size>4) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_NORMAL,
		  "Game: We Love The %s Day canceled in %s",
		  get_ruler_title(pplayer->government),
		  pcity->name);

  }
  pcity->was_happy=city_happy(pcity);
    {
      int id=pcity->id;
      city_populate(pcity);
      if(!city_list_find_id(&pplayer->cities, id))
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
		       "Game: Civil disorder in %s", pcity->name);
    else
      notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_DISORDER,
		       "Game: CIVIL DISORDER CONTINUES in %s.", pcity->name);
  }
  else {
    if (pcity->anarchy)
      notify_player_ex(pplayer, pcity->x, pcity->y, E_CITY_NORMAL,
		       "Game: Order restored in %s", pcity->name);
    pcity->anarchy=0;
  }
  check_pollution(pcity);
  city_incite_cost(pcity);

  send_city_info(0, pcity, 0);
  if (pcity->anarchy>2 && get_government(pplayer->player_no)==G_DEMOCRACY) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_ANARCHY,
		     "Game: The people have overthrown your democracy, your country is in turmoil");
    handle_player_revolution(pplayer);
  }
  sanity_check_city(pcity);
  return got_tech;
}

