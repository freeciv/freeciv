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
#include <mapgen.h>
#include <unittools.h>
#include <cityhand.h>
#include <citytools.h>
#include <plrhand.h>
#include <aitools.h>
#include <aiunit.h>

struct city *dist_nearest_enemy_city(struct player *pplayer, int x, int y)
{
  struct player *pplay;
  struct city *pc=NULL;
  int i;
  int dist=40;
  int con = map_get_continent(x, y);
  for(i=0; i<game.nplayers; i++) {
    pplay=&game.players[i];
    if (pplay!=pplayer) {
      city_list_iterate(pplay->cities, pcity) {
        if (real_map_distance(x, y, pcity->x, pcity->y)<dist 
         && (!con || con == map_get_continent(pcity->x, pcity->y))
         && (pplayer==NULL || map_get_known(pcity->x, pcity->y, pplayer))) 
           { 
           dist=real_map_distance(x, y, pcity->x, pcity->y);
           pc=pcity;
           }
      }
      city_list_iterate_end;
    }
  }
  return  pc;
}


struct unit *dist_nearest_enemy_unit(struct player *pplayer, int x, int y)
{
  struct player *pplay;
  struct unit *pu=NULL;
  int i;
  int dist=40;
  for(i=0; i<game.nplayers; i++) {
    pplay=&game.players[i];
    if (pplay!=pplayer) {
      unit_list_iterate(pplay->units, punit) {
        if (real_map_distance(x, y, punit->x, punit->y)<dist 
         && map_get_continent(x, y)==map_get_continent(punit->x, punit->y)
         && map_get_known(x,y,pplayer)) 
           { 
           dist=real_map_distance(x, y, punit->x, punit->y);
           pu=punit;
           }
      }
      unit_list_iterate_end;
    }
  }
  return  pu;
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
    if (!is_wonder(i) || (!is_building_other_wonder(pcity) &&
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
        } /* else printf("%s can't build %s\n", pcity->name, get_improvement_name(i)); */
      } /* id is the building we like the best */
    }
  }
  
/* if (!want) printf("AI_Chosen: None for %s\n", pcity->name);
  else printf("AI_Chosen: %s with desire = %d for %s\n",
          get_improvement_name(id), want, pcity->name); */
  choice->want = want;
  choice->choice = id;
  choice->type = 0;
}
