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
...
***************************************************************/
int unit_move_rate(struct unit *punit)
{
  int val;
  struct player *pplayer;
  pplayer = &game.players[punit->owner];
  val = get_unit_type(punit->type)->move_rate;
  if (!is_air_unit(punit) && !is_heli_unit(punit)) 
    val = (val * punit->hp) / get_unit_type(punit->type)->hp;
  if(is_sailing_unit(punit)) {
    if(player_owns_active_wonder(pplayer, B_LIGHTHOUSE)) 
      val+=SINGLE_MOVE;
    if(player_owns_active_wonder(pplayer, B_MAGELLAN)) 
      val += (improvement_variant(B_MAGELLAN)==1) ? SINGLE_MOVE : 2 * SINGLE_MOVE;
    val += player_knows_techs_with_flag(pplayer,TF_BOAT_FAST) * SINGLE_MOVE;
    if (val < 2 * SINGLE_MOVE)
      val = 2 * SINGLE_MOVE;
  }
  if (val < SINGLE_MOVE) val = SINGLE_MOVE;
  return val;
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
int diplomat_can_do_action(struct unit *pdiplomat,
			   enum diplomat_actions action, 
			   int destx, int desty)
{
  if(!is_diplomat_action_available(pdiplomat, action, destx, desty))
    return 0;

  if(!is_tiles_adjacent(pdiplomat->x, pdiplomat->y, destx, desty)
     && !same_pos(pdiplomat->x, pdiplomat->y, destx, desty))
    return 0;

  if(!pdiplomat->moves_left)
    return 0;

  return 1;
}

/**************************************************************************
Whether a diplomat can perform a particular action at a particular
tile.  This does _not_ check whether the diplomat can move there.
If the action is DIPLOMAT_ANY_ACTION, checks whether there is any
action the diplomat can perform at the tile.
**************************************************************************/
int is_diplomat_action_available(struct unit *pdiplomat,
				 enum diplomat_actions action, 
				 int destx, int desty)
{
  struct city *pcity=map_get_city(destx, desty);
  int playerid = pdiplomat->owner;

  if (action!=DIPLOMAT_MOVE && map_get_terrain(pdiplomat->x, pdiplomat->y)==T_OCEAN)
    return 0;

  if (pcity) {
    if(pcity->owner!=pdiplomat->owner &&
       real_map_distance(pdiplomat->x, pdiplomat->y, pcity->x, pcity->y) <= 1) {
      if(action==DIPLOMAT_SABOTAGE)
        return players_at_war(playerid, pcity->owner);
      if(action==DIPLOMAT_MOVE)
        return players_allied(playerid, pcity->owner);
      if(action==DIPLOMAT_EMBASSY &&
	 !is_barbarian(&game.players[pcity->owner]) &&
	 !player_has_embassy(&game.players[pdiplomat->owner], 
			     &game.players[pcity->owner]))
	return 1;
      if(action==SPY_POISON &&
	 pcity->size>1 &&
	 unit_flag(pdiplomat->type, F_SPY))
        return players_at_war(playerid, pcity->owner);
      if(action==DIPLOMAT_INVESTIGATE)
        return 1;
      if(action==DIPLOMAT_STEAL && !is_barbarian(&game.players[pcity->owner]))
        return 1;
      if(action==DIPLOMAT_INCITE)
        return !players_allied(pcity->owner, pdiplomat->owner);
      if(action==DIPLOMAT_ANY_ACTION)
        return 1;
      if (action==SPY_GET_SABOTAGE_LIST && unit_flag(pdiplomat->type, F_SPY))
	return players_at_war(playerid, pcity->owner);
    }
  } else { /* Action against a unit at a tile */
    /* If it is made possible to do action against allied units
       handle_unit_move_request() should be changed so that pdefender
       is also set to allied units */
    struct tile *ptile = map_get_tile(destx, desty);
    struct unit *punit;

    if ((action==SPY_SABOTAGE_UNIT || action==DIPLOMAT_ANY_ACTION) &&
	unit_list_size(&ptile->units)==1 &&
	unit_flag(pdiplomat->type, F_SPY)) {
      punit = unit_list_get(&ptile->units, 0);
      return players_at_war(playerid, punit->owner);
    }

    if ((action==DIPLOMAT_BRIBE || action==DIPLOMAT_ANY_ACTION) &&
	unit_list_size(&ptile->units)==1) {
      punit = unit_list_get(&ptile->units, 0);
      return !players_allied(punit->owner, pdiplomat->owner);
    }
  }
  return 0;
}

/**************************************************************************
FIXME: Maybe we should allow airlifts between allies
**************************************************************************/
int unit_can_airlift_to(struct unit *punit, struct city *pcity)
{
  struct city *city1;

  if(!punit->moves_left)
    return 0;
  if(!(city1=map_get_city(punit->x, punit->y))) 
    return 0;
  if(city1==pcity)
    return 0;
  if(city1->owner != pcity->owner) 
    return 0;
  if (city1->airlift + pcity->airlift < 2) 
    return 0;
  if (!is_ground_unit(punit))
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int unit_can_help_build_wonder(struct unit *punit, struct city *pcity)
{
  if (!is_tiles_adjacent(punit->x, punit->y, pcity->x, pcity->y)
      && !same_pos(punit->x, punit->y, pcity->x, pcity->y))
    return 0;

  return unit_flag(punit->type, F_CARAVAN)
    && punit->owner==pcity->owner
    && !pcity->is_building_unit
    && is_wonder(pcity->currently_building)
    && pcity->shield_stock < improvement_value(pcity->currently_building);
}


/**************************************************************************
...
**************************************************************************/
int unit_can_help_build_wonder_here(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  return pcity && unit_can_help_build_wonder(punit, pcity);
}


/**************************************************************************
...
**************************************************************************/
int unit_can_est_traderoute_here(struct unit *punit)
{
  struct city *phomecity, *pdestcity;

  if (!unit_flag(punit->type, F_CARAVAN)) return 0;
  pdestcity = map_get_city(punit->x, punit->y);
  if (!pdestcity) return 0;
  phomecity = find_city_by_id(punit->homecity);
  if (!phomecity) return 0;
  return can_establish_trade_route(phomecity, pdestcity);
}

/**************************************************************************
...
**************************************************************************/
int unit_can_defend_here(struct unit *punit)
{
  if(is_ground_unit(punit) && map_get_terrain(punit->x, punit->y)==T_OCEAN)
    return 0;
  
  return 1;
}

/**************************************************************************
Returns the number of free spaces for ground units. Can be 0 or negative.
**************************************************************************/
int ground_unit_transporter_capacity(int x, int y, int playerid)
{
  int availability = 0;
  struct tile *ptile = map_get_tile(x, y);

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner == playerid) {
      if (is_ground_units_transport(punit)
	  && !(is_ground_unit(punit) && ptile->terrain == T_OCEAN))
	availability += get_transporter_capacity(punit);
      else if (is_ground_unit(punit))
	availability--;
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
  return unit_types[punit->type].transport_capacity;
}

/**************************************************************************
...
**************************************************************************/
int is_ground_units_transport(struct unit *punit)
{
  return (get_transporter_capacity(punit)
	  && !unit_flag(punit->type, F_MISSILE_CARRIER)
	  && !unit_flag(punit->type, F_CARRIER));
}

/**************************************************************************
...
**************************************************************************/
int is_air_units_transport(struct unit *punit)
{
  return (get_transporter_capacity(punit)
	  && (unit_flag(punit->type, F_MISSILE_CARRIER)
	      || unit_flag(punit->type, F_CARRIER)));
}

/**************************************************************************
...
**************************************************************************/
int is_sailing_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == SEA_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_air_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == AIR_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_heli_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == HELI_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_ground_unit(struct unit *punit)
{
  return (unit_types[punit->type].move_type == LAND_MOVING);
}

/**************************************************************************
...
**************************************************************************/
int is_military_unit(struct unit *punit)
{
  return (unit_flag(punit->type, F_NONMIL) == 0);
}

/**************************************************************************
...
**************************************************************************/
int is_diplomat_unit(struct unit *punit)
{
  return (unit_flag(punit->type, F_DIPLOMAT));
}

/**************************************************************************
...
**************************************************************************/
int is_ground_threat(struct player *pplayer, struct unit *punit)
{
  return (players_at_war(pplayer->player_no, punit->owner)
	  && (unit_flag(punit->type, F_DIPLOMAT)
	      || (is_ground_unit(punit)
		  && is_military_unit(punit))));
}

/**************************************************************************
...
**************************************************************************/
int is_square_threatened(struct player *pplayer, int x, int y)
{
  int threat=0;

  square_iterate(x, y, 2, x1, y1) {
    unit_list_iterate(map_get_tile(x1, y1)->units, punit) {
      threat += is_ground_threat(pplayer, punit);
    } unit_list_iterate_end;
  } square_iterate_end;

  return threat;
}

/**************************************************************************
...
**************************************************************************/
int is_field_unit(struct unit *punit)
{
  return ((unit_flag(punit->type, F_FIELDUNIT) > 0));
}


/**************************************************************************
  Is the unit one that is invisible on the map, which is currently limited
  to subs and missiles in subs.
  FIXME: this should be made more general: does not handle cargo units
  on an invisible transport, or planes on invisible carrier.
**************************************************************************/
int is_hiding_unit(struct unit *punit)
{
  if(unit_flag(punit->type, F_PARTIAL_INVIS)) return 1;
  if(unit_flag(punit->type, F_MISSILE)) {
    if(map_get_terrain(punit->x, punit->y)==T_OCEAN) {
      unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
	if(unit_flag(punit2->type, F_PARTIAL_INVIS)
	   && unit_flag(punit2->type, F_MISSILE_CARRIER)) {
	  return 1;
	}
      } unit_list_iterate_end;
    }
  }
  return 0;
}


/**************************************************************************
...
**************************************************************************/
int can_unit_build_city(struct unit *punit)
{
  if(!unit_flag(punit->type, F_CITIES))
    return 0;

  if(!punit->moves_left)
    return 0;

  return city_can_be_built_here(punit->x, punit->y);
}

/**************************************************************************
...
**************************************************************************/
int kills_citizen_after_attack(struct unit *punit) {
  return (game.killcitizen >> ((int)unit_types[punit->type].move_type-1)) & 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_add_to_city(struct unit *punit)
{
  struct city *pcity;

  if(!unit_flag(punit->type, F_CITIES))
    return 0;
  if(!punit->moves_left)
    return 0;

  pcity = map_get_city(punit->x, punit->y);

  if(!pcity)
    return 0;
  if(pcity->size >= game.add_to_size_limit)
    return 0;
  if(pcity->owner != punit->owner)
    return 0;

  if(improvement_exists(B_AQUEDUCT)
     && !city_got_building(pcity, B_AQUEDUCT) 
     && pcity->size >= game.aqueduct_size)
    return 0;
  
  if(improvement_exists(B_SEWER)
     && !city_got_building(pcity, B_SEWER)
     && pcity->size >= game.sewer_size)
    return 0;

  return 1;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_change_homecity(struct unit *punit)
{
  struct city *pcity=map_get_city(punit->x, punit->y);
  return pcity && pcity->owner==punit->owner;
}

/**************************************************************************
Return whether the unit can be put in auto-mode.
(Auto-settler for settlers, auto-attack for military units.)
**************************************************************************/
int can_unit_do_auto(struct unit *punit) 
{
  if (unit_flag(punit->type, F_SETTLERS))
    return 1;
  if (is_military_unit(punit) && map_get_city(punit->x, punit->y))
    return 1;
  return 0;
}

/**************************************************************************
Return whether the unit can connect with given activity (or with
any activity if activity arg is set to ACTIVITY_IDLE)
**************************************************************************/
int can_unit_do_connect (struct unit *punit, enum unit_activity activity) 
{
  struct player  *pplayer = get_player (punit->owner);

  if (!unit_flag(punit->type, F_SETTLERS))
    return 0;

  if (activity == ACTIVITY_IDLE)   /* IDLE here means "any activity" */
    return 1;

  if (activity == ACTIVITY_ROAD 
      || activity == ACTIVITY_IRRIGATE 
      || (activity == ACTIVITY_RAILROAD
	  && player_knows_techs_with_flag(pplayer, TF_RAILROAD))
      || (activity == ACTIVITY_FORTRESS 
	  && player_knows_techs_with_flag(pplayer, TF_FORTRESS)))
  return 1;

  return 0;
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
  case ACTIVITY_PATROL:  	text = _("Patrol"); break;
  default:			text = _("Unknown"); break;
  }

  return text;
}

/**************************************************************************
Return whether the unit can be paradropped.
That is if the unit is in a friendly city or on an Airbase
special, have enough movepoints left and have not paradropped
before in this turn.
**************************************************************************/
int can_unit_paradrop(struct unit *punit)
{
  struct city *pcity;
  struct unit_type *utype;
  struct tile *ptile;

  if (!unit_flag(punit->type, F_PARATROOPERS))
    return 0;

  if(punit->paradropped)
    return 0;

  utype = get_unit_type(punit->type);

  if(punit->moves_left < utype->paratroopers_mr_req)
    return 0;

  ptile=map_get_tile(punit->x, punit->y);
  if(ptile->special&S_AIRBASE)
    return 1;

  if(!(pcity = map_get_city(punit->x, punit->y)))
    return 0;

  return 1;
}

/**************************************************************************
Check if the unit's current activity is actually legal.
**************************************************************************/
int can_unit_continue_current_activity(struct unit *punit)
{
  enum unit_activity current = punit->activity;
  int target = punit->activity_target;
  int current2 = current == ACTIVITY_FORTIFIED ? ACTIVITY_FORTIFYING : current;
  int result;

  if (punit->connecting)
    return can_unit_do_connect(punit, current);

  punit->activity = ACTIVITY_IDLE;
  punit->activity_target = 0;

  result = can_unit_do_activity_targeted(punit, current2, target);

  punit->activity = current;
  punit->activity_target = target;

  return result;
}

/**************************************************************************
...
**************************************************************************/
int can_unit_do_activity(struct unit *punit, enum unit_activity activity)
{
  return can_unit_do_activity_targeted(punit, activity, 0);
}

/**************************************************************************
Note that if you make changes here you should also change the code for
autosettlers in server/settler.c. The code there does not use this function
as it would be a ajor CPU hog.
**************************************************************************/
int can_unit_do_activity_targeted(struct unit *punit,
				  enum unit_activity activity, int target)
{
  struct player *pplayer;
  struct tile *ptile;
  struct tile_type *type;

  pplayer = &game.players[punit->owner];
  ptile = map_get_tile(punit->x, punit->y);
  type = get_tile_type(ptile->terrain);

  switch(activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_GOTO:
  case ACTIVITY_PATROL:
    return 1;

  case ACTIVITY_POLLUTION:
    return unit_flag(punit->type, F_SETTLERS) && (ptile->special&S_POLLUTION);

  case ACTIVITY_FALLOUT:
    return unit_flag(punit->type, F_SETTLERS) && (ptile->special&S_FALLOUT);

  case ACTIVITY_ROAD:
    return (terrain_control.may_road &&
	    unit_flag(punit->type, F_SETTLERS) &&
	    !(ptile->special&S_ROAD) && type->road_time &&
	    ((ptile->terrain!=T_RIVER && !(ptile->special&S_RIVER)) || 
	     player_knows_techs_with_flag(pplayer, TF_BRIDGE)));

  case ACTIVITY_MINE:
    /* Don't allow it if someone else is irrigating this tile.
     * *Do* allow it if they're transforming - the mine may survive */
    if (terrain_control.may_mine &&
	unit_flag(punit->type, F_SETTLERS) &&
	( (ptile->terrain==type->mining_result && 
	   !(ptile->special&S_MINE)) ||
	  (ptile->terrain!=type->mining_result &&
	   type->mining_result!=T_LAST &&
	   (ptile->terrain!=T_OCEAN || type->mining_result==T_OCEAN ||
	    can_reclaim_ocean(punit->x, punit->y)) &&
	   (ptile->terrain==T_OCEAN || type->mining_result!=T_OCEAN ||
	    can_channel_land(punit->x, punit->y)) &&
	   (type->mining_result!=T_OCEAN ||
	    !(map_get_city(punit->x, punit->y)))) )) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_IRRIGATE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_IRRIGATE:
    /* Don't allow it if someone else is mining this tile.
     * *Do* allow it if they're transforming - the irrigation may survive */
    if (terrain_control.may_irrigate &&
	unit_flag(punit->type, F_SETTLERS) &&
	(!(ptile->special&S_IRRIGATION) ||
	 (!(ptile->special&S_FARMLAND) &&
	  player_knows_techs_with_flag(pplayer, TF_FARMLAND))) &&
	( (ptile->terrain==type->irrigation_result && 
	   is_water_adjacent_to_tile(punit->x, punit->y)) ||
	  (ptile->terrain!=type->irrigation_result &&
	   type->irrigation_result!=T_LAST &&
	   (ptile->terrain!=T_OCEAN || type->irrigation_result==T_OCEAN ||
	    can_reclaim_ocean(punit->x, punit->y)) &&
	   (ptile->terrain==T_OCEAN || type->irrigation_result!=T_OCEAN ||
	    can_channel_land(punit->x, punit->y)) &&
	   (type->irrigation_result!=T_OCEAN ||
	    !(map_get_city(punit->x, punit->y)))) )) {
      unit_list_iterate(ptile->units, tunit) {
	if(tunit->activity==ACTIVITY_MINE) return 0;
      }
      unit_list_iterate_end;
      return 1;
    } else return 0;

  case ACTIVITY_FORTIFYING:
    return (is_ground_unit(punit) &&
	    (punit->activity != ACTIVITY_FORTIFIED) &&
	    !unit_flag(punit->type, F_SETTLERS) &&
	    ptile->terrain != T_OCEAN);

  case ACTIVITY_FORTIFIED:
    return 0;

  case ACTIVITY_FORTRESS:
    return (unit_flag(punit->type, F_SETTLERS) &&
	    !map_get_city(punit->x, punit->y) &&
	    player_knows_techs_with_flag(pplayer, TF_FORTRESS) &&
	    !(ptile->special&S_FORTRESS) && ptile->terrain!=T_OCEAN);

  case ACTIVITY_AIRBASE:
    return (unit_flag(punit->type, F_AIRBASE) &&
	    player_knows_techs_with_flag(pplayer, TF_AIRBASE) &&
	    !(ptile->special&S_AIRBASE) && ptile->terrain!=T_OCEAN);

  case ACTIVITY_SENTRY:
    return 1;

  case ACTIVITY_RAILROAD:
    /* if the tile has road, the terrain must be ok.. */
    return (terrain_control.may_road &&
	    unit_flag(punit->type, F_SETTLERS) &&
	    ((ptile->special&S_ROAD) ||
	     (punit->connecting &&
	      (type->road_time &&
	       ((ptile->terrain!=T_RIVER && !(ptile->special&S_RIVER))
		|| player_knows_techs_with_flag(pplayer, TF_BRIDGE))))) &&
	    !(ptile->special&S_RAILROAD) &&
	    player_knows_techs_with_flag(pplayer, TF_RAILROAD));

  case ACTIVITY_PILLAGE:
    {
      int pspresent;
      int psworking;
      pspresent = get_tile_infrastructure_set(ptile);
      if (pspresent && is_ground_unit(punit)) {
	psworking = get_unit_tile_pillage_set(punit->x, punit->y);
	if (ptile->city && (target & (S_ROAD | S_RAILROAD)))
	    return 0;
	if (target == S_NO_SPECIAL) {
	  if (ptile->city)
	    return ((pspresent & (~(psworking | S_ROAD |S_RAILROAD))) != 0);
	  else
	    return ((pspresent & (~psworking)) != 0);
	}
	else if ((!game.rgame.pillage_select) &&
		 (target != get_preferred_pillage(pspresent)))
	  return 0;
	else
	  return ((pspresent & (~psworking) & target) != 0);
      } else {
	return 0;
      }
    }

  case ACTIVITY_EXPLORE:
    return (is_ground_unit(punit) || is_sailing_unit(punit));

  case ACTIVITY_TRANSFORM:
    return (terrain_control.may_transform &&
	    (type->transform_result!=T_LAST) &&
	    (ptile->terrain!=type->transform_result) &&
	    (ptile->terrain!=T_OCEAN || type->transform_result==T_OCEAN ||
	     can_reclaim_ocean(punit->x, punit->y)) &&
	    (ptile->terrain==T_OCEAN || type->transform_result!=T_OCEAN ||
	     can_channel_land(punit->x, punit->y)) &&
	    (type->transform_result!=T_OCEAN ||
	     !(map_get_city(punit->x, punit->y))) &&
	    unit_flag(punit->type, F_TRANSFORM));

  default:
    freelog(LOG_ERROR, "Unknown activity %d in can_unit_do_activity_targeted()",
	    activity);
    return 0;
  }
}

/**************************************************************************
  assign a new task to a unit.
**************************************************************************/
void set_unit_activity(struct unit *punit, enum unit_activity new_activity)
{
  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target=0;
  punit->connecting = 0;
}

/**************************************************************************
  assign a new targeted task to a unit.
**************************************************************************/
void set_unit_activity_targeted(struct unit *punit,
				enum unit_activity new_activity, int new_target)
{
  punit->activity=new_activity;
  punit->activity_count=0;
  punit->activity_target=new_target;
  punit->connecting = 0;
}

/**************************************************************************
...
**************************************************************************/
int is_unit_activity_on_tile(enum unit_activity activity, int x, int y)
{
  unit_list_iterate(map_get_tile(x, y)->units, punit) 
    if(punit->activity==activity)
      return 1;
  unit_list_iterate_end;
  return 0;
}

/**************************************************************************
...
**************************************************************************/
int get_unit_tile_pillage_set(int x, int y)
{
  int tgt_ret = S_NO_SPECIAL;
  unit_list_iterate(map_get_tile(x, y)->units, punit)
    if(punit->activity==ACTIVITY_PILLAGE)
      tgt_ret |= punit->activity_target;
  unit_list_iterate_end;
  return tgt_ret;
}

/**************************************************************************
 ...
**************************************************************************/
char *unit_description(struct unit *punit)
{
  struct city *pcity;
  static char buffer[512];

  pcity = player_find_city_by_id(game.player_ptr, punit->homecity);

  my_snprintf(buffer, sizeof(buffer), "%s%s\n%s\n%s", 
	  get_unit_type(punit->type)->name, 
	  punit->veteran ? _(" (veteran)") : "",
	  unit_activity_text(punit), 
	  pcity ? pcity->name : "");

  return buffer;
}

/**************************************************************************
 ...
**************************************************************************/
char *unit_activity_text(struct unit *punit)
{
  static char text[64];
  char *moves_str;
   
  switch(punit->activity) {
   case ACTIVITY_IDLE:
     moves_str = _("Moves");
     if(is_air_unit(punit)) {
       int rate,f;
       rate=get_unit_type(punit->type)->move_rate/SINGLE_MOVE;
       f=((punit->fuel)-1);
       if(punit->moves_left%SINGLE_MOVE) {
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
       if(punit->moves_left%SINGLE_MOVE) {
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
   case ACTIVITY_PATROL:
     return get_activity_text (punit->activity);
   case ACTIVITY_PILLAGE:
     if(punit->activity_target == 0) {
       return get_activity_text (punit->activity);
     } else {
       my_snprintf(text, sizeof(text), "%s: %s",
		   get_activity_text (punit->activity),
		   map_get_infrastructure_text(punit->activity_target));
       return (text);
     }
   default:
    freelog(LOG_FATAL, "Unknown unit activity %d in unit_activity_text()",
	    punit->activity);
    exit(1);
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
struct unit *unit_list_find(struct unit_list *This, int id)
{
  struct genlist_iterator myiter;

  genlist_iterator_init(&myiter, &This->list, 0);

  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter))
    if(((struct unit *)ITERATOR_PTR(myiter))->id==id)
      return ITERATOR_PTR(myiter);

  return 0;
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
  if(unit_list_size(This) > 1) {
    genlist_sort(&This->list, compar_unit_ord_map);
  }
}

/**************************************************************************
...
**************************************************************************/
void unit_list_sort_ord_city(struct unit_list *This)
{
  if(unit_list_size(This) > 1) {
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

/**************************************************************************
Returns the number of free spaces for missiles. Can be 0 or negative.
**************************************************************************/
int missile_carrier_capacity(int x, int y, int playerid,
			     int count_units_with_extra_fuel)
{
  struct tile *ptile = map_get_tile(x, y);
  int misonly = 0;
  int airall = 0;
  int totalcap;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner == playerid) {
      if (unit_flag(punit->type, F_CARRIER)
	  && !(is_ground_unit(punit) && ptile->terrain == T_OCEAN)) {
	airall += get_transporter_capacity(punit);
	continue;
      }
      if (unit_flag(punit->type, F_MISSILE_CARRIER)
	  && !(is_ground_unit(punit) && ptile->terrain == T_OCEAN)) {
	misonly += get_transporter_capacity(punit);
	continue;
      }
      /* Don't count units which have enough fuel (>1) */
      if (is_air_unit(punit)
	  && (count_units_with_extra_fuel || punit->fuel <= 1)) {
	if (unit_flag(punit->type, F_MISSILE))
	  misonly--;
	else
	  airall--;
      }
    }
  }
  unit_list_iterate_end;

  if (airall < 0)
    airall = 0;

  totalcap = airall + misonly;

  return totalcap;
}

/**************************************************************************
Returns the number of free spaces for airunits (includes missiles).
Can be 0 or negative.
**************************************************************************/
int airunit_carrier_capacity(int x, int y, int playerid,
			     int count_units_with_extra_fuel)
{
  struct tile *ptile = map_get_tile(x, y);
  int misonly = 0;
  int airall = 0;

  unit_list_iterate(map_get_tile(x, y)->units, punit) {
    if (punit->owner == playerid) {
      if (unit_flag(punit->type, F_CARRIER)
	  && !(is_ground_unit(punit) && ptile->terrain == T_OCEAN)) {
	airall += get_transporter_capacity(punit);
	continue;
      }
      if (unit_flag(punit->type, F_MISSILE_CARRIER)
	  && !(is_ground_unit(punit) && ptile->terrain == T_OCEAN)) {
	misonly += get_transporter_capacity(punit);
	continue;
      }
      /* Don't count units which have enough fuel (>1) */
      if (is_air_unit(punit)
	  && (count_units_with_extra_fuel || punit->fuel <= 1)) {
	if (unit_flag(punit->type, F_MISSILE))
	  misonly--;
	else
	  airall--;
      }
    }
  }
  unit_list_iterate_end;

  if (misonly < 0)
    airall += misonly;

  return airall;
}

/**************************************************************************
Returns true if the tile contains an allied unit and only allied units.
(ie, if your nation A is allied with B, and B is allied with C, a tile
containing units from B and C will return false)
**************************************************************************/
struct unit *is_allied_unit_tile(struct tile *ptile, int playerid)
{
  struct unit *punit = NULL;

  unit_list_iterate(ptile->units, cunit)
    if (players_allied(playerid, cunit->owner))
      punit = cunit;
    else
      return NULL;
  unit_list_iterate_end;

  return punit;
}

/**************************************************************************
 is there an enemy unit on this tile?
**************************************************************************/
struct unit *is_enemy_unit_tile(struct tile *ptile, int playerid)
{
  unit_list_iterate(ptile->units, punit)
    if (players_at_war(punit->owner, playerid))
      return punit;
  unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 is there an non-allied unit on this tile?
**************************************************************************/
struct unit *is_non_allied_unit_tile(struct tile *ptile, int playerid)
{
  unit_list_iterate(ptile->units, punit)
    if (!players_allied(punit->owner, playerid))
      return punit;
  unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
 is there an unit we have peace or ceasefire with on this tile?
**************************************************************************/
struct unit *is_non_attack_unit_tile(struct tile *ptile, int playerid)
{
  unit_list_iterate(ptile->units, punit)
    if (players_non_attack(punit->owner, playerid))
      return punit;
  unit_list_iterate_end;

  return NULL;
}

/**************************************************************************
  Is this square controlled by the unit's owner?

  Here "is_my_zoc" means essentially a square which is
  *not* adjacent to an enemy unit on a land tile.
  (Or, currently, an enemy city even if empty.)

  Note this function only makes sense for ground units.
**************************************************************************/
int is_my_zoc(struct unit *myunit, int x0, int y0)
{
  int owner=myunit->owner;

  assert(is_ground_unit(myunit));
     
  square_iterate(x0, y0, 1, x1, y1) {
    if ((map_get_terrain(x1, y1)!=T_OCEAN)
	&& is_non_allied_unit_tile(map_get_tile(x1, y1), owner))
      return 0;
  } square_iterate_end;

  return 1;
}

/*
 * Triremes have a varying loss percentage. based on tech.
 * Seafaring reduces this to 25%, Navigation to 12.5%.  The Lighthouse
 * wonder reduces this to 0.  AJS 20010301
 */
int trireme_loss_pct(struct player *pplayer, int x, int y) {
  int losspct = 50;

  /*
   * If we are in a city or next to land, we have no chance of losing
   * the ship.  To make this really useful for ai planning purposes, we'd
   * need to confirm that we can exist/move at the x,y location we are given.
   */
  if ((map_get_terrain(x, y) != T_OCEAN) || is_coastline(x, y) ||
      (player_owns_active_wonder(pplayer, B_LIGHTHOUSE)))
	losspct = 0;
  else if (player_knows_techs_with_flag(pplayer,TF_REDUCE_TRIREME_LOSS2))
	losspct /= 4;
  else if (player_knows_techs_with_flag(pplayer,TF_REDUCE_TRIREME_LOSS1))
	losspct /= 2;

  return losspct;
}
