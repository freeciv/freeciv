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
#include <shared.h>
#include <packets.h>
#include <events.h>
#include <map.h>
#include <mapgen.h>
#include <plrhand.h>
#include <cityhand.h>
#include <citytools.h>
#include <cityturn.h>
#include <unithand.h>
#include <aitools.h>
#include <aihand.h>
#include <aiunit.h>
#include <aicity.h>
#include <advisland.h>
#include <advleader.h>
#include <advmilitary.h>
#include <advdomestic.h>
int ai_fix_unhappy(struct city *pcity);
void ai_manage_city(struct player *pplayer, struct city *);
void ai_scientists_taxmen(struct city *pcity);

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/

void ai_manage_cities(struct player *pplayer)
{ 
  city_list_iterate(pplayer->cities, pcity)
    ai_manage_city(pplayer, pcity);
  city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/

int city_get_buildings(struct city *pcity)
{
  int b=0;
  int i;
  for (i=0; i<B_LAST; i++) {
    if (is_wonder(i)) continue;
    if (i==B_PALACE)  continue;
    if (city_got_building(pcity, i))
      b++;
  }
  return b;
}
/**************************************************************************
... find a good (bad) tile to remove
**************************************************************************/

int worst_elvis_tile(struct city *pcity, int x, int y, int bx, int by)
{
    return (16*get_food_tile(x, y, pcity) + 
          12*get_shields_tile(x, y, pcity) + 
          8*get_trade_tile(x, y, pcity) < 
          16*get_food_tile(bx, by, pcity) + 
          12*get_shields_tile(bx, by, pcity) +
          8*get_trade_tile(bx, by, pcity) 
          );
}

/************************************************************************** 
...
**************************************************************************/

void ai_do_build_unit(struct city *pcity, int unit_type)
{
  pcity->is_building_unit = 1;
  pcity->currently_building = unit_type;
}
/************************************************************************** 
...
**************************************************************************/

int is_defender_unit(int unit_type) 
{
  return ((U_WARRIORS <= unit_type) && (unit_type <= U_MECH));
}

/************************************************************************** 
...
**************************************************************************/

int city_get_defenders(struct city *pcity)
{
  int def=0;
  unit_list_iterate(pcity->units_supported, punit) {
    if (!can_build_unit_direct(pcity, punit->type))
      continue;
    if (!is_defender_unit(punit->type))
      continue;
    if (punit->x==pcity->x && punit->y==pcity->y)
      def++;
  }
  unit_list_iterate_end;
  return def;
}

/************************************************************************** 
...
**************************************************************************/

int city_get_settlers(struct city *pcity)
{
  int set=0;
  unit_list_iterate(pcity->units_supported, punit) {
    if (unit_flag(punit->type, F_SETTLERS))
      set++;
  }
  unit_list_iterate_end;
  return set;
}

/************************************************************************** 
...
**************************************************************************/

int ai_in_initial_expand(struct player *pplayer)
{
  int expand_cities [3] = {3, 5, 7};
  return (pplayer->score.cities < expand_cities[get_race(pplayer)->expand]);  
}

/************************************************************************** 
...
**************************************************************************/

void ai_city_build_defense(struct city *pcity)
{
  int i;
  int best= 0;
  int bestid = 0;
  for (i = U_WARRIORS; i < U_HORSEMEN ; i++) {
    if (can_build_unit(pcity, i) && 
	get_unit_type(i)->defense_strength >= best) {
      best = get_unit_type(i)->defense_strength;
      bestid = i;
    }
  }
  ai_do_build_unit(pcity, bestid);
}

/************************************************************************** 
...
**************************************************************************/

void ai_city_build_settler(struct city *pcity)
{
  int i;
  for (i = 0 ; i < U_LAST ; i ++) {
    if (unit_flag(i, F_SETTLERS) && can_build_unit(pcity, i)) {
      ai_do_build_unit(pcity, i);
      break;
    }
  }
}

/************************************************************************** 
...
**************************************************************************/

int ai_city_build_peaceful_unit(struct city *pcity)
{
  int i;
  if (is_building_other_wonder(pcity)) {
    for (i = 0 ; i < U_LAST ; i ++) {
      if (unit_flag(i, F_CARAVAN) && can_build_unit(pcity, i)) {
	ai_do_build_unit(pcity, i);
	break;
      }
    }
    if (i < U_LAST) 
      return 1;
  } else {
    if (can_build_improvement(pcity, B_CAPITAL)) {
      pcity->currently_building=B_CAPITAL;
      pcity->is_building_unit=0;
      return 1;
    }
  }
  return 0;
}


/************************************************************************** 
...
**************************************************************************/
void adjust_build_choice(struct player *pplayer, struct ai_choice *cur, 
                               struct ai_choice *best, int advisor)
{
  island_adjust_build_choice(pplayer, cur, advisor);
  leader_adjust_build_choice(pplayer, cur, advisor);
  copy_if_better_choice(cur, best);
}


/************************************************************************** 
... change the build order.
**************************************************************************/

void ai_city_choose_build(struct player *pplayer, struct city *pcity)
{
  struct ai_choice bestchoice, curchoice;
  int def = city_get_defenders(pcity);
  int set = city_get_settlers(pcity);

  bestchoice.choice = A_NONE;      
  bestchoice.want   = 0;
  bestchoice.type   = 0;
  military_advisor_choose_build(pplayer, &curchoice);
  adjust_build_choice(pplayer, &curchoice, &bestchoice, ADV_MILITARY);
  domestic_advisor_choose_build(pplayer, &curchoice);
  adjust_build_choice(pplayer, &curchoice, &bestchoice, ADV_DOMESTIC);

  if (bestchoice.want) {
    pcity->currently_building = bestchoice.choice;
    pcity->is_building_unit    = bestchoice.type;
    return;
  }
  /* fallback to my old ai, if functions haven't been implemented */
  
  
  if (pcity->ai.ai_role == AICITY_NONE) {
    if (!set && pcity->food_surplus >= 2 && ai_want_settlers(pplayer, map_get_continent(pcity->x, pcity->y))) {

      if (def || ai_in_initial_expand(pplayer)) {
	ai_city_build_settler(pcity); 
	return;
      }
    }
    if (!def) {
      ai_city_build_defense(pcity);
      return;
    }
    if(!advisor_choose_build(pcity)) {
      if (!ai_city_build_peaceful_unit(pcity))
	ai_city_build_defense(pcity);
      return;
    }
  }
}

/**************************************************************************
... 
**************************************************************************/

int building_unwanted(struct player *plr, int i)
{
  if (plr->research.researching != A_NONE)
    return 0;
  return (i == B_LIBRARY || i == B_UNIVERSITY || i == B_RESEARCH);
}

/**************************************************************************
...
**************************************************************************/

void ai_sell_obsolete_buildings(struct city *pcity)
{
  int i;
  struct player *pplayer = city_owner(pcity);
  for (i=0;i<B_LAST;i++) {
    if(city_got_building(pcity, i) 
       && !is_wonder(i) 
       && (wonder_replacement(pcity, i) || building_unwanted(city_owner(pcity), i))) {
      do_sell_building(pplayer, pcity, i);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: %s is selling %s (not needed) for %d", 
		       pcity->name, get_improvement_name(i), 
		       improvement_value(i)/2);
      return; /* max 1 building each turn */
    }
  }
}

/**************************************************************************
 cities, build order and worker allocation stuff here..
**************************************************************************/

void ai_manage_city(struct player *pplayer, struct city *pcity)
{
  auto_arrange_workers(pcity);
  if (ai_fix_unhappy(pcity))
    ai_scientists_taxmen(pcity);
  ai_sell_obsolete_buildings(pcity);
  ai_city_choose_build(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/

int ai_find_elvis_pos(struct city *pcity, int *xp, int *yp)
{
  int x,y;
  *xp = 0;
  *yp = 0;
  city_map_iterate(x, y) {
    if (x==2 && y==2)
      continue; 
    if (get_worker_city(pcity, x, y) == C_TILE_WORKER) {
      if (*xp==0 && *yp==0) { 
	*xp=x;
	*yp=y;
      } else {
	if (worst_elvis_tile(pcity, x, y, *xp, *yp)) {
	  *xp=x;
	  *yp=y;
	}
      }
    }
  }
  return (*xp!=0 || *yp!=0);
}
/**************************************************************************
...
**************************************************************************/

int ai_make_elvis(struct city *pcity)
{
  int xp, yp;
  if (ai_find_elvis_pos(pcity, &xp, &yp)) {
    set_worker_city(pcity, xp, yp, C_TILE_EMPTY);
    pcity->ppl_elvis++;
    return 1;
  } else
    return 0;
}

/**************************************************************************
...
**************************************************************************/

int free_tiles(struct city *pcity)
{
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void make_elvises(struct city *pcity)
{
  int xp, yp;
  pcity->ppl_elvis += (pcity->ppl_taxman + pcity->ppl_scientist);
  pcity->ppl_taxman = 0;
  pcity->ppl_scientist = 0;
  city_refresh(pcity);
 
  if (pcity->size < 8 || 
      (city_got_building(pcity, B_AQUEDUCT) && pcity->size < 12)) 
    return;
  if (city_got_building(pcity, B_SEWER))
    return;
  while (1) {
    if (ai_find_elvis_pos(pcity, &xp, &yp)) {
      if (get_food_tile(xp, yp, pcity) > pcity->food_surplus)
	break;
      set_worker_city(pcity, xp, yp, C_TILE_EMPTY);
      pcity->ppl_elvis++;
      city_refresh(pcity);
    } else
      break;
  }
    
}

/**************************************************************************
...
**************************************************************************/
void make_taxmen(struct city *pcity)
{
  while (!city_unhappy(pcity) && pcity->ppl_elvis) {
    pcity->ppl_taxman++;
    pcity->ppl_elvis--;
    city_refresh(pcity);
  }
  if (city_unhappy(pcity)) {
    pcity->ppl_taxman--;
    pcity->ppl_elvis++;
    city_refresh(pcity);
  }

}

/**************************************************************************
...
**************************************************************************/
void make_scientists(struct city *pcity)
{
  make_taxmen(pcity); /* reuse the code */
  pcity->ppl_scientist = pcity->ppl_taxman;
  pcity->ppl_taxman = 0;
}

/**************************************************************************
 we prefer science, but if both is 0 we prefer $ 
 (out of research goal situation)
**************************************************************************/
void ai_scientists_taxmen(struct city *pcity)
{
  int science_bonus, tax_bonus;
  make_elvises(pcity);
  if (pcity->ppl_elvis == 0 || city_unhappy(pcity)) 
    return;
  tax_bonus = city_tax_bonus(pcity);
  science_bonus = city_science_bonus(pcity);
  
  if (tax_bonus > science_bonus || (city_owner(pcity)->research.researching == A_NONE)) 
    make_taxmen(pcity);
  else
    make_scientists(pcity);
}

/**************************************************************************
...
**************************************************************************/
int ai_fix_unhappy(struct city *pcity)
{
  if (!city_unhappy(pcity))
    return 1;
  while (city_unhappy(pcity)) {
    if(!ai_make_elvis(pcity)) break;
    city_refresh(pcity);
  }
  return (city_unhappy(pcity));
}
