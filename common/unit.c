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

/* utility */
#include "astring.h"
#include "bitvector.h"
#include "fcintl.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "ai.h"
#include "actions.h"
#include "base.h"
#include "city.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "road.h"
#include "specialist.h"
#include "tech.h"
#include "traderoutes.h"
#include "unitlist.h"

#include "unit.h"

const Activity_type_id tile_changing_activities[] =
    { ACTIVITY_PILLAGE, ACTIVITY_GEN_ROAD, ACTIVITY_IRRIGATE, ACTIVITY_MINE,
      ACTIVITY_BASE, ACTIVITY_CULTIVATE, ACTIVITY_PLANT, ACTIVITY_TRANSFORM,
      ACTIVITY_CLEAN, ACTIVITY_LAST };

struct cargo_iter {
  struct iterator vtable;
  const struct unit_list_link *links[GAME_TRANSPORT_MAX_RECURSIVE];
  int depth;
};
#define CARGO_ITER(iter) ((struct cargo_iter *) (iter))

/**********************************************************************//**
  Checks unit orders for equality.
**************************************************************************/
bool are_unit_orders_equal(const struct unit_order *order1,
                           const struct unit_order *order2)
{
  return order1->order == order2->order
      && order1->activity == order2->activity
      && order1->target == order2->target
      && order1->sub_target == order2->sub_target
      && order1->action == order2->action
      && order1->dir == order2->dir;
}

/**********************************************************************//**
  Determines if punit can be airlifted to dest_city now! So punit needs
  to be in a city now.
  If pdest_city is NULL, just indicate whether it's possible for the unit
  to be airlifted at all from its current position.
  The 'restriction' parameter specifies which player's knowledge this is
  based on -- one player can't see whether another's cities are currently
  able to airlift. (Clients other than global observers should only call
  this with a non-NULL 'restriction'.)
  Note that it does not take into account the possibility of conquest
  of unseen cities by an ally. That is to facilitate airlifting dialog
  usage most times. It is supposed that you don't ally ones who
  won't share maps with you when needed.
**************************************************************************/
enum unit_airlift_result
    test_unit_can_airlift_to(const struct civ_map *nmap,
                             const struct player *restriction,
                             const struct unit *punit,
                             const struct city *pdest_city)
{
  const struct city *psrc_city = tile_city(unit_tile(punit));
  const struct player *punit_owner;
  const struct tile *dst_tile = nullptr;
  const struct unit_type *putype = unit_type_get(punit);
  bool flagless = utype_has_flag(putype, UTYF_FLAGLESS);
  enum unit_airlift_result ok_result = AR_OK;

  if (0 == punit->moves_left
      && !utype_may_act_move_frags(putype, ACTION_AIRLIFT, 0)) {
    /* No moves left. */
    return AR_NO_MOVES;
  }

  if (!unit_can_do_action(punit, ACTION_AIRLIFT)) {
    return AR_WRONG_UNITTYPE;
  }

  if (0 < get_transporter_occupancy(punit)) {
    /* Units with occupants can't be airlifted currently. */
    return AR_OCCUPIED;
  }

  if (nullptr == psrc_city) {
    /* No city there. */
    return AR_NOT_IN_CITY;
  }

  if (psrc_city == pdest_city) {
    /* Airlifting to our current position doesn't make sense. */
    return AR_BAD_DST_CITY;
  }

  if (nullptr != pdest_city) {
    dst_tile = city_tile(pdest_city);

    if (nullptr != restriction
        ? !could_exist_in_city(nmap, restriction, putype, pdest_city)
        : !can_exist_at_tile(nmap, putype, dst_tile)) {
      /* Can't exist at the destination tile. */
      return AR_BAD_DST_CITY;
    }
  }

  punit_owner = unit_owner(punit);

  /* Check validity of both source and destination before checking capacity,
   * to avoid misleadingly optimistic returns. */

  if (punit_owner != city_owner(psrc_city)
      && !(game.info.airlifting_style & AIRLIFTING_ALLIED_SRC
           && pplayers_allied(punit_owner, city_owner(psrc_city)))) {
    /* Not allowed to airlift from this source. */
    return AR_BAD_SRC_CITY;
  }

  /* Check diplomatic possibility of the destination */
  if (nullptr != pdest_city) {
    if (punit_owner != city_owner(pdest_city)) {
      if (!(game.info.airlifting_style & AIRLIFTING_ALLIED_DEST
            && pplayers_allied(punit_owner, city_owner(pdest_city)))
          || flagless
          || is_non_allied_unit_tile(dst_tile, punit_owner, FALSE)) {
        /* Not allowed to airlift to this destination. */
        return AR_BAD_DST_CITY;
      }
    } else if (flagless
               && is_non_allied_unit_tile(dst_tile, punit_owner, TRUE)) {
      /* Foreign units block airlifting to this destination */
      return AR_BAD_DST_CITY;
    }
  }

  /* Check airlift capacities */
  if (!game.info.airlift_from_always_enabled) {
    if (nullptr == restriction || city_owner(psrc_city) == restriction) {
      /* We know for sure whether or not src can airlift this turn. */
      if (0 >= psrc_city->airlift
          && !(game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)) {
        /* The source cannot airlift for this turn (maybe already airlifted
         * or no airport).
         * See also do_airline() in server/unittools.h. */
        return AR_SRC_NO_FLIGHTS;
      } /* else, there is capacity; continue to other checks */
    } else {
      /* We don't have access to the 'airlift' field. Assume it's OK; can
       * only find out for sure by trying it. */
      ok_result = AR_OK_SRC_UNKNOWN;
    }
  }

  if (nullptr != pdest_city && !game.info.airlift_to_always_enabled) {
    if (nullptr == restriction || city_owner(pdest_city) == restriction) {
      if (0 >= pdest_city->airlift
          && !(game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)) {
        /* The destination cannot support airlifted units for this turn
         * (maybe already airlifted or no airport).
         * See also do_airline() in server/unittools.h. */
        return AR_DST_NO_FLIGHTS;
      } /* else continue */
    } else {
      ok_result = AR_OK_DST_UNKNOWN;
    }
  }

  return ok_result;
}

/**********************************************************************//**
  Determines if punit can be airlifted to dest_city now! So punit needs
  to be in a city now.
  On the server this gives correct information; on the client it errs on the
  side of saying airlifting is possible even if it's not certain given
  player knowledge.
**************************************************************************/
bool unit_can_airlift_to(const struct civ_map *nmap,
                         const struct unit *punit,
                         const struct city *pdest_city)
{
  if (is_server()) {
    return is_action_enabled_unit_on_city(nmap, ACTION_AIRLIFT,
                                          punit, pdest_city);
  } else {
    return action_prob_possible(action_prob_vs_city(nmap, punit, ACTION_AIRLIFT,
                                                    pdest_city));
  }
}

/**********************************************************************//**
  Return TRUE iff the unit is following client-side orders.
**************************************************************************/
bool unit_has_orders(const struct unit *punit)
{
  return punit->has_orders;
}

/**********************************************************************//**
  Returns how many shields the unit (type) is worth.
  @param punit     the unit. Can be NULL if punittype is set.
  @param punittype the unit's type. Can be NULL iff punit is set.
  @param paction   the action the unit does when valued.
  @return the unit's value in shields.
**************************************************************************/
int unit_shield_value(const struct unit *punit,
                      const struct unit_type *punittype,
                      const struct action *paction)
{
  int value;

  bool has_unit;
  const struct player *act_player;

  has_unit = punit != NULL;

  if (has_unit && punittype == NULL) {
    punittype = unit_type_get(punit);
  }

  fc_assert_ret_val(punittype != NULL, 0);
  fc_assert(punit == NULL || unit_type_get(punit) == punittype);
  fc_assert_ret_val(paction != NULL, 0);

  act_player = has_unit ? unit_owner(punit) : NULL;
  /* TODO: determine if tile and city should be where the unit currently is
   * located or the target city. Those two may differ. Wait for ruleset
   * author feed back. */

  value = utype_build_shield_cost_base(punittype);
  value += ((value
             * get_target_bonus_effects(NULL,
                                        &(const struct req_context) {
                                          .player = act_player,
                                          .unit = punit,
                                          .unittype = punittype,
                                          .action = paction,
                                        },
                                        NULL,
                                        EFT_UNIT_SHIELD_VALUE_PCT))
            / 100);

  return value;
}

/**********************************************************************//**
  Return TRUE unless it is known to be impossible to disband this unit at
  its current position to get full shields for building a wonder.
**************************************************************************/
bool unit_can_help_build_wonder_here(const struct civ_map *nmap,
                                     const struct unit *punit)
{
  struct city *pcity = tile_city(unit_tile(punit));

  if (pcity == NULL) {
    /* No city to help at this tile. */
    return FALSE;
  }

  if (!utype_can_do_action(unit_type_get(punit), ACTION_HELP_WONDER)) {
    /* This unit can never do help wonder. */
    return FALSE;
  }

  /* Evaluate all action enablers for extra accuracy. */
  /* TODO: Is it worth it? */
  return action_prob_possible(action_prob_vs_city(nmap, punit,
                                                  ACTION_HELP_WONDER,
                                                  pcity));
}

/**********************************************************************//**
  Return TRUE iff this unit can be disbanded at its current location to
  provide a trade route from the homecity to the target city.
**************************************************************************/
bool unit_can_est_trade_route_here(const struct unit *punit)
{
  struct city *phomecity, *pdestcity;

  return (utype_can_do_action(unit_type_get(punit), ACTION_TRADE_ROUTE)
          && (pdestcity = tile_city(unit_tile(punit)))
          && (phomecity = game_city_by_number(punit->homecity))
          && can_cities_trade(phomecity, pdestcity));
}

/**********************************************************************//**
  Return the number of units the transporter can hold (or 0).
**************************************************************************/
int get_transporter_capacity(const struct unit *punit)
{
  return unit_type_get(punit)->transport_capacity;
}

/**********************************************************************//**
  Is the unit capable of attacking?
**************************************************************************/
bool is_attack_unit(const struct unit *punit)
{
  return ((unit_can_do_action_result(punit, ACTRES_ATTACK)
           || unit_can_do_action_result(punit, ACTRES_BOMBARD)
           || unit_can_do_action_result(punit, ACTRES_WIPE_UNITS))
          && unit_type_get(punit)->attack_strength > 0);
}

/**********************************************************************//**
  Is unit capable of enforcing martial law?
**************************************************************************/
bool is_martial_law_unit(const struct unit *punit)
{
  return !unit_has_type_flag(punit, UTYF_CIVILIAN);
}

/**********************************************************************//**
  Does unit occupy the tile?
**************************************************************************/
bool is_occupying_unit(const struct unit *punit)
{
  return !unit_has_type_flag(punit, UTYF_CIVILIAN);
}

/**********************************************************************//**
  Can this unit enter peaceful borders?
**************************************************************************/
bool is_enter_borders_unit(const struct unit *punit)
{
  return unit_has_type_flag(punit, UTYF_CIVILIAN);
}

/**********************************************************************//**
  Does it make sense for this unit to protect others?
**************************************************************************/
bool is_guard_unit(const struct unit *punit)
{
  return !unit_has_type_flag(punit, UTYF_CIVILIAN);
}

/**********************************************************************//**
  Does it make sense to use this unit for some special role?

  If yes, don't waste it as cannon fodder.
**************************************************************************/
bool is_special_unit(const struct unit *punit)
{
  return unit_has_type_flag(punit, UTYF_CIVILIAN);
}

/**********************************************************************//**
  Does player see flag of the unit?

  @param  punit   Unit to check flag for
  @param  pplayer Player whose vision we're interested about, or nullptr
                  for global observer.
  @return         Is flag seen
**************************************************************************/
bool is_flagless_to_player(const struct unit *punit,
                           const struct player *pplayer)
{
  if (unit_owner(punit) != pplayer
      && unit_has_type_flag(punit, UTYF_FLAGLESS)) {
    if (pplayer == nullptr) {
      /* Global observer always sees the flags */
      return FALSE;
    }

    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Return TRUE iff this unit can do the specified generalized (ruleset
  defined) action enabler controlled action.
**************************************************************************/
bool unit_can_do_action(const struct unit *punit,
                        const action_id act_id)
{
  return utype_can_do_action(unit_type_get(punit), act_id);
}

/**********************************************************************//**
  Return TRUE iff this unit can do any enabler controlled action with the
  specified action result.
**************************************************************************/
bool unit_can_do_action_result(const struct unit *punit,
                               enum action_result result)
{
  return utype_can_do_action_result(unit_type_get(punit), result);
}

/**********************************************************************//**
  Return TRUE iff this unit can do any enabler controlled action with the
  specified action sub result.
**************************************************************************/
bool unit_can_do_action_sub_result(const struct unit *punit,
                                   enum action_sub_result sub_result)
{
  return utype_can_do_action_sub_result(unit_type_get(punit), sub_result);
}

/**********************************************************************//**
  Return TRUE iff this tile is threatened from any unit within 2 tiles.
**************************************************************************/
bool is_square_threatened(const struct civ_map *nmap,
                          const struct player *pplayer,
                          const struct tile *ptile, bool omniscient)
{
  square_iterate(nmap, ptile, 2, ptile1) {
    unit_list_iterate(ptile1->units, punit) {
      if ((omniscient
           || can_player_see_unit(pplayer, punit))
          && pplayers_at_war(pplayer, unit_owner(punit))
          && utype_acts_hostile(unit_type_get(punit))
          && (is_native_tile(unit_type_get(punit), ptile)
              || (can_attack_non_native(unit_type_get(punit))
                  && is_native_near_tile(nmap,
                                         unit_class_get(punit), ptile)))) {
        return TRUE;
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  This checks the "field unit" flag on the unit.  Field units cause
  unhappiness (under certain governments) even when they aren't abroad.
**************************************************************************/
bool is_field_unit(const struct unit *punit)
{
  return unit_has_type_flag(punit, UTYF_FIELDUNIT);
}

/**********************************************************************//**
  Is the unit one that is invisible on the map. A unit is invisible if
  it's vision layer is either V_INVIS or V_SUBSURFACE, or if it's
  transported by such unit.

  FIXME: Should the transports recurse all the way?
**************************************************************************/
bool is_hiding_unit(const struct unit *punit)
{
  enum vision_layer vl = unit_type_get(punit)->vlayer;

  if (vl == V_INVIS || vl == V_SUBSURFACE) {
    return TRUE;
  }

  if (unit_transported(punit)) {
    vl = unit_type_get(unit_transport_get(punit))->vlayer;
    if (vl == V_INVIS || vl == V_SUBSURFACE) {
      return TRUE;
    }
  }

  return FALSE;
}

/**********************************************************************//**
  Return TRUE iff this unit can add to a current city or build a new city
  at its current location.
**************************************************************************/
bool unit_can_add_or_build_city(const struct civ_map *nmap,
                                const struct unit *punit)
{
  struct city *tgt_city;

  if ((tgt_city = tile_city(unit_tile(punit)))) {
    return action_prob_possible(action_prob_vs_city(nmap, punit,
        ACTION_JOIN_CITY, tgt_city));
  } else {
    return action_prob_possible(action_prob_vs_tile(nmap, punit,
        ACTION_FOUND_CITY, unit_tile(punit), NULL));
  }
}

/**********************************************************************//**
  Return TRUE iff the unit can change homecity to the given city.
**************************************************************************/
bool can_unit_change_homecity_to(const struct civ_map *nmap,
                                 const struct unit *punit,
				 const struct city *pcity)
{
  if (pcity == NULL) {
    /* Can't change home city to a non existing city. */
    return FALSE;
  }

  return action_prob_possible(action_prob_vs_city(nmap, punit, ACTION_HOME_CITY,
                                                  pcity));
}

/**********************************************************************//**
  Return TRUE iff the unit can change homecity at its current location.
**************************************************************************/
bool can_unit_change_homecity(const struct civ_map *nmap,
                              const struct unit *punit)
{
  return can_unit_change_homecity_to(nmap, punit, tile_city(unit_tile(punit)));
}

/**********************************************************************//**
  Returns the speed of a unit doing an activity. This depends on the
  veteran level and the base move_rate of the unit (regardless of HP or
  effects). Usually this is just used for settlers but the value is also
  used for military units doing fortify/pillage activities.

  The speed is multiplied by ACTIVITY_FACTOR.
**************************************************************************/
int get_activity_rate(const struct unit *punit)
{
  const struct veteran_level *vlevel;
  const struct unit_type *ptype;
  int move_rate;

  fc_assert_ret_val(punit != NULL, 0);

  ptype = unit_type_get(punit);
  vlevel = utype_veteran_level(ptype, punit->veteran);
  fc_assert_ret_val(vlevel != NULL, 0);

  /* The speed of the settler depends on its base move_rate, not on
   * the number of moves actually remaining or the adjusted move rate.
   * This means sea formers won't have their activity rate increased by
   * Magellan's, and it means injured units work just as fast as
   * uninjured ones. Note the value is never less than SINGLE_MOVE. */
  move_rate = ptype->move_rate;

  /* All settler actions are multiplied by ACTIVITY_FACTOR. */
  return ACTIVITY_FACTOR
         * (float)vlevel->power_fact / 100
         * move_rate / SINGLE_MOVE;
}

/**********************************************************************//**
  Returns the amount of work a unit does (will do) on an activity this
  turn. Units that have no MP do no work.

  The speed is multiplied by ACTIVITY_FACTOR.
**************************************************************************/
int get_activity_rate_this_turn(const struct unit *punit)
{
  /* This logic is also coded in client/goto.c. */
  if (punit->moves_left > 0) {
    return get_activity_rate(punit);
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Return the estimated number of turns for the worker unit to start and
  complete the activity at the given location. This assumes no other
  worker units are helping out, and doesn't take account of any work
  already done by this unit.
**************************************************************************/
int get_turns_for_activity_at(const struct unit *punit,
                              enum unit_activity activity,
                              const struct tile *ptile,
                              struct extra_type *tgt)
{
  /* FIXME: This is just an approximation since we don't account for
   * get_activity_rate_this_turn. */
  int speed = get_activity_rate(punit);
  int points_needed;

  fc_assert(tgt != NULL || !is_targeted_activity(activity));

  points_needed = tile_activity_time(activity, ptile, tgt);

  if (points_needed >= 0 && speed > 0) {
    return (points_needed - 1) / speed + 1; /* Round up */
  } else {
    return FC_INFINITY;
  }
}

/**********************************************************************//**
  Return TRUE if activity requires some sort of target to be specified.
**************************************************************************/
bool activity_requires_target(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_PILLAGE:
  case ACTIVITY_BASE:
  case ACTIVITY_GEN_ROAD:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_MINE:
  case ACTIVITY_CLEAN:
    return TRUE;
  case ACTIVITY_IDLE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_CONVERT:
    return FALSE;
  /* These shouldn't be kicking around internally. */
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);

  return FALSE;
}

/**********************************************************************//**
  Return whether the unit can be put in auto-worker mode.

  NOTE: we used to have "auto" mode including autoworker and auto-attack.
  This was bad because the two were indestinguishable even though they
  are very different. Now auto-attack is done differently so we just have
  auto-worker. If any new auto modes are introduced they should be
  handled separately.
**************************************************************************/
bool can_unit_do_autoworker(const struct unit *punit)
{
  return unit_type_get(punit)->adv.worker;
}

/**********************************************************************//**
  Return the name of the activity in a static buffer.
**************************************************************************/
const char *get_activity_text(enum unit_activity activity)
{
  /* The switch statement has just the activities listed with no "default"
   * handling.  This enables the compiler to detect missing entries
   * automatically, and still handles everything correctly. */
  switch (activity) {
  case ACTIVITY_IDLE:
    return _("Idle");
  case ACTIVITY_CLEAN:
    return _("Clean");
  case ACTIVITY_MINE:
    /* TRANS: Activity name, verb in English */
    return Q_("?act:Mine");
  case ACTIVITY_PLANT:
    /* TRANS: Activity name, verb in English */
    return _("Plant");
  case ACTIVITY_IRRIGATE:
    return _("Irrigate");
  case ACTIVITY_CULTIVATE:
    return _("Cultivate");
  case ACTIVITY_FORTIFYING:
    return _("Fortifying");
  case ACTIVITY_FORTIFIED:
    return _("Fortified");
  case ACTIVITY_SENTRY:
    return _("Sentry");
  case ACTIVITY_PILLAGE:
    return _("Pillage");
  case ACTIVITY_GOTO:
    return _("Goto");
  case ACTIVITY_EXPLORE:
    return _("Explore");
  case ACTIVITY_TRANSFORM:
    return _("Transform");
  case ACTIVITY_BASE:
    return _("Base");
  case ACTIVITY_GEN_ROAD:
    return _("Road");
  case ACTIVITY_CONVERT:
    return _("Convert");
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);
  return _("Unknown");
}

/**********************************************************************//**
  Tells whether pcargo could possibly be in ptrans, disregarding
  unit positions and ownership and load actions possibility,
  but regarding number and types of their current cargo.
  pcargo and ptrans must be valid unit pointers, pcargo not loaded anywhere
**************************************************************************/
bool could_unit_be_in_transport(const struct unit *pcargo,
                                const struct unit *ptrans)
{
  /* Make sure this transporter can carry this type of unit. */
  if (!can_unit_transport(ptrans, pcargo)) {
    return FALSE;
  }

  /* Make sure there's room in the transporter. */
  if (get_transporter_occupancy(ptrans)
      >= get_transporter_capacity(ptrans)) {
    return FALSE;
  }

  /* Check iff this is a valid transport. */
  if (!unit_transport_check(pcargo, ptrans)) {
    return FALSE;
  }

  /* Check transport depth. */
  if (GAME_TRANSPORT_MAX_RECURSIVE
      < 1 + unit_transport_depth(ptrans) + unit_cargo_depth(pcargo)) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Return TRUE iff the given unit could be loaded into the transporter
  if we moved there.
**************************************************************************/
bool could_unit_load(const struct unit *pcargo, const struct unit *ptrans)
{
  if (!pcargo || !ptrans || pcargo == ptrans) {
    return FALSE;
  }

  /* Double-check ownership of the units: you can load into an allied unit
   * (of course only allied units can be on the same tile). */
  if (!pplayers_allied(unit_owner(pcargo), unit_owner(ptrans))) {
    return FALSE;
  }

  /* Un-embarkable transport must be in city or base to load cargo. */
  if (!utype_can_freely_load(unit_type_get(pcargo), unit_type_get(ptrans))
      && !tile_city(unit_tile(ptrans))
      && !tile_has_native_base(unit_tile(ptrans), unit_type_get(ptrans))) {
    return FALSE;
  }

  return could_unit_be_in_transport(pcargo, ptrans);
}

/**********************************************************************//**
  Return TRUE iff the given unit can be loaded into the transporter.
**************************************************************************/
bool can_unit_load(const struct unit *pcargo, const struct unit *ptrans)
{
  /* This function needs to check EVERYTHING. */

  /* Check positions of the units.  Of course you can't load a unit onto
   * a transporter on a different tile... */
  if (!same_pos(unit_tile(pcargo), unit_tile(ptrans))) {
    return FALSE;
  }

  /* Cannot load if cargo is already loaded onto something else. */
  if (unit_transported(pcargo)) {
    return FALSE;
  }

  return could_unit_load(pcargo, ptrans);
}

/**********************************************************************//**
  Return TRUE iff the given unit can be unloaded from its current
  transporter.

  This function checks everything *except* the legality of the position
  after the unloading.  The caller may also want to call
  can_unit_exist_at_tile() to check this, unless the unit is unloading and
  moving at the same time.
**************************************************************************/
bool can_unit_unload(const struct unit *pcargo, const struct unit *ptrans)
{
  if (!pcargo || !ptrans) {
    return FALSE;
  }

  /* Make sure the unit's transporter exists and is known. */
  if (unit_transport_get(pcargo) != ptrans) {
    return FALSE;
  }

  /* Un-disembarkable transport must be in city or base to unload cargo. */
  if (!utype_can_freely_unload(unit_type_get(pcargo), unit_type_get(ptrans))
      && !tile_city(unit_tile(ptrans))
      && !tile_has_native_base(unit_tile(ptrans), unit_type_get(ptrans))) {
    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Return TRUE iff the given unit can leave its current transporter without
  doing any other action or move.
**************************************************************************/
bool can_unit_deboard_or_be_unloaded(const struct civ_map *nmap,
                                     const struct unit *pcargo,
                                     const struct unit *ptrans)
{
  if (!pcargo || !ptrans) {
    return FALSE;
  }

  fc_assert_ret_val(unit_transport_get(pcargo) == ptrans, FALSE);

  if (is_server()) {
    return (is_action_enabled_unit_on_unit(nmap, ACTION_TRANSPORT_DEBOARD,
                                           pcargo, ptrans)
            || is_action_enabled_unit_on_unit(nmap, ACTION_TRANSPORT_UNLOAD,
                                              ptrans, pcargo));
  } else {
    return (action_prob_possible(
             action_prob_vs_unit(nmap, pcargo, ACTION_TRANSPORT_DEBOARD, ptrans))
            || action_prob_possible(
              action_prob_vs_unit(nmap, ptrans, ACTION_TRANSPORT_UNLOAD,
                                  pcargo)));
  }
}

/**********************************************************************//**
  Return whether the unit can be paradropped - that is, if the unit is in
  a friendly city or on an airbase special, has enough movepoints left, and
  has not paradropped yet this turn.
**************************************************************************/
bool can_unit_teleport(const struct civ_map *nmap, const struct unit *punit)
{
  action_by_result_iterate(paction, ACTRES_TELEPORT) {
    if (action_maybe_possible_actor_unit(nmap, action_id(paction), punit)) {
      return TRUE;
    }
  } action_by_result_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Return whether the unit can be paradropped - that is, if the unit is in
  a friendly city or on an airbase special, has enough movepoints left, and
  has not paradropped yet this turn.
**************************************************************************/
bool can_unit_paradrop(const struct civ_map *nmap, const struct unit *punit)
{
  action_by_result_iterate(paction, ACTRES_PARADROP) {
    if (action_maybe_possible_actor_unit(nmap, action_id(paction), punit)) {
      return TRUE;
    }
  } action_by_result_iterate_end;
  action_by_result_iterate(paction, ACTRES_PARADROP_CONQUER) {
    if (action_maybe_possible_actor_unit(nmap, action_id(paction), punit)) {
      return TRUE;
    }
  } action_by_result_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Check if the unit's current activity is actually legal.
**************************************************************************/
bool can_unit_continue_current_activity(const struct civ_map *nmap,
                                        struct unit *punit)
{
  enum unit_activity current = punit->activity;
  struct extra_type *target = punit->activity_target;
  enum unit_activity current2
    = (current == ACTIVITY_FORTIFIED) ? ACTIVITY_FORTIFYING : current;
  enum gen_action action = punit->action;
  bool result;

  punit->activity = ACTIVITY_IDLE;
  punit->activity_target = NULL;

  result = can_unit_do_activity_targeted(nmap, punit, current2, action,
                                         target);

  punit->activity = current;
  punit->activity_target = target;
  punit->action = action;

  return result;
}

/**********************************************************************//**
  Return TRUE iff the unit can do the given untargeted activity at its
  current location.

  Note that some activities must be targeted; see
  can_unit_do_activity_targeted().
**************************************************************************/
bool can_unit_do_activity(const struct civ_map *nmap,
                          const struct unit *punit,
                          enum unit_activity activity,
                          enum gen_action action)
{
  struct extra_type *target = NULL;

  /* FIXME: Lots of callers (usually client real_menus_update()) rely on
   * being able to find out whether an activity is in general possible.
   * Find one for them, but when they come to do the activity, they will
   * have to determine the target themselves */
  {
    struct tile *ptile = unit_tile(punit);

    if (activity == ACTIVITY_IRRIGATE) {
      target = next_extra_for_tile(ptile,
                                   EC_IRRIGATION,
                                   unit_owner(punit),
                                   punit);
      if (NULL == target) {
        return FALSE; /* No more irrigation extras available. */
      }
    } else if (activity == ACTIVITY_MINE) {
      target = next_extra_for_tile(ptile,
                                   EC_MINE,
                                   unit_owner(punit),
                                   punit);
      if (NULL == target) {
        return FALSE; /* No more mine extras available. */
      }
    }
  }

  return can_unit_do_activity_targeted(nmap, punit, activity, action,
                                       target);
}

/**********************************************************************//**
  Return whether the unit can do the targeted activity at its current
  location.
**************************************************************************/
bool can_unit_do_activity_targeted(const struct civ_map *nmap,
                                   const struct unit *punit,
                                   enum unit_activity activity,
                                   enum gen_action action,
                                   struct extra_type *target)
{
  return can_unit_do_activity_targeted_at(nmap, punit, activity, action,
                                          target, unit_tile(punit));
}

/**********************************************************************//**
  Return TRUE if the unit can do the targeted activity at the given
  location.
**************************************************************************/
bool can_unit_do_activity_targeted_at(const struct civ_map *nmap,
                                      const struct unit *punit,
                                      enum unit_activity activity,
                                      enum gen_action action,
                                      struct extra_type *target,
                                      const struct tile *ptile)
{
  /* Check that no build activity conflicting with one already in progress
   * gets executed. */
  /* FIXME: Should check also the cases where one of the activities is terrain
   *        change that destroys the target of the other activity */
  if (target != NULL && is_build_activity(activity)) {
    if (tile_is_placing(ptile)) {
      return FALSE;
    }

    unit_list_iterate(ptile->units, tunit) {
      if (is_build_activity(tunit->activity)
          && !can_extras_coexist(target, tunit->activity_target)) {
        return FALSE;
      }
    } unit_list_iterate_end;
  }

#define RETURN_IS_ACTIVITY_ENABLED_UNIT_ON(paction)                       \
{                                                                         \
    switch (action_get_target_kind(paction)) {                            \
    case ATK_TILE:                                                        \
      return is_action_enabled_unit_on_tile(nmap, paction->id,            \
                                            punit, ptile, target);        \
    case ATK_EXTRAS:                                                      \
      return is_action_enabled_unit_on_extras(nmap, paction->id,          \
                                              punit, ptile, target);      \
    case ATK_CITY:                                                        \
    case ATK_UNIT:                                                        \
    case ATK_STACK:                                                       \
    case ATK_SELF:                                                        \
      return FALSE;                                                       \
    case ATK_COUNT:                                                       \
      break; /* Handle outside switch */                                  \
    }                                                                     \
    fc_assert(action_target_kind_is_valid(                                \
                action_get_target_kind(paction)));                        \
    return FALSE;                                                         \
  }

  switch (activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_GOTO:
    return TRUE;

  case ACTIVITY_CLEAN:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, target);

  case ACTIVITY_MINE:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action, punit,
                                          ptile, target);

  case ACTIVITY_PLANT:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, NULL);

  case ACTIVITY_IRRIGATE:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action, punit,
                                          ptile, target);

  case ACTIVITY_CULTIVATE:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, NULL);

  case ACTIVITY_FORTIFYING:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_self()");
    return is_action_enabled_unit_on_self(nmap, action,
                                          punit);

  case ACTIVITY_FORTIFIED:
    return FALSE;

  case ACTIVITY_BASE:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, target);

  case ACTIVITY_GEN_ROAD:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, target);

  case ACTIVITY_SENTRY:
    if (!can_unit_survive_at_tile(nmap, punit, unit_tile(punit))
        && !unit_transported(punit)) {
      /* Don't let units sentry on tiles they will die on. */
      return FALSE;
    }
    return TRUE;

  case ACTIVITY_PILLAGE:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    RETURN_IS_ACTIVITY_ENABLED_UNIT_ON(action_by_number(action));

  case ACTIVITY_EXPLORE:
    return (!unit_type_get(punit)->fuel && !is_losing_hp(punit));

  case ACTIVITY_TRANSFORM:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_tile()");
    return is_action_enabled_unit_on_tile(nmap, action,
                                          punit, ptile, NULL);

  case ACTIVITY_CONVERT:
    /* The call below doesn't support actor tile speculation. */
    fc_assert_msg(unit_tile(punit) == ptile,
                  "Please use action_speculate_unit_on_self()");
    return is_action_enabled_unit_on_self(nmap, action, punit);

  case ACTIVITY_LAST:
    break;
  }

  log_error("can_unit_do_activity_targeted_at() unknown activity %d",
            activity);

  return FALSE;

#undef RETURN_IS_ACTIVITY_ENABLED_UNIT_ON
}

/**********************************************************************//**
  Assign a new task to a unit. Doesn't account for changed_from.
**************************************************************************/
static void set_unit_activity_internal(struct unit *punit,
                                       enum unit_activity new_activity,
                                       enum gen_action trigger_action)
{
  punit->activity = new_activity;
  punit->action = trigger_action;
  punit->activity_count = 0;
  punit->activity_target = NULL;
  if (new_activity == ACTIVITY_IDLE && punit->moves_left > 0) {
    /* No longer done. */
    punit->done_moving = FALSE;
  }
}

/**********************************************************************//**
  Assign a new untargeted task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity,
                       enum gen_action trigger_action)
{
  fc_assert_ret(!activity_requires_target(new_activity));

  if (new_activity == ACTIVITY_FORTIFYING
      && punit->changed_from == ACTIVITY_FORTIFIED) {
    new_activity = ACTIVITY_FORTIFIED;
  }
  set_unit_activity_internal(punit, new_activity, trigger_action);
  if (new_activity == punit->changed_from) {
    punit->activity_count = punit->changed_from_count;
  }
}

/**********************************************************************//**
  assign a new targeted task to a unit.
**************************************************************************/
void set_unit_activity_targeted(struct unit *punit,
                                enum unit_activity new_activity,
                                struct extra_type *new_target,
                                enum gen_action trigger_action)
{
  fc_assert_ret(activity_requires_target(new_activity)
                || new_target == NULL);

  set_unit_activity_internal(punit, new_activity, trigger_action);
  punit->activity_target = new_target;
  if (new_activity == punit->changed_from
      && new_target == punit->changed_from_target) {
    punit->activity_count = punit->changed_from_count;
  }
}

/**********************************************************************//**
  Return whether any units on the tile are doing this activity.
**************************************************************************/
bool is_unit_activity_on_tile(enum unit_activity activity,
                              const struct tile *ptile)
{
  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == activity) {
      return TRUE;
    }
  } unit_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Return a mask of the extras which are actively (currently) being
  pillaged on the given tile.
**************************************************************************/
bv_extras get_unit_tile_pillage_set(const struct tile *ptile)
{
  bv_extras tgt_ret;

  BV_CLR_ALL(tgt_ret);
  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == ACTIVITY_PILLAGE) {
      BV_SET(tgt_ret, extra_index(punit->activity_target));
    }
  } unit_list_iterate_end;

  return tgt_ret;
}

/**********************************************************************//**
  Append text describing the unit's current activity to the given astring.
**************************************************************************/
void unit_activity_astr(const struct unit *punit, struct astring *astr)
{
  if (!punit || !astr) {
    return;
  }

  switch (punit->activity) {
  case ACTIVITY_IDLE:
    if (utype_fuel(unit_type_get(punit))) {
      int rate, f;

      rate = unit_type_get(punit)->move_rate;
      f = ((punit->fuel) - 1);

      /* Add in two parts as move_points_text() returns ptr to static
       * End result: "Moves: (fuel)moves_left" */
      astr_add_line(astr, "%s: (%s)", _("Moves"),
                    move_points_text((rate * f) + punit->moves_left, FALSE));
      astr_add(astr, "%s",
               move_points_text(punit->moves_left, FALSE));
    } else {
      astr_add_line(astr, "%s: %s", _("Moves"),
                    move_points_text(punit->moves_left, FALSE));
    }
    return;
  case ACTIVITY_CLEAN:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_CONVERT:
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
    astr_add_line(astr, "%s", get_activity_text(punit->activity));
    return;
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
    if (punit->activity_target == NULL) {
      astr_add_line(astr, "%s", get_activity_text(punit->activity));
    } else {
      astr_add_line(astr, "Building %s",
                    extra_name_translation(punit->activity_target));
    }
    return;
  case ACTIVITY_PILLAGE:
    if (punit->activity_target != NULL) {
      astr_add_line(astr, "%s: %s", get_activity_text(punit->activity),
                    extra_name_translation(punit->activity_target));
    } else {
      astr_add_line(astr, "%s", get_activity_text(punit->activity));
    }
    return;
  case ACTIVITY_BASE:
    astr_add_line(astr, "%s: %s", get_activity_text(punit->activity),
                  extra_name_translation(punit->activity_target));
    return;
  case ACTIVITY_GEN_ROAD:
    astr_add_line(astr, "%s: %s", get_activity_text(punit->activity),
                  extra_name_translation(punit->activity_target));
    return;
  case ACTIVITY_LAST:
    break;
  }

  log_error("Unknown unit activity %d for %s (nb %d) in %s()",
            punit->activity, unit_rule_name(punit), punit->id, __FUNCTION__);
}

/**********************************************************************//**
  Append a line of text describing the unit's upkeep to the astring.

  NB: In the client it is assumed that this information is only available
  for units owned by the client's player; the caller must check this.
**************************************************************************/
void unit_upkeep_astr(const struct unit *punit, struct astring *astr)
{
  if (!punit || !astr) {
    return;
  }

  astr_add_line(astr, "%s %d/%d/%d", _("Food/Shield/Gold:"),
                punit->upkeep[O_FOOD], punit->upkeep[O_SHIELD],
                punit->upkeep[O_GOLD]);
}

/**********************************************************************//**
  Return the nationality of the unit.
**************************************************************************/
struct player *unit_nationality(const struct unit *punit)
{
  fc_assert_ret_val(NULL != punit, NULL);
  return punit->nationality;
}

/**********************************************************************//**
  Set the tile location of the unit.
  Tile can be NULL (for transported units).
**************************************************************************/
void unit_tile_set(struct unit *punit, struct tile *ptile)
{
  fc_assert_ret(NULL != punit);
  punit->tile = ptile;
}

/**********************************************************************//**
  Returns one of the units, if the tile contains an allied unit and
  only allied units. Returns NULL if there is no units, or some of them
  are not allied.
  (ie, if your nation A is allied with B, and B is allied with C, a tile
  containing units from B and C will return NULL)
**************************************************************************/
struct unit *tile_allied_unit(const struct tile *ptile,
                              const struct player *pplayer)
{
  struct unit *punit = NULL;

  unit_list_iterate(ptile->units, cunit) {
    if (pplayers_allied(pplayer, unit_owner(cunit))) {
      punit = cunit;
    } else {
      return NULL;
    }
  }
  unit_list_iterate_end;

  return punit;
}

/**********************************************************************//**
  Is there an enemy unit on this tile? Returns the unit or NULL if none.

  This function is likely to fail if used at the client because the client
  doesn't see all units. (Maybe it should be moved into the server code.)
**************************************************************************/
struct unit *tile_enemy_unit(const struct tile *ptile,
                             const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_at_war(unit_owner(punit), pplayer)) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Return one of the non-allied units on the tile, if there is any
**************************************************************************/
struct unit *tile_non_allied_unit(const struct tile *ptile,
                                  const struct player *pplayer,
                                  bool everyone_non_allied)
{
  unit_list_iterate(ptile->units, punit) {
    struct player *owner = unit_owner(punit);

    if (everyone_non_allied && owner != pplayer) {
      return punit;
    }

    if (!pplayers_allied(owner, pplayer)
        || is_flagless_to_player(punit, pplayer)) {
      return punit;
    }
  }
  unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Return an unit belonging to any other player, if there are any
  on the tile.
**************************************************************************/
struct unit *tile_other_players_unit(const struct tile *ptile,
                                     const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (unit_owner(punit) != pplayer) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Return an unit we have peace or ceasefire with on this tile,
  if any exist.
**************************************************************************/
struct unit *tile_non_attack_unit(const struct tile *ptile,
                                  const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_non_attack(unit_owner(punit), pplayer)) {
      return punit;
    }
  }
  unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Is there an occupying unit on this tile?

  Intended for both client and server; assumes that hiding units are not
  sent to client. First check tile for known and seen.

  Called by city_can_work_tile().
**************************************************************************/
struct unit *unit_occupies_tile(const struct tile *ptile,
                                const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (!is_occupying_unit(punit)) {
      continue;
    }

    if (uclass_has_flag(unit_class_get(punit), UCF_DOESNT_OCCUPY_TILE)) {
      continue;
    }

    if (pplayers_at_war(unit_owner(punit), pplayer)) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Is this square controlled by the pplayer? Function to be used on
  server side only.

  Here "plr zoc" means essentially a square which is *not* adjacent to an
  enemy unit (that has a ZOC) on a terrain that has zoc rules.
**************************************************************************/
bool is_plr_zoc_srv(const struct player *pplayer, const struct tile *ptile0,
                    const struct civ_map *zmap)
{
  struct extra_type_list *zoccers = extra_type_list_of_zoccers();

  square_iterate(zmap, ptile0, 1, ptile) {
    struct terrain *pterrain;
    struct city *pcity;

    pterrain = tile_terrain(ptile);
    if (terrain_has_flag(pterrain, TER_NO_ZOC)) {
      continue;
    }

    pcity = tile_non_allied_city(ptile, pplayer);
    if (pcity != NULL) {
      if (unit_list_size(ptile->units) > 0) {
        /* Occupied enemy city, it doesn't matter if units inside have
         * UTYF_NOZOC or not. */
        return FALSE;
      }
    } else {
      if (!pplayers_allied(extra_owner(ptile), pplayer)) {
        extra_type_list_iterate(zoccers, pextra) {
          if (tile_has_extra(ptile, pextra)) {
            return FALSE;
          }
        } extra_type_list_iterate_end;
      }

      unit_list_iterate(ptile->units, punit) {
        if (!pplayers_allied(unit_owner(punit), pplayer)
            && !unit_has_type_flag(punit, UTYF_NOZOC)
            && !unit_transported_server(punit)) {
          bool hidden = FALSE;

          /* We do NOT check the possibility that player is allied with an extra owner,
           * and should thus see inside the extra.
           * This is to avoid the situation where having an alliance with third player
           * suddenly causes ZoC from a unit that would not cause it without the alliance. */
          extra_type_list_iterate(unit_class_get(punit)->cache.hiding_extras, pextra) {
            if (tile_has_extra(ptile, pextra)) {
              hidden = TRUE;
              break;
            }
          } extra_type_list_iterate_end;

          if (!hidden) {
            return FALSE;
          }
        }
      } unit_list_iterate_end;
    }
  } square_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Is this square controlled by the pplayer? Function to be used on
  client side only.

  Here "plr zoc" means essentially a square which is *not* adjacent to an
  enemy unit (that has a ZOC) on a terrain that has zoc rules.

  Since this function is used in the client, it has to deal with some
  client-specific features, like FoW and the fact that the client cannot
  see units inside enemy cities.
**************************************************************************/
bool is_plr_zoc_client(const struct player *pplayer, const struct tile *ptile0,
                       const struct civ_map *zmap)
{
  struct extra_type_list *zoccers = extra_type_list_of_zoccers();

  square_iterate(zmap, ptile0, 1, ptile) {
    struct terrain *pterrain;
    struct city *pcity;

    pterrain = tile_terrain(ptile);
    if (T_UNKNOWN == pterrain
        || terrain_has_flag(pterrain, TER_NO_ZOC)) {
      continue;
    }

    pcity = tile_non_allied_city(ptile, pplayer);
    if (pcity != NULL) {
      if (pcity->client.occupied
          || TILE_KNOWN_UNSEEN == tile_get_known(ptile, pplayer)) {
        /* Occupied enemy city, it doesn't matter if units inside have
         * UTYF_NOZOC or not. Fogged city is assumed to be occupied. */
        return FALSE;
      }
    } else {
      if (!pplayers_allied(extra_owner(ptile), pplayer)) {
        extra_type_list_iterate(zoccers, pextra) {
          if (tile_has_extra(ptile, pextra)) {
            return FALSE;
          }
        } extra_type_list_iterate_end;
      }

      unit_list_iterate(ptile->units, punit) {
        if (!unit_transported_client(punit)
            && !pplayers_allied(unit_owner(punit), pplayer)
            && !unit_has_type_flag(punit, UTYF_NOZOC)) {
          return FALSE;
        }
      } unit_list_iterate_end;
    }
  } square_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Takes into account unit class flag UCF_ZOC as well as IGZOC
**************************************************************************/
bool unit_type_really_ignores_zoc(const struct unit_type *punittype)
{
  return (!uclass_has_flag(utype_class(punittype), UCF_ZOC)
	  || utype_has_flag(punittype, UTYF_IGZOC));
}

/**********************************************************************//**
  An "aggressive" unit is a unit which may cause unhappiness
  under a Republic or Democracy.
  A unit is *not* aggressive if one or more of following is true:
  - zero attack strength
  - inside a city
  - near friendly city and in extra that provides "no aggressive"
**************************************************************************/
bool unit_being_aggressive(const struct civ_map *nmap,
                           const struct unit *punit)
{
  const struct tile *ptile = unit_tile(punit);
  int max_friendliness_range;

  if (!is_attack_unit(punit)) {
    return FALSE;
  }
  if (tile_city(ptile)) {
    return FALSE;
  }
  if (BORDERS_DISABLED != game.info.borders) {
    switch (game.info.happyborders) {
    case HB_DISABLED:
      break;
    case HB_NATIONAL:
      if (tile_owner(ptile) == unit_owner(punit)) {
        return FALSE;
      }
      break;
    case HB_ALLIANCE:
      if (pplayers_allied(tile_owner(ptile), unit_owner(punit))) {
        return FALSE;
      }
      break;
    }
  }

  max_friendliness_range = tile_has_not_aggressive_extra_for_unit(ptile,
                                                                  unit_type_get(punit));
  if (max_friendliness_range >= 0) {
    return !is_unit_near_a_friendly_city(nmap, punit, max_friendliness_range);
  }

  return TRUE;
}

/**********************************************************************//**
  Returns true if given activity builds an extra.
**************************************************************************/
bool is_build_activity(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_BASE:
  case ACTIVITY_GEN_ROAD:
    return TRUE;
  default:
    return FALSE;
  }
}

/**********************************************************************//**
  Returns true if given activity is some kind of cleaning.
**************************************************************************/
bool is_clean_activity(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_PILLAGE:
  case ACTIVITY_CLEAN:
    return TRUE;
  default:
    return FALSE;
  }
}

/**********************************************************************//**
  Returns true if given activity changes terrain.
**************************************************************************/
bool is_terrain_change_activity(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_CULTIVATE:
  case ACTIVITY_PLANT:
  case ACTIVITY_TRANSFORM:
    return TRUE;
  default:
    return FALSE;
  }
}

/**********************************************************************//**
  Returns true if given activity affects tile.
**************************************************************************/
bool is_tile_activity(enum unit_activity activity)
{
  return is_build_activity(activity)
    || is_clean_activity(activity)
    || is_terrain_change_activity(activity);
}

/**********************************************************************//**
  Returns true if given activity requires target
**************************************************************************/
bool is_targeted_activity(enum unit_activity activity)
{
  return is_build_activity(activity)
    || is_clean_activity(activity);
}

/**********************************************************************//**
  Create a virtual unit skeleton. pcity can be nullptr, but then you need
  to set tile and homecity yourself.
**************************************************************************/
struct unit *unit_virtual_create(struct player *pplayer, struct city *pcity,
                                 const struct unit_type *punittype,
                                 int veteran_level)
{
  /* Make sure that contents of unit structure are correctly initialized,
   * if you ever allocate it by some other mean than fc_calloc() */
  struct unit *punit = fc_calloc(1, sizeof(*punit));
  int max_vet_lvl;

  /* It does not register the unit so the id is set to 0. */
  punit->id = IDENTITY_NUMBER_ZERO;

  fc_assert_ret_val(punittype != nullptr, nullptr);               /* No untyped units! */
  punit->utype = punittype;

  fc_assert_ret_val(!is_server() || pplayer != nullptr, nullptr); /* No unowned units! */
  punit->owner = pplayer;
  punit->nationality = pplayer;

  punit->refcount = 1;
  punit->facing = rand_direction();

  if (pcity != nullptr) {
    unit_tile_set(punit, pcity->tile);
    punit->homecity = pcity->id;
  } else {
    unit_tile_set(punit, nullptr);
    punit->homecity = IDENTITY_NUMBER_ZERO;
  }

  memset(punit->upkeep, 0, O_LAST * sizeof(*punit->upkeep));
  punit->goto_tile = nullptr;
  max_vet_lvl = utype_veteran_levels(punittype) - 1;
  punit->veteran = MIN(veteran_level, max_vet_lvl);
  /* A unit new and fresh ... */
  punit->fuel = utype_fuel(unit_type_get(punit));
  punit->hp = unit_type_get(punit)->hp;
  if (utype_has_flag(punittype, UTYF_RANDOM_MOVEMENT)) {
    /* Random moves units start with zero movement as their first movement
     * will be only after their moves have been reset in the beginning of
     * the next turn. */
    punit->moves_left = 0;
  } else {
    punit->moves_left = unit_move_rate(punit);
  }
  punit->moved = FALSE;

  punit->ssa_controller = SSA_NONE;
  punit->paradropped = FALSE;
  punit->done_moving = FALSE;

  punit->transporter = nullptr;
  punit->transporting = unit_list_new();

  punit->carrying = nullptr;

  set_unit_activity(punit, ACTIVITY_IDLE, ACTION_NONE);
  punit->battlegroup = BATTLEGROUP_NONE;
  punit->has_orders = FALSE;

  punit->action_decision_want = ACT_DEC_NOTHING;
  punit->action_decision_tile = nullptr;

  punit->stay = FALSE;

  punit->birth_turn = game.info.turn;
  punit->current_form_turn = game.info.turn;

  if (is_server()) {
    punit->server.debug = FALSE;

    punit->server.dying = FALSE;

    punit->server.removal_callback = nullptr;

    memset(punit->server.upkeep_paid, 0,
           O_LAST * sizeof(*punit->server.upkeep_paid));

    punit->server.ord_map = 0;
    punit->server.ord_city = 0;

    punit->server.vision = nullptr; /* No vision. */
    punit->server.action_timestamp = 0;
    /* Must be an invalid turn number, and an invalid previous turn
     * number. */
    punit->server.action_turn = -2;
    /* punit->server.moving = NULL; set by fc_calloc(). */

    punit->server.adv = fc_calloc(1, sizeof(*punit->server.adv));

    CALL_FUNC_EACH_AI(unit_alloc, punit);
  } else {
    punit->client.focus_status = FOCUS_AVAIL;
    punit->client.transported_by = -1;
    punit->client.colored = FALSE;
    punit->client.act_prob_cache = nullptr;
  }

  return punit;
}

/**********************************************************************//**
  Free the memory used by virtual unit. By the time this function is
  called, you should already have unregistered it everywhere.
**************************************************************************/
void unit_virtual_destroy(struct unit *punit)
{
  free_unit_orders(punit);

  /* Unload unit if transported. */
  unit_transport_unload(punit);
  fc_assert(!unit_transported(punit));

  /* Check for transported units. Use direct access to the list. */
  if (unit_list_size(punit->transporting) != 0) {
    /* Unload all units. */
    unit_list_iterate_safe(punit->transporting, pcargo) {
      unit_transport_unload(pcargo);
    } unit_list_iterate_safe_end;
  }
  fc_assert(unit_list_size(punit->transporting) == 0);

  if (punit->transporting) {
    unit_list_destroy(punit->transporting);
  }

  CALL_FUNC_EACH_AI(unit_free, punit);

  if (is_server() && punit->server.adv) {
    FC_FREE(punit->server.adv);
  } else {
    if (punit->client.act_prob_cache) {
      FC_FREE(punit->client.act_prob_cache);
    }
  }

  if (--punit->refcount <= 0) {
    FC_FREE(punit);
  }
}

/**********************************************************************//**
  Free and reset the unit's goto route (punit->pgr).  Only used by the
  server.
**************************************************************************/
void free_unit_orders(struct unit *punit)
{
  if (punit->has_orders) {
    punit->goto_tile = NULL;
    free(punit->orders.list);
    punit->orders.list = NULL;
  }
  punit->orders.length = 0;
  punit->has_orders = FALSE;
}

/**********************************************************************//**
  Return how many units are in the transport.
**************************************************************************/
int get_transporter_occupancy(const struct unit *ptrans)
{
  fc_assert_ret_val(ptrans, -1);

  return unit_list_size(ptrans->transporting);
}

/**********************************************************************//**
  Helper for transporter_for_unit() and transporter_for_unit_at()
**************************************************************************/
static struct unit *base_transporter_for_unit(const struct unit *pcargo,
                                              const struct tile *ptile,
                                              bool (*unit_load_test)
                                                  (const struct unit *pc,
                                                   const struct unit *pt))
{
  struct unit *best_trans = NULL;
  struct {
    bool has_orders, is_idle, can_freely_unload;
    int depth, outermost_moves_left, total_moves;
  } cur, best = { FALSE };

  unit_list_iterate(ptile->units, ptrans) {
    if (!unit_load_test(pcargo, ptrans)) {
      continue;
    } else if (best_trans == NULL) {
      best_trans = ptrans;
    }

    /* Gather data from transport stack in a single pass, for use in
     * various conditions below. */
    cur.has_orders = unit_has_orders(ptrans);
    cur.outermost_moves_left = ptrans->moves_left;
    cur.total_moves = ptrans->moves_left + unit_move_rate(ptrans);
    unit_transports_iterate(ptrans, ptranstrans) {
      if (unit_has_orders(ptranstrans)) {
        cur.has_orders = TRUE;
      }
      cur.outermost_moves_left = ptranstrans->moves_left;
      cur.total_moves += ptranstrans->moves_left + unit_move_rate(ptranstrans);
    } unit_transports_iterate_end;

    /* Criteria for deciding the 'best' transport to load onto.
     * The following tests are applied in order; earlier ones have
     * lexicographically greater significance than later ones. */

    /* Transports which have orders, or are on transports with orders,
     * are less preferable to transport stacks without orders (to
     * avoid loading on units that are just passing through). */
    if (best_trans != ptrans) {
      if (!cur.has_orders && best.has_orders) {
        best_trans = ptrans;
      } else if (cur.has_orders && !best.has_orders) {
        continue;
      }
    }

    /* Else, transports which are idle are preferable (giving players
     * some control over loading) -- this does not check transports
     * of transports. */
    cur.is_idle = (ptrans->activity == ACTIVITY_IDLE);
    if (best_trans != ptrans) {
      if (cur.is_idle && !best.is_idle) {
        best_trans = ptrans;
      } else if (!cur.is_idle && best.is_idle) {
        continue;
      }
    }

    /* Else, transports from which the cargo could unload at any time
     * are preferable to those where the cargo can only disembark in
     * cities/bases. */
    cur.can_freely_unload = utype_can_freely_unload(unit_type_get(pcargo),
                                                    unit_type_get(ptrans));
    if (best_trans != ptrans) {
      if (cur.can_freely_unload && !best.can_freely_unload) {
        best_trans = ptrans;
      } else if (!cur.can_freely_unload && best.can_freely_unload) {
        continue;
      }
    }

    /* Else, transports which are less deeply nested are preferable. */
    cur.depth = unit_transport_depth(ptrans);
    if (best_trans != ptrans) {
      if (cur.depth < best.depth) {
        best_trans = ptrans;
      } else if (cur.depth > best.depth) {
        continue;
      }
    }

    /* Else, transport stacks where the outermost transport has more
     * moves left are preferable (on the assumption that it's the
     * outermost transport that's about to move). */
    if (best_trans != ptrans) {
      if (cur.outermost_moves_left > best.outermost_moves_left) {
        best_trans = ptrans;
      } else if (cur.outermost_moves_left < best.outermost_moves_left) {
        continue;
      }
    }

    /* All other things being equal, as a tie-breaker, compare the total
     * moves left (this turn) and move rate (future turns) for the whole
     * stack, to take into account total potential movement for both
     * short and long journeys (we don't know which the cargo intends to
     * make). Doesn't try to account for whether transports can unload,
     * etc. */
    if (best_trans != ptrans) {
      if (cur.total_moves > best.total_moves) {
        best_trans = ptrans;
      } else {
        continue;
      }
    }

    fc_assert(best_trans == ptrans);
    best = cur;
  } unit_list_iterate_end;

  return best_trans;
}

/**********************************************************************//**
  Find the best transporter at the given location for the unit. See also
  unit_can_load() to test if there will be transport might be suitable
  for 'pcargo'.
**************************************************************************/
struct unit *transporter_for_unit(const struct unit *pcargo)
{
  return base_transporter_for_unit(pcargo, unit_tile(pcargo), can_unit_load);
}

/**********************************************************************//**
  Find the best transporter at the given location for the unit. See also
  unit_could_load_at() to test if there will be transport might be suitable
  for 'pcargo'.
**************************************************************************/
struct unit *transporter_for_unit_at(const struct unit *pcargo,
                                     const struct tile *ptile)
{
  return base_transporter_for_unit(pcargo, ptile, could_unit_load);
}

/**********************************************************************//**
  Check if unit of given type would be able to transport all of transport's
  cargo.
**************************************************************************/
static bool can_type_transport_units_cargo(const struct unit_type *utype,
                                           const struct unit *punit)
{
  if (get_transporter_occupancy(punit) > utype->transport_capacity) {
    return FALSE;
  }

  unit_list_iterate(punit->transporting, pcargo) {
    if (!can_unit_type_transport(utype, unit_class_get(pcargo))) {
      return FALSE;
    }
  } unit_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Tests if something prevents punit from being transformed to to_unittype
  where it is now, presuming its current position is valid.

  FIXME: The transport stack may still fail unit_transport_check()
  in result.
**************************************************************************/
enum unit_upgrade_result
unit_transform_result(const struct civ_map *nmap,
                      const struct unit *punit,
                      const struct unit_type *to_unittype)
{
  fc_assert_ret_val(NULL != to_unittype, UU_NO_UNITTYPE);

  if (!can_type_transport_units_cargo(to_unittype, punit)) {
    return UU_NOT_ENOUGH_ROOM;
  }

  if (punit->transporter != NULL) {
    if (!can_unit_type_transport(unit_type_get(punit->transporter),
                                 utype_class(to_unittype))) {
      return UU_UNSUITABLE_TRANSPORT;
    }
  } else if (!can_exist_at_tile(nmap, to_unittype, unit_tile(punit))) {
    /* The new unit type can't survive on this terrain. */
    return UU_NOT_TERRAIN;
  }

  return UU_OK;
}

/**********************************************************************//**
  Tests if the unit could be updated. Returns UU_OK if is this is
  possible.

  is_free should be set if the unit upgrade is "free" (e.g., Leonardo's).
  Otherwise money is needed and the unit must be in an owned city.

  Note that this function is strongly tied to unittools.c:transform_unit().
**************************************************************************/
enum unit_upgrade_result unit_upgrade_test(const struct civ_map *nmap,
                                           const struct unit *punit,
                                           bool is_free)
{
  struct player *pplayer = unit_owner(punit);
  const struct unit_type *to_unittype = can_upgrade_unittype(pplayer,
                                                             unit_type_get(punit));
  struct city *pcity;
  int cost;

  if (!to_unittype) {
    return UU_NO_UNITTYPE;
  }

  if (punit->activity == ACTIVITY_CONVERT) {
    /*  TODO: There may be other activities that the upgraded unit is not
        allowed to do, which we could also test.
        -
        If convert were legal for new unit_type we could allow, but then it
        has a 'head start' getting activity time from the old conversion. */
    return UU_NOT_ACTIVITY;
  }

  if (!is_free) {
    cost = unit_upgrade_price(pplayer, unit_type_get(punit), to_unittype);
    if (pplayer->economic.gold < cost) {
      return UU_NO_MONEY;
    }

    pcity = tile_city(unit_tile(punit));
    if (!pcity) {
      return UU_NOT_IN_CITY;
    }
    if (city_owner(pcity) != pplayer) {
      /* TODO: Should upgrades in allied cities be possible? */
      return UU_NOT_CITY_OWNER;
    }
  }

  /* TODO: Allow transported units to be reassigned. Check here
   * and make changes to upgrade_unit. */
  return unit_transform_result(nmap, punit, to_unittype);
}

/**********************************************************************//**
  Tests if unit can be converted to another type.
**************************************************************************/
bool unit_can_convert(const struct civ_map *nmap,
                      const struct unit *punit)
{
  const struct unit_type *tgt = unit_type_get(punit)->converted_to;

  if (tgt == NULL) {
    return FALSE;
  }

  return UU_OK == unit_transform_result(nmap, punit, tgt);
}

/**********************************************************************//**
  Find the result of trying to upgrade the unit, and a message that
  most callers can use directly.
**************************************************************************/
enum unit_upgrade_result unit_upgrade_info(const struct civ_map *nmap,
                                           const struct unit *punit,
                                           char *buf, size_t bufsz)
{
  struct player *pplayer = unit_owner(punit);
  enum unit_upgrade_result result = unit_upgrade_test(nmap, punit, FALSE);
  int upgrade_cost;
  const struct unit_type *from_unittype = unit_type_get(punit);
  const struct unit_type *to_unittype = can_upgrade_unittype(pplayer,
                                                             unit_type_get(punit));
  char tbuf[MAX_LEN_MSG];

  fc_snprintf(tbuf, ARRAY_SIZE(tbuf), PL_("Treasury contains %d gold.",
                                          "Treasury contains %d gold.",
                                          pplayer->economic.gold),
              pplayer->economic.gold);

  switch (result) {
  case UU_OK:
    upgrade_cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    /* This message is targeted toward the GUI callers. */
    /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
    fc_snprintf(buf, bufsz, PL_("Upgrade %s to %s for %d gold?\n%s",
                                "Upgrade %s to %s for %d gold?\n%s",
                                upgrade_cost),
                utype_name_translation(from_unittype),
                utype_name_translation(to_unittype),
                upgrade_cost, tbuf);
    break;
  case UU_NO_UNITTYPE:
    fc_snprintf(buf, bufsz,
                _("Sorry, cannot upgrade %s (yet)."),
                utype_name_translation(from_unittype));
    break;
  case UU_NO_MONEY:
    upgrade_cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    /* TRANS: Last %s is pre-pluralised "Treasury contains %d gold." */
    fc_snprintf(buf, bufsz, PL_("Upgrading %s to %s costs %d gold.\n%s",
                                "Upgrading %s to %s costs %d gold.\n%s",
                                upgrade_cost),
                utype_name_translation(from_unittype),
                utype_name_translation(to_unittype),
                upgrade_cost, tbuf);
    break;
  case UU_NOT_IN_CITY:
  case UU_NOT_CITY_OWNER:
    fc_snprintf(buf, bufsz,
                _("You can only upgrade units in your cities."));
    break;
  case UU_NOT_ENOUGH_ROOM:
    fc_snprintf(buf, bufsz,
                _("Upgrading this %s would strand units it transports."),
                utype_name_translation(from_unittype));
    break;
  case UU_NOT_TERRAIN:
    fc_snprintf(buf, bufsz,
                _("Upgrading this %s would result in a %s which can not "
                  "survive at this place."),
                utype_name_translation(from_unittype),
                utype_name_translation(to_unittype));
    break;
  case UU_UNSUITABLE_TRANSPORT:
    fc_snprintf(buf, bufsz,
                _("Upgrading this %s would result in a %s which its "
                  "current transport, %s, could not transport."),
                utype_name_translation(from_unittype),
                utype_name_translation(to_unittype),
                unit_name_translation(punit->transporter));
    break;
  case UU_NOT_ACTIVITY:
    fc_snprintf(buf, bufsz,
                _("Cannot upgrade %s while doing '%s'."),
                utype_name_translation(from_unittype),
                unit_activity_name(punit->activity));
    break;
  }

  return result;
}

/**********************************************************************//**
  Returns the amount of movement points successfully performing the
  specified action will consume in the actor unit.
**************************************************************************/
int unit_pays_mp_for_action(const struct action *paction,
                            const struct unit *punit)
{
  int mpco;

  mpco = get_target_bonus_effects(NULL,
                                  &(const struct req_context) {
                                    .player = unit_owner(punit),
                                    .city = unit_tile(punit)
                                            ? tile_city(unit_tile(punit))
                                            : NULL,
                                    .tile = unit_tile(punit),
                                    .unit = punit,
                                    .unittype = unit_type_get(punit),
                                    .action = paction,
                                  },
                                  NULL,
                                  EFT_ACTION_SUCCESS_MOVE_COST);

  mpco += utype_pays_mp_for_action_base(paction, unit_type_get(punit));

  return mpco;
}

/**********************************************************************//**
  Returns how many hp's a unit will gain standing still on current tile.
**************************************************************************/
int hp_gain_coord(const struct unit *punit)
{
  int hp = 0;
  const int base = unit_type_get(punit)->hp;
  int min = base * get_unit_bonus(punit, EFT_MIN_HP_PCT) / 100;

  /* Includes barracks (100%), fortress (25%), etc. */
  hp += base * get_unit_bonus(punit, EFT_HP_REGEN) / 100;

  /* Minimum HP after regen effects applied. */
  hp = MAX(hp, min);

  /* Regen2 effects that apply after there's at least Min HP */
  hp += ceil(base / 10) * get_unit_bonus(punit, EFT_HP_REGEN_2) / 10;

  return MAX(hp, 0);
}

/**********************************************************************//**
  How many hitpoints does unit recover?
**************************************************************************/
int unit_gain_hitpoints(const struct unit *punit)
{
  const struct unit_type *utype = unit_type_get(punit);
  struct unit_class *pclass = unit_class_get(punit);
  struct city *pcity = tile_city(unit_tile(punit));
  int gain;

  if (!punit->moved) {
    gain = hp_gain_coord(punit);
  } else {
    gain = 0;
  }

  gain += get_unit_bonus(punit, EFT_UNIT_RECOVER);

  if (!punit->homecity && 0 < game.server.killunhomed
      && !unit_has_type_flag(punit, UTYF_GAMELOSS)) {
    /* Hit point loss of units without homecity; at least 1 hp! */
    /* Gameloss units are immune to this effect. */
    int hp_loss = MAX(utype->hp * game.server.killunhomed / 100, 1);

    if (gain > hp_loss) {
      gain = -1;
    } else {
      gain -= hp_loss;
    }
  }

  if (pcity == NULL && !tile_has_native_base(unit_tile(punit), utype)
      && !unit_transported(punit)) {
    gain -= utype->hp * pclass->hp_loss_pct / 100;
  }

  if (punit->hp + gain > utype->hp) {
    gain = utype->hp - punit->hp;
  } else if (punit->hp + gain < 0) {
    gain = -punit->hp;
  }

  return gain;
}

/**********************************************************************//**
  Does unit lose hitpoints each turn?
**************************************************************************/
bool is_losing_hp(const struct unit *punit)
{
  const struct unit_type *punittype = unit_type_get(punit);

  return get_unit_bonus(punit, EFT_UNIT_RECOVER)
    < (punittype->hp *
       utype_class(punittype)->hp_loss_pct / 100);
}

/**********************************************************************//**
  Does unit lose hitpoints each turn?
**************************************************************************/
bool unit_type_is_losing_hp(const struct player *pplayer,
                            const struct unit_type *punittype)
{
  return get_unittype_bonus(pplayer, NULL, punittype, NULL,
                            EFT_UNIT_RECOVER)
    < (punittype->hp *
       utype_class(punittype)->hp_loss_pct / 100);
}

/**********************************************************************//**
  Check if unit with given id is still alive. Use this before using
  old unit pointers when unit might have died.
**************************************************************************/
bool unit_is_alive(int id)
{
  /* Check if unit exist in game */
  if (game_unit_by_number(id)) {
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Return TRUE if this is a valid unit pointer but does not correspond to
  any unit that exists in the game.

  NB: A return value of FALSE implies that either the pointer is NULL or
  that the unit exists in the game.
**************************************************************************/
bool unit_is_virtual(const struct unit *punit)
{
  if (!punit) {
    return FALSE;
  }

  return punit != game_unit_by_number(punit->id);
}

/**********************************************************************//**
  Return pointer to ai data of given unit and ai type.
**************************************************************************/
void *unit_ai_data(const struct unit *punit, const struct ai_type *ai)
{
  return punit->server.ais[ai_type_number(ai)];
}

/**********************************************************************//**
  Attach ai data to unit
**************************************************************************/
void unit_set_ai_data(struct unit *punit, const struct ai_type *ai,
                      void *data)
{
  punit->server.ais[ai_type_number(ai)] = data;
}

/**********************************************************************//**
  Calculate how expensive it is to bribe the unit. The cost depends on the
  distance to the capital, the owner's treasury, and the build cost of the
  unit. For a damaged unit the price is reduced. For a veteran unit, it is
  increased.

  @param  punit       Unit to bribe
  @param  briber      Player that wants to bribe
  @param  briber_unit Unit that does the bribing
  @return             Bribe cost
**************************************************************************/
int unit_bribe_cost(const struct unit *punit, const struct player *briber,
                    const struct unit *briber_unit)
{
  int cost, default_hp;
  struct tile *ptile = unit_tile(punit);
  struct player *owner = unit_owner(punit);
  const struct unit_type *ptype = unit_type_get(punit);

  fc_assert_ret_val(punit != NULL, 0);

  default_hp = ptype->hp;

  cost = game.info.base_bribe_cost;

  if (owner != nullptr) {
    int dist = 0;

    cost += owner->economic.gold;

    /* Consider the distance to the capital. */
    dist = GAME_UNIT_BRIBE_DIST_MAX;
    city_list_iterate(owner->cities, capital) {
      if (is_capital(capital)) {
        int tmp = map_distance(capital->tile, ptile);

        if (tmp < dist) {
          dist = tmp;
        }
      }
    } city_list_iterate_end;

    cost /= dist + 2;
  }

  /* Consider the build cost. */
  cost *= unit_build_shield_cost_base(punit) / 10.0;

  /* Rule set specific cost modification */
  cost += (cost * get_target_bonus_effects(NULL,
                    &(const struct req_context) {
                      .player = owner,
                      .city = game_city_by_number(punit->homecity),
                      .tile = ptile,
                      .unit = punit,
                      .unittype = ptype,
                    },
                    &(const struct req_context) {
                      .player = briber,
                      .unit = briber_unit,
                      .unittype = briber_unit ? unit_type_get(briber_unit)
                                              : nullptr,
                      .tile = briber_unit ? unit_tile(briber_unit)
                                          : nullptr,
                      .city = briber_unit
                        ? game_city_by_number(briber_unit->homecity)
                        : nullptr,
                    },
                    EFT_UNIT_BRIBE_COST_PCT))
       / 100;

  /* Veterans are not cheap. */
  {
    const struct veteran_level *vlevel
      = utype_veteran_level(ptype, punit->veteran);

    fc_assert_ret_val(vlevel != NULL, 0);
    cost = cost * vlevel->power_fact / 100;
    if (ptype->move_rate > 0) {
      cost += cost * vlevel->move_bonus / ptype->move_rate;
    } else {
      cost += cost * vlevel->move_bonus / SINGLE_MOVE;
    }
  }

  /* Cost now contains the basic bribe cost. We now reduce it by:
   *    bribecost = cost / 2 + cost / 2 * damage / hp
   *              = cost / 2 * (1 + damage / hp) */
  return ((float)cost / 2 * (1.0 + (float)punit->hp / default_hp));
}

/**********************************************************************//**
  Calculate how expensive it is to bribe entire unit stack.

  @param  ptile       Tile to bribe units from
  @param  briber      Player that wants to bribe
  @param  briber_unit Unit that does the bribing
  @return             Bribe cost
**************************************************************************/
int stack_bribe_cost(const struct tile *ptile, const struct player *briber,
                     const struct unit *briber_unit)
{
  int bribe_cost = 0;

  unit_list_iterate(ptile->units, pbribed) {
    bribe_cost += unit_bribe_cost(pbribed, briber, briber_unit);
  } unit_list_iterate_end;

  return bribe_cost;
}

/**********************************************************************//**
  Load pcargo onto ptrans. Returns TRUE on success.
**************************************************************************/
bool unit_transport_load(struct unit *pcargo, struct unit *ptrans, bool force)
{
  fc_assert_ret_val(ptrans != NULL, FALSE);
  fc_assert_ret_val(pcargo != NULL, FALSE);

  fc_assert_ret_val(!unit_list_search(ptrans->transporting, pcargo), FALSE);

  if (force || can_unit_load(pcargo, ptrans)) {
    pcargo->transporter = ptrans;
    unit_list_append(ptrans->transporting, pcargo);

    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Unload pcargo from ptrans. Returns TRUE on success.
**************************************************************************/
bool unit_transport_unload(struct unit *pcargo)
{
  struct unit *ptrans;

  fc_assert_ret_val(pcargo != NULL, FALSE);

  if (!unit_transported(pcargo)) {
    /* 'pcargo' is not transported. */
    return FALSE;
  }

  /* Get the transporter; must not be defined on the client! */
  ptrans = unit_transport_get(pcargo);
  if (ptrans) {
    /* 'pcargo' and 'ptrans' should be on the same tile. */
    fc_assert(same_pos(unit_tile(pcargo), unit_tile(ptrans)));

#ifndef FREECIV_NDEBUG
    bool success =
#endif
      unit_list_remove(ptrans->transporting, pcargo);

    /* It is an error if 'pcargo' can not be removed from the 'ptrans'. */
    fc_assert(success);
  }

  /* For the server (also safe for the client). */
  pcargo->transporter = NULL;

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE iff the unit is transported.
**************************************************************************/
bool unit_transported(const struct unit *pcargo)
{
  fc_assert_ret_val(pcargo != NULL, FALSE);

  /* The unit is transported if a transporter unit is set or, (for the client)
   * if the transported_by field is set. */
  if (is_server()) {
    return unit_transported_server(pcargo);
  } else {
    return unit_transported_client(pcargo);
  }
}

/**********************************************************************//**
  Returns the transporter of the unit or NULL if it is not transported.
**************************************************************************/
struct unit *unit_transport_get(const struct unit *pcargo)
{
  fc_assert_ret_val(pcargo != NULL, NULL);

  return pcargo->transporter;
}

/**********************************************************************//**
  Returns the list of cargo units.
**************************************************************************/
struct unit_list *unit_transport_cargo(const struct unit *ptrans)
{
  fc_assert_ret_val(ptrans != NULL, NULL);
  fc_assert_ret_val(ptrans->transporting != NULL, NULL);

  return ptrans->transporting;
}

/**********************************************************************//**
  Helper for unit_transport_check().
**************************************************************************/
static inline bool
unit_transport_check_one(const struct unit_type *cargo_utype,
                         const struct unit_type *trans_utype)
{
  return (trans_utype != cargo_utype
          && !can_unit_type_transport(cargo_utype,
                                      utype_class(trans_utype)));
}

/**********************************************************************//**
  Returns whether 'pcargo' in 'ptrans' is a valid transport. Note that
  'pcargo' can already be (but doesn't need) loaded into 'ptrans'.

  It may fail if one of the cargo unit has the same type of one of the
  transporter unit or if one of the cargo unit can transport one of
  the transporters.
**************************************************************************/
bool unit_transport_check(const struct unit *pcargo,
                          const struct unit *ptrans)
{
  const struct unit_type *cargo_utype = unit_type_get(pcargo);

  /* Check 'pcargo' against 'ptrans'. */
  if (!unit_transport_check_one(cargo_utype, unit_type_get(ptrans))) {
    return FALSE;
  }

  /* Check 'pcargo' against 'ptrans' parents. */
  unit_transports_iterate(ptrans, pparent) {
    if (!unit_transport_check_one(cargo_utype, unit_type_get(pparent))) {
      return FALSE;
    }
  } unit_transports_iterate_end;

  /* Check cargo children... */
  unit_cargo_iterate(pcargo, pchild) {
    cargo_utype = unit_type_get(pchild);

    /* ...against 'ptrans'. */
    if (!unit_transport_check_one(cargo_utype, unit_type_get(ptrans))) {
      return FALSE;
    }

    /* ...and against 'ptrans' parents. */
    unit_transports_iterate(ptrans, pparent) {
      if (!unit_transport_check_one(cargo_utype, unit_type_get(pparent))) {
        return FALSE;
      }
    } unit_transports_iterate_end;
  } unit_cargo_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns whether 'pcargo' is transported by 'ptrans', either directly
  or indirectly.
**************************************************************************/
bool unit_contained_in(const struct unit *pcargo, const struct unit *ptrans)
{
  unit_transports_iterate(pcargo, plevel) {
    if (ptrans == plevel) {
      return TRUE;
    }
  } unit_transports_iterate_end;
  return FALSE;
}

/**********************************************************************//**
  Returns the number of unit cargo layers within transport 'ptrans'.
**************************************************************************/
int unit_cargo_depth(const struct unit *ptrans)
{
  struct cargo_iter iter;
  struct iterator *it;
  int depth = 0;

  for (it = cargo_iter_init(&iter, ptrans); iterator_valid(it);
       iterator_next(it)) {
    if (iter.depth > depth) {
      depth = iter.depth;
    }
  }
  return depth;
}

/**********************************************************************//**
  Returns the number of unit transport layers which carry unit 'pcargo'.
**************************************************************************/
int unit_transport_depth(const struct unit *pcargo)
{
  int level = 0;

  unit_transports_iterate(pcargo, plevel) {
    level++;
  } unit_transports_iterate_end;
  return level;
}

/**********************************************************************//**
  Returns the size of the unit cargo iterator.
**************************************************************************/
size_t cargo_iter_sizeof(void)
{
  return sizeof(struct cargo_iter);
}

/**********************************************************************//**
  Get the unit of the cargo iterator.
**************************************************************************/
static void *cargo_iter_get(const struct iterator *it)
{
  const struct cargo_iter *iter = CARGO_ITER(it);

  return unit_list_link_data(iter->links[iter->depth - 1]);
}

/**********************************************************************//**
  Try to find next unit for the cargo iterator.
**************************************************************************/
static void cargo_iter_next(struct iterator *it)
{
  struct cargo_iter *iter = CARGO_ITER(it);
  const struct unit_list_link *piter;
  const struct unit_list_link *pnext;

  /* Variant 1: unit has cargo. */
  pnext = unit_list_head(unit_transport_cargo(cargo_iter_get(it)));
  if (NULL != pnext) {
    fc_assert(iter->depth < ARRAY_SIZE(iter->links));
    iter->links[iter->depth++] = pnext;
    return;
  }

  fc_assert(iter->depth > 0);

  while (iter->depth > 0) {
    piter = iter->links[iter->depth - 1];

    /* Variant 2: there are other cargo units at same level. */
    pnext = unit_list_link_next(piter);
    if (NULL != pnext) {
      iter->links[iter->depth - 1] = pnext;
      return;
    }

    /* Variant 3: return to previous level, and do same tests. */
    iter->depth--;
  }
}

/**********************************************************************//**
  Return whether the iterator is still valid.
**************************************************************************/
static bool cargo_iter_valid(const struct iterator *it)
{
  return (0 < CARGO_ITER(it)->depth);
}

/**********************************************************************//**
  Initialize the cargo iterator.
**************************************************************************/
struct iterator *cargo_iter_init(struct cargo_iter *iter,
                                 const struct unit *ptrans)
{
  struct iterator *it = ITERATOR(iter);

  it->get = cargo_iter_get;
  it->next = cargo_iter_next;
  it->valid = cargo_iter_valid;
  iter->links[0] = unit_list_head(unit_transport_cargo(ptrans));
  iter->depth = (NULL != iter->links[0] ? 1 : 0);

  return it;
}

/**********************************************************************//**
  Is a cityfounder unit?
**************************************************************************/
bool unit_is_cityfounder(const struct unit *punit)
{
  return utype_is_cityfounder(unit_type_get(punit));
}

/**********************************************************************//**
  Returns TRUE iff the unit order array is sane.
**************************************************************************/
bool unit_order_list_is_sane(const struct civ_map *nmap,
                             int length, const struct unit_order *orders)
{
  int i;

  for (i = 0; i < length; i++) {
    struct action *paction;
    struct extra_type *pextra;

    if (orders[i].order > ORDER_LAST) {
      log_error("invalid order %d at index %d", orders[i].order, i);
      return FALSE;
    }
    switch (orders[i].order) {
    case ORDER_MOVE:
    case ORDER_ACTION_MOVE:
      if (!map_untrusted_dir_is_valid(orders[i].dir)) {
        log_error("in order %d, invalid move direction %d.", i, orders[i].dir);
        return FALSE;
      }
      break;
    case ORDER_ACTIVITY:
      switch (orders[i].activity) {
      case ACTIVITY_SENTRY:
        if (i != length - 1) {
          /* Only allowed as the last order. */
          log_error("activity %d is not allowed at index %d.", orders[i].activity,
                    i);
          return FALSE;
        }
        break;
      /* Replaced by action orders */
      case ACTIVITY_BASE:
      case ACTIVITY_GEN_ROAD:
      case ACTIVITY_CLEAN:
      case ACTIVITY_PILLAGE:
      case ACTIVITY_MINE:
      case ACTIVITY_IRRIGATE:
      case ACTIVITY_PLANT:
      case ACTIVITY_CULTIVATE:
      case ACTIVITY_TRANSFORM:
      case ACTIVITY_CONVERT:
      case ACTIVITY_FORTIFYING:
        log_error("at index %d, use action rather than activity %d.",
                  i, orders[i].activity);
        return FALSE;
      /* Not supported. */
      case ACTIVITY_EXPLORE:
      case ACTIVITY_IDLE:
      /* Not set from the client. */
      case ACTIVITY_GOTO:
      case ACTIVITY_FORTIFIED:
      /* Unused. */
      case ACTIVITY_LAST:
        log_error("at index %d, unsupported activity %d.", i, orders[i].activity);
        return FALSE;
      }

      break;
    case ORDER_PERFORM_ACTION:
      if (!action_id_exists(orders[i].action)) {
        /* Non-existent action. */
        log_error("at index %d, the action %d doesn't exist.", i, orders[i].action);
        return FALSE;
      }

      paction = action_by_number(orders[i].action);

      /* Validate main target. */
      if (index_to_tile(nmap, orders[i].target) == NULL) {
        log_error("at index %d, invalid tile target %d for the action %d.",
                  i, orders[i].target, orders[i].action);
        return FALSE;
      }

      if (orders[i].dir != DIR8_ORIGIN) {
        log_error("at index %d, the action %d sets the outdated target"
                  " specification dir.",
                  i, orders[i].action);
      }

      /* Validate sub target. */
      switch (action_id_get_sub_target_kind(orders[i].action)) {
      case ASTK_BUILDING:
        /* Sub target is a building. */
        if (!improvement_by_number(orders[i].sub_target)) {
          /* Sub target is invalid. */
          log_error("at index %d, cannot do %s without a target.", i,
                    action_id_rule_name(orders[i].action));
          return FALSE;
        }
        break;
      case ASTK_TECH:
        /* Sub target is a technology. */
        if (orders[i].sub_target == A_NONE
            || (!valid_advance_by_number(orders[i].sub_target)
                && orders[i].sub_target != A_FUTURE)) {
          /* Target tech is invalid. */
          log_error("at index %d, cannot do %s without a target.", i,
                    action_id_rule_name(orders[i].action));
          return FALSE;
        }
        break;
      case ASTK_EXTRA:
      case ASTK_EXTRA_NOT_THERE:
        /* Sub target is an extra. */
        pextra = (!(orders[i].sub_target == NO_TARGET
                    || (orders[i].sub_target < 0
                        || (orders[i].sub_target
                            >= game.control.num_extra_types)))
                  ? extra_by_number(orders[i].sub_target) : NULL);
        fc_assert(pextra == NULL || !(pextra->ruledit_disabled));
        if (pextra == NULL) {
          if (paction->target_complexity != ACT_TGT_COMPL_FLEXIBLE) {
            /* Target extra is invalid. */
            log_error("at index %d, cannot do %s without a target.", i,
                      action_id_rule_name(orders[i].action));
            return FALSE;
          }
        } else {
          if (!actres_removes_extra(paction->result, pextra)
              && !actres_creates_extra(paction->result, pextra)) {
            /* Target extra is irrelevant for the action. */
            log_error("at index %d, cannot do %s to %s.", i,
                      action_id_rule_name(orders[i].action),
                      extra_rule_name(pextra));
            return FALSE;
          }
        }
        break;
      case ASTK_SPECIALIST:
        if (!specialist_by_number(orders[i].sub_target)) {
          log_error("at index %d, cannot do %s without a target.", i,
                    action_id_rule_name(orders[i].action));
          return FALSE;
        }
        break;
      case ASTK_NONE:
        /* No validation required. */
        break;
      /* Invalid action? */
      case ASTK_COUNT:
        fc_assert_ret_val_msg(
            action_id_get_sub_target_kind(orders[i].action) != ASTK_COUNT,
            FALSE,
            "Bad action %d in order number %d.", orders[i].action, i);
      }

      /* Some action orders are sane only in the last order. */
      if (i != length - 1) {
        /* If the unit is dead, */
        if (utype_is_consumed_by_action(paction, NULL)
            /* or if Freeciv has no idea where the unit will end up after it
             * has performed this action, */
            || !(utype_is_unmoved_by_action(paction, NULL)
                 || utype_is_moved_to_tgt_by_action(paction, NULL))
            /* or if the unit will end up standing still, */
            || action_has_result(paction, ACTRES_FORTIFY)) {
          /* than having this action in the middle of a unit's orders is
           * probably wrong. */
          log_error("action %d is not allowed at index %d.",
                    orders[i].action, i);
          return FALSE;
        }
      }

      /* Don't validate that the target tile really contains a target or
       * that the actor player's map think the target tile has one.
       * The player may target something from their player map that isn't
       * there any more, a target they think is there even if their player
       * map doesn't have it, or even a target they assume will be there
       * when the unit reaches the target tile.
       *
       * With that said: The client should probably at least have an
       * option to only aim city targeted actions at cities. */

      break;
    case ORDER_FULL_MP:
      break;
    case ORDER_LAST:
      /* An invalid order.  This is handled above. */
      break;
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Sanity-check unit order arrays from a packet and create a unit_order array
  from their contents if valid.
**************************************************************************/
struct unit_order *create_unit_orders(const struct civ_map *nmap,
                                      int length,
                                      const struct unit_order *orders)
{
  struct unit_order *unit_orders;

  if (!unit_order_list_is_sane(nmap, length, orders)) {
    return NULL;
  }

  unit_orders = fc_malloc(length * sizeof(*(unit_orders)));
  memcpy(unit_orders, orders, length * sizeof(*(unit_orders)));

  return unit_orders;
}

/**********************************************************************//**
  Action that matches activity. Currently dummy placeholder.
**************************************************************************/
enum gen_action activity_default_action(enum unit_activity act)
{
  enum gen_action act_act[ACTIVITY_LAST] = {
    ACTION_NONE,              // ACTIVITY_IDLE
    ACTION_CULTIVATE,         // ACTIVITY_CULTIVATE
    ACTION_MINE,              // ACTIVITY_MINE
    ACTION_IRRIGATE,          // ACTIVITY_IRRIGATE
    ACTION_FORTIFY,           // ACTIVITY_FORTIFIED
    ACTION_NONE,              // ACTIVITY_SENTRY
    ACTION_PILLAGE,           // ACTIVITY_PILLAGE
    ACTION_NONE,              // ACTIVITY_GOTO
    ACTION_NONE,              // ACTIVITY_EXPLORE
    ACTION_TRANSFORM_TERRAIN, // ACTIVITY_TRANSFORM
    ACTION_FORTIFY,           // ACTIVITY_FORTIFYING
    ACTION_CLEAN,             // ACTIVITY_CLEAN
    ACTION_BASE,              // ACTIVITY_BASE
    ACTION_ROAD,              // ACTIVITY_GEN_ROAD
    ACTION_CONVERT,           // ACTIVITY_CONVERT
    ACTION_PLANT              // ACTIVITY_PLANT
  };

  return act_act[act];
}

/**********************************************************************//**
  Get the upkeep of a unit.
  @param punit Unit to get upkeep for
  @param otype What kind of upkeep we are interested about
  @return Upkeep of the specified type
**************************************************************************/
int unit_upkeep_cost(const struct unit *punit, Output_type_id otype)
{
  return utype_upkeep_cost(unit_type_get(punit), punit, unit_owner(punit),
                           otype);
}
