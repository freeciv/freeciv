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
#include <sys/time.h>
#include <advmilitary.h>
#include <gotohand.h>
#include <citytools.h>
#include <assert.h>

void do_unit_goto(struct player *pplayer, struct unit *punit);
int unit_defensiveness(struct unit *punit);

extern struct move_cost_map warmap;
unsigned char minimap[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];

/**************************************************************************
 do all the gritty nitty chess like analysis here... (argh)
**************************************************************************/

void ai_manage_units(struct player *pplayer) 
{
#ifdef CHRONO
  int sec, usec;
  struct timeval tv;
  gettimeofday(&tv, 0);
  sec = tv.tv_sec; usec = tv.tv_usec;
#endif
/*  printf("Managing units for %s\n", pplayer->name);  */
  unit_list_iterate(pplayer->units, punit)
/*  printf("Managing %s's %s\n", pplayer->name, unit_types[punit->type].name); */
      ai_manage_unit(pplayer, punit); 
  unit_list_iterate_end;
/*  printf("Managed units successfully.\n");   */
#ifdef CHRONO
  gettimeofday(&tv, 0);
  printf("%s's units consumed %d microseconds.\n", pplayer->name,
       (tv.tv_sec - sec) * 1000000 + (tv.tv_usec - usec));
#endif
}
 
/**************************************************************************
... do something sensible with the unit...
**************************************************************************/

void ai_manage_explorer(struct player *pplayer, struct unit *punit)
{ /* this is smarter than the old one, but much slower too. -- Syela */
  int i, j, d, f, x, y, con, dest_x, dest_y, best = 255, cur, a, b, ct = 3;
  int id = punit->id;

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);

  x = punit->x; y = punit->y;
  con = map_get_continent(x, y);

  generate_warmap(map_get_city(x, y), punit); /* CPU-expensive but worth it -- Syela */
/* otherwise did not understand about bays and did really stupid things! */

  if (!is_ground_unit(punit)) return; /* this funct not designed for boats! */
  for (d = 1; d <= 24; d++) { /* boats don't hunt huts */
/* printf("Hut-hunting: D = %d\n", d); */
    for (i = 0 - d; i <= d; i++) {
      f = 1;
      if (i != 0 - d && i != d) f = d * 2; /* I was an idiot to forget this */
      for (j = 0 - d; j <= d; j += f) {
        if (map_get_tile(x + i, y + j)->special & S_HUT &&
        warmap.cost[map_adjust_x(x + i)][y + j] < best &&
        map_get_continent(x + i, y + j) == con) {
          dest_x = map_adjust_x(x + i);
          dest_y = y + j;
          best = warmap.cost[dest_x][dest_y];
        }
      }
    }
  } /* end for D */
  if (best < 255) {
    punit->goto_dest_x = dest_x;
    punit->goto_dest_y = dest_y;
    punit->activity = ACTIVITY_GOTO;
    do_unit_goto(pplayer, punit);
    return; /* maybe have moves left but I want to return anyway. */
  }

/* OK, failed to find huts.  Will explore basically at random */
/* my old code was dumb dumb dumb dumb dumb and I'm rewriting it -- Syela */

  while (best && punit->moves_left && ct) {
    best = 0;
    x = punit->x; y = punit->y;
    for (i = -1; i <= 1; i++) {
      for (j = -1; j <= 1; j++) {
        if (map_get_continent(x + i, y + j) == con &&
        !is_enemy_unit_tile(x + i, y + j, punit->owner) &&
        !map_get_tile(x + i, y + j)->city_id) {
          cur = 0;
          for (a = i - 1; a <= i + 1; a++)
            for (b = j - 1; b <= j + 1; b++)
              if (!map_get_known(x + a, y + b, pplayer)) cur++;
          if (cur > best || (cur == best && myrand(2))) {
            dest_x = map_adjust_x(x + i);
            dest_y = y + j;
            best = cur;
          }
        }
      } /* end j */
    } /* end i */
    if (best) {
      if (!handle_unit_move_request(pplayer, punit, dest_x, dest_y)) {
        punit->goto_dest_x = dest_x;
        punit->goto_dest_y = dest_y;
        punit->activity = ACTIVITY_GOTO;
        do_unit_goto(pplayer, punit); /* going around ZOC hell */
      }
    }
    ct--; /* trying to avoid loops */
  }
  if (!punit->moves_left) return;
/* no adjacent squares help us to explore */

/* really slow part follows */
  best = 0;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      if (map_get_continent(x, y) == con && !is_enemy_unit_tile(x, y, punit->owner) &&
             !map_get_tile(x, y)->city_id) {
        cur = 0;
        for (a = -1; a <= 1; a++)
          for (b = -1; b <= 1; b++)
            if (!map_get_known(x + a, y + b, pplayer)) cur++;
        if (cur) {
          cur += 24; /* whatever you like, could be 255 */
          cur -= warmap.cost[x][y];
          if (cur > best || (cur == best && myrand(2))) {
            dest_x = map_adjust_x(x);
            dest_y = y;
            best = cur;
          }
        }
      }
    }
  }
  if (best > 0) {
    punit->goto_dest_x = dest_x;
    punit->goto_dest_y = dest_y;
    punit->activity = ACTIVITY_GOTO;
    do_unit_goto(pplayer, punit);
    return;
  }
/*  printf("%s's %s at (%d,%d) failed to explore.\n", pplayer->name,
    unit_types[punit->type].name, punit->x, punit->y); */
  if (is_military_unit(punit)) ai_military_gohome(pplayer, punit);
/*  if (!is_military_unit(punit)) wipe_unit(pplayer, punit); */
}


void ai_manage_unit(struct player *pplayer, struct unit *punit) 
{
  if (!punit->moves_left) return; /* can't do anything with no moves */
  if (unit_flag(punit->type, F_SETTLERS)) {
    ai_manage_settler(pplayer, punit);
  } else if (unit_flag(punit->type, F_CARAVAN)) {
    ai_manage_caravan(pplayer, punit);
  } else if (is_military_unit(punit)) {
    ai_manage_military(pplayer,punit); 
  } else {
    ai_manage_explorer(pplayer, punit); /* what else could this be? -- Syela */
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
const char *get_a_name(struct player *pplayer)
{
  static char buf[80];
  static int x=0;
  sprintf(buf, "%s", city_name_suggestion(pplayer)); /* Not a Number -- Syela */
  if (!buf[0]) sprintf(buf, "city%d", x++);
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
  strcpy(req.name, get_a_name(pplayer));
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

int should_unit_change_homecity(struct player *pplayer, struct unit *punit)
{
  int val;
  struct city *pcity;

  pcity = map_get_city(punit->x, punit->y);
  if (!pcity) return(0);
  if (pcity->id == punit->homecity) return(0);
  if (pcity->owner != punit->owner) return(0);
  else {
    val = -1;
    unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef)
      if (unit_defensiveness(punit) >= val &&
         is_military_unit(pdef) && pdef != punit) val = 0;
    unit_list_iterate_end;
 
/* printf("Incity at (%d,%d).  Val = %d.\n", punit->x, punit->y, val); */
   if (val) { /* Guess I better stay / you can live at home now */
      punit->ai.ai_role=AIUNIT_DEFEND_HOME;

/* change homecity to this city */
      unit_list_insert(&pcity->units_supported, punit);
      if ((pcity=city_list_find_id(&pplayer->cities, punit->homecity)))
            unit_list_unlink(&pcity->units_supported, punit);

      punit->homecity = map_get_city(punit->x, punit->y)->id;
      send_unit_info(pplayer, punit, 0);
/* homecity changed */
      return(1);
    }
  }
  return(0);
}


/**************************************************************************
decides what to do with a military unit.
**************************************************************************/

void ai_manage_military(struct player *pplayer,struct unit *punit)
{
  int id;

  id = punit->id;
  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);

  punit->ai.ai_role = AIUNIT_NONE;
/* was getting a bad bug where a settlers caused a defender to leave home */
/* and then all other supported units went on DEFEND_HOME/goto */
  ai_military_findjob(pplayer, punit);
  switch (punit->ai.ai_role) {
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
    case AIUNIT_EXPLORE:
      ai_manage_explorer(pplayer, punit);
      break;
  }

  if (punit = find_unit_by_id(id)) {
    if (punit->activity != ACTIVITY_IDLE) /* leaving units on GOTO is BAD news! */
      handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);

    if (punit->moves_left) { /* didn't move, or reached goal */
      handle_unit_activity_request(pplayer, punit, ACTIVITY_FORTIFY);
    } /* better than doing nothing */
  }
}

int unit_belligerence(struct unit *punit)
{
  int v, w;
  v = get_attack_power(punit) * punit->hp * 
            get_unit_type(punit->type)->firepower;
  w = v * v / get_unit_type(punit->type)->build_cost + 2;
  return(w);
}

int unit_vulnerability(struct unit *punit, struct unit *pdef)
{
  int v;

  v = get_total_defense_power(punit, pdef) * pdef->hp * 
           get_unit_type(pdef->type)->firepower;

  if (map_get_city(pdef->x, pdef->y)) /* prefer to bash cities */
       return (v * v / (get_unit_type(pdef->type)->build_cost + 40) *
           get_unit_type(punit->type)->hp / punit->hp);

  return (v * v / get_unit_type(pdef->type)->build_cost);
}

int reinforcements_value(struct unit *punit, struct unit *pdef)
{ /* if someone else can clean up my mess, all the better */
  int val = 0;
  unit_list_iterate(game.players[punit->owner].units, aunit)
    if (aunit != punit && is_tiles_adjacent(aunit->x, aunit->y, pdef->x, pdef->y))
      val += unit_belligerence(aunit);
  unit_list_iterate_end;
  return(val);
}

int ai_military_findvictim(struct player *pplayer,struct unit *punit)
{ /* work of Syela - mostly to fix the ZOC/goto strangeness */
  int xx[3], yy[3], x, y;
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int i, j, k, weakest, v;
  int dest;
  struct unit *pdef;
  struct city *pcity;
  x = punit->x;
  y = punit->y;
  dest = y * map.xsize + x;
  weakest = unit_belligerence(punit);
  if (punit->unhappiness) weakest = 2000000000; /* desperation */

/* stolen from gotohand.c */
  if((xx[2]=x+1)==map.xsize) xx[2]=0;
  if((xx[0]=x-1)==-1) xx[0]=map.xsize-1;
  xx[1] = x;
  if ((yy[0]=y-1)==-1) yy[0] = 0;
  if ((yy[2]=y+1)==map.ysize) yy[2]=y;
  yy[1] = y;

  for (k = 0; k < 8; k++) {
    i = ii[k]; j = jj[k]; /* saves CPU cycles? */
    pdef = get_defender(pplayer, punit, xx[i], yy[j]);
    if (pdef) {
      if (can_unit_attack_tile(punit, xx[i], yy[j])) { /* thanks, Roar */
        v = unit_vulnerability(punit, pdef);
        if (map_get_city(x, y) && /* pikemen defend Knights, attack Catapults */
              get_total_defense_power(pdef, punit) *
              get_total_defense_power(punit, pdef) >
              get_total_attack_power(pdef, punit) *
              get_total_attack_power(punit, pdef) &&
              get_total_attack_power(pdef, punit)) ;
/*              printf("%s defending %s from %s's %s\n",
            get_unit_type(punit->type)->name, map_get_city(x, y)->name,
            game.players[pdef->owner].name, get_unit_type(pdef->type)->name); */
        else if (v < weakest + reinforcements_value(punit, pdef)) {
/*          printf("Better than %d is %d (%s)\n", weakest, v,
             unit_types[pdef->type].name);  */
          weakest = v; dest = yy[j] * map.xsize + xx[i];
         } /* else printf("NOT better than %d is %d (%s)\n", weakest, v,
             unit_types[pdef->type].name); */
      }
    } else { /* no pdef */
      pcity = map_get_city(xx[i], yy[j]);
      if (pcity) {
        if (pcity->owner != pplayer->player_no) { /* free goodies */
          weakest = -1; dest = yy[j] * map.xsize + xx[i];
        }
      }
      if (map_get_tile(xx[i], yy[j])->special & S_HUT && weakest > 0 &&
          zoc_ok_move(punit, xx[i], yy[j]) &&
          punit->ai.ai_role != AIUNIT_DEFEND_HOME) { /* Oops! -- Syela */
        weakest = 0; dest = yy[j] * map.xsize + xx[i];
      }
    }
  }
  return(dest);
}
 
int ai_military_gothere(struct player *pplayer, struct unit *punit, int dest_x, int dest_y)
{
  int id, x, y;
  id = punit->id; x = punit->x; y = punit->y;
  if (!(dest_x == punit->x && dest_y == punit->y)) {
    punit->goto_dest_x = dest_x;
    punit->goto_dest_y = dest_y;
/*    printf("Syela - Doing unit goto.  From (%d,%d) to (%d,%d)\n",
        punit->x, punit->y, dest_x, dest_y);  */
    punit->activity = ACTIVITY_GOTO;
    do_unit_goto(pplayer,punit);
/* liable to bump into someone that will kill us.  Should avoid? */
  }
  if (unit_list_find(&pplayer->units, id)) { /* didn't die */
    punit->ai.ai_role = AIUNIT_NONE; /* in case we need to change */
    if (x != punit->x || y != punit->y) return 1; /* moved */
    else return 0; /* didn't move, didn't die */
  }
  return(-1); /* died */
}


int unit_defensiveness(struct unit *punit)
{
/* assert get_attack_power(punit); */
  return get_defense_power(punit) * punit->hp / MAX(get_attack_power(punit),1) /
    get_unit_type(punit->type)->move_rate / (unit_flag(punit->type, F_IGTER) ? 3 : 1);
}

void ai_military_findjob(struct player *pplayer,struct unit *punit)
{
  struct city *pcity = NULL, *acity = NULL;
  struct unit *aunit;
  int val;
  int d = 0, def = 0, bestd = 0;

/* tired of AI abandoning its cities! -- Syela */
  if (punit->homecity) {
    pcity = find_city_by_id(punit->homecity);
    if (pcity->ai.danger) { /* otherwise we can attack */
      def = assess_defense(pcity);
      if (punit->x == pcity->x && punit->y == pcity->y) { /* I'm home! */
        val = unit_defensiveness(punit);
        d = get_defense_power(punit) * punit->hp *
                  get_unit_type(punit->type)->firepower;
        if (city_got_citywalls(pcity) && is_ground_unit(punit)) d *= 3;
        d /= 20;
        def -= d * d;
      } else val = -1;
/* only the least defensive unit may leave home */
/* and only if this does not jeopardize the city */
/* def is the defense of the city without punit */
      unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef)
        if (is_military_unit(pdef) && pdef != punit) {
          if (unit_defensiveness(pdef) >= val) val = 0;
        }
      unit_list_iterate_end; /* was getting confused without the is_military part in */
      if (val > 0 || pcity->ai.danger > def * 
          unit_types[punit->type].attack_strength /
          unit_types[punit->type].defense_strength / 2) { /* Guess I better stay */
/* units leave too eagerly if I use just d * d.  This is somewhat of a WAG -- Syela */
        punit->ai.ai_role=AIUNIT_DEFEND_HOME;
        return;
/* } else if (!val) { printf("%s leaving %s.  defense = %d, def = %d, danger = %d\n",
unit_types[punit->type].name, pcity->name, assess_defense(pcity), def, pcity->ai.danger); */
      } else if (val < 0) { /* you can live at home / back to the old house */
        /* if I am closer to home than any other unit from my city AND */
        /* I am closer to home than the unit threatening me AND */
        /* I am closer to home than I am to my target , THEN go home */
        d = real_map_distance(punit->x, punit->y, pcity->x, pcity->y);
        unit_list_iterate(pcity->units_supported, pdef)
          if (real_map_distance(pdef->x, pdef->y, pcity->x, pcity->y) < d) val++;
        unit_list_iterate_end;
        aunit = dist_nearest_enemy_unit(pplayer, pcity->x, pcity->y);
        if (aunit)
          if (real_map_distance(aunit->x, aunit->y, pcity->x, pcity->y) > d) val++;
        acity = dist_nearest_enemy_city(pplayer, punit->x, punit->y);
        if (acity)
          if (real_map_distance(punit->x, punit->y, acity->x, acity->y) < d) val++;
        if (val < 0) { punit->ai.ai_role=AIUNIT_DEFEND_HOME; return; }
        /* printf("%s not needed to return to %s.\n", unit_types[punit->type].name, pcity->name); */
      } /* otherwise will let another unit defend the city for us */
    } /* else printf("%s not needed to defend %s.\n", unit_types[punit->type].name, pcity->name); */
  }
/* ok, what if I'm somewhere new? - ugly, kludgy code by Syela */
  if (should_unit_change_homecity(pplayer, punit)) {
    return;
  }

  if (unit_attack_desirability(punit->type) ||
      (pcity && !same_pos(pcity->x, pcity->y, punit->x, punit->y)))
     punit->ai.ai_role = AIUNIT_ATTACK;
  else punit->ai.ai_role = AIUNIT_DEFEND_HOME;
}


void ai_military_gohome(struct player *pplayer,struct unit *punit)
{
struct city *pcity;
int dest, dest_x, dest_y;
if (punit->homecity)
   {
   pcity=find_city_by_id(punit->homecity);
/*   printf("GOHOME
(%d)(%d,%d)C(%d,%d)\n",punit->id,punit->x,punit->y,pcity->x,pcity->y); */
   if ((punit->x == pcity->x)&&(punit->y == pcity->y))
      {
/*      printf("INHOUSE. GOTO AI_NONE(%d)\n", punit->id); */
/* aggro defense goes here -- Syela */
      dest = ai_military_findvictim(pplayer, punit);
      punit->ai.ai_role=AIUNIT_NONE;
      dest_x = dest % map.xsize;
      dest_y = dest / map.xsize;
      handle_unit_move_request(pplayer, punit, dest_x, dest_y); /* might bash someone */
      }
   else
      {
 /*     printf("GOHOME(%d,%d)\n",punit->goto_dest_x,punit->goto_dest_y); */
      punit->goto_dest_x=pcity->x;
      punit->goto_dest_y=pcity->y;
      punit->activity=ACTIVITY_GOTO;
      do_unit_goto(pplayer,punit);
      }
   }
else
   {
     handle_unit_activity_request(pplayer, punit, ACTIVITY_FORTIFY);
   }
}

#ifdef SIMPLISTIC
void find_something_to_kill(struct player *pplayer, struct unit *punit, int *x, int *y)
{
  struct city *pcity;
  pcity=dist_nearest_enemy_city(pplayer,punit->x,punit->y);
  if (pcity && !is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y)) {
    *x = pcity->x; *y = pcity->y; return;
  }
}

#else

void find_something_to_kill(struct player *pplayer, struct unit *punit, int *x, int *y)
{

/* your mission, should you choose to accept it, is to seek out and destroy the
nearest vulnerable enemy unit or city */

/* this could be awful to the CPU, will profile it later and see ... */

  int belli, vul, i, d, maxd;
  int con = map_get_continent(punit->x, punit->y);
  struct player *aplayer;
  struct unit *pdef;
  int kluge = 1000; /* Necessary in some regard */
  maxd = THRESHOLD * unit_types[punit->type].move_rate;
  if (unit_flag(punit->type, F_IGTER)) maxd *= 3;
  if (maxd > THRESHOLD * 6) maxd = THRESHOLD * 6; /* limit of warmap */
  belli = unit_belligerence(punit) * maxd;
  *x = punit->x; *y = punit->y;

  generate_warmap(map_get_city(*x, *y), punit); /* most flexible but costs milliseconds */
/*  printf("%s's %s at (%d, %d) has belligerence %d.\n",
     pplayer->name, unit_types[punit->type].name, punit->x, punit->y, belli); */

  for (i = 0; i < game.nplayers; i++) {
    aplayer = &game.players[i];
    if (aplayer != pplayer) { /* enemy */
      city_list_iterate(aplayer->cities, acity)
        if ((is_ground_unit(punit) &&
                map_get_continent(acity->x, acity->y) == con &&
                warmap.cost[acity->x][acity->y] < maxd) ||
            (is_sailing_unit(punit) &&
                warmap.seacost[acity->x][acity->y] < maxd)) {
          if (pdef = get_defender(pplayer, punit, acity->x, acity->y))
            vul = unit_vulnerability(punit, pdef);
          else vul = 0;
/* probably ought to make empty cities less enticing. -- Syela */
          vul = (vul + kluge) * warmap.cost[acity->x][acity->y];
          if (vul < belli) {
            belli = vul;
            *x = acity->x;
            *y = acity->y;
          }
        }
      city_list_iterate_end;
/* I'm not sure the following code is good but it seems to be adequate. -- Syela */
      unit_list_iterate(aplayer->units, aunit)
        if (aunit == get_defender(pplayer, punit, aunit->x, aunit->y) &&
           ((is_ground_unit(punit) &&
                map_get_continent(aunit->x, aunit->y) == con &&
                warmap.cost[aunit->x][aunit->y] < maxd) ||
            (is_sailing_unit(punit) &&
                warmap.seacost[aunit->x][aunit->y] < maxd))) {
          vul = unit_vulnerability(punit, aunit);
          d = warmap.cost[aunit->x][aunit->y];
          d *= unit_types[aunit->type].move_rate;
          if (unit_flag(aunit->type, F_IGTER)) d *= 3;
          d /= unit_types[punit->type].move_rate;
          if (d < 1) d = 1;
/* this is experimental.  I'm worried about units chasing explorers and diplomats */
/* arguably should be ADDING the runaway-potential, but this seems to be better */
          vul = (vul + kluge) * d;
          if (vul < belli) {
            belli = vul;
            *x = aunit->x;
            *y = aunit->y;
          }
        }
      unit_list_iterate_end;
    } /* end if enemy */
  } /* end for all players */
/*  printf("%s's %s at (%d, %d) targeting (%d, %d) [%d / %d].\n", pplayer->name,
   unit_types[punit->type].name, punit->x, punit->y, *x, *y, belli,
   unit_belligerence(punit) * maxd); */

}
#endif

void ai_military_attack(struct player *pplayer,struct unit *punit)
{ /* rewritten by Syela - old way was crashy and not smart (nor is this) */
  int dest, dest_x, dest_y; 
  struct city *pcity;
  int id, flag, went, ct = 10;

  if (punit->activity!=ACTIVITY_GOTO) {
    id = punit->id;
    do {
      flag = 0;
      dest = ai_military_findvictim(pplayer, punit);  
      dest_x = dest % map.xsize;
      dest_y = dest / map.xsize;
      if (dest_x == punit->x && dest_y == punit->y) {
/* no one to bash here.  Will try to move onward */
        find_something_to_kill(pplayer, punit, &dest_x, &dest_y);
        if (same_pos(punit->x, punit->y, dest_x, dest_y)) {
/* nothing to kill.  Adjacency is something for us to kill later. */
          ai_manage_explorer(pplayer, punit); /* nothing else to do */
          return; /* Jane, stop this crazy thing! */
        } else if (!is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y)) {
/* if what we want to kill is adjacent, and findvictim didn't want it, WAIT! */
          if (went = ai_military_gothere(pplayer, punit, dest_x, dest_y)) {
            if (went > 0) flag = punit->moves_left;
            else punit = 0;
          } /* else we're having ZOC hell and need to break out of the loop */
        } /* else nothing to kill */
      } else { /* goto does NOT work for fast units */
/*        printf("%s's %s at (%d, %d) bashing (%d, %d)\n", pplayer->name,
unit_types[punit->type].name, punit->x, punit->y, dest_x, dest_y); */
        handle_unit_move_request(pplayer, punit, dest_x, dest_y);
        punit = find_unit_by_id(id);
        if (punit) flag = punit->moves_left; else flag = 0;
      }
      if (punit)
         if (should_unit_change_homecity(pplayer, punit)) return;
      ct--; /* infinite loops from railroads must be stopped */
    } while (flag && ct); /* want units to attack multiple times */
  } /* end if */
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
      if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
	auto_settler_do_goto(pplayer,punit, pcity->x, pcity->y);
        handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);
      } else {
	req.unit_id = punit->id;
	req.city_id = pcity->id;
	handle_unit_help_build_wonder(pplayer, &req);
      }
    }
  }
}

int amortize(int b, int d)
{
  int num = MORT - 1;
  int denom;
  int i = 0;
  int s = 1;
  assert(d >= 0);
  if (b < 0) { s = -1; b *= s; }
  while (d && b) {
    denom = 1;
    while (d >= 12 && !(b>>28) && !(denom>>27)) {
      b *= 3;          /* this is a kluge but it is 99.9% accurate and saves time */
      denom *= 5;      /* as long as MORT remains 24! -- Syela */
      i++;
      d -= 12;
    }
    while (!(b>>25) && d && !(denom>>25)) {
      b *= num;
      denom *= MORT;
      i++;
      d--;
    }
    if (i) {
      b = (b + (denom>>1)) / denom;
      i = 0;
    }
  }
  return(b * s);
}

void generate_minimap(struct player *pplayer)
{
  int a, i, j;

  memset(minimap, 0, sizeof(minimap));
  for (a = 0; a < game.nplayers; a++) {
    city_list_iterate(game.players[a].cities, pcity)
      city_map_iterate(i, j) {
        minimap[map_adjust_x(pcity->x+i-2)][map_adjust_y(pcity->y+j-2)] = 1;
      }
    city_list_iterate_end;
  }
  unit_list_iterate(pplayer->units, punit)
    if (punit->ai.ai_role == AIUNIT_BUILD_CITY) {
      city_map_iterate(i, j) {
        minimap[map_adjust_x(punit->goto_dest_x+i-2)][map_adjust_y(punit->goto_dest_y+j-2)] = 1;
      }
    }
  unit_list_iterate_end;
}

int city_desirability(int x, int y, int dist, struct player *pplayer)
{ /* this whole funct assumes G_REPUBLIC -- Syela */
  int taken[5][5], food[5][5], shield[5][5], trade[5][5];
  int irrig[5][5], mine[5][5], road[5][5];
  int i, j, f, n = 0;
  int worst, b2, best, ii, jj, val, cur;
  int d = 0;
  int a, i0, j0; /* need some temp variables */
  int temp, tmp;
  int debug = 0;
  int g = 1;
  struct tile *ptile;
  int con, con2;
  int har, t, ff, sh;
  struct tile_type *ptype;
  struct city *pcity;
  
  har = is_terrain_near_tile(x, y, T_OCEAN);

  ff = FOOD_WEIGHTING * MORT;
  sh = SHIELD_WEIGHTING * MORT;
  t = 8 * MORT - (8 * MORT * dist * 3 / (20 * 10));

  ptile = map_get_tile(x, y);
  if (ptile->terrain == T_OCEAN) return(0);
  pcity = map_get_city(x, y);
  if (pcity && pcity->size > 7) return(0);
  if (!pcity && minimap[x][y]) return(0);

  con = ptile->continent;

  city_list_iterate(pplayer->cities, acity)
    if (city_got_building(acity, B_PYRAMIDS)) g++;
  city_list_iterate_end;

  memset(taken, 0, sizeof(taken));
  memset(food, 0, sizeof(food));
  memset(shield, 0, sizeof(shield));
  memset(trade, 0, sizeof(trade));
  memset(irrig, 0, sizeof(irrig));
  memset(mine, 0, sizeof(mine));
  memset(road, 0, sizeof(road));

  city_map_iterate(i, j) {
    if ((!minimap[map_adjust_x(x+i-2)][map_adjust_y(y+j-2)] && !pcity) ||
       (pcity && get_worker_city(pcity, i, j) == C_TILE_EMPTY)) {
      ptile = map_get_tile(x+i-2, y+j-2);
      con2 = ptile->continent;
      ptype = get_tile_type(ptile->terrain);
      food[i][j] = ((ptile->special&S_SPECIAL ? ptype->food_special : ptype->food) - 2) * ff;
      if (i == 2 && j == 2) food[i][j] += 2 * ff;
      if (ptype->irrigation_result == ptile->terrain && con2 == con) {
        if (ptile->special&S_IRRIGATION || (i == 2 && j == 2)) irrig[i][j] = ff;
        else if (is_water_adjacent_to_tile(x+i-2, y+j-2) &&
                 ptile->terrain != T_HILLS) irrig[i][j] = ff - 165; /* KLUGE */
/* all of these kluges are hardcoded amortize calls to save much CPU use -- Syela */
      } else if (ptile->terrain == T_OCEAN && har) food[i][j] += ff; /* harbor */
      shield[i][j] = (ptile->special&S_SPECIAL ? ptype->shield_special : ptype->shield) * sh;
      if (i == 2 && j == 2 && !shield[i][j]) shield[i][j] = sh;
      if (ptile->special&S_MINE) mine[i][j] = (ptile->terrain == T_HILLS ? sh * 3 : sh);
      else if (ptile->terrain == T_HILLS && con2 == con) mine[i][j] = sh * 3 - 300; /* KLUGE */
      trade[i][j] =(ptile->special&S_SPECIAL ? ptype->trade_special : ptype->trade) * t;
      if (ptile->terrain == T_DESERT || ptile->terrain == T_GRASSLAND ||
        ptile->terrain == T_PLAINS) {
        if (ptile->special&S_ROAD || (i == 2 && j == 2)) road[i][j] = t;
        else if (con2 == con)
          road[i][j] = t - 70; /* KLUGE */ /* actualy exactly 70 1/2 */
      }
      if (trade[i][j]) trade[i][j] += t;
      else if (road[i][j]) road[i][j] += t;
    }
  }

  if (pcity) { /* quick-n-dirty immigration routine -- Syela */
    n = pcity->size;
    best = 0;
    city_map_iterate(i, j) {
      cur = (food[i][j] + 0 * irrig[i][j]) * 4 / n +
            (shield[i][j] + mine[i][j]) +
            (trade[i][j] + road[i][j]);
      if (cur > best && (i != 2 || j != 2)) { best = cur; ii = i; jj = j; }
    }
    if (!best) return(0);
    val = (shield[ii][jj] + mine[ii][jj]) +
          (food[ii][jj] + irrig[ii][jj]) + /* seems to be needed */
          (trade[ii][jj] + road[ii][jj]);
/*printf("Desire to immigrate to %s = %d -> %d\n", pcity->name, val,
(val * 100) / MORT / 70);*/
    return(val);
  }

  f = food[2][2] + irrig[2][2];
  if (!f) return(0); /* no starving cities, thank you! -- Syela */
  val = food[2][2] + irrig[2][2] + /* this feels wrong but works better */
          (shield[2][2] + mine[2][2]) +
          (trade[2][2] + road[2][2]);
  taken[2][2]++;
  /* val is mort times the real value */
  /* treating harbor as free to approximate advantage of building boats. -- Syela */
  val += (4 * (get_tile_type(map_get_tile(x, y)->terrain)->defense_bonus) - 40) * SHIELD_WEIGHTING;
/* don't build cities in danger!! FIX! -- Syela */
  val += 8 * MORT; /* one science per city */
if (debug)
printf("City value (%d, %d) = %d, har = %d, f = %d\n", x, y, val, har, f);

  for (n = 1; n <= 20 && f > 0; n++) {
    for (a = 1; a; a--) {
      best = 0; worst = -1; b2 = 0;
      city_map_iterate(i, j) {
        cur = (food[i][j] + 0 * irrig[i][j]) * 4 / n +
              (shield[i][j] + mine[i][j]) +
              (trade[i][j] + road[i][j]);
        if (!taken[i][j]) {
          if (cur > best) { b2 = best; best = cur; ii = i; jj = j; }
          else if (cur > b2) b2 = cur;
        } else if (i != 2 || j != 2) {
          if (cur < worst || worst < 0) { worst = cur; i0 = i; j0 = j; }
        }
      }
      if (!best) break;
      cur = amortize((shield[ii][jj] + mine[ii][jj]) +
            (food[ii][jj] + irrig[ii][jj]) + /* seems to be needed */
            (trade[ii][jj] + road[ii][jj]), d);
      f += food[ii][jj] + irrig[ii][jj];
      if (cur > 0) val += cur;
      taken[ii][jj]++;
if (debug)
printf("Value of (%d, %d) = %d food = %d, type = %s, n = %d, d = %d\n",
 ii, jj, cur, (food[ii][jj] + irrig[ii][jj]) / FOOD_WEIGHTING,
get_tile_type(map_get_tile(x + ii - 2, y + jj - 2)->terrain)->terrain_name, n, d);

/* I hoped to avoid the following, but it seems I can't take ANY shortcuts
in this unspeakable routine, so here comes even more CPU usage in order to
get this EXACTLY right instead of just reasonably close. -- Syela */
      if (worst < b2 && worst >= 0) {
        cur = amortize((shield[i0][j0] + mine[i0][j0]) +
              (trade[i0][j0] + road[i0][j0]), d);
        f -= (food[i0][j0] + irrig[i0][j0]) / FOOD_WEIGHTING;
        val -= cur;
        taken[i0][j0]--;
        a++;
if (debug)
printf("REJECTING Value of (%d, %d) = %d food = %d, type = %s, n = %d, d = %d\n",
 i0, j0, cur, (food[i0][j0] + irrig[i0][j0]) / FOOD_WEIGHTING,
get_tile_type(map_get_tile(x + i0 - 2, y + j0 - 2)->terrain)->terrain_name, n, d);
      }
    }
    if (!best) break;
    if (f > 0) d += (game.foodbox * MORT * n * FOOD_WEIGHTING + (f*g) - 1) / (f*g);
    if (n == 4) {
      val -= amortize(40 * SHIELD_WEIGHTING + (50 - 20 * g) * FOOD_WEIGHTING, d); /* lers */
      temp = amortize(40 * SHIELD_WEIGHTING, d); /* temple */
      tmp = val;
    }
  } /* end while */
  if (n > 4) {
    if (val - temp > tmp) val -= temp;
    else val = tmp;
  }
  val -= 110 * SHIELD_WEIGHTING; /* WAG: walls, defenders */

if (debug)
printf("Total value of (%d, %d) [%d workers] = %d\n", x, y, n, val);
  return(val);
}

/**************************************************************************
...
**************************************************************************/
void ai_manage_settler(struct player *pplayer, struct unit *punit)
{
  punit->ai.control = 1;
  if (punit->ai.ai_role == AIUNIT_NONE) /* if BUILD_CITY must remain BUILD_CITY */
    punit->ai.ai_role = AIUNIT_AUTO_SETTLER;
/* gonna handle city-building in the auto-settler routine -- Syela */
  return;
}
