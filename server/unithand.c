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

#include "barbarian.h"
#include "city.h"
#include "combat.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "unit.h"

#include "citytools.h"
#include "cityturn.h"
#include "diplomats.h"
#include "gamelog.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "shared.h"
#include "spacerace.h"
#include "srv_main.h"
#include "unittools.h"

#include "aitools.h"
#include "aiunit.h"

#include "unithand.h"

static void city_add_or_build_error(struct player *pplayer,
				    struct unit *punit,
				    enum add_build_city_result res);
static void city_add_unit(struct player *pplayer, struct unit *punit);
static void city_build(struct player *pplayer, struct unit *punit,
		       char *name);

/**************************************************************************
...
**************************************************************************/
void handle_unit_goto_tile(struct player *pplayer, 
			   struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  /* 
   * Discard invalid packet. Replace this with is_normal_map_pos if
   * capstr is set to "1.12.0".
   */
  if (!normalize_map_pos(&req->x, &req->y)) {
    return;
  }

  if(!punit) {
      return;
  }

  punit->goto_dest_x = req->x;
  punit->goto_dest_y = req->y;

  set_unit_activity(punit, ACTIVITY_GOTO);

  send_unit_info(NULL, punit);

  /* 
   * Normally units on goto does not pick up extra units, even if the
   * units are in a city and are sentried. But if we just started the
   * goto We want to take them with us, so we do this. 
   */
  if (get_transporter_capacity(punit) > 0) {
    assign_units_to_transporter(punit, TRUE);
  }

  do_unit_goto(punit, GOTO_MOVE_ANY, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_airlift(struct player *pplayer,
			 struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  struct city *pcity;

  /* 
   * Discard invalid packet. Replace this with is_normal_map_pos if
   * capstr is set to "1.12.0".
   */
  if (!normalize_map_pos(&req->x, &req->y)) {
    return;
  }

  pcity = map_get_city(req->x, req->y);
  if (punit && pcity) {
    do_airline(punit, pcity);
  }
}

/**************************************************************************
Handler for PACKET_UNIT_CONNECT request
The unit is send on way and will build something (roads only for now)
along the way
**************************************************************************/
void handle_unit_connect(struct player *pplayer, 
		          struct packet_unit_connect *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  /* 
   * Discard invalid packet. Replace this with is_normal_map_pos if
   * capstr is set to "1.12.0".
   */
  if (!normalize_map_pos(&req->dest_x, &req->dest_y)) {
    return;
  }

  if (!punit || !can_unit_do_connect(punit, req->activity_type)) {
    return;
  }

  punit->goto_dest_x = req->dest_x;
  punit->goto_dest_y = req->dest_y;

  set_unit_activity(punit, req->activity_type);
  punit->connecting = TRUE;

  send_unit_info(NULL, punit);

  /* 
   * Avoid wasting first turn if unit cannot do the activity on the
   * starting tile.
   */
  if (!can_unit_do_activity(punit, req->activity_type)) {
    do_unit_goto(punit, get_activity_move_restriction(req->activity_type),
		 FALSE);
  }
}

/**************************************************************************
 Upgrade all units of a given type.
**************************************************************************/
void handle_upgrade_unittype_request(struct player *pplayer, 
				     struct packet_unittype_info *packet)
{
  int cost;
  int to_unit;
  int upgraded = 0;
  if ((to_unit =can_upgrade_unittype(pplayer, packet->type)) == -1) {
    notify_player(pplayer, _("Game: Illegal packet, can't upgrade %s (yet)."), 
		  unit_types[packet->type].name);
    return;
  }
  cost = unit_upgrade_price(pplayer, packet->type, to_unit);
  conn_list_do_buffer(&pplayer->connections);
  unit_list_iterate(pplayer->units, punit) {
    struct city *pcity;
    if (cost > pplayer->economic.gold)
      break;
    pcity = map_get_city(punit->x, punit->y);
    if (punit->type == packet->type && pcity && pcity->owner == pplayer->player_no) {
      pplayer->economic.gold -= cost;
      
      upgrade_unit(punit, to_unit);
      upgraded++;
    }
  }
  unit_list_iterate_end;
  conn_list_do_unbuffer(&pplayer->connections);
  if (upgraded > 0) {
    notify_player(pplayer, _("Game: %d %s upgraded to %s for %d gold."), 
		  upgraded, unit_types[packet->type].name, 
		  unit_types[to_unit].name, cost * upgraded);
    send_player_info(pplayer, pplayer);
  } else {
    notify_player(pplayer, _("Game: No units could be upgraded."));
  }
}

/**************************************************************************
 Upgrade a single unit.
 TODO: should upgrades in allied cities be possible?
**************************************************************************/
void handle_unit_upgrade_request(struct player *pplayer,
                                 struct packet_unit_request *packet)
{
  int cost;
  int from_unit, to_unit;
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);
  struct city *pcity = player_find_city_by_id(pplayer, packet->city_id);
  
  if (!punit || !pcity) {
    return;
  }

  if(punit->x!=pcity->x || punit->y!=pcity->y)  {
    notify_player(pplayer, _("Game: Illegal move, unit not in city!"));
    return;
  }
  from_unit = punit->type;
  if((to_unit=can_upgrade_unittype(pplayer, punit->type)) == -1) {
    notify_player(pplayer, _("Game: Illegal package, can't upgrade %s (yet)."), 
		  unit_type(punit)->name);
    return;
  }
  cost = unit_upgrade_price(pplayer, punit->type, to_unit);
  if(cost > pplayer->economic.gold) {
    notify_player(pplayer, _("Game: Insufficient funds, upgrade costs %d."),
		  cost);
    return;
  }
  pplayer->economic.gold -= cost;
  upgrade_unit(punit, to_unit);
  send_player_info(pplayer, pplayer);
  notify_player(pplayer, _("Game: %s upgraded to %s for %d gold."), 
		unit_name(from_unit), unit_name(to_unit), cost);
}


/***************************************************************
  Tell the client the cost of inciting a revolt or bribing a unit.
  Only send result back to the requesting connection, not all
  connections for that player.
***************************************************************/
void handle_incite_inq(struct connection *pconn,
		       struct packet_generic_integer *packet)
{
  struct player *pplayer = pconn->player;
  struct city *pcity=find_city_by_id(packet->value);
  struct unit *punit=find_unit_by_id(packet->value);
  struct packet_generic_values req;

  if (!pplayer) {
    freelog(LOG_ERROR, "Incite inquiry from non-player %s",
	    conn_description(pconn));
    return;
  }
  if(pcity)  {
    city_incite_cost(pcity);
    req.id=packet->value;
    req.value1=pcity->incite_revolt_cost;
    if(pplayer->player_no == pcity->original) req.value1/=2;
    send_packet_generic_values(pconn, PACKET_INCITE_COST, &req);
    return;
  }
  if(punit)  {
    punit->bribe_cost = unit_bribe_cost(punit);
    req.id=packet->value;
    req.value1=punit->bribe_cost;
    send_packet_generic_values(pconn, PACKET_INCITE_COST, &req);
  }
}

/***************************************************************
...
***************************************************************/
void handle_diplomat_action(struct player *pplayer, 
			    struct packet_diplomat_action *packet)
{
  struct unit *pdiplomat=player_find_unit_by_id(pplayer, packet->diplomat_id);
  struct unit *pvictim=find_unit_by_id(packet->target_id);
  struct city *pcity=find_city_by_id(packet->target_id);

  if (!pdiplomat || !unit_flag(pdiplomat, F_DIPLOMAT)) {
    return;
  }

  if(pdiplomat->moves_left > 0) {
    switch(packet->action_type) {
    case DIPLOMAT_BRIBE:
      if(pvictim && diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE,
					   pvictim->x, pvictim->y)) {
	diplomat_bribe(pplayer, pdiplomat, pvictim);
      }
      break;
    case SPY_SABOTAGE_UNIT:
      if(pvictim && diplomat_can_do_action(pdiplomat, SPY_SABOTAGE_UNIT,
					   pvictim->x, pvictim->y)) {
	spy_sabotage_unit(pplayer, pdiplomat, pvictim);
      }
      break;
     case DIPLOMAT_SABOTAGE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_SABOTAGE,
					 pcity->x, pcity->y)) {
	/* packet value is improvement ID + 1 (or some special codes) */
	diplomat_sabotage(pplayer, pdiplomat, pcity, packet->value - 1);
      }
      break;
    case SPY_POISON:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_POISON,
					 pcity->x, pcity->y)) {
	spy_poison(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_INVESTIGATE:
      if(pcity && diplomat_can_do_action(pdiplomat,DIPLOMAT_INVESTIGATE,
					 pcity->x, pcity->y)) {
	diplomat_investigate(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_EMBASSY:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_EMBASSY,
					 pcity->x, pcity->y)) {
	diplomat_embassy(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_INCITE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE,
					 pcity->x, pcity->y)) {
	diplomat_incite(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_MOVE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_MOVE,
					 pcity->x, pcity->y)) {
	handle_unit_move_request(pdiplomat, pcity->x, pcity->y, FALSE, TRUE);
      }
      break;
    case DIPLOMAT_STEAL:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_STEAL,
					 pcity->x, pcity->y)) {
	/* packet value is technology ID (or some special codes) */
	diplomat_get_tech(pplayer, pdiplomat, pcity, packet->value);
      }
      break;
    case SPY_GET_SABOTAGE_LIST:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_GET_SABOTAGE_LIST,
					 pcity->x, pcity->y)) {
	spy_get_sabotage_list(pplayer, pdiplomat, pcity);
      }
      break;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_change_homecity(struct player *pplayer, 
				 struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  struct city *old_pcity = player_find_city_by_id(pplayer, punit->homecity);
  struct city *new_pcity = player_find_city_by_id(pplayer, req->city_id);

  if(!punit || !new_pcity) {
      return;
  }

  unit_list_insert(&new_pcity->units_supported, punit);
  if (old_pcity) {
    unit_list_unlink(&old_pcity->units_supported, punit);
  }

  punit->homecity = req->city_id;
  send_unit_info(pplayer, punit);

  city_refresh(new_pcity);
  send_city_info(pplayer, new_pcity);

  if (old_pcity) {
    city_refresh(old_pcity);
    send_city_info(pplayer, old_pcity);
  }
}

/**************************************************************************
  Disband a unit.  If its in a city, add 1/2 of the worth of the unit
  to the city's shield stock for the current production.
  "iter" may be NULL, see wipe_unit_safe for details.
  NOTE: AI calls do_unit_disband_safe directly, but player calls
  of course handle_unit_disband_safe
**************************************************************************/
void do_unit_disband_safe(struct city *pcity, struct unit *punit,
			  struct genlist_iterator *iter)
{
  if (pcity) {
    pcity->shield_stock += (unit_type(punit)->build_cost/2);
    /* If we change production later at this turn. No penalty is added. */
    pcity->disbanded_shields += (unit_type(punit)->build_cost/2);

    /* Note: Nowadays it's possible to disband unit in allied city and
     * your ally receives those shields. Should it be like this? Why not?
     * That's why we must use city_owner instead of pplayer -- Zamar */
    send_city_info(city_owner(pcity), pcity);
  }
  wipe_unit_safe(punit, iter);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_disband_safe(struct player *pplayer, 
			      struct packet_unit_request *req,
			      struct genlist_iterator *iter)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  struct city *pcity = map_get_city(punit->x, punit->y);

  if (!punit) {
    return;
  }

  do_unit_disband_safe(pcity, punit, iter);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_disband(struct player *pplayer, 
			 struct packet_unit_request *req)
{
  handle_unit_disband_safe(pplayer, req, NULL);
}

/**************************************************************************
 This function assumes that there is a valid city at punit->(x,y) for
 certain values of test_add_build_or_city.  It should only be called
 after a call to unit_add_build_city_result, which does the
 consistency checking.
**************************************************************************/
static void city_add_or_build_error(struct player *pplayer,
				    struct unit *punit,
				    enum add_build_city_result res)
{
  /* Given that res came from test_unit_add_or_build_city, pcity will
     be non-null for all required status values. */
  struct city *pcity = map_get_city(punit->x, punit->y);
  char *unit_name = unit_type(punit)->name;

  switch (res) {
  case AB_NOT_BUILD_LOC:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Can't place city here."));
    break;
  case AB_NOT_BUILD_UNIT:
    {
      char *us = get_units_with_flag_string(F_CITIES);
      if (us) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Only %s can build a city."),
			 us);
	free(us);
      } else {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Can't build a city."));
      }
    }
    break;
  case AB_NOT_ADDABLE_UNIT:
    {
      char *us = get_units_with_flag_string(F_ADD_TO_CITY);
      if (us) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Only %s can add to a city."),
			 us);
	free(us);
      } else {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Can't add to a city."));
      }
    }
    break;
  case AB_NO_MOVES_ADD:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s unit has no moves left to add to %s."),
		     unit_name, pcity->name);
    break;
  case AB_NO_MOVES_BUILD:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s unit has no moves left to build city."),
		     unit_name);
    break;
  case AB_TOO_BIG:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s is too big to add %s."),
		     pcity->name, unit_name);
    break;
  case AB_NO_AQUEDUCT:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s needs %s to grow, so you cannot add %s."),
		     pcity->name, get_improvement_name(B_AQUEDUCT),
		     unit_name);
    break;
  case AB_NO_SEWER:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s needs %s to grow, so you cannot add %s."),
		     pcity->name, get_improvement_name(B_SEWER),
		     unit_name);
    break;
  default:
    /* Shouldn't happen */
    freelog(LOG_ERROR, "Cannot add %s to %s for unknown reason",
	    unit_name, pcity->name);
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Can't add %s to %s."),
		     unit_name, pcity->name);
    break;
  }
}

/**************************************************************************
 This function assumes that there is a valid city at punit->(x,y) It
 should only be called after a call to a function like
 test_unit_add_or_build_city, which does the checking.
**************************************************************************/
static void city_add_unit(struct player *pplayer, struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  char *unit_name = unit_type(punit)->name;

  assert(unit_pop_value(punit->type) > 0);
  pcity->size += unit_pop_value(punit->type);
  add_adjust_workers(pcity);
  wipe_unit(punit);
  send_city_info(NULL, pcity);
  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		   _("Game: %s added to aid %s in growing."),
		   unit_name, pcity->name);
}

/**************************************************************************
 This function assumes a certain level of consistency checking: There
 is no city under punit->(x,y), and that location is a valid one on
 which to build a city. It should only be called after a call to a
 function like test_unit_add_or_build_city, which does the checking.
**************************************************************************/
static void city_build(struct player *pplayer, struct unit *punit,
		       char *name)
{
  char *city_name = get_sane_name(name);

  if (!city_name) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Let's not build a city with "
		       "such a stupid name."));
    return;
  }
  create_city(pplayer, punit->x, punit->y, city_name);
  wipe_unit(punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_build_city(struct player *pplayer,
			    struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  enum add_build_city_result res;

  if (!punit) {
    return;
  }

  res = test_unit_add_or_build_city(punit);

  if (res == AB_BUILD_OK)
    city_build(pplayer, punit, req->name);
  else if (res == AB_ADD_OK)
    city_add_unit(pplayer, punit);
  else
    city_add_or_build_error(pplayer, punit, res);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo)
{
  struct unit *punit = player_find_unit_by_id(pplayer, pinfo->id);

  if (!punit) {
    return;
  }

  if (!same_pos(punit->x, punit->y, pinfo->x, pinfo->y)) {
    /* 
     * Discard invalid packet. Replace this with is_normal_map_pos
     * if capstr is set to "1.12.0".
     */
    if (!normalize_map_pos(&pinfo->x, &pinfo->y)) {
      return;
    }

    if (is_tiles_adjacent(punit->x, punit->y, pinfo->x, pinfo->y)) {
      punit->ai.control = FALSE;
      handle_unit_move_request(punit, pinfo->x, pinfo->y, FALSE, FALSE);
    } else {
      /* This can happen due to lag, so don't complain too loudly */
      freelog(LOG_DEBUG, "tiles are not adjacent, unit pos %d,%d trying "
	      "to go to  %d,%d!\n", punit->x, punit->y, pinfo->x, pinfo->y);
    }
  } else if (punit->activity != pinfo->activity ||
	     punit->activity_target != pinfo->activity_target ||
	     pinfo->select_it || punit->ai.control) {
    /* Treat change in ai.control as change in activity, so
     * idle autosettlers behave correctly when selected --dwp
     */
    punit->ai.control = FALSE;
    handle_unit_activity_request_targeted(punit,
					  pinfo->activity,
					  pinfo->activity_target,
					  pinfo->select_it);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_move_unit(struct player *pplayer, struct packet_move_unit *pmove)
{
  struct unit *punit = player_find_unit_by_id(pplayer, pmove->unid);

  /* 
   * Discard invalid packet. Replace this with is_normal_map_pos if
   * capstr is set to "1.12.0".
   */
  if (!normalize_map_pos(&pmove->x, &pmove->y)) {
    return;
  }

  if (!punit || !is_tiles_adjacent(punit->x, punit->y, pmove->x, pmove->y)) {
    return;
  }
  handle_unit_move_request(punit, pmove->x, pmove->y, FALSE, FALSE);
}

/**************************************************************************
This function assumes the attack is legal. The calling function should have
already made all neccesary checks.
**************************************************************************/
static void handle_unit_attack_request(struct unit *punit, struct unit *pdefender)
{
  int o;
  struct player *pplayer = unit_owner(punit);
  struct packet_unit_combat combat;
  struct unit *plooser, *pwinner;
  struct unit old_punit = *punit;	/* Used for new ship algorithm. -GJW */
  struct city *pcity;
  int def_x = pdefender->x, def_y = pdefender->y;
  
  freelog(LOG_DEBUG, "Start attack: %s's %s against %s's %s.",
	  pplayer->name, unit_type(punit)->name, 
	  unit_owner(pdefender)->name,
	  unit_type(pdefender)->name);

  /* Sanity checks */
  if (pplayers_non_attack(unit_owner(punit), unit_owner(pdefender))) {
    freelog(LOG_FATAL,
	    "Trying to attack a unit with which you have peace or cease-fire at %i, %i",
	    def_x, def_y);
    abort();
  }
  if (pplayers_allied(unit_owner(punit), unit_owner(pdefender))
      && !(unit_flag(punit, F_NUCLEAR) && punit == pdefender)) {
    freelog(LOG_FATAL,
	    "Trying to attack a unit with which you have alliance at %i, %i",
	    def_x, def_y);
    abort();
  }

  if(unit_flag(punit, F_NUCLEAR)) {
    struct packet_nuke_tile packet;
    
    packet.x=def_x;
    packet.y=def_y;
    if ((pcity=sdi_defense_close(unit_owner(punit), def_x, def_y))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST_ATT,
		       _("Game: Your Nuclear missile was shot down by"
			 " SDI defences, what a waste."));
      notify_player_ex(city_owner(pcity), def_x, def_y, E_UNIT_WIN,
		       _("Game: The nuclear attack on %s was avoided by"
			 " your SDI defense."), pcity->name);
      wipe_unit(punit);
      return;
    } 

    lsend_packet_nuke_tile(&game.game_connections, &packet);

    wipe_unit(punit);
    do_nuclear_explosion(pplayer, def_x, def_y);
    return;
  }
  
  unit_versus_unit(punit, pdefender);

  /* Adjust attackers moves_left _after_ unit_versus_unit() so that
   * the movement attack modifier is correct! --dwp
   *
   * And for greater Civ2 compatibility (and ship balance issues), ships
   * use a different algorithm.  Recompute a new total MP based on the HP
   * the ship has left after being damaged, and subtract out all of the MP
   * that had been used so far this turn (plus the points used in the attack
   * itself). -GJW
   */
  if (is_sailing_unit (punit)) {
    int moves_used = unit_move_rate (&old_punit) - old_punit.moves_left;
    punit->moves_left = unit_move_rate (punit) - moves_used - SINGLE_MOVE;
  } else {
    punit->moves_left -= SINGLE_MOVE;
  }
  if(punit->moves_left<0)
    punit->moves_left=0;

  if (punit->hp &&
      (pcity=map_get_city(def_x, def_y)) &&
      pcity->size>1 &&
      !city_got_citywalls(pcity) &&
      kills_citizen_after_attack(punit)) {
    city_reduce_size(pcity,1);
    city_refresh(pcity);
    send_city_info(NULL, pcity);
  }
  if (unit_flag(punit, F_ONEATTACK)) 
    punit->moves_left = 0;
  pwinner = (punit->hp > 0) ? punit : pdefender;
  plooser = (pdefender->hp > 0) ? punit : pdefender;
    
  combat.attacker_unit_id=punit->id;
  combat.defender_unit_id=pdefender->id;
  combat.attacker_hp=punit->hp / game.firepower_factor;
  combat.defender_hp=pdefender->hp / game.firepower_factor;
  combat.make_winner_veteran=pwinner->veteran?1:0;
  
  for(o=0; o<game.nplayers; o++)
    if (map_get_known_and_seen(punit->x, punit->y, get_player(o)) ||
	map_get_known_and_seen(def_x, def_y, get_player(o))) {
      lsend_packet_unit_combat(&game.players[o].connections, &combat);
    }
  conn_list_iterate(game.game_connections, pconn) {
    if (!pconn->player && pconn->observer) {
      send_packet_unit_combat(pconn, &combat);
    }
  } conn_list_iterate_end;
  
  if(punit==plooser) {
    /* The attacker lost */
    freelog(LOG_DEBUG, "Attacker lost: %s's %s against %s's %s.",
	    pplayer->name, unit_type(punit)->name,
	    unit_owner(pdefender)->name, unit_type(pdefender)->name);

    notify_player_ex(unit_owner(pwinner),
		     pwinner->x, pwinner->y, E_UNIT_WIN,
		     _("Game: Your %s%s survived the pathetic attack"
		       " from %s's %s."),
		     unit_name(pwinner->type),
		     get_location_str_in(unit_owner(pwinner),
					 pwinner->x, pwinner->y),
		     unit_owner(plooser)->name, unit_name(plooser->type));
    
    notify_player_ex(unit_owner(plooser),
		     def_x, def_y, E_UNIT_LOST_ATT,
		     _("Game: Your attacking %s failed "
		       "against %s's %s%s!"),
		     unit_name(plooser->type), unit_owner(pwinner)->name,
		     unit_name(pwinner->type),
		     get_location_str_at(unit_owner(plooser),
					 pwinner->x, pwinner->y));
    wipe_unit(plooser);
  }
  else {
    /* The defender lost, the attacker punit lives! */
    freelog(LOG_DEBUG, "Defender lost: %s's %s against %s's %s.",
	    pplayer->name, unit_type(punit)->name,
	    unit_owner(pdefender)->name, unit_type(pdefender)->name);

    punit->moved = TRUE;	/* We moved */

    notify_player_ex(unit_owner(pwinner), punit->x, punit->y,
		     E_UNIT_WIN_ATT,
		     _("Game: Your attacking %s succeeded"
		       " against %s's %s%s!"), unit_name(pwinner->type),
		     unit_owner(plooser)->name, unit_name(plooser->type),
		     get_location_str_at(unit_owner(pwinner), plooser->x,
					 plooser->y));
    kill_unit(pwinner, plooser);
               /* no longer pplayer - want better msgs -- Syela */
  }
  if (pwinner == punit && unit_flag(punit, F_MISSILE)) {
    wipe_unit(pwinner);
    return;
  }

  /* If attacker wins, and occupychance > 0, it might move in.  Don't move in
     if there are enemy units in the tile (a fortress or city with multiple
     defenders and unstacked combat).  Note that this could mean capturing (or
     destroying) a city. -GJW */

  if (pwinner == punit && myrand(100) < game.occupychance &&
      !is_non_allied_unit_tile(map_get_tile(def_x, def_y),
			       unit_owner(punit))) {

    /* Hack: make sure the unit has enough moves_left for the move to succeed,
       and adjust moves_left to afterward (if successful). */

    int old_moves = punit->moves_left;
    int full_moves = unit_move_rate (punit);
    punit->moves_left = full_moves;
    if (handle_unit_move_request(punit, def_x, def_y, FALSE, FALSE)) {
      punit->moves_left = old_moves - (full_moves - punit->moves_left);
      if (punit->moves_left < 0) {
	punit->moves_left = 0;
      }
    } else {
      punit->moves_left = old_moves;
    }
  }

  send_unit_info(NULL, pwinner);
}

/**************************************************************************
...
**************************************************************************/
static void how_to_declare_war(struct player *pplayer)
{
  notify_player_ex(pplayer, -1, -1, E_NOEVENT,
		   _("Game: Cancel treaty in the players dialog first (F3)."));
}

/**************************************************************************
...
**************************************************************************/
static bool can_unit_move_to_tile_with_notify(struct unit *punit, int dest_x,
					      int dest_y, bool igzoc)
{
  enum unit_move_result reason;
  int src_x = punit->x, src_y = punit->y;

  reason =
      test_unit_move_to_tile(punit->type, unit_owner(punit),
			     punit->activity, punit->connecting,
			     punit->x, punit->y, dest_x, dest_y, igzoc);
  if (reason == MR_OK)
    return TRUE;

  if (reason == MR_BAD_TYPE_FOR_CITY_TAKE_OVER) {
    char *units_str = get_units_with_flag_string(F_MARINES);
    if (units_str) {
      notify_player_ex(unit_owner(punit), src_x, src_y,
		       E_NOEVENT, _("Game: Only %s can attack from sea."),
		       units_str);
      free(units_str);
    } else {
      notify_player_ex(unit_owner(punit), src_x, src_y,
		       E_NOEVENT, _("Game: Cannot attack from sea."));
    }
  } else if (reason == MR_NO_WAR) {
    notify_player_ex(unit_owner(punit), src_x, src_y,
		     E_NOEVENT,
		     _("Game: Cannot attack unless you declare war first."));
  } else if (reason == MR_ZOC) {
    notify_player_ex(unit_owner(punit), src_x, src_y, E_NOEVENT,
		     _("Game: %s can only move into your own zone of control."),
		     unit_type(punit)->name);
  }
  return FALSE;
}

/**************************************************************************
  Will try to move to/attack the tile dest_x,dest_y.  Returns true if this
  could be done, false if it couldn't for some reason.
  
  'igzoc' means ignore ZOC rules - not necessary for igzoc units etc, but
  done in some special cases (moving barbarians out of initial hut).
  Should normally be FALSE.

  'move_diplomat_city' is another special case which should normally be
  FALSE.  If TRUE, try to move diplomat (or spy) into city (should be
  allied) instead of telling client to popup diplomat/spy dialog.
**************************************************************************/
bool handle_unit_move_request(struct unit *punit, int dest_x, int dest_y,
			     bool igzoc, bool move_diplomat_city)
{
  struct player *pplayer = unit_owner(punit);
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);
  struct tile *psrctile = map_get_tile(punit->x, punit->y);
  struct unit *pdefender = get_defender(punit, dest_x, dest_y);
  struct city *pcity = pdesttile->city;

  /* barbarians shouldn't enter huts */
  if (is_barbarian(pplayer) && tile_has_special(pdesttile, S_HUT)) {
    return FALSE;
  }

  /* 
   * Discard invalid packet. Replace this with is_normal_map_pos if
   * capstr is set to "1.12.0".
   */
  if (!normalize_map_pos(&dest_x, &dest_y)) {
    return FALSE;
  }

  /* this occurs often during lag, and to the AI due to some quirks -- Syela */
  if (!is_tiles_adjacent(punit->x, punit->y, dest_x, dest_y)) {
    freelog(LOG_DEBUG, "tiles not adjacent in move request");
    return FALSE;
  }

  if (unit_flag(punit, F_TRADE_ROUTE) && pcity && pcity->owner != punit->owner
      && !pplayers_allied(city_owner(pcity), unit_owner(punit))
      && punit->homecity != 0) {
    struct packet_unit_request req;
    req.unit_id = punit->id;
    req.city_id = pcity->id;
    req.name[0] = '\0';
    return handle_unit_establish_trade(pplayer, &req);
  }

  /* Pop up a diplomat action dialog in the client.  If the AI has used
     a goto to send a diplomat to a target do not pop up a dialog in
     the client.  For allied cities, keep moving if move_diplomat_city
     tells us to, or if the unit is on goto and the city is not the
     final destination.
  */
  if (is_diplomat_unit(punit)
      && (is_non_allied_unit_tile(pdesttile, unit_owner(punit))
	  || is_non_allied_city_tile(pdesttile, unit_owner(punit))
	  || !move_diplomat_city)) {
    if (is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION,
				     dest_x, dest_y)) {
      struct packet_diplomat_action packet;
      if (punit->activity == ACTIVITY_GOTO && pplayer->ai.control)
	return FALSE;

      /* If we didn't send_unit_info the client would sometimes think that
	 the diplomat didn't have any moves left and so don't pop up the box.
	 (We are in the middle of the unit restore cycle when doing goto's, and
	 the unit's movepoints have been restored, but we only send the unit
	 info at the end of the function.) */
      send_unit_info(pplayer, punit);

      /* if is_diplomat_action_available() then there must be a city or a unit */
      if (pcity) {
	packet.target_id = pcity->id;
      } else if (pdefender) {
	packet.target_id = pdefender->id;
      } else {
	freelog(LOG_FATAL, "Bug in unithand.c: no diplomat target.");
	abort();
      }
      packet.diplomat_id = punit->id;
      packet.action_type = DIPLOMAT_CLIENT_POPUP_DIALOG;
      lsend_packet_diplomat_action(player_reply_dest(pplayer), &packet);
      return FALSE;
    } else if (!can_unit_move_to_tile(punit, dest_x, dest_y, igzoc)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       map_get_terrain(punit->x, punit->y) == T_OCEAN
		       ? _("Game: Unit must be on land to "
			   "perform diplomatic action.")
		       : _("Game: No diplomat action possible."));
      return FALSE;
    }
  }

  /*** Try to attack if there is an enemy unit on the target tile ***/
  if (pdefender
      && pplayers_at_war(unit_owner(punit), unit_owner(pdefender))) {
    if (!can_unit_attack_tile(punit, dest_x , dest_y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
  		       _("Game: You can't attack there."));
      return FALSE;
    }
 
    if (punit->moves_left<=0)  {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: This unit has no moves left."));
      return FALSE;
    }

    if (pcity && !pplayers_at_war(city_owner(pcity), unit_owner(punit))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: Can't attack %s's unit in the city of %s "
			 "because you are not at war with %s."),
		       unit_owner(pdefender)->name,
		       pcity->name,
		       city_owner(pcity)->name);
      how_to_declare_war(pplayer);
      return FALSE;
    }

    /* DO NOT Auto-attack.  Findvictim routine will decide if we should. */
    if (pplayer->ai.control && punit->activity == ACTIVITY_GOTO) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: Aborting GOTO for AI attack procedures."));
      return FALSE;
    }
 
    if (punit->activity == ACTIVITY_GOTO &&
 	(dest_x != punit->goto_dest_x || dest_y != punit->goto_dest_y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: %s aborted GOTO as there are units in the way."),
 		       unit_type(punit)->name);
      return FALSE;
    }
 
    /* This is for debugging only, and seems to be obsolete as the error message
       never appears */
    if (pplayer->ai.control && punit->ai.passenger != 0) {
      struct unit *passenger;
      passenger = unit_list_find(&psrctile->units, punit->ai.passenger);
      if (passenger) {
 	/* removed what seemed like a very bad abort() -- JMC/jjm */
 	if (get_transporter_capacity(punit) == 0) {
 	  freelog(LOG_NORMAL, "%s#%d@(%d,%d) thinks %s#%d is a passenger?",
 		  unit_name(punit->type), punit->id, punit->x, punit->y,
 		  unit_name(passenger->type), passenger->id);
 	}
      }
    }

    /* This should be part of can_unit_attack_tile(), but I think the AI
       depends on can_unit_attack_tile() not stopping it in the goto, so
       leave it here for now. */
    if (unit_type(punit)->attack_strength == 0) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: A %s cannot attack other units."),
		       unit_name(punit->type));
      return FALSE;
    }

    handle_unit_attack_request(punit, pdefender);
    return TRUE;
  } /* End attack case */
 
  /* There are no players we are at war with at desttile. But if there
     is a unit we have a treaty!=alliance with we can't move there */
  pdefender = is_non_allied_unit_tile(pdesttile, unit_owner(punit));
  if (pdefender) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: No war declared against %s, cannot attack."),
		     unit_owner(pdefender)->name);
    how_to_declare_war(pplayer);
    return FALSE;
  }


  {
    struct unit *bodyguard;
    if (pplayer->ai.control &&
 	punit->ai.bodyguard > 0 &&
 	(bodyguard = unit_list_find(&psrctile->units, punit->ai.bodyguard)) &&
 	bodyguard->moves_left == 0) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: %s doesn't want to leave its bodyguard."),
 		       unit_type(punit)->name);
      return FALSE;
    }
  }
 
  /* Mao had this problem with chariots ending turns next to enemy cities. -- Syela */
  if (pplayer->ai.control &&
      punit->moves_left <= map_move_cost(punit, dest_x, dest_y) &&
      unit_type(punit)->move_rate > map_move_cost(punit, dest_x, dest_y) &&
      enemies_at(punit, dest_x, dest_y) &&
      !enemies_at(punit, punit->x, punit->y)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
  		     _("Game: %s ending move early to stay out of trouble."),
  		     unit_type(punit)->name);
    return FALSE;
  }

  /* If there is a city it is empty.
     If not it would have been caught in the attack case. */
  if (pcity && !pplayers_allied(city_owner(pcity), unit_owner(punit))) {
    if (is_air_unit(punit) || !is_military_unit(punit) || is_sailing_unit(punit)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: Only ground troops can take over "
			 "a city."));
      return FALSE;
    }

    if (!pplayers_at_war(city_owner(pcity), unit_owner(punit))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: No war declared against %s, cannot take "
			 "over city."), city_owner(pcity)->name);
      how_to_declare_war(pplayer);
      return FALSE;
    }
  }

  /******* ok now move the unit *******/
  if(can_unit_move_to_tile_with_notify(punit, dest_x, dest_y, igzoc) &&
     try_move_unit(punit, dest_x, dest_y)) {
    int src_x = punit->x;
    int src_y = punit->y;
    int move_cost = map_move_cost(punit, dest_x, dest_y);
    /* The ai should assign the relevant units itself, but for now leave this */
    bool take_from_land = punit->activity == ACTIVITY_IDLE;
    bool survived;

    survived = move_unit(punit, dest_x, dest_y, TRUE, take_from_land, move_cost);
    if (!survived)
      return TRUE;

    /* bodyguard code */
    if(pplayer->ai.control && punit->ai.bodyguard > 0) {
      struct unit *bodyguard = unit_list_find(&psrctile->units, punit->ai.bodyguard);
      if (bodyguard) {
	bool success;

	/* FIXME: it is stupid to set to ACTIVITY_IDLE if the unit is
	   ACTIVITY_FORTIFIED and the unit has no chance of moving anyway */
	handle_unit_activity_request(bodyguard, ACTIVITY_IDLE);
	success = handle_unit_move_request(bodyguard,
					   dest_x, dest_y, igzoc, FALSE);
	freelog(LOG_DEBUG, "Dragging %s from (%d,%d)->(%d,%d) (Success=%d)",
		unit_type(bodyguard)->name, src_x, src_y,
		dest_x, dest_y, success);
	handle_unit_activity_request(bodyguard, ACTIVITY_FORTIFYING);
      }
    }
    return TRUE;
  } else {
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_help_build_wonder(struct player *pplayer, 
				   struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  struct city *pcity_dest = find_city_by_id(req->city_id);

  if (!punit || !unit_flag(punit, F_HELP_WONDER) || !pcity_dest
      || !unit_can_help_build_wonder(punit, pcity_dest)) {
    return;
  }

  if (!is_tiles_adjacent(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
      && !same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y))
    return;

  if (!(same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
	|| try_move_unit(punit, pcity_dest->x, pcity_dest->y)))
    return;

  /* we're there! */
  pcity_dest->shield_stock += unit_type(punit)->build_cost;
  pcity_dest->caravan_shields += unit_type(punit)->build_cost;

  conn_list_do_buffer(&pplayer->connections);
  notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		   _("Game: Your %s helps build the %s in %s (%d remaining)."), 
		   unit_name(punit->type),
		   get_improvement_type(pcity_dest->currently_building)->name,
		   pcity_dest->name, 
		   build_points_left(pcity_dest));

  wipe_unit(punit);
  send_player_info(pplayer, pplayer);
  send_city_info(pplayer, pcity_dest);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
bool handle_unit_establish_trade(struct player *pplayer, 
				 struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  struct city *pcity_homecity, *pcity_dest = find_city_by_id(req->city_id);
  int revenue;
  
  if (!punit || !unit_flag(punit, F_TRADE_ROUTE) || !pcity_dest) {
    return FALSE;
  }

  pcity_homecity = player_find_city_by_id(pplayer, punit->homecity);
  if (!pcity_homecity) {
    return FALSE;
  }
    
  if (!is_tiles_adjacent(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
      && !same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y))
    return FALSE;

  if (!(same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
	|| try_move_unit(punit, pcity_dest->x, pcity_dest->y)))
    return FALSE;

  if (!can_establish_trade_route(pcity_homecity, pcity_dest)) {
    int i;
    notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route here!"),
		     unit_name(punit->type));
    for (i = 0; i < NUM_TRADEROUTES; i++) {
      if (pcity_homecity->trade[i]==pcity_dest->id) {
	notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		      _("      A traderoute already exists between %s and %s!"),
		      pcity_homecity->name, pcity_dest->name);
	return FALSE;
      }
    }
    if (city_num_trade_routes(pcity_homecity) == NUM_TRADEROUTES) {
      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		       _("      The city of %s already has %d "
			 "trade routes!"), pcity_homecity->name,
		       NUM_TRADEROUTES);
      return FALSE;
    }
    if (city_num_trade_routes(pcity_dest) == NUM_TRADEROUTES) {
      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		       _("      The city of %s already has %d "
			 "trade routes!"), pcity_dest->name,
		       NUM_TRADEROUTES);
      return FALSE;
    }
    return FALSE;
  }
  
  revenue = establish_trade_route(pcity_homecity, pcity_dest);
  conn_list_do_buffer(&pplayer->connections);
  notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		   _("Game: Your %s from %s has arrived in %s,"
		   " and revenues amount to %d in gold and research."), 
		   unit_name(punit->type), pcity_homecity->name,
		   pcity_dest->name, revenue);
  wipe_unit(punit);
  pplayer->economic.gold+=revenue;
  update_tech(pplayer, revenue);
  send_player_info(pplayer, pplayer);
  city_refresh(pcity_homecity);
  city_refresh(pcity_dest);

  /* Notify the owners of the city. */
  send_city_info(pplayer, pcity_homecity);
  send_city_info(city_owner(pcity_dest), pcity_dest);

  /* 
   * Notify the players about the other city so that they get the
   * tile_trade value.
   */
  send_city_info(city_owner(pcity_dest), pcity_homecity);
  send_city_info(pplayer, pcity_dest);

  conn_list_do_unbuffer(&pplayer->connections);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_auto_request(struct player *pplayer, 
			      struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  if (!punit || !can_unit_do_auto(punit))
    return;

  punit->ai.control = TRUE;
  send_unit_info(pplayer, punit);
}

/**************************************************************************
...
**************************************************************************/
static void handle_unit_activity_dependencies(struct unit *punit,
					      enum unit_activity old_activity,
					      int old_target)
{
  switch (punit->activity) {
  case ACTIVITY_IDLE:
    if (old_activity == ACTIVITY_PILLAGE) {
      enum tile_special_type prereq =
	  map_get_infrastructure_prerequisite(old_target);
      if (prereq != S_NO_SPECIAL) {
	unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
	  if ((punit2->activity == ACTIVITY_PILLAGE) &&
	      (punit2->activity_target == prereq)) {
	    set_unit_activity(punit2, ACTIVITY_IDLE);
	    send_unit_info(NULL, punit2);
	  }
	unit_list_iterate_end;
      }
    }
    break;
  case ACTIVITY_EXPLORE:
    if (punit->moves_left > 0) {
      int id = punit->id;
      bool more_to_explore = ai_manage_explorer(punit);
      /* ai_manage_explorer sets the activity to idle, so we reset it */
      if (more_to_explore && (punit = find_unit_by_id(id))) {
	set_unit_activity(punit, ACTIVITY_EXPLORE);
	send_unit_info(NULL, punit);
      }
    }
    break;
  default:
    /* do nothing */
    break;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request(struct unit *punit, 
				  enum unit_activity new_activity)
{
  if (can_unit_do_activity(punit, new_activity)) {
    enum unit_activity old_activity = punit->activity;
    enum tile_special_type old_target = punit->activity_target;
    set_unit_activity(punit, new_activity);
    if (punit->pgr) {
      free(punit->pgr->pos);
      free(punit->pgr);
      punit->pgr = NULL;
    }
    send_unit_info(NULL, punit);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request_targeted(struct unit *punit,
					   enum unit_activity new_activity,
					   enum tile_special_type new_target,
					   bool select_unit)
{
  if (can_unit_do_activity_targeted(punit, new_activity, new_target)) {
    enum unit_activity old_activity = punit->activity;
    enum tile_special_type old_target = punit->activity_target;
    set_unit_activity_targeted(punit, new_activity, new_target);

    if (punit->pgr) {
      free(punit->pgr->pos);
      free(punit->pgr);
      punit->pgr = NULL;
    }

    send_unit_info_to_onlookers(NULL, punit, punit->x, punit->y, FALSE,
				select_unit);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_unload_request(struct player *pplayer, 
				struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  if (!punit) {
    return;
  }

  unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
    if (punit != punit2 && punit2->activity == ACTIVITY_SENTRY) {
      bool wakeup = FALSE;

      if (is_ground_units_transport(punit)) {
	if (is_ground_unit(punit2))
	  wakeup = TRUE;
      }

      if (unit_flag(punit, F_MISSILE_CARRIER)) {
	if (unit_flag(punit2, F_MISSILE))
	  wakeup = TRUE;
      }

      if (unit_flag(punit, F_CARRIER)) {
	if (is_air_unit(punit2))
	  wakeup = TRUE;
      }

      if (wakeup) {
	set_unit_activity(punit2, ACTIVITY_IDLE);
	send_unit_info(NULL, punit2);
      }
    }
  } unit_list_iterate_end;
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void handle_unit_nuke(struct player *pplayer, 
                     struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  if (!punit) {
    return;
  }
  handle_unit_attack_request(punit, punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_paradrop_to(struct player *pplayer, 
			     struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);
  
  if (!punit) {
    return;
  }

  do_paradrop(punit, req->x, req->y);
}



/**************************************************************************
Sanity checks on the goto route.
**************************************************************************/
static bool check_route(struct player *pplayer, struct packet_goto_route *packet)
{
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);

  if (!punit) {
    return FALSE;
  }

  if (packet->first_index != 0) {
    freelog(LOG_ERROR, "Bad route packet, first_index is %d!",
	    packet->first_index);
    return FALSE;
  }
  if (packet->last_index != packet->length - 1) {
    freelog(LOG_ERROR, "bad route, li: %d != l-1: %d",
	    packet->last_index, packet->length - 1);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
Receives goto route packages.
**************************************************************************/
static void handle_route(struct player *pplayer, struct packet_goto_route *packet)
{
  struct goto_route *pgr = NULL;
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);

  pgr = fc_malloc(sizeof(struct goto_route));

  pgr->pos = packet->pos; /* here goes the malloced packet->pos */
  pgr->first_index = 0;
  pgr->length = packet->length;
  pgr->last_index = packet->length-1; /* index magic... */

  punit->pgr = pgr;

#ifdef DEBUG
  {
    int i = pgr->first_index;
    freelog(LOG_DEBUG, "first:%d, last:%d, length:%d",
	   pgr->first_index, pgr->last_index, pgr->length);
    for (; i < pgr->last_index;i++)
      freelog(LOG_DEBUG, "%d,%d", pgr->pos[i].x, pgr->pos[i].y);
  }
#endif

  if (punit->activity == ACTIVITY_GOTO) {
    punit->goto_dest_x = pgr->pos[pgr->last_index-1].x;
    punit->goto_dest_y = pgr->pos[pgr->last_index-1].y;
    send_unit_info(pplayer, punit);
  }

  assign_units_to_transporter(punit, TRUE);
  goto_route_execute(punit);
}

/**************************************************************************
Receives goto route packages.
**************************************************************************/
void handle_goto_route(struct player *pplayer, struct packet_goto_route *packet)
{
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);

  if (!check_route(pplayer, packet))
    return;

  handle_unit_activity_request(punit, ACTIVITY_GOTO);
  handle_route(pplayer, packet);
}

/**************************************************************************
Receives patrol route packages.
**************************************************************************/
void handle_patrol_route(struct player *pplayer, struct packet_goto_route *packet)
{
  struct unit *punit = player_find_unit_by_id(pplayer, packet->unit_id);

  if (!check_route(pplayer, packet)) {
    free(packet->pos);
    packet->pos = NULL;
    return;
  }

  handle_unit_activity_request(punit, ACTIVITY_PATROL);
  handle_route(pplayer, packet);
}
