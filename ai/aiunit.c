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
#include <assert.h>

#include "city.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "timing.h"
#include "unit.h"

#include "barbarian.h"
#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"
#include "maphand.h"
#include "settlers.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "advmilitary.h"
#include "aicity.h"
#include "aihand.h"
#include "aitools.h"

#include "aiunit.h"

extern struct move_cost_map warmap;

static void ai_manage_barbarian_leader(struct player *pplayer, struct unit *leader);
static void ai_manage_diplomat(struct player *pplayer, struct unit *pdiplomat);

/**************************************************************************
  Similar to is_my_zoc(), but with some changes:
  - destination (x0,y0) need not be adjacent?
  - don't care about some directions?
  
  Note this function only makes sense for ground units.
**************************************************************************/
static int could_be_my_zoc(struct unit *myunit, int x0, int y0)
{
  /* Fix to bizarre did-not-find bug.  Thanks, Katvrr -- Syela */
  int ax, ay, k;
  int owner=myunit->owner;

  assert(is_ground_unit(myunit));
  
  if (same_pos(x0, y0, myunit->x, myunit->y))
    return 0; /* can't be my zoc */
  if (is_tiles_adjacent(x0, y0, myunit->x, myunit->y)
      && !is_non_allied_unit_tile(map_get_tile(x0, y0), owner))
    return 0;

  for (k = 0; k < 8; k++) {
    ax = map_adjust_x(myunit->x + DIR_DX[k]);
    ay = map_adjust_y(myunit->y + DIR_DY[k]);
    
    if (map_get_terrain(ax,ay)!=T_OCEAN
	&& is_non_allied_unit_tile(map_get_tile(ax,ay),owner))
      return 0;
  }
  
  return 1;
  /* return was -1 as a flag value; I've moved -1 value into
   * could_unit_move_to_tile(), since that's where it becomes
   * distinct/relevant --dwp
   */
}

/**************************************************************************
  this WAS can_unit_move_to_tile with the notifys removed -- Syela 
  but is now a little more complicated to allow non-adjacent tiles
  returns:
    0 if can't move
    1 if zoc_ok
   -1 if zoc could be ok?
**************************************************************************/
int could_unit_move_to_tile(struct unit *punit, int x0, int y0, int x, int y)
{
  struct tile *ptile,*ptile2;
  struct city *pcity;

  if(punit->activity!=ACTIVITY_IDLE &&
     punit->activity!=ACTIVITY_GOTO && !punit->connecting)
    return 0;

  if(x<0 || x>=map.xsize || y<0 || y>=map.ysize)
    return 0;
  
  if(!is_tiles_adjacent(x0, y0, x, y))
    return 0; 

  if(is_non_allied_unit_tile(map_get_tile(x,y),punit->owner))
    return 0;

  ptile=map_get_tile(x, y);
  ptile2=map_get_tile(x0, y0);
  if(is_ground_unit(punit)) {
    /* Check condition 4 */
    if(ptile->terrain==T_OCEAN &&
       ground_unit_transporter_capacity(x, y, punit->owner) <= 0)
	return 0;

    /* Moving from ocean */
    if(ptile2->terrain==T_OCEAN) {
      /* Can't attack a city from ocean unless marines */
      if(!unit_flag(punit->type, F_MARINES)
	 && is_enemy_city_tile(map_get_tile(x, y), punit->owner)) {
	return 0;
      }
    }
  } else if(is_sailing_unit(punit)) {
    if(ptile->terrain!=T_OCEAN && ptile->terrain!=T_UNKNOWN)
      if(!is_allied_city_tile(map_get_tile(x, y), punit->owner))
	return 0;
  } 

  pcity = map_get_city(x, y);
  if (pcity && !is_allied_city_tile(map_get_tile(x, y), punit->owner)) {
    if (is_air_unit(punit) || !is_military_unit(punit)) {
      return 0;  
    }
  }

  if (zoc_ok_move_gen(punit, x0, y0, x, y))
    return 1;
  else if (could_be_my_zoc(punit, x0, y0))
    return -1;	/* flag value  */
  else
    return 0;
}

/********************************************************************** 
  This is a much simplified form of assess_defense (see advmilitary.c),
  but which doesn't use pcity->ai.wallvalue and just returns a boolean
  value.  This is for use with "foreign" cities, especially non-ai
  cities, where ai.wallvalue may be out of date or uninitialized --dwp
***********************************************************************/
static int has_defense(struct city *pcity)
{
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    if (is_military_unit(punit) && get_defense_power(punit) && punit->hp) {
      return 1;
    }
  }
  unit_list_iterate_end;
  return 0;
}

/********************************************************************** 
  ...
***********************************************************************/
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
      m += (improvement_variant(B_MAGELLAN)==1) ? 3 : 6;
    m += player_knows_techs_with_flag(pplayer,TF_BOAT_FAST)*3;
  }   

  if (unit_types[punit->type].move_type == LAND_MOVING)
    d = warmap.cost[x][y] / m;
  else if (unit_types[punit->type].move_type == SEA_MOVING)
    d = warmap.seacost[x][y] / m;
  else d = real_map_distance(punit->x, punit->y, x, y) * 3 / m;
  return(d);
}

/**************************************************************************
  is there any hope of reaching this tile without violating ZOC? 
**************************************************************************/
static int tile_is_accessible(struct unit *punit, int x, int y)
{
  int dir, x1, y1;
  
  if (unit_really_ignores_zoc(punit))  return 1;
  if (is_my_zoc(punit, x, y))          return 1;

  for (dir = 0; dir < 8; dir++) {
    x1 = x + DIR_DX[dir];
    y1 = y + DIR_DY[dir];
    if (!normalize_map_pos(&x1, &y1))
      continue;

    if (map_get_terrain(x1, y1) != T_OCEAN
	&& is_my_zoc(punit, x1, y1))
      return 1;
  }

  return 0;
}
 
/**************************************************************************
Explores unknown territory, finds huts.
Returns whether there is any more territory to be explored.
**************************************************************************/
int ai_manage_explorer(struct player *pplayer, struct unit *punit)
{
  int i, j, d, f, x, y, con, dest_x=0, dest_y=0, best, lmt, cur, a, b;
  int ok, ct = punit->moves_left + 3;
  struct city *pcity;
  int id = punit->id; /* we can now die because easy AI may accidently
			 stumble on huts it fuzzily ignored */

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(punit, ACTIVITY_IDLE);

  x = punit->x; y = punit->y;
  if (is_ground_unit(punit)) con = map_get_continent(x, y);
  else con = 0; /* Thanks, Tony */

  lmt = (pplayer->ai.control ? 2 * THRESHOLD : 3);
  best = lmt * 3 + 1;

  generate_warmap(map_get_city(x, y), punit); /* CPU-expensive but worth it -- Syela */
/* otherwise did not understand about bays and did really stupid things! */

/* BEGIN PART ONE: Look for huts.  Non-Barbarian Ground units ONLY. */
  if (!is_barbarian(pplayer)) {
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
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
      return 1; /* maybe have moves left but I want to return anyway. */
    }
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
	struct tile *ptile = map_get_tile(x + i, y + j);
        ok = (unit_flag(punit->type, F_TRIREME) ? 0 : 1);
        if (map_get_continent(x + i, y + j) == con &&
	    !is_non_allied_unit_tile(ptile, punit->owner) &&
	    (!ptile->city || is_allied_city_tile(ptile, punit->owner))) {
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
              could_unit_move_to_tile(punit, punit->x, punit->y, x+i, y+j) &&
	      !(is_barbarian(pplayer) &&
		(map_get_tile(x+i, y+j)->special & S_HUT))) {
            dest_x = map_adjust_x(x + i);
            dest_y = y + j;
            best = cur;
          }
        }
      } /* end j */
    } /* end i */
    if (best) {
      handle_unit_move_request(pplayer, punit, dest_x, dest_y, FALSE);
      if(!player_find_unit_by_id(pplayer, id)) return 0; /* died */
    }
    ct--; /* trying to avoid loops */
  }
  if (!punit->moves_left) return 1;
/* END PART TWO */

/* BEGIN PART THREE: Go towards unexplored territory */
/* no adjacent squares help us to explore - really slow part follows */
  best = 0;
  for (x = 0; x < map.xsize; x++) {
    for (y = 0; y < map.ysize; y++) {
      struct tile *ptile = map_get_tile(x,y);
      if (map_get_continent(x, y) == con && map_get_known(x, y, pplayer)
	  && !is_non_allied_unit_tile(ptile, punit->owner)
	  && (!ptile->city || is_allied_city_tile(ptile, punit->owner))
	  && tile_is_accessible(punit, x, y)) {
        cur = 0;
        for (a = -1; a <= 1; a++)
          for (b = -1; b <= 1; b++)
            if (!map_get_known(x + a, y + b, pplayer)) cur++;
        if (cur) {
          cur += 9 * (THRESHOLD - unit_move_turns(punit, x, y));
          if ((cur > best || (cur == best && myrand(2))) &&
	      !(is_barbarian(pplayer) &&
		(map_get_tile(x, y)->special & S_HUT))) {
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
    set_unit_activity(punit, ACTIVITY_GOTO);
    do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
    return 1;
  }
/* END PART THREE */

  freelog(LOG_DEBUG, "%s's %s at (%d,%d) failed to explore.",
	  pplayer->name, unit_types[punit->type].name, punit->x, punit->y);
  handle_unit_activity_request(punit, ACTIVITY_IDLE);
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
        do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
      }
    }
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
static struct city *wonder_on_continent(struct player *pplayer, int cont)
{
  city_list_iterate(pplayer->cities, pcity) 
    if (!(pcity->is_building_unit) && is_wonder(pcity->currently_building) && map_get_continent(pcity->x, pcity->y) == cont)
      return pcity;
  city_list_iterate_end;
  return NULL;
}

static int should_unit_change_homecity(struct player *pplayer,
				       struct unit *punit)
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
 
    freelog(LOG_DEBUG, "Incity at (%d,%d).  Val = %d.", punit->x, punit->y, val);
    if (val) { /* Guess I better stay / you can live at home now */
      punit->ai.ai_role=AIUNIT_DEFEND_HOME;

      punit->ai.charge = 0; /* Very important, or will not stay -- Syela */
/* change homecity to this city */
      unit_list_insert(&pcity->units_supported, punit);
      if ((pcity=player_find_city_by_id(pplayer, punit->homecity)))
            unit_list_unlink(&pcity->units_supported, punit);

      punit->homecity = map_get_city(punit->x, punit->y)->id;
      send_unit_info(pplayer, punit);
/* homecity changed */
      return(1);
    }
  }
  return(0);
}

static int unit_belligerence_primitive(struct unit *punit)
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

static int stack_attack_value(int x, int y)
{
  int val = 0;
  struct tile *ptile = map_get_tile(x, y);
  unit_list_iterate(ptile->units, aunit)
    val += unit_belligerence_basic(aunit);
  unit_list_iterate_end;
  return(val);
}

static void invasion_funct(struct unit *punit, int dest, int n, int which)
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
        if (dest || punit->activity != ACTIVITY_GOTO || !has_defense(pcity))
          pcity->ai.invasion |= which;
    }
  }
}

static int reinforcements_value(struct unit *punit, int x, int y)
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

static int city_reinforcements_cost_and_value(struct city *pcity, struct unit *punit)
{ 
  int val = 0, val2 = 0, val3 = 0, i, j, x, y;
  struct tile *ptile;

  if (!pcity) return 0;

  x=pcity->x;
  y=pcity->y;
  for (j = y - 1; j <= y + 1; j++) {
    if (j < 0 || j >= map.ysize) continue;
    for (i = x - 1; i <= x + 1; i++) {
      ptile = map_get_tile(i, j);
      unit_list_iterate(ptile->units, aunit)
        if (aunit == punit || aunit->owner != punit->owner) continue;
        val = (unit_belligerence_basic(aunit));
        if (val) {
          val2 += val;
          val3 += unit_types[aunit->type].build_cost;
        }
      unit_list_iterate_end;
    }
  }
  pcity->ai.a = val2;
  pcity->ai.f = val3;
  return(val2);
}

#ifdef UNUSED
static int reinforcements_cost(struct unit *punit, int x, int y)
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
#endif

/*************************************************************************
...
**************************************************************************/
static int is_my_turn(struct unit *punit, struct unit *pdef)
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

/*************************************************************************
This looks at tiles neighbouring the unit to find something to kill or
explore. It prefers tiles in the following order:
1. Undefended cities
2. Huts
3. Enemy units weaker than the unit
4. Land barbarians also like unfrastructure tiles (for later pillage)
If none of the following is there, nothing is chosen.

work of Syela - mostly to fix the ZOC/goto strangeness
**************************************************************************/
static int ai_military_findvictim(struct player *pplayer, struct unit *punit,
				  int *dest_x, int *dest_y)
{
  int x, y, x1, y1, k;
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

  for (k = 0; k < 8; k++) {
    x1 = x + DIR_DX[k];
    y1 = y + DIR_DY[k];
    if (!normalize_map_pos(&x1, &y1))
      continue;

    pdef = get_defender(pplayer, punit, x1, y1);
    if (pdef) {
      patt = get_attacker(pplayer, punit, x1, y1);
/* horsemen in city refused to attack phalanx just outside that was
bodyguarding catapult - patt will resolve this bug nicely -- Syela */
      if (can_unit_attack_tile(punit, x1, y1)) { /* thanks, Roar */
        d = unit_vulnerability(punit, pdef);
        if (map_get_city(x, y) && /* pikemen defend Knights, attack Catapults */
              get_total_defense_power(pdef, punit) *
              get_total_defense_power(punit, pdef) >= /* didn't like > -- Syela */
              get_total_attack_power(patt, punit) * /* patt, not pdef */
              get_total_attack_power(punit, pdef) &&
              unit_list_size(&(map_get_tile(punit->x, punit->y)->units)) < 2 &&
	      get_total_attack_power(patt, punit)) { 
	  freelog(LOG_DEBUG, "%s defending %s from %s's %s",
			get_unit_type(punit->type)->name,
			map_get_city(x, y)->name,
			game.players[pdef->owner].name,
			get_unit_type(pdef->type)->name);
        } else {
          a = reinforcements_value(punit, pdef->x, pdef->y);
          a += unit_belligerence_primitive(punit);
          a *= a;
          b = unit_types[pdef->type].build_cost;
          if (map_get_city(x1, y1)) /* bonus restored 980804 -- Syela */
            b = (b + 40) * punit->hp / unit_types[punit->type].hp;
/* c is always equal to zero in this routine, and f is already known */
/* arguable that I should use reinforcement_cost here?? -- Syela */
          if (a && is_my_turn(punit, pdef)) {
            e = ((b * a - f * d) * SHIELD_WEIGHTING / (a + d) - c * SHIELD_WEIGHTING);
/* no need to amortize! */
            if (e > best && ai_fuzzy(pplayer,1)) {
	      freelog(LOG_DEBUG, "Better than %d is %d (%s)",
			    best, e, unit_types[pdef->type].name);
              best = e; *dest_y = y1; *dest_x = x1;
            } else {
	      freelog(LOG_DEBUG, "NOT better than %d is %d (%s)",
			    best, e, unit_types[pdef->type].name);
	    }
          } /* end if we have non-zero belligerence */
        }
      }
    } else { /* no pdef */
      pcity = map_get_city(x1, y1);
      if (pcity && is_ground_unit(punit) &&
          map_get_terrain(punit->x, punit->y) != T_OCEAN) {
        if (pcity->owner != pplayer->player_no) { /* free goodies */
          best = 99999; *dest_y = y1; *dest_x = x1;
        }
      }
      if (map_get_tile(x1, y1)->special & S_HUT && best < 99999 &&
          could_unit_move_to_tile(punit, punit->x, punit->y, x1, y1) &&
          !is_barbarian(&game.players[punit->owner]) &&
/*          zoc_ok_move(punit, x1, y1) && !is_sailing_unit(punit) &&*/
          punit->ai.ai_role != AIUNIT_ESCORT && /* makes life easier */
          !punit->ai.charge && /* above line seems not to work. :( */
          punit->ai.ai_role != AIUNIT_DEFEND_HOME) { /* Oops! -- Syela */
        best = 99998; *dest_y = y1; *dest_x = x1;
      }
      if( is_land_barbarian(pplayer) && best == 0 &&
          get_tile_infrastructure_set(map_get_tile(x1, y1)) &&
          could_unit_move_to_tile(punit, punit->x, punit->y, x1, y1) ) {
        best = 1; *dest_y = y1; *dest_x = x1;
      } /* next to nothing is better than nothing */
    }
  }
  return(best);
}

/*************************************************************************
...
**************************************************************************/
static void ai_military_bodyguard(struct player *pplayer, struct unit *punit)
{
  struct unit *aunit = player_find_unit_by_id(pplayer, punit->ai.charge);
  struct city *acity = find_city_by_id(punit->ai.charge);
  int x, y, id = punit->id, i;

  if (aunit && aunit->owner == punit->owner) { x = aunit->x; y = aunit->y; }
  else if (acity && acity->owner == punit->owner) { x = acity->x; y = acity->y; }
  else { /* should be impossible */
    punit->ai.charge = 0;
    return;
  }
  
  if (aunit) {
    freelog(LOG_DEBUG, "%s#%d@(%d,%d) -> %s#%d@(%d,%d) [body=%d]",
	    unit_types[punit->type].name, punit->id, punit->x, punit->y,
	    unit_types[aunit->type].name, aunit->id, aunit->x, aunit->y,
	    aunit->ai.bodyguard);
  }

  if (!same_pos(punit->x, punit->y, x, y)) {
    if (goto_is_sane(pplayer, punit, x, y, 1)) {
      punit->goto_dest_x = x;
      punit->goto_dest_y = y;
      do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
    } else punit->ai.charge = 0; /* can't possibly get there to help */
  } else { /* I had these guys set to just fortify, which is so dumb. -- Syela */
    i = ai_military_findvictim(pplayer, punit, &x, &y);
    freelog(LOG_DEBUG,
	    "Stationary escort @(%d,%d) received %d best @(%d,%d)",
	    punit->x, punit->y, i, x, y);
    if (i >= 40 * SHIELD_WEIGHTING)
      handle_unit_move_request(pplayer, punit, x, y, FALSE);
/* otherwise don't bother, but free cities are free cities and must be snarfed. -- Syela */
  }
  if (aunit && unit_list_find(&map_get_tile(x, y)->units, id) && aunit->ai.bodyguard)
    aunit->ai.bodyguard = id;
}

/*************************************************************************
...
**************************************************************************/
int find_beachhead(struct unit *punit, int dest_x, int dest_y, int *x, int *y)
{
  int x1, y1, dir, l, ok, t, fu, best = 0;

  for (dir = 0; dir < 8; dir++) {
    x1 = dest_x + DIR_DX[dir];
    y1 = dest_y + DIR_DY[dir];
    if (!normalize_map_pos(&x1, &y1))
      continue;

    ok = 0; fu = 0;
    t = map_get_terrain(x1, y1);
    if (warmap.seacost[x1][y1] <= 6 * THRESHOLD && t != T_OCEAN) { /* accessible beachhead */
      for (l = 0; l < 8 && !ok; l++) {
        if (map_get_terrain(x1 + DIR_DX[l], y1 + DIR_DY[l]) == T_OCEAN) {
          fu++;
          if (is_my_zoc(punit, x1 + DIR_DX[l], y1 + DIR_DY[l])) ok++;
        }
      }
      if (ok) { /* accessible beachhead with zoc-ok water tile nearby */
        ok = get_tile_type(t)->defense_bonus;
	if (map_get_special(x1, y1) & S_RIVER)
	  ok += (ok * terrain_control.river_defense_bonus) / 100;
        if (get_tile_type(t)->movement_cost * 3 <
            unit_types[punit->type].move_rate) ok *= 8;
        ok += (6 * THRESHOLD - warmap.seacost[x1][y1]);
        if (ok > best) { best = ok; *x = x1; *y = y1; }
      }
    }
  }

  return(best);
}

/**************************************************************************
find_beachhead() works only when city is not further that 1 tile from
the sea. But Sea Raiders might want to attack cities inland.
So this finds the nearest land tile on the same continent as the city.
**************************************************************************/
static void find_city_beach( struct city *pc, struct unit *punit, int *x, int *y)
{
  int i, j;
  int xx, yy, best_xx = punit->x, best_yy = punit->y;
  int dist = 100;
  int far = real_map_distance( pc->x, pc->y, punit->x, punit->y );

  for( i = 1-far; i < far; i++ )
    for( j = 1-far; j < far; j++ ) {
      xx = map_adjust_x(punit->x + i);
      yy = map_adjust_y(punit->y + j);
      if( map_same_continent( xx, yy, pc->x, pc->y ) &&
          real_map_distance( punit->x, punit->y, xx, yy ) < dist ) {
        dist = real_map_distance( punit->x, punit->y, xx, yy );
        best_xx = xx; best_yy = yy;
      }
    }
  *x = best_xx;
  *y = best_yy;
}

/**************************************************************************
...
**************************************************************************/
static int ai_military_gothere(struct player *pplayer, struct unit *punit,
			       int dest_x, int dest_y)
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
    freelog(LOG_DEBUG,
	    "%s@(%d,%d) looking for bodyguard, d_val=%d, my_val=%d",
	    unit_types[punit->type].name, punit->x, punit->y, d_val,
	    (punit->hp * (punit->veteran ? 15 : 10)
	     * unit_types[punit->type].defense_strength));
    ptile = map_get_tile(punit->x, punit->y);
    if (d_val >= punit->hp * (punit->veteran ? 15 : 10) *
                unit_types[punit->type].defense_strength) {
/*      if (!unit_list_find(&pplayer->units, punit->ai.bodyguard))      Ugggggh */
      if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
        punit->ai.bodyguard = -1;
    } else if (!unit_list_find(&ptile->units, punit->ai.bodyguard))
      punit->ai.bodyguard = 0;

    if( is_barbarian(pplayer) )  /* barbarians must have more currage */
      punit->ai.bodyguard = 0;
/* end protection subroutine */

    if (!goto_is_sane(pplayer, punit, dest_x, dest_y, 1) ||
       (ferryboat && goto_is_sane(pplayer, ferryboat, dest_x, dest_y, 1))) { /* Important!! */
      punit->ai.ferryboat = boatid;
      freelog(LOG_DEBUG, "%s: %d@(%d, %d): Looking for BOAT (id=%d).",
		    pplayer->name, punit->id, punit->x, punit->y, boatid);
      if (!same_pos(x, y, bx, by)) {
        punit->goto_dest_x = bx;
        punit->goto_dest_y = by;
	set_unit_activity(punit, ACTIVITY_GOTO);
        do_unit_goto(pplayer,punit, GOTO_MOVE_ANY);
        if (!player_find_unit_by_id(pplayer, id)) return(-1); /* died */
      }
      ptile = map_get_tile(punit->x, punit->y);
      ferryboat = unit_list_find(&ptile->units, punit->ai.ferryboat);
      if (ferryboat && (!ferryboat->ai.passenger ||
          ferryboat->ai.passenger == punit->id)) {
	freelog(LOG_DEBUG, "We have FOUND BOAT, %d ABOARD %d@(%d,%d)->(%d, %d).",
		punit->id, ferryboat->id, punit->x, punit->y,
		dest_x, dest_y);
        set_unit_activity(punit, ACTIVITY_SENTRY); /* kinda cheating -- Syela */ 
        ferryboat->ai.passenger = punit->id;
/* the code that worked for settlers wasn't good for piles of cannons */
        if (find_beachhead(punit, dest_x, dest_y, &ferryboat->goto_dest_x,
               &ferryboat->goto_dest_y)) {
/*  set_unit_activity(ferryboat, ACTIVITY_GOTO);   -- Extremely bad!! -- Syela */
          punit->goto_dest_x = dest_x;
          punit->goto_dest_y = dest_y;
          set_unit_activity(punit, ACTIVITY_SENTRY); /* anything but GOTO!! */
          if (ground_unit_transporter_capacity(punit->x, punit->y, pplayer->player_no) <= 0) {
	    freelog(LOG_DEBUG, "All aboard!");
	    /* perhaps this should only require two passengers */
            unit_list_iterate(ptile->units, mypass)
              if (mypass->ai.ferryboat == ferryboat->id) {
                set_unit_activity(mypass, ACTIVITY_SENTRY);
                def = unit_list_find(&ptile->units, mypass->ai.bodyguard);
                if (def) set_unit_activity(def, ACTIVITY_SENTRY);
              }
            unit_list_iterate_end; /* passengers are safely stowed away */
            do_unit_goto(pplayer, ferryboat, GOTO_MOVE_ANY);
	    if (!player_find_unit_by_id(pplayer, boatid)) return(-1); /* died */
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
       !has_defense(dcity)))))) {
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
		freelog(LOG_DEBUG,
			"Bodyguard at (%d, %d) is adjacent to (%d, %d)",
			i, j, punit->x, punit->y);
                if (aunit->moves_left) return(0);
                else return(handle_unit_move_request(pplayer, punit, i, j, FALSE));
              }
            unit_list_iterate_end;
          } /* end j */
        } /* end i */
      } /* end if */
/* end 'short leash' subroutine */

      if (ferryboat) {
	freelog(LOG_DEBUG, "GOTHERE: %s#%d@(%d,%d)->(%d,%d)", 
		unit_types[punit->type].name, punit->id,
		punit->x, punit->y, dest_x, dest_y);
      }
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(pplayer,punit,GOTO_MOVE_ANY);
      /* liable to bump into someone that will kill us.  Should avoid? */
    } else {
      freelog(LOG_DEBUG, "%s#%d@(%d,%d) not moving -> (%d, %d)",
		    unit_types[punit->type].name, punit->id,
		    punit->x, punit->y, dest_x, dest_y);
    }
  }
  if (player_find_unit_by_id(pplayer, id)) { /* didn't die */
    punit->ai.ai_role = AIUNIT_NONE; /* in case we need to change */
    if (x != punit->x || y != punit->y) return 1; /* moved */
    else return 0; /* didn't move, didn't die */
  }
  return(-1); /* died */
}

/*************************************************************************
...
**************************************************************************/
int unit_can_defend(int type)
{
  if (unit_types[type].move_type != LAND_MOVING) return 0; /* temporary kluge */
  return (unit_has_role(type, L_DEFEND_GOOD));
}

#if 0
/* pre-rulesets method, which was too hard to generalize (because
   whether a unit is a "good" defender depends on how good other
   units are)
*/
int old_unit_can_defend(int type)
{
  if (unit_types[type].move_type != LAND_MOVING) return 0; /* temporary kluge */
  if (unit_types[type].defense_strength * 
      (unit_types[type].hp > 10 ? 5 : 3) >=
      unit_types[type].attack_strength * 4 &&
      !unit_types[type].transport_capacity &&
      !unit_flag(type, F_NONMIL)) return 1;
  return 0;
}
#endif

/*************************************************************************
...
**************************************************************************/
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
    freelog(LOG_DEBUG, "(%d,%d)->(%d,%d), %d turns, def=%d",
	    punit->x, punit->y, buddy->x, buddy->y, d, def);
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
  freelog(LOG_DEBUG, "%s@(%d,%d) looking for charge; %d/%d",
	  unit_types[punit->type].name,
	  punit->x, punit->y, val, val * 100 / u);
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

  /* keep barbarians aggresive and primitive */
  if( is_barbarian(pplayer) ) {
     if( get_tile_infrastructure_set(map_get_tile(punit->x,punit->y)) &&
         is_land_barbarian(pplayer) )
       punit->ai.ai_role = AIUNIT_PILLAGE;  /* land barbarians pillage */
     else
       punit->ai.ai_role = AIUNIT_ATTACK;
     return;
  }

  if (punit->ai.charge) { /* I am a bodyguard */
    aunit = player_find_unit_by_id(pplayer, punit->ai.charge);
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
    freelog(LOG_DEBUG, "%s@(%d, %d) going to defend %s@(%d, %d)",
		  unit_types[punit->type].name, punit->x, punit->y,
		  acity->name, acity->x, acity->y);
  } else if (aunit) {
    punit->ai.ai_role = AIUNIT_ESCORT;
    punit->ai.charge = aunit->id;
    freelog(LOG_DEBUG, "%s@(%d, %d) going to defend %s@(%d, %d)",
		  unit_types[punit->type].name, punit->x, punit->y,
		  unit_types[aunit->type].name, aunit->x, aunit->y);
  } else if (unit_attack_desirability(punit->type) ||
      (pcity && !same_pos(pcity->x, pcity->y, punit->x, punit->y)))
     punit->ai.ai_role = AIUNIT_ATTACK;
  else punit->ai.ai_role = AIUNIT_DEFEND_HOME; /* for default */
}

void ai_military_gohome(struct player *pplayer,struct unit *punit)
{
  struct city *pcity;
  int dest_x, dest_y;
  if (punit->homecity){
    pcity=find_city_by_id(punit->homecity);
    freelog(LOG_DEBUG, "GOHOME (%d)(%d,%d)C(%d,%d)",
		 punit->id,punit->x,punit->y,pcity->x,pcity->y); 
    if ((punit->x == pcity->x)&&(punit->y == pcity->y)) {
      freelog(LOG_DEBUG, "INHOUSE. GOTO AI_NONE(%d)", punit->id);
      /* aggro defense goes here -- Syela */
      ai_military_findvictim(pplayer, punit, &dest_x, &dest_y);
      punit->ai.ai_role=AIUNIT_NONE;
      handle_unit_move_request(pplayer, punit, dest_x, dest_y, FALSE);
                                       /* might bash someone */
    } else {
      freelog(LOG_DEBUG, "GOHOME(%d,%d)",
		   punit->goto_dest_x, punit->goto_dest_y);
      punit->goto_dest_x=pcity->x;
      punit->goto_dest_y=pcity->y;
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(pplayer,punit,GOTO_MOVE_ANY);
    }
  } else {
    handle_unit_activity_request(punit, ACTIVITY_FORTIFYING);
  }
}

/*************************************************************************
...
**************************************************************************/
int find_something_to_kill(struct player *pplayer, struct unit *punit, int *x, int *y)
{
  int a=0, b, c, d, e, m, n, v, i, f, a0, b0, ab, g;
  int aa = 0, bb = 0, cc = 0, dd = 0, bestb0 = 0;
  int con = map_get_continent(punit->x, punit->y);
  struct player *aplayer;
  struct unit *pdef;
  int best = 0, maxd, boatid = 0;
  int harborcity = 0, bx = 0, by = 0;
  int fprime, handicap;
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
    if (aplayer == pplayer) continue;
    /* AI will try to conquer only enemy cities. -- Nb */
    city_list_iterate(aplayer->cities, acity)
      city_reinforcements_cost_and_value(acity, punit);
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

  if (pcity && (!punit->id || pcity->id == punit->homecity))
    unhap = ai_assess_military_unhappiness(pcity, get_gov_pplayer(pplayer));

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

  generate_warmap(map_get_city(*x, *y), punit);
                             /* most flexible but costs milliseconds */
  freelog(LOG_DEBUG, "%s's %s at (%d, %d) has belligerence %d.",
		pplayer->name, unit_types[punit->type].name,
		punit->x, punit->y, a);

  if (is_ground_unit(punit)) boatid = find_boat(pplayer, &bx, &by, 2);
  if (boatid) ferryboat = player_find_unit_by_id(pplayer, boatid);
  if (ferryboat) really_generate_warmap(map_get_city(ferryboat->x, ferryboat->y),
                       ferryboat, SEA_MOVING);

  if (ferryboat) boatspeed = (unit_flag(ferryboat->type, F_TRIREME) ? 6 : 12);
  else boatspeed = (get_invention(pplayer, game.rtech.nav) != TECH_KNOWN ? 6 : 12);

  if (is_ground_unit(punit) && !punit->id &&
      is_terrain_near_tile(punit->x, punit->y, T_OCEAN)) harborcity++;

  handicap=ai_handicap(pplayer, H_TARGETS);
  for (i = 0; i < game.nplayers; i++) {
    aplayer = &game.players[i];
    if (aplayer != pplayer) { /* enemy */
      city_list_iterate(aplayer->cities, acity)
        if (handicap && !map_get_known(acity->x, acity->y, pplayer)) continue;
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

	  if (punit->id && ferryboat && is_ground_unit(punit)) {
	    freelog(LOG_DEBUG, "%s@(%d, %d) -> %s@(%d, %d) -> %s@(%d, %d)"
		    " (sanity=%d, c=%d, e=%d, best=%d)",
		    unit_types[punit->type].name, punit->x, punit->y,
		    unit_types[ferryboat->type].name, bx, by,
		    acity->name, acity->x, acity->y, sanity, c, e, best);
	  }

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
        if (handicap && !map_get_known(aunit->x, aunit->y, pplayer)) continue;
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

#ifdef DEBUG
  if (best && map_get_city(*x, *y) && !punit->id) {
    freelog(LOG_DEBUG,
	    "%s's %s#%d at (%d, %d) targeting (%d, %d) [desire = %d/%d]",
	    pplayer->name, unit_types[punit->type].name, punit->id,
	    punit->x, punit->y, *x, *y, best, bestb0);
    freelog(LOG_DEBUG, "A = %d, B = %d, C = %d, D = %d, F = %d, E = %d",
	    aa, bb, cc, dd, f, best);
  }
  if (punit->id && (pcity = map_get_city(*x, *y)) &&
      pcity->owner != punit->owner && pcity->ai.invasion == 2 &&
      (is_ground_unit(punit) || is_heli_unit(punit))) {
    freelog(LOG_DEBUG, "%s's %s#%d@(%d,%d) invading %s@(%d,%d)",
	    pplayer->name, unit_name(punit->type), punit->id,
	    punit->x, punit->y, pcity->name, *x, *y);
  }
  if (!best && bk) {
    freelog(LOG_DEBUG, "%s's %s@(%d,%d) faking target %s@(%d,%d)",
	    pplayer->name, unit_name(punit->type), punit->x, punit->y, 
	    map_get_city(*x, *y)->name, *x, *y);
  }
#endif
  return(best);
}

static int find_nearest_friendly_port(struct unit *punit)
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
  freelog(LOG_DEBUG, "Friendly port nearest to (%d,%d) is %s@(%d,%d) [%d]",
		punit->x, punit->y,
		map_get_city(punit->goto_dest_x, punit->goto_dest_y)->name,
		punit->goto_dest_x, punit->goto_dest_y, best);
  return 1;
}

/*************************************************************************
This seems to do the attack. First find victim on the neighbouring tiles.
If no enemies nearby find_something_to_kill() anywhere else. If there is
nothing to kill, sailing units go home, others explore.
**************************************************************************/
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
            if (find_nearest_friendly_port(punit))
	      do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
          } else {
            ai_manage_explorer(pplayer, punit); /* nothing else to do */
            /* you can still have some moves left here, but barbarians should
               not sit helplessly, but advance towards nearest known enemy city */
	    punit = find_unit_by_id(id);   /* unit might die while exploring */
            if( punit && punit->moves_left && is_barbarian(pplayer) ) {
              struct city *pc;
              int fx, fy;
              freelog(LOG_DEBUG,"Barbarians looking for target");
              if( (pc = dist_nearest_city(pplayer, punit->x, punit->y, 0, 1)) ) {
                if( map_get_terrain(punit->x,punit->y) != T_OCEAN ) {
                  freelog(LOG_DEBUG,"Marching to city");
                  ai_military_gothere(pplayer, punit, pc->x, pc->y);
                }
                else {
                  /* sometimes find_beachhead is not enough */
                  if( !find_beachhead(punit, pc->x, pc->y, &fx, &fy) )
                    find_city_beach(pc, punit, &fx, &fy);           
                  freelog(LOG_DEBUG,"Sailing to city");
                  ai_military_gothere(pplayer, punit, fx, fy);
                }
              }
            }
          }
          return; /* Jane, stop this crazy thing! */
        } else if (!is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y)) {
/* if what we want to kill is adjacent, and findvictim didn't want it, WAIT! */
          if ((went = ai_military_gothere(pplayer, punit, dest_x, dest_y))) {
            if (went > 0) flag = punit->moves_left;
            else punit = 0;
          } /* else we're having ZOC hell and need to break out of the loop */
        } /* else nothing to kill */
      } else { /* goto does NOT work for fast units */
	freelog(LOG_DEBUG, "%s's %s at (%d, %d) bashing (%d, %d)",
		      pplayer->name, unit_types[punit->type].name,
		      punit->x, punit->y, dest_x, dest_y); 
        handle_unit_move_request(pplayer, punit, dest_x, dest_y, FALSE);
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
  int tradeval, best_city = -1, best=0;

  if (punit->activity != ACTIVITY_IDLE)
    return;
  if (punit->ai.ai_role == AIUNIT_NONE) {
    if ((pcity = wonder_on_continent(pplayer, map_get_continent(punit->x, punit->y))) &&
	build_points_left(pcity) > (pcity->shield_surplus*2)) {
      if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
        if (!punit->moves_left) return;
	auto_settler_do_goto(pplayer,punit, pcity->x, pcity->y);
        handle_unit_activity_request(punit, ACTIVITY_IDLE);
      } else {
      /*
       * We really don't want to just drop all caravans in immediately.
       * Instead, we want to aggregate enough caravans to build instantly.
       * -AJS, 990704
       */
	req.unit_id = punit->id;
	req.city_id = pcity->id;
	handle_unit_help_build_wonder(pplayer, &req);
      }
    }
     else {
       /* A caravan without a home?  Kinda strange, but it might happen.  */
       pcity=player_find_city_by_id(pplayer, punit->homecity);
       city_list_iterate(pplayer->cities,pdest)
         if (pcity && can_establish_trade_route(pcity,pdest) &&
            map_same_continent(pcity->x, pcity->y, pdest->x, pdest->y)) {
           tradeval=trade_between_cities(pcity, pdest);
           if (tradeval) {
             if (best < tradeval) {
               best=tradeval;
               best_city=pdest->id;
             }
           }
         }
       city_list_iterate_end;
       pcity=player_find_city_by_id(pplayer, best_city);
       if (pcity) {
         if (!same_pos(pcity->x, pcity->y, punit->x, punit->y)) {
           if (!punit->moves_left) return;
           auto_settler_do_goto(pplayer,punit, pcity->x, pcity->y);
         } else {
           req.unit_id = punit->id;
           req.city_id = pcity->id;
           handle_unit_establish_trade(pplayer, &req);
        }
      }
    }
  }
}

/**************************************************************************
This seems to manage the ferryboat. When it carries units on their way
to invade something, it goes there. If it carries other units, it returns home.
When empty, it tries to find some units to carry or goes home or explores.
Military units handled by ai_manage_military()
**************************************************************************/

static void ai_manage_ferryboat(struct player *pplayer, struct unit *punit)
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
	if (pcity) {
	  freelog(LOG_DEBUG, "Ferrying to %s to %s, invasion = %d, body = %d",
		  unit_name(aunit->type), pcity->name,
		  pcity->ai.invasion, aunit->ai.bodyguard);
	}
        n++;
        set_unit_activity(aunit, ACTIVITY_SENTRY);
        if (bodyguard) set_unit_activity(bodyguard, ACTIVITY_SENTRY);
      }
    }
  unit_list_iterate_end;
  if (p) {
    freelog(LOG_DEBUG, "%s#%d@(%d,%d), p=%d, n=%d",
		  unit_name(punit->type), punit->id, punit->x, punit->y, p, n);
    if (punit->moves_left && n)
      do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
    else if (!n && !map_get_city(punit->x, punit->y)) { /* rest in a city, for unhap */
      x = punit->goto_dest_x; y = punit->goto_dest_y;
      if (find_nearest_friendly_port(punit))
	do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);
      if (!player_find_unit_by_id(pplayer, id)) return; /* oops! */
      punit->goto_dest_x = x; punit->goto_dest_y = y;
      send_unit_info(pplayer, punit); /* to get the crosshairs right -- Syela */
    } else {
      freelog(LOG_DEBUG, "Ferryboat %d@(%d,%d) stalling.",
		    punit->id, punit->x, punit->y);
      if(is_barbarian(pplayer)) /* just in case */
        ai_manage_explorer(pplayer, punit);
    }
    return;
  }

  /* check if barbarian boat is empty and so not needed - the crew has landed */
  if( is_barbarian(pplayer) && unit_list_size(&map_get_tile(punit->x, punit->y)->units)<2 ) {
    wipe_unit(punit);
    return;
  }

/* ok, not carrying anyone, even the ferryman */
  punit->ai.passenger = 0;
  freelog(LOG_DEBUG, "Ferryboat %d@(%d, %d) is lonely.",
		punit->id, punit->x, punit->y);
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
          ground_unit_transporter_capacity(aunit->x, aunit->y, pplayer->player_no) <= 0 &&
	  ai_fuzzy(pplayer,1)) {
      freelog(LOG_DEBUG, "Found a friend %d@(%d, %d)",
		    aunit->id, aunit->x, aunit->y);
      x = aunit->x;
      y = aunit->y;
      best = warmap.seacost[x][y];
    }
    if (is_sailing_unit(aunit) && map_get_terrain(aunit->x, aunit->y) == T_OCEAN) p++;
  unit_list_iterate_end;
  if (best < 4 * unit_types[punit->type].move_rate) {
    punit->goto_dest_x = x;
    punit->goto_dest_y = y;
    set_unit_activity(punit, ACTIVITY_GOTO);
    do_unit_goto(pplayer,punit,GOTO_MOVE_ANY);
    if ((punit = player_find_unit_by_id(pplayer, id)))
      set_unit_activity(punit, ACTIVITY_IDLE);
    return;
  }

/* do cool stuff here */

  if (!punit->moves_left) return;  
  pcity = find_city_by_id(punit->homecity);
  if (pcity) {
    if (!ai_handicap(pplayer, H_TARGETS) ||
        unit_move_turns(punit, pcity->x, pcity->y) < p) {
      freelog(LOG_DEBUG, "No friends.  Going home.");
      punit->goto_dest_x = pcity->x;
      punit->goto_dest_y = pcity->y;
      set_unit_activity(punit, ACTIVITY_GOTO);
      do_unit_goto(pplayer,punit,GOTO_MOVE_ANY);
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

  id = punit->id;

  if (punit->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(punit, ACTIVITY_IDLE);

  punit->ai.ai_role = AIUNIT_NONE;
  /* was getting a bad bug where a settlers caused a defender to leave home */
  /* and then all other supported units went on DEFEND_HOME/goto */
  ai_military_findjob(pplayer, punit);

#ifdef DEBUG
  {
    struct city *pcity;
    if (unit_flag(punit->type, F_FIELDUNIT)
	&& (pcity = map_get_city(punit->x, punit->y))) {
      freelog(LOG_DEBUG, "%s in %s is going to %d", unit_name(punit->type),
	      pcity->name, punit->ai.ai_role);
    }
  }
#endif
  
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
    case AIUNIT_PILLAGE:
      handle_unit_activity_request(punit, ACTIVITY_PILLAGE);
      return; /* when you pillage, you have moves left, avoid later fortify */
      break;
    case AIUNIT_EXPLORE:
      ai_manage_explorer(pplayer, punit);
      break;
  }

  if ((punit = find_unit_by_id(id))) {
    if (punit->activity != ACTIVITY_IDLE &&
        punit->activity != ACTIVITY_GOTO)
      handle_unit_activity_request(punit, ACTIVITY_IDLE); 

    if (punit->moves_left) {
      if (unit_list_find(&(map_get_tile(punit->x, punit->y)->units),
          punit->ai.ferryboat))
        handle_unit_activity_request(punit, ACTIVITY_SENTRY);
      else 
        handle_unit_activity_request(punit, ACTIVITY_FORTIFYING);
    } /* better than doing nothing */
  }
}

/**************************************************************************
  Barbarian units may disband spontaneously if their age is more then 5,
  they are not in cities, and they are far from any enemy units. It is to 
  remove barbarians that do not engage into any activity for a long time.
**************************************************************************/
static int unit_can_be_retired(struct unit *punit)
{
  int x, y, x1;

  if( punit->fuel ) {   /* fuel abused for barbarian life span */
    punit->fuel--;
    return 0;
  }

  if (is_allied_city_tile(map_get_tile(punit->x, punit->y), punit->owner))
    return 0;

  /* check if there is enemy nearby */
  for(x = punit->x - 3; x < punit->x + 4; x++)
    for(y = punit->y - 3; y < punit->y + 4; y++) { 
      if( y < 0 || y > map.ysize )
        continue;
      x1 = map_adjust_x(x);
      if (is_enemy_city_tile(map_get_tile(x1, y), punit->owner) ||
          is_enemy_unit_tile(map_get_tile(x1, y), punit->owner))
        return 0;
    }

  return 1;
}

/**************************************************************************
 manage one unit
 Careful: punit may have been destroyed upon return from this routine!
**************************************************************************/
static void ai_manage_unit(struct player *pplayer, struct unit *punit)
{
  /* retire useless barbarian units here, before calling the management
     function */
  if( is_barbarian(pplayer) ) {
    if( unit_can_be_retired(punit) && myrand(100) > 90 ) {
      wipe_unit(punit);
      return;
    }
    if( !is_military_unit(punit)
	&& !unit_has_role(punit->type, L_BARBARIAN_LEADER)) {
      freelog(LOG_VERBOSE, "Barbarians picked up non-military unit.");
      return;
    }
  }

  if ((unit_flag(punit->type, F_DIPLOMAT))
      || (unit_flag(punit->type, F_SPY))) {
    ai_manage_diplomat(pplayer, punit);
    /* the right test if the unit is in a city and 
       there is no other diplomat it musn't move.
       This unit is a bodyguard against enemy diplomats.
       Right now I don't know how to use bodyguards! (17/12/98) (--NB)
    */
    return;
  } else if (unit_flag(punit->type, F_SETTLERS)
	     ||unit_flag(punit->type, F_CITIES)) {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_settler(pplayer, punit);
    return;
  } else if (unit_flag(punit->type, F_CARAVAN)) {
    ai_manage_caravan(pplayer, punit);
    return;
  } else if (unit_has_role(punit->type, L_BARBARIAN_LEADER)) {
    ai_manage_barbarian_leader(pplayer, punit);
    return;
  } else if (get_transporter_capacity(punit)) {
    ai_manage_ferryboat(pplayer, punit);
    return;
  } else if (is_military_unit(punit)) {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_military(pplayer,punit); 
    return;
  } else {
    if (!punit->moves_left) return; /* can't do anything with no moves */
    ai_manage_explorer(pplayer, punit); /* what else could this be? -- Syela */
    return;
  }
  /* should never get here */
}

/**************************************************************************
 do all the gritty nitty chess like analysis here... (argh)
**************************************************************************/
void ai_manage_units(struct player *pplayer) 
{
  static struct timer *t = NULL;      /* alloc once, never free */
  static int *unitids = NULL;         /* realloc'd, never freed */
  static int max_units = 0;

  int count, index;
  struct unit *punit;
  char *type_name;

  t = renew_timer_start(t, TIMER_CPU, TIMER_DEBUG);

  freelog(LOG_DEBUG, "Managing units for %s", pplayer->name);

  count = genlist_size(&(pplayer->units.list));
  if (count > 0) {
    if (max_units < count) {
      max_units = count;
      unitids = fc_realloc(unitids, max_units * sizeof(int));
    }
    index = 0;
    unit_list_iterate(pplayer->units, punit) {
      unitids[index++] = punit->id;
    }
    unit_list_iterate_end;

    for (index = 0; index < count; index++) {
      punit = player_find_unit_by_id(pplayer, unitids[index]);
      if (!punit) {
	freelog(LOG_DEBUG, "Can't manage %s's dead unit %d", pplayer->name,
		unitids[index]);
	continue;
      }
      type_name = unit_types[punit->type].name;

      freelog(LOG_DEBUG, "Managing %s's %s %d@(%d,%d)", pplayer->name,
	      type_name, unitids[index], punit->x, punit->y);

      ai_manage_unit(pplayer, punit);
      /* Note punit might be gone!! */

      freelog(LOG_DEBUG, "Finished managing %s's %s %d", pplayer->name,
	      type_name, unitids[index]);
    }
  }

  freelog(LOG_DEBUG, "Managed %s's units successfully.", pplayer->name);

  if (timer_in_use(t)) {
    freelog(LOG_VERBOSE, "%s's units consumed %g milliseconds.",
	    pplayer->name, 1000.0*read_timer_seconds(t));
  }
}

/**************************************************************************
 Assign tech wants for techs to get better units with given role/flag.
 Returns the best we can build so far, or U_LAST if none.  (dwp)
**************************************************************************/
int ai_wants_role_unit(struct player *pplayer, struct city *pcity,
		       int role, int want)
{
  int i, n, iunit, itech;

  n = num_role_units(role);
  for (i=n-1; i>=0; i--) {
    iunit = get_role_unit(role, i);
    if (can_build_unit(pcity, iunit)) {
      return iunit;
    } else {
      /* careful; might be unable to build for non-tech reason... */
      itech = get_unit_type(iunit)->tech_requirement;
      if (get_invention(pplayer, itech) != TECH_KNOWN) {
	pplayer->ai.tech_want[itech] += want;
      }
    }
  }
  return U_LAST;
}

/**************************************************************************
 As ai_wants_role_unit, but also set choice->choice if we can build something.
**************************************************************************/
void ai_choose_role_unit(struct player *pplayer, struct city *pcity,
			 struct ai_choice *choice, int role, int want)
{
  int iunit = ai_wants_role_unit(pplayer, pcity, role, want);
  if (iunit != U_LAST)
    choice->choice = iunit;
}

/**************************************************************************
 Whether unit_type test is on the "upgrade path" of unit_type base,
 even if we can't upgrade now.
**************************************************************************/
int is_on_unit_upgrade_path(int test, int base)
{
#if 0
  /* This is a hack for regression testing; I believe the new version
   * is actually better (at least as it is used in aicity.c)  --dwp
   */
  return (base==U_WARRIORS && test==U_PHALANX)
    || (base==U_PHALANX && test==U_MUSKETEERS);
#else
  /* This is the real function: */
  do {
    base = unit_types[base].obsoleted_by;
    if (base == test) {
      return 1;
    }
  } while (base != -1);
  return 0;
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
    && !unit_flag(type, F_NO_LAND_ATTACK)
    && !(get_unit_type(type)->transport_capacity >= 8);
}

/*
 * If we are the only diplomat in a city, defend against enemy actions.
 * The passive defense is set by game.diplchance.  The active defense is
 * to bribe units which end their move nearby.
 * Our next trick is to look for enemy units and cities on our continent.
 * If we find a city, we look first to establish an embassy.  The
 * information gained this way is assumed to be more useful than anything
 * else we could do.  Making this come true is for future code.  -AJS
 */
static void ai_manage_diplomat(struct player *pplayer, struct unit *pdiplomat)
{
  int i, x, y, handicap, has_emb, continent, dist, rmd, oic, did;
  struct packet_unit_request req;
  struct packet_diplomat_action dact;
  struct city *pcity, *ctarget;
  struct tile *ptile;
  struct unit *ptres;
  struct player *aplayer = NULL;

  if (pdiplomat->activity != ACTIVITY_IDLE)
    handle_unit_activity_request(pdiplomat, ACTIVITY_IDLE);

  pcity = map_get_city(pdiplomat->x, pdiplomat->y);

  if (pcity && count_diplomats_on_tile(pdiplomat->x, pdiplomat->y) == 1) {
    /* We're the only diplomat in a city.  Defend it. */
    if (pdiplomat->homecity != pcity->id) {
      /* this may be superfluous, but I like it.  -AJS */
      req.unit_id=pdiplomat->id;
      req.city_id=pcity->id;
      req.name[0]='\0';
      handle_unit_change_homecity(pplayer, &req);
    }
  } else {
    if (pcity) {
      /*
       * More than one diplomat in a city: may try to bribe trespassers.
       * We may want this to be a city_map_iterate, or a warmap distance
       * check, but this will suffice for now.  -AJS
       */
      for (x=-1; x<= 1; x++) {
	for (y=-1; y<= 1; y++) {
	  if (x == 0 && y == 0) continue;
	  if (diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE,
				     pcity->x+x, pcity->y+y)) {
	    /* A lone trespasser! Seize him! -AJS */
	    ptile=map_get_tile(pcity->x+x, pcity->y+y);
	    ptres = unit_list_get(&ptile->units, 0);
	    ptres->bribe_cost=unit_bribe_cost (ptres);
	    if ( ptres->bribe_cost <
		 (pplayer->economic.gold-pplayer->ai.est_upkeep)) {
	      dact.diplomat_id=pdiplomat->id;
	      dact.target_id=ptres->id;
	      dact.action_type=DIPLOMAT_BRIBE;
	      handle_diplomat_action(pplayer, &dact);
	      return;
	    }
	  }
	}
      }
    }
    /*
     *  We're wandering in the desert, or there is more than one diplomat
     *  here.  Go elsewhere.
     *  First, we look for an embassy, to steal a tech, or for a city to
     *  subvert.  Then we look for a city of our own without a defending
     *  diplomat.  This may not prove to work so very well, but it's
     *  possible otherwise that all diplomats we ever produce are home
     *  guard, and that's kind of silly.  -AJS, 20000130
     */
    ctarget=0;
    ptres=0;
    dist=MAX(map.xsize, map.ysize);
    continent=map_get_continent(pdiplomat->x, pdiplomat->y);
    handicap = ai_handicap(pplayer, H_TARGETS);
    for( i = 0; i < game.nplayers; i++) {
      aplayer = &game.players[i];
      if (aplayer == pplayer) continue;
      /* sneaky way of avoiding foul diplomat capture  -AJS */
      has_emb=player_has_embassy(pplayer, aplayer) || pdiplomat->foul;
      city_list_iterate(aplayer->cities, acity)
	if (handicap && !map_get_known(acity->x, acity->y, pplayer)) continue;
	if (continent != map_get_continent(acity->x, acity->y)) continue;
	city_incite_cost(acity);
	/* figure our incite cost */
	oic = acity->incite_revolt_cost;
	if (pplayer->player_no == acity->original) oic = oic / 2;
	rmd=real_map_distance(pdiplomat->x, pdiplomat->y, acity->x, acity->y);
	if (!ctarget || (dist > rmd)) {
	  if (!has_emb || !acity->steal || (oic < 
               pplayer->economic.gold - pplayer->ai.est_upkeep)) {
	    /* We have the closest enemy city so far on the same continent */
	    ctarget = acity;
	    dist = rmd;
	  }
	}
      city_list_iterate_end;
    }
    if (!ctarget && aplayer) {
      /* No enemy cities are useful.  Check our own. -AJS */
      city_list_iterate(aplayer->cities, acy)
	if (continent != map_get_continent(acy->x, acy->y)) continue;
	if (!count_diplomats_on_tile(acy->x, acy->y)) {
	  ctarget=acy;
	  dist=real_map_distance(pdiplomat->x, pdiplomat->y, acy->x, acy->y);
	  /* keep dist's integrity, and we can use it later. -AJS */
	}
      city_list_iterate_end;
    }
    if (ctarget) {
      /* Otherwise, we just kinda sit here.  -AJS */
      did = -1;
      if ((dist == 1) && (pplayer->player_no != ctarget->owner)) {
	dact.diplomat_id=pdiplomat->id;
	dact.target_id=ctarget->id;
	if (!pdiplomat->foul && diplomat_can_do_action(pdiplomat,
	     DIPLOMAT_EMBASSY, ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_EMBASSY;
	    handle_diplomat_action(pplayer, &dact);
	} else if (!ctarget->steal && diplomat_can_do_action(pdiplomat,
		    DIPLOMAT_STEAL, ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_STEAL;
	    handle_diplomat_action(pplayer, &dact);
	} else if (diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE,
		   ctarget->x, ctarget->y)) {
	    did=pdiplomat->id;
	    dact.action_type=DIPLOMAT_INCITE;
	    handle_diplomat_action(pplayer, &dact);
	}
      }
      if ((did < 0) || find_unit_by_id(did)) {
	pdiplomat->goto_dest_x=ctarget->x;
	pdiplomat->goto_dest_y=ctarget->y;
	set_unit_activity(pdiplomat, ACTIVITY_GOTO);
	do_unit_goto(pplayer, pdiplomat, GOTO_MOVE_ANY);
      }
    }
  }
  return;
}

/*************************************************************************
Barbarian leader tries to stack with other barbarian units, and if it's
not possible it runs away. When on coast, it may disappear with 33% chance.
**************************************************************************/
static void ai_manage_barbarian_leader(struct player *pplayer, struct unit *leader)
{
  int con = map_get_continent(leader->x, leader->y), i, x, y, dx, dy,
    safest = 0, safest_x = leader->x, safest_y = leader->y;
  struct unit *closest_unit = NULL;
  int dist, mindist = 10000;

  if (leader->moves_left == 0 || 
      (map_get_terrain(leader->x, leader->y) != T_OCEAN &&
       unit_list_size(&(map_get_tile(leader->x, leader->y)->units)) > 1) ) {
      handle_unit_activity_request(leader, ACTIVITY_SENTRY);
      return;
  }
  /* the following takes much CPU time and could be avoided */
  generate_warmap(map_get_city(leader->x, leader->y), leader);

  /* duck under own units */
  unit_list_iterate(pplayer->units, aunit) {
    if (unit_has_role(aunit->type, L_BARBARIAN_LEADER)
	|| !is_ground_unit(aunit)
	|| map_get_continent(aunit->x, aunit->y) != con)
      continue;

    if (warmap.cost[aunit->x][aunit->y] < mindist) {
      mindist = warmap.cost[aunit->x][aunit->y];
      closest_unit = aunit;
    }
  } unit_list_iterate_end;

  if (closest_unit != NULL
      && !same_pos(closest_unit->x, closest_unit->y, leader->x, leader->y)
      && map_same_continent(leader->x, leader->y, closest_unit->x, closest_unit->y)) {
    auto_settler_do_goto(pplayer, leader, closest_unit->x, closest_unit->y);
    handle_unit_activity_request(leader, ACTIVITY_IDLE);
    return; /* sticks better to own units with this -- jk */
  }

  freelog(LOG_DEBUG, "Barbarian leader needs to flee");
  mindist = 1000000;
  closest_unit = NULL;
  for (i = 0; i < game.nplayers; i++) {
    unit_list_iterate(game.players[i].units, aunit) {
      if (is_military_unit(aunit)
	  && is_ground_unit(aunit)
	  && map_get_continent(aunit->x, aunit->y) == con) {
	/* questionable assumption: aunit needs as many moves to reach us as we
	   need to reach it */
	dist = warmap.cost[aunit->x][aunit->y] - unit_move_rate(aunit);
	if (dist < mindist) {
	  freelog(LOG_DEBUG, "Barbarian leader: closest enemy is %s at %d, %d, dist %d",
                  unit_name(aunit->type), aunit->x, aunit->y, dist);
	  mindist = dist;
	  closest_unit = aunit;
	}
      }
    } unit_list_iterate_end;
  }

  /* Disappearance - 33% chance on coast, when older than barbarian life span */
  if( is_at_coast(leader->x, leader->y) && !leader->fuel) {
    if(myrand(3) == 0) {
      freelog(LOG_DEBUG, "Barbarian leader disappeared at %d %d", leader->x, leader->y);
      wipe_unit(leader);
      return;
    }
  }

  if (closest_unit == NULL) {
    handle_unit_activity_request(leader, ACTIVITY_IDLE);
    freelog(LOG_DEBUG, "Barbarian leader: No enemy.");
    return;
  }

  generate_warmap(map_get_city(closest_unit->x, closest_unit->y), closest_unit);

  do {
    int last_x, last_y;
    freelog(LOG_DEBUG, "Barbarian leader: moves left: %d\n", leader->moves_left);
    for (dx = -1; dx <= 1; dx++) {
      for (dy = -1; dy <= 1; dy++) {
	x = map_adjust_x(leader->x + dx);
	y = map_adjust_x(leader->y + dy);

	if (warmap.cost[x][y] > safest
	    && can_unit_move_to_tile(leader, x, y, FALSE)) {
	  safest = warmap.cost[x][y];
	  freelog(LOG_DEBUG, "Barbarian leader: safest is %d, %d, safeness %d",
                  x, y, safest);
	  safest_x = x;
	  safest_y = y;
	}
      }
    }

    if (same_pos(leader->x, leader->y, safest_x, safest_y)) {
      freelog(LOG_DEBUG, "Barbarian leader reached the safest position.");
      handle_unit_activity_request(leader, ACTIVITY_IDLE);
      return;
    }

    freelog(LOG_DEBUG, "Fleeing to %d, %d.", safest_x, safest_y);
    last_x = leader->x;
    last_y = leader->y;
    auto_settler_do_goto(pplayer, leader, safest_x, safest_y);
    if (leader->x == last_x && leader->y == last_y) {
      /* Deep inside the goto handling code, in 
	 server/unithand.c::handle_unite_move_request(), the server
	 may decide that a unit is better off not moving this turn,
	 because the unit doesn't have quite enough movement points
	 remaining.  Unfortunately for us, this favor that the server
	 code does may lead to an endless loop here in the barbarian
	 leader code:  the BL will try to flee to a new location, execute 
	 the goto, find that it's still at its present (unsafe) location,
	 and repeat.  To break this loop, we test for the condition
	 where the goto doesn't do anything, and break if this is
	 the case. */
      break;
    }
  } while (leader->moves_left > 0);
}

