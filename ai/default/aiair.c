/***********************************************************************
 Freeciv - Copyright (C) 2002 - The Freeciv Team
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

/* utility */
#include "log.h"

/* common */
#include "combat.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "player.h"
#include "pf_tools.h"
#include "unit.h"

/* server */
#include "citytools.h"
#include "maphand.h"
#include "srv_log.h"
#include "unithand.h"
#include "unittools.h"

/* server/advisors */
#include "advbuilding.h"
#include "advgoto.h"

/* ai */
#include "handicaps.h"

/* ai/default */
#include "aiplayer.h"
#include "ailog.h"
#include "aitools.h"
#include "aiunit.h"
#include "daicity.h"

#include "aiair.h"

/******************************************************************//**
  Looks for nearest airbase for punit reachable imediatly.
  Returns NULL if not found.  The path is stored in the path
  argument if not NULL.
  TODO: Special handicaps for planes running out of fuel
        IMO should be less restrictive than general H_MAP, H_FOG
**********************************************************************/
static struct tile *find_nearest_airbase(const struct unit *punit,
                                         struct pf_path **path)
{
  struct player *pplayer = unit_owner(punit);
  struct pf_parameter parameter;
  struct pf_map *pfm;

  pft_fill_unit_parameter(&parameter, punit);
  parameter.omniscience = !has_handicap(pplayer, H_MAP);
  pfm = pf_map_new(&parameter);

  pf_map_move_costs_iterate(pfm, ptile, move_cost, TRUE) {
    if (move_cost > punit->moves_left) {
      /* Too far! */
      break;
    }

    if (is_airunit_refuel_point(ptile, pplayer, punit)) {
      if (path) {
        *path = pf_map_path(pfm, ptile);
      }
      pf_map_destroy(pfm);
      return ptile;
    }
  } pf_map_move_costs_iterate_end;

  pf_map_destroy(pfm);
  return NULL;
}

/******************************************************************//**
  Very preliminary estimate for our intent to attack the tile (x, y).
  Used by bombers only.
**********************************************************************/
static bool dai_should_we_air_attack_tile(struct ai_type *ait,
                                          struct unit *punit, struct tile *ptile)
{
  struct city *acity = tile_city(ptile);

  /* For a virtual unit (punit->id == 0), all targets are good */
  /* TODO: There is a danger of producing too many units that will not 
   * attack anything.  Production should not happen if there is an idle 
   * unit of the same type nearby */
  if (acity && punit->id != 0
      && def_ai_city_data(acity, ait)->invasion.occupy == 0
      && !unit_can_take_over(punit)) {
    /* No units capable of occupying are invading */
    log_debug("Don't want to attack %s, although we could",
              city_name_get(acity));
    return FALSE;
  }

  return TRUE;
}

/******************************************************************//**
  Returns an estimate for the profit gained through attack.
  Assumes that the victim is within one day's flight
**********************************************************************/
static int dai_evaluate_tile_for_air_attack(struct unit *punit,
                                            struct tile *dst_tile)
{
  struct unit *pdefender;
  /* unit costs in shields */
  int balanced_cost, unit_cost, victim_cost = 0;
  /* unit stats */
  int unit_attack, victim_defence;
  /* final answer */
  int profit;
  /* time spent in the air */
  int sortie_time;
#define PROB_MULTIPLIER 100 /* should unify with those in combat.c */

  if (!can_unit_attack_tile(punit, dst_tile)
      || !(pdefender = get_defender(punit, dst_tile))) {
    return 0;
  }

  /* Ok, we can attack, but is it worth it? */

  /* Cost of our unit */
  unit_cost = unit_build_shield_cost_base(punit);
  /* This is to say "wait, ill unit will get better!" */
  unit_cost = unit_cost * unit_type_get(punit)->hp / punit->hp; 

  /* Determine cost of enemy units */
  victim_cost = stack_cost(punit, pdefender);
  if (0 == victim_cost) {
    return 0;
  }

  /* Missile would die 100% so we adjust the victim_cost -- GB */
  if (utype_can_do_action(unit_type_get(punit), ACTION_SUICIDE_ATTACK)) {
    /* Assume that the attack will be a suicide attack even if a regular
     * attack may be legal. */
    victim_cost -= unit_build_shield_cost_base(punit);
  }

  unit_attack = (int) (PROB_MULTIPLIER
                       * unit_win_chance(punit, pdefender));

  victim_defence = PROB_MULTIPLIER - unit_attack;

  balanced_cost = build_cost_balanced(unit_type_get(punit));

  sortie_time = (unit_has_type_flag(punit, UTYF_ONEATTACK) ? 1 : 0);

  profit = kill_desire(victim_cost, unit_attack, unit_cost, victim_defence, 1) 
    - SHIELD_WEIGHTING + 2 * TRADE_WEIGHTING;
  if (profit > 0) {
    profit = military_amortize(unit_owner(punit), 
                               game_city_by_number(punit->homecity),
                               profit, sortie_time, balanced_cost);
    log_debug("%s at (%d, %d) is a worthy target with profit %d", 
              unit_rule_name(pdefender), TILE_XY(dst_tile), profit);
  } else {
    log_debug("%s(%d, %d): %s at (%d, %d) is unworthy with profit %d",
              unit_rule_name(punit), TILE_XY(unit_tile(punit)),
              unit_rule_name(pdefender), TILE_XY(dst_tile), profit);
    profit = 0;
  }

  return profit;
}
  

/******************************************************************//**
  Find something to bomb
  Air-units specific victim search
  Returns the want for the best target.  The targets are stored in the
  path and pptile arguments if not NULL.
  TODO: take counterattack dangers into account
  TODO: make separate handicaps for air units seeing targets
        IMO should be more restrictive than general H_MAP, H_FOG
**********************************************************************/
static int find_something_to_bomb(struct ai_type *ait, struct unit *punit,
                                  struct pf_path **path, struct tile **pptile)
{
  struct player *pplayer = unit_owner(punit);
  struct pf_parameter parameter;
  struct pf_map *pfm;
  struct tile *best_tile = NULL;
  int best = 0;

  pft_fill_unit_parameter(&parameter, punit);
  parameter.omniscience = !has_handicap(pplayer, H_MAP);
  pfm = pf_map_new(&parameter);

  /* Let's find something to bomb */
  pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
    if (move_cost >= punit->moves_left) {
      /* Too far! */
      break;
    }

    if (has_handicap(pplayer, H_MAP) && !map_is_known(ptile, pplayer)) {
      /* The target tile is unknown */
      continue;
    }

    if (has_handicap(pplayer, H_FOG) 
        && !map_is_known_and_seen(ptile, pplayer, V_MAIN)) {
      /* The tile is fogged */
      continue;
    }

    if (is_enemy_unit_tile(ptile, pplayer)
        && dai_should_we_air_attack_tile(ait, punit, ptile)
        && can_unit_attack_tile(punit, ptile)) {
      int new_best = dai_evaluate_tile_for_air_attack(punit, ptile);

      if (new_best > best) {
        best_tile = ptile;
        best = new_best;
        log_debug("%s wants to attack tile (%d, %d)", 
                  unit_rule_name(punit), TILE_XY(ptile));
      }
    }
  } pf_map_move_costs_iterate_end;

  /* Return the best values. */
  if (pptile) {
    *pptile = best_tile;
  }
  if (path) {
    *path = best_tile ? pf_map_path(pfm, best_tile) : NULL;
  }

  pf_map_destroy(pfm);
  return best;
} 

/******************************************************************//**
  Iterates through reachable cities and appraises them as a possible 
  base for air operations by (air)unit punit.  Returns NULL if not
  found.  The path is stored in the path argument if not NULL.
**********************************************************************/
static struct tile *dai_find_strategic_airbase(struct ai_type *ait,
                                               const struct unit *punit,
                                               struct pf_path **path)
{
  struct player *pplayer = unit_owner(punit);
  struct pf_parameter parameter;
  struct pf_map *pfm;
  struct tile *best_tile = NULL;
  struct city *pcity;
  struct unit *pvirtual = NULL;
  int best_worth = 0, target_worth;

  pft_fill_unit_parameter(&parameter, punit);
  parameter.omniscience = !has_handicap(pplayer, H_MAP);
  pfm = pf_map_new(&parameter);
  pf_map_move_costs_iterate(pfm, ptile, move_cost, FALSE) {
    if (move_cost >= punit->moves_left) {
      break; /* Too far! */
    }

    if (!is_airunit_refuel_point(ptile, pplayer, punit)) {
      continue; /* Cannot refuel here. */
    }

    if ((pcity = tile_city(ptile))
        && def_ai_city_data(pcity, ait)->grave_danger != 0) {
      best_tile = ptile;
      break; /* Fly there immediately!! */
    }

    if (!pvirtual) {
      pvirtual =
        unit_virtual_create(pplayer,
                            player_city_by_number(pplayer, punit->homecity),
                            unit_type_get(punit), punit->veteran);
    }

    unit_tile_set(pvirtual, ptile);
    target_worth = find_something_to_bomb(ait, pvirtual, NULL, NULL);
    if (target_worth > best_worth) {
      /* It's either a first find or it's better than the previous. */
      best_worth = target_worth;
      best_tile = ptile;
      /* We can still look for something better. */
    }
  } pf_map_move_costs_iterate_end;

  if (pvirtual) {
    unit_virtual_destroy(pvirtual);
  }

  if (path) {
    /* Stores the path. */
    *path = best_tile ? pf_map_path(pfm, best_tile) : NULL;
  }
  pf_map_destroy(pfm);

  return best_tile;
}

/******************************************************************//**
  Trying to manage bombers and stuff.
  If we are in the open {
    if moving intelligently on a valid GOTO, {
      carry on doing it.
    } else {
      go refuel
    }
  } else {
    try to attack something
  } 
  TODO: distant target selection, support for fuel > 2
**********************************************************************/
void dai_manage_airunit(struct ai_type *ait, struct player *pplayer,
                        struct unit *punit)
{
  struct tile *dst_tile = unit_tile(punit);
  /* Loop prevention */
  int moves = punit->moves_left;
  int id = punit->id;
  struct pf_parameter parameter;
  struct pf_map *pfm;
  struct pf_path *path;

  CHECK_UNIT(punit);

  if (!is_unit_being_refueled(punit)) {
    /* We are out in the open, what shall we do? */
    if (punit->activity == ACTIVITY_GOTO
        /* We are on a GOTO.  Check if it will get us anywhere */
        && NULL != punit->goto_tile
        && !same_pos(unit_tile(punit), punit->goto_tile)
        && is_airunit_refuel_point(punit->goto_tile, pplayer, punit)) {
      pfm = pf_map_new(&parameter);
      path = pf_map_path(pfm, punit->goto_tile);
      if (path) {
        bool alive = adv_follow_path(punit, path, punit->goto_tile);

        pf_path_destroy(path);
        pf_map_destroy(pfm);
        if (alive && punit->moves_left > 0) {
          /* Maybe do something else. */
          dai_manage_airunit(ait, pplayer, punit);
        }
        return;
      }
      pf_map_destroy(pfm);
    } else if ((dst_tile = find_nearest_airbase(punit, &path))) {
      /* Go refuelling */
      if (!adv_follow_path(punit, path, dst_tile)) {
        pf_path_destroy(path);
        return; /* The unit died. */
      }
      pf_path_destroy(path);
    } else {
      if (punit->fuel == 1) {
	UNIT_LOG(LOG_DEBUG, punit, "Oops, fallin outta the sky");
      }
      def_ai_unit_data(punit, ait)->done = TRUE; /* Won't help trying again */
      return;
    }

  } else if (punit->fuel == unit_type_get(punit)->fuel) {
    /* We only leave a refuel point when we are on full fuel */

    if (find_something_to_bomb(ait, punit, &path, &dst_tile) > 0) {
      /* Found target, coordinates are in punit's goto_dest.
       * TODO: separate attacking into a function, check for the best 
       * tile to attack from */
      fc_assert_ret(path != NULL && dst_tile != NULL);
      if (!adv_follow_path(punit, path, dst_tile)) {
        pf_path_destroy(path);
        return; /* The unit died. */
      }
      pf_path_destroy(path);

      /* goto would be aborted: "Aborting GOTO for AI attack procedures"
       * now actually need to attack */
      /* We could use ai_military_findvictim here, but I don't trust it... */
      unit_activity_handling(punit, ACTIVITY_IDLE);
      if (is_tiles_adjacent(unit_tile(punit), dst_tile)) {
        dai_unit_attack(ait, punit, dst_tile);
      }
    } else if ((dst_tile = dai_find_strategic_airbase(ait, punit, &path))) {
      log_debug("%s will fly to (%i, %i) (%s) to fight there",
                unit_rule_name(punit), TILE_XY(dst_tile),
                tile_city(dst_tile) ? city_name_get(tile_city(dst_tile)) : "");
      def_ai_unit_data(punit, ait)->done = TRUE; /* Wait for next turn */
      if (!adv_follow_path(punit, path, dst_tile)) {
        pf_path_destroy(path);
        return; /* The unit died. */
      }
      pf_path_destroy(path);
    } else {
      log_debug("%s cannot find anything to kill and is staying put", 
                unit_rule_name(punit));
      def_ai_unit_data(punit, ait)->done = TRUE;
      unit_activity_handling(punit, ACTIVITY_IDLE);
    }
  }

  if ((punit = game_unit_by_number(id)) != NULL && punit->moves_left > 0
      && punit->moves_left != moves) {
    /* We have moved this turn, might have ended up stuck out in the fields
     * so, as a safety measure, let's manage again */
    dai_manage_airunit(ait, pplayer, punit);
  }

}

/******************************************************************//**
  Chooses the best available and usable air unit and records it in
  choice, if it's better than previous choice
  The interface is somewhat different from other ai_choose, but
  that's what it should be like, I believe -- GB
**********************************************************************/
bool dai_choose_attacker_air(struct ai_type *ait, struct player *pplayer,
                             struct city *pcity, struct adv_choice *choice,
                             bool allow_gold_upkeep)
{
  bool want_something = FALSE;

  /* This AI doesn't know to build planes */
  if (has_handicap(pplayer, H_NOPLANES)) {
    return FALSE;
  }

  /* military_advisor_choose_build does something idiotic, 
   * this function should not be called if there is danger... */
  if (choice->want >= 100 && choice->type != CT_ATTACKER) {
    return FALSE;
  }

  if (!player_knows_techs_with_flag(pplayer, TF_BUILD_AIRBORNE)) {
    return FALSE;
  }

  unit_type_iterate(punittype) {
    struct unit_class *pclass = utype_class(punittype);

    if (pclass->adv.land_move == MOVE_NONE
        || pclass->adv.sea_move == MOVE_NONE
        || uclass_has_flag(pclass, UCF_TERRAIN_SPEED)
        || unit_type_is_losing_hp(pplayer, punittype)) {
      /* We don't consider this a plane */
      continue;
    }

    if (!allow_gold_upkeep && utype_upkeep_cost(punittype, pplayer, O_GOLD) > 0) {
      continue;
    }

    /* Temporary hack because pathfinding can't handle Fighters. */
    if (!utype_can_do_action(punittype, ACTION_SUICIDE_ATTACK)
        && 1 == utype_fuel(punittype)) {
      continue;
    }

    if (can_city_build_unit_now(pcity, punittype)) {
      struct unit *virtual_unit =
	unit_virtual_create(pplayer, pcity, punittype,
                            do_make_unit_veteran(pcity, punittype));
      int profit = find_something_to_bomb(ait, virtual_unit, NULL, NULL);

      if (profit > choice->want) {
        /* Update choice */
        choice->want = profit;
        choice->value.utype = punittype;
        choice->type = CT_ATTACKER;
        choice->need_boat = FALSE;
        adv_choice_set_use(choice, "offensive air");
        want_something = TRUE;
        log_debug("%s wants to build %s (want=%d)",
                  city_name_get(pcity), utype_rule_name(punittype), profit);
      } else {
        log_debug("%s doesn't want to build %s (want=%d)",
                  city_name_get(pcity), utype_rule_name(punittype), profit);
      }
      unit_virtual_destroy(virtual_unit);
    }
  } unit_type_iterate_end;

  return want_something;
}
