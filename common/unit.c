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

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"
#include "tech.h"

#include "unit.h"

/* get 'struct unit_list' functions: */
#define SPECLIST_TAG unit
#define SPECLIST_TYPE struct unit
#include "speclist_c.h"

/***************************************************************
This function calculates the move rate of the unit taking into 
account the penalty for reduced hitpoints (affects sea and land 
units only) and the effects of wonders for sea units
+ veteran bonus.

FIXME: Use generalised improvements code instead of hardcoded
wonder effects --RK
***************************************************************/
int unit_move_rate(struct unit *punit)
{
  int move_rate = 0;
  int base_move_rate = unit_type(punit)->move_rate 
                       + unit_type(punit)->veteran[punit->veteran].move_bonus;

  switch (unit_type(punit)->move_type) {
  case LAND_MOVING:
    move_rate = (base_move_rate * punit->hp) / unit_type(punit)->hp;
    break;
 
  case SEA_MOVING:
    move_rate = (base_move_rate * punit->hp) / unit_type(punit)->hp;

    if (player_owns_active_wonder(unit_owner(punit), B_LIGHTHOUSE)) {
      move_rate += SINGLE_MOVE;
    }
 
    if (player_owns_active_wonder(unit_owner(punit), B_MAGELLAN)) {
      move_rate += (improvement_variant(B_MAGELLAN) == 1) 
                     ? SINGLE_MOVE : 2 * SINGLE_MOVE;
    }
 
    if (player_knows_techs_with_flag(unit_owner(punit), TF_BOAT_FAST)) {
      move_rate += SINGLE_MOVE;
    }
 
    if (move_rate < 2 * SINGLE_MOVE) {
      move_rate = MIN(2 * SINGLE_MOVE, base_move_rate);
    }
    break;

  case HELI_MOVING:
  case AIR_MOVING:
    move_rate = base_move_rate;
    break;

  default:
    die("In common/unit.c:unit_move_rate: illegal move type %d",
  					unit_type(punit)->move_type);
  }
  
  if (move_rate < SINGLE_MOVE && base_move_rate > 0) {
    move_rate = SINGLE_MOVE;
  }
  return move_rate;
}

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
bool diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty)
{
  if(!is_diplomat_action_available(pdiplomat, action, destx, desty))
    return FALSE;

  if(!is_tiles_adjacent(pdiplomat->x, pdiplomat->y, destx, desty)
     && !same_pos(pdiplomat->x, pdiplomat->y, destx, desty))
    return FALSE;

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
bool is_diplomat_action_available(struct unit *pdiplomat,
				 enum diplomat_actions action, 
				 int destx, int desty)
{
  struct city *pcity=map_get_city(destx, desty);

  if (action!=DIPLOMAT_MOVE
      && is_ocean(map_get_terrain(pdiplomat->x, pdiplomat->y))) {
    return FALSE;
  }

  if (pcity) {
    if(pcity->owner!=pdiplomat->owner &&
       real_map_distance(pdiplomat->x, pdiplomat->y, pcity->x, pcity->y) <= 1) {
      if(action==DIPLOMAT_SABOTAGE)
	return pplayers_at_war(unit_owner(pdiplomat), city_owner(pcity));
      if(action==DIPLOMAT_MOVE)
        return pplayers_allied(unit_owner(pdiplomat), city_owner(pcity));
      if (action == DIPLOMAT_EMBASSY && !is_barbarian(city_owner(pcity)) &&
	  !player_has_embassy(unit_owner(pdiplomat), city_owner(pcity)))
	return TRUE;
      if(action==SPY_POISON &&
	 pcity->size>1 &&
	 unit_flag(pdiplomat, F_SPY))
	return pplayers_at_war(unit_owner(pdiplomat), city_owner(pcity));
      if(action==DIPLOMAT_INVESTIGATE)
        return TRUE;
      if (action == DIPLOMAT_STEAL && !is_barbarian(city_owner(pcity)))
	return TRUE;
      if(action==DIPLOMAT_INCITE)
        return !pplayers_allied(city_owner(pcity), unit_owner(pdiplomat));
      if(action==DIPLOMAT_ANY_ACTION)
        return TRUE;
      if (action==SPY_GET_SABOTAGE_LIST && unit_flag(pdiplomat, F_SPY))
	return pplayers_at_war(unit_owner(pdiplomat), city_owner(pcity));
    }
  } else { /* Action against a unit at a tile */
    /* If it is made possible to do action against allied units
       handle_unit_move_request() should be changed so that pdefender
       is also set to allied units */
    struct tile *ptile = map_get_tile(destx, desty);
    struct unit *punit;

    if ((action == SPY_SABOTAGE_UNIT || action == DIPLOMAT_ANY_ACTION) 
        && unit_list_size(&ptile->units) == 1
        && unit_flag(pdiplomat, F_SPY)) {
      punit = unit_list_get(&ptile->units, 0);
      if (pplayers_at_war(unit_owner(pdiplomat), unit_owner(punit))) {
        return TRUE;
      }
    }

    if ((action == DIPLOMAT_BRIBE || action == DIPLOMAT_ANY_ACTION)
        && unit_list_size(&ptile->units) == 1) {
      punit = unit_list_get(&ptile->units, 0);
      if (!pplayers_allied(unit_owner(punit), unit_owner(pdiplomat))) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

/**************************************************************************
FIXME: Maybe we should allow airlifts between allies
**************************************************************************/
bool unit_can_airlift_to(struct unit *punit, struct city *pcity)
{
  struct city *city1;

  if(punit->moves_left == 0)
    return FALSE;
  if(!(city1=map_get_city(punit->x, punit->y))) 
    return FALSE;
  if(city1==pcity)
    return FALSE;
  if(city1->owner != pcity->owner) 
    return FALSE;
  if (!city1->airlift || !pcity->airlift) 
    return FALSE;
  if (!is_ground_unit(punit))
    return FALSE;

  return TRUE;
}

/****************************************************************************
  Return TRUE iff the unit is following client-side orders.
****************************************************************************/
bool unit_has_orders(struct unit *punit)
{
  return punit->has_orders;
}

/**************************************************************************
...
**************************************************************************/
bool unit_can_help_build_wonder(struct unit *punit, struct city *pcity)
{
  if (!is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y)
      && !same_pos(punit->x, punit->y, pcity->x, pcity->y))
    return FALSE;

  return (unit_flag(punit, F_HELP_WONDER)
	  && punit->owner == pcity->owner
	  && !pcity->is_building_unit && is_wonder(pcity->currently_building)
	  && (pcity->shield_stock
	      < impr_build_shield_cost(pcity->currently_building)));
}


/**************************************************************************
...
**************************************************************************/
bool unit_can_help_build_wonder_here(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  return pcity && unit_can_help_build_wonder(punit, pcity);
}


/**************************************************************************
...
**************************************************************************/
bool unit_can_est_traderoute_here(struct unit *punit)
{
  struct city *phomecity, *pdestcity;

  return (unit_flag(punit, F_TRADE_ROUTE)
	  && (pdestcity = map_get_city(punit->x, punit->y))
	  && (phomecity = find_city_by_id(punit->homecity))
	  && can_cities_trade(phomecity, pdestcity));
}

/**************************************************************************
...
**************************************************************************/
bool unit_can_defend_here(struct unit *punit)
{
  if (is_ground_unit(punit)
      && is_ocean(map_get_terrain(punit->x, punit->y))) {
    return FALSE;
  }
  
  return TRUE;
}

/**************************************************************************
Returns the number of free spaces for ground units. Can be 0 or negative.
**************************************************************************/
int ground_unit_transporter_capacity(int x, int y, struct player *pplayer)
{
  int availability = 0;
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (unit_owner(punit) == pplayer
        || pplayers_allied(unit_owner(punit), pplayer)) {
      if (is_ground_units_transport(punit)
	  && !(is_ground_unit(punit) && is_ocean(ptile->terrain))) {
	availability += get_transporter_capacity(punit);
      } else if (is_ground_unit(punit)) {
	availability--;
      }
    }
  }
  unit_list_iterate_end;

  return availability;
}

/**************************************************************************
...
**************************************************************************/
int get_transporter_capacity(struct unit *punit)
{
  return unit_type(punit)->transport_capacity;
}

/**************************************************************************
...
**************************************************************************/
bool is_ground_units_transport(struct unit *punit)
{
  return (get_transporter_capacity(punit) > 0
	  && !unit_flag(punit, F_MISSILE_CARRIER)
	  && !unit_flag(punit, F_CARRIER));
}

/**************************************************************************
...
**************************************************************************/
bool is_air_units_transport(struct unit *punit)
{
  return (get_transporter_capacity(punit) > 0
	  && (unit_flag(punit, F_MISSILE_CARRIER)
	      || unit_flag(punit, F_CARRIER)));
}

/**************************************************************************
...
**************************************************************************/
bool is_sailing_unit(struct unit *punit)
{
  return (unit_type(punit)->move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
bool is_air_unit(struct unit *punit)
{
  return (unit_type(punit)->move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
bool is_heli_unit(struct unit *punit)
{
  return (unit_type(punit)->move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
bool is_ground_unit(struct unit *punit)
{
  return (unit_type(punit)->move_type == LAND_MOVING);
}

/**************************************************************************
  Is the unit capable of attacking?
**************************************************************************/
bool is_attack_unit(struct unit *punit)
{
  return (unit_type(punit)->attack_strength > 0);
}

/**************************************************************************
  Military units are capable of enforcing martial law. Military ground
  and heli units can occupy empty cities -- see COULD_OCCUPY(punit).
  Some military units, like the Galleon, have no attack strength.
**************************************************************************/
bool is_military_unit(struct unit *punit)
{
  return !unit_flag(punit, F_NONMIL);
}

/**************************************************************************
...
**************************************************************************/
bool is_diplomat_unit(struct unit *punit)
{
  return (unit_flag(punit, F_DIPLOMAT));
}

/**************************************************************************
...
**************************************************************************/
static bool is_ground_threat(struct player *pplayer, struct unit *punit)
{
  return (pplayers_at_war(pplayer, unit_owner(punit))
	  && (unit_flag(punit, F_DIPLOMAT)
	      || (is_ground_unit(punit) && is_military_unit(punit))));
}

/**************************************************************************
...
**************************************************************************/
bool is_square_threatened(struct player *pplayer, int x, int y)
{
  square_iterate(x, y, 2, x1, y1) {
    unit_list_iterate(map_get_tile(x1, y1)->units, punit) {
      if (is_ground_threat(pplayer, punit)) {
	return TRUE;
      }
    } unit_list_iterate_end;
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
bool is_field_unit(struct unit *punit)
{
  return unit_flag(punit, F_FIELDUNIT);
}


/**************************************************************************
  Is the unit one that is invisible on the map. A unit is invisible if
  it has the F_PARTIAL_INVIS flag or if it transported by a unit with
  this flag.
**************************************************************************/
bool is_hiding_unit(struct unit *punit)
{
  struct unit *transporter = find_unit_by_id(punit->transported_by);

  return (unit_flag(punit, F_PARTIAL_INVIS)
	  || (transporter && unit_flag(transporter, F_PARTIAL_INVIS)));
}

/**************************************************************************
...
**************************************************************************/
bool kills_citizen_after_attack(struct unit *punit)
{
  return TEST_BIT(game.killcitizen, (int) (unit_type(punit)->move_type) - 1);
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_add_to_city(struct unit *punit)
{
  return (test_unit_add_or_build_city(punit) == AB_ADD_OK);
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_build_city(struct unit *punit)
{
  return (test_unit_add_or_build_city(punit) == AB_BUILD_OK);
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_add_or_build_city(struct unit *punit)
{
  enum add_build_city_result r = test_unit_add_or_build_city(punit);
  return (r == AB_BUILD_OK || r == AB_ADD_OK);
}

/**************************************************************************
...
**************************************************************************/
enum add_build_city_result test_unit_add_or_build_city(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  bool is_build = unit_flag(punit, F_CITIES);
  bool is_add = unit_flag(punit, F_ADD_TO_CITY);
  int new_pop;

  /* See if we can build */
  if (!pcity) {
    if (!is_build)
      return AB_NOT_BUILD_UNIT;
    if (punit->moves_left == 0)
      return AB_NO_MOVES_BUILD;
    if (!city_can_be_built_here(punit->x, punit->y, punit)) {
      return AB_NOT_BUILD_LOC;
    }
    return AB_BUILD_OK;
  }
  
  /* See if we can add */

  if (!is_add)
    return AB_NOT_ADDABLE_UNIT;
  if (punit->moves_left == 0)
    return AB_NO_MOVES_ADD;

  assert(unit_pop_value(punit->type) > 0);
  new_pop = pcity->size + unit_pop_value(punit->type);

  if (new_pop > game.add_to_size_limit)
    return AB_TOO_BIG;
  if (pcity->owner != punit->owner)
    return AB_NOT_OWNER;
  if (improvement_exists(B_AQUEDUCT)
      && !city_got_building(pcity, B_AQUEDUCT)
      && new_pop > game.aqueduct_size)
    return AB_NO_AQUEDUCT;
  if (improvement_exists(B_SEWER)
      && !city_got_building(pcity, B_SEWER)
      && new_pop > game.sewer_size)
    return AB_NO_SEWER;
  return AB_ADD_OK;
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_change_homecity(struct unit *punit)
{
  struct city *pcity=map_get_city(punit->x, punit->y);
  return pcity && pcity->owner==punit->owner;
}

/**************************************************************************
Return whether the unit can be put in auto-mode.
(Auto-settler for settlers, auto-attack for military units.)
**************************************************************************/
bool can_unit_do_auto(struct unit *punit) 
{
  if (unit_flag(punit, F_SETTLERS)) {
    return TRUE;
  }
  if (is_military_unit(punit) && is_attack_unit(punit)
      && map_get_city(punit->x, punit->y)) {
    return TRUE;
  }
  return FALSE;
}

/**************************************************************************
Return whether the unit can connect with given activity (or with
any activity if activity arg is set to ACTIVITY_IDLE)
**************************************************************************/
bool can_unit_do_connect (struct unit *punit, enum unit_activity activity) 
{
  struct player *pplayer = unit_owner(punit);

  if (!unit_flag(punit, F_SETTLERS))
    return FALSE;

  if (activity == ACTIVITY_IDLE)   /* IDLE here means "any activity" */
    return TRUE;

  if (activity == ACTIVITY_ROAD 
      || activity == ACTIVITY_IRRIGATE 
      || (activity == ACTIVITY_RAILROAD
	  && player_knows_techs_with_flag(pplayer, TF_RAILROAD))
      || (activity == ACTIVITY_FORTRESS 
	  && player_knows_techs_with_flag(pplayer, TF_FORTRESS)))
  return TRUE;

  return FALSE;
}

/**************************************************************************
Return name of activity in static buffer
**************************************************************************/
char* get_activity_text (int activity)
{
  char *text;

  switch (activity) {
  case ACTIVITY_IDLE:		text = _("Idle"); break;
  case ACTIVITY_POLLUTION:	text = _("Pollution"); break;
  case ACTIVITY_ROAD:		text = _("Road"); break;
  case ACTIVITY_MINE:		text = _("Mine"); break;
  case ACTIVITY_IRRIGATE:	text = _("Irrigation"); break;
  case ACTIVITY_FORTIFYING:	text = _("Fortifying"); break;
  case ACTIVITY_FORTIFIED:	text = _("Fortified"); break;
  case ACTIVITY_FORTRESS:	text = _("Fortress"); break;
  case ACTIVITY_SENTRY:		text = _("Sentry"); break;
  case ACTIVITY_RAILROAD:	text = _("Railroad"); break;
  case ACTIVITY_PILLAGE:	text = _("Pillage"); break;
  case ACTIVITY_GOTO:		text = _("Goto"); break;
  case ACTIVITY_EXPLORE:	text = _("Explore"); break;
  case ACTIVITY_TRANSFORM:	text = _("Transform"); break;
  case ACTIVITY_AIRBASE:	text = _("Airbase"); break;
  case ACTIVITY_FALLOUT:	text = _("Fallout"); break;
  default:			text = _("Unknown"); break;
  }

  return text;
}

/****************************************************************************
  Return TRUE iff the given unit can be loaded into the transporter.
****************************************************************************/
bool can_unit_load(struct unit *pcargo, struct unit *ptrans)
{
  /* This function needs to check EVERYTHING. */

  if (!pcargo || !ptrans) {
    return FALSE;
  }

  /* Check positions of the units.  Of course you can't load a unit onto
   * a transporter on a different tile... */
  if (!same_pos(pcargo->x, pcargo->y, ptrans->x, ptrans->y)) {
    return FALSE;
  }

  /* Double-check ownership of the units: you can load into an allied unit
   * (of course only allied units can be on the same tile). */
  if (!pplayers_allied(unit_owner(pcargo), unit_owner(ptrans))) {
    return FALSE;
  }

  /* Only top-level transporters may be loaded or loaded into. */
  if (pcargo->transported_by != -1 || ptrans->transported_by != -1) {
    return FALSE;
  }

  /* Make sure this transporter can carry this type of unit. */
  if (is_ground_unit(pcargo)) {
    if (!is_ground_units_transport(ptrans)) {
      return FALSE;
    }
  } else if (unit_flag(pcargo, F_MISSILE)) {
    if (!is_air_units_transport(ptrans)) {
      return FALSE;
    }
  } else if (is_air_unit(pcargo) || is_heli_unit(pcargo)) {
    if (!unit_flag(ptrans, F_CARRIER)) {
      return FALSE;
    }
  } else {
    return FALSE;
  }

  /* Make sure there's room in the transporter. */
  return (get_transporter_occupancy(ptrans)
	  < get_transporter_capacity(ptrans));
}

/****************************************************************************
  Return TRUE iff the given unit can be unloaded from its current
  transporter.

  This function checks everything *except* the legality of the position
  after the unloading.  The caller may also want to call
  can_unit_exist_at_tile() to check this, unless the unit is unloading and
  moving at the same time.
****************************************************************************/
bool can_unit_unload(struct unit *pcargo, struct unit *ptrans)
{
  if (!pcargo || !ptrans) {
    return FALSE;
  }

  /* Make sure the unit's transporter exists and is known. */
  if (pcargo->transported_by != ptrans->id) {
    return FALSE;
  }

  /* Only top-level transporters may be unloaded.  However the unit being
   * unloaded may be transporting other units. */
  if (ptrans->transported_by != -1) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
Return whether the unit can be paradropped.
That is if the unit is in a friendly city or on an Airbase
special, have enough movepoints left and have not paradropped
before in this turn.
**************************************************************************/
bool can_unit_paradrop(struct unit *punit)
{
  struct unit_type *utype;
  struct tile *ptile;

  if (!unit_flag(punit, F_PARATROOPERS))
    return FALSE;

  if(punit->paradropped)
    return FALSE;

  utype = unit_type(punit);

  if(punit->moves_left < utype->paratroopers_mr_req)
    return FALSE;

  ptile=map_get_tile(punit->x, punit->y);
  if (tile_has_special(ptile, S_AIRBASE))
    return TRUE;

  if(!map_get_city(punit->x, punit->y))
    return FALSE;

  return TRUE;
}

/**************************************************************************
  Return whether the unit can bombard.
  Basically if it is a bombarder, isn't being transported, and hasn't 
  moved this turn.
**************************************************************************/
bool can_unit_bombard(struct unit *punit)
{
  if (!unit_flag(punit, F_BOMBARDER)) {
    return FALSE;
  }

  if (punit->transported_by != -1) {
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
  enum unit_activity current2 = 
              (current == ACTIVITY_FORTIFIED) ? ACTIVITY_FORTIFYING : current;
  bool result;

  if (punit->connecting)
    return can_unit_do_connect(punit, current);

  punit->activity = ACTIVITY_IDLE;
  punit->activity_target = S_NO_SPECIAL;

  result = can_unit_do_activity_targeted(punit, current2, target);

  punit->activity = current;
  punit->activity_target = target;

  return result;
}

/**************************************************************************
...
**************************************************************************/
bool can_unit_do_activity(struct unit *punit, enum unit_activity activity)
{
  return can_unit_do_activity_targeted(punit, activity, S_NO_SPECIAL);
}

/**************************************************************************
  Return whether the unit can do the targeted activity at its current
  location.
**************************************************************************/
bool can_unit_do_activity_targeted(struct unit *punit,
				   enum unit_activity activity,
				   enum tile_special_type target)
{
  return can_unit_do_activity_targeted_at(punit, activity, target,
					  punit->x, punit->y);
}

/**************************************************************************
  Return TRUE if the unit can do the targeted activity at the given
  location.

  Note that if you make changes here you should also change the code for
  autosettlers in server/settler.c. The code there does not use this
  function as it would be a major CPU hog.
**************************************************************************/
bool can_unit_do_activity_targeted_at(struct unit *punit,
				      enum unit_activity activity,
				      enum tile_special_type target,
				      int map_x, int map_y)
{
  struct player *pplayer = unit_owner(punit);
  struct tile *ptile = map_get_tile(map_x, map_y);
  struct tile_type *type = get_tile_type(ptile->terrain);

  switch(activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_GOTO:
    return TRUE;

  case ACTIVITY_POLLUTION:
    return (unit_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_POLLUTION));

  case ACTIVITY_FALLOUT:
    return (unit_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_FALLOUT));

  case ACTIVITY_ROAD:
    return (terrain_control.may_road
	    && unit_flag(punit, F_SETTLERS)
	    && !tile_has_special(ptile, S_ROAD)
	    && type->road_time != 0
	    && (!tile_has_special(ptile, S_RIVER)
		|| player_knows_techs_with_flag(pplayer, TF_BRIDGE)));

  case ACTIVITY_MINE:
    /* Don't allow it if someone else is irrigating this tile.
     * *Do* allow it if they're transforming - the mine may survive */
    if (terrain_control.may_mine
	&& unit_flag(punit, F_SETTLERS)
	&& ((ptile->terrain == type->mining_result
	     && !tile_has_special(ptile, S_MINE))
	    || (ptile->terrain != type->mining_result
		&& type->mining_result != T_LAST
		&& (!is_ocean(ptile->terrain)
		    || is_ocean(type->mining_result)
		    || can_reclaim_ocean(map_x, map_y))
		&& (is_ocean(ptile->terrain)
		    || !is_ocean(type->mining_result)
		    || can_channel_land(map_x, map_y))
		&& (!is_ocean(type->mining_result)
		    || !map_get_city(map_x, map_y))))) {
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
	&& unit_flag(punit, F_SETTLERS)
	&& (!tile_has_special(ptile, S_IRRIGATION)
	    || (!tile_has_special(ptile, S_FARMLAND)
		&& player_knows_techs_with_flag(pplayer, TF_FARMLAND)))
	&& ((ptile->terrain == type->irrigation_result
	     && is_water_adjacent_to_tile(map_x, map_y))
	    || (ptile->terrain != type->irrigation_result
		&& type->irrigation_result != T_LAST
		&& (!is_ocean(ptile->terrain)
		    || is_ocean(type->irrigation_result)
		    || can_reclaim_ocean(map_x, map_y))
		&& (is_ocean(ptile->terrain)
		    || !is_ocean(type->irrigation_result)
		    || can_channel_land(map_x, map_y))
		&& (!is_ocean(type->irrigation_result)
		    || !map_get_city(map_x, map_y))))) {
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
    return (is_ground_unit(punit)
	    && punit->activity != ACTIVITY_FORTIFIED
	    && !unit_flag(punit, F_SETTLERS)
	    && !is_ocean(ptile->terrain));

  case ACTIVITY_FORTIFIED:
    return FALSE;

  case ACTIVITY_FORTRESS:
    return (unit_flag(punit, F_SETTLERS)
	    && !map_get_city(map_x, map_y)
	    && player_knows_techs_with_flag(pplayer, TF_FORTRESS)
	    && !tile_has_special(ptile, S_FORTRESS)
	    && !is_ocean(ptile->terrain));

  case ACTIVITY_AIRBASE:
    return (unit_flag(punit, F_AIRBASE)
	    && player_knows_techs_with_flag(pplayer, TF_AIRBASE)
	    && !tile_has_special(ptile, S_AIRBASE)
	    && !is_ocean(ptile->terrain));

  case ACTIVITY_SENTRY:
    return TRUE;

  case ACTIVITY_RAILROAD:
    /* if the tile has road, the terrain must be ok.. */
    return (terrain_control.may_road
	    && unit_flag(punit, F_SETTLERS)
	    && tile_has_special(ptile, S_ROAD)
	    && !tile_has_special(ptile, S_RAILROAD)
	    && player_knows_techs_with_flag(pplayer, TF_RAILROAD));

  case ACTIVITY_PILLAGE:
    {
      enum tile_special_type pspresent = get_tile_infrastructure_set(ptile);
      enum tile_special_type psworking;

      if (pspresent != S_NO_SPECIAL && is_ground_unit(punit)) {
	psworking = get_unit_tile_pillage_set(map_x, map_y);
	if (ptile->city && (contains_special(target, S_ROAD)
			    || contains_special(target, S_RAILROAD))) {
	  return FALSE;
	}
	if (target == S_NO_SPECIAL) {
	  if (ptile->city) {
	    return ((pspresent & (~(psworking | S_ROAD | S_RAILROAD)))
		    != S_NO_SPECIAL);
	  } else {
	    return ((pspresent & (~psworking)) != S_NO_SPECIAL);
	  }
	} else if (!game.rgame.pillage_select
		   && target != get_preferred_pillage(pspresent)) {
	  return FALSE;
	} else {
	  return ((pspresent & (~psworking) & target) != S_NO_SPECIAL);
	}
      } else {
	return FALSE;
      }
    }

  case ACTIVITY_EXPLORE:
    return (is_ground_unit(punit) || is_sailing_unit(punit));

  case ACTIVITY_TRANSFORM:
    return (terrain_control.may_transform
	    && type->transform_result != T_LAST
	    && ptile->terrain != type->transform_result
	    && (!is_ocean(ptile->terrain)
		|| is_ocean(type->transform_result)
		|| can_reclaim_ocean(map_x, map_y))
	    && (is_ocean(ptile->terrain)
		|| !is_ocean(type->transform_result)
		|| can_channel_land(map_x, map_y))
	    && (!is_ocean(type->transform_result)
		|| !(map_get_city(map_x, map_y)))
	    && unit_flag(punit, F_TRANSFORM));

  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
  case ACTIVITY_UNKNOWN:
    break;
  }
  freelog(LOG_ERROR,
	  "Unknown activity %d in can_unit_do_activity_targeted_at()",
	  activity);
  return FALSE;
}

/**************************************************************************
  assign a new task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity)
{
  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target = S_NO_SPECIAL;
  punit->connecting = FALSE;
  if (new_activity == ACTIVITY_IDLE && punit->moves_left > 0) {
    /* No longer done. */
    punit->done_moving = FALSE;
  }
}

/**************************************************************************
  assign a new targeted task to a unit.
**************************************************************************/
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity,
				enum tile_special_type new_target)
{
  set_unit_activity(punit, new_activity);
  punit->activity_target = new_target;
}

/**************************************************************************
...
**************************************************************************/
bool is_unit_activity_on_tile(enum unit_activity activity, int x, int y)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit) 
    if(punit->activity==activity)
      return TRUE;
  unit_list_iterate_end;
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
enum tile_special_type get_unit_tile_pillage_set(int x, int y)
{
  enum tile_special_type tgt_ret = S_NO_SPECIAL;

  unit_list_iterate(map_get_tile(x, y)->units, punit)
    if(punit->activity==ACTIVITY_PILLAGE)
      tgt_ret |= punit->activity_target;
  unit_list_iterate_end;
  return tgt_ret;
}

/**************************************************************************
 ...
**************************************************************************/
const char *unit_activity_text(struct unit *punit)
{
  static char text[64];
  char *moves_str;
   
  switch(punit->activity) {
   case ACTIVITY_IDLE:
     moves_str = _("Moves");
     if (is_air_unit(punit) && unit_type(punit)->fuel > 0) {
       int rate,f;
       rate=unit_type(punit)->move_rate/SINGLE_MOVE;
       f=((punit->fuel)-1);
      if ((punit->moves_left % SINGLE_MOVE) != 0) {
	 if(punit->moves_left/SINGLE_MOVE>0) {
	   my_snprintf(text, sizeof(text), "%s: (%d)%d %d/%d", moves_str,
		       ((rate*f)+(punit->moves_left/SINGLE_MOVE)),
		       punit->moves_left/SINGLE_MOVE, punit->moves_left%SINGLE_MOVE,
		       SINGLE_MOVE);
	 } else {
	   my_snprintf(text, sizeof(text), "%s: (%d)%d/%d", moves_str,
		       ((rate*f)+(punit->moves_left/SINGLE_MOVE)),
		       punit->moves_left%SINGLE_MOVE, SINGLE_MOVE);
	 }
       } else {
	 my_snprintf(text, sizeof(text), "%s: (%d)%d", moves_str,
		     rate*f+punit->moves_left/SINGLE_MOVE,
		     punit->moves_left/SINGLE_MOVE);
       }
     } else {
      if ((punit->moves_left % SINGLE_MOVE) != 0) {
	 if(punit->moves_left/SINGLE_MOVE>0) {
	   my_snprintf(text, sizeof(text), "%s: %d %d/%d", moves_str,
		       punit->moves_left/SINGLE_MOVE, punit->moves_left%SINGLE_MOVE,
		       SINGLE_MOVE);
	 } else {
	   my_snprintf(text, sizeof(text),
		       "%s: %d/%d", moves_str, punit->moves_left%SINGLE_MOVE,
		       SINGLE_MOVE);
	 }
       } else {
	 my_snprintf(text, sizeof(text),
		     "%s: %d", moves_str, punit->moves_left/SINGLE_MOVE);
       }
     }
     return text;
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
     return get_activity_text (punit->activity);
   case ACTIVITY_PILLAGE:
     if(punit->activity_target == S_NO_SPECIAL) {
       return get_activity_text (punit->activity);
     } else {
       my_snprintf(text, sizeof(text), "%s: %s",
		   get_activity_text (punit->activity),
		   map_get_infrastructure_text(punit->activity_target));
       return (text);
     }
   default:
    die("Unknown unit activity %d in unit_activity_text()", punit->activity);
  }
  return NULL;
}

/**************************************************************************
...
**************************************************************************/
struct unit *unit_list_find(struct unit_list *This, int id)
{
  unit_list_iterate(*This, punit) {
    if (punit->id == id) {
      return punit;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 Comparison function for genlist_sort, sorting by ord_map:
 The indirection is a bit gory:
 Read from the right:
   1. cast arg "a" to "ptr to void*"   (we're sorting a list of "void*"'s)
   2. dereference to get the "void*"
   3. cast that "void*" to a "struct unit*"
**************************************************************************/
static int compar_unit_ord_map(const void *a, const void *b)
{
  const struct unit *ua, *ub;
  ua = (const struct unit*) *(const void**)a;
  ub = (const struct unit*) *(const void**)b;
  return ua->ord_map - ub->ord_map;
}

/**************************************************************************
 Comparison function for genlist_sort, sorting by ord_city: see above.
**************************************************************************/
static int compar_unit_ord_city(const void *a, const void *b)
{
  const struct unit *ua, *ub;
  ua = (const struct unit*) *(const void**)a;
  ub = (const struct unit*) *(const void**)b;
  return ua->ord_city - ub->ord_city;
}

/**************************************************************************
...
**************************************************************************/
void unit_list_sort_ord_map(struct unit_list *This)
{
  if (unit_list_size(This) > 1) {
    genlist_sort(&This->list, compar_unit_ord_map);
  }
}

/**************************************************************************
...
**************************************************************************/
void unit_list_sort_ord_city(struct unit_list *This)
{
  if (unit_list_size(This) > 1) {
    genlist_sort(&This->list, compar_unit_ord_city);
  }
}

/**************************************************************************
...
**************************************************************************/
struct player *unit_owner(struct unit *punit)
{
  return (&game.players[punit->owner]);
}

/****************************************************************************
  Measure the carrier (missile + airplane) capacity of the given tile for
  a player.

  In the future this should probably look at the actual occupancy of the
  transporters.  However for now we only look at the potential capacity and
  leave loading up to the caller.
****************************************************************************/
static void count_carrier_capacity(int *airall, int *misonly,
				   int x, int y, struct player *pplayer,
				   bool count_units_with_extra_fuel)
{
  struct tile *ptile = map_get_tile(x, y);

  *airall = *misonly = 0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (unit_owner(punit) == pplayer) {
      if (unit_flag(punit, F_CARRIER)
	  && !(is_ground_unit(punit) && is_ocean(ptile->terrain))) {
	*airall += get_transporter_capacity(punit);
	continue;
      }
      if (unit_flag(punit, F_MISSILE_CARRIER)
	  && !(is_ground_unit(punit) && is_ocean(ptile->terrain))) {
	*misonly += get_transporter_capacity(punit);
	continue;
      }

      /* Don't count units which have enough fuel (>1) */
      if (is_air_unit(punit)
	  && (count_units_with_extra_fuel || punit->fuel <= 1)) {
	if (unit_flag(punit, F_MISSILE)) {
	  (*misonly)--;
	} else {
	  (*airall)--;
	}
      }
    }
  } unit_list_iterate_end;
}

/**************************************************************************
Returns the number of free spaces for missiles. Can be 0 or negative.
**************************************************************************/
int missile_carrier_capacity(int x, int y, struct player *pplayer,
			     bool count_units_with_extra_fuel)
{
  int airall, misonly;

  count_carrier_capacity(&airall, &misonly, x, y, pplayer,
			 count_units_with_extra_fuel);

  /* Any extra air spaces can be used by missles, but if there aren't enough
   * air spaces this doesn't bother missiles. */
  return MAX(airall, 0) + misonly;
}

/**************************************************************************
Returns the number of free spaces for airunits (includes missiles).
Can be 0 or negative.
**************************************************************************/
int airunit_carrier_capacity(int x, int y, struct player *pplayer,
			     bool count_units_with_extra_fuel)
{
  int airall, misonly;

  count_carrier_capacity(&airall, &misonly, x, y, pplayer,
			 count_units_with_extra_fuel);

  /* Any extra missile spaces are useless to air units, but if there aren't
   * enough missile spaces the missles must take up airunit capacity. */
  return airall + MIN(misonly, 0);
}

/**************************************************************************
Returns true if the tile contains an allied unit and only allied units.
(ie, if your nation A is allied with B, and B is allied with C, a tile
containing units from B and C will return false)
**************************************************************************/
struct unit *is_allied_unit_tile(struct tile *ptile,
				 struct player *pplayer)
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

/**************************************************************************
 is there an enemy unit on this tile?
**************************************************************************/
struct unit *is_enemy_unit_tile(struct tile *ptile, struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_at_war(unit_owner(punit), pplayer))
      return punit;
  }
  unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 is there an non-allied unit on this tile?
**************************************************************************/
struct unit *is_non_allied_unit_tile(struct tile *ptile,
				     struct player *pplayer)
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
struct unit *is_non_attack_unit_tile(struct tile *ptile,
				     struct player *pplayer)
{
  unit_list_iterate(ptile->units, punit) {
    if (pplayers_non_attack(unit_owner(punit), pplayer))
      return punit;
  }
  unit_list_iterate_end;

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
bool is_my_zoc(struct player *pplayer, int x0, int y0)
{
  square_iterate(x0, y0, 1, x1, y1) {
    struct tile *ptile = map_get_tile(x1, y1);
    
    if (is_ocean(ptile->terrain)) {
      continue;
    }
    if (is_non_allied_unit_tile(ptile, pplayer)) {
      /* Note: in the client, the above function will return NULL 
       * if there is a city there, even if the city is occupied */
      return FALSE;
    }
    
    if (!is_server) {
      struct city *pcity = is_non_allied_city_tile(ptile, pplayer);

      if (pcity 
          && (pcity->client.occupied 
              || map_get_known(x1, y1, pplayer) == TILE_KNOWN_FOGGED)) {
        /* If the city is fogged, we assume it's occupied */
        return FALSE;
      }
    }
  } square_iterate_end;

  return TRUE;
}

/**************************************************************************
  Takes into account unit move_type as well as IGZOC
**************************************************************************/
bool unit_type_really_ignores_zoc(Unit_Type_id type)
{
  return (!is_ground_unittype(type)) || (unit_type_flag(type, F_IGZOC));
}

/**************************************************************************
  Returns whether the unit is allowed (by ZOC) to move from (src_x,src_y)
  to (dest_x,dest_y) (assumed adjacent).
  You CAN move if:
  1. You have units there already
  2. Your unit isn't a ground unit
  3. Your unit ignores ZOC (diplomat, freight, etc.)
  4. You're moving from or to a city
  5. You're moving from an ocean square (from a boat)
  6. The spot you're moving from or to is in your ZOC
**************************************************************************/
bool can_step_taken_wrt_to_zoc(Unit_Type_id type,
			      struct player *unit_owner, int src_x,
			      int src_y, int dest_x, int dest_y)
{
  if (unit_type_really_ignores_zoc(type))
    return TRUE;
  if (is_allied_unit_tile(map_get_tile(dest_x, dest_y), unit_owner))
    return TRUE;
  if (map_get_city(src_x, src_y) || map_get_city(dest_x, dest_y))
    return TRUE;
  if (is_ocean(map_get_terrain(src_x, src_y)) ||
      is_ocean(map_get_terrain(dest_x, dest_y))) {
    return TRUE;
  }
  return (is_my_zoc(unit_owner, src_x, src_y) ||
	  is_my_zoc(unit_owner, dest_x, dest_y));
}

/**************************************************************************
...
**************************************************************************/
static bool zoc_ok_move_gen(struct unit *punit, int x1, int y1, int x2,
			    int y2)
{
  return can_step_taken_wrt_to_zoc(punit->type, unit_owner(punit),
				   x1, y1, x2, y2);
}

/**************************************************************************
  Convenience wrapper for zoc_ok_move_gen(), using the unit's (x,y)
  as the starting point.
**************************************************************************/
bool zoc_ok_move(struct unit *punit, int x, int y)
{
  return zoc_ok_move_gen(punit, punit->x, punit->y, x, y);
}

/****************************************************************************
  Return TRUE iff the unit can "exist" at this location.  This means it can
  physically be present on the tile (without the use of a transporter).
****************************************************************************/
bool can_unit_exist_at_tile(struct unit *punit, int x, int y)
{
  struct tile *ptile = map_get_tile(x, y);

  if (ptile->city) {
    return TRUE;
  }

  switch (unit_types[punit->type].move_type) {
  case LAND_MOVING:
    return !is_ocean(ptile->terrain);
  case SEA_MOVING:
    return is_ocean(ptile->terrain);
  case AIR_MOVING:
  case HELI_MOVING:
    return TRUE;
  }
  die("Invalid move type");
  return FALSE;
}

/****************************************************************************
  Return TRUE iff the unit can "survive" at this location.  This means it can
  not only be phsically present at the tile but will be able to survive
  indefinitely on its own (without a transporter).  Units that require fuel
  or have a danger of drowning are examples of non-survivable units.
****************************************************************************/
bool can_unit_survive_at_tile(struct unit *punit, int x, int y)
{
  if (!can_unit_exist_at_tile(punit, x, y)) {
    return FALSE;
  }

  if (map_get_city(x, y)) {
    return TRUE;
  }

  /* TODO: check for dangerous positions (like triremes in deep water). */

  switch (unit_types[punit->type].move_type) {
  case LAND_MOVING:
  case SEA_MOVING:
    return TRUE;
  case AIR_MOVING:
  case HELI_MOVING:
    return FALSE;
  }
  die("Invalid move type");
  return TRUE;
}

/**************************************************************************
  Convenience wrapper for test_unit_move_to_tile.
**************************************************************************/
bool can_unit_move_to_tile(struct unit *punit, int dest_x, int dest_y,
			   bool igzoc)
{
  return MR_OK == test_unit_move_to_tile(punit->type, unit_owner(punit),
					 punit->activity, punit->connecting,
					 punit->x, punit->y, dest_x, dest_y,
					 igzoc);
}

/**************************************************************************
  unit can be moved if:
  1) the unit is idle or on goto or connecting.
  2) the target location is on the map
  3) the target location is next to the unit
  4) there are no non-allied units on the target tile
  5) a ground unit can only move to ocean squares if there
     is a transporter with free capacity
  6) marines are the only units that can attack from a ocean square
  7) naval units can only be moved to ocean squares or city squares
  8) there are no peaceful but un-allied units on the target tile
  9) there is not a peaceful but un-allied city on the target tile
  10) there is no non-allied unit blocking (zoc) [or igzoc is true]
**************************************************************************/
enum unit_move_result test_unit_move_to_tile(Unit_Type_id type,
					     struct player *unit_owner,
					     enum unit_activity activity,
					     bool connecting, int src_x,
					     int src_y, int dest_x,
					     int dest_y, bool igzoc)
{
  struct tile *pfromtile, *ptotile;
  bool zoc;
  struct city *pcity;

  /* 1) */
  if (activity != ACTIVITY_IDLE
      && activity != ACTIVITY_GOTO
      && !connecting) {
    return MR_BAD_ACTIVITY;
  }

  /* 2) */
  if (!normalize_map_pos(&dest_x, &dest_y)) {
    return MR_BAD_MAP_POSITION;
  }

  /* 3) */
  if (!is_tiles_adjacent(src_x, src_y, dest_x, dest_y)) {
    return MR_BAD_DESTINATION;
  }

  pfromtile = map_get_tile(src_x, src_y);
  ptotile = map_get_tile(dest_x, dest_y);

  /* 4) */
  if (is_non_allied_unit_tile(ptotile, unit_owner)) {
    return MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_UNIT;
  }

  if (unit_types[type].move_type == LAND_MOVING) {
    /* 5) */
    if (is_ocean(ptotile->terrain) &&
	ground_unit_transporter_capacity(dest_x, dest_y, unit_owner) <= 0) {
      return MR_NO_SEA_TRANSPORTER_CAPACITY;
    }

    /* Moving from ocean */
    if (is_ocean(pfromtile->terrain)) {
      /* 6) */
      if (!unit_type_flag(type, F_MARINES)
	  && is_enemy_city_tile(ptotile, unit_owner)) {
	return MR_BAD_TYPE_FOR_CITY_TAKE_OVER;
      }
    }
  } else if (unit_types[type].move_type == SEA_MOVING) {
    /* 7) */
    if (!is_ocean(ptotile->terrain)
	&& ptotile->terrain != T_UNKNOWN
	&& !is_allied_city_tile(ptotile, unit_owner)) {
      return MR_DESTINATION_OCCUPIED_BY_NON_ALLIED_CITY;
    }
  }

  /* 8) */
  if (is_non_attack_unit_tile(ptotile, unit_owner)) {
    return MR_NO_WAR;
  }

  /* 9) */
  pcity = ptotile->city;
  if (pcity && pplayers_non_attack(city_owner(pcity), unit_owner)) {
    return MR_NO_WAR;
  }

  /* 10) */
  zoc = igzoc
      || can_step_taken_wrt_to_zoc(type, unit_owner, src_x,
				   src_y, dest_x, dest_y);
  if (!zoc) {
    return MR_ZOC;
  }

  return MR_OK;
}

/**************************************************************************
  Like base_trireme_loss_pct but take the position into account.
**************************************************************************/
int trireme_loss_pct(struct player *pplayer, int x, int y,
		     struct unit *punit)
{
  /*
   * If we are in a city or next to land, we have no chance of losing
   * the ship.  To make this really useful for ai planning purposes,
   * we'd need to confirm that we can exist/move at the (x, y)
   * location we are given.
   */
  if (!is_ocean(map_get_terrain(x, y)) || is_coastline(x, y)) {
    return 0;
  } else {
    return base_trireme_loss_pct(pplayer, punit);
  }
}

/**************************************************************************
  Triremes have a varying loss percentage based on tech and veterancy
  level.
**************************************************************************/
int base_trireme_loss_pct(struct player *pplayer, struct unit *punit)
{
  if (player_owns_active_wonder(pplayer, B_LIGHTHOUSE)) {
    return 0;
  } else if (player_knows_techs_with_flag(pplayer, TF_REDUCE_TRIREME_LOSS2)) {
    return game.trireme_loss_chance[punit->veteran] / 4;
  } else if (player_knows_techs_with_flag(pplayer, TF_REDUCE_TRIREME_LOSS1)) {
    return game.trireme_loss_chance[punit->veteran] / 2;
  } else {
    return game.trireme_loss_chance[punit->veteran];
  }
}

/**************************************************************************
An "aggressive" unit is a unit which may cause unhappiness
under a Republic or Democracy.
A unit is *not* aggressive if one or more of following is true:
- zero attack strength
- inside a city
- ground unit inside a fortress within 3 squares of a friendly city
**************************************************************************/
bool unit_being_aggressive(struct unit *punit)
{
  if (!is_attack_unit(punit))
    return FALSE;
  if (map_get_city(punit->x,punit->y))
    return FALSE;
  if (is_ground_unit(punit) &&
      map_has_special(punit->x, punit->y, S_FORTRESS))
    return !is_unit_near_a_friendly_city (punit);
  
  return TRUE;
}

/*
 * Returns true if given activity is some kind of building/cleaning.
 */
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
    return TRUE;
  default:
    return FALSE;
  }
}

/**************************************************************************
  Create a virtual unit skeleton. pcity can be NULL, but then you need
  to set x, y and homecity yourself.
**************************************************************************/
struct unit *create_unit_virtual(struct player *pplayer, struct city *pcity,
                                 Unit_Type_id type, int veteran_level)
{
  struct unit *punit = fc_calloc(1, sizeof(struct unit));

  punit->type = type;
  punit->owner = pplayer->player_no;
  if (pcity) {
    CHECK_MAP_POS(pcity->x, pcity->y);
    punit->x = pcity->x;
    punit->y = pcity->y;
    punit->homecity = pcity->id;
  } else {
    punit->x = -1;
    punit->y = -1;
    punit->homecity = 0;
  }
  clear_goto_dest(punit);
  punit->veteran = veteran_level;
  punit->upkeep = 0;
  punit->upkeep_food = 0;
  punit->upkeep_gold = 0;
  punit->unhappiness = 0;
  /* A unit new and fresh ... */
  punit->foul = FALSE;
  punit->debug = FALSE;
  punit->fuel = unit_type(punit)->fuel;
  punit->hp = unit_type(punit)->hp;
  punit->moves_left = unit_move_rate(punit);
  punit->moved = FALSE;
  punit->paradropped = FALSE;
  punit->connecting = FALSE;
  punit->done_moving = FALSE;
  if (is_barbarian(pplayer)) {
    punit->fuel = BARBARIAN_LIFE;
  }
  punit->ai.control = FALSE;
  punit->ai.ai_role = AIUNIT_NONE;
  punit->ai.ferryboat = 0;
  punit->ai.passenger = 0;
  punit->ai.bodyguard = 0;
  punit->ai.charge = 0;
  punit->bribe_cost = -1; /* flag value */
  punit->transported_by = -1;
  punit->focus_status = FOCUS_AVAIL;
  punit->ord_map = 0;
  punit->ord_city = 0;
  set_unit_activity(punit, ACTIVITY_IDLE);
  punit->occupy = 0;
  punit->has_orders = FALSE;

  return punit;
}

/**************************************************************************
  Free the memory used by virtual unit. By the time this function is
  called, you should already have unregistered it everywhere.
**************************************************************************/
void destroy_unit_virtual(struct unit *punit)
{
  free_unit_orders(punit);
  free(punit);
}

/**************************************************************************
  Free and reset the unit's goto route (punit->pgr).  Only used by the
  server.
**************************************************************************/
void free_unit_orders(struct unit *punit)
{
  if (punit->has_orders) {
    if (is_goto_dest_set(punit)) {
      clear_goto_dest(punit);
    }
    free(punit->orders.list);
    punit->orders.list = NULL;
  }
  punit->has_orders = FALSE;
}

/****************************************************************************
  Expensive function to check how many units are in the transport.
****************************************************************************/
int get_transporter_occupancy(struct unit *ptrans)
{
  int occupied = 0;

  unit_list_iterate(map_get_tile(ptrans->x, ptrans->y)->units, pcargo) {
    if (pcargo->transported_by == ptrans->id) {
      occupied++;
    }
  } unit_list_iterate_end;

  return occupied;
}

/****************************************************************************
  Find a transporter at the given location for the unit.
****************************************************************************/
struct unit *find_transporter_for_unit(struct unit *pcargo, int x, int y)
{ 
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(ptile->units, ptrans) {
    if (can_unit_load(pcargo, ptrans)) {
      return ptrans;
    }
  } unit_list_iterate_end;

  return FALSE;
}

/***************************************************************************
  Tests if the unit could be updated. Returns UR_OK if is this is
  possible.

  is_free should be set if the unit upgrade is "free" (e.g., Leonardo's).
  Otherwise money is needed and the unit must be in an owned city.

  Note that this function is strongly tied to unittools.c:upgrade_unit().
***************************************************************************/
enum unit_upgrade_result test_unit_upgrade(struct unit *punit, bool is_free)
{
  struct player *pplayer = unit_owner(punit);
  Unit_Type_id to_unittype = can_upgrade_unittype(pplayer, punit->type);
  struct city *pcity;
  int cost;

  if (to_unittype == -1) {
    return UR_NO_UNITTYPE;
  }

  if (!is_free) {
    cost = unit_upgrade_price(pplayer, punit->type, to_unittype);
    if (pplayer->economic.gold < cost) {
      return UR_NO_MONEY;
    }

    pcity = map_get_city(punit->x, punit->y);
    if (!pcity) {
      return UR_NOT_IN_CITY;
    }
    if (city_owner(pcity) != pplayer) {
      /* TODO: should upgrades in allied cities be possible? */
      return UR_NOT_CITY_OWNER;
    }
  }

  if (get_transporter_occupancy(punit) >
      unit_types[to_unittype].transport_capacity) {
    /* TODO: allow transported units to be reassigned.  Check for
     * ground_unit_transporter_capacity here and make changes to
     * upgrade_unit. */
    return UR_NOT_ENOUGH_ROOM;
  }

  return UR_OK;
}

/**************************************************************************
  Find the result of trying to upgrade the unit, and a message that
  most callers can use directly.
**************************************************************************/
enum unit_upgrade_result get_unit_upgrade_info(char *buf, size_t bufsz,
					       struct unit *punit)
{
  struct player *pplayer = unit_owner(punit);
  enum unit_upgrade_result result = test_unit_upgrade(punit, FALSE);
  int upgrade_cost;
  Unit_Type_id from_unittype = punit->type;
  Unit_Type_id to_unittype = can_upgrade_unittype(pplayer,
						  punit->type);

  switch (result) {
  case UR_OK:
    upgrade_cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    /* This message is targeted toward the GUI callers. */
    my_snprintf(buf, bufsz, _("Upgrade %s to %s for %d gold?\n"
			      "Treasury contains %d gold."),
		unit_types[from_unittype].name, unit_types[to_unittype].name,
		upgrade_cost, pplayer->economic.gold);
    break;
  case UR_NO_UNITTYPE:
    my_snprintf(buf, bufsz,
		_("Sorry, cannot upgrade %s (yet)."),
		unit_types[from_unittype].name);
    break;
  case UR_NO_MONEY:
    upgrade_cost = unit_upgrade_price(pplayer, from_unittype, to_unittype);
    my_snprintf(buf, bufsz,
		_("Upgrading %s to %s costs %d gold.\n"
		  "Treasury contains %d gold."),
		unit_types[from_unittype].name, unit_types[to_unittype].name,
		upgrade_cost, pplayer->economic.gold);
    break;
  case UR_NOT_IN_CITY:
  case UR_NOT_CITY_OWNER:
    my_snprintf(buf, bufsz,
		_("You can only upgrade units in your cities."));
    break;
  case UR_NOT_ENOUGH_ROOM:
    my_snprintf(buf, bufsz,
		_("Upgrading this %s would strand units it transports."),
		unit_types[from_unittype].name);
    break;
  }

  return result;
}
