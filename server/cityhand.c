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
#include <unitfunc.h>
#include <civserver.h>
#include <map.h>
#include <maphand.h>
#include <mapgen.h>
#include <cityhand.h>
#include <citytools.h>
#include <cityturn.h>
#include <unit.h>
#include <city.h>
#include <player.h>
#include <tech.h>
#include <shared.h>
#include <plrhand.h>
#include <events.h>
#include <aicity.h>

/**************************************************************************
Establish a trade route, notice that there has to be space for them, 
So use can_establish_Trade_route first.
returns the revenue aswell.
**************************************************************************/
int establish_trade_route(struct city *pc1, struct city *pc2)
{
  int i;
  int tb;
  for (i=0;i<4;i++) {
    if (!pc1->trade[i]) {
      pc1->trade[i]=pc2->id;
      break;
    }
  }
  for (i=0;i<4;i++) {
    if (!pc2->trade[i]) {
      pc2->trade[i]=pc1->id;
      break;
    }
  }

  tb=(map_distance(pc1->x, pc1->y, pc2->x, pc2->y)+10);
/* should this be real_map_distance?  Leaving for now -- Syela */
  tb=(tb*(pc1->trade_prod+pc2->trade_prod))/24;
  if (map_get_continent(pc1->x, pc1->y) == map_get_continent(pc2->x, pc2->y))
    tb*=0.5;
  if (pc1->owner==pc2->owner)
    tb*=0.5;
  if (get_invention(city_owner(pc1), A_RAILROAD)==TECH_KNOWN)
    tb*=0.66;
  if (get_invention(city_owner(pc1), A_FLIGHT)==TECH_KNOWN)
    tb*=0.66;
  return tb;
}

/**************************************************************************
...
**************************************************************************/
void create_city(struct player *pplayer, int x, int y, char *name)
{
  struct city *pcity, *othercity;
  int i;
/* printf("Creating city %s\n", name);   */
  pcity=(struct city *)malloc(sizeof(struct city));

  pcity->id=get_next_id_number();
  pcity->owner=pplayer->player_no;
  pcity->x=x;
  pcity->y=y;
  strcpy(pcity->name, name);
  pcity->size=1;
  pcity->ppl_elvis=1;
  pcity->ppl_scientist=pcity->ppl_taxman=0;
  pcity->ppl_happy[4]=0;
  pcity->ppl_content[4]=1;
  pcity->ppl_unhappy[4]=0;
  pcity->was_happy=0;
  pcity->steal=0;
  for (i=0;i<4;i++)
    pcity->trade[i]=0;
  pcity->food_stock=0;
  pcity->shield_stock=0;
  pcity->trade_prod=0;
  pcity->original = pplayer->player_no;
  pcity->is_building_unit=1;
  pcity->did_buy=-1; /* code so we get a different message */
  pcity->airlift=0;
  if (can_build_unit(pcity, U_RIFLEMEN))
      pcity->currently_building=U_RIFLEMEN;
  else if (can_build_unit(pcity, U_MUSKETEERS))
      pcity->currently_building=U_MUSKETEERS;
  else if (can_build_unit(pcity, U_PIKEMEN))
      pcity->currently_building=U_PIKEMEN;
  else if (can_build_unit(pcity, U_PHALANX))
      pcity->currently_building=U_PHALANX;
  else
      pcity->currently_building=U_WARRIORS;
  for (y = 0; y < CITY_MAP_SIZE; y++)
    for (x = 0; x < CITY_MAP_SIZE; x++)
      pcity->city_map[x][y]=C_TILE_EMPTY;

  for(i=0; i<B_LAST; i++)
    pcity->improvements[i]=0;
  if(!pplayer->capital) {
    pplayer->capital=1;
    pcity->improvements[B_PALACE]=1;
  }
  pcity->anarchy=0;
  pcity->ai.ai_role = AICITY_NONE;
  pcity->ai.trade_want = 8; /* default value */
  memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
  pcity->ai.workremain = 1; /* there's always work to be done! */
  pcity->ai.danger = -1; /* flag, may come in handy later */
  pcity->corruption = 0;
  pcity->shield_bonus = 100;
  pcity->tax_bonus = 100;
  pcity->science_bonus = 100;
  map_set_city(pcity->x, pcity->y, pcity);
  
  unit_list_init(&pcity->units_supported);
  city_list_insert(&pplayer->cities, pcity);
  add_city_to_minimap(pcity->x, pcity->y);

/* it is possible to build a city on a tile that is already worked */
/* this will displace the worker on the newly-built city's tile -- Syela */

  city_map_iterate(x, y) { /* usurping the parameters x, y */
    othercity = map_get_city(pcity->x+x-2, pcity->y+y-2);
    if (othercity && othercity != pcity) {
      if (get_worker_city(othercity, 4 - x, 4 - y) == C_TILE_WORKER) {
        set_worker_city(othercity, 4 - x, 4 - y, C_TILE_UNAVAILABLE);
        add_adjust_workers(othercity); /* will place the displaced */
        city_refresh(othercity); /* may be unnecessary; can't hurt */
      } else set_worker_city(othercity, 4 - x, 4 - y, C_TILE_UNAVAILABLE);
      send_city_info(city_owner(othercity), othercity, 1);
    }
  }
 
  city_check_workers(pplayer, pcity);
  auto_arrange_workers(pcity); /* forces a worker onto (2,2), thus the above */

  city_refresh(pcity);
  city_incite_cost(pcity);
  send_city_info(0, pcity, 0);
/* fnord -- Syela */
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change_specialist(struct player *pplayer, 
				   struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);
  if(!pcity) 
    return;
  if(!player_owns_city(pplayer, pcity))  
    return;
  if(preq->specialist_from==SP_ELVIS) {
    if(pcity->size<5) 
      return; 

    if(!pcity->ppl_elvis)
      return;
    pcity->ppl_elvis--;
  } else if(preq->specialist_from==SP_TAXMAN) {
    if (!pcity->ppl_taxman)
      return;
    pcity->ppl_taxman--;
  } else if (preq->specialist_from==SP_SCIENTIST) {
    if (!pcity->ppl_scientist)
      return;
    pcity->ppl_scientist--;
  } else {
    return;
  }
  switch (preq->specialist_to) {
  case SP_ELVIS:
    pcity->ppl_elvis++;
    break;
  case SP_TAXMAN:
    pcity->ppl_taxman++;
    break;
  case SP_SCIENTIST:
    pcity->ppl_scientist++;
    break;
  default:
    pcity->ppl_elvis++;
    break;
  }

  city_refresh(pcity);
  send_city_info(pplayer, pcity, 0);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_specialist(struct player *pplayer, 
				 struct packet_city_request *preq)
{
  struct city *pcity;

  pcity=find_city_by_id(preq->city_id);
  if(!pcity) 
    return;
  if (!player_owns_city(pplayer, pcity))  return;
  if (preq->worker_x==2 && preq->worker_y==2) {
    auto_arrange_workers(pcity);
    return;
  }
  if (is_worker_here(pcity, preq->worker_x, preq->worker_y) == C_TILE_WORKER) {
    set_worker_city(pcity, preq->worker_x, preq->worker_y, C_TILE_EMPTY);
    pcity->ppl_elvis++;
    city_refresh(pcity);
    send_city_info(pplayer, pcity, 0);
  } else {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, "Game: you don't have a worker here"); 
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_worker(struct player *pplayer, 
			     struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);

  if (preq->worker_x < 0 || preq->worker_x > 4)
    return;
  if (preq->worker_y < 0 || preq->worker_y > 4)
    return;
  
  if(!pcity) 
    return;
  
  if(!player_owns_city(pplayer, pcity))
    return;

  if(preq->worker_x==2 && preq->worker_y==2) {
    auto_arrange_workers(pcity);
  }
  if (!city_specialists(pcity) || 
      !can_place_worker_here(pcity, preq->worker_x, preq->worker_y))
    return;

  set_worker_city(pcity, preq->worker_x, preq->worker_y, C_TILE_WORKER);

  if(pcity->ppl_elvis) 
    pcity->ppl_elvis--;
  else if(pcity->ppl_scientist) 
    pcity->ppl_scientist--;
  else 
    pcity->ppl_taxman--;
  
  city_refresh(pcity);
  send_city_info(pplayer, pcity, 1);
}

/**************************************************************************
...
**************************************************************************/
void do_sell_building(struct player *pplayer, struct city *pcity, int id)
{
  if (!is_wonder(id)) {
    pcity->improvements[id]=0;
    pplayer->economic.gold += improvement_value(id);
  }
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_sell(struct player *pplayer, struct city *pcity, int id)
{  
  if (pcity->did_sell) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		  "Game: You have already sold something here this turn.");
    return;
  }

  if (!can_sell_building(pcity, id))
    return;

  pcity->did_sell=1;
  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		   "Game: You sell %s for %d credits.", 
		   get_improvement_name(id), 
		   improvement_value(id));
  do_sell_building(pplayer, pcity, id);

  city_refresh(pcity);
  send_city_info(pplayer, pcity, 1);
  send_player_info(pplayer, pplayer);
}

void handle_city_sell(struct player *pplayer, struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);
  if (!pcity || !player_owns_city(pplayer, pcity) 
      || preq->build_id>=B_LAST) 
    return;
  really_handle_city_sell(pplayer, pcity, preq->build_id);
}

void really_handle_city_buy(struct player *pplayer, struct city *pcity)
{
  char *name;
  int cost, total;
  if (!pcity || !player_owns_city(pplayer, pcity)) return;
 
  if (pcity->did_buy > 0) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		  "Game: You have already bought this turn.");
    return;
  }

  if (pcity->did_buy < 0) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		  "Game: Cannot buy in city created this turn.");
    return;
  }

  if (!pcity->is_building_unit) {
    total=improvement_value(pcity->currently_building);
    name=get_improvement_name(pcity->currently_building);
    
  } else {
    name=unit_types[pcity->currently_building].name;
    total=unit_value(pcity->currently_building);
    if (pcity->anarchy) {
      notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		    "Game: Can't buy units when city is in disorder.");
      return;
    }
  }
  cost=city_buy_cost(pcity);
   if (cost>pplayer->economic.gold)
    return;
  pcity->did_buy=1;
  pplayer->economic.gold-=cost;
  if (pcity->shield_stock < total) pcity->shield_stock=total; /* AI wants this -- Syela */
  notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		   "Game: %s bought for %d", name, cost); 
  
  city_refresh(pcity);
  send_city_info(pplayer, pcity, 1);
  send_player_info(pplayer,pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_buy(struct player *pplayer, struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);
  really_handle_city_buy(pplayer, pcity);
}

void handle_city_refresh(struct player *pplayer, struct packet_generic_integer *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->value);
  if (pcity) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity, 1);
  } else if (!preq->value) global_city_refresh(pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change(struct player *pplayer, 
			struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);
  if (!pcity) {
    printf("Pcity null in handle_city_change (%s, id = %d)!\n", pplayer->name, preq->city_id);
    return;
  }
   if(!player_owns_city(pplayer, pcity))
    return;
   if (preq->is_build_id_unit_id && !can_build_unit(pcity, preq->build_id))
     return;
   if (!preq->is_build_id_unit_id && !can_build_improvement(pcity, preq->build_id))
     return;
  if (pcity->did_buy && pcity->shield_stock) { /* did_buy > 0 should be same -- Syela */
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		     "Game: You have bought this turn, can't change.");
    return;
  }

   if(!pcity->is_building_unit && is_wonder(pcity->currently_building)) {
     notify_player_ex(0, pcity->x, pcity->y, E_NOEVENT,
		   "Game: The %s have stopped building The %s in %s.",
		   get_race_name_plural(pplayer->race),
		   get_imp_name_ex(pcity, pcity->currently_building),
		   pcity->name);
   }
  
  if(preq->is_build_id_unit_id) {
    if (!pcity->is_building_unit)
      pcity->shield_stock/=2;
      pcity->currently_building=preq->build_id;
      pcity->is_building_unit=1;
  }
  else {
    if (pcity->is_building_unit ||(is_wonder(pcity->currently_building)!=is_wonder(preq->build_id)))
      pcity->shield_stock/=2;
    
    pcity->currently_building=preq->build_id;
    pcity->is_building_unit=0;
    
    if(is_wonder(preq->build_id)) {
      notify_player_ex(0, pcity->x, pcity->y, E_NOEVENT,
		       "Game: The %s have started building The %s.",
		       get_race_name_plural(pplayer->race),
		       get_imp_name_ex(pcity, pcity->currently_building));
    }
  }
  
  city_refresh(pcity);
  send_city_info(pplayer, pcity, 1);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_rename(struct player *pplayer, 
			struct packet_city_request *preq)
{
  char *cp;
  struct city *pcity;
  
  pcity=find_city_by_id(preq->city_id);

  if(!player_owns_city(pplayer, pcity))
    return;

  if((cp=get_sane_name(preq->name))) {
    /* more sanity tests! any existing city with that name? */
    strcpy(pcity->name, cp);
    city_refresh(pcity);
    send_city_info(pplayer, pcity, 1);
  }
  else
    notify_player(pplayer, "Game: %s is not a valid name.", preq->name);
}

/**************************************************************************
...
**************************************************************************/
void send_player_cities(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity, 0);
  }
  city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void send_city_info(struct player *dest, struct city *pcity, int dosend)
{
  int i, o, x, y;
  char *p;
  struct packet_city_info packet;
  if (nocity_send && dest) 
    return;
  packet.id=pcity->id;
  packet.owner=pcity->owner;
  packet.x=pcity->x;
  packet.y=pcity->y;
  strcpy(packet.name, pcity->name);

  packet.size=pcity->size;
  packet.ppl_happy=pcity->ppl_happy[4];
  packet.ppl_content=pcity->ppl_content[4];
  packet.ppl_unhappy=pcity->ppl_unhappy[4];
  packet.ppl_elvis=pcity->ppl_elvis;
  packet.ppl_scientist=pcity->ppl_scientist;
  packet.ppl_taxman=pcity->ppl_taxman;
  for (i=0;i<4;i++)
    packet.trade[i]=pcity->trade[i];

  packet.food_prod=pcity->food_prod;
  packet.food_surplus=pcity->food_surplus;
  packet.shield_prod=pcity->shield_prod;
  packet.shield_surplus=pcity->shield_surplus;
  packet.trade_prod=pcity->trade_prod;
  packet.corruption=pcity->corruption;
  
  packet.luxury_total=pcity->luxury_total;
  packet.tax_total=pcity->tax_total;
  packet.science_total=pcity->science_total;
  
  packet.food_stock=pcity->food_stock;
  packet.shield_stock=pcity->shield_stock;
  packet.pollution=pcity->pollution;
  packet.incite_revolt_cost=pcity->incite_revolt_cost;
  
  packet.is_building_unit=pcity->is_building_unit;
  packet.currently_building=pcity->currently_building;
  packet.airlift=pcity->airlift;
  packet.did_buy=pcity->did_buy;
  packet.did_sell=pcity->did_sell;
  packet.was_happy=pcity->was_happy;
  p=packet.city_map;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++)
      *p++=get_worker_city(pcity, x, y)+'0';
  *p='\0';

  p=packet.improvements;
  for(i=0; i<B_LAST; i++)
    *p++=(pcity->improvements[i]) ? '1' : '0';
  *p='\0';
  
  for(o=0; o<game.nplayers; o++) {           /* dests */
    if(!dest || &game.players[o]==dest) {
      if(nocity_send && &game.players[o]==dest) continue;
      if(dosend || map_get_known(pcity->x, pcity->y, &game.players[o])) {
	send_packet_city_info(game.players[o].conn, &packet);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_trade_route(int c1, int c2) 
{
  int i;
  struct city *pc1, *pc2;
  
  pc1=find_city_by_id(c1);
  pc2=find_city_by_id(c2);
  if (pc1) {
    for (i=0;i<4;i++)
      if (pc1->trade[i]==c2)
	pc1->trade[i]=0;
  }
  if (pc2) {
    for (i=0;i<4;i++)
      if (pc2->trade[i]==c2)
	pc2->trade[i]=0;
  }
}

/**************************************************************************
...
**************************************************************************/
void remove_city(struct city *pcity)
{
  int o, x, y;
  struct packet_generic_integer packet;
  for (o=0; o<4; o++)
    remove_trade_route(pcity->trade[0], pcity->id);
  packet.value=pcity->id;
  for(o=0; o<game.nplayers; o++)           /* dests */
    send_packet_generic_integer(game.players[o].conn,
				PACKET_REMOVE_CITY,&packet);
  unit_list_iterate(pcity->units_supported, punit) 
    wipe_unit(0, punit);
  unit_list_iterate_end;
  city_map_iterate(x,y) {
    set_worker_city(pcity, x, y, C_TILE_EMPTY);
  }
  remove_city_from_minimap(x, y);
  game_remove_city(pcity->id);
}
