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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

#include "aiair.h"
#include "aicity.h"
#include "aidiplomat.h"
#include "aihand.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"

#include "advmilitary.h"

static int assess_danger(struct city *pcity);

/********************************************************************** 
This function should assign a value to choice and want, where want is a value
between 1 and 100.

If choice is A_UNSET, this advisor doesn't want any particular tech
researched at the moment.
***********************************************************************/
void military_advisor_choose_tech(struct player *pplayer,
				  struct ai_choice *choice)
{
  /* This function hasn't been implemented yet. */
  init_choice(choice);
}

/**************************************************************************
  Choose best attacker based on movement type. It chooses based on unit
  desirability without regard to cost, unless costs are equal. This is
  very wrong. FIXME, use amortize on time to build.
**************************************************************************/
static Unit_Type_id ai_choose_attacker(struct city *pcity,
                                       enum unit_move_type which)
{
  Unit_Type_id bestid = -1;
  int best = 0;
  int cur;

  simple_ai_unit_type_iterate(i) {
    cur = ai_unit_attack_desirability(i);
    if (which == unit_types[i].move_type) {
      if (can_build_unit(pcity, i)
          && (cur > best
              || (cur == best
                  && get_unit_type(i)->build_cost
                     <= get_unit_type(bestid)->build_cost))) {
        best = cur;
        bestid = i;
      }
    }
  } simple_ai_unit_type_iterate_end;

  return bestid;
}

/**************************************************************************
  Choose best defender based on movement type. It chooses based on unit
  desirability without regard to cost, unless costs are equal. This is
  very wrong. FIXME, use amortize on time to build.

  We should only be passed with L_DEFEND_GOOD role for now, since this
  is the only role being considered worthy of bodyguarding in findjob.
**************************************************************************/
static Unit_Type_id ai_choose_bodyguard(struct city *pcity,
                                        enum unit_move_type move_type,
                                        enum unit_role_id role)
{
  Unit_Type_id bestid = -1;
  int j, best = 0;

  simple_ai_unit_type_iterate(i) {
    /* Only consider units of given role, or any if L_LAST */
    if (role != L_LAST) {
      if (!unit_has_role(i, role)) {
        continue;
      }
    }

    /* Only consider units of same move type */
    if (unit_types[i].move_type != move_type) {
      continue;
    }

    /* Now find best */
    if (can_build_unit(pcity, i)) {
      j = ai_unit_defence_desirability(i);
      if (j > best || (j == best && get_unit_type(i)->build_cost <=
                               get_unit_type(bestid)->build_cost)) {
        best = j;
        bestid = i;
      }
    }
  } simple_ai_unit_type_iterate_end;
  return bestid;
}

/********************************************************************** 
Helper for assess_defense_quadratic and assess_defense_unit.
***********************************************************************/
static int base_assess_defense_unit(struct city *pcity, struct unit *punit,
                                    bool igwall, bool quadratic,
                                    int wall_value)
{
  int defense;
  bool do_wall = FALSE;

  if (!is_military_unit(punit)) {
    return 0;
  }

  defense = get_defense_power(punit) * punit->hp;
  if (!is_sailing_unit(punit)) {
    defense *= unit_type(punit)->firepower;
    if (is_ground_unit(punit)) {
      if (pcity) {
        do_wall = (!igwall && city_got_citywalls(pcity));
        defense *= 3;
      }
    }
  }
  defense /= POWER_DIVIDER;

  if (quadratic) {
    defense *= defense;
  }

  if (do_wall) {
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
    if (defense > 1<<15) {
      defense = 1<<15; /* more defense than we know what to do with! */
    }
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
  if (sailing && !is_ocean_near_tile(pcity->x, pcity->y)) {
    return 0;
  }

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
  Set (overwrite) our want for a building. Syela tries to explain:
   
    My first attempt to allow ng_wa >= 200 led to stupidity in cities 
    with no defenders and danger = 0 but danger > 0.  Capping ng_wa at 
    100 + urgency led to a failure to buy walls as required.  Allowing 
    want > 100 with !urgency led to the AI spending too much gold and 
    falling behind on science.  I'm trying again, but this will require 
    yet more tedious observation -- Syela
   
  The idea in this horrible function is that there is an enemy nearby
  that can whack us, so let's build something that can defend against
  him. If danger is urgent and overwhelming, danger is 200+, if it is
  only overwhelming, set it depending on danger. If it is underwhelming,
  set it to 100 pluss urgency.

  This algorithm is very strange. But I created it by nesting up
  Syela's convoluted if ... else logic, and it seems to work. -- Per
***********************************************************************/
static void ai_reevaluate_building(struct city *pcity, int *value, 
                                   int urgency, int danger, 
                                   int defense)
{
  if (*value == 0 || danger <= 0) {
    return;
  }

  *value = 100 + MAX(0, urgency); /* default */

  if (urgency > 0 && danger > defense * 2) {
    *value += 100;
  } else if (defense != 0 && danger > defense) {
    *value = MAX(danger * 100 / defense, *value);
  }
}

/********************************************************************** 
  Create cached information about danger, urgency and grave danger to
  our cities. 

  Danger is a weight on how much power enemy units nearby have, which
  is compared to our defence.

  Urgency is the number of hostile units that can attack us in three
  turns.

  Grave danger is number of units that can attack us next turn.

  Generates a warmap around pcity.
***********************************************************************/
static int assess_danger(struct city *pcity)
{
  int i;
  int danger[5];
  Unit_Type_id utype;
  struct player *pplayer = city_owner(pcity);
  bool pikemen = FALSE;
  int urgency = 0;
  int igwall_threat = 0;
  struct unit virtualunit;
  struct unit *funit = &virtualunit; /* saves me a lot of typing. -- Syela */
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);

  memset(&virtualunit, 0, sizeof(struct unit));
  memset(&danger, 0, sizeof(danger));

  generate_warmap(pcity, NULL);	/* generates both land and sea maps */

  pcity->ai.grave_danger = 0;
  pcity->ai.diplomat_threat = FALSE;
  pcity->ai.has_diplomat = FALSE;

  unit_list_iterate(ptile->units, punit) {
    if (unit_flag(punit, F_DIPLOMAT)) pcity->ai.has_diplomat = TRUE;
    if (unit_flag(punit, F_PIKEMEN)) pikemen = TRUE;
  } unit_list_iterate_end;

  players_iterate(aplayer) {
    int boatspeed;
    int boatid, boatdist;
    int x = pcity->x, y = pcity->y; /* dummy variables */
    int move_rate, dist, vulnerability;
    bool igwall;

    if (!pplayers_at_war(city_owner(pcity), aplayer)) {
      /* Ignore players we are not at war with. This is not optimal,
         but will have to do until we have working diplomacy. */
      continue;
    }

    boatspeed = ((get_invention(aplayer, game.rtech.nav) == TECH_KNOWN) 
                 ? 4 * SINGLE_MOVE : 2 * SINGLE_MOVE); /* likely speed */
    boatid = find_boat(aplayer, &x, &y, 0); /* acquire a boat */
    if (boatid != 0) {
      boatdist = warmap.seacost[x][y]; /* distance to acquired boat */
    } else {
      boatdist = -1; /* boat wanted */
    }

    /* Look for enemy cities that will complete a unit next turn */
    city_list_iterate(aplayer->cities, acity) {
      if (!acity->is_building_unit
          || build_points_left(acity) > acity->shield_surplus
	  || ai_fuzzy(pplayer, TRUE)) {
        /* the enemy city will not complete a unit next turn */
        continue;
      }
      virtualunit.owner = aplayer->player_no;
      virtualunit.x = acity->x;
      virtualunit.y = acity->y;
      utype = acity->currently_building;
      virtualunit.type = utype;
      virtualunit.veteran = do_make_unit_veteran(acity, utype);
      virtualunit.hp = unit_types[utype].hp;
      /* yes, I know cloning all this code is bad form.  I don't really
       * want to write a funct that takes nine ints by reference. 
       * -- Syela 
       */
      move_rate = unit_type(funit)->move_rate;
      vulnerability = assess_danger_unit(pcity, funit);
      dist = assess_distance(pcity, funit, move_rate, boatid, boatdist, 
                             boatspeed);
      igwall = unit_really_ignores_citywalls(funit);
      if ((is_ground_unit(funit) && vulnerability != 0) ||
          (is_ground_units_transport(funit))) {
        if (dist <= move_rate * 3) urgency++;
        if (dist <= move_rate) pcity->ai.grave_danger++;
        /* NOTE: This should actually implement grave_danger, which is 
         * supposed to be a feedback-sensitive formula for immediate 
         * danger.  I'm having second thoughts about the best 
         * implementation, and therefore this will have to wait until 
         * after 1.7.0.  I don't want to do anything I'm not totally 
         * sure about and can't thoroughly test in the hours before
         * the release.  The AI is in fact vulnerable to gang-attacks, 
         * but I'm content to let it remain that way for now. -- Syela 
         * 980805 */
      }

      if (unit_flag(funit, F_HORSE)) {
	if (pikemen) {
	  vulnerability /= 2;
	} else {
	  (void) ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				    (vulnerability * move_rate /
				     (dist * 2)));
	}
      }

      if (unit_flag(funit, F_DIPLOMAT) && (dist <= 2 * move_rate)) {
        pcity->ai.diplomat_threat = TRUE;
      }

      vulnerability *= vulnerability; /* positive feedback */

      if (!igwall) {
        danger[1] += vulnerability * move_rate / dist; /* walls */
      } else if (is_sailing_unit(funit)) {
        danger[2] += vulnerability * move_rate / dist; /* coastal */
      } else if (is_air_unit(funit) && !unit_flag(funit, F_NUCLEAR)) {
        danger[3] += vulnerability * move_rate / dist; /* SAM */
      }
      if (unit_flag(funit, F_MISSILE)) {
        /* SDI */
        danger[4] += vulnerability * move_rate / MAX(move_rate, dist);
      }
      if (!unit_flag(funit, F_NUCLEAR)) { 
        /* only SDI helps against NUCLEAR */
        vulnerability = dangerfunct(vulnerability, move_rate, dist);
        danger[0] += vulnerability;
        if (igwall) {
          igwall_threat += vulnerability;
        }
      }
    } city_list_iterate_end;

    /* Now look for enemy units */
    unit_list_iterate(aplayer->units, punit) {
      move_rate = unit_type(punit)->move_rate;
      vulnerability = assess_danger_unit(pcity, punit);
      dist = assess_distance(pcity, punit, move_rate, boatid, boatdist, 
                             boatspeed);
      igwall = unit_really_ignores_citywalls(punit);

      if ((is_ground_unit(punit) && vulnerability != 0) ||
          (is_ground_units_transport(punit))) {
        if (dist <= move_rate * 3) urgency++;
        if (dist <= move_rate) pcity->ai.grave_danger++;
        /* see comment in previous loop on grave danger */
      }

      if (unit_flag(punit, F_HORSE)) {
	if (pikemen) {
	  vulnerability /= 2;
	} else {
	  (void) ai_wants_role_unit(pplayer, pcity, F_PIKEMEN,
				    (vulnerability * move_rate /
				     (dist * 2)));
	}
      }

      if (unit_flag(punit, F_DIPLOMAT) && (dist <= 2 * move_rate)) {
	pcity->ai.diplomat_threat = TRUE;
      }

      vulnerability *= vulnerability; /* positive feedback */

      if (!igwall) {
        danger[1] += vulnerability * move_rate / dist; /* walls */
      } else if (is_sailing_unit(funit)) {
        danger[2] += vulnerability * move_rate / dist; /* coastal */
      } else if (is_air_unit(funit) && !unit_flag(funit, F_NUCLEAR)) {
        danger[3] += vulnerability * move_rate / dist; /* SAM */
      }
      if (unit_flag(funit, F_MISSILE)) {
        /* SDI */
        danger[4] += vulnerability * move_rate / MAX(move_rate, dist);
      }
      if (!unit_flag(funit, F_NUCLEAR)) {
        /* only SDI helps against NUCLEAR */
        vulnerability = dangerfunct(vulnerability, move_rate, dist);
        danger[0] += vulnerability;
        if (igwall) {
          igwall_threat += vulnerability;
        }
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  if (igwall_threat == 0) {
    pcity->ai.wallvalue = 90;
  } else {
    pcity->ai.wallvalue = (danger[0] * 9 - igwall_threat * 8) 
                           * 10 / (danger[0]);
  }

  /* Watch out for integer overflows */
  for (i = 0; i < 5; i++) {
    if (danger[i] < 0 || danger[i] > 1<<24) {
      /* I hope never to see this! */
      freelog(LOG_ERROR, "Dangerous danger[%d] (%d) in %s.  Beware of "
              "overflow.", i, danger[i], pcity->name);
      danger[i] = danger[i]>>2; /* reduce danger of overflow */
    }
  }

  if (pcity->ai.grave_danger != 0) {
    /* really, REALLY urgent to defend */
    urgency += 10;
  }

  ai_reevaluate_building(pcity, &pcity->ai.building_want[B_CITY],
                         urgency, danger[1], assess_defense(pcity));
  ai_reevaluate_building(pcity, &pcity->ai.building_want[B_COASTAL],
                         urgency, danger[2], 
                         assess_defense_igwall(pcity));
  ai_reevaluate_building(pcity, &pcity->ai.building_want[B_SAM],
                         urgency, danger[3], 
                         assess_defense_igwall(pcity));
  ai_reevaluate_building(pcity, &pcity->ai.building_want[B_SDI],
                         urgency, danger[4], 
                         assess_defense_igwall(pcity));

  pcity->ai.danger = danger[0];
  pcity->ai.urgency = urgency;

  return urgency;
}

/************************************************************************** 
  How much we would want that unit to defend a city? (Do not use this 
  function to find bodyguards for ships or air units.)
**************************************************************************/
int ai_unit_defence_desirability(Unit_Type_id i)
{
  int desire = get_unit_type(i)->hp;
  int attack = get_unit_type(i)->attack_strength;
  int defense = get_unit_type(i)->defense_strength;

  /* Sea and helicopters often have their firepower set to 1 when
   * defending. We can't have such units as defenders. */
  if (unit_types[i].move_type != SEA_MOVING
      && unit_types[i].move_type != HELI_MOVING) {
    /* Sea units get 1 firepower in Pearl Harbour,
     * and helicopters very bad against air units */
    desire *= get_unit_type(i)->firepower;
  }
  desire *= defense;
  desire += get_unit_type(i)->move_rate / SINGLE_MOVE;
  desire += attack;
  if (unit_type_flag(i, F_PIKEMEN)) {
    desire += desire / 2;
  }
  return desire;
}

/************************************************************************** 
  How much we would want that unit to attack with?
**************************************************************************/
int ai_unit_attack_desirability(Unit_Type_id i)
{
  int desire = get_unit_type(i)->hp;
  int attack = get_unit_type(i)->attack_strength;
  int defense = get_unit_type(i)->defense_strength;

  desire *= get_unit_type(i)->move_rate;
  desire *= get_unit_type(i)->firepower;
  desire *= attack;
  desire += defense;
  if (unit_type_flag(i, F_IGTER)) {
    desire += desire / 2;
  }
  if (unit_type_flag(i, F_IGTIRED)) {
    desire += desire / 4;
  }
  if (unit_type_flag(i, F_MARINES)) {
    desire += desire / 4;
  }
  if (unit_type_flag(i, F_IGWALL)) {
    desire += desire / 4;
  }
  return desire;
}

/************************************************************************** 
  What would be the best defender for that city? Records the best defender 
  type in choice. Also sets the technology want for the units we can't 
  build yet.
**************************************************************************/
static void process_defender_want(struct player *pplayer, struct city *pcity,
                                  int danger, struct ai_choice *choice)
{
  bool walls = city_got_citywalls(pcity);
  bool shore = is_ocean_near_tile(pcity->x, pcity->y);
  /* Technologies we would like to have. */
  int tech_desire[U_LAST];
  /* Our favourite unit. */
  int best = 0;
  Unit_Type_id best_unit_type = 0; /* zero is settler but not a problem */

  memset(tech_desire, 0, sizeof(tech_desire));
  
  simple_ai_unit_type_iterate (unit_type) {
      int move_type = unit_types[unit_type].move_type;
    
      /* How many technologies away it is? */
      int tech_dist = num_unknown_techs_for_goal(pplayer,
                        unit_types[unit_type].tech_requirement);

      /* How much we want the unit? */
      int desire = ai_unit_defence_desirability(unit_type);

      if (unit_type_flag(unit_type, F_FIELDUNIT)) {
        /* Causes unhappiness even when in defense, so not a good
         * idea for a defender, unless it is _really_ good */
       desire /= 2;
      }      

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
  } simple_ai_unit_type_iterate_end;
  
  if (!walls && unit_types[best_unit_type].move_type == LAND_MOVING) {
    best *= pcity->ai.wallvalue;
    best /= POWER_FACTOR;
  }

  if (best == 0) best = 1; /* Avoid division by zero below. */

  /* Update tech_want for appropriate techs for units we want to build. */
  simple_ai_unit_type_iterate (unit_type) {
    if (tech_desire[unit_type] > 0) {
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
  This function decides, what unit would be best for erasing enemy. It is 
  called, when we just want to kill something, we've found it but we don't 
  have the unit for killing that built yet - here we'll choose the type of 
  that unit.

  We will also set increase the technology want to get units which could 
  perform the job better.

  I decided this funct wasn't confusing enough, so I made 
  kill_something_with send it some more variables for it to meddle with. 
  -- Syela

  (x,y) is location of the target.
  best_choice is pre-filled with our current choice, we only 
  consider units of the same move_type as best_choice
**************************************************************************/
static void process_attacker_want(struct city *pcity,
                                  int value, Unit_Type_id victim_unit_type,
                                  bool veteran, int x, int y,
                                  struct ai_choice *best_choice,
                                  struct unit *boat, Unit_Type_id boattype)
{
  struct player *pplayer = city_owner(pcity);
  /* The enemy city.  acity == NULL means stray enemy unit */
  struct city *acity = map_get_city(x, y);
  bool shore = is_ocean_near_tile(pcity->x, pcity->y);
  int orig_move_type = unit_types[best_choice->choice].move_type;
  int victim_count = 1;
  int needferry = 0;
  bool unhap = ai_assess_military_unhappiness(pcity,
                                              get_gov_pplayer(pplayer));

  assert(orig_move_type == SEA_MOVING || orig_move_type == LAND_MOVING);

  if (orig_move_type == LAND_MOVING && !boat && boattype < U_LAST) {
    /* cost of ferry */
    needferry = unit_types[boattype].build_cost;
  }
  
  if (acity) {
    /* If it is a city, we may have to whack it many times */
    /* FIXME: Also valid for fortresses! */
    victim_count += unit_list_size(&(map_get_tile(x, y)->units));
  }

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
      if (acity) {
        move_time = turns_to_enemy_city(unit_type, acity, move_rate,
                                        (boattype < U_LAST), boat, boattype);
      } else {
        /* Target is in the field */
        move_time = turns_to_enemy_unit(unit_type, move_rate, x, y, 
                                        victim_unit_type);
      }

      /* Estimate strength of the enemy. */
      
      vuln = unit_vulnerability_virtual2(unit_type, victim_unit_type, x, y,
                                         FALSE, veteran, FALSE, 0);

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

      want = military_amortize(pplayer, pcity, desire, MAX(1, move_time),
                               bcost_balanced + needferry);
      
      if (want > 0) {
        if (tech_dist > 0) {
          /* This is a future unit, tell the scientist how much we need it */
          pplayer->ai.tech_want[tech_req] += want;
          
          CITY_LOG(LOG_DEBUG, pcity, "wants %s to build %s to punish %s@(%d,%d)"
                   " with desire %d", advances[tech_req].name, 
                   unit_name(unit_type), (acity ? acity->name : "enemy"),
                   x, y, want);

        } else if (want > best_choice->want) {
          if (can_build_unit(pcity, unit_type)) {
            /* This is a real unit and we really want it */

            CITY_LOG(LOG_DEBUG, pcity, "overriding %s(%d) with %s(%d)"
                     " [attack=%d,value=%d,move_time=%d,vuln=%d,bcost=%d]",
                     unit_name(best_choice->choice), best_choice->want,
                     unit_name(unit_type), want, attack, value, move_time,
                     vuln, bcost);

            best_choice->choice = unit_type;
            best_choice->want = want;
            best_choice->type = CT_ATTACKER;
          } /* FIXME: else find out why we can't build it and do something */
        }
      }
    }
  } simple_ai_unit_type_iterate_end;
}

/************************************************************************** 
This function 
1. receives (in myunit) a first estimate of what we would like to build.
2. finds a potential victim for it.
3. calculates the relevant stats of the victim.
4. finds the best attacker for this type of victim (in process_attacker_want)
5. if we still want to attack, records the best attacker in choice.
**************************************************************************/
static void kill_something_with(struct player *pplayer, struct city *pcity, 
				struct unit *myunit, struct ai_choice *choice)
{
  /* Our attack rating (with reinforcements) */
  int attack;
  /* Benefit from fighting the target */
  int benefit;
  /* Enemy defender type */
  Unit_Type_id def_type;
  /* Target coordinates */
  int x, y;
  /* Our transport */
  struct unit *ferryboat = NULL;
  /* Out target */
  struct city *acity;
  /* Defender of the target city/tile */
  struct unit *pdef; 
  /* Coordinates of the boat */
  int bx = 0, by = 0;
  /* Type of the boat (real or a future one) */
  Unit_Type_id boattype = U_LAST;
  int boatspeed;
  bool go_by_boat;
  /* Is the defender veteran? */
  bool def_vet;
  struct ai_choice best_choice;

  init_choice(&best_choice);
  best_choice.choice = myunit->type;
  best_choice.type = CT_ATTACKER;
  best_choice.want = choice->want;

  assert(is_military_unit(myunit) && !is_air_unit(myunit));

  if (pcity->ai.danger != 0 && assess_defense(pcity) == 0) {
    /* Defence comes first! */
    return;
  }

  if (!is_ground_unit(myunit) && !is_sailing_unit(myunit)) {
    freelog(LOG_ERROR, "ERROR: Attempting to deal with non-trivial"
            " unit_type in kill_something_with");
    return;
  }

  if (is_ground_unit(myunit)) {
    int boatid = find_boat(pplayer, &bx, &by, 2);
    ferryboat = player_find_unit_by_id(pplayer, boatid);
  }

  if (ferryboat) {
    boattype = ferryboat->type;
  } else {
    boattype = best_role_unit_for_player(pplayer, L_FERRYBOAT);
    if (boattype == U_LAST) {
      /* We pretend that we can have the simplest boat -- to stimulate tech */
      boattype = get_role_unit(L_FERRYBOAT, 0);
    }
  }
  boatspeed = unit_types[boattype].move_rate;

  best_choice.want = find_something_to_kill(pplayer, myunit, &x, &y);

  acity = map_get_city(x, y);

  if (myunit->id != 0) {
    freelog(LOG_ERROR, "ERROR: Non-virtual unit in kill_something_with!");
    return;
  }
  
  attack = unit_belligerence_basic(myunit);
  if (acity) {
    attack += acity->ai.attack;
  }
  attack *= attack;
  
  if (acity) {
    /* Our move rate */
    int move_rate = unit_types[myunit->type].move_rate;
    /* Distance to target (in turns) */
    int move_time;
    /* Rating of enemy defender */
    int vuln;

    if (unit_flag(myunit, F_IGTER)) {
      /* See comment in unit_move_turns */
      move_rate *= 3;
    }
    
    if (!pplayers_at_war(pplayer, city_owner(acity))) {
      /* Not a valid target */
      return;
    }

    go_by_boat = !(goto_is_sane(myunit, acity->x, acity->y, TRUE) 
                  && warmap.cost[x][y] <= (MIN(6, move_rate) * THRESHOLD));
    move_time = turns_to_enemy_city(myunit->type, acity, move_rate, go_by_boat,
                                    ferryboat, boattype);

    def_type = ai_choose_defender_versus(acity, myunit->type);
    if (move_time > 1) {
      def_vet = do_make_unit_veteran(acity, def_type);
      vuln = unit_vulnerability_virtual2(myunit->type, def_type, x, y, FALSE,
                                         def_vet, FALSE, 0);
      benefit = unit_types[def_type].build_cost;
    } else {
      vuln = 0;
      benefit = 0;
      def_vet = FALSE;
    }

    pdef = get_defender(myunit, x, y);
    if (pdef) {
      int m = unit_vulnerability_virtual2(myunit->type, pdef->type, x, y, FALSE,
                                          pdef->veteran, FALSE, 0);
      if (vuln < m) {
        vuln = m;
        benefit = unit_type(pdef)->build_cost;
        def_vet = pdef->veteran;
        def_type = pdef->type; 
      }
    }
    if (COULD_OCCUPY(myunit) || TEST_BIT(acity->ai.invasion, 0)) {
      /* bonus for getting the city */
      benefit += 40;
    }

    /* end dealing with cities */
  } else {

    pdef = get_defender(myunit, x, y);
    if (!pdef) {
      /* Nobody to attack! */
      return;
    }

    benefit = unit_type(pdef)->build_cost;
    go_by_boat = FALSE;

    def_type = pdef->type;
    def_vet = pdef->veteran;
    /* end dealing with units */
  }
  
  if (!go_by_boat) {
    process_attacker_want(pcity, benefit, def_type, def_vet, x, y, 
                          &best_choice, NULL, U_LAST);
  } else { 
    process_attacker_want(pcity, benefit, def_type, def_vet, x, y, 
                          &best_choice, ferryboat, boattype);
  }

  if (best_choice.want > choice->want) {
    /* We want attacker more that what we have selected before */
    copy_if_better_choice(&best_choice, choice);
    if (go_by_boat && !ferryboat) {
      ai_choose_ferryboat(pplayer, pcity, choice);
    }
    freelog(LOG_DEBUG, "%s has chosen attacker, %s, want=%d",
            pcity->name, unit_types[choice->choice].name, choice->want);
  } 
}

/**********************************************************************
... this function should assign a value to choice and want and type, 
    where want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
***********************************************************************/
static void ai_unit_consider_bodyguard(struct city *pcity,
                                       Unit_Type_id unit_type,
                                       struct ai_choice *choice)
{
  struct unit *virtualunit;
  struct player *pplayer = city_owner(pcity);
  struct unit *aunit = NULL;
  struct city *acity = NULL;

  virtualunit = create_unit_virtual(pplayer, pcity, unit_type,
                                    do_make_unit_veteran(pcity, unit_type));

  if (choice->want < 100) {
    int want = look_for_charge(pplayer, virtualunit, &aunit, &acity);
    if (want > choice->want) {
      choice->want = want;
      choice->choice = unit_type;
      choice->type = CT_DEFENDER;
    }
  }
  destroy_unit_virtual(virtualunit);
}

/*********************************************************************
  Before building a military unit, AI builds a barracks/port/airport
  NB: It is assumed this function isn't called in an emergency 
  situation, when we need a defender _now_.
 
  TODO: something more sophisticated, like estimating future demand
  for military units, considering Sun Tzu instead.
*********************************************************************/
static void adjust_ai_unit_choice(struct city *pcity, 
                                  struct ai_choice *choice)
{
  enum unit_move_type move_type;
  struct player *pplayer = city_owner(pcity);

  /* Sanity */
  if (!is_unit_choice_type(choice->choice)) return;
  if (unit_type_flag(choice->choice, F_NONMIL)) return;
  if (do_make_unit_veteran(pcity, choice->choice)) return;

  move_type = get_unit_type(choice->choice)->move_type;
  if (improvement_variant(B_BARRACKS)==1) {
    /* Barracks will work for all units! */
    move_type = LAND_MOVING;
  }

  switch(move_type) {
  case LAND_MOVING:
    if (player_knows_improvement_tech(pplayer, B_BARRACKS3)) {
      choice->choice = B_BARRACKS3;
      choice->type = CT_BUILDING;
    } else if (player_knows_improvement_tech(pplayer, B_BARRACKS2)) {
      choice->choice = B_BARRACKS2;
      choice->type = CT_BUILDING;
    } else if (player_knows_improvement_tech(pplayer, B_BARRACKS)) {
      choice->choice = B_BARRACKS;
      choice->type = CT_BUILDING;
    }
    break;
  case SEA_MOVING:
    if (player_knows_improvement_tech(pplayer, B_PORT)) {
      choice->choice = B_PORT;
      choice->type = CT_BUILDING;
    }
    break;
  case HELI_MOVING:
  case AIR_MOVING:
    if (player_knows_improvement_tech(pplayer, B_AIRPORT)
        && pcity->shield_surplus > improvement_value(B_AIRPORT) / 10) {
      /* Only build this if we have really high production */
      choice->choice = B_AIRPORT;
      choice->type = CT_BUILDING;
    }
    break;
  default:
    freelog(LOG_ERROR, "Unknown move_type in adjust_ai_unit_choice");
    assert(FALSE);
  }
}

/********************************************************************** 
... this function should assign a value to choice and want and type, 
    where want is a value between 1 and 100.
    if want is 0 this advisor doesn't want anything
***********************************************************************/
void military_advisor_choose_build(struct player *pplayer, struct city *pcity,
				   struct ai_choice *choice)
{
  Unit_Type_id unit_type;
  int def, danger, urgency;
  struct tile *ptile = map_get_tile(pcity->x, pcity->y);
  struct unit *virtualunit;

  init_choice(choice);

  /* Note: assess_danger() creates a warmap for us */
  urgency = assess_danger(pcity); /* calling it now, rewriting old wall code */
  freelog(LOG_DEBUG, "%s: danger %d, grave_danger %d",
          pcity->name, pcity->ai.danger, pcity->ai.grave_danger);
  def = assess_defense_quadratic(pcity); /* has to be AFTER assess_danger thanks to wallvalue */
/* changing to quadratic to stop AI from building piles of small units -- Syela */
  danger = pcity->ai.danger; /* we now have our warmap and will use it! */
  freelog(LOG_DEBUG, "Assessed danger for %s = %d, Def = %d",
          pcity->name, danger, def);

  ai_choose_diplomat_defensive(pplayer, pcity, choice, def);


  if (danger != 0) { /* otherwise might be able to wait a little longer to defend */
    if (danger >= def) {
      if (urgency == 0) danger = 100; /* don't waste money otherwise */
      else if (danger >= def * 2) danger = 200 + urgency;
      else { danger *= 100; danger /= def; danger += urgency; }
    } else { danger *= 100; danger /= def; }
    if (pcity->shield_surplus <= 0 && def != 0) danger = 0;
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
      pcity->ppl_unhappy[4] > pcity->ppl_unhappy[2]) return; /* and no disorder */

  /* Consider making a land bodyguard */
  unit_type = ai_choose_bodyguard(pcity, LAND_MOVING, L_DEFEND_GOOD);
  if (unit_type >= 0) {
    ai_unit_consider_bodyguard(pcity, unit_type, choice);
  }

  /* If we are in severe danger, don't consider attackers. This is probably
     too general. In many cases we will want to buy attackers to counterattack.
     -- Per */
  if (choice->want > 100 && pcity->ai.grave_danger > 0) {
    CITY_LOG(LOGLEVEL_BUILD, pcity, "severe danger (want %d), force defender",
             choice->want);
    return;
  }

  /* Consider making an offensive diplomat */
  ai_choose_diplomat_offensive(pplayer, pcity, choice);

  /* Consider making a sea bodyguard */
  unit_type = ai_choose_bodyguard(pcity, SEA_MOVING, L_DEFEND_GOOD);
  if (unit_type >= 0) {
    ai_unit_consider_bodyguard(pcity, unit_type, choice);
  }

  /* Consider making an airplane */
  (void) ai_choose_attacker_air(pplayer, pcity, choice);

  /* Check if we want a sailing attacker. Have to put sailing first
     before we mung the seamap */
  unit_type = ai_choose_attacker(pcity, SEA_MOVING);
  if (unit_type >= 0) {
    virtualunit = create_unit_virtual(pplayer, pcity, unit_type,
                              player_knows_improvement_tech(pplayer, B_PORT));
    kill_something_with(pplayer, pcity, virtualunit, choice);
    destroy_unit_virtual(virtualunit);
  }

  /* Consider a land attacker */
  unit_type = ai_choose_attacker(pcity, LAND_MOVING);
  if (unit_type >= 0) {
    virtualunit = create_unit_virtual(pplayer, pcity, unit_type, TRUE);
    kill_something_with(pplayer, pcity, virtualunit, choice);
    destroy_unit_virtual(virtualunit);
  }

  /* Consider veteran level enhancing buildings before non-urgent units */
  adjust_ai_unit_choice(pcity, choice);

  if (choice->want <= 0) {
    CITY_LOG(LOGLEVEL_BUILD, pcity, "military advisor has no advice");
  } else if (is_unit_choice_type(choice->type)) {
    CITY_LOG(LOGLEVEL_BUILD, pcity, "military advisor choice: %s (want %d)",
             unit_types[choice->choice].name, choice->want);
  } else {
    CITY_LOG(LOGLEVEL_BUILD, pcity, "military advisor choice: %s (want %d)",
             improvement_types[choice->choice].name, choice->want);
  }
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
    wonder_continent = map_get_continent(pcity->x, pcity->y, NULL);
  } else {
    wonder_continent = 0;
  }

  pcity->ai.downtown = 0;
  city_list_iterate(pplayer->cities, othercity) {
    distance = warmap.cost[othercity->x][othercity->y];
    if (wonder_continent != 0
        && map_get_continent(othercity->x, othercity->y, NULL) 
             == wonder_continent) {
      othercity->ai.distance_to_wonder_city = distance;
    }

    /* How many people near enough would help us? */
    distance += moverate - 1; distance /= moverate;
    pcity->ai.downtown += MAX(0, 5 - distance);
  } city_list_iterate_end;
}
