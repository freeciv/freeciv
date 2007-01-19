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
#include "sanitycheck.h"

#include "cityhand.h"

/**************************************************************************
  Send city_name_suggestion packet back to requesting conn, with
  suggested name and with same id which was passed in (either unit id
  for city builder or existing city id for rename, we don't care here).
**************************************************************************/
void handle_city_name_suggestion_req(struct player *pplayer, int value)
{
  struct unit *punit = player_find_unit_by_id(pplayer, value);
  
  if (!punit) {
    return;
  }

  freelog(LOG_VERBOSE, "handle_city_name_suggest_req(unit_pos=(%d,%d))",
	  punit->tile->x, punit->tile->y);

  dlsend_packet_city_name_suggestion_info(&pplayer->connections, value, 
      city_name_suggestion(pplayer, punit->tile));
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change_specialist(struct player *pplayer, int city_id,
				   Specialist_type_id from,
				   Specialist_type_id to)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }

  if (to < 0 || to >= SP_COUNT
      || from < 0 || from >= SP_COUNT
      || !city_can_use_specialist(pcity, to)
      || pcity->specialists[from] == 0) {
    freelog(LOG_ERROR, "Error in specialist change request from client.");
    return;
  }

  pcity->specialists[from]--;
  pcity->specialists[to]++;

  sanity_check_city(pcity);
  city_refresh(pcity);
  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_specialist(struct player *pplayer, int city_id,
				 int worker_x, int worker_y)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }
  if (is_city_center(worker_x, worker_y)) {
    auto_arrange_workers(pcity);
    sync_cities();
    return;
  }
  if (is_worker_here(pcity, worker_x, worker_y)) {
    server_remove_worker_city(pcity, worker_x, worker_y);
    pcity->specialists[SP_ELVIS]++;
    city_refresh(pcity);
    sync_cities();
  } else {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		     _("Game: You don't have a worker here.")); 
  }
  sanity_check_city(pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_make_worker(struct player *pplayer, int city_id,
			     int worker_x, int worker_y)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);
  int i;

  if (!is_valid_city_coords(worker_x, worker_y)) {
    freelog(LOG_ERROR, "invalid city coords %d,%d in package",
	    worker_x, worker_y);
    return;
  }
  
  if (!pcity) {
    return;
  }

  if (is_city_center(worker_x, worker_y)) {
    auto_arrange_workers(pcity);
    sync_cities();
    return;
  }

  if (city_specialists(pcity) == 0
      || get_worker_city(pcity, worker_x, worker_y) != C_TILE_EMPTY)
    return;

  server_set_worker_city(pcity, worker_x, worker_y);

  for (i = 0; i < SP_COUNT; i++) {
    if (pcity->specialists[i] > 0) {
      pcity->specialists[i]--;
      break;
    }
  }
  assert(i < SP_COUNT);

  sanity_check_city(pcity);
  city_refresh(pcity);
  sync_cities();
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_sell(struct player *pplayer, struct city *pcity,
			     Impr_Type_id id)
{  
  if (pcity->did_sell) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT, 
		  _("Game: You have already sold something here this turn."));
    return;
  }

  if (!can_sell_building(pcity, id))
    return;

  pcity->did_sell=TRUE;
  notify_player_ex(pplayer, pcity->tile, E_IMP_SOLD,
		   _("Game: You sell %s in %s for %d gold."), 
		   get_improvement_name(id), pcity->name,
		   impr_sell_gold(id));
  do_sell_building(pplayer, pcity, id);

  city_refresh(pcity);

  /* If we sold the walls the other players should see it */
  send_city_info(NULL, pcity);
  send_player_info(pplayer, pplayer);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_sell(struct player *pplayer, int city_id, int build_id)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity || build_id >= game.num_impr_types) {
    return;
  }
  really_handle_city_sell(pplayer, pcity, build_id);
}

/**************************************************************************
...
**************************************************************************/
void really_handle_city_buy(struct player *pplayer, struct city *pcity)
{
  const char *name;
  int cost, total;

  assert(pcity && player_owns_city(pplayer, pcity));
 
  if (pcity->turn_founded == game.turn) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		  _("Game: Cannot buy in city created this turn."));
    return;
  }

  if (!city_can_change_build(pcity)) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		  _("Game: You have already bought this turn."));
    return;
  }

  if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
    assert(!pcity->is_building_unit);
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
                     _("Game: You don't buy %s!"),
		     improvement_types[pcity->currently_building].name);
    return;
  }

  if (pcity->is_building_unit && pcity->anarchy != 0) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT, 
		     _("Game: Can't buy units when city is in disorder."));
    return;
  }

  if (pcity->is_building_unit) {
    name = unit_types[pcity->currently_building].name;
    total = unit_build_shield_cost(pcity->currently_building);
  } else {
    name = get_improvement_name(pcity->currently_building);
    total = impr_build_shield_cost(pcity->currently_building);
  }
  cost = city_buy_cost(pcity);
  if (cost == 0 || cost > pplayer->economic.gold) {
    return;
  }

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
    pcity->did_buy = TRUE;	/* !PS: no need to set buy flag otherwise */
  }
  city_refresh(pcity);
  
  conn_list_do_buffer(&pplayer->connections);
  notify_player_ex(pplayer, pcity->tile, 
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
void handle_city_worklist(struct player *pplayer, int city_id,
			  struct worklist *worklist)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }

  copy_worklist(&pcity->worklist, worklist);

  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_buy(struct player *pplayer, int city_id)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }

  really_handle_city_buy(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_refresh(struct player *pplayer, int city_id)
{
  if (city_id != 0) {
    struct city *pcity = player_find_city_by_id(pplayer, city_id);

    if (!pcity) {
      return;
    }

    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  } else {
    global_city_refresh(pplayer);
  }
}

/**************************************************************************
...
**************************************************************************/
void handle_city_change(struct player *pplayer, int city_id, int build_id,
			bool is_build_id_unit_id)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }

  if (pcity->is_building_unit == is_build_id_unit_id
      && pcity->currently_building == build_id) {
    /* The client probably shouldn't send such a packet. */
    return;
  }

   if (is_build_id_unit_id && !can_build_unit(pcity, build_id))
     return;
   if (!is_build_id_unit_id && !can_build_improvement(pcity, build_id))
     return;
  if (pcity->did_buy && pcity->shield_stock > 0) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		     _("Game: You have bought this turn, can't change."));
    return;
  }

  change_build_target(pplayer, pcity, build_id, is_build_id_unit_id,
		      E_NOEVENT);

  sanity_check_city(pcity);
  city_refresh(pcity);
  send_city_info(pplayer, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_rename(struct player *pplayer, int city_id, char *name)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);
  char message[1024];

  if (!pcity) {
    return;
  }

  if (!is_allowed_city_name(pplayer, name, message, sizeof(message))) {
    notify_player_ex(pplayer, pcity->tile, E_NOEVENT,
		     _("Game: %s"),  message);
    return;
  }

  sz_strlcpy(pcity->name, name);
  city_refresh(pcity);
  send_city_info(NULL, pcity);
}

/**************************************************************************
...
**************************************************************************/
void handle_city_options_req(struct player *pplayer, int city_id, int value)
{
  struct city *pcity = player_find_city_by_id(pplayer, city_id);

  if (!pcity) {
    return;
  }

  pcity->city_options = value;
  send_city_info(pplayer, pcity);
}

/***************************************************************
  Tell the client the cost of inciting a revolt or bribing a unit.
  Only send result back to the requesting connection, not all
  connections for that player.
***************************************************************/
void handle_city_incite_inq(struct connection *pc, int city_id)
{
  struct player *pplayer = pc->player;
  struct city *pcity = find_city_by_id(city_id);

  if (pplayer && pcity) {
    dsend_packet_city_incite_info(pc, city_id,
				  city_incite_cost(pplayer, pcity));
  }
}
