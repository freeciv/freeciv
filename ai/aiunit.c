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
/*  printf("Managed units successfully.\n");  */
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
{
  int i, j, d, f, x, y, con, best, dest_x, dest_y, cur, a, b;
  if (punit->activity == ACTIVITY_IDLE) {
    x = punit->x; y = punit->y;
    con = map_get_continent(x, y);
    for (d = 1; d <= 24; d++) { /* won't go more than 24 squares to hut */
/* printf("Hut-hunting: D = %d\n", d); */
      for (i = 0 - d; i <= d; i++) {
        f = 1;
        if (i != 0 - d && i != d) f = d * 2; /* I was an idiot to forget this */
        for (j = 0 - d; j <= d; j += f) {
          if (map_get_tile(x + i, y + j)->special & S_HUT &&
          map_get_continent(x + i, y + j) == con) {
            punit->goto_dest_x = map_adjust_x(x + i);
            punit->goto_dest_y = y + j;
            punit->activity = ACTIVITY_GOTO;
            do_unit_goto(pplayer, punit);
            if (!punit->moves_left) return; /* otherwise, had some ZOC problem ? */
          } /* end if HUT */
        } /* end for J */
      } /* end for I */
    } /* end for D */
/* OK, failed to find huts.  Will explore basically at random */
/* my old code was dumb dumb dumb dumb dumb and I'm rewriting it -- Syela */

    d = 0; /* to allow the following important && - DUH */
    while (punit->moves_left && d < 24) { /* that && is VERY important - DUH */
      x = punit->x; y = punit->y;
      for (d = 1; d <= 24; d++) {
  /* printf("Exploring: D = %d\n", d); */
        best = 0; dest_x = 0; dest_y = 0;
        for (i = 0 - d; i <= d; i++) {
          f = 1;
          if (i != 0 - d && i != d) f = d * 2; /* I was an idiot to forget this */
          for (j = 0 - d; j <= d; j += f) {
            if (map_get_continent(x + i, y + j) == con) {
              cur = 0;
              for (a = i - 1; a <= i + 1; a++)
                for (b = j - 1; b <= j + 1; b++)
                  if (!map_get_known(x + a, y + b, pplayer)) cur++;
              if (cur > best || (cur == best && myrand(2))) {
                best = cur;
                dest_x = map_adjust_x(x + i);
                dest_y = y + j;
              }
            }
          } /* end j */
        } /* end i */
        if (best) {
          punit->goto_dest_x = dest_x;
          punit->goto_dest_y = dest_y;
          punit->activity = ACTIVITY_GOTO;
          if (d > 1) do_unit_goto(pplayer, punit); /* don't trust goto for d == 1 */
          else handle_unit_move_request(pplayer, punit, dest_x, dest_y);
          if (x != punit->x || y != punit->y) break; /* this ought to work, I hope */
        } /* end if best */
      } /* end for D */
    } /* end while we can move */
  } /* end if not already doing something */
}


void ai_manage_unit(struct player *pplayer, struct unit *punit) 
{
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
/* leaving. not changing to real_map_distance -- Syela */
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
/* leaving. not changing to real_map_distance -- Syela */
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

  if (!can_unit_change_homecity(punit)) {
/*    printf("%s (%d,%d) can't change homecity.\n", 
        get_unit_type(punit->type)->name, punit->x, punit->y); */
    return(0);
  } /* else printf("%s (%d,%d) can change homecity.\n",
        get_unit_type(punit->type)->name, punit->x, punit->y); */


  if (pcity = map_get_city(punit->x, punit->y)) {
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
  }

  if (punit = find_unit_by_id(id)) {
    if (punit->moves_left) { /* didn't move, or reached goal */
      handle_unit_activity_request(pplayer, punit, ACTIVITY_FORTIFY);
    } /* better than doing nothing */
  }
}


int ai_military_findvictim(struct player *pplayer,struct unit *punit)
{ /* work of Syela - mostly to fix the ZOC/goto strangeness */
  int xx[3], yy[3], x, y;
  int ii[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int jj[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int i, j, k, weakest, v;
  int dest;
  struct unit *pdef;
  struct city *pcity;
  x = punit->x;
  y = punit->y;
  dest = y * map.xsize + x;
  v = get_attack_power(punit) * punit->hp * 
            get_unit_type(punit->type)->firepower;
  weakest = v * v / get_unit_type(punit->type)->build_cost + 2;
/* kluge to prevent stacks of fortified, stuck units */
  unit_list_iterate(map_get_tile(x,y)->units, pdef)
    if (pdef->activity == ACTIVITY_FORTIFY && !map_get_city(x,y)) weakest = 2000000000;
  unit_list_iterate_end;
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
        v = get_total_defense_power(punit, pdef) * pdef->hp * 
                 get_unit_type(pdef->type)->firepower *
             get_total_defense_power(punit, pdef) * pdef->hp * 
                 get_unit_type(pdef->type)->firepower /
                 get_unit_type(pdef->type)->build_cost;
        if (map_get_city(xx[i], yy[j])) v /= 2; /* prefer to bash cities */
        if (map_get_city(x, y) && /* pikemen defend Knights, attack Catapults */
              get_total_defense_power(pdef, punit) *
              get_total_defense_power(punit, pdef) >
              get_total_attack_power(pdef, punit) *
              get_total_attack_power(punit, pdef) &&
              get_total_attack_power(pdef, punit)) ;
/*              printf("%s defending %s from %s's %s\n",
            get_unit_type(punit->type)->name, map_get_city(x, y)->name,
            game.players[pdef->owner].name, get_unit_type(pdef->type)->name); */
        else if (v < weakest) {
/*          printf("Better than %d is %d (%s)\n", weakest, v,
             unit_types[pdef->type].name);  */
          weakest = v; dest = yy[j] * map.xsize + xx[i];
         } /* else printf("NOT better than %d is %d (%s)\n", weakest, v,
             unit_types[pdef->type].name);   */
      } else { /* no pdef */
        pcity = map_get_city(xx[i], yy[j]);
        if (pcity) {
          if (pcity->owner != pplayer->player_no) { /* free goodies */
            weakest = -1; dest = yy[j] * map.xsize + xx[i];
          }
        }
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
  return get_defense_power(punit) * punit->hp / get_attack_power(punit) /
            get_unit_type(punit->type)->move_rate;
}

void ai_military_findjob(struct player *pplayer,struct unit *punit)
{
  struct city *pcity;
  struct unit *pdef;
  int val;

/* tired of AI abandoning its cities! -- Syela */
  if (punit->homecity) {
    pcity = find_city_by_id(punit->homecity);
    if (punit->x == pcity->x && punit->y == pcity->y) /* I'm home! */
      val = unit_defensiveness(punit);
    else val = -1;
    unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef)
      if (unit_defensiveness(punit) >= val &&
            is_military_unit(pdef) && pdef != punit) val = 0;
    unit_list_iterate_end; /* was getting confused without the is_military part in */
    if (val) { /* Guess I better stay / you can live at home now */
      punit->ai.ai_role=AIUNIT_DEFEND_HOME;
      return;
    }
  }

/* ok, what if I'm somewhere new? - ugly, kludgy code by Syela */
  if (should_unit_change_homecity(pplayer, punit)) {
    return;
  }

/* here follows the old code */
if (ai_military_findtarget(pplayer,punit) || punit->unhappiness)
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
int dest, dest_x, dest_y;
if (punit->homecity)
   {
   pcity=find_city_by_id(punit->homecity);
/*   printf("GOHOME
(%d)(%d,%d)C(%d,%d)\n",punit->id,punit->x,punit->y,pcity->x,pcity->y); */
   if ((punit->x == pcity->x)&&(punit->y == pcity->y))
      {
/*      printf("INHOUSE. GOTO AI_NONE(%d)\n", punit->id); */
      punit->ai.ai_role=AIUNIT_NONE;
/* aggro defense goes here -- Syela */
      dest = ai_military_findvictim(pplayer, punit);
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

void ai_military_attack(struct player *pplayer,struct unit *punit)
{ /* rewritten by Syela - old way was crashy and not smart (nor is this) */
  int dest, dest_x, dest_y; 
  struct city *pcity;
  int id, flag, went;

  if (punit->activity!=ACTIVITY_GOTO) {
    id = punit->id;
    do {
      flag = 0;
      dest = ai_military_findvictim(pplayer, punit);  
      dest_x = dest % map.xsize;
      dest_y = dest / map.xsize;
      if (dest_x == punit->x && dest_y == punit->y) {
/* no one to bash here.  Will try to move onward */
        if (pcity=dist_nearest_enemy_city(pplayer,punit->x,punit->y)) {
          if (!is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y)) {
            dest_x = pcity->x; dest_y = pcity->y;
            if (went = ai_military_gothere(pplayer, punit, dest_x, dest_y)) {
              if (went > 0) flag = punit->moves_left;
              else punit = 0;
            } /* else we're having ZOC hell and need to break out of the loop */
          } /* else printf("Adjacency.\n"); */
        }
      } else { /* goto does NOT work for fast units */
        handle_unit_move_request(pplayer, punit, dest_x, dest_y);
        punit = find_unit_by_id(id);
        if (punit) flag = punit->moves_left; else flag = 0;
      }
      if (punit)
         if (should_unit_change_homecity(pplayer, punit)) return;
    } while (flag); /* want units to attack multiple times */
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









