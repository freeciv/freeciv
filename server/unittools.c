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
#include "maphand.h"
#include "plrhand.h"
#include "sernet.h"
#include "settlers.h"
#include "srv_main.h"
#include "unithand.h"
#include "gamehand.h"

#include "aiexplorer.h"
#include "aitools.h"
#include "aiunit.h"

#include "unittools.h"


static void unit_restore_hitpoints(struct player *pplayer, struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void update_unit_activity(struct unit *punit);
static void wakeup_neighbor_sentries(struct unit *punit);
static void do_upgrade_effects(struct player *pplayer);

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
  } else {
    struct player *plr;
    int mod = 100;

    plr = get_player(punit->owner);

    if (is_ground_unittype(punit->type)) {
      mod += get_player_bonus(plr, EFT_LAND_VET_COMBAT);
    }

    /* The modification is tacked on as a multiplier to the base chance.
     * For example with a base chance of 50% for green units and a modifier
     * of +50% the end chance is 75%. */
    if (myrand(100) < game.veteran_chance[punit->veteran] * mod / 100) {
      punit->veteran++;
      return TRUE;
    }
    return FALSE;
  }
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
  Do unit auto-upgrades to players with the EFT_UNIT_UPGRADE effect
  (traditionally from Leonardo's Workshop).
****************************************************************************/
static void do_upgrade_effects(struct player *pplayer)
{
  int upgrades = get_player_bonus(pplayer, EFT_UPGRADE_UNIT);
  struct unit_list candidates;

  if (upgrades <= 0) {
    return;
  }

  unit_list_init(&candidates);

  unit_list_iterate(pplayer->units, punit) {
    /* We have to be careful not to strand units at sea, for example by
     * upgrading a frigate to an ironclad while it was carrying a unit. */
    if (test_unit_upgrade(punit, TRUE) == UR_OK) {
      unit_list_insert(&candidates, punit);	/* Potential candidate :) */
    }
  } unit_list_iterate_end;

  while (upgrades > 0 && unit_list_size(&candidates) > 0) {
    /* Upgrade one unit.  The unit is chosen at random from the list of
     * available candidates. */
    int candidate_to_upgrade = myrand(unit_list_size(&candidates));
    struct unit *punit = unit_list_get(&candidates, candidate_to_upgrade);
    Unit_Type_id upgrade_type = can_upgrade_unittype(pplayer, punit->type);

    notify_player(pplayer,
		  _("Game: %s was upgraded for free to %s%s."),
		  unit_type(punit)->name,
		  get_unit_type(upgrade_type)->name,
		  get_location_str_in(pplayer, punit->tile));

    /* For historical reasons we negate the unit's veteran status.  Note that
     * the upgraded unit may have the NoVeteran flag set. */
    punit->veteran = 0;
    assert(test_unit_upgrade(punit, TRUE) == UR_OK);
    upgrade_unit(punit, upgrade_type, TRUE);
    unit_list_unlink(&candidates, punit);
    upgrades--;
  }

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
      
      notify_player_ex(pplayer, NULL, E_UNIT_LOST,
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
  do_upgrade_effects(pplayer);

  unit_list_iterate_safe(pplayer->units, punit) {

    /* 2) Modify unit hitpoints. Helicopters can even lose them. */
    unit_restore_hitpoints(pplayer, punit);

    /* 3) Check that unit has hitpoints */
    if (punit->hp<=0) {
      /* This should usually only happen for heli units,
	 but if any other units get 0 hp somehow, catch
	 them too.  --dwp  */
      notify_player_ex(pplayer, punit->tile, E_UNIT_LOST, 
          _("Game: Your %s has run out of hit points."), 
          unit_name(punit->type));
      gamelog(GAMELOG_UNITLOSS, punit, NULL, "out of hp");
      wipe_unit(punit);
      continue; /* Continue iterating... */
    }

    /* 4) Check for units on unsafe terrains. */
    if (unit_flag(punit, F_TRIREME)) {
      /* Triremes away from coast have a chance of death. */
      /* Note if a trireme died on a TER_UNSAFE terrain, this would
       * erronously give the high seas message.  This is impossible under
       * the current rulesets. */
      int loss_chance = unit_loss_pct(pplayer, punit->tile, punit);

      if (myrand(100) < loss_chance) {
        notify_player_ex(pplayer, punit->tile, E_UNIT_LOST, 
                         _("Game: Your %s has been lost on the high seas."),
                         unit_name(punit->type));
        gamelog(GAMELOG_UNITLOSS, punit, NULL, "lost at sea");
        wipe_unit(punit);
        continue; /* Continue iterating... */
      } else if (loss_chance > 0) {
        if (maybe_make_veteran(punit)) {
	  notify_player_ex(pplayer, punit->tile, E_UNIT_BECAME_VET,
                           _("Game: Your %s survived on the high seas "
	                   "and became more experienced!"), 
                           unit_name(punit->type));
        }
      }
    } else if (!(is_air_unit(punit) || is_heli_unit(punit))
	       && (myrand(100) < unit_loss_pct(pplayer,
					       punit->tile, punit))) {
      /* All units may have a chance of dying if they are on TER_UNSAFE
       * terrain. */
      notify_player_ex(pplayer, punit->tile, E_UNIT_LOST,
		       _("Game: Your %s has been lost on unsafe terrain."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITLOSS, punit, NULL, "unsafe terrain");
      wipe_unit(punit);
      continue;			/* Continue iterating... */
    }

    /* 5) Rescue planes if needed */
    if (is_air_unit(punit)) {
      /* Shall we emergency return home on the last vapors? */

      /* I think this is strongly against the spirit of client goto.
       * The problem is (again) that here we know too much. -- Zamar */

      if (punit->fuel == 1
	  && !is_airunit_refuel_point(punit->tile,
				      unit_owner(punit), punit->type, TRUE)) {
	iterate_outward(punit->tile, punit->moves_left/3, itr_tile) {
	  if (is_airunit_refuel_point(itr_tile, unit_owner(punit),
				      punit->type, FALSE)
	      &&(air_can_move_between(punit->moves_left / 3, punit->tile,
				      itr_tile, unit_owner(punit)) >= 0)) {
	    free_unit_orders(punit);
	    punit->goto_tile = itr_tile;
	    set_unit_activity(punit, ACTIVITY_GOTO);
	    (void) do_unit_goto(punit, GOTO_MOVE_ANY, FALSE);
	    notify_player_ex(pplayer, punit->tile, E_NOEVENT, 
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
      if (map_get_city(punit->tile)
	  || map_has_special(punit->tile, S_AIRBASE)
	  || punit->transported_by != -1) {
	punit->fuel=unit_type(punit)->fuel;
      }
    }
  } unit_list_iterate_safe_end;

  /* 8) Check if there are air units without fuel */
  unit_list_iterate_safe(pplayer->units, punit) {
    if (is_air_unit(punit) && punit->fuel <= 0
        && unit_type(punit)->fuel > 0) {
      notify_player_ex(pplayer, punit->tile, E_UNIT_LOST, 
		       _("Game: Your %s has run out of fuel."),
		       unit_name(punit->type));
      gamelog(GAMELOG_UNITLOSS, punit, NULL, "fuel");
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

  /* Bonus recovery HP (traditionally from the United Nations) */
  punit->hp += get_player_bonus(pplayer, EFT_UNIT_RECOVER);

  if(is_heli_unit(punit)) {
    struct city *pcity = map_get_city(punit->tile);
    if(!pcity && !map_has_special(punit->tile, S_AIRBASE)
       && punit->transported_by == -1) {
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
  if((pcity=map_get_city(punit->tile))) {
    if ((get_city_bonus(pcity, EFT_LAND_REGEN) > 0
	 && is_ground_unit(punit))
	|| (get_city_bonus(pcity, EFT_AIR_REGEN) > 0
	    && (is_air_unit(punit) || is_heli_unit(punit)))
	|| (get_city_bonus(pcity, EFT_SEA_REGEN) > 0
	    && is_sailing_unit(punit))) {
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
static int total_activity (struct tile *ptile, enum unit_activity act)
{
  int total = 0;

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
static int total_activity_targeted(struct tile *ptile, enum unit_activity act,
				   enum tile_special_type tgt)
{
  int total = 0;

  unit_list_iterate (ptile->units, punit)
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
  struct tile *ptile = punit->tile;
  bool check_adjacent_units = FALSE;
  
  if (activity != ACTIVITY_IDLE && activity != ACTIVITY_FORTIFIED
      && activity != ACTIVITY_GOTO && activity != ACTIVITY_EXPLORE) {
    /*  We don't need the activity_count for the above */
    punit->activity_count += get_activity_rate_this_turn(punit);

    /* settler may become veteran when doing something useful */
    if (activity != ACTIVITY_FORTIFYING && activity != ACTIVITY_SENTRY
       && maybe_settler_become_veteran(punit)) {
      notify_player_ex(pplayer, punit->tile, E_UNIT_BECAME_VET,
	_("Game: Your %s became more experienced!"), unit_name(punit->type));
    }
    
  }

  unit_restore_movepoints(pplayer, punit);

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
	       get_tile_infrastructure_set(punit->tile));

	if (what != S_NO_SPECIAL) {
	  map_clear_special(punit->tile, what);
	  update_tile_knowledge(punit->tile);
	  set_unit_activity(punit, ACTIVITY_IDLE);
	  check_adjacent_units = TRUE;
	}

	/* If a watchtower has been pillaged, reduce sight to normal */
	if (what == S_FORTRESS) {
	  unit_list_iterate(punit->tile->units, punit2) {
            struct player *owner = unit_owner(punit2);
            freelog(LOG_VERBOSE, "Watchtower pillaged!");
            if (player_knows_techs_with_flag(owner, TF_WATCHTOWER)) {
              if (is_ground_unit(punit2)) {
                /* Unfog (increase seen counter) first, fog (decrease counter)
                 * later, so tiles that are within vision range both before and
                 * after are not even temporarily marked fogged. */
                unfog_area(owner, punit2->tile,
                           unit_type(punit2)->vision_range);
                fog_area(owner, punit2->tile,
                         get_watchtower_vision(punit2));
              }
            }
          }
	  unit_list_iterate_end;
	}
      }
    }
    else if (total_activity_targeted(punit->tile, ACTIVITY_PILLAGE, 
                                     punit->activity_target) >= 1) {
      enum tile_special_type what_pillaged = punit->activity_target;

      map_clear_special(punit->tile, what_pillaged);
      unit_list_iterate (punit->tile->units, punit2) {
        if ((punit2->activity == ACTIVITY_PILLAGE) &&
	    (punit2->activity_target == what_pillaged)) {
	  set_unit_activity(punit2, ACTIVITY_IDLE);
	  send_unit_info(NULL, punit2);
	}
      } unit_list_iterate_end;
      update_tile_knowledge(punit->tile);
      
      /* If a watchtower has been pillaged, reduce sight to normal */
      if (what_pillaged == S_FORTRESS) {
	freelog(LOG_VERBOSE, "Watchtower(2) pillaged!");
	unit_list_iterate(punit->tile->units, punit2) {
          struct player *owner = unit_owner(punit2);
          if (player_knows_techs_with_flag(owner, TF_WATCHTOWER)) {
            if (is_ground_unit(punit2)) {
              /* Unfog (increase seen counter) first, fog (decrease counter)
               * later, so tiles that are within vision range both before and
               * after are not even temporarily marked fogged. */
              unfog_area(owner, punit2->tile,
                         unit_type(punit2)->vision_range);
              fog_area(owner, punit2->tile,
                       get_watchtower_vision(punit2));
            }
          }
        }
	unit_list_iterate_end;
      }
    }  
  }

  if (activity==ACTIVITY_POLLUTION) {
    if (total_activity (punit->tile, ACTIVITY_POLLUTION)
	>= map_clean_pollution_time(punit->tile)) {
      map_clear_special(punit->tile, S_POLLUTION);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_FALLOUT) {
    if (total_activity (punit->tile, ACTIVITY_FALLOUT)
	>= map_clean_fallout_time(punit->tile)) {
      map_clear_special(punit->tile, S_FALLOUT);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_FORTRESS) {
    if (total_activity (punit->tile, ACTIVITY_FORTRESS)
	>= map_build_fortress_time(punit->tile)) {
      map_set_special(punit->tile, S_FORTRESS);
      unit_activity_done = TRUE;
      /* watchtower becomes effective */
      unit_list_iterate(ptile->units, punit) {
        struct player *owner = unit_owner(punit);
        if (player_knows_techs_with_flag(owner, TF_WATCHTOWER)) {
	  if (is_ground_unit(punit)) {
            /* Unfog (increase seen counter) first, fog (decrease counter)
             * later, so tiles that are within vision range both before and
             * after are not even temporarily marked fogged. */
	    unfog_area(owner, punit->tile,
		       get_watchtower_vision(punit));
	    fog_area(owner, punit->tile,
		     unit_type(punit)->vision_range);
	  }
	}
      }
      unit_list_iterate_end;
    }
  }

  if (activity==ACTIVITY_AIRBASE) {
    if (total_activity (punit->tile, ACTIVITY_AIRBASE)
	>= map_build_airbase_time(punit->tile)) {
      map_set_special(punit->tile, S_AIRBASE);
      unit_activity_done = TRUE;
    }
  }
  
  if (activity==ACTIVITY_IRRIGATE) {
    if (total_activity (punit->tile, ACTIVITY_IRRIGATE) >=
        map_build_irrigation_time(punit->tile)) {
      Terrain_type_id old = map_get_terrain(punit->tile);
      map_irrigate_tile(punit->tile);
      solvency = check_terrain_ocean_land_change(punit->tile, old);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_ROAD) {
    if (total_activity (punit->tile, ACTIVITY_ROAD)
	+ total_activity (punit->tile, ACTIVITY_RAILROAD) >=
        map_build_road_time(punit->tile)) {
      map_set_special(punit->tile, S_ROAD);
      unit_activity_done = TRUE;
    }
  }

  if (activity==ACTIVITY_RAILROAD) {
    if (total_activity (punit->tile, ACTIVITY_RAILROAD)
	>= map_build_rail_time(punit->tile)) {
      map_set_special(punit->tile, S_RAILROAD);
      unit_activity_done = TRUE;
    }
  }
  
  if (activity==ACTIVITY_MINE) {
    if (total_activity (punit->tile, ACTIVITY_MINE) >=
        map_build_mine_time(punit->tile)) {
      Terrain_type_id old = map_get_terrain(punit->tile);
      map_mine_tile(punit->tile);
      solvency = check_terrain_ocean_land_change(punit->tile, old);
      unit_activity_done = TRUE;
      check_adjacent_units = TRUE;
    }
  }

  if (activity==ACTIVITY_TRANSFORM) {
    if (total_activity (punit->tile, ACTIVITY_TRANSFORM) >=
        map_transform_time(punit->tile)) {
      Terrain_type_id old = map_get_terrain(punit->tile);
      map_transform_tile(punit->tile);
      solvency = check_terrain_ocean_land_change(punit->tile, old);
      unit_activity_done = TRUE;
      check_adjacent_units = TRUE;
    }
  }

  if (unit_activity_done) {
    update_tile_knowledge(punit->tile);
    unit_list_iterate (punit->tile->units, punit2) {
      if (punit2->activity == activity) {
	set_unit_activity(punit2, ACTIVITY_IDLE);
	send_unit_info(NULL, punit2);
      }
    } unit_list_iterate_end;
  }

  /* Some units nearby can not continue irrigating */
  if (check_adjacent_units) {
    adjc_iterate(punit->tile, ptile2) {
      unit_list_iterate(ptile2->units, punit2) {
        if (!can_unit_continue_current_activity(punit2)) {
          handle_unit_activity_request(punit2, ACTIVITY_IDLE);
        }
      } unit_list_iterate_end;
    } adjc_iterate_end;
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
    unit_list_iterate_safe(ptile->units, punit2) {
      bool unit_alive = TRUE;

      if (is_ground_unit(punit2)) {
	/* look for nearby land */
	adjc_iterate(punit->tile, ptile2) {
	  if (!is_ocean(ptile2->terrain)
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->tile->x, punit2->tile->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->tile, E_UNIT_RELOCATED,
			     _("Game: Moved your %s due to changing"
			       " land to sea."), unit_name(punit2->type));
	    unit_alive = move_unit(punit2, ptile2, 0);
	    if (unit_alive && punit2->activity == ACTIVITY_SENTRY) {
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
            }
	    goto START;
	  }
	} adjc_iterate_end;
        /* look for nearby transport */
        adjc_iterate(punit->tile, ptile2) {
          if (is_ocean(ptile2->terrain)
              && ground_unit_transporter_capacity(ptile2,
                                                  unit_owner(punit2)) > 0) {
            if (get_transporter_capacity(punit2) > 0)
              sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Embarked %s's %s due to changing land to sea at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->tile->x, punit2->tile->x);
	    notify_player_ex(unit_owner(punit2),
			     punit2->tile, E_UNIT_RELOCATED,
			     _("Game: Embarked your %s due to changing"
			       " land to sea."), unit_name(punit2->type));
	    unit_alive = move_unit(punit2, ptile2, 0);
	    if (unit_alive && punit2->activity == ACTIVITY_SENTRY) {
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
            }
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing land to sea at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->tile->x, punit2->tile->y);
	notify_player_ex(unit_owner(punit2),
			 punit2->tile, E_UNIT_LOST,
			 _("Game: Disbanded your %s due to changing"
			   " land to sea."), unit_name(punit2->type));
	wipe_unit_spec_safe(punit2, FALSE);
	goto START;
      }
    } unit_list_iterate_safe_end;
    break;
  case OLC_OCEAN_TO_LAND:
    unit_list_iterate_safe(ptile->units, punit2) {
      bool unit_alive = TRUE;

      if (is_sailing_unit(punit2)) {
	/* look for nearby water */
	adjc_iterate(punit->tile, ptile2) {
	  if (is_ocean(ptile2->terrain)
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Moved %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->tile->x, punit2->tile->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->tile, E_UNIT_RELOCATED,
			     _("Game: Moved your %s due to changing"
			       " sea to land."), unit_name(punit2->type));
	    unit_alive = move_unit(punit2, ptile2, 0);
	    if (unit_alive && punit2->activity == ACTIVITY_SENTRY) {
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
            }
	    goto START;
	  }
	} adjc_iterate_end;
	/* look for nearby port */
	adjc_iterate(punit->tile, ptile2) {
	  if (is_allied_city_tile(ptile2, unit_owner(punit2))
	      && !is_non_allied_unit_tile(ptile2, unit_owner(punit2))) {
	    if (get_transporter_capacity(punit2) > 0)
	      sentry_transported_idle_units(punit2);
	    freelog(LOG_VERBOSE,
		    "Docked %s's %s due to changing sea to land at (%d, %d).",
		    unit_owner(punit2)->name, unit_name(punit2->type),
		    punit2->tile->x, punit2->tile->y);
	    notify_player_ex(unit_owner(punit2),
			     punit2->tile, E_UNIT_RELOCATED,
			     _("Game: Docked your %s due to changing"
			       " sea to land."), unit_name(punit2->type));
	    unit_alive = move_unit(punit2, ptile2, 0);
	    if (unit_alive && punit2->activity == ACTIVITY_SENTRY) {
	      handle_unit_activity_request(punit2, ACTIVITY_IDLE);
            }
	    goto START;
	  }
	} adjc_iterate_end;
	/* if we get here we could not move punit2 */
	freelog(LOG_VERBOSE,
		"Disbanded %s's %s due to changing sea to land at (%d, %d).",
		unit_owner(punit2)->name, unit_name(punit2->type),
		punit2->tile->x, punit2->tile->y);
	notify_player_ex(unit_owner(punit2),
			 punit2->tile, E_UNIT_LOST,
			 _("Game: Disbanded your %s due to changing"
			   " sea to land."), unit_name(punit2->type));
	wipe_unit_spec_safe(punit2, FALSE);
	goto START;
      }
    } unit_list_iterate_safe_end;
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
static char *get_location_str(struct player *pplayer, struct tile *ptile, bool use_at)
{
  static char buffer[MAX_LEN_NAME+64];
  struct city *incity, *nearcity;

  incity = map_get_city(ptile);
  if (incity) {
    if (use_at) {
      my_snprintf(buffer, sizeof(buffer), _(" at %s"), incity->name);
    } else {
      my_snprintf(buffer, sizeof(buffer), _(" in %s"), incity->name);
    }
  } else {
    nearcity = dist_nearest_city(pplayer, ptile, FALSE, FALSE);
    if (nearcity) {
      if (is_tiles_adjacent(ptile, nearcity->tile)) {
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
char *get_location_str_in(struct player *pplayer, struct tile *ptile)
{
  return get_location_str(pplayer, ptile, FALSE);
}

/**************************************************************************
  See get_location_str() above.
**************************************************************************/
char *get_location_str_at(struct player *pplayer, struct tile *ptile)
{
  return get_location_str(pplayer, ptile, TRUE);
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
				      struct tile **dst_tile)
{
  int bestvalue = 0;
  /* coords of best tile in arg pointers */
  map_city_radius_iterate(pcity->tile, ptile) {
    int value;
    if (is_ocean(ptile->terrain)) {
      continue;
    }
    if (ptile->city)
      continue;
    if (unit_list_size(&ptile->units) > 0)
      continue;
    value = get_virtual_defense_power(U_LAST, u_type, ptile, FALSE, 0);
    value *= 10;

    if (ptile->continent != map_get_continent(pcity->tile)) {
      value /= 2;
    }

    value -= myrand(value/3);

    if (value > bestvalue) {
      *dst_tile = ptile;
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
  struct tile *ptile = NULL;
  int u_type = get_role_unit(L_PARTISAN, 0);

  while ((count--) > 0 && find_a_good_partisan_spot(pcity, u_type, &ptile)) {
    struct unit *punit;
    punit = create_unit(city_owner(pcity), ptile, u_type, 0, 0, -1);
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
bool enemies_at(struct unit *punit, struct tile *ptile)
{
  int a = 0, d, db;
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = ptile->city;

  if (pcity && pplayers_allied(city_owner(pcity), unit_owner(punit))
      && !is_non_allied_unit_tile(ptile, pplayer)) {
    /* We will be safe in a friendly city */
    return FALSE;
  }

  /* Calculate how well we can defend at (x,y) */
  db = get_tile_type(map_get_terrain(ptile))->defense_bonus;
  if (map_has_special(ptile, S_RIVER))
    db += (db * terrain_control.river_defense_bonus) / 100;
  d = unit_def_rating_basic_sq(punit) * db;

  adjc_iterate(ptile, ptile1) {
    if (ai_handicap(pplayer, H_FOG)
	&& !map_is_known_and_seen(ptile1, unit_owner(punit))) {
      /* We cannot see danger at (ptile1) => assume there is none */
      continue;
    }
    unit_list_iterate(ptile1->units, enemy) {
      if (pplayers_at_war(unit_owner(enemy), unit_owner(punit)) 
          && can_unit_attack_unit_at_tile(enemy, punit, ptile)
          && can_unit_attack_all_at_tile(enemy, ptile)) {
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
  struct tile *src_tile = punit->tile, *dst_tile = pcity->tile;

  if (pcity->owner == punit->owner){
    freelog(LOG_VERBOSE, "Teleported %s's %s from (%d, %d) to %s",
	    unit_owner(punit)->name, unit_name(punit->type),
	    src_tile->x, src_tile->y, pcity->name);
    if (verbose) {
      notify_player_ex(unit_owner(punit), pcity->tile, E_NOEVENT,
		       _("Game: Teleported your %s to %s."),
		       unit_name(punit->type), pcity->name);
    }

    /* Silently free orders since they won't be applicable anymore. */
    free_unit_orders(punit);

    if (move_cost == -1)
      move_cost = punit->moves_left;
    return move_unit(punit, dst_tile, move_cost);
  }
  return FALSE;
}

/**************************************************************************
  Teleport or remove a unit due to stack conflict.
**************************************************************************/
void bounce_unit(struct unit *punit, bool verbose)
{
  struct player *pplayer = unit_owner(punit);
  struct city *pcity = find_closest_owned_city(pplayer, punit->tile,
                                               is_sailing_unit(punit), NULL);

  if (pcity) {
    (void) teleport_unit_to_city(punit, pcity, 0, verbose);
  } else {
    /* remove it */
    if (verbose) {
      notify_player_ex(unit_owner(punit), punit->tile, E_NOEVENT,
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
    struct tile *ptile = punit->tile;

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
    struct tile *ptile = punit->tile;

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
    if (map_is_known_and_seen(punit->tile, pplayer) &&
        !can_player_see_unit(pplayer, punit)) {
      unit_goes_out_of_sight(pplayer, punit);
    }
  } unit_list_iterate_end;

  city_list_iterate(aplayer->cities, pcity) {
    /* The player used to know what units were in these cities.  Now that he
     * doesn't, he needs to get a new short city packet updating the
     * occupied status. */
    if (map_is_known_and_seen(pcity->tile, pplayer)) {
      send_city_info(pplayer, pcity);
    }
  } city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
bool is_airunit_refuel_point(struct tile *ptile, struct player *pplayer,
			     Unit_Type_id type, bool unit_is_on_tile)
{
  struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

  if ((is_allied_city_tile(ptile, pplayer)
       && !is_non_allied_unit_tile(ptile, pplayer))
      || (contains_special(plrtile->special, S_AIRBASE)
	  && !is_non_allied_unit_tile(ptile, pplayer)))
    return TRUE;

  if (unit_type_flag(type, F_MISSILE)) {
    int cap = missile_carrier_capacity(ptile, pplayer, FALSE);
    if (unit_is_on_tile)
      cap++;
    return cap>0;
  } else {
    int cap = airunit_carrier_capacity(ptile, pplayer, FALSE);
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
  if (map_has_special(punit->tile, S_FORTRESS)
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

  /* Apply new vision range
   *
   * Unfog (increase seen counter) first, fog (decrease counter)
   * later, so tiles that are within vision range both before and
   * after are not even temporarily marked fogged. */

  if (map_has_special(punit->tile, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    unfog_area(pplayer, punit->tile, get_watchtower_vision(punit));
  else
    unfog_area(pplayer, punit->tile,
	       get_unit_type(to_unit)->vision_range);

  fog_area(pplayer, punit->tile, range);

  send_unit_info(NULL, punit);
  conn_list_do_unbuffer(&pplayer->connections);
}

/************************************************************************* 
  Wrapper of the below
*************************************************************************/
struct unit *create_unit(struct player *pplayer, struct tile *ptile, 
                         Unit_Type_id type, int veteran_level, 
                         int homecity_id, int moves_left)
{
  return create_unit_full(pplayer, ptile, type, veteran_level, homecity_id, 
                          moves_left, -1, NULL);
}

/**************************************************************************
  Creates a unit, and set it's initial values, and put it into the right 
  lists.
  If moves_left is less than zero, unit will get max moves.
**************************************************************************/
struct unit *create_unit_full(struct player *pplayer, struct tile *ptile,
			      Unit_Type_id type, int veteran_level, 
                              int homecity_id, int moves_left, int hp_left,
			      struct unit *ptrans)
{
  struct unit *punit = create_unit_virtual(pplayer, NULL, type, veteran_level);
  struct city *pcity;

  /* Register unit */
  punit->id = get_next_id_number();
  idex_register_unit(punit);

  assert(ptile != NULL);
  punit->tile = ptile;

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

  unit_list_insert(&pplayer->units, punit);
  unit_list_insert(&ptile->units, punit);
  if (pcity && !unit_type_flag(type, F_NOHOME)) {
    assert(city_owner(pcity) == pplayer);
    unit_list_insert(&pcity->units_supported, punit);
    /* Refresh the unit's homecity. */
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  }

  if (map_has_special(ptile, S_FORTRESS)
      && unit_profits_of_watchtower(punit)) {
    unfog_area(pplayer, punit->tile, get_watchtower_vision(punit));
  } else {
    unfog_area(pplayer, ptile, unit_type(punit)->vision_range);
  }

  send_unit_info(NULL, punit);
  maybe_make_contact(ptile, unit_owner(punit));
  wakeup_neighbor_sentries(punit);

  /* The unit may have changed the available tiles in nearby cities. */
  map_city_radius_iterate(ptile, ptile1) {
    struct city *acity = map_get_city(ptile1);

    if (acity) {
      update_city_tile_status_map(acity, ptile);
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
  struct city *pcity = map_get_city(punit->tile);
  struct city *phomecity = find_city_by_id(punit->homecity);
  struct tile *unit_tile = punit->tile;
  struct player *unitowner = unit_owner(punit);

#ifndef NDEBUG
  unit_list_iterate(punit->tile->units, pcargo) {
    assert(pcargo->transported_by != punit->id);
  } unit_list_iterate_end;
#endif

  /* Since settlers plot in new cities in the minimap before they
     are built, so that no two settlers head towards the same city
     spot, we need to ensure this reservation is cleared should
     the settler die on the way. */
  if ((unit_owner(punit)->ai.control || punit->ai.control)
      && punit->ai.ai_role != AIUNIT_NONE) {
    ai_unit_new_role(punit, AIUNIT_NONE, NULL);
  }

  players_iterate(pplayer) {
    if (map_is_known_and_seen(unit_tile, pplayer)) {
      dlsend_packet_unit_remove(&pplayer->connections, punit->id);
    }
  } players_iterate_end;

  remove_unit_sight_points(punit);

  /* check if this unit had F_GAMELOSS flag */
  if (unit_flag(punit, F_GAMELOSS) && unit_owner(punit)->is_alive) {
    notify_conn_ex(&game.est_connections, punit->tile, E_UNIT_LOST,
                   _("Unable to defend %s, %s has lost the game."),
                   unit_name(punit->type), unit_owner(punit)->name);
    notify_player(unit_owner(punit), _("Losing %s meant losing the game! "
                  "Be more careful next time!"), unit_name(punit->type));
    gamelog(GAMELOG_UNITGAMELOSS, punit);
    unit_owner(punit)->is_dying = TRUE;
  }

  game_remove_unit(punit);
  punit = NULL;

  /* Hide any submarines that are no longer visible. */
  conceal_hidden_units(unitowner, unit_tile);

  /* This unit may have blocked tiles of adjacent cities. Update them. */
  map_city_radius_iterate(unit_tile, ptile1) {
    struct city *pcity = map_get_city(ptile1);
    if (pcity) {
      update_city_tile_status_map(pcity, unit_tile);
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
  if (pcity && unit_list_size(&unit_tile->units) == 0) {
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
  struct tile *ptile = punit->tile;
  struct player *pplayer = unit_owner(punit);
  struct unit_type *ptype = unit_type(punit);

  /* First pull all units off of the transporter. */
  if (get_transporter_capacity(punit) > 0) {
    /* FIXME: there's no send_unit_info call below so these units aren't
     * updated at the client.  I guess this works because the unit info
     * will be sent eventually anyway, but it's surely a bug. */
    unit_list_iterate(ptile->units, pcargo) {
      if (pcargo->transported_by == punit->id) {
	/* Could use unload_unit_from_transporter here, but that would
	 * call send_unit_info for the transporter unnecessarily. */
	pull_unit_from_transporter(pcargo, punit);
	if (pcargo->activity == ACTIVITY_SENTRY) {
	  /* Activate sentried units - like planes on a disbanded carrier.
	   * Note this will activate ground units even if they just change
	   * transporter. */
	  set_unit_activity(pcargo, ACTIVITY_IDLE);
	}
	send_unit_info(NULL, pcargo);
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
      && is_ocean(map_get_terrain(ptile))
      && !map_get_city(ptile)) {
    struct city *pcity = NULL;
    int capacity = ground_unit_transporter_capacity(ptile, pplayer);

    /* Get rid of excess standard units. */
    if (capacity < 0) {
      unit_list_iterate_safe(ptile->units, pcargo) {
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
      unit_list_iterate_safe(ptile->units, pcargo) {
	if (is_ground_unit(pcargo) && pcargo->transported_by == -1) {
	  if (unit_flag(pcargo, F_UNDISBANDABLE)) {
	    pcity = find_closest_owned_city(unit_owner(pcargo),
					    pcargo->tile, TRUE, NULL);
	    if (pcity && teleport_unit_to_city(pcargo, pcity, 0, FALSE)) {
	      notify_player_ex(pplayer, ptile, E_NOEVENT,
			       _("Game: %s escaped the destruction of %s, and "
				 "fled to %s."), unit_type(pcargo)->name,
			       ptype->name, pcity->name);
	    }
	  }
	  if (!unit_flag(pcargo, F_UNDISBANDABLE) || !pcity) {
	    notify_player_ex(pplayer, ptile, E_UNIT_LOST,
			     _("Game: %s lost when %s was lost."),
			     unit_type(pcargo)->name,
			     ptype->name);
	    gamelog(GAMELOG_UNITLOSS, pcargo, NULL, "transport lost");
	    server_remove_unit(pcargo);
	  }
	  if (++capacity >= 0) {
	    break;
	  }
	}
      } unit_list_iterate_safe_end;
    }

    /* Reassign existing units.  This is an O(n^2) operation as written. */
    unit_list_iterate(ptile->units, ptrans) {
      if (is_ground_units_transport(ptrans)) {
	int occupancy = get_transporter_occupancy(ptrans);

	unit_list_iterate(ptile->units, pcargo) {
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
  char *loc_str = get_location_str_in(pplayer, punit->tile);
  int num_killed[MAX_NUM_PLAYERS + MAX_NUM_BARBARIANS];
  int ransom, unitcount = 0;
  
  /* barbarian leader ransom hack */
  if( is_barbarian(pplayer) && unit_has_role(punit->type, L_BARBARIAN_LEADER)
      && (unit_list_size(&punit->tile->units) == 1)
      && (is_ground_unit(pkiller) || is_heli_unit(pkiller)) ) {
    ransom = (pplayer->economic.gold >= 100)?100:pplayer->economic.gold;
    notify_player_ex(destroyer, pkiller->tile, E_UNIT_WIN_ATT,
		     _("Game: Barbarian leader captured, %d gold ransom paid."),
                     ransom);
    destroyer->economic.gold += ransom;
    pplayer->economic.gold -= ransom;
    send_player_info(destroyer, NULL);   /* let me see my new gold :-) */
    unitcount = 1;
  }

  if (unitcount == 0) {
    unit_list_iterate(punit->tile->units, vunit)
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(vunit)))
	unitcount++;
    unit_list_iterate_end;
  }

  if (!is_stack_vulnerable(punit->tile) || unitcount == 1) {
    notify_player_ex(pplayer, punit->tile, E_UNIT_LOST,
		     _("Game: %s lost to an attack by %s's %s%s."),
		     unit_type(punit)->name, destroyer->name,
		     unit_name(pkiller->type), loc_str);

    gamelog(GAMELOG_UNITLOSS, punit, destroyer);
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
    unit_list_iterate(punit->tile->units, vunit)
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(vunit)))
	num_killed[vunit->owner]++;
    unit_list_iterate_end;

    /* inform the owners */
    for (i = 0; i<MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS; i++) {
      if (num_killed[i]>0) {
	notify_player_ex(get_player(i), punit->tile, E_UNIT_LOST,
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
    unit_list_iterate(punit->tile->units, punit2) {
      if (pplayers_at_war(unit_owner(pkiller), unit_owner(punit2))) {
	notify_player_ex(unit_owner(punit2), 
			 punit2->tile, E_UNIT_LOST,
			 _("Game: %s lost to an attack"
			   " from %s's %s."),
			 unit_type(punit2)->name, destroyer->name,
			 unit_name(pkiller->type));

        gamelog(GAMELOG_UNITLOSS, punit2, destroyer);
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
  packet->x = punit->tile->x;
  packet->y = punit->tile->y;
  packet->homecity = punit->homecity;
  packet->veteran = punit->veteran;
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
  if (punit->goto_tile) {
    packet->goto_dest_x = punit->goto_tile->x;
    packet->goto_dest_y = punit->goto_tile->y;
  } else {
    packet->goto_dest_x = 255;
    packet->goto_dest_y = 255;
    assert(!is_normal_map_pos(255, 255));
  }
  packet->activity_target = punit->activity_target;
  packet->paradropped = punit->paradropped;
  packet->connecting = FALSE;
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
      packet->orders_activities[i] = punit->orders.list[i].activity;
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
  packet->x = punit->tile->x;
  packet->y = punit->tile->y;
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
				 struct tile *ptile, bool remove_unseen)
{
  struct packet_unit_info info;
  struct packet_unit_short_info sinfo;
  bool new_information_for_enemy = FALSE;

  if (!dest) {
    dest = &game.game_connections;
  }

/* maybe the wrong position for that, but this is the lowlevel function
 * where we check if we have to increase timeout, or remove_turn_done */
  
  package_unit(punit, &info);
  package_short_unit(punit, &sinfo, UNIT_INFO_IDENTITY, FALSE, FALSE);
            
  conn_list_iterate(*dest, pconn) {
    struct player *pplayer = pconn->player;
    
    if ((!pplayer && pconn->observer) 
	|| (pplayer && pplayer->player_no == punit->owner)) {
      send_packet_unit_info(pconn, &info);
    } else if (pplayer) {
      bool see_in_old;
      bool see_in_new = can_player_see_unit_at(pplayer, punit, punit->tile);

      if (punit->tile == ptile) {
	/* This is not about movement */
	see_in_old = see_in_new;
      } else {
	see_in_old = can_player_see_unit_at(pplayer, punit, ptile);
      }

      if (see_in_new || see_in_old) {
        /* First send movement */
        send_packet_unit_short_info(pconn, &sinfo);

        if (!see_in_new) {
          /* Then remove unit if necessary */
          unit_goes_out_of_sight(pplayer, punit);
        }
	if (pplayers_at_war(pplayer,unit_owner(punit))
	    && !pplayer->ai.control) {
	  /* increase_timeout_because_unit_moved(pplayer) possible here */
	   new_information_for_enemy = TRUE;
	}
      } else {
	if (remove_unseen) {
	  dsend_packet_unit_remove(pconn, punit->id);
	}
      }
    }
  } conn_list_iterate_end;
  
  if (game.timeout != 0 && new_information_for_enemy){
    increase_timeout_because_unit_moved();
  }

}

/**************************************************************************
  send the unit to the players who need the info 
  dest = NULL means all connections (game.game_connections)
**************************************************************************/
void send_unit_info(struct player *dest, struct unit *punit)
{
  struct conn_list *conn_dest = (dest ? &dest->connections
				 : &game.game_connections);
  send_unit_info_to_onlookers(conn_dest, punit, punit->tile, FALSE);
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
	    || map_is_known_and_seen(punit->tile, pplayer)) {
	  send_unit_info_to_onlookers(&pconn->self, punit,
				      punit->tile, FALSE);
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
  struct tile *ptile = ptrans->tile;

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
static void do_nuke_tile(struct player *pplayer, struct tile *ptile)
{
  struct city *pcity = map_get_city(ptile);

  unit_list_iterate(ptile->units, punit) {
    notify_player_ex(unit_owner(punit), ptile, E_UNIT_LOST,
		     _("Game: Your %s was nuked by %s."),
		     unit_name(punit->type),
		     pplayer == unit_owner(punit) ? _("yourself") : pplayer->name);
    if (unit_owner(punit) != pplayer) {
      notify_player_ex(pplayer,
		       ptile, E_UNIT_WIN,
		       _("Game: %s's %s was nuked."),
		       unit_owner(punit)->name,
		       unit_name(punit->type));
    }
    wipe_unit_spec_safe(punit, FALSE);
  } unit_list_iterate_end;

  if (pcity) {
    notify_player_ex(city_owner(pcity),
		     ptile, E_CITY_NUKED,
		     _("Game: %s was nuked by %s."),
		     pcity->name,
		     pplayer == city_owner(pcity) ? _("yourself") : pplayer->name);

    if (city_owner(pcity) != pplayer) {
      notify_player_ex(pplayer,
		       ptile, E_CITY_NUKED,
		       _("Game: You nuked %s."),
		       pcity->name);
    }

    city_reduce_size(pcity, pcity->size / 2);
  }

  if (!is_ocean(map_get_terrain(ptile)) && myrand(2) == 1) {
    if (game.rgame.nuke_contamination == CONTAMINATION_POLLUTION) {
      if (!map_has_special(ptile, S_POLLUTION)) {
	map_set_special(ptile, S_POLLUTION);
	update_tile_knowledge(ptile);
      }
    } else {
      if (!map_has_special(ptile, S_FALLOUT)) {
	map_set_special(ptile, S_FALLOUT);
	update_tile_knowledge(ptile);
      }
    }
  }
}

/**************************************************************************
  Nuke all the squares in a 3x3 square around the center of the explosion
  pplayer is the player that caused the explosion. You lose reputation
  each time.
**************************************************************************/
void do_nuclear_explosion(struct player *pplayer, struct tile *ptile)
{
  square_iterate(ptile, 1, ptile1) {
    do_nuke_tile(pplayer, ptile1);
  } square_iterate_end;

  /* Give reputation penalty to nuke users */
  pplayer->reputation = MAX(pplayer->reputation - REPUTATION_LOSS_NUKE, 0);
  send_player_info(pplayer, NULL);

  notify_conn_ex(&game.game_connections, ptile, E_NUKE,
		 _("Game: %s detonated a nuke!"), pplayer->name);
}

/**************************************************************************
Move the unit if possible 
  if the unit has less moves than it costs to enter a square, we roll the dice:
  1) either succeed
  2) or have it's moves set to 0
  a unit can always move atleast once
**************************************************************************/
 bool try_move_unit(struct unit *punit, struct tile *dst_tile)
{
  if (myrand(1 + map_move_cost(punit, dst_tile)) > punit->moves_left
      && punit->moves_left<unit_move_rate(punit)) {
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
  struct tile *src_tile = punit->tile;
  struct city *city1 = src_tile->city;

  if (!city1)
    return FALSE;
  if (!unit_can_airlift_to(punit, city2))
    return FALSE;
  if (get_transporter_occupancy(punit) > 0) {
    return FALSE;
  }
  city1->airlift = FALSE;
  city2->airlift = FALSE;

  notify_player_ex(unit_owner(punit), city2->tile, E_NOEVENT,
		   _("Game: %s transported succesfully."),
		   unit_name(punit->type));

  (void) move_unit(punit, city2->tile, punit->moves_left);

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
bool do_paradrop(struct unit *punit, struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);

  if (!unit_flag(punit, F_PARATROOPERS)) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
                     _("Game: This unit type can not be paradropped."));
    return FALSE;
  }

  if (!can_unit_paradrop(punit)) {
    return FALSE;
  }

  if (get_transporter_occupancy(punit) > 0) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
		     _("Game: You cannot paradrop a transporter unit."));
  }

  if (!map_is_known(ptile, pplayer)) {
    notify_player_ex(pplayer, ptile, E_NOEVENT,
                     _("Game: The destination location is not known."));
    return FALSE;
  }

  if (is_ocean(map_get_player_tile(ptile, pplayer)->terrain)
      && is_ground_unit(punit)) {
    notify_player_ex(pplayer, ptile, E_NOEVENT,
                     _("Game: This unit cannot paradrop into ocean."));
    return FALSE;    
  }

  if (map_is_known_and_seen(ptile, pplayer)
      && ((ptile->city
	  && pplayers_non_attack(pplayer, city_owner(ptile->city)))
      || is_non_attack_unit_tile(ptile, pplayer))) {
    notify_player_ex(pplayer, ptile, E_NOEVENT,
                     _("Game: Cannot attack unless you declare war first."));
    return FALSE;    
  }

  {
    int range = unit_type(punit)->paratroopers_range;
    int distance = real_map_distance(punit->tile, ptile);
    if (distance > range) {
      notify_player_ex(pplayer, ptile, E_NOEVENT,
                       _("Game: The distance to the target (%i) "
                         "is greater than the unit's range (%i)."),
                       distance, range);
      return FALSE;
    }
  }

  if (is_ocean(map_get_terrain(ptile))
      && is_ground_unit(punit)) {
    int srange = unit_type(punit)->vision_range;

    show_area(pplayer, ptile, srange);

    notify_player_ex(pplayer, ptile, E_UNIT_LOST,
                     _("Game: Your %s paradropped into the ocean "
                       "and was lost."),
                     unit_type(punit)->name);
    server_remove_unit(punit);
    return TRUE;
  }

  if ((ptile->city && pplayers_non_attack(pplayer, city_owner(ptile->city)))
      || is_non_allied_unit_tile(ptile, pplayer)) {
    int srange = unit_type(punit)->vision_range;
    show_area(pplayer, ptile, srange);
    maybe_make_contact(ptile, pplayer);
    notify_player_ex(pplayer, ptile, E_UNIT_LOST_ATT,
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
    return move_unit(punit, ptile, move_cost);
  }
}

/**************************************************************************
  Get gold from entering a hut.
**************************************************************************/
static void hut_get_gold(struct unit *punit, int cred)
{
  struct player *pplayer = unit_owner(punit);
  notify_player_ex(pplayer, punit->tile, E_HUT_GOLD,
		   _("Game: You found %d gold."), cred);
  pplayer->economic.gold += cred;
}

/**************************************************************************
  Get a tech from entering a hut.
**************************************************************************/
static void hut_get_tech(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  Tech_Type_id new_tech;
  const char *tech_name;
  
  new_tech = give_random_free_tech(pplayer);

  tech_name = get_tech_name(pplayer, new_tech);
  notify_player_ex(pplayer, punit->tile, E_HUT_TECH,
		   _("Game: You found %s in ancient scrolls of wisdom."),
		   tech_name);
  gamelog(GAMELOG_TECH, pplayer, NULL, new_tech);
  notify_embassies(pplayer, NULL, _("Game: The %s have acquired %s"
				    " from ancient scrolls of wisdom."),
		   get_nation_name_plural(pplayer->nation), tech_name);
}

/**************************************************************************
  Get a mercenary unit from entering a hut.
**************************************************************************/
static void hut_get_mercenaries(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  
  notify_player_ex(pplayer, punit->tile, E_HUT_MERC,
		   _("Game: A band of friendly mercenaries joins your cause."));
  (void) create_unit(pplayer, punit->tile,
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

  if (city_exists_within_city_radius(punit->tile, TRUE)
      || unit_flag(punit, F_GAMELOSS)) {
    notify_player_ex(pplayer, punit->tile, E_HUT_BARB_CITY_NEAR,
		     _("Game: An abandoned village is here."));
  } else {
    /* save coords and type in case unit dies */
    struct tile *unit_tile = punit->tile;
    Unit_Type_id type = punit->type;

    ok = unleash_barbarians(unit_tile);

    if (ok) {
      notify_player_ex(pplayer, unit_tile, E_HUT_BARB,
		       _("Game: You have unleashed a horde of barbarians!"));
    } else {
      notify_player_ex(pplayer, unit_tile, E_HUT_BARB_KILLED,
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

  if (city_can_be_built_here(punit->tile, NULL)) {
    notify_player_ex(pplayer, punit->tile, E_HUT_CITY,
		     _("Game: You found a friendly city."));
    create_city(pplayer, punit->tile,
		city_name_suggestion(pplayer, punit->tile));

    if (unit_flag(punit, F_CITIES) || unit_flag(punit, F_SETTLERS)) {
      /* In case city was found during autosettler activities */
      initialize_infrastructure_cache(pplayer);
    }

    /* Init ai.choice. Handling ferryboats might use it. */
    init_choice(&punit->tile->city->ai.choice);

  } else {
    notify_player_ex(pplayer, punit->tile, E_HUT_SETTLER,
		     _("Game: Friendly nomads are impressed by you,"
		       " and join you."));
    (void) create_unit(pplayer, punit->tile, get_role_unit(F_CITIES,0),
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

  map_clear_special(punit->tile, S_HUT);
  update_tile_knowledge(punit->tile);

  if (game.rgame.hut_overflight==OVERFLIGHT_FRIGHTEN && is_air_unit(punit)) {
    notify_player_ex(pplayer, punit->tile, E_NOEVENT,
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
  square_iterate(punit->tile, 3, ptile) {
    unit_list_iterate(ptile->units, penemy) {
      int range;
      enum unit_move_type move_type = unit_type(penemy)->move_type;
      Terrain_type_id terrain = map_get_terrain(ptile);

      if (map_has_special(ptile, S_FORTRESS)
	  && unit_profits_of_watchtower(penemy))
	range = get_watchtower_vision(penemy);
      else
	range = unit_type(penemy)->vision_range;
      
      if (!pplayers_allied(unit_owner(punit), unit_owner(penemy))
	  && penemy->activity == ACTIVITY_SENTRY
	  && range >= real_map_distance(punit->tile, ptile)
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
  square_iterate(punit->tile, 3, ptile) {
    unit_list_iterate(ptile->units, ppatrol) {
      if (punit != ppatrol
	  && unit_has_orders(ppatrol)
	  && ppatrol->orders.vigilant) {
	if (maybe_cancel_patrol_due_to_enemy(ppatrol)) {
	  notify_player_ex(unit_owner(ppatrol), ppatrol->tile, E_UNIT_ORDERS, 
			   _("Game: Your %s cancelled patrol order because it "
			     "encountered a foreign unit."),
			   unit_name(ppatrol->type));
	}
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
static void handle_unit_move_consequences(struct unit *punit,
					  struct tile *src_tile,
					  struct tile *dst_tile)
{
  struct city *fromcity = map_get_city(src_tile);
  struct city *tocity = map_get_city(dst_tile);
  struct city *homecity = NULL;
  struct player *pplayer = unit_owner(punit);
  /*  struct government *g = get_gov_pplayer(pplayer);*/
  bool refresh_homecity = FALSE;

  if (punit->homecity != 0)
    homecity = find_city_by_id(punit->homecity);

  if (tocity)
    handle_unit_enter_city(punit, tocity);

  /* We only do this for non-AI players to now make sure the AI turns
     doesn't take too long. Perhaps we should make a special refresh_city
     functions that only refreshed happiness. */
  if (!pplayer->ai.control) {
    /* might have changed owners or may be destroyed */
    tocity = map_get_city(dst_tile);

    if (tocity) { /* entering a city */
      if (tocity->owner == punit->owner) {
	if (tocity != homecity) {
	  city_refresh(tocity);
	  send_city_info(pplayer, tocity);
	}
      }

      if (homecity) {
        refresh_homecity = TRUE;
      }
    }

    if (fromcity) { /* leaving a city */
      if (homecity) {
	refresh_homecity = TRUE;
      }
      if (fromcity != homecity && fromcity->owner == punit->owner) {
	city_refresh(fromcity);
	send_city_info(pplayer, fromcity);
      }
    }

    /* entering/leaving a fortress or friendly territory */
    if (homecity) {
      if ((game.happyborders > 0 && src_tile->owner != dst_tile->owner)
          ||
	  (map_has_special(dst_tile, S_FORTRESS)
	   && is_friendly_city_near(unit_owner(punit), dst_tile))
	  ||
          (map_has_special(src_tile, S_FORTRESS) 
	   && is_friendly_city_near(unit_owner(punit), src_tile))) {
        refresh_homecity = TRUE;
      }
    }
    
    if (refresh_homecity) {
      city_refresh(homecity);
      send_city_info(pplayer, homecity);
    }
  }


  /* The unit block different tiles of adjacent enemy cities before and
     after. Update the relevant cities. */

  /* First check cities near the source. */
  map_city_radius_iterate(src_tile, tile1) {
    struct city *pcity = map_get_city(tile1);

    if (pcity && update_city_tile_status_map(pcity, src_tile)) {
      auto_arrange_workers(pcity);
      send_city_info(NULL, pcity);
    }
  } map_city_radius_iterate_end;
  /* Then check cities near the destination. */
  map_city_radius_iterate(dst_tile, tile1) {
    struct city *pcity = map_get_city(tile1);
    if (pcity && update_city_tile_status_map(pcity, dst_tile)) {
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
      && punit->activity != ACTIVITY_GOTO) {
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

  Returns TRUE iff unit still alive.
**************************************************************************/
bool move_unit(struct unit *punit, struct tile *pdesttile, int move_cost)
{
  struct player *pplayer = unit_owner(punit);
  struct tile *psrctile = punit->tile;
  struct city *pcity;
  struct unit *ptransporter = NULL;
    
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
      unfog_area(unit_owner(pcargo), pdesttile, unit_type(pcargo)->vision_range);

      /* Silently free orders since they won't be applicable anymore. */
      free_unit_orders(pcargo);

      pcargo->tile = pdesttile;

      unit_list_insert(&pdesttile->units, pcargo);
      check_unit_activity(pcargo);
      send_unit_info_to_onlookers(NULL, pcargo, psrctile, FALSE);
      fog_area(unit_owner(pcargo), psrctile, unit_type(pcargo)->vision_range);
      handle_unit_move_consequences(pcargo, psrctile, pdesttile);
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
    unfog_area(pplayer, pdesttile, get_watchtower_vision(punit));
  } else {
    unfog_area(pplayer, pdesttile, unit_type(punit)->vision_range);
  }

  unit_list_unlink(&psrctile->units, punit);
  punit->tile = pdesttile;
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
  send_unit_info_to_onlookers(NULL, punit, psrctile, FALSE);
    
  /* Special checks for ground units in the ocean. */
  if (!can_unit_survive_at_tile(punit, pdesttile)) {
    ptransporter = find_transporter_for_unit(punit, pdesttile);
    if (ptransporter) {
      put_unit_onto_transporter(punit, ptransporter);
    }

    /* Set activity to sentry if boarding a ship. */
    if (ptransporter && !pplayer->ai.control && !unit_has_orders(punit)
	&& !can_unit_exist_at_tile(punit, pdesttile)) {
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
    send_unit_info_to_onlookers(NULL, punit, punit->tile, TRUE);
  }
  
  if ((pcity = map_get_city(psrctile))) {
    refresh_dumb_city(pcity);
  }
  if ((pcity = map_get_city(pdesttile))) {
    refresh_dumb_city(pcity);
  }

  /* The hidden units might not have been previously revealed 
   * because when we did the unfogging, the unit was still 
   * at (src_x, src_y) */
  reveal_hidden_units(pplayer, pdesttile);

  if (unit_profits_of_watchtower(punit)
      && tile_has_special(psrctile, S_FORTRESS)) {
    fog_area(pplayer, psrctile, get_watchtower_vision(punit));
  } else {
    fog_area(pplayer, psrctile, unit_type(punit)->vision_range);
  }

  /* Remove hidden units (like submarines) which aren't seen anymore. */
  square_iterate(psrctile, 1, tile1) {
    players_iterate(pplayer) {
      /* We're only concerned with known, unfogged tiles which may contain
       * hidden units that are no longer visible.  These units will not
       * have been handled by the fog code, above. */
      if (map_get_known(tile1, pplayer) == TILE_KNOWN) {
        unit_list_iterate(tile1->units, punit2) {
          if (punit2 != punit && !can_player_see_unit(pplayer, punit2)) {
	    unit_goes_out_of_sight(pplayer, punit2);
	  }
        } unit_list_iterate_end;
      }
    } players_iterate_end;
  } square_iterate_end;

  handle_unit_move_consequences(punit, psrctile, pdesttile);
  wakeup_neighbor_sentries(punit);
  maybe_make_contact(pdesttile, unit_owner(punit));

  conn_list_do_unbuffer(&pplayer->connections);

  /* Note, an individual call to move_unit may leave things in an unstable
   * state (e.g., negative transporter capacity) if more than one unit is
   * being moved at a time (e.g., bounce unit) and they are not done in the
   * right order.  This is probably not a bug. */

  if (map_has_special(pdesttile, S_HUT)) {
    return unit_enter_hut(punit);
  } else {
    return TRUE;
  }
}

/**************************************************************************
  Maybe cancel the goto if there is an enemy in the way
**************************************************************************/
static bool maybe_cancel_goto_due_to_enemy(struct unit *punit, 
                                           struct tile *ptile)
{
  struct player *pplayer = unit_owner(punit);
  
  if (is_non_allied_unit_tile(ptile, pplayer) 
      || is_non_allied_city_tile(ptile, pplayer)) {
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

  if (map_has_special(punit->tile, S_FORTRESS)
      && unit_profits_of_watchtower(punit))
    range = get_watchtower_vision(punit);
  else
    range = unit_type(punit)->vision_range;
  
  square_iterate(punit->tile, range, ptile) {
    struct unit *penemy =
	is_non_allied_unit_tile(ptile, pplayer);
    struct dumb_city *pdcity = map_get_player_tile(ptile, pplayer)->city;

    if ((penemy && can_player_see_unit(pplayer, penemy))
	|| (pdcity && !pplayers_allied(pplayer, get_player(pdcity->owner))
	    && pdcity->occupied)) {
      cancel = TRUE;
      break;
    }
  } square_iterate_end;

  if (cancel) {
    handle_unit_activity_request(punit, ACTIVITY_IDLE);
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
  struct tile *dst_tile;
  bool res, last_order;
  int unitid = punit->id;
  struct player *pplayer = unit_owner(punit);
  int moves_made = 0;
  enum unit_activity activity;

  assert(unit_has_orders(punit));

  if (punit->activity != ACTIVITY_IDLE) {
    /* Unit's in the middle of an activity; wait for it to finish. */
    punit->done_moving = TRUE;
    return TRUE;
  }

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
      notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
		       _("Game: Orders for %s aborted as there "
			 "are units nearby."),
		       unit_name(punit->type));
      return TRUE;
    }

    if (moves_made == punit->orders.length) {
      /* For repeating orders, don't repeat more than once per turn. */
      freelog(LOG_DEBUG, "  stopping because we ran a round");
      punit->done_moving = TRUE;
      send_unit_info(NULL, punit);
      return TRUE;
    }
    moves_made++;

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
    case ORDER_FULL_MP:
      if (punit->moves_left != unit_move_rate(punit)) {
	/* If the unit doesn't have full MP then it just waits until the
	 * next turn.  We assume that the next turn it will have full MP
	 * (there's no check for that). */
	punit->done_moving = TRUE;
	freelog(LOG_DEBUG, "  waiting this turn");
	send_unit_info(NULL, punit);
      }
      break;
    case ORDER_ACTIVITY:
      activity = order.activity;
      if (!can_unit_do_activity(punit, activity)) {
	cancel_orders(punit, "  orders canceled because of failed activity");
	notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted since they "
			   "give an invalid activity."),
			 unit_name(punit->type));
	return TRUE;
      }
      punit->done_moving = TRUE;
      set_unit_activity(punit, activity);
      send_unit_info(NULL, punit);
      break;
    case ORDER_MOVE:
      /* Move unit */
      if (!(dst_tile = mapstep(punit->tile, order.dir))) {
	cancel_orders(punit, "  move order sent us to invalid location");
	notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted since they "
			   "give an invalid location."),
			 unit_name(punit->type));
	return TRUE;
      }

      if (!last_order
	  && maybe_cancel_goto_due_to_enemy(punit, dst_tile)) {
	cancel_orders(punit, "  orders canceled because of enemy");
	notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted as there "
			   "are units in the way."),
			 unit_name(punit->type));
	return TRUE;
      }

      freelog(LOG_DEBUG, "  moving to %d,%d",
	      dst_tile->x, dst_tile->y);
      res = handle_unit_move_request(punit, dst_tile, FALSE, !last_order);
      if (!player_find_unit_by_id(pplayer, unitid)) {
	freelog(LOG_DEBUG, "  unit died while moving.");
	/* A player notification should already have been sent. */
	return FALSE;
      }

      if (res && !same_pos(dst_tile, punit->tile)) {
	/* Movement succeeded but unit didn't move. */
	freelog(LOG_DEBUG, "  orders resulted in combat.");
	send_unit_info(NULL, punit);
	return TRUE;
      }

      if (!res && punit->moves_left > 0) {
	/* Movement failed (ZOC, etc.) */
	cancel_orders(punit, "  attempt to move failed.");
	notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
			 _("Game: Orders for %s aborted because of "
			   "failed move."),
			 unit_name(punit->type));
	return TRUE;
      }

      if (!res && punit->moves_left == 0) {
	/* Movement failed (not enough MP).  Keep this move around for
	 * next turn. */
	freelog(LOG_DEBUG, "  orders move failed (out of MP).");
	if (unit_has_orders(punit)) {
	  punit->orders.index--;
	} else {
	  /* FIXME: If this was the last move, the orders will already have
	   * been freed, so we have to add them back on.  This is quite a
	   * hack; one problem is that the no-orders unit has probably
	   * already had its unit info sent out to the client. */
	  punit->has_orders = TRUE;
	  punit->orders.length = 1;
	  punit->orders.index = 0;
	  punit->orders.repeat = FALSE;
	  punit->orders.vigilant = FALSE;
	  punit->orders.list = fc_malloc(sizeof(order));
	  punit->orders.list[0] = order;
	}
	send_unit_info(NULL, punit);
	return TRUE;
      }

      break;
    case ORDER_LAST:
      cancel_orders(punit, "  client sent invalid order!");
      notify_player_ex(pplayer, punit->tile, E_UNIT_ORDERS,
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
