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
#include <advmilitary.h>

struct move_cost_map warmap;

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

void generate_warmap(struct city *pcity, struct unit *punit)
{
  int x, y, c, i, j, k, xx[3], yy[3], tm;
  int orig_x, orig_y;
  int igter = 0;
  int ii[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int jj[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int maxcost = THRESHOLD * 6; /* should be big enough without being TOO big */
  struct tile *tile0, *tile1;
  struct city *acity;

  warmap.warcity = pcity;
  warmap.warunit = punit;
  if (pcity) { orig_x = pcity->x; orig_y = pcity->y; }
  else { orig_x = punit->x; orig_y = punit->y; }
  if (punit && unit_flag(punit->type, F_IGTER)) igter++;
  for (x = 0; x < map.xsize; x++) {
    if (x > orig_x)
      i = MIN(x - orig_x, orig_x + map.xsize - x);
    else
      i = MIN(orig_x - x, x + map.xsize - orig_x);      
    for (y = 0; y < map.ysize; y++) {
      if (y > orig_y)
        j = MAX(y - orig_y, i);
      else
        j = MAX(orig_y - y, i);
      j *= 3;
      if (j < maxcost) j = maxcost;
      if (j > 255) j = 255;
      warmap.cost[x][y] = j;
    }
  }
  warmap.cost[orig_x][orig_y] = 0;
  warstacksize = 0;
  warnodes = 0;

  add_to_stack(orig_x, orig_y);
  
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
      if (igter) c = 3; /* NOT c = 1 */
      else c = tile0->move_cost[k];
      tm = warmap.cost[x][y] + c;
      if (warmap.cost[xx[i]][yy[j]] > tm && tm < maxcost) {
        warmap.cost[xx[i]][yy[j]] = tm;
        add_to_stack(xx[i], yy[j]);
/*       if (acity = map_get_city(xx[i], yy[j])) printf("(%d,%d) to %s: %d\n",
             orig_x, orig_y, acity->name, warmap.cost[xx[i]][yy[j]]); */
      }
    } /* end for */
  } while (warstacksize > warnodes);
/* printf("Generated warmap for (%d,%d) with %d nodes checked.\n", orig_x, orig_y, warnodes); */
/* warnodes is often as much as 2x the size of the continent -- Syela */
}

int assess_danger(struct city *pcity)
{
  struct unit *punit;
  int i, danger = 0, v, dist, con, m;
  int danger2 = 0; /* linear for walls */
  struct player *aplayer, *pplayer;
  int pikemen = 0;
  int urgency = 0;
  int igwall;

  pplayer = &game.players[pcity->owner];
  con = map_get_continent(pcity->x, pcity->y); /* Not using because of boats */

  generate_warmap(pcity, 0);

  pcity->ai.grave_danger = 0;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (unit_flag(punit->type, F_PIKEMEN)) pikemen++;
  unit_list_iterate_end;

  for(i=0; i<game.nplayers; i++) {
    if (i != pcity->owner) {
      aplayer = &game.players[i];
/* should I treat empty enemy cities as danger? */
      unit_list_iterate(aplayer->units, punit)
        igwall = 0;
        v = get_unit_type(punit->type)->attack_strength * 10;
        if (punit->veteran) v *= 1.5;
/* get_attack_power will be wrong if moves_left == 1 || == 2 */
        v *= punit->hp * get_unit_type(punit->type)->firepower;
        if (unit_ignores_citywalls(punit) || 
            (!is_heli_unit(punit) && !is_ground_unit(punit))) igwall++;
        if (city_got_citywalls(pcity) && igwall) v *= 3;

        dist = real_map_distance(punit->x, punit->y, pcity->x, pcity->y) * 3;
        if (unit_flag(punit->type, F_IGTER)) dist /= 3;
        else if (is_ground_unit(punit)) dist = warmap.cost[punit->x][punit->y];

        m = get_unit_type(punit->type)->move_rate;
/* if dist = 9, a chariot is 1.5 turns away.  NOT 2 turns away. */
/* Samarkand bug should be obsoleted by re-ordering of events */
        if (!dist) { /* Discovered the obvious that railroads are to blame! -- Syela */
/*          printf("Dist = 0: %s's %s at (%d, %d), %s at (%d, %d)\n",
             game.players[punit->owner].name, unit_types[punit->type].name,
             punit->x, punit->y, pcity->name, pcity->x, pcity->y); */
          dist = 1;
        }
        if (dist <= m * 3 && v) urgency++;
        if (dist <= m && v) pcity->ai.grave_danger++;

        if (unit_flag(punit->type, F_HORSE)) {
          if (pikemen) v /= 2;
          else if (get_invention(pplayer, A_FEUDALISM) != TECH_KNOWN)
            pplayer->ai.tech_want[A_FEUDALISM] += v * m / 2 / dist;
        }

        v /= 30; /* rescaling factor to stop the overflow nonsense */
        v *= v;

        if (!igwall) danger2 += v * m / dist;
        if (dist * dist < m * 3) danger += v * m / 3; /* knights can't attack more than twice */
        else danger += v * m * m / dist / dist;
      unit_list_iterate_end;
    }
  } /* end for */
  if (danger < 0 || danger > 1<<30) /* I hope never to see this! */
    printf("Dangerous danger (%d) in %s.  Beware of overflow.\n", danger, pcity->name);
  pcity->ai.danger = danger;
  if (pcity->ai.building_want[B_CITY] > 0 && danger2) {
    i = assess_defense(pcity); 
    if (danger2 > i / 2) pcity->ai.building_want[B_CITY] = 100 + urgency;
    else pcity->ai.building_want[B_CITY] += danger2 * 100 / i;
  }
  return(urgency);
  
}

int assess_defense(struct city *pcity)
{
  int v, def;
  def = 0;
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    v = get_defense_power(punit) * punit->hp * get_unit_type(punit->type)->firepower;
    v /= 20; /* divides reasonably well, should stop overflows */
/* actually is *= 1.5 for city, then /= 30 for scale - don't be confused */
/* just trying to minimize calculations and thereby rounding errors -- Syela */
    if (is_military_unit(punit)) def += v * v;
  unit_list_iterate_end;
  if (city_got_citywalls(pcity)) def *= 9; /* walls rule */
  /* def is an estimate of our total defensive might */
  return(def);
/* unclear whether this should treat settlers/caravans as defense -- Syela */
}

int unit_desirability(int i, int def)
{
  int cur, a, d;   
  cur = get_unit_type(i)->firepower *
        get_unit_type(i)->hp;
  if (def) cur *= 3;
  else cur *= get_unit_type(i)->move_rate;
  a = get_unit_type(i)->attack_strength;
  d = get_unit_type(i)->defense_strength;
  if (def) cur *= d;
  else if (d > a) return(0);
  else if (d == 1) { cur *= (a + d); cur /= 2; }
  else cur *= a; /* wanted to rank Legion > Catapult > Archer */
  if (unit_flag(i, F_IGTER) && !def) cur *= 3;
  if (unit_flag(i, F_PIKEMEN) && def) cur *= 1.5;
  return(cur);  
}

void military_advise_tech(struct player *pplayer, struct ai_choice *choice, int def)
{
  int i, j, k, a;
  int best = 0, x = A_NONE;
  if (!choice->type) return;
  a = unit_desirability(choice->choice, def);
  if (!a && !def) a = unit_desirability(choice->choice, 1); /* trust me */
  for (i = 0; i <= U_HOWITZER; i++) {
    j = ai_tech_goal_turns(pplayer, unit_types[i].tech_requirement);
    if (j > 0) {
/* WAS  k = (unit_desirability(i, def) / j - a) * choice->want / a; */
      k = (unit_desirability(i, def) - a) * choice->want / j / a;
      if (k > best) { best = k; x = unit_types[i].tech_requirement; }
    }
  }
  pplayer->ai.tech_want[x] += best * ai_tech_goal_turns(pplayer, x);
/* printf("%s wants %s with desire %d -> +%d desire for %s\n",
    pplayer->name, unit_types[choice->choice].name, choice->want,
    best * ai_tech_goal_turns(pplayer, x), advances[x].name); */
  return;
}

/********************************************************************** 
... this function should assign a value to choice and want and type, 
    where want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
    type = 1 means unit, type = 0 means building
***********************************************************************/

void military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				    struct ai_choice *choice)
{
  int def, danger, dist, ag, v, urgency, x, y;
  struct unit *punit;
  struct city *acity;
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;

/* TODO: recognize units that can DEFEND_HOME but are in the field. -- Syela */

  def = assess_defense(pcity);
/* logically we should adjust this for race attack tendencies */
  urgency = assess_danger(pcity); /* calling it now, rewriting old wall code */
  danger = pcity->ai.danger; /* we now have our warmap and will use it! */
/* printf("Assessed danger for %s = %d, Def = %d\n", pcity->name, danger, def); */

  if (danger) { /* might be able to wait a little longer to defend */
    danger -= def;   /* I think we're better off with this now -- Syela */
    if (danger >= def) {
      if (urgency) danger = 101; /* > 100 means BUY RIGHT NOW */
      else danger = 100;
    } else { danger *= 100; danger /= def; }
    if (pcity->ai.building_want[B_CITY] && def && can_build_improvement(pcity, B_CITY)
        && (danger < 101 || unit_list_size(&ptile->units) > 1)) {
/* walls before a second defender, unless we need it RIGHT NOW */
      choice->choice = B_CITY; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_CITY]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = 0;
    } else if (danger > 0) {
      choice->choice = ai_choose_defender(pcity);
      choice->want = danger;
      choice->type = 1;
      military_advise_tech(pplayer, choice, 1);
/*    printf("%s wants %s to defend with desire %d.\n", pcity->name,
       get_unit_type(choice->choice)->name, choice->want); */
/*    return; - this is just stupid */
    }
  } /* ok, don't need to defend */

  acity = dist_nearest_enemy_city(pplayer, pcity->x, pcity->y);
  if (acity) { x = acity->x; y = acity->y; }
  punit = dist_nearest_enemy_unit(pplayer, pcity->x, pcity->y);
  if (punit) {
    if (!acity) { x = punit->x; y = punit->y; }
    else if (warmap.cost[punit->x][punit->y] < warmap.cost[x][y]) { 
      x = punit->x; y = punit->y;
    }
  }

  if (acity || punit) {
    if (def || !pcity->ai.danger) {
      v = ai_choose_attacker(pcity);
      unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, aunit)
        if (get_unit_type(aunit->type)->attack_strength >
            get_unit_type(aunit->type)->defense_strength)
         v = ai_choose_defender(pcity);
      unit_list_iterate_end;
    } /* this isn't efficient but should work - I'm tired! -- Syela */
    else v = ai_choose_defender(pcity);
/* otherwise the AI would never build the first military unit! */
/* there may be a better solution but I don't want to unbalance anything. -- Syela */
    /* at this moment, we only choose ground units */
    if (get_unit_type(v)->flags & F_IGTER)
      dist = real_map_distance(pcity->x, pcity->y, x, y);
    else dist = warmap.cost[x][y];
    
    danger = 100 - (dist * 100) / get_unit_type(v)->move_rate / THRESHOLD;
    /* 0 if we're 12 turns away, 91 if we're one turn away */
    if (danger <= 0) danger = 0;

    if (danger > choice->want) {
      choice->choice = v;
      choice->want = danger;
      choice->type = 1;
      military_advise_tech(pplayer, choice, 0);
    }
  } 
  return;
}
