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
#include <plrhand.h>
#include <aitools.h>
#include <aiunit.h>

void ai_calculate_city_value(int isle);
int ai_military_findtarget(struct player *pplayer,struct unit *punit);


struct ai_map_struct ai_map;
void map_set_city_value(int x, int y, int val)
{
  *(ai_map.city_value + map_adjust_x(x) + y*map.xsize) = val;
}

int map_get_city_value(int x, int y)
{
  return *(ai_map.city_value + map_adjust_x(x) + y*map.xsize);
}


/**************************************************************************
 calculate some good to know stuff about the continents.
**************************************************************************/

void ai_init_island_info(int nr_islands)
{
  int i;
  int x, y;
  int isle;
  ai_map.nr_islands = nr_islands;
  ai_map.city_value = (int *)malloc(sizeof(int)*map.xsize*map.ysize);
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++)
      map_set_city_value(x, y, 0);

  for (i = 3 ; i < ai_map.nr_islands; i++) {
    ai_map.island_data[i].center.x = islands[i].x;
    ai_map.island_data[i].center.y = islands[i].y;
    ai_map.island_data[i].shared = 0;
    ai_map.island_data[i].loc.top    = 0;
    ai_map.island_data[i].loc.left   = -1;
    ai_map.island_data[i].loc.bottom = 0;
    ai_map.island_data[i].loc.right  = 0;
  }
  for (y=0;y<map.ysize;y++)
    for (x=0;x<map.xsize;x++) 
      if ((isle = map_get_continent(x, y)) > 2) {
	if (ai_map.island_data[isle].loc.top == 0) {
	  ai_map.island_data[isle].loc.top = y;
	} else
	  ai_map.island_data[isle].loc.bottom = y;
      }
  for (x=0;x<map.xsize;x++) 
    for (y=0;y<map.ysize;y++)
      if ((isle = map_get_continent(x, y)) > 2) {
	if (ai_map.island_data[isle].loc.left == -1) {
	  ai_map.island_data[isle].loc.left = x;
	  ai_map.island_data[isle].loc.right = x;
	} else if (ai_map.island_data[isle].loc.right + 1 < x) {
	  ai_map.island_data[isle].loc.right = x +map.xsize;
	  ai_map.island_data[isle].loc.left  = x;
	} else if (ai_map.island_data[isle].loc.right < map.xsize) {
	  ai_map.island_data[isle].loc.right = x;
	}
      }
  /* Don't ask me how this works, i want to forget as fast as possible */
  
  for ( i = 3; i < ai_map.nr_islands; i++) {
    ai_calculate_city_value(i);
  }
  { 
    int p;
    for (p=0;p<game.nplayers;p++) {
      for ( i = 3; i < ai_map.nr_islands; i++) {
	game.players[p].ai.island_data[i].cityspot.x = 0;
	game.players[p].ai.island_data[i].cityspot.y = 0;
	game.players[p].ai.island_data[i].harbour.x  = 0;
	game.players[p].ai.island_data[i].harbour.y  = 0;
	game.players[p].ai.island_data[i].wonder.x   = 0;
	game.players[p].ai.island_data[i].wonder.y   = 0;
      }
    }
  }
}

/* nearest city can be found with player = NULL, notice this function ignores
   cities on other continents. */


struct city *dist_nearest_enemy_city(struct player *pplayer, int x, int y)
{
  struct player *pplay;
  struct city *pc=NULL;
  struct city *pcity=NULL;
  int i;
  int dist=40;
  for(i=0; i<game.nplayers; i++) {
    pplay=&game.players[i];
    if (pplay!=pplayer) {
      city_list_iterate(pplay->cities, pcity) {
        if (map_distance(x, y, pcity->x, pcity->y)<dist 
         && map_get_continent(x, y)==map_get_continent(pcity->x, pcity->y)
         && map_get_known(x,y,pplayer)) 
           { 
           dist=map_distance(x, y, pcity->x, pcity->y);
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
  struct unit *punit=NULL;
  int i;
  int dist=40;
  for(i=0; i<game.nplayers; i++) {
    pplay=&game.players[i];
    if (pplay!=pplayer) {
      unit_list_iterate(pplay->units, punit) {
        if (map_distance(x, y, punit->x, punit->y)<dist 
         && map_get_continent(x, y)==map_get_continent(punit->x, punit->y)
         && map_get_known(x,y,pplayer)) 
           { 
           dist=map_distance(x, y, punit->x, punit->y);
           pu=punit;
           }
      }
      unit_list_iterate_end;
    }
  }
  return  pu;
}


int ai_city_spot_value(int xp, int yp) 
{
  int food;
  int x, y;
  int val;
  int dist;
  struct city *pcity=dist_nearest_enemy_city(NULL, xp, yp);
  val = 0;
  if (pcity) {
    dist = map_distance(xp, yp, pcity->x, pcity->y);
    if (dist < 4) 
      return 0;
  }
  food = get_food_tile_bc(xp, yp);
  if ((map_get_terrain(xp, yp) == T_OCEAN) || food < 2) 
    return 0;

  city_map_iterate(x, y)
    val+=is_good_tile(xp+x-2, yp+y-2);
  val*=2;
  if (is_terrain_near_tile(xp, yp, T_OCEAN))
    val+=10;
  if (food == 1) 
    val/=2;
  if (val < 10) 
    return 0;
  return val;
}

void ai_calculate_city_value(int isle)
{
      ISLAND_ITERATE(isle) {
        map_set_city_value(x, y, ai_city_spot_value(x, y));
      }
      ISLAND_END
}

int is_free_work_tile(struct player *pplayer, int x, int y)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner!=pplayer->player_no)
      return 0;
    if (unit_flag(punit->type, F_SETTLERS))
      return 0;
  }
  unit_list_iterate_end;
  unit_list_iterate(pplayer->units, punit) {
    if (same_pos(punit->goto_dest_x, punit->goto_dest_y, x, y) && 
	punit->activity==ACTIVITY_GOTO && unit_flag(punit->type, F_SETTLERS))
      return 0;
  }
  unit_list_iterate_end;
  return 1;
}


int work_on_tile(struct player *pplayer, int x, int y)
{
  if (!is_free_work_tile(pplayer, x,y)) 
    return 0;
  return (benefit_irrigate(pplayer, x, y) + benefit_mine(pplayer, x, y) +
	  benefit_road(pplayer, x, y) + benefit_pollution(pplayer, x, y)/2);
}

int ai_city_workremain(struct city *pcity)
{
  int val = 0;
  int x,y;
  city_map_iterate(x, y){
    if ((map_get_city(pcity->x+x-2, pcity->y+y-2)) || 
	  (map_get_continent(pcity->x, pcity->y) != 
	   map_get_continent(pcity->x+x-2, pcity->y+y-2)))
      continue;
    val+= work_on_tile(city_owner(pcity), pcity->x+x-2, pcity->y+y-2);
  }
  return val;
}

void ai_update_player_island_info(struct player *pplayer)
{
  int i;
  int cont;
  for (i = 3; i < ai_map.nr_islands; i++) {
    pplayer->ai.island_data[i].cities = 0;
    pplayer->ai.island_data[i].workremain = 0;
    pplayer->ai.island_data[i].settlers = 0;
  }
  city_list_iterate(pplayer->cities, pcity) {
    cont =map_get_continent(pcity->x, pcity->y);
    pplayer->ai.island_data[cont].cities++;
    pcity->ai.workremain = ai_city_workremain(pcity);
    if (pcity->ai.workremain) 
      pplayer->ai.island_data[cont].workremain++;
  }
  city_list_iterate_end;
  unit_list_iterate(pplayer->units, punit) { 
    cont = map_get_continent(punit->x, punit->y);
    if(unit_flag(punit->type, F_SETTLERS))
      pplayer->ai.island_data[cont].settlers++;
  }
  unit_list_iterate_end;
}

/**************************************************************************
..... can we find a military target?........
**************************************************************************/
int ai_military_findtarget(struct player *pplayer,struct unit *punit)
{
struct city *pcity;
struct unit *penemyunit;

int myreturn=0;
int agression=0;
int closestcity;
int closestunit;

/* Pick out units that have attack=defense or better */
agression=10*(get_attack_power(punit)/10+1-get_defense_power(punit)/10);
if (agression<0) { agression=0; }
else { agression+=5; }

pcity=dist_nearest_enemy_city(pplayer,punit->x,punit->y); 
penemyunit=dist_nearest_enemy_unit(pplayer,punit->x,punit->y); 
closestcity=map_distance(punit->x,punit->y,pcity->x,pcity->y);
if (pcity)
   {
   if (closestcity < agression*2 )
      {
      myreturn=1;
      }
   }
if (penemyunit)
   {
   closestunit=map_distance(punit->x,punit->y,penemyunit->x,penemyunit->y);
    if (closestunit < agression*2 )
      {
      myreturn=1;
      }
   }
if (punit->homecity && closestcity+closestunit > agression*2)
   {
   myreturn=0;
   }
else 
   {
   myreturn=1;
   }
return(myreturn);
}

/* -----------------------------GOVERNMENT------------------------------ */



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
}

/* --------------------------------TAX---------------------------------- */


/**************************************************************************
... Credits the AI wants to have in reserves.
**************************************************************************/
int ai_gold_reserve(struct player *pplayer)
{
  return total_player_citizens(pplayer)*2;
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






