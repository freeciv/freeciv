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

#include <assert.h>
#include <math.h>

#include "log.h"
#include "map.h"
#include "packets.h"
#include "unit.h"

#include "combat.h"

/***********************************************************************
  Checks if player is restricted diplomatically from attacking the tile.
  Returns FLASE if
  1) the tile is empty or
  2) the tile contains a non-enemy city or
  3) the tile contains a non-enemy unit
***********************************************************************/
bool can_player_attack_tile(struct player *pplayer, const struct tile *ptile)
{
  struct city *pcity = ptile->city;
  
  /* 1. Is there anyone there at all? */
  if (!pcity && unit_list_size(&(ptile->units)) == 0) {
    return FALSE;
  }

  /* 2. If there is a city there, can we attack it? */
  if (pcity && !pplayers_at_war(city_owner(pcity), pplayer)) {
    return FALSE;
  }

  /* 3. Are we allowed to attack _all_ units there? */
  unit_list_iterate(ptile->units, aunit) {
    if (!pplayers_at_war(unit_owner(aunit), pplayer)) {
      /* Enemy hiding behind a human/diplomatic shield */
      return FALSE;
    }
  } unit_list_iterate_end;

  return TRUE;
}

/***********************************************************************
  Checks if a unit can physically attack pdefender at the tile
  (assuming it is adjacent and at war).

  Unit can NOT attack if:
  1) it does not have any attack power.
  2) it is not a fighter and defender is a flying unit (except city/airbase).
  3) it is a ground unit without marine ability and it attacks from ocean.
  4) it is a ground unit and it attacks a target on an ocean square.
  5) it is a sailing unit without shore bombardment capability and it
     attempts to attack land.

  Does NOT check:
  1) Moves left
  2) Adjacency
  3) Diplomatic status
***********************************************************************/
bool can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender,
                                  const struct tile *dest_tile)
{
  Terrain_type_id fromtile = punit->tile->terrain;
  Terrain_type_id totile = dest_tile->terrain;
  struct city *pcity = dest_tile->city;

  /* 1. Can we attack _anything_ ? */
  if (!is_military_unit(punit) || !is_attack_unit(punit)) {
    return FALSE;
  }

  /* 2. Only fighters can attack planes, except in city or airbase attacks */
  if (!unit_flag(punit, F_FIGHTER) && is_air_unit(pdefender)
      && !(pcity || map_has_special(dest_tile, S_AIRBASE))) {
    return FALSE;
  }

  /* 3. Can't attack with ground unit from ocean, except for marines */
  if (is_ocean(fromtile)
      && is_ground_unit(punit)
      && !unit_flag(punit, F_MARINES)) {
    return FALSE;
  }

  /* 4. Ground units cannot attack water units */
  if (is_ocean(totile) && is_ground_unit(punit)) {
    return FALSE;
  }

  /* 5. Shore bombardement can be done by certain units only */
  if (unit_flag(punit, F_NO_LAND_ATTACK) && !is_ocean(totile)) {
    return FALSE;
  }

  return TRUE;
}

/***********************************************************************
  To attack a stack, unit must be able to attack every unit there (not
  including transported units).
************************************************************************/
bool can_unit_attack_all_at_tile(struct unit *punit,
				 const struct tile *ptile)
{
  unit_list_iterate(ptile->units, aunit) {
    /* HACK: we don't count transported units here.  This prevents some
     * bugs like a submarine carrying a cruise missile being invulnerable
     * to other sea units.  However from a gameplay perspective it's a hack,
     * since players can load and unload their units manually to protect
     * their transporters. */
    if (aunit->transported_by == -1
	&& !can_unit_attack_unit_at_tile(punit, aunit, ptile)) {
      return FALSE;
    }
  } unit_list_iterate_end;

  return TRUE;
}

/***********************************************************************
  Is unit (1) diplomatically allowed to attack and (2) physically able
  to do so?
***********************************************************************/
bool can_unit_attack_tile(struct unit *punit, const struct tile *dest_tile)
{
  if (!can_player_attack_tile(unit_owner(punit), dest_tile)) {
    return FALSE;
  }

  return can_unit_attack_all_at_tile(punit, dest_tile);
}

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

  double binom_save = pow(def_P_lose1, (double)(def_N_lose - 1));
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

  /* Check CityBuster flag */
  if (unit_flag(attacker, F_CITYBUSTER)
      && map_get_city(defender->tile)) {
    *att_fp *= 2;
  }

  /* pearl harbour - defender's firepower is reduced to one, 
   *                 attacker's is multiplied by two         */
  if (is_sailing_unit(defender) && map_get_city(defender->tile)) {
    *att_fp *= 2;
    *def_fp = 1;
  }
  
  /* 
   * When attacked by fighters, helicopters have their firepower
   * reduced to 1.
   */
  if (is_heli_unit(defender) && unit_flag(attacker, F_FIGHTER)) {
    *def_fp = 1;
  }

  /* In land bombardment both units have their firepower reduced to 1 */
  if (is_sailing_unit(attacker)
      && !is_ocean(map_get_terrain(defender->tile))
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
static bool unit_ignores_citywalls(struct unit *punit)
{
  return (unit_flag(punit, F_IGWALL));
}

/**************************************************************************
  Takes into account unit move_type as well, and Walls variant.
**************************************************************************/
bool unit_really_ignores_citywalls(struct unit *punit)
{
  return (unit_ignores_citywalls(punit)
	  || is_air_unit(punit)
	  || is_sailing_unit(punit));
}

/**************************************************************************
 a wrapper function returns 1 if the unit is on a square with fortress
**************************************************************************/
bool unit_on_fortress(struct unit *punit)
{
  return map_has_special(punit->tile, S_FORTRESS);
}

/**************************************************************************
  a wrapper function returns 1 if there is a sdi-defense close to the square
**************************************************************************/
struct city *sdi_defense_close(struct player *owner,
			       const struct tile *ptile)
{
  square_iterate(ptile, 2, ptile1) {
    struct city *pcity = map_get_city(ptile1);
    if (pcity && (!pplayers_allied(city_owner(pcity), owner))
	&& get_city_bonus(pcity, EFT_NUKE_PROOF) > 0) {
      return pcity;
    }
  } square_iterate_end;

  return NULL;
}

/**************************************************************************
 Convenience wrapper for base_get_attack_power.
**************************************************************************/
int get_attack_power(struct unit *punit)
{
  return base_get_attack_power(punit->type, punit->veteran,
			       punit->moves_left);
}

/**************************************************************************
 Returns the attack power, modified by moves left, and veteran
 status. Set moves_left to SINGLE_MOVE to disable the reduction of
 power caused by tired units.
**************************************************************************/
int base_get_attack_power(Unit_Type_id type, int veteran, int moves_left)
{
  int power;

  power = get_unit_type(type)->attack_strength * POWER_FACTOR;
  power *= get_unit_type(type)->veteran[veteran].power_fact;

  if (!unit_type_flag(type, F_IGTIRED) && moves_left < SINGLE_MOVE) {
    power = (power * moves_left) / SINGLE_MOVE;
  }
  return power;
}

/**************************************************************************
  Returns the defense power, modified by veteran status.
**************************************************************************/
int base_get_defense_power(struct unit *punit)
{
  return unit_type(punit)->defense_strength * POWER_FACTOR
  	* unit_type(punit)->veteran[punit->veteran].power_fact;
}

/**************************************************************************
  Returns the defense power, modified by terrain and veteran status.
**************************************************************************/
int get_defense_power(struct unit *punit)
{
  int db, power = base_get_defense_power(punit);

  db = get_tile_type(punit->tile->terrain)->defense_bonus;
  if (map_has_special(punit->tile, S_RIVER)) {
    db += (db * terrain_control.river_defense_bonus) / 100;
  }
  power = (power * db) / 10;

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

/**************************************************************************
 Return an increased defensepower. Effects which increase the
 defensepower are:
  - unit type effects (horse vs pikemen for example)
  - defender in a fortress
  - fortified defender

May be called with a non-existing att_type to avoid any unit type
effects.
**************************************************************************/
static int defense_multiplication(Unit_Type_id att_type,
				  Unit_Type_id def_type,
				  const struct tile *ptile,
				  int defensepower, bool fortified)
{
  struct city *pcity = map_get_city(ptile);
  int mod;

  if (unit_type_exists(att_type)) {
    if (unit_type_flag(def_type, F_PIKEMEN)
	&& unit_type_flag(att_type, F_HORSE)) {
      defensepower *= 2;
    }

    if (unit_type_flag(def_type, F_AEGIS) &&
	(is_air_unittype(att_type) || is_heli_unittype(att_type))) {
      defensepower *= 5;
    }
         
    if (is_air_unittype(att_type) && pcity) {
      if ((mod = get_city_bonus(pcity, EFT_AIR_DEFEND)) > 0) {
	defensepower = defensepower * (100 + mod) / 100;
      }
      if ((mod = get_city_bonus(pcity, EFT_MISSILE_DEFEND)) > 0
	  && unit_type_flag(att_type, F_MISSILE)) {
	defensepower = defensepower * (100 + mod) / 100;
      }
    } else if (is_water_unit(att_type) && pcity) {
      if ((mod = get_city_bonus(pcity, EFT_SEA_DEFEND)) > 0) {
	defensepower = defensepower * (100 + mod) / 100;
      }
    }
    if (!unit_type_flag(att_type, F_IGWALL)
	&& (is_ground_unittype(att_type) || is_heli_unittype(att_type))
        && pcity
        && (mod = get_city_bonus(pcity, EFT_LAND_DEFEND)) > 0) {
      defensepower = defensepower * (100 + mod) / 100;
    }

    if (unit_type_flag(att_type, F_FIGHTER) && is_heli_unittype(def_type)) {
      defensepower /= 2;
    }
  }

  if (map_has_special(ptile, S_FORTRESS) && !pcity) {
    defensepower +=
	(defensepower * terrain_control.fortress_defense_bonus) / 100;
  }

  if ((pcity || fortified) && is_ground_unittype(def_type)) {
    defensepower = (defensepower * 3) / 2;
  }

  return defensepower;
}

/**************************************************************************
 May be called with a non-existing att_type to avoid any effects which
 depend on the attacker.
**************************************************************************/
int get_virtual_defense_power(Unit_Type_id att_type, Unit_Type_id def_type,
			      const struct tile *ptile,
			      bool fortified, int veteran)
{
  int defensepower = unit_types[def_type].defense_strength;
  Terrain_type_id t = map_get_terrain(ptile);
  int db;

  if (unit_types[def_type].move_type == LAND_MOVING && is_ocean(t)) {
    /* Ground units on ship doesn't defend. */
    return 0;
  }

  db = get_tile_type(t)->defense_bonus;
  if (map_has_special(ptile, S_RIVER)) {
    db += (db * terrain_control.river_defense_bonus) / 100;
  }
  defensepower *= db;
  defensepower *= get_unit_type(def_type)->veteran[veteran].power_fact;

  return defense_multiplication(att_type, def_type, ptile, defensepower,
				fortified);
}

/***************************************************************************
 return the modified defense power of a unit.
 An veteran aegis cruiser in a mountain city with SAM and SDI defense 
 being attacked by a missile gets defense 288.
***************************************************************************/
int get_total_defense_power(struct unit *attacker, struct unit *defender)
{
  return defense_multiplication(attacker->type, defender->type,
				defender->tile,
				get_defense_power(defender),
				defender->activity == ACTIVITY_FORTIFIED);
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
  Finds the best defender on the tile, given an attacker.  The diplomatic
  relationship of attacker and defender is ignored; the caller should check
  this.
**************************************************************************/
struct unit *get_defender(struct unit *attacker, const struct tile *ptile)
{
  struct unit *bestdef = NULL;
  int bestvalue = -1, best_cost = 0, rating_of_best = 0;

  /* Simply call win_chance with all the possible defenders in turn, and
   * take the best one.  It currently uses build cost as a tiebreaker in
   * case 2 units are identical, but this is crude as build cost does not
   * neccesarily have anything to do with the value of a unit.  This function
   * could be improved to take the value of the unit into account.  It would
   * also be nice if the function was a bit more fuzzy about prioritizing,
   * making it able to fx choose a 1a/9d unit over a 10a/10d unit. It should
   * also be able to spare units without full hp's to some extent, as these
   * could be more valuable later. */
  unit_list_iterate(ptile->units, defender) {
    /* We used to skip over allied units, but the logic for that is
     * complicated and is now handled elsewhere. */
    if (unit_can_defend_here(defender)) {
      bool change = FALSE;
      int build_cost = unit_build_shield_cost(defender->type);
      int defense_rating = get_defense_rating(attacker, defender);
      /* This will make units roughly evenly good defenders look alike. */
      int unit_def 
        = (int) (100000 * (1 - unit_win_chance(attacker, defender)));

      assert(unit_def >= 0);

      if (unit_def > bestvalue) {
	change = TRUE;
      } else if (unit_def == bestvalue) {
	if (build_cost < best_cost) {
	  change = TRUE;
	} else if (build_cost == best_cost) {
	  if (rating_of_best < defense_rating) {	
	    change = TRUE;
	  }
	}
      }

      if (change) {
	bestvalue = unit_def;
	bestdef = defender;
	best_cost = build_cost;
	rating_of_best = defense_rating;
      }
    }
  } unit_list_iterate_end;

  if (unit_list_size(&ptile->units) > 0 && !bestdef) {
    struct unit *punit = unit_list_get(&ptile->units, 0);

    freelog(LOG_ERROR, "get_defender bug: %s's %s vs %s's %s (total %d"
            " units) on %s at (%d,%d). ", unit_owner(attacker)->name,
            unit_type(attacker)->name, unit_owner(punit)->name,
            unit_type(punit)->name, unit_list_size(&ptile->units), 
            get_terrain_name(ptile->terrain), ptile->x, ptile->y);
  }

  return bestdef;
}

/**************************************************************************
get unit at (x, y) that wants to kill defender.

Works like get_defender; see comment there.
This function is mostly used by the AI.
**************************************************************************/
struct unit *get_attacker(struct unit *defender, const struct tile *ptile)
{
  struct unit *bestatt = 0;
  int bestvalue = -1, unit_a, best_cost = 0;

  unit_list_iterate(ptile->units, attacker) {
    int build_cost = unit_build_shield_cost(attacker->type);

    if (pplayers_allied(unit_owner(defender), unit_owner(attacker))) {
      return NULL;
    }
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

/**************************************************************************
  Is it a city/fortress/air base or will the whole stack die in an attack
**************************************************************************/
bool is_stack_vulnerable(const struct tile *ptile)
{
  return !(ptile->city != NULL
           || map_has_special(ptile, S_FORTRESS)
           || map_has_special(ptile, S_AIRBASE)
           || !game.rgame.killstack);
}
