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

#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "unit.h"

#include "cityhand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diptreaty.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unittools.h"

#include "diplhand.h"

struct genlist treaties;
int did_init_treaties;


/**************************************************************************
...
**************************************************************************/
static struct Treaty *find_treaty(struct player *plr0, struct player *plr1)
{
  struct genlist_iterator myiter;
  
  if(!did_init_treaties) {
    genlist_init(&treaties);
    did_init_treaties=1;
  }

  genlist_iterator_init(&myiter, &treaties, 0);
  
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Treaty *ptreaty=(struct Treaty *)ITERATOR_PTR(myiter);
    if((ptreaty->plr0==plr0 && ptreaty->plr1==plr1) ||
       (ptreaty->plr0==plr1 && ptreaty->plr1==plr0))
      return ptreaty;
  }
  return 0;
}


/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_accept_treaty(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver, *pdest;
  
  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  pgiver=&game.players[packet->plrno_from];
  
  if((ptreaty=find_treaty(plr0, plr1)) && pgiver==pplayer) {
    if(ptreaty->plr0==pgiver)
      ptreaty->accept0=!ptreaty->accept0;
    else
      ptreaty->accept1=!ptreaty->accept1;
      
    send_packet_diplomacy_info(plr0->conn, 
			       PACKET_DIPLOMACY_ACCEPT_TREATY, 
			       packet);
    send_packet_diplomacy_info(plr1->conn, 
			       PACKET_DIPLOMACY_ACCEPT_TREATY, 
			       packet);
    
    if(ptreaty->accept0 && ptreaty->accept1) {
      struct genlist_iterator myiter;
      
      send_packet_diplomacy_info(plr0->conn,
				 PACKET_DIPLOMACY_CANCEL_MEETING, 
				 packet);
      send_packet_diplomacy_info(plr1->conn, 
				 PACKET_DIPLOMACY_CANCEL_MEETING, 
				 packet);
      
      
      notify_player(plr0, "Game: A treaty containing %d clauses was agreed upon",
		    genlist_size(&ptreaty->clauses));
      notify_player(plr1, "Game: A treaty containing %d clauses was agreed upon",
		    genlist_size(&ptreaty->clauses));
      gamelog(GAMELOG_TREATY, "%s and %s agree to a treaty",
	      get_race_name_plural(plr0->race),
	      get_race_name_plural(plr1->race));
      
      
      /* verify gold! the player's gold amount could have changed during
       * the meeting */
      genlist_iterator_init(&myiter, &ptreaty->clauses, 0);
      for(;ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
	struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
	pgiver=pclause->from;

	if(pclause->type==CLAUSE_GOLD && pgiver->economic.gold<pclause->value) {
	  
	  notify_player(plr0, "Game: The %s don't have the promised amount of gold! Treaty canceled!",
			get_race_name_plural(pgiver->race));
	  notify_player(plr1, "Game: The %s don't have the promised amount of gold! Treaty canceled!",
			get_race_name_plural(pgiver->race));
	  goto cleanup;
	}
      }
      
      genlist_iterator_init(&myiter, &ptreaty->clauses, 0);
      
      for(;ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
	struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);

	pgiver=pclause->from;
	pdest=(plr0==pgiver) ? plr1 : plr0;
	
	switch(pclause->type) {
	 case CLAUSE_ADVANCE:
	  notify_player(pdest, "Game: You are taught the knowledge of %s",
			advances[pclause->value].name);
	  if (tech_flag(pclause->value,TF_RAILROAD)) {
	    upgrade_city_rails(pdest, 0);
	  }
	  gamelog(GAMELOG_TECH, "%s acquire %s (Treaty) from %s",
                  get_race_name_plural(pdest->race),
                  advances[pclause->value].name,
                  get_race_name_plural(pgiver->race));
	  set_invention(pdest, pclause->value, TECH_KNOWN);
	  update_research(pdest);
	  do_dipl_cost(pdest);
	  pdest->research.researchpoints++;
	  break;
	 case CLAUSE_GOLD:
	  notify_player(pdest, "Game: You get %d gold", pclause->value);
	  pgiver->economic.gold-=pclause->value;
	  pdest->economic.gold+=pclause->value;
	  break;
	 case CLAUSE_MAP:
	  give_map_from_player_to_player(pgiver, pdest);
	  notify_player(pdest, "Game: You receive %s's worldmap",
			pgiver->name);
	  break;
	 case CLAUSE_SEAMAP:
	  give_seamap_from_player_to_player(pgiver, pdest);
	  notify_player(pdest, "Game: You receive %s's seamap",
			pgiver->name);
	  break;
	case CLAUSE_CITY:{
	  struct city *pcity = find_city_by_id(pclause->value);
	  struct city *pnewcity = NULL;

	  if (!pcity) {
	    freelog(LOG_NORMAL, "Treaty city id %d not found - skipping clause.",
		    pclause->value);
	    break;
	  }
	  
	  notify_player(pdest, "Game: You receive city of %s from %s",
			pcity->name, pgiver->name);
	  
	  notify_player(pgiver, "Game: You give city of %s to %s",
			pcity->name, pdest->name);
	  
	  if(!(pnewcity = transfer_city(pdest, pgiver, pcity))){
	    freelog(LOG_NORMAL, "Transfer city returned no city - skipping clause.");
	    break;
	  }
	  map_set_city(pnewcity->x, pnewcity->y, pnewcity);   
	  transfer_city_units(pdest, pgiver, pnewcity, pcity, 0, 1);
	  remove_city(pcity); /* don't forget this! */
	  map_set_city(pnewcity->x, pnewcity->y, pnewcity);
	  
	  reestablish_city_trade_routes(pnewcity); 
	  city_check_workers(pdest ,pnewcity);
	  update_map_with_city_workers(pnewcity);
	  city_refresh(pnewcity);
	  initialize_infrastructure_cache(pnewcity);
	  send_city_info(0, pnewcity, 0);
	  
	  unit_list_iterate(pdest->units, punit) 
	    resolve_unit_stack(punit->x, punit->y, 1);
	  unit_list_iterate_end;

	  break;
	}
	}
	
      }
cleanup:      
      genlist_unlink(&treaties, ptreaty);
      free(ptreaty);
      send_player_info(plr0, 0);
      send_player_info(plr1, 0);
    }
  }
  
}


/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_remove_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver;
  
  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  pgiver=&game.players[packet->plrno_from];
  
  if((ptreaty=find_treaty(plr0, plr1))) {
    if(remove_clause(ptreaty, pgiver, packet->clause_type, packet->value)) {
      send_packet_diplomacy_info(plr0->conn, 
				 PACKET_DIPLOMACY_REMOVE_CLAUSE, packet);
      send_packet_diplomacy_info(plr1->conn, 
				 PACKET_DIPLOMACY_REMOVE_CLAUSE, packet);
    }
  }

}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_create_clause(struct player *pplayer, 
				    struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *pgiver;
  
  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  pgiver=&game.players[packet->plrno_from];

  /* 
   * If we are trading cities, then it is possible that the
   * dest is unaware of it's existence.  We have 2 choices,
   * forbid it, or lighten that area.  If we assume that
   * the giver knows what they are doing, then 2. is the
   * most powerful option - I'll choose that for now.
   *                           - Kris Bubendorfer
   */

  if(packet->clause_type == CLAUSE_CITY){
    struct city *pcity = find_city_by_id(packet->value);
    if(pcity && !map_get_known(pcity->x, pcity->y, plr1))
      lighten_area(plr1, pcity->x, pcity->y);
  }
  
  if((ptreaty=find_treaty(plr0, plr1))) {
    if(add_clause(ptreaty, pgiver, packet->clause_type, packet->value)) {
      send_packet_diplomacy_info(plr0->conn, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
      send_packet_diplomacy_info(plr1->conn, 
				 PACKET_DIPLOMACY_CREATE_CLAUSE, 
				 packet);
    }
  }
}


/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_cancel_meeting(struct player *pplayer, 
				     struct packet_diplomacy_info *packet)
{
  struct Treaty *ptreaty;
  struct player *plr0, *plr1, *theother;
  
  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  
  theother=(pplayer==plr0) ? plr1 : plr0;
  
  if((ptreaty=find_treaty(pplayer, theother))) {
    send_packet_diplomacy_info(theother->conn, 
			       PACKET_DIPLOMACY_CANCEL_MEETING, 
			       packet);
    notify_player(theother, "Game: %s canceled the meeting!", 
		  pplayer->name);
    genlist_unlink(&treaties, ptreaty);
    free(ptreaty);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_diplomacy_init(struct player *pplayer, 
			   struct packet_diplomacy_info *packet)
{
  struct packet_diplomacy_info pa;
  struct player *plr0, *plr1;
  
  plr0=&game.players[packet->plrno0];
  plr1=&game.players[packet->plrno1];
  
  if(!find_treaty(plr0, plr1)) {
    if(player_has_embassy(plr0, plr1) && plr0->is_connected && 
       plr0->is_alive && plr1->is_connected && plr1->is_alive) {
    
    
    struct Treaty *ptreaty;
    
    ptreaty=fc_malloc(sizeof(struct Treaty));
    init_treaty(ptreaty, plr0, plr1);
    genlist_insert(&treaties, ptreaty, 0);
    
    pa.plrno0=plr0->player_no;
    pa.plrno1=plr1->player_no;
    send_packet_diplomacy_info(plr0->conn, PACKET_DIPLOMACY_INIT_MEETING, &pa);

    pa.plrno0=plr1->player_no;
    pa.plrno1=plr0->player_no;
    send_packet_diplomacy_info(plr1->conn, PACKET_DIPLOMACY_INIT_MEETING, &pa);
    }
    
  }

}

