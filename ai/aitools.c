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
#include <map.h>
#include <log.h>

#include <mapgen.h>
#include <unittools.h>
#include <cityhand.h>
#include <cityturn.h>
#include <citytools.h>
#include <plrhand.h>

#include <aitools.h>
#include <aiunit.h>

struct city *dist_nearest_city(struct player *pplayer, int x, int y)
{ /* dist_nearest_enemy_* are no longer ever used.  This is
dist_nearest_enemy_city, respaced so I can read it and therefore
debug it into something useful. -- Syela */
  struct player *pplay;
  struct city *pc=NULL;
  int i;
  int dist = MAX(map.xsize / 2, map.ysize);
  int con = map_get_continent(x, y);
  for(i = 0; i < game.nplayers; i++) {
    pplay = &game.players[i];
    city_list_iterate(pplay->cities, pcity)
      if (real_map_distance(x, y, pcity->x, pcity->y) < dist &&
         (!con || con == map_get_continent(pcity->x, pcity->y)) &&
         (!pplayer || map_get_known(pcity->x, pcity->y, pplayer))) {
        dist = real_map_distance(x, y, pcity->x, pcity->y);
        pc = pcity;
      }
    city_list_iterate_end;
  }
  return(pc);
}

/**************************************************************************
.. change government,pretty fast....
**************************************************************************/
void ai_government_change(struct player *pplayer, int gov)
{
  struct packet_player_request preq;
  if (gov == pplayer->government)
    return;
  preq.government=gov;
  pplayer->revolution=0;
  pplayer->government=G_ANARCHY;
  handle_player_government(pplayer, &preq);
  pplayer->revolution = -1; /* yes, I really mean this. -- Syela */
}

/* --------------------------------TAX---------------------------------- */


/**************************************************************************
... Credits the AI wants to have in reserves.
**************************************************************************/
int ai_gold_reserve(struct player *pplayer)
{
  return MAX(pplayer->ai.maxbuycost, total_player_citizens(pplayer)*2);
/* I still don't trust this function -- Syela */
}

/* --------------------------------------------------------------------- */

void adjust_choice(int value, struct ai_choice *choice)
{
  choice->want = (choice->want *value)/100;
}

/**************************************************************************
...
**************************************************************************/

void copy_if_better_choice(struct ai_choice *cur, struct ai_choice *best)
{
  if (cur->want > best->want) {
    best->choice =cur->choice;
    best->want = cur->want;
    best->type = cur->type;
  }
}

void ai_advisor_choose_building(struct city *pcity, struct ai_choice *choice)
{ /* I prefer the ai_choice as a return value; gcc prefers it as an arg -- Syela */
  int i;
  int id = B_LAST;
  int prod = 0, danger = 0, downtown = 0, cities = 0;
  int want=0;
  struct player *plr;
        
  plr = &game.players[pcity->owner];
     
  /* too bad plr->score isn't kept up to date. */
  city_list_iterate(plr->cities, acity)
    prod += acity->shield_prod;
    danger += acity->ai.danger;
    downtown += acity->ai.downtown;
    cities++;
  city_list_iterate_end;
 
  for(i=0; i<B_LAST; i++) {
    if (!is_wonder(i) ||
       (!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
       pcity->shield_stock >= improvement_value(i) / 2) ||
       (!is_building_other_wonder(pcity) &&
/* built_elsewhere removed; it was very, very bad with multi-continent empires */
/* city_got_building(pcity, B_TEMPLE) && - too much to ask for, I think */
        !pcity->ai.grave_danger && /* otherwise caravans will be killed! */
/*        pcity->shield_prod * cities >= prod &&         this shouldn't matter much */
        pcity->ai.downtown * cities >= downtown &&
        pcity->ai.danger * cities <= danger)) { /* too many restrictions? */
/* trying to keep wonders in safe places with easy caravan access -- Syela */
/* new code 980620; old code proved grossly inadequate after extensive testing -- Syela */
      if(pcity->ai.building_want[i]>want) {
/* we have to do the can_build check to avoid Built Granary.  Now Building Granary. */
        if (can_build_improvement(pcity, i)) {
          want=pcity->ai.building_want[i];
          id=i;
        } else if(0) {
	  freelog(LOG_DEBUG, "%s can't build %s", pcity->name,
		  get_improvement_name(i));
	}
      } /* id is the building we like the best */
    }
  }

  if(0) {
    if (!want) freelog(LOG_DEBUG, "AI_Chosen: None for %s", pcity->name);
    else freelog(LOG_DEBUG, "AI_Chosen: %s with desire = %d for %s",
		 get_improvement_name(id), want, pcity->name);
  }
  choice->want = want;
  choice->choice = id;
  choice->type = 0;
}


/**********************************************************************
The following evaluates the unhappiness caused by military units
in the field (or aggressive) at a city when at Republic or Democracy
**********************************************************************/
int ai_assess_military_unhappiness(struct city *pcity, int gov)
{
  int unhap=0;
  int have_police;
  int variant;
  
  if (gov < G_REPUBLIC)
    return 0;
  
  have_police = city_got_effect(pcity, B_POLICE);
  variant = improvement_variant(B_WOMENS);
  
  if (gov == G_REPUBLIC) {
    if (have_police && variant==1 )
      return 0;
    
    unit_list_iterate(pcity->units_supported, punit) {
      if (unit_being_aggressive(punit) || is_field_unit(punit)) {
	unhap++;
      }
    }
    unit_list_iterate_end;
    if (have_police) unhap--;
  } 
  else if (gov == G_DEMOCRACY) {
    unit_list_iterate(pcity->units_supported, punit) {
      if (have_police && variant==1) {
	if (unit_being_aggressive(punit)) {
	  unhap++;
	}
      } else {
	if (unit_being_aggressive(punit)) {
	  unhap += 2;
	} else if (is_field_unit(punit)) {
	  unhap += 1;
	}
      }
    }
    unit_list_iterate_end;
    if (have_police && variant==0) {
      unhap -= 2;
    }
  }
  if (unhap < 0) unhap = 0;
  
  return unhap;
}
