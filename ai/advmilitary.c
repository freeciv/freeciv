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

#include "combat.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"

#include "citytools.h"
#include "cityturn.h"
#include "gotohand.h"		/* warmap has been redeployed */
#include "settlers.h"

#include "aicity.h"
#include "aitools.h"
#include "aiunit.h"

#include "advmilitary.h"


/********************************************************************** 
This function should assign a value to choice and want, where want is a value
between 1 and 100.

If choice is A_NONE, this advisor doesn't want any particular tech researched
at the moment.
***********************************************************************/
void military_advisor_choose_tech(struct player *pplayer,
				  struct ai_choice *choice)
{
  choice->choice = A_NONE;
  choice->want   = 0;
  /* This function hasn't been implemented yet. */
}

/********************************************************************** 
Helper for assess_defense_quadratic and assess_defense_unit.
***********************************************************************/
static int base_assess_defense_unit(struct city *pcity, struct unit *punit,
                                    bool igwall, bool quadratic,
                                    int wall_value)
{
  int defense;

  if (!is_military_unit(punit)) {
    return 0;
  }

  defense = get_defense_power(punit) * punit->hp *
            (is_sailing_unit(punit) ? 1 : unit_type(punit)->firepower);
  if (is_ground_unit(punit)) {
    defense = (3 * defense) / 2;
  }
  defense /= POWER_DIVIDER;

  if (quadratic) {
    defense *= defense;
  }

  if (!igwall && city_got_citywalls(pcity) && is_ground_unit(punit)) {
    defense *= wall_value;
    defense /= 10;
  }

  return defense;
}

/********************************************************************** 
Need positive feedback in m_a_c_b and bodyguard routines. -- Syela
***********************************************************************/
int assess_defense_quadratic(struct city *pcity)
{
  int defense = 0, walls = 0;
  /* This can be an arg if needed, but we don't need to change it now. */
  const bool igwall = FALSE;

  /* wallvalue = 10, walls = 10,
   * wallvalue = 40, walls = 20,
   * wallvalue = 90, walls = 30 */

  while (walls * walls < pcity->ai.wallvalue * 10)
    walls++;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    defense += base_assess_defense_unit(pcity, punit, igwall, FALSE,
                                        walls);
  } unit_list_iterate_end;

  if (defense > 1<<12) {
    freelog(LOG_VERBOSE, "Very large defense in assess_defense_quadratic: %d in %s",
            defense, pcity->name);
  }

  if (defense > 1<<15) {
    defense = 1<<15; /* more defense than we know what to do with! */
  }

  return defense * defense;
}

/**************************************************************************
One unit only, mostly for findjob; handling boats correctly. 980803 -- Syela
**************************************************************************/
int assess_defense_unit(struct city *pcity, struct unit *punit, bool igwall)
{
  return base_assess_defense_unit(pcity, punit, igwall, TRUE,
				  pcity->ai.wallvalue);
}

/********************************************************************** 
Most of the time we don't need/want positive feedback. -- Syela

It's unclear whether this should treat settlers/caravans as defense. -- Syela
TODO: It looks like this is never used while deciding if we should attack
pcity, if we have pcity defended properly, so I think it should. --pasky
***********************************************************************/
static int assess_defense_backend(struct city *pcity, bool igwall)
{
  /* Estimate of our total city defensive might */
  int defense = 0;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit) {
    defense += assess_defense_unit(pcity, punit, igwall);
  } unit_list_iterate_end;

  return defense;
}

/************************************************************************** 
...
**************************************************************************/
int assess_defense(struct city *pcity)
{
  return assess_defense_backend(pcity, FALSE);
}

/************************************************************************** 
...
**************************************************************************/
static int assess_defense_igwall(struct city *pcity)
{
  return assess_defense_backend(pcity, TRUE);
}

/************************************************************************** 
Compute actual danger depending on move rate of enemy and its distance.
**************************************************************************/
static int dangerfunct(int danger, int move_rate, int distance)
{
  /* XXX: I don't have a clue about these, it probably has something in common
   * with the way how attack force is computed when attacker already spent some
   * move points..? --pasky */
  int num = move_rate * 4;
  int denom = move_rate * 4;

  danger *= 2;

  /* Turns to reach us.. */
  while (distance >= move_rate) {
    danger /= 2;
    distance -= move_rate;
  }

  /* Moves in the last turn to reach us.. */
  move_rate /= SINGLE_MOVE;
  while (distance > 0 && distance >= move_rate) {
    num *= 4;
    denom *= 5;
    distance -= move_rate;
  }

  /* Partial moves in the last turn.. */
  while (distance > 0) {
    denom += (denom + move_rate * 2) / (move_rate * 4);
    distance--;
  }

  danger = (danger * num + (denom/2)) / denom;

  return danger;
}

/************************************************************************** 
How dangerous a unit is for a city?
**************************************************************************/
static int assess_danger_unit(struct city *pcity, struct unit *punit)
{
  int danger;
  bool sailing;

  if (unit_flag(punit, F_NO_LAND_ATTACK)) return 0;

  sailing = is_sailing_unit(punit);
  if (sailing && !is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN)) return 0;

  danger = unit_belligerence_basic(punit);
  if (sailing && city_got_building(pcity, B_COASTAL)) danger /= 2;
  if (is_air_unit(punit) && city_got_building(pcity, B_SAM)) danger /= 2;

  return danger;
}

/************************************************************************** 
...
**************************************************************************/
static int assess_distance(struct city *pcity, struct unit *punit,
                           int move_rate, int boatid, int boatdist,
                           int boatspeed)
{
  int x, y, distance;

  if (same_pos(punit->x, punit->y, pcity->x, pcity->y))
    return 0;

  if (is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y))
    distance = SINGLE_MOVE;
  else if (is_sailing_unit(punit))
    distance = warmap.seacost[punit->x][punit->y];
  else if (!is_ground_unit(punit))
    distance = real_map_distance(punit->x, punit->y, pcity->x, pcity->y)
               * SINGLE_MOVE;
  else if (unit_flag(punit, F_IGTER))
    distance = real_map_distance(punit->x, punit->y, pcity->x, pcity->y);
  else
    distance = warmap.cost[punit->x][punit->y];

  /* If distance = 9, a chariot is 1.5 turns away.  NOT 2 turns away. */
  if (distance < SINGLE_MOVE) distance = SINGLE_MOVE;

  if (is_ground_unit(punit) && boatid != 0
      && find_beachhead(punit, pcity->x, pcity->y, &x, &y) != 0) {
    /* Sea travellers. */

    y = warmap.seacost[punit->x][punit->y];
    if (y >= 6 * THRESHOLD)
      y = real_map_distance(pcity->x, pcity->y, punit->x, punit->y) * SINGLE_MOVE;

    x = MAX(y, boatdist) * move_rate / boatspeed;

    if (distance > x) distance = x;
    if (distance < SINGLE_MOVE) distance = SINGLE_MOVE;
  }

  return distance;
}

/********************************************************************** 
Call assess_danger() for all cities owned by pplayer.

This is necessary to initialize some ai data before some ai calculations.
***********************************************************************/
void assess_danger_player(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    assess_danger(pcity);
  } city_list_iterate_end;
}

/********************************************************************** 
In how big danger given city is?
***********************************************************************/
int assess_danger(struct city *pcity)
{
  int i, danger = 0, v, dist, m;
  Unit_Type_id utype;
  int danger2 = 0; /* linear for walls */
  int danger3 = 0; /* linear for coastal */
  int danger4 = 0; /* linear for SAM */
  int danger5 = 0; /* linear for SDI */
  struct player *pplayer;
  bool pikemen = FALSE;
  bool diplomat = FALSE; /* TRUE mean that this town can defend
		     * against diplomats or spies */
  int urgency = 0;
  bool igwall;
  int badmojo = 0;
  int boatspeed;
  int boatid, boatdist;
  int x = pcity->x, y = pcity->y; /* dummy variables */
  struct unit virtualunit;
  struct unit *funit = &virtualunit; /* saves me a lot of typing. -- Syela */

  memset(&virtualunit, 0, sizeof(struct unit));
  pplayer = city_owner(pcity);

  generate_warmap(pcity, NULL);	/* generates both land and sea maps */

  pcity->ai.grave_danger = 0;
  pcity->ai.diplomat_threat = FALSE;

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (unit_flag(punit, F_PIKEMEN)) pikemen = TRUE;
    if (unit_flag(punit, F_DIPLOMAT)) diplomat = TRUE;
  unit_list_iterate_end;

  players_iterate(aplayer) {
    if (pplayers_at_war(city_owner(pcity), aplayer)) {
      boatspeed = ((get_invention(aplayer, game.rtech.nav) == TECH_KNOWN) 
                      ? 4 * SINGLE_MOVE : 2 * SINGLE_MOVE);
      boatid = find_boat(aplayer, &x, &y, 0);
      if (boatid != 0) boatdist = warmap.seacost[x][y];
      else boatdist = -1;
/* should I treat empty enemy cities as danger? */
/* After much contemplation, I've decided the answer is sometimes -- Syela */
      city_list_iterate(aplayer->cities, acity)
        if (acity->is_building_unit &&
            build_points_left(acity) <= acity->shield_surplus &&
	    ai_fuzzy(pplayer, TRUE)) {
          virtualunit.owner = aplayer->player_no;
          virtualunit.x = acity->x;
          virtualunit.y = acity->y;
          utype = acity->currently_building;
          virtualunit.type = utype;
          virtualunit.veteran = do_make_unit_veteran(acity, utype);
          virtualunit.hp = unit_types[utype].hp;
/* yes, I know cloning all this code is bad form.  I don't really
want to write a funct that takes nine ints by reference. -- Syela */
          m = unit_type(funit)->move_rate;
          v = assess_danger_unit(pcity, funit);
          dist = assess_distance(pcity, funit, m, boatid, boatdist, boatspeed);
          igwall = unit_really_ignores_citywalls(funit);
          if ((is_ground_unit(funit) && v != 0) ||
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

          if (unit_flag(funit, F_HORSE)) {
            if (pikemen) v /= 2;
            else ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				    (v * m / (dist*2)));
          }

        if (unit_flag(funit, F_DIPLOMAT) && (dist <= 2 * m)) 
	     pcity->ai.diplomat_threat = !diplomat;

          v *= v;

          if (!igwall) danger2 += v * m / dist;
          else if (is_sailing_unit(funit)) danger3 += v * m / dist;
          else if (is_air_unit(funit) && !unit_flag(funit, F_NUCLEAR)) danger4 += v * m / dist;
          if (unit_flag(funit, F_MISSILE)) danger5 += v * m / MAX(m, dist);
          if (!unit_flag(funit, F_NUCLEAR)) { /* only SDI helps against NUCLEAR */
            v = dangerfunct(v, m, dist);
            danger += v;
            if (igwall) badmojo += v;
          }
        }
      city_list_iterate_end;

      unit_list_iterate(aplayer->units, punit)
        m = unit_type(punit)->move_rate;
        v = assess_danger_unit(pcity, punit);
        dist = assess_distance(pcity, punit, m, boatid, boatdist, boatspeed);
	igwall = unit_really_ignores_citywalls(punit);
          
        if ((is_ground_unit(punit) && v != 0) ||
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

        if (unit_flag(punit, F_HORSE)) {
          if (pikemen) v /= 2;
	  else ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				  (v * m / (dist*2)));
        }

        if (unit_flag(punit, F_DIPLOMAT) && (dist <= 2 * m))
	     pcity->ai.diplomat_threat = !diplomat;

        v *= v;

        if (!igwall) danger2 += v * m / dist;
        else if (is_sailing_unit(punit)) danger3 += v * m / dist;
        else if (is_air_unit(punit) && !unit_flag(punit, F_NUCLEAR)) danger4 += v * m / dist;
        if (unit_flag(punit, F_MISSILE)) danger5 += v * m / MAX(m, dist);
        if (!unit_flag(punit, F_NUCLEAR)) { /* only SDI helps against NUCLEAR */
          v = dangerfunct(v, m, dist);
          danger += v;
          if (igwall) badmojo += v;
        }
      unit_list_iterate_end;
    }
  } players_iterate_end;

  if (badmojo == 0) pcity->ai.wallvalue = 90;
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
  if (pcity->ai.grave_danger != 0)
    urgency += 10; /* really, REALLY urgent to defend */
  pcity->ai.danger = danger;
  if (pcity->ai.building_want[B_CITY] > 0 && danger2 != 0) {
    i = assess_defense(pcity);
    if (i == 0) pcity->ai.building_want[B_CITY] = 100 + urgency;
    else if (urgency != 0) {
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
  if (pcity->ai.building_want[B_COASTAL] > 0 && danger3 != 0) {
    i = assess_defense_igwall(pcity);
    if (i == 0) pcity->ai.building_want[B_COASTAL] = 100 + urgency;
    else if (urgency != 0) {
      if (danger3 > i * 2) pcity->ai.building_want[B_COASTAL] = 200 + urgency;
      else pcity->ai.building_want[B_COASTAL] = danger3 * 100 / i;
    } else {
      if (danger3 > i) pcity->ai.building_want[B_COASTAL] = 100;
      else pcity->ai.building_want[B_COASTAL] = danger3 * 100 / i;
    }
  }
/* COASTAL and SAM are TOTALLY UNTESTED and could be VERY WRONG -- Syela */
/* update 980803; COASTAL seems to work; still don't know about SAM. -- Syela */
  if (pcity->ai.building_want[B_SAM] > 0 && danger4 != 0) {
    i = assess_defense_igwall(pcity);
    if (i == 0) pcity->ai.building_want[B_SAM] = 100 + urgency;
    else if (danger4 > i * 2) pcity->ai.building_want[B_SAM] = 200 + urgency;
    else pcity->ai.building_want[B_SAM] = danger4 * 100 / i;
  }
  if (pcity->ai.building_want[B_SDI] > 0 && danger5 != 0) {
    i = assess_defense_igwall(pcity);
    if (i == 0) pcity->ai.building_want[B_SDI] = 100 + urgency;
    else if (danger5 > i * 2) pcity->ai.building_want[B_SDI] = 200 + urgency;
    else pcity->ai.building_want[B_SDI] = danger5 * 100 / i;
  }
  pcity->ai.urgency = urgency; /* need to cache this for bodyguards now -- Syela */
  return urgency;
}

/************************************************************************** 
How much we would want that unit..?
**************************************************************************/
int unit_desirability(Unit_Type_id i, bool defender)
{
  int desire = get_unit_type(i)->hp;
  int attack = get_unit_type(i)->attack_strength;
  int defense = get_unit_type(i)->defense_strength;

  if (unit_types[i].move_type != SEA_MOVING || !defender) {
    desire *= get_unit_type(i)->firepower;
  }

  /* It's irrelevant for defenders how fast they move. */
  desire *= defender ? SINGLE_MOVE : get_unit_type(i)->move_rate;

  if (defender) {
    desire *= defense;
  } else if (defense > attack) {
    /* We shouldn't use defense units as attackers. */
    return 0;
  } else {
    desire *= attack;
  }

  if (unit_type_flag(i, F_IGTER) && !defender) {
    desire *= 3;
  }
  if (unit_type_flag(i, F_PIKEMEN) && defender) {
    desire = (3 * desire) / 2;
  }
  if (unit_types[i].move_type == LAND_MOVING && defender) {
    /* Irnoclads could be *so* tempting.. */
    desire = (3 * desire) / 2;
  }

  return desire;
}

/************************************************************************** 
...
**************************************************************************/
int unit_attack_desirability(Unit_Type_id i)
{
  return unit_desirability(i, FALSE);
} 

/************************************************************************** 
What would be the best defender for that city?
Records the best defender type in choice.
Also sets the technology want for the units we can't build yet.
**************************************************************************/
static void process_defender_want(struct player *pplayer, struct city *pcity,
                                  int danger, struct ai_choice *choice)
{
  bool walls = city_got_citywalls(pcity);
  bool shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);
  bool defended = has_a_normal_defender(pcity);
  /* Technologies we would like to have. */
  int tech_desire[U_LAST];
  /* Our favourite unit. */
  int best = 0;
  Unit_Type_id best_unit_type = 0; /* FIXME: 0 is legal value! */

  memset(tech_desire, 0, sizeof(tech_desire));
  
  simple_ai_unit_type_iterate (unit_type) {
    int move_type = unit_types[unit_type].move_type;
    
    if (move_type == LAND_MOVING || move_type == SEA_MOVING) {
      /* How many technologies away it is? */
      int tech_dist = num_unknown_techs_for_goal(pplayer,
                        unit_types[unit_type].tech_requirement);
      /* How much we want the unit? */
      int desire = unit_desirability(unit_type, TRUE);
      
      /* We won't leave the castle empty when driving out to battlefield. */
      if (!defended && unit_type_flag(unit_type, F_FIELDUNIT)) desire = 0;
      
      desire /= POWER_DIVIDER/2; /* Good enough, no rounding errors. */
      desire *= desire;
      
      if (can_build_unit(pcity, unit_type)) {
        /* We can build the unit now... */
      
        if (walls && move_type == LAND_MOVING) {
          desire *= pcity->ai.wallvalue;
          /* TODO: More use of POWER_FACTOR ! */
          desire /= POWER_FACTOR;
        }
        
        if ((desire > best ||
             (desire == best && get_unit_type(unit_type)->build_cost <=
                                get_unit_type(best_unit_type)->build_cost))
            && unit_types[unit_type].build_cost <= pcity->shield_stock + 40) {
          best = desire;
          best_unit_type = unit_type;
        }
        
      } else if (tech_dist > 0 && (shore || move_type == LAND_MOVING)
                 && unit_types[unit_type].tech_requirement != A_LAST) {
        /* We first need to develop the tech required by the unit... */

        /* Cost (shield equivalent) of gaining these techs. */
        /* FIXME? Katvrr advises that this should be weighted more heavily in
         * big danger. */
        int tech_cost = total_bulbs_required_for_goal(pplayer,
                          unit_types[unit_type].tech_requirement) / 4
                        / city_list_size(&pplayer->cities);
        
        /* Contrary to the above, we don't care if walls are actually built 
         * - we're looking into the future now. */
        if (move_type == LAND_MOVING) {
          desire *= pcity->ai.wallvalue;
          desire /= POWER_FACTOR;
        }

        /* Yes, there's some similarity with kill_desire(). */
        tech_desire[unit_type] = desire * danger /
                                (unit_types[unit_type].build_cost + tech_cost);
      }
    }
  } simple_ai_unit_type_iterate_end;
  
  if (!walls && unit_types[best_unit_type].move_type == LAND_MOVING) {
    best *= pcity->ai.wallvalue;
    best /= POWER_FACTOR;
  }

  if (best == 0) best = 1; /* Avoid division by zero below. */

  /* Request appropriate techs for units we want to build. */
  
  simple_ai_unit_type_iterate (unit_type) {
    if (tech_desire[unit_type]) {
      Tech_Type_id tech_req = unit_types[unit_type].tech_requirement;
      int desire = tech_desire[unit_type]
                   * unit_types[best_unit_type].build_cost / best;
      
      pplayer->ai.tech_want[tech_req] += desire;
      
      freelog(LOG_DEBUG, "%s wants %s for defense with desire %d <%d>",
              pcity->name, advances[tech_req].name, desire,
              tech_desire[unit_type]);
    }
  } simple_ai_unit_type_iterate_end;
  
  choice->choice = best_unit_type;
  choice->want = danger;
  choice->type = CT_DEFENDER;
  return;
}

/************************************************************************** 
This function decides, what unit would be best for erasing enemy. It is called,
when we just want to kill something, we've found it but we don't have the unit
for killing that built yet - here we'll choose the type of that unit.

We will also set increase the technology want to get units which could perform
the job better.

I decided this funct wasn't confusing enough, so I made kill_something_with
send it some more variables for it to meddle with. -- Syela

TODO: Get rid of these parameters :).

(x,y) is location of the target.
(best_value, best_choice) is pre-filled with our current choice, we only 
  consider units of the same move_type as best_choice
**************************************************************************/
static void process_attacker_want(struct player *pplayer, struct city *pcity,
                                  int value, Unit_Type_id victim_unit_type,
                                  bool veteran, int x, int y, bool unhap,
                                  int *best_value, int *best_choice,
                                  int boatx, int boaty, int boatspeed,
                                  int needferry)
{ 
  /* The enemy city.  acity == NULL means stray enemy unit */
  struct city *acity = map_get_city(x, y);
  bool shore = is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN);
  int orig_move_type = unit_types[*best_choice].move_type;
  int victim_move_rate = (acity ? 1
                                : (unit_types[victim_unit_type].move_rate *
                             (unit_type_flag(victim_unit_type, F_IGTER) ? 3
                                                                        : 1)));
  int victim_count = (acity ? unit_list_size(&(map_get_tile(x, y)->units)) + 1
                            : 1);

  simple_ai_unit_type_iterate (unit_type) {
    Tech_Type_id tech_req = unit_types[unit_type].tech_requirement;
    int move_type = unit_types[unit_type].move_type;
    int tech_dist;
    
    if (tech_req != A_LAST) {
      tech_dist = num_unknown_techs_for_goal(pplayer, tech_req);
    } else {
      tech_dist = 0;
    }
    
    if ((move_type == LAND_MOVING || (move_type == SEA_MOVING && shore))
        && tech_req != A_LAST
        && (tech_dist > 0 ||
            !can_build_unit_direct(pcity, unit_types[unit_type].obsoleted_by))
        && unit_types[unit_type].attack_strength > 0 /* or we'll get SIGFPE */
        && move_type == orig_move_type) {
      /* TODO: Case for B_AIRPORT. -- Raahul */
      bool will_be_veteran = (move_type == LAND_MOVING ||
                              player_knows_improvement_tech(pplayer, B_PORT));
      /* Cost (shield equivalent) of gaining these techs. */
      /* FIXME? Katvrr advises that this should be weighted more heavily in big
       * danger. */
      int tech_cost = total_bulbs_required_for_goal(pplayer,
                        unit_types[unit_type].tech_requirement) / 4
                      / city_list_size(&pplayer->cities);
      int move_rate = unit_types[unit_type].move_rate;
      int move_time;
      int bcost_balanced = build_cost_balanced(unit_type);
      /* See description of kill_desire() for info about this variables. */
      int bcost = unit_types[unit_type].build_cost;
      int vuln;
      int attack = base_unit_belligerence_primitive(unit_type, will_be_veteran,
                     SINGLE_MOVE, unit_types[unit_type].hp);
      /* Values to be computed */
      int desire, want;
      
      /* Take into account reinforcements strength */
      if (acity) attack += acity->ai.attack;
      
      attack *= attack;

      if (unit_type_flag(unit_type, F_IGTER)) {
        /* TODO: Use something like IGTER_MOVE_COST. -- Raahul */
        move_rate *= SINGLE_MOVE;
      }

      /* Set the move_time appropriatelly. */
      
      switch(move_type) {
      case LAND_MOVING:
        if (boatspeed > 0) {
          /* It's either city or too far away, so don't bother with
           * victim_move_rate. */
          move_time = (warmap.cost[boatx][boaty] + move_rate - 1) / move_rate
                      + 1 + warmap.seacost[x][y] / boatspeed; /* kludge */
          if (unit_type_flag(unit_type, F_MARINES)) move_time -= 1;
          
        } else if (warmap.cost[x][y] <= move_rate) {
          /* It's adjacent. */
          move_time = 1;

        } else {
          /* Cost for attacking the victim. */
          /* FIXME? Why we should multiply the cost by move rate?! --pasky */
          move_time = (warmap.cost[x][y] * victim_move_rate + move_rate - 1)
                      / move_rate;
        }
        break;
      case SEA_MOVING:
        if (warmap.seacost[x][y] <= move_rate) {
          /* It's adjectent. */
          move_time = 1;
          
        } else {
          /* See above. */
          move_time = (warmap.seacost[x][y] * victim_move_rate + move_rate - 1)
                      / move_rate;
        }
        break;
      default:
        /* FIXME? This is never reached? */
        if (real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE 
            <= move_rate) {
          /* It's adjectent. */
          move_time = 1;
        } else {
          move_time = real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE
                      * victim_move_rate / move_rate;
        }
      }

      /* Estimate strength of the enemy. */
      
      vuln = unit_vulnerability_virtual2(unit_type, victim_unit_type, x, y,
                                         FALSE, veteran, FALSE, 0);

      if (move_type == LAND_MOVING && acity
          && move_time > (player_knows_improvement_tech(city_owner(acity),
                                                        B_CITY) ? 2 : 4)
          && !unit_type_flag(unit_type, F_IGWALL)
          && !city_got_citywalls(acity)) {
        /* Bonus of Citywalls can be expressed as * 3 (because Citywalls have
         * 200% defense bonus [TODO: genimpr]), and vulnerability is quadratic,
         * thus we multiply it by 9. */
        /* FIXME: Use acity->ai.wallvalue? --pasky */
        vuln *= 9;
      }

      /* Not bothering to s/!vuln/!pdef/ here for the time being. -- Syela
       * (this is noted elsewhere as terrible bug making warships yoyoing) 
       * as the warships will go to enemy cities hoping that the enemy builds 
       * something for them to kill*/
      if (move_type != LAND_MOVING && vuln == 0) {
        desire = 0;
        
      } else if ((move_type == LAND_MOVING || move_type == HELI_MOVING)
                 && acity && acity->ai.invasion == 2) {
        desire = bcost * SHIELD_WEIGHTING;
        
      } else {
        if (!acity) {
          desire = kill_desire(value, attack, bcost, vuln, victim_count);
          
        } else {
          int city_attack = acity->ai.attack * acity->ai.attack;

          /* See aiunit.c:find_something_to_kill() for comments. */
          
          desire = kill_desire(value, attack, (bcost + acity->ai.bcost), vuln,
                               victim_count);
          
          if (value * city_attack > acity->ai.bcost * vuln) {
            desire -= kill_desire(value, city_attack, acity->ai.bcost, vuln,
                                  victim_count);
          }
        }
      }
      
      desire -= tech_cost * SHIELD_WEIGHTING;
      /* We can be possibly making some people of our homecity unhappy - then
       * we don't want to travel too far away to our victims. */
      /* TODO: Unify the 'unhap' dependency to some common function. */
      desire -= move_time * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING
                             : SHIELD_WEIGHTING);

      want = military_amortize(desire, MAX(1, move_time),
                               bcost_balanced + needferry);
      
      if (want > 0) {
        if (tech_dist > 0) {
          /* This is a future unit, tell the scientist how much we need it */
          pplayer->ai.tech_want[tech_req] += want;
          
          freelog(LOG_DEBUG,
                  "%s wants %s, %s to punish %s@(%d, %d) with desire %d.", 
                  pcity->name, advances[tech_req].name, unit_name(unit_type),
                  (acity ? acity->name : "enemy"), x, y, want);
          
        } else if (want > *best_value) {
          /* This is a real unit and we really want it */
          freelog(LOG_DEBUG, "%s overriding %s(%d) with %s(%d)"
                  " [attack=%d,value=%d,move_time=%d,vuln=%d,bcost=%d]",
                  pcity->name, unit_name(*best_choice), *best_value,
                  unit_name(unit_type), want, attack, value, move_time, vuln,
                  bcost);

          *best_choice = unit_type;
          *best_value = want;
        }
      }
    }
  } simple_ai_unit_type_iterate_end;
}

/************************************************************************** 
...
**************************************************************************/
static void kill_something_with(struct player *pplayer, struct city *pcity, 
				struct unit *myunit, struct ai_choice *choice)
{
  int a, b, c, d, e, f, g; /* variables in the attacker-want equation */
  Unit_Type_id v, n;
  int m;
  bool vet;
  int dist, b0, fprime;
  int x, y;
  bool unhap = FALSE;
  struct unit *pdef, *aunit, *ferryboat;
  struct city *acity;
  int boatid = 0, bx = 0, by = 0;
  int needferry = 0, fstk, boatspeed;
  bool sanity;

  if (pcity->ai.danger != 0 && assess_defense(pcity) == 0) return;

  if (is_ground_unit(myunit)) boatid = find_boat(pplayer, &bx, &by, 2);

  ferryboat = player_find_unit_by_id(pplayer, boatid);
  /* FIXME: hardcoded boat speed */
 if (ferryboat) boatspeed = (unit_flag(ferryboat, F_TRIREME)
			     ? 2*SINGLE_MOVE : 4*SINGLE_MOVE);
  else boatspeed = (get_invention(pplayer, game.rtech.nav) != TECH_KNOWN
		    ? 2*SINGLE_MOVE : 4*SINGLE_MOVE);

  fstk = find_something_to_kill(pplayer, myunit, &x, &y);

  acity = map_get_city(x, y);
  if (!acity) {
    aunit = get_defender(myunit, x, y);
  } else {
    aunit = NULL;
  }

  if (acity && !pplayers_at_war(pplayer, city_owner(acity))) {
    acity = NULL;
  }

/* this code block and the analogous block in f_s_t_k should be calls
to the same utility function, but I don't want to break anything right
before the 1.7.0 release so I'm letting this stay ugly. -- Syela */

/* logically we should adjust this for race attack tendencies */
  if ((acity || aunit)) {
    v = myunit->type;
    vet = myunit->veteran;

    a = unit_belligerence_basic(myunit);
    if (acity) a += acity->ai.attack;
    a *= a;

    if (acity) {
      pdef = get_defender(myunit, x, y);

      m = unit_types[v].move_rate;
      if (unit_type_flag(v, F_IGTER)) m *= 3; /* not quite right */

      sanity = (goto_is_sane(myunit, acity->x, acity->y, TRUE) &&
              warmap.cost[x][y] <= (MIN(6, m) * THRESHOLD));

      if (is_ground_unit(myunit)) {
        if (!sanity) {
          if (boatid != 0) c = (warmap.cost[bx][by] + m - 1) / m + 1 +
                warmap.seacost[acity->x][acity->y] / boatspeed; /* kluge */
          else c = warmap.seacost[acity->x][acity->y] / boatspeed + 1;
          if (unit_flag(myunit, F_MARINES)) c -= 1;
	  freelog(LOG_DEBUG, "%s attempting to attack via ferryboat"
			" (boatid = %d, c = %d)", unit_types[v].name, boatid, c);
        } else c = (warmap.cost[acity->x][acity->y] + m - 1) / m;
      } else if (is_sailing_unit(myunit))
        c = (warmap.seacost[acity->x][acity->y] + m - 1) / m;
      else c = real_map_distance(myunit->x, myunit->y, acity->x, acity->y) * SINGLE_MOVE / m;

      n = ai_choose_defender_versus(acity, v);
      if (c > 1) {
	d = unit_vulnerability_virtual2(v, n, x, y, FALSE,
					do_make_unit_veteran(acity, n),
					FALSE, 0);
        b = unit_types[n].build_cost + 40;
        vet = do_make_unit_veteran(acity, n);
      } else {
        d = 0;
        b = 40;
        vet = FALSE;
      }
      if (pdef) {
/*        n = pdef->type;    Now, really, I could not possibly have written this.
Yet, somehow, this line existed, and remained here for months, bugging the AI
tech progression beyond all description.  Only when adding the override code
did I realize the magnitude of my transgression.  How despicable. -- Syela */
	m = unit_vulnerability_virtual2(v, pdef->type, x, y, FALSE,
					pdef->veteran, myunit->id, pdef->hp);
        if (d < m) {
          d = m;
          b = unit_type(pdef)->build_cost + 40; 
          vet = pdef->veteran;
          n = pdef->type; /* and not before, or heinous things occur!! */
        }
      }
      if (!is_ground_unit(myunit) && !is_heli_unit(myunit) &&
         !TEST_BIT(acity->ai.invasion, 0)) b -= 40; /* boats can't conquer cities */
      if (myunit->id == 0 && (!unit_really_ignores_citywalls(myunit)) &&
          c > (player_knows_improvement_tech(city_owner(acity),
					     B_CITY) ? 2 : 4) &&
          !city_got_citywalls(acity)) d *= 9;
    } /* end dealing with cities */

    else {
      pdef = aunit; /* we KNOW this is the get_defender -- Syela */

      m = unit_type(pdef)->build_cost;
      b = m;
      sanity = TRUE;

      if (is_ground_unit(myunit)) dist = warmap.cost[x][y];
      else if (is_sailing_unit(myunit)) dist = warmap.seacost[x][y];
      else dist = real_map_distance(pcity->x, pcity->y, x, y) * SINGLE_MOVE;
      if (dist > m) {
        dist *= unit_type(pdef)->move_rate;
        if (unit_flag(pdef, F_IGTER)) dist *= 3;
      }
      if (dist == 0) dist = 1;

      m = unit_types[v].move_rate;
      if (unit_type_flag(v, F_IGTER)) m *= 3; /* not quite right */
      c = ((dist + m - 1) / m);

      n = pdef->type;
      d = unit_vulnerability_virtual2(v, n, x, y,
				      pdef->activity == ACTIVITY_FORTIFIED,
				      pdef->veteran, myunit->id, pdef->hp);
      vet = pdef->veteran;
    } /* end dealing with units */

    if (pcity->id == myunit->homecity || myunit->id == 0)
      unhap = ai_assess_military_unhappiness(pcity, get_gov_pplayer(pplayer));

    if (is_ground_unit(myunit) && !sanity && boatid == 0)
      needferry = 40; /* cost of ferry */
    f = unit_types[v].build_cost;
    fprime = build_cost_balanced(v);

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
      if (!acity) {
	b0 = kill_desire(b, a, f, d, g);
      } else {
       int a_squared = acity->ai.attack * acity->ai.attack;

       b0 = kill_desire(b, a, f + acity->ai.bcost, d, g);
       if (b * a_squared > acity->ai.bcost * d) {
         b0 -= kill_desire(b, a_squared, acity->ai.bcost, d, g);
	}
      }
    }
    b0 -= c * (unhap ? SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING : SHIELD_WEIGHTING);
    e = military_amortize(b0, MAX(1, c), fprime + needferry);

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
		myunit->id, unit_type(pdef)->name,
		pdef->x, pdef->y, a, b, c, d, e, fstk, f);
    }
#endif

    if (myunit->id == 0) {  /* I don't want to know what happens otherwise -- Syela */
      if (sanity)
        process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, 0, 0, 0, needferry);
      else if (boatid != 0) process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, bx, by, boatspeed, needferry);
      else process_attacker_want(pplayer, pcity, b, n, vet, x, y, unhap,
           &e, &v, myunit->x, myunit->y, boatspeed, needferry);
    }

    if (e > choice->want) { 
      if (!city_got_barracks(pcity) && is_ground_unit(myunit)) {
        if (player_knows_improvement_tech(pplayer, B_BARRACKS3))
          choice->choice = B_BARRACKS3;
        else if (player_knows_improvement_tech(pplayer, B_BARRACKS2))
          choice->choice = B_BARRACKS2;
        else choice->choice = B_BARRACKS;
        choice->want = e;
        choice->type = CT_BUILDING;
      } else {
        if (myunit->id == 0) {
          choice->choice = v;
          choice->type = CT_ATTACKER;
          choice->want = e;
          if (needferry != 0) ai_choose_ferryboat(pplayer, pcity, choice);
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
Checks if there is a port or a ship being built within certain distance.
***********************************************************************/
static bool port_is_within(struct player *pplayer, int distance)
{
  city_list_iterate(pplayer->cities, pcity) {
    if (warmap.seacost[pcity->x][pcity->y] <= distance) {
      if (city_got_building(pcity, B_PORT))
	return TRUE;

      if (!pcity->is_building_unit && pcity->currently_building == B_PORT
	  && pcity->shield_stock >= improvement_value(B_PORT))
	return TRUE;

      if (!player_knows_improvement_tech(pplayer, B_PORT)
	  && pcity->is_building_unit
	  && is_water_unit(pcity->currently_building)
	  && unit_types[pcity->currently_building].attack_strength >
	  unit_types[pcity->currently_building].transport_capacity)
	return TRUE;
    }
  } city_list_iterate_end;

  return FALSE;
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

  init_choice(choice);

/* TODO: recognize units that can DEFEND_HOME but are in the field. -- Syela */

  urgency = assess_danger(pcity); /* calling it now, rewriting old wall code */
  def = assess_defense_quadratic(pcity); /* has to be AFTER assess_danger thanks to wallvalue */
/* changing to quadratic to stop AI from building piles of small units -- Syela */
  danger = pcity->ai.danger; /* we now have our warmap and will use it! */
  freelog(LOG_DEBUG, "Assessed danger for %s = %d, Def = %d",
	  pcity->name, danger, def);

  if (pcity->ai.diplomat_threat && def != 0){
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

  if (danger != 0) { /* otherwise might be able to wait a little longer to defend */
/* old version had danger -= def in it, which was necessary before disband/upgrade
code was added and walls got built, but now danger -= def would be very bad -- Syela */
    if (danger >= def) {
      if (urgency == 0) danger = 100; /* don't waste money otherwise */
      else if (danger >= def * 2) danger = 200 + urgency;
      else { danger *= 100; danger /= def; danger += urgency; }
/* without the += urgency, wasn't buying with danger == def.  Duh. -- Syela */
    } else { danger *= 100; danger /= def; }
    if (pcity->shield_surplus <= 0 && def != 0) danger = 0;
/* this is somewhat of an ugly kluge, but polar cities with no ability to
increase prod were buying alpines, panicking, disbanding them, buying alpines
and so on every other turn.  This will fix that problem, hopefully without
creating any other problems that are worse. -- Syela */
    if (pcity->ai.building_want[B_CITY] != 0 && def != 0 && can_build_improvement(pcity, B_CITY)
        && (danger < 101 || unit_list_size(&ptile->units) > 1 ||
/* walls before a second defender, unless we need it RIGHT NOW */
         (pcity->ai.grave_danger == 0 && /* I'm not sure this is optimal */
         pplayer->economic.gold > (80 - pcity->shield_stock) * 2)) &&
	ai_fuzzy(pplayer, TRUE)) {
/* or we can afford just to buy walls.  Added 980805 -- Syela */
      choice->choice = B_CITY; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_CITY]; /* hacked by assess_danger */
      if (urgency == 0 && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (pcity->ai.building_want[B_COASTAL] != 0 && def != 0 &&
        can_build_improvement(pcity, B_COASTAL) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1) &&
	ai_fuzzy(pplayer, TRUE)) {
      choice->choice = B_COASTAL; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_COASTAL]; /* hacked by assess_danger */
      if (urgency == 0 && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (pcity->ai.building_want[B_SAM] != 0 && def != 0 &&
        can_build_improvement(pcity, B_SAM) &&
        (danger < 101 || unit_list_size(&ptile->units) > 1) &&
	ai_fuzzy(pplayer, TRUE)) {
      choice->choice = B_SAM; /* great wall is under domestic */
      choice->want = pcity->ai.building_want[B_SAM]; /* hacked by assess_danger */
      if (urgency == 0 && choice->want > 100) choice->want = 100;
      choice->type = CT_BUILDING;
    } else if (danger > 0 && unit_list_size(&ptile->units) <= urgency) {
      process_defender_want(pplayer, pcity, danger, choice);
      if (urgency == 0 && unit_types[choice->choice].defense_strength == 1) {
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

  if (choice->want > 100) {
    /* We are in severe danger, don't try to build attackers */
    return;
  }

  unit_list_iterate(map_get_tile(pcity->x, pcity->y)->units, punit)
    if (((unit_type(punit)->attack_strength * 4 >
        unit_type(punit)->defense_strength * 5) ||
        unit_flag(punit, F_FIELDUNIT)) &&
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
    virtualunit.veteran = TRUE;
    virtualunit.hp = unit_types[v].hp;
    kill_something_with(pplayer, pcity, &virtualunit, choice);
  }
  return;
}

/************************************************************************** 
This function computes distances between cities for purpose of building crowds
of water-consuming Caravans or smoggish Freights which want to add their brick
to the wonder being built somewhere out there.

At the function entry point, our warmap is intact.  We need to do two things:
establish faraway for THIS city and establish distance_to_wonder_city for ALL
cities.

FIXME? I think this just can't work when we have more wonder cities on one
continent, can it? --pasky

FIXME: This should be definitively somewhere else. And aicity.c sounds like
fine candidate. --pasky
**************************************************************************/
void establish_city_distances(struct player *pplayer, struct city *pcity)
{
  int distance, wonder_continent;
  Unit_Type_id freight = best_role_unit(pcity, F_HELP_WONDER);
  int moverate = (freight == U_LAST) ? SINGLE_MOVE
                                     : get_unit_type(freight)->move_rate;

  if (!pcity->is_building_unit && is_wonder(pcity->currently_building)) {
    wonder_continent = map_get_continent(pcity->x, pcity->y);
  } else {
    wonder_continent = 0;
  }

  pcity->ai.downtown = 0;
  city_list_iterate(pplayer->cities, othercity) {
    distance = warmap.cost[othercity->x][othercity->y];
    if (wonder_continent != 0
        && map_get_continent(othercity->x, othercity->y) == wonder_continent) {
      othercity->ai.distance_to_wonder_city = distance;
    }

    /* How many people near enough would help us? */
    distance += moverate - 1; distance /= moverate;
    pcity->ai.downtown += MAX(0, 5 - distance);
  } city_list_iterate_end;
}
