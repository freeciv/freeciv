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
#include <unithand.h>
#include <civserver.h>
#include <map.h>
#include <maphand.h>
#include <mapgen.h>
#include <cityhand.h>
#include <cityturn.h>
#include <citytools.h>
#include <unit.h>
#include <city.h>
#include <player.h>
#include <tech.h>
#include <shared.h>
#include <plrhand.h>
#include <events.h>
#include <aicity.h>

/****************************************************************
...
*****************************************************************/

int city_got_barracks(struct city *pcity)
{
  return (city_affected_by_wonder(pcity, B_SUNTZU) ||
	  city_got_building(pcity, B_BARRACKS) || 
	  city_got_building(pcity, B_BARRACKS2) ||
	  city_got_building(pcity, B_BARRACKS3));
}

/**************************************************************************
...
**************************************************************************/

int can_sell_building(struct city *pcity, int id)
{

  return (city_got_building(pcity, id) ? (!is_wonder(id)) : 0);
}

/**************************************************************************
...
**************************************************************************/

enum city_tile_type get_worker_city(struct city *pcity, int x, int y)
{
  if ((x==0 || x==4) && (y == 0 || y == 4)) 
    return C_TILE_UNAVAILABLE;
  return(pcity->city_map[x][y]);
}

/**************************************************************************
...
**************************************************************************/
int is_worker_here(struct city *pcity, int x, int y) 
{
  if (x < 0 || x > 4 || y < 0 || y > 4 || ((x == 0 || x == 4) && (y == 0|| y==4))) {
    return 0;
  }
  return (get_worker_city(pcity,x,y)==C_TILE_WORKER); 
}

/**************************************************************************
...
**************************************************************************/

struct city *find_city_wonder(enum improvement_type_id id)
{
  return (find_city_by_id(game.global_wonders[id]));
}

/**************************************************************************
Locate the city where the players palace is located, (NULL Otherwise) 
**************************************************************************/

struct city *find_palace(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) 
    if (city_got_building(pcity, B_PALACE)) 
      return pcity;
  city_list_iterate_end;
  return NULL;
}


/**************************************************************************
...
**************************************************************************/

int city_specialists(struct city *pcity)
{
  return (pcity->ppl_elvis+pcity->ppl_scientist+pcity->ppl_taxman);
}

/**************************************************************************
... v 1.0j code was too rash, only first penalty now!
**************************************************************************/
int content_citizens(struct player *pplayer) 
{
  int cities =  city_list_size(&pplayer->cities);
  int content = game.unhappysize;
  int basis   = game.cityfactor - (G_DEMOCRACY - pplayer->government);

  if (cities > basis) 
    content--;
  return content;
}

/**************************************************************************
...
**************************************************************************/

int get_temple_power(struct city *pcity)
{
  struct player *p=&game.players[pcity->owner];
  int power=1;
  if (get_invention(p, A_MYSTICISM)==TECH_KNOWN)
    power=2;
  if (city_affected_by_wonder(pcity, B_ORACLE)) 
    power*=2;
  return power;
}

/**************************************************************************
...
**************************************************************************/

int get_cathedral_power(struct city *pcity)
{
  struct player *p=&game.players[pcity->owner];
  int power = 3;
  if (get_invention(p, A_COMMUNISM))
   power--;
  if (get_invention(p, A_THEOLOGY))
   power++;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int get_colosseum_power(struct city *pcity)
{
  int power = 3;
  struct player *p=&game.players[pcity->owner];
  if (get_invention(p, A_ELECTRICITY))
   power++;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int is_worked_here(int x, int y)
{
  struct player *pplayer;
  int my, i;
  int xx;

  x = map_adjust_x(x);
  for(i=0; i<game.nplayers; i++) {
    pplayer=&game.players[i];
    city_list_iterate(pplayer->cities, pcity) {

      my=y+2-pcity->y;
      if (0 <= my && my <5)
	for ( xx = 0; xx<5; xx++) 
	  if (map_adjust_x(pcity->x+xx-2) == x) 
	    if(get_worker_city(pcity, xx, my)==C_TILE_WORKER) return 1;
    }
    city_list_iterate_end;
  }
  return 0;
}

/**************************************************************************
x and y are city cords in the range [0;4]
**************************************************************************/
int can_place_worker_here(struct city *pcity, int x, int y)
{
  if ((x == 0 || x == 4) && (y == 0 || y == 4))
    return 0;
  if (x < 0  || x > 4 || y < 0 || y > 4)
    return 0;
  if (get_worker_city(pcity, x, y) != C_TILE_EMPTY)
    return 0;
  return  (map_get_known(pcity->x+x-2, pcity->y+y-2, city_owner(pcity))
    && !is_worked_here(map_adjust_x(pcity->x+x-2), pcity->y+y-2));
}

int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed)
{ /* by Syela, unifies best_tile, best_food_tile, worst_elvis_tile */
  int a, f, s;
  int i, j, k;
  int shield_weighting[3] = { 11, 13, 15 };
  int food_weighting[3] = { 15, 14, 13 };
  struct player *plr;

  plr = city_owner(pcity);

  a = get_race(plr)->attack;
  s = pcity->size;

  i = food_weighting[a];
  if (foodneed > 0) i *= 10; /* all else is secondary until we are fed */
  else { i *= 4; i /= s; }
  i *= get_food_tile(x, y, pcity);
  
  j = get_shields_tile(x, y, pcity) * shield_weighting[a] *
      city_shield_bonus(pcity) / 100;
  if (prodneed > 0) j *= 10; /* trying to avoid non-support of units */
  k = get_trade_tile(x, y, pcity) * (8 - city_corruption(pcity, 8)) *
      (city_tax_bonus(pcity) * plr->economic.tax +
       city_science_bonus(pcity) * plr->economic.science +
       100 * plr->economic.luxury) / 100 / 100;
  return(i + j + k);
}  
  
int better_tile(struct city *pcity, int x, int y, int bx, int by, int foodneed, int prodneed)
{
  return (city_tile_value(pcity, x, y, foodneed, prodneed) >
          city_tile_value(pcity, bx, by, foodneed, prodneed));
}

/**************************************************************************
...
**************************************************************************/
int best_tile(struct city *pcity, int x, int y, int bx, int by)
{ /* obsoleted but not deleted by Syela */
  return (4*get_food_tile(x, y, pcity) +
          12*get_shields_tile(x, y, pcity) +
          8*get_trade_tile(x, y, pcity) >
          4*get_food_tile(bx, by, pcity) +
          12*get_shields_tile(bx, by, pcity) +
          8*get_trade_tile(bx, by, pcity) 
          );
}

/**************************************************************************
...
**************************************************************************/
int best_food_tile(struct city *pcity, int x, int y, int bx, int by)
{ /* obsoleted but not deleted by Syela */
  return (100*get_food_tile(x, y, pcity) +
          12*get_shields_tile(x, y, pcity) +
          8*get_trade_tile(x, y, pcity) >
          100*get_food_tile(bx, by, pcity) +
          12*get_shields_tile(bx, by, pcity) +
          8*get_trade_tile(bx, by, pcity) 
          );
}

/**************************************************************************
...
**************************************************************************/
int settler_eats(struct city *pcity)
{
  int eat=0;
  unit_list_iterate(pcity->units_supported, this_unit) {
    if (unit_flag(this_unit->type, F_SETTLERS)) {
      eat++;
      if (get_government(pcity->owner)>=G_COMMUNISM) {
        eat++;
      }
    }
  }
  unit_list_iterate_end;
  return eat;
}

/**************************************************************************
...
**************************************************************************/
int is_building_other_wonder(struct city *pc)
{
  struct player *pplayer = city_owner(pc);
  city_list_iterate(pplayer->cities, pcity) 
    if ((pc != pcity) && !(pcity->is_building_unit) && is_wonder(pcity->currently_building) && map_get_continent(pcity->x, pcity->y) == map_get_continent(pc->x, pc->y))
      return pcity->currently_building; /* why return 1? -- Syela */
  city_list_iterate_end;
  return 0;
}

/**************************************************************************
...
**************************************************************************/
int built_elsewhere(struct city *pc, int wonder)
{
  struct player *pplayer = city_owner(pc);
  city_list_iterate(pplayer->cities, pcity) 
    if ((pc != pcity) && !(pcity->is_building_unit) && pcity->currently_building == wonder)
      return 1;
  city_list_iterate_end;
  return 0;
}


/**************************************************************************
...
**************************************************************************/
void eval_buildings(struct city *pcity,int *values)
{
  int i, gov, tech;
  struct player *plr = city_owner(pcity);
  gov = get_government(pcity->owner);
  tech = (plr->research.researching != A_NONE);
    
  for (i=0;i<B_LAST;i++) {
    if (is_wonder(i) && can_build_improvement(pcity, i) && !built_elsewhere(pcity, i)) {
      if (wonder_obsolete(i))
	values[i]=1;
      else
	values[i]=99;
    } else
    values[i]=0;
  }
  
  if (gov <= G_COMMUNISM || pcity->size < 5) {
    if (can_build_improvement(pcity, B_GRANARY)) 
      values[B_GRANARY]=pcity->food_surplus*50;
  }
  if (can_build_improvement(pcity, B_SUPERMARKET))
    values[B_SUPERMARKET]=pcity->size*55;

  if (can_build_improvement(pcity, B_AQUEDUCT) && pcity->size > 6)
    values[B_AQUEDUCT]=pcity->size*125+pcity->food_surplus*50;
  if (can_build_improvement(pcity, B_SEWER) && pcity->size > 11)
    values[B_SEWER]=pcity->size*100+pcity->food_surplus*50;
  
  if (can_build_improvement(pcity, B_HARBOUR) && (pcity->size > 5)) 
    values[B_HARBOUR]=pcity->size*60;
  if (can_build_improvement(pcity, B_OFFSHORE) && 
      !can_build_improvement(pcity, B_HARBOUR))
    values[B_OFFSHORE]=pcity->size*60;
  
  if (can_build_improvement(pcity, B_MARKETPLACE)) 
    values[B_MARKETPLACE]=pcity->trade_prod*200;
  
  if (pcity->trade_prod > 15) 
    if (can_build_improvement(pcity, B_BANK)) 
      values[B_BANK]=pcity->tax_total*100;

    if (can_build_improvement(pcity, B_STOCK)) 
      values[B_STOCK]=pcity->tax_total*100;
  
  if (gov > G_COMMUNISM)
    if (can_build_improvement(pcity, B_SUPERHIGHWAYS)) 
      values[B_SUPERHIGHWAYS]=pcity->trade_prod*60;
  if (can_build_improvement(pcity, B_COURTHOUSE)) {
    if (gov != G_COMMUNISM && gov != G_DEMOCRACY) 
      values[B_COURTHOUSE]=pcity->corruption*100;
    else 
      values[B_COURTHOUSE]=pcity->ppl_unhappy[4]*200+pcity->ppl_elvis*400;
  }
  if (tech) {
    if (can_build_improvement(pcity, B_LIBRARY)) 
      values[B_LIBRARY]=pcity->science_total*200;
    
    if (can_build_improvement(pcity, B_UNIVERSITY)) 
      values[B_UNIVERSITY]=pcity->science_total*101;
    
    if (can_build_improvement(pcity, B_RESEARCH)) 
      values[B_RESEARCH]=pcity->science_total*100;
  }
  if (can_build_improvement(pcity, B_AIRPORT))
    values[B_AIRPORT]=pcity->shield_prod*49;

  if (pcity->shield_prod >= 15)
    if (can_build_improvement(pcity, B_PORT))
      values[B_PORT]=pcity->shield_prod*48;

  if (pcity->shield_prod >= 5) {
    if (can_build_improvement(pcity, B_BARRACKS))
      values[B_BARRACKS]=pcity->shield_prod*50;

    if (can_build_improvement(pcity, B_BARRACKS2))
      values[B_BARRACKS2]=pcity->shield_prod*50;

    if (can_build_improvement(pcity, B_BARRACKS3))
      values[B_BARRACKS3]=pcity->shield_prod*50;
  }
  if (can_build_improvement(pcity, B_TEMPLE))
     values[B_TEMPLE]=pcity->ppl_unhappy[4]*200+pcity->ppl_elvis*600;

  if (can_build_improvement(pcity, B_COLOSSEUM))
    values[B_COLOSSEUM]=pcity->ppl_unhappy[4]*200+pcity->ppl_elvis*300;

  if (can_build_improvement(pcity, B_CATHEDRAL))
    values[B_CATHEDRAL]=pcity->ppl_unhappy[4]*201+pcity->ppl_elvis*300;

  if (!(tech && plr->economic.tax > 50)) {
    if (can_build_improvement(pcity, B_COASTAL))
      values[B_COASTAL]=pcity->size*36;
    if (can_build_improvement(pcity, B_CITY))
      values[B_CITY]=pcity->size*35;
  } 
  if (!(tech && plr->economic.tax > 40)) {
    if (can_build_improvement(pcity, B_SAM))
      values[B_SAM]=pcity->size*24;
    if (can_build_improvement(pcity, B_SDI))
      values[B_SDI]=pcity->size*25;
  }
  if (pcity->shield_prod >= 10)
    if (can_build_improvement(pcity, B_FACTORY)) 
      values[B_FACTORY]=pcity->shield_prod*125;

  if (city_got_building(pcity, B_FACTORY)) {
    
    if (can_build_improvement(pcity, B_HYDRO))
      values[B_HYDRO]=pcity->shield_prod*100+pcity->pollution*100;
    
    if (can_build_improvement(pcity, B_NUCLEAR))
      values[B_NUCLEAR]=pcity->shield_prod*101+pcity->pollution*100;
    
    if (can_build_improvement(pcity, B_POWER))
      values[B_POWER]=pcity->shield_prod*100;
  }

  if (can_build_improvement(pcity, B_MFG)) 
    values[B_MFG]=pcity->shield_prod*125;

  if (can_build_improvement(pcity, B_MASS)) 
    values[B_MASS]=pcity->pollution*(100+pcity->size);

  if (can_build_improvement(pcity, B_RECYCLING)) 
    values[B_RECYCLING]=pcity->pollution*(100+pcity->shield_prod);
    
  if (can_build_improvement(pcity, B_CAPITAL))
    values[B_CAPITAL]=pcity->shield_prod;
}

/**************************************************************************
...
**************************************************************************/
int do_make_unit_veteran(struct city *pcity, enum unit_type_id id)
{
  if (is_ground_unittype(id))
    return city_got_barracks(pcity);
  else if (is_water_unit(id))
    return (city_affected_by_wonder(pcity, B_LIGHTHOUSE) || city_got_building(pcity, B_PORT));
  else
    return city_got_building(pcity, B_AIRPORT);
  return 0;
}

/**************************************************************************
corruption, corruption is halfed during love the XXX days.
**************************************************************************/
int city_corruption(struct city *pcity, int trade)
{
  struct city *capital;
  int dist;
  int val;
  int corruption[]= { 12,8,20,24,20,0}; /* original {12,8,16,20,24,0} */
  if (get_government(pcity->owner)==G_DEMOCRACY) {
    return(0);
  }
  if (get_government(pcity->owner)==G_COMMUNISM) {
    dist=10;
  } else {
    capital=find_palace(city_owner(pcity));
    if (!capital)
      dist=36;
    else
      dist=min(36,map_distance(capital->x, capital->y, pcity->x, pcity->y));
  }
  if (get_government(pcity->owner) == G_DESPOTISM)
    dist = dist*2 + 3; /* yes, DESPOTISM is even worse than ANARCHY */
  
  val=(trade*dist*3)/(corruption[get_government(pcity->owner)]*10);

  if (city_got_building(pcity, B_COURTHOUSE) ||   
      city_got_building(pcity, B_PALACE))
    val /= 2;
  if (val >= trade && val)
    val = pcity->trade_prod - 1;
}
  

int city_shield_bonus(struct city *pcity)
{
  int tmp = 0;
  if (city_got_building(pcity, B_FACTORY)) {
    if (city_got_building(pcity, B_MFG))
      tmp = 100;
    else
      tmp = 50;
  }

  if (city_affected_by_wonder(pcity, B_HOOVER) ||
      city_got_building(pcity, B_POWER) ||
      city_got_building(pcity, B_HYDRO) ||
      city_got_building(pcity,B_NUCLEAR))
    tmp *= 1.5;

  return (tmp + 100);

}

/**************************************************************************
...
**************************************************************************/
int city_tax_bonus(struct city *pcity)
{
  int tax_bonus = 100;
  if (city_got_building(pcity, B_MARKETPLACE)) {
    tax_bonus+=50;
    if (city_got_building(pcity, B_BANK)) {
      tax_bonus+=50;
      if (city_got_building(pcity, B_STOCK)) 
	tax_bonus+=50;
    }
  }
  return tax_bonus;
}

/**************************************************************************
...
**************************************************************************/
int city_science_bonus(struct city *pcity)
{
  int science_bonus = 100;
  if (city_got_building(pcity, B_LIBRARY)) {
    science_bonus+=50;
    if (city_got_building(pcity, B_UNIVERSITY)) {
      science_bonus+=50;
    }
    if ((city_affected_by_wonder(pcity, B_SETI) || city_got_building(pcity, B_RESEARCH)))
      science_bonus+=50;
  }
  if (city_affected_by_wonder(pcity, B_COPERNICUS)) 
    science_bonus+=50;
  if (city_affected_by_wonder(pcity, B_ISAAC))
    science_bonus+=100;
  return science_bonus;
}

int wants_to_be_bigger(struct city *pcity)
{
  if (pcity->size < 8) return 1;
  if (city_got_building(pcity, B_SEWER)) return 1;
  if (city_got_building(pcity, B_AQUEDUCT) && pcity->size < 12) return 1;
  return 0;
}

