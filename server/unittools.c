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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "diplhand.h"
#include "gamelog.h"
#include "gotohand.h"
#include "mapgen.h"		/* assign_continent_numbers */
#include "maphand.h"
#include "plrhand.h"
#include "sernet.h"
#include "settlers.h"
#include "srv_main.h"
#include "unithand.h"

#include "aitools.h"
#include "aiunit.h"

#include "unittools.h"


static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void update_unit_activity(struct unit *punit);
static void wakeup_neighbor_sentries(struct unit *punit);
static void handle_leonardo(struct player *pplayer);

static void sentry_transported_idle_units(struct unit *ptrans);

static bool maybe_cancel_patrol_due_to_enemy(struct unit *punit);
static int hp_gain_coord(struct unit *punit);

static void put_unit_onto_transporter(struct unit *punit, struct unit *ptrans);
static void pull_unit_from_transporter(struct unit *punit,
				       struct unit *ptrans);

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
    die("No unit types in find_a_unit_type(%d,%d)!", role, role_tech);
  }
  return which[myrand(num)];
}

/**************************************************************************
  After a battle, after diplomatic aggression and after surviving trireme
  loss chance, this routine is called to decide whether or not the unit
  should become more experienced.

  There is a specified chance for it to happen, (+50% if player got SUNTZU)
  the chances are specified in the units.ruleset file.
**************************************************************************/
bool maybe_make_veteran(struct unit *punit)
{
  if (punit->veteran + 1 >= MAX_VET_LEVELS
      || unit_type(punit)->veteran[punit->veteran].name[0] == '\0'
      || unit_flag(punit, F_NO_VETERAN)) {
    return FALSE;
  }
  if (is_ground_unittype(punit->type)
      && player_owns_active_wonder(get_player(punit->owner), B_SUNTZU)) {
    if (myrand(100) < 1.5 * game.veteran_chance[punit->veteran]) {
      punit->veteran++;
      return TRUE;
    }
  } else {
    if (myrand(100) < game.veteran_chance[punit->veteran]) {
      punit->veteran++;
      return TRUE;
    }
  }

  return FALSE;  
}

/**************************************************************************
  This is the basic unit versus unit combat routine.
  1) ALOT of modifiers bonuses etc is added to the 2 units rates.
  2) If the attack is a bombardment, do rate attacks and don't kill the
     defender, then return.
  3) the combat loop, which continues until one of the units are dead
  4) the aftermath, the loser (and potentially the stack which is below it)
     is wiped, and the winner gets a chance of gaining veteran status
**************************************************************************/
void unit_versus_unit(struct unit *attacker, struct unit *defender,
		      bool bombard)
{
  int attackpower = get_total_attack_power(attacker,defender);
  int defensepower = get_total_defense_power(attacker,defender);

  int attack_firepower, defense_firepower;
  get_modified_firepower(attacker, defender,
			 &attack_firepower, &defense_firepower);

  freelog(LOG_VERBOSE, "attack:%d, defense:%d, attack firepower:%d, defense firepower:%d",
	  attackpower, defensepower, attack_firepower, defense_firepower);

  if (bombard) {
    int i;
    int rate = unit_type(attacker)->bombard_rate;

    for (i = 0; i < rate; i++) {
      if (myrand(attackpower+defensepower) >= defensepower) {
	defender->hp -= attack_firepower;
      }
    }

    /* Don't kill the target. */
    if (defender->hp <= 0) {
      defender->hp = 1;
    }
    return;
  }

  if (attackpower == 0) {
      attacker->hp=0; 
  } else if (defensepower == 0) {
      defender->hp=0;
  }
  while (attacker->hp>0 && defender->hp>0) {
    if (myrand(attackpower+defensepower) >= defensepower) {
      defender->hp -= attack_firepower;
    } else {
      attacker->hp -= defense_firepower;
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
Do Leonardo's Workshop upgrade(s). Select unit to upgrade by random. --Zamar
Now be careful not to strand units at sea with the Workshop. --dwp
****************************************************************************/
static void handle_leonardo(struct player *pplayer)
{
  int leonardo_variant;
	
  struct unit_list candidates;
  int candidate_to_upgrade=-1;

  int i;

  leonardo_variant = improvement_variant(B_LEONARDO);
	
  unit_list_init(&candidates);
	
  unit_list_iterate(pplayer->units, punit) {
    if (test_unit_upgrade(punit, TRUE) == UR_OK) {
      unit_list_insert(&candidates, punit);	/* Potential candidate :) */
    }
  } unit_list_iterate_end;
	
  if (unit_list_size(&candidates) == 0)
    return; /* We have Leonardo, but nothing to upgrade! */
	
  if (leonardo_variant == 0)
    candidate_to_upgrade=myrand(unit_list_size(&candidates));

  i=0;	
  unit_list_iterate(candidates, punit) {
    if (leonardo_variant != 0 || i == candidate_to_upgrade) {
      Unit_Type_id upgrade_type = can_upgrade_unittype(pplayer, punit->type);

      notify_player(pplayer,
            _("Game: %s has upgraded %s to %s%s."),
            improvement_types[B_LEONARDO].name,
            unit_type(punit)->name,
            get_unit_type(upgrade_type)->name,
            get_location_str_in(pplayer, punit->x, punit->y));
      punit->veteran = 0;
      assert(test_unit_upgrade(punit, TRUE) == UR_OK);
      upgrade_unit(punit, upgrade_type, TRUE);
    }
    i++;
  } unit_list_iterate_end;
	
  unit_list_unlink_all(&candidates);
}

/***************************************************************************
  Pay the cost of supported units of one city
***************************************************************************/
void pay_for_units(struct player *pplayer, struct city *pcity)
{
  int potential_gold = 0;

  built_impr_iterate(pcity, pimpr) {
    potential_gold += impr_sell_gold(pimpr);
  } built_impr_iterate_end;

  unit_list_iterate_safe(pcity->units_supported, punit) {

    if (pplayer->economic.gold + potential_gold < punit->upkeep_gold) {
      /* We cannot upkeep this unit any longer and selling off city
       * improvements will not help so we will have to disband */
      assert(pplayer->economic.gold + potential_gold >= 0);
      
      notify_player_ex(pplayer, -1, -1, E_UNIT_LOST,
		       _("Not enough gold. %s disbanded"),
		       unit_type(punit)->name);
      wipe_unit(punit);
    } else {
      /* Gold can get negative here as city improvements will be sold
       * afterwards to balance our budget. FIXME: Should units with gold 
       * upkeep give gold when they are disbanded? */
      pplayer->economic.gold -= punit->upkeep_gold;
    }
  } unit_list_iterate_safe_end;
}

/***************************************************************************
  1. Do Leonardo's Workshop upgrade if applicable.

  2. Restore/decrease unit hitpoints.

  3. Kill dead units.

  4. Randomly kill units on unsafe terrain or unsafe-ocean.

  5. Rescue airplanes by returning them to base automatically.

  6. Decrease fuel of planes in the air.

  7. Refuel planes that are in bases.

  8. Kill planes that are out of fuel.
****************************************************************************/
void player_restore_units(struct player *pplayer)
{
  /* 1) get Leonardo out of the way first: */
  if (player_owns_active_wonder(pplayer, B_LEONARDO))
    handle_leonardo(pplayer);

  unit_list_iterate_safe(pplayer->units, punit) {

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
      wipe_unit(punit);
      continue; /* Continue iterating... */
    }

    /* 4) Check for units on unsafe terrains. */
    if (unit_flag(punit, F_TRIREME)) {
      /* Triremes away from coast have a chance of death. */
      /* Note if a trireme died on a TER_UNSAFE terrain, this would
       * erronously give the high seas message.  This is impossible under
       * the current rulesets. */
      int loss_chance = unit_loss_pct(pplayer, punit->x, punit->y, punit);

      if (myrand(100) < loss_chance) {
        notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
                         _("Game: Your %s has been lost on the high seas."),
                         unit_name(punit->type));
        gamelog(GAMELOG_UNITTRI, _("%s's %s lost at sea"),
	        get_nation_name_plural(pplayer->nation), 
                unit_name(punit->type));
        wipe_unit(punit);
        continue; /* Continue iterating... */
      } else if (loss_chance > 0) {
        if (maybe_make_veteran(punit)) {
	  notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_BECAME_VET,
                           _("Game: Your %s survived on the high seas "
	                   "and became more experienced!"), 
                           unit_name(punit->type));
        }
      }
    } else if ((!is_air_unit(punit))
	       && (myrand(100) < unit_loss_pct(pplayer,
					       punit->x, punit->y, punit))) {
      /* All units may have a chance of dying if they are on TER_UNSAFE
       * terrain. */
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST,
		       _("Game: Your %s has been lost on unsafe terrain."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITTRI, _("%s unit lost on unsafe terrain"),
	      get_nation_name_plural(pplayer->nation));
      wipe_unit(punit);
      continue;			/* Continue iterating... */
    }

    /* 5) Rescue planes if needed */
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
	    set_goto_dest(punit, x_itr, y_itr);
	    set_unit_activity(punit, ACTIVITY_GOTO);
	    (void) do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
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

      /* 7) Automatically refuel air units in cities, airbases, and
       *    transporters (carriers). */
      if (map_get_city(punit->x, punit->y)
	  || map_has_special(punit->x, punit->y, S_AIRBASE)
	  || punit->transported_by != -1) {
	punit->fuel=unit_type(punit)->fuel;
      }
    }
  } unit_list_iterate_safe_end;

  /* 8) Check if there are air units without fuel */
  unit_list_iterate_safe(pplayer->units, punit) {
    if (is_air_unit(punit) && punit->fuel <= 0
        && unit_type(punit)->fuel > 0) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST, 
		       _("Game: Your %s has run out of fuel."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITF, _("%s lose a %s (fuel)"),
	      get_nation_name_plural(pplayer->nation),
	      unit_name(punit->type));
      wipe_unit(punit);
    } 
  } unit_list_iterate_safe_end;
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
    if(was_lower&&punit->activity==ACTIVITY_SENTRY){
      set_unit_activity(punit,ACTIVITY_IDLE);
    }
  }
  if(punit->hp<0)
    punit->hp=0;

  punit->moved = FALSE;
  punit->paradropped = FALSE;
}
  
/***************************************************************************
  Move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs. Then add veteran
  bonus, if any.
***************************************************************************/
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left = unit_move_rate(punit);
  punit->done_moving = FALSE;
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
static int hp_gain_coord(struct unit *punit)
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

/***************************************************************************
  Maybe settler/worker gains a veteran level?
****************************************************************************/
static bool maybe_settler_become_veteran(struct unit *punit)
{
  if (punit->veteran + 1 >= MAX_VET_LEVELS
      || unit_type(punit)->veteran[punit->veteran].name[0] == '\0'
      || unit_flag(punit, F_NO_VETERAN)) {
    return FALSE;
  }
  if (unit_flag(punit, F_SETTLERS)
      && myrand(100) < game.work_veteran_chance[punit->veteran]) {
    punit->veteran++;
    return TRUE;
  }
  return FALSE;  
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
  bool unit_activity_done = FALSE;
  enum unit_activity activity = punit->activity;
  enum ocean_land_change solvency = OLC_NONE;
  struct tile *ptile = map_get_tile(punit->x, punit->y);
  
  if (activity != ACTIVITY_IDLE && activity != ACTIVITY_FORTIFIED
      && activity != ACTIVITY_GOTO && activity != ACTIVITY_EXPLORE) {
    /*  We don't need the activity_count for the above */
    punit->activity_count += get_settler_speed(punit);

    /* settler may become veteran when doing something useful */
    if (activity != ACTIVITY_FORTIFYING && activity != ACTIVITY_SENTRY
       && maybe_settler_become_veteran(punit)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_BECAME_VET,
	_("Game: Your %s became more experienced!"), unit_name(punit->type));
    }
    
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

    if (!player_find_unit_by_id(pplayer, id)) {
      /* Died */
      return;
    }

    assert(punit->activity == ACTIVITY_EXPLORE); 
    if (!more_to_explore) {
      handle_unit_activity_request(punit, ACTIVITY_IDLE);
    }
    send_unit_info(NULL, punit);
    return;
  }

  if (activity==ACTIVITY_PILLAGE) {
    if (punit->activity_target == S_NO_SPECIAL) { /* case for old save files */
      if (punit->activity_count >= 1) {
	enum tile_special_type what =
	  get_preferred_pillage(
	       get_tile_infrastructure_set(map_get_tile(punit->x, punit->y)));

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
    else if (total_activity_targeted(punit->x, punit->y, ACTIVITY_PILLAGE, 
                                     punit->activity_target) >= 1) {
      enum tile_special_type what_pillaged = punit->activity_target;

      map_clear_special(punit->x, punit->y, what_pillaged);
      unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2) {
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(NULL, punit2);
	}
      } unit_list_iterate_end;
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
      (void) do_unit_goto(punit, GOTO_MOVE_ANY, TRUE);
    }
    return;
  }

  if (unit_has_orders(punit)) {
    if (!execute_orders(punit)) {
      /* Unit died. */
      return;
    }
  }

  send_unit_info(NULL, punit);

  unit_list_iterate(ptile->units, punit2) {
    if (!can_unit_continue_current_activity(punit2))
    {
      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
    }
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
	  if (!is_ocean(ptile2->terrain)
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
			       " land to sea."), unit_name(punit2->type));
	    (void) move_unit(punit2, x, y, 0);
	    if (punit2->activity == ACTIVITY_SENTRY)
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby transport */
	adjc_iterate(punit->x, punit->y, x, y) {
	  struct tile *ptile2 = map_get_tile(x, y);
	  if (is_ocean(ptile2->terrain)
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
			       " land to sea."), unit_name(punit2->type));
	    (void) move_unit(punit2, x, y, 0);
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
			   " land to sea."), unit_name(punit2->type));
	wipe_unit_spec_safe(punit2, FALSE);
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
	  if (is_ocean(ptile2->terrain)
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
			       " sea to land."), unit_name(punit2->type));
	    (void) move_unit(punit2, x, y, 0);
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
			       " sea to land."), unit_name(punit2->type));
	    (void) move_unit(punit2, x, y, 0);
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
			   " sea to land."), unit_name(punit2->type));
	wipe_unit_spec_safe(punit2, FALSE);
	goto START;
      }
    } unit_list_iterate_end;
    break;
  }
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
    if (is_ocean(ptile->terrain)) {
      continue;
    }
    if (ptile->city)
      continue;
    if (unit_list_size(&ptile->units) > 0)
      continue;
    value = get_virtual_defense_power(U_LAST, u_type, x1, y1, FALSE, 0);
    value *= 10;

    if (ptile->continent != map_get_continent(pcity->x, pcity->y)) {
      value /= 2;
    }

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
    punit = create_unit(city_owner(pcity), x, y, u_type, 0, 0, -1);
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
Are there dangerous enemies at or adjacent to (x, y)?

N.B. This function should only be used by (cheating) AI, as it iterates 
through all units stacked on the tiles, an info not normally available 
to the human player.
**************************************************************************/
bool enemies_at(struct unit *punit, int x, int y)
{
  int a = 0, d, db;
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = map_get_tile(x,y)->city;

  if (pcity && pplayers_allied(city_owner(pcity), unit_owner(punit))
      && !is_non_allied_unit_tile(map_get_tile(x,y), pplayer)) {
    /* We will be safe in a friendly city */
    return FALSE;
  }

  /* Calculate how well we can defend at (x,y) */
  db = get_tile_type(map_get_terrain(x, y))->defense_bonus;
  if (map_has_special(x, y, S_RIVER))
    db += (db * terrain_control.river_defense_bonus) / 100;
  d = unit_def_rating_basic_sq(punit) * db;

  adjc_iterate(x, y, x1, y1) {
    if (ai_handicap(pplayer, H_FOG)
	&& !map_is_known_and_seen(x1, y1, unit_owner(punit))) {
      /* We cannot see danger at (x1, y1) => assume there is none */
      continue;
    }
    unit_list_iterate(map_get_tile(x1, y1)->units, enemy) {
      if (pplayers_at_war(unit_owner(enemy), unit_owner(punit)) 
          && can_unit_attack_unit_at_tile(enemy, punit, x, y)
          && can_unit_attack_all_at_tile(enemy, x, y)) {
	a += unit_att_rating(enemy);
	if ((a * a * 10) >= d) {
          /* The enemies combined strength is too big! */
          return TRUE;
        }
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;

  return FALSE; /* as good a quick'n'dirty should be -- Syela */
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
      notify_player_ex(unit_owner(punit), pcity->x, pcity->y, E_NOEVENT,
		       _("Game: Teleported your %s to %s."),
		       unit_name(punit->type), pcity->name);
    }

    if (move_cost == -1)
      move_cost = punit->moves_left;
    return move_unit(punit, dest_x, dest_y, move_cost);
  }
  return FALSE;
}

/**************************************************************************
  Teleport or remove a unit due to stack conflict.
**************************************************************************/
void bounce_unit(struct unit *punit, bool verbose)
{
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = find_closest_owned_city(pplayer, punit->x, punit->y,
                                               is_sailing_unit(punit), NULL);

  if (pcity) {
    (void) teleport_unit_to_city(punit, pcity, 0, verbose);
  } else {
    /* remove it */
    if (verbose) {
      notify_player_ex(unit_owner(punit), punit->x, punit->y, E_NOEVENT,
		       _("Game: Disbanded your %s."),
		       unit_name(punit->type));
    }
    wipe_unit(punit);
  }
}


/**************************************************************************
  Throw pplayer's units from non allied cities

  If verbose is true, pplayer gets messages about where each units goes.
**************************************************************************/
static void throw_units_from_illegal_cities(struct player *pplayer,
                                           bool verbose)
{
  unit_list_iterate_safe(pplayer->units, punit) {
    struct tile *ptile = map_get_tile(punit->x, punit->y);

    if (ptile->city && !pplayers_allied(city_owner(ptile->city), pplayer)) {
      bounce_unit(punit, verbose);
    }
  } unit_list_iterate_safe_end;    
}

/**************************************************************************
  For each pplayer's unit, check if we stack illegally, if so,
  bounce both players' units. If on ocean tile, bounce everyone
  to avoid drowning. This function assumes that cities are clean.

  If verbose is true, the unit owner gets messages about where each
  units goes.
**************************************************************************/
static void resolve_stack_conflicts(struct player *pplayer,
                                    struct player *aplayer, bool verbose)
{
  unit_list_iterate_safe(pplayer->units, punit) {
    int x = punit->x, y = punit->y;
    struct tile *ptile = map_get_tile(x, y);

    if (is_non_allied_unit_tile(ptile, pplayer)) {
      unit_list_iterate_safe(ptile->units, aunit) {
        if (unit_owner(aunit) == pplayer
            || unit_owner(aunit) == aplayer
            || is_ocean(ptile->terrain)) {
          bounce_unit(aunit, verbose);
        }
      } unit_list_iterate_safe_end;
    }    
  } unit_list_iterate_safe_end;
}
				
/**************************************************************************
  When in civil war or an alliance breaks there will potentially be units 
  from both sides coexisting on the same squares.  This routine resolves 
  this by first bouncing off non-allied units from their cities, then by 
  bouncing both players' units in now illegal multiowner stacks.  To avoid
  drowning due to removal of transports, we bounce everyone (including
  third parties' units) from ocean tiles.

  If verbose is true, the unit owner gets messages about where each
  units goes.
**************************************************************************/
void resolve_unit_stacks(struct player *pplayer, struct player *aplayer,
                         bool verbose)
{
  throw_units_from_illegal_cities(pplayer, verbose);
  throw_units_from_illegal_cities(aplayer, verbose);
  
  resolve_stack_conflicts(pplayer, aplayer, verbose);
  resolve_stack_conflicts(aplayer, pplayer, verbose);
}

/****************************************************************************
  When two players cancel an alliance, a lot of units that were visible may
  no longer be visible (this includes units in transporters and cities).
  Call this function to inform the clients that these units are no longer
  visible.  Note that this function should be called _after_
  resolve_unit_stacks().
****************************************************************************/
void remove_allied_visibility(struct player* pplayer, struct player* aplayer)
{
  unit_list_iterate(aplayer->units, punit) {
    /* We don't know exactly which units have been hidden.  But only a unit
     * whose tile is visible but who aren't visible themselves are
     * candidates.  This solution just tells the client to drop all such
     * units.  If any of these are unknown to the client the client will
     * just ignore them. */
    if (map_is_known_and_seen(punit->x, punit->y, pplayer) &&
        !can_player_see_unit(pplayer, punit)) {
      unit_goes_out_of_sight(pplayer, punit);
    }
  } unit_list_iterate_end;

  city_list_iterate(aplayer->cities, pcity) {
    /* The player used to know what units were in these cities.  Now that he
     * doesn't, he needs to get a new short city packet updating the
     * occupied status. */
    send_city_info(pplayer, pcity);
  } city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
bool is_airunit_refuel_point(int x, int y, struct player *pplayer,
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
  Really upgrades a single unit.

  Before calling this function you should use unit_upgrade to test if
  this is possible.

  is_free: Leonardo upgrade for free, in all other cases the unit
  owner has to pay

  Note that this function is strongly tied to unit.c:test_unit_upgrade().
**************************************************************************/
void upgrade_unit(struct unit *punit, Unit_Type_id to_unit, bool is_free)
{
  struct player *pplayer = unit_owner(punit);
  int range;
  int old_mr = unit_move_rate(punit), old_hp = unit_type(punit)->hp;

  assert(test_unit_upgrade(punit, is_free) == UR_OK);

  if (!is_free) {
    pplayer->economic.gold -=
	unit_upgrade_price(pplayer, punit->type, to_unit);
  }

  /* save old vision range */
  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    range = get_watchtower_vision(punit);
  else
    range = unit_type(punit)->vision_range;

  punit->type = to_unit;

  /* Scale HP and MP, rounding down.  Be careful with integer arithmetic,
   * and don't kill the unit.  unit_move_rate is used to take into account
   * global effects like Magellan's Expedition. */
  punit->hp = MAX(punit->hp * unit_type(punit)->hp / old_hp, 1);
  punit->moves_left = punit->moves_left * unit_move_rate(punit) / old_mr;

  conn_list_do_buffer(&pplayer->connections);

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

/************************************************************************* 
  Wrapper of the below
*************************************************************************/
struct unit *create_unit(struct player *pplayer, int x, int y, 
                         Unit_Type_id type, int veteran_level, 
                         int homecity_id, int moves_left)
{
  return create_unit_full(pplayer, x, y, type, veteran_level, homecity_id, 
                          moves_left, -1, NULL);
}

/**************************************************************************
  Creates a unit, and set it's initial values, and put it into the right 
  lists.
**************************************************************************/
struct unit *create_unit_full(struct player *pplayer, int x, int y,
			      Unit_Type_id type, int veteran_level, 
                              int homecity_id, int moves_left, int hp_left,
			      struct unit *ptrans)
{
  struct unit *punit = create_unit_virtual(pplayer, NULL, type, veteran_level);
  struct city *pcity;

  /* Register unit */
  punit->id = get_next_id_number();
  idex_register_unit(punit);

  CHECK_MAP_POS(x, y);
  punit->x = x;
  punit->y = y;

  pcity = find_city_by_id(homecity_id);
  if (unit_type_flag(type, F_NOHOME)) {
    punit->homecity = 0; /* none */
  } else {
    punit->homecity = homecity_id;
  }

  if (hp_left >= 0) {
    /* Override default full HP */
    punit->hp = hp_left;
  }

  if (moves_left >= 0) {
    /* Override default full MP */
    punit->moves_left = MIN(moves_left, unit_move_rate(punit));
  }

  if (ptrans) {
    /* Set transporter for unit. */
    punit->transported_by = ptrans->id;
  }

  /* Assume that if moves_left < 0 then the unit is "fresh",
   * and not moved; else the unit has had something happen
   * to it (eg, bribed) which we treat as equivalent to moved.
   * (Otherwise could pass moved arg too...)  --dwp */
  punit->moved = (moves_left >= 0);

  /* See if this is a spy that has been moved (corrupt and therefore 
   * unable to establish an embassy. */
  punit->foul = (moves_left != -1 && unit_flag(punit, F_SPY));

  unit_list_insert(&pplayer->units, punit);
  unit_list_insert(&map_get_tile(x, y)->units, punit);
  if (pcity && !unit_type_flag(type, F_NOHOME)) {
    assert(city_owner(pcity) == pplayer);
    unit_list_insert(&pcity->units_supported, punit);
    /* Refresh the unit's homecity. */
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  }

  if (map_has_special(x, y, S_FORTRESS)
      && unit_profits_of_watchtower(punit)) {
    unfog_area(pplayer, punit->x, punit->y, get_watchtower_vision(punit));
  } else {
    unfog_area(pplayer, x, y, unit_type(punit)->vision_range);
  }

  send_unit_info(NULL, punit);
  maybe_make_contact(x, y, unit_owner(punit));
  wakeup_neighbor_sentries(punit);

  /* The unit may have changed the available tiles in nearby cities. */
  map_city_radius_iterate(x, y, x1, y1) {
    struct city *acity = map_get_city(x1, y1);

    if (acity) {
      update_city_tile_status_map(acity, x, y);
    }
  } map_city_radius_iterate_end;

  sync_cities();

  return punit;
}

/**************************************************************************
We remove the unit and see if it's disappearance has affected the homecity
and the city it was in.
**************************************************************************/
static void server_remove_unit(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  struct city *phomecity = find_city_by_id(punit->homecity);
  int punit_x = punit->x, punit_y = punit->y;

#ifndef NDEBUG
  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, pcargo) {
    assert(pcargo->transported_by != punit->id);
  } unit_list_iterate_end;
#endif

  /* Since settlers plot in new cities in the minimap before they
     are built, so that no two settlers head towards the same city
     spot, we need to ensure this reservation is cleared should
     the settler die on the way. */
  if ((unit_owner(punit)->ai.control || punit->ai.control)
      && punit->ai.ai_role != AIUNIT_NONE) {
    ai_unit_new_role(punit, AIUNIT_NONE, -1, -1);
  }

  players_iterate(pplayer) {
    if (map_is_known_and_seen(punit_x, punit_y, pplayer)) {
      dlsend_packet_unit_remove(&pplayer->connections, punit->id);
    }
  } players_iterate_end;

  remove_unit_sight_points(punit);

  /* check if this unit had F_GAMELOSS flag */
  if (unit_flag(punit, F_GAMELOSS) && unit_owner(punit)->is_alive) {
    notify_conn_ex(&game.est_connections, punit->x, punit->y, E_UNIT_LOST,
                   _("Unable to defend %s, %s has lost the game."),
                   unit_name(punit->type), unit_owner(punit)->name);
    notify_player(unit_owner(punit), _("Losing %s meant losing the game! "
                  "Be more careful next time!"), unit_name(punit->type));
    gamelog(GAMELOG_NORMAL, _("Player %s lost a game loss unit and died"),
            unit_owner(punit)->name);
    unit_owner(punit)->is_dying = TRUE;
  }

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
  if (pcity && unit_list_size(&map_get_tile(punit_x, punit_y)->units) == 0) {
    /* The last unit in the city was killed: update the occupied flag. */
    send_city_info(NULL, pcity);
  }
}

/**************************************************************************
  Remove the unit, and passengers if it is a carrying any. Remove the 
  _minimum_ number, eg there could be another boat on the square.
**************************************************************************/
void wipe_unit_spec_safe(struct unit *punit, bool wipe_cargo)
{
  int x = punit->x;
  int y = punit->y;
  struct player *pplayer = unit_owner(punit);
  struct unit_type *ptype = unit_type(punit);

  /* First pull all units off of the transporter. */
  if (get_transporter_capacity(punit) > 0) {
    unit_list_iterate(map_get_tile(x, y)->units, pcargo) {
      if (pcargo->transported_by == punit->id) {
	pull_unit_from_transporter(pcargo, punit);
      } 
    } unit_list_iterate_end;
  }

  /* No need to wipe the cargo unless it's a ground transporter. */
  wipe_cargo &= is_ground_units_transport(punit);

  /* Now remove the unit. */
  server_remove_unit(punit);

  /* Finally reassign, bounce, or destroy all ground units at this location.
   * There's no need to worry about air units; they can fly away. */
  if (wipe_cargo
      && is_ocean(map_get_terrain(x, y))
      && !map_get_city(x, y)) {
    struct city *pcity = NULL;
    int capacity = ground_unit_transporter_capacity(x, y, pplayer);

    /* Get rid of excess standard units. */
    if (capacity < 0) {
      unit_list_iterate_safe(map_get_tile(x, y)->units, pcargo) {
	if (is_ground_unit(pcargo)
	    && pcargo->transported_by == -1
	    && !unit_flag(pcargo, F_UNDISBANDABLE)
	    && !unit_flag(pcargo, F_GAMELOSS)) {
	  server_remove_unit(pcargo);
	  if (++capacity >= 0) {
	    break;
	  }
	}
      } unit_list_iterate_safe_end;
    }

    /* Get rid of excess undisbandable/gameloss units. */
    if (capacity < 0) {
      unit_list_iterate_safe(map_get_tile(x, y)->units, pcargo) {
	if (is_ground_unit(pcargo) && pcargo->transported_by == -1) {
	  if (unit_flag(pcargo, F_UNDISBANDABLE)) {
	    pcity = find_closest_owned_city(unit_owner(pcargo),
					    pcargo->x, pcargo->y, TRUE, NULL);
	    if (pcity && teleport_unit_to_city(pcargo, pcity, 0, FALSE)) {
	      notify_player_ex(pplayer, x, y, E_NOEVENT,
			       _("Game: %s escaped the destruction of %s, and "
				 "fled to %s."), unit_type(pcargo)->name,
			       ptype->name, pcity->name);
	    }
	  }
	  if (!unit_flag(pcargo, F_UNDISBANDABLE) || !pcity) {
	    notify_player_ex(pplayer, x, y, E_UNIT_LOST,
			     _("Game: %s lost when %s was lost."),
			     unit_type(pcargo)->name,
			     ptype->name);
	    gamelog(GAMELOG_UNITL, _("%s lose %s when %s lost"),
		    get_nation_name_plural(pplayer->nation),
		    unit_type(pcargo)->name, ptype->name);
	    server_remove_unit(pcargo);
	  }
	  if (++capacity >= 0) {
	    break;
	  }
	}
      } unit_list_iterate_safe_end;
    }

    /* Reassign existing units.  This is an O(n^2) operation as written. */
    unit_list_iterate(map_get_tile(x, y)->units, ptrans) {
      if (is_ground_units_transport(ptrans)) {
	int occupancy = get_transporter_occupancy(ptrans);

	unit_list_iterate(map_get_tile(x, y)->units, pcargo) {
	  if (occupancy >= get_transporter_capacity(ptrans)) {
	    break;
	  }
	  if (is_ground_unit(pcargo) && pcargo->transported_by == -1) {
	    put_unit_onto_transporter(pcargo, ptrans);
	    occupancy++;
	  }
	} unit_list_iterate_end;
      }
    } unit_list_iterate_end;
  }
}

/**************************************************************************
...
**************************************************************************/
void wipe_unit(struct unit *punit)
{
  wipe_unit_spec_safe(punit, TRUE);
}

/**************************************************************************
this is a highlevel routine
the unit has been killed in combat => all other units on the
tile dies unless ...
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit)
{
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

  if (!is_stack_vulnerable(punit->x,punit->y) || unitcount == 1) {
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
      die("Error in kill_unit, unitcount is %i", unitcount);
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
	wipe_unit_spec_safe(punit2, FALSE);
      }
    }
    unit_list_iterate_end;
  }
}

/**************************************************************************
  Package a unit_info packet.  This packet contains basically all
  information about a unit.
**************************************************************************/
void package_unit(struct unit *punit, struct packet_unit_info *packet)
{
  packet->id = punit->id;
  packet->owner = punit->owner;
  packet->x = punit->x;
  packet->y = punit->y;
  packet->homecity = punit->homecity;
  packet->veteran = punit->veteran;
  packet->veteran_old = (punit->veteran > 0);
  packet->type = punit->type;
  packet->movesleft = punit->moves_left;
  packet->hp = punit->hp;
  packet->activity = punit->activity;
  packet->activity_count = punit->activity_count;
  packet->unhappiness = punit->unhappiness;
  packet->upkeep = punit->upkeep;
  packet->upkeep_food = punit->upkeep_food;
  packet->upkeep_gold = punit->upkeep_gold;
  packet->ai = punit->ai.control;
  packet->fuel = punit->fuel;
  if (is_goto_dest_set(punit)) {
    packet->goto_dest_x = goto_dest_x(punit);
    packet->goto_dest_y = goto_dest_y(punit);
  } else {
    packet->goto_dest_x = 255;
    packet->goto_dest_y = 255;
    assert(!is_normal_map_pos(255, 255));
  }
  packet->activity_target = punit->activity_target;
  packet->paradropped = punit->paradropped;
  packet->connecting = punit->connecting;
  packet->done_moving = punit->done_moving;
  if (punit->transported_by == -1) {
    packet->transported = FALSE;
    packet->transported_by = 0;
  } else {
    packet->transported = TRUE;
    packet->transported_by = punit->transported_by;
  }
  packet->occupy = get_transporter_occupancy(punit);
  packet->has_orders = punit->has_orders;
  if (punit->has_orders) {
    int i;

    packet->orders_length = punit->orders.length;
    packet->orders_index = punit->orders.index;
    packet->orders_repeat = punit->orders.repeat;
    packet->orders_vigilant = punit->orders.vigilant;
    for (i = 0; i < punit->orders.length; i++) {
      packet->orders[i] = punit->orders.list[i].order;
      packet->orders_dirs[i] = punit->orders.list[i].dir;
    }
  } else {
    packet->orders_length = packet->orders_index = 0;
    packet->orders_repeat = packet->orders_vigilant = FALSE;
    /* No need to initialize array. */
  }
}

/**************************************************************************
  Package a short_unit_info packet.  This contains a limited amount of
  information about the unit, and is sent to players who shouldn't know
  everything (like the unit's owner's enemies).
**************************************************************************/
void package_short_unit(struct unit *punit,
			struct packet_unit_short_info *packet,
			enum unit_info_use packet_use,
			int info_city_id, bool new_serial_num)
{
  static unsigned int serial_num = 0;

  /* a 16-bit unsigned number, never zero */
  if (new_serial_num) {
    serial_num = (serial_num + 1) & 0xFFFF;
    if (serial_num == 0) {
      serial_num++;
    }
  }
  packet->serial_num = serial_num;
  packet->packet_use = packet_use;
  packet->info_city_id = info_city_id;

  packet->id = punit->id;
  packet->owner = punit->owner;
  packet->x = punit->x;
  packet->y = punit->y;
  packet->veteran = punit->veteran;
  packet->type = punit->type;
  packet->hp = punit->hp;
  packet->occupied = (get_transporter_occupancy(punit) > 0);
  if (punit->activity == ACTIVITY_EXPLORE
      || punit->activity == ACTIVITY_GOTO) {
    packet->activity = ACTIVITY_IDLE;
  } else {
    packet->activity = punit->activity;
  }

  /* Transported_by information is sent to the client even for units that
   * aren't fully known.  Note that for non-allied players, any transported
   * unit can't be seen at all.  For allied players we have to know if
   * transporters have room in them so that we can load units properly. */
  if (punit->transported_by == -1) {
    packet->transported = FALSE;
    packet->transported_by = 0;
  } else {
    packet->transported = TRUE;
    packet->transported_by = punit->transported_by;
  }

  packet->goes_out_of_sight = FALSE;
}

/**************************************************************************
...
**************************************************************************/
void unit_goes_out_of_sight(struct player *pplayer, struct unit *punit)
{
  struct packet_unit_short_info packet;

  if (unit_owner(punit) == pplayer) {
    /* The unit is about to die. No need to send an info. */
  } else {
    memset(&packet, 0, sizeof(packet));
    packet.id = punit->id;
    packet.goes_out_of_sight = TRUE;
    lsend_packet_unit_short_info(&pplayer->connections, &packet);
  }
}

/**************************************************************************
  Send the unit into to those connections in dest which can see the units
  at it's position, or the specified (x,y) (if different).
  Eg, use x and y as where the unit came from, so that the info can be
  sent if the other players can see either the target or destination tile.
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info_to_onlookers(struct conn_list *dest, struct unit *punit,
				 int x, int y, bool remove_unseen)
{
  struct packet_unit_info info;
  struct packet_unit_short_info sinfo;
  
  if (!dest) {
    dest = &game.game_connections;
  }

  package_unit(punit, &info);
  package_short_unit(punit, &sinfo, UNIT_INFO_IDENTITY, FALSE, FALSE);
            
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    
    if ((!pplayer && pconn->observer) 
	|| pplayer->player_no == punit->owner) {
      send_packet_unit_info(pconn, &info);
    } else {
      if (can_player_see_unit_at(pplayer, punit, punit->x, punit->y)
	  || can_player_see_unit_at(pplayer, punit, x, y)) {
	send_packet_unit_short_info(pconn, &sinfo);
      } else {
	if (remove_unseen) {
	  dsend_packet_unit_remove(pconn, punit->id);
	}
      }
    }
  } conn_list_iterate_end;
}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  struct conn_list *conn_dest = (dest ? &dest->connections
				 : &game.game_connections);
  send_unit_info_to_onlookers(conn_dest, punit, punit->x, punit->y, FALSE);
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
	    || map_is_known_and_seen(punit->x, punit->y, pplayer)) {
	  send_unit_info_to_onlookers(&pconn->self, punit,
				      punit->x, punit->y, FALSE);
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
  Nuke a square: 1) remove all units on the square, and 2) halve the 
  size of the city on the square.

  If it isn't a city square or an ocean square then with 50% chance add 
  some fallout, then notify the client about the changes.
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
    wipe_unit_spec_safe(punit, FALSE);
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

    city_reduce_size(pcity, pcity->size / 2);
  }

  if (!is_ocean(map_get_terrain(x, y)) && myrand(2) == 1) {
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
  Nuke all the squares in a 3x3 square around the center of the explosion
  pplayer is the player that caused the explosion. You lose reputation
  each time.
**************************************************************************/
void do_nuclear_explosion(struct player *pplayer, int x, int y)
{
  square_iterate(x, y, 1, x1, y1) {
    do_nuke_tile(pplayer, x1, y1);
  } square_iterate_end;

  /* Give reputation penalty to nuke users */
  pplayer->reputation = MAX(pplayer->reputation - REPUTATION_LOSS_NUKE, 0);
  send_player_info(pplayer, NULL);

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
  if (get_transporter_occupancy(punit) > 0) {
    return FALSE;
  }
  city1->airlift = FALSE;
  city2->airlift = FALSE;

  notify_player_ex(unit_owner(punit), city2->x, city2->y, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));

  (void) move_unit(punit, city2->x, city2->y, punit->moves_left);

  /* airlift fields have changed. */
  send_city_info(city_owner(city1), city1);
  send_city_info(city_owner(city2), city2);

  return TRUE;
}

/**************************************************************************
  Returns whether the drop was made or not. Note that it also returns 1 
  in the case where the drop was succesful, but the unit was killed by
  barbarians in a hut.
**************************************************************************/
bool do_paradrop(struct unit *punit, int dest_x, int dest_y)
{
  struct tile *ptile = map_get_tile(dest_x, dest_y);
  struct player *pplayer = unit_owner(punit);

  if (!unit_flag(punit, F_PARATROOPERS)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                     _("Game: This unit type can not be paradropped."));
    return FALSE;
  }

  if (!can_unit_paradrop(punit)) {
    return FALSE;
  }

  if (get_transporter_occupancy(punit) > 0) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: You cannot paradrop a transporter unit."));
  }

  if (!map_is_known(dest_x, dest_y, pplayer)) {
    notify_player_ex(pplayer, dest_x, dest_y, E_NOEVENT,
                     _("Game: The destination location is not known."));
    return FALSE;
  }

  if (is_ocean(map_get_player_tile(dest_x, dest_y, pplayer)->terrain)
      && is_ground_unit(punit)) {
    notify_player_ex(pplayer, dest_x, dest_y, E_NOEVENT,
                     _("Game: This unit cannot paradrop into ocean."));
    return FALSE;    
  }

  if (map_is_known_and_seen(dest_x, dest_y, pplayer)
      && ((ptile->city
	  && pplayers_non_attack(pplayer, city_owner(ptile->city)))
      || is_non_attack_unit_tile(ptile, pplayer))) {
    notify_player_ex(pplayer, dest_x, dest_y, E_NOEVENT,
                     _("Game: Cannot attack unless you declare war first."));
    return FALSE;    
  }

  {
    int range = unit_type(punit)->paratroopers_range;
    int distance = real_map_distance(punit->x, punit->y, dest_x, dest_y);
    if (distance > range) {
      notify_player_ex(pplayer, dest_x, dest_y, E_NOEVENT,
                       _("Game: The distance to the target (%i) "
                         "is greater than the unit's range (%i)."),
                       distance, range);
      return FALSE;
    }
  }

  if (is_ocean(map_get_terrain(dest_x, dest_y))
      && is_ground_unit(punit)) {
    int srange = unit_type(punit)->vision_range;

    show_area(pplayer, dest_x, dest_y, srange);

    notify_player_ex(pplayer, dest_x, dest_y, E_UNIT_LOST,
                     _("Game: Your %s paradropped into the ocean "
                       "and was lost."),
                     unit_type(punit)->name);
    server_remove_unit(punit);
    return TRUE;
  }

  if ((ptile->city && pplayers_non_attack(pplayer, city_owner(ptile->city)))
      || is_non_allied_unit_tile(ptile, pplayer)) {
    int srange = unit_type(punit)->vision_range;
    show_area(pplayer, dest_x, dest_y, srange);
    maybe_make_contact(dest_x, dest_y, pplayer);
    notify_player_ex(pplayer, dest_x, dest_y, E_UNIT_LOST_ATT,
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
    return move_unit(punit, dest_x, dest_y, move_cost);
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
  notify_embassies(pplayer, NULL, _("Game: The %s have acquired %s"
				    " from ancient scrolls of wisdom."),
		   get_nation_name_plural(pplayer->nation), tech_name);

  do_free_cost(pplayer);
  if (!is_future_tech(new_tech)) {
    found_new_tech(pplayer, new_tech, FALSE, TRUE, A_NONE);
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

  if (city_exists_within_city_radius(punit->x, punit->y, TRUE)
      || unit_flag(punit, F_GAMELOSS)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_BARB_CITY_NEAR,
		     _("Game: An abandoned village is here."));
  } else {
    /* save coords and type in case unit dies */
    int punit_x = punit->x;
    int punit_y = punit->y;
    Unit_Type_id type = punit->type;

    ok = unleash_barbarians(punit_x, punit_y);

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

  if (city_can_be_built_here(punit->x, punit->y, NULL)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_CITY,
		     _("Game: You found a friendly city."));
    create_city(pplayer, punit->x, punit->y,
		city_name_suggestion(pplayer, punit->x, punit->y));
  } else {
    notify_player_ex(pplayer, punit->x, punit->y, E_HUT_SETTLER,
		     _("Game: Friendly nomads are impressed by you,"
		       " and join you."));
    (void) create_unit(pplayer, punit->x, punit->y, get_role_unit(F_CITIES,0),
		0, punit->homecity, -1);
  }
}

/**************************************************************************
  Return 1 if unit is alive, and 0 if it was killed
**************************************************************************/
static bool unit_enter_hut(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  bool ok = TRUE;
  int hut_chance = myrand(12);
  
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
  
  /* AI with H_LIMITEDHUTS only gets 25 gold (or barbs if unlucky) */
  if (pplayer->ai.control && ai_handicap(pplayer, H_LIMITEDHUTS) 
      && hut_chance != 10) {
    hut_chance = 0;
  }

  switch (hut_chance) {
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

/****************************************************************************
  Put the unit onto the transporter.  Don't do any other work.
****************************************************************************/
static void put_unit_onto_transporter(struct unit *punit, struct unit *ptrans)
{
  /* In the future we may updated ptrans->occupancy. */
  assert(punit->transported_by == -1);
  punit->transported_by = ptrans->id;
}

/****************************************************************************
  Put the unit onto the transporter.  Don't do any other work.
****************************************************************************/
static void pull_unit_from_transporter(struct unit *punit,
				       struct unit *ptrans)
{
  /* In the future we may updated ptrans->occupancy. */
  assert(punit->transported_by == ptrans->id);
  punit->transported_by = -1;
}

/****************************************************************************
  Put the unit onto the transporter, and tell everyone.
****************************************************************************/
void load_unit_onto_transporter(struct unit *punit, struct unit *ptrans)
{
  put_unit_onto_transporter(punit, ptrans);
  send_unit_info(NULL, punit);
  send_unit_info(NULL, ptrans);
}

/****************************************************************************
  Pull the unit off of the transporter, and tell everyone.
****************************************************************************/
void unload_unit_from_transporter(struct unit *punit)
{
  struct unit *ptrans = find_unit_by_id(punit->transported_by);

  pull_unit_from_transporter(punit, ptrans);
  send_unit_info(NULL, punit);
  send_unit_info(NULL, ptrans);
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
	  && range >= real_map_distance(punit->x, punit->y, x, y)
	  && can_player_see_unit(unit_owner(penemy), punit)
	  /* on board transport; don't awaken */
	  && !(move_type == LAND_MOVING && is_ocean(terrain))) {
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
	  && unit_has_orders(ppatrol)
	  && ppatrol->orders.vigilant) {
	(void) maybe_cancel_patrol_due_to_enemy(ppatrol);
      }
    } unit_list_iterate_end;
  } square_iterate_end;
}

/**************************************************************************
Does: 1) updates  the units homecity and the city it enters/leaves (the
         cities happiness varies). This also takes into account if the
	 unit enters/leaves a fortress.
      2) handles any huts at the units destination.
      3) updates adjacent cities' unavailable tiles.

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

    if (pcity && update_city_tile_status_map(pcity, src_x, src_y)) {
      auto_arrange_workers(pcity);
      send_city_info(NULL, pcity);
    }
  } map_city_radius_iterate_end;
  /* Then check cities near the destination. */
  map_city_radius_iterate(dest_x, dest_y, x1, y1) {
    struct city *pcity = map_get_city(x1, y1);
    if (pcity && update_city_tile_status_map(pcity, dest_x, dest_y)) {
      auto_arrange_workers(pcity);
      send_city_info(NULL, pcity);
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
      && punit->activity != ACTIVITY_SENTRY
      && punit->activity != ACTIVITY_EXPLORE
      && punit->activity != ACTIVITY_GOTO
      && !punit->connecting) {
    set_unit_activity(punit, ACTIVITY_IDLE);
  }
}

/**************************************************************************
  Moves a unit. No checks whatsoever! This is meant as a practical 
  function for other functions, like do_airline, which do the checking 
  themselves.

  If you move a unit you should always use this function, as it also sets 
  the transported_by unit field correctly. take_from_land is only relevant 
  if you have set transport_units. Note that the src and dest need not be 
  adjacent.
**************************************************************************/
bool move_unit(struct unit *punit, int dest_x, int dest_y, int move_cost)
{
  int src_x = punit->x;
  int src_y = punit->y;
  struct player *pplayer = unit_owner(punit);
  struct tile *psrctile = map_get_tile(src_x, src_y);
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);
  struct city *pcity;
  struct unit *ptransporter = NULL;
    
  CHECK_MAP_POS(dest_x, dest_y);

  conn_list_do_buffer(&pplayer->connections);

  /* Transporting units. We first make a list of the units to be moved and
     then insert them again. The way this is done makes sure that the
     units stay in the same order. */
  if (get_transporter_capacity(punit) > 0) {
    struct unit_list cargo_units;

    /* First make a list of the units to be moved. */
    unit_list_init(&cargo_units);
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
      send_unit_info_to_onlookers(NULL, pcargo, src_x, src_y, FALSE);
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

  /* Enhance vision if unit steps into a fortress */
  if (unit_profits_of_watchtower(punit)
      && tile_has_special(pdesttile, S_FORTRESS)) {
    unfog_area(pplayer, dest_x, dest_y, get_watchtower_vision(punit));
  } else {
    unfog_area(pplayer, dest_x, dest_y,
	       unit_type(punit)->vision_range);
  }

  unit_list_unlink(&psrctile->units, punit);
  punit->x = dest_x;
  punit->y = dest_y;
  punit->moved = TRUE;
  if (punit->transported_by != -1) {
    ptransporter = find_unit_by_id(punit->transported_by);
    pull_unit_from_transporter(punit, ptransporter);
  }
  punit->moves_left = MAX(0, punit->moves_left - move_cost);
  if (punit->moves_left == 0) {
    punit->done_moving = TRUE;
  }
  unit_list_insert(&pdesttile->units, punit);
  check_unit_activity(punit);

  /*
   * Transporter info should be send first becouse this allow us get right
   * update_menu effect in client side.
   */
  
  /*
   * Send updated information to anyone watching that transporter was unload
   * cargo.
   */
  if (ptransporter) {
    send_unit_info(NULL, ptransporter);
  }
  
  /* Send updated information to anyone watching.  If the unit moves
   * in or out of a city we update the 'occupied' field.  Note there may
   * be cities at both src and dest under some rulesets. */
  send_unit_info_to_onlookers(NULL, punit, src_x, src_y, FALSE);
    
  /* Special checks for ground units in the ocean. */
  if (!can_unit_survive_at_tile(punit, dest_x, dest_y)) {
    ptransporter = find_transporter_for_unit(punit, dest_x, dest_y);
    if (ptransporter) {
      put_unit_onto_transporter(punit, ptransporter);
    }

    /* Set activity to sentry if boarding a ship. */
    if (ptransporter && !pplayer->ai.control && !unit_has_orders(punit)
	&& !can_unit_exist_at_tile(punit, dest_x, dest_y)) {
      set_unit_activity(punit, ACTIVITY_SENTRY);
    }

    /*
     * Transporter info should be send first becouse this allow us get right
     * update_menu effect in client side.
     */
    
    /*
     * Send updated information to anyone watching that transporter has cargo.
     */
    if (ptransporter) {
      send_unit_info(NULL, ptransporter);
    }

    /*
     * Send updated information to anyone watching that unit is on transport.
     * All players without shared vison with owner player get
     * REMOVE_UNIT package.
     */
    send_unit_info_to_onlookers(NULL, punit, punit->x, punit->y, TRUE);
  }
  
  if ((pcity = map_get_city(src_x, src_y))) {
    refresh_dumb_city(pcity);
  }
  if ((pcity = map_get_city(dest_x, dest_y))) {
    refresh_dumb_city(pcity);
  }

  /* The hidden units might not have been previously revealed 
   * because when we did the unfogging, the unit was still 
   * at (src_x, src_y) */
  reveal_hidden_units(pplayer, dest_x, dest_y);

  if (unit_profits_of_watchtower(punit)
      && tile_has_special(psrctile, S_FORTRESS)) {
    fog_area(pplayer, src_x, src_y, get_watchtower_vision(punit));
  } else {
    fog_area(pplayer, src_x, src_y,
	     unit_type(punit)->vision_range);
  }

  /*
   * Let the unit goes out of sight for the players which doesn't see
   * the unit anymore.
   */
  players_iterate(pplayer) {
    if (can_player_see_unit_at(pplayer, punit, src_x, src_y)
	&& !can_player_see_unit_at(pplayer, punit, dest_x, dest_y)) {
      unit_goes_out_of_sight(pplayer, punit);
    }
  } players_iterate_end;

  handle_unit_move_consequences(punit, src_x, src_y, dest_x, dest_y);
  wakeup_neighbor_sentries(punit);
  maybe_make_contact(dest_x, dest_y, unit_owner(punit));

  conn_list_do_unbuffer(&pplayer->connections);

  /* Note, an individual call to move_unit may leave things in an unstable
   * state (e.g., negative transporter capacity) if more than one unit is
   * being moved at a time (e.g., bounce unit) and they are not done in the
   * right order.  This is probably not a bug. */

  if (map_has_special(dest_x, dest_y, S_HUT)) {
    return unit_enter_hut(punit);
  } else {
    return TRUE;
  }
}

/**************************************************************************
  Maybe cancel the goto if there is an enemy in the way
**************************************************************************/
static bool maybe_cancel_goto_due_to_enemy(struct unit *punit, 
                                           int x, int y)
{
  struct player *pplayer = unit_owner(punit);
  struct tile *ptile = map_get_tile(x, y);
  
  if (is_non_allied_unit_tile(ptile, pplayer) 
      || is_non_allied_city_tile(ptile, pplayer)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                     _("Game: %s aborted GOTO "
                       "as there are units in the way."),
                     unit_type(punit)->name);
    return TRUE;
  }

  return FALSE;
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
  struct player *pplayer = unit_owner(punit);

  if (map_has_special(punit->x, punit->y, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    range = get_watchtower_vision(punit);
  else
    range = unit_type(punit)->vision_range;
  
  square_iterate(punit->x, punit->y, range, x, y) {
    struct unit *penemy =
	is_non_allied_unit_tile(map_get_tile(x, y), pplayer);
    struct dumb_city *pdcity = map_get_player_tile(x, y, pplayer)->city;

    if ((penemy && can_player_see_unit(pplayer, penemy))
	|| (pdcity && !pplayers_allied(pplayer, get_player(pdcity->owner))
	    && pdcity->occupied)) {
      cancel = TRUE;
      break;
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

/****************************************************************************
  Cancel orders for the unit.
****************************************************************************/
static void cancel_orders(struct unit *punit, char *dbg_msg)
{
  free_unit_orders(punit);
  send_unit_info(NULL, punit);
  freelog(LOG_DEBUG, "%s", dbg_msg);
}

/****************************************************************************
  Executes a unit's orders stored in punit->orders.  The unit is put on idle
  if an action fails or if "patrol" is set and an enemy unit is encountered.

  The return value will be TRUE if the unit lives, FALSE otherwise.  (This
  function used to return a goto_result enumeration, declared in gotohand.h.
  But this enumeration was never checked by the caller and just lead to
  confusion.  All the caller really needs to know is if the unit lived or
  died; everything else is handled internally within execute_orders.)

  If the orders are repeating the loop starts over at the beginning once it
  completes.  To avoid infinite loops on railroad we stop for this
  turn when the unit is back where it started, even if it have moves left.

  A unit will attack under orders only on its final action.
****************************************************************************/
bool execute_orders(struct unit *punit)
{
  int dest_x, dest_y;
  bool res, last_order;
  int unitid = punit->id;
  struct player *pplayer = unit_owner(punit);
  int moves_made = 0;

  assert(unit_has_orders(punit));

  freelog(LOG_DEBUG, "Executing orders for %s %d",
	  unit_name(punit->type), punit->id);   

  /* Any time the orders are canceled we should give the player a message. */

  while (TRUE) {
    struct unit_order order;

    if (punit->moves_left == 0) {
      /* FIXME: this check won't work when actions take 0 MP. */
      freelog(LOG_DEBUG, "  stopping because of no more move points");
      return TRUE;
    }

    if (punit->done_moving) {
      freelog(LOG_DEBUG, "  stopping because we're done this turn");
      return TRUE;
    }

    if (punit->orders.vigilant && maybe_cancel_patrol_due_to_enemy(punit)) {
      /* "Patrol" orders are stopped if an enemy is near. */
      cancel_orders(punit, "  stopping because of nearby enemy");
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_ORDERS,
		       _("Game: Orders for %s aborted as there "
			 "are units nearby."),
		       unit_name(punit->type));
      return TRUE;
    }

    if (moves_made == punit->orders.length) {
      /* For repeating orders, don't repeat more than once per turn. */
      freelog(LOG_DEBUG, "  stopping because we ran a round");
      return TRUE;
    }

    last_order = (!punit->orders.repeat
		  && punit->orders.index + 1 == punit->orders.length);

    order = punit->orders.list[punit->orders.index];
    if (last_order) {
      /* Clear the orders before we engage in the move.  That way any
       * has_orders checks will yield FALSE and this will be treated as
       * a normal move.  This is important: for instance a caravan goto
       * will popup the caravan dialog on the last move only. */
      free_unit_orders(punit);
    }

    /* Advance the orders one step forward.  This is needed because any
     * updates sent to the client as a result of the action should include
     * the new index value.  Note that we have to send_unit_info somewhere
     * after this point so that the client is properly updated. */
    punit->orders.index++;

    switch (order.order) {
    case ORDER_FINISH_TURN:
      punit->done_moving = TRUE;
      freelog(LOG_DEBUG, "  waiting this turn");
      send_unit_info(NULL, punit);
      break;
    case ORDER_MOVE:
      /* Move unit */
      if (!MAPSTEP(dest_x, dest_y, punit->x, punit->y, order.dir)) {
	cancel_orders(punit, "  move order sent us to invalid location");
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted since they "
			   "give an invalid location."),
			 unit_name(punit->type));
	return TRUE;
      }

      if (!last_order
	  && maybe_cancel_goto_due_to_enemy(punit, dest_x, dest_y)) {
	cancel_orders(punit, "  orders canceled because of enemy");
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted as there "
			   "are units in the way."),
			 unit_name(punit->type));
	return TRUE;
      }

      freelog(LOG_DEBUG, "  moving to %d,%d", dest_x, dest_y);
      res = handle_unit_move_request(punit, dest_x, dest_y,
				     FALSE, !last_order);
      if (!player_find_unit_by_id(pplayer, unitid)) {
	freelog(LOG_DEBUG, "  unit died while moving.");
	/* A player notification should already have been sent. */
	return FALSE;
      }

      if (res && !same_pos(dest_x, dest_y, punit->x, punit->y)) {
	/* Movement succeeded but unit didn't move. */
	freelog(LOG_DEBUG, "  orders resulted in combat.");
	send_unit_info(NULL, punit);
	return TRUE;
      }

      if (!res && punit->moves_left > 0) {
	/* Movement failed (ZOC, etc.) */
	cancel_orders(punit, "  attempt to move failed.");
	notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted because of "
			   "failed move."),
			 unit_name(punit->type));
	return TRUE;
      }

      break;
    case ORDER_LAST:
      cancel_orders(punit, "  client sent invalid order!");
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_ORDERS,
		       _("Game: Your %s has invalid orders."),
		       unit_name(punit->type));
      return TRUE;
    }

    if (last_order) {
      assert(punit->has_orders == FALSE);
      freelog(LOG_DEBUG, "  stopping because orders are complete");
      return TRUE;
    }

    if (punit->orders.index == punit->orders.length) {
      assert(punit->orders.repeat);
      /* Start over. */
      freelog(LOG_DEBUG, "  repeating orders.");
      punit->orders.index = 0;
    }
  } /* end while */
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
