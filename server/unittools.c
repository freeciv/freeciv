/***********************************************************************
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
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "support.h"

/* common */
#include "base.h"
#include "city.h"
#include "combat.h"
#include "events.h"
#include "game.h"
#include "government.h"
#include "idex.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "terrain.h"
#include "unit.h"
#include "unitlist.h"
#include "unittype.h"

/* common/scriptcore */
#include "luascript_signal.h"
#include "luascript_types.h"

/* aicore */
#include "path_finding.h"
#include "pf_tools.h"

/* server/scripting */
#include "script_server.h"

/* server */
#include "actiontools.h"
#include "aiiface.h"
#include "barbarian.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "gamehand.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "sernet.h"
#include "srv_main.h"
#include "techtools.h"
#include "unithand.h"

/* server/advisors */
#include "advgoto.h"
#include "autoexplorer.h"
#include "autoworkers.h"

/* ai */
#include "handicaps.h"

#include "unittools.h"


/* Tools for controlling the client vision of every unit when a unit
 * moves + script effects. See unit_move(). You can access this data with
 * punit->server.moving; it may be NULL if the unit is not moving). */
struct unit_move_data {
  int ref_count;
  struct unit *punit; /* NULL for invalidating. */
  struct player *powner;
  bv_player can_see_unit;
  bv_player can_see_move;
  struct vision *old_vision;
};

#define SPECLIST_TAG unit_move_data
#include "speclist.h"
#define unit_move_data_list_iterate(_plist, _pdata)                         \
  TYPED_LIST_ITERATE(struct unit_move_data, _plist, _pdata)
#define unit_move_data_list_iterate_end LIST_ITERATE_END
#define unit_move_data_list_iterate_rev(_plist, _pdata)                     \
  TYPED_LIST_ITERATE_REV(struct unit_move_data, _plist, _pdata)
#define unit_move_data_list_iterate_rev_end LIST_ITERATE_REV_END

/* This data structure lets the auto attack code cache each potential
 * attacker unit's probability of success against the target unit during
 * the checks if the unit can do autoattack. It is then reused when the
 * list of potential attackers is sorted by probability of success. */
struct autoattack_prob {
  int unit_id;
  struct act_prob prob;
};

#define SPECLIST_TAG autoattack_prob
#define SPECLIST_TYPE struct autoattack_prob
#include "speclist.h"

#define autoattack_prob_list_iterate_safe(autoattack_prob_list, _aap_,     \
                                          _unit_)                          \
  TYPED_LIST_ITERATE(struct autoattack_prob, autoattack_prob_list, _aap_)  \
  struct unit *_unit_ = game_unit_by_number(_aap_->unit_id);               \
                                                                           \
  if (_unit_ == NULL) {                                                    \
    continue;                                                              \
  }

#define autoattack_prob_list_iterate_safe_end  LIST_ITERATE_END

static void unit_restore_hitpoints(struct unit *punit);
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit);
static void update_unit_activity(struct unit *punit);
static bool try_to_save_unit(struct unit *punit, const struct unit_type *pttype,
                             bool helpless, bool teleporting,
                             const struct city *pexclcity);
static void wakeup_neighbor_sentries(struct unit *punit);
static void do_upgrade_effects(struct player *pplayer);

static bool maybe_cancel_patrol_due_to_enemy(struct unit *punit);

static bool maybe_become_veteran_real(struct unit *punit, int base_chance,
                                      bool worker);

static void unit_transport_load_tp_status(struct unit *punit,
                                               struct unit *ptrans,
                                               bool force);

static void wipe_unit_full(struct unit *punit, bool transported,
                           enum unit_loss_reason reason,
                           struct player *killer);

static int get_unit_vision_base(const struct unit *punit,
                                enum vision_layer vlayer,
                                const int base);
static int unit_vision_range_modifiers(const struct unit *punit,
                                       const struct tile *ptile);

/**********************************************************************//**
  Returns a unit type that matches the role_tech or role roles.

  If role_tech is given, then we look at all units with this role
  whose requirements are met by any player, and return a random one.  This
  can be used to give a unit to barbarians taken from the set of most
  advanced units researched by the 'real' players.

  If role_tech is not give (-1) or if there are no matching unit types,
  then we look at 'role' value and return a random matching unit type.

  It is an error if there are no available units.  This function will
  always return a valid unit.
**************************************************************************/
struct unit_type *find_a_unit_type(enum unit_role_id role,
                                   enum unit_role_id role_tech)
{
  struct unit_type *which[U_LAST];
  int i, num = 0;

  if (role_tech != -1) {
    for (i = 0; i < num_role_units(role_tech); i++) {
      struct unit_type *iunit = get_role_unit(role_tech, i);
      const int minplayers = 2;
      int players = 0;

      /* Note, if there's only one player in the game this check will always
       * fail. */
      players_iterate(pplayer) {
        if (!is_barbarian(pplayer)
            && can_player_build_unit_direct(pplayer, iunit, TRUE)) {
          players++;
        }
      } players_iterate_end;

      if (players > minplayers) {
	which[num++] = iunit;
      }
    }
  }
  if (num == 0) {
    for (i = 0; i < num_role_units(role); i++) {
      which[num++] = get_role_unit(role, i);
    }
  }

  /* Ruleset code should ensure there is at least one unit for each
   * possibly-required role, or check before calling this function. */
  fc_assert_exit_msg(0 < num, "No unit types in find_a_unit_type(%d, %d)!",
                     role, role_tech);

  return which[fc_rand(num)];
}

/**********************************************************************//**
  Unit has a chance to become veteran. This should not be used for settlers
  for the work they do.
**************************************************************************/
bool maybe_make_veteran(struct unit *punit, int base_chance)
{
  return maybe_become_veteran_real(punit, base_chance, FALSE);
}

/**********************************************************************//**
  After a battle and after diplomatic aggression this routine is called
  to decide whether or not the unit should become more experienced.

  There is a specified chance for it to happen. Before the chance
  is passed to this function, it might get modified by the factor
  related to how challenging the battle was. This function applies
  modifiers from effects, and veterancy system.

  @param punit       Unit that might gain veterancy
  @param base_chance Base percentage chance of gaining veterancy
  @param worker      Veterancy gained because of work, not combat
  @return            Whether unit should become veteran
**************************************************************************/
static bool maybe_become_veteran_real(struct unit *punit, int base_chance,
                                      bool worker)
{
  const struct veteran_system *vsystem;
  const struct veteran_level *vlevel;
  int chance;
  const struct civ_map *nmap = &(wld.map);

  fc_assert_ret_val(punit != NULL, FALSE);

  vsystem = utype_veteran_system(unit_type_get(punit));
  fc_assert_ret_val(vsystem != NULL, FALSE);
  fc_assert_ret_val(vsystem->levels > punit->veteran, FALSE);

  vlevel = vsystem_veteran_level(vsystem, punit->veteran);
  fc_assert_ret_val(vlevel != NULL, FALSE);

  if (punit->veteran + 1 >= vsystem->levels
      || !is_action_enabled_unit_on_self(nmap, ACTION_GAIN_VETERANCY, punit)) {
    return FALSE;
  } else if (!worker) {
    int mod = base_chance + get_unit_bonus(punit, EFT_VETERAN_COMBAT);

    /* The modification is tacked on as a multiplier to the base chance.
     * For example with a base chance of 50% for green units and a modifier
     * of +50% the end chance is 75%. */
    chance = vlevel->base_raise_chance * mod / 100;
  } else if (worker && unit_has_type_flag(punit, UTYF_WORKERS)) {
    int mod = base_chance + get_unit_bonus(punit, EFT_VETERAN_WORK);

    chance = vlevel->work_raise_chance * mod / 100;
  } else {
    /* No battle and no work done. */
    return FALSE;
  }

  if (fc_rand(100) < chance) {
    punit->veteran++;
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  This is the basic unit versus unit combat routine.
  1) A LOT of modifiers, bonuses etc are added to the 2 units' rates.
  2) The combat loop, which continues until one of the units are dead or
     EFT_COMBAT_ROUNDS rounds have been fought.
  3) The aftermath, the loser (and potentially the stack which is below it)
     might get wiped, and the winner gets a chance of gaining veteran status

  Returns whether either side was completely powerless.
**************************************************************************/
bool unit_versus_unit(struct unit *attacker, struct unit *defender,
                      int *att_hp, int *def_hp, int *att_vet, int *def_vet,
                      const struct action *paction)
{
  int attackpower = get_total_attack_power(attacker, defender, paction);
  int defensepower = get_total_defense_power(attacker, defender);
  int attack_firepower, defense_firepower;
  struct player *plr1 = unit_owner(attacker);
  struct player *plr2 = unit_owner(defender);
  struct civ_map *nmap = &(wld.map);
  int max_rounds;
  int rounds;
  int att_strength;
  int def_strength;

  *att_hp = attacker->hp;
  *def_hp = defender->hp;
  get_modified_firepower(nmap, attacker, defender,
			 &attack_firepower, &defense_firepower);

  log_verbose("attack:%d, defense:%d, attack firepower:%d, "
              "defense firepower:%d", attackpower, defensepower,
              attack_firepower, defense_firepower);

  player_update_last_war_action(plr1);
  player_update_last_war_action(plr2);

  att_strength = attackpower * attacker->hp * attack_firepower;
  def_strength = defensepower * defender->hp * defense_firepower;

  /* In a combat between equal strength units the values are 50% / 50%.
   * -> scaling that to 100% by doubling, to match scale of chances
   *    in existing rulesets, and in !combat_odds_scaled_veterancy case. */
  *att_vet = 100 * def_strength * 2 / (att_strength + def_strength);
  *def_vet = 100 * att_strength * 2 / (att_strength + def_strength);

  if (attackpower == 0) {
    *att_hp = 0; 
  } else if (defensepower == 0) {
    *def_hp = 0;
  }
  max_rounds = get_unit_bonus(attacker, EFT_COMBAT_ROUNDS);
  for (rounds = 0;
       *att_hp > 0 && *def_hp > 0
         && (max_rounds <= 0 || max_rounds > rounds);
       rounds++) {
    if (fc_rand(attackpower + defensepower) >= defensepower) {
      *def_hp -= attack_firepower;
    } else {
      *att_hp -= defense_firepower;
    }
  }
  if (*att_hp < 0) {
    *att_hp = 0;
  }
  if (*def_hp < 0) {
    *def_hp = 0;
  }

  return attackpower <= 0 || defensepower <= 0;
}

/**********************************************************************//**
  This is the basic unit versus unit bombardment routine.
  1) A LOT of modifiers, bonuses etc are added to the 2 units' rates.
  2) Do rate attacks, never kill defender if action has
     non-lethal sub result.
**************************************************************************/
void unit_bombs_unit(struct unit *attacker, struct unit *defender,
                     int *att_hp, int *def_hp,
                     const struct action *paction)
{
  int i;
  int rate = unit_bombard_rate(attacker);
  int attackpower = get_total_attack_power(attacker, defender, paction);
  int defensepower = get_total_defense_power(attacker, defender);
  int attack_firepower, defense_firepower;
  struct player *plr1 = unit_owner(attacker);
  struct player *plr2 = unit_owner(defender);
  struct civ_map *nmap = &(wld.map);

  *att_hp = attacker->hp;
  *def_hp = defender->hp;
  get_modified_firepower(nmap, attacker, defender,
                         &attack_firepower, &defense_firepower);

  log_verbose("attack:%d, defense:%d, attack firepower:%d, "
              "defense firepower:%d", attackpower, defensepower,
              attack_firepower, defense_firepower);

  player_update_last_war_action(plr1);
  player_update_last_war_action(plr2);

  for (i = 0; i < rate; i++) {
    if (fc_rand(attackpower + defensepower) >= defensepower) {
      *def_hp -= attack_firepower;
    }
  }

  if (*def_hp <= 0) {
    if (BV_ISSET(paction->sub_results, ACT_SUB_RES_NON_LETHAL)) {
      /* Don't kill the target. */
      *def_hp = 1;
    } else {
      *def_hp = 0;
    }
  }
}

/**********************************************************************//**
  Maybe make either side of combat veteran.
  Powerless means that either side of the combat was completely powerless.
**************************************************************************/
void combat_veterans(struct unit *attacker, struct unit *defender,
                     bool powerless, int att_vet, int def_vet)
{
  if (!powerless || !game.info.only_real_fight_makes_veteran) {
    if (attacker->hp <= 0 || defender->hp <= 0
        || !game.info.only_killing_makes_veteran) {
      if (!game.info.combat_odds_scaled_veterancy) {
        att_vet = 100;
        def_vet = 100;
      }
      if (attacker->hp > 0) {
        maybe_make_veteran(attacker, att_vet);
      }
      if (defender->hp > 0) {
        maybe_make_veteran(defender, def_vet);
      }
    }
  }
}

/**********************************************************************//**
  Do unit auto-upgrades to players with the EFT_UNIT_UPGRADE effect
  (traditionally from Leonardo's Workshop).
**************************************************************************/
static void do_upgrade_effects(struct player *pplayer)
{
  int upgrades = get_player_bonus(pplayer, EFT_UPGRADE_UNIT);
  struct unit_list *candidates;

  if (upgrades <= 0) {
    return;
  }
  candidates = unit_list_new();

  unit_list_iterate(pplayer->units, punit) {
    /* We have to be careful not to strand units at sea, for example by
     * upgrading a frigate to an ironclad while it was carrying a unit. */
    if (UU_OK == unit_upgrade_test(&(wld.map), punit, TRUE)) {
      unit_list_prepend(candidates, punit);	/* Potential candidate :) */
    }
  } unit_list_iterate_end;

  while (upgrades > 0 && unit_list_size(candidates) > 0) {
    /* Upgrade one unit.  The unit is chosen at random from the list of
     * available candidates. */
    int candidate_to_upgrade = fc_rand(unit_list_size(candidates));
    struct unit *punit = unit_list_get(candidates, candidate_to_upgrade);
    const struct unit_type *type_from = unit_type_get(punit);
    const struct unit_type *type_to = can_upgrade_unittype(pplayer, type_from);

    transform_unit(punit, type_to, game.server.autoupgrade_veteran_loss);
    notify_player(pplayer, unit_tile(punit), E_UNIT_UPGRADED, ftc_server,
                  _("%s was upgraded for free to %s."),
                  utype_name_translation(type_from),
                  unit_link(punit));
    unit_list_remove(candidates, punit);
    upgrades--;
  }

  unit_list_destroy(candidates);
}

/**********************************************************************//**
  1. Do Leonardo's Workshop upgrade if applicable.

  2. Restore/decrease unit hitpoints.

  3. Kill dead units.

  4. Rescue airplanes by returning them to base automatically.

  5. Decrease fuel of planes in the air.

  6. Refuel planes that are in bases.

  7. Kill planes that are out of fuel.
**************************************************************************/
void player_restore_units(struct player *pplayer)
{
  const struct civ_map *nmap = &(wld.map);

  /* 1) get Leonardo out of the way first: */
  do_upgrade_effects(pplayer);

  unit_list_iterate_safe(pplayer->units, punit) {

    /* 2) Modify unit hitpoints. Helicopters can even lose them. */
    unit_restore_hitpoints(punit);

    /* 3) Check that unit has hitpoints */
    if (punit->hp <= 0) {
      /* This should usually only happen for heli units, but if any other
       * units get 0 hp somehow, catch them too.  --dwp  */
      /* if 'game.server.killunhomed' is activated unhomed units are slowly
       * killed; notify player here */
      if (!punit->homecity && 0 < game.server.killunhomed) {
        notify_player(pplayer, unit_tile(punit), E_UNIT_LOST_MISC,
                      ftc_server, _("Your %s has run out of hit points "
                                    "because it was not supported by a city."),
                      unit_tile_link(punit));
      } else {
        notify_player(pplayer, unit_tile(punit), E_UNIT_LOST_MISC, ftc_server,
                      _("Your %s has run out of hit points."),
                      unit_tile_link(punit));
      }

      wipe_unit(punit, ULR_HP_LOSS, NULL);
      continue; /* Continue iterating... */
    }

    /* 4) Rescue planes if needed */
    if (utype_fuel(unit_type_get(punit))) {
      /* Shall we emergency return home on the last vapors? */

      /* I think this is strongly against the spirit of client goto.
       * The problem is (again) that here we know too much. -- Zamar */

      if (punit->fuel <= 1
          && !is_unit_being_refueled(punit)) {
        struct unit *carrier;

        carrier = transporter_for_unit(punit);
        if (carrier) {
          unit_transport_load_tp_status(punit, carrier, FALSE);
        } else {
          struct pf_map *pfm;
          struct pf_parameter parameter;
          bool alive = TRUE;

          pft_fill_unit_parameter(&parameter, nmap, punit);
          parameter.omniscience = !has_handicap(pplayer, H_MAP);
          pfm = pf_map_new(&parameter);

          pf_map_move_costs_iterate(pfm, ptile, move_cost, TRUE) {
            if (move_cost > punit->moves_left) {
              /* Too far */
              break;
            }

            if (is_refuel_point(ptile, pplayer, punit)) {
              struct pf_path *path;
              int id = punit->id;

              /* Client orders may be running for this unit - if so
               * we free them before engaging goto. */
              free_unit_orders(punit);

              path = pf_map_path(pfm, ptile);

              alive = adv_follow_path(punit, path, ptile);

              if (!alive) {
                log_error("rescue plane: unit %d died enroute!", id);
              } else if (!same_pos(unit_tile(punit), ptile)) {
                /* Enemy units probably blocked our route
                 * FIXME: We should try find alternative route around
                 * the enemy unit instead of just giving up and crashing. */
                log_debug("rescue plane: unit %d could not move to "
                          "refuel point!", punit->id);
              }

              if (alive) {
                /* Clear activity. Unit info will be sent in the end of
	         * the function. */
                unit_activity_handling(punit, ACTIVITY_IDLE, ACTION_NONE);
                adv_unit_new_task(punit, AUT_NONE, NULL);
                punit->goto_tile = NULL;

                if (!is_unit_being_refueled(punit)) {
                  carrier = transporter_for_unit(punit);
                  if (carrier) {
                    unit_transport_load_tp_status(punit, carrier, FALSE);
                  }
                }

                notify_player(pplayer, unit_tile(punit),
                              E_UNIT_ORDERS, ftc_server,
                              _("Your %s has returned to refuel."),
                              unit_link(punit));
	      }
              pf_path_destroy(path);
              break;
            }
          } pf_map_move_costs_iterate_end;
          pf_map_destroy(pfm);

          if (!alive) {
            /* Unit died trying to move to refuel point. */
            return;
	  }
        }
      }

      /* 5) Update fuel */
      punit->fuel--;

      /* 6) Automatically refuel air units in cities, airbases, and
       *    transporters (carriers). */
      if (is_unit_being_refueled(punit)) {
	punit->fuel = utype_fuel(unit_type_get(punit));
      }
    }
  } unit_list_iterate_safe_end;

  /* 7) Check if there are air units without fuel */
  unit_list_iterate_safe(pplayer->units, punit) {
    const struct unit_type *utype = unit_type_get(punit);

    if (punit->fuel <= 0 && utype_fuel(utype)) {
      /* Notifications sent from the lua script when unit wiped. */
      wipe_unit(punit, ULR_FUEL, NULL);
    } 
  } unit_list_iterate_safe_end;

  /* Send all updates. */
  unit_list_iterate(pplayer->units, punit) {
    send_unit_info(NULL, punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Add hitpoints to the unit, hp_gain_coord returns the amount to add
  united nations will speed up the process by 2 hp's / turn, means helicopters
  will actually not lose hp's every turn if player have that wonder.
  Units which have moved don't gain hp, except the United Nations and
  helicopter effects still occur.

  If 'game.server.killunhomed' is greater than 0, unhomed units lose
  'game.server.killunhomed' hitpoints each turn, killing the unit at the end.
**************************************************************************/
static void unit_restore_hitpoints(struct unit *punit)
{
  bool was_lower;
  const struct unit_type *utype = unit_type_get(punit);

  was_lower = (punit->hp < utype->hp);

  punit->hp += unit_gain_hitpoints(punit);

  fc_assert(punit->hp >= 0);
  fc_assert(punit->hp <= utype->hp);

  if (punit->hp == utype->hp) {
    if (was_lower && punit->activity == ACTIVITY_SENTRY) {
      set_unit_activity(punit, ACTIVITY_IDLE, ACTION_NONE);
    }
  }

  punit->moved = FALSE;
  punit->paradropped = FALSE;
}
  
/**********************************************************************//**
  Move points are trivial, only modifiers to the base value is if it's
  sea units and the player has certain wonders/techs. Then add veteran
  bonus, if any.
**************************************************************************/
static void unit_restore_movepoints(struct player *pplayer, struct unit *punit)
{
  punit->moves_left = unit_move_rate(punit);
  punit->done_moving = FALSE;
}

/**********************************************************************//**
  Iterate through all units and update them.
**************************************************************************/
void update_unit_activities(struct player *pplayer)
{
  unit_list_iterate_safe(pplayer->units, punit) {
    update_unit_activity(punit);
  } unit_list_iterate_safe_end;
}

/**********************************************************************//**
  Iterate through all units and execute their orders.
**************************************************************************/
void execute_unit_orders(struct player *pplayer)
{
  unit_list_iterate_safe(pplayer->units, punit) {
    if (unit_has_orders(punit)) {
      execute_orders(punit, FALSE);
    }
  } unit_list_iterate_safe_end;
}

/**********************************************************************//**
  Recalculate some unit related effects on turn change
**************************************************************************/
void unit_tc_effect_refresh(struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit) {
    unit_refresh_vision(punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Iterate through all units and remember their current activities.
**************************************************************************/
void finalize_unit_phase_beginning(struct player *pplayer)
{
  /* Remember activities only after all knock-on effects of unit activities
   * on other units have been resolved */
  unit_list_iterate(pplayer->units, punit) {
    punit->changed_from = punit->activity;
    punit->changed_from_target = punit->activity_target;
    punit->changed_from_count = punit->activity_count;
    send_unit_info(NULL, punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Calculate the total amount of activity performed by all units on a tile
  for a given task and target.
**************************************************************************/
static int total_activity(struct tile *ptile, enum unit_activity act,
                          struct extra_type *tgt)
{
  int total = 0;
  bool tgt_matters = activity_requires_target(act);

  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == act
        && (!tgt_matters || punit->activity_target == tgt)) {
      total += punit->activity_count;
    }
  } unit_list_iterate_end;

  return total;
}

/**********************************************************************//**
  Check the total amount of activity performed by all units on a tile
  for a given task.
**************************************************************************/
static bool total_activity_done(struct tile *ptile, enum unit_activity act,
                                struct extra_type *tgt)
{
  return total_activity(ptile, act, tgt) >= tile_activity_time(act, ptile, tgt);
}

/**********************************************************************//**
  Common notification for all experience levels.
**************************************************************************/
void notify_unit_experience(struct unit *punit)
{
  const struct veteran_level *vlevel;
  const struct veteran_system *vsystem;

  if (!punit) {
    return;
  }

  vsystem = utype_veteran_system(unit_type_get(punit));
  fc_assert_ret(vsystem != NULL);
  fc_assert_ret(vsystem->levels > punit->veteran);

  vlevel = vsystem_veteran_level(vsystem, punit->veteran);
  fc_assert_ret(vlevel != NULL);

  notify_player(unit_owner(punit), unit_tile(punit),
                E_UNIT_BECAME_VET, ftc_server,
                /* TRANS: Your <unit> became ... rank of <veteran level>. */
                _("Your %s became more experienced and achieved the rank "
                  "of %s."),
                unit_link(punit), name_translation_get(&vlevel->name));
}

/**********************************************************************//**
  Convert a single unit to another type.
**************************************************************************/
static void unit_convert(struct unit *punit)
{
  const struct unit_type *to_type;
  const struct unit_type *from_type;

  from_type = unit_type_get(punit);
  to_type = from_type->converted_to;

  if (unit_can_convert(&(wld.map), punit)) {
    transform_unit(punit, to_type, 0);
    notify_player(unit_owner(punit), unit_tile(punit),
                  E_UNIT_UPGRADED, ftc_server,
                  _("%s converted to %s."),
                  utype_name_translation(from_type),
                  utype_name_translation(to_type));
  } else {
    notify_player(unit_owner(punit), unit_tile(punit),
                  E_UNIT_UPGRADED, ftc_server,
                  _("%s cannot be converted."),
                  utype_name_translation(from_type));
  }
}

/**********************************************************************//**
  Cancel activities, and orders that it might be part of.
**************************************************************************/
void unit_activities_cancel(struct unit *punit)
{
  if (unit_has_orders(punit)) {
    notify_player(unit_owner(punit), unit_tile(punit),
                  E_UNIT_ORDERS, ftc_server,
                  _("Orders for %s aborted because activity "
                    "is no longer available."),
                  unit_link(punit));
    free_unit_orders(punit);
  }

  set_unit_activity(punit, ACTIVITY_IDLE, ACTION_NONE);
  send_unit_info(NULL, punit);
}

/**********************************************************************//**
  Cancel all illegal activities done by units of the specified owner.
**************************************************************************/
void unit_activities_cancel_all_illegal_plr(const struct player *pplayer)
{
  unit_list_iterate(pplayer->units, punit) {
    if (!can_unit_continue_current_activity(&(wld.map), punit)) {
      unit_activities_cancel(punit);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Cancel all illegal activities done by units at the specified tile.
**************************************************************************/
void unit_activities_cancel_all_illegal_tile(const struct tile *ptile)
{
  unit_list_iterate(ptile->units, punit) {
    if (!can_unit_continue_current_activity(&(wld.map), punit)) {
      unit_activities_cancel(punit);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Cancel all illegal activities done by units at the specified tile,
  and surrounding tiles. For most rulesets this is for cancelling
  irrigation on surrounding tiles when the central tile was the only
  source of water, but does not provide water any more.
**************************************************************************/
void unit_activities_cancel_all_illegal_area(const struct tile *ptile)
{
  unit_activities_cancel_all_illegal_tile(ptile);

  adjc_iterate(&(wld.map), ptile, ptile2) {
    unit_activities_cancel_all_illegal_tile(ptile2);
  } adjc_iterate_end;
}

/**********************************************************************//**
  Progress settlers in their current tasks, and units that is pillaging.
  Restore unit move points (information needed for settler tasks)
**************************************************************************/
static void update_unit_activity(struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  bool unit_activity_done = FALSE;
  enum unit_activity activity = punit->activity;
  struct tile *ptile = unit_tile(punit);
  const struct unit_type *act_utype = unit_type_get(punit);

  switch (activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_LAST:
    /* We don't need the activity_count for the above */
    break;

  case ACTIVITY_FORTIFYING:
  case ACTIVITY_CONVERT:
    punit->activity_count += get_activity_rate_this_turn(punit);
    break;

  case ACTIVITY_CLEAN:
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_PILLAGE:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_BASE:
  case ACTIVITY_GEN_ROAD:
    punit->activity_count += get_activity_rate_this_turn(punit);

    /* Worker may become veteran when doing something useful */
    if (maybe_become_veteran_real(punit, 100, TRUE)) {
      notify_unit_experience(punit);
    }
    break;
  };

  unit_restore_movepoints(pplayer, punit);

  switch (activity) {
  case ACTIVITY_EXPLORE:
    /* Not accumulating activity - will be handled more like movement
     * after the TC */
    fc__fallthrough;
  case ACTIVITY_IDLE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_CONVERT:
  case ACTIVITY_LAST:
    /* No default, ensure all handled */
    break;

  case ACTIVITY_PILLAGE:
    if (total_activity_done(ptile, ACTIVITY_PILLAGE,
                            punit->activity_target)) {
      destroy_extra(ptile, punit->activity_target);
      unit_activity_done = TRUE;

      bounce_units_on_terrain_change(ptile);

      /* Change vision if effects have changed. */
      unit_list_refresh_vision(ptile->units);
    }
    break;

  case ACTIVITY_CLEAN:
    /* TODO: Remove this fallback target setting when target always correctly
     *       set */
    {
      struct extra_type *pextra;

      if (punit->activity_target == NULL) {
        pextra = prev_extra_in_tile(ptile, ERM_CLEAN,
                                    NULL, punit);
        if (pextra != NULL) {
          punit->activity_target = pextra;
        }
      } else {
        if (is_extra_removed_by(punit->activity_target, ERM_CLEAN)) {
          pextra = punit->activity_target;
        } else {
          pextra = NULL;
        }
      }

      if (pextra != NULL) {
        if (total_activity_done(ptile, ACTIVITY_CLEAN, pextra)) {
          destroy_extra(ptile, pextra);
          unit_activity_done = TRUE;
        }
      }
    }
    break;

  case ACTIVITY_BASE:
    {
      if (total_activity(ptile, ACTIVITY_BASE, punit->activity_target)
          >= tile_activity_time(ACTIVITY_BASE, ptile, punit->activity_target)) {
        create_extra(ptile, punit->activity_target, unit_owner(punit));
        unit_activity_done = TRUE;
      }
    }
    break;

  case ACTIVITY_GEN_ROAD:
    {
      if (total_activity(ptile, ACTIVITY_GEN_ROAD, punit->activity_target)
          >= tile_activity_time(ACTIVITY_GEN_ROAD, ptile, punit->activity_target)) {
        create_extra(ptile, punit->activity_target, unit_owner(punit));
        unit_activity_done = TRUE;
      }
    }
    break;

  case ACTIVITY_IRRIGATE:
  case ACTIVITY_MINE:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_TRANSFORM:
    if (total_activity_done(ptile, activity, punit->activity_target)) {
      struct terrain *old = tile_terrain(ptile);

      /* The function below could change the terrain. Therefore, we have to
       * check the terrain (which will also do a sanity check for the tile). */
      tile_apply_activity(ptile, activity, punit->activity_target);
      check_terrain_change(ptile, old);
      unit_activity_done = TRUE;
    }
    break;
  }

  if (unit_activity_done) {
    struct extra_type *act_tgt = punit->activity_target;

    update_tile_knowledge(ptile);

    unit_list_iterate(ptile->units, punit2) {
      if (punit2->activity == activity
          && punit2->activity_target == act_tgt) {
        /* This unit was helping with the work just finished.
         * Mark it idle (already finished) so its "current"
         * activity is not considered illegal
         * in tile_change_side_effects() . */
        set_unit_activity(punit2, ACTIVITY_IDLE, ACTION_NONE);
      }
    } unit_list_iterate_end;

    tile_change_side_effects(ptile, FALSE);
  }

  if (activity == ACTIVITY_FORTIFYING) {
    if (punit->activity_count
        >= action_id_get_act_time(ACTION_FORTIFY,
                                  punit, ptile, punit->activity_target)) {
      set_unit_activity(punit, ACTIVITY_FORTIFIED, punit->action);
      unit_activity_done = TRUE;
    }
  }

  if (activity == ACTIVITY_CONVERT) {
    if (punit->activity_count
        >= action_id_get_act_time(ACTION_CONVERT,
                                  punit, ptile, punit->activity_target)) {
      unit_convert(punit);
      set_unit_activity(punit, ACTIVITY_IDLE, ACTION_NONE);
      unit_activity_done = TRUE;
    }
  }

  if (unit_activity_done) {
    if (activity == ACTIVITY_PILLAGE) {
      /* Casus Belli for when the action is completed. */
      /* TODO: is it more logical to put Casus_Belli_Success here, change
       * Casus_Belli_Complete to Casus_Belli_Successful_Beginning and
       * trigger it when an activity successfully has began? */
      action_consequence_complete(action_by_number(punit->action),
                                  unit_owner(punit),
                                  act_utype,
                                  tile_owner(unit_tile(punit)),
                                  unit_tile(punit),
                                  tile_link(unit_tile(punit)));
    }
  }
}

/**********************************************************************//**
  Forget the unit's last activity so that it can't be resumed. This is
  used for example when the unit moves or attacks.
**************************************************************************/
void unit_forget_last_activity(struct unit *punit)
{
  punit->changed_from = ACTIVITY_IDLE;
}

/**********************************************************************//**
  Return TRUE iff activity requires some sort of target to be specified by
  the client.
**************************************************************************/
bool unit_activity_needs_target_from_client(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_PILLAGE:
    /* Can be set server side. */
    return FALSE;
  default:
    return activity_requires_target(activity);
  }
}

/**********************************************************************//**
  For some activities (currently only pillaging), the precise target can
  be assigned by the server rather than explicitly requested by the client.
  This function assigns a specific activity+target if the current
  settings are open-ended (otherwise leaves them unchanged).

  Please update unit_activity_needs_target_from_client() if you add server
  side unit activity target setting to more activities.
**************************************************************************/
void unit_assign_specific_activity_target(struct unit *punit,
                                          enum unit_activity *activity,
                                          enum gen_action action,
                                          struct extra_type **target)
{
  const struct civ_map *nmap = &(wld.map);

  if (*activity == ACTIVITY_PILLAGE
      && *target == nullptr) {
    struct tile *ptile = unit_tile(punit);
    struct extra_type *tgt;
    bv_extras extras = *tile_extras(ptile);

    while ((tgt = get_preferred_pillage(extras))) {

      BV_CLR(extras, extra_index(tgt));

      if (can_unit_do_activity_targeted(nmap, punit, *activity, action,
                                        tgt)) {
        *target = tgt;
        return;
      }
    }

    /* Nothing we can pillage here. */
    *activity = ACTIVITY_IDLE;
  }
}

/**********************************************************************//**
  Find place to place partisans. Returns whether such spot was found, and
  if it has been found, dst_tile contains that tile.
**************************************************************************/
static bool find_a_good_partisan_spot(struct tile *pcenter,
                                      struct player *powner,
                                      struct unit_type *u_type,
                                      int sq_radius,
                                      struct tile **dst_tile)
{
  int bestvalue = 0;
  struct civ_map *nmap = &(wld.map);

  /* coords of best tile in arg pointers */
  circle_iterate(nmap, pcenter, sq_radius, ptile) {
    int value;

    if (!is_native_tile(u_type, ptile)) {
      continue;
    }

    if (NULL != tile_city(ptile)) {
      continue;
    }

    if (0 < unit_list_size(ptile->units)) {
      continue;
    }

    /* City may not have changed hands yet; see place_partisans(). */
    value = get_virtual_defense_power(nmap, NULL, u_type, powner,
                                      ptile, FALSE, 0);
    value *= 10;

    if (tile_continent(ptile) != tile_continent(pcenter)) {
      value /= 2;
    }

    value -= fc_rand(value/3);

    if (value > bestvalue) {
      *dst_tile = ptile;
      bestvalue = value;
    }
  } circle_iterate_end;

  return bestvalue > 0;
}

/**********************************************************************//**
  Place partisans for powner around pcenter (normally around a city).
**************************************************************************/
void place_partisans(struct tile *pcenter, struct player *powner,
                     int count, int sq_radius)
{
  struct tile *ptile = NULL;
  struct unit_type *u_type = get_role_unit(L_PARTISAN, 0);
  const struct civ_map *nmap = &(wld.map);

  while (count-- > 0
         && find_a_good_partisan_spot(pcenter, powner, u_type,
                                      sq_radius, &ptile)) {
    struct unit *punit;
    enum gen_action action = activity_default_action(ACTIVITY_FORTIFYING);

    punit = unit_virtual_prepare(powner, ptile, u_type, 0, 0, -1, -1);
    if (can_unit_do_activity(nmap, punit, ACTIVITY_FORTIFYING, action)) {
      punit->activity = ACTIVITY_FORTIFIED; /* Yes; directly fortified */
      punit->action = action;
    }

    (void) place_unit(punit, powner, NULL, NULL, FALSE);
  }
}

/**********************************************************************//**
  Teleport punit to city at cost specified. Returns success. Note that unit
  may die if it successfully moves, i.e., even when return value is TRUE.
  (If specified cost is -1, then teleportation costs all movement.)
**************************************************************************/
bool teleport_unit_to_city(struct unit *punit, struct city *pcity,
                           int move_cost, bool verbose)
{
  struct tile *src_tile = unit_tile(punit), *dst_tile = pcity->tile;

  if (city_owner(pcity) == unit_owner(punit)) {
    log_verbose("Teleported %s %s from (%d,%d) to %s",
                nation_rule_name(nation_of_unit(punit)),
                unit_rule_name(punit), TILE_XY(src_tile), city_name_get(pcity));
    if (verbose) {
      notify_player(unit_owner(punit), city_tile(pcity),
                    E_UNIT_RELOCATED, ftc_server,
                    _("Teleported your %s to %s."),
                    unit_link(punit),
                    city_link(pcity));
    }

    /* Silently free orders since they won't be applicable anymore. */
    free_unit_orders(punit);

    if (move_cost == -1) {
      move_cost = punit->moves_left;
    }
    unit_move(punit, dst_tile, move_cost,
              NULL, FALSE, FALSE, FALSE, FALSE, FALSE);

    return TRUE;
  }
  return FALSE;
}

/**********************************************************************//**
  Move or remove a unit due to stack conflicts. This function will try to
  find a random safe tile within a two tile distance of the unit's current
  tile and move the unit there. If no tiles are found, the unit is
  disbanded. If 'verbose' is TRUE, a message is sent to the unit owner
  regarding what happened.
**************************************************************************/
void bounce_unit(struct unit *punit, bool verbose)
{
  struct player *pplayer;
  struct tile *punit_tile;
  int count = 0;

  /* I assume that there are no topologies that have more than
   * (2d + 1)^2 tiles in the "square" of "radius" d. */
  const int DIST = 2;
  struct tile *tiles[(2 * DIST + 1) * (2 * DIST + 1)];

  if (!punit) {
    return;
  }

  pplayer = unit_owner(punit);
  punit_tile = unit_tile(punit);

  square_iterate(&(wld.map), punit_tile, DIST, ptile) {
    if (count >= ARRAY_SIZE(tiles)) {
      break;
    }

    if (ptile == punit_tile) {
      continue;
    }

    if (can_unit_survive_at_tile(&(wld.map), punit, ptile)
        && !is_non_allied_city_tile(ptile, pplayer)
        && !is_non_allied_unit_tile(ptile, pplayer,
                                    unit_has_type_flag(punit, UTYF_FLAGLESS))) {
      tiles[count++] = ptile;
    }
  } square_iterate_end;

  if (count > 0) {
    struct tile *ptile = tiles[fc_rand(count)];

    if (verbose) {
      notify_player(pplayer, ptile, E_UNIT_RELOCATED, ftc_server,
                    /* TRANS: A unit is moved to resolve stack conflicts. */
                    _("Moved your %s."),
                    unit_link(punit));
    }

    /* TODO: should a unit be able to bounce to a transport like is done
     * below? What if the unit can't legally enter the transport, say
     * because the transport is Unreachable and the unit doesn't have it in
     * its embarks field or because "Transport Embark" isn't enabled? Kept
     * like it was to preserve the old rules for now. -- Sveinung */
    unit_move(punit, ptile, 0, NULL, TRUE, FALSE, FALSE, FALSE, FALSE);
    return;
  }

  /* Didn't find a place to bounce the unit, going to disband it.
   * Try to bounce transported units. */
  if (0 < get_transporter_occupancy(punit)) {
    struct unit_list *pcargo_units;

    pcargo_units = unit_transport_cargo(punit);
    unit_list_iterate_safe(pcargo_units, pcargo) {
      bounce_unit(pcargo, verbose);
    } unit_list_iterate_safe_end;
  }

  if (verbose) {
    notify_player(pplayer, punit_tile, E_UNIT_LOST_MISC, ftc_server,
                  /* TRANS: A unit is disbanded to resolve stack conflicts. */
                  _("Disbanded your %s."),
                  unit_tile_link(punit));
  }

  wipe_unit(punit, ULR_STACK_CONFLICT, NULL);
}

/**********************************************************************//**
  Throw pplayer's units from non-allied cities

  If verbose is true, pplayer gets messages about where each units goes.
**************************************************************************/
static void throw_units_from_illegal_cities(struct player *pplayer,
                                            bool verbose)
{
  struct tile *ptile;
  struct city *pcity;
  struct unit *ptrans;
  struct unit_list *pcargo_units;

  /* Unload undesired units from transports, if possible. */
  unit_list_iterate(pplayer->units, punit) {
    ptile = unit_tile(punit);
    pcity = tile_city(ptile);
    if (NULL != pcity
        && !pplayers_allied(city_owner(pcity), pplayer)
        && 0 < get_transporter_occupancy(punit)) {
      pcargo_units = unit_transport_cargo(punit);
      unit_list_iterate(pcargo_units, pcargo) {
        if (!pplayers_allied(unit_owner(pcargo), pplayer)) {
          if (can_unit_exist_at_tile(&(wld.map), pcargo, ptile)) {
            unit_transport_unload_send(pcargo);
          }
        }
      } unit_list_iterate_end;
    }
  } unit_list_iterate_end;

  /* Bounce units except transported ones which will be bounced with their
   * transport. */
  unit_list_iterate_safe(pplayer->units, punit) {
    ptile = unit_tile(punit);
    pcity = tile_city(ptile);
    if (NULL != pcity
        && !pplayers_allied(city_owner(pcity), pplayer)) {
      ptrans = unit_transport_get(punit);
      if (NULL == ptrans || pplayer != unit_owner(ptrans)) {
        bounce_unit(punit, verbose);
      }
    }
  } unit_list_iterate_safe_end;

#ifdef FREECIV_DEBUG
  /* Sanity check. */
  unit_list_iterate(pplayer->units, punit) {
    ptile = unit_tile(punit);
    pcity = tile_city(ptile);
    fc_assert_msg(NULL == pcity
                  || pplayers_allied(city_owner(pcity), pplayer),
                  "Failed to throw %s %d from %s %d (%d, %d)",
                  unit_rule_name(punit),
                  punit->id,
                  city_name_get(pcity),
                  pcity->id,
                  TILE_XY(ptile));
  } unit_list_iterate_end;
#endif /* FREECIV_DEBUG */
}

/**********************************************************************//**
  For each pplayer's unit, check if we stack illegally, if so,
  bounce both players' units. If on ocean tile, bounce everyone but ships
  to avoid drowning. This function assumes that cities are clean.

  If verbose is true, the unit owner gets messages about where each
  units goes.
**************************************************************************/
static void resolve_stack_conflicts(struct player *pplayer,
                                    struct player *aplayer, bool verbose)
{
  unit_list_iterate_safe(pplayer->units, punit) {
    struct tile *ptile = unit_tile(punit);

    /* Can pass FALSE 'everyone_non_allied' since no flagless
     * unit could be stacked there to begin with. */
    if (is_non_allied_unit_tile(ptile, pplayer, FALSE)) {
      unit_list_iterate_safe(ptile->units, aunit) {
        if (unit_owner(aunit) == pplayer
            || unit_owner(aunit) == aplayer
            || !can_unit_survive_at_tile(&(wld.map), aunit, ptile)) {
          bounce_unit(aunit, verbose);
        }
      } unit_list_iterate_safe_end;
    }
  } unit_list_iterate_safe_end;
}

/**********************************************************************//**
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

/**********************************************************************//**
  Returns the list of the units seen by 'pplayer' potentially seen only
  thanks to an alliance with 'aplayer'. The returned pointer is newly
  allocated and should be freed by the caller, using unit_list_destroy().
**************************************************************************/
struct unit_list *get_units_seen_via_ally(const struct player *pplayer,
                                          const struct player *aplayer)
{
  struct unit_list *seen_units = unit_list_new();

  /* Anybody's units inside ally's cities */
  city_list_iterate(aplayer->cities, pcity) {
    unit_list_iterate(city_tile(pcity)->units, punit) {
      if (can_player_see_unit(pplayer, punit)) {
        unit_list_append(seen_units, punit);
      }
    } unit_list_iterate_end;
  } city_list_iterate_end;

  /* Ally's own units inside transports */
  unit_list_iterate(aplayer->units, punit) {
    if (unit_transported(punit) && can_player_see_unit(pplayer, punit)) {
      unit_list_append(seen_units, punit);
    }
  } unit_list_iterate_end;

  /* Make sure the same unit is not added in multiple phases
   * (unit within transport in a city) */
  unit_list_unique(seen_units);

  return seen_units;
}

/**********************************************************************//**
  When two players cancel an alliance, a lot of units that were visible may
  no longer be visible (this includes units in transporters and cities).
  Call this function to inform the clients that these units are no longer
  visible. Pass the list of seen units returned by get_units_seen_via_ally()
  before alliance was broken up.
**************************************************************************/
void remove_allied_visibility(struct player *pplayer, struct player *aplayer,
                              const struct unit_list *seen_units)
{
  unit_list_iterate(seen_units, punit) {
    /* We need to hide units previously seen by the client. */
    if (!can_player_see_unit(pplayer, punit)) {
      unit_goes_out_of_sight(pplayer, punit);
    }
  } unit_list_iterate_end;

  city_list_iterate(aplayer->cities, pcity) {
    /* The player used to know what units were in these cities.  Now that
     * they don't, they need to get a new short city packet updating the
     * occupied status. */
    if (map_is_known_and_seen(pcity->tile, pplayer, V_MAIN)) {
      send_city_info(pplayer, pcity);
    }
  } city_list_iterate_end;
}

/**********************************************************************//**
  Refresh units visibility of 'aplayer' for 'pplayer' after alliance have
  been contracted.
**************************************************************************/
void give_allied_visibility(struct player *pplayer,
                            struct player *aplayer)
{
  unit_list_iterate(aplayer->units, punit) {
    if (can_player_see_unit(pplayer, punit)) {
      send_unit_info(pplayer->connections, punit);
    }
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Will unit refuel at this tile? Does not consider carriers.
**************************************************************************/
static bool is_refuel_tile(const struct tile *ptile,
                           const struct player *pplayer,
                           const struct unit *punit)
{
  const struct unit_type *utype;
  const struct unit_class *pclass;

  if (is_allied_city_tile(ptile, pplayer)) {
    return TRUE;
  }

  utype = unit_type_get(punit);
  if (utype_has_flag(utype, UTYF_COAST) && is_safe_ocean(&(wld.map), ptile)) {
    return TRUE;
  }

  pclass = utype_class(utype);
  if (NULL != pclass->cache.refuel_extras) {
    const struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

    extra_type_list_iterate(pclass->cache.refuel_extras, pextra) {
      if (BV_ISSET(plrtile->extras, extra_index(pextra))) {
        return TRUE;
      }
    } extra_type_list_iterate_end;
  }

  return FALSE;
}

/**********************************************************************//**
  Is unit being refueled in its current position?
**************************************************************************/
bool is_unit_being_refueled(const struct unit *punit)
{
  return unit_transported(punit) /* Carrier */
    || is_refuel_tile(unit_tile(punit), unit_owner(punit), punit);
}

/**********************************************************************//**
  Can unit refuel on tile. Considers also carrier capacity on tile.
**************************************************************************/
bool is_refuel_point(const struct tile *ptile,
                     const struct player *pplayer,
                     const struct unit *punit)
{
  if (is_non_allied_unit_tile(ptile, pplayer,
                              unit_has_type_flag(punit, UTYF_FLAGLESS))) {
    return FALSE;
  }

  return is_refuel_tile(ptile, pplayer, punit) || unit_could_load_at(punit, ptile);
}

/**********************************************************************//**
  Really transforms a single unit to another type.

  This function performs no checks. You should perform the appropriate
  test first to check that the transformation is legal (unit_upgrade_test()
  or unit_can_convert()).

  vet_loss: Number of veteran levels lost in process.

  Note that this function is strongly tied to unit.c:unit_upgrade_test().
**************************************************************************/
void transform_unit(struct unit *punit, const struct unit_type *to_unit,
                    int vet_loss)
{
  struct player *pplayer = unit_owner(punit);
  const struct unit_type *old_type = punit->utype;
  int old_mr = unit_move_rate(punit);
  int old_hp = unit_type_get(punit)->hp;
  int lvls;

  punit->utype = to_unit;

  /* New type may not have the same veteran system, and we may want to
   * knock some levels off. */
  lvls = utype_veteran_system(to_unit)->levels - 1;
  punit->veteran = MIN(punit->veteran, lvls);
  /* Keeping the old behavior, so first clip top, then reduce */
  punit->veteran = MAX(punit->veteran - vet_loss, 0);

  /* Scale HP and MP, rounding down. Be careful with integer arithmetic,
   * and don't kill the unit. unit_move_rate() is used to take into account
   * global effects like Magellan's Expedition. */
  punit->hp = MAX(punit->hp * unit_type_get(punit)->hp / old_hp, 1);
  if (old_mr == 0) {
    punit->moves_left = unit_move_rate(punit);
  } else {
    punit->moves_left = punit->moves_left * unit_move_rate(punit) / old_mr;
  }

  punit->current_form_turn = game.info.turn;

  unit_forget_last_activity(punit);
  if (!can_unit_continue_current_activity(&(wld.map), punit)) {
    unit_activities_cancel(punit);
  }

  /* Update unit upkeep */
  city_units_upkeep(game_city_by_number(punit->homecity));

  conn_list_do_buffer(pplayer->connections);

  unit_refresh_vision(punit);

  CALL_PLR_AI_FUNC(unit_transformed, pplayer, punit, old_type);
  CALL_FUNC_EACH_AI(unit_info, punit);

  send_unit_info(NULL, punit);
  conn_list_do_unbuffer(pplayer->connections);
}

/**********************************************************************//**
  Wrapper of the below
**************************************************************************/
struct unit *create_unit(struct player *pplayer, struct tile *ptile, 
                         const struct unit_type *type, int veteran_level, 
                         int homecity_id, int moves_left)
{
  return create_unit_full(pplayer, ptile, type, veteran_level, homecity_id, 
                          moves_left, -1, NULL);
}

/**********************************************************************//**
  Set carried goods for unit.
**************************************************************************/
void unit_get_goods(struct unit *punit)
{
  if (punit->homecity != 0) {
    struct city *home = game_city_by_number(punit->homecity);

    if (home != NULL && game.info.goods_selection == GSM_LEAVING) {
      punit->carrying = goods_from_city_to_unit(home, punit);
    }
  }
}

/**********************************************************************//**
  Creates a unit, sets its initial values, and puts it into the right
  lists. The unit must be placed to a valid tile or a loadable transport.
  See unit_virtual_prepare() for the processing of moves_left and hp_left
**************************************************************************/
struct unit *create_unit_full(struct player *pplayer, struct tile *ptile,
                              const struct unit_type *type, int veteran_level, 
                              int homecity_id, int moves_left, int hp_left,
                              struct unit *ptrans)
{
  struct unit *punit
    = unit_virtual_prepare(pplayer, ptile, type, veteran_level,
                           homecity_id, moves_left, hp_left);
  struct city *pcity = (!homecity_id || utype_has_flag(type, UTYF_NOHOME))
      ? NULL : game_city_by_number(homecity_id);
  bool could_place;

  fc_assert_ret_val(punit, NULL);
  could_place = place_unit(punit, pplayer, pcity, ptrans, FALSE);
  fc_assert(could_place);
  if (!could_place) {
    unit_virtual_destroy(punit);
    punit = NULL;
  }

  return punit;
}

/**********************************************************************//**
  Creates a virtual unit, and sets its initial values, but does not
  register the unit in any other data structures or set the vision.
  If moves_left is less than zero, unit will get max moves;
  otherwise, it will get the specified number of movement fragments
  and will be considered moved.
  If hp_left is zero or less, unit will get full hp.
  homecity_id won't be set to units with "NoHome" flag.
  ptile must be a valid tile (its livability for the unit is not checked)
**************************************************************************/
struct unit *unit_virtual_prepare(struct player *pplayer, struct tile *ptile,
                                  const struct unit_type *type,
                                  int veteran_level, int homecity_id,
                                  int moves_left, int hp_left)
{
  struct unit *punit;

  fc_assert_ret_val(ptile != NULL, NULL);
  punit = unit_virtual_create(pplayer, NULL, type, veteran_level);
  unit_tile_set(punit, ptile);

  if (utype_has_flag(type, UTYF_NOHOME)) {
    punit->homecity = 0; /* none */
  } else {
    punit->homecity = homecity_id;
  }

  if (hp_left >= 0) {
    /* Override default full HP */
    punit->hp = hp_left;
  }

  if (moves_left >= 0) {
    int mr = unit_move_rate(punit);

    /* Override default full MP */
    /* FIXME: there are valid situations when a unit have mp
     * over its move rate. Here, keeping the old behavior. */
    punit->moves_left = MIN(moves_left, mr);
    /* Assume that if moves_left < 0 then the unit is "fresh",
     * and not moved; else the unit has had something happen
     * to it (eg, bribed) which we treat as equivalent to moved.
     * (Otherwise could pass moved arg too...)  --dwp */
    punit->moved = TRUE;
  }

  return punit;
}

/**********************************************************************//**
  Places a virtual unit into the game, assigning it an index, putting it
  on the right lists and dispatching the information around.
  The unit must have a tile, pcity and pplayer must be valid
  and accord to the unit's fields (basically, set by unit_virtual_prepare()).
  ptrans if not NULL must be a transporter on the same tile
  the unit can freely load into (or just that can transport it if force)
  Returns if the unit is placed (must be TRUE if input data are valid)
+**************************************************************************/
bool place_unit(struct unit *punit, struct player *pplayer,
                struct city *pcity, struct unit *ptrans, bool force)
{
  struct tile *ptile;

  fc_assert_ret_val(pplayer, FALSE);
  fc_assert_ret_val(punit, FALSE);
  ptile = punit->tile;
  fc_assert_ret_val(ptile, FALSE);

  /* Register unit */
  punit->id = identity_number();
  idex_register_unit(&wld, punit);

  if (ptrans) {
    /* Set transporter for unit. */
    unit_transport_load_tp_status(punit, ptrans, force);
  }

  fc_assert_ret_val(unit_transport_get(punit)
                    || can_unit_exist_at_tile(&(wld.map), punit, ptile),
                    FALSE);

  unit_list_prepend(pplayer->units, punit);
  unit_list_prepend(ptile->units, punit);
  unit_make_contact(punit, ptile, nullptr);
  if (pcity && !unit_has_type_flag(punit, UTYF_NOHOME)) {
    fc_assert(punit->homecity == pcity->id);
    fc_assert(city_owner(pcity) == pplayer);
    unit_list_prepend(pcity->units_supported, punit);
    /* update unit upkeep */
    city_units_upkeep(pcity);
    /* Refresh the unit's homecity. */
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  }

  punit->server.vision = vision_new(pplayer, ptile);
  unit_refresh_vision(punit);

  unit_get_goods(punit);

  send_unit_info(NULL, punit);
  wakeup_neighbor_sentries(punit);

  /* The unit may have changed the available tiles in nearby cities. */
  city_map_update_tile_now(ptile);
  sync_cities();

  CALL_FUNC_EACH_AI(unit_created, punit);
  CALL_PLR_AI_FUNC(unit_got, pplayer, punit);

  return TRUE;
}

/**********************************************************************//**
  Set the call back to run when the server removes the unit.
**************************************************************************/
void unit_set_removal_callback(struct unit *punit,
                               void (*callback)(struct unit *punit))
{
  /* Tried to overwrite another call back. If this assertion is triggered
   * in a case where two call back are needed it may be time to support
   * more than one unit removal call back at a time. */
  fc_assert_ret(punit->server.removal_callback == NULL);

  punit->server.removal_callback = callback;
}

/**********************************************************************//**
  Remove the call back so nothing runs when the server removes the unit.
**************************************************************************/
void unit_unset_removal_callback(struct unit *punit)
{
  punit->server.removal_callback = NULL;
}

/**********************************************************************//**
  We remove the unit and see if it's disappearance has affected the homecity
  and the city it was in.
**************************************************************************/
static void server_remove_unit_full(struct unit *punit, bool transported,
                                    enum unit_loss_reason reason)
{
  struct packet_unit_remove packet;
  struct tile *ptile = unit_tile(punit);
  struct city *pcity = tile_city(ptile);
  struct city *phomecity = game_city_by_number(punit->homecity);
  struct unit *ptrans;
  struct player *pplayer = unit_owner(punit);

  /* The unit is doomed. */
  punit->server.dying = TRUE;

#if defined(FREECIV_DEBUG) && !defined(FREECIV_NDEBUG)
  unit_list_iterate(ptile->units, pcargo) {
    fc_assert(unit_transport_get(pcargo) != punit);
  } unit_list_iterate_end;
#endif /* FREECIV_DEBUG */

  CALL_PLR_AI_FUNC(unit_lost, pplayer, punit);
  CALL_FUNC_EACH_AI(unit_destroyed, punit);

  /* Save transporter for updating below. */
  ptrans = unit_transport_get(punit);
  /* Unload unit. */
  unit_transport_unload(punit);

  /* Since settlers plot in new cities in the minimap before they
     are built, so that no two settlers head towards the same city
     spot, we need to ensure this reservation is cleared should
     the settler disappear on the way. */
  adv_unit_new_task(punit, AUT_NONE, NULL);

  /* Clear the vision before sending unit remove. Else, we might duplicate
   * the PACKET_UNIT_REMOVE if we lose vision of the unit tile. */
  if (punit->server.vision != NULL) {
    vision_clear_sight(punit->server.vision);
    vision_free(punit->server.vision);
    punit->server.vision = NULL;
  }

  packet.unit_id = punit->id;
  /* Send to onlookers. */
  players_iterate(aplayer) {
    if (can_player_see_unit_at(aplayer, punit, unit_tile(punit),
                               transported)) {
      lsend_packet_unit_remove(aplayer->connections, &packet);
    }
  } players_iterate_end;
  /* Send to global observers. */
  conn_list_iterate(game.est_connections, pconn) {
    if (conn_is_global_observer(pconn)) {
      send_packet_unit_remove(pconn, &packet);
    }
  } conn_list_iterate_end;

  if (punit->server.moving != NULL) {
    /* Do not care of this unit for running moves. */
    punit->server.moving->punit = NULL;
  }

  if (punit->server.removal_callback != NULL) {
    /* Run the unit removal call back. */
    punit->server.removal_callback(punit);
  }

  /* check if this unit had UTYF_GAMELOSS flag */
  if (unit_has_type_flag(punit, UTYF_GAMELOSS) && unit_owner(punit)->is_alive
      && ULR_EDITOR != reason) {
    notify_conn(game.est_connections, ptile, E_UNIT_LOST_MISC, ftc_server,
                _("Unable to defend %s, %s has lost the game."),
                unit_link(punit),
                player_name(pplayer));
    notify_player(pplayer, ptile, E_GAME_END, ftc_server,
                  _("Losing %s meant losing the game! "
                  "Be more careful next time!"),
                  unit_link(punit));
    player_status_add(unit_owner(punit), PSTATUS_DYING);
  }

  script_server_signal_emit("unit_lost", punit, unit_owner(punit),
                            unit_loss_reason_name(reason));

  script_server_remove_exported_object(punit);
  game_remove_unit(&wld, punit);
  punit = NULL;

  if (NULL != ptrans) {
    /* Update the occupy info. */
    send_unit_info(NULL, ptrans);
  }

  /* This unit may have blocked tiles of adjacent cities. Update them. */
  city_map_update_tile_now(ptile);
  sync_cities();

  if (phomecity) {
    city_refresh(phomecity);
    send_city_info(city_owner(phomecity), phomecity);
  }

  if (pcity && pcity != phomecity) {
    city_refresh(pcity);
    send_city_info(city_owner(pcity), pcity);
  }

  if (pcity && unit_list_size(ptile->units) == 0) {
    /* The last unit in the city was killed: update the occupied flag. */
    send_city_info(NULL, pcity);
  }
}

/**********************************************************************//**
  We remove the unit and see if it's disappearance has affected the homecity
  and the city it was in.
**************************************************************************/
static void server_remove_unit(struct unit *punit,
                               enum unit_loss_reason reason)
{
  server_remove_unit_full(punit, unit_transported(punit), reason);
}

/**********************************************************************//**
  Handle units destroyed when their transport is destroyed
**************************************************************************/
static void unit_lost_with_transport(const struct player *pplayer,
                                     struct unit *pcargo,
                                     const struct unit_type *ptransport,
                                     struct player *killer)
{
  notify_player(pplayer, unit_tile(pcargo), E_UNIT_LOST_MISC, ftc_server,
                _("%s lost when %s was lost."),
                unit_tile_link(pcargo),
                utype_name_translation(ptransport));
  /* Unit is not transported any more at this point, but it has jumped
   * off the transport and drowns outside. So it must be removed from
   * all clients.
   * However, we don't know if given client has received ANY updates
   * about the swimming unit, and we can't remove it if it's not there
   * in the first place -> we send it once here just to be sure it's
   * there. */
  send_unit_info(NULL, pcargo);
  wipe_unit_full(pcargo, FALSE, ULR_TRANSPORT_LOST, killer);
}

/**********************************************************************//**
  Remove the unit, and passengers if it is a carrying any. Remove the
  _minimum_ number, eg there could be another boat on the square.
**************************************************************************/
static void wipe_unit_full(struct unit *punit, bool transported,
                           enum unit_loss_reason reason,
                           struct player *killer)
{
  struct tile *ptile = unit_tile(punit);
  struct player *pplayer = unit_owner(punit);
  const struct unit_type *putype_save = unit_type_get(punit); /* for notify messages */
  struct unit_list *helpless = unit_list_new();
  struct unit_list *imperiled = unit_list_new();
  struct unit_list *unsaved = unit_list_new();
  struct unit *ptrans = unit_transport_get(punit);
  struct city *pexclcity;
  struct civ_map *nmap = &(wld.map);

  if (killer != NULL
      && (game.info.gameloss_style & GAMELOSS_STYLE_LOOT)
      && unit_has_type_flag(punit, UTYF_GAMELOSS)) {
    player_loot_player(killer, pplayer);
  }

  /* The unit is doomed. */
  punit->server.dying = TRUE;

  /* If a unit is being lost due to loss of its city, ensure that we don't
   * try to teleport any of its cargo to that city (which may not yet
   * have changed hands or disappeared). (It is assumed that the unit's
   * home city is always the one that is being lost/transferred/etc.) */
  if (reason == ULR_CITY_LOST) {
    pexclcity = unit_home(punit);
  } else {
    pexclcity = NULL;
  }

  /* Remove unit itself from its transport */
  if (ptrans != NULL) {
    unit_transport_unload(punit);
    send_unit_info(NULL, ptrans);
  }

  /* First pull all units off of the transporter. */
  if (get_transporter_occupancy(punit) > 0) {
    /* Use iterate_safe as unloaded units will be removed from the list
     * while iterating. */
    unit_list_iterate_safe(unit_transport_cargo(punit), pcargo) {
      bool healthy = FALSE;

      if (!can_unit_unload(pcargo, punit)) {
        unit_list_prepend(helpless, pcargo);
      } else {
        if (!can_unit_exist_at_tile(nmap, pcargo, ptile)) {
          unit_list_prepend(imperiled, pcargo);
        } else {
          /* These units do not need to be saved. */
          healthy = TRUE;
        }
      }

      /* Could use unit_transport_unload_send() here, but that would
       * call send_unit_info() for the transporter unnecessarily.
       * Note that this means that unit might to get seen briefly
       * by clients other than owner's, for example as a result of
       * update of homecity common to this cargo and some other
       * destroyed unit. */
      unit_transport_unload(pcargo);
      if (pcargo->activity == ACTIVITY_SENTRY) {
        /* Activate sentried units - like planes on a disbanded carrier.
         * Note this will activate ground units even if they just change
         * transporter. */
        set_unit_activity(pcargo, ACTIVITY_IDLE, ACTION_NONE);
      }

      /* Unit info for unhealthy units will be sent when they are
       * assigned new transport or removed. */
      if (healthy) {
        send_unit_info(NULL, pcargo);
      }
    } unit_list_iterate_safe_end;
  }

  /* Now remove the unit. */
  server_remove_unit_full(punit, transported, reason);

  switch (reason) {
  case ULR_KILLED:
  case ULR_EXECUTED:
  case ULR_SDI:
  case ULR_NUKE:
  case ULR_BRIBED:
  case ULR_CAPTURED:
  case ULR_CAUGHT:
  case ULR_ELIMINATED:
  case ULR_TRANSPORT_LOST:
    if (killer != NULL) {
      killer->score.units_killed++;
    }
    pplayer->score.units_lost++;
    break;
  case ULR_BARB_UNLEASH:
  case ULR_CITY_LOST:
  case ULR_STARVED:
  case ULR_UPKEEP:
  case ULR_NONNATIVE_TERR:
  case ULR_ARMISTICE:
  case ULR_HP_LOSS:
  case ULR_FUEL:
  case ULR_STACK_CONFLICT:
  case ULR_SOLD:
    pplayer->score.units_lost++;
    break;
  case ULR_RETIRED:
  case ULR_DISBANDED:
  case ULR_USED:
  case ULR_DETONATED:
  case ULR_MISSILE:
    pplayer->score.units_used++;
    break;
  case ULR_EDITOR:
  case ULR_PLAYER_DIED:
    break;
  }

  /* First, sort out helpless cargo. */
  if (unit_list_size(helpless) > 0) {
    struct unit_list *remaining = unit_list_new();

    /* Grant priority to gameloss units and units with the EvacuateFirst
     * unit type flag. */
    unit_list_iterate_safe(helpless, pcargo) {
      if (unit_has_type_flag(pcargo, UTYF_EVAC_FIRST)
          || unit_has_type_flag(pcargo, UTYF_GAMELOSS)) {
        if (!try_to_save_unit(pcargo, putype_save, TRUE,
                              unit_has_type_flag(pcargo,
                                                 UTYF_EVAC_FIRST),
                              pexclcity)) {
          unit_list_prepend(unsaved, pcargo);
        }
      } else {
        unit_list_prepend(remaining, pcargo);
      }
    } unit_list_iterate_safe_end;

    /* Handle non-priority units. */
    unit_list_iterate_safe(remaining, pcargo) {
      if (!try_to_save_unit(pcargo, putype_save, TRUE, FALSE, pexclcity)) {
        unit_list_prepend(unsaved, pcargo);
      }
    } unit_list_iterate_safe_end;

    unit_list_destroy(remaining);
  }
  unit_list_destroy(helpless);

  /* Then, save any imperiled cargo. */
  if (unit_list_size(imperiled) > 0) {
    struct unit_list *remaining = unit_list_new();

    /* Grant priority to gameloss units and units with the EvacuateFirst
     * unit type flag. */
    unit_list_iterate_safe(imperiled, pcargo) {
      if (unit_has_type_flag(pcargo, UTYF_EVAC_FIRST)
          || unit_has_type_flag(pcargo, UTYF_GAMELOSS)) {
        if (!try_to_save_unit(pcargo, putype_save, FALSE,
                              unit_has_type_flag(pcargo,
                                                 UTYF_EVAC_FIRST),
                              pexclcity)) {
          unit_list_prepend(unsaved, pcargo);
        }
      } else {
        unit_list_prepend(remaining, pcargo);
      }
    } unit_list_iterate_safe_end;

    /* Handle non-priority units. */
    unit_list_iterate_safe(remaining, pcargo) {
      if (!try_to_save_unit(pcargo, putype_save, FALSE, FALSE, pexclcity)) {
        unit_list_prepend(unsaved, pcargo);
      }
    } unit_list_iterate_safe_end;

    unit_list_destroy(remaining);
  }
  unit_list_destroy(imperiled);

  /* Finally, kill off the unsaved units. */
  if (unit_list_size(unsaved) > 0) {
    unit_list_iterate_safe(unsaved, dying_unit) {
      unit_lost_with_transport(pplayer, dying_unit, putype_save, killer);
    } unit_list_iterate_safe_end;
  }
  unit_list_destroy(unsaved);
}

/**********************************************************************//**
  Remove the unit, and passengers if it is a carrying any. Remove the
  _minimum_ number, eg there could be another boat on the square.
**************************************************************************/
void wipe_unit(struct unit *punit, enum unit_loss_reason reason,
               struct player *killer)
{
  wipe_unit_full(punit, unit_transported(punit), reason, killer);
}

/**********************************************************************//**
  Determine if it is possible to save a given unit, and if so, save them.
  'pexclcity' is a city to avoid teleporting to, if 'teleporting' is set.
  Note that despite being saved from drowning, teleporting the units to
  "safety" may have killed them in the end.
**************************************************************************/
static bool try_to_save_unit(struct unit *punit, const struct unit_type *pttype,
                             bool helpless, bool teleporting,
                             const struct city *pexclcity)
{
  struct tile *ptile = unit_tile(punit);
  struct player *pplayer = unit_owner(punit);
  struct unit *ptransport = transporter_for_unit(punit);

  /* Helpless units cannot board a transport in their current state. */
  if (!helpless
      && ptransport != NULL) {
    unit_transport_load_tp_status(punit, ptransport, FALSE);
    send_unit_info(NULL, punit);
    return TRUE;
  } else {
    /* Only units that cannot find transport are considered for teleport. */
    if (teleporting) {
      struct city *pcity = find_closest_city(ptile, pexclcity,
                                             unit_owner(punit),
                                             FALSE, FALSE, FALSE, TRUE, FALSE,
                                             utype_class(pttype));
      if (pcity != NULL) {
        char tplink[MAX_LEN_LINK]; /* In case unit dies when teleported */

        sz_strlcpy(tplink, unit_link(punit));

        if (teleport_unit_to_city(punit, pcity, 0, FALSE)) {
          notify_player(pplayer, ptile, E_UNIT_RELOCATED, ftc_server,
                        _("%s escaped the destruction of %s, and fled to %s."),
                        tplink,
                        utype_name_translation(pttype),
                        city_link(pcity));
          return TRUE;
        }
      }
    }
  }

  /* The unit could not use transport on the tile, and could not teleport. */
  return FALSE;
}

/**********************************************************************//**
  We don't really change owner of the unit, but create completely new
  unit as its copy. The new pointer to 'punit' is returned.
  Always wipes the source unit but sometimes returns NULL or a unit
  of a player other than pplayer.
**************************************************************************/
struct unit *unit_change_owner(struct unit *punit, struct player *pplayer,
                               int homecity, enum unit_loss_reason reason)
{
  struct unit *gained_unit;
  int id = 0;

#ifndef FREECIV_NDEBUG
  bool placed;
#endif

  fc_assert(!utype_player_already_has_this_unique(pplayer,
                                                  unit_type_get(punit)));

  /* Convert the unit to your cause. It's supposed that the original unit
   * is on a valid tile and is not transported. */
  gained_unit = unit_virtual_prepare(pplayer, unit_tile(punit),
                                     unit_type_get(punit), punit->veteran,
                                     homecity, punit->moves_left, punit->hp);
  fc_assert_action(gained_unit, goto uco_wipe); /* Tile must be valid */

  /* Owner changes, nationality not. */
  gained_unit->nationality = punit->nationality;

  /* Copy some more unit fields */
  gained_unit->fuel = punit->fuel;
  gained_unit->paradropped = punit->paradropped;
  gained_unit->birth_turn = punit->birth_turn;
  gained_unit->current_form_turn = punit->current_form_turn;

  /* Fog is lifted in the placing algorithm. */
#ifndef FREECIV_NDEBUG
  placed =
#endif
    place_unit(gained_unit, pplayer,
               homecity ? game_city_by_number(homecity) : NULL,
               NULL, FALSE);

  fc_assert_action(placed, unit_virtual_destroy(gained_unit); goto uco_wipe);

  id = gained_unit->id;

  /* Update unit upkeep in the new homecity */
  if (homecity > 0) {
    city_units_upkeep(game_city_by_number(homecity));
  }

  /* Be sure to wipe the converted unit! */
  /* Old homecity upkeep is updated in process */
#ifndef FREECIV_NDEBUG
  uco_wipe:
#endif

  wipe_unit(punit, reason, NULL);

  if (!unit_is_alive(id)) {
    /* Destroyed by a script */
    return NULL;
  }

  return gained_unit;   /* Returns the replacement. */
}

/**********************************************************************//**
  Called when one unit kills another in combat (this function is only
  called in one place). It handles all side effects including
  notifications and killstack.
**************************************************************************/
void kill_unit(struct unit *pkiller, struct unit *punit, bool vet)
{
  char pkiller_link[MAX_LEN_LINK], punit_link[MAX_LEN_LINK];
  struct player *pvictim = unit_owner(punit);
  struct player *pvictor = unit_owner(pkiller);
  struct tile *deftile = unit_tile(punit);
  int unitcount = 0;
  bool escaped;
  bool flagless_killer = unit_has_type_flag(pkiller, UTYF_FLAGLESS);
  bool flagless_victim = unit_has_type_flag(punit, UTYF_FLAGLESS);
  const struct civ_map *nmap = &(wld.map);

  sz_strlcpy(pkiller_link, unit_link(pkiller));
  sz_strlcpy(punit_link, unit_tile_link(punit));

  /* The unit is doomed. */
  punit->server.dying = TRUE;

  unit_list_iterate(deftile->units, vunit) {
    if ((flagless_killer
         || pplayers_at_war(pvictor, unit_owner(vunit))
         || unit_has_type_flag(vunit, UTYF_FLAGLESS))
        && is_unit_reachable_at(vunit, pkiller, deftile)) {
      unitcount++;
    }
  } unit_list_iterate_end;

  if (!is_stack_vulnerable(deftile) || unitcount == 1) {
    if (vet) {
      notify_unit_experience(pkiller);
    }
    wipe_unit(punit, ULR_KILLED, pvictor);
  } else { /* Unitcount > 1 */
    int i;
    int slots = player_slot_count();
    int num_killed[slots];
    int num_escaped[slots];
    struct unit *other_killed[slots];

    fc_assert(unitcount > 1);

    /* Initialize */
    for (i = 0; i < slots; i++) {
      num_killed[i] = 0;
      other_killed[i] = NULL;
      num_escaped[i] = 0;
    }

    /* Count killed units */
    unit_list_iterate_safe(deftile->units, vunit) {
      struct player *vplayer = unit_owner(vunit);

      if (pplayers_at_war(pvictor, vplayer)
          && is_unit_reachable_at(vunit, pkiller, deftile)) {
        escaped = FALSE;

        if (is_action_enabled_unit_on_self(nmap, ACTION_ESCAPE, vunit)
            && !unit_has_type_flag(pkiller, UTYF_CANKILLESCAPING)
            && vunit->hp > 0
            && vunit->moves_left > pkiller->moves_left
            && fc_rand(2)) {
          int curr_def_bonus;
          int def_bonus = 0;
          struct tile *dsttile = NULL;
          int move_cost;

          fc_assert(vunit->hp > 0);

          adjc_iterate(nmap, deftile, ptile2) {
            if (can_exist_at_tile(nmap, vunit->utype, ptile2)
                && NULL == tile_city(ptile2)) {
              move_cost = map_move_cost_unit(nmap, vunit, ptile2);
              if (pkiller->moves_left <= vunit->moves_left - move_cost
                  && (is_allied_unit_tile(ptile2, pvictim)
                      || unit_list_size(ptile2->units)) == 0) {
                curr_def_bonus = tile_extras_defense_bonus(ptile2,
                                                           vunit->utype);
                if (def_bonus <= curr_def_bonus) {
                  def_bonus = curr_def_bonus;
                  dsttile = ptile2;
                }
              }
            }
          } adjc_iterate_end;

          if (dsttile != NULL) {
            escaped = (action_auto_perf_unit_do(AAPC_UNIT_STACK_DEATH,
                                                vunit, tile_owner(dsttile),
                                                NULL, NULL, dsttile,
                                                tile_city(dsttile),
                                                NULL, NULL)
                       != NULL);

            if (escaped) {
              num_escaped[player_index(vplayer)]++;
              unitcount--;
            }
          }
        }

        if (!escaped) {
          num_killed[player_index(vplayer)]++;

          if (vunit != punit) {
            other_killed[player_index(vplayer)] = vunit;
            other_killed[player_index(pvictor)] = vunit;
          }
        }
      }
    } unit_list_iterate_safe_end;

    /* Inform the destroyer again if more than one unit was killed */
    if (unitcount > 1) {
      if (flagless_victim) {
        notify_player(pvictor, unit_tile(pkiller), E_UNIT_WIN_ATT, ftc_server,
                      /* TRANS: "... Cannon ... Destroyer ...." */
                      PL_("Your attacking %s succeeded against %s "
                          "(and %d other unit)!",
                          "Your attacking %s succeeded against %s "
                          "(and %d other units)!", unitcount - 1),
                      pkiller_link,
                      punit_link,
                      unitcount - 1);
      } else {
        notify_player(pvictor, unit_tile(pkiller), E_UNIT_WIN_ATT, ftc_server,
                      /* TRANS: "... Cannon ... the Polish Destroyer ...." */
                      PL_("Your attacking %s succeeded against the %s %s "
                          "(and %d other unit)!",
                          "Your attacking %s succeeded against the %s %s "
                          "(and %d other units)!", unitcount - 1),
                      pkiller_link,
                      nation_adjective_for_player(pvictim),
                      punit_link,
                      unitcount - 1);
      }
    }

    if (vet) {
      notify_unit_experience(pkiller);
    }

    /* Inform the owners: this only tells about owned units that were killed.
     * there may have been 20 units who died but if only 2 belonged to the
     * particular player they'll only learn about those.
     *
     * Also if a large number of units die you don't find out what type
     * they all are. */
    for (i = 0; i < slots; i++) {
      if (num_killed[i] == 1) {
        if (i == player_index(pvictim)) {
          fc_assert(other_killed[i] == NULL);

          if (flagless_killer) {
            notify_player(player_by_number(i), deftile,
                          E_UNIT_LOST_DEF, ftc_server,
                          /* TRANS: "Cannon ... Destroyer." */
                          _("%s lost to an attack by %s."),
                          punit_link,
                          pkiller_link);
          } else {
            notify_player(player_by_number(i), deftile,
                          E_UNIT_LOST_DEF, ftc_server,
                          /* TRANS: "Cannon ... the Polish Destroyer." */
                          _("%s lost to an attack by the %s %s."),
                          punit_link,
                          nation_adjective_for_player(pvictor),
                          pkiller_link);
          }
        } else {
          fc_assert(other_killed[i] != punit);

          if (flagless_victim) {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Cannon lost when Destroyer
                             * attacked Musketeers." */
                            _("%s lost when %s attacked %s."),
                            unit_link(other_killed[i]),
                            pkiller_link,
                            punit_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Cannon lost when the Polish Destroyer
                             * attacked Musketeers." */
                            _("%s lost when the %s %s attacked %s."),
                            unit_link(other_killed[i]),
                            nation_adjective_for_player(pvictor),
                            pkiller_link,
                            punit_link);
            }
          } else {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Cannon lost when Destroyer
                             * attacked the German Musketeers." */
                            _("%s lost when %s attacked the %s %s."),
                            unit_link(other_killed[i]),
                            pkiller_link,
                            nation_adjective_for_player(pvictim),
                            punit_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Cannon lost when the Polish Destroyer
                             * attacked the German Musketeers." */
                            _("%s lost when the %s %s attacked the %s %s."),
                            unit_link(other_killed[i]),
                            nation_adjective_for_player(pvictor),
                            pkiller_link,
                            nation_adjective_for_player(pvictim),
                            punit_link);
            }
          }
        }
      } else if (num_killed[i] > 1) {
        if (i == player_index(pvictim)) {
          int others = num_killed[i] - 1;

          if (others == 1) {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Musketeers (and Cannon) lost to an
                             * attack from Destroyer." */
                            _("%s (and %s) lost to an attack from %s."),
                            punit_link,
                            unit_link(other_killed[i]),
                            pkiller_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Musketeers (and Cannon) lost to an
                             * attack from the Polish Destroyer." */
                            _("%s (and %s) lost to an attack from the %s %s."),
                            punit_link,
                            unit_link(other_killed[i]),
                            nation_adjective_for_player(pvictor),
                            pkiller_link);
            }
          } else {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Musketeers and 3 other units lost to
                             * an attack from Destroyer."
                             * (only happens with at least 2 other units) */
                            PL_("%s and %d other unit lost to an attack "
                                "from %s.",
                                "%s and %d other units lost to an attack "
                                "from %s.", others),
                            punit_link,
                            others,
                            pkiller_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "Musketeers and 3 other units lost to
                             * an attack from the Polish Destroyer."
                             * (only happens with at least 2 other units) */
                            PL_("%s and %d other unit lost to an attack "
                                "from the %s %s.",
                                "%s and %d other units lost to an attack "
                                "from the %s %s.", others),
                            punit_link,
                            others,
                            nation_adjective_for_player(pvictor),
                            pkiller_link);
            }
          }
        } else {
          if (flagless_victim) {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "2 units lost when Destroyer
                             * attacked Musketeers."
                             * (only happens with at least 2 other units) */
                            PL_("%d unit lost when %s attacked %s.",
                                "%d units lost when %s attacked %s.",
                                num_killed[i]),
                            num_killed[i],
                            pkiller_link,
                            punit_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "2 units lost when the Polish Destroyer
                             * attacked Musketeers."
                             * (only happens with at least 2 other units) */
                            PL_("%d unit lost when the %s %s attacked %s.",
                                "%d units lost when the %s %s attacked %s.",
                                num_killed[i]),
                            num_killed[i],
                            nation_adjective_for_player(pvictor),
                            pkiller_link,
                            punit_link);
            }
          } else {
            if (flagless_killer) {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "2 units lost when Destroyer
                             * attacked the German Musketeers."
                             * (only happens with at least 2 other units) */
                            PL_("%d unit lost when %s attacked the %s %s.",
                                "%d units lost when %s attacked the %s %s.",
                                num_killed[i]),
                            num_killed[i],
                            pkiller_link,
                            nation_adjective_for_player(pvictim),
                            punit_link);
            } else {
              notify_player(player_by_number(i), deftile,
                            E_UNIT_LOST_DEF, ftc_server,
                            /* TRANS: "2 units lost when the Polish Destroyer
                             * attacked the German Musketeers."
                             * (only happens with at least 2 other units) */
                            PL_("%d unit lost when the %s %s attacked the %s %s.",
                                "%d units lost when the %s %s attacked the %s %s.",
                                num_killed[i]),
                            num_killed[i],
                            nation_adjective_for_player(pvictor),
                            pkiller_link,
                            nation_adjective_for_player(pvictim),
                            punit_link);
            }
          }
        }
      }
    }

    /* Inform the owner of the units that escaped.
     * 'deftile' is the original tile they defended at, not where
     * they escaped to, as there might be multiple different tiles
     * different units escaped to. */
    for (i = 0; i < slots; i++) {
      if (0 < num_escaped[i]) {
        if (flagless_killer) {
          notify_player(player_by_number(i), deftile,
                        E_UNIT_ESCAPED, ftc_server,
                        PL_("%d unit escaped from attack by %s",
                            "%d units escaped from attack by %s",
                            num_escaped[i]),
                        num_escaped[i],
                        pkiller_link);
        } else {
          notify_player(player_by_number(i), deftile,
                        E_UNIT_ESCAPED, ftc_server,
                        PL_("%d unit escaped from attack by %s %s",
                            "%d units escaped from attack by %s %s",
                            num_escaped[i]),
                        num_escaped[i],
                        nation_adjective_for_player(pvictor),
                        pkiller_link);
        }
      }
    }

    /* Remove the units - note the logic of which units actually die
     * must be mimiced exactly in at least one place up above. */
    punit = NULL; /* Wiped during following iteration so unsafe to use */

    unit_list_iterate_safe(deftile->units, punit2) {
      if (pplayers_at_war(pvictor, unit_owner(punit2))
	  && is_unit_reachable_at(punit2, pkiller, deftile)) {
        wipe_unit(punit2, ULR_KILLED, pvictor);
      }
    } unit_list_iterate_safe_end;
  }
}

/**********************************************************************//**
  Called when one unit collects ransom of another.
**************************************************************************/
void collect_ransom(struct unit *pcollector, struct unit *punit, bool vet)
{
  struct player *pvictim = unit_owner(punit);
  struct player *pvictor = unit_owner(pcollector);
  char pcollector_link[MAX_LEN_LINK], punit_link[MAX_LEN_LINK];
  struct tile *deftile = unit_tile(punit);
  int unitcount, ransom;

  /* The unit is doomed. */
  punit->server.dying = TRUE;

  unitcount = unit_list_size(deftile->units);
  ransom = unitcount * game.server.ransom_gold;

  if (pvictim->economic.gold < ransom) {
    ransom = pvictim->economic.gold;
  }

  notify_player(pvictor, unit_tile(pcollector), E_UNIT_WIN_ATT, ftc_server,
                PL_("%d Barbarian leader captured.",
                    "%d Barbarian leaders captured.",
                    unitcount),
                unitcount);
  notify_player(pvictor, unit_tile(pcollector), E_UNIT_WIN_ATT, ftc_server,
                PL_("%d gold ransom paid.",
                    "%d gold ransom paid.",
                    ransom),
                ransom);
  pvictor->economic.gold += ransom;
  pvictim->economic.gold -= ransom;
  send_player_info_c(pvictor, NULL);   /* Let me see my new gold :-) */

  if (vet) {
    notify_unit_experience(pcollector);
  }

  sz_strlcpy(pcollector_link, unit_link(pcollector));
  sz_strlcpy(punit_link, unit_tile_link(punit));

  if (!is_stack_vulnerable(deftile) || unitcount == 1) {
    wipe_unit(punit, ULR_KILLED, pvictor);
  } else {
    int i;
    int slots = player_slot_count();
    int num_collected[slots];

    fc_assert(unitcount > 1);

    /* Initialize */
    for (i = 0; i < slots; i++) {
      num_collected[i] = 0;
    }

    /* Count captured units */
    unit_list_iterate_safe(deftile->units, vunit) {
      struct player *vplayer = unit_owner(vunit);

      num_collected[player_index(vplayer)]++;
    } unit_list_iterate_safe_end;

    for (i = 0; i < slots; i++) {
      if (num_collected[i] == 1) {
        notify_player(player_by_number(i), deftile,
                      E_UNIT_LOST_DEF, ftc_server,
                      _("%s %s collected ransom of %s."),
                      nation_adjective_for_player(pvictor),
                      pcollector_link, punit_link);
      } else if (num_collected[i] > 1) {
        notify_player(player_by_number(i), deftile,
                      E_UNIT_LOST_DEF, ftc_server,
                      PL_("%s %s collected ransom of %s and %d other unit.",
                          "%s %s collected ransom of %s and %d other units.",
                          num_collected[i] - 1),
                      nation_adjective_for_player(pvictor),
                      pcollector_link,
                      punit_link,
                      num_collected[i] - 1);
      }
    }

    unit_list_iterate_safe(deftile->units, punit2) {
      wipe_unit(punit2, ULR_KILLED, pvictor);
    } unit_list_iterate_safe_end;
  }
}

/**********************************************************************//**
  Package a unit_info packet. This packet contains basically all
  information about a unit.
**************************************************************************/
void package_unit(struct unit *punit, struct packet_unit_info *packet)
{
  packet->id = punit->id;
  packet->owner = player_number(unit_owner(punit));
  packet->nationality = player_number(unit_nationality(punit));
  packet->tile = tile_index(unit_tile(punit));
  packet->facing = punit->facing;
  packet->homecity = punit->homecity;
  output_type_iterate(o) {
    packet->upkeep[o] = punit->upkeep[o];
  } output_type_iterate_end;
  packet->veteran = punit->veteran;
  packet->type = utype_number(unit_type_get(punit));
  packet->movesleft = punit->moves_left;
  packet->hp = punit->hp;
  packet->activity = punit->activity;
  packet->activity_count = punit->activity_count;

  if (punit->activity_target != NULL) {
    packet->activity_tgt = extra_index(punit->activity_target);
  } else {
    packet->activity_tgt = EXTRA_NONE;
  }

  packet->changed_from = punit->changed_from;
  packet->changed_from_count = punit->changed_from_count;

  if (punit->changed_from_target != NULL) {
    packet->changed_from_tgt = extra_index(punit->changed_from_target);
  } else {
    packet->changed_from_tgt = EXTRA_NONE;
  }

  packet->ssa_controller = punit->ssa_controller;
  packet->fuel = punit->fuel;
  packet->goto_tile = (NULL != punit->goto_tile
                       ? tile_index(punit->goto_tile) : -1);
  packet->paradropped = punit->paradropped;
  packet->done_moving = punit->done_moving;
  packet->stay        = punit->stay;
  packet->birth_turn  = punit->birth_turn;
  packet->current_form_turn = punit->current_form_turn;
  if (!unit_transported(punit)) {
    packet->transported = FALSE;
    packet->transported_by = 0;
  } else {
    packet->transported = TRUE;
    packet->transported_by = unit_transport_get(punit)->id;
  }
  if (punit->carrying != NULL) {
    packet->carrying = goods_index(punit->carrying);
  } else {
    packet->carrying = -1;
  }
  packet->occupied = (get_transporter_occupancy(punit) > 0);
  packet->battlegroup = punit->battlegroup;
  packet->has_orders = punit->has_orders;
  if (punit->has_orders) {
    packet->orders_length = punit->orders.length;
    packet->orders_index = punit->orders.index;
    packet->orders_repeat = punit->orders.repeat;
    packet->orders_vigilant = punit->orders.vigilant;
    memcpy(packet->orders, punit->orders.list,
           punit->orders.length * sizeof(struct unit_order));
  } else {
    packet->orders_length = packet->orders_index = 0;
    packet->orders_repeat = packet->orders_vigilant = FALSE;
    /* No need to initialize array. */
  }

  packet->action_decision_want = punit->action_decision_want;
  packet->action_decision_tile = (punit->action_decision_tile
                                  ? tile_index(punit->action_decision_tile)
                                  : IDENTITY_NUMBER_ZERO);
}

/**********************************************************************//**
  Package a short_unit_info packet. This contains a limited amount of
  information about the unit, and is sent to players who shouldn't know
  everything (like the unit's owner's enemies).
**************************************************************************/
void package_short_unit(struct unit *punit,
			struct packet_unit_short_info *packet,
                        enum unit_info_use packet_use, int info_city_id)
{
  packet->packet_use = packet_use;
  packet->info_city_id = info_city_id;

  packet->id = punit->id;

  if (unit_has_type_flag(punit, UTYF_FLAGLESS)) {
    packet->owner = OWNER_NONE;
  } else {
    packet->owner = player_number(unit_owner(punit));
  }

  packet->tile = tile_index(unit_tile(punit));
  packet->facing = punit->facing;
  packet->veteran = punit->veteran;
  packet->type = utype_number(unit_type_get(punit));
  packet->hp = punit->hp;
  packet->occupied = (get_transporter_occupancy(punit) > 0);
  if (punit->activity == ACTIVITY_EXPLORE
      || punit->activity == ACTIVITY_GOTO) {
    packet->activity = ACTIVITY_IDLE;
  } else {
    packet->activity = punit->activity;
  }

  if (punit->activity_target == nullptr) {
    packet->activity_tgt = EXTRA_NONE;
  } else {
    packet->activity_tgt = extra_index(punit->activity_target);
  }

  /* Transported_by information is sent to the client even for units that
   * aren't fully known. Note that for non-allied players, any transported
   * unit can't be seen at all. For allied players we have to know if
   * transporters have room in them so that we can load units properly. */
  if (!unit_transported(punit)) {
    packet->transported = FALSE;
    packet->transported_by = 0;
  } else {
    packet->transported = TRUE;
    packet->transported_by = unit_transport_get(punit)->id;
  }
}

/**********************************************************************//**
  Handle situation where unit goes out of player sight.
**************************************************************************/
void unit_goes_out_of_sight(struct player *pplayer, struct unit *punit)
{
  dlsend_packet_unit_remove(pplayer->connections, punit->id);
  if (punit->server.moving != NULL) {
    /* Update status of 'pplayer' vision for 'punit'. */
    BV_CLR(punit->server.moving->can_see_unit, player_index(pplayer));
  }
}

/**********************************************************************//**
  Send the unit to the players who need the info.
  dest = NULL means all connections (game.est_connections)
**************************************************************************/
void send_unit_info(struct conn_list *dest, struct unit *punit)
{
  const struct player *powner;
  struct packet_unit_info info;
  struct packet_unit_short_info sinfo;
  struct unit_move_data *pdata;

  if (dest == NULL) {
    dest = game.est_connections;
  }

  CHECK_UNIT(punit);

  powner = unit_owner(punit);
  package_unit(punit, &info);
  package_short_unit(punit, &sinfo, UNIT_INFO_IDENTITY, 0);
  pdata = punit->server.moving;

  conn_list_iterate(dest, pconn) {
    struct player *pplayer = conn_get_player(pconn);

    /* Be careful to consider all cases where pplayer is NULL... */
    if (pplayer == NULL) {
      if (pconn->observer) {
        send_packet_unit_info(pconn, &info);
      }
    } else if (pplayer == powner) {
      send_packet_unit_info(pconn, &info);
      if (pdata != NULL) {
        BV_SET(pdata->can_see_unit, player_index(pplayer));
      }
    } else if (can_player_see_unit(pplayer, punit)) {
      send_packet_unit_short_info(pconn, &sinfo, FALSE);
      if (pdata != NULL) {
        BV_SET(pdata->can_see_unit, player_index(pplayer));
      }
    }
  } conn_list_iterate_end;
}

/**********************************************************************//**
  For each specified connections, send information about all the units
  known to that player/conn.
**************************************************************************/
void send_all_known_units(struct conn_list *dest)
{
  conn_list_do_buffer(dest);
  conn_list_iterate(dest, pconn) {
    struct player *pplayer = pconn->playing;

    if (NULL == pplayer && !pconn->observer) {
      continue;
    }

    players_iterate(unitowner) {
      unit_list_iterate(unitowner->units, punit) {
        send_unit_info(dest, punit);
      } unit_list_iterate_end;
    } players_iterate_end;
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
  flush_packets();
}

/**********************************************************************//**
  Nuke a square: 1) remove all units on the square, and 2) halve the
  size of the city on the square.

  If it isn't a city square or an ocean square then with 50% chance add
  some fallout, then notify the client about the changes.
**************************************************************************/
static void do_nuke_tile(struct player *pplayer, struct tile *ptile,
                         const struct action *paction)
{
  struct city *pcity = NULL;
  int pop_loss;

  pcity = tile_city(ptile);

  unit_list_iterate_safe(ptile->units, punit) {

    /* unit in a city may survive */
    if (pcity && fc_rand(100) < game.info.nuke_defender_survival_chance_pct) {
      continue;
    }
    notify_player(unit_owner(punit), ptile, E_UNIT_LOST_MISC, ftc_server,
                  _("Your %s was nuked by %s."),
                  unit_tile_link(punit),
                  pplayer == unit_owner(punit)
                  ? _("yourself")
                  : nation_plural_for_player(pplayer));
    if (unit_owner(punit) != pplayer) {
      notify_player(pplayer, ptile, E_UNIT_WIN_ATT, ftc_server,
                    _("The %s %s was nuked."),
                    nation_adjective_for_player(unit_owner(punit)),
                    unit_tile_link(punit));
    }
    wipe_unit(punit, ULR_NUKE, pplayer);
  } unit_list_iterate_safe_end;


  if (pcity) {
    struct player *owner = city_owner(pcity);
    char city_name[MAX_LEN_LINK];

    sz_strlcpy(city_name, city_link(pcity));

    notify_player(owner, ptile, E_CITY_NUKED, ftc_server,
                  _("%s was nuked by %s."),
                  city_name,
                  pplayer == owner
                  ? _("yourself")
                  : nation_plural_for_player(pplayer));

    if (owner != pplayer) {
      notify_player(pplayer, ptile, E_CITY_NUKED, ftc_server,
                    _("You nuked %s."),
                    city_name);
    }

    pop_loss = round((game.info.nuke_pop_loss_pct * city_size_get(pcity)) / 100.0);
    if (city_reduce_size(pcity, pop_loss, pplayer, "nuke")) {
      /* Send city size reduction to everyone seeing it */
      send_city_info(NULL, pcity);
    } else {
      /* City was destroyed */
      notify_player(owner, ptile, E_CITY_NUKED, ftc_server,
                    _("%s was destroyed by the nuke."),
                    city_name);
      if (owner != pplayer) {
        notify_player(pplayer, ptile, E_CITY_NUKED, ftc_server,
                      _("Your nuke destroyed %s."),
                      city_name);
      }
    }
  }

  if (fc_rand(2) == 1) {
    struct extra_type *pextra;

    pextra = rand_extra_for_tile(ptile, EC_FALLOUT, FALSE);
    if (pextra != NULL && !tile_has_extra(ptile, pextra)) {
      tile_add_extra(ptile, pextra);
      update_tile_knowledge(ptile);
    }
  }
}

/**********************************************************************//**
  Nuke all the squares in a the specified radius around the center of the
  explosion.
  @param paction the action that caused the explosion
  @param act_utype the type of the unit that caused to explosionn.
  @param pplayer the player that caused the explosion.
  @param ptile the tile at the center of the explosion.
**************************************************************************/
void do_nuclear_explosion(const struct action *paction,
                          const struct unit_type *act_utype,
                          struct player *pplayer, struct tile *ptile)
{
  int nuke_radius_size
      = get_target_bonus_effects(NULL,
                                 &(const struct req_context) {
                                   .player = pplayer,
                                 /* City: Wait for users before choosing
                                  * home city or target tile city */
                                   .tile = ptile,
                                   .unittype = act_utype,
                                   .action = paction,
                                 },
                                 NULL,
                                 EFT_NUKE_BLAST_RADIUS_1_SQ);

  circle_iterate(&(wld.map), ptile, nuke_radius_size, ptile1) {
    do_nuke_tile(pplayer, ptile1, paction);
  } circle_iterate_end;

  script_server_signal_emit("nuke_exploded", 2, API_TYPE_TILE, ptile,
                            API_TYPE_PLAYER, pplayer);
  notify_conn(NULL, ptile, E_NUKE, ftc_server,
              _("The %s detonated a nuke!"),
              nation_plural_for_player(pplayer));
}

/**********************************************************************//**
  Go by airline, if both cities have an airport and neither has been used this
  turn the unit will be transported by it and have its moves set to 0
**************************************************************************/
bool do_airline(struct unit *punit, struct city *pdest_city,
                const struct action *paction)
{
  struct city *psrc_city = tile_city(unit_tile(punit));

  notify_player(unit_owner(punit), city_tile(pdest_city),
                E_UNIT_RELOCATED, ftc_server,
                _("%s transported successfully."),
                unit_link(punit));

  unit_move(punit, pdest_city->tile, punit->moves_left,
            NULL, BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK),
            /* Can only airlift to allied and domestic cities */
            FALSE, FALSE,
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
            BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN));

  /* Update airlift fields. */
  if (!(game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)) {
    psrc_city->airlift--;
    send_city_info(city_owner(psrc_city), psrc_city);
  }
  if (!(game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)) {
    pdest_city->airlift--;
    send_city_info(city_owner(pdest_city), pdest_city);
  }

  return TRUE;
}

/**********************************************************************//**
  Autoexplore with unit.
**************************************************************************/
void do_explore(struct unit *punit)
{
  switch (manage_auto_explorer(punit)) {
   case MR_DEATH:
     /* don't use punit! */
     return;
   case MR_NOT_ALLOWED:
     /* Needed for something else */
     return;
   case MR_OK:
     /* FIXME: manage_auto_explorer() isn't supposed to change the activity,
      * but don't count on this. See PR#39792.
      */
     if (punit->activity == ACTIVITY_EXPLORE) {
       break;
     }

     fc__fallthrough;
   default:
     unit_activity_handling(punit, ACTIVITY_IDLE, ACTION_NONE);

     /* FIXME: When the manage_auto_explorer() call changes the activity from
      * EXPLORE to IDLE, in unit_activity_handling() ai.control is left
      * alone. We reset it here. See PR#12931. */
     punit->ssa_controller = SSA_NONE;
     break;
  }

  send_unit_info(NULL, punit); /* Probably duplicate */
}

/**********************************************************************//**
  Returns whether the drop was made or not. Note that it also returns 1
  in the case where the drop was successful, but the unit was killed by
  barbarians in a hut.
**************************************************************************/
bool do_paradrop(struct unit *punit, struct tile *ptile,
                 const struct action *paction)
{
  struct player *pplayer = unit_owner(punit);
  struct player *tgt_player = tile_owner(ptile);
  const struct unit_type *act_utype = unit_type_get(punit);
  const struct city *pcity;

  /* Hard requirements */
  /* FIXME: hard requirements belong in common/actions's
   * is_action_possible() and the explanation texts belong in
   * server/unithand's action not enabled system (expl_act_not_enabl(),
   * ane_kind, explain_why_no_action_enabled(), etc)
   */
  if (!map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
    /* Only take in account values from player map. */
    const struct player_tile *plrtile = map_get_player_tile(ptile, pplayer);

    if (NULL == plrtile->site) {
      bv_extras fbv;

      dbv_to_bv(fbv.vec, &(plrtile->extras));

      if (!is_native_to_class(unit_class_get(punit), plrtile->terrain,
                              &fbv)) {
        notify_player(pplayer, ptile, E_BAD_COMMAND, ftc_server,
                      _("This unit cannot paradrop into %s."),
                      terrain_name_translation(plrtile->terrain));
        return FALSE;
      }
    }

    if (NULL != plrtile->site
        && plrtile->owner != NULL
        && !pplayers_allied(pplayer, plrtile->owner)
        && !action_has_result(paction, ACTRES_PARADROP_CONQUER)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND, ftc_server,
                    /* TRANS: Paratroopers ... Paradrop Unit */
                    _("%s cannot conquer a city with \"%s\"."),
                    unit_name_translation(punit),
                    action_name_translation(paction));
      return FALSE;
    }

    if (NULL != plrtile->site
        && plrtile->owner != NULL
        && (pplayers_non_attack(pplayer, plrtile->owner)
            || (player_diplstate_get(pplayer, plrtile->owner)->type
                == DS_ALLIANCE)
            || (player_diplstate_get(pplayer, plrtile->owner)->type
                == DS_TEAM))
        && action_has_result(paction, ACTRES_PARADROP_CONQUER)) {
      notify_player(pplayer, ptile, E_BAD_COMMAND, ftc_server,
                    _("Cannot attack unless you declare war first."));
      return FALSE;
    }
  }


  /* Kill the unit when the landing goes wrong. */

  /* Safe terrain, really? Not transformed since player last saw it. */
  if (!can_unit_exist_at_tile(&(wld.map), punit, ptile)
      && (!BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK)
          || !unit_could_load_at(punit, ptile))) {
    map_show_circle(pplayer, ptile, act_utype->vision_radius_sq);
    notify_player(pplayer, ptile, E_UNIT_LOST_MISC, ftc_server,
                  _("Your %s paradropped into the %s and was lost."),
                  unit_tile_link(punit),
                  terrain_name_translation(tile_terrain(ptile)));
    pplayer->score.units_lost++;
    server_remove_unit(punit, ULR_NONNATIVE_TERR);
    return TRUE;
  }

  pcity = tile_city(ptile);

  if ((pcity != NULL && !pplayers_allied(pplayer, city_owner(pcity))
       && !action_has_result(paction, ACTRES_PARADROP_CONQUER))
      || is_non_allied_unit_tile(ptile, pplayer,
                                 unit_has_type_flag(punit, UTYF_FLAGLESS))) {
    struct player *main_victim = NULL;
    struct player *secondary_victim = NULL;
    char victim_link[MAX_LEN_LINK];

    map_show_circle(pplayer, ptile, act_utype->vision_radius_sq);
    unit_make_contact(punit, ptile, pplayer);
    notify_player(pplayer, ptile, E_UNIT_LOST_MISC, ftc_server,
                  _("Your %s was killed by enemy units at the "
                    "paradrop destination."),
                  unit_tile_link(punit));
    /* TODO: Should defender score.units_killed get increased too?
     * What if there's units of several allied players? Should the
     * city owner or owner of the first/random unit get the kill? */
    pplayer->score.units_lost++;

    if (pcity != NULL) {
      struct player *owner = city_owner(pcity);

      if (!pplayers_at_war(pplayer, owner)) {
        main_victim = owner;
      } else {
        secondary_victim = owner;
      }

      sz_strlcpy(victim_link, city_link(pcity));
    } else {
      sz_strlcpy(victim_link, tile_link(ptile));
    }

    if (main_victim == NULL) {
      unit_list_iterate(ptile->units, tgt) {
        struct player *owner = unit_owner(tgt);

        if (!pplayers_at_war(pplayer, owner)) {
          main_victim = owner;
          break;
        } else if (secondary_victim == NULL) {
          secondary_victim = owner;
        }
      } unit_list_iterate_end;

      if (main_victim == NULL) {
        /* There's no victim with whom the attacker isn't in war,
         * fallback to one with whom there's already a war. */
        main_victim = secondary_victim;
      }
    }

    action_consequence_caught(paction, pplayer, act_utype,
                              main_victim, ptile,
                              victim_link);

    server_remove_unit(punit, ULR_KILLED);
    return TRUE;
  }

  /* All ok */
  punit->paradropped = TRUE;
  if (unit_move(punit, ptile,
                /* Done by Action_Success_Actor_Move_Cost */
                0,
                NULL, BV_ISSET(paction->sub_results,
                               ACT_SUB_RES_MAY_EMBARK),
                paction->result == ACTRES_PARADROP_CONQUER,
                paction->result == ACTRES_PARADROP_CONQUER,
                BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_ENTER),
                BV_ISSET(paction->sub_results, ACT_SUB_RES_HUT_FRIGHTEN))) {
    /* Ensure we finished on valid state. */
    fc_assert(can_unit_exist_at_tile(&(wld.map), punit, unit_tile(punit))
              || unit_transported(punit));
  }

  /* May cause an incident */
  action_consequence_success(paction, pplayer, act_utype, tgt_player,
                             ptile, tile_link(ptile));

  return TRUE;
}

/**********************************************************************//**
  Give 25 Gold or kill the unit. For H_LIMITEDHUTS
  Return TRUE if unit is alive, and FALSE if it was killed
**************************************************************************/
static bool hut_get_limited(struct unit *punit)
{
  bool ok = TRUE;
  int hut_chance = fc_rand(12);
  struct player *pplayer = unit_owner(punit);
  const struct civ_map *nmap = &(wld.map);

  /* 1 in 12 to get barbarians */
  if (hut_chance != 0) {
    int cred = 25;

    notify_player(pplayer, unit_tile(punit), E_HUT_GOLD, ftc_server,
                  PL_("You found %d gold.",
                      "You found %d gold.", cred), cred);
    pplayer->economic.gold += cred;
  } else if (city_exists_within_max_city_map(nmap, unit_tile(punit), TRUE)
             || unit_has_type_flag(punit, UTYF_GAMELOSS)) {
    notify_player(pplayer, unit_tile(punit),
                  E_HUT_BARB_CITY_NEAR, ftc_server,
                  _("An abandoned village is here."));
  } else {
    notify_player(pplayer, unit_tile(punit), E_HUT_BARB_KILLED, ftc_server,
                  _("Your %s has been killed by barbarians!"),
                  unit_tile_link(punit));
    wipe_unit(punit, ULR_BARB_UNLEASH, NULL);
    ok = FALSE;
  }

  return ok;
}

/**********************************************************************//**
  Due to the effects in the scripted hut behavior can not be predicted,
  unit_enter_hut returns nothing.
**************************************************************************/
static void unit_enter_hut(struct unit *punit, bool frighten_hut)
{
  struct player *pplayer = unit_owner(punit);
  int id = punit->id;
  struct tile *ptile = unit_tile(punit);
  bool hut = FALSE;
  const struct req_context context = {
    .player = pplayer,
    .tile = ptile,
  };

  extra_type_by_rmcause_iterate(ERM_ENTER, pextra) {
    if (tile_has_extra(ptile, pextra)
        && are_reqs_active(&context,
                           &(const struct req_context) {
                             .player = tile_owner(ptile),
                           },
                           &pextra->rmreqs,
                           RPT_CERTAIN)
       ) {
      hut = TRUE;
      /* FIXME: are all enter-removes extras worth counting? */
      pplayer->server.huts++;

      destroy_extra(ptile, pextra);
      update_tile_knowledge(unit_tile(punit));

      /* FIXME: enable different classes
       * to behave differently with different huts */
      if (frighten_hut) {
        script_server_signal_emit("hut_frighten", punit,
                                  extra_rule_name(pextra));
      } else if (is_ai(pplayer) && has_handicap(pplayer, H_LIMITEDHUTS)) {
        /* AI with H_LIMITEDHUTS only gets 25 gold (or barbs if unlucky) */
        (void) hut_get_limited(punit);
      } else {
        script_server_signal_emit("hut_enter", punit, extra_rule_name(pextra));
      }

      /* We need punit for the callbacks, can't continue if the unit died */
      if (!unit_is_alive(id)) {
        break;
      }
    }
  } extra_type_by_rmcause_iterate_end;

  if (hut) {
    send_player_info_c(pplayer, pplayer->connections); /* eg, gold */
  }
  return;
}

/**********************************************************************//**
  Put the unit onto the transporter, and tell everyone.
**************************************************************************/
void unit_transport_load_send(struct unit *punit, struct unit *ptrans)
{
  bv_player can_see_unit;

  fc_assert_ret(punit != NULL);
  fc_assert_ret(ptrans != NULL);

  BV_CLR_ALL(can_see_unit);
  players_iterate(pplayer) {
    if (can_player_see_unit(pplayer, punit)) {
      BV_SET(can_see_unit, player_index(pplayer));
    }
  } players_iterate_end;

  unit_transport_load(punit, ptrans, FALSE);

  players_iterate(pplayer) {
    if (BV_ISSET(can_see_unit, player_index(pplayer))
        && !can_player_see_unit(pplayer, punit)) {
      unit_goes_out_of_sight(pplayer, punit);
    }
  } players_iterate_end;

  send_unit_info(NULL, punit);
  send_unit_info(NULL, ptrans);
}

/**********************************************************************//**
  Load unit to transport, send transport's loaded status to everyone.
**************************************************************************/
static void unit_transport_load_tp_status(struct unit *punit,
                                          struct unit *ptrans,
                                          bool force)
{
  bool had_cargo;

  fc_assert_ret(punit != NULL);
  fc_assert_ret(ptrans != NULL);

  had_cargo = get_transporter_occupancy(ptrans) > 0;

  unit_transport_load(punit, ptrans, force);

  if (!had_cargo) {
    /* Transport's loaded status changed */
    send_unit_info(NULL, ptrans);
  }
}

/**********************************************************************//**
  Pull the unit off of the transporter, and tell everyone.
**************************************************************************/
void unit_transport_unload_send(struct unit *punit)
{
  struct unit *ptrans;

  fc_assert_ret(punit);

  ptrans = unit_transport_get(punit);

  fc_assert_ret(ptrans);

  unit_transport_unload(punit);

  send_unit_info(NULL, punit);
  send_unit_info(NULL, ptrans);
}

/**********************************************************************//**
  Used when unit_survive_autoattack()'s autoattack_prob_list
  autoattack frees its items.
**************************************************************************/
static void autoattack_prob_free(struct autoattack_prob *prob)
{
  free(prob);
}

/**********************************************************************//**
  This function is passed to autoattack_prob_list_sort() to sort a list of
  units and action probabilities according to their win chance against the
  autoattack target, modified by transportation relationships.

  The reason for making sure that a cargo unit is ahead of its
  transporter(s) is to leave transports out of combat if at all possible.
  (The transport could be destroyed during combat.)
**************************************************************************/
static int compare_units(const struct autoattack_prob *const *p1,
                         const struct autoattack_prob *const *q1)
{
  const struct unit *p1unit = game_unit_by_number((*p1)->unit_id);
  const struct unit *q1unit = game_unit_by_number((*q1)->unit_id);

  /* Sort by transport depth first. This makes sure that no transport
   * attacks before its cargo does -- cargo sorts earlier in the list. */
  {
    const struct unit *p1trans = p1unit, *q1trans = q1unit;

    /* Walk the transport stacks in parallel, so as to bail out as soon as
     * one of them is empty (avoid walking deep stacks more often than
     * necessary). */
    while (p1trans && q1trans) {
      p1trans = unit_transport_get(p1trans);
      q1trans = unit_transport_get(q1trans);
    }
    if (!p1trans && q1trans) {
      /* q1 is at greater depth (perhaps it's p1's cargo). It should sort
       * earlier in the list (p1 > q1). */
      return 1;
    } else if (p1trans && !q1trans) {
      /* p1 is at greater depth, so should sort earlier (p1 < q1). */
      return -1;
    }
    /* else same depth, so move on to checking win chance: */
  }

  /* Put the units with the highest probability of success first. The up
   * side of this is that units with bonuses against the victim attacks
   * before other units. The downside is that strong units can be led
   * away by sacrificial units. */
  return (-1
          /* Assume the worst. */
          * action_prob_cmp_pessimist((*p1)->prob, (*q1)->prob));
}

/**********************************************************************//**
  Check if unit survives enemy autoattacks. We assume that any
  unit that is adjacent to us can see us.
**************************************************************************/
static bool unit_survive_autoattack(struct unit *punit)
{
  struct autoattack_prob_list *autoattack;
  int moves = punit->moves_left;
  int sanity1 = punit->id;
  struct civ_map *nmap = &(wld.map);

  if (!game.server.autoattack) {
    return TRUE;
  }

  autoattack = autoattack_prob_list_new_full(autoattack_prob_free);

  /* Kludge to prevent attack power from dropping to zero during calc */
  punit->moves_left = MAX(punit->moves_left, 1);

  adjc_iterate(nmap, unit_tile(punit), ptile) {
    /* First add all eligible units to a autoattack list */
    unit_list_iterate(ptile->units, penemy) {
      struct autoattack_prob *probability = fc_malloc(sizeof(*probability));
      struct tile *tgt_tile = unit_tile(punit);

      fc_assert_action(tgt_tile, continue);

      probability->prob =
          action_auto_perf_unit_prob(AAPC_UNIT_MOVED_ADJ,
                                     penemy, unit_owner(punit), NULL, NULL,
                                     tgt_tile, tile_city(tgt_tile),
                                     punit, NULL);

      if (action_prob_possible(probability->prob)) {
        probability->unit_id = penemy->id;
        autoattack_prob_list_prepend(autoattack, probability);
      } else {
        FC_FREE(probability);
      }
    } unit_list_iterate_end;
  } adjc_iterate_end;

  /* Sort the potential attackers from highest to lowest success
   * probability. */
  if (autoattack_prob_list_size(autoattack) >= 2) {
    autoattack_prob_list_sort(autoattack, &compare_units);
  }

  autoattack_prob_list_iterate_safe(autoattack, peprob, penemy) {
    int sanity2 = penemy->id;
    struct tile *ptile = unit_tile(penemy);
    struct unit *enemy_defender = get_defender(nmap, punit, ptile, NULL);
    double punitwin, penemywin;
    double threshold = 0.25;
    struct tile *tgt_tile = unit_tile(punit);

    fc_assert(tgt_tile);

    if (tile_city(ptile) && unit_list_size(ptile->units) == 1) {
      /* Don't leave city defenseless */
      threshold = 0.90;
    }

    if (NULL != enemy_defender) {
      punitwin = unit_win_chance(nmap, punit, enemy_defender, NULL);
    } else {
      /* 'penemy' can attack 'punit' but it may be not reciproque. */
      punitwin = 1.0;
    }

    /* Previous attacks may have changed the odds. Recalculate. */
    peprob->prob =
        action_auto_perf_unit_prob(AAPC_UNIT_MOVED_ADJ,
                                   penemy, unit_owner(punit), NULL, NULL,
                                   tgt_tile, tile_city(tgt_tile),
                                   punit, NULL);

    if (!action_prob_possible(peprob->prob)) {
      /* No longer legal. */
      continue;
    }

    /* Assume the worst. */
    penemywin = action_prob_to_0_to_1_pessimist(peprob->prob);

    if ((penemywin > 1.0 - punitwin
         || unit_has_type_flag(punit, UTYF_PROVOKING))
        && penemywin > threshold) {

#ifdef REALLY_DEBUG_THIS
      log_test("AA %s -> %s (%d,%d) %.2f > %.2f && > %.2f",
               unit_rule_name(penemy), unit_rule_name(punit),
               TILE_XY(unit_tile(punit)), penemywin,
               1.0 - punitwin, threshold);
#endif

      unit_activity_handling(penemy, ACTIVITY_IDLE, ACTION_NONE);
      action_auto_perf_unit_do(AAPC_UNIT_MOVED_ADJ,
                               penemy, unit_owner(punit), NULL, NULL,
                               tgt_tile, tile_city(tgt_tile), punit, NULL);
    } else {
#ifdef REALLY_DEBUG_THIS
      log_test("!AA %s -> %s (%d,%d) %.2f > %.2f && > %.2f",
               unit_rule_name(penemy), unit_rule_name(punit),
               TILE_XY(unit_tile(punit)), penemywin,
               1.0 - punitwin, threshold);
#endif
      continue;
    }

    if (game_unit_by_number(sanity2)) {
      send_unit_info(NULL, penemy);
    }
    if (game_unit_by_number(sanity1)) {
      send_unit_info(NULL, punit);
    } else {
      autoattack_prob_list_destroy(autoattack);
      return FALSE; /* moving unit dead */
    }
  } autoattack_prob_list_iterate_safe_end;

  autoattack_prob_list_destroy(autoattack);
  if (game_unit_by_number(sanity1)) {
    /* We could have lost movement in combat */
    punit->moves_left = MIN(punit->moves_left, moves);
    send_unit_info(NULL, punit);
    return TRUE;
  } else {
    return FALSE;
  }
}

/**********************************************************************//**
  Cancel orders for the unit.
**************************************************************************/
static void cancel_orders(struct unit *punit, char *dbg_msg)
{
  free_unit_orders(punit);
  send_unit_info(NULL, punit);
  log_debug("%s", dbg_msg);
}

/**********************************************************************//**
  Will wake up any neighboring enemy sentry units or patrolling
  units.
**************************************************************************/
static void wakeup_neighbor_sentries(struct unit *punit)
{
  bool alone_in_city;

  if (NULL != tile_city(unit_tile(punit))) {
    int count = 0;

    unit_list_iterate(unit_tile(punit)->units, aunit) {
      /* Consider only units not transported. */
      if (!unit_transported(aunit)) {
        count++;
      }
    } unit_list_iterate_end;

    alone_in_city = (1 == count);
  } else {
    alone_in_city = FALSE;
  }

  /* There may be sentried units with a sightrange > 3, but we don't
     wake them up if the punit is farther away than 3. */
  square_iterate(&(wld.map), unit_tile(punit), 3, ptile) {
    unit_list_iterate(ptile->units, penemy) {
      int distance_sq = sq_map_distance(unit_tile(punit), ptile);
      int radius_sq = get_unit_vision_at(penemy, unit_tile(penemy), V_MAIN);

      if (!pplayers_allied(unit_owner(punit), unit_owner(penemy))
          && penemy->activity == ACTIVITY_SENTRY
          && radius_sq >= distance_sq
          /* If the unit moved on a city, and the unit is alone, consider
           * it is visible. */
          && (alone_in_city
              || can_player_see_unit(unit_owner(penemy), punit))
          /* on board transport; don't awaken */
          && can_unit_exist_at_tile(&(wld.map), penemy, unit_tile(penemy))) {
        set_unit_activity(penemy, ACTIVITY_IDLE, ACTION_NONE);
        send_unit_info(NULL, penemy);
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  /* Wakeup patrolling units we bump into.
     We do not wakeup units further away than 3 squares... */
  square_iterate(&(wld.map), unit_tile(punit), 3, ptile) {
    unit_list_iterate(ptile->units, ppatrol) {
      if (punit != ppatrol
	  && unit_has_orders(ppatrol)
	  && ppatrol->orders.vigilant) {
	if (maybe_cancel_patrol_due_to_enemy(ppatrol)) {
	  cancel_orders(ppatrol, "  stopping because of nearby enemy");
          notify_player(unit_owner(ppatrol), unit_tile(ppatrol),
                        E_UNIT_ORDERS, ftc_server,
                        _("Orders for %s aborted after enemy movement was "
                          "spotted."),
                        unit_link(ppatrol));
        }
      }
    } unit_list_iterate_end;
  } square_iterate_end;
}

/**********************************************************************//**
  Does: 1) updates the unit's homecity and the city it enters/leaves
           (the city's happiness varies). This also takes into account when
           the unit enters/leaves a fortress.
        2) updates adjacent cities' unavailable tiles.

  FIXME: Sometimes it is not necessary to send cities because
         the government doesn't care whether a unit is away or not.
**************************************************************************/
static bool unit_move_consequences(struct unit *punit,
                                   struct tile *src_tile,
                                   struct tile *dst_tile,
                                   bool passenger,
                                   bool conquer_city_allowed)
{
  struct city *fromcity = tile_city(src_tile);
  struct city *tocity = tile_city(dst_tile);
  struct city *homecity_start_pos = NULL;
  struct city *homecity_end_pos = NULL;
  int homecity_id_start_pos = punit->homecity;
  int homecity_id_end_pos = punit->homecity;
  struct player *pplayer_start_pos = unit_owner(punit);
  struct player *pplayer_end_pos = pplayer_start_pos;
  const struct unit_type *type_start_pos = unit_type_get(punit);
  const struct unit_type *type_end_pos = type_start_pos;
  bool refresh_homecity_start_pos = FALSE;
  bool refresh_homecity_end_pos = FALSE;
  int saved_id = punit->id;
  bool alive = TRUE;
  const struct civ_map *nmap = &(wld.map);

  if (tocity && conquer_city_allowed) {
    if (!passenger) {
      /* The unit that does the move may conquer. */
      unit_conquer_city(punit, tocity);
    }

    /* Run for passengers too. A passenger may have been killed when its
     * transport conquered a city. (unit_conquer_city() can cause Lua code
     * to run) */

    alive = unit_is_alive(saved_id);
    if (alive) {
      /* In case script has changed something about unit */
      pplayer_end_pos = unit_owner(punit);
      type_end_pos = unit_type_get(punit);
      homecity_id_end_pos = punit->homecity;
    }
  }

  if (homecity_id_start_pos != 0) {
    homecity_start_pos = game_city_by_number(homecity_id_start_pos);
  }
  if (homecity_id_start_pos != homecity_id_end_pos) {
    homecity_end_pos = game_city_by_number(homecity_id_end_pos);
  } else {
    homecity_end_pos = homecity_start_pos;
  }

  /* We only do refreshes for non-AI players to now make sure the AI turns
     doesn't take too long. Perhaps we should make a special refresh_city
     functions that only refreshed happines. */

  /* might have changed owners or may be destroyed */
  tocity = tile_city(dst_tile);

  if (tocity) { /* entering a city */
    if (tocity->owner == pplayer_end_pos) {
      if (tocity != homecity_end_pos && is_human(pplayer_end_pos)) {
        city_refresh(tocity);
        send_city_info(pplayer_end_pos, tocity);
      }
    }
    if (homecity_start_pos) {
      refresh_homecity_start_pos = TRUE;
    }
  }

  if (fromcity) { /* leaving a city */
    if (homecity_start_pos) {
      refresh_homecity_start_pos = TRUE;
    }
    if (fromcity != homecity_start_pos
        && fromcity->owner == pplayer_start_pos
        && is_human(pplayer_start_pos)) {
      city_refresh(fromcity);
      send_city_info(pplayer_start_pos, fromcity);
    }
  }

  /* Entering/leaving a fortress or friendly territory */
  if (homecity_start_pos || homecity_end_pos) {
    bool friendly_end = FALSE;

    if (game.info.happyborders != HB_DISABLED && tile_owner(src_tile) != tile_owner(dst_tile)) {
      friendly_end = TRUE;
    } else {
      int max_friendliness_range = tile_has_not_aggressive_extra_for_unit(dst_tile,
                                                                          type_end_pos);

      if (max_friendliness_range >= 0
          && is_friendly_city_near(nmap, pplayer_end_pos, dst_tile, max_friendliness_range)) {
        friendly_end = TRUE;
      } else {
        max_friendliness_range = tile_has_not_aggressive_extra_for_unit(src_tile,
                                                                        type_start_pos);

        if (max_friendliness_range >= 0
            && is_friendly_city_near(nmap, pplayer_start_pos, src_tile, max_friendliness_range)) {
          friendly_end = TRUE;
        }
      }
    }

    if (friendly_end) {
      refresh_homecity_start_pos = TRUE;
      refresh_homecity_end_pos = TRUE;
    }
  }

  if (refresh_homecity_start_pos && is_human(pplayer_start_pos)) {
    city_refresh(homecity_start_pos);
    send_city_info(pplayer_start_pos, homecity_start_pos);
  }
  if (refresh_homecity_end_pos
      && (!refresh_homecity_start_pos
          || homecity_start_pos != homecity_end_pos)
      && is_human(pplayer_end_pos)) {
    city_refresh(homecity_end_pos);
    send_city_info(pplayer_end_pos, homecity_end_pos);
  }

  city_map_update_tile_now(dst_tile);
  sync_cities();

  return alive;
}

/**********************************************************************//**
  Check if the units activity is legal for a move, and reset it if
  it isn't.
**************************************************************************/
static void check_unit_activity(struct unit *punit)
{
  switch (punit->activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_SENTRY:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_GOTO:
    break;
  case ACTIVITY_CLEAN:
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_PILLAGE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_BASE:
  case ACTIVITY_GEN_ROAD:
  case ACTIVITY_CONVERT:
  case ACTIVITY_LAST:
    set_unit_activity(punit, ACTIVITY_IDLE, ACTION_NONE);
    break;
  };
}

/**********************************************************************//**
  Create a new unit move data, or use previous one if available.
**************************************************************************/
static struct unit_move_data *unit_move_data_get(struct unit *punit,
                                                 const struct tile *psrctile,
                                                 const struct tile *pdesttile)
{
  struct unit_move_data *pdata;
  struct player *powner = unit_owner(punit);

  if (punit->server.moving) {
    /* Recursive moving (probably due to a script). */
    pdata = punit->server.moving;
    pdata->ref_count++;
    fc_assert_msg(pdata->punit == punit,
                  "Unit number %d (%p) was going to die, but "
                  "server attempts to move it.",
                  punit->id, punit);
    fc_assert_msg(pdata->old_vision == NULL,
                  "Unit number %d (%p) has done an incomplete move.",
                  punit->id, punit);
  } else {
    pdata = fc_malloc(sizeof(*pdata));
    pdata->ref_count = 1;
    pdata->punit = punit;
    punit->server.moving = pdata;
    BV_CLR_ALL(pdata->can_see_unit);
  }
  pdata->powner = powner;
  BV_CLR_ALL(pdata->can_see_move);
  pdata->old_vision = punit->server.vision;

  return pdata;
}

/**********************************************************************//**
  Move the unit using move_data.
**************************************************************************/
static void unit_move_by_data(struct unit_move_data *pdata,
                              const struct tile *psrctile,
                              struct tile *pdesttile)
{
  struct vision *new_vision;
  struct unit *punit = pdata->punit;
  int mod = unit_vision_range_modifiers(punit, pdesttile);
  const v_radius_t radius_sq
    = V_RADIUS(get_unit_vision_base(punit, V_MAIN, mod),
               get_unit_vision_base(punit, V_INVIS, mod),
               get_unit_vision_base(punit, V_SUBSURFACE, mod));

  /* Remove unit from the source tile. */
  fc_assert(unit_tile(punit) == psrctile);

#ifndef FREECIV_NDEBUG
  bool success =
#endif
    unit_list_remove(psrctile->units, punit);
  fc_assert(success);

  /* Set new tile. */
  unit_tile_set(punit, pdesttile);
  unit_list_prepend(pdesttile->units, punit);

  if (unit_transported(punit)) {
    /* Silently free orders since they won't be applicable anymore. */
    free_unit_orders(punit);
  }

  /* Check unit activity. */
  check_unit_activity(punit);
  unit_did_action(punit);
  unit_forget_last_activity(punit);

  /* We first unfog the destination, then send the move,
   * and then fog the old territory. This means that the player
   * gets a chance to see the newly explored territory while the
   * client moves the unit, and both areas are visible during the
   * move */

  /* Enhance vision if unit steps into a fortress */
  new_vision = vision_new(pdata->powner, pdesttile);
  punit->server.vision = new_vision;
  vision_change_sight(new_vision, radius_sq);
  ASSERT_VISION(new_vision);
}

/**********************************************************************//**
  Decrease the reference counter and destroy if needed.
**************************************************************************/
static void unit_move_data_unref(struct unit_move_data *pdata)
{
  fc_assert_ret(pdata != NULL);
  fc_assert_ret(pdata->ref_count > 0);
  fc_assert_msg(pdata->old_vision == NULL,
                "Unit number %d (%p) has done an incomplete move.",
                pdata->punit != NULL ? pdata->punit->id : -1, pdata->punit);

  pdata->ref_count--;
  if (pdata->ref_count == 0) {
    if (pdata->punit != NULL) {
      fc_assert(pdata->punit->server.moving == pdata);
      pdata->punit->server.moving = NULL;
    }
    free(pdata);
  }
}

/**********************************************************************//**
  Construct list of move datas of the unit and it its cargo.
**************************************************************************/
static struct unit_move_data_list *construct_move_data_list(struct unit *punit,
                                                            const struct tile *psrctile,
                                                            struct tile *pdesttile,
                                                            bool adj)
{
  struct unit_move_data_list *plist
    = unit_move_data_list_new_full(unit_move_data_unref);
  struct unit_move_data *pdata;

  /* Make new data for 'punit'. */
  pdata = unit_move_data_get(punit, psrctile, pdesttile);
  unit_move_data_list_prepend(plist, pdata);

  /* Add all contained units. */
  unit_cargo_iterate(punit, pcargo) {
    struct unit_move_data *cargo_data;

    cargo_data = unit_move_data_get(pcargo, psrctile, pdesttile);
    unit_move_data_list_append(plist, cargo_data);
  } unit_cargo_iterate_end;

  /* Determine the players able to see the move(s), now that the player
   * vision has been increased. */
  if (adj) {
    /* Main unit for adjacent move: the move is visible for every player
     * able to see on the matching unit layer. */
    enum vision_layer vlayer = unit_type_get(punit)->vlayer;

    players_iterate(oplayer) {
      if (map_is_known_and_seen(psrctile, oplayer, vlayer)
          || map_is_known_and_seen(pdesttile, oplayer, vlayer)) {
        BV_SET(pdata->can_see_unit, player_index(oplayer));
        BV_SET(pdata->can_see_move, player_index(oplayer));
      }
    } players_iterate_end;
  }

  unit_move_data_list_iterate(plist, pmove_data) {

    unit_move_by_data(pmove_data, psrctile, pdesttile);

    if (adj && pmove_data->punit == punit) {
      /* If positions are adjacent, we have already handled 'punit'. See
       * above. */
      continue;
    }

    players_iterate(oplayer) {
      if ((adj
           && can_player_see_unit_at(oplayer, pmove_data->punit, psrctile,
                                     pmove_data != pdata))
          || can_player_see_unit_at(oplayer, pmove_data->punit, pdesttile,
                                    pmove_data != pdata)) {
        BV_SET(pmove_data->can_see_unit, player_index(oplayer));
        BV_SET(pmove_data->can_see_move, player_index(oplayer));
      }
      if (can_player_see_unit_at(oplayer, pmove_data->punit, psrctile,
                                 pmove_data != pdata)) {
        /* The unit was seen with its source tile even if it was
         * teleported. */
        BV_SET(pmove_data->can_see_unit, player_index(oplayer));
      }
    } players_iterate_end;
  } unit_move_data_list_iterate_end;

  return plist;
}

/**********************************************************************//**
  Moves a unit. No checks whatsoever! This is meant as a practical
  function for other functions, like do_airline(), which do the checking
  either by themselves or by their callers

  If you move a unit you should always use this function, as it also sets
  the transport status of the unit correctly. Note that the source tile (the
  current tile of the unit) and pdesttile need not be adjacent.

  Returns TRUE iff unit still alive.
**************************************************************************/
bool unit_move(struct unit *punit, struct tile *pdesttile, int move_cost,
               struct unit *embark_to, bool find_embark_target,
               bool conquer_city_allowed, bool conquer_extras_allowed,
               bool enter_hut, bool frighten_hut)
{
  struct player *pplayer;
  struct tile *psrctile;
  struct city *psrccity;
  struct city *pdestcity;
  struct unit *ptransporter;
  struct packet_unit_info src_info, dest_info;
  struct packet_unit_short_info src_sinfo, dest_sinfo;
  struct unit_move_data_list *plist;
  struct unit_move_data *pdata;
  int saved_id;
  bool unit_lives;
  bool adj;
  enum direction8 facing;
  bool flagless = unit_has_type_flag(punit, UTYF_FLAGLESS);

  /* Some checks. */
  fc_assert_ret_val(punit != NULL, FALSE);
  fc_assert_ret_val(pdesttile != NULL, FALSE);

  pplayer = unit_owner(punit);
  saved_id = punit->id;
  psrctile = unit_tile(punit);
  adj = base_get_direction_for_step(&(wld.map), psrctile, pdesttile, &facing);

  conn_list_do_buffer(game.est_connections);

  /* Unload the unit if on a transport. */
  ptransporter = unit_transport_get(punit);
  if (ptransporter != NULL) {
    /* Unload unit _before_ setting the new tile! */
    unit_transport_unload(punit);
    /* Send updated information to anyone watching that transporter
     * was unloading cargo. */
    send_unit_info(NULL, ptransporter);
  }

  /* Wakup units next to us before we move. */
  wakeup_neighbor_sentries(punit);

  /* Make info packets at 'psrctile'. */
  if (adj) {
    /* If tiles are adjacent, we will show the move to users able
     * to see it. */
    package_unit(punit, &src_info);
    package_short_unit(punit, &src_sinfo, UNIT_INFO_IDENTITY, 0);
  }

  /* Set unit orientation */
  if (adj) {
    /* Only change orientation when moving to adjacent tile */
    punit->facing = facing;
  }

  plist = construct_move_data_list(punit, psrctile, pdesttile, adj);

  /* Move magic. */
  punit->moved = TRUE;
  punit->moves_left = MAX(0, punit->moves_left - move_cost);
  if (punit->moves_left == 0 && !unit_has_orders(punit)) {
    /* The next order may not require any remaining move fragments. */
    punit->done_moving = TRUE;
  }

  /* No longer relevant. */
  punit->action_decision_tile = NULL;
  punit->action_decision_want = ACT_DEC_NOTHING;

  /* Claim ownership of fortress? */
  if (conquer_extras_allowed
      && tile_has_claimable_base(pdesttile, unit_type_get(punit))) {
    /* Yes. We claim *all* bases if there's *any* claimable base(s).
     * Even if original unit cannot claim other kind of bases, the
     * first claimed base will have influence over other bases,
     * or something like that. */
    tile_claim_bases(pdesttile, pplayer);
  }

  pdata = unit_move_data_list_front(plist);
  fc_assert(pdata->punit == punit);

  /* Check timeout settings. */
  if (current_turn_timeout() != 0 && game.server.timeoutaddenemymove > 0) {
    bool new_information_for_enemy = FALSE;

    /* FIXME: Seen enemy cargo in a non-enemy transport should count too,
    *         if they are ever seen. */
    phase_players_iterate(penemy) {
      /* Increase the timeout if an enemy unit moves and the
       * timeoutaddenemymove setting is in use. */
      if (penemy->is_connected
          && pplayer != penemy
          && pplayers_at_war(pplayer, penemy)
          && BV_ISSET(pdata->can_see_move, player_index(penemy))) {
        new_information_for_enemy = TRUE;
        break;
      }
    } phase_players_iterate_end;

    if (new_information_for_enemy) {
      increase_timeout_because_unit_moved();
    }
  }

  if (!adj && action_tgt_city(punit, pdesttile, FALSE)) {
    /* The unit can perform an action to the city at the destination tile.
     * A long distance move (like an airlift) doesn't ask what action to
     * perform before moving. Ask now. */

    punit->action_decision_want = ACT_DEC_PASSIVE;
    punit->action_decision_tile = pdesttile;
  }

  /* Notifications of the move to the clients. */
  if (adj) {
    /* Special case: 'punit' is moving to adjacent position. Then we show
     * 'punit' move to all users able to see 'psrctile' or 'pdesttile'. */

    /* Make info packets at 'pdesttile'. */
    package_unit(punit, &dest_info);
    package_short_unit(punit, &dest_sinfo, UNIT_INFO_IDENTITY, 0);

    conn_list_iterate(game.est_connections, pconn) {
      struct player *aplayer = conn_get_player(pconn);

      if (aplayer == NULL) {
        if (pconn->observer) {
          /* Global observers see all... */
          send_packet_unit_info(pconn, &src_info);
          send_packet_unit_info(pconn, &dest_info);
        }
      } else if (BV_ISSET(pdata->can_see_move, player_index(aplayer))) {
        if (aplayer == pplayer) {
          send_packet_unit_info(pconn, &src_info);
          send_packet_unit_info(pconn, &dest_info);
        } else {
          send_packet_unit_short_info(pconn, &src_sinfo, FALSE);
          send_packet_unit_short_info(pconn, &dest_sinfo, FALSE);
        }
      }
    } conn_list_iterate_end;
  }

  /* Other moves. */
  unit_move_data_list_iterate(plist, pmove_data) {
    if (adj && pmove_data == pdata) {
      /* If positions are adjacent, we have already shown 'punit' move.
       * See above. */
      continue;
    }

    /* Make info packets at 'pdesttile'. */
    package_unit(pmove_data->punit, &dest_info);
    package_short_unit(pmove_data->punit, &dest_sinfo,
                       UNIT_INFO_IDENTITY, 0);

    conn_list_iterate(game.est_connections, pconn) {
      struct player *aplayer = conn_get_player(pconn);

      if (aplayer == NULL) {
        if (pconn->observer) {
          /* Global observers see all... */
          send_packet_unit_info(pconn, &dest_info);
        }
      } else if (BV_ISSET(pmove_data->can_see_move, player_index(aplayer))) {
        if (aplayer == pmove_data->powner) {
          send_packet_unit_info(pconn, &dest_info);
        } else {
          send_packet_unit_short_info(pconn, &dest_sinfo, FALSE);
        }
      }
    } conn_list_iterate_end;
  } unit_move_data_list_iterate_end;

  /* Clear old vision. */
  unit_move_data_list_iterate(plist, pmove_data) {
    if (pmove_data->old_vision != NULL) {
      vision_clear_sight(pmove_data->old_vision);
      vision_free(pmove_data->old_vision);
      pmove_data->old_vision = NULL;
    }
  } unit_move_data_list_iterate_end;

  /* Move consequences. */
  unit_move_data_list_iterate(plist, pmove_data) {
    struct unit *aunit = pmove_data->punit;

    if (aunit != NULL
        && unit_owner(aunit) == pmove_data->powner
        && unit_tile(aunit) == pdesttile) {
      (void) unit_move_consequences(aunit, psrctile, pdesttile,
                                    pdata != pmove_data,
                                    conquer_city_allowed);
    }
  } unit_move_data_list_iterate_end;

  unit_lives = (pdata->punit == punit);

  /* Wakeup units and make contact. */
  if (unit_lives) {
    wakeup_neighbor_sentries(punit);
  }

  /* Do not pass 'punit' to unit_make_contact(), as it
   * might refer to a dead unit.
   * Flagless units do not make contact. */
  if (!flagless) {
    unit_make_contact(nullptr, pdesttile, pplayer);
  }

  if (unit_lives) {
    /* Special checks for ground units in the ocean. */
    if (embark_to || !can_unit_survive_at_tile(&(wld.map), punit, pdesttile)) {
      if (embark_to != NULL) {
        ptransporter = embark_to;
      } else if (find_embark_target) {
        /* TODO: Consider to stop supporting find_embark_target and make all
         * callers that wants auto loading set embark_to. */
        ptransporter = transporter_for_unit(punit);
      } else {
        ptransporter = NULL;
      }
      if (ptransporter) {
        unit_transport_load_tp_status(punit, ptransporter, FALSE);

        /* Set activity to sentry if boarding a ship. */
        if (is_human(pplayer)
            && !unit_has_orders(punit)
            && punit->ssa_controller == SSA_NONE
            && !can_unit_exist_at_tile(&(wld.map), punit, pdesttile)) {
          set_unit_activity(punit, ACTIVITY_SENTRY, ACTION_NONE);
        }

        send_unit_info(NULL, punit);
      }
    }
  }

  /* Remove units going out of sight. */
  unit_move_data_list_iterate_rev(plist, pmove_data) {
    struct unit *aunit = pmove_data->punit;

    if (aunit == NULL) {
      continue; /* Died! */
    }

    players_iterate(aplayer) {
      if (BV_ISSET(pmove_data->can_see_unit, player_index(aplayer))
          && !can_player_see_unit(aplayer, aunit)) {
        unit_goes_out_of_sight(aplayer, aunit);
      }
    } players_iterate_end;
  } unit_move_data_list_iterate_rev_end;

  /* Inform the owner's client about actor unit arrival. Can, depending on
   * the client settings, cause the client to start the process that makes
   * the action selection dialog pop up. */
  pdestcity = tile_city(pdesttile);
  if (pdestcity != NULL) {
    /* Arrival in a city counts. */

    unit_move_data_list_iterate(plist, pmove_data) {
      struct unit *ptrans;
      bool ok;
      struct unit *act_unit;
      struct player *act_player;

      act_unit = pmove_data->punit;
      act_player = unit_owner(act_unit);

      if (act_unit == NULL
          || !unit_is_alive(act_unit->id)) {
        /* The unit died before reaching this point. */
        continue;
      }

      if (unit_tile(act_unit) != pdesttile) {
        /* The unit didn't arrive at the destination tile. */
        continue;
      }

      if (!is_human(act_player)) {
        /* Only humans need reminders. */
        continue;
      }

      if (!unit_transported(act_unit)) {
        /* Don't show the action selection dialog again. Non transported
         * units are handled before they move to the tile.  */
        continue;
      }

      /* Open action dialog only if 'act_unit' and all its transporters
       * (recursively) don't have orders. */
      if (unit_has_orders(act_unit)) {
        /* The unit it self has orders. */
        continue;
      }

      for (ptrans = unit_transport_get(act_unit);;
           ptrans = unit_transport_get(ptrans)) {
        if (NULL == ptrans) {
          /* No (recursive) transport has orders. */
          ok = TRUE;
          break;
        } else if (unit_has_orders(ptrans)) {
          /* A unit transporting the unit has orders */
          ok = FALSE;
          break;
        }
      }

      if (!ok) {
        /* A unit transporting act_unit has orders. */
        continue;
      }

      if (action_tgt_city(act_unit, pdesttile, FALSE)) {
        /* There is a valid target. */

        act_unit->action_decision_want = ACT_DEC_PASSIVE;
        act_unit->action_decision_tile = pdesttile;

        /* Let the client know that this unit wants the player to decide
         * what to do. */
        send_unit_info(player_reply_dest(act_player), act_unit);
      }
    } unit_move_data_list_iterate_end;
  }

  unit_move_data_list_destroy(plist);
  /* Check cities at source and destination. */
  psrccity = tile_city(psrctile);
  if (psrccity != NULL) {
    refresh_dumb_city(psrccity);
  }
  if (pdestcity != NULL) {
    refresh_dumb_city(pdestcity);
  }

  if (unit_lives) {
    /* Let the scripts run ... */
    script_server_signal_emit("unit_moved", punit, psrctile, pdesttile);
    unit_lives = unit_is_alive(saved_id);
  }

  if (unit_lives) {
    /* Autoattack. */
    unit_lives = unit_survive_autoattack(punit);
  }

  if (unit_lives && (enter_hut || frighten_hut)) {
    /* Is there a hut? */
    unit_enter_hut(punit, frighten_hut);
    fc_assert(!(enter_hut && frighten_hut));
    unit_lives = unit_is_alive(saved_id);
  }

  conn_list_do_unbuffer(game.est_connections);

  if (unit_lives) {
    CALL_FUNC_EACH_AI(unit_move_seen, punit);
  }

  return unit_lives;
}

/**********************************************************************//**
  Maybe cancel the goto if there is an enemy in the way
**************************************************************************/
static bool maybe_cancel_goto_due_to_enemy(struct unit *punit, 
                                           struct tile *ptile)
{
  struct player *owner = unit_owner(punit);

  return (is_non_allied_unit_tile(ptile, owner,
                                  unit_has_type_flag(punit, UTYF_FLAGLESS)) 
          || is_non_allied_city_tile(ptile, owner));
}

/**********************************************************************//**
  Maybe cancel the patrol as there is an enemy near.

  If you modify the wakeup range you should change it in
  wakeup_neighbor_sentries() too.
**************************************************************************/
static bool maybe_cancel_patrol_due_to_enemy(struct unit *punit)
{
  bool cancel = FALSE;
  int radius_sq = get_unit_vision_at(punit, unit_tile(punit), V_MAIN);
  struct player *pplayer = unit_owner(punit);

  circle_iterate(&(wld.map), unit_tile(punit), radius_sq, ptile) {
    struct unit *penemy = tile_non_allied_unit(ptile, pplayer,
                                               unit_has_type_flag(punit, UTYF_FLAGLESS));
    struct vision_site *pdcity = map_get_player_site(ptile, pplayer);

    if ((penemy && can_player_see_unit(pplayer, penemy))
        || (pdcity && !pplayers_allied(pplayer, vision_site_owner(pdcity))
            && pdcity->occupied)) {
      cancel = TRUE;
      break;
    }
  } circle_iterate_end;

  return cancel;
}

/**********************************************************************//**
  Returns TRUE iff it is reasonable to assume that the player is wathing
  the unit.

  Since the player is watching the unit there is no need to inform them
  about things they could see happening. Remember that it still may
  be necessary to explain why something happened.
**************************************************************************/
static inline bool player_is_watching(struct unit *punit, const bool fresh)
{
  /* The player just sent the orders to the unit. The unit has moves left.
   * It is therefore safe to assume that the player already is paying
   * attention to the unit. */
  return fresh && punit->moves_left > 0;
}

/**********************************************************************//**
  Executes a unit's orders stored in punit->orders. The unit is put on idle
  if an action fails or if "patrol" is set and an enemy unit is encountered.

  The return value will be TRUE if the unit lives, FALSE otherwise.
  (This function used to return a goto_result enumeration, declared
  in gotohand.h. But this enumeration was never checked by the caller and
  just lead to confusion. All the caller really needs to know is if the unit
  lived or died; everything else is handled internally within execute_orders.)

  If the orders are repeating the loop starts over at the beginning once it
  completes. To avoid infinite loops on railroad we stop for this
  turn when the unit is back where it started, even if it have moves left.

  A unit will attack under orders only on its final action.

  The fresh parameter is true if the order execution happens because the
  orders just were received.
**************************************************************************/
bool execute_orders(struct unit *punit, const bool fresh)
{
  struct act_prob prob;
  bool performed;
  const char *name;
  bool res, last_order;
  int unitid = punit->id;
  struct player *pplayer = unit_owner(punit);
  int moves_made = 0;
  const struct civ_map *nmap = &(wld.map);

  fc_assert_ret_val(unit_has_orders(punit), TRUE);

  if (punit->activity != ACTIVITY_IDLE) {
    /* Unit's in the middle of an activity; wait for it to finish. */
    punit->done_moving = TRUE;
    return TRUE;
  }

  log_debug("Executing orders for %s %d", unit_rule_name(punit), punit->id);

  /* Any time the orders are canceled we should give the player a message. */

  while (TRUE) {
    struct unit_order order;

    struct action *oaction;

    struct tile *dst_tile;
    struct city *tgt_city;
    struct unit *tgt_unit;
    int tgt_id;
    int sub_tgt_id;
    struct extra_type *pextra;

    if (punit->done_moving) {
      log_debug("  stopping because we're done this turn");
      return TRUE;
    }

    if (punit->orders.vigilant && maybe_cancel_patrol_due_to_enemy(punit)) {
      /* "Patrol" orders are stopped if an enemy is near. */
      cancel_orders(punit, "  stopping because of nearby enemy");
      notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                    _("Orders for %s aborted as there are units nearby."),
                    unit_link(punit));
      return TRUE;
    }

    if (moves_made == punit->orders.length) {
      /* For repeating orders, don't repeat more than once per turn. */
      log_debug("  stopping because we ran a round");
      punit->done_moving = TRUE;
      send_unit_info(NULL, punit);
      return TRUE;
    }
    moves_made++;

    order = punit->orders.list[punit->orders.index];

    /* An ORDER_PERFORM_ACTION that doesn't specify an action should not get
     * this far. */
    fc_assert_action((order.order != ORDER_PERFORM_ACTION
                      || action_id_exists(order.action)),
                     continue);

    switch (order.order) {
    case ORDER_MOVE:
    case ORDER_ACTION_MOVE:
    case ORDER_FULL_MP:
      if (0 == punit->moves_left) {
        log_debug("  stopping because of no more move points");
        return TRUE;
      }
      break;
    case ORDER_PERFORM_ACTION:
      if (action_mp_full_makes_legal(punit, order.action)) {
        log_debug("  stopping. Not enough move points this turn");
        return TRUE;
      }
      break;
    case ORDER_ACTIVITY:
    case ORDER_LAST:
      /* Those actions don't require moves left. */
      break;
    }

    last_order = (!punit->orders.repeat
		  && punit->orders.index + 1 == punit->orders.length);

    if (last_order) {
      /* Clear the orders before we engage in the move. That way any
       * has_orders checks will yield FALSE and this will be treated as
       * a normal move. This is important: for instance a caravan goto
       * will popup the caravan dialog on the last move only. */
      free_unit_orders(punit);
    }

    /* Advance the orders one step forward. This is needed because any
     * updates sent to the client as a result of the action should include
     * the new index value. Note that we have to send_unit_info() somewhere
     * after this point so that the client is properly updated. */
    punit->orders.index++;

    switch (order.order) {
    case ORDER_FULL_MP:
      if (punit->moves_left < unit_move_rate(punit)) {
	/* If the unit doesn't have full MP then it just waits until the
	 * next turn.  We assume that the next turn it will have full MP
	 * (there's no check for that). */
	punit->done_moving = TRUE;
        log_debug("  waiting this turn");
	send_unit_info(NULL, punit);
      }
      break;
    case ORDER_ACTIVITY:
      {
        enum unit_activity activity = order.activity;

        fc_assert(activity == ACTIVITY_SENTRY);

        if (can_unit_do_activity(nmap, punit, activity, order.action)) {
          punit->done_moving = TRUE;
          set_unit_activity(punit, activity, order.action);
          send_unit_info(NULL, punit);

          break;
        }
      }

      cancel_orders(punit, "  orders canceled because of failed activity");
      notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                    _("Orders for %s aborted since they "
                      "give an invalid activity."),
                    unit_link(punit));
      return TRUE;
    case ORDER_MOVE:
    case ORDER_ACTION_MOVE:
      /* Move unit */
      if (!(dst_tile = mapstep(&(wld.map), unit_tile(punit), order.dir))) {
        cancel_orders(punit, "  move order sent us to invalid location");
        notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                      _("Orders for %s aborted since they "
                        "give an invalid location."),
                      unit_link(punit));
        return TRUE;
      }

      if (order.order != ORDER_ACTION_MOVE
          && maybe_cancel_goto_due_to_enemy(punit, dst_tile)) {
        /* Plain move required: no attack, trade route etc. */
        cancel_orders(punit, "  orders canceled because of enemy");
        notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                      _("Orders for %s aborted as there "
                        "are units in the way."),
                      unit_link(punit));
        return TRUE;
      }

      log_debug("  moving to %d,%d", TILE_XY(dst_tile));
      res = unit_move_handling(punit, dst_tile,
                               order.order != ORDER_ACTION_MOVE);
      if (!player_unit_by_number(pplayer, unitid)) {
        log_debug("  unit died while moving.");
        /* A player notification should already have been sent. */
        return FALSE;
      }

      if (res && !same_pos(dst_tile, unit_tile(punit))) {
        /* Movement succeeded but unit didn't move. */
        log_debug("  orders resulted in combat.");
        send_unit_info(NULL, punit);
        return TRUE;
      }

      if (!res) {
        fc_assert(0 <= punit->moves_left);

        /* Movement failed (ZOC, etc.) */
        cancel_orders(punit, "  attempt to move failed.");

        if (!player_is_watching(punit, fresh)
            /* The final move "failed" because the unit needs to ask the
             * player what action it should take.
             *
             * The action decision request notifies the player. Its
             * location at the unit's last order makes it clear to the
             * player who the decision is for. ("The Spy I sent to Berlin
             * has arrived.")
             *
             * A notification message is therefore redundant. */
            && !(last_order
                 && punit->action_decision_want == ACT_DEC_ACTIVE
                 && punit->action_decision_tile == dst_tile)) {
          /* The player may have missed this. No one else will announce it
           * in a satisfying manner. Inform the player. */
          notify_player(pplayer, unit_tile(punit),
                        E_UNIT_ORDERS, ftc_server,
                        _("Orders for %s aborted because of failed move."),
                        unit_link(punit));
        }

        return TRUE;
      }
      break;
    case ORDER_PERFORM_ACTION:
      oaction = action_by_number(order.action);

      /* Checked in unit_order_list_is_sane() */
      fc_assert_action(oaction != NULL, continue);

      log_debug("  orders: doing action %s", action_rule_name(oaction));

      dst_tile = index_to_tile(&(wld.map), order.target);

      if (dst_tile == NULL) {
        /* Could be at the edge of the map while trying to target a tile
         * outside of it. */

        cancel_orders(punit, "  target location doesn't exist");
        illegal_action_msg(unit_owner(punit), E_UNIT_ORDERS, punit,
                           order.action, dst_tile, NULL, NULL);

        return TRUE;
      }

      /* Get the target city from the target tile. */
      tgt_city = tile_city(dst_tile);

      if (tgt_city == NULL
          && action_id_get_target_kind(order.action) == ATK_CITY) {
        /* This action targets a city but no city target was found. */

        cancel_orders(punit, "  perform action vs city with no city");
        illegal_action_msg(unit_owner(punit), E_UNIT_ORDERS, punit,
                           order.action, dst_tile, tgt_city, NULL);

        return TRUE;
      }

      /* Get a target unit at the target tile. */
      tgt_unit = action_tgt_unit(punit, dst_tile, TRUE);

      if (tgt_unit == NULL
          && action_id_get_target_kind(order.action) == ATK_UNIT) {
        /* This action targets a unit but no target unit was found. */

        cancel_orders(punit, "  perform action vs unit with no unit");
        illegal_action_msg(unit_owner(punit), E_UNIT_ORDERS, punit,
                           order.action, dst_tile, tgt_city, tgt_unit);

        return TRUE;
      }

      /* Server side sub target assignment */
      if (oaction->target_complexity == ACT_TGT_COMPL_FLEXIBLE
          && order.sub_target == NO_TARGET) {
        /* Try to find a sub target. */
        sub_tgt_id = action_sub_target_id_for_action(oaction, punit);
      } else {
        /* The client should have specified a sub target if needed */
        sub_tgt_id = order.sub_target;
      }

      /* Get a target extra at the target tile */
      pextra = (sub_tgt_id == NO_TARGET ?
                  NULL :
                  extra_by_number(sub_tgt_id));

      if (action_get_sub_target_kind(oaction) == ASTK_EXTRA_NOT_THERE
          && pextra != NULL
          && actres_creates_extra(oaction->result, pextra)
          && tile_has_extra(dst_tile, pextra)) {
        /* Already there. Move on to the next order. */
        break;
      }

      if (action_get_sub_target_kind(oaction) == ASTK_EXTRA
          && pextra != NULL
          && actres_removes_extra(oaction->result, pextra)
          && !tile_has_extra(dst_tile, pextra)) {
        /* Already not there. Move on to the next order. */
        break;
      }

      /* No target selected. */
      tgt_id = -1;

      /* Assume impossible until told otherwise. */
      prob = ACTPROB_IMPOSSIBLE;

      switch (action_id_get_target_kind(order.action)) {
      case ATK_STACK:
        prob = action_prob_vs_stack(nmap, punit, order.action,
                                    dst_tile);
        tgt_id = dst_tile->index;
        break;
      case ATK_TILE:
        prob = action_prob_vs_tile(nmap, punit, order.action,
                                   dst_tile, pextra);
        tgt_id = dst_tile->index;
        break;
      case ATK_EXTRAS:
        prob = action_prob_vs_extras(nmap, punit, order.action,
                                     dst_tile, pextra);
        tgt_id = dst_tile->index;
        break;
      case ATK_CITY:
        prob = action_prob_vs_city(nmap, punit, order.action,
                                   tgt_city);
        tgt_id = tgt_city->id;
        break;
      case ATK_UNIT:
        prob = action_prob_vs_unit(nmap, punit, order.action,
                                   tgt_unit);

        tgt_id = tgt_unit->id;
        break;
      case ATK_SELF:
        prob = action_prob_self(nmap, punit, order.action);

        tgt_id = unitid;
        break;
      case ATK_COUNT:
        log_error("Invalid action target kind");

        /* The check below will abort and cancel the orders because prob
         * was initialized to impossible above this switch statement. */

        break;
      }

      if (!action_prob_possible(prob)) {
        /* The player has enough information to know that this action is
         * against the rules. Don't risk any punishment by trying to
         * perform it. */

        cancel_orders(punit, "  illegal action");
        notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                      _("%s could not do %s to %s."),
                      unit_link(punit),
                      action_id_name_translation(order.action),
                      tile_link(dst_tile));

        /* Try to explain what rule made it illegal. */
        illegal_action_msg(unit_owner(punit), E_BAD_COMMAND, punit,
                           order.action, dst_tile, tgt_city, tgt_unit);

        return TRUE;
      }

      if (action_id_has_result_safe(order.action, ACTRES_FOUND_CITY)) {
        /* This action needs a name. */
        name = city_name_suggestion(pplayer, unit_tile(punit));
      } else {
        /* This action doesn't need a name. */
        name = "";
      }

      performed = unit_perform_action(pplayer,
                                      unitid,
                                      tgt_id,
                                      sub_tgt_id,
                                      name,
                                      order.action,
                                      ACT_REQ_PLAYER);

      if (!player_unit_by_number(pplayer, unitid)) {
        /* The unit "died" while performing the action. */
        return FALSE;
      }

      if (!performed) {
        /* The action wasn't performed as ordered. */

        cancel_orders(punit, "  failed action");
        notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                      _("Orders for %s aborted because "
                        "doing %s to %s failed."),
                      unit_link(punit),
                      action_id_name_translation(order.action),
                      tile_link(dst_tile));

        return TRUE;
      }

      if (action_id_get_act_time(order.action, punit, dst_tile, pextra)
          != ACT_TIME_INSTANTANEOUS) {
        /* Done at turn change. */
        punit->done_moving = TRUE;
        send_unit_info(NULL, punit);
        break;
      }

      break;
    case ORDER_LAST:
      /* Should be caught when loading the unit orders from the savegame or
       * when receiving the unit orders from the client. */
      fc_assert_msg(order.order != ORDER_LAST, "Invalid order: last.");
      cancel_orders(punit, "  invalid order!");
      notify_player(pplayer, unit_tile(punit), E_UNIT_ORDERS, ftc_server,
                    _("Your %s has invalid orders."),
                    unit_link(punit));
      return TRUE;
    }

    if (last_order) {
      fc_assert(!punit->has_orders);
      log_debug("  stopping because orders are complete");
      return TRUE;
    }

    if (punit->orders.index == punit->orders.length) {
      fc_assert(punit->orders.repeat);
      /* Start over. */
      log_debug("  repeating orders.");
      punit->orders.index = 0;
    }
  } /* end while */
}

/**********************************************************************//**
  Return the base vision range the unit will have at the given tile.
  Modifier effects are not considered here.

  Note that vision MUST be independent of transported_by for this to work
  properly.
**************************************************************************/
static int get_unit_vision_base(const struct unit *punit,
                                enum vision_layer vlayer,
                                const int base)
{
  switch (vlayer) {
  case V_MAIN:
    return MAX(0, base);
  case V_INVIS:
  case V_SUBSURFACE:
    return CLIP(0, base, 2);
  case V_COUNT:
    break;
  }

  log_error("Unsupported vision layer variant: %d.", vlayer);

  return 0;
}

/**********************************************************************//**
  Return unit vision range modifiers unit would have at the given tile.
**************************************************************************/
static int unit_vision_range_modifiers(const struct unit *punit,
                                       const struct tile *ptile)
{
  const struct unit_type *utype = unit_type_get(punit);

  return (utype->vision_radius_sq
          + get_target_bonus_effects(NULL,
                                     &(const struct req_context) {
                                       .player = unit_owner(punit),
                                       .tile = ptile,
                                       .unittype = utype,
                                       .unit = punit,
                                     },
                                     NULL, EFT_UNIT_VISION_RADIUS_SQ));
}

/**********************************************************************//**
  Return the vision the unit will have at the given tile. The base vision
  range may be modified by effects.

  Note that vision MUST be independent of transported_by for this to work
  properly.
**************************************************************************/
int get_unit_vision_at(const struct unit *punit, const struct tile *ptile,
                       enum vision_layer vlayer)
{
  return get_unit_vision_base(punit, vlayer,
                              unit_vision_range_modifiers(punit, ptile));
}

/**********************************************************************//**
  Refresh the unit's vision.

  This function has very small overhead and can be called any time effects
  may have changed the vision range of the unit.
**************************************************************************/
void unit_refresh_vision(struct unit *punit)
{
  struct vision *uvision = punit->server.vision;
  const struct tile *utile = unit_tile(punit);
  int mod = unit_vision_range_modifiers(punit, utile);
  const v_radius_t radius_sq
    = V_RADIUS(get_unit_vision_base(punit, V_MAIN, mod),
               get_unit_vision_base(punit, V_INVIS, mod),
               get_unit_vision_base(punit, V_SUBSURFACE, mod));

  vision_change_sight(uvision, radius_sq);
  ASSERT_VISION(uvision);
}

/**********************************************************************//**
  Refresh the vision of all units in the list - see unit_refresh_vision().
**************************************************************************/
void unit_list_refresh_vision(struct unit_list *punitlist)
{
  unit_list_iterate(punitlist, punit) {
    unit_refresh_vision(punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Used to implement the game rule controlled by the unitwaittime setting.
  Notifies the unit owner if the unit is unable to act.
**************************************************************************/
bool unit_can_do_action_now(const struct unit *punit)
{
  time_t dt;

  if (!punit) {
    return FALSE;
  }

  if (game.server.unitwaittime <= 0) {
    return TRUE;
  }

  if (punit->server.action_turn != game.info.turn - 1) {
    return TRUE;
  }

  dt = time(NULL) - punit->server.action_timestamp;
  if (dt < game.server.unitwaittime) {
    char buf[64];
    format_time_duration(game.server.unitwaittime - dt, buf, sizeof(buf));
    notify_player(unit_owner(punit), unit_tile(punit), E_BAD_COMMAND,
                  ftc_server, _("Your unit may not act for another %s "
                                "this turn. See /help unitwaittime."), buf);
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Mark a unit as having done something at the current time. This is used
  in conjunction with unit_can_do_action_now() and the unitwaittime setting.
**************************************************************************/
void unit_did_action(struct unit *punit)
{
  if (!punit) {
    return;
  }

  punit->server.action_timestamp = time(NULL);
  punit->server.action_turn = game.info.turn;
}

/**********************************************************************//**
  Units (usually barbarian units) may disband spontaneously if they are
  far from any enemy units or cities. It is to remove barbarians that do
  not engage into any activity for a long time.
**************************************************************************/
bool unit_can_be_retired(struct unit *punit)
{
  /* check if there is enemy nearby */
  square_iterate(&(wld.map), unit_tile(punit), 3, ptile) {
    if (is_enemy_city_tile(ptile, unit_owner(punit))
        || is_enemy_unit_tile(ptile, unit_owner(punit))) {
      return FALSE;
    }
  }
  square_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Make random movements of the units that move that way.
**************************************************************************/
void random_movements(struct player *pplayer)
{
  const struct civ_map *nmap = &(wld.map);

  game.server.random_move_time = pplayer;

  unit_list_iterate_safe(pplayer->units, punit) {
    if (unit_has_type_flag(punit, UTYF_RANDOM_MOVEMENT)) {
      bool moved = TRUE;
      struct tile *curtile = unit_tile(punit);
      int id = punit->id;

      while (moved && unit_is_alive(id) && punit->moves_left > 0) {
        /*
         * List of all 8 directions
         */
        enum direction8 dirs[8] = {
          DIR8_NORTHWEST, DIR8_NORTH, DIR8_NORTHEAST, DIR8_WEST, DIR8_EAST,
          DIR8_SOUTHWEST, DIR8_SOUTH, DIR8_SOUTHEAST
        };
        int n;

        moved = FALSE;

        for (n = 8; n > 0 && !moved; n--) {
          enum direction8 choice = (enum direction8) fc_rand(n);
          struct tile *dest = mapstep(&(wld.map), curtile, dirs[choice]);

          if (dest != NULL) {
            if (action_prob_possible(action_prob_vs_stack(nmap, punit, ACTION_ATTACK,
                                                          dest))) {
              if (unit_perform_action(pplayer, id, tile_index(dest), NO_TARGET,
                                      "", ACTION_ATTACK, ACT_REQ_RULES)) {
                moved = TRUE;
                break;
              }
            }

            if (!moved) {
              if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE, punit, dest,
                                                 NULL)) {
                if (unit_perform_action(pplayer, id, tile_index(dest),
                                        NO_TARGET, "", ACTION_UNIT_MOVE,
                                        ACT_REQ_RULES)) {
                  moved = TRUE;
                }
              } else if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE2,
                                                        punit, dest, NULL)) {
                if (unit_perform_action(pplayer, id, tile_index(dest),
                                        NO_TARGET, "", ACTION_UNIT_MOVE2,
                                        ACT_REQ_RULES)) {
                  moved = TRUE;
                }
              } else if (is_action_enabled_unit_on_tile(nmap, ACTION_UNIT_MOVE3,
                                                        punit, dest, NULL)) {
                if (unit_perform_action(pplayer, id, tile_index(dest),
                                        NO_TARGET, "", ACTION_UNIT_MOVE3,
                                        ACT_REQ_RULES)) {
                  moved = TRUE;
                }
              }
            }
          }

          if (!moved) {
            /* Choice was bad, so replace it with the last direction
             * in the list. On the next iteration, one fewer choice
             * will remain. */
            dirs[choice] = dirs[n - 1];
          }
        }
      }
    }
  } unit_list_iterate_safe_end;

  game.server.random_move_time = NULL;
}

/**********************************************************************//**
  Make contact between a player and everyone adjacent to a tile via a unit
  moving to that tile. Tile and player default to the unit's location and
  owner if nullptr, but they may be different.
**************************************************************************/
void unit_make_contact(const struct unit *punit,
                       struct tile *ptile, struct player *pplayer)
{
  fc_assert_ret(punit != nullptr
                || (ptile != nullptr && pplayer != nullptr));

  if (punit != nullptr && unit_has_type_flag(punit, UTYF_FLAGLESS)) {
    return; /* Flagless unit can't make contact */
  }

  maybe_make_contact(ptile != nullptr ? ptile : unit_tile(punit),
                     pplayer != nullptr ? pplayer : unit_owner(punit));
}
