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
#include <game.h>
#include <unit.h>
#include <unittools.h>
#include <unitfunc.h>
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
#include <maphand.h>


/**************************************************************************
 do all the gritty nitty chess like analysis here... (argh)
**************************************************************************/

void ai_manage_units(struct player *pplayer) 
{
  unit_list_iterate(pplayer->units, punit)
    ai_manage_unit(pplayer, punit);
  unit_list_iterate_end;
}
 
/**************************************************************************
... do something sensible with the unit...
**************************************************************************/

void ai_manage_unit(struct player *pplayer, struct unit *punit) 
{
  if (unit_flag(punit->type, F_SETTLERS)) {
    ai_manage_settler(pplayer, punit);
  } else if (unit_flag(punit->type, F_CARAVAN)) {
    ai_manage_caravan(pplayer, punit);
  } else if (is_military_unit(punit)) {
    ai_manage_military(pplayer,punit); 
  } else {
    
  }
  /* Careful Unit maybe void here */
}


/**************************************************************************
 settlers..
**************************************************************************/

void ai_do_immigrate(struct player *pplayer, struct unit *punit)
{
  
}

/**************************************************************************
...
**************************************************************************/

void ai_do_move_build_city(struct player *pplayer, struct unit *punit)
{
  int cont = map_get_continent(punit->x, punit->y);
  punit->ai.control = 0;
  punit->ai.ai_role = AIUNIT_BUILD_CITY;
  auto_settler_do_goto(pplayer,punit, 
		       pplayer->ai.island_data[cont].cityspot.x, 
		       pplayer->ai.island_data[cont].cityspot.y);
}

/**************************************************************************
...
**************************************************************************/

int ai_want_immigrate(struct player *pplayer, struct unit *punit)
{
  return 0;
}

/**************************************************************************
...
**************************************************************************/

int ai_want_work(struct player *pplayer, struct unit *punit)
{
  return 1;
}

/**************************************************************************
...
**************************************************************************/

int city_move_penalty(int x, int y, int x1, int x2) 
{
  int dist =  map_distance(x,y, x1, x2);
  if (dist == 0) return 100;
  if (dist == 1) return 99;
  if (dist == 2) return 98;
  if (dist == 3) return 97;
  if (dist < 6) return 95;
  return 50;
}

/**************************************************************************
...
**************************************************************************/

int city_close_modifier(struct city *pcity, int x, int y)
{
  int dist = map_distance(x,y, pcity->x, pcity->y);
  if (dist < 4) return 0;
  if (dist == 5) return 125;
  if (dist == 6) return 105;
  if (dist == 7) return 90;
  return 100;
}

/**************************************************************************
...
**************************************************************************/

int ai_want_city(struct player *pplayer, struct unit *punit)
{
  int best;
  int val;
  struct city *pcity;
  int cont;
  cont = map_get_continent(punit->x, punit->y);
  if (cont < 3) 
    return 0;
  if ((pplayer->ai.island_data[cont].cityspot.x == 0) && 
      (pplayer->ai.island_data[cont].cityspot.y == 0)) {
    best =0;
    ISLAND_ITERATE(cont) { 
      if (map_get_city_value(x, y)) {
	pcity = dist_nearest_enemy_city(NULL,x, y);
	if (!pcity) {
	  val = (map_get_city_value(x,y) * 
		 city_move_penalty(x,y, punit->x, punit->y)) / 100;
	} else if (pcity->owner == punit->owner) {
	  val = (((map_get_city_value(x,y) * 
		   city_move_penalty(x,y, punit->x, punit->y)) / 100) * 
		 city_close_modifier(pcity, x, y))/100;  
	} else {
	  val = 0;
	}
	if (val > best) {
	  pplayer->ai.island_data[cont].cityspot.x = x;
	  pplayer->ai.island_data[cont].cityspot.y = y;
	  best = val;
	}
      }
  }
   ISLAND_END;
    if (best == 0) 
      return 0;
  }
  if (!is_free_work_tile(pplayer,
			pplayer->ai.island_data[cont].cityspot.x, 
			pplayer->ai.island_data[cont].cityspot.y))
    return 0;
  return 1;
}

/**************************************************************************
...
**************************************************************************/
int get_settlers_on_island(struct player *pplayer, int cont)
{
  int set = 0;
  unit_list_iterate(pplayer->units, punit) 
    if ((unit_flag(punit->type, F_SETTLERS)) && (map_get_continent(punit->x, punit->y) == cont))
      set++;
  unit_list_iterate_end;
  return set;
}

/**************************************************************************
...
**************************************************************************/
int get_cities_on_island(struct player *pplayer, int cont)
{
  int cit = 0;
  city_list_iterate(pplayer->cities, pcity)
    if (map_get_continent(pcity->x, pcity->y) == cont)
      cit++;
  city_list_iterate_end;
  return cit;
}

/**************************************************************************
...
**************************************************************************/
int ai_enough_auto_settlers(struct player *pplayer, int cont) 
{
  int expand_factor[3] = {1,3,8}; 
  return (pplayer->ai.island_data[cont].workremain + 1 - pplayer->ai.island_data[cont].settlers <= expand_factor[get_race(pplayer)->expand]); 
}

/**************************************************************************
...
**************************************************************************/
int ai_want_settlers(struct player *pplayer, int cont)
{
  int set = get_settlers_on_island(pplayer, cont);
  int cit = get_cities_on_island(pplayer, cont);

  if (ai_in_initial_expand(pplayer))
    return 1;
  if (!ai_enough_auto_settlers(pplayer, cont))
    return 1;
  if (pplayer->ai.island_data[cont].workremain + 1 >= cit / 2)
    return 1;
  return (set * 3 < cit);
}

/**************************************************************************
...
**************************************************************************/
const char *get_a_name(void)
{
  static char buf[80];
  static int x=0;
  sprintf(buf, "city%d", x++);
  return buf;
}

/**************************************************************************
...
**************************************************************************/
int ai_do_build_city(struct player *pplayer, struct unit *punit)
{
  int x,y;
  struct packet_unit_request req;
  struct city *pcity;
  req.unit_id=punit->id;
  strcpy(req.name, get_a_name());
  handle_unit_build_city(pplayer, &req);        
  pcity=map_get_city(punit->x, punit->y);
  if (!pcity)
    return 0;
  for (y=0;y<5;y++)
    for (x=0;x<5;x++) {
      if ((x==0 || x==4) && (y==0 || y==4)) 
        continue;
      light_square(pplayer, x+pcity->x-2, y+pcity->y-2, 0);
    }
  return 1;

}

/**************************************************************************
...
**************************************************************************/
struct city *wonder_on_continent(struct player *pplayer, int cont)
{
  city_list_iterate(pplayer->cities, pcity) 
    if (!(pcity->is_building_unit) && is_wonder(pcity->currently_building) && map_get_continent(pcity->x, pcity->y) == cont)
      return pcity;
  city_list_iterate_end;
  return NULL;
}
/**************************************************************************
decides what to do with a military unit.
**************************************************************************/

void ai_manage_military(struct player *pplayer,struct unit *punit)
{
switch (punit->ai.ai_role)
   {
   case AIUNIT_NONE:
      ai_military_findjob(pplayer, punit);
      break;
   case AIUNIT_AUTO_SETTLER:
      punit->ai.ai_role = AIUNIT_NONE; 
      break;
   case AIUNIT_BUILD_CITY:
      punit->ai.ai_role = AIUNIT_NONE; 
      break;
   case AIUNIT_DEFEND_HOME:
      ai_military_gohome(pplayer, punit);
      break;
   case AIUNIT_ATTACK:
      ai_military_attack(pplayer, punit);
      break;
   case AIUNIT_FORTIFY:
      ai_military_gohome(pplayer, punit);
      break;
   case AIUNIT_RUNAWAY: 
      break;
   case AIUNIT_ESCORT: 
      break;
   }
}


void ai_military_findjob(struct player *pplayer,struct unit *punit)
{
if (ai_military_findtarget(pplayer,punit))
   {
   punit->ai.ai_role=AIUNIT_ATTACK;
   }
else 
   {
   punit->ai.ai_role=AIUNIT_NONE;
   }
}


void ai_military_gohome(struct player *pplayer,struct unit *punit)
{
struct city *pcity;
if (punit->homecity)
   {
   pcity=find_city_by_id(punit->homecity);
   printf("GOHOME (%d)(%d,%d)C(%d,%d)\n",punit->id,punit->x,punit->y,pcity->x,pcity->y);
   if ((punit->x == pcity->x)&&(punit->y == pcity->y))
      {
      printf("INHOUSE. GOTO AI_NONE(%d)\n", punit->id);
      punit->ai.ai_role=AIUNIT_NONE;
      }
   else
      {
      printf("GOHOME(%d,%d)\n",punit->goto_dest_x,punit->goto_dest_y);
      punit->goto_dest_x=pcity->x;
      punit->goto_dest_y=pcity->y;
      punit->activity=ACTIVITY_GOTO;
      punit->activity_count=0;
      }
   }
else
   {
   punit->ai.ai_role=ACTIVITY_FORTIFY;
   }
}


void ai_military_attack(struct player *pplayer,struct unit *punit)
{
int dist_city;
int dist_unit;
struct city *pcity;
struct unit *penemyunit;

if (pcity=dist_nearest_enemy_city(pplayer,punit->x,punit->y))
   {
   dist_city=map_distance(punit->x,punit->y, pcity->x, pcity->y);
   
   punit->goto_dest_x=pcity->x;
   punit->goto_dest_y=pcity->y;
   punit->activity=ACTIVITY_GOTO;
   punit->activity_count=0;
   printf("unit %d of kill city(%d,%d)\n",punit->id,punit->goto_dest_x,punit->goto_dest_y);
   }
if (penemyunit=dist_nearest_enemy_unit(pplayer,punit->x,punit->y))
   {
   dist_unit=map_distance(punit->x,punit->y,penemyunit->x,penemyunit->y);
   if (dist_unit < dist_city)
      {
      punit->goto_dest_x=penemyunit->x;
      punit->goto_dest_y=penemyunit->y;
      punit->activity=ACTIVITY_GOTO;
      punit->activity_count=0;
      printf("unit %d to kill unit(%d,%d) instead\n",punit->id,punit->goto_dest_x,punit->goto_dest_y);
      }
   }
   
/* We should never get here. If we have, someone boo-booed */
if (!(dist_city||dist_unit))
   {      
   punit->ai.ai_role=AIUNIT_NONE;
   }
}



/*************************************************************************
...
**************************************************************************/
void ai_manage_caravan(struct player *pplayer, struct unit *punit)
{
  struct city *pcity;
  struct packet_unit_request req;
  if (punit->activity != ACTIVITY_IDLE)
    return;
  if (punit->ai.ai_role == AIUNIT_NONE) {
    if ((pcity = wonder_on_continent(pplayer, map_get_continent(punit->x, punit->y))) && build_points_left(pcity)) {
      if (!same_pos(pcity->x, pcity->y, punit->x, punit->y))
	auto_settler_do_goto(pplayer,punit, pcity->x, pcity->y);
      else {
	req.unit_id = punit->id;
	req.city_id = pcity->id;
	handle_unit_help_build_wonder(pplayer, &req);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void ai_manage_settler(struct player *pplayer, struct unit *punit)
{
  int cont = map_get_continent(punit->x, punit->y);

  if (punit->ai.control && 
      pplayer->score.cities > 5) /* controlled by standard ai */
    return;
  if (punit->ai.control && 
      !ai_enough_auto_settlers(pplayer, map_get_continent(punit->x, punit->y)))
    return;
  if (punit->activity != ACTIVITY_IDLE)
    return;
  if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
    if (same_pos(punit->x, punit->y, 
		 pplayer->ai.island_data[cont].cityspot.x, 
		 pplayer->ai.island_data[cont].cityspot.y)) {
      pplayer->ai.island_data[cont].cityspot.x = 0;
      pplayer->ai.island_data[cont].cityspot.y = 0;
      punit->ai.ai_role = AIUNIT_NONE;
      if (map_get_city_value(punit->x, punit->y)) {
	if (ai_do_build_city(pplayer, punit)) 
	  return;
      }  
    }
  }
  if (ai_want_city(pplayer, punit) && 
      ai_enough_auto_settlers(pplayer, 
			      map_get_continent(punit->x, punit->y))) {
    ai_do_move_build_city(pplayer, punit);
     return;
  }
  if (ai_want_immigrate(pplayer, punit) > ai_want_work(pplayer, punit)) {
      ai_do_immigrate(pplayer, punit);
      return;
  } else {
    punit->ai.control = 1;
    punit->ai.ai_role = AIUNIT_AUTO_SETTLER;
  }
}









