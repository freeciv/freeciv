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
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "log.h"
#include "map.h"
#include "unit.h"

#include "combat.h"

/***********************************************************************
Returns the chance of the attacker winning, a number between 0 and 1.
If you want the chance that the defender wins just use 1-chance(...)

NOTE: this number can be _very_ small, fx in a battle between an
ironclad and a battleship the ironclad has less than 1/100000 chance of
winning.

The algoritm calculates the probability of each possible number of HP's
the attacker has left. Maybe that info should be preserved for use in
the AI.
***********************************************************************/
double win_chance(int as, int ahp, int afp, int ds, int dhp, int dfp)
{
  /* number of rounds a unit can fight without dying */
  int att_N_lose = (ahp + dfp - 1) / dfp;
  int def_N_lose = (dhp + afp - 1) / afp;
  /* Probability of losing one round */
  double att_P_lose1 = (as + ds == 0) ? 0.5 : (double) ds / (as + ds);
  double def_P_lose1 = 1 - att_P_lose1;

  /*
    This calculates 

    binomial_coeff(def_N_lose-1 + lr, lr)
      * def_P_lose1^(def_N_lose-1)
      * att_P_lose1^(lr)
      * def_P_lose1

    for each possible number of rounds lost (rl) by the winning unit.
    rl is of course less than the number of rounds the winning unit
    should lose to lose all it's hit points.
    The probabilities are then summed.

    To see this is correct consider the set of series for all valid fights.
    These series are the type (win, lose, lose...). The possible lenghts are
    def_N_lose to def_N_lose+att_N_lose-1. A series is not valid unless it
    contains def_N_lose wins, and one of the wins must be the last one, or
    the series would be equivalent the a shorter series (the attacker would
    have won one or more fights previously).
    So since the last fight is a win we disregard it while calculating. Now
    a series contains def_N_lose-1 wins. So for each possible lenght of a
    series we find the probability of every valid series and then sum.
    For a specific lenght (a "lr") every series have the probability
      def_P_lose1^(def_N_lose-1) * att_P_lose1^(lr)
    and then getting from that to the real series requires a win, ie factor
    def_N_lose. The number of series with lenght (def_N_lose-1 + lr) and
    "lr" lost fights is
      binomial_coeff(def_N_lose-1 + lr, lr)
    And by multiplying we get the formula on the top of this code block.
    Adding the cumulative probability for each valid lenght then gives the
    total probability.

    We clearly have all valid series this way. To see that we have counted
    none twice note that would require a series with a smaller series inbedded.
    But since the smaller series already included def_N_lose wins, and the
    larger series ends with a win, it would have too many wins and therefore
    cannot exist.

    In practice each binomial coefficient for a series lenght can be calculated
    from the previous. In the coefficient (n, k) n is increased and k is
    unchanged.
    The "* def_P_lose1" is multiplied on the sum afterwards.

    (lots of talk for so little code)
  */

  double binom_save = pow(def_P_lose1, def_N_lose - 1);
  double accum_prob = binom_save; /* lr = 0 */

  int lr; /* the number of Lost Rounds by the attacker */
  for (lr = 1; lr < att_N_lose; lr++) {
    /* update the coefficient */
    int n = lr + def_N_lose - 1;
    binom_save *= n;
    binom_save /= lr;
    binom_save *= att_P_lose1;
    /* use it for this lr */
    accum_prob += binom_save;
  }
  /* Every element of the sum needs a factor for the very last fight round */
  accum_prob *= def_P_lose1;

  return accum_prob;
}

/**************************************************************************
A unit's effective firepower depend on the situation.
**************************************************************************/
void get_modified_firepower(struct unit *attacker, struct unit *defender,
			    int *att_fp, int *def_fp)
{
  *att_fp = unit_type(attacker)->firepower;
  *def_fp = unit_type(defender)->firepower;

  /* pearl harbour */
  if (is_sailing_unit(defender) && map_get_city(defender->x, defender->y))
    *def_fp = 1;

  /* In land bombardment both units have their firepower reduced to 1 */
  if (is_sailing_unit(attacker)
      && map_get_terrain(defender->x, defender->y) != T_OCEAN
      && is_ground_unit(defender)) {
    *att_fp = 1;
    *def_fp = 1;
  }
}

/**************************************************************************
Returns a double in the range [0;1] indicating the attackers chance of
winning. The calculation takes all factors into account.
**************************************************************************/
double unit_win_chance(struct unit *attacker, struct unit *defender)
{
  int def_power = get_total_defense_power(attacker, defender);
  int att_power = get_total_attack_power(attacker, defender);

  double chance;

  int def_fp, att_fp;
  get_modified_firepower(attacker, defender, &att_fp, &def_fp);

  chance = win_chance(att_power, attacker->hp, att_fp,
		      def_power, defender->hp, def_fp);

  return chance;
}

/**************************************************************************
  a wrapper that returns whether or not a unit ignores citywalls
**************************************************************************/
int unit_ignores_citywalls(struct unit *punit)
{
  return (unit_flag(punit->type, F_IGWALL));
}

/**************************************************************************
  Takes into account unit move_type as well, and Walls variant.
**************************************************************************/
int unit_really_ignores_citywalls(struct unit *punit)
{
  return unit_ignores_citywalls(punit)
    || is_air_unit(punit)
    || (is_sailing_unit(punit) && !(improvement_variant(B_CITY)==1));
}

/**************************************************************************
 a wrapper function that returns whether or not the unit is on a citysquare
 with citywalls
**************************************************************************/
int unit_behind_walls(struct unit *punit)
{
  struct city *pcity;
  
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_citywalls(pcity);
  
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with fortress
**************************************************************************/
int unit_on_fortress(struct unit *punit)
{
  return (map_get_special(punit->x, punit->y)&S_FORTRESS);
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with coastal defense
**************************************************************************/
int unit_behind_coastal(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_COASTAL);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sam site
**************************************************************************/
int unit_behind_sam(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SAM);
  return 0;
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with sdi defense
**************************************************************************/
int unit_behind_sdi(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y)))
    return city_got_building(pcity, B_SDI);
  return 0;
}

/**************************************************************************
  a wrapper function returns 1 if there is a sdi-defense close to the square
**************************************************************************/
struct city *sdi_defense_close(struct player *owner, int x, int y)
{
  square_iterate(x, y, 2, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && (city_owner(pcity) != owner)
	&& city_got_building(pcity, B_SDI)) return pcity;
  } square_iterate_end;

  return NULL;
}

/**************************************************************************
 returns the attack power, modified by moves left, and veteran status.
**************************************************************************/
int get_attack_power(struct unit *punit)
{
  int power;
  power=unit_type(punit)->attack_strength*10;
  if (punit->veteran) {
    power *= 3;
    power /= 2;
  }
  if (unit_flag(punit->type, F_IGTIRED)) return power;
  if ( punit->moves_left < SINGLE_MOVE )
     return (power*punit->moves_left)/SINGLE_MOVE;
    return power;
}

/**************************************************************************
  returns the defense power, modified by terrain and veteran status
**************************************************************************/
int get_defense_power(struct unit *punit)
{
  int power;
  int terra;
  int db;

  if (!punit || punit->type<0 || punit->type>=U_LAST
      || punit->type>=game.num_unit_types)
    abort();
  power=unit_type(punit)->defense_strength*10;
  if (punit->veteran) {
    power *= 3;
    power /= 2;
  }
  terra=map_get_terrain(punit->x, punit->y);
  db = get_tile_type(terra)->defense_bonus;
  if (map_get_special(punit->x, punit->y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  power=(power*db)/10;

  return power;
}

/***************************************************************************
 return the modified attack power of a unit.  Currently they aren't any
 modifications...
***************************************************************************/
int get_total_attack_power(struct unit *attacker, struct unit *defender)
{
  int attackpower = get_attack_power(attacker);

  return attackpower;
}

/***************************************************************************
 Like get_virtual_defense_power, but don't include most of the modifications.
 (For calls which used to be g_v_d_p(U_HOWITZER,...))
 Specifically, include:
 unit def, terrain effect, fortress effect, ground unit in city effect
***************************************************************************/
int get_simple_defense_power(Unit_Type_id d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

/**************************************************************************
...
**************************************************************************/
int get_virtual_defense_power(Unit_Type_id a_type, Unit_Type_id d_type, int x, int y)
{
  int defensepower=unit_types[d_type].defense_strength;
  int m_type = unit_types[a_type].move_type;
  struct city *pcity = map_get_city(x, y);
  enum tile_terrain_type t = map_get_terrain(x, y);
  int db;

  if (unit_types[d_type].move_type == LAND_MOVING && t == T_OCEAN) return 0;
/* I had this dorky bug where transports with mech inf aboard would go next
to enemy ships thinking the mech inf would defend them adequately. -- Syela */

  db = get_tile_type(t)->defense_bonus;
  if (map_get_special(x, y) & S_RIVER)
    db += (db * terrain_control.river_defense_bonus) / 100;
  defensepower *= db;

  if (unit_flag(d_type, F_PIKEMEN) && unit_flag(a_type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(d_type, F_AEGIS) &&
       (m_type == AIR_MOVING || m_type == HELI_MOVING)) defensepower*=5;
  if (m_type == AIR_MOVING && pcity) {
    if (city_got_building(pcity, B_SAM))
      defensepower*=2;
    if (city_got_building(pcity, B_SDI) && unit_flag(a_type, F_MISSILE))
      defensepower*=2;
  } else if (m_type == SEA_MOVING && pcity) {
    if (city_got_building(pcity, B_COASTAL))
      defensepower*=2;
  }
  if (!unit_flag(a_type, F_IGWALL)
      && (m_type == LAND_MOVING || m_type == HELI_MOVING
	  || (improvement_variant(B_CITY)==1 && m_type == SEA_MOVING))
      && pcity && city_got_citywalls(pcity)) {
    defensepower*=3;
  }
  if (map_get_special(x, y)&S_FORTRESS && !pcity)
    defensepower+=(defensepower*terrain_control.fortress_defense_bonus)/100;
  if (pcity && unit_types[d_type].move_type == LAND_MOVING)
    defensepower*=1.5;

  return defensepower;
}

/***************************************************************************
 return the modified defense power of a unit.
 An veteran aegis cruiser in a mountain city with SAM and SDI defense 
 being attacked by a missile gets defense 288.
***************************************************************************/
int get_total_defense_power(struct unit *attacker, struct unit *defender)
{
  int defensepower=get_defense_power(defender);
  if (unit_flag(defender->type, F_PIKEMEN) && unit_flag(attacker->type, F_HORSE)) 
    defensepower*=2;
  if (unit_flag(defender->type, F_AEGIS) && (is_air_unit(attacker) || is_heli_unit(attacker)))
    defensepower*=5;
  if (is_air_unit(attacker)) {
    if (unit_behind_sam(defender))
      defensepower*=2;
    if (unit_behind_sdi(defender) && unit_flag(attacker->type, F_MISSILE))
      defensepower*=2;
  } else if (is_sailing_unit(attacker)) {
    if (unit_behind_coastal(defender))
      defensepower*=2;
  }
  if (!unit_really_ignores_citywalls(attacker)
      && unit_behind_walls(defender)) 
    defensepower*=3;
  if (unit_on_fortress(defender) && 
      !map_get_city(defender->x, defender->y)) 
    defensepower*=2;
  if ((defender->activity == ACTIVITY_FORTIFIED || 
       map_get_city(defender->x, defender->y)) && 
      is_ground_unit(defender))
    defensepower*=1.5;

  return defensepower;
}

/**************************************************************************
A number indicating the defense strength.
Unlike the one got from win chance this doesn't potentially get insanely
small if the units are unevenly matched, unlike win_chance.
**************************************************************************/
static int get_defense_rating(struct unit *attacker, struct unit *defender)
{
  int afp, dfp;

  int rating = get_total_defense_power(attacker, defender);
  get_modified_firepower(attacker, defender, &afp, &dfp);

  /* How many rounds the defender will last */
  rating *= (defender->hp + afp-1)/afp;

  rating *= dfp;

  return rating;
}

/**************************************************************************
Finds the best defender on the square, given an attacker.

This is simply done by calling win_chance with all the possible defenders
in turn.
This functions could be improved to take the value of the unit into
account. It currently uses build cost as a modifier in case the chances of
2 units are identical, but this is crude as build cost does not neccesarily
have anything to do with the value of a unit.
It would be nice if the function was a bit more fuzzy about prioritizing,
making it able to fx choose a 1a/9d unit over a 10a/10d unit. It should
also be able to spare units without full hp's to some extend, as these
could be more valuable later.
**************************************************************************/
struct unit *get_defender(struct unit *attacker, int x, int y)
{
  struct unit *bestdef = NULL;
  int bestvalue = -1, count = 0, best_cost = 0, rating_of_best = 0;

  unit_list_iterate(map_get_tile(x, y)->units, defender) {
    if (pplayers_allied(unit_owner(attacker), unit_owner(defender)))
      continue;
    count++;
    if (unit_can_defend_here(defender)) {
      int change = 0;
      int build_cost = unit_type(defender)->build_cost;

      /* This will make units roughly evenly good defenders look alike. */
      int unit_def = (int) (100000 * (1 - unit_win_chance(attacker, defender)));
      assert(unit_def >= 0);

      if (unit_def > bestvalue) {
	change = 1;
      } else if (unit_def == bestvalue) {
	if (build_cost < best_cost) {
	  change = 1;
	} else if (build_cost == best_cost) {
	  if (rating_of_best < get_defense_rating(attacker, defender)) {	
	    change = 1;
	  }
	}
      }

      if (change) {
	bestvalue = unit_def;
	bestdef = defender;
	best_cost = build_cost;
	rating_of_best = get_defense_rating(attacker, bestdef);
      }
    }
  } unit_list_iterate_end;

  if (count && !bestdef) {
    struct unit *debug_unit = unit_list_get(&map_get_tile(x, y)->units, 0);
    freelog(LOG_ERROR, "Get_def bugged at (%d,%d). The most likely course"
	    " is a unit on an ocean square without a transport. The owner"
	    " of the unit is %s", x, y, unit_owner(debug_unit)->name);
  }

  return bestdef;
}

/**************************************************************************
get unit at (x, y) that wants to kill defender.

Works like get_defender; see comment there.
This function is mostly used by the AI.
**************************************************************************/
struct unit *get_attacker(struct unit *defender, int x, int y)
{
  struct unit *bestatt = 0;
  int bestvalue = -1, unit_a, best_cost = 0;

  unit_list_iterate(map_get_tile(x, y)->units, attacker) {
    int build_cost = unit_type(attacker)->build_cost;

    if (pplayers_allied(unit_owner(defender), unit_owner(attacker)))
      return 0;
    unit_a = (int) (100000 * (unit_win_chance(attacker, defender)));
    if (unit_a > bestvalue ||
	(unit_a == bestvalue && build_cost < best_cost)) {
      bestvalue = unit_a;
      bestatt = attacker;
      best_cost = build_cost;
    }
  } unit_list_iterate_end;

  return bestatt;
}
