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
#include <aitools.h>
#include <game.h>
#include <map.h>

struct move_cost_map {
  unsigned char cost[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];
} warmap;

struct stack_element {
  unsigned char x, y;
} warstack[MAP_MAX_WIDTH * MAP_MAX_HEIGHT];

/* this wastes ~20K of memory and should really be fixed but I'm lazy  -- Syela */

int warstacksize;
int warnodes;

/********************************************************************** 
... this function should assign a value to choice and want, where 
    want is a value between 1 and 100.
    if choice is A_NONE this advisor doesn't want any tech researched at
    the moment
***********************************************************************/

void military_advisor_choose_tech(struct player *pplayer,
				  struct ai_choice *choice)
{
  choice->choice = A_NONE;
  choice->want   = 0;
  /* this function haven't been implemented yet */
}

void add_to_stack(int x, int y)
{
  int i;
  struct stack_element tmp;
  warstack[warstacksize].x = x;
  warstack[warstacksize].y = y;
  warstacksize++;
}

void generate_warmap(struct city *pcity)
{
  int x, y, c, i, j, k, xx[3], yy[3], tm;
  int ii[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int jj[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int maxcost = 72; /* should be big enough without being TOO big */
  struct tile *tile0, *tile1;
  struct city *acity;

  for (x = 0; x < map.xsize; x++)
    for (y = 0; y < map.ysize; y++)
      warmap.cost[x][y] = maxcost;
  warmap.cost[pcity->x][pcity->y] = 0;
  warstacksize = 0;
  warnodes = 0;

  add_to_stack(pcity->x, pcity->y);
  
  do {
    x = warstack[warnodes].x;
    y = warstack[warnodes].y;
    warnodes++; /* for debug purposes */
    tile0 = map_get_tile(x, y);
    if((xx[2]=x+1)==map.xsize) xx[2]=0;
    if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
    xx[1] = x;
    if ((yy[0]=y-1)==-1) yy[0] = 0;
    if ((yy[2]=y+1)==map.ysize) yy[2]=y;
    yy[1] = y;
    for (k = 0; k < 8; k++) {
      i = ii[k]; j = jj[k]; /* saves CPU cycles? */
      tile1 = map_get_tile(xx[i], yy[j]);
      if (tile1->terrain == T_OCEAN) c = maxcost;
      else if ((tile0->special & tile1->special) & S_RAILROAD) c = 0;
      else if ((tile0->special & tile1->special) & S_ROAD) c = 1;
      else if (tile0->terrain == T_RIVER && tile1->terrain == T_RIVER) c = 1;
      else c = get_tile_type(tile1->terrain)->movement_cost*3;
      tm = warmap.cost[x][y] + c;
      if (warmap.cost[xx[i]][yy[j]] > tm) {
        warmap.cost[xx[i]][yy[j]] = tm;
        add_to_stack(xx[i], yy[j]);
/*       if (acity = map_get_city(xx[i], yy[j])) printf("%s to %s: %d\n",
             pcity->name, acity->name, warmap.cost[xx[i]][yy[j]]); */
      }
    } /* end for */
  } while (warstacksize > warnodes);
/* printf("Generated warmap for %s with %d nodes checked.\n", pcity->name, warnodes); */
/* warnodes is often as much as 2x the size of the continent -- Syela */
}

int assess_danger(struct city *pcity)
{
  struct unit *punit;
  int i, danger = 0, v, dist, con, m;
  struct player *aplayer;

  con = map_get_continent(pcity->x, pcity->y); /* Not using because of boats */

  generate_warmap(pcity);  

  for(i=0; i<game.nplayers; i++) {
    if (i != pcity->owner) {
      aplayer = &game.players[i];
      unit_list_iterate(aplayer->units, punit)
        v = get_unit_type(punit->type)->attack_strength * 10;
        if (punit->veteran) v *= 1.5;
/* get_attack_power will be wrong if moves_left == 1 || == 2 */
        v *= punit->hp * get_unit_type(punit->type)->firepower;
        if (city_got_citywalls(pcity) && (unit_ignores_citywalls(punit) ||
            (!is_heli_unit(punit) && !is_ground_unit(punit)))) v *= 3;
        v *= v;
        dist = real_map_distance(punit->x, punit->y, pcity->x, punit->y) * 3;
        if (unit_flag(punit->type, F_IGTER)) dist /= 3;
        else if (is_ground_unit(punit)) dist = warmap.cost[punit->x][punit->y];

        m = get_unit_type(punit->type)->move_rate;
        dist += (m - 1);
        dist /= m;
        dist--; /* because of random turn shuffling; fixes Samarkand bug -- Syela */
        if (dist < 1) dist = 1;
        v /= (dist * dist);
        danger += v;
      unit_list_iterate_end;
    }
  } /* end for */
  return(danger);
}

int assess_defense(struct city *pcity)
{
  int v, def;
  def = 0;
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    v = get_defense_power(punit) * punit->hp * get_unit_type(punit->type)->firepower;
    def += v * v;
  unit_list_iterate_end;
  def *= 2.25; /* because we are in a city == fortified */ 
  if (city_got_citywalls(pcity)) def *= 9; /* walls rule */
  /* def is an estimate of our total defensive might */
  return(def);
}

/********************************************************************** 
... this function should assign a value to choice and want and type, 
    where want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
    type = 1 means unit, type = 0 means building
***********************************************************************/

void  military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				    struct ai_choice *choice)
{
  int def, danger, dist, ag, v;
  struct unit *punit;
  struct city *acity;
  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;

  def = assess_defense(pcity);
/* logically we should adjust this for race attack tendencies */
  danger = assess_danger(pcity);
/* printf("Assessed danger for %s = %d, Def = %d\n", pcity->name, danger, def); */
  danger -= def;

  if (danger > 1000) { /* might be able to wait a little longer to defend */
    if (danger >= def) danger = 101; /* > 100 means BUY RIGHT NOW */
    else { danger *= 100; danger /= def; }
    if (pcity->ai.building_want[B_CITY]) { /* walls before a second defender */
      choice->choice = B_CITY; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_CITY];
      choice->type = 0;
    } else {
      choice->choice = ai_choose_defender(pcity);
      choice->want = danger;
      choice->type = 1;
/*    printf("%s wants %s to defend with desire %d.\n", pcity->name,
       get_unit_type(choice->choice)->name, choice->want); */
/*    return; - this is just stupid */
    }
  } /* ok, don't need to defend */
  acity = dist_nearest_enemy_city(pplayer, pcity->x, pcity->y);
  if (acity && def) { /* stupid to build attackers if we have no def! */
    v = ai_choose_attacker(pcity);
    /* at this moment, we only choose ground units */
    if (get_unit_type(v)->flags & F_IGTER)
      dist = real_map_distance(pcity->x, pcity->y, acity->x, acity->y);
    else dist = warmap.cost[acity->x][acity->y]; /* had pcity - OOPS! */

    danger = 100 - (dist * 100) / get_unit_type(v)->move_rate / 12;
    /* 0 if we're 12 turns away, 91 if we're one turn away */
    if (danger <= 0) danger = 0;
/*    else printf("%s wants %s to attack %s (%d away) with desire %d.\n",
        pcity->name,  get_unit_type(v), acity->name, dist, danger); */
    if (danger > choice->want) {
      choice->choice = v;
      choice->want = danger;
      choice->type = 1;
    }
  }
  return;
}
