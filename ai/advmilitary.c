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
#include <aitools.h>
#include <game.h>
#include <map.h>
#include <unitfunc.h>
#include <citytools.h>
#include <aicity.h>
#include <aiunit.h>
#include <unittools.h> /* for get_defender, amazingly */
#include <gotohand.h> /* warmap has been redeployed */

extern struct move_cost_map warmap;

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

int assess_defense_backend(struct city *pcity, int igwall)
{
  int v, def;
  def = 0;
  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    v = get_defense_power(punit) * punit->hp * get_unit_type(punit->type)->firepower;
    v /= 20; /* divides reasonably well, should stop overflows */
/* actually is *= 1.5 for city, then /= 30 for scale - don't be confused */
/* just trying to minimize calculations and thereby rounding errors -- Syela */
    v *= v;
    if (!igwall && city_got_citywalls(pcity) && is_ground_unit(punit)) {
      v *= pcity->ai.wallvalue; v /= 10;
    }
    if (is_military_unit(punit)) def += v;
  unit_list_iterate_end;
  /* def is an estimate of our total defensive might */
  /* now with regard to the IGWALL nature of the units threatening us -- Syela */
  return(def);
/* unclear whether this should treat settlers/caravans as defense -- Syela */
}

int assess_defense(struct city *pcity)
{
  return(assess_defense_backend(pcity, 0));
}

int assess_defense_igwall(struct city *pcity)
{
  return(assess_defense_backend(pcity, 1));
}

int assess_danger(struct city *pcity)
{
  struct unit *punit;
  int i, danger = 0, v, dist, con, m;
  int danger2 = 0; /* linear for walls */
  int danger3 = 0; /* linear for coastal */
  int danger4 = 0; /* linear for SAM */
  int danger5 = 0; /* linear for SDI */
  struct player *aplayer, *pplayer;
  int pikemen = 0;
  int urgency = 0;
  int igwall;
  int badmojo = 0;

  pplayer = &game.players[pcity->owner];
  con = map_get_continent(pcity->x, pcity->y); /* Not using because of boats */

  generate_warmap(pcity, 0); /* generates both land and sea maps */

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
        if (unit_flag(punit->type, F_SUBMARINE)) v = 0;
        if (is_sailing_unit(punit) && !is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN)) v = 0;
/* get_attack_power will be wrong if moves_left == 1 || == 2 */
        v *= punit->hp * get_unit_type(punit->type)->firepower;
        if (unit_ignores_citywalls(punit) || 
            (!is_heli_unit(punit) && !is_ground_unit(punit))) igwall++;
        if (city_got_building(pcity, B_COASTAL) && is_sailing_unit(punit)) v /= 2;
        if (city_got_building(pcity, B_SAM) && is_air_unit(punit)) v /= 2;

        dist = real_map_distance(punit->x, punit->y, pcity->x, pcity->y) * 3;
        if (unit_flag(punit->type, F_IGTER)) dist /= 3;
        else if (is_ground_unit(punit)) dist = warmap.cost[punit->x][punit->y];
        else if (is_sailing_unit(punit)) dist = warmap.seacost[punit->x][punit->y];

        m = get_unit_type(punit->type)->move_rate;
/* if dist = 9, a chariot is 1.5 turns away.  NOT 2 turns away. */
/* Samarkand bug should be obsoleted by re-ordering of events */
        if (dist < 3) dist = 3;
        if (dist <= m * 3 && v) urgency++;
        if (dist <= m && v) pcity->ai.grave_danger = punit;

        if (unit_flag(punit->type, F_HORSE)) {
          if (pikemen) v /= 2;
          else if (get_invention(pplayer, A_FEUDALISM) != TECH_KNOWN)
            pplayer->ai.tech_want[A_FEUDALISM] += v * m / 2 / dist;
        }

        v /= 30; /* rescaling factor to stop the overflow nonsense */
        v *= v;

        if (!igwall) danger2 += v * m / dist;
        else if (is_sailing_unit(punit)) danger3 += v * m / dist;
        else if (is_air_unit(punit)) danger4 += v * m / dist;
        if (unit_flag(punit->type, F_MISSILE)) danger5 += v * m / MAX(m, dist);
        if (punit->type != U_NUCLEAR) { /* only SDI helps against NUCLEAR */
          if (dist * dist < m * 3) { v *= m; v /= 3; } /* knights can't attack more than twice */
          else { v *= m * m; v /= dist * dist; }
          danger += v;
          if (igwall) badmojo += v;
        }

/* if (v > 1000 || (v > 100 && !city_got_citywalls(pcity)))
printf("%s sees %d danger from %s at (%d, %d) [%d]\n", pcity->name, v,
unit_types[punit->type].name, punit->x, punit->y, dist);  */
      unit_list_iterate_end;
    }
  } /* end for */

  if (!badmojo) pcity->ai.wallvalue = 90;
  else pcity->ai.wallvalue = (danger * 9 - badmojo * 8) * 10 / (danger);
/* afraid *100 would create integer overflow, but *10 surely won't */

  if (danger < 0 || danger > 1<<24) /* I hope never to see this! */
    printf("Dangerous danger (%d) in %s.  Beware of overflow.\n", danger, pcity->name);
  if (danger2 < 0 || danger2 > 1<<24) /* I hope never to see this! */
    printf("Dangerous danger2 (%d) in %s.  Beware of overflow.\n", danger2, pcity->name);
  if (danger3 < 0 || danger3 > 1<<24) /* I hope never to see this! */
    printf("Dangerous danger3 (%d) in %s.  Beware of overflow.\n", danger3, pcity->name);
  if (danger4 < 0 || danger4 > 1<<24) /* I hope never to see this! */
    printf("Dangerous danger4 (%d) in %s.  Beware of overflow.\n", danger4, pcity->name);
  if (danger5 < 0 || danger5 > 1<<24) /* I hope never to see this! */
    printf("Dangerous danger5 (%d) in %s.  Beware of overflow.\n", danger5, pcity->name);
  if (pcity->ai.grave_danger) urgency += 10; /* really, REALLY urgent to defend */
  pcity->ai.danger = danger;
  if (pcity->ai.building_want[B_CITY] > 0 && danger2) {
    i = assess_defense(pcity);
    if (danger2 > i * 1 / 2) pcity->ai.building_want[B_CITY] = 100 + urgency;
    else pcity->ai.building_want[B_CITY] += danger2 * 100 / i;
  }
/* COASTAL and SAM are TOTALLY UNTESTED and could be VERY WRONG -- Syela */
/* was * 3 / 2 and 200 + but that leads to stupidity in cities with
no defenders and danger = 0 but danger2 > 0 -- Syela */
  if (pcity->ai.building_want[B_COASTAL] > 0 && danger3) {
    i = assess_defense_igwall(pcity);
    if (danger3 > i * 1 / 2) pcity->ai.building_want[B_COASTAL] = 100 + urgency;
    else pcity->ai.building_want[B_COASTAL] += danger3 * 100 / i;
  }
  if (pcity->ai.building_want[B_SAM] > 0 && danger4) {
    i = assess_defense_igwall(pcity);
    if (danger4 > i * 1 / 2) pcity->ai.building_want[B_SAM] = 100 + urgency;
    else pcity->ai.building_want[B_SAM] += danger4 * 100 / i;
  }
  if (pcity->ai.building_want[B_SDI] > 0 && danger5) {
    i = assess_defense_igwall(pcity);
    if (danger5 > i * 1 / 2) pcity->ai.building_want[B_SDI] = 100 + urgency;
    else pcity->ai.building_want[B_SDI] += danger5 * 100 / i;
  }
  return(urgency);
}

int unit_desirability(int i, int def)
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
  else if (d == 1) { cur *= (a + d); cur /= 2; }
  else cur *= a; /* wanted to rank Legion > Catapult > Archer */
  if (unit_flag(i, F_IGTER) && !def) cur *= 3;
  if (unit_flag(i, F_PIKEMEN) && def) cur *= 1.5;
  return(cur);  
}

void process_defender_want(struct player *pplayer, struct city *pcity, int danger,
                           struct ai_choice *choice)
{
  int i, j, k, l, m, n;
  int best= 0;
  int bestid = 0;
  int walls = city_got_citywalls(pcity);
  int desire[U_LAST]; /* what you get is what you see */
  int shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);

  memset(desire, 0, sizeof(desire));
  for (i = U_WARRIORS; i <= U_BATTLESHIP; i++) {
    m = unit_types[i].move_type;
    if ((m == LAND_MOVING || m == SEA_MOVING)) {
      k = pplayer->ai.tech_turns[unit_types[i].tech_requirement];
      j = unit_desirability(i, 1);
      j /= 15; /* good enough, no rounding errors */
      j *= j;
      if (can_build_unit(pcity, i)) {
        if (walls && m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
        if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
          best = j;
          bestid = i;
        }
      } else if (k > 0 && (shore || m == LAND_MOVING) &&
                unit_types[i].tech_requirement != A_LAST) {
        if (m == LAND_MOVING) { j *= pcity->ai.wallvalue; j /= 10; }
        l = k * (k + pplayer->research.researchpoints) * game.techlevel /
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
/* multiply by unit_types[bestid].build_cost / best */
  for (i = U_WARRIORS; i <= U_BATTLESHIP; i++) {
    if (desire[i]) {
      j = unit_types[i].tech_requirement;
      n = desire[i] * unit_types[bestid].build_cost / best;
      pplayer->ai.tech_want[j] += n; /* not the totally idiotic
      pplayer->ai.tech_want[j] += n * pplayer->ai.tech_turns[j];  I amaze myself. -- Syela */
/*      printf("%s wants %s for defense with desire %d <%d>\n",
pcity->name, advances[j].name, n, desire[i]); */
    }
  }
  choice->choice = bestid;
  choice->want = danger;
  choice->type = 3;
  return;
}

void process_attacker_want(struct player *pplayer, struct city *pcity, int b, int n,
                            int vet, int x, int y, int unhap, int movetype)
{
/* n is type of defender, vet is vet status, x,y is location of target */
  int a, c, d, e, i;
  int j, k, l, m, dist;
  int shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);
  for (i = U_WARRIORS; i <= U_BATTLESHIP; i++) {
    m = unit_types[i].move_type;
    j = unit_types[i].tech_requirement;
    if (j != A_LAST) k = pplayer->ai.tech_turns[j];
    if ((m == LAND_MOVING || (m == SEA_MOVING && shore)) && j != A_LAST && k &&
         unit_types[i].attack_strength && /* otherwise we get SIGFPE's */
         m == movetype) { /* I don't think I want the duplication otherwise -- Syela */
      l = k * (k + pplayer->research.researchpoints) * game.techlevel /
         (game.year > 0 ? 2 : 4); /* cost (shield equiv) of gaining these techs */
      l /= city_list_size(&pplayer->cities);
/* Katvrr advises that with danger high, l should be weighted more heavily */

      a = unit_types[i].attack_strength * (do_make_unit_veteran(pcity, i) ? 15 : 10) *
             unit_types[i].firepower * unit_types[i].hp;
      a /= 30; /* scaling factor to prevent integer overflows */
      a *= a;
      /* b is unchanged */
      if (unit_types[i].move_type == LAND_MOVING) dist = warmap.cost[x][y];
      else if (unit_types[i].move_type == SEA_MOVING) dist = warmap.seacost[x][y];
      else dist = real_map_distance(pcity->x, pcity->y, x, y); /* for future use */
      if (get_unit_type(i)->flags & F_IGTER) dist /= 3; /* not quite right */
      if (!dist) dist = 1;
      m = unit_types[i].move_rate;
      c = ((dist + m - 1) / m);
      m = get_virtual_defense_power(i, n, x, y);
      m *= unit_types[n].hp * unit_types[n].firepower;
      m /= (vet ? 20 : 30);
      m *= m;
      d = m;
      if (unhap > 0) c *= 2;
      c += l;
      e = (b * a - unit_types[i].build_cost * d) * 100 / (a + d) - c * 100;
      e /= unit_types[i].build_cost;
      if (e > 0) {
        pplayer->ai.tech_want[j] += e;
/*      printf("%s wants %s for attack with desire %d\n",
pcity->name, advances[j].name, e); */
      }
    }
  }
}

void kill_something_with(struct player *pplayer, struct city *pcity, 
                         struct unit *myunit, struct ai_choice *choice)
{
  int a, b, c, d, e; /* variables in the attacker-want equation */
  int m, n, vet, dist, v;
  int x, y, unhap = 0;
  struct unit *pdef, *aunit;
  struct city *acity;

  find_something_to_kill(pplayer, myunit, &x, &y);
  acity = map_get_city(x, y);
  if (!acity) aunit = get_defender(pplayer, myunit, x, y);
  else aunit = 0;
  if (acity && acity->owner == pplayer->player_no) acity = 0;

/* logically we should adjust this for race attack tendencies */
  if ((acity || aunit) && (assess_defense(pcity) || !pcity->ai.danger)) {
    v = myunit->type;
    vet = myunit->veteran;

    a = unit_types[v].attack_strength * (vet ? 15 : 10) *
             unit_types[v].firepower * unit_types[v].hp;
    a /= 30; /* scaling factor to prevent integer overflows */
    a *= a;

    if (acity) {
      m = 0;
      pdef = get_defender(pplayer, myunit, x, y);

      if (is_ground_unit(myunit)) dist = warmap.cost[x][y];
      else if (is_sailing_unit(myunit)) dist = warmap.seacost[x][y];
      else dist = real_map_distance(pcity->x, pcity->y, x, y);
      if (get_unit_type(v)->flags & F_IGTER) dist /= 3; /* not quite right */
      if (!dist) dist = 1;

      m = unit_types[v].move_rate;
      c = ((dist + m - 1) / m);

      n = ai_choose_defender_versus(acity, v);
      m = get_virtual_defense_power(v, n, x, y);
      m *= unit_types[n].hp * unit_types[n].firepower;
      m /= (do_make_unit_veteran(acity, n) ? 20 : 30);
      d = m * m;
      b = unit_types[n].build_cost + 40;
      vet = do_make_unit_veteran(acity, n);
      if (pdef) {
        n = pdef->type;
        m = get_virtual_defense_power(v, n, x, y);
        m *= unit_types[n].hp * unit_types[n].firepower;
        m /= (pdef->veteran ? 20 : 30);
        if (d < m * m) {
          d = m * m;
          b = unit_types[pdef->type].build_cost + 40; 
          vet = pdef->veteran;
        }
      }
    } /* end dealing with cities */

    if (aunit) {
      m = 0;
      pdef = aunit; /* we KNOW this is the get_defender -- Syela */

      m = unit_types[pdef->type].build_cost;
      b = m;

      if (is_ground_unit(myunit)) dist = warmap.cost[x][y];
      else if (is_sailing_unit(myunit)) dist = warmap.seacost[x][y];
      else dist = real_map_distance(pcity->x, pcity->y, x, y);
      dist *= unit_types[pdef->type].move_rate;
      if (unit_flag(pdef->type, F_IGTER)) dist *= 3;
      if (get_unit_type(v)->flags & F_IGTER) dist /= 3; /* not quite right */
      if (!dist) dist = 1;

      m = unit_types[v].move_rate;
      c = ((dist + m - 1) / m);

      n = pdef->type;
      m = get_virtual_defense_power(v, n, x, y);
      m *= unit_types[n].hp * unit_types[n].firepower;
      m /= (pdef->veteran ? 20 : 30);
      m *= m;
      d = m;
      vet = pdef->veteran;
    } /* end dealing with units */

    if (get_government(pplayer->player_no) == G_REPUBLIC) {
      unit_list_iterate(pcity->units_supported, punit)
        if(get_unit_type(punit->type)->attack_strength &&
            !map_get_city(punit->x, punit->y )) {
          unhap++;
        } else if (is_field_unit(punit)) {
          unhap++;
        }
      unit_list_iterate_end;
      if (city_affected_by_wonder(pcity, B_WOMENS) ||
          city_got_building(pcity, B_POLICE)) unhap--;
    } /* handle other governments later */
    if (unhap > 0) c *= 2;

    e = (b * a - unit_types[v].build_cost * d) * 100 / (a + d) - c * 100;
    e /= unit_types[v].build_cost;

    process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap, unit_types[v].move_type);

/* if (acity && e > 0) {
printf("%s (%d, %d) wants to attack %s (%d, %d) with desire %d [dist = %d, c = %d]\n",
pcity->name, pcity->x, pcity->y, acity->name, acity->x, acity->y,
e, dist, c);
} else if (aunit && e > 0) {
printf("%s (%d, %d) wants to attack %s (%d, %d) with desire %d [dist = %d, c = %d]\n",
pcity->name, pcity->x, pcity->y, unit_types[pdef->type].name, pdef->x, pdef->y,
e, dist, c);
} */
    if (e > choice->want) {
      if (get_invention(pplayer, A_GUNPOWDER) == TECH_KNOWN &&
          !city_got_barracks(pcity) && is_ground_unit(myunit)) {
        if (get_invention(pplayer, A_COMBUSTION) == TECH_KNOWN)
          choice->choice = B_BARRACKS3;
        else choice->choice = B_BARRACKS2;
        choice->want = e;
        choice->type = 0;
      } else {
        if (myunit->id) {
          choice->choice = v;
          choice->type = 2; /* type == 2 identifies attackers */
        } else {
          choice->choice = ai_choose_defender(pcity);
          choice->type = 1;
        }
        choice->want = e;
        if (is_sailing_unit(myunit) && !city_got_building(pcity, B_PORT)) {
          if (get_invention(pplayer, A_AMPHIBIOUS) == TECH_KNOWN) {
            choice->choice = B_PORT;
            choice->want = e;
            choice->type = 0;
          } else pplayer->ai.tech_want[A_AMPHIBIOUS] += e;
        }
      }
    }
  } 
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
  int def, danger, dist, ag, v, urgency, vet, m, n;
  struct unit *aunit, *myunit = 0, *pdef = 0;
  struct city *acity;
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  struct unit virtualunit;
  choice->choice = 0;
  choice->want   = 0;
  choice->type   = 0;

/* TODO: recognize units that can DEFEND_HOME but are in the field. -- Syela */

  urgency = assess_danger(pcity); /* calling it now, rewriting old wall code */
  def = assess_defense(pcity); /* has to be AFTER assess_danger thanks to wallvalue */
  danger = pcity->ai.danger; /* we now have our warmap and will use it! */
/* printf("Assessed danger for %s = %d, Def = %d\n", pcity->name, danger, def); */

  if (danger) { /* otherwise might be able to wait a little longer to defend */
/* old version had danger -= def in it, which was necessary before disband/upgrade
code was added and walls got built, but now danger -= def would be very bad -- Syela */
    if (danger >= def) {
      if (!urgency) danger = 100; /* don't waste money otherwise */
      else if (danger >= def * 2) danger = 200 + urgency;
      else { danger *= 100; danger /= def; }
    } else { danger *= 100; danger /= def; }
    if (pcity->ai.building_want[B_CITY] && def && can_build_improvement(pcity, B_CITY)
        && (danger < 101 || unit_list_size(&ptile->units) > 1)) {
/* walls before a second defender, unless we need it RIGHT NOW */
      choice->choice = B_CITY; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_CITY]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = 0;
    } else if (pcity->ai.building_want[B_COASTAL] && def &&
        can_build_improvement(pcity, B_COASTAL) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1)) {
      choice->choice = B_COASTAL; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_COASTAL]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = 0;
    } else if (pcity->ai.building_want[B_SAM] && def &&
        can_build_improvement(pcity, B_SAM) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1)) {
/* walls before a second defender, unless we need it RIGHT NOW */
      choice->choice = B_SAM; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_SAM]; /* hacked by assess_danger */
      if (!urgency && choice->want > 100) choice->want = 100;
      choice->type = 0;
    } else if (danger > 0 && unit_list_size(&ptile->units) <= urgency) {
      process_defender_want(pplayer, pcity, danger, choice);
      if (!urgency && unit_types[choice->choice].defense_strength == 1) {
        if (city_got_barracks(pcity)) choice->want = MIN(49, danger); /* unlikely */
        else choice->want = MIN(25, danger);
      } else choice->want = danger;
/*    printf("%s wants %s to defend with desire %d.\n", pcity->name,
       get_unit_type(choice->choice)->name, choice->want); */
/*    return; - this is just stupid */
    }
  } /* ok, don't need to defend */

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (get_unit_type(punit->type)->attack_strength * 4 >
        get_unit_type(punit->type)->defense_strength * 5)
     myunit = punit;
  unit_list_iterate_end;
/* if myunit is non-null, it is an attacker forced to defend */
/* and we will use its attack values, otherwise we will use virtualunit */
  if (myunit) kill_something_with(pplayer, pcity, myunit, choice);
  else {
    v = ai_choose_attacker_ground(pcity);
    memset(&virtualunit, 0, sizeof(&virtualunit));
    virtualunit.type = v;
    virtualunit.owner = pplayer->player_no;
    virtualunit.x = pcity->x;
    virtualunit.y = pcity->y;
    virtualunit.veteran = do_make_unit_veteran(pcity, v);
    virtualunit.hp = unit_types[v].hp;
    kill_something_with(pplayer, pcity, &virtualunit, choice);
    v = ai_choose_attacker_sailing(pcity);
    if (v > 0) {
      virtualunit.type = v;
      virtualunit.veteran = do_make_unit_veteran(pcity, v);
      virtualunit.hp = unit_types[v].hp;
      kill_something_with(pplayer, pcity, &virtualunit, choice);
    }
  }
  return;
}

void establish_city_distances(struct player *pplayer, struct city *pcity)
{
  int dist = 0;
  int wondercity;
  int freight;
/* at this moment, our warmap is intact.  we need to do two things: */
/* establish faraway for THIS city, and establish d_t_w_c for ALL cities */

  if (!pcity->is_building_unit && is_wonder(pcity->currently_building))
    wondercity = 1;
  else wondercity = 0;
  if (get_invention(pplayer, A_CORPORATION) == TECH_KNOWN)
    freight = 6;
  else freight = 3;

  pcity->ai.downtown = 0;
  city_list_iterate(pplayer->cities, othercity)
    dist = warmap.cost[othercity->x][othercity->y];
    if (wondercity) othercity->ai.distance_to_wonder_city = dist;
    dist += freight - 1; dist /= freight;
    pcity->ai.downtown += MAX(0, 5 - dist); /* four three two one fire */
  city_list_iterate_end;
  return;
}
