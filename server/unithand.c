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
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    punit->goto_dest_x=req->x;
    punit->goto_dest_y=req->y;

    set_unit_activity(punit, ACTIVITY_GOTO);

    send_unit_info(0, punit, 0);
      
    do_unit_goto(pplayer, punit);  
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
  unit_list_iterate(pplayer->units, punit) {
    if (cost > pplayer->economic.gold)
      break;
    if (punit->type == packet->type && map_get_city(punit->x, punit->y)) {
      pplayer->economic.gold -= cost;
      if (punit->hp==get_unit_type(punit->type)->hp) {
	punit->hp=get_unit_type(to_unit)->hp;
      }
      punit->type = to_unit;
      send_unit_info(0, punit, 0);
      upgraded++;
    }
  }
  unit_list_iterate_end;
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
  
  if(!(punit=unit_list_find(&pplayer->units, packet->unit_id))) return;
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
  if(punit->hp==get_unit_type(punit->type)->hp) 
    punit->hp=get_unit_type(to_unit)->hp;
  punit->type = to_unit;
  send_unit_info(0, punit, 0);
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
  struct unit *pdiplomat=unit_list_find(&pplayer->units, packet->diplomat_id);
  struct unit *pvictim=find_unit_by_id(packet->target_id);
  struct city *pcity=find_city_by_id(packet->target_id);
  int x,y, move_cost;  

  if (!pdiplomat) return;
  if (!unit_flag(pdiplomat->type, F_DIPLOMAT)) 
    return;

  if(pcity){
    x = pcity->x;
    y = pcity->y;
  }else{
    x = pvictim->x;
    y = pvictim->y;
  }
  move_cost = tile_move_cost(pdiplomat,pdiplomat->x,pdiplomat->y, x, y);
  
  if(pdiplomat->moves_left >= move_cost ||
     pdiplomat->moves_left >= unit_move_rate(pdiplomat)) {

    switch(packet->action_type) {
    case DIPLOMAT_BRIBE:
      if(pvictim && diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE,
					   pvictim->x, pvictim->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	diplomat_bribe(pplayer, pdiplomat, pvictim);
      }
      break;
    case SPY_SABOTAGE_UNIT:
      if(pvictim && diplomat_can_do_action(pdiplomat, SPY_SABOTAGE_UNIT,
					   pvictim->x, pvictim->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	spy_sabotage_unit(pplayer, pdiplomat, pvictim);
      }
      break;
     case DIPLOMAT_SABOTAGE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_SABOTAGE, 
					 pcity->x, pcity->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	diplomat_sabotage(pplayer, pdiplomat, pcity, packet->value);
      }
      break;
    case SPY_POISON:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_POISON,
					 pcity->x, pcity->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	spy_poison(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_INVESTIGATE:

      /* Note: this is free (no movement cost) for spies */
      
      if(pcity && diplomat_can_do_action(pdiplomat,DIPLOMAT_INVESTIGATE ,
					 pcity->x, pcity->y)){
	send_unit_info(pplayer, pdiplomat, 0);
	diplomat_investigate(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_EMBASSY:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_EMBASSY, 
					 pcity->x, pcity->y)) {
	if(pdiplomat->foul){
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			   _("Game: Your %s was executed in %s on suspicion"
			     " of spying.  The %s welcome future diplomatic"
			     " efforts providing the Ambassador is reputable."),
			   unit_name(pdiplomat->type),
			   pcity->name, get_nation_name_plural(pplayer->nation));
	  
	  notify_player_ex(&game.players[pcity->owner], pcity->x, pcity->y,
			   E_DIPLOMATED,
			   _("You executed a %s the %s had sent to establish"
			     " an embassy in %s"),
			   unit_name(pdiplomat->type),
			   get_nation_name_plural(pplayer->nation),
			   pcity->name);
	  
	  wipe_unit(0, pdiplomat);
	  break;
	}

        if(is_barbarian(&game.players[pcity->owner])) {
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			   _("Game: Your %s was executed in %s by primitive %s."),
			   unit_name(pdiplomat->type),
			   pcity->name, get_nation_name_plural(pplayer->nation));
	  wipe_unit(0, pdiplomat);
	  break;
        }
	
	pplayer->embassy|=(1<<pcity->owner);
	send_player_info(pplayer, pplayer);
	notify_player_ex(pplayer, pcity->x, pcity->y, E_MY_DIPLOMAT,
			 _("Game: You have established an embassy in %s."),
			 pcity->name);
	
	notify_player_ex(&game.players[pcity->owner], pcity->x, pcity->y,
			 E_DIPLOMATED, 
			 _("Game: The %s have established an embassy in %s."),
		         get_nation_name_plural(pplayer->nation), pcity->name);
        gamelog(GAMELOG_EMBASSY,"%s establish an embassy in %s (%s) (%i,%i)\n",
                get_nation_name_plural(pplayer->nation),
                pcity->name,
                get_nation_name(game.players[pcity->owner].nation),
		pcity->x,pcity->y);

      }
      wipe_unit(0, pdiplomat);
      break;
    case DIPLOMAT_INCITE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE, 
					 pcity->x, pcity->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	diplomat_incite(pplayer, pdiplomat, pcity);
      }
      break;
    case DIPLOMAT_STEAL:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_STEAL, 
					 pcity->x, pcity->y)){
	pdiplomat->moves_left -= move_cost;    
	send_unit_info(pplayer, pdiplomat, 0);
	diplomat_get_tech(pplayer, pdiplomat, pcity, packet->value);
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
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    struct city *pcity;
    if((pcity=city_list_find_id(&pplayer->cities, req->city_id))) {
      unit_list_insert(&pcity->units_supported, punit);
      
      if((pcity=city_list_find_id(&pplayer->cities, punit->homecity)))
	unit_list_unlink(&pcity->units_supported, punit);
      
      punit->homecity=req->city_id;
      send_unit_info(pplayer, punit, 0);
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
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    if ((pcity=map_get_city(punit->x, punit->y))) {
      pcity->shield_stock+=(get_unit_type(punit->type)->build_cost/2);
      send_city_info(pplayer, pcity, 0);
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
  
  punit = unit_list_find(&pplayer->units, req->unit_id) ;
  if(!punit)
    return;
  
  unit_name = get_unit_type(punit->type)->name;
  pcity = map_get_city(punit->x, punit->y);
  
  if (!unit_flag(punit->type, F_SETTLERS)) {
    char *us = get_units_with_flag_string(F_SETTLERS);
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
      send_city_info(pplayer, pcity, 0);
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       _("Game: %s added to aid %s in growing."), 
		       unit_name, pcity->name);
    } else {
      if(pcity->size > 8) {
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
  send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
  create_city(pplayer, punit->x, punit->y, name);
  game_remove_unit(req->unit_id);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo)
{
  struct unit *punit;

  punit=unit_list_find(&pplayer->units, pinfo->id);

  if(punit) {
    if (!same_pos(punit->x, punit->y, pinfo->x, pinfo->y)) {
      punit->ai.control=0;
      handle_unit_move_request(pplayer, punit, pinfo->x, pinfo->y);
    }
    else if(punit->activity!=pinfo->activity || punit->activity_target!=pinfo->activity_target ||
	    punit->ai.control==1) {
      /* Treat change in ai.control as change in activity, so
       * idle autosettlers behave correctly when selected --dwp
       */
      punit->ai.control=0;
      handle_unit_activity_request_targeted(pplayer, punit,
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

  punit=unit_list_find(&pplayer->units, pmove->unid);
  if(punit)  {
    handle_unit_move_request(pplayer, punit, pmove->x, pmove->y);
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
  struct city *pcity;
  struct city *incity, *nearcity1, *nearcity2;
  incity = map_get_city(pdefender->x, pdefender->y);
  
  freelog(LOG_DEBUG, "Start attack: %s's %s against %s's %s.",
	  pplayer->name, unit_types[punit->type].name, 
	  game.players[pdefender->owner].name, 
	  unit_types[pdefender->type].name);

  if(unit_flag(punit->type, F_NUCLEAR)) {
    struct packet_nuke_tile packet;
    
    packet.x=pdefender->x;
    packet.y=pdefender->y;
    if ((pcity=sdi_defense_close(punit->owner, pdefender->x, pdefender->y))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_UNIT_LOST_ATT,
		       _("Game: Your Nuclear missile was shot down by"
			 " SDI defences, what a waste."));
      notify_player_ex(&game.players[pcity->owner], 
		       pdefender->x, pdefender->y, E_UNIT_WIN, 
		       _("Game: The nuclear attack on %s was avoided by"
			 " your SDI defense."),
		       pcity->name); 
      wipe_unit(0, punit);
      return;
    } 

    for(o=0; o<game.nplayers; o++)
      send_packet_nuke_tile(game.players[o].conn, &packet);
    
    do_nuclear_explosion(pdefender->x, pdefender->y);
    return;
  }
  
  unit_versus_unit(punit, pdefender);

  /* Adjust attackers moves_left _after_ unit_versus_unit() so that
   * the movement attack modifier is correct! --dwp
   */
  punit->moves_left-=3;
  if(punit->moves_left<0)
    punit->moves_left=0;

  if (punit->hp && (pcity=map_get_city(pdefender->x, pdefender->y)) && pcity->size>1 && !city_got_citywalls(pcity) && is_ground_unit(punit)) {
    pcity->size--;
    city_auto_remove_worker(pcity);
    city_refresh(pcity);
    send_city_info(0, pcity, 0);
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
    if(map_get_known(punit->x, punit->y, &game.players[o]) ||
       map_get_known(pdefender->x, pdefender->y, &game.players[o]))
      send_packet_unit_combat(game.players[o].conn, &combat);

  nearcity1 = dist_nearest_city(get_player(pwinner->owner), pdefender->x, pdefender->y, 0, 0);
  nearcity2 = dist_nearest_city(get_player(plooser->owner), pdefender->x, pdefender->y, 0, 0);
  
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
		     pdefender->x, pdefender->y, E_UNIT_LOST_ATT, 
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
  send_unit_info(0, pwinner, 0);
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
  send_tile_info(0, punit->x, punit->y, TILE_KNOWN);

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

      choose_random_tech(pplayer);
 
      pplayer->research.researchpoints++;
      if (pplayer->research.researching!=A_NONE) {
       notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                        _("Game: You gain knowledge about %s."), 
                        advances[pplayer->research.researching].name);
       if (tech_flag(pplayer->research.researching,TF_RAILROAD)) {
	 upgrade_city_rails(pplayer, 1);
       }
       set_invention(pplayer, pplayer->research.researching, TECH_KNOWN);
      }
      else {
       pplayer->future_tech++;
       notify_player(pplayer,
                     _("Game: You gain knowledge about Future Tech. %d."),
                     pplayer->future_tech);
      }
      remove_obsolete_buildings(pplayer);
      
      if (get_invention(pplayer,wasres)==TECH_KNOWN) {
	if (!choose_goal_tech(pplayer))
	  choose_random_tech(pplayer);
	pplayer->research.researched=res;
      }  else {
	pplayer->research.researched=res;
	pplayer->research.researching=wasres;
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
		       _("Game: You have unleashed a horde of barbarians!"));
      if(!ok)
        notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
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
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);

      create_city(pplayer, punit->x, punit->y, city_name_suggestion(pplayer));
    } else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: Friendly nomads are impressed by you,"
			 " and join you."));
      create_unit(pplayer, punit->x, punit->y, get_role_unit(F_SETTLERS,0),
		  0, punit->homecity, -1);
    }
    break;
  }
  send_player_info(pplayer, pplayer);
  return ok;
}


/*****************************************************************
  Will wake up any neighboring enemy sentry units
*****************************************************************/
static void wakeup_neighbor_sentries(struct player *pplayer,
				     int cent_x, int cent_y)
{
  int x,y;
/*  struct unit *punit; The unit_list_iterate defines punit locally. -- Syela */

  for (x = cent_x-1;x <= cent_x+1;x++)
    for (y = cent_y-1;y <= cent_y+1;y++)
      if ((x != cent_x)||(y != cent_y))
	{
	  unit_list_iterate(map_get_tile(x,y)->units, punit) {
	    if ((pplayer->player_no != punit->owner)&&
		(punit->activity == ACTIVITY_SENTRY))
	      {
		set_unit_activity(punit, ACTIVITY_IDLE);
		send_unit_info(0,punit,0);
	      }
	  }
	  unit_list_iterate_end;
	}
}

/**************************************************************************
  Will try to move to/attack the tile dest_x,dest_y.  Returns true if this
  could be done, false if it couldn't for some reason.
**************************************************************************/
int handle_unit_move_request(struct player *pplayer, struct unit *punit,
			      int dest_x, int dest_y)
{
  int unit_id, transport_units = 1, ok;
  struct unit *pdefender, *ferryboat, *bodyguard, *passenger;
  struct unit_list cargolist;
  struct city *pcity;

  /* barbarians shouldn't enter huts */
  if(is_barbarian(pplayer) && (map_get_tile(dest_x, dest_y)->special&S_HUT)) {
    return 0;
  }

  if (same_pos(punit->x, punit->y, dest_x, dest_y)) return 0;
/* this occurs often during lag, and to the AI due to some quirks -- Syela */
 
  unit_id=punit->id;
  if (do_airline(punit, dest_x, dest_y))
    return 1;

  if (unit_flag(punit->type, F_CARAVAN)
      && (pcity=map_get_city(dest_x, dest_y))
      && (pcity->owner != punit->owner)
      && punit->homecity) {
    struct packet_unit_request req;
    req.unit_id = punit->id;
    req.city_id = pcity->id;
    req.name[0] = '\0';
    return handle_unit_establish_trade(pplayer, &req);
  }
  
  pdefender=get_defender(pplayer, punit, dest_x, dest_y);

  if(pdefender && pdefender->owner!=punit->owner) {
    if(can_unit_attack_tile(punit,dest_x , dest_y)) {
      if(punit->moves_left<=0)  {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: This unit has no moves left."));
        return 0;
      } else if (pplayer->ai.control && punit->activity == ACTIVITY_GOTO) {
/* DO NOT Auto-attack.  Findvictim routine will decide if we should. */
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Aborting GOTO for AI attack procedures."));
        return 0;
      } else {
        if (pplayer->ai.control && punit->ai.passenger) {
/* still trying to track down this weird bug.  I can't find anything in
my own code that seems responsible, so I'm guessing that recycling ids
is the source of the problem.  Hopefully we won't abort() now. -- Syela */
          passenger = unit_list_find(&(map_get_tile(punit->x, punit->y)->units),
              punit->ai.passenger);
          if (passenger) {
            if (get_transporter_capacity(punit)) abort();
            else {
	      freelog(LOG_NORMAL, "%s#%d@(%d,%d) thinks %s#%d is a passenger?",
		      unit_name(punit->type), punit->id, punit->x, punit->y,
		      unit_name(passenger->type), passenger->id);
	    }
          }
        }
        if (get_unit_type(punit->type)->attack_strength == 0) {
          char message[MAX_LEN_NAME + 64];
          sprintf(message, _("Game: A %s cannot attack other units."),
                           unit_name(punit->type));
          notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                           message);
          return 0;
        }
	handle_unit_attack_request(pplayer, punit, pdefender);
        return 1;
      };
    } else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       _("Game: You can't attack there."));
      return 0;
    };
  } else if (punit->ai.bodyguard > 0 && (bodyguard = unit_list_find(&map_get_tile(punit->x,
      punit->y)->units, punit->ai.bodyguard)) && !bodyguard->moves_left) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s doesn't want to leave its bodyguard."),
		     unit_types[punit->type].name);
  } else if (pplayer->ai.control && punit->moves_left <=
             map_move_cost(punit, dest_x, dest_y) &&
             unit_types[punit->type].move_rate >
             map_move_cost(punit, dest_x, dest_y) &&
             enemies_at(punit, dest_x, dest_y) &&
             !enemies_at(punit, punit->x, punit->y)) {
/* Mao had this problem with chariots ending turns next to enemy cities. -- Syela */
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     _("Game: %s ending move early to stay out of trouble."),
		     unit_types[punit->type].name);
  } else if(can_unit_move_to_tile(punit, dest_x, dest_y) && try_move_unit(punit, dest_x, dest_y)) {
    int src_x, src_y;
    
    if((pcity=map_get_city(dest_x, dest_y))) {
      if (pcity->owner!=punit->owner &&  
	  (is_air_unit(punit) || !is_military_unit(punit))) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 _("Game: Only ground troops can take over a city."));
	return 0;
      }
    }

    if(!unit_list_find(&pplayer->units, unit_id))
      return 0; /* diplomat or caravan action killed unit */

    connection_do_buffer(pplayer->conn);

    /* light the squares the unit is entering */
    light_square(pplayer, dest_x, dest_y, 
		 get_unit_type(punit->type)->vision_range);
    
    /* ok now move the unit */

    if (punit->ai.ferryboat) {
      ferryboat = unit_list_find(&map_get_tile(punit->x, punit->y)->units,
                  punit->ai.ferryboat);
      if (ferryboat) {
	freelog(LOG_DEBUG, "%d disembarking from ferryboat %d",
		      punit->id, punit->ai.ferryboat);
        ferryboat->ai.passenger = 0;
        punit->ai.ferryboat = 0;
      }
    } /* just clearing that up */

    if((punit->activity == ACTIVITY_GOTO) &&
       get_defender(pplayer,punit,punit->goto_dest_x,punit->goto_dest_y) &&
       (map_get_tile(punit->x,punit->y)->terrain != T_OCEAN)) {
        /* we should not take units with us -- fisch */
        transport_units = 0;
    }
    
    src_x=punit->x;
    src_y=punit->y;

    unit_list_unlink(&map_get_tile(src_x, src_y)->units, punit);
    
    if(get_transporter_capacity(punit) && transport_units) {
        transporter_cargo_to_unitlist(punit, &cargolist);
        
        unit_list_iterate(cargolist, pcarried) { 
            pcarried->x=dest_x;
            pcarried->y=dest_y;
            send_unit_info(0, pcarried, 1);
        }
        unit_list_iterate_end;
    }
    
    if((punit->moves_left-=map_move_cost(punit, dest_x, dest_y))<0)
      punit->moves_left=0;

    punit->x=dest_x;
    punit->y=dest_y;

    send_unit_info(0, punit, 1);
    
    unit_list_insert(&map_get_tile(dest_x, dest_y)->units, punit);
    
    if(get_transporter_capacity(punit) && transport_units) {
      move_unit_list_to_tile(&cargolist, punit->x, punit->y);
      genlist_unlink_all(&cargolist.list); 
    }
    
    /* ok entered new tile */
    
    if(pcity)
      handle_unit_enter_city(pplayer, pcity);

    ok = 1;
    if((map_get_tile(dest_x, dest_y)->special&S_HUT)) {
      /* punit might get killed by horde of barbarians */
      ok = handle_unit_enter_hut(punit);
    }
     
    wakeup_neighbor_sentries(pplayer,dest_x,dest_y);
    
    connection_do_unbuffer(pplayer->conn);

    if (!ok) return 1;
    
    /* bodyguard code */
    if(unit_list_find(&pplayer->units, unit_id)) {
      if (punit->ai.bodyguard > 0) {
        bodyguard = unit_list_find(&(map_get_tile(src_x, src_y)->units),
                    punit->ai.bodyguard);
        if (bodyguard) {
	  int success;
          handle_unit_activity_request(pplayer, bodyguard, ACTIVITY_IDLE);
	  /* may be fortifying, have to FIX this eventually -- Syela */
	  success = handle_unit_move_request(pplayer, bodyguard,
					     dest_x, dest_y);
	  freelog(LOG_DEBUG, "Dragging %s from (%d,%d)->(%d,%d) (Success=%d)",
		  unit_types[bodyguard->type].name, src_x, src_y,
		  dest_x, dest_y, success);
          handle_unit_activity_request(pplayer, bodyguard, ACTIVITY_FORTIFY);
        }
      }
    }

    punit->moved=1;

    return 1;
  }
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_help_build_wonder(struct player *pplayer, 
				   struct packet_unit_request *req)
{
  struct unit *punit;
  struct city *pcity_dest;
    
  punit = unit_list_find(&pplayer->units, req->unit_id);
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
  send_city_info(pplayer, pcity_dest, 0);
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
  
  punit = unit_list_find(&pplayer->units, req->unit_id);
  if (!punit || !unit_flag(punit->type, F_CARAVAN))
    return 0;
    
  pcity_homecity=city_list_find_id(&pplayer->cities, punit->homecity);
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
		   " and revenues amount to %d in gold."), 
		   unit_name(punit->type), pcity_homecity->name,
		   pcity_dest->name, revenue);
  wipe_unit(0, punit);
  pplayer->economic.gold+=revenue;
  send_player_info(pplayer, pplayer);
  city_refresh(pcity_homecity);
  city_refresh(pcity_dest);
  send_city_info(pplayer, pcity_homecity, 0);
  send_city_info(city_owner(pcity_dest), pcity_dest, 0);
  connection_do_unbuffer(pplayer->conn);
  return 1;
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_enter_city(struct player *pplayer, struct city *pcity)
{
  int i, x, y, old_id;
  int coins;
  struct player *cplayer;
  if(pplayer->player_no!=pcity->owner) {
    struct city *pnewcity;
    cplayer=&game.players[pcity->owner];
    pcity->size--;

    /* If a capital is captured, then spark off a civil war 
       - Kris Bubendorfer
       Also check spaceships --dwp
    */

    if(city_got_building(pcity, B_PALACE)
       && cplayer->spaceship.state == SSHIP_LAUNCHED) {
      spaceship_lost(cplayer);
    }
       
    if(city_got_building(pcity, B_PALACE) 
       && city_list_size(&cplayer->cities) >= game.civilwarsize 
       && game.nplayers < game.nation_count
       && game.nplayers < MAX_NUM_PLAYERS+MAX_NUM_BARBARIANS
       && game.civilwarsize < GAME_MAX_CIVILWARSIZE
       && civil_war_triggered(cplayer))
      civil_war(cplayer);

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
      return;
    }
    city_auto_remove_worker(pcity);
    get_a_tech(pplayer, cplayer);
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

    pnewcity=fc_malloc(sizeof(struct city));
    make_partisans(pcity);
    *pnewcity=*pcity;
    remove_city(pcity);

    /* now set things up for the new owner */
    
    old_id = pnewcity->id;
    pnewcity->id=get_next_id_number();
    
    add_city_to_cache(pnewcity);
    for (i = 0; i < B_LAST; i++) {
      if (is_wonder(i) && city_got_building(pnewcity, i))
        game.global_wonders[i] = pnewcity->id;
    } 
    pnewcity->owner=pplayer->player_no;

    unit_list_init(&pnewcity->units_supported);
    city_list_insert(&pplayer->cities, pnewcity);
    
    map_set_city(pnewcity->x, pnewcity->y, pnewcity);

    if (terrain_control.may_road &&
       (player_knows_techs_with_flag(pplayer, TF_RAILROAD)) &&
       (!player_knows_techs_with_flag(cplayer, TF_RAILROAD)) &&
       (!(map_get_special(pnewcity->x,pnewcity->y)&S_RAILROAD))) {
      notify_player(pplayer,
		    _("Game: The people in %s are stunned by your"
		      " technological insight!\n"
		      "      Workers spontaneously gather and upgrade"
		      " the city with railroads."),
		    pnewcity->name);
      map_set_special(pnewcity->x, pnewcity->y, S_RAILROAD);
      send_tile_info(0, pnewcity->x, pnewcity->y, TILE_KNOWN);
    }

    raze_city(pnewcity);

    if (pplayer->ai.control) { /* let there be light! */
      for (y=0;y<5;y++) {
        for (x=0;x<5;x++) {
          if ((x==0 || x==4) && (y==0 || y==4))
            continue;
          light_square(pplayer, x+pnewcity->x-2, y+pnewcity->y-2, 0);
        }
      }
    }

    reestablish_city_trade_routes(pnewcity); 

/* relocate workers of tiles occupied by enemy units */ 
    city_check_workers(pplayer,pnewcity);  
    update_map_with_city_workers(pnewcity);
    city_refresh(pnewcity);
    initialize_infrastructure_cache(pnewcity);
    send_city_info(0, pnewcity, 0);
    send_player_info(pplayer, pplayer);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_auto_request(struct player *pplayer, 
			      struct packet_unit_request *req)
{
  struct unit *punit = unit_list_find(&pplayer->units, req->unit_id);

  if (punit==NULL || !can_unit_do_auto(punit))
    return;

  punit->ai.control=1;
  send_unit_info(pplayer, punit, 0);
}

/**************************************************************************
...
**************************************************************************/
static void handle_unit_activity_dependencies(struct unit *punit,
					      enum unit_activity old_activity,
					      int old_target)
{
  if (punit->activity == ACTIVITY_IDLE) {
    if (old_activity == ACTIVITY_PILLAGE) {
      int prereq = map_get_infrastructure_prerequisite(old_target);
      if (prereq) {
	unit_list_iterate (map_get_tile(punit->x, punit->y)->units, punit2)
	  if ((punit2->activity == ACTIVITY_PILLAGE) &&
	      (punit2->activity_target == prereq)) {
	    set_unit_activity(punit2, ACTIVITY_IDLE);
	    send_unit_info(0, punit2, 0);
	  }
	unit_list_iterate_end;
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request(struct player *pplayer, struct unit *punit, 
				  enum unit_activity new_activity)
{
  if(can_unit_do_activity(punit, new_activity)) {
    enum unit_activity old_activity = punit->activity;
    int old_target = punit->activity_target;
    set_unit_activity(punit, new_activity);
    send_unit_info(0, punit, 0);
    handle_unit_activity_dependencies(punit, old_activity, old_target);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request_targeted(struct player *pplayer,
					   struct unit *punit, 
					   enum unit_activity new_activity,
					   int new_target)
{
  if(can_unit_do_activity_targeted(punit, new_activity, new_target)) {
    enum unit_activity old_activity = punit->activity;
    int old_target = punit->activity_target;
    set_unit_activity_targeted(punit, new_activity, new_target);
    send_unit_info(0, punit, 0);
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
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, punit2) {
      if(punit2->activity==ACTIVITY_SENTRY) {
	set_unit_activity(punit2, ACTIVITY_IDLE);
	send_unit_info(0, punit2, 0);
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
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id)))
    handle_unit_attack_request(pplayer, punit, punit);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_paradrop_to(struct player *pplayer, 
                     struct packet_unit_request *req)
{
  struct unit *punit;
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    do_paradrop(pplayer,punit,req->x, req->y);
  }
}

