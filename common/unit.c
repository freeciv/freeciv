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
#include "base.h"
#include "city.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "movement.h"
#include "packets.h"
#include "player.h"
#include "road.h"
#include "tech.h"
#include "unitlist.h"

#include "unit.h"

/**************************************************************************
bribe unit
investigate
poison
make revolt
establish embassy
sabotage city
**************************************************************************/

/**************************************************************************
Whether a diplomat can move to a particular tile and perform a
particular action there.
**************************************************************************/
bool diplomat_can_do_action(const struct unit *pdiplomat,
			    enum diplomat_actions action, 
			    const struct tile *ptile)
{
  if (!is_diplomat_action_available(pdiplomat, action, ptile)) {
    return FALSE;
  }

  if (!is_tiles_adjacent(unit_tile(pdiplomat), ptile)
      && !same_pos(unit_tile(pdiplomat), ptile)) {
    return FALSE;
  }

  if(pdiplomat->moves_left == 0)
    return FALSE;

  return TRUE;
}

/**************************************************************************
Whether a diplomat can perform a particular action at a particular
tile.  This does _not_ check whether the diplomat can move there.
If the action is DIPLOMAT_ANY_ACTION, checks whether there is any
action the diplomat can perform at the tile.
**************************************************************************/
bool is_diplomat_action_available(const struct unit *pdiplomat,
				  enum diplomat_actions action, 
				  const struct tile *ptile)
{
  struct city *pcity=tile_city(ptile);

  if (action != DIPLOMAT_MOVE
      && !can_unit_exist_at_tile(pdiplomat, unit_tile(pdiplomat))) {
    return FALSE;
  }

  if (pcity) {
    if (city_owner(pcity) != unit_owner(pdiplomat)
       && real_map_distance(unit_tile(pdiplomat), pcity->tile) <= 1) {
      if(action==DIPLOMAT_SABOTAGE)
	return pplayers_at_war(unit_owner(pdiplomat), city_owner(pcity));
      if(action==DIPLOMAT_MOVE)
        return pplayers_allied(unit_owner(pdiplomat), city_owner(pcity));
      if (action == DIPLOMAT_EMBASSY
          && !get_player_bonus(city_owner(pcity), EFT_NO_DIPLOMACY)
          && !player_has_real_embassy(unit_owner(pdiplomat),
                                      city_owner(pcity))) {
        return TRUE;
      }
      if(action == SPY_POISON
         && city_size_get(pcity) > 1
         && unit_has_type_flag(pdiplomat, F_SPY)) {
        return pplayers_at_war(unit_owner(pdiplomat), city_owner(pcity));
      }
      if(action==DIPLOMAT_INVESTIGATE)
        return TRUE;
      if (action == DIPLOMAT_STEAL && !is_barbarian(city_owner(pcity))) {
	return TRUE;
      }
      if(action==DIPLOMAT_INCITE)
        return !pplayers_allied(city_owner(pcity), unit_owner(pdiplomat));
      if(action==DIPLOMAT_ANY_ACTION)
        return TRUE;
    }
  } else { /* Action against a unit at a tile */
    /* If it is made possible to do action against allied units
       unit_move_handling() should be changed so that pdefender
       is also set to allied units */
    struct unit *punit;

    if ((action == SPY_SABOTAGE_UNIT || action == DIPLOMAT_ANY_ACTION) 
        && unit_list_size(ptile->units) == 1
        && unit_has_type_flag(pdiplomat, F_SPY)) {
      punit = unit_list_get(ptile->units, 0);
      if (pplayers_at_war(unit_owner(pdiplomat), unit_owner(punit))) {
        return TRUE;
      }
    }

    if ((action == DIPLOMAT_BRIBE || action == DIPLOMAT_ANY_ACTION)
        && unit_list_size(ptile->units) == 1) {
      punit = unit_list_get(ptile->units, 0);
      if (!pplayers_allied(unit_owner(punit), unit_owner(pdiplomat))) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

/****************************************************************************
  Determines if punit can be airlifted to dest_city now!  So punit needs
  to be in a city now.
  If pdest_city is NULL, just indicate whether it's possible for the unit
  to be airlifted at all from its current position.
  The 'restriction' parameter specifies which player's knowledge this is
  based on -- one player can't see whether another's cities are currently
  able to airlift.  (Clients other than global observers should only call
  this with a non-NULL 'restriction'.)
****************************************************************************/
enum unit_airlift_result
    test_unit_can_airlift_to(const struct player *restriction,
                             const struct unit *punit,
                             const struct city *pdest_city)
{
  const struct city *psrc_city = tile_city(unit_tile(punit));
  const struct player *punit_owner;
  enum unit_airlift_result ok_result = AR_OK;

  if (0 == punit->moves_left) {
    /* No moves left. */
    return AR_NO_MOVES;
  }

  if (!is_ground_unit(punit)) {
    /* Only land units can be airlifted currently. */
    return AR_WRONG_UNITTYPE;
  }

  if (0 < get_transporter_occupancy(punit)) {
    /* Units with occupants can't be airlifted currently. */
    return AR_OCCUPIED;
  }

  if (NULL == psrc_city) {
    /* No city there. */
    return AR_NOT_IN_CITY;
  }

  if (psrc_city == pdest_city) {
    /* Airlifting to our current position doesn't make sense. */
    return AR_BAD_DST_CITY;
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

  if (pdest_city &&
      punit_owner != city_owner(pdest_city)
      && !(game.info.airlifting_style & AIRLIFTING_ALLIED_DEST
           && pplayers_allied(punit_owner, city_owner(pdest_city)))) {
    /* Not allowed to airlift to this destination. */
    return AR_BAD_DST_CITY;
  }

  if (NULL == restriction || city_owner(psrc_city) == restriction) {
    /* We know for sure whether or not src can airlift this turn. */
    if (0 >= psrc_city->airlift) {
      /* The source cannot airlift for this turn (maybe already airlifted
       * or no airport).
       *
       * Note that (game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)
       * is not handled here because it always needs an airport to airlift.
       * See also do_airline() in server/unittools.h. */
      return AR_SRC_NO_FLIGHTS;
    } /* else, there is capacity; continue to other checks */
  } else {
    /* We don't have access to the 'airlift' field. Assume it's OK; can
     * only find out for sure by trying it. */
    ok_result = AR_OK_SRC_UNKNOWN;
  }

  if (pdest_city) {
    if (NULL == restriction || city_owner(pdest_city) == restriction) {
      if (0 >= pdest_city->airlift
          && !(game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)) {
        /* The destination cannot support airlifted units for this turn
         * (maybe already airlifed or no airport).
         * See also do_airline() in server/unittools.h. */
        return AR_DST_NO_FLIGHTS;
      } /* else continue */
    } else {
      ok_result = AR_OK_DST_UNKNOWN;
    }
  }

  return ok_result;
}

/****************************************************************************
  Encapsulates whether a return from test_unit_can_airlift_to() should be
  treated as a successful result.
****************************************************************************/
bool is_successful_airlift_result(enum unit_airlift_result result)
{
  switch (result) {
  case AR_OK:
  case AR_OK_SRC_UNKNOWN:
  case AR_OK_DST_UNKNOWN:
    return TRUE;
  default:  /* everything else is failure */
    return FALSE;
  }
}

/****************************************************************************
  Determines if punit can be airlifted to dest_city now!  So punit needs
  to be in a city now.
  On the server this gives correct information; on the client it errs on the
  side of saying airlifting is possible even if it's not certain given
  player knowledge.
****************************************************************************/
bool unit_can_airlift_to(const struct unit *punit,
                         const struct city *pdest_city)
{
  /* FIXME: really we want client_player(), not unit_owner(). */
  struct player *restriction = is_server() ? NULL : unit_owner(punit);
  fc_assert_ret_val(pdest_city, FALSE);
  return is_successful_airlift_result(
      test_unit_can_airlift_to(restriction, punit, pdest_city));
}

/****************************************************************************
  Return TRUE iff the unit is following client-side orders.
****************************************************************************/
bool unit_has_orders(const struct unit *punit)
{
  return punit->has_orders;
}

/**************************************************************************
  Return TRUE iff this unit can be disbanded at the given city to get full
  shields for building a wonder.
**************************************************************************/
bool unit_can_help_build_wonder(const struct unit *punit,
				const struct city *pcity)
{
  if (!is_tiles_adjacent(unit_tile(punit), pcity->tile)
      && !same_pos(unit_tile(punit), pcity->tile)) {
    return FALSE;
  }

  return (unit_has_type_flag(punit, F_HELP_WONDER)
	  && unit_owner(punit) == city_owner(pcity)
	  && VUT_IMPROVEMENT == pcity->production.kind
	  && is_wonder(pcity->production.value.building)
	  && (pcity->shield_stock
	      < impr_build_shield_cost(pcity->production.value.building)));
}


/**************************************************************************
  Return TRUE iff this unit can be disbanded at its current position to
  get full shields for building a wonder.
**************************************************************************/
bool unit_can_help_build_wonder_here(const struct unit *punit)
{
  struct city *pcity = tile_city(unit_tile(punit));

  return pcity && unit_can_help_build_wonder(punit, pcity);
}


/**************************************************************************
  Return TRUE iff this unit can be disbanded at its current location to
  provide a trade route from the homecity to the target city.
**************************************************************************/
bool unit_can_est_trade_route_here(const struct unit *punit)
{
  struct city *phomecity, *pdestcity;

  return (unit_has_type_flag(punit, F_TRADE_ROUTE)
          && (pdestcity = tile_city(unit_tile(punit)))
          && (phomecity = game_city_by_number(punit->homecity))
          && can_cities_trade(phomecity, pdestcity));
}

/**************************************************************************
  Return the number of units the transporter can hold (or 0).
**************************************************************************/
int get_transporter_capacity(const struct unit *punit)
{
  return unit_type(punit)->transport_capacity;
}

/**************************************************************************
  Is the unit capable of attacking?
**************************************************************************/
bool is_attack_unit(const struct unit *punit)
{
  return (unit_type(punit)->attack_strength > 0);
}

/**************************************************************************
  Military units are capable of enforcing martial law. Military ground
  and heli units can occupy empty cities -- see unit_can_take_over(punit).
  Some military units, like the Galleon, have no attack strength.
**************************************************************************/
bool is_military_unit(const struct unit *punit)
{
  return !unit_has_type_flag(punit, F_CIVILIAN);
}

/**************************************************************************
  Return TRUE iff this unit is a diplomat (spy) unit.  Diplomatic units
  can do diplomatic actions (not to be confused with diplomacy).
**************************************************************************/
bool is_diplomat_unit(const struct unit *punit)
{
  return (unit_has_type_flag(punit, F_DIPLOMAT));
}

/**************************************************************************
  Return TRUE iff the player should consider this unit to be a threat on
  the ground.
**************************************************************************/
static bool is_ground_threat(const struct player *pplayer,
			     const struct unit *punit)
{
  return (pplayers_at_war(pplayer, unit_owner(punit))
	  && (unit_has_type_flag(punit, F_DIPLOMAT)
	      || (is_ground_unit(punit) && is_military_unit(punit))));
}

/**************************************************************************
  Return TRUE iff this tile is threatened from any threatening ground unit
  within 2 tiles.
**************************************************************************/
bool is_square_threatened(const struct player *pplayer,
			  const struct tile *ptile)
{
  square_iterate(ptile, 2, ptile1) {
    unit_list_iterate(ptile1->units, punit) {
      if (is_ground_threat(pplayer, punit)) {
	return TRUE;
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
  This checks the "field unit" flag on the unit.  Field units cause
  unhappiness (under certain governments) even when they aren't abroad.
**************************************************************************/
bool is_field_unit(const struct unit *punit)
{
  return unit_has_type_flag(punit, F_FIELDUNIT);
}


/**************************************************************************
  Is the unit one that is invisible on the map. A unit is invisible if
  it has the F_PARTIAL_INVIS flag or if it transported by a unit with
  this flag.
**************************************************************************/
bool is_hiding_unit(const struct unit *punit)
{
  return (unit_has_type_flag(punit, F_PARTIAL_INVIS)
          || (unit_transported(punit)
              && unit_has_type_flag(unit_transport_get(punit),
                                    F_PARTIAL_INVIS)));
}

/**************************************************************************
  Return TRUE iff an attack from this unit would kill a citizen in a city
  (city walls protect against this).
**************************************************************************/
bool kills_citizen_after_attack(const struct unit *punit)
{
  return TEST_BIT(game.info.killcitizen, 
                  uclass_move_type(unit_class(punit)));
}

/****************************************************************************
  Return TRUE iff this unit may be disbanded to add its pop_cost to a
  city at its current location.
****************************************************************************/
bool unit_can_add_to_city(const struct unit *punit)
{
  return (UAB_ADD_OK == unit_add_or_build_city_test(punit));
}

/****************************************************************************
  Return TRUE iff this unit is capable of building a new city at its
  current location.
****************************************************************************/
bool unit_can_build_city(const struct unit *punit)
{
  return (UAB_BUILD_OK == unit_add_or_build_city_test(punit));
}

/****************************************************************************
  Return TRUE iff this unit can add to a current city or build a new city
  at its current location.
****************************************************************************/
bool unit_can_add_or_build_city(const struct unit *punit)
{
  enum unit_add_build_city_result res = unit_add_or_build_city_test(punit);

  return (UAB_BUILD_OK == res || UAB_ADD_OK == res);
}

/****************************************************************************
  See if the unit can add to an existing city or build a new city at
  its current location, and return a 'result' value telling what is
  allowed.
****************************************************************************/
enum unit_add_build_city_result
unit_add_or_build_city_test(const struct unit *punit)
{
  struct tile *ptile = unit_tile(punit);
  struct city *pcity = tile_city(ptile);
  bool is_build = unit_has_type_flag(punit, F_CITIES);
  bool is_add = unit_has_type_flag(punit, F_ADD_TO_CITY);
  int new_pop;

  /* Test if we can build. */
  if (NULL == pcity) {
    if (!is_build) {
      return UAB_NOT_BUILD_UNIT;
    }
    if (punit->moves_left == 0) {
      return UAB_NO_MOVES_BUILD;
    }
    switch (city_build_here_test(ptile, punit)) {
    case CB_OK:
      return UAB_BUILD_OK;
    case CB_BAD_CITY_TERRAIN:
      return UAB_BAD_CITY_TERRAIN;
    case CB_BAD_UNIT_TERRAIN:
      return UAB_BAD_UNIT_TERRAIN;
    case CB_BAD_BORDERS:
      return UAB_BAD_BORDERS;
    case CB_NO_MIN_DIST:
      return UAB_NO_MIN_DIST;
    }
    log_error("%s(): Internal error.", __FUNCTION__);
    return UAB_NO_MOVES_BUILD; /* Returns something prohibitive. */
  }

  /* Test if we can add. */
  if (!is_add) {
    return UAB_NOT_ADDABLE_UNIT;
  }
  if (punit->moves_left == 0) {
    return UAB_NO_MOVES_ADD;
  }

  fc_assert(unit_pop_value(punit) > 0);
  new_pop = city_size_get(pcity) + unit_pop_value(punit);

  if (new_pop > game.info.add_to_size_limit) {
    return UAB_TOO_BIG;
  }
  if (city_owner(pcity) != unit_owner(punit)) {
    return UAB_NOT_OWNER;
  }
  if (!city_can_grow_to(pcity, new_pop)) {
    return UAB_NO_SPACE;
  }
  return UAB_ADD_OK;
}

/**************************************************************************
  Return TRUE iff the unit can change homecity to the given city.
**************************************************************************/
bool can_unit_change_homecity_to(const struct unit *punit,
				 const struct city *pcity)
{
  struct city *acity = tile_city(unit_tile(punit));

  /* Requirements to change homecity:
   *
   * 1. Homeless units can't change homecity (this is a feature since
   *    being homeless is a big benefit).
   * 2. The unit must be inside the city it is rehoming to.
   * 3. Of course you can only have your own cities as homecity.
   * 4. You can't rehome to the current homecity. */
  return (punit && pcity
	  && punit->homecity > 0
	  && acity
	  && city_owner(acity) == unit_owner(punit)
	  && punit->homecity != acity->id);
}

/**************************************************************************
  Return TRUE iff the unit can change homecity at its current location.
**************************************************************************/
bool can_unit_change_homecity(const struct unit *punit)
{
  return can_unit_change_homecity_to(punit, tile_city(unit_tile(punit)));
}

/**************************************************************************
  Returns the speed of a unit doing an activity.  This depends on the
  veteran level and the base move_rate of the unit (regardless of HP or
  effects).  Usually this is just used for settlers but the value is also
  used for military units doing fortify/pillage activities.

  The speed is multiplied by ACTIVITY_COUNT.
**************************************************************************/
int get_activity_rate(const struct unit *punit)
{
  const struct veteran_level *vlevel;

  fc_assert_ret_val(punit != NULL, 0);

  vlevel = utype_veteran_level(unit_type(punit), punit->veteran);
  fc_assert_ret_val(vlevel != NULL, 0);

  /* The speed of the settler depends on its base move_rate, not on
   * the number of moves actually remaining or the adjusted move rate.
   * This means sea formers won't have their activity rate increased by
   * Magellan's, and it means injured units work just as fast as
   * uninjured ones.  Note the value is never less than SINGLE_MOVE. */
  int move_rate = unit_type(punit)->move_rate;

  /* All settler actions are multiplied by ACTIVITY_COUNT. */
  return ACTIVITY_FACTOR
         * (float)vlevel->power_fact / 100
         * move_rate / SINGLE_MOVE;
}

/**************************************************************************
  Returns the amount of work a unit does (will do) on an activity this
  turn.  Units that have no MP do no work.

  The speed is multiplied by ACTIVITY_COUNT.
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

/**************************************************************************
  Return the estimated number of turns for the worker unit to start and
  complete the activity at the given location.  This assumes no other
  worker units are helping out, and doesn't take account of any work
  already done by this unit.
**************************************************************************/
int get_turns_for_activity_at(const struct unit *punit,
			      enum unit_activity activity,
			      const struct tile *ptile)
{
  /* FIXME: This is just an approximation since we don't account for
   * get_activity_rate_this_turn. */
  int speed = get_activity_rate(punit);
  int time = tile_activity_time(activity, ptile);

  if (time >= 0 && speed >= 0) {
    return (time - 1) / speed + 1; /* round up */
  } else {
    return FC_INFINITY;
  }
}

/**************************************************************************
  Return TRUE if activity requires some sort of target to be specified.
**************************************************************************/
bool activity_requires_target(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_PILLAGE:
  case ACTIVITY_BASE:
    return TRUE;
  case ACTIVITY_IDLE:
  case ACTIVITY_POLLUTION:
  case ACTIVITY_ROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_FALLOUT:
    return FALSE;
  /* These shouldn't be kicking around internally. */
  case ACTIVITY_FORTRESS:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_PATROL_UNUSED:
  default:
    fc_assert_ret_val(FALSE, FALSE);
  }
}

/**************************************************************************
  Return whether the unit can be put in auto-settler mode.

  NOTE: we used to have "auto" mode including autosettlers and auto-attack.
  This was bad because the two were indestinguishable even though they
  are very different.  Now auto-attack is done differently so we just have
  auto-settlers.  If any new auto modes are introduced they should be
  handled separately.
**************************************************************************/
bool can_unit_do_autosettlers(const struct unit *punit) 
{
  return unit_has_type_flag(punit, F_SETTLERS);
}

/**************************************************************************
  Return if given activity really is in game. For savegame compatibility
  activity enum cannot be reordered and there is holes in it.
**************************************************************************/
bool is_real_activity(enum unit_activity activity)
{
  /* ACTIVITY_FORTRESS and ACTIVITY_AIRBASE are deprecated */
  return (0 <= activity && activity < ACTIVITY_LAST)
          && activity != ACTIVITY_FORTRESS
          && activity != ACTIVITY_AIRBASE
          && activity != ACTIVITY_UNKNOWN
          && activity != ACTIVITY_PATROL_UNUSED;
}

/**************************************************************************
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
  case ACTIVITY_POLLUTION:
    return _("Pollution");
  case ACTIVITY_ROAD:
  case ACTIVITY_RAILROAD:
    return road_name_translation(road_by_activity(activity));
  case ACTIVITY_MINE:
    return _("Mine");
  case ACTIVITY_IRRIGATE:
    return _("Irrigation");
  case ACTIVITY_FORTIFYING:
    return _("Fortifying");
  case ACTIVITY_FORTIFIED:
    return _("Fortified");
  case ACTIVITY_FORTRESS:
    return _("Fortress");
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
  case ACTIVITY_AIRBASE:
    return _("Airbase");
  case ACTIVITY_FALLOUT:
    return _("Fallout");
  case ACTIVITY_BASE:
    return _("Base");
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);
  return _("Unknown");
}

/****************************************************************************
  Return TRUE iff the given unit could be loaded into the transporter
  if we moved there.
****************************************************************************/
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

  /* Only top-level transporters may be loaded or loaded into. */
  if (unit_transported(pcargo)
      || unit_transported(ptrans)) {
    return FALSE;
  }

  /* Check iff this is a valid transport. */
  if (unit_transported(pcargo)
      && !unit_transport_check(pcargo, ptrans)) {
    return FALSE;
  }

  /* Make sure this transporter can carry this type of unit. */
  if (!can_unit_transport(ptrans, pcargo)) {
    return FALSE;
  }

  /* Transporter must be native to the tile it is on (or it itself is
   * transported). */
  if (!can_unit_exist_at_tile(ptrans, unit_tile(ptrans))
      && !unit_transported(ptrans)) {
    return FALSE;
  }

  /* Make sure there's room in the transporter. */
  return (get_transporter_occupancy(ptrans)
	  < get_transporter_capacity(ptrans));
}

/****************************************************************************
  Return TRUE iff the given unit can be loaded into the transporter.
****************************************************************************/
bool can_unit_load(const struct unit *pcargo, const struct unit *ptrans)
{
  /* This function needs to check EVERYTHING. */

  /* Check positions of the units.  Of course you can't load a unit onto
   * a transporter on a different tile... */
  if (!same_pos(unit_tile(pcargo), unit_tile(ptrans))) {
    return FALSE;
  }

  return could_unit_load(pcargo, ptrans);
}

/****************************************************************************
  Return TRUE iff the given unit can be unloaded from its current
  transporter.

  This function checks everything *except* the legality of the position
  after the unloading.  The caller may also want to call
  can_unit_exist_at_tile() to check this, unless the unit is unloading and
  moving at the same time.
****************************************************************************/
bool can_unit_unload(const struct unit *pcargo, const struct unit *ptrans)
{
  if (!pcargo || !ptrans) {
    return FALSE;
  }

  /* Make sure the unit's transporter exists and is known. */
  if (unit_transport_get(pcargo) != ptrans) {
    return FALSE;
  }

  /* Only top-level transporters may be unloaded.  However the unit being
   * unloaded may be transporting other units (well, at least it's allowed
   * here: elsewhere this may be disallowed). */
  if (unit_transport_get(ptrans)) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Return whether the unit can be paradropped - that is, if the unit is in
  a friendly city or on an airbase special, has enough movepoints left, and
  has not paradropped yet this turn.
**************************************************************************/
bool can_unit_paradrop(const struct unit *punit)
{
  struct unit_type *utype;

  if (!unit_has_type_flag(punit, F_PARATROOPERS))
    return FALSE;

  if(punit->paradropped)
    return FALSE;

  utype = unit_type(punit);

  if(punit->moves_left < utype->paratroopers_mr_req)
    return FALSE;

  if (tile_has_base_flag(unit_tile(punit), BF_PARADROP_FROM)) {
    /* Paradrop has to be possible from non-native base.
     * Paratroopers are "Land" units, but they can paradrom from Airbase. */
    return TRUE;
  }

  if (!tile_city(unit_tile(punit))) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Return whether the unit can bombard.
  Basically if it is a bombarder, isn't being transported, and hasn't 
  moved this turn.
**************************************************************************/
bool can_unit_bombard(const struct unit *punit)
{
  if (!unit_has_type_flag(punit, F_BOMBARDER)) {
    return FALSE;
  }

  if (unit_transported(punit)) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Check if the unit's current activity is actually legal.
**************************************************************************/
bool can_unit_continue_current_activity(struct unit *punit)
{
  enum unit_activity current = punit->activity;
  enum tile_special_type target = punit->activity_target;
  Base_type_id base = punit->activity_base;
  enum unit_activity current2 = 
              (current == ACTIVITY_FORTIFIED) ? ACTIVITY_FORTIFYING : current;
  bool result;

  punit->activity = ACTIVITY_IDLE;
  punit->activity_target = S_LAST;
  punit->activity_base = BASE_NONE;

  result = can_unit_do_activity_targeted(punit, current2, target, base);

  punit->activity = current;
  punit->activity_target = target;
  punit->activity_base = base;

  return result;
}

/**************************************************************************
  Return TRUE iff the unit can do the given untargeted activity at its
  current location.

  Note that some activities must be targeted; see
  can_unit_do_activity_targeted.
**************************************************************************/
bool can_unit_do_activity(const struct unit *punit,
                          enum unit_activity activity)
{
  return can_unit_do_activity_targeted(punit, activity, S_LAST, BASE_NONE);
}

/**************************************************************************
  Return TRUE iff the unit can do the given base building activity at its
  current location.
**************************************************************************/
bool can_unit_do_activity_base(const struct unit *punit,
                               Base_type_id base)
{
  return can_unit_do_activity_targeted(punit, ACTIVITY_BASE, S_LAST, base);
}

/**************************************************************************
  Return whether the unit can do the targeted activity at its current
  location.
**************************************************************************/
bool can_unit_do_activity_targeted(const struct unit *punit,
				   enum unit_activity activity,
				   enum tile_special_type target,
                                   Base_type_id base)
{
  return can_unit_do_activity_targeted_at(punit, activity, target,
					  unit_tile(punit), base);
}

/**************************************************************************
  Return TRUE if the unit can do the targeted activity at the given
  location.

  Note that if you make changes here you should also change the code for
  autosettlers in server/settler.c. The code there does not use this
  function as it would be a major CPU hog.
**************************************************************************/
bool can_unit_do_activity_targeted_at(const struct unit *punit,
				      enum unit_activity activity,
				      enum tile_special_type target,
				      const struct tile *ptile,
                                      Base_type_id base)
{
  struct player *pplayer = unit_owner(punit);
  struct terrain *pterrain = tile_terrain(ptile);
  struct unit_class *pclass = unit_class(punit);
  struct base_type *pbase = base_by_number(base);

  switch(activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_GOTO:
    return TRUE;

  case ACTIVITY_POLLUTION:
    return (unit_has_type_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_POLLUTION));

  case ACTIVITY_FALLOUT:
    return (unit_has_type_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_FALLOUT));

  case ACTIVITY_ROAD:
    return (terrain_control.may_road
	    && unit_has_type_flag(punit, F_SETTLERS)
	    && !tile_has_special(ptile, S_ROAD)
	    && pterrain->road_time != 0
	    && (!tile_has_special(ptile, S_RIVER)
		|| player_knows_techs_with_flag(pplayer, TF_BRIDGE)));

  case ACTIVITY_MINE:
    /* Don't allow it if someone else is irrigating this tile.
     * *Do* allow it if they're transforming - the mine may survive */
    if (terrain_control.may_mine
	&& unit_has_type_flag(punit, F_SETTLERS)
	&& ((pterrain == pterrain->mining_result
	     && !tile_has_special(ptile, S_MINE))
	    || (pterrain != pterrain->mining_result
		&& pterrain->mining_result != T_NONE
		&& (!is_ocean(pterrain)
		    || is_ocean(pterrain->mining_result)
		    || can_reclaim_ocean(ptile))
		&& (is_ocean(pterrain)
		    || !is_ocean(pterrain->mining_result)
		    || can_channel_land(ptile))
                && (!terrain_has_flag(pterrain->mining_result, TER_NO_CITIES)
		    || !tile_city(ptile))))) {
      unit_list_iterate(ptile->units, tunit) {
	if (tunit->activity == ACTIVITY_IRRIGATE) {
	  return FALSE;
	}
      } unit_list_iterate_end;
      return TRUE;
    } else {
      return FALSE;
    }

  case ACTIVITY_IRRIGATE:
    /* Don't allow it if someone else is mining this tile.
     * *Do* allow it if they're transforming - the irrigation may survive */
    if (terrain_control.may_irrigate
	&& unit_has_type_flag(punit, F_SETTLERS)
	&& (!tile_has_special(ptile, S_IRRIGATION)
	    || (!tile_has_special(ptile, S_FARMLAND)
		&& player_knows_techs_with_flag(pplayer, TF_FARMLAND)))
	&& ((pterrain == pterrain->irrigation_result
             && can_be_irrigated(ptile, punit))
	    || (pterrain != pterrain->irrigation_result
		&& pterrain->irrigation_result != T_NONE
		&& (!is_ocean(pterrain)
		    || is_ocean(pterrain->irrigation_result)
		    || can_reclaim_ocean(ptile))
		&& (is_ocean(pterrain)
		    || !is_ocean(pterrain->irrigation_result)
		    || can_channel_land(ptile))
                && (!terrain_has_flag(pterrain->irrigation_result, TER_NO_CITIES)
		    || !tile_city(ptile))))) {
      unit_list_iterate(ptile->units, tunit) {
	if (tunit->activity == ACTIVITY_MINE) {
	  return FALSE;
	}
      } unit_list_iterate_end;
      return TRUE;
    } else {
      return FALSE;
    }

  case ACTIVITY_FORTIFYING:
    return (uclass_has_flag(pclass, UCF_CAN_FORTIFY)
	    && punit->activity != ACTIVITY_FORTIFIED
	    && !unit_has_type_flag(punit, F_SETTLERS)
	    && (!is_ocean(pterrain) || tile_city(ptile)));

  case ACTIVITY_FORTIFIED:
    return FALSE;

  case ACTIVITY_BASE:
    return can_build_base(punit, pbase, ptile);

  case ACTIVITY_SENTRY:
    if (!can_unit_survive_at_tile(punit, unit_tile(punit))
        && !unit_transported(punit)) {
      /* Don't let units sentry on tiles they will die on. */
      return FALSE;
    }
    return TRUE;

  case ACTIVITY_RAILROAD:
    /* if the tile has road, the terrain must be ok.. */
    return (terrain_control.may_road
	    && unit_has_type_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_ROAD)
	    && !tile_has_special(ptile, S_RAILROAD)
	    && player_knows_techs_with_flag(pplayer, TF_RAILROAD));

  case ACTIVITY_PILLAGE:
    {
      if (uclass_has_flag(pclass, UCF_CAN_PILLAGE)) {
        bv_special pspresent = get_tile_infrastructure_set(ptile, NULL);
        bv_bases bspresent = get_tile_pillageable_base_set(ptile, NULL);
        bv_special psworking = get_unit_tile_pillage_set(ptile);
        bv_bases bsworking = get_unit_tile_pillage_base_set(ptile);
        bv_special pspossible;
        bv_bases bspossible;

        BV_CLR_ALL(pspossible);
        tile_special_type_iterate(spe) {
          /* Only one unit can pillage a given improvement at a time */
          if (BV_ISSET(pspresent, spe) && !BV_ISSET(psworking, spe)) {
            BV_SET(pspossible, spe);
          }
        } tile_special_type_iterate_end;
        tile_special_type_iterate(spe) {
          enum tile_special_type prereq = get_infrastructure_prereq(spe);
          /* If an improvement is present, we can't pillage its prerequisite */
          /* (FIXME: Could in principle allow simultaneous pillaging of
           * an improvement and its prerequisite, but this would require care
           * to ensure that the unit pillaging the topmost improvement
           * finished first.) */
          if (prereq != S_LAST && BV_ISSET(pspresent, spe)) {
            BV_CLR(pspossible, prereq);
          }
        } tile_special_type_iterate_end;
        if (tile_city(ptile)) {
          /* Can't pillage roads on city tiles */
          BV_CLR(pspossible, S_ROAD);
          BV_CLR(pspossible, S_RAILROAD);
        }

        BV_CLR_ALL(bspossible);
        base_type_iterate(pb) {
          Base_type_id b = base_index(pb);
          if (BV_ISSET(bspresent, b) && !BV_ISSET(bsworking, b)) {
            BV_SET(bspossible, b);
          }
        } base_type_iterate_end;

        if (!BV_ISSET_ANY(pspossible) && !BV_ISSET_ANY(bspossible)) {
          /* Nothing available to pillage */
          return FALSE;
        }

        if (target == S_LAST && base == BASE_NONE) {
          /* Undirected pillaging. If we've got this far, then there's
           * *something* we can pillage; work out what when we come to it */
          return TRUE;
        } else {
          if (!game.info.pillage_select) {
            /* Hobson's choice (this case mostly exists for old clients) */
            /* Needs to match what unit_activity_assign_target chooses */
            int pre_target = get_preferred_pillage(pspossible, bspossible);
            Base_type_id pre_base = BASE_NONE;
            fc_assert_ret_val(pre_target != S_LAST, FALSE);
            if (pre_target > S_LAST) {
              pre_base = pre_target - S_LAST - 1;
              pre_target = S_LAST;
            }
            if (target != pre_target || base != pre_base) {
              /* Only one target allowed, which wasn't the requested one */
              return FALSE;
            }
          }
          if (target != S_LAST) {
            return BV_ISSET(pspossible, target);
          } else {
            return BV_ISSET(bspossible, base);
          }
        }
      } else {
        /* Unit is not a type that can pillage at all */
        return FALSE;
      }
    }

  case ACTIVITY_EXPLORE:
    return (is_ground_unit(punit) || is_sailing_unit(punit));

  case ACTIVITY_TRANSFORM:
    return (terrain_control.may_transform
	    && pterrain->transform_result != T_NONE
	    && pterrain != pterrain->transform_result
	    && (!is_ocean(pterrain)
		|| is_ocean(pterrain->transform_result)
		|| can_reclaim_ocean(ptile))
	    && (is_ocean(pterrain)
		|| !is_ocean(pterrain->transform_result)
		|| can_channel_land(ptile))
	    && (!terrain_has_flag(pterrain->transform_result, TER_NO_CITIES)
		|| !(tile_city(ptile)))
	    && unit_has_type_flag(punit, F_TRANSFORM));

  case ACTIVITY_FORTRESS:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
  case ACTIVITY_UNKNOWN:
    break;
  }
  log_error("can_unit_do_activity_targeted_at() unknown activity %d",
            activity);
  return FALSE;
}

/**************************************************************************
  Assign a new task to a unit. Doesn't account for changed_from.
**************************************************************************/
static void set_unit_activity_internal(struct unit *punit,
                                       enum unit_activity new_activity)
{
  fc_assert_ret(new_activity != ACTIVITY_FORTRESS
                && new_activity != ACTIVITY_AIRBASE);

  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target = S_LAST;
  punit->activity_base = BASE_NONE;
  if (new_activity == ACTIVITY_IDLE && punit->moves_left > 0) {
    /* No longer done. */
    punit->done_moving = FALSE;
  }
}

/**************************************************************************
  assign a new untargeted task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity)
{
  fc_assert_ret(!activity_requires_target(new_activity));

  if (new_activity == ACTIVITY_FORTIFYING
      && punit->changed_from == ACTIVITY_FORTIFIED) {
    new_activity = ACTIVITY_FORTIFIED;
  }
  set_unit_activity_internal(punit, new_activity);
  if (new_activity == punit->changed_from) {
    punit->activity_count = punit->changed_from_count;
  }
}

/**************************************************************************
  assign a new targeted task to a unit.
**************************************************************************/
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity,
				enum tile_special_type new_target,
                                Base_type_id base)
{
  fc_assert_ret(activity_requires_target(new_activity));
  fc_assert_ret(new_activity != ACTIVITY_BASE);
  fc_assert_ret(new_target != S_OLD_FORTRESS
                && new_target != S_OLD_AIRBASE);

  set_unit_activity_internal(punit, new_activity);
  punit->activity_target = new_target;
  punit->activity_base = base;
  if (new_activity == punit->changed_from
      && (new_target == punit->changed_from_target)
      && (new_target != S_LAST || (base == punit->changed_from_base))) {
    punit->activity_count = punit->changed_from_count;
  }
}

/**************************************************************************
  Assign a new base building task to unit
**************************************************************************/
void set_unit_activity_base(struct unit *punit,
                            Base_type_id base)
{
  set_unit_activity_internal(punit, ACTIVITY_BASE);
  punit->activity_base = base;
  if (ACTIVITY_BASE == punit->changed_from
      && (base == punit->changed_from_base)) {
    punit->activity_count = punit->changed_from_count;
  }
}

/**************************************************************************
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

/****************************************************************************
  Return a mask of the specials which are actively (currently) being
  pillaged on the given tile.
****************************************************************************/
bv_special get_unit_tile_pillage_set(const struct tile *ptile)
{
  bv_special tgt_ret;

  BV_CLR_ALL(tgt_ret);
  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == ACTIVITY_PILLAGE
        && punit->activity_target != S_LAST) {
      fc_assert_action(punit->activity_target < S_LAST, continue);
      BV_SET(tgt_ret, punit->activity_target);
    }
  } unit_list_iterate_end;

  return tgt_ret;
}

/****************************************************************************
  Return a mask of the bases which are actively (currently) being
  pillaged on the given tile.
****************************************************************************/
bv_bases get_unit_tile_pillage_base_set(const struct tile *ptile)
{
  bv_bases tgt_ret;

  BV_CLR_ALL(tgt_ret);
  unit_list_iterate(ptile->units, punit) {
    if (punit->activity == ACTIVITY_PILLAGE
        && punit->activity_target == S_LAST
        && punit->activity_base != BASE_NONE) {
      fc_assert(punit->activity_base < base_count());
      BV_SET(tgt_ret, punit->activity_base);
    }
  } unit_list_iterate_end;

  return tgt_ret;
}

/**************************************************************************
  Return text describing the unit's current activity as a static string.

  FIXME: Convert all callers of this function to unit_activity_astr()
  because this function is not re-entrant.
**************************************************************************/
const char *unit_activity_text(const struct unit *punit) {
  static struct astring str = ASTRING_INIT;

  astr_clear(&str);
  unit_activity_astr(punit, &str);

  return astr_str(&str);
}

/**************************************************************************
  Append text describing the unit's current activity to the given astring.
**************************************************************************/
void unit_activity_astr(const struct unit *punit, struct astring *astr)
{
  if (!punit || !astr) {
    return;
  }

  switch (punit->activity) {
  case ACTIVITY_IDLE:
    if (utype_fuel(unit_type(punit))) {
      int rate, f;
      rate = unit_type(punit)->move_rate / SINGLE_MOVE;
      f = ((punit->fuel) - 1);

      if ((punit->moves_left % SINGLE_MOVE) != 0) {
        if (punit->moves_left / SINGLE_MOVE > 0) {
          astr_add_line(astr, "%s: (%d)%d %d/%d", _("Moves"),
                        ((rate * f) + (punit->moves_left / SINGLE_MOVE)),
                        punit->moves_left / SINGLE_MOVE,
                        punit->moves_left % SINGLE_MOVE, SINGLE_MOVE);
        } else {
          astr_add_line(astr, "%s: (%d)%d/%d", _("Moves"),
                        ((rate * f) + (punit->moves_left / SINGLE_MOVE)),
                        punit->moves_left % SINGLE_MOVE, SINGLE_MOVE);
        }
      } else {
        astr_add_line(astr, "%s: (%d)%d", _("Moves"),
                      rate * f + punit->moves_left / SINGLE_MOVE,
                      punit->moves_left / SINGLE_MOVE);
      }
    } else {
      if ((punit->moves_left % SINGLE_MOVE) != 0) {
        if (punit->moves_left / SINGLE_MOVE > 0) {
          astr_add_line(astr, "%s: %d %d/%d", _("Moves"),
                        punit->moves_left / SINGLE_MOVE,
                        punit->moves_left % SINGLE_MOVE, SINGLE_MOVE);
        } else {
          astr_add_line(astr, "%s: %d/%d", _("Moves"),
                        punit->moves_left % SINGLE_MOVE, SINGLE_MOVE);
        }
      } else {
        astr_add_line(astr, "%s: %d", _("Moves"),
                      punit->moves_left / SINGLE_MOVE);
      }
    }
    return;
  case ACTIVITY_POLLUTION:
  case ACTIVITY_FALLOUT:
  case ACTIVITY_ROAD:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
    astr_add_line(astr, "%s", get_activity_text(punit->activity));
    return;
  case ACTIVITY_PILLAGE:
    if (punit->activity_target == S_LAST) {
      astr_add_line(astr, "%s", get_activity_text(punit->activity));
    } else {
      bv_special pset;
      bv_bases bases;

      BV_CLR_ALL(pset);
      BV_SET(pset, punit->activity_target);
      BV_CLR_ALL(bases);
      if (0 <= punit->activity_base && punit->activity_base < base_count()) {
        BV_SET(bases, punit->activity_base);
      }
      astr_add_line(astr, "%s: %s", get_activity_text(punit->activity),
                    get_infrastructure_text(pset, bases));
    }
    return;
  case ACTIVITY_BASE:
    {
      struct base_type *pbase;
      pbase = base_by_number(punit->activity_base);
      astr_add_line(astr, "%s: %s", get_activity_text(punit->activity),
                    base_name_translation(pbase));
    }
    return;
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
    break;
  }

  log_error("Unknown unit activity %d for %s (nb %d) in %s()",
            punit->activity, unit_rule_name(punit), punit->id, __FUNCTION__);
}

/**************************************************************************
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

/**************************************************************************
  Return the owner of the unit.
**************************************************************************/
struct player *unit_owner(const struct unit *punit)
{
  fc_assert_ret_val(NULL != punit, NULL);
  fc_assert(NULL != punit->owner);
  return punit->owner;
}

/**************************************************************************
  Return the tile location of the unit.
  Not (yet) always used, mostly for debugging.
**************************************************************************/
struct tile *unit_tile(const struct unit *punit)
{
  fc_assert_ret_val(NULL != punit, NULL);
  return punit->tile;
}

/*****************************************************************************
  Set the tile location of the unit. Tile can be NULL (for transported units.
*****************************************************************************/
void unit_tile_set(struct unit *punit, struct tile *ptile)
{
  fc_assert_ret(NULL != punit);
  punit->tile = ptile;
}

/**************************************************************************
Returns true if the tile contains an allied unit and only allied units.
(ie, if your nation A is allied with B, and B is allied with C, a tile
containing units from B and C will return false)
**************************************************************************/
struct unit *is_allied_unit_tile(const struct tile *ptile,
				 const struct player *pplayer)
{
  struct unit *punit = NULL;

  unit_list_iterate(ptile->units, cunit) {
    if (pplayers_allied(pplayer, unit_owner(cunit)))
      punit = cunit;
    else
      return NULL;
  }
  unit_list_iterate_end;

  return punit;
}

/****************************************************************************
  Is there an enemy unit on this tile?  Returns the unit or NULL if none.

  This function is likely to fail if used at the client because the client
  doesn't see all units.  (Maybe it should be moved into the server code.)
****************************************************************************/
struct unit *is_enemy_unit_tile(const struct tile *ptile,
				const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_at_war(unit_owner(punit), pplayer))
      return punit;
  } unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 is there an non-allied unit on this tile?
**************************************************************************/
struct unit *is_non_allied_unit_tile(const struct tile *ptile,
				     const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (!pplayers_allied(unit_owner(punit), pplayer))
      return punit;
  }
  unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 is there an unit we have peace or ceasefire with on this tile?
**************************************************************************/
struct unit *is_non_attack_unit_tile(const struct tile *ptile,
				     const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_non_attack(unit_owner(punit), pplayer))
      return punit;
  }
  unit_list_iterate_end;

  return NULL;
}

/****************************************************************************
  Is there an occupying unit on this tile?

  Intended for both client and server; assumes that hiding units are not
  sent to client.  First check tile for known and seen.

  called by city_can_work_tile().
****************************************************************************/
struct unit *unit_occupies_tile(const struct tile *ptile,
				const struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (!is_military_unit(punit)) {
      continue;
    }

    if (uclass_has_flag(unit_class(punit), UCF_DOESNT_OCCUPY_TILE)) {
      continue;
    }

    if (pplayers_at_war(unit_owner(punit), pplayer)) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Is this square controlled by the pplayer?

  Here "is_my_zoc" means essentially a square which is *not* adjacent to an
  enemy unit on a land tile.

  Note this function only makes sense for ground units.

  Since this function is also used in the client, it has to deal with some
  client-specific features, like FoW and the fact that the client cannot 
  see units inside enemy cities.
**************************************************************************/
bool is_my_zoc(const struct player *pplayer, const struct tile *ptile0)
{
  square_iterate(ptile0, 1, ptile) {
    if (is_ocean_tile(ptile)) {
      continue;
    }
    if (is_non_allied_unit_tile(ptile, pplayer)) {
      /* Note: in the client, the above function will return NULL 
       * if there is a city there, even if the city is occupied */
      return FALSE;
    }
    
    if (!is_server()) {
      struct city *pcity = is_non_allied_city_tile(ptile, pplayer);

      if (pcity 
          && (pcity->client.occupied 
              || TILE_KNOWN_UNSEEN == tile_get_known(ptile, pplayer))) {
        /* If the city is fogged, we assume it's occupied */
        return FALSE;
      }
    }
  } square_iterate_end;

  return TRUE;
}

/**************************************************************************
  Takes into account unit class flag UCF_ZOC as well as IGZOC
**************************************************************************/
bool unit_type_really_ignores_zoc(const struct unit_type *punittype)
{
  return (!uclass_has_flag(utype_class(punittype), UCF_ZOC)
	  || utype_has_flag(punittype, F_IGZOC));
}

/**************************************************************************
An "aggressive" unit is a unit which may cause unhappiness
under a Republic or Democracy.
A unit is *not* aggressive if one or more of following is true:
- zero attack strength
- inside a city
- ground unit inside a fortress within 3 squares of a friendly city
**************************************************************************/
bool unit_being_aggressive(const struct unit *punit)
{
  if (!is_attack_unit(punit)) {
    return FALSE;
  }
  if (tile_city(unit_tile(punit))) {
    return FALSE;
  }
  if (BORDERS_DISABLED != game.info.borders
      && game.info.happyborders
      && tile_owner(unit_tile(punit)) == unit_owner(punit)) {
    return FALSE;
  }
  if (tile_has_base_flag_for_unit(unit_tile(punit),
                                  unit_type(punit),
                                  BF_NOT_AGGRESSIVE)) {
    return !is_unit_near_a_friendly_city (punit);
  }
  
  return TRUE;
}

/**************************************************************************
  Returns true if given activity is some kind of building/cleaning.
**************************************************************************/
bool is_build_or_clean_activity(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_POLLUTION:
  case ACTIVITY_ROAD:
  case ACTIVITY_MINE:
  case ACTIVITY_IRRIGATE:
  case ACTIVITY_FORTRESS:
  case ACTIVITY_RAILROAD:
  case ACTIVITY_TRANSFORM:
  case ACTIVITY_AIRBASE:
  case ACTIVITY_FALLOUT:
  case ACTIVITY_BASE:
    return TRUE;
  default:
    return FALSE;
  }
}

/**************************************************************************
  Create a virtual unit skeleton. pcity can be NULL, but then you need
  to set tile and homecity yourself.
**************************************************************************/
struct unit *unit_virtual_create(struct player *pplayer, struct city *pcity,
                                 struct unit_type *punittype,
                                 int veteran_level)
{
  /* Make sure that contents of unit structure are correctly initialized,
   * if you ever allocate it by some other mean than fc_calloc() */
  struct unit *punit = fc_calloc(1, sizeof(*punit));

  /* It does not register the unit so the id is set to 0. */
  punit->id = IDENTITY_NUMBER_ZERO;

  fc_assert_ret_val(NULL != punittype, NULL);   /* No untyped units! */
  punit->utype = punittype;

  fc_assert_ret_val(NULL != pplayer, NULL);     /* No unowned units! */
  punit->owner = pplayer;

  punit->facing = rand_direction();

  if (pcity) {
    unit_tile_set(punit, pcity->tile);
    punit->homecity = pcity->id;
  } else {
    unit_tile_set(punit, NULL);
    punit->homecity = IDENTITY_NUMBER_ZERO;
  }

  memset(punit->upkeep, 0, O_LAST * sizeof(*punit->upkeep));
  punit->goto_tile = NULL;
  punit->veteran = veteran_level;
  /* A unit new and fresh ... */
  punit->fuel = utype_fuel(unit_type(punit));
  punit->hp = unit_type(punit)->hp;
  punit->moves_left = unit_move_rate(punit);
  punit->moved = FALSE;

  punit->ai_controlled = FALSE;
  punit->paradropped = FALSE;
  punit->done_moving = FALSE;

  punit->transporter = NULL;
  punit->transporting = unit_list_new();

  set_unit_activity(punit, ACTIVITY_IDLE);
  punit->battlegroup = BATTLEGROUP_NONE;
  punit->has_orders = FALSE;

  if (is_server()) {
    punit->server.debug = FALSE;
    punit->server.birth_turn = game.info.turn;

    punit->server.ord_map = 0;
    punit->server.ord_city = 0;

    punit->server.vision = NULL; /* No vision. */
    punit->server.action_timestamp = 0;
    punit->server.action_turn = 0;

    punit->server.adv = fc_calloc(1, sizeof(*punit->server.adv));

    CALL_FUNC_EACH_AI(unit_alloc, punit);
    CALL_PLR_AI_FUNC(unit_got, pplayer, punit);
  } else {
    punit->client.focus_status = FOCUS_AVAIL;
    punit->client.colored = FALSE;
  }

  return punit;
}

/**************************************************************************
  Free the memory used by virtual unit. By the time this function is
  called, you should already have unregistered it everywhere.
**************************************************************************/
void unit_virtual_destroy(struct unit *punit)
{
  free_unit_orders(punit);

  /* Check if this unit is transported. */
  fc_assert(!unit_transported(punit));
  if (unit_transported(punit)) {
    fc_assert(unit_transport_unload(punit));
  }

  /* Check for transported units. Use direct access to the list. */
  fc_assert(unit_list_size(punit->transporting) == 0);
  if (unit_list_size(punit->transporting) != 0) {
    /* Unload all units. */
    unit_list_iterate(punit->transporting, pcargo) {
      fc_assert(unit_transport_unload(pcargo));
    } unit_list_iterate_end;
  }

  if (punit->transporting) {
    unit_list_destroy(punit->transporting);
  }

  CALL_PLR_AI_FUNC(unit_lost, punit->owner, punit);
  CALL_FUNC_EACH_AI(unit_free, punit);

  if (is_server() && punit->server.adv) {
    FC_FREE(punit->server.adv);
  }

  FC_FREE(punit);
}

/**************************************************************************
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
  punit->has_orders = FALSE;
}

/****************************************************************************
  Return how many units are in the transport.
****************************************************************************/
int get_transporter_occupancy(const struct unit *ptrans)
{
  fc_assert_ret_val(ptrans, -1);

  return unit_list_size(ptrans->transporting);
}

/****************************************************************************
  Find a transporter at the given location for the unit.
****************************************************************************/
struct unit *transporter_for_unit(const struct unit *pcargo)
{
  struct tile *ptile = unit_tile(pcargo);

  unit_list_iterate(ptile->units, ptrans) {
    if (can_unit_load(pcargo, ptrans)) {
      return ptrans;
    }
  } unit_list_iterate_end;

  return NULL;
}

/****************************************************************************
  Tests if the unit could be updated. Returns UU_OK if is this is
  possible.

  is_free should be set if the unit upgrade is "free" (e.g., Leonardo's).
  Otherwise money is needed and the unit must be in an owned city.

  Note that this function is strongly tied to unittools.c:upgrade_unit().
****************************************************************************/
enum unit_upgrade_result unit_upgrade_test(const struct unit *punit,
                                           bool is_free)
{
  struct player *pplayer = unit_owner(punit);
  struct unit_type *to_unittype = can_upgrade_unittype(pplayer, unit_type(punit));
  struct city *pcity;
  int cost;

  if (!to_unittype) {
    return UU_NO_UNITTYPE;
  }

  if (!is_free) {
    cost = unit_upgrade_price(pplayer, unit_type(punit), to_unittype);
    if (pplayer->economic.gold < cost) {
      return UU_NO_MONEY;
    }

    pcity = tile_city(unit_tile(punit));
    if (!pcity) {
      return UU_NOT_IN_CITY;
    }
    if (city_owner(pcity) != pplayer) {
      /* TODO: should upgrades in allied cities be possible? */
      return UU_NOT_CITY_OWNER;
    }
  }

  if (get_transporter_occupancy(punit) > to_unittype->transport_capacity) {
    /* TODO: allow transported units to be reassigned.  Check for
     * unit_class_transporter_capacity() here and make changes to
     * upgrade_unit. */
    return UU_NOT_ENOUGH_ROOM;
  }

  if (!can_exist_at_tile(to_unittype, unit_tile(punit))) {
    /* The new unit type can't survive on this terrain. */
    return UU_NOT_TERRAIN;
  }

  return UU_OK;
}

/**************************************************************************
  Tests if unit can be converted to another type.
**************************************************************************/
bool unit_can_convert(const struct unit *punit)
{
  return unit_type(punit)->converted_to != NULL;
}

/**************************************************************************
  Find the result of trying to upgrade the unit, and a message that
  most callers can use directly.
**************************************************************************/
enum unit_upgrade_result unit_upgrade_info(const struct unit *punit,
                                           char *buf, size_t bufsz)
{
  struct player *pplayer = unit_owner(punit);
  enum unit_upgrade_result result = unit_upgrade_test(punit, FALSE);
  int upgrade_cost;
  struct unit_type *from_unittype = unit_type(punit);
  struct unit_type *to_unittype = can_upgrade_unittype(pplayer,
                                                       unit_type(punit));
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
  }

  return result;
}

/**************************************************************************
  Does unit lose hitpoints each turn?
**************************************************************************/
bool is_losing_hp(const struct unit *punit)
{
  return unit_type_is_losing_hp(unit_owner(punit), unit_type(punit));
}

/**************************************************************************
  Does unit lose hitpoints each turn?
**************************************************************************/
bool unit_type_is_losing_hp(const struct player *pplayer,
                            const struct unit_type *punittype)
{
  return get_player_bonus(pplayer, EFT_UNIT_RECOVER)
    < (punittype->hp *
       utype_class(punittype)->hp_loss_pct / 100);
}

/**************************************************************************
  Check if unit with given id is still alive. Use this before using
  old unit pointers when unit might have dead.
**************************************************************************/
bool unit_alive(int id)
{
  /* Check if unit exist in game */
  if (game_unit_by_number(id)) {
    return TRUE;
  }

  return FALSE;
}

/**************************************************************************
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

/**************************************************************************
  Return pointer to ai data of given unit and ai type.
**************************************************************************/
void *unit_ai_data(const struct unit *punit, const struct ai_type *ai)
{
  return punit->server.ais[ai_type_number(ai)];
}

/**************************************************************************
  Attach ai data to unit
**************************************************************************/
void unit_set_ai_data(struct unit *punit, const struct ai_type *ai,
                      void *data)
{
  punit->server.ais[ai_type_number(ai)] = data;
}

/*****************************************************************************
  Calculate how expensive it is to bribe the unit. The cost depends on the
  distance to the capital and government form. For a damaged unit the price is
  reduced.

  The bribe cost for settlers are halved.
**************************************************************************/
int unit_bribe_cost(struct unit *punit)
{
  int cost, default_hp, dist = 0;
  struct city *capital;

  fc_assert_ret_val(punit != NULL, 0);

  default_hp = unit_type(punit)->hp;
  cost = unit_owner(punit)->economic.gold + game.info.base_bribe_cost;
  capital = player_capital(unit_owner(punit));

  /* Consider the distance to the capital. */
  if (capital != NULL) {
    dist = MIN(GAME_UNIT_BRIBE_DIST_MAX,
               map_distance(capital->tile, unit_tile(punit)));
  } else {
    dist = GAME_UNIT_BRIBE_DIST_MAX;
  }
  cost /= dist + 2;

  /* Consider the build cost. */
  cost *= unit_build_shield_cost(punit) / 10;

  /* FIXME: This is a weird one - should be replaced. */
  if (unit_has_type_flag(punit, F_CITIES)) {
    cost /= 2;
  }

  /* Veterans are not cheap. */
  /* FIXME: Should this depend on the veteran level? */
  cost += cost * punit->veteran / 3;

  /* Cost now contains the basic bribe cost.  We now reduce it by:
   *    bribecost = cost/2 + cost/2 * damage/hp
   *              = cost/2 * (1 + damage/hp) */
  return ((float)cost / 2 * (1.0 + punit->hp / default_hp));
}

/*****************************************************************************
  Load pcargo onto ptrans. Returns TRUE on success.
*****************************************************************************/
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

/*****************************************************************************
  Unload pcargo from ptrans. Returns TRUE on success.
*****************************************************************************/
bool unit_transport_unload(struct unit *pcargo)
{
  struct unit *ptrans;

  fc_assert_ret_val(pcargo != NULL, FALSE);

  ptrans = unit_transport_get(pcargo);

  if (ptrans == NULL) {
    /* 'pcargo' is not transported. */
    return FALSE;
  }

  /* 'pcargo' and 'ptrans' should be on the same tile. */
  fc_assert_ret_val(same_pos(unit_tile(pcargo), unit_tile(ptrans)), FALSE);
  /* It is an error if 'pcargo' can not be removed from the 'ptrans'. */
  fc_assert_ret_val(unit_list_remove(ptrans->transporting, pcargo), FALSE);

  /* For the server (also save for the client). */
  pcargo->transporter = NULL;
  /* For the client - needed as the client does not know all units. */
  if (!is_server()) {
    pcargo->client.transported_by = -1;
  }

  return TRUE;
}

/*****************************************************************************
  Returns TRUE iff the unit is transported.
*****************************************************************************/
bool unit_transported(const struct unit *pcargo)
{
  fc_assert_ret_val(pcargo != NULL, FALSE);

  /* The unit is transported if a transporter unit is set or, (for the client)
   * if the transported_by field is set. */
  if (pcargo->transporter != NULL
      || (!is_server() && pcargo->client.transported_by != -1)) {
    return TRUE;
  }

  return FALSE;
}

/*****************************************************************************
  Returns the transporter of the unit or NULL if it is not transported.
*****************************************************************************/
struct unit *unit_transport_get(const struct unit *pcargo)
{
  fc_assert_ret_val(pcargo != NULL, NULL);

  if (pcargo->transporter == NULL) {
    return NULL;
  } else {
    /* Need this as the return value is not 'const'! */
    return game_unit_by_number(pcargo->transporter->id);
  }
}

/*****************************************************************************
  Returns the list of cargo units.
*****************************************************************************/
struct unit_list *unit_transport_cargo(const struct unit *ptrans)
{
  fc_assert_ret_val(ptrans != NULL, NULL);
  fc_assert_ret_val(ptrans->transporting != NULL, NULL);

  return ptrans->transporting;
}

/*****************************************************************************
  Returns if pcargo in ptrans is a valid transport.
*****************************************************************************/
bool unit_transport_check(const struct unit *pcargo,
                          const struct unit *ptrans)
{
  struct unit_list *ptrans_recursive = unit_list_new();
  /* Get !const unit struct for pcargo. */
  struct unit *plevel = game_unit_by_number(pcargo->id);
  bool transport_ok = TRUE;

  /* Recursive loop over all transporters up to max level. */
  while (transport_ok && plevel && unit_list_size(ptrans_recursive)
                                   < GAME_TRANSPORT_MAX_RECURSIVE) {
    /* Check if the unit can be transported. If the unit can be transported
     * by any of the previous transporters, fail. THis disallows to carry
     * units of one type within the same unit type. */
    unit_list_iterate(ptrans_recursive, pcargo_recursive) {
      if (can_unit_transport(pcargo_recursive, plevel)) {
        transport_ok = FALSE;
        break;
      }
    } unit_list_iterate_end;

    if (!transport_ok) {
#ifdef DEBUG
      char buf[512] = "";
      fc_snprintf(buf, sizeof(buf), "%s [Error] ",
                  unit_name_translation(plevel));
      unit_list_iterate(ptrans_recursive, pcargo_recursive) {
        cat_snprintf(buf, sizeof(buf), " in %s",
                     unit_name_translation(pcargo_recursive));
      } unit_list_iterate_end;
      log_error("Invalid transport at level %d (%s).",
                unit_list_size(ptrans_recursive), buf);
#endif /* DEBUG */
      break;
    }

    /* Insert cargo at the beginning of the list. */
    unit_list_prepend(ptrans_recursive, plevel);

    /* Check for next level. */
    plevel = unit_transport_get(plevel);
  }

  unit_list_destroy(ptrans_recursive);

  return transport_ok;
}
