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

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "tech.h"
#include "unit.h"

#include "cityhand.h"
#include "cityturn.h"
#include "civserver.h"
#include "gamehand.h"		/* send_game_info */
#include "maphand.h"
#include "player.h"
#include "plrhand.h"
#include "settlers.h"
#include "stdinhand.h"		/* set_ai_level_direct */
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"	/* assess_danger_player() */

#include "citytools.h"

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

struct city *find_city_wonder(enum improvement_type_id id)
{
  return (find_city_by_id(game.global_wonders[id]));
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
  int basis   = game.cityfactor + get_gov_pplayer(pplayer)->empire_size_mod;
  int step    = get_gov_pplayer(pplayer)->empire_size_inc;

  if (cities > basis) {
    content--;
    if (step)
      content -= (cities - basis) / step;
  }
  return content;
}

/**************************************************************************
...
**************************************************************************/

int get_temple_power(struct city *pcity)
{
  struct player *p=&game.players[pcity->owner];
  int power=1;
  if (get_invention(p, game.rtech.temple_plus)==TECH_KNOWN)
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
  if (get_invention(p, game.rtech.cathedral_minus/*A_COMMUNISM*/) == TECH_KNOWN)
   power--;
  if (get_invention(p, game.rtech.cathedral_plus/*A_THEOLOGY*/) == TECH_KNOWN)
   power++;
  if (improvement_variant(B_MICHELANGELO)==1 && city_affected_by_wonder(pcity, B_MICHELANGELO))
   power*=2;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int get_colosseum_power(struct city *pcity)
{
  int power = 3;
  struct player *p=&game.players[pcity->owner];
  if (get_invention(p, game.rtech.colosseum_plus/*A_ELECTRICITY*/) == TECH_KNOWN)
   power++;
  return power;
}

/**************************************************************************
...
**************************************************************************/
int is_worked_here(int x, int y)
{
  return (map_get_tile(x, y)->worked != NULL); /* saves at least 10% of runtime CPU usage! */
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

int food_weighting(int n)
{
  static int value[56] = { -1, 57, 38, 25, 19, 15, 12, 10, 9, 8, 7,
                         6, 6, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3,
                         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
  return(value[n]);
}

int city_tile_value(struct city *pcity, int x, int y, int foodneed, int prodneed)
{ /* by Syela, unifies best_tile, best_food_tile, worst_elvis_tile */
  int i, j, k;
  struct player *plr;

  plr = city_owner(pcity);

  i = get_food_tile(x, y, pcity);
  if (foodneed > 0) i += 9 * (MIN(i, foodneed));
/* *= 10 led to stupidity with foodneed = 1, mine, and farmland -- Syela */
  i *= food_weighting(MAX(2,pcity->size));
  
  j = get_shields_tile(x, y, pcity);
  if (prodneed > 0) j += 9 * (MIN(j, prodneed));
  j *= SHIELD_WEIGHTING * city_shield_bonus(pcity);
  j /= 100;
  k = get_trade_tile(x, y, pcity) * pcity->ai.trade_want *
      (city_tax_bonus(pcity) * (plr->economic.tax + plr->economic.luxury) +
       city_science_bonus(pcity) * plr->economic.science) / 10000;
  return(i + j + k);
}  

int worst_worker_tile_value(struct city *pcity)
{
  int x, y;
  int worst = 0, tmp;
  city_map_iterate(x, y) {
    if ((x !=2 || y != 2) && get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      tmp = city_tile_value(pcity, x, y, 0, 0);
      if (tmp < worst || !worst) worst = tmp;
    }
  }
  return(worst);
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
  struct government *g = get_gov_pcity(pcity);
  int free_food   = citygov_free_food(pcity, g);
  int eat = 0;

  unit_list_iterate(pcity->units_supported, this_unit) {
    int food_cost = utype_food_cost(get_unit_type(this_unit->type), g);
    adjust_city_free_cost(&free_food, &food_cost);
    if (food_cost > 0) {
      eat += food_cost;
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
  int i, tech;
  struct player *plr = city_owner(pcity);
  struct government *g = get_gov_pcity(pcity);
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
  
  if (government_has_hint(g, G_FAVORS_GROWTH) || pcity->size < 5) {
    if (can_build_improvement(pcity, B_GRANARY)) 
      values[B_GRANARY]=pcity->food_surplus*50;
  }
  if (can_build_improvement(pcity, B_SUPERMARKET) &&
      player_knows_techs_with_flag(plr, TF_FARMLAND))
    values[B_SUPERMARKET]=pcity->size*55;

  if (can_build_improvement(pcity, B_AQUEDUCT)
      && pcity->size > game.aqueduct_size-2)
    values[B_AQUEDUCT]=pcity->size*125+pcity->food_surplus*50;
  if (can_build_improvement(pcity, B_SEWER)
      && pcity->size > game.sewer_size-1)
    values[B_SEWER]=pcity->size*100+pcity->food_surplus*50;
  
  if (can_build_improvement(pcity, B_HARBOUR) && (pcity->size > 5)) 
    values[B_HARBOUR]=pcity->size*60;
  if (can_build_improvement(pcity, B_OFFSHORE) && 
      !can_build_improvement(pcity, B_HARBOUR))
    values[B_OFFSHORE]=pcity->size*60;
  
  if (can_build_improvement(pcity, B_MARKETPLACE)) 
    values[B_MARKETPLACE]=pcity->trade_prod*200;
  
  if (pcity->trade_prod > 15) {
    if (can_build_improvement(pcity, B_BANK)) 
      values[B_BANK]=pcity->tax_total*100;

    if (can_build_improvement(pcity, B_STOCK)) 
      values[B_STOCK]=pcity->tax_total*100;
  }

  if (g->trade_bonus > 0 && can_build_improvement(pcity, B_SUPERHIGHWAYS))
    values[B_SUPERHIGHWAYS]=pcity->trade_prod*60;
  
  if (can_build_improvement(pcity, B_COURTHOUSE)) {
    if (g->corruption_level) 
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
int do_make_unit_veteran(struct city *pcity, Unit_Type_id id)
{
  if (unit_flag(id,F_DIPLOMAT))
    return government_has_flag(get_gov_pcity(pcity), G_BUILD_VETERAN_DIPLOMAT);

  if (is_ground_unittype(id) || improvement_variant(B_BARRACKS)==1)
    return city_got_barracks(pcity);
  else if (is_water_unit(id))
    return (city_affected_by_wonder(pcity, B_LIGHTHOUSE) || city_got_building(pcity, B_PORT));
  else
    return city_got_building(pcity, B_AIRPORT);
}

/**************************************************************************
corruption, corruption is halfed during love the XXX days.
**************************************************************************/
int city_corruption(struct city *pcity, int trade)
{
  struct government *g = get_gov_pcity(pcity);
  struct city *capital;
  int dist;
  int val;

  if (g->corruption_level == 0) {
    return 0;
  }
  if (g->fixed_corruption_distance) {
    dist = g->fixed_corruption_distance;
  } else {
    capital=find_palace(city_owner(pcity));
    if (!capital)
      dist=36;
    else {
      int tmp = map_distance(capital->x, capital->y, pcity->x, pcity->y);
      dist=MIN(36,tmp);
    }
  }
  dist = dist * g->corruption_distance_factor + g->extra_corruption_distance;
  val=trade*dist/g->corruption_modifier;

  if (city_got_building(pcity, B_COURTHOUSE) ||   
      city_got_building(pcity, B_PALACE))
    val /= 2;
  val *= g->corruption_level;
  val /= 100;
  if (val >= trade && val)
    val = trade - 1;
  return(val); /* how did y'all let me forget this one? -- Syela */
}
  

int set_city_shield_bonus(struct city *pcity)
{
  int tmp = 0;
  if (city_got_building(pcity, B_FACTORY)) {
    if (city_got_building(pcity, B_MFG))
      tmp = 100;
    else
      tmp = 50;

    if (city_affected_by_wonder(pcity, B_HOOVER) ||
        city_got_building(pcity, B_POWER) ||
        city_got_building(pcity, B_HYDRO) ||
        city_got_building(pcity,B_NUCLEAR))
      tmp *= 1.5;
  }

  pcity->shield_bonus = tmp + 100;
  return (tmp + 100);
}

int city_shield_bonus(struct city *pcity)
{
  return pcity->shield_bonus;
}

/**************************************************************************
...
**************************************************************************/
int set_city_tax_bonus(struct city *pcity)
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
  pcity->tax_bonus = tax_bonus;
  return tax_bonus;
}

int city_tax_bonus(struct city *pcity)
{
  return pcity->tax_bonus;
}

/**************************************************************************
...
**************************************************************************/
int set_city_science_bonus(struct city *pcity)
{
  int science_bonus = 100;
  if (city_got_building(pcity, B_LIBRARY)) {
    science_bonus+=50;
    if (city_got_building(pcity, B_UNIVERSITY)) {
      science_bonus+=50;
    }
    if (city_got_effect(pcity, B_RESEARCH))
      science_bonus+=50;
  }
  if (city_affected_by_wonder(pcity, B_COPERNICUS)) 
    science_bonus+=50;
  if (city_affected_by_wonder(pcity, B_ISAAC))
    science_bonus+=100;
  pcity->science_bonus = science_bonus;
  return science_bonus;
}

int city_science_bonus(struct city *pcity)
{
  return pcity->science_bonus;
}

int wants_to_be_bigger(struct city *pcity)
{
  if (pcity->size < game.aqueduct_size) return 1;
  if (city_got_building(pcity, B_SEWER)) return 1;
  if (city_got_building(pcity, B_AQUEDUCT)
      && pcity->size < game.sewer_size) return 1;
  if (!pcity->is_building_unit) {
    if (pcity->currently_building == B_SEWER && pcity->did_buy == 1) return 1;
    if (pcity->currently_building == B_AQUEDUCT && pcity->did_buy == 1) return 1;
  } /* saves a lot of stupid flipflops -- Syela */
  return 0;
}


/*
 *  Units in a bought city are transferred to the new owner, units 
 * supported by the city, but held in other cities are updated to
 * reflect those cities as their new homecity.  Units supported 
 * by the bought city, that are not in a city square may be deleted.
 * This depends on the value of kill_outside.  Just in case the
 * supported units are in an unexplored part of the map, the 
 * area around them is lightened.
 *
 * - Kris Bubendorfer <Kris.Bubendorfer@MCS.VUW.AC.NZ>
 *
 * If verbose is true, send extra messages to players detailing what
 * happens to all the units.  --dwp
 *
 * If statement added to see if the owner of pcity is the same as vcity. If so,
 * then the city is disbanded and not bought, and we only need to evaluate the
 * units supported by the city, and not those actually present.
 * - Thue <thue.kristensen@get2net.dk>
 *
 * Interpretation of kill_outside changed to mean the radius outside
 * of which supported units are killed.  If 0, all supported units not
 * in the city are killed.  If -1, no supported units are killed.  --jjm
 */
void transfer_city_units(struct player *pplayer, struct player *pvictim, 
			 struct city *pcity, struct city *vcity, 
			 int kill_outside, int verbose)
{
  int x = vcity->x;
  int y = vcity->y;

  if (city_owner(pcity)!=city_owner(vcity)) { /* true=>bought, false =>disbanded */
    /* Transfer units in the city to the new owner */
    unit_list_iterate(map_get_tile(x, y)->units, vunit)  {
      freelog(LOG_VERBOSE, "Transfered %s in %s from %s to %s",
	      unit_name(vunit->type), vcity->name, pvictim->name, pplayer->name);
      if (verbose) {
	notify_player(pvictim, _("Game: Transfered %s in %s from %s to %s."),
		      unit_name(vunit->type), vcity->name,
		      pvictim->name, pplayer->name);
      }
      create_unit_full(pplayer, x, y, vunit->type, vunit->veteran,
		       pcity->id, vunit->moves_left, vunit->hp);
      wipe_unit(0, vunit);
    } unit_list_iterate_end;
  }

  /* Any remaining units supported by the city are either given new home
   * cities or maybe destroyed */
  unit_list_iterate(vcity->units_supported, vunit) {
    struct city* new_home_city = map_get_city(vunit->x, vunit->y);
    if(new_home_city) {
      /* unit is in another city: make that the new homecity,
	 unless that city is actually the same city (happens if disbanding) */

      if (pvictim == city_owner(new_home_city)) {
	freelog(LOG_VERBOSE, "Changed homecity of %s's %s in %s",
	     pvictim->name, unit_name(vunit->type), new_home_city->name);
	if(verbose) {
	  notify_player(pvictim, _("Game: Changed homecity of %s in %s."),
			unit_name(vunit->type), new_home_city->name);
	}
      } else {
	freelog(LOG_VERBOSE, "Transfered %s in %s from %s to %s",
	     unit_name(vunit->type), new_home_city->name,
	     pvictim->name, city_owner(new_home_city)->name);
	if (verbose) {
	  notify_player(pvictim, _("Game: Transfered %s in %s from %s to %s."),
			unit_name(vunit->type), new_home_city->name,
			pvictim->name, pplayer->name);
	}
      }
      if(new_home_city != vcity) {
	create_unit_full(city_owner(new_home_city), vunit->x, vunit->y,
			 vunit->type, vunit->veteran, new_home_city->id,
			 vunit->moves_left, vunit->hp);
      } else {
	create_unit_full(pplayer, vunit->x, vunit->y,
			 vunit->type, vunit->veteran, pcity->id,
			 vunit->moves_left, vunit->hp);
      }
    }else if((kill_outside < 0) ||
	     ((kill_outside > 0) &&
	      (real_map_distance(vunit->x, vunit->y, x, y) <= kill_outside))) {

      freelog(LOG_VERBOSE, "Transfered %s at (%d, %d) from %s to %s",
	      unit_name(vunit->type), vunit->x, vunit->y,
	      pvictim->name, pplayer->name);
      if (verbose) {
	notify_player(pvictim,
		      _("Game: Transfered %s at (%d, %d) from %s to %s."),
		      unit_name(vunit->type), vunit->x, vunit->y,
		      pvictim->name, pplayer->name);
      }
      create_unit_full(pplayer, vunit->x, vunit->y, vunit->type, 
		       vunit->veteran, pcity->id, vunit->moves_left,
		       vunit->hp);
      lighten_area(pplayer, vunit->x,vunit->y);

    }
    wipe_unit_spec_safe(0, vunit, NULL, 0);
  } unit_list_iterate_end;
}


/*
 * dist_nearest_city (in ai.c) does not seem to do what I want or expect
 * this function finds the closest friendly city to pos x,y.  I'm sure 
 * there must be a similar function somewhere, I just can't find it.
 * 
 *                                - Kris Bubendorfer 
 */

struct city *find_closest_owned_city(struct player *pplayer, int x, int y)
{
  int dist;
  struct city *rcity = city_list_get(&pplayer->cities, 0);
  
  if(rcity){
    dist = real_map_distance(x, y, rcity->x, rcity->y);
    
    city_list_iterate(pplayer->cities, pcity)
      if (real_map_distance(x, y, pcity->x, pcity->y) < dist){
	dist = real_map_distance(x, y, pcity->x, pcity->y);
	rcity = pcity;
      }      
    city_list_iterate_end;
  }  
  return rcity;
}

/*
 * This function creates a new player and copies all of it's science
 * research etc.  Players are both thrown into anarchy and gold is
 * split between both players.
 *                                - Kris Bubendorfer 
 */

static struct player *split_player(struct player *pplayer)
{
  int *nations_used, i, num_nations_avail=game.playable_nation_count, pick;
  int newplayer = game.nplayers;
  struct player *cplayer = &game.players[newplayer];

  nations_used = fc_calloc(game.playable_nation_count,sizeof(int));
  
  /* make a new player */

  player_init(cplayer);
  
  /* select a new name and nation for the copied player. */

  for(i=0; i<game.playable_nation_count;i++){
    nations_used[i]=i;
  }

  for(i = 0; i < game.nplayers; i++){
    if( game.players[i].nation < game.playable_nation_count ) {
      nations_used[game.players[i].nation] = -1;
      num_nations_avail--;
    }
  }

  pick = myrand(num_nations_avail);

  for(i=0; i<game.playable_nation_count; i++){ 
    if(nations_used[i] != -1)
      pick--;
    if(pick < 0) break;
  }
  
  /* Rebel will always be an AI player */

  cplayer->nation = nations_used[i];
  free(nations_used);
  pick_ai_player_name(cplayer->nation,cplayer->name);

  cplayer->is_connected = 0;
  cplayer->conn = NULL;
  cplayer->government = game.government_when_anarchy;  
  pplayer->revolution = 1;
  cplayer->capital = 1;

  /* Split the resources */
  
  cplayer->economic.gold = pplayer->economic.gold/2;

  /* Copy the research */

  cplayer->research.researched = 0;
  cplayer->research.researchpoints = pplayer->research.researchpoints;
  cplayer->research.researching = pplayer->research.researching;
  
  for(i = 0; i<game.num_tech_types ; i++)
    cplayer->research.inventions[i] = pplayer->research.inventions[i];
  cplayer->turn_done = 1; /* Have other things to think about - paralysis*/
  cplayer->embassy = 0;   /* all embassys destroyed */

  /* Do the ai */

  cplayer->ai.control = 1;
  cplayer->ai.tech_goal = pplayer->ai.tech_goal;
  cplayer->ai.prev_gold = pplayer->ai.prev_gold;
  cplayer->ai.maxbuycost = pplayer->ai.maxbuycost;
  cplayer->ai.handicap = pplayer->ai.handicap;
  cplayer->ai.warmth = pplayer->ai.warmth;
  set_ai_level_direct(cplayer, game.skill_level);
		    
  for(i = 0; i<game.num_tech_types ; i++){
    cplayer->ai.tech_want[i] = pplayer->ai.tech_want[i];
    cplayer->ai.tech_turns[i] = pplayer->ai.tech_turns[i];
  }
  
  /* change the original player */

  pplayer->government = game.government_when_anarchy;
  pplayer->revolution = 1;
  pplayer->economic.tax = PLAYER_DEFAULT_TAX_RATE;
  pplayer->economic.science = PLAYER_DEFAULT_SCIENCE_RATE;
  pplayer->economic.luxury = PLAYER_DEFAULT_LUXURY_RATE;
  pplayer->economic.gold = cplayer->economic.gold;
  pplayer->research.researched = 0;
  pplayer->turn_done = 1; /* Have other things to think about - paralysis*/
  pplayer->embassy = 0; /* all embassys destroyed */

  player_limit_to_government_rates(pplayer);

  /* copy the maps */

  give_map_from_player_to_player(pplayer, cplayer);

  game.nplayers++;
  game.max_players = game.nplayers;
  
  /* Not sure if this is necessary, but might be a good idea
     to avoid doing some ai calculations with bogus data:
  */
  assess_danger_player(cplayer);
  if (pplayer->ai.control) {
    assess_danger_player(pplayer);
  }
		    
  return cplayer;
}

/*
 * civil_war_triggered:
 *
 * The capture of a capital is not a sure fire way to throw
 * and empire into civil war.  Some governments are more susceptible 
 * than others, here are the base probabilities:
 *
 *      Anarchy   	90%   
 *	Despotism 	80%
 *	Monarchy  	70%
 *	Fundamentalism  60% (In case it gets implemented one day)
 *	Communism 	50%
 *   	Republic  	40%
 *	Democracy 	30%	
 *
 * Note:  In the event that Fundamentalism is added, you need to
 * update the array government_civil_war[G_LAST] in player.c
 *
 * [ SKi: That would now be data/default/governments.ruleset. ]
 *
 * In addition each city in revolt adds 5%, each city in rapture 
 * subtracts 5% from the probability of a civil war.  
 *
 * If you have at least 1 turns notice of the impending loss of 
 * your capital, you can hike luxuries up to the hightest value,
 * and by this reduce the chance of a civil war.  In fact by
 * hiking the luxuries to 100% under Democracy, it is easy to
 * get massively negative numbers - guaranteeing imunity from
 * civil war.  Likewise, 3 revolting cities under despotism
 * guarantees a civil war.
 *
 * This routine calculates these probabilities and returns true
 * if a civil war is triggered.
 *                                    - Kris Bubendorfer 
 */

int civil_war_triggered(struct player *pplayer)
{
  /* Get base probabilities */

  int dice = myrand(100); /* Throw the dice */
  int prob = get_government_civil_war_prob(pplayer->government);

  /* Now compute the contribution of the cities. */
  
  city_list_iterate(pplayer->cities, pcity)
    prob += city_unhappy(pcity) * 5 - city_celebrating(pcity) * 5;
  city_list_iterate_end;

  freelog(LOG_VERBOSE, "Civil war chance for %s: prob %d, dice %d",
	  pplayer->name, prob, dice);
  
  return(dice < prob);
}

/*
 * Capturing a nation's capital is a devastating blow.  This function
 * creates a new AI player, and randomly splits the original players
 * city list into two.  Of course this results in a real mix up of 
 * teritory - but since when have civil wars ever been tidy, or civil.
 * 
 * Embassies:  All embassies with other players are lost.  Other players
 *             retain their embassies with pplayer.
 *
 * Units:      Units inside cities are assigned to the new owner
 *             of the city.  Units outside are transferred along 
 *             with the ownership of their supporting city.
 *             If the units are in a unit stack with non rebel units,
 *             then whichever units are nearest an allied city
 *             are teleported to that city.  If the stack is a 
 *             transport at sea, then all rebel units on the 
 *             transport are teleported to their nearest allied city.
 * 
 * Cities:     Are split randomly into 2.  This results in a real
 *             mix up of teritory - but since when have civil wars 
 *             ever been tidy, or for any matter civil?
 *
 *
 * One caveat, since the spliting of cities is random, you can
 * conceive that this could result in either the original player
 * or the rebel getting 0 cities.  To prevent this, the hack below
 * ensures that each side gets roughly half, which ones is still 
 * determined randomly.
 *                                    - Kris Bubendorfer
 */

void civil_war(struct player *pplayer)
{
  int i, j;
  struct city *pnewcity;
  struct player *cplayer;

  cplayer = split_player(pplayer);

  /* So that clients get the correct game.nplayers: */
  send_game_info(0);
  
  /* Before units, cities, so clients know name of new nation
   * (for debugging etc).
   */
  send_player_info(cplayer,  NULL);
  send_player_info(pplayer,  NULL); 
  
  /* Now split the empire */

  freelog(LOG_VERBOSE,
	  "%s's nation is thrust into civil war, created AI player %s",
	  pplayer->name, cplayer->name);
  notify_player(pplayer,
		_("Game: Your nation is thrust into civil war, "
		  " %s is declared the leader of the rebel states."),
		cplayer->name);

  i = city_list_size(&pplayer->cities)/2;   /* number to flip */
  j = city_list_size(&pplayer->cities);	    /* number left to process */
  city_list_iterate(pplayer->cities, pcity) {
    if (!city_got_building(pcity, B_PALACE)) {
      if (i >= j || (i > 0 && myrand(2))) {
	
	/* Transfer city and units supported by this city to the new owner */
      
	if(!(pnewcity = transfer_city(cplayer,pplayer,pcity))){
	   freelog(LOG_VERBOSE,
		   "Transfer city returned no city - aborting civil war.");
	   return;
	}
	
	freelog(LOG_VERBOSE, "%s declares allegiance to %s",
		pnewcity->name, cplayer->name);
	notify_player(pplayer, _("Game: %s declares allegiance to %s."),
		      pnewcity->name,cplayer->name);
	map_set_city(pnewcity->x, pnewcity->y, pnewcity);   
	transfer_city_units(cplayer, pplayer, pnewcity, pcity, -1, 0);
	remove_city(pcity); /* don't forget this! */
	map_set_city(pnewcity->x, pnewcity->y, pnewcity);

	reestablish_city_trade_routes(pnewcity);
	city_check_workers(cplayer,pnewcity);
	update_map_with_city_workers(pnewcity);
	city_refresh(pnewcity);
	initialize_infrastructure_cache(pnewcity);
	send_city_info(0, pnewcity, 0);
	i--;
      }
    }
    j--;
  }
  city_list_iterate_end;
  
  /* Resolve Stack conflicts */

  i = 0;

  unit_list_iterate(pplayer->units, punit) 
    resolve_unit_stack(punit->x, punit->y, 0);
  unit_list_iterate_end;
  
  notify_player(0,
		_("Game: The capture of %s's capital and the destruction "
		  "of the empire's administrative\n"
		  "      structures have sparked a civil war.  "
		  "Opportunists have flocked to the rebel cause,\n"
		  "      and the upstart %s now holds power in %d "
		  "rebel provinces."),
		pplayer->name, cplayer->name, city_list_size(&cplayer->cities));
    
}  


/* 
 * Transfers pcity from dest, srcplayer.  No units are moved.  This
 * is vanila.  It is now used for incite, and I suspect it could
 * be used to trade cities in the diplomatic section too.
 *
 *  - Kris Bubendorfer
 *
 * Note that the old city is not yet destroyed, which means we
 * can't yet transfer (re-establish) the trade routes for
 * the new city.  (Though the new city struct has the same
 * traderoute data as the old city.)  Thus you should call
 *    reestablish_trade_routes(p_new_city);
 * sometime _after_ calling
 *    remove_city(p_old_city);
 * after this function.  --dwp
 *
 */
struct city *transfer_city(struct player *pplayer, struct player *cplayer,
			   struct city *pcity)
{
  struct city *pnewcity;
  int i;

  if (!pcity)
    return NULL;

  if (cplayer==pplayer || cplayer==NULL) 
    return NULL;
  
  if(pcity->owner != cplayer->player_no)
    return NULL;

  pnewcity=fc_malloc(sizeof(struct city));
  *pnewcity=*pcity;

  pnewcity->id=get_next_id_number();
  add_city_to_cache(pnewcity);

  for (i = 0; i < B_LAST; i++) {
    if (is_wonder(i) && city_got_building(pnewcity, i))
      game.global_wonders[i] = pnewcity->id;
  }
  pnewcity->owner=pplayer->player_no;
  unit_list_init(&pnewcity->units_supported);
  city_list_insert(&pplayer->cities, pnewcity);

  give_citymap_from_player_to_player(pnewcity, cplayer, pplayer);

  return pnewcity;
}

/**************************************************************************
  Here num_free is eg government->free_unhappy, and this_cost is
  the unhappy cost for a single unit.  We subtract this_cost from
  num_free as much as possible. 

  Note this treats the free_cost as number of eg happiness points,
  not number of eg military units.  This seems to make more sense
  and makes results not depend on order of calculation. --dwp
**************************************************************************/
void adjust_city_free_cost(int *num_free, int *this_cost)
{
  if (*num_free <= 0 || *this_cost <= 0) {
    return;
  }
  if (*num_free >= *this_cost) {
    *num_free -= *this_cost;
    *this_cost = 0;
  } else {
    *this_cost -= *num_free;
    *num_free = 0;
  }
}

