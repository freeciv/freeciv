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
#include <settlers.h>
#include <cityturn.h>

extern struct move_cost_map warmap;

int unit_move_turns(struct unit *punit, int x, int y)
{
  int m, d;
  m = unit_types[punit->type].move_rate;
  if (unit_flag(punit->type, F_IGTER)) m *= 3;
  if(is_sailing_unit(punit)) {
    struct player *pplayer = get_player(punit->owner);
    if (player_owns_active_wonder(pplayer, B_LIGHTHOUSE)) 
      m += 3;
    if (player_owns_active_wonder(pplayer, B_MAGELLAN))
      m += 6;
    if (get_invention(pplayer, A_POWER) == TECH_KNOWN)
      m += 3;
  }   

  if (unit_types[punit->type].move_type == LAND_MOVING)
    d = warmap.cost[x][y] / m;
  else if (unit_types[punit->type].move_type == SEA_MOVING)
    d = warmap.seacost[x][y] / m;
  else d = real_map_distance(punit->x, punit->y, x, y) * 3 / m;
  return(d);
}

int tile_is_accessible(struct unit *punit, int x, int y)
{ /* is there any hope of reaching this tile without violating ZOC? */
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int k;
  if (!is_ground_unit(punit) || unit_flag(punit->type, F_IGZOC)) return 1;
  if (is_my_zoc(punit, x, y)) return 1;
  for (k = 0; k < 8; k++)
    if (map_get_terrain(x+ii[k], y+jj[k]) != T_OCEAN &&
        is_my_zoc(punit, x + ii[k], y + jj[k])) return 1;
  return 0;
}
 
/**************************************************************************
... do something sensible with the unit...
**************************************************************************/

void ai_manage_explorer(struct player *pplayer, struct unit *punit)
{ /* this is smarter than the old one, but much slower too. -- Syela */
  int i, j, d, f, x, y, con, dest_x=0, dest_y=0, best, lmt, cur, a, b;
  int ok, ct = punit->moves_left + 3;
  struct city *pcity;
  int id = punit->id; /* we can now die because easy AI may accidently
			 stumble on huts it fuzzily ignored */

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);

  x = punit->x; y = punit->y;
  if (is_ground_unit(punit)) con = map_get_continent(x, y);
  else con = 0; /* Thanks, Tony */

  lmt = (pplayer->ai.control ? 2 * THRESHOLD : 3);
  best = lmt * 3 + 1;

  generate_warmap(map_get_city(x, y), punit); /* CPU-expensive but worth it -- Syela */
/* otherwise did not understand about bays and did really stupid things! */

/* BEGIN PART ONE: Look for huts.  Ground units ONLY. */
  if (is_ground_unit(punit)) { /* boats don't hunt huts */
    for (d = 1; d <= lmt; d++) {
      for (i = 0 - d; i <= d; i++) {
        f = 1;
        if (i != 0 - d && i != d) f = d * 2; /* I was an idiot to forget this */
        for (j = 0 - d; j <= d; j += f) {
          if (map_get_tile(x + i, y + j)->special & S_HUT &&
          warmap.cost[map_adjust_x(x + i)][y + j] < best &&
          (!ai_handicap(pplayer, H_HUTS) || map_get_known(x+i, y+j, pplayer)) &&
          map_get_continent(x + i, y + j) == con &&
          tile_is_accessible(punit, x+i, y+j) && ai_fuzzy(pplayer,1)) {
            dest_x = map_adjust_x(x + i);
            dest_y = y + j;
            best = warmap.cost[dest_x][dest_y];
          }
        }
      }
    } /* end for D */
  }
  if (best <= lmt * 3) {
    punit->goto_dest_x = dest_x;
    punit->goto_dest_y = dest_y;
    punit->activity = ACTIVITY_GOTO;
    do_unit_goto(pplayer, punit);
    return; /* maybe have moves left but I want to return anyway. */
  }

/* END PART ONE */

/* BEGIN PART TWO: Move into unexplored territory */

/* OK, failed to find huts.  Will explore basically at random */
/* my old code was dumb dumb dumb dumb dumb and I'm rewriting it -- Syela */

  while (best && punit->moves_left && ct) {
    best = 0;
    x = punit->x; y = punit->y;
    for (i = -1; i <= 1; i++) {
      for (j = -1; j <= 1; j++) {
        ok = (unit_flag(punit->type, F_TRIREME) ? 0 : 1);
        if (map_get_continent(x + i, y + j) == con &&
        !is_enemy_unit_tile(x + i, y + j, punit->owner) &&
        !map_get_city(x + i, y + j)) {
          cur = 0;
          for (a = i - 1; a <= i + 1; a++) {
            for (b = j - 1; b <= j + 1; b++) {
              if (!map_get_known(x + a, y + b, pplayer)) {
                cur++;
                if (is_sailing_unit(punit)) {
                  if (map_get_continent(x+a, y+b) > 2) cur++;
                }
              }
              if (!ok && map_get_terrain(x+a, y+b) != T_OCEAN) ok++;
            }
          }
          if (ok && (cur > best || (cur == best && myrand(2))) &&
              could_unit_move_to_tile(punit, punit->x, punit->y, x+i, y+j)) {
            dest_x = map_adjust_x(x + i);
            dest_y = y + j;
            best = cur;
          }
        }
      } /* end j */
    } /* end i */
    if (best) {
      handle_unit_move_request(pplayer, punit, dest_x, dest_y);
      if(!unit_list_find(&pplayer->units, id)) return; /* died */
    }
    ct--; /* trying to avoid loops */
  }
  if (!punit->moves_left) return;
/* END PART TWO */

/* BEGIN PART THREE: Go towards unexplored territory */
/* no adjacent squares help us to explore - really slow part follows */
  best = 0;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      if (map_get_continent(x, y) == con && map_get_known(x, y, pplayer) &&
         !is_enemy_unit_tile(x, y, punit->owner) && !map_get_city(x, y) &&
         tile_is_accessible(punit, x, y)) {
        cur = 0;
        for (a = -1; a <= 1; a++)
          for (b = -1; b <= 1; b++)
            if (!map_get_known(x + a, y + b, pplayer)) cur++;
        if (cur) {
          cur += 9 * (THRESHOLD - unit_move_turns(punit, x, y));
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
/* END PART THREE */

/*  printf("%s's %s at (%d,%d) failed to explore.\n", pplayer->name,
    unit_types[punit->type].name, punit->x, punit->y); */
  handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);
  if (pplayer->ai.control && is_military_unit(punit)) {
    pcity = find_city_by_id(punit->homecity);
    if (pcity && map_get_continent(pcity->x, pcity->y) == con)
      ai_military_gohome(pplayer, punit);
    else if (pcity) {
      if (!find_boat(pplayer, &x, &y, 0)) /* Gilligan's Island */
        punit->ai.ferryboat = -1;
      else {
        punit->goto_dest_x = x;
        punit->goto_dest_y = y;
        do_unit_goto(pplayer, punit);
      }
    }
  }
/*  if (!is_military_unit(punit)) wipe_unit(pplayer, punit); */
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
      if (assess_defense_unit(pcity, punit, 0) >= val &&
         is_military_unit(pdef) && pdef != punit) val = 0;
    unit_list_iterate_end;
 
/* printf("Incity at (%d,%d).  Val = %d.\n", punit->x, punit->y, val); */
   if (val) { /* Guess I better stay / you can live at home now */
      punit->ai.ai_role=AIUNIT_DEFEND_HOME;

      punit->ai.charge = 0; /* Very important, or will not stay -- Syela */
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

int unit_belligerence_primitive(struct unit *punit)
{
  int v;
  v = get_attack_power(punit) * punit->hp * 
            get_unit_type(punit->type)->firepower / 30;
  return(v);
}

int unit_belligerence_basic(struct unit *punit)
{
  int v;
  v = unit_types[punit->type].attack_strength * (punit->veteran ? 15 : 10) * 
          punit->hp * get_unit_type(punit->type)->firepower / 30;
  return(v);
}

int unit_belligerence(struct unit *punit)
{
  int v = unit_belligerence_basic(punit);
  return(v * v);
}

int unit_vulnerability_basic(struct unit *punit, struct unit *pdef)
{
  int v;

  v = get_total_defense_power(punit, pdef) * 
      (punit->id ? pdef->hp : unit_types[pdef->type].hp) * 
           get_unit_type(pdef->type)->firepower / 30;

  return(v);
}

int unit_vulnerability_virtual(struct unit *punit)
{
  int v;
  v = get_unit_type(punit->type)->defense_strength *
      (punit->veteran ? 15 : 10) * punit->hp / 30;
  return(v * v);
}

int unit_vulnerability(struct unit *punit, struct unit *pdef)
{
  int v = unit_vulnerability_basic(punit, pdef);
  return (v * v);
}

int stack_attack_value(int x, int y)
{
  int val = 0;
  struct tile *ptile = map_get_tile(x, y);
  unit_list_iterate(ptile->units, aunit)
    val += unit_belligerence_basic(aunit);
  unit_list_iterate_end;
  return(val);
}

void invasion_funct(struct unit *punit, int dest, int n, int which)
{ 
  int i, j;
  struct city *pcity;
  int x, y;
  if (dest) { x = punit->goto_dest_x; y = punit->goto_dest_y; }
  else { x = punit->x; y = punit->y; }
  for (j = y - n; j <= y + n; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - n; i <= x + n; i++) {
      pcity = map_get_city(i, j);
      if (pcity && pcity->owner != punit->owner)
        if (dest || punit->activity != ACTIVITY_GOTO || !assess_defense(pcity))
          pcity->ai.invasion |= which;
    }
  }
}

int reinforcements_value(struct unit *punit, int x, int y)
{ /* this is still pretty dopey but better than the original -- Syela */
  int val = 0, i, j;
  struct tile *ptile;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      ptile = map_get_tile(i, j);
      unit_list_iterate(ptile->units, aunit)
        if (aunit == punit || aunit->owner != punit->owner) continue;
        val += (unit_belligerence_basic(aunit));
      unit_list_iterate_end;
    }
  }
  return(val);
}

int reinforcements_cost(struct unit *punit, int x, int y)
{ /* I might rather have one function which does this and the above -- Syela */
  int val = 0, i, j;
  struct tile *ptile;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      ptile = map_get_tile(i, j);
      unit_list_iterate(ptile->units, aunit)
        if (aunit == punit || aunit->owner != punit->owner) continue;
        if (unit_belligerence_basic(aunit))
          val += unit_types[aunit->type].build_cost;
      unit_list_iterate_end;
    }
  }
  return(val);
}

int is_my_turn(struct unit *punit, struct unit *pdef)
{
  int val = unit_belligerence_primitive(punit), i, j, cur, d;
  struct tile *ptile;
  for (j = pdef->y - 1; j <= pdef->y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = pdef->x - 1; i <= pdef->x + 1; i++) {
      ptile = map_get_tile(i, j);
      unit_list_iterate(ptile->units, aunit)
        if (aunit == punit || aunit->owner != punit->owner) continue;
        if (!can_unit_attack_unit_at_tile(aunit, pdef, pdef->x, pdef->y)) continue;
        d = get_virtual_defense_power(aunit->type, pdef->type, pdef->x, pdef->y);
        if (!d) return 1; /* Thanks, Markus -- Syela */
        cur = unit_belligerence_primitive(aunit) *
              get_virtual_defense_power(punit->type, pdef->type, pdef->x, pdef->y) / d;
        if (cur > val && ai_fuzzy(get_player(punit->owner),1)) return(0);
      unit_list_iterate_end;
    }
  }
  return(1);
}

int ai_military_findvictim(struct player *pplayer, struct unit *punit, int *dest_x, int *dest_y)
{ /* work of Syela - mostly to fix the ZOC/goto strangeness */
  int xx[3], yy[3], x, y;
  int ii[8] = { 0, 1, 2, 0, 2, 0, 1, 2 };
  int jj[8] = { 0, 0, 0, 1, 1, 2, 2, 2 };
  int i, j, k;
  int best = 0, a, b, c, d, e, f;
  struct unit *pdef;
  struct unit *patt;
  struct city *pcity;
  x = punit->x;
  y = punit->y;
  *dest_y = y;
  *dest_x = x;
  if (punit->unhappiness) best = 0 - 2 * MORT * TRADE_WEIGHTING; /* desperation */
  f = unit_types[punit->type].build_cost;
  c = 0; /* only dealing with adjacent victims here */

  if (get_transporter_capacity(punit)) {
    unit_list_iterate(map_get_tile(x, y)->units, aunit)
      if (!is_sailing_unit(aunit)) return(0);
    unit_list_iterate_end;
  } /* ferryboats do not attack.  no. -- Syela */

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
      patt = get_attacker(pplayer, punit, xx[i], yy[j]);
/* horsemen in city refused to attack phalanx just outside that was
bodyguarding catapult - patt will resolve this bug nicely -- Syela */
      if (can_unit_attack_tile(punit, xx[i], yy[j])) { /* thanks, Roar */
        d = unit_vulnerability(punit, pdef);
        if (map_get_city(x, y) && /* pikemen defend Knights, attack Catapults */
              get_total_defense_power(pdef, punit) *
              get_total_defense_power(punit, pdef) >= /* didn't like > -- Syela */
              get_total_attack_power(patt, punit) * /* patt, not pdef */
              get_total_attack_power(punit, pdef) &&
              unit_list_size(&(map_get_tile(punit->x, punit->y)->units)) < 2 &&
              get_total_attack_power(patt, punit)) ;
/*              printf("%s defending %s from %s's %s\n",
            get_unit_type(punit->type)->name, map_get_city(x, y)->name,
            game.players[pdef->owner].name, get_unit_type(pdef->type)->name); */
        else {
          a = reinforcements_value(punit, pdef->x, pdef->y);
          a += unit_belligerence_primitive(punit);
          a *= a;
          b = unit_types[pdef->type].build_cost;
          if (map_get_city(xx[i], yy[j])) /* bonus restored 980804 -- Syela */
            b = (b + 40) * punit->hp / unit_types[punit->type].hp;
/* c is always equal to zero in this routine, and f is already known */
/* arguable that I should use reinforcement_cost here?? -- Syela */
          if (a && is_my_turn(punit, pdef)) {
            e = ((b * a - f * d) * SHIELD_WEIGHTING / (a + d) - c * SHIELD_WEIGHTING);
/* no need to amortize! */
            if (e > best && ai_fuzzy(pplayer,1)) {
/*printf("Better than %d is %d (%s)\n", best, e, unit_types[pdef->type].name);*/
              best = e; *dest_y = yy[j]; *dest_x = xx[i];
            } /*else printf("NOT better than %d is %d (%s)\n", best, e,
               unit_types[pdef->type].name);*/
          } /* end if we have non-zero belligerence */
        }
      }
    } else { /* no pdef */
      pcity = map_get_city(xx[i], yy[j]);
      if (pcity && is_ground_unit(punit) &&
          map_get_terrain(punit->x, punit->y) != T_OCEAN) {
        if (pcity->owner != pplayer->player_no) { /* free goodies */
          best = 99999; *dest_y = yy[j]; *dest_x = xx[i];
        }
      }
      if (map_get_tile(xx[i], yy[j])->special & S_HUT && best < 99999 &&
          could_unit_move_to_tile(punit, punit->x, punit->y, xx[i], yy[j]) &&
/*          zoc_ok_move(punit, xx[i], yy[j]) && !is_sailing_unit(punit) &&*/
          punit->ai.ai_role != AIUNIT_ESCORT && /* makes life easier */
          !punit->ai.charge && /* above line seems not to work. :( */
          punit->ai.ai_role != AIUNIT_DEFEND_HOME) { /* Oops! -- Syela */
        best = 99998; *dest_y = yy[j]; *dest_x = xx[i];
      }
    }
  }
  return(best);
}

void ai_military_bodyguard(struct player *pplayer, struct unit *punit)
{
  struct unit *aunit = unit_list_find(&pplayer->units, punit->ai.charge);
  struct city *acity = find_city_by_id(punit->ai.charge);
  int x, y, id = punit->id, i;

  if (aunit && aunit->owner == punit->owner) { x = aunit->x; y = aunit->y; }
  else if (acity && acity->owner == punit->owner) { x = acity->x; y = acity->y; }
  else { /* should be impossible */
    punit->ai.charge = 0;
    return;
  }
  
/*if (aunit) printf("%s#%d@(%d,%d) -> %s#%d@(%d,%d) [body=%d]\n",
unit_types[punit->type].name, punit->id, punit->x, punit->y,
unit_types[aunit->type].name, aunit->id, aunit->x, aunit->y,
aunit->ai.bodyguard);*/

  if (!same_pos(punit->x, punit->y, x, y)) {
    if (goto_is_sane(pplayer, punit, x, y, 1)) {
      punit->goto_dest_x = x;
      punit->goto_dest_y = y;
      do_unit_goto(pplayer, punit);
    } else punit->ai.charge = 0; /* can't possibly get there to help */
  } else { /* I had these guys set to just fortify, which is so dumb. -- Syela */
    i = ai_military_findvictim(pplayer, punit, &x, &y);
/*printf("Stationary escort @(%d,%d) received %d best @(%d,%d)\n", punit->x, punit->y, i, x, y);*/
    if (i >= 40 * SHIELD_WEIGHTING)
      handle_unit_move_request(pplayer, punit, x, y);
/* otherwise don't bother, but free cities are free cities and must be snarfed. -- Syela */
  }
  if (aunit && unit_list_find(&map_get_tile(x, y)->units, id) && aunit->ai.bodyguard)
    aunit->ai.bodyguard = id;
}

int find_beachhead(struct unit *punit, int dest_x, int dest_y, int *x, int *y)
{
  int ii[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  int jj[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  int i, j, k, l, ok, t, fu;
  int best = 0;
  for (k = 0; k < 8; k++) {
    i = map_adjust_x(dest_x + ii[k]);
    j = map_adjust_y(dest_y + jj[k]);
    ok = 0; fu = 0;
    t = map_get_terrain(i, j);
    if (warmap.seacost[i][j] <= 6 * THRESHOLD && t != T_OCEAN) { /* accessible beachhead */
      for (l = 0; l < 8 && !ok; l++) {
        if (map_get_terrain(i + ii[l], j + jj[l]) == T_OCEAN &&
            is_my_zoc(punit, i + ii[l], j + jj[l])) ok++;
        if (map_get_terrain(i + ii[l], j + jj[l]) == T_OCEAN) fu++;
      }
      if (ok) { /* accessible beachhead with zoc-ok water tile nearby */
        ok = get_tile_type(t)->defense_bonus;
        if (get_tile_type(t)->movement_cost * 3 <
            unit_types[punit->type].move_rate) ok *= 8;
        ok += (6 * THRESHOLD - warmap.seacost[i][j]);
        if (ok > best) { best = ok; *x = i; *y = j; }
      }
    }
  }

  return(best);
}

int ai_military_gothere(struct player *pplayer, struct unit *punit, int dest_x, int dest_y)
{
  int id, x, y, boatid = 0, bx, by, i, j;
  struct unit *ferryboat;
  struct unit *def;
  struct city *dcity;
  struct tile *ptile;
  int d_type, d_val = 0;

  id = punit->id; x = punit->x; y = punit->y;

  if (is_ground_unit(punit)) boatid = find_boat(pplayer, &bx, &by, 2);
  ferryboat = unit_list_find(&(map_get_tile(x, y)->units), boatid);

  if (!(dest_x == x && dest_y == y)) {

/* do we require protection? */
    d_val = stack_attack_value(dest_x, dest_y) * 30;
    if ((dcity = map_get_city(dest_x, dest_y))) {
      d_type = ai_choose_defender_versus(dcity, punit->type);
      j = unit_types[d_type].hp * (do_make_unit_veteran(dcity, d_type) ? 15 : 10) *
          unit_types[d_type].attack_strength;
      d_val += j;
    }
    d_val /= (unit_types[punit->type].move_rate / 3);
    if (unit_flag(punit->type, F_IGTER)) d_val /= 1.5;
/*printf("%s@(%d,%d) looking for bodyguard, d_val=%d, my_val=%d\n",
unit_types[punit->type].name, punit->x, punit->y, d_val, punit->hp *
(punit->veteran ? 15 : 10) * unit_types[punit->type].defense_strength);*/
    ptile = map_get_tile(punit->x, punit->y);
    if (d_val >= punit->hp * (punit->veteran ? 15 : 10) *
                unit_types[punit->type].defense_strength) {
/*      if (!unit_list_find(&pplayer->units, punit->ai.bodyguard))      Ugggggh */
      if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
        punit->ai.bodyguard = -1;
    } else if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
      punit->ai.bodyguard = 0;
/* end protection subroutine */

    if (!goto_is_sane(pplayer, punit, dest_x, dest_y, 1) ||
       (ferryboat && goto_is_sane(pplayer, ferryboat, dest_x, dest_y, 1))) { /* Important!! */
      punit->ai.ferryboat = boatid;
/*printf("%s: %d@(%d, %d): Looking for BOAT (id=%d).\n", pplayer->name, punit->id, punit->x, punit->y, boatid);*/
      if (!same_pos(x, y, bx, by)) {
        punit->goto_dest_x = bx;
        punit->goto_dest_y = by;
        punit->activity = ACTIVITY_GOTO;
        do_unit_goto(pplayer,punit);
        if (!unit_list_find(&pplayer->units, id)) return(-1); /* died */
      }
      ptile = map_get_tile(punit->x, punit->y);
      ferryboat = unit_list_find(&ptile->units, punit->ai.ferryboat);
      if (ferryboat && (!ferryboat->ai.passenger ||
          ferryboat->ai.passenger == punit->id)) {
/*printf("We have FOUND BOAT, %d ABOARD %d@(%d,%d)->(%d, %d).\n", punit->id,
ferryboat->id, punit->x, punit->y, dest_x, dest_y);*/
        set_unit_activity(punit, ACTIVITY_SENTRY); /* kinda cheating -- Syela */ 
        ferryboat->ai.passenger = punit->id;
/* the code that worked for settlers wasn't good for piles of cannons */
        if (find_beachhead(punit, dest_x, dest_y, &ferryboat->goto_dest_x,
               &ferryboat->goto_dest_y)) {
/*  set_unit_activity(ferryboat, ACTIVITY_GOTO);   -- Extremely bad!! -- Syela */
          punit->goto_dest_x = dest_x;
          punit->goto_dest_y = dest_y;
          set_unit_activity(punit, ACTIVITY_SENTRY); /* anything but GOTO!! */
          if (!is_transporter_with_free_space(pplayer, punit->x, punit->y)) {
/*printf("All aboard!\n");*/
/* perhaps this should only require two passengers */
            unit_list_iterate(ptile->units, mypass)
              if (mypass->ai.ferryboat == ferryboat->id) {
                set_unit_activity(mypass, ACTIVITY_SENTRY);
                def = unit_list_find(&ptile->units, mypass->ai.bodyguard);
                if (def) set_unit_activity(def, ACTIVITY_SENTRY);
              }
            unit_list_iterate_end; /* passengers are safely stowed away */
            do_unit_goto(pplayer, ferryboat);
            set_unit_activity(punit, ACTIVITY_IDLE);
          } /* else wait, we can GOTO later. */
        }
      } 
    }
    if (goto_is_sane(pplayer, punit, dest_x, dest_y, 1) && punit->moves_left &&
       ((!ferryboat) || 
       (real_map_distance(punit->x, punit->y, dest_x, dest_y) < 3 &&
       (!punit->ai.bodyguard || unit_list_find(&(map_get_tile(punit->x,
       punit->y)->units), punit->ai.bodyguard) || (dcity &&
       !assess_defense(dcity)))))) {
/* if we are on a boat, disembark only if we are within two tiles of
our target, and either 1) we don't need a bodyguard, 2) we have a
bodyguard, or 3) we are going to an empty city.  Previously, cannons
would disembark before the cruisers arrived and die. -- Syela */

      punit->goto_dest_x = dest_x;
      punit->goto_dest_y = dest_y;

/* The following code block is supposed to stop units from running away from their
bodyguards, and not interfere with those that don't have bodyguards nearby -- Syela */
/* The case where the bodyguard has moves left and could meet us en route is not
handled properly.  There should be a way to do it with dir_ok but I'm tired now. -- Syela */
      if (punit->ai.bodyguard < 0) { 
       for (i = punit->x - 1; i <= punit->x + 1; i++) {
          for (j = punit->y - 1; j <= punit->y + 1; j++) {
            if (i == punit->x && j == punit->y) continue; /* I'm being lazy -- Syela */
            unit_list_iterate(map_get_tile(i, j)->units, aunit)
              if (aunit->ai.charge == punit->id) {
/*printf("Bodyguard at (%d, %d) is adjacent to (%d, %d)\n", i, j, punit->x, punit->y);*/
                if (aunit->moves_left) return(0);
                else return(handle_unit_move_request(pplayer, punit, i, j));
              }
            unit_list_iterate_end;
          } /* end j */
        } /* end i */
      } /* end if */
/* end 'short leash' subroutine */

/*if (ferryboat) printf("GOTHERE: %s#%d@(%d,%d)->(%d,%d)\n", 
unit_types[punit->type].name, punit->id, punit->x, punit->y, dest_x, dest_y);*/
      punit->activity = ACTIVITY_GOTO;
      do_unit_goto(pplayer,punit);
/* liable to bump into someone that will kill us.  Should avoid? */
    } /*else printf("%s#%d@(%d,%d) not moving -> (%d, %d)\n",
unit_types[punit->type].name, punit->id, punit->x, punit->y, dest_x, dest_y);*/
  }
  if (unit_list_find(&pplayer->units, id)) { /* didn't die */
    punit->ai.ai_role = AIUNIT_NONE; /* in case we need to change */
    if (x != punit->x || y != punit->y) return 1; /* moved */
    else return 0; /* didn't move, didn't die */
  }
  return(-1); /* died */
}

int unit_can_defend(int type)
{
  if (unit_types[type].move_type != LAND_MOVING) return 0; /* temporary kluge */
  if (unit_types[type].defense_strength * 
      (unit_types[type].hp > 10 ? 5 : 3) >=
      unit_types[type].attack_strength * 4 &&
      !unit_types[type].transport_capacity &&
      !unit_flag(type, F_NONMIL)) return 1;
  return 0;
}

int look_for_charge(struct player *pplayer, struct unit *punit, struct unit **aunit, struct city **acity)
{
  int d, def, val = 0, u;
  u = unit_vulnerability_virtual(punit);
  if (!u) return(0);
  unit_list_iterate(pplayer->units, buddy)
    if (!buddy->ai.bodyguard) continue;
    if (!goto_is_sane(pplayer, punit, buddy->x, buddy->y, 1)) continue;
    if (unit_types[buddy->type].move_rate > unit_types[punit->type].move_rate) continue;
    if (unit_types[buddy->type].move_type != unit_types[punit->type].move_type) continue;
    d = unit_move_turns(punit, buddy->x, buddy->y);
    def = (u - unit_vulnerability_virtual(buddy))>>d;
/*printf("(%d,%d)->(%d,%d), %d turns, def=%d\n",
punit->x, punit->y, buddy->x, buddy->y, d, def);*/
    unit_list_iterate(pplayer->units, body)
      if (body->ai.charge == buddy->id) def = 0;
    unit_list_iterate_end;
    if (def > val && ai_fuzzy(pplayer,1)) { *aunit = buddy; val = def; }
  unit_list_iterate_end;
  city_list_iterate(pplayer->cities, mycity)
    if (!goto_is_sane(pplayer, punit, mycity->x, mycity->y, 1)) continue;
    if (!mycity->ai.urgency) continue;
    d = unit_move_turns(punit, mycity->x, mycity->y);
    def = (mycity->ai.danger - assess_defense_quadratic(mycity))>>d;
    if (def > val && ai_fuzzy(pplayer,1)) { *acity = mycity; val = def; }
  city_list_iterate_end;
/*printf("%s@(%d,%d) looking for charge; %d/%d\n", unit_types[punit->type].name,
punit->x, punit->y, val, val * 100 / u);*/
  return((val * 100) / u);
}

void ai_military_findjob(struct player *pplayer,struct unit *punit)
{
  struct city *pcity = NULL, *acity = NULL;
  struct unit *aunit;
  int val;
  int d = 0, def = 0;
  int q = 0;

/* tired of AI abandoning its cities! -- Syela */
  if (punit->homecity && (pcity = find_city_by_id(punit->homecity))) {
    if (pcity->ai.danger) { /* otherwise we can attack */
      def = assess_defense(pcity);
      if (punit->x == pcity->x && punit->y == pcity->y) { /* I'm home! */
        val = assess_defense_unit(pcity, punit, 0); 
        def -= val; /* old bad kluge fixed 980803 -- Syela */
/* only the least defensive unit may leave home */
/* and only if this does not jeopardize the city */
/* def is the defense of the city without punit */
        if (unit_flag(punit->type, F_FIELDUNIT)) val = -1;
        unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, pdef)
          if (is_military_unit(pdef) && pdef != punit &&
              !unit_flag(pdef->type, F_FIELDUNIT)) {
            if (assess_defense_unit(pcity, pdef, 0) >= val) val = 0;
          }
        unit_list_iterate_end; /* was getting confused without the is_military part in */
        if (!unit_vulnerability_virtual(punit)) q = 0; /* thanks, JMT, Paul */
        else q = (pcity->ai.danger * 2 - def * unit_types[punit->type].attack_strength /
             unit_types[punit->type].defense_strength);
/* this was a WAG, but it works, so now it's just good code! -- Syela */
        if (val > 0 || q > 0) { /* Guess I better stay */
          ;
        } else q = 0;
      } /* end if home */
    } /* end if home is in danger */
  } /* end if we have a home */

  if (punit->ai.charge) { /* I am a bodyguard */
    aunit = unit_list_find(&pplayer->units, punit->ai.charge);
    acity = find_city_by_id(punit->ai.charge);

    if ((aunit && aunit->ai.bodyguard && unit_vulnerability_virtual(punit) >
         unit_vulnerability_virtual(aunit)) ||
         (acity && acity->owner == punit->owner && acity->ai.urgency &&
          acity->ai.danger > assess_defense_quadratic(acity))) {
      punit->ai.ai_role = AIUNIT_ESCORT;
      return;
    } else punit->ai.charge = 0;
  }

/* ok, what if I'm somewhere new? - ugly, kludgy code by Syela */
  if (should_unit_change_homecity(pplayer, punit)) {
    return;
  }

  if (q > 0) {
    if (pcity->ai.urgency) {
      punit->ai.ai_role = AIUNIT_DEFEND_HOME;
      return;
    }
  }

/* I'm not 100% sure this is the absolute best place for this... -- Syela */
  generate_warmap(map_get_city(punit->x, punit->y), punit);
/* I need this in order to call unit_move_turns, here and in look_for_charge */

  if (q > 0) {
    q *= 100;
    q /= unit_vulnerability_virtual(punit);
    d = unit_move_turns(punit, pcity->x, pcity->y);
    q >>= d;
  }

  val = 0; acity = 0; aunit = 0;
  if (unit_can_defend(punit->type)) {

/* This is a defending unit that doesn't need to stay put. 
It needs to defend something, but not necessarily where it's at.
Therefore, it will consider becoming a bodyguard. -- Syela */

    val = look_for_charge(pplayer, punit, &aunit, &acity);

  }
  if (q > val && ai_fuzzy(pplayer,1)) {
    punit->ai.ai_role = AIUNIT_DEFEND_HOME;
    return;
  }
 /* this is bad; riflemen might rather attack if val is low -- Syela */
  if (acity) {
    punit->ai.ai_role = AIUNIT_ESCORT;
    punit->ai.charge = acity->id;
/*printf("%s@(%d, %d) going to defend %s@(%d, %d)\n",
unit_types[punit->type].name, punit->x, punit->y,
acity->name, acity->x, acity->y);*/
  } else if (aunit) {
    punit->ai.ai_role = AIUNIT_ESCORT;
    punit->ai.charge = aunit->id;
/*printf("%s@(%d, %d) going to defend %s@(%d, %d)\n",
unit_types[punit->type].name, punit->x, punit->y,
unit_types[aunit->type].name, aunit->x, aunit->y);*/
  } else if (unit_attack_desirability(punit->type) ||
      (pcity && !same_pos(pcity->x, pcity->y, punit->x, punit->y)))
     punit->ai.ai_role = AIUNIT_ATTACK;
  else punit->ai.ai_role = AIUNIT_DEFEND_HOME; /* for default */
}

void ai_military_gohome(struct player *pplayer,struct unit *punit)
{
struct city *pcity;
int dest_x, dest_y;
if (punit->homecity)
   {
   pcity=find_city_by_id(punit->homecity);
/*   printf("GOHOME
(%d)(%d,%d)C(%d,%d)\n",punit->id,punit->x,punit->y,pcity->x,pcity->y); */
   if ((punit->x == pcity->x)&&(punit->y == pcity->y))
      {
/*      printf("INHOUSE. GOTO AI_NONE(%d)\n", punit->id); */
/* aggro defense goes here -- Syela */
      ai_military_findvictim(pplayer, punit, &dest_x, &dest_y);
      punit->ai.ai_role=AIUNIT_NONE;
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

int find_something_to_kill(struct player *pplayer, struct unit *punit, int *x, int *y)
{
  int a, b, c, d, e, m, n, v, i, f, a0, b0, ab, g;
  int aa = 0, bb = 0, cc = 0, dd = 0, bestb0 = 0;
  int con = map_get_continent(punit->x, punit->y);
  struct player *aplayer;
  struct unit *pdef;
  int best = 0, maxd, boatid = 0;
  int harborcity = 0, bx = 0, by = 0;
  int fprime;
  struct unit *ferryboat = 0;
  int sanity, boatspeed, unhap = 0;
  struct city *pcity;
  int xx, yy; /* for beachheads */
  int bk = 0; /* this is a kluge, because if we don't set x and y with !punit->id,
p_a_w isn't called, and we end up not wanting ironclads and therefore never
learning steam engine, even though ironclads would be very useful. -- Syela */

/* this is horrible, but I need to do something like this somewhere. -- Syela */
  for (i = 0; i < game.nplayers; i++) {
    aplayer = &game.players[i];
    city_list_iterate(aplayer->cities, acity)
      acity->ai.a = reinforcements_value(punit, acity->x, acity->y);
      acity->ai.f = reinforcements_cost(punit, acity->x, acity->y);
      acity->ai.invasion = 0;
    city_list_iterate_end;
  }

  unit_list_iterate(pplayer->units, aunit)
    if (aunit == punit) continue;
    if (aunit->activity == ACTIVITY_GOTO &&
       (pcity = map_get_city(aunit->goto_dest_x, aunit->goto_dest_y))) {
      if (unit_types[aunit->type].attack_strength >
        unit_types[aunit->type].transport_capacity) { /* attacker */
        a = unit_belligerence_basic(aunit);
        pcity->ai.a += a;
        pcity->ai.f += unit_types[aunit->type].build_cost;
      }
    }
/* dealing with invasion stuff */
    if (unit_types[aunit->type].attack_strength >
      unit_types[aunit->type].transport_capacity) { /* attacker */
      if (aunit->activity == ACTIVITY_GOTO) invasion_funct(aunit, 1, 0,
         ((is_ground_unit(aunit) || is_heli_unit(aunit)) ? 1 : 2));
      invasion_funct(aunit, 0, unit_types[aunit->type].move_rate / 3,
         ((is_ground_unit(aunit) || is_heli_unit(aunit)) ? 1 : 2));
    } else if (aunit->ai.passenger &&
              !same_pos(aunit->x, aunit->y, punit->x, punit->y)) {
      if (aunit->activity == ACTIVITY_GOTO) invasion_funct(aunit, 1, 1, 1);
      invasion_funct(aunit, 0, 2, 1);
    }
  unit_list_iterate_end;
/* end horrible initialization subroutine */
        
  pcity = map_get_city(punit->x, punit->y);

  if (get_government(pplayer->player_no) == G_REPUBLIC &&
      pcity && (!punit->id || pcity->id == punit->homecity)) {
    unit_list_iterate(pcity->units_supported, punit)
      if (unit_being_aggressive(punit)) {
        unhap++;
      } else if (is_field_unit(punit)) {
        unhap++;   
      }
    unit_list_iterate_end;
    if (city_got_effect(pcity, B_POLICE)) unhap--;   
  } /* handle other governments later */

  *x = punit->x; *y = punit->y;
  ab = unit_belligerence_basic(punit);
  if (!ab) return(0); /* don't want to deal with SIGFPE's -- Syela */
  m = unit_types[punit->type].move_rate;
  if (unit_flag(punit->type, F_IGTER)) m *= 3;
  maxd = MIN(6, m) * THRESHOLD + 1;
  f = unit_types[punit->type].build_cost;
  fprime = f * 2 * unit_types[punit->type].attack_strength /
           (unit_types[punit->type].attack_strength +
            unit_types[punit->type].defense_strength);

  generate_warmap(map_get_city(*x, *y), punit); /* most flexible but costs milliseconds */
/*  printf("%s's %s at (%d, %d) has belligerence %d.\n",
     pplayer->name, unit_types[punit->type].name, punit->x, punit->y, a);*/

  if (is_ground_unit(punit)) boatid = find_boat(pplayer, &bx, &by, 2);
  if (boatid) ferryboat = unit_list_find(&pplayer->units, boatid);
  if (ferryboat) really_generate_warmap(map_get_city(ferryboat->x, ferryboat->y),
                       ferryboat, SEA_MOVING);

  if (ferryboat) boatspeed = (unit_flag(ferryboat->type, F_TRIREME) ? 6 : 12);
  else boatspeed = (get_invention(pplayer, A_NAVIGATION) != TECH_KNOWN ? 6 : 12);

  if (is_ground_unit(punit) && !punit->id &&
      is_terrain_near_tile(punit->x, punit->y, T_OCEAN)) harborcity++;
 
  for (i = 0; i < game.nplayers; i++) {
    aplayer = &game.players[i];
    if (aplayer != pplayer) { /* enemy */
      city_list_iterate(aplayer->cities, acity)
        if (ai_handicap(pplayer, H_TARGETS) &&
            !map_get_known(acity->x, acity->y, pplayer)) continue;
        sanity = (goto_is_sane(pplayer, punit, acity->x, acity->y, 1) &&
                 warmap.cost[acity->x][acity->y] < maxd); /* for Tangier->Malaga */
        if (ai_fuzzy(pplayer,1) && ((is_ground_unit(punit) &&
          ((sanity) || 
          ((ferryboat || harborcity) &&
                      warmap.seacost[acity->x][acity->y] <= 6 * THRESHOLD))) ||
          (is_sailing_unit(punit) && warmap.seacost[acity->x][acity->y] < maxd))) {
          if ((pdef = get_defender(pplayer, punit, acity->x, acity->y))) {
            d = unit_vulnerability(punit, pdef);
            b = unit_types[pdef->type].build_cost + 40;
          } else { d = 0; b = 40; }
/* attempting to make empty cities less enticing. -- Syela */
          if (is_ground_unit(punit)) {
            if (!sanity) {
              c = (warmap.seacost[acity->x][acity->y]) / boatspeed; /* kluge */
              if (boatspeed < 9 && c > 2) c = 999; /* tired of Kaput! -- Syela */
              if (ferryboat) c += (warmap.cost[bx][by] + m - 1) / m + 1;
              else c += 1;
              if (unit_flag(punit->type, F_MARINES)) c -= 1;
            } else c = (warmap.cost[acity->x][acity->y] + m - 1) / m;
          } else if (is_sailing_unit(punit))
             c = (warmap.seacost[acity->x][acity->y] + m - 1) / m;
          else c = (real_map_distance(punit->x, punit->y, acity->x, acity->y) * 3) / m;
          if (c > 1) {
            n = ai_choose_defender_versus(acity, punit->type);
            v = get_virtual_defense_power(punit->type, n, acity->x, acity->y) *
                unit_types[n].hp * unit_types[n].firepower *
                (do_make_unit_veteran(acity, n) ? 1.5 : 1) / 30;
            if (v * v >= d) { d = v * v; b = unit_types[n].build_cost + 40; }
          } /* let's hope this works! */
          if (!is_ground_unit(punit) && !is_heli_unit(punit) &&
              (!(acity->ai.invasion&1))) b -= 40; /* boats can't conquer cities */
          if (!punit->id
	      && !unit_really_ignores_citywalls(punit)
	      && c > (player_knows_improvement_tech(aplayer, B_CITY) ? 2 : 4)
	      && !city_got_citywalls(acity))
	    d *= 9;

          a = (ab + acity->ai.a) * (ab + acity->ai.a);
/* Avoiding handling upkeep aggregation this way -- Syela */

          g = unit_list_size(&(map_get_tile(acity->x, acity->y)->units)) + 1;
/* AI was not sending enough reinforcements to totally wipe out a city
and conquer it in one turn.  This variable enables total carnage. -- Syela */

          if (!is_ground_unit(punit) && !is_heli_unit(punit) && !pdef) b0 = 0;
/* !d instead of !pdef was horrible bug that led to yoyo-ing AI warships -- Syela */
          else if (c > THRESHOLD) b0 = 0;
          else if ((is_ground_unit(punit) || is_heli_unit(punit)) &&
                   acity->ai.invasion == 2) b0 = f * SHIELD_WEIGHTING;
          else {
            b0 = (b * a - (f + acity->ai.f) * d) * g * SHIELD_WEIGHTING / (a + g * d);
            if (b * acity->ai.a * acity->ai.a > acity->ai.f * d)
               b0 -= (b * acity->ai.a * acity->ai.a - acity->ai.f * d) *
                      g * SHIELD_WEIGHTING / (acity->ai.a * acity->ai.a + g * d);
          }
          b0 -= c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
          if (b0 > 0) {
            a0 = amortize(b0, MAX(1, c));
            if (!sanity && !boatid && is_ground_unit(punit))
              e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / ((fprime + 40) * MORT);
            else e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / (fprime * MORT);
 /* BEGIN STEAM-ENGINES-ARE-OUR-FRIENDS KLUGE */
          } else if (!punit->id && !best) {
            b0 = b * SHIELD_WEIGHTING;
            a0 = amortize(b0, MAX(1, c));
            if (!sanity && !boatid && is_ground_unit(punit))
              e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / ((fprime + 40) * MORT);
            else e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / (fprime * MORT);
            if (e > bk) {
              if (punit->id && is_ground_unit(punit) &&
                       !unit_flag(punit->type, F_MARINES) &&
                       map_get_continent(acity->x, acity->y) != con) {
                if (find_beachhead(punit, acity->x, acity->y, &xx, &yy)) {
                  *x = acity->x;
                  *y = acity->y;
                  bk = e;
                } /* else no beachhead */
              } else {
                *x = acity->x;
                *y = acity->y;
                bk = e;
              }
            }
            e = 0; /* END STEAM-ENGINES KLUGE */
          } else e = 0;

/*if (is_ground_unit(punit) && punit->id && ferryboat)
printf("%s@(%d, %d) -> %s@(%d, %d) -> %s@(%d, %d) (sanity=%d, c=%d, e=%d, best=%d)\n",
unit_types[punit->type].name, punit->x, punit->y,
unit_types[ferryboat->type].name, bx, by,
acity->name, acity->x, acity->y, sanity, c, e, best);*/

          if (e > best && ai_fuzzy(pplayer,1)) {
            if (punit->id && is_ground_unit(punit) &&
                !unit_flag(punit->type, F_MARINES) &&
                map_get_continent(acity->x, acity->y) != con) {
/* a non-virtual ground unit is trying to attack something on another continent */
/* need a beachhead which is adjacent to the city and an available ocean tile */
              if (find_beachhead(punit, acity->x, acity->y, &xx, &yy)) { /* LaLALala */
                aa = a; bb = b; cc = c; dd = d;
                best = e; bestb0 = b0;
                *x = acity->x;
                *y = acity->y;
/* the ferryboat needs to target the beachhead, but the unit needs to target
the city itself.  This is a little weird, but it's the best we can do. -- Syela */
              } /* else do nothing, since we have no beachhead */
            } else {
              aa = a; bb = b; cc = c; dd = d;
              best = e; bestb0 = b0;
              *x = acity->x;
              *y = acity->y;
            } /* end need-beachhead else */
          }
        }
      city_list_iterate_end;

      a = unit_belligerence(punit);
/* I'm not sure the following code is good but it seems to be adequate. -- Syela */
/* I am deliberately not adding ferryboat code to the unit_list_iterate. -- Syela */
      unit_list_iterate(aplayer->units, aunit)
        if (map_get_city(aunit->x, aunit->y)) continue; /* already dealt with it */
        if (ai_handicap(pplayer, H_TARGETS) &&
            !map_get_known(aunit->x, aunit->y, pplayer)) continue;
        if (unit_flag(aunit->type, F_CARAVAN) && !punit->id) continue; /* kluge */
        if (ai_fuzzy(pplayer,1) &&
	    (aunit == get_defender(pplayer, punit, aunit->x, aunit->y) &&
           ((is_ground_unit(punit) &&
                map_get_continent(aunit->x, aunit->y) == con &&
                warmap.cost[aunit->x][aunit->y] < maxd) ||
            (is_sailing_unit(punit) &&
                goto_is_sane(pplayer, punit, aunit->x, aunit->y, 1) && /* Thanks, Damon */
                warmap.seacost[aunit->x][aunit->y] < maxd)))) {
          d = unit_vulnerability(punit, aunit);

          b = unit_types[aunit->type].build_cost;
          if (is_ground_unit(punit)) n = warmap.cost[aunit->x][aunit->y];
          else if (is_sailing_unit(punit)) n = warmap.seacost[aunit->x][aunit->y];
          else n = real_map_distance(punit->x, punit->y, aunit->x, aunit->y) * 3;
          if (n > m) { /* if n <= m, it can't run away -- Syela */
            n *= unit_types[aunit->type].move_rate;
            if (unit_flag(aunit->type, F_IGTER)) n *= 3;
          }
          c = (n + m - 1) / m;
          if (!is_ground_unit(punit) && !d) b0 = 0;
          else if (c > THRESHOLD) b0 = 0;
          else b0 = ((b * a - f * d) * SHIELD_WEIGHTING / (a + d)) - 
            c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
          if (b0 > 0) {
            a0 = amortize(b0, MAX(1, c));
            e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / (fprime * MORT);
          } else e = 0;
          if (e > best && ai_fuzzy(pplayer,1)) {
            aa = a; bb = b; cc = c; dd = d;
            best = e; bestb0 = b0;
            *x = aunit->x;
            *y = aunit->y;
          }
        }
      unit_list_iterate_end;
    } /* end if enemy */
  } /* end for all players */
/*if (best && map_get_city(*x, *y) && !punit->id) {
    printf("%s's %s#%d at (%d, %d) targeting (%d, %d) [desire = %d/%d]\n", pplayer->name,
       unit_types[punit->type].name, punit->id, punit->x, punit->y, *x, *y, best, bestb0);
    printf("A = %d, B = %d, C = %d, D = %d, F = %d, E = %d\n", aa, bb, cc, dd, f, best);
  }*/
/*if (punit->id && (pcity = map_get_city(*x, *y)) &&
pcity->owner != punit->owner && pcity->ai.invasion == 2 &&
(is_ground_unit(punit) || is_heli_unit(punit)))
printf("%s's %s#%d@(%d,%d) invading %s@(%d,%d)\n",
pplayer->name, unit_name(punit->type), punit->id,
punit->x, punit->y, pcity->name, *x, *y);*/
/*if (!best && bk) printf("%s's %s@(%d,%d) faking target %s@(%d,%d)\n",
pplayer->name, unit_name(punit->type), punit->x, punit->y, 
map_get_city(*x, *y)->name, *x, *y);*/
  return(best);
}

int find_nearest_friendly_port(struct unit *punit)
{
  struct player *pplayer = get_player(punit->owner);
  int best = 6 * THRESHOLD + 1, cur;
  generate_warmap(map_get_city(punit->x, punit->y), punit);
  city_list_iterate(pplayer->cities, pcity)
    cur = warmap.seacost[pcity->x][pcity->y];
    if (city_got_building(pcity, B_PORT)) cur /= 3;
    if (cur < best) {
      punit->goto_dest_x = pcity->x;
      punit->goto_dest_y = pcity->y;
      best = cur;
    }
  city_list_iterate_end;
  if (best > 6 * THRESHOLD) return 0;
/*printf("Friendly port nearest to (%d,%d) is %s@(%d,%d) [%d]\n",
punit->x, punit->y, map_get_city(punit->goto_dest_x, punit->goto_dest_y)->name,
punit->goto_dest_x, punit->goto_dest_y, best);*/
  return 1;
}

void ai_military_attack(struct player *pplayer,struct unit *punit)
{ /* rewritten by Syela - old way was crashy and not smart (nor is this) */
  int dest_x, dest_y; 
  int id, flag, went, ct = 10;

  if (punit->activity!=ACTIVITY_GOTO) {
    id = punit->id;
    do {
      flag = 0;
      ai_military_findvictim(pplayer, punit, &dest_x, &dest_y);  
      if (dest_x == punit->x && dest_y == punit->y) {
/* no one to bash here.  Will try to move onward */
        find_something_to_kill(pplayer, punit, &dest_x, &dest_y);
        if (same_pos(punit->x, punit->y, dest_x, dest_y)) {
/* nothing to kill.  Adjacency is something for us to kill later. */
          if (is_sailing_unit(punit)) {
            if (find_nearest_friendly_port(punit)) do_unit_goto(pplayer, punit);
          } else ai_manage_explorer(pplayer, punit); /* nothing else to do */
          return; /* Jane, stop this crazy thing! */
        } else if (!is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y)) {
/* if what we want to kill is adjacent, and findvictim didn't want it, WAIT! */
          if ((went = ai_military_gothere(pplayer, punit, dest_x, dest_y))) {
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
    if ((pcity = wonder_on_continent(pplayer, map_get_continent(punit->x, punit->y))) && build_points_left(pcity) > (pcity->shield_surplus<<1)) {
      if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
        if (!punit->moves_left) return;
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

void ai_manage_ferryboat(struct player *pplayer, struct unit *punit)
{ /* It's about 12 feet square and has a capacity of almost 1000 pounds.
     It is well constructed of teak, and looks seaworthy. */
  struct city *pcity;
  struct unit *bodyguard;
  int id = punit->id;
  int best = 4 * unit_types[punit->type].move_rate, x = punit->x, y = punit->y;
  int n = 0, p = 0;

  if (!unit_list_find(&map_get_tile(punit->x, punit->y)->units, punit->ai.passenger))
    punit->ai.passenger = 0;

  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit)
    if (aunit->ai.ferryboat == punit->id) {
      if (!punit->ai.passenger) punit->ai.passenger = aunit->id; /* oops */
      p++;
      bodyguard = unit_list_find(&map_get_tile(punit->x, punit->y)->units, aunit->ai.bodyguard);
      pcity = map_get_city(aunit->goto_dest_x, aunit->goto_dest_y);
      if (!aunit->ai.bodyguard || bodyguard ||
         (pcity && pcity->ai.invasion >= 2)) {
/*if (pcity) printf("Ferrying to %s to %s, invasion = %d, body = %d\n",
unit_name(aunit->type), pcity->name, pcity->ai.invasion, aunit->ai.bodyguard);*/
        n++;
        set_unit_activity(aunit, ACTIVITY_SENTRY);
        if (bodyguard) set_unit_activity(bodyguard, ACTIVITY_SENTRY);
      }
    }
  unit_list_iterate_end;
  if (p) {
/*printf("%s#%d@(%d,%d), p=%d, n=%d\n", unit_name(punit->type),
punit->id, punit->x, punit->y, p, n);*/
    if (punit->moves_left && n) do_unit_goto(pplayer, punit);
    else if (!n && !map_get_city(punit->x, punit->y)) { /* rest in a city, for unhap */
      x = punit->goto_dest_x; y = punit->goto_dest_y;
      if (find_nearest_friendly_port(punit)) do_unit_goto(pplayer, punit);
      if (!unit_list_find(&pplayer->units, id)) return; /* oops! */
      punit->goto_dest_x = x; punit->goto_dest_y = y;
      send_unit_info(pplayer, punit, 0); /* to get the crosshairs right -- Syela */
    }
/*else printf("Ferryboat %d@(%d,%d) stalling.\n", punit->id, punit->x, punit->y);*/
    return;
  }
/* ok, not carrying anyone, even the ferryman */
  punit->ai.passenger = 0;
/*printf("Ferryboat %d@(%d, %d) is lonely.\n", punit->id, punit->x, punit->y);*/
  set_unit_activity(punit, ACTIVITY_IDLE);
  punit->goto_dest_x = punit->x;
  punit->goto_dest_y = punit->y;

  if (unit_types[punit->type].attack_strength >
      unit_types[punit->type].transport_capacity) {
    if (punit->moves_left) ai_manage_military(pplayer, punit);
    return;
  } /* AI used to build frigates to attack and then use them as ferries -- Syela */

  generate_warmap(map_get_city(punit->x, punit->y), punit);
  p = 0; /* yes, I know it's already zero. -- Syela */
  unit_list_iterate(pplayer->units, aunit)
/*    if (aunit->ai.ferryboat == punit->id && warmap.seacost[aunit->x][aunit->y] < best) {*/
    if (aunit->ai.ferryboat && warmap.seacost[aunit->x][aunit->y] < best &&
          !is_transporter_with_free_space(pplayer, aunit->x, aunit->y) &&
	  ai_fuzzy(pplayer,1)) {
/*printf("Found a friend %d@(%d, %d)\n", aunit->id, aunit->x, aunit->y);*/
      x = aunit->x;
      y = aunit->y;
      best = warmap.seacost[x][y];
    }
    if (is_sailing_unit(aunit) && map_get_terrain(aunit->x, aunit->y) == T_OCEAN) p++;
  unit_list_iterate_end;
  if (best < 4 * unit_types[punit->type].move_rate) {
    punit->goto_dest_x = x;
    punit->goto_dest_y = y;
    punit->activity = ACTIVITY_GOTO;
    do_unit_goto(pplayer,punit);
    if ((punit = unit_list_find(&pplayer->units, id)))
      set_unit_activity(punit, ACTIVITY_IDLE);
    return;
  }

/* do cool stuff here */

  if (!punit->moves_left) return;  
  pcity = find_city_by_id(punit->homecity);
  if (pcity) {
    if (!ai_handicap(pplayer, H_TARGETS) ||
        unit_move_turns(punit, pcity->x, pcity->y) < p) {
/*printf("No friends.  Going home.\n");*/
      punit->goto_dest_x = pcity->x;
      punit->goto_dest_y = pcity->y;
      punit->activity = ACTIVITY_GOTO;
      do_unit_goto(pplayer,punit);
      return;
    }
  }
  if (map_get_terrain(punit->x, punit->y) == T_OCEAN) /* thanks, Tony */
    ai_manage_explorer(pplayer, punit);
  return;
}

/**************************************************************************
decides what to do with a military unit.
**************************************************************************/

void ai_manage_military(struct player *pplayer,struct unit *punit)
{
  int id;
/*  struct city *pcity;*/

  id = punit->id;

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE);

  punit->ai.ai_role = AIUNIT_NONE;
/* was getting a bad bug where a settlers caused a defender to leave home */
/* and then all other supported units went on DEFEND_HOME/goto */
  ai_military_findjob(pplayer, punit);
/*if (unit_flag(punit->type, F_FIELDUNIT) && (pcity = map_get_city(punit->x, punit->y)))
printf("%s in %s is going to %d\n", unit_name(punit->type), pcity->name, punit->ai.ai_role);*/
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
      ai_military_bodyguard(pplayer, punit);
      break;
    case AIUNIT_EXPLORE:
      ai_manage_explorer(pplayer, punit);
      break;
  }

  if ((punit = find_unit_by_id(id))) {
    if (punit->activity != ACTIVITY_IDLE &&
        punit->activity != ACTIVITY_GOTO)
      handle_unit_activity_request(pplayer, punit, ACTIVITY_IDLE); 

    if (punit->moves_left) {
      if (unit_list_find(&(map_get_tile(punit->x, punit->y)->units),
          punit->ai.ferryboat))
        handle_unit_activity_request(pplayer, punit, ACTIVITY_SENTRY);
      else handle_unit_activity_request(pplayer, punit, ACTIVITY_FORTIFY);
    } /* better than doing nothing */
  }
}

void ai_manage_unit(struct player *pplayer, struct unit *punit) 
{
  if ((unit_flag(punit->type, F_DIPLOMAT))
      || (unit_flag(punit->type, F_SPY))) {
    return;
    /* the right test if the unit is in a city and 
       there is no other diplomat it musn't move.
       This unit is a bodyguard against enemy diplomats.
       Rigth now I don't know how to use bodyguards! (17/12/98) (--NB)
    */
  }
  else if (unit_flag(punit->type, F_SETTLERS)) {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_settler(pplayer, punit);
  } else if (unit_flag(punit->type, F_CARAVAN)) {
    ai_manage_caravan(pplayer, punit);
  } else if (get_transporter_capacity(punit)) {
    ai_manage_ferryboat(pplayer, punit);
  } else if (is_military_unit(punit)) {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_military(pplayer,punit); 
  } else {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_explorer(pplayer, punit); /* what else could this be? -- Syela */
  }
  /* Careful Unit maybe void here */
}

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
/*printf("Managing %s's %s %d@(%d,%d)\n", pplayer->name,
unit_types[punit->type].name, punit->id, punit->x, punit->y); */
      ai_manage_unit(pplayer, punit); 
/*printf("Managed %s's %s %d@(%d,%d)\n", pplayer->name,
unit_types[punit->type].name, punit->id, punit->x, punit->y); */
  unit_list_iterate_end;
/*  printf("Managed units successfully.\n");   */
#ifdef CHRONO
  gettimeofday(&tv, 0);
  printf("%s's units consumed %d microseconds.\n", pplayer->name,
       (tv.tv_sec - sec) * 1000000 + (tv.tv_usec - usec));
#endif
}

/**************************************************************************
 This a hack to enable emulation of old loops previously hardwired as
   for (i = U_WARRIORS; i <= U_BATTLESHIP; i++)
 (Could probably just adjust the loops themselves fairly simply, but this
 is safer for regression testing.) 
**************************************************************************/
int is_ai_simple_military(int type)
{
  return !unit_flag(type, F_NONMIL)
    && !unit_flag(type, F_MISSILE)
    && !(get_unit_type(type)->transport_capacity >= 8);
}
