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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "combat.h"
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "gamelog.h"
#include "gotohand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "maphand.h"
#include "plrhand.h"
#include "sernet.h"
#include "settlers.h"
#include "srv_main.h"
#include "unithand.h"

#include "aiunit.h"
#include "aitools.h"

#include "unittools.h"


static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void update_unit_activity(struct unit *punit);
static void wakeup_neighbor_sentries(struct unit *punit);
static bool upgrade_would_strand(struct unit *punit, int upgrade_type);
static void handle_leonardo(struct player *pplayer);

static void sentry_transported_idle_units(struct unit *ptrans);

static void refuel_air_units_from_carriers(struct player *pplayer);
static struct unit *find_best_air_unit_to_refuel(struct player *pplayer, 
						 int x, int y, bool missile);
static struct unit *choose_more_important_refuel_target(struct unit *punit1,
							struct unit *punit2);
static bool is_airunit_refuel_point(int x, int y, struct player *pplayer,
				   Unit_Type_id type, bool unit_is_on_tile);
static bool maybe_cancel_patrol_due_to_enemy(struct unit *punit);

/**************************************************************************
  returns a unit type with a given role, use -1 if you don't want a tech 
  role. Always try tech role and only if not available, return role unit.
**************************************************************************/
int find_a_unit_type(int role, int role_tech)
{
  int which[U_LAST];
  int i, num=0;

  if (role_tech != -1) {
    for(i=0; i<num_role_units(role_tech); i++) {
      int iunit = get_role_unit(role_tech, i);
      if (game.global_advances[get_unit_type(iunit)->tech_requirement] >= 2) {
	which[num++] = iunit;
      }
    }
  }
  if(num==0) {
    for(i=0; i<num_role_units(role); i++) {
      which[num++] = get_role_unit(role, i);
    }
  }
  if(num==0) {
    /* Ruleset code should ensure there is at least one unit for each
     * possibly-required role, or check before calling this function.
     */
    freelog(LOG_FATAL, "No unit types in find_a_unit_type(%d,%d)!",
	    role, role_tech);
    abort();
  }
  return which[myrand(num)];
}

/**************************************************************************
  Unit can't attack if:
 1) it doesn't have any attack power.
 2) it's not a fighter and the defender is a flying unit (except city/airbase).
 3) if it's not a marine (and ground unit) and it attacks from ocean.
 4) a ground unit can't attack a ship on an ocean square (except marines).
 5) the players are not at war.
**************************************************************************/
bool can_unit_attack_unit_at_tile(struct unit *punit, struct unit *pdefender,
				 int dest_x, int dest_y)
{
  enum tile_terrain_type fromtile;
  enum tile_terrain_type totile;

  fromtile = map_get_terrain(punit->x, punit->y);
  totile   = map_get_terrain(dest_x, dest_y);
  
  if(!is_military_unit(punit))
    return FALSE;

  /* only fighters can attack planes, except for city or airbase attacks */
  if (!unit_flag(punit, F_FIGHTER) && is_air_unit(pdefender) &&
      !(map_get_city(dest_x, dest_y) || map_has_special(dest_x, dest_y, S_AIRBASE))) {
    return FALSE;
  }
  /* can't attack with ground unit from ocean, except for marines */
  if(fromtile==T_OCEAN && is_ground_unit(punit) && !unit_flag(punit, F_MARINES)) {
    return FALSE;
  }

  if(totile==T_OCEAN && is_ground_unit(punit)) {
    return FALSE;
  }

  if (unit_flag(punit, F_NO_LAND_ATTACK) && totile!=T_OCEAN)  {
    return FALSE;
  }
  
  if (!pplayers_at_war(unit_owner(punit), unit_owner(pdefender)))
    return FALSE;

  /* Shore bombardement */
  if (fromtile==T_OCEAN && is_sailing_unit(punit) && totile!=T_OCEAN)
    return (get_attack_power(punit)>0);

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_attack_tile(struct unit *punit, int dest_x, int dest_y)
{
  struct unit *pdefender;
  pdefender=get_defender(punit, dest_x, dest_y);
  return(can_unit_attack_unit_at_tile(punit, pdefender, dest_x, dest_y));
}

/**************************************************************************
  after a battle this routine is called to decide whether or not the unit
  should become a veteran, if unit isn't already.
  there is a 50/50% chance for it to happend, (100% if player got SUNTZU)
**************************************************************************/
void maybe_make_veteran(struct unit *punit)
{
  if (punit->veteran)
    return;
  if (player_owns_active_wonder(unit_owner(punit), B_SUNTZU))
    punit->veteran = TRUE;
  else
    punit->veteran = (myrand(2) == 1);
}

/**************************************************************************
  This is the basic unit versus unit combat routine.
  1) ALOT of modifiers bonuses etc is added to the 2 units rates.
  2) the combat loop, which continues until one of the units are dead
  3) the aftermath, the looser (and potentially the stack which is below it)
     is wiped, and the winner gets a chance of gaining veteran status
**************************************************************************/
void unit_versus_unit(struct unit *attacker, struct unit *defender)
{
  int attackpower = get_total_attack_power(attacker,defender);
  int defensepower = get_total_defense_power(attacker,defender);

  int attack_firepower, defense_firepower;
  get_modified_firepower(attacker, defender,
			 &attack_firepower, &defense_firepower);

  freelog(LOG_VERBOSE, "attack:%d, defense:%d, attack firepower:%d, defense firepower:%d",
	  attackpower, defensepower, attack_firepower, defense_firepower);
  if (attackpower == 0) {
      attacker->hp=0; 
  } else if (defensepower == 0) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower) >= defensepower) {
      defender->hp -= attack_firepower * game.firepower_factor;
    } else {
      attacker->hp -= defense_firepower * game.firepower_factor;
    }
  }
  if (attacker->hp<0) attacker->hp = 0;
  if (defender->hp<0) defender->hp = 0;

  if (attacker->hp > 0)
    maybe_make_veteran(attacker); 
  else if (defender->hp > 0)
    maybe_make_veteran(defender);
}

/***************************************************************************
 Return 1 if upgrading this unit could cause passengers to
 get stranded at sea, due to transport capacity changes
 caused by the upgrade.
 Return 0 if ok to upgrade (as far as stranding goes).
***************************************************************************/
static bool upgrade_would_strand(struct unit *punit, int upgrade_type)
{
  int old_cap, new_cap, tile_cap, tile_ncargo;
  
  if (!is_sailing_unit(punit))
    return FALSE;
  if (map_get_terrain(punit->x, punit->y) != T_OCEAN)
    return FALSE;

  /* With weird non-standard unit types, upgrading these could
     cause air units to run out of fuel; too bad. */
  if (unit_flag(punit, F_CARRIER)
      || unit_flag(punit, F_MISSILE_CARRIER))
    return FALSE;

  old_cap = get_transporter_capacity(punit);
  new_cap = unit_types[upgrade_type].transport_capacity;

  if (new_cap >= old_cap)
    return FALSE;

  tile_cap = 0;
  tile_ncargo = 0;
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
    if (punit2->owner == punit->owner
        || pplayers_allied(unit_owner(punit2), unit_owner(punit))) {
      if (is_sailing_unit(punit2) && is_ground_units_transport(punit2)) { 
	tile_cap += get_transporter_capacity(punit2);
      } else if (is_ground_unit(punit2)) {
	tile_ncargo++;
      }
    }
  }
  unit_list_iterate_end;

  if (tile_ncargo <= tile_cap - old_cap + new_cap)
    return FALSE;

  freelog(LOG_VERBOSE, "Can't upgrade %s at (%d,%d)"
	  " because would strand passenger(s)",
	  unit_type(punit)->name, punit->x, punit->y);
  return TRUE;
}

/***************************************************************************
Do Leonardo's Workshop upgrade(s). Select unit to upgrade by random. --Zamar
Now be careful not to strand units at sea with the Workshop. --dwp
****************************************************************************/
static void handle_leonardo(struct player *pplayer)
{
  int upgrade_type; 
  int leonardo_variant;
	
  struct unit_list candidates;
  int candidate_to_upgrade=-1;

  int i;

  leonardo_variant = improvement_variant(B_LEONARDO);
	
  unit_list_init(&candidates);
	
  unit_list_iterate(pplayer->units, punit) {
    if ((upgrade_type=can_upgrade_unittype(pplayer, punit->type))!=-1 &&
       !upgrade_would_strand(punit, upgrade_type))
      unit_list_insert(&candidates, punit); /* Potential candidate :) */
  } unit_list_iterate_end;
	
  if (unit_list_size(&candidates) == 0)
    return; /* We have Leonardo, but nothing to upgrade! */
	
  if (leonardo_variant == 0)
    candidate_to_upgrade=myrand(unit_list_size(&candidates));

  i=0;	
  unit_list_iterate(candidates, punit) {
    if (leonardo_variant != 0 || i == candidate_to_upgrade) {
      upgrade_type=can_upgrade_unittype(pplayer, punit->type);
      notify_player(pplayer,
            _("Game: %s has upgraded %s to %s%s."),
            improvement_types[B_LEONARDO].name,
            unit_type(punit)->name,
            get_unit_type(upgrade_type)->name,
            get_location_str_in(pplayer, punit->x, punit->y));
      punit->veteran = FALSE;
      upgrade_unit(punit, upgrade_type);
    }
    i++;
  } unit_list_iterate_end;
	
  unit_list_unlink_all(&candidates);
}

/***************************************************************************
Select which unit is more important to refuel:
It's more important to refuel plane which has less fuel.
If those are equal then we refuel more valuable unit.
If those are equal then we refuel veteran unit.
If those are equal then we refuel unit which has more hp.
If those are equal then we refuel the first unit.
****************************************************************************/
static struct unit *choose_more_important_refuel_target(struct unit *punit1,
							struct unit *punit2)
{
  if (punit1->fuel != punit2->fuel)
    return punit1->fuel < punit2->fuel? punit1: punit2;
  
  if (unit_type(punit1)->build_cost != unit_type(punit2)->build_cost)
    return unit_type(punit1)->build_cost >
	unit_type(punit2)->build_cost ? punit1 : punit2;
  
  if (punit1->veteran != punit2->veteran)
    return punit1->veteran > punit2->veteran? punit1: punit2;
  
  if (punit1->hp != punit2->hp)
    return punit1->hp > punit2->hp? punit1: punit2;
  return punit1;
}

/***************************************************************************
...
****************************************************************************/
static struct unit *find_best_air_unit_to_refuel(struct player *pplayer, 
						 int x, int y, bool missile)
{
  struct unit *best_unit=NULL;
  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if ((unit_owner(punit) == pplayer) && is_air_unit(punit) && 
        (!missile || unit_flag(punit, F_MISSILE))) {
      /* We must check that it isn't already refuelled. */ 
      if (punit->fuel < unit_type(punit)->fuel) { 
        if (!best_unit) 
          best_unit=punit;
        else 
          best_unit=choose_more_important_refuel_target(best_unit, punit);
      }
    }
  } unit_list_iterate_end;
  return best_unit;
}

/***************************************************************************
...
****************************************************************************/
static void refuel_air_units_from_carriers(struct player *pplayer)
{
  struct unit_list carriers;
  struct unit_list missile_carriers;
  
  struct unit *punit_to_refuel;
  
  unit_list_init(&carriers);
  unit_list_init(&missile_carriers);
  
  /* Temporarily use 'fuel' on Carriers and Subs to keep track
     of numbers of supported Air Units:   --dwp */

  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_CARRIER)) {
      unit_list_insert(&carriers, punit);
      punit->fuel = unit_type(punit)->transport_capacity;
    } else if (unit_flag(punit, F_MISSILE_CARRIER)) {
      unit_list_insert(&missile_carriers, punit);
      punit->fuel = unit_type(punit)->transport_capacity;
    }
  } unit_list_iterate_end;

  /* Now iterate through missile_carriers and
   * refuel as many missiles as possible */

  unit_list_iterate(missile_carriers, punit) {
    while(punit->fuel > 0) {
      punit_to_refuel= find_best_air_unit_to_refuel(
          pplayer, punit->x, punit->y, TRUE /*missile */);
      if (!punit_to_refuel)
        break; /* Didn't find any */
      punit_to_refuel->fuel = unit_type(punit_to_refuel)->fuel;
      punit->fuel--;
    }
  } unit_list_iterate_end;

  /* Now refuel air units from carriers (also not yet refuelled missiles) */

  unit_list_iterate(carriers, punit) {
    while(punit->fuel > 0) {
      punit_to_refuel= find_best_air_unit_to_refuel(
          pplayer, punit->x, punit->y, FALSE /* any */);
      if (!punit_to_refuel) 
        break;
      punit_to_refuel->fuel = unit_type(punit_to_refuel)->fuel;
      punit->fuel--;
    }
  } unit_list_iterate_end;			

  /* Clean up temporary use of 'fuel' on Carriers and Subs: */
  unit_list_iterate(carriers, punit) {
    punit->fuel = 0;
  } unit_list_iterate_end;

  unit_list_unlink_all(&carriers);

  unit_list_iterate(missile_carriers, punit) {
    punit->fuel = 0;
  } unit_list_iterate_end;

  unit_list_unlink_all(&missile_carriers);
}

/***************************************************************************
Do Leonardo's Workshop upgrade if applicable.
Restore unit hitpoints. (movepoint-restoration moved to update_unit_activities)
Adjust air units for fuel: air units lose fuel unless in a city,
on a Carrier or on a airbase special (or, for Missles, on a Submarine).
Air units which run out of fuel get wiped.
Carriers and Submarines can now only supply fuel to a limited
number of units each, equal to their transport_capacity. --dwp
(Hitpoint adjustments include Helicopters out of cities, but
that is handled within unit_restore_hitpoints().)
Triremes will be wiped with a variable chance if they're not close to
land, and player doesn't have Lighthouse.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  /* 1) get Leonardo out of the way first: */
  if (player_owns_active_wonder(pplayer, B_LEONARDO))
    handle_leonardo(pplayer);

  unit_list_iterate(pplayer->units, punit) {

    /* 2) Modify unit hitpoints. Helicopters can even lose them. */
    unit_restore_hitpoints(pplayer, punit);

    /* 3) Check that unit has hitpoints */
    if (punit->hp<=0) {
      /* This should usually only happen for heli units,
	 but if any other units get 0 hp somehow, catch
	 them too.  --dwp  */
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
          _("Game: Your %s has run out of hit points."), 
          unit_name(punit->type));
      gamelog(GAMELOG_UNITF, _("%s lose a %s (out of hp)"),
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit_safe(punit, &myiter);
      continue; /* Continue iterating... */
    }

    /* 4) Check that triremes are near coastline, otherwise... */
    if (unit_flag(punit, F_TRIREME)
	&& myrand(100) < trireme_loss_pct(pplayer, punit->x, punit->y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has been lost on the high seas."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITTRI, _("%s Trireme lost at sea"),
	      get_nation_name_plural(pplayer->nation));
      wipe_unit_safe(punit, &myiter);
      continue; /* Continue iterating... */
    }

    /* 5) Resque planes if needed */
    if (is_air_unit(punit)) {
      /* Shall we emergency return home on the last vapors? */

      /* I think this is strongly against the spirit of client goto.
       * The problem is (again) that here we know too much. -- Zamar */

      if (punit->fuel == 1
	  && !is_airunit_refuel_point(punit->x, punit->y,
				      unit_owner(punit), punit->type, TRUE)) {
	iterate_outward(punit->x, punit->y, punit->moves_left/3, x_itr, y_itr) {
	  if (is_airunit_refuel_point
	      (x_itr, y_itr, unit_owner(punit), punit->type, FALSE)
	      &&
	      (air_can_move_between
	       (punit->moves_left / 3, punit->x, punit->y, x_itr, y_itr,
		unit_owner(punit)) >= 0)) {
	    punit->goto_dest_x = x_itr;
	    punit->goto_dest_y = y_itr;
	    set_unit_activity(punit, ACTIVITY_GOTO);
	    do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
	    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			     _("Game: Your %s has returned to refuel."),
			     unit_name(punit->type));
	    goto OUT;
	  }
	} iterate_outward_end;
      }
    OUT:

      /* 6) Update fuel */
      punit->fuel--;

      /* 7) Automatically refuel air units in cities and airbases */
      if (map_get_city(punit->x, punit->y) ||
	  map_has_special(punit->x, punit->y, S_AIRBASE))
	punit->fuel=unit_type(punit)->fuel;
    }
  } unit_list_iterate_end;

  /* 8) Use carriers and submarines to refuel air units */
  refuel_air_units_from_carriers(pplayer);
  
  /* 9) Check if there are air units without fuel */
  unit_list_iterate(pplayer->units, punit) {
    if (is_air_unit(punit) && punit->fuel<=0) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has run out of fuel."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, _("%s lose a %s (fuel)"),
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit_safe(punit, &myiter);
    } 
  } unit_list_iterate_end;
}

/****************************************************************************
  add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not loose hp's every turn if player have that wonder.
  Units which have moved don't gain hp, except the United Nations and
  helicopter effects still occur.
*****************************************************************************/
static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit)
{
  bool was_lower;

  was_lower=(punit->hp < unit_type(punit)->hp);

  if(!punit->moved) {
    punit->hp+=hp_gain_coord(punit);
  }
  
  if (player_owns_active_wonder(pplayer, B_UNITED)) 
    punit->hp+=2;
    
  if(is_heli_unit(punit)) {
    struct city *pcity = map_get_city(punit->x,punit->y);
    if(!pcity) {
      if (!map_has_special(punit->x, punit->y, S_AIRBASE))
        punit->hp-=unit_type(punit)->hp/10;
    }
  }

  if(punit->hp>=unit_type(punit)->hp) {
    punit->hp=unit_type(punit)->hp;
    if(was_lower&&punit->activity==ACTIVITY_SENTRY)
      set_unit_activity(punit,ACTIVITY_IDLE);
  }
  if(punit->hp<0)
    punit->hp=0;

  punit->moved = FALSE;
  punit->paradropped = FALSE;
}
  
/***************************************************************************
 move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs
***************************************************************************/
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left=unit_move_rate(punit);
}

/**************************************************************************
  iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate_safe(pplayer->units, punit)
    update_unit_activity(punit);
  unit_list_iterate_safe_end;
}

/**************************************************************************
  returns how many hp's a unit will gain on this square
  depends on whether or not it's inside city or fortress.
  barracks will regen landunits completely
  airports will regen airunits  completely
  ports    will regen navalunits completely
  fortify will add a little extra.
***************************************************************************/
int hp_gain_coord(struct unit *punit)
{
  int hp;
  struct city *pcity;
  if (unit_on_fortress(punit))
    hp=unit_type(punit)->hp/4;
  else
    hp=0;
  if((pcity=map_get_city(punit->x,punit->y))) {
    if ((city_got_barracks(pcity) &&
	 (is_ground_unit(punit) || improvement_variant(B_BARRACKS)==1)) ||
	(city_got_building(pcity, B_AIRPORT) && is_air_unit(punit)) || 
	(city_got_building(pcity, B_AIRPORT) && is_heli_unit(punit)) || 
	(city_got_building(pcity, B_PORT) && is_sailing_unit(punit))) {
      hp=unit_type(punit)->hp;
    }
    else
      hp=unit_type(punit)->hp/3;
  }
  else if (!is_heli_unit(punit))
    hp++;

  if(punit->activity==ACTIVITY_FORTIFIED)
    hp++;
  
  return hp;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task.
**************************************************************************/
static int total_activity (int x, int y, enum unit_activity act)
{
  struct tile *ptile;
  int total = 0;

  ptile = map_get_tile (x, y);
  unit_list_iterate (ptile->units, punit)
    if (punit->activity == act)
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  Calculate the total amount of activity performed by all units on a tile
  for a given task and target.
**************************************************************************/
static int total_activity_targeted(int x, int y, enum unit_activity act,
				   enum tile_special_type tgt)
{
  int total = 0;

  unit_list_iterate (map_get_tile (x, y)->units, punit)
    if ((punit->activity == act) && (punit->activity_target == tgt))
      total += punit->activity_count;
  unit_list_iterate_end;
  return total;
}

/**************************************************************************
  For each city adjacent to (x,y), check if landlocked.  If so, sell all
  improvements in the city that depend upon being next to an ocean tile.
  (Should be called after any ocean to land terrain changes.
   Assumes no city at (x,y).)
**************************************************************************/
static void city_landlocked_sell_coastal_improvements(int x, int y)
{
  /* FIXME: this should come from buildings.ruleset */
  static int coastal_improvements[] =
  {
    B_HARBOUR,
    B_PORT,
    B_COASTAL,
    B_OFFSHORE
  };

#define coastal_improvements_count ARRAY_SIZE(coastal_improvements)

  adjc_iterate(x, y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && !is_terrain_near_tile(x1, y1, T_OCEAN)) {
      struct player *pplayer = city_owner(pcity);
      int k;
      for (k=0; k<coastal_improvements_count; k++) {
	if (city_got_building(pcity, coastal_improvements[k])) {
	  do_sell_building(pplayer, pcity, coastal_improvements[k]);
	  notify_player_ex(pplayer, x1, y1, E_IMP_SOLD,
			   _("Game: You sell %s in %s (now landlocked)"
			     " for %d gold."),
			   get_improvement_name(coastal_improvements[k]),
			   pcity->name,
			   improvement_value(coastal_improvements[k]));
	}
      }
    }
  } adjc_iterate_end;
}

/**************************************************************************
  Set the tile to be a river if required.
  It's required if one of the tiles nearby would otherwise be part of a
  river to nowhere.
  For simplicity, I'm assuming that this is the only exit of the river,
  so I don't need to trace it across the continent.  --CJM
  Also, note that this only works for R_AS_SPECIAL type rivers.  --jjm
**************************************************************************/
static void ocean_to_land_fix_rivers(int x, int y)
{
  /* clear the river if it exists */
  map_clear_special(x, y, S_RIVER);

  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (map_has_special(x1, y1, S_RIVER)) {
      bool ocean_near = FALSE;
      cartesian_adjacent_iterate(x1, y1, x2, y2) {
	if (map_get_terrain(x2, y2) == T_OCEAN)
	  ocean_near = TRUE;
      } cartesian_adjacent_iterate_end;
      if (!ocean_near) {
	map_set_special(x, y, S_RIVER);
	return;
      }
    }
  } cartesian_adjacent_iterate_end;
}

/**************************************************************************
  Checks for terrain change between ocean and land.  Handles side-effects.
  (Should be called after any potential ocean/land terrain changes.)
  Also, returns an enum ocean_land_change, describing the change, if any.
**************************************************************************/
enum ocean_land_change { OLC_NONE, OLC_OCEAN_TO_LAND, OLC_LAND_TO_OCEAN };

static enum ocean_land_change check_terrain_ocean_land_change(int x, int y,
					      enum tile_terrain_type oldter)
{
  enum tile_terrain_type newter = map_get_terrain(x, y);

  if ((oldter == T_OCEAN) && (newter != T_OCEAN)) {
    /* ocean to land ... */
    ocean_to_land_fix_rivers(x, y);
    city_landlocked_sell_coastal_improvements(x, y);
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, _("(%d,%d) land created from ocean"), x, y);
    return OLC_OCEAN_TO_LAND;
  } else if ((oldter != T_OCEAN) && (newter == T_OCEAN)) {
    /* land to ocean ... */
    assign_continent_numbers();
    gamelog(GAMELOG_MAP, _("(%d,%d) ocean created from land"), x, y);
    return OLC_LAND_TO_OCEAN;
  }
  return OLC_NONE;
}

/**************************************************************************
  progress settlers in their current tasks, 
  and units that is pillaging.
  also move units that is on a goto.
  restore unit move points (information needed for settler tasks)
**************************************************************************/
static void update_unit_activity(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  int id = punit->id;
  int mr = unit_type(punit)->move_rate;
  bool unit_activity_done = FALSE;
  int activity = punit->activity;
  enum ocean_land_change solvency = OLC_NONE;
  struct tile *ptile = map_get_tile(punit->x, punit->y);

  /* to allow a settler to begin a task with no moves left
     without it counting toward the time to finish */
  if (punit->moves_left > 0) {
    punit->activity_count += mr/SINGLE_MOVE;
  } else if (mr == 0) {
    punit->activity_count += 1;
  }

  unit_restore_movepoints(pplayer, punit);

  if (punit->connecting && !can_unit_do_activity(punit, activity)) {
    punit->activity_count = 0;
    if (do_unit_goto(punit, get_activity_move_restriction(activity), FALSE)
	== GR_DIED) {
      return;
    }
  }

  /* if connecting, automagically build prerequisities first */
  if (punit->connecting && activity == ACTIVITY_RAILROAD &&
      !map_has_special(punit->x, punit->y, S_ROAD)) {
    activity = ACTIVITY_ROAD;
  }

  if (activity == ACTIVITY_EXPLORE) {
    bool more_to_explore = ai_manage_explorer(punit);
    if (more_to_explore && player_find_unit_by_id(pplayer, id))
      handle_unit_activity_request(punit, ACTIVITY_EXPLORE);
    else
      return;
  }

  if (activity==ACTIVITY_PILLAGE) {
    if (punit->activity_target == S_NO_SPECIAL) {	/* case for old save files */
      if (punit->activity_count >= 1) {
	int what =
	  get_preferred_pillage(
	    get_tile_infrastructure_set(
	      map_get_tile(punit->x, punit->y)));
	if (what != S_NO_SPECIAL) {
	  map_clear_special(punit->x, punit->y, what);
	  send_tile_info(NULL, punit->x, punit->y);
	  set_unit_activity(punit, ACTIVITY_IDLE);
	}

	/* If a watchtower has been pillaged, reduce sight to normal */
	if (what == S_FORTRESS
	    && player_knows_techs_with_flag(pplayer, TF_WATCHTOWER)) {
	  freelog(LOG_VERBOSE, "Watchtower pillaged!");
	  unit_list_iterate(map_get_tile(punit->x, punit->y)->units,
			    punit2) {
	    if (is_ground_unit(punit2)) {
	      unfog_area(pplayer, punit2->x, punit2->y,
			 unit_type(punit2)->vision_range);
	      fog_area(pplayer, punit2->x, punit2->y,
		       get_watchtower_vision(punit2));
	    }
	  }
	  unit_list_iterate_end;
	}
      }
    }
    else if (total_activity_targeted (punit->x, punit->y,
				      ACTIVITY_PILLAGE, punit->activity_target) >= 1) {
      enum tile_special_type what_pillaged = punit->activity_target;
      map_clear_special(punit->x, punit->y, what_pillaged);
      unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(NULL, punit2);
	}
      unit_list_iterate_end;
      send_tile_info(NULL, punit->x, punit->y);
      
      /* If a watchtower has been pillaged, reduce sight to normal */
      if (what_pillaged == S_FORTRESS
	  && player_knows_techs_with_flag(pplayer, TF_WATCHTOWER)) {
	freelog(LOG_VERBOSE, "Watchtower(2) pillaged!");
	unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	  if (is_ground_unit(punit2)) {
	    unfog_area(pplayer, punit2->x, punit2->y,
		       unit_type(punit2)->vision_range);
	    fog_area(pplayer, punit2->x, punit2->y,
		     get_watchtower_vision(punit2));
	  }
	}
	unit_list_iterate_end;
      }
    }    
  }

  if (activity==ACTIVITY_POLLUTION) {
    if (total_activity (punit->x, punit->y, ACTIVITY_POLLUTION)
	>= map_clean_pollution_time(punit->x, punit->y)) {
      map_clear_special(punit->x, punit->y, S_POLLUTION);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_FALLOUT) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FALLOUT)
	>= map_clean_fallout_time(punit->x, punit->y)) {
      map_clear_special(punit->x, punit->y, S_FALLOUT);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_FORTRESS) {
    if (total_activity (punit->x, punit->y, ACTIVITY_FORTRESS)
	>= map_build_fortress_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_FORTRESS);
      unit_activity_done = TRUE;
      /* watchtower becomes effective */
      if (player_knows_techs_with_flag(pplayer, TF_WATCHTOWER)) {
	unit_list_iterate(ptile->units, punit) {
	  if (is_ground_unit(punit)) {
	    fog_area(pplayer, punit->x, punit->y,
		     unit_type(punit)->vision_range);
	    unfog_area(pplayer, punit->x, punit->y,
		       get_watchtower_vision(punit));
	  }
	}
	unit_list_iterate_end;
      }
    }
  }

  if (activity==ACTIVITY_AIRBASE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_AIRBASE)
	>= map_build_airbase_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_AIRBASE);
      unit_activity_done = TRUE;
    }
  }
  
  if (activity==ACTIVITY_IRRIGATE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_IRRIGATE) >=
        map_build_irrigation_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_irrigate_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_ROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_ROAD)
	+ total_activity (punit->x, punit->y, ACTIVITY_RAILROAD) >=
        map_build_road_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_ROAD);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_RAILROAD) {
    if (total_activity (punit->x, punit->y, ACTIVITY_RAILROAD)
	>= map_build_rail_time(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_RAILROAD);
      unit_activity_done = TRUE;
    }
  }
  
  if (activity==ACTIVITY_MINE) {
    if (total_activity (punit->x, punit->y, ACTIVITY_MINE) >=
        map_build_mine_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_mine_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->x, punit->y, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->x, punit->y)) {
      enum tile_terrain_type old = map_get_terrain(punit->x, punit->y);
      map_transform_tile(punit->x, punit->y);
      solvency = check_terrain_ocean_land_change(punit->x, punit->y, old);
      unit_activity_done = TRUE;
    }
  }

  if (unit_activity_done) {
    send_tile_info(NULL, punit->x, punit->y);
    unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2) {
      if (punit2->activity == activity) {
	bool alive = TRUE;
	if (punit2->connecting) {
	  punit2->activity_count = 0;
	  alive = (do_unit_goto(punit2,
				get_activity_move_restriction(activity),
				FALSE) != GR_DIED);
	} else {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	}
	if (alive) {
	  send_unit_info(NULL, punit2);
	}
      }
    } unit_list_iterate_end;
  }

  if (activity==ACTIVITY_FORTIFYING) {
    if (punit->activity_count >= 1) {
      set_unit_activity(punit,ACTIVITY_FORTIFIED);
    }
  }

  if (activity==ACTIVITY_GOTO) {
    if (!punit->ai.control && (!is_military_unit(punit) ||
       punit->ai.passenger != 0 || !pplayer->ai.control)) {
/* autosettlers otherwise waste time; idling them breaks assignment */
/* Stalling infantry on GOTO so I can see where they're GOing TO. -- Syela */
      do_unit_goto(punit, GOTO_MOVE_ANY, TRUE);
    }
    return;
  }

  if (punit->activity == ACTIVITY_PATROL) {
    if (goto_route_execute(punit) == GR_DIED) {
      return;
    }
  }

  send_unit_info(NULL, punit);

  unit_list_iterate(ptile->units, punit2) {
    if (!can_unit_continue_current_activity(punit2))
      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
  } unit_list_iterate_end;

  /* Any units that landed in water or boats that landed on land as a
     result of settlers changing terrain must be moved back into their
     right environment.
     We advance the unit_list iterator passed into this routine from
     update_unit_activities() if we delete the unit it points to.
     We go to START each time we moved a unit to avoid problems with the
     tile unit list getting corrupted.

     FIXME:  We shouldn't do this at all!  There seems to be another
     bug which is expressed when units wind up on the "wrong" terrain;
     this is the bug that should be fixed.  Also, introduction of the
     "amphibious" movement category would allow the definition of units
     that can "safely" change land<->ocean -- in which case all others
     would (here) be summarily disbanded (suicide to accomplish their
     task, for the greater good :).   --jjm
  */
 START:
  switch (solvency) {
  case OLC_NONE:
    break; /* nothing */

  case OLC_LAND_TO_OCEAN:
    unit_list_iterate(ptile->units, punit2) {
      if (is_ground_unit(punit2)) {
	/* look for nearby land */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain != T_OCEAN
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->x, punit2->y, E_UNIT_RELOCATED,
			     _("Game: Moved your %s due to changing"
			       " land to sea at (%d, %d)."),
			     unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, TRUE, FALSE, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby transport */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain == T_OCEAN
	      && ground_unit_transporter_capacity(x, y,
						  unit_owner(punit2)) > 0) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Embarked %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->x, punit2->y, E_UNIT_RELOCATED,
			     _("Game: Embarked your %s due to changing"
			       " land to sea at (%d, %d)."),
			     unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, TRUE, FALSE, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing land to sea at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player_ex(unit_owner(punit2),
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: Disbanded your %s due to changing"
			   " land to sea at (%d, %d)."),
			 unit_name(punit2->type), punit2->x, punit2->y);
	wipe_unit_spec_safe(punit2, NULL, FALSE);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  case OLC_OCEAN_TO_LAND:
    unit_list_iterate(ptile->units, punit2) {
      if (is_sailing_unit(punit2)) {
	/* look for nearby water */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (ptile2->terrain == T_OCEAN
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player_ex(unit_owner(punit2),
			  punit2->x, punit2->y, E_UNIT_RELOCATED,
			  _("Game: Moved your %s due to changing"
			    " sea to land at (%d, %d)."),
			  unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, TRUE, TRUE, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby port */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (is_allied_city_tile(ptile2, unit_owner(punit2))
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Docked %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->x, punit2->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->x, punit2->y, E_UNIT_RELOCATED,
			     _("Game: Docked your %s due to changing"
			       " sea to land at (%d, %d)."),
			     unit_name(punit2->type), punit2->x, punit2->y);
	    move_unit(punit2, x, y, TRUE, TRUE, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing sea to land at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->x, punit2->y);
	notify_player_ex(unit_owner(punit2),
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: Disbanded your %s due to changing"
			   " sea to land at (%d, %d)."),
			 unit_name(punit2->type), punit2->x, punit2->y);
	wipe_unit_spec_safe(punit2, NULL, FALSE);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  }
}


/**************************************************************************
...
**************************************************************************/
void advance_unit_focus(struct unit *punit)
{
  conn_list_iterate(unit_owner(punit)->connections, pconn) {
    struct packet_generic_integer packet;
    packet.value = punit->id;
    send_packet_generic_integer(pconn, PACKET_ADVANCE_FOCUS, &packet);
  } conn_list_iterate_end;
}

/**************************************************************************
  Returns a pointer to a (static) string which gives an informational
  message about location (x,y), in terms of cities known by pplayer.
  One of:
    "in Foo City"  or  "at Foo City" (see below)
    "outside Foo City"
    "near Foo City"
    "" (if no cities known)
  There are two variants for the first case, one when something happens
  inside the city, otherwise when it happens "at" but "outside" the city.
  Eg, when an attacker fails, the attacker dies "at" the city, but
  not "in" the city (since the attacker never made it in).
  Don't call this function directly; use the wrappers below.
**************************************************************************/
static char *get_location_str(struct player *pplayer, int x, int y, bool use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(x, y);
  if (incity) {
    if (use_at) {
      my_snprintf(buffer, sizeof(buffer), _(" at %s"), incity->name);
    } else {
      my_snprintf(buffer, sizeof(buffer), _(" in %s"), incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, x, y, FALSE, FALSE);
    if (nearcity) {
      if (is_tiles_adjacent(x, y, nearcity->x, nearcity->y)) {
	my_snprintf(buffer, sizeof(buffer),
		   _(" outside %s"), nearcity->name);
      } else {
	my_snprintf(buffer, sizeof(buffer),
		    _(" near %s"), nearcity->name);
      }
    } else {
      buffer[0] = '\0';
    }
  }
  return buffer;
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_in(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, FALSE);
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_at(struct player *pplayer, int x, int y)
{
  return get_location_str(pplayer, x, y, TRUE);
}

/**************************************************************************
...
**************************************************************************/
enum goto_move_restriction get_activity_move_restriction(enum unit_activity activity)
{
  enum goto_move_restriction restr;

  switch (activity) {
  case ACTIVITY_IRRIGATE:
    restr = GOTO_MOVE_CARDINAL_ONLY;
    break;
  case ACTIVITY_POLLUTION:
  case ACTIVITY_ROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_PILLAGE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_FALLOUT:
    restr = GOTO_MOVE_STRAIGHTEST;
    break;
  default:
    restr = GOTO_MOVE_ANY;
    break;
  }

  return (restr);
}

/**************************************************************************
...
**************************************************************************/
static bool find_a_good_partisan_spot(struct city *pcity, int u_type,
				     int *x, int *y)
{
  int bestvalue = 0;
  /* coords of best tile in arg pointers */
  map_city_radius_iterate(pcity->x, pcity->y, x1, y1) {
    struct tile *ptile = map_get_tile(x1, y1);
    int value;
    if (ptile->terrain == T_OCEAN)
      continue;
    if (ptile->city)
      continue;
    if (unit_list_size(&ptile->units) > 0)
      continue;
    value = get_virtual_defense_power(U_LAST, u_type, x1, y1, FALSE, FALSE);
    value *= 10;

    if (ptile->continent != map_get_continent(pcity->x, pcity->y))
      value /= 2;

    value -= myrand(value/3);

    if (value > bestvalue) {
      *x = x1;
      *y = y1;
      bestvalue = value;
    }
  } map_city_radius_iterate_end;

  return bestvalue > 0;
}

/**************************************************************************
  finds a spot around pcity and place a partisan.
**************************************************************************/
static void place_partisans(struct city *pcity, int count)
{
  int x, y;
  int u_type = get_role_unit(L_PARTISAN, 0);

  while ((count--) > 0 && find_a_good_partisan_spot(pcity, u_type, &x, &y)) {
    struct unit *punit;
    punit = create_unit(city_owner(pcity), x, y, u_type, FALSE, 0, -1);
    if (can_unit_do_activity(punit, ACTIVITY_FORTIFYING)) {
      punit->activity = ACTIVITY_FORTIFIED; /* yes; directly fortified */
      send_unit_info(NULL, punit);
    }
  }
}

/**************************************************************************
  if requirements to make partisans when a city is conquered is fullfilled
  this routine makes a lot of partisans based on the city's size.
  To be candidate for partisans the following things must be satisfied:
  1) Guerilla warfare must be known by atleast 1 player
  2) The owner of the city is the original player.
  3) The player must know about communism and gunpowder
  4) the player must run either a democracy or a communist society.
**************************************************************************/
void make_partisans(struct city *pcity)
{
  struct player *pplayer;
  int i, partisans;

  if (num_role_units(L_PARTISAN)==0)
    return;
  if (!tech_exists(game.rtech.u_partisan)
      || game.global_advances[game.rtech.u_partisan] == 0
      || pcity->original != pcity->owner)
    return;

  if (!government_has_flag(get_gov_pcity(pcity), G_INSPIRES_PARTISANS))
    return;
  
  pplayer = city_owner(pcity);
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    int tech = game.rtech.partisan_req[i];
    if (tech == A_LAST) break;
    if (get_invention(pplayer, tech) != TECH_KNOWN) return;
    /* Was A_COMMUNISM and A_GUNPOWDER */
  }
  
  partisans = myrand(1 + pcity->size/2) + 1;
  if (partisans > 8) 
    partisans = 8;
  
  place_partisans(pcity,partisans);
}

/**************************************************************************
...
**************************************************************************/
bool enemies_at(struct unit *punit, int x, int y)
{
  int a = 0, d, db;
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = map_get_tile(x,y)->city;

  if (pcity && pplayers_allied(city_owner(pcity), unit_owner(punit))
      && !is_non_allied_unit_tile(map_get_tile(x,y), pplayer)) {
    return FALSE;
  }

  db = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_has_special(x, y, S_RIVER))
    db += (db * terrain_control.river_defense_bonus) / 100;
  d = unit_vulnerability_virtual(punit) * db;
  adjc_iterate(x, y, x1, y1) {
    if (!pplayer->ai.control
	&& !map_get_known_and_seen(x1, y1, unit_owner(punit))) continue;
    if (is_enemy_city_tile(map_get_tile(x1, y1), unit_owner(punit)))
      return TRUE;
    unit_list_iterate(map_get_tile(x1, y1)->units, enemy) {
      if (pplayers_at_war(unit_owner(enemy), unit_owner(punit)) &&
	  can_unit_attack_unit_at_tile(enemy, punit, x, y)) {
	a += unit_belligerence_basic(enemy);
	if ((a * a * 10) >= d) return TRUE;
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;

  return FALSE; /* as good a quick'n'dirty should be -- Syela */
}

/**************************************************************************
Disband given unit because of a stack conflict.
**************************************************************************/
static void disband_stack_conflict_unit(struct unit *punit, bool verbose)
{
  freelog(LOG_VERBOSE, "Disbanded %s's %s at (%d, %d)",
	  unit_owner(punit)->name, unit_name(punit->type),
	  punit->x, punit->y);
  if (verbose) {
    notify_player(unit_owner(punit),
		  _("Game: Disbanded your %s at (%d, %d)."),
		  unit_name(punit->type), punit->x, punit->y);
  }
  wipe_unit(punit);
}

/**************************************************************************
Teleport punit to city at cost specified.  Returns success.
(If specified cost is -1, then teleportation costs all movement.)
                         - Kris Bubendorfer
**************************************************************************/
bool teleport_unit_to_city(struct unit *punit, struct city *pcity,
			  int move_cost, bool verbose)
{
  int src_x = punit->x;
  int src_y = punit->y;
  int dest_x = pcity->x;
  int dest_y = pcity->y;
  if (pcity->owner == punit->owner){
    freelog(LOG_VERBOSE, "Teleported %s's %s from (%d, %d) to %s",
	    unit_owner(punit)->name, unit_name(punit->type),
	    src_x, src_y, pcity->name);
    if (verbose) {
      notify_player(unit_owner(punit),
		    _("Game: Teleported your %s from (%d, %d) to %s."),
		    unit_name(punit->type), src_x, src_y, pcity->name);
    }

    if (move_cost == -1)
      move_cost = punit->moves_left;
    return move_unit(punit, dest_x, dest_y, FALSE, FALSE, move_cost);
  }
  return FALSE;
}

/**************************************************************************
  Resolve unit stack

  When in civil war (or an alliance breaks) there will potentially be units 
  from both sides coexisting on the same squares.  This routine resolves 
  this by teleporting the units in multiowner stacks to the closest city.

  That is, if a unit is closer to its city than the coexistent enemy unit,
  then the enemy unit is teleported to its owner's closest city.

                         - Kris Bubendorfer

  If verbose is true, the unit owner gets messages about where each
  units goes.  --dwp
**************************************************************************/
void resolve_unit_stack(int x, int y, bool verbose)
{
  struct unit *punit, *cunit;
  struct tile *ptile = map_get_tile(x,y);

  /* We start by reducing the unit list until we only have allied units */
  while(TRUE) {
    struct city *pcity, *ccity;

    if (unit_list_size(&ptile->units) == 0)
      return;

    punit = unit_list_get(&(ptile->units), 0);
    pcity = find_closest_owned_city(unit_owner(punit), x, y,
				    is_sailing_unit(punit), NULL);

    /* If punit is in an enemy city we send it to the closest friendly city
       This is not always caught by the other checks which require that
       there are units from two nations on the tile */
    if (ptile->city && !is_allied_city_tile(ptile, unit_owner(punit))) {
      if (pcity)
	teleport_unit_to_city(punit, pcity, 0, verbose);
      else
	disband_stack_conflict_unit(punit, verbose);
      continue;
    }

    cunit = is_non_allied_unit_tile(ptile, unit_owner(punit));
    if (!cunit)
      break;

    ccity = find_closest_owned_city(unit_owner(cunit), x, y,
				    is_sailing_unit(cunit), NULL);

    if (pcity && ccity) {
      /* Both unit owners have cities; teleport unit farthest from its
	 owner's city to that city. This also makes sure we get no loops
	 from when we resolve the stack inside a city. */
      if (map_distance(x, y, pcity->x, pcity->y) 
	  < map_distance(x, y, ccity->x, ccity->y))
	teleport_unit_to_city(cunit, ccity, 0, verbose);
      else
	teleport_unit_to_city(punit, pcity, 0, verbose);
    } else {
      /* At least one of the unit owners doesn't have any cities;
	 if the other owner has any cities we teleport his units to
	 the closest. We take care not to teleport the unit to the
	 original square, as that would cause the while loop in this
	 function to potentially never stop. */
      if (pcity) {
	if (same_pos(x, y, pcity->x, pcity->y))
	  disband_stack_conflict_unit(cunit, verbose);
	else
	  teleport_unit_to_city(punit, pcity, 0, verbose);
      } else if (ccity) {
	if (same_pos(x, y, ccity->x, ccity->y))
	  disband_stack_conflict_unit(punit, verbose);
	else
	  teleport_unit_to_city(cunit, ccity, 0, verbose);
      } else {
	/* Neither unit owners have cities;
	   disband both units. */
	disband_stack_conflict_unit(punit, verbose);
	disband_stack_conflict_unit(cunit, verbose);
      }      
    }
  } /* end while */

  /* There is only one allied units left on this square.  If there is not 
     enough transporter capacity left send surplus to the closest friendly 
     city. */
  punit = unit_list_get(&(ptile->units), 0);

  if (ptile->terrain == T_OCEAN) {
  START:
    unit_list_iterate(ptile->units, vunit) {
      if (ground_unit_transporter_capacity(x, y, unit_owner(punit)) < 0) {
 	unit_list_iterate(ptile->units, wunit) {
 	  if (is_ground_unit(wunit) && wunit->owner == vunit->owner) {
	    struct city *wcity =
		find_closest_owned_city(unit_owner(wunit), x, y, FALSE, NULL);
 	    if (wcity)
 	      teleport_unit_to_city(wunit, wcity, 0, verbose);
 	    else
 	      disband_stack_conflict_unit(wunit, verbose);
 	    goto START;
 	  }
 	} unit_list_iterate_end; /* End of find a unit from that player to disband*/
      }
    } unit_list_iterate_end; /* End of find a player */
  }
}

/**************************************************************************
...
**************************************************************************/
static bool is_airunit_refuel_point(int x, int y, struct player *pplayer,
				   Unit_Type_id type, bool unit_is_on_tile)
{
  struct player_tile *plrtile = map_get_player_tile(x, y, pplayer);

  if ((is_allied_city_tile(map_get_tile(x, y), pplayer)
       && !is_non_allied_unit_tile(map_get_tile(x, y), pplayer))
      || (contains_special(plrtile->special, S_AIRBASE)
	  && !is_non_allied_unit_tile(map_get_tile(x, y), pplayer)))
    return TRUE;

  if (unit_type_flag(type, F_MISSILE)) {
    int cap = missile_carrier_capacity(x, y, pplayer, FALSE);
    if (unit_is_on_tile)
      cap++;
    return cap>0;
  } else {
    int cap = airunit_carrier_capacity(x, y, pplayer, FALSE);
    if (unit_is_on_tile)
      cap++;
    return cap>0;
  }
}

/**************************************************************************
...
**************************************************************************/
void upgrade_unit(struct unit *punit, Unit_Type_id to_unit)
{
  struct player *pplayer = unit_owner(punit);
  int range;

  /* save old vision range */
  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    range = get_watchtower_vision(punit);
  else
    range = unit_type(punit)->vision_range;

  if (punit->hp==unit_type(punit)->hp) {
    punit->hp=get_unit_type(to_unit)->hp;
  }

  conn_list_do_buffer(&pplayer->connections);
  punit->type = to_unit;

  /* apply new vision range */
  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    unfog_area(pplayer, punit->x, punit->y, get_watchtower_vision(punit));
  else
    unfog_area(pplayer, punit->x, punit->y,
	       get_unit_type(to_unit)->vision_range);

  fog_area(pplayer,punit->x,punit->y,range);

  send_unit_info(NULL, punit);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
 creates a unit, and set it's initial values, and put it into the right 
 lists.
**************************************************************************/

/* This is a wrapper */

struct unit *create_unit(struct player *pplayer, int x, int y, Unit_Type_id type,
			 bool make_veteran, int homecity_id, int moves_left)
{
  return create_unit_full(pplayer,x,y,type,make_veteran,homecity_id,moves_left,-1);
}

/* This is the full call.  We don't want to have to change all other calls to
   this function to ensure the hp are set */
struct unit *create_unit_full(struct player *pplayer, int x, int y,
			      Unit_Type_id type, bool make_veteran, int homecity_id,
			      int moves_left, int hp_left)
{
  struct unit *punit;
  struct city *pcity;
  punit=fc_calloc(1,sizeof(struct unit));

  punit->type=type;
  punit->id=get_next_id_number();
  idex_register_unit(punit);
  punit->owner=pplayer->player_no;

  CHECK_MAP_POS(x, y);
  punit->x = x;
  punit->y = y;

  punit->goto_dest_x=0;
  punit->goto_dest_y=0;
  
  pcity=find_city_by_id(homecity_id);
  punit->veteran=make_veteran;
  punit->homecity=homecity_id;

  if(hp_left == -1)
    punit->hp=unit_type(punit)->hp;
  else
    punit->hp = hp_left;
  set_unit_activity(punit, ACTIVITY_IDLE);

  punit->upkeep=0;
  punit->upkeep_food=0;
  punit->upkeep_gold=0;
  punit->unhappiness=0;

  /* 
     See if this is a spy that has been moved (corrupt and therefore unable 
     to establish an embassy.
  */
  if(moves_left != -1 && unit_flag(punit, F_SPY))
    punit->foul = TRUE;
  else
    punit->foul = FALSE;
  
  punit->fuel=unit_type(punit)->fuel;
  punit->ai.control = FALSE;
  punit->ai.ai_role = AIUNIT_NONE;
  punit->ai.ferryboat = 0;
  punit->ai.passenger = 0;
  punit->ai.bodyguard = 0;
  punit->ai.charge = 0;
  unit_list_insert(&pplayer->units, punit);
  unit_list_insert(&map_get_tile(x, y)->units, punit);
  if (pcity) {
    unit_list_insert(&pcity->units_supported, punit);
    assert(city_owner(pcity) == pplayer);
  }
  punit->bribe_cost=-1;		/* flag value */
  if(moves_left < 0)  
    punit->moves_left=unit_move_rate(punit);
  else
    punit->moves_left=moves_left;

  /* Assume that if moves_left<0 then the unit is "fresh",
     and not moved; else the unit has had something happen
     to it (eg, bribed) which we treat as equivalent to moved.
     (Otherwise could pass moved arg too...)  --dwp
  */
  punit->moved = (moves_left>=0);

  /* Probably not correct when unit changed owner (e.g. bribe) */
  punit->paradropped = FALSE;

  if( is_barbarian(pplayer) )
    punit->fuel = BARBARIAN_LIFE;

  /* ditto for connecting units */
  punit->connecting = FALSE;

  punit->transported_by = -1;
  punit->pgr = NULL;

  if (map_has_special(x, y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    unfog_area(pplayer, punit->x, punit->y, get_watchtower_vision(punit));
  else
    unfog_area(pplayer, x, y, unit_type(punit)->vision_range);

  send_unit_info(NULL, punit);
  maybe_make_first_contact(x, y, unit_owner(punit));
  wakeup_neighbor_sentries(punit);

  /* The unit may have changed the available tiles in nearby cities. */
  map_city_radius_iterate(x, y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity) {
      update_city_tile_status_map(pcity, x, y);
    }
  } map_city_radius_iterate_end;

  /* Refresh the unit's homecity. */
  {
    struct city *pcity = find_city_by_id(homecity_id);
    if (pcity) {
      assert(city_owner(pcity) == pplayer);
      city_refresh(pcity);
      send_city_info(pplayer, pcity);
    }
  }

  sync_cities();

  return punit;
}

/**************************************************************************
We remove the unit and see if it's disapperance have affected the homecity
and the city it was in.
**************************************************************************/
static void server_remove_unit(struct unit *punit)
{
  struct packet_generic_integer packet;
  struct city *pcity = map_get_city(punit->x, punit->y);
  struct city *phomecity = find_city_by_id(punit->homecity);
  int punit_x = punit->x, punit_y = punit->y;

  /* Since settlers plot in new cities in the minimap before they
     are built, so that no two settlers head towards the same city
     spot, we need to ensure this reservation is cleared should
     the settler die on the way. */
  if (unit_owner(punit)->ai.control 
      && punit->ai.ai_role == AIUNIT_AUTO_SETTLER) {
    if (normalize_map_pos(&punit->goto_dest_x, &punit->goto_dest_y)) {
      remove_city_from_minimap(punit->goto_dest_x, punit->goto_dest_y);
    }
  }

  remove_unit_sight_points(punit);

  if (punit->pgr) {
    free(punit->pgr->pos);
    free(punit->pgr);
    punit->pgr = NULL;
  }

  packet.value = punit->id;
  players_iterate(pplayer) {
    if (map_get_known_and_seen(punit_x, punit_y, pplayer)) {
      lsend_packet_generic_integer(&pplayer->connections, PACKET_REMOVE_UNIT,
				   &packet);
    }
  } players_iterate_end;

  game_remove_unit(punit);
  punit = NULL;

  /* This unit may have blocked tiles of adjacent cities. Update them. */
  map_city_radius_iterate(punit_x, punit_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity) {
      update_city_tile_status_map(pcity, punit_x, punit_y);
    }
  } map_city_radius_iterate_end;
  sync_cities();

  if (phomecity) {
    city_refresh(phomecity);
    send_city_info(city_owner(phomecity), phomecity);
  }
  if (pcity && pcity != phomecity) {
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
  }
}

/**************************************************************************
this is a highlevel routine
Remove the unit, and passengers if it is a carrying any.
Remove the _minimum_ number, eg there could be another boat on the square.
Parameter iter, if non-NULL, should be an iterator for a unit list,
and if it points to a unit which we wipe, we advance it first to
avoid dangling pointers.
NOTE: iter should not be an iterator for the map units list, but
city supported, or player units, is ok.
**************************************************************************/
void wipe_unit_spec_safe(struct unit *punit, struct genlist_iterator *iter,
			 bool wipe_cargo)
{
  /* No need to remove air units as they can still fly away */
  if (is_ground_units_transport(punit)
      && map_get_terrain(punit->x, punit->y)==T_OCEAN
      && wipe_cargo) {
    int x = punit->x;
    int y = punit->y;

    int capacity =
	ground_unit_transporter_capacity(x, y, unit_owner(punit));
    capacity -= get_transporter_capacity(punit);

    unit_list_iterate(map_get_tile(x, y)->units, pcargo) {
      if (capacity >= 0)
	break;
      if (is_ground_unit(pcargo)) {
	if (iter && ((struct unit*)ITERATOR_PTR((*iter))) == pcargo) {
	  freelog(LOG_DEBUG, "iterating over %s in wipe_unit_safe",
		  unit_name(pcargo->type));
	  ITERATOR_NEXT((*iter));
	}
	notify_player_ex(unit_owner(punit), x, y, E_UNIT_LOST,
			 _("Game: %s lost when %s was lost."),
			 unit_type(pcargo)->name,
			 unit_type(punit)->name);
	gamelog(GAMELOG_UNITL, _("%s lose %s when %s lost"),
		get_nation_name_plural(unit_owner(punit)->nation),
		unit_type(pcargo)->name, unit_type(punit)->name);
	server_remove_unit(pcargo);
	capacity++;
      }
    } unit_list_iterate_end;
  }

  server_remove_unit(punit);
}

/**************************************************************************
...
**************************************************************************/

void wipe_unit_safe(struct unit *punit, struct genlist_iterator *iter){
  wipe_unit_spec_safe(punit, iter, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct unit *punit)
{
  wipe_unit_safe(punit, NULL);
}

/**************************************************************************
this is a highlevel routine
the unit has been killed in combat => all other units on the
tile dies unless ...
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit)
{
  struct city   *incity    = map_get_city(punit->x, punit->y);
  struct player *pplayer   = unit_owner(punit);
  struct player *destroyer = unit_owner(pkiller);
  char *loc_str = get_location_str_in(pplayer, punit->x, punit->y);
  int num_killed[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int ransom, unitcount = 0;
  
  /* barbarian leader ransom hack */
  if( is_barbarian(pplayer) && unit_has_role(punit->type, L_BARBARIAN_LEADER)
      && (unit_list_size(&(map_get_tile(punit->x, punit->y)->units)) == 1)
      && (is_ground_unit(pkiller) || is_heli_unit(pkiller)) ) {
    ransom = (pplayer->economic.gold >= 100)?100:pplayer->economic.gold;
    notify_player_ex(destroyer, pkiller->x, pkiller->y, E_UNIT_WIN_ATT,
		     _("Game: Barbarian leader captured, %d gold ransom paid."),
                     ransom);
    destroyer->economic.gold += ransom;
    pplayer->economic.gold -= ransom;
    send_player_info(destroyer, NULL);   /* let me see my new gold :-) */
    unitcount = 1;
  }

  if (unitcount == 0) {
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, vunit)
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(vunit)))
	unitcount++;
    unit_list_iterate_end;
  }

  if( (incity) ||
      map_has_special(punit->x, punit->y, S_FORTRESS) ||
      map_has_special(punit->x, punit->y, S_AIRBASE) ||
      unitcount == 1) {
    notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		     _("Game: %s lost to an attack by %s's %s%s."),
		     unit_type(punit)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);
    gamelog(GAMELOG_UNITL, _("%s lose %s to the %s"),
	    get_nation_name_plural(pplayer->nation), unit_type(punit)->name,
	    get_nation_name_plural(destroyer->nation));

    wipe_unit(punit);
  } else { /* unitcount > 1 */
    int i;
    if (!(unitcount > 1)) {
      freelog(LOG_FATAL, "Error in kill_unit, unitcount is %i", unitcount);
      abort();
    }
    /* initialize */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      num_killed[i] = 0;
    }

    /* count killed units */
    unit_list_iterate((map_get_tile(punit->x ,punit->y)->units), vunit)
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(vunit)))
	num_killed[vunit->owner]++;
    unit_list_iterate_end;

    /* inform the owners */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      if (num_killed[i]>0) {
	notify_player_ex(get_player(i), punit->x, punit->y, E_UNIT_LOST,
			 PL_("Game: You lost %d unit to an attack "
			     "from %s's %s%s.",
			     "Game: You lost %d units to an attack "
			     "from %s's %s%s.",
			     num_killed[i]), num_killed[i],
			 destroyer->name, unit_name(pkiller->type),
			 loc_str);
      }
    }

    /* remove the units */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(punit2))) {
	notify_player_ex(unit_owner(punit2), 
			 punit2->x, punit2->y, E_UNIT_LOST,
			 _("Game: %s lost to an attack"
			   " from %s's %s."),
			 unit_type(punit2)->name, destroyer->name,
			 unit_name(pkiller->type));
	gamelog(GAMELOG_UNITL, _("%s lose %s to the %s"),
		get_nation_name_plural(unit_owner(punit2)->nation),
		unit_type(punit2)->name,
		get_nation_name_plural(destroyer->nation));
	wipe_unit_spec_safe(punit2, NULL, FALSE);
      }
    }
    unit_list_iterate_end;
  }
}

/**************************************************************************
...
**************************************************************************/
void package_unit(struct unit *punit, struct packet_unit_info *packet,
		  bool carried, bool select_it, enum unit_info_use packet_use,
		  int info_city_id, bool new_serial_num)
{
  static unsigned int serial_num = 0;

  /* a 16-bit unsigned number, never zero */
  if (new_serial_num) {
    serial_num = (serial_num + 1) & 0xFFFF;
    if (serial_num == 0)
      serial_num++;
  }

  packet->id = punit->id;
  packet->owner = punit->owner;
  packet->x = punit->x;
  packet->y = punit->y;
  packet->homecity = punit->homecity;
  packet->veteran = punit->veteran;
  packet->type = punit->type;
  packet->movesleft = punit->moves_left;
  packet->hp = punit->hp / game.firepower_factor;
  packet->activity = punit->activity;
  packet->activity_count = punit->activity_count;
  packet->unhappiness = punit->unhappiness;
  packet->upkeep = punit->upkeep;
  packet->upkeep_food = punit->upkeep_food;
  packet->upkeep_gold = punit->upkeep_gold;
  packet->ai = punit->ai.control;
  packet->fuel = punit->fuel;
  packet->goto_dest_x = punit->goto_dest_x;
  packet->goto_dest_y = punit->goto_dest_y;
  packet->activity_target = punit->activity_target;
  packet->paradropped = punit->paradropped;
  packet->connecting = punit->connecting;
  packet->carried = carried;
  packet->select_it = select_it;
  packet->packet_use = packet_use;
  packet->info_city_id = info_city_id;
  packet->serial_num = serial_num;
}

/**************************************************************************
  Send the unit into to those connections in dest which can see the units
  position, or the specified (x,y) (if different).
  Eg, use x and y as where the unit came from, so that the info can be
  sent if the other players can see either the target or destination tile.
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info_to_onlookers(struct conn_list *dest, struct unit *punit,
				 int x, int y, bool carried, bool select_it)
{
  struct packet_unit_info info;

  if (!dest) dest = &game.game_connections;
  
  package_unit(punit, &info, carried, select_it,
	       UNIT_INFO_IDENTITY, FALSE, FALSE);

  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (!pplayer && !pconn->observer) continue;
    if (!pplayer || ((map_get_known_and_seen(info.x, info.y, pplayer)
		      || map_get_known_and_seen(x, y, pplayer))
		     && player_can_see_unit(pplayer, punit))) {
      send_packet_unit_info(pconn, &info);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  struct conn_list *conn_dest = (dest ? &dest->connections
				 : &game.game_connections);
  send_unit_info_to_onlookers(conn_dest, punit, punit->x, punit->y, FALSE, FALSE);
}

/**************************************************************************
  For each specified connections, send information about all the units
  known to that player/conn.
**************************************************************************/
void send_all_known_units(struct conn_list *dest)
{
  int p;
  
  conn_list_do_buffer(dest);
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    if (!pconn->player && !pconn->observer) {
      continue;
    }
    for(p=0; p<game.nplayers; p++) { /* send the players units */
      struct player *unitowner = &game.players[p];
      unit_list_iterate(unitowner->units, punit) {
	if (!pplayer
	    || map_get_known_and_seen(punit->x, punit->y, pplayer)) {
	  send_unit_info_to_onlookers(&pconn->self, punit,
				      punit->x, punit->y, FALSE, FALSE);
	}
      }
      unit_list_iterate_end;
    }
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
  flush_packets();
}






/**************************************************************************
For all units which are transported by the given unit and that are
currently idle, sentry them.
**************************************************************************/
static void sentry_transported_idle_units(struct unit *ptrans)
{
  int x = ptrans->x;
  int y = ptrans->y;
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(ptile->units, pcargo) {
    if (pcargo->transported_by == ptrans->id
	&& pcargo->id != ptrans->id
	&& pcargo->activity == ACTIVITY_IDLE) {
      pcargo->activity = ACTIVITY_SENTRY;
      send_unit_info(unit_owner(pcargo), pcargo);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
 nuke a square
 1) remove all units on the square
 2) half the size of the city on the square
 if it isn't a city square or an ocean square then with 50% chance 
 add some fallout, then notify the client about the changes.
**************************************************************************/
static void do_nuke_tile(struct player *pplayer, int x, int y)
{
  struct city *pcity = map_get_city(x, y);

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    notify_player_ex(unit_owner(punit),
		     x, y, E_UNIT_LOST,
		     _("Game: Your %s was nuked by %s."),
		     unit_name(punit->type),
		     pplayer == unit_owner(punit) ? _("yourself") : pplayer->name);
    if (unit_owner(punit) != pplayer) {
      notify_player_ex(pplayer,
		       x, y, E_UNIT_WIN,
		       _("Game: %s's %s was nuked."),
		       unit_owner(punit)->name,
		       unit_name(punit->type));
    }
    wipe_unit_spec_safe(punit, NULL, FALSE);
  } unit_list_iterate_end;

  if (pcity) {
    notify_player_ex(city_owner(pcity),
		     x, y, E_CITY_NUKED,
		     _("Game: %s was nuked by %s."),
		     pcity->name,
		     pplayer == city_owner(pcity) ? _("yourself") : pplayer->name);

    if (city_owner(pcity) != pplayer) {
      notify_player_ex(pplayer,
		       x, y, E_CITY_NUKED,
		       _("Game: You nuked %s."),
		       pcity->name);
    }

    if (pcity->size > 1) { /* size zero cities are ridiculous -- Syela */
      pcity->size /= 2;
      auto_arrange_workers(pcity);
      send_city_info(NULL, pcity);
    }
  }

  if (map_get_terrain(x, y) != T_OCEAN && myrand(2) == 1) {
    if (game.rgame.nuke_contamination == CONTAMINATION_POLLUTION) {
      if (!map_has_special(x, y, S_POLLUTION)) {
	map_set_special(x, y, S_POLLUTION);
	send_tile_info(NULL, x, y);
      }
    } else {
      if (!map_has_special(x, y, S_FALLOUT)) {
	map_set_special(x, y, S_FALLOUT);
	send_tile_info(NULL, x, y);
      }
    }
  }
}

/**************************************************************************
  nuke all the squares in a 3x3 square around the center of the explosion
  pplayer is the player that caused the explosion.
**************************************************************************/
void do_nuclear_explosion(struct player *pplayer, int x, int y)
{
  square_iterate(x, y, 1, x1, y1) {
    do_nuke_tile(pplayer, x1, y1);
  } square_iterate_end;

  notify_conn_ex(&game.game_connections, x, y, E_NUKE,
		 _("Game: %s detonated a nuke!"), pplayer->name);
}

/**************************************************************************
Move the unit if possible 
  if the unit has less moves than it costs to enter a square, we roll the dice:
  1) either succeed
  2) or have it's moves set to 0
  a unit can always move atleast once
**************************************************************************/
bool try_move_unit(struct unit *punit, int dest_x, int dest_y) 
{
  if (myrand(1+map_move_cost(punit, dest_x, dest_y))>punit->moves_left &&
      punit->moves_left<unit_move_rate(punit)) {
    punit->moves_left=0;
    send_unit_info(unit_owner(punit), punit);
  }
  return punit->moves_left > 0;
}

/**************************************************************************
  go by airline, if both cities have an airport and neither has been used this
  turn the unit will be transported by it and have it's moves set to 0
**************************************************************************/
bool do_airline(struct unit *punit, struct city *city2)
{
  struct city *city1;
  int src_x = punit->x;
  int src_y = punit->y;

  if (!(city1=map_get_city(src_x, src_y)))
    return FALSE;
  if (!unit_can_airlift_to(punit, city2))
    return FALSE;
  city1->airlift = FALSE;
  city2->airlift = FALSE;

  notify_player_ex(unit_owner(punit), city2->x, city2->y, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));

  move_unit(punit, city2->x, city2->y, FALSE, FALSE, punit->moves_left);

  /* airlift fields have changed. */
  send_city_info(city_owner(city1), city1);
  send_city_info(city_owner(city2), city2);

  return TRUE;
}

/**************************************************************************
Returns whether the drop was made or not. Note that it also returns 1 in
the case where the drop was succesfull, but the unit was killed by
barbarians in a hut
**************************************************************************/
bool do_paradrop(struct unit *punit, int dest_x, int dest_y)
{
  if (!unit_flag(punit, F_PARATROOPERS)) {
    notify_player_ex(unit_owner(punit), punit->x, punit->y, E_NOEVENT,
		     _("Game: This unit type can not be paradropped."));
    return FALSE;
  }

  if (!can_unit_paradrop(punit))
    return FALSE;

  if (!map_get_known(dest_x, dest_y, unit_owner(punit))) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: The destination location is not known."));
    return FALSE;
  }

  if (map_get_player_tile(dest_x, dest_y, unit_owner(punit))->terrain == T_OCEAN) {
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		     _("Game: Cannot paradrop into ocean."));
    return FALSE;    
  }

  {
    int range = unit_type(punit)->paratroopers_range;
    int distance = real_map_distance(punit->x, punit->y, dest_x, dest_y);
    if (distance > range) {
      notify_player_ex(unit_owner(punit), dest_x, dest_y, E_NOEVENT,
		       _("Game: The distance to the target (%i) "
			 "is greater than the unit's range (%i)."),
		       distance, range);
      return FALSE;
    }
  }

  if (map_get_terrain(dest_x, dest_y) == T_OCEAN) {
    int srange = unit_type(punit)->vision_range;
    show_area(unit_owner(punit), dest_x, dest_y, srange);

    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_UNIT_LOST,
		     _("Game: Your %s paradropped into the ocean "
		       "and was lost."),
		     unit_type(punit)->name);
    server_remove_unit(punit);
    return TRUE;
  }

  if (is_non_allied_unit_tile
      (map_get_tile(dest_x, dest_y), unit_owner(punit))) {
    int srange = unit_type(punit)->vision_range;
    show_area(unit_owner(punit), dest_x, dest_y, srange);
    notify_player_ex(unit_owner(punit), dest_x, dest_y, E_UNIT_LOST_ATT,
		     _("Game: Your %s was killed by enemy units at the "
		       "paradrop destination."),
		     unit_type(punit)->name);
    server_remove_unit(punit);
    return TRUE;
  }

  /* All ok */
  {
    int move_cost = unit_type(punit)->paratroopers_mr_sub;
    punit->paradropped = TRUE;
    return move_unit(punit, dest_x, dest_y, FALSE, FALSE, move_cost);
  }
}


/**************************************************************************
  Get gold from entering a hut.
**************************************************************************/
static void hut_get_gold(struct unit *punit, int cred)
{
  struct player *pplayer = unit_owner(punit);
  notify_player_ex(pplayer, punit->x, punit->y, E_HUT_GOLD,
		   _("Game: You found %d gold."), cred);
  pplayer->economic.gold += cred;
}

/**************************************************************************
  Get a tech from entering a hut.
**************************************************************************/
static void hut_get_tech(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  int res_ed, res_ing;
  Tech_Type_id new_tech;
  const char *tech_name;
  
  /* Save old values, choose tech, then restore old values: */
  res_ed = pplayer->research.bulbs_researched;
  res_ing = pplayer->research.researching;
  
  choose_random_tech(pplayer);
  new_tech = pplayer->research.researching;
  
  pplayer->research.bulbs_researched = res_ed;
  pplayer->research.researching = res_ing;

  tech_name = get_tech_name(pplayer, new_tech);
  notify_player_ex(pplayer, punit->x, punit->y, E_HUT_TECH,
		   _("Game: You found %s in ancient scrolls of wisdom."),
		   tech_name);
  gamelog(GAMELOG_TECH, _("%s discover %s (Hut)"),
	  get_nation_name_plural(pplayer->nation), tech_name);
  notify_embassies(pplayer, NULL, _("Game: The %s have aquired %s"
				    " from ancient scrolls of wisdom."),
		   get_nation_name_plural(pplayer->nation), tech_name);

  do_free_cost(pplayer);
  if (!is_future_tech(new_tech)) {
    found_new_tech(pplayer, new_tech, FALSE, TRUE);
  } else {
    found_new_future_tech(pplayer);
  }
}

/**************************************************************************
  Get a mercenary unit from entering a hut.
**************************************************************************/
static void hut_get_mercenaries(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  
  notify_player_ex(pplayer, punit->x, punit->y, E_HUT_MERC,
		   _("Game: A band of friendly mercenaries joins your cause."));
  (void) create_unit(pplayer, punit->x, punit->y,
		     find_a_unit_type(L_HUT, L_HUT_TECH), FALSE,
		     punit->homecity, -1);
}

/**************************************************************************
  Get barbarians from hut, unless close to a city.
  Unit may die: returns 1 if unit is alive after, or 0 if it was killed.
**************************************************************************/
static bool hut_get_barbarians(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  bool ok = TRUE;

  if (city_exists_within_city_radius(punit->x, punit->y, TRUE)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_BARB_CITY_NEAR,
		     _("Game: An abandoned village is here."));
  }
  else {
    /* save coords and type in case unit dies */
    int punit_x = punit->x;
    int punit_y = punit->y;
    Unit_Type_id type = punit->type;

    ok = unleash_barbarians(pplayer, punit_x, punit_y);

    if (ok) {
      notify_player_ex(pplayer, punit_x, punit_y, E_HUT_BARB,
		       _("Game: You have unleashed a horde of barbarians!"));
    } else {
      notify_player_ex(pplayer, punit_x, punit_y, E_HUT_BARB_KILLED,
		       _("Game: Your %s has been killed by barbarians!"),
		       unit_name(type));
    }
  }
  return ok;
}

/**************************************************************************
  Get new city from hut, or settlers (nomads) if terrain is poor.
**************************************************************************/
static void hut_get_city(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  
  if (city_can_be_built_here(punit->x, punit->y)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_CITY,
		     _("Game: You found a friendly city."));
    create_city(pplayer, punit->x, punit->y,
		city_name_suggestion(pplayer, punit->x, punit->y));
  } else {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_SETTLER,
		     _("Game: Friendly nomads are impressed by you,"
		       " and join you."));
    (void) create_unit(pplayer, punit->x, punit->y, get_role_unit(F_CITIES,0),
		FALSE, punit->homecity, -1);
  }
}

/**************************************************************************
Return 1 if unit is alive, and 0 if it was killed
**************************************************************************/
static bool unit_enter_hut(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  bool ok = TRUE;

  if (game.rgame.hut_overflight==OVERFLIGHT_NOTHING && is_air_unit(punit)) {
    return ok;
  }

  map_clear_special(punit->x, punit->y, S_HUT);
  send_tile_info(NULL, punit->x, punit->y);

  if (game.rgame.hut_overflight==OVERFLIGHT_FRIGHTEN && is_air_unit(punit)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Your overflight frightens the tribe;"
		       " they scatter in terror."));
    return ok;
  }

  switch (myrand(12)) {
  case 0:
    hut_get_gold(punit, 25);
    break;
  case 1: case 2: case 3:
    hut_get_gold(punit, 50);
    break;
  case 4:
    hut_get_gold(punit, 100);
    break;
  case 5: case 6: case 7:
    hut_get_tech(punit);
    break;
  case 8: case 9:
    hut_get_mercenaries(punit);
    break;
  case 10:
    ok = hut_get_barbarians(punit);
    break;
  case 11:
    hut_get_city(punit);
    break;
  }
  send_player_info(pplayer, pplayer);       /* eg, gold */
  return ok;
}

/**************************************************************************
Assigns units on ptrans' tile to ptrans if they should be. This is done by
setting their transported_by fields to the id of ptrans.
Checks a zillion things, some from situations that should never happen.
First drop all previously assigned units that do not fit on the transport.
If on land maybe pick up some extra units (decided by take_from_land variable)

This function is getting a bit larger and ugly. Perhaps it would be nicer
if it was recursive!?

FIXME: If in the open (not city), and leaving with only those units that are
       already assigned to us would strand some units try to reassign the
       transports. This reassign sometimes makes more changes than it needs to.

       Groundunit ground unit transports moving to and from ships never take
       units with them. This is ok, but not always practical.
**************************************************************************/
void assign_units_to_transporter(struct unit *ptrans, bool take_from_land)
{
  int x = ptrans->x;
  int y = ptrans->y;
  int playerid = ptrans->owner;
  int capacity = get_transporter_capacity(ptrans);
  struct tile *ptile = map_get_tile(x, y);

  /*** FIXME: We kludge AI compatability problems with the new code away here ***/
  if (is_sailing_unit(ptrans)
      && is_ground_units_transport(ptrans)
      && unit_owner(ptrans)->ai.control) {
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->owner == playerid) {
	pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    unit_list_iterate(ptile->units, pcargo) {
      if ((ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	  && capacity > 0
	  && is_ground_unit(pcargo)
	  && pcargo->owner == playerid) {
	pcargo->transported_by = ptrans->id;
	capacity--;
      }
    } unit_list_iterate_end;
    return;
  }

  /*** Ground units transports first ***/
  if (is_ground_units_transport(ptrans)) {
    int available =
	ground_unit_transporter_capacity(x, y, unit_owner(ptrans));

    /* See how many units are dedicated to this transport, and remove extra units */
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->transported_by == ptrans->id) {
	if (is_ground_unit(pcargo)
	    && capacity > 0
	    && (ptile->terrain == T_OCEAN || pcargo->activity == ACTIVITY_SENTRY)
	    && (pcargo->owner == playerid
                || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))
	    && pcargo->id != ptrans->id
	    && !(is_ground_unit(ptrans)
            && (ptile->terrain == T_OCEAN || is_ground_units_transport(pcargo))))
	  capacity--;
	else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are on an ocean tile. All units must get a transport **/
    if (ptile->terrain == T_OCEAN) {
      /* While the transport is not full and units will strand if we don't take
	 them with we us dedicate them to this transport. */
      if (available - capacity < 0 && !is_ground_unit(ptrans)) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && (pcargo->owner == playerid
                  || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    } else { /** We are on a land tile **/
      /* If ordered to do so we take extra units with us, provided they
	 are not already commited to another transporter on the tile */
      if (take_from_land) {
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_ground_unit(pcargo)
	      && pcargo->transported_by != ptrans->id
	      && pcargo->activity == ACTIVITY_SENTRY
	      && pcargo->id != ptrans->id
	      && pcargo->owner == playerid
	      && !(is_ground_unit(ptrans)
      	      && (ptile->terrain == T_OCEAN || is_ground_units_transport(pcargo)))) {
	    bool has_trans = FALSE;

	    unit_list_iterate(ptile->units, ptrans2) {
	      if (ptrans2->id == pcargo->transported_by)
		has_trans = TRUE;
	    } unit_list_iterate_end;
	    if (!has_trans) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  }
	} unit_list_iterate_end;
      }
    }
    return;
    /*** Allocate air and missile units ***/
  } else if (is_air_units_transport(ptrans)) {
    struct player_tile *plrtile =
	map_get_player_tile(x, y, unit_owner(ptrans));
    bool is_refuel_point =
	is_allied_city_tile(map_get_tile(x, y), unit_owner(ptrans))
	|| (contains_special(plrtile->special, S_AIRBASE)
	    && !is_non_allied_unit_tile(map_get_tile(x, y),
					unit_owner(ptrans)));
    bool missiles_only = unit_flag(ptrans, F_MISSILE_CARRIER)
      && !unit_flag(ptrans, F_CARRIER);

    /* Make sure we can transport the units marked as being transported by ptrans */
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by) {
	if ((pcargo->owner == playerid
             || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))
	    && pcargo->id != ptrans->id
	    && (!is_sailing_unit(pcargo))
	    && (unit_flag(pcargo, F_MISSILE) || !missiles_only)
	    && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)
	    && (capacity > 0)) {
	  if (is_air_unit(pcargo))
	    capacity--;
	  /* Ground units are handled below */
	} else
	  pcargo->transported_by = -1;
      }
    } unit_list_iterate_end;

    /** We are at a refuel point **/
    if (is_refuel_point) {
      unit_list_iterate(ptile->units, pcargo) {
	if (capacity == 0)
	  break;
	if (is_air_unit(pcargo)
	    && pcargo->id != ptrans->id
	    && pcargo->transported_by != ptrans->id
	    && pcargo->activity == ACTIVITY_SENTRY
	    && (unit_flag(pcargo, F_MISSILE) || !missiles_only)
	    && (pcargo->owner == playerid
                || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))) {
	  bool has_trans = FALSE;

	  unit_list_iterate(ptile->units, ptrans2) {
	    if (ptrans2->id == pcargo->transported_by)
	      has_trans = TRUE;
	  } unit_list_iterate_end;
	  if (!has_trans) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	}
      } unit_list_iterate_end;
    } else { /** We are in the open. All units must have a transport if possible **/
      int aircap = airunit_carrier_capacity(x, y, unit_owner(ptrans), TRUE);
      int miscap = missile_carrier_capacity(x, y, unit_owner(ptrans), TRUE);

      /* Not enough capacity. Take anything we can */
      if ((aircap < capacity || miscap < capacity)
	  && !(is_ground_unit(ptrans) && ptile->terrain == T_OCEAN)) {
	/* We first take nonmissiles, as missiles can be transported on anything,
	   but nonmissiles can not */
	if (!missiles_only) {
	  unit_list_iterate(ptile->units, pcargo) {
	    if (capacity == 0)
	      break;
	    if (is_air_unit(pcargo)
		&& pcargo->id != ptrans->id
		&& pcargo->transported_by != ptrans->id
		&& !unit_flag(pcargo, F_MISSILE)
		&& (pcargo->owner == playerid
                    || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))) {
	      capacity--;
	      pcargo->transported_by = ptrans->id;
	    }
	  } unit_list_iterate_end;
	}
	
	/* Just take anything there's left.
	   (which must be missiles if we have capacity left) */
	unit_list_iterate(ptile->units, pcargo) {
	  if (capacity == 0)
	    break;
	  if (is_air_unit(pcargo)
	      && pcargo->id != ptrans->id
	      && pcargo->transported_by != ptrans->id
	      && (!missiles_only || unit_flag(pcargo, F_MISSILE))
	      && (pcargo->owner == playerid
                  || pplayers_allied(unit_owner(pcargo), unit_owner(ptrans)))) {
	    capacity--;
	    pcargo->transported_by = ptrans->id;
	  }
	} unit_list_iterate_end;
      }
    }

    /** If any of the transported air units have land units on board take them with us **/
    {
      int totcap = 0;
      int available =
	  ground_unit_transporter_capacity(x, y, unit_owner(ptrans));
      struct unit_list trans2s;
      unit_list_init(&trans2s);

      unit_list_iterate(ptile->units, pcargo) {
	if (pcargo->transported_by == ptrans->id
	    && is_ground_units_transport(pcargo)
	    && is_air_unit(pcargo)) {
	  totcap += get_transporter_capacity(pcargo);
	  unit_list_insert(&trans2s, pcargo);
	}
      } unit_list_iterate_end;

      unit_list_iterate(ptile->units, pcargo2) {
	bool has_trans = FALSE;

	unit_list_iterate(trans2s, ptrans2) {
	  if (pcargo2->transported_by == ptrans2->id)
	    has_trans = TRUE;
	} unit_list_iterate_end;
	if (pcargo2->transported_by == ptrans->id)
	  has_trans = TRUE;

	if (has_trans
	    && is_ground_unit(pcargo2)) {
	  if (totcap > 0
	      && (ptile->terrain == T_OCEAN || pcargo2->activity == ACTIVITY_SENTRY)
	      && (pcargo2->owner == playerid
                  || pplayers_allied(unit_owner(pcargo2), unit_owner(ptrans)))
	      && pcargo2 != ptrans) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  } else
	    pcargo2->transported_by = -1;
	}
      } unit_list_iterate_end;

      /* Uh oh. Not enough space on the square we leave if we don't
	 take extra units with us */
      if (totcap > available && ptile->terrain == T_OCEAN) {
	unit_list_iterate(ptile->units, pcargo2) {
	  if (is_ground_unit(pcargo2)
	      && totcap > 0
	      && (pcargo2->owner == playerid
                  || pplayers_allied(unit_owner(pcargo2), unit_owner(ptrans)))
	      && pcargo2->transported_by != ptrans->id) {
	    pcargo2->transported_by = ptrans->id;
	    totcap--;
	  }
	} unit_list_iterate_end;
      }
    }
  } else {
    unit_list_iterate(ptile->units, pcargo) {
      if (ptrans->id == pcargo->transported_by)
	pcargo->transported_by = -1;
    } unit_list_iterate_end;
    freelog (LOG_VERBOSE, "trying to assign cargo to a non-transporter "
	     "of type %s at %i,%i",
	     get_unit_name(ptrans->type), ptrans->x, ptrans->y);
  }
}

/*****************************************************************
Will wake up any neighboring enemy sentry units or patrolling units
*****************************************************************/
static void wakeup_neighbor_sentries(struct unit *punit)
{
  /* There may be sentried units with a sightrange>3, but we don't
     wake them up if the punit is farther away than 3. */
  square_iterate(punit->x, punit->y, 3, x, y) {
    unit_list_iterate(map_get_tile(x, y)->units, penemy) {
      int range;
      enum unit_move_type move_type = unit_type(penemy)->move_type;
      enum tile_terrain_type terrain = map_get_terrain(x, y);

      if (map_has_special(x, y, S_FORTRESS)
	  && unit_profits_of_watchtower(penemy))
	range = get_watchtower_vision(penemy);
      else
	range = unit_type(penemy)->vision_range;
      
      if (!pplayers_allied(unit_owner(punit), unit_owner(penemy))
	  && penemy->activity == ACTIVITY_SENTRY
	  && map_get_known_and_seen(punit->x, punit->y, unit_owner(penemy))
	  && range >= real_map_distance(punit->x, punit->y, x, y)
	  && player_can_see_unit(unit_owner(penemy), punit)
	  /* on board transport; don't awaken */
	  && !(move_type == LAND_MOVING && terrain == T_OCEAN)) {
	set_unit_activity(penemy, ACTIVITY_IDLE);
	send_unit_info(NULL, penemy);
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  /* Wakeup patrolling units we bump into.
     We do not wakeup units further away than 3 squares... */
  square_iterate(punit->x, punit->y, 3, x, y) {
    unit_list_iterate(map_get_tile(x, y)->units, ppatrol) {
      if (punit != ppatrol
	  && ppatrol->activity == ACTIVITY_PATROL) {
	maybe_cancel_patrol_due_to_enemy(ppatrol);
      }
    } unit_list_iterate_end;
  } square_iterate_end;
}

/**************************************************************************
Does: 1) updates  the units homecity and the city it enters/leaves (the
         cities happiness varies). This also takes into account if the
	 unit enters/leaves a fortress.
      2) handles any huts at the units destination.
      3) awakes any sentried units on neightboring tiles.
      4) updates adjacent cities' unavailable tiles.

FIXME: Sometimes it is not neccesary to send cities because the goverment
       doesn't care if a unit is away or not.
**************************************************************************/
static void handle_unit_move_consequences(struct unit *punit, int src_x, int src_y,
					  int dest_x, int dest_y)
{
  struct city *fromcity = map_get_city(src_x, src_y);
  struct city *tocity = map_get_city(dest_x, dest_y);
  struct city *homecity = NULL;
  struct player *pplayer = unit_owner(punit);
  /*  struct government *g = get_gov_pplayer(pplayer);*/
  bool senthome = FALSE;

  if (punit->homecity != 0)
    homecity = find_city_by_id(punit->homecity);

  wakeup_neighbor_sentries(punit);
  maybe_make_first_contact(dest_x, dest_y, unit_owner(punit));

  if (tocity)
    handle_unit_enter_city(punit, tocity);

  /* We only do this for non-AI players to now make sure the AI turns
     doesn't take too long. Perhaps we should make a special refresh_city
     functions that only refreshed happiness. */
  if (!pplayer->ai.control) {
    /* might have changed owners or may be destroyed */
    tocity = map_get_city(dest_x, dest_y);

    if (tocity) { /* entering a city */
      if (tocity->owner == punit->owner) {
	if (tocity != homecity) {
	  city_refresh(tocity);
	  send_city_info(pplayer, tocity);
	}
      }

      if (homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      senthome = TRUE;
    }

    if (fromcity) { /* leaving a city */
      if (!senthome && homecity) {
	city_refresh(homecity);
	send_city_info(pplayer, homecity);
      }
      if (fromcity != homecity && fromcity->owner == punit->owner) {
	city_refresh(fromcity);
	send_city_info(pplayer, fromcity);
      }
      senthome = TRUE;
    }

    /* entering/leaving a fortress */
    if (map_has_special(dest_x, dest_y, S_FORTRESS)
	&& homecity
	&& is_friendly_city_near(unit_owner(punit), dest_x, dest_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }

    if (map_has_special(src_x, src_y, S_FORTRESS)
	&& homecity
	&& is_friendly_city_near(unit_owner(punit), src_x, src_y)
	&& !senthome) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }
  }


  /* The unit block different tiles of adjacent enemy cities before and
     after. Update the relevant cities. */

  /* First check cities near the source. */
  map_city_radius_iterate(src_x, src_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity) {
      update_city_tile_status_map(pcity, src_x, src_y);
    }
  } map_city_radius_iterate_end;
  /* Then check cities near the destination. */
  map_city_radius_iterate(dest_x, dest_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity) {
      update_city_tile_status_map(pcity, dest_x, dest_y);
    }
  } map_city_radius_iterate_end;
  sync_cities();
}

/**************************************************************************
Check if the units activity is legal for a move , and reset it if it isn't.
**************************************************************************/
static void check_unit_activity(struct unit *punit)
{
  if (punit->activity != ACTIVITY_IDLE
      && punit->activity != ACTIVITY_GOTO
      && punit->activity != ACTIVITY_SENTRY
      && punit->activity != ACTIVITY_EXPLORE
      && punit->activity != ACTIVITY_PATROL
      && !punit->connecting)
    set_unit_activity(punit, ACTIVITY_IDLE);
}

/**************************************************************************
Moves a unit. No checks whatsoever! This is meant as a practical function
for other functions, like do_airline, which do the checking themselves.
If you move a unit you should always use this function, as it also sets the
transported_by unit field correctly.
take_from_land is only relevant if you have set transport_units.
Note that the src and dest need not be adjacent.
**************************************************************************/
bool move_unit(struct unit *punit, int dest_x, int dest_y,
	      bool transport_units, bool take_from_land, int move_cost)
{
  int src_x = punit->x;
  int src_y = punit->y;
  int playerid = punit->owner;
  struct player *pplayer = get_player(playerid);
  struct tile *psrctile = map_get_tile(src_x, src_y);
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);

  CHECK_MAP_POS(dest_x, dest_y);

  conn_list_do_buffer(&pplayer->connections);

  if (punit->ai.ferryboat != 0) {
    struct unit *ferryboat;
    ferryboat = unit_list_find(&psrctile->units, punit->ai.ferryboat);
    if (ferryboat) {
      freelog(LOG_DEBUG, "%d disembarking from ferryboat %d",
	      punit->id, punit->ai.ferryboat);
      ferryboat->ai.passenger = 0;
      punit->ai.ferryboat = 0;
    }
  }
  /* A transporter should not take units with it when on an attack goto -- fisch */
  if ((punit->activity == ACTIVITY_GOTO) &&
      get_defender(punit, punit->goto_dest_x, punit->goto_dest_y) &&
      psrctile->terrain != T_OCEAN) {
    transport_units = FALSE;
  }

  /* A ground unit cannot hold units on an ocean tile */
  if (transport_units
      && is_ground_unit(punit)
      && pdesttile->terrain == T_OCEAN)
    transport_units = FALSE;

  /* Make sure we don't accidentally insert units into a transporters list */
  unit_list_iterate(pdesttile->units, pcargo) { 
    if (pcargo->transported_by == punit->id)
      pcargo->transported_by = -1;
  } unit_list_iterate_end;

  /* Transporting units. We first make a list of the units to be moved and
     then insert them again. The way this is done makes sure that the
     units stay in the same order. */
  if (get_transporter_capacity(punit) > 0 && transport_units) {
    struct unit_list cargo_units;

    /* First make a list of the units to be moved. */
    unit_list_init(&cargo_units);
    assign_units_to_transporter(punit, take_from_land);
    unit_list_iterate(psrctile->units, pcargo) {
      if (pcargo->transported_by == punit->id) {
	unit_list_unlink(&psrctile->units, pcargo);
	unit_list_insert(&cargo_units, pcargo);
      }
    } unit_list_iterate_end;

    /* Insert them again. */
    unit_list_iterate(cargo_units, pcargo) {
      unfog_area(unit_owner(pcargo), dest_x, dest_y, unit_type(pcargo)->vision_range);
      pcargo->x = dest_x;
      pcargo->y = dest_y;
      unit_list_insert(&pdesttile->units, pcargo);
      check_unit_activity(pcargo);
      send_unit_info_to_onlookers(NULL, pcargo, src_x, src_y, TRUE, FALSE);
      fog_area(unit_owner(pcargo), src_x, src_y, unit_type(pcargo)->vision_range);
      handle_unit_move_consequences(pcargo, src_x, src_y, dest_x, dest_y);
    } unit_list_iterate_end;
    unit_list_unlink_all(&cargo_units);
  }

  /* We first unfog the destination, then move the unit and send the
     move, and then fog the old territory. This means that the player
     gets a chance to see the newly explored territory while the
     client moves the unit, and both areas are visible during the
     move */

  /* Enhace vision if unit steps into a fortress */
  if (unit_profits_of_watchtower(punit)
      && tile_has_special(pdesttile, S_FORTRESS))
    unfog_area(pplayer, dest_x, dest_y, get_watchtower_vision(punit));
  else
    unfog_area(pplayer, dest_x, dest_y,
	       unit_type(punit)->vision_range);

  unit_list_unlink(&psrctile->units, punit);
  punit->x = dest_x;
  punit->y = dest_y;
  punit->moved = TRUE;
  punit->transported_by = -1;
  punit->moves_left = MAX(0, punit->moves_left - move_cost);
  unit_list_insert(&pdesttile->units, punit);
  check_unit_activity(punit);

  /* set activity to sentry if boarding a ship unless the unit is just 
     passing through the ship on its way somewhere else */
  if (is_ground_unit(punit)
      && pdesttile->terrain == T_OCEAN
      && !(pplayer->ai.control)
      && !(punit->activity == ACTIVITY_GOTO      /* if unit is GOTOing and the ship */ 
	   && (dest_x != punit->goto_dest_x      /* isn't the final destination */
	       || dest_y != punit->goto_dest_y)) /* then don't go to sleep */
      ) {
    set_unit_activity(punit, ACTIVITY_SENTRY);
  }
  send_unit_info_to_onlookers(NULL, punit, src_x, src_y, FALSE, FALSE);

  if (unit_profits_of_watchtower(punit)
      && tile_has_special(psrctile, S_FORTRESS))
    fog_area(pplayer, src_x, src_y, get_watchtower_vision(punit));
  else
    fog_area(pplayer, src_x, src_y,
	     unit_type(punit)->vision_range);

  handle_unit_move_consequences(punit, src_x, src_y, dest_x, dest_y);

  conn_list_do_unbuffer(&pplayer->connections);

  if (map_has_special(dest_x, dest_y, S_HUT))
    return unit_enter_hut(punit);
  else
    return TRUE;
}

/**************************************************************************
Maybe cancel the patrol as there is an enemy near.

If you modify the wakeup range you should change it in
wakeup_neighbor_sentries() too.
**************************************************************************/
static bool maybe_cancel_patrol_due_to_enemy(struct unit *punit)
{
  bool cancel = FALSE;
  int range;

  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    range = get_watchtower_vision(punit);
  else
    range = unit_type(punit)->vision_range;
  
  square_iterate(punit->x, punit->y, range, x, y) {
    struct unit *penemy =
	is_non_allied_unit_tile(map_get_tile(x, y), unit_owner(punit));
    if (penemy && player_can_see_unit(unit_owner(punit), penemy)) {
      cancel = TRUE;
    }
  } square_iterate_end;

  if (cancel) {
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
    notify_player_ex(unit_owner(punit), punit->x, punit->y, E_NOEVENT, 
		     _("Game: Your %s cancelled patrol order because it "
		       "encountered a foreign unit."), unit_name(punit->type));
  }

  return cancel;
}

/**************************************************************************
Moves a unit according to its pgr (goto or patrol order). If two consequetive
positions in the route is not adjacent it is assumed to be a goto. The unit
is put on idle if a move fails.
If the activity is ACTIVITY_PATROL the map positions are put back in the
route (at the end).  To avoid infinite loops on railroad we stop for this
turn when the unit is back where it started, eben if it have moves left.

If a patrolling unit came across an enemy we could make the patrolling unit
autoattack or just stop and wait for the owner to attack. It is also
debateable if units on goto should stop when they encountered an enemy
unit. Currently the unit continues if it can, or if it is blocked it stops.
**************************************************************************/
enum goto_result goto_route_execute(struct unit *punit)
{
  struct goto_route *pgr = punit->pgr;
  int index, x, y;
  bool res, last_tile;
  int patrol_stop_index = pgr->last_index;
  int unitid = punit->id;
  struct player *pplayer = unit_owner(punit);

  assert(pgr != NULL);
  while (TRUE) {
    freelog(LOG_DEBUG, "running a round\n");

    index = pgr->first_index;
    if (index == pgr->last_index) {
      free(punit->pgr);
      punit->pgr = NULL;
      if (punit->activity == ACTIVITY_GOTO) 
	/* the activity could already be SENTRY (if boarded a ship) 
	   -- leave it as it is then */
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
      return GR_ARRIVED;
    }
    x = pgr->pos[index].x; y = pgr->pos[index].y;
    freelog(LOG_DEBUG, "%i,%i -> %i,%i\n", punit->x, punit->y, x, y);

    if (punit->moves_left == 0) {
      return GR_OUT_OF_MOVEPOINTS;
    }

    if (punit->activity == ACTIVITY_PATROL
	&& maybe_cancel_patrol_due_to_enemy(punit)) {
      return GR_FAILED;
    }

    /* Move unit */
    last_tile = (((index + 1) % pgr->length) == (pgr->last_index));
    freelog(LOG_DEBUG, "handling\n");
    res = handle_unit_move_request(punit, x, y, FALSE, !last_tile);

    if (!player_find_unit_by_id(pplayer, unitid)) {
      return GR_DIED;
    }

    if (same_pos(punit->x, punit->y, x, y)) {
      /* We succeeded in moving one step forward */
      pgr->first_index = (pgr->first_index + 1) % pgr->length;
      if (punit->activity == ACTIVITY_PATROL) {
	/* When patroling we go in little circles; 
	 * done by reinserting points */
	pgr->pos[pgr->last_index].x = x;
	pgr->pos[pgr->last_index].y = y;
	pgr->last_index = (pgr->last_index + 1) % pgr->length;

	if (patrol_stop_index == pgr->first_index) {
	  freelog(LOG_DEBUG, "stopping because we ran a round\n");
	  return GR_ARRIVED;	/* don't patrol more than one round */
	}
	if (maybe_cancel_patrol_due_to_enemy(punit)) {
	  return GR_FAILED;
	}
      }
    }

    if (res && !same_pos(x, y, punit->x, punit->y)) {
      /*
       * unit is alive, moved alright, didn't arrive, has moves_left
       * --- what else can it be 
       */
      return GR_FOUGHT;
    }

    if (!res && punit->moves_left > 0) {
      freelog(LOG_DEBUG, "move idling\n");
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
      return GR_FAILED;
    }
  } /* end while*/
}

/**************************************************************************
...
**************************************************************************/
int get_watchtower_vision(struct unit *punit)
{
  int base_vision = unit_type(punit)->vision_range;

  assert(base_vision > 0);
  assert(game.watchtower_vision > 0);
  assert(game.watchtower_extra_vision >= 0);

  return MAX(base_vision,
	     MAX(game.watchtower_vision,
		 base_vision + game.watchtower_extra_vision));
}

/**************************************************************************
...
**************************************************************************/
bool unit_profits_of_watchtower(struct unit *punit)
{
  return (is_ground_unit(punit)
	  && player_knows_techs_with_flag(unit_owner(punit),
					  TF_WATCHTOWER));
}
