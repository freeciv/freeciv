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
#include <string.h>

#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"

#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"		/* warmap has been redeployed */
#include "settlers.h"
#include "unittools.h"		/* for get_defender, amazingly */

#include "aicity.h"
#include "aitools.h"
#include "aiunit.h"

#include "advmilitary.h"

/********************************************************************** 
 This function should assign a value to choice and want, where 
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

/********************************************************************** 
Need positive feedback in m_a_c_b and bodyguard routines. -- Syela
***********************************************************************/
int assess_defense_quadratic(struct city *pcity)
{
  int v, def, l;
  int igwall = 0; /* this can be an arg if needed, but seems unneeded */
  def = 0;
  for (l = 0; l * l < pcity->ai.wallvalue * 10; l++) ;
/* wallvalue = 10, l = 10, wallvalue = 40, l = 20, wallvalue = 90, l = 30 */
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    v = get_defense_power(punit) * punit->hp *
        (is_sailing_unit(punit) ? 1 : get_unit_type(punit->type)->firepower);
    if (is_ground_unit(punit)) v *= 1.5;
    v /= 30;
    if (!igwall && city_got_citywalls(pcity) && is_ground_unit(punit)) {
      v *= l; v /= 10;
    }
    if (is_military_unit(punit)) def += v;
  unit_list_iterate_end;
  if (def > 1<<12) {
    freelog(LOG_VERBOSE, "Very large def in assess_defense_quadratic: %d in %s",
	 def, pcity->name);
  }
  if (def > 1<<15) def = 1<<15; /* more defense than we know what to do with! */
  return(def * def);
}

/**************************************************************************
one unit only, mostly for findjob; handling boats correctly 980803 -- Syela
**************************************************************************/
int assess_defense_unit(struct city *pcity, struct unit *punit, int igwall)
{
  int v;
  v = get_defense_power(punit) * punit->hp *
      (is_sailing_unit(punit) ? 1 : get_unit_type(punit->type)->firepower);
  if (is_ground_unit(punit)) v *= 1.5;
  v /= 30;
  v *= v;
  if (!igwall && city_got_citywalls(pcity) && is_ground_unit(punit)) {
    v *= pcity->ai.wallvalue; v /= 10;
  }
  if (is_military_unit(punit)) return(v);
  return(0);
}

/********************************************************************** 
 Most of the time we don't need/want positive feedback. -- Syela
***********************************************************************/
static int assess_defense_backend(struct city *pcity, int igwall)
{
  int def;
  def = 0;
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    def += assess_defense_unit(pcity, punit, igwall);
  unit_list_iterate_end;
  /* def is an estimate of our total defensive might */
  /* now with regard to the IGWALL nature of the units threatening us -- Syela */
  return(def);
/* unclear whether this should treat settlers/caravans as defense -- Syela */
}

/************************************************************************** 
...
**************************************************************************/
int assess_defense(struct city *pcity)
{
  return(assess_defense_backend(pcity, 0));
}

/************************************************************************** 
...
**************************************************************************/
static int assess_defense_igwall(struct city *pcity)
{
  return(assess_defense_backend(pcity, 1));
}

/************************************************************************** 
...
**************************************************************************/
static int dangerfunct(int v, int m, int dist)
{
#ifdef OLDCODE
  if (dist * dist < m * 3) { v *= m; v /= 3; } /* knights can't attack more than twice */
  else { v *= m * m; v /= dist * dist; }
  return(v);
#else
  int num, denom;
  num = m * 4; denom = m * 4;
  v *= 2;
  while (dist >= m) {
    v /= 2;
    dist -= m;
  }
  m /= SINGLE_MOVE;
  while (dist && dist >= m) {
    num *= 4;
    denom *= 5;
    dist -= m;
  }
  while (dist) {
    denom += (denom + m * 2) / (m * 4);
    dist--;
  }
  v = (v*num + (denom/2)) / denom;
  return(v);
#endif
}

/************************************************************************** 
...
**************************************************************************/
static int assess_danger_unit(struct city *pcity, struct unit *punit)
{
  int v, sailing;
  struct unit_type *ptype;

  if (unit_flag(punit->type, F_NO_LAND_ATTACK)) return(0);
  sailing = is_sailing_unit(punit);
  if (sailing && !is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN)) return(0);

  ptype=get_unit_type(punit->type);
/* get_attack_power will be wrong if moves_left == 1 || == 2 */
  v = ptype->attack_strength * 10 * punit->hp * ptype->firepower;
  if (punit->veteran) v += v/2;
  if (sailing && city_got_building(pcity, B_COASTAL)) v /= 2;
  if (is_air_unit(punit) && city_got_building(pcity, B_SAM)) v /= 2;
  v /= 30; /* rescaling factor to stop the overflow nonsense */
  return(v);
}

/************************************************************************** 
...
**************************************************************************/
static int assess_distance(struct city *pcity, struct unit *punit, int m,
			   int boatid, int boatdist, int boatspeed)
{
  int x, y, dist;
  if (is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y)) dist = SINGLE_MOVE;
  else if (is_sailing_unit(punit)) dist = warmap.seacost[punit->x][punit->y];
  else if (!is_ground_unit(punit))
    dist = real_map_distance(punit->x, punit->y, pcity->x, pcity->y) * SINGLE_MOVE;
  else if (unit_flag(punit->type, F_IGTER))
    dist = real_map_distance(punit->x, punit->y, pcity->x, pcity->y);
  else dist = warmap.cost[punit->x][punit->y];
/* if dist = 9, a chariot is 1.5 turns away.  NOT 2 turns away. */
/* Samarkand bug should be obsoleted by re-ordering of events */
  if (dist < SINGLE_MOVE) dist = SINGLE_MOVE;

  if (is_ground_unit(punit) && boatid &&
      find_beachhead(punit, pcity->x, pcity->y, &x, &y)) {
/* this bug is so obvious I can't believe it wasn't discovered sooner. -- Syela */
    y = warmap.seacost[punit->x][punit->y];
    if (y >= 6 * THRESHOLD)
      y = real_map_distance(pcity->x, pcity->y, punit->x, punit->y) * SINGLE_MOVE;
    x = MAX(y, boatdist) * m / boatspeed;
    if (dist > x) dist = x;
    if (dist < SINGLE_MOVE) dist = SINGLE_MOVE;
  }
  return(dist);
}

/********************************************************************** 
  Call assess_danger() for all cities owned by pplayer.
  This is necessary to initialize ... <some ai data>
  before ... <some ai calculations> ...
***********************************************************************/
void assess_danger_player(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity)
    assess_danger(pcity);
  city_list_iterate_end;
}
	  
/********************************************************************** 
...
***********************************************************************/
int assess_danger(struct city *pcity)
{
  int i, danger = 0, v, dist, m;
  Unit_Type_id utype;
  int danger2 = 0; /* linear for walls */
  int danger3 = 0; /* linear for coastal */
  int danger4 = 0; /* linear for SAM */
  int danger5 = 0; /* linear for SDI */
  struct player *aplayer, *pplayer;
  int pikemen = 0;
  int diplomat = 0; /* > 0 mean that this town can defend
		     * against diplomats or spies */
  int urgency = 0;
  int igwall;
  int badmojo = 0;
  int boatspeed;
  int boatid, boatdist;
  int x = pcity->x, y = pcity->y; /* dummy variables */
  struct unit virtualunit;
  struct unit *funit = &virtualunit; /* saves me a lot of typing. -- Syela */

  memset(&virtualunit, 0, sizeof(struct unit));
  pplayer = &game.players[pcity->owner];

  generate_warmap(pcity, 0); /* generates both land and sea maps */

  pcity->ai.grave_danger = 0;
  pcity->ai.diplomat_threat = 0;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (unit_flag(punit->type, F_PIKEMEN)) pikemen++;
    if (unit_flag(punit->type, F_DIPLOMAT)) diplomat++;
  unit_list_iterate_end;

  for(i=0; i<game.nplayers; i++) {
    if (i != pcity->owner) {
      aplayer = &game.players[i];
      boatspeed = (get_invention(aplayer, game.rtech.nav)
		   == TECH_KNOWN ? 12 : 6);
      boatid = find_boat(aplayer, &x, &y, 0);
      if (boatid) boatdist = warmap.seacost[x][y];
      else boatdist = -1;
/* should I treat empty enemy cities as danger? */
/* After much contemplation, I've decided the answer is sometimes -- Syela */
      city_list_iterate(aplayer->cities, acity)
        if (acity->is_building_unit &&
            build_points_left(acity) <= acity->shield_surplus &&
	    ai_fuzzy(pplayer,1)) {
          virtualunit.owner = i;
          virtualunit.x = acity->x;
          virtualunit.y = acity->y;
          utype = acity->currently_building;
          virtualunit.type = utype;
          virtualunit.veteran = do_make_unit_veteran(acity, utype);
          virtualunit.hp = unit_types[utype].hp;
/* yes, I know cloning all this code is bad form.  I don't really
want to write a funct that takes nine ints by reference. -- Syela */
          m = get_unit_type(funit->type)->move_rate;
          v = assess_danger_unit(pcity, funit);
          dist = assess_distance(pcity, funit, m, boatid, boatdist, boatspeed);
          igwall = unit_really_ignores_citywalls(funit);
          if ((is_ground_unit(funit) && v) ||
              (is_ground_units_transport(funit))) {
            if (dist <= m * 3) urgency++;
            if (dist <= m) pcity->ai.grave_danger++;
/* NOTE: This should actually implement grave_danger, which is supposed
to be a feedback-sensitive formula for immediate danger.  I'm having
second thoughts about the best implementation, and therefore this
will have to wait until after 1.7.0.  I don't want to do anything I'm
not totally sure about and can't thoroughly test in the hours before
the release.  The AI is in fact vulnerable to gang-attacks, but I'm
content to let it remain that way for now. -- Syela 980805 */
          }

          if (unit_flag(funit->type, F_HORSE)) {
            if (pikemen) v /= 2;
            else ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				    (v * m / (dist*2)));
          }

        if (unit_flag(funit->type, F_DIPLOMAT) && (dist <= 2 * m)) 
	     pcity->ai.diplomat_threat = !diplomat;

          v *= v;

          if (!igwall) danger2 += v * m / dist;
          else if (is_sailing_unit(funit)) danger3 += v * m / dist;
          else if (is_air_unit(funit) && !unit_flag(funit->type, F_NUCLEAR)) danger4 += v * m / dist;
          if (unit_flag(funit->type, F_MISSILE)) danger5 += v * m / MAX(m, dist);
          if (!unit_flag(funit->type, F_NUCLEAR)) { /* only SDI helps against NUCLEAR */
            v = dangerfunct(v, m, dist);
            danger += v;
            if (igwall) badmojo += v;
          }
        }
      city_list_iterate_end;

      unit_list_iterate(aplayer->units, punit)
        m = get_unit_type(punit->type)->move_rate;
        v = assess_danger_unit(pcity, punit);
        dist = assess_distance(pcity, punit, m, boatid, boatdist, boatspeed);
	igwall = unit_really_ignores_citywalls(punit);
          
        if ((is_ground_unit(punit) && v) ||
            (is_ground_units_transport(punit))) {
          if (dist <= m * 3) urgency++;
          if (dist <= m) pcity->ai.grave_danger++;
/* NOTE: This should actually implement grave_danger, which is supposed
to be a feedback-sensitive formula for immediate danger.  I'm having
second thoughts about the best implementation, and therefore this
will have to wait until after 1.7.0.  I don't want to do anything I'm
not totally sure about and can't thoroughly test in the hours before
the release.  The AI is in fact vulnerable to gang-attacks, but I'm
content to let it remain that way for now. -- Syela 980805 */
        }

        if (unit_flag(punit->type, F_HORSE)) {
          if (pikemen) v /= 2;
	  else ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				  (v * m / (dist*2)));
        }

        if (unit_flag(punit->type, F_DIPLOMAT) && (dist <= 2 * m))
	     pcity->ai.diplomat_threat = !diplomat;

        v *= v;

        if (!igwall) danger2 += v * m / dist;
        else if (is_sailing_unit(punit)) danger3 += v * m / dist;
        else if (is_air_unit(punit) && !unit_flag(punit->type, F_NUCLEAR)) danger4 += v * m / dist;
        if (unit_flag(punit->type, F_MISSILE)) danger5 += v * m / MAX(m, dist);
        if (!unit_flag(punit->type, F_NUCLEAR)) { /* only SDI helps against NUCLEAR */
          v = dangerfunct(v, m, dist);
          danger += v;
          if (igwall) badmojo += v;
        }
      unit_list_iterate_end;
    }
  } /* end for */

  if (!badmojo) pcity->ai.wallvalue = 90;
  else pcity->ai.wallvalue = (danger * 9 - badmojo * 8) * 10 / (danger);
/* afraid *100 would create integer overflow, but *10 surely won't */

  if (danger < 0 || danger > 1<<24) /* I hope never to see this! */
    freelog(LOG_ERROR, "Dangerous danger (%d) in %s.  Beware of overflow.",
	    danger, pcity->name);
  if (danger2 < 0 || danger2 > 1<<24) /* I hope never to see this! */
    freelog(LOG_ERROR, "Dangerous danger2 (%d) in %s.  Beware of overflow.",
	    danger2, pcity->name);
  if (danger3 < 0 || danger3 > 1<<24) /* I hope never to see this! */
    freelog(LOG_ERROR, "Dangerous danger3 (%d) in %s.  Beware of overflow.",
	    danger3, pcity->name);
  if (danger4 < 0 || danger4 > 1<<24) /* I hope never to see this! */
    freelog(LOG_ERROR, "Dangerous danger4 (%d) in %s.  Beware of overflow.",
	    danger4, pcity->name);
  if (danger5 < 0 || danger5 > 1<<24) /* I hope never to see this! */
    freelog(LOG_ERROR, "Dangerous danger5 (%d) in %s.  Beware of overflow.",
	    danger5, pcity->name);
  if (pcity->ai.grave_danger)
    urgency += 10; /* really, REALLY urgent to defend */
  pcity->ai.danger = danger;
  if (pcity->ai.building_want[B_CITY] > 0 && danger2) {
    i = assess_defense(pcity);
    if (!i) pcity->ai.building_want[B_CITY] = 100 + urgency;
    else if (urgency) {
      if (danger2 > i * 2) pcity->ai.building_want[B_CITY] = 200 + urgency;
      else pcity->ai.building_want[B_CITY] = danger2 * 100 / i;
    } else {
      if (danger2 > i) pcity->ai.building_want[B_CITY] = 100;
      else pcity->ai.building_want[B_CITY] = danger2 * 100 / i;
    }
  }
/* My first attempt to allow ng_wa >= 200 led to stupidity in cities with
no defenders and danger = 0 but danger > 0.  Capping ng_wa at 100 + urgency
led to a failure to buy walls as required.  Allowing want > 100 with !urgency
led to the AI spending too much gold and falling behind on science.  I'm
trying again, but this will require yet more tedious observation -- Syela */
  if (pcity->ai.building_want[B_COASTAL] > 0 && danger3) {
    i = assess_defense_igwall(pcity);
    if (!i) pcity->ai.building_want[B_COASTAL] = 100 + urgency;
    else if (urgency) {
      if (danger3 > i * 2) pcity->ai.building_want[B_COASTAL] = 200 + urgency;
      else pcity->ai.building_want[B_COASTAL] = danger3 * 100 / i;
    } else {
      if (danger3 > i) pcity->ai.building_want[B_COASTAL] = 100;
      else pcity->ai.building_want[B_COASTAL] = danger3 * 100 / i;
    }
  }
/* COASTAL and SAM are TOTALLY UNTESTED and could be VERY WRONG -- Syela */
/* update 980803; COASTAL seems to work; still don't know about SAM. -- Syela */
  if (pcity->ai.building_want[B_SAM] > 0 && danger4) {
    i = assess_defense_igwall(pcity);
    if (!i) pcity->ai.building_want[B_SAM] = 100 + urgency;
    else if (danger4 > i * 2) pcity->ai.building_want[B_SAM] = 200 + urgency;
    else pcity->ai.building_want[B_SAM] = danger4 * 100 / i;
  }
  if (pcity->ai.building_want[B_SDI] > 0 && danger5) {
    i = assess_defense_igwall(pcity);
    if (!i) pcity->ai.building_want[B_SDI] = 100 + urgency;
    else if (danger5 > i * 2) pcity->ai.building_want[B_SDI] = 200 + urgency;
    else pcity->ai.building_want[B_SDI] = danger5 * 100 / i;
  }
  pcity->ai.urgency = urgency; /* need to cache this for bodyguards now -- Syela */
  return(urgency);
}

/************************************************************************** 
...
**************************************************************************/
int unit_desirability(Unit_Type_id i, int def)
{
  int cur, a, d;
  cur = get_unit_type(i)->hp;
  if (unit_types[i].move_type != SEA_MOVING || !def)
    cur *= get_unit_type(i)->firepower;
  if (def) cur *= 3;
  else cur *= get_unit_type(i)->move_rate;
  a = get_unit_type(i)->attack_strength;
  d = get_unit_type(i)->defense_strength;
  if (def) cur *= d;
  else if (d > a) return(0);
/*  else if (d < 2) cur = (cur * (a + d))/2; Don't believe in this anymore */
  else cur *= a; /* wanted to rank Legion > Catapult > Archer */
/* which we will do by munging f in the attacker want equations */
  if (unit_flag(i, F_IGTER) && !def) cur *= 3;
  if (unit_flag(i, F_PIKEMEN) && def) cur *= 1.5;
  if (unit_types[i].move_type == LAND_MOVING && def) cur *= 1.5;
  return(cur);  
}

/************************************************************************** 
...
**************************************************************************/
int unit_attack_desirability(Unit_Type_id i)
{
  return(unit_desirability(i, 0));
} 

static void process_defender_want(struct player *pplayer, struct city *pcity,
				  int danger, struct ai_choice *choice)
{
  Unit_Type_id i;
  Unit_Type_id bestid = 0; /* ??? Zero is legal value! (Settlers by default) */
  Tech_Type_id tech_req;
  int j, k, l, m, n;
  int best= 0;
  int walls = city_got_citywalls(pcity);
  int desire[U_LAST]; /* what you get is what you seek */
  int shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);
  int isdef = has_a_normal_defender(pcity);

  memset(desire, 0, sizeof(desire));
  for (i = 0; i < game.num_unit_types; i++) {
    if (!is_ai_simple_military(i)) continue;
    m = unit_types[i].move_type;
    if ((m == LAND_MOVING || m == SEA_MOVING)) {
      k = pplayer->ai.tech_turns[unit_types[i].tech_requirement];
      j = unit_desirability(i, 1);
      if (!isdef && unit_flag(i, F_FIELDUNIT)) j = 0;
      j /= 15; /* good enough, no rounding errors */
      j *= j;
      if (can_build_unit(pcity, i)) {
        if (walls && m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
        if ((j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) &&
            unit_types[i].build_cost <= pcity->shield_stock + 40) {
          best = j;
          bestid = i;
        }
      } else if (k > 0 && (shore || m == LAND_MOVING) &&
                unit_types[i].tech_requirement != A_LAST) {
        if (m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
        l = k * (k + pplayer->research.researchpoints) * game.researchcost /
         (game.year > 0 ? 2 : 4); /* cost (shield equiv) of gaining these techs */
        l /= city_list_size(&pplayer->cities);
/* Katvrr advises that with danger high, l should be weighted more heavily */
        desire[i] = j * danger / (unit_types[i].build_cost + l);
      }
    }
  }
  if (!walls && unit_types[bestid].move_type == LAND_MOVING) {
    best *= pcity->ai.wallvalue;
    best /= 10;
  } /* was getting four-figure desire for battleships otherwise. -- Syela */
/* Phalanx would be 16 * danger / 20.  Pikemen would be 36 * danger / (20 + l) */

  if (best == 0) best = 1;   /* avoid divide-by-zero below */

/* multiply by unit_types[bestid].build_cost / best */
  for (i = 0; i < game.num_unit_types; i++) {
    if (desire[i] && is_ai_simple_military(i)) {
      tech_req = unit_types[i].tech_requirement;
      n = desire[i] * unit_types[bestid].build_cost / best;
      pplayer->ai.tech_want[tech_req] += n; /* not the totally idiotic
      pplayer->ai.tech_want[tech_req] += n * pplayer->ai.tech_turns[tech_req];
                                               I amaze myself. -- Syela */
      freelog(LOG_DEBUG, "%s wants %s for defense with desire %d <%d>",
		    pcity->name, advances[tech_req].name, n, desire[i]);
    }
  }
  choice->choice = bestid;
  choice->want = danger;
  choice->type = CT_DEFENDER;
  return;
}

/************************************************************************** 
n is type of defender, vet is vet status, x,y is location of target
I decided this funct wasn't confusing enough, so I made k_s_w send
it some more variables for it to meddle with -- Syela
**************************************************************************/
static void process_attacker_want(struct player *pplayer,
			    struct city *pcity, int b, Unit_Type_id n,
                            int vet, int x, int y, int unhap, int *e0, int *v,
                            int bx, int by, int boatspeed, int needferry)
{ 
  Unit_Type_id i;
  Tech_Type_id j;
  int a, c, d, e, a0, b0, f, g, fprime;
  int k, l, m, q;
  int shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);
  struct city *acity = map_get_city(x, y);
  int movetype = unit_types[*v].move_type;

  for (i = 0; i < game.num_unit_types; i++) {
    if (!is_ai_simple_military(i)) continue;
    m = unit_types[i].move_type;
    j = unit_types[i].tech_requirement;
    if (j != A_LAST) k = pplayer->ai.tech_turns[j];
    else k = 0;
    if ((m == LAND_MOVING || (m == SEA_MOVING && shore)) && j != A_LAST &&
         (k || !can_build_unit_direct(pcity, unit_types[i].obsoleted_by)) &&
         unit_types[i].attack_strength && /* otherwise we get SIGFPE's */
         m == movetype) { /* I don't think I want the duplication otherwise -- Syela */
      l = k * (k + pplayer->research.researchpoints) * game.researchcost;
      if (game.year > 0) l /= 2;
      else l /= 4; /* cost (shield equiv) of gaining these techs */
      l /= city_list_size(&pplayer->cities);
/* Katvrr advises that with danger high, l should be weighted more heavily */

      a = unit_types[i].attack_strength *  ((m == LAND_MOVING ||
          player_knows_improvement_tech(pplayer, B_PORT)) ? 15 : 10) *
             unit_types[i].firepower * unit_types[i].hp;
      a /= 30; /* scaling factor to prevent integer overflows */
      if (acity) a += acity->ai.a;
      a *= a;
      /* b is unchanged */

      m = unit_types[i].move_rate;
      q = (acity ? 1 : unit_types[n].move_rate * (unit_flag(n, F_IGTER) ? 3 : 1));
      if (unit_flag(i, F_IGTER)) m *= SINGLE_MOVE; /* not quite right */
      if (unit_types[i].move_type == LAND_MOVING) {
        if (boatspeed) { /* has to be a city, so don't bother with q */
          c = (warmap.cost[bx][by] + m - 1) / m + 1 +
                warmap.seacost[x][y] / boatspeed; /* kluge */
          if (unit_flag(i, F_MARINES)) c -= 1;
        } else if (warmap.cost[x][y] <= m) c = 1;
        else c = (warmap.cost[x][y] * q + m - 1) / m;
      } else if (unit_types[i].move_type == SEA_MOVING) {
        if (warmap.seacost[x][y] <= m) c = 1;
        else c = (warmap.seacost[x][y] * q + m - 1) / m;
      } else if (real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE <= m) c = 1;
      else c = real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE * q / m;

      m = get_virtual_defense_power(i, n, x, y);
      m *= unit_types[n].hp * unit_types[n].firepower;
      if (vet) m *= 1.5;
      m /= 30;
      m *= m;
      d = m;

      if (unit_types[i].move_type == LAND_MOVING && acity &&
          c > (player_knows_improvement_tech(city_owner(acity),
					     B_CITY) ? 2 : 4) &&
          !unit_flag(i, F_IGWALL) && !city_got_citywalls(acity)) d *= 9; 

      f = unit_types[i].build_cost;
      fprime = f * 2 * unit_types[i].attack_strength /
           (unit_types[i].attack_strength +
            unit_types[i].defense_strength);

      if (acity) g = unit_list_size(&(map_get_tile(acity->x, acity->y)->units)) + 1;
      else g = 1;
      if (unit_types[i].move_type != LAND_MOVING && !d) b0 = 0;
/* not bothering to s/!d/!pdef here for the time being -- Syela */
      else if ((unit_types[i].move_type == LAND_MOVING ||
              unit_types[i].move_type == HELI_MOVING) && acity &&
              acity->ai.invasion == 2) b0 = f * SHIELD_WEIGHTING;
      else {
        b0 = (b * a - (f + (acity ? acity->ai.f : 0)) * d) * g * SHIELD_WEIGHTING / (a + g * d);
        if (acity && b * acity->ai.a * acity->ai.a > acity->ai.f * d)
          b0 -= (b * acity->ai.a * acity->ai.a - acity->ai.f * d) *
                           g * SHIELD_WEIGHTING / (acity->ai.a * acity->ai.a + g * d);
      }
      if (b0 > 0) {
        b0 -= l * SHIELD_WEIGHTING;
        b0 -= c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
      }
      if (b0 > 0) {
        a0 = amortize(b0, MAX(1, c));
        e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / ((fprime + needferry) * MORT);
      } else e = 0;  
      if (e > 0) {
        if (k) {
          pplayer->ai.tech_want[j] += e;
	  if (movetype == SEA_MOVING) {	   /* huh? */
	    freelog(LOG_DEBUG,
		    "%s wants %s, %s to punish %s@(%d, %d) with desire %d.", 
		    pcity->name, advances[j].name, unit_name(i),
		    (acity ? acity->name : "punit"), x, y, e);
	  } else {
	    freelog(LOG_DEBUG,
		    "%s wants %s, %s for something with desire %d.", 
		    pcity->name, advances[j].name, unit_name(i), e);
	  }
	} else if (e > *e0) {
	  freelog(LOG_DEBUG, "%s overriding %s(%d) with %s(%d)"
		  " [a=%d,b=%d,c=%d,d=%d,f=%d]",
		  pcity->name, unit_name(*v), *e0, unit_name(i), e,
		  a, b, c, d, f);
          *v = i;
          *e0 = e;
        }
      }
    }
  }
}

static void kill_something_with(struct player *pplayer, struct city *pcity, 
				struct unit *myunit, struct ai_choice *choice)
{
  int a, b, c, d, e, f, g; /* variables in the attacker-want equation */
  Unit_Type_id v, n;
  int m, vet, dist, a0, b0, fprime;
  int x, y, unhap = 0;
  struct unit *pdef, *aunit, *ferryboat;
  struct city *acity;
  int boatid = 0, bx = 0, by = 0;
  int needferry = 0, fstk, boatspeed, sanity;

  if (pcity->ai.danger && !assess_defense(pcity)) return;

  if (is_ground_unit(myunit)) boatid = find_boat(pplayer, &bx, &by, 2);

  ferryboat = player_find_unit_by_id(pplayer, boatid);
  /* FIXME: hardcoded boat speed */
 if (ferryboat) boatspeed = (unit_flag(ferryboat->type, F_TRIREME)
			     ? 2*SINGLE_MOVE : 4*SINGLE_MOVE);
  else boatspeed = (get_invention(pplayer, game.rtech.nav) != TECH_KNOWN
		    ? 2*SINGLE_MOVE : 4*SINGLE_MOVE);

  fstk = find_something_to_kill(pplayer, myunit, &x, &y);

  acity = map_get_city(x, y);
  if (!acity) aunit = get_defender(pplayer, myunit, x, y);
  else aunit = 0;
  if (acity && acity->owner == pplayer->player_no) acity = 0;

/* this code block and the analogous block in f_s_t_k should be calls
to the same utility function, but I don't want to break anything right
before the 1.7.0 release so I'm letting this stay ugly. -- Syela */

/* logically we should adjust this for race attack tendencies */
  if ((acity || aunit)) {
    v = myunit->type;
    vet = myunit->veteran;

    a = unit_types[v].attack_strength * (vet ? 15 : 10) *
             unit_types[v].firepower * myunit->hp;
    a /= 30; /* scaling factor to prevent integer overflows */
    if (acity) a += acity->ai.a;
    a *= a;

    if (acity) {
      pdef = get_defender(pplayer, myunit, x, y);

      m = unit_types[v].move_rate;
      if (unit_flag(v, F_IGTER)) m *= 3; /* not quite right */

      sanity = (goto_is_sane(myunit, acity->x, acity->y, 1) &&
              warmap.cost[x][y] <= (MIN(6, m) * THRESHOLD));

      if (is_ground_unit(myunit)) {
        if (!sanity) {
          if (boatid) c = (warmap.cost[bx][by] + m - 1) / m + 1 +
                warmap.seacost[acity->x][acity->y] / boatspeed; /* kluge */
          else c = warmap.seacost[acity->x][acity->y] / boatspeed + 1;
          if (unit_flag(myunit->type, F_MARINES)) c -= 1;
	  freelog(LOG_DEBUG, "%s attempting to attack via ferryboat"
			" (boatid = %d, c = %d)", unit_types[v].name, boatid, c);
        } else c = (warmap.cost[acity->x][acity->y] + m - 1) / m;
      } else if (is_sailing_unit(myunit))
        c = (warmap.seacost[acity->x][acity->y] + m - 1) / m;
      else c = real_map_distance(myunit->x, myunit->y, acity->x, acity->y) * SINGLE_MOVE / m;

      n = ai_choose_defender_versus(acity, v);
      m = get_virtual_defense_power(v, n, x, y);
      m *= unit_types[n].hp * unit_types[n].firepower;
      if (do_make_unit_veteran(acity, n)) m *= 1.5;
      m /= 30;
      if (c > 1) {
        d = m * m;
        b = unit_types[n].build_cost + 40;
        vet = do_make_unit_veteran(acity, n);
      } else {
        d = 0;
        b = 40;
        vet = 0;
      }
      if (pdef) {
/*        n = pdef->type;    Now, really, I could not possibly have written this.
Yet, somehow, this line existed, and remained here for months, bugging the AI
tech progression beyond all description.  Only when adding the override code
did I realize the magnitude of my transgression.  How despicable. -- Syela */
        m = get_virtual_defense_power(v, pdef->type, x, y);
        if (pdef->veteran) m *= 1.5; /* with real defenders, this must be before * hp -- Syela */
        m *= (myunit->id ? pdef->hp : unit_types[pdef->type].hp) *
              unit_types[pdef->type].firepower;
/*        m /= (pdef->veteran ? 20 : 30);  -- led to rounding errors.  Duh! -- Syela */
        m /= 30;
        if (d < m * m) {
          d = m * m;
          b = unit_types[pdef->type].build_cost + 40; 
          vet = pdef->veteran;
          n = pdef->type; /* and not before, or heinous things occur!! */
        }
      }
      if (!is_ground_unit(myunit) && !is_heli_unit(myunit) &&
         (!(acity->ai.invasion&1))) b -= 40; /* boats can't conquer cities */
      if (!myunit->id && (!unit_really_ignores_citywalls(myunit)) &&
          c > (player_knows_improvement_tech(city_owner(acity),
					     B_CITY) ? 2 : 4) &&
          !city_got_citywalls(acity)) d *= 9;
    } /* end dealing with cities */

    else {
      pdef = aunit; /* we KNOW this is the get_defender -- Syela */

      m = unit_types[pdef->type].build_cost;
      b = m;
      sanity = 1;

      if (is_ground_unit(myunit)) dist = warmap.cost[x][y];
      else if (is_sailing_unit(myunit)) dist = warmap.seacost[x][y];
      else dist = real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE;
      if (dist > m) {
        dist *= unit_types[pdef->type].move_rate;
        if (unit_flag(pdef->type, F_IGTER)) dist *= 3;
      }
      if (!dist) dist = 1;

      m = unit_types[v].move_rate;
      if (unit_flag(v, F_IGTER)) m *= 3; /* not quite right */
      c = ((dist + m - 1) / m);

      n = pdef->type;
      m = get_virtual_defense_power(v, n, x, y);
      if (pdef->veteran) m += m / 2;
      if (pdef->activity == ACTIVITY_FORTIFIED) m += m / 2;
/* attempting to recreate the rounding errors in get_total_defense_power -- Syela */

      m *= (myunit->id ? pdef->hp : unit_types[n].hp) * unit_types[n].firepower;
/* let this be the LAST discrepancy!  How horribly many there have been! -- Syela */
/*     m /= (pdef->veteran ? 20 : 30);*/
      m /= 30;
      m *= m;
      d = m;
      vet = pdef->veteran;
    } /* end dealing with units */

    if (pcity->id == myunit->homecity || !myunit->id)
      unhap = ai_assess_military_unhappiness(pcity, get_gov_pplayer(pplayer));

    if (is_ground_unit(myunit) && !sanity && !boatid)
      needferry = 40; /* cost of ferry */
    f = unit_types[v].build_cost;
    fprime = f * 2 * unit_types[v].attack_strength /
           (unit_types[v].attack_strength +
            unit_types[v].defense_strength);

    if (acity) g = unit_list_size(&(map_get_tile(acity->x, acity->y)->units)) + 1;
    else g = 1;
    if (unit_types[v].move_type != LAND_MOVING &&
        unit_types[v].move_type != HELI_MOVING && !pdef) b0 = 0;
/* !d instead of !pdef was horrible bug that led to yoyo-ing AI warships -- Syela */
    else if (c > THRESHOLD) b0 = 0;
    else if ((unit_types[v].move_type == LAND_MOVING ||
              unit_types[v].move_type == HELI_MOVING) && acity &&
              acity->ai.invasion == 2) b0 = f * SHIELD_WEIGHTING;
    else {
      b0 = (b * a - (f + (acity ? acity->ai.f : 0)) * d) * g * SHIELD_WEIGHTING / (a + g * d);
      if (acity && b * acity->ai.a * acity->ai.a > acity->ai.f * d)
        b0 -= (b * acity->ai.a * acity->ai.a - acity->ai.f * d) *
               g * SHIELD_WEIGHTING / (acity->ai.a * acity->ai.a + g * d);
    }
    b0 -= c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
    if (b0 > 0) {
      a0 = amortize(b0, MAX(1, c));
      e = ((a0 * b0) / (MAX(1, b0 - a0))) * 100 / ((fprime + needferry) * MORT);
    } else e = 0;  

#ifdef DEBUG
    if (e != fstk && acity) {
	freelog(LOG_DEBUG, "%s (%d, %d), %s#%d -> %s[%d] (%d, %d)"
		" [A=%d, B=%d, C=%d, D=%d, E=%d/%d, F=%d]",
		pcity->name, pcity->x, pcity->y, unit_types[v].name,
		myunit->id, acity->name, acity->ai.invasion,
		acity->x, acity->y, a, b, c, d, e, fstk, f);
    } else if (e != fstk && aunit) {
	freelog(LOG_DEBUG, "%s (%d, %d), %s#%d -> %s (%d, %d)"
		" [A=%d, B=%d, C=%d, D=%d, E=%d/%d, F=%d]", 
		pcity->name, pcity->x, pcity->y, unit_types[v].name,
		myunit->id, unit_types[pdef->type].name,
		pdef->x, pdef->y, a, b, c, d, e, fstk, f);
    }
#endif

    if (!myunit->id) {  /* I don't want to know what happens otherwise -- Syela */
      if (sanity)
        process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, 0, 0, 0, needferry);
      else if (boatid) process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, bx, by, boatspeed, needferry);
      else process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, myunit->x, myunit->y, boatspeed, needferry);
    }

    if (e > choice->want && /* Without this &&, the AI will try to make attackers */
        choice->want <= 100) { /* instead of defenders when being attacked -- Syela */
      if (!city_got_barracks(pcity) && is_ground_unit(myunit)) {
        if (player_knows_improvement_tech(pplayer, B_BARRACKS3))
          choice->choice = B_BARRACKS3;
        else if (player_knows_improvement_tech(pplayer, B_BARRACKS2))
          choice->choice = B_BARRACKS2;
        else choice->choice = B_BARRACKS;
        choice->want = e;
        choice->type = CT_BUILDING;
      } else {
        if (!myunit->id) {
          choice->choice = v;
          choice->type = CT_ATTACKER;
          choice->want = e;
          if (needferry) ai_choose_ferryboat(pplayer, pcity, choice);
	  freelog(LOG_DEBUG, "%s has chosen attacker, %s",
			pcity->name, unit_types[choice->choice].name);
        } else {
          choice->choice = ai_choose_defender(pcity);
	  freelog(LOG_DEBUG, "%s has chosen defender, %s",
			pcity->name, unit_types[choice->choice].name);
          choice->type = CT_NONMIL;
          choice->want = e;
        }
        if (is_sailing_unit(myunit) && improvement_exists(B_PORT)
	    && !city_got_building(pcity, B_PORT)) {
	  Tech_Type_id tech = get_improvement_type(B_PORT)->tech_req;
          if (get_invention(pplayer, tech) == TECH_KNOWN) {
            choice->choice = B_PORT;
            choice->want = e;
            choice->type = CT_BUILDING;
          } else pplayer->ai.tech_want[tech] += e;
        }
      }
    }
  } 
}

/********************************************************************** 
Checks if there is a port or a ship being build within d distance.
***********************************************************************/
static int port_is_within(struct player *pplayer, int d)
{
  city_list_iterate(pplayer->cities, pcity)
    if (warmap.seacost[pcity->x][pcity->y] <= d) {
      if (city_got_building(pcity, B_PORT))
	return 1;

      if (!pcity->is_building_unit && pcity->currently_building == B_PORT
	  && pcity->shield_stock >= improvement_value(B_PORT))
	return 1;

      if (!player_knows_improvement_tech(pplayer, B_PORT)
	  && pcity->is_building_unit
	  && is_water_unit(pcity->currently_building)
	  && unit_types[pcity->currently_building].attack_strength >
	  unit_types[pcity->currently_building].transport_capacity)
	return 1;
    }
  city_list_iterate_end;

  return 0;
}

/********************************************************************** 
... this function should assign a value to choice and want and type, 
    where want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
***********************************************************************/
void military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				    struct ai_choice *choice)
{
  Unit_Type_id v;
  int def, danger, urgency, want;
  struct unit *myunit = 0;
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  struct unit virtualunit;
  struct city *acity = 0;
  struct unit *aunit = 0;
  choice->choice = 0;
  choice->want   = 0;
  choice->type   = CT_NONE;

/* TODO: recognize units that can DEFEND_HOME but are in the field. -- Syela */

  urgency = assess_danger(pcity); /* calling it now, rewriting old wall code */
  def = assess_defense_quadratic(pcity); /* has to be AFTER assess_danger thanks to wallvalue */
/* changing to quadratic to stop AI from building piles of small units -- Syela */
  danger = pcity->ai.danger; /* we now have our warmap and will use it! */
  freelog(LOG_DEBUG, "Assessed danger for %s = %d, Def = %d",
	  pcity->name, danger, def);

  if ((pcity->ai.diplomat_threat) && (def)){
  /* It's useless to build a diplomat as the last defender of a town. --nb */ 

    Unit_Type_id u = best_role_unit(pcity, F_DIPLOMAT);
    if (u<U_LAST) {
       freelog(LOG_DEBUG, "A diplomat will be built in city %s.", pcity->name);
       choice->want = 16000; /* diplomat more important than soldiers */ 
       pcity->ai.urgency = 1;
       choice->type = CT_DEFENDER;
       choice->choice = u;
       return;
    } else if (num_role_units(F_DIPLOMAT)>0) {
      u = get_role_unit(F_DIPLOMAT, 0);
      pplayer->ai.tech_want[get_unit_type(u)->tech_requirement] += 16000;
    }
  } 

  if (danger) { /* otherwise might be able to wait a little longer to defend */
/* old version had danger -= def in it, which was necessary before disband/upgrade
code was added and walls got built, but now danger -= def would be very bad -- Syela */
    if (danger >= def) {
      if (!urgency) danger = 100; /* don't waste money otherwise */
      else if (danger >= def * 2) danger = 200 + urgency;
      else { danger *= 100; danger /= def; danger += urgency; }
/* without the += urgency, wasn't buying with danger == def.  Duh. -- Syela */
    } else { danger *= 100; danger /= def; }
    if (pcity->shield_surplus <= 0 && def) danger = 0;
/* this is somewhat of an ugly kluge, but polar cities with no ability to
increase prod were buying alpines, panicking, disbanding them, buying alpines
and so on every other turn.  This will fix that problem, hopefully without
creating any other problems that are worse. -- Syela */
    if (pcity->ai.building_want[B_CITY] && def && can_build_improvement(pcity, B_CITY)
        && (danger < 101 || unit_list_size(&ptile->units) > 1 ||
/* walls before a second defender, unless we need it RIGHT NOW */
         (!pcity->ai.grave_danger && /* I'm not sure this is optimal */
         pplayer->economic.gold > (80 - pcity->shield_stock) * 2)) &&
	ai_fuzzy(pplayer,1)) {
/* or we can afford just to buy walls.  Added 980805 -- Syela */
      choice->choice = B_CITY; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_CITY]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (pcity->ai.building_want[B_COASTAL] && def &&
        can_build_improvement(pcity, B_COASTAL) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1) &&
	ai_fuzzy(pplayer,1)) {
      choice->choice = B_COASTAL; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_COASTAL]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (pcity->ai.building_want[B_SAM] && def &&
        can_build_improvement(pcity, B_SAM) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1) &&
	ai_fuzzy(pplayer,1)) {
      choice->choice = B_SAM; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_SAM]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (danger > 0 && unit_list_size(&ptile->units) <= urgency) {
      process_defender_want(pplayer, pcity, danger, choice);
      if (!urgency && unit_types[choice->choice].defense_strength == 1) {
        if (city_got_barracks(pcity)) choice->want = MIN(49, danger); /* unlikely */
        else choice->want = MIN(25, danger);
      } else choice->want = danger;
      freelog(LOG_DEBUG, "%s wants %s to defend with desire %d.",
		    pcity->name, get_unit_type(choice->choice)->name,
		    choice->want);
      /* return; - this is just stupid */
    }
  } /* ok, don't need to defend */

  if (pcity->shield_surplus <= 0 || /* must be able to upkeep units */
      pcity->ppl_unhappy[4] > pcity->ppl_unhappy[2]) return; /* and no disorder! */

  memset(&virtualunit, 0, sizeof(struct unit));
/* this memset didn't work because of syntax that
the intrepid David Pfitzner discovered was in error. -- Syela */
  virtualunit.owner = pplayer->player_no;
  virtualunit.x = pcity->x;
  virtualunit.y = pcity->y;
  virtualunit.id = 0;
  v = ai_choose_defender_by_type(pcity, LAND_MOVING);  /* Temporary -- Syela */
  virtualunit.type = v;
  virtualunit.veteran = do_make_unit_veteran(pcity, v);
  virtualunit.hp = unit_types[v].hp;

  if (choice->want < 100) {
    want = look_for_charge(pplayer, &virtualunit, &aunit, &acity);
    if (want > choice->want) {
      choice->want = want;
      choice->choice = v;
      choice->type = CT_NONMIL; /* Why not CT_DEFENDER? -- Caz */
    }
  }

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (((get_unit_type(punit->type)->attack_strength * 4 >
        get_unit_type(punit->type)->defense_strength * 5) ||
        unit_flag(punit->type, F_FIELDUNIT)) &&
        punit->activity != ACTIVITY_GOTO) /* very important clause, this -- Syela */
     myunit = punit;
  unit_list_iterate_end;
/* if myunit is non-null, it is an attacker forced to defend */
/* and we will use its attack values, otherwise we will use virtualunit */
  if (myunit) kill_something_with(pplayer, pcity, myunit, choice);
  else {
    freelog(LOG_DEBUG, "Killing with virtual unit in %s", pcity->name);
    v = ai_choose_attacker_sailing(pcity);
    if (v > 0 && /* have to put sailing first before we mung the seamap */
      (city_got_building(pcity, B_PORT) || /* only need a few ports */
      !port_is_within(pplayer, 18))) { /* using move_rate is quirky -- Syela */
      virtualunit.type = v;
/*     virtualunit.veteran = do_make_unit_veteran(pcity, v); */
      virtualunit.veteran = (player_knows_improvement_tech(pplayer, B_PORT));
      virtualunit.hp = unit_types[v].hp;
      kill_something_with(pplayer, pcity, &virtualunit, choice);
    } /* ok.  can now mung seamap for ferryboat code.  Proceed! */
    v = ai_choose_attacker_ground(pcity);
    virtualunit.type = v;
/*    virtualunit.veteran = do_make_unit_veteran(pcity, v);*/
    virtualunit.veteran = 1;
    virtualunit.hp = unit_types[v].hp;
    kill_something_with(pplayer, pcity, &virtualunit, choice);
  }
  return;
}

void establish_city_distances(struct player *pplayer, struct city *pcity)
{
  int dist;
  int wondercity;
  Unit_Type_id freight;
  int moverate;
/* at this moment, our warmap is intact.  we need to do two things: */
/* establish faraway for THIS city, and establish d_t_w_c for ALL cities */

  if (!pcity->is_building_unit && is_wonder(pcity->currently_building))
    wondercity = map_get_continent(pcity->x, pcity->y);
  else wondercity = 0;
  freight = best_role_unit(pcity, F_CARAVAN);
  moverate = (freight==U_LAST) ? SINGLE_MOVE : get_unit_type(freight)->move_rate;

  pcity->ai.downtown = 0;
  city_list_iterate(pplayer->cities, othercity)
    dist = warmap.cost[othercity->x][othercity->y];
    if (wondercity && map_get_continent(othercity->x, othercity->y) == wondercity)
      othercity->ai.distance_to_wonder_city = dist;
    dist += moverate - 1; dist /= moverate;
    pcity->ai.downtown += MAX(0, 5 - dist); /* four three two one fire */
  city_list_iterate_end;
  return;
}
