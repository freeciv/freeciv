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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "events.h"
#include "fcintl.h"
#include "idex.h"
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "unit.h"
#include "worklist.h"

#include "citytools.h"
#include "cityturn.h"
#include "plrhand.h"

#include "cityhand.h"

/**************************************************************************
  Send city_name_suggestion packet back to requesting conn, with
  suggested name and with same id which was passed in (either unit id
  for city builder or existing city id for rename, we don't care here).
**************************************************************************/
void handle_city_name_suggest_req(struct connection *pconn,
				  struct packet_generic_integer *packet)
{
  struct packet_city_name_suggestion reply;
  if (!pconn->player) {
    freelog(LOG_ERROR, "City-name suggestion request from non-player %s",
	    conn_description(pconn));
    return;
  }
  reply.id = packet->value;
  sz_strlcpy(reply.name, city_name_suggestion(pconn->player));
  send_packet_city_name_suggestion(pconn, &reply);
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
  case SP_TAXMAN:
    pcity->ppl_taxman++;
    break;
  case SP_SCIENTIST:
    pcity->ppl_scientist++;
    break;
  case SP_ELVIS:
  default:
    pcity->ppl_elvis++;
    break;
  }

  city_refresh(pcity);
  send_city_info(pplayer, pcity);
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
    send_adjacent_cities(pcity);
    return;
  }
  if (is_worker_here(pcity, preq->worker_x, preq->worker_y) == C_TILE_WORKER) {
    set_worker_city(pcity, preq->worker_x, preq->worker_y, C_TILE_EMPTY);
    pcity->ppl_elvis++;
    city_refresh(pcity);
    send_adjacent_cities(pcity);
    send_city_info(pplayer, pcity);
  } else {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		     _("Game: you don't have a worker here.")); 
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
      send_adjacent_cities(pcity);
      return;
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
  send_adjacent_cities(pcity);
  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_sell(struct player *pplayer, struct city *pcity, int id)
{  
  if (pcity->did_sell) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		  _("Game: You have already sold something here this turn."));
    return;
  }

  if (!can_sell_building(pcity, id))
    return;

  pcity->did_sell=1;
  notify_player_ex(pplayer, pcity->x, pcity->y, E_IMP_SOLD,
		   _("Game: You sell %s in %s for %d gold."), 
		   get_improvement_name(id), pcity->name,
		   improvement_value(id));
  do_sell_building(pplayer, pcity, id);

  city_refresh(pcity);
  send_city_info(0, pcity); /* If we sold the walls the other players should see it */
  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_sell(struct player *pplayer, struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);
  if (!pcity || !player_owns_city(pplayer, pcity) 
      || preq->build_id>=game.num_impr_types) 
    return;
  really_handle_city_sell(pplayer, pcity, preq->build_id);
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_buy(struct player *pplayer, struct city *pcity)
{
  char *name;
  int cost, total;
  if (!pcity || !player_owns_city(pplayer, pcity)) return;
 
  if (pcity->did_buy > 0) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		  _("Game: You have already bought this turn."));
    return;
  }

  if (pcity->did_buy < 0) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
		  _("Game: Cannot buy in city created this turn."));
    return;
  }

  if (!pcity->is_building_unit && pcity->currently_building==B_CAPITAL)  {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT,
                     _("Game: You don't buy %s!"),
		     improvement_types[B_CAPITAL].name);
    return;
  }

  if (pcity->is_building_unit && pcity->anarchy) {
    notify_player_ex(pplayer, pcity->x, pcity->y, E_NOEVENT, 
		     _("Game: Can't buy units when city is in disorder."));
    return;
  }

  if (pcity->is_building_unit) {
    name=unit_types[pcity->currently_building].name;
    total=unit_value(pcity->currently_building);
  } else {
    name=get_improvement_name(pcity->currently_building);
    total=improvement_value(pcity->currently_building);
  }
  cost=city_buy_cost(pcity);
  if ((!cost) || (cost>pplayer->economic.gold))
   return;

  /*
   * Need to make this more restrictive.  AI is sometimes buying
   * things that force it to sell buildings due to upkeep problems.
   * upkeep expense is only known in ai_manage_taxes().
   * Also, we should sort this list so cheapest things are bought first,
   * and/or take danger into account.
   * AJS, 1999110
   */

  pplayer->economic.gold-=cost;
  if (pcity->shield_stock < total){
    /* As we never put penalty on disbanded_shields, we can
     * fully well add the missing shields there. */
    pcity->disbanded_shields += total - pcity->shield_stock;
    pcity->shield_stock=total; /* AI wants this -- Syela */
    pcity->did_buy=1; /* !PS: no need to set buy flag otherwise */
  }
  city_refresh(pcity);
  
  conn_list_do_buffer(&pplayer->connections);
  notify_player_ex(pplayer, pcity->x, pcity->y, 
                   pcity->is_building_unit?E_UNIT_BUY:E_IMP_BUY,
		   _("Game: %s bought in %s for %d gold."), 
		   name, pcity->name, cost);
  send_city_info(pplayer, pcity);
  send_player_info(pplayer,pplayer);
  conn_list_do_unbuffer(&pplayer->connections);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_worklist(struct player *pplayer, struct packet_city_request *preq)
{
  struct city *pcity;
  pcity=find_city_by_id(preq->city_id);

  if (!pcity) 
    return;
  if (!player_owns_city(pplayer, pcity))  
    return;

  copy_worklist(pcity->worklist, &preq->worklist);

  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_buy(struct player *pplayer, struct packet_city_request *preq)
{
  struct city *pcity = find_city_by_id(preq->city_id);

  if (!pcity) 
    return;
  if (!player_owns_city(pplayer, pcity))  
    return;

  really_handle_city_buy(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_refresh(struct player *pplayer, struct packet_generic_integer *preq)
{
  if (preq->value) {
    struct city *pcity = find_city_by_id(preq->value);

    if (!pcity) 
      return;
    if (!player_owns_city(pplayer, pcity))  
      return;

    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  } else {
    global_city_refresh(pplayer);
  }
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
    freelog(LOG_ERROR, "Pcity null in handle_city_change"
	                " (%s, id = %d)!", pplayer->name, preq->city_id);
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
		     _("Game: You have bought this turn, can't change."));
    return;
  }

  change_build_target(pplayer, pcity, preq->build_id,
		      preq->is_build_id_unit_id, E_NOEVENT);

  city_refresh(pcity);
  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_rename(struct player *pplayer, 
			struct packet_city_request *preq)
{
  char *cp;
  struct city *pcity;
  
  pcity = find_city_by_id(preq->city_id);
  if (!pcity)
    return;
  if (!player_owns_city(pplayer, pcity))
    return;

  if((cp=get_sane_name(preq->name))) {
    /* more sanity tests! any existing city with that name? */
    sz_strlcpy(pcity->name, cp);
    city_refresh(pcity);
    send_city_info(0, pcity);
  } else {
    notify_player(pplayer, _("Game: %s is not a valid name."), preq->name);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_options(struct player *pplayer,
				struct packet_generic_values *preq)
{
  struct city *pcity = find_city_by_id(preq->value1);
  if (!pcity || pcity->owner != pplayer->player_no) return;
  pcity->city_options = preq->value2;
  /* We don't need to send the full city info, since no other properties
   * depend on the attack options. --dwp
   * Otherwise could do:
   *   send_city_info(pplayer, pcity);
   */
  lsend_packet_generic_values(&pplayer->connections, PACKET_CITY_OPTIONS, preq);
}
