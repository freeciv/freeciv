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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <player.h>
#include <unithand.h>
#include <unittools.h>
#include <unitfunc.h>
#include <packets.h>
#include <civserver.h>
#include <map.h>
#include <maphand.h>
#include <cityhand.h>
#include <citytools.h>
#include <cityturn.h>
#include <unit.h>
#include <plrhand.h>
#include <city.h>
#include <log.h>
#include <mapgen.h>
#include <events.h>
#include <shared.h>
#include <aiunit.h>
#include <settlers.h>
void do_unit_goto(struct player *pplayer, struct unit *punit);

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

    punit->activity=ACTIVITY_GOTO;
    punit->activity_count=0;

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
    notify_player(pplayer, "Game: illegal package, can't upgrade %s (yet).", 
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
    notify_player(pplayer, "Game: %d %s upgraded to %s for %d credits", 
		  upgraded, unit_types[packet->type].name, 
		  unit_types[to_unit].name, cost * upgraded);
    send_player_info(pplayer, pplayer);
  } else {
    notify_player(pplayer, "Game: No units could be upgraded.");
  }
}

/**************************************************************************
 Upgrade a single unit
**************************************************************************/
void handle_unit_upgrade_request(struct player *pplayer,
                                 struct packet_unit_request *packet)
{
  int cost;
  int to_unit;
  struct unit *punit;
  struct city *pcity;
  
  if(!(punit=unit_list_find(&pplayer->units, packet->unit_id))) return;
  if(!(pcity=find_city_by_id(packet->city_id))) return;

  if(punit->x!=pcity->x || punit->y!=pcity->y)  {
    notify_player(pplayer, "Game: illegal move, unit not in city!");
    return;
  }
  if((to_unit=can_upgrade_unittype(pplayer, punit->type)) == -1) {
    notify_player(pplayer, "Game: illegal package, can't upgrade %s (yet).", 
		  unit_types[punit->type].name);
    return;
  }
  cost = unit_upgrade_price(pplayer, punit->type, to_unit);
  if(cost > pplayer->economic.gold) {
    notify_player(pplayer, "Game: Insufficient funds, upgrade costs %d", cost);
    return;
  }
  pplayer->economic.gold -= cost;
  if(punit->hp==get_unit_type(punit->type)->hp) 
    punit->hp=get_unit_type(to_unit)->hp;
  punit->type = to_unit;
  send_unit_info(0, punit, 0);
  send_player_info(pplayer, pplayer);
  notify_player(pplayer, "Game: Unit upgraded to %s for %d credits", 
		unit_types[to_unit].name, cost);
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
    if(pplayer->player_no == pcity->original) pcity->incite_revolt_cost/=2;
    req.id=packet->value;
    req.value1=pcity->incite_revolt_cost;
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
  
  if(pdiplomat && pdiplomat->moves_left >= move_cost) {
    pdiplomat->moves_left -= move_cost;    
    send_unit_info(pplayer, pdiplomat, 0);
    switch(packet->action_type) {
    case DIPLOMAT_BRIBE:
      if(pvictim && diplomat_can_do_action(pdiplomat, DIPLOMAT_BRIBE,
					   pvictim->x, pvictim->y))
	diplomat_bribe(pplayer, pdiplomat, pvictim);
      break;
    case SPY_SABOTAGE_UNIT:
      if(pvictim && diplomat_can_do_action(pdiplomat, SPY_SABOTAGE_UNIT,
					   pvictim->x, pvictim->y))
	spy_sabotage_unit(pplayer, pdiplomat, pvictim);
      break;
     case DIPLOMAT_SABOTAGE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_SABOTAGE, 
					 pcity->x, pcity->y))
	diplomat_sabotage(pplayer, pdiplomat, pcity, packet->value);
      break;
    case SPY_POISON:
      if(pcity && diplomat_can_do_action(pdiplomat, SPY_POISON,
					 pcity->x, pcity->y))
	spy_poison(pplayer, pdiplomat, pcity);
      break;
    case DIPLOMAT_INVESTIGATE:

      /* Note: this is free (no movement cost) for spies */
      
      if(pcity && diplomat_can_do_action(pdiplomat,DIPLOMAT_INVESTIGATE ,
					 pcity->x, pcity->y))
	diplomat_investigate(pplayer, pdiplomat, pcity);
      break;
    case DIPLOMAT_EMBASSY:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_EMBASSY, 
					 pcity->x, pcity->y)) {
	if(pdiplomat->foul){
	  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			   "Game: Your Spy has been executed in %s on suspicion of spying.  The %s welcome future diplomatic efforts providing the Ambassador is reputable.",pcity->name, get_race_name_plural(pplayer->race));
	  
	  notify_player_ex(&game.players[pcity->owner], pcity->x, pcity->y, E_DIPLOMATED, "You executed a spy the %s had sent to establish an embassy in %s",
			   get_race_name_plural(pplayer->race),
			   pcity->name);
	  
	  wipe_unit(0, pdiplomat);
	  break;
	}
	
	pplayer->embassy|=(1<<pcity->owner);
	send_player_info(pplayer, pplayer);
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
			 "Game: You have established an embassy in %s",
			 pcity->name);
	
	notify_player_ex(&game.players[pcity->owner], pcity->x, pcity->y, E_DIPLOMATED, "Game: The %s have established an embassy in %s",
		      get_race_name_plural(pplayer->race),
		      pcity->name);
      }
      wipe_unit(0, pdiplomat);
      break;
    case DIPLOMAT_INCITE:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_INCITE, 
					 pcity->x, pcity->y))
	diplomat_incite(pplayer, pdiplomat, pcity);
      break;
    case DIPLOMAT_STEAL:
      if(pcity && diplomat_can_do_action(pdiplomat, DIPLOMAT_STEAL, 
					 pcity->x, pcity->y)){
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
...
**************************************************************************/
void handle_unit_disband(struct player *pplayer, 
			 struct packet_unit_request *req)
{
  struct unit *punit;
  struct city *pcity;
  /* give 1/2 of the worth of the unit, to the currently builded thing 
     have to be on the location of the city_square
   */
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    if ((pcity=map_get_city(punit->x, punit->y))) {
      pcity->shield_stock+=(get_unit_type(punit->type)->build_cost/2);
      send_city_info(pplayer, pcity, 0);
    }
    wipe_unit(pplayer, punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_build_city(struct player *pplayer, 
			    struct packet_unit_request *req)
{
  struct unit *punit;
  char *name;
  struct city *pcity;
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    if (!unit_flag(punit->type, F_SETTLERS)) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		    "Game: You need a settler to build a city.");
      return;
    }  

    if(!punit->moves_left)  {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                       "Game: Settler has no moves left to build city.");
      return;
    }
    
    if ((pcity=map_get_city(punit->x, punit->y))) {
      if (pcity->size>8) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
		    "Game: Your settlers doesn't feel comfortable here.");
	return;
      }
      else {
	pcity->size++;
	  if (!add_adjust_workers(pcity))
	    auto_arrange_workers(pcity);
        wipe_unit(0, punit);
	send_city_info(pplayer, pcity, 0);
	notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		      "Game: Settlers added to aid %s in growing", 
		      pcity->name);
	return;
      }
    }
    
    if(can_unit_build_city(punit)) {
      if(!(name=get_sane_name(req->name))) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
		      "Game: Let's not build a city with such a stupid name.");
	return;
      }

      send_remove_unit(0, req->unit_id);
      map_set_special(punit->x, punit->y, S_ROAD);
      if (get_invention(pplayer, A_RAILROAD)==TECH_KNOWN)
	map_set_special(punit->x, punit->y, S_RAILROAD);
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
      create_city(pplayer, punit->x, punit->y, name);
      game_remove_unit(req->unit_id);
    } else
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		    "Game: Can't place city here.");
  }
}
/**************************************************************************
...
**************************************************************************/
void handle_unit_info(struct player *pplayer, struct packet_unit_info *pinfo)
{
  struct unit *punit;

  punit=unit_list_find(&pplayer->units, pinfo->id);

  if(punit) {
    if(punit->ai.control) {
      punit->ai.control=0;
    }
    if(punit->activity!=pinfo->activity)
      handle_unit_activity_request(pplayer, punit, pinfo->activity);
    else if (!same_pos(punit->x, punit->y, pinfo->x, pinfo->y))
      handle_unit_move_request(pplayer, punit, pinfo->x, pinfo->y);
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
  punit->moves_left-=3;
    
  if(punit->moves_left<0)
    punit->moves_left=0;
  

  if(punit->type==U_NUCLEAR) {
    struct packet_nuke_tile packet;
    
    packet.x=pdefender->x;
    packet.y=pdefender->y;
    if ((pcity=sdi_defense_close(punit->owner, pdefender->x, pdefender->y))) {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, "Game: Your Nuclear missile has been shot down by SDI, what a waste.");
      notify_player_ex(&game.players[pcity->owner], 
		       pdefender->x, pdefender->y, E_NOEVENT, 
             "Game: The nuclear attack on %s was avoided by your SDI defense",
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
  combat.attacker_hp=punit->hp;
  combat.defender_hp=pdefender->hp;
  combat.make_winner_veteran=pwinner->veteran?1:0;
  
  for(o=0; o<game.nplayers; o++)
    if(map_get_known(punit->x, punit->y, &game.players[o]) ||
       map_get_known(pdefender->x, pdefender->y, &game.players[o]))
      send_packet_unit_combat(game.players[o].conn, &combat);

  nearcity1 = dist_nearest_city(get_player(pwinner->owner), pdefender->x, pdefender->y);
  nearcity2 = dist_nearest_city(get_player(plooser->owner), pdefender->x, pdefender->y);
  
  if(punit==plooser) {
    if (incity) notify_player_ex(&game.players[pwinner->owner], 
		     pwinner->x, pwinner->y, E_UNIT_WIN, 
		  "Game: Your %s in %s survived the pathetic attack from %s's %s.",
                  unit_name(pwinner->type), incity->name,
		  game.players[plooser->owner].name,
		  unit_name(punit->type));
    else if (nearcity1 && is_tiles_adjacent(pdefender->x, pdefender->y, nearcity1->x,
         nearcity1->y)) notify_player_ex(&game.players[pwinner->owner], 
		     pwinner->x, pwinner->y, E_UNIT_WIN, 
		  "Game: Your %s outside %s survived the pathetic attack from %s's %s.",
                  unit_name(pwinner->type), nearcity1->name,
		  game.players[plooser->owner].name,
		  unit_name(punit->type));
    else if (nearcity1) notify_player_ex(&game.players[pwinner->owner], 
		     pwinner->x, pwinner->y, E_UNIT_WIN, 
		  "Game: Your %s near %s survived the pathetic attack from %s's %s.",
                  unit_name(pwinner->type), nearcity1->name,
		  game.players[plooser->owner].name,
		  unit_name(punit->type));
    else notify_player_ex(&game.players[pwinner->owner], 
		     pwinner->x, pwinner->y, E_UNIT_WIN, 
		  "Game: Your %s survived the pathetic attack from %s's %s.",
                  unit_name(pwinner->type),
		  game.players[plooser->owner].name,
		  unit_name(punit->type));
    if (incity) notify_player_ex(&game.players[plooser->owner], 
		     pdefender->x, pdefender->y, E_NOEVENT, 
		     "Game: Your attacking %s failed against %s's %s at %s!",
                      unit_name(plooser->type),
		  game.players[pwinner->owner].name,
		  unit_name(pwinner->type), incity->name);
    else if (nearcity2 && is_tiles_adjacent(pdefender->x, pdefender->y,
      nearcity2->x, nearcity2->y)) notify_player_ex(&game.players[plooser->owner], 
		     pdefender->x, pdefender->y, E_NOEVENT, 
		     "Game: Your attacking %s failed against %s's %s outside %s!",
                      unit_name(plooser->type),
		  game.players[pwinner->owner].name,
		  unit_name(pwinner->type), nearcity2->name);
    else if (nearcity2) notify_player_ex(&game.players[plooser->owner], 
		     pdefender->x, pdefender->y, E_NOEVENT, 
		     "Game: Your attacking %s failed against %s's %s near %s!",
                      unit_name(plooser->type),
		  game.players[pwinner->owner].name,
		  unit_name(pwinner->type), nearcity2->name);
    else notify_player_ex(&game.players[plooser->owner], 
		     pdefender->x, pdefender->y, E_NOEVENT, 
		     "Game: Your attacking %s failed against %s's %s!",
                      unit_name(plooser->type),
		  game.players[pwinner->owner].name,
		  unit_name(pwinner->type));
    wipe_unit(pplayer, plooser);
  }
  else {
    if (incity) notify_player_ex(&game.players[pwinner->owner], 
		     punit->x, punit->y, E_NOEVENT, 
		     "Game: Your attacking %s was successful against %s's %s at %s!",
                      unit_name(pwinner->type),
		  game.players[plooser->owner].name,
		  unit_name(plooser->type), incity->name);
    else if (nearcity1 && is_tiles_adjacent(pdefender->x, pdefender->y,
       nearcity1->x, nearcity1->y)) notify_player_ex(&game.players[pwinner->owner], 
		     punit->x, punit->y, E_NOEVENT, 
		     "Game: Your attacking %s was successful against %s's %s outside %s!",
                      unit_name(pwinner->type),
		  game.players[plooser->owner].name,
		  unit_name(plooser->type), nearcity1->name);
    else if (nearcity1) notify_player_ex(&game.players[pwinner->owner], 
		     punit->x, punit->y, E_NOEVENT, 
		     "Game: Your attacking %s was successful against %s's %s near %s!",
                      unit_name(pwinner->type),
		  game.players[plooser->owner].name,
		  unit_name(plooser->type), nearcity1->name);
    else notify_player_ex(&game.players[pwinner->owner], 
		     punit->x, punit->y, E_NOEVENT, 
		     "Game: Your attacking %s was successful against %s's %s!",
                      unit_name(pwinner->type),
		  game.players[plooser->owner].name,
		  unit_name(plooser->type));
    kill_unit(pwinner, plooser); /* no longer pplayer - want better msgs -- Syela */
  }
  if (pwinner == punit && unit_flag(punit->type, F_MISSILE)) {
    wipe_unit(pplayer, pwinner);
    return;
  }
  send_unit_info(0, pwinner, 0);
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_enter_hut(struct unit *punit)
{
  struct player *pplayer=&game.players[punit->owner];
  if (is_air_unit(punit))
    return;
  map_get_tile(punit->x, punit->y)->special^=S_HUT;
  
  send_tile_info(0, punit->x, punit->y, TILE_KNOWN);
  
  switch (myrand(12)) {
  case 0:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT, 
		     "Game: You found 25 credits.");
    pplayer->economic.gold+=25;
    break;
  case 1:
  case 2:
  case 3:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		  "Game: You found 50 credits.");
    pplayer->economic.gold+=50;
    break;
  case 4:
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		     "Game: You found 100 credits"); 
    pplayer->economic.gold+=100;
    break;
  case 5:
  case 6:
  case 7:
/*this function is hmmm a hack */
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		  "Game: You found ancient scrolls of wisdom."); 
    {
      int res=pplayer->research.researched;
      int wasres=pplayer->research.researching;

      choose_random_tech(pplayer);
 
      pplayer->research.researchpoints++;
      if (pplayer->research.researching!=A_NONE) {
       notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
                        "Game: You gain knowledge about %s.", 
                        advances[pplayer->research.researching].name);
       if (pplayer->research.researching==A_RAILROAD) {
/*         struct city_list cl=pplayer->cities;*/
         struct genlist_iterator myiter;
         genlist_iterator_init(&myiter, &pplayer->cities.list, 0);
         notify_player(pplayer, "Game: New hope sweeps like fire through the country as the discovery of railroad is announced.\n      Workers spontaneously gather and upgrade all cities with railroads.");
         for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
           struct city *pcity=(struct city *)ITERATOR_PTR(myiter);
           map_set_special(pcity->x, pcity->y, S_RAILROAD);
           send_tile_info(0, pcity->x, pcity->y, TILE_KNOWN);
         }
       }
       set_invention(pplayer, pplayer->research.researching, TECH_KNOWN);
      }
      else {
       pplayer->future_tech++;
       notify_player(pplayer,
                     "Game: You gain knowledge about Future Tech. %d.",
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
		     "Game: A band of friendly mercenaries joins your cause.");
    create_unit(pplayer, punit->x, punit->y, find_a_unit_type(), 0, punit->homecity, -1);
    break;
  case 10:
    if (in_city_radius(punit->x, punit->y))
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       "Game: An abandoned village is here.");
    else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       "Game: Your unit has been cowardly slaughtered by a band of barbarians");
      wipe_unit(pplayer, punit);
    }
    break;
  case 11:
    if (is_ok_city_spot(punit->x, punit->y)) {
      map_set_special(punit->x, punit->y, S_ROAD);
      send_tile_info(0, punit->x, punit->y, TILE_KNOWN);

      create_city(pplayer, punit->x, punit->y, city_name_suggestion(pplayer));
    } else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		   "Game: Friendly nomads are impressed by you, and join you");
      create_unit(pplayer, punit->x, punit->y, U_SETTLERS, 0, punit->homecity, -1);
    }
    break;
  }
  send_player_info(pplayer, pplayer);
}


/*****************************************************************
  Will wake up any neighboring enemy sentry units
  *****************************************************************/
void wakeup_neighbor_sentries(struct player *pplayer,int cent_x,int cent_y)
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
  int unit_id;
  struct unit *pdefender, *ferryboat, *bodyguard, *passenger;
  struct unit_list cargolist;

  if (same_pos(punit->x, punit->y, dest_x, dest_y)) return 0;
/* this occurs often during lag, and to the AI due to some quirks -- Syela */
 
  unit_id=punit->id;
  if (do_airline(punit, dest_x, dest_y))
    return 1;
  pdefender=get_defender(pplayer, punit, dest_x, dest_y);

  if(pdefender && pdefender->owner!=punit->owner) {
    if(can_unit_attack_tile(punit,dest_x , dest_y)) {
      if(punit->moves_left<=0)  {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 "Game: This unit has no moves left.");
        return 0;
      } else if (pplayer->ai.control && punit->activity == ACTIVITY_GOTO) {
/* DO NOT Auto-attack.  Findvictim routine will decide if we should. */
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 "Game: Aborting GOTO for AI attack procedures.");
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
            else printf("%s#%d@(%d,%d) thinks %s#%d is a passenger?\n",
               unit_name(punit->type), punit->id, punit->x, punit->y,
               unit_name(passenger->type), passenger->id);
          }
        }
	handle_unit_attack_request(pplayer, punit, pdefender);
        return 1;
      };
    } else {
      notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
		       "Game: You can't attack there.");
      return 0;
    };
  } else if (punit->ai.bodyguard > 0 && (bodyguard = unit_list_find(&map_get_tile(punit->x,
      punit->y)->units, punit->ai.bodyguard)) && !bodyguard->moves_left) {
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
       "Game: %s doesn't want to leave its bodyguard.", unit_types[punit->type].name);
  } else if (pplayer->ai.control && punit->moves_left <=
             map_move_cost(punit, dest_x, dest_y) &&
             unit_types[punit->type].move_rate >
             map_move_cost(punit, dest_x, dest_y) &&
             enemies_at(punit, dest_x, dest_y) &&
             !enemies_at(punit, punit->x, punit->y)) {
/* Mao had this problem with chariots ending turns next to enemy cities. -- Syela */
    notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
       "Game: %s ending move early to stay out of trouble.",
       unit_types[punit->type].name);
  } else if(can_unit_move_to_tile(punit, dest_x, dest_y) && try_move_unit(punit, dest_x, dest_y)) {
    int src_x, src_y;
    struct city *pcity;
    
    if((pcity=map_get_city(dest_x, dest_y))) {
      if ((pcity->owner!=punit->owner && (is_air_unit(punit) || 
					  !is_military_unit(punit)))) {
	notify_player_ex(pplayer, punit->x, punit->y, E_NOEVENT,
			 "Game: Only ground troops can take over a city.");
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
/*printf("%d disembarking from ferryboat %d\n", punit->id, punit->ai.ferryboat);*/
        ferryboat->ai.passenger = 0;
        punit->ai.ferryboat = 0;
      }
    } /* just clearing that up */

    src_x=punit->x;
    src_y=punit->y;

    unit_list_unlink(&map_get_tile(src_x, src_y)->units, punit);

    if(get_transporter_capacity(punit)) {
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

    if(get_transporter_capacity(punit)) {
      move_unit_list_to_tile(&cargolist, punit->x, punit->y);
      genlist_unlink_all(&cargolist.list); 
    }
      
    /* ok entered new tile */
    
    if(pcity)
      handle_unit_enter_city(pplayer, pcity);

    if((map_get_tile(dest_x, dest_y)->special&S_HUT))
      handle_unit_enter_hut(punit);

    wakeup_neighbor_sentries(pplayer,dest_x,dest_y);
    
    connection_do_unbuffer(pplayer->conn);

/* bodyguard code */
    if(unit_list_find(&pplayer->units, unit_id)) {
      if (punit->ai.bodyguard > 0) {
        bodyguard = unit_list_find(&(map_get_tile(src_x, src_y)->units),
                    punit->ai.bodyguard);
        if (bodyguard) {
          handle_unit_activity_request(pplayer, bodyguard, ACTIVITY_IDLE);
/* may be fortifying, have to FIX this eventually -- Syela */
/*printf("Dragging %s from (%d,%d)->(%d,%d) (Success=%d)\n",
unit_types[bodyguard->type].name, src_x, src_y, dest_x, dest_y,
handle_unit_move_request(pplayer, bodyguard, dest_x, dest_y));*/
          handle_unit_move_request(pplayer, bodyguard, dest_x, dest_y);
          handle_unit_activity_request(pplayer, bodyguard, ACTIVITY_FORTIFY);
        }
      }
    }
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
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    struct city *pcity_dest;
    
    pcity_dest=find_city_by_id(req->city_id);
    
    if(unit_flag(punit->type, F_CARAVAN) && pcity_dest &&
       unit_can_help_build_wonder(punit, pcity_dest)) {
      pcity_dest->shield_stock+=50;
      if (build_points_left(pcity_dest) < 0) {
	pcity_dest->shield_stock = improvement_value(pcity_dest->currently_building);
      }

      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		  "Game: Your %s help building the %s in %s. (%d remaining)", 
		       unit_name(punit->type),
		       get_improvement_type(pcity_dest->currently_building)->name,
		       pcity_dest->name, 
		       build_points_left(pcity_dest)
		       );

      wipe_unit(0, punit);
      send_player_info(pplayer, pplayer);
      send_city_info(pplayer, pcity_dest, 0);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_unit_establish_trade(struct player *pplayer, 
				 struct packet_unit_request *req)
{
  struct unit *punit;
  
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    
    struct city *pcity_homecity, *pcity_dest;
    
    pcity_homecity=city_list_find_id(&pplayer->cities, punit->homecity);
    pcity_dest=find_city_by_id(req->city_id);
    
    if(unit_flag(punit->type, F_CARAVAN) && pcity_homecity && pcity_dest && 
       is_tiles_adjacent(punit->x, punit->y, pcity_dest->x, pcity_dest->y) &&
       can_establish_trade_route(pcity_homecity, pcity_dest)) {
      int revenue;

      revenue=establish_trade_route(pcity_homecity, pcity_dest);
      connection_do_buffer(pplayer->conn);
      notify_player_ex(pplayer, pcity_dest->x, pcity_dest->y, E_NOEVENT,
		       "Game: Your %s has arrived in %s, and revenues amount to %d in gold.", 
		       unit_name(punit->type), pcity_dest->name, revenue);
      wipe_unit(0, punit);
      pplayer->economic.gold+=revenue;
      send_player_info(pplayer, pplayer);
      city_refresh(pcity_homecity);
      city_refresh(pcity_dest);
      send_city_info(pplayer, pcity_homecity, 0);
      send_city_info(city_owner(pcity_dest), pcity_dest, 0);
      connection_do_unbuffer(pplayer->conn);
    }
  }
}
/**************************************************************************
...
**************************************************************************/
void handle_unit_enter_city(struct player *pplayer, struct city *pcity)
{
  int i, x, y, old_id;
  int coins;
  struct city *pc2;
  struct player *cplayer;
  if(pplayer->player_no!=pcity->owner) {
    struct city *pnewcity;
    cplayer=&game.players[pcity->owner];
    pcity->size--;
    if (pcity->size<1) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		       "Game: You destroy %s completely.", pcity->name);
      notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		    "Game: %s has been destroyed by %s", 
		    pcity->name, pplayer->name);
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
		       "Game: You conquer %s, your lootings accumulate to %d gold", 
		       pcity->name, coins);
      notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		       "Game: %s conquered %s and looted %d gold from the city.",
		       pplayer->name, pcity->name, coins);
    } else {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		       "Game: You have liberated %s!! lootings accumulate to %d gold", 		       pcity->name, coins);
      
      notify_player_ex(cplayer, pcity->x, pcity->y, E_CITY_LOST, 
		       "Game: %s liberated %s and looted %d gold from the city.",		       pplayer->name, pcity->name, coins);
    }

    pnewcity=(struct city *)malloc(sizeof(struct city));
    make_partisans(pcity);
    *pnewcity=*pcity;
    remove_city(pcity);
    for (i=0;i<4;i++) {
      pc2=find_city_by_id(pnewcity->trade[i]);
      if (can_establish_trade_route(pnewcity, pc2))    
	establish_trade_route(pnewcity, pc2);
    }
    /* now set things up for the new owner */
    
    old_id = pnewcity->id;

    pnewcity->id=get_next_id_number();
    add_city_to_cache(pnewcity);
    for (i = 0; i < B_LAST; i++) {
      if (is_wonder(i) && city_got_building(pnewcity, i))
        game.global_wonders[i] = pnewcity->id;
    } /* Once I added this block, why was the below for() needed? -- Syela */
    pnewcity->owner=pplayer->player_no;

    unit_list_init(&pnewcity->units_supported);
    city_list_insert(&pplayer->cities, pnewcity);
    
    /* Transfer wonders to new owner - Kris Bubendorfer*/

    for(i = B_APOLLO;i <= B_WOMENS; i++)
      if(game.global_wonders[i] == old_id)
	game.global_wonders[i] = pnewcity->id;
    map_set_city(pnewcity->x, pnewcity->y, pnewcity);

    if ((get_invention(pplayer, A_RAILROAD)==TECH_KNOWN) &&
       (get_invention(cplayer, A_RAILROAD)!=TECH_KNOWN) &&
       (!(map_get_special(pnewcity->x,pnewcity->y)&S_RAILROAD))) {
      notify_player(pplayer, "Game: The people in %s are stunned by your technological insight!\n      Workers spontaneously gather and upgrade the city with railroads.",pnewcity->name);
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

/* maybe should auto_arr or otherwise reset ->worked? -- Syela */

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
  struct unit *punit;
  if((punit=unit_list_find(&pplayer->units, req->unit_id))) {
    punit->ai.control=1;
    send_unit_info(pplayer, punit, 0);
  }
}
 
/**************************************************************************
...
**************************************************************************/
void handle_unit_activity_request(struct player *pplayer, struct unit *punit, 
				  enum unit_activity new_activity)
{
  if(can_unit_do_activity(punit, new_activity)) {
    punit->activity=new_activity;
    punit->activity_count=0;
    send_unit_info(0, punit, 0);
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
