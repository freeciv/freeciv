/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

#include "base.h"
#include "fc_types.h"
#include "map.h"
#include "movement.h"
#include "unit.h"
#include "unitlist.h"
#include "unittype.h"
#include "terrain.h"

static const char *move_type_names[] = {
  "Air", "Land", "Sea", "Both"
};

/****************************************************************************
  This function calculates the move rate of the unit, taking into 
  account the penalty for reduced hitpoints (affects sea and land 
  units only), the effects of wonders for sea units, and any veteran
  bonuses.
****************************************************************************/
int unit_move_rate(const struct unit *punit)
{
  int move_rate = 0;
  int base_move_rate = unit_type(punit)->move_rate 
    + unit_type(punit)->veteran[punit->veteran].move_bonus;
  struct unit_class *pclass = get_unit_class(punit->type);

  move_rate = base_move_rate;

  if (unit_class_flag(pclass, UCF_DAMAGE_SLOWS)) {
    /* Scale the MP based on how many HP the unit has. */
    move_rate = (move_rate * punit->hp) / unit_type(punit)->hp;
  }

  /* Add on effects bonus (Magellan's Expedition, Lighthouse,
   * Nuclear Power). */
  move_rate += (get_unit_bonus(punit, EFT_MOVE_BONUS) * SINGLE_MOVE);

  /* Don't let the move_rate be less than min_speed unless the base_move_rate is
   * also less than min_speed. */
  if (move_rate < pclass->min_speed) {
    move_rate = MIN(pclass->min_speed, base_move_rate);
  }

  return move_rate;
}


/****************************************************************************
  Return TRUE iff the unit can be a defender at its current location.  This
  should be checked when looking for a defender - not all units on the
  tile are valid defenders.
****************************************************************************/
bool unit_can_defend_here(const struct unit *punit)
{
  /* Do not just check if unit is transported.
   * Even transported units may step out from transport to fight,
   * if this is their native terrain. */
  return can_unit_exist_at_tile(punit, punit->tile);
}

/****************************************************************************
 This unit can attack non-native tiles (eg. Ships ability to
 shore bombardment)
****************************************************************************/
bool can_attack_non_native(struct unit_type *utype)
{
  return (get_unit_class(utype)->move_type == SEA_MOVING
          && !unit_type_flag(utype, F_NO_LAND_ATTACK));
}

/****************************************************************************
  Return TRUE iff the unit is a sailing/naval/sea/water unit.
****************************************************************************/
bool is_sailing_unit(const struct unit *punit)
{
  return (get_unit_move_type(unit_type(punit)) == SEA_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit is an air/plane unit (including missiles).
****************************************************************************/
bool is_air_unit(const struct unit *punit)
{
  return (get_unit_move_type(unit_type(punit)) == AIR_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit is a helicopter unit.
****************************************************************************/
bool is_heli_unit(const struct unit *punit)
{
  return (get_unit_move_type(unit_type(punit)) == HELI_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit is a ground/land/normal unit.
****************************************************************************/
bool is_ground_unit(const struct unit *punit)
{
  return (get_unit_move_type(unit_type(punit)) == LAND_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit type is a sailing/naval/sea/water unit type.
****************************************************************************/
bool is_sailing_unittype(const struct unit_type *punittype)
{
  return (get_unit_move_type(punittype) == SEA_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit type is an air unit type (including missiles).
****************************************************************************/
bool is_air_unittype(const struct unit_type *punittype)
{
  return (get_unit_move_type(punittype) == AIR_MOVING);
}


/****************************************************************************
  Return TRUE iff this unit type is a ground/land/normal unit type.
****************************************************************************/
bool is_ground_unittype(const struct unit_type *punittype)
{
  return (get_unit_move_type(punittype) == LAND_MOVING);
}


/****************************************************************************
  Return TRUE iff the unit can "exist" at this location.  This means it can
  physically be present on the tile (without the use of a transporter).  See
  also can_unit_survive_at_tile.
****************************************************************************/
bool can_unit_exist_at_tile(const struct unit *punit,
                            const struct tile *ptile)
{
  if (ptile->city && !(is_sailing_unit(punit)
      && !is_ocean_near_tile(ptile))) {
    return TRUE;
  }

  return is_native_tile(punit->type, ptile);
}

/****************************************************************************
  This tile is native to unit.

  See is_native_to_class()
****************************************************************************/
bool is_native_tile(const struct unit_type *punittype,
                    const struct tile *ptile)
{
  return is_native_to_class(get_unit_class(punittype), ptile->terrain,
                            ptile->special);
}


/****************************************************************************
  This terrain is native to unit.

  See is_native_to_class()
****************************************************************************/
bool is_native_terrain(const struct unit_type *punittype,
                       const struct terrain *pterrain,
                       bv_special special)
{
  return is_native_to_class(get_unit_class(punittype), pterrain, special);
}

/****************************************************************************
  This tile is native to unit class.

  See is_native_to_class()
****************************************************************************/
bool is_native_tile_to_class(const struct unit_class *punitclass,
                             const struct tile *ptile)
{
  return is_native_to_class(punitclass, ptile->terrain, ptile->special);
}

/****************************************************************************
  This terrain is native to unit class. Units that require fuel dont survive
  even on native terrain.
****************************************************************************/
bool is_native_to_class(const struct unit_class *punitclass,
                        const struct terrain *pterrain,
                        bv_special special)
{
  if (!pterrain) {
    /* Unknown is considered native terrain */
    return TRUE;
  }

  if (unit_class_flag(punitclass, UCF_ROAD_NATIVE)
      && contains_special(special, S_ROAD)) {
    return TRUE;
  }

  return BV_ISSET(pterrain->native_to, punitclass->id);
}

/****************************************************************************
  Is there native tile adjacent to given tile
****************************************************************************/
bool is_native_near_tile(const struct unit_type *utype, const struct tile *ptile)
{
  if (is_native_tile(utype, ptile)) {
    return TRUE;
  }

  adjc_iterate(ptile, ptile2) {
    if (is_native_tile(utype, ptile2)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return TRUE iff the unit can "survive" at this location.  This means it can
  not only be physically present at the tile but will be able to survive
  indefinitely on its own (without a transporter).  Units that require fuel
  or have a danger of drowning are examples of non-survivable units.  See
  also can_unit_exist_at_tile.

  (This function could be renamed as unit_wants_transporter.)
****************************************************************************/
bool can_unit_survive_at_tile(const struct unit *punit,
                              const struct tile *ptile)
{
  if (!can_unit_exist_at_tile(punit, ptile)) {
    return FALSE;
  }

  if (tile_get_city(ptile)) {
    return TRUE;
  }

  switch (get_unit_move_type(unit_type(punit))) {
  case LAND_MOVING:
  case SEA_MOVING:
    return TRUE;
  case AIR_MOVING:
    return tile_has_base_flag(punit->tile, BF_REFUEL);
  case HELI_MOVING:
    return tile_has_base_flag(punit->tile, BF_NO_HP_LOSS);
  default:
    die("Invalid move type");
  }
  return TRUE;
}


/****************************************************************************
  Returns whether the unit is allowed (by ZOC) to move from src_tile
  to dest_tile (assumed adjacent).

  You CAN move if:
    1. You have units there already
    2. Your unit isn't a ground unit
    3. Your unit ignores ZOC (diplomat, freight, etc.)
    4. You're moving from or to a city
    5. You're moving from an ocean square (from a boat)
    6. The spot you're moving from or to is in your ZOC
****************************************************************************/
bool can_step_taken_wrt_to_zoc(const struct unit_type *punittype,
                               const struct player *unit_owner,
                               const struct tile *src_tile,
                               const struct tile *dst_tile)
{
  if (unit_type_really_ignores_zoc(punittype)) {
    return TRUE;
  }
  if (is_allied_unit_tile(dst_tile, unit_owner)) {
    return TRUE;
  }
  if (tile_get_city(src_tile) || tile_get_city(dst_tile)) {
    return TRUE;
  }
  if (is_ocean(tile_get_terrain(src_tile))
      || is_ocean(tile_get_terrain(dst_tile))) {
    return TRUE;
  }
  return (is_my_zoc(unit_owner, src_tile)
	  || is_my_zoc(unit_owner, dst_tile));
}


/****************************************************************************
  See can_step_take_wrt_to_zoc().  This function is exactly the same but
  it takes a unit instead of a unittype and player.
****************************************************************************/
static bool zoc_ok_move_gen(const struct unit *punit,
                            const struct tile *src_tile,
                            const struct tile *dst_tile)
{
  return can_step_taken_wrt_to_zoc(punit->type, unit_owner(punit),
				   src_tile, dst_tile);
}


/****************************************************************************
  Returns whether the unit can safely move from its current position to
  the adjacent dst_tile.  This function checks only ZOC.

  See can_step_taken_wrt_to_zoc().
****************************************************************************/
bool zoc_ok_move(const struct unit *punit, const struct tile *dst_tile)
{
  return zoc_ok_move_gen(punit, punit->tile, dst_tile);
}


/****************************************************************************
  Returns whether the unit can move from its current tile to the destination
  tile.

  See test_unit_move_to_tile().
****************************************************************************/
bool can_unit_move_to_tile(const struct unit *punit,
                           const struct tile *dst_tile,
                           bool igzoc)
{
  return MR_OK == test_unit_move_to_tile(punit->type, unit_owner(punit),
					 punit->activity,
					 punit->tile, dst_tile,
					 igzoc);
}


/**************************************************************************
  Returns whether the unit can move from its current tile to the
  destination tile.  An enumerated value is returned indication the error
  or success status.

  The unit can move if:
    1) The unit is idle or on server goto.
    2) The target location is next to the unit.
    3) There are no non-allied units on the target tile.
    4) Unit can move to non-native tile if there is city
       or free transport capacity.
    5) Marines are the only land units that can attack from a ocean square.
    6) There are no peaceful but un-allied units on the target tile.
    7) There is not a peaceful but un-allied city on the target tile.
    8) There is no non-allied unit blocking (zoc) [or igzoc is true].
**************************************************************************/
enum unit_move_result test_unit_move_to_tile(const struct unit_type *punittype,
					     const struct player *unit_owner,
					     enum unit_activity activity,
					     const struct tile *src_tile,
					     const struct tile *dst_tile,
					     bool igzoc)
{
  bool zoc;
  struct city *pcity;

  /* 1) */
  if (activity != ACTIVITY_IDLE
      && activity != ACTIVITY_GOTO) {
    /* For other activities the unit must be stationary. */
    return MR_BAD_ACTIVITY;
  }

  /* 2) */
  if (!is_tiles_adjacent(src_tile, dst_tile)) {
    /* Of course you can only move to adjacent positions. */
    return MR_BAD_DESTINATION;
  }

  /* 3) */
  if (is_non_allied_unit_tile(dst_tile, unit_owner)) {
    /* You can't move onto a tile with non-allied units on it (try
     * attacking instead). */
    return MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_UNIT;
  }

  /* 4) */
  if (!is_native_tile(punittype, dst_tile)
      && !is_allied_city_tile(dst_tile, unit_owner)
      && unit_class_transporter_capacity(dst_tile, unit_owner, punittype->class) <= 0) {
    return MR_NO_TRANSPORTER_CAPACITY;
  }

  if (get_unit_move_type(punittype) == LAND_MOVING) {

    /* Moving from ocean */
    if (is_ocean(src_tile->terrain)) {
      /* 5) */
      if (!unit_type_flag(punittype, F_MARINES)
	  && is_enemy_city_tile(dst_tile, unit_owner)) {
	/* Most ground units can't move into cities from ships.  (Note this
	 * check is only for movement, not attacking: most ground units
	 * can't attack from ship at *any* units on land.) */
	return MR_BAD_TYPE_FOR_CITY_TAKE_OVER;
      }
    }
  }

  /* 6) */
  if (is_non_attack_unit_tile(dst_tile, unit_owner)) {
    /* You can't move into a non-allied tile.
     *
     * FIXME: this should never happen since it should be caught by check
     * #3. */
    return MR_NO_WAR;
  }

  /* 7) */
  pcity = dst_tile->city;
  if (pcity && pplayers_non_attack(city_owner(pcity), unit_owner)) {
    /* You can't move into an empty city of a civilization you're at
     * peace with - you must first either declare war or make alliance. */
    return MR_NO_WAR;
  }

  /* 8) */
  zoc = igzoc
    || can_step_taken_wrt_to_zoc(punittype, unit_owner, src_tile, dst_tile);
  if (!zoc) {
    /* The move is illegal because of zones of control. */
    return MR_ZOC;
  }

  return MR_OK;
}

/**************************************************************************
  Return true iff transporter has ability to transport transported.
**************************************************************************/
bool can_unit_transport(const struct unit *transporter,
                        const struct unit *transported)
{
  return can_unit_type_transport(transporter->type, transported->type->class);
}

/**************************************************************************
  Return TRUE iff transporter type has ability to transport transported class.
**************************************************************************/
bool can_unit_type_transport(const struct unit_type *transporter,
                             const struct unit_class *transported)
{
  if (transporter->transport_capacity <= 0) {
    return FALSE;
  }

  return BV_ISSET(transporter->cargo, transported->id);
}

/**************************************************************************
  Convert move type names to enum; case insensitive;
  returns MOVETYPE_LAST if can't match.
**************************************************************************/
enum unit_move_type move_type_from_str(const char *s)
{
  int i;

  for (i = 0; i < MOVETYPE_LAST; i++) {
    if (mystrcasecmp(move_type_names[i], s)==0) {
      return i;
    }
  }
  return MOVETYPE_LAST;
}

/**************************************************************************
  Search transport suitable for given unit from tile. It has to have
  free space in it.
**************************************************************************/
struct unit *find_transport_from_tile(struct unit *punit, struct tile *ptile)
{
  unit_list_iterate(ptile->units, ptransport) {
    if (get_transporter_capacity(ptransport) > get_transporter_occupancy(ptransport)
        && can_unit_transport(ptransport, punit)
        && is_native_tile(unit_type(ptransport), ptile)) {
      return ptransport;
    }
  } unit_list_iterate_end;

  return NULL;
}
 
/**************************************************************************
 Returns the number of free spaces for units of given class.
 Can be 0.
**************************************************************************/
int unit_class_transporter_capacity(const struct tile *ptile,
                                    const struct player *pplayer,
                                    const struct unit_class *pclass)
{
  int availability = 0;

  unit_list_iterate(ptile->units, punit) {
    if (unit_owner(punit) == pplayer
        || pplayers_allied(unit_owner(punit), pplayer)) {

      if (can_unit_type_transport(punit->type, pclass)) {
        availability += get_transporter_capacity(punit);
        availability -= get_transporter_occupancy(punit);
      }
    }
  } unit_list_iterate_end;

  return availability;
}
