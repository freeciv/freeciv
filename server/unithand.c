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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barbarian.h"
#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "unit.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "civserver.h"
#include "gamelog.h"
#include "gotohand.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "shared.h"
#include "spacerace.h"
#include "unitfunc.h"
#include "unittools.h"

#include "aitools.h"
#include "aiunit.h"

#include "unithand.h"

/**************************************************************************
...
**************************************************************************/
void handle_unit_goto_tile(struct player *pplayer, 
			   struct packet_unit_request *req)
{
  struct unit *punit;
  
  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    punit->goto_dest_x=req->x;
    punit->goto_dest_y=req->y;

    set_unit_activity(punit, ACTIVITY_GOTO);

    send_unit_info(0, punit);
      
    do_unit_goto(pplayer, punit, GOTO_MOVE_ANY);  
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
  struct unit *punit;

  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    if (can_unit_do_connect (punit, req->activity_type)) {
      punit->goto_dest_x=req->dest_x;
      punit->goto_dest_y=req->dest_y;

      set_unit_activity(punit, req->activity_type);
      punit->connecting = 1;

      send_unit_info(0, punit);

      /* avoid wasting first turn if unit cannot do the activity
	 on the starting tile */
      if (! can_unit_do_activity (punit, req->activity_type)) 
	do_unit_goto (pplayer, punit,
		      get_activity_move_restriction(req->activity_type));
    }
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
    notify_player(pplayer, _("Game: Illegal package, can't upgrade %s (yet)."), 
		  unit_types[packet->type].name);
    return;
  }
  cost = unit_upgrade_price(pplayer, packet->type, to_unit);
  connection_do_buffer(pplayer->conn);
  unit_list_iterate(pplayer->units, punit) {
    if (cost > pplayer->economic.gold)
      break;
    if (punit->type == packet->type && map_get_city(punit->x, punit->y)) {
      pplayer->economic.gold -= cost;
      
      upgrade_unit(punit, to_unit);
      upgraded++;
    }
  }
  unit_list_iterate_end;
  connection_do_unbuffer(pplayer->conn);
  if (upgraded > 0) {
    notify_player(pplayer, _("Game: %d %s upgraded to %s for %d credits."), 
		  upgraded, unit_types[packet->type].name, 
		  unit_types[to_unit].name, cost * upgraded);
    send_player_info(pplayer, pplayer);
  } else {
    notify_player(pplayer, _("Game: No units could be upgraded."));
  }
}

/**************************************************************************
 Upgrade a single unit
**************************************************************************/
void handle_unit_upgrade_request(struct player *pplayer,
                                 struct packet_unit_request *packet)
{
  int cost;
  int from_unit, to_unit;
  struct unit *punit;
  struct city *pcity;
  
  if(!(punit=player_find_unit_by_id(pplayer, packet->unit_id))) return;
  if(!(pcity=find_city_by_id(packet->city_id))) return;

  if(punit->x!=pcity->x || punit->y!=pcity->y)  {
    notify_player(pplayer, _("Game: Illegal move, unit not in city!"));
    return;
  }
  from_unit = punit->type;
  if((to_unit=can_upgrade_unittype(pplayer, punit->type)) == -1) {
    notify_player(pplayer, _("Game: Illegal package, can't upgrade %s (yet)."), 
		  unit_types[punit->type].name);
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
  notify_player(pplayer, _("Game: %s upgraded to %s for %d credits."), 
		unit_name(from_unit), unit_name(to_unit), cost);
}


/***************************************************************
...  Tell the client the cost of inciting a revolt
     or bribing a unit
***************************************************************/
void handle_incite_inq(struct player *pplayer,
		       struct packet_generic_integer *packet)
{
  struct city *pcity=find_city_by_id(packet->value);
  struct unit *punit=find_unit_by_id(packet->value);
  struct packet_generic_values req;

  if(pcity)  {
    city_incite_cost(pcity);
    req.id=packet->value;
    req.value1=pcity->incite_revolt_cost;
    if(pplayer->player_no == pcity->original) req.value1/=2;
    send_packet_generic_values(pplayer->conn, PACKET_INCITE_COST, &req);
    return;
  }
  if(punit)  {
    punit->bribe_cost = unit_bribe_cost(punit);
    req.id=packet->value;
    req.value1=punit->bribe_cost;
    send_packet_generic_values(pplayer->conn, PACKET_INCITE_COST, &req);
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

  if (!pdiplomat) return;
  if (!unit_flag(pdiplomat->type, F_DIPLOMAT)) 
    return;

  if(pdiplomat->moves_left) {
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
  struct unit *punit;
  
  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    struct city *pcity;
    if((pcity=player_find_city_by_id(pplayer, req->city_id))) {
      unit_list_insert(&pcity->units_supported, punit);
      
      if((pcity=player_find_city_by_id(pplayer, punit->homecity)))
	unit_list_unlink(&pcity->units_supported, punit);
      
      punit->homecity=req->city_id;
      send_unit_info(pplayer, punit);
    }
  }
}

/**************************************************************************
  Disband a unit.  If its in a city, add 1/2 of the worth of the unit
  to the city's shield stock for the current production.
  "iter" may be NULL, see wipe_unit_safe for details.
**************************************************************************/
void handle_unit_disband_safe(struct player *pplayer, 
			      struct packet_unit_request *req,
			      struct genlist_iterator *iter)
{
  struct unit *punit;
  struct city *pcity;
  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    if ((pcity=map_get_city(punit->x, punit->y))) {
      pcity->shield_stock+=(get_unit_type(punit->type)->build_cost/2);
      send_city_info(pplayer, pcity);
    }
    wipe_unit_safe(pplayer, punit, iter);
  }
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
...
**************************************************************************/
void handle_unit_build_city(struct player *pplayer, 
			    struct packet_unit_request *req)
{
  struct unit *punit;
  char *unit_name;
  struct city *pcity;
  char *name;
  
  punit = player_find_unit_by_id(pplayer, req->unit_id) ;
  if(!punit)
    return;
  
  unit_name = get_unit_type(punit->type)->name;
  pcity = map_get_city(punit->x, punit->y);
  
  if (!unit_flag(punit->type, F_CITIES)) {
    char *us = get_units_with_flag_string(F_CITIES);
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Only %s can build or add to a city."), us);
    free(us);
    return;
  }  

  if(!punit->moves_left)  {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s unit has no moves left to %s city."),
		     unit_name, (pcity ? _("add to") : _("build")));
    return;
  }
    
  if (pcity) {
    if (can_unit_add_to_city(punit)) {
      pcity->size++;
      if (!add_adjust_workers(pcity))
	auto_arrange_workers(pcity);
      wipe_unit(0, punit);
      send_city_info(0, pcity);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       _("Game: %s added to aid %s in growing."), 
		       unit_name, pcity->name);
    } else {
      if(pcity->size > game.add_to_size_limit) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			 _("Game: %s is too big to add %s."),
			 pcity->name, unit_name);
      }
      else if(improvement_exists(B_AQUEDUCT)
	 && !city_got_building(pcity, B_AQUEDUCT) 
	 && pcity->size >= game.aqueduct_size) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			 _("Game: %s needs %s to grow, so you cannot add %s."),
			 pcity->name, get_improvement_name(B_AQUEDUCT),
			 unit_name);
      }
      else if(improvement_exists(B_SEWER)
	      && !city_got_building(pcity, B_SEWER)
	      && pcity->size >= game.sewer_size) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			 _("Game: %s needs %s to grow, so you cannot add %s."),
			 pcity->name, get_improvement_name(B_SEWER),
			 unit_name);
      }
      else {
	/* this shouldn't happen? */
	freelog(LOG_NORMAL, "Cannot add %s to %s for unknown reason",
		unit_name, pcity->name);
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
			 _("Game: cannot add %s to %s."),
			 unit_name, pcity->name);
      }
    }
    return;
  }
    
  if(!can_unit_build_city(punit)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: Can't place city here."));
      return;
  }
      
  if(!(name=get_sane_name(req->name))) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
		     _("Game: Let's not build a city with such a stupid name."));
    return;
  }

  send_remove_unit(0, req->unit_id);
  if (terrain_control.may_road) {
    map_set_special(punit->x, punit->y, S_ROAD);
    if (player_knows_techs_with_flag(pplayer, TF_RAILROAD))
      map_set_special(punit->x, punit->y, S_RAILROAD);
  }
  send_tile_info(0, punit->x, punit->y);

  create_city(pplayer, punit->x, punit->y, name);
  server_remove_unit(punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo)
{
  struct unit *punit;

  punit=player_find_unit_by_id(pplayer, pinfo->id);

  if(punit) {
    if (!same_pos(punit->x, punit->y, pinfo->x, pinfo->y)) {
      punit->ai.control=0;
      handle_unit_move_request(pplayer, punit, pinfo->x, pinfo->y, FALSE);
    }
    else if(punit->activity!=pinfo->activity ||
	    punit->activity_target!=pinfo->activity_target ||
	    punit->ai.control==1) {
      /* Treat change in ai.control as change in activity, so
       * idle autosettlers behave correctly when selected --dwp
       */
      punit->ai.control=0;
      handle_unit_activity_request_targeted(punit,
					    pinfo->activity, pinfo->activity_target);
    }
  }
}
/**************************************************************************
...
**************************************************************************/
void handle_move_unit(struct player *pplayer, struct packet_move_unit *pmove)
{
  struct unit *punit;

  punit=player_find_unit_by_id(pplayer, pmove->unid);
  if(punit)  {
    handle_unit_move_request(pplayer, punit, pmove->x, pmove->y, FALSE);
  }
}
/**************************************************************************
...
**************************************************************************/
void handle_unit_attack_request(struct player *pplayer, struct unit *punit,
				struct unit *pdefender)
{
  int o;
  struct packet_unit_combat combat;
  struct unit *plooser, *pwinner;
  struct unit old_punit = *punit;	/* Used for new ship algorithm. -GJW */
  struct city *pcity;
  struct city *nearcity1, *nearcity2;
  int def_x = pdefender->x, def_y = pdefender->y;
  
  freelog(LOG_DEBUG, "Start attack: %s's %s against %s's %s.",
	  pplayer->name, unit_types[punit->type].name, 
	  game.players[pdefender->owner].name, 
	  unit_types[pdefender->type].name);

  if(unit_flag(punit->type, F_NUCLEAR)) {
    struct packet_nuke_tile packet;
    
    packet.x=def_x;
    packet.y=def_y;
    if ((pcity=sdi_defense_close(punit->owner, def_x, def_y))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST_ATT,
		       _("Game: Your Nuclear missile was shot down by"
			 " SDI defences, what a waste."));
      notify_player_ex(&game.players[pcity->owner], 
		       def_x, def_y, E_UNIT_WIN, 
		       _("Game: The nuclear attack on %s was avoided by"
			 " your SDI defense."),
		       pcity->name); 
      wipe_unit(0, punit);
      return;
    } 

    for(o=0; o<game.nplayers; o++)
      send_packet_nuke_tile(game.players[o].conn, &packet);
    
    do_nuclear_explosion(def_x, def_y);
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
    punit->moves_left = unit_move_rate (punit) - moves_used - 3;
  } else {
    punit->moves_left-=3;
  }
  if(punit->moves_left<0)
    punit->moves_left=0;

  if (punit->hp &&
      (pcity=map_get_city(def_x, def_y)) &&
      pcity->size>1 &&
      !city_got_citywalls(pcity) &&
      kills_citizen_after_attack(punit)) {
    pcity->size--;
    city_auto_remove_worker(pcity);
    city_refresh(pcity);
    send_city_info(0, pcity);
  }
  if (unit_flag(punit->type, F_ONEATTACK)) 
    punit->moves_left = 0;
  pwinner=(punit->hp) ?     punit : pdefender;
  plooser=(pdefender->hp) ? punit : pdefender;
    
  combat.attacker_unit_id=punit->id;
  combat.defender_unit_id=pdefender->id;
  combat.attacker_hp=punit->hp / game.firepower_factor;
  combat.defender_hp=pdefender->hp / game.firepower_factor;
  combat.make_winner_veteran=pwinner->veteran?1:0;
  
  for(o=0; o<game.nplayers; o++)
    if(map_get_known_and_seen(punit->x, punit->y, &game.players[o]) ||
       map_get_known_and_seen(def_x, def_y, &game.players[o]))
      send_packet_unit_combat(game.players[o].conn, &combat);

  nearcity1 = dist_nearest_city(get_player(pwinner->owner), def_x, def_y, 0, 0);
  nearcity2 = dist_nearest_city(get_player(plooser->owner), def_x, def_y, 0, 0);
  
  if(punit==plooser) {
    /* The attacker lost */
    freelog(LOG_DEBUG, "Attacker lost: %s's %s against %s's %s.",
	    pplayer->name, unit_types[punit->type].name, 
	    game.players[pdefender->owner].name, 
	    unit_types[pdefender->type].name);

    notify_player_ex(get_player(pwinner->owner),
		     pwinner->x, pwinner->y, E_UNIT_WIN, 
		     _("Game: Your %s%s survived the pathetic attack"
		     " from %s's %s."),
		     unit_name(pwinner->type), 
		     get_location_str_in(get_player(pwinner->owner),
					 pwinner->x, pwinner->y, " "),
		     get_player(plooser->owner)->name,
		     unit_name(plooser->type));
    
    notify_player_ex(get_player(plooser->owner),
		     def_x, def_y, E_UNIT_LOST_ATT, 
		     _("Game: Your attacking %s failed against %s's %s%s!"),
		     unit_name(plooser->type),
		     get_player(pwinner->owner)->name,
		     unit_name(pwinner->type),
		     get_location_str_at(get_player(plooser->owner),
					 pwinner->x, pwinner->y, " "));
    wipe_unit(pplayer, plooser);
  }
  else {
    /* The defender lost, the attacker punit lives! */
    freelog(LOG_DEBUG, "Defender lost: %s's %s against %s's %s.",
	    pplayer->name, unit_types[punit->type].name, 
	    game.players[pdefender->owner].name, 
	    unit_types[pdefender->type].name);

    punit->moved=1; /* We moved */

    notify_player_ex(get_player(pwinner->owner), 
		     punit->x, punit->y, E_UNIT_WIN_ATT, 
		     _("Game: Your attacking %s succeeded"
		       " against %s's %s%s!"),
		     unit_name(pwinner->type),
		     get_player(plooser->owner)->name,
		     unit_name(plooser->type),
		     get_location_str_at(get_player(pwinner->owner),
					 plooser->x, plooser->y, " "));
    kill_unit(pwinner, plooser);
               /* no longer pplayer - want better msgs -- Syela */
  }
  if (pwinner == punit && unit_flag(punit->type, F_MISSILE)) {
    wipe_unit(pplayer, pwinner);
    return;
  }

  /* If attacker wins, and occupychance > 0, it might move in.  Don't move in
     if there are enemy units in the tile (a fortress or city with multiple
     defenders and unstacked combat).  Note that this could mean capturing (or
     destroying) a city. -GJW */

  if (pwinner == punit && myrand(100) < game.occupychance &&
      !is_enemy_unit_tile (def_x, def_y, punit->owner)) {

    /* Hack: make sure the unit has enough moves_left for the move to succeed,
       and adjust moves_left to afterward (if successful). */

    int old_moves = punit->moves_left;
    int full_moves = unit_move_rate (punit);
    punit->moves_left = full_moves;
    if (handle_unit_move_request (pplayer, punit, def_x, def_y, FALSE)) {
      punit->moves_left = old_moves - (full_moves - punit->moves_left);
      if (punit->moves_left < 0) {
	punit->moves_left = 0;
      }
    } else {
      punit->moves_left = old_moves;
    }
  }

  send_unit_info(0, pwinner);
}

/**************************************************************************
...
Return 1 if unit is alive, and 0 if it was killed
**************************************************************************/
int handle_unit_enter_hut(struct unit *punit)
{
  struct player *pplayer=&game.players[punit->owner];
  int ok = 1;
  int cred;

  if ((game.civstyle==1) && is_air_unit(punit)) {
    return ok;
  }

  map_get_tile(punit->x, punit->y)->special^=S_HUT;
  send_tile_info(0, punit->x, punit->y);

  if ((game.civstyle==2) && is_air_unit(punit)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Your overflight frightens the tribe;"
		       " they scatter in terror."));
    return ok;
  }

  switch (myrand(12)) {
  case 0:
    cred = 25;
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
		     _("Game: You found %d credits."), cred);
    pplayer->economic.gold += cred;
    break;
  case 1:
  case 2:
  case 3:
    cred = 50;
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: You found %d credits."), cred);
    pplayer->economic.gold+=50;
    break;
  case 4:
    cred = 100;
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: You found %d credits."), cred); 
    pplayer->economic.gold += cred;
    break;
  case 5:
  case 6:
  case 7:
/*this function is hmmm a hack */
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: You found ancient scrolls of wisdom.")); 
    {
      int res=pplayer->research.researched;
      int wasres=pplayer->research.researching;
      int new_tech;

      choose_random_tech(pplayer);
      new_tech=pplayer->research.researching;
      pplayer->research.researched=res;
      pplayer->research.researching=wasres;
 
      if (new_tech!=A_NONE) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: You gain knowledge about %s."), 
			 advances[new_tech].name);

	gamelog(GAMELOG_TECH,"%s discover %s (Hut)",
		get_nation_name_plural(pplayer->nation),advances[new_tech].name
		);

	notify_embassies(pplayer, (struct player *)0,
		  _("Game: The %s have aquired %s from ancient scrolls of wisdom"),
		  get_nation_name_plural(pplayer->nation),
		  advances[new_tech].name);

	found_new_tech(pplayer,new_tech,0,1);
      }
      else {
       pplayer->future_tech++;
       notify_player(pplayer,
                     _("Game: You gain knowledge about Future Tech. %d."),
                     pplayer->future_tech);
       pplayer->research.researchpoints++; /* don't call found_new_tech() */
      }
      
      do_free_cost(pplayer);
    }
    break;
  case 8:
  case 9:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: A band of friendly mercenaries"
		       " joins your cause."));
    create_unit(pplayer, punit->x, punit->y, find_a_unit_type(L_HUT,L_HUT_TECH),
		0, punit->homecity, -1);
    break;
  case 10:
    if (in_city_radius(punit->x, punit->y))
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: An abandoned village is here."));
    else {
      ok = unleash_barbarians(pplayer, punit->x, punit->y);
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       ok ?
		         _("Game: You have unleashed a horde of barbarians!") :
		         _("Game: Your unit has been killed by barbarians."));
    }
    break;
  case 11:
    if (is_ok_city_spot(punit->x, punit->y)) {
      if (terrain_control.may_road) {
	map_set_special(punit->x, punit->y, S_ROAD);
	if (player_knows_techs_with_flag(pplayer, TF_RAILROAD))
	  map_set_special(punit->x, punit->y, S_RAILROAD);
      }
      send_tile_info(0, punit->x, punit->y);

      create_city(pplayer, punit->x, punit->y, city_name_suggestion(pplayer));
    } else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: Friendly nomads are impressed by you,"
			 " and join you."));
      create_unit(pplayer, punit->x, punit->y, get_role_unit(F_CITIES,0),
		  0, punit->homecity, -1);
    }
    break;
  }
  send_player_info(pplayer, pplayer);
  return ok;
}

/**************************************************************************
...
**************************************************************************/
static void transporter_cargo_move_to_tile(struct unit_list *units, int x, int y)
{
  unit_list_iterate(*units, punit) {
    /* cancel activities when transport moves
       otherwise could carry activity to tile where it is illegal */
    if(punit->activity!=ACTIVITY_IDLE && punit->activity!=ACTIVITY_SENTRY) {
      set_unit_activity(punit, ACTIVITY_IDLE);
      send_unit_info(0, punit);
    }
    punit->x=x;
    punit->y=y;
    unit_list_insert_back(&map_get_tile(x, y)->units, punit);
  } unit_list_iterate_end;
}

/**************************************************************************
  Will try to move to/attack the tile dest_x,dest_y.  Returns true if this
  could be done, false if it couldn't for some reason.
**************************************************************************/
int handle_unit_move_request(struct player *pplayer, struct unit *punit,
			      int dest_x, int dest_y, int igzoc)
{
  struct tile *pdesttile = map_get_tile(dest_x, dest_y);
  struct tile *psrctile = map_get_tile(punit->x, punit->y);
  struct unit *pdefender = get_defender(pplayer, punit, dest_x, dest_y);
  struct city *pcity = pdesttile->city;

  /* barbarians shouldn't enter huts */
  if (is_barbarian(pplayer) && pdesttile->special&S_HUT) {
    return 0;
  }

  /* this occurs often during lag, and to the AI due to some quirks -- Syela */
  if (same_pos(punit->x, punit->y, dest_x, dest_y))
    return 0;
 
  if (do_airline(punit, dest_x, dest_y))
    return 1;

  if (unit_flag(punit->type, F_CARAVAN)
      && pcity
      && pcity->owner != punit->owner
      && punit->homecity) {
    struct packet_unit_request req;
    req.unit_id = punit->id;
    req.city_id = pcity->id;
    req.name[0] = '\0';
    return handle_unit_establish_trade(pplayer, &req);
  }

  if (is_diplomat_unit(punit)
      && is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION, dest_x, dest_y)) {
    struct packet_diplomat_action packet;
    /* If we didn't send_unit_info the client would sometimes think that
       the diplomat didn't have any moves left and so don't pop up the box.
       (We are in the middle of the unit restore cycle when doing goto's, and
       the unit's movepoints have been restored, but we only send the unit
       info at the end of the function. */
    send_unit_info(unit_owner(punit), punit);

    /* if is_diplomat_action_available() then there must be a city or a unit */
    if ((pcity = map_get_city(dest_x,dest_y)))
      packet.target_id = pcity->id;
    else if (pdefender)
      packet.target_id = pdefender->id;
    else
      freelog(LOG_FATAL, "Bug in unithand.c");
    packet.diplomat_id = punit->id;
    packet.action_type = DIPLOMAT_CLIENT_POPUP_DIALOG;
    send_packet_diplomat_action(unit_owner(punit)->conn, &packet);
    return 0;
  }

  /*** Try to attack if there is an enemy unit on the target tile ***/
  if (pdefender && pdefender->owner!=punit->owner) {
    if (!can_unit_attack_tile(punit, dest_x , dest_y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
  		       _("Game: You can't attack there."));
      return 0;
    }
 
    if (punit->moves_left<=0)  {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: This unit has no moves left."));
      return 0;
    }

    /* DO NOT Auto-attack.  Findvictim routine will decide if we should. */
    if (pplayer->ai.control && punit->activity == ACTIVITY_GOTO) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: Aborting GOTO for AI attack procedures."));
      return 0;
    }
 
    if (punit->activity == ACTIVITY_GOTO &&
 	(dest_x != punit->goto_dest_x || dest_y != punit->goto_dest_y)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: %s aborted GOTO as there are units in the way."),
 		       unit_types[punit->type].name);
      return 0;
    }
 
    /* This is for debuging only, and seems to be obsolete as the error message
       never appears */
    if (pplayer->ai.control && punit->ai.passenger) {
      struct unit *passenger;
      passenger = unit_list_find(&psrctile->units, punit->ai.passenger);
      if (passenger) {
 	/* removed what seemed like a very bad abort() -- JMC/jjm */
 	if (!get_transporter_capacity(punit)) {
 	  freelog(LOG_NORMAL, "%s#%d@(%d,%d) thinks %s#%d is a passenger?",
 		  unit_name(punit->type), punit->id, punit->x, punit->y,
 		  unit_name(passenger->type), passenger->id);
 	}
      }
    }

    handle_unit_attack_request(pplayer, punit, pdefender);
    return 1;
  } /* End attack case */
 
  {
    struct unit *bodyguard;
    if (pplayer->ai.control &&
 	punit->ai.bodyguard > 0 &&
 	(bodyguard = unit_list_find(&psrctile->units, punit->ai.bodyguard)) &&
 	!bodyguard->moves_left) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
 		       _("Game: %s doesn't want to leave its bodyguard."),
 		       unit_types[punit->type].name);
      return 0;
    }
  }
 
  /* Mao had this problem with chariots ending turns next to enemy cities. -- Syela */
  if (pplayer->ai.control &&
      punit->moves_left <= map_move_cost(punit, dest_x, dest_y) &&
      unit_types[punit->type].move_rate > map_move_cost(punit, dest_x, dest_y) &&
      enemies_at(punit, dest_x, dest_y) &&
      !enemies_at(punit, punit->x, punit->y)) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
  		     _("Game: %s ending move early to stay out of trouble."),
  		     unit_types[punit->type].name);
    return 0;
  }

  /* If there is a city it is empty.
     If not it would have been caught in the attack case. */
  if (pcity &&
      pcity->owner!=punit->owner && 
      (is_air_unit(punit) || !is_military_unit(punit))) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: Only ground troops can take over a city."));
    return 0;
  }

  /******* ok now move the unit *******/
  if(can_unit_move_to_tile(punit, dest_x, dest_y, igzoc) &&
     try_move_unit(punit, dest_x, dest_y)) {
    int src_x, src_y, ok;
    struct unit_list cargolist;
    int transport_units = 1;
 
    connection_do_buffer(pplayer->conn);

    src_x = punit->x; src_y = punit->y;

    if (punit->ai.ferryboat) {
      struct unit *ferryboat;
      ferryboat = unit_list_find(&psrctile->units, punit->ai.ferryboat);
      if (ferryboat) {
	freelog(LOG_DEBUG, "%d disembarking from ferryboat %d",
		punit->id, punit->ai.ferryboat);
        ferryboat->ai.passenger = 0;
        punit->ai.ferryboat = 0;
      }
    }

    /* A transporter should not take units with it when on an attack goto -- fisch */
    if ((punit->activity == ACTIVITY_GOTO) &&
	get_defender(pplayer, punit, punit->goto_dest_x, punit->goto_dest_y) &&
	psrctile->terrain != T_OCEAN) {
      transport_units = 0;
    }

    unit_list_unlink(&psrctile->units, punit);
    if (get_transporter_capacity(punit) && transport_units) {
      transporter_cargo_to_unitlist(punit, &cargolist);

      unit_list_iterate(cargolist, pcarried) { 
	pcarried->x = dest_x;
	pcarried->y = dest_y;
	send_unit_info_to_onlookers(0, pcarried,src_x,src_y);
	handle_unit_move_consequences(pcarried, src_x, src_y, dest_x, dest_y, 0);
      }
      unit_list_iterate_end;
    }

    punit->moves_left -= map_move_cost(punit, dest_x, dest_y);
    if (punit->moves_left < 0)
      punit->moves_left = 0;

    punit->x = dest_x;
    punit->y = dest_y;

    /* set activity to sentry if boarding a ship */
    if (is_ground_unit(punit) &&
	pdesttile->terrain == T_OCEAN &&
	!(pplayer->ai.control)) {
      set_unit_activity(punit, ACTIVITY_SENTRY);
    }

    unit_list_insert(&pdesttile->units, punit);

    if (get_transporter_capacity(punit) && transport_units) {
      transporter_cargo_move_to_tile(&cargolist, punit->x, punit->y);
      genlist_unlink_all(&cargolist.list); 
    }

    /***** ok entered new tile *****/

    punit->moved = 1;

    send_unit_info_to_onlookers(0, punit, src_x, src_y);
    ok = handle_unit_move_consequences(punit, src_x, src_y, dest_x, dest_y, 1);

    connection_do_unbuffer(pplayer->conn);

    if (!ok) /* has it been killed? then exit here */
      return 1;

    /* bodyguard code */
    if(pplayer->ai.control && punit->ai.bodyguard > 0) {
      struct unit *bodyguard = unit_list_find(&psrctile->units, punit->ai.bodyguard);
      if (bodyguard) {
	int success;
	/* FIXME: it is stupid to set to ACTIVITY_IDLE if the unit is
	   ACTIVITY_FORTIFIED and the unit has no chance of moving anyway */
	handle_unit_activity_request(bodyguard, ACTIVITY_IDLE);
	success = handle_unit_move_request(pplayer, bodyguard,
					   dest_x, dest_y, igzoc);
	freelog(LOG_DEBUG, "Dragging %s from (%d,%d)->(%d,%d) (Success=%d)",
		unit_types[bodyguard->type].name, src_x, src_y,
		dest_x, dest_y, success);
	handle_unit_activity_request(bodyguard, ACTIVITY_FORTIFYING);
      }
    }

    return 1;
  } else { /* Move failed */
    return 0;
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_help_build_wonder(struct player *pplayer, 
				   struct packet_unit_request *req)
{
  struct unit *punit;
  struct city *pcity_dest;
    
  punit = player_find_unit_by_id(pplayer, req->unit_id);
  if (!punit || !unit_flag(punit->type, F_CARAVAN))
    return;

  pcity_dest = find_city_by_id(req->city_id);
  if (!pcity_dest || !unit_can_help_build_wonder(punit, pcity_dest))
    return;

  if (!is_tiles_adjacent(punit->x, punit->y, pcity_dest->x, pcity_dest->y))
    return;

  if (!(same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
	|| try_move_unit(punit, pcity_dest->x, pcity_dest->y)))
    return;

  /* we're there! */

  pcity_dest->shield_stock+=50;
  if (build_points_left(pcity_dest) < 0) {
    pcity_dest->shield_stock = improvement_value(pcity_dest->currently_building);
  }

  connection_do_buffer(pplayer->conn);
  notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		   _("Game: Your %s helps build the %s in %s (%d remaining)."), 
		   unit_name(punit->type),
		   get_improvement_type(pcity_dest->currently_building)->name,
		   pcity_dest->name, 
		   build_points_left(pcity_dest));

  wipe_unit(0, punit);
  send_player_info(pplayer, pplayer);
  send_city_info(pplayer, pcity_dest);
  connection_do_unbuffer(pplayer->conn);
}

/**************************************************************************
...
**************************************************************************/
int handle_unit_establish_trade(struct player *pplayer, 
				 struct packet_unit_request *req)
{
  struct unit *punit;
  struct city *pcity_homecity, *pcity_dest;
  int revenue;
  
  punit = player_find_unit_by_id(pplayer, req->unit_id);
  if (!punit || !unit_flag(punit->type, F_CARAVAN))
    return 0;
    
  pcity_homecity=player_find_city_by_id(pplayer, punit->homecity);
  pcity_dest=find_city_by_id(req->city_id);
  if(!pcity_homecity || !pcity_dest)
    return 0;
    
  if (!is_tiles_adjacent(punit->x, punit->y, pcity_dest->x, pcity_dest->y))
    return 0;

  if (!(same_pos(punit->x, punit->y, pcity_dest->x, pcity_dest->y)
	|| try_move_unit(punit, pcity_dest->x, pcity_dest->y)))
    return 0;

  if (!can_establish_trade_route(pcity_homecity, pcity_dest)) {
    int i;
    notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		     _("Game: Sorry, your %s cannot establish"
		       " a trade route here!"),
		     unit_name(punit->type));
    for (i=0;i<4;i++) {
      if (pcity_homecity->trade[i]==pcity_dest->id) {
	notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		      _("      A traderoute already exists between %s and %s!"),
		      pcity_homecity->name, pcity_dest->name);
	return 0;
      }
    }
    if (city_num_trade_routes(pcity_homecity)==4) {
      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		       _("      The city of %s already has 4 trade routes!"),
		       pcity_homecity->name);
      return 0;
    } 
    if (city_num_trade_routes(pcity_dest)==4) {
      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		       _("      The city of %s already has 4 trade routes!"),
		       pcity_dest->name);
      return 0;
    }
    return 0;
  }
  
  revenue = establish_trade_route(pcity_homecity, pcity_dest);
  connection_do_buffer(pplayer->conn);
  notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		   _("Game: Your %s from %s has arrived in %s,"
		   " and revenues amount to %d in gold and research."), 
		   unit_name(punit->type), pcity_homecity->name,
		   pcity_dest->name, revenue);
  wipe_unit(0, punit);
  pplayer->economic.gold+=revenue;
  update_tech(pplayer, revenue);
  send_player_info(pplayer, pplayer);
  city_refresh(pcity_homecity);
  city_refresh(pcity_dest);
  send_city_info(pplayer, pcity_homecity);
  send_city_info(city_owner(pcity_dest), pcity_dest);
  connection_do_unbuffer(pplayer->conn);
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_enter_city(struct unit *punit, struct city *pcity)
{
  int i, n, do_civil_war = 0;
  int coins;
  struct player *pplayer = unit_owner(punit);
  struct player *cplayer;
  struct city *pnewcity;

  if (punit->owner==pcity->owner)
    return;

  cplayer = city_owner(pcity);
  
  /* If a capital is captured, then spark off a civil war 
     - Kris Bubendorfer
     Also check spaceships --dwp
  */
  if(city_got_building(pcity, B_PALACE)
     && ((cplayer->spaceship.state == SSHIP_STARTED)
	 || (cplayer->spaceship.state == SSHIP_LAUNCHED))) {
    spaceship_lost(cplayer);
  }
  
  if(city_got_building(pcity, B_PALACE) 
     && city_list_size(&cplayer->cities) >= game.civilwarsize 
     && game.nplayers < game.nation_count
     && game.civilwarsize < GAME_MAX_CIVILWARSIZE) {
    n = 0;
    for( i = 0; i < game.nplayers; i++ )
      if(!is_barbarian(&game.players[i]))
	n++;
    if(n < MAX_NUM_PLAYERS && civil_war_triggered(cplayer))
      do_civil_war = 1;
  }
  
  pcity->size--;
  if (pcity->size<1) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		     _("Game: You destroy %s completely."), pcity->name);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s has been destroyed by %s."), 
		     pcity->name, pplayer->name);
    gamelog(GAMELOG_LOSEC,"%s (%s) (%i,%i) destroyed by %s",
	    pcity->name,
	    get_nation_name(game.players[pcity->owner].nation),
	    pcity->x,pcity->y,
	    get_nation_name_plural(pplayer->nation));
    remove_city_from_minimap(pcity->x, pcity->y);
    remove_city(pcity);
    if (do_civil_war)
      civil_war(cplayer);
    return;
  }

  city_auto_remove_worker(pcity);
  coins=cplayer->economic.gold;
  coins=myrand((coins/20)+1)+(coins*(pcity->size))/200;
  pplayer->economic.gold+=coins;
  cplayer->economic.gold-=coins;
  send_player_info(cplayer, cplayer);
  if (pcity->original != pplayer->player_no) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     _("Game: You conquer %s, your lootings accumulate"
		       " to %d gold!"), 
		     pcity->name, coins);
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s conquered %s and looted %d gold"
		       " from the city."),
		     pplayer->name, pcity->name, coins);
    gamelog(GAMELOG_CONQ, "%s (%s) (%i,%i) conquered by %s",
	    pcity->name,
	    get_nation_name(game.players[pcity->owner].nation),
	    pcity->x,pcity->y,
	    get_nation_name_plural(pplayer->nation));
    
  } else {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     _("Game: You have liberated %s!!"
		       " lootings accumulate to %d gold."),
		     pcity->name, coins);
    
    notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		     _("Game: %s liberated %s and looted %d gold"
		       " from the city."),
		     pplayer->name, pcity->name, coins);
    gamelog(GAMELOG_CONQ, "%s (%s) (%i,%i) liberated by %s",
	    pcity->name,
	    get_nation_name(game.players[pcity->owner].nation),
	    pcity->x,pcity->y,
	    get_nation_name_plural(pplayer->nation));
  }

  get_a_tech(pplayer, cplayer);
  make_partisans(pcity);

  pnewcity = transfer_city(pplayer, cplayer, pcity , 0, 0, 1, 1);

  if (do_civil_war)
    civil_war(cplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_auto_request(struct player *pplayer, 
			      struct packet_unit_request *req)
{
  struct unit *punit = player_find_unit_by_id(pplayer, req->unit_id);

  if (punit==NULL || !can_unit_do_auto(punit))
    return;

  punit->ai.control=1;
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
      int prereq = map_get_infrastructure_prerequisite(old_target);
      if (prereq) {
	unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
	  if ((punit2->activity == ACTIVITY_PILLAGE) &&
	      (punit2->activity_target == prereq)) {
	    set_unit_activity(punit2, ACTIVITY_IDLE);
	    send_unit_info(0, punit2);
	  }
	unit_list_iterate_end;
      }
    }
    break;
  case ACTIVITY_EXPLORE:
    if (punit->moves_left > 0) {
      int id = punit->id;
      ai_manage_explorer(unit_owner(punit), punit);
      /* ai_manage_explorer sets the activity to idle, so we reset it */
      if ((punit = find_unit_by_id(id))) {
	set_unit_activity(punit, ACTIVITY_EXPLORE);
	send_unit_info(0, punit);
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
  if(can_unit_do_activity(punit, new_activity)) {
    enum unit_activity old_activity = punit->activity;
    int old_target = punit->activity_target;
    set_unit_activity(punit, new_activity);
    send_unit_info(0, punit);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request_targeted(struct unit *punit, 
					   enum unit_activity new_activity,
					   int new_target)
{
  if(can_unit_do_activity_targeted(punit, new_activity, new_target)) {
    enum unit_activity old_activity = punit->activity;
    int old_target = punit->activity_target;
    set_unit_activity_targeted(punit, new_activity, new_target);
    send_unit_info(0, punit);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_unload_request(struct player *pplayer, 
				struct packet_unit_request *req)
{
  struct unit *punit;
  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
      if(punit2->activity==ACTIVITY_SENTRY) {
	set_unit_activity(punit2, ACTIVITY_IDLE);
	send_unit_info(0, punit2);
      }
    }
    unit_list_iterate_end;
  }
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void handle_unit_nuke(struct player *pplayer, 
                     struct packet_unit_request *req)
{
  struct unit *punit;
  
  if((punit=player_find_unit_by_id(pplayer, req->unit_id)))
    handle_unit_attack_request(pplayer, punit, punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_paradrop_to(struct player *pplayer, 
                     struct packet_unit_request *req)
{
  struct unit *punit;
  
  if((punit=player_find_unit_by_id(pplayer, req->unit_id))) {
    do_paradrop(pplayer,punit,req->x, req->y);
  }
}

/**************************************************************************
We remove the unit and see if it's disapperance have affected the homecity
and the city it was in.
**************************************************************************/
void server_remove_unit(struct unit *punit)
{
  struct city *pcity = map_get_city(punit->x, punit->y);
  struct city *phomecity = find_city_by_id(punit->homecity);

  remove_unit_sight_points(punit);
  game_remove_unit(punit->id);  

  if (phomecity) {
    city_refresh(phomecity);
    send_city_info(get_player(phomecity->owner), phomecity);
  }
  if (pcity && pcity != phomecity) {
    city_refresh(pcity);
    send_city_info(get_player(pcity->owner), pcity);
  }
}
