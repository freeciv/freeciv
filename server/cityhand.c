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
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "tech.h"
#include "unit.h"
#include "worklist.h"

#include "citytools.h"
#include "cityturn.h"
#include "civserver.h"
#include "gamelog.h"
#include "maphand.h"
#include "plrhand.h"
#include "settlers.h"
#include "unitfunc.h"
#include "unithand.h"
#include "unittools.h"

#include "aicity.h"

#include "cityhand.h"

static void package_dumb_city(struct player* pplayer, int x, int y,
			      struct packet_city_info *packet);
static void remove_trade_route(int c1, int c2); 

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
    tb/=2;
  if (pc1->owner==pc2->owner)
    tb/=2;

  for(i=0;i<player_knows_techs_with_flag(city_owner(pc1),TF_TRADE_REVENUE_REDUCE);i++) {
    tb = (tb * 2)/3;
  }
  /* was: A_RAILROAD, A_FLIGHT */
  return tb;
}

/****************************************************************
...
*****************************************************************/

char *city_name_suggestion(struct player *pplayer)
{
  char **nptr;
  int i, j, k;
  static int n_misc=0;
  static char tempname[100];

  freelog(LOG_VERBOSE, "Suggesting city name for %s", pplayer->name);
  
  if (!n_misc) {
    for (i=0; misc_city_names[i]; i++) {}
    n_misc = i;
  }

  for(nptr=get_nation_by_plr(pplayer)->default_city_names; *nptr; nptr++) {
    if(!game_find_city_by_name(*nptr))
      return *nptr;
  }

  j = myrand(n_misc);
  for (i=0; i<n_misc; i++) {
    k = (i+j) % n_misc;
    if (!game_find_city_by_name(misc_city_names[k])) 
      return misc_city_names[k];
  }

  for (i = 0; i < 1000;i++ ) {
    my_snprintf(tempname, sizeof(tempname), _("city %d"), i);
    if (!game_find_city_by_name(tempname)) 
      return tempname;
  }
  return "";
}

/**************************************************************************
...
**************************************************************************/
void create_city(struct player *pplayer, const int x, const int y, char *name)
{
  struct city *pcity, *othercity;
  int i, x_itr, y_itr;
  struct tile_type *pcitytile;

  freelog(LOG_DEBUG, "Creating city %s", name);
  gamelog(GAMELOG_FOUNDC,"%s (%i, %i) founded by the %s", name, 
	  x,y, get_nation_name_plural(pplayer->nation));
  pcity=fc_malloc(sizeof(struct city));

  pcity->id=get_next_id_number();
  idex_register_city(pcity);
  pcity->owner=pplayer->player_no;
  pcity->x=x;
  pcity->y=y;
  sz_strlcpy(pcity->name, name);
  pcity->size=1;
  pcity->ppl_elvis=1;
  pcity->ppl_scientist=pcity->ppl_taxman=0;
  pcity->ppl_happy[4]=0;
  pcity->ppl_content[4]=1;
  pcity->ppl_unhappy[4]=0;
  pcity->was_happy=0;
  pcity->steal=0;
  for (i=0;i<4;i++)
    pcity->trade_value[i]=pcity->trade[i]=0;
  pcity->food_stock=0;
  pcity->shield_stock=0;
  pcity->trade_prod=0;
  pcity->original = pplayer->player_no;
  pcity->is_building_unit=1;
  pcity->did_buy=-1; /* code so we get a different message */
  pcity->airlift=0;
  pcity->currently_building=best_role_unit(pcity, L_FIRSTBUILD);

  /* Set up the worklist */
  pcity->worklist = create_worklist();

  for (y_itr = 0; y_itr < CITY_MAP_SIZE; y_itr++)
    for (x_itr = 0; x_itr < CITY_MAP_SIZE; x_itr++)
      pcity->city_map[x_itr][y_itr]=C_TILE_EMPTY;

  for(i=0; i<B_LAST; i++)
    pcity->improvements[i]=0;
  if(!pplayer->capital) {
    pplayer->capital=1;
    pcity->improvements[B_PALACE]=1;
  }
  pcity->turn_last_built = game.year;
  pcity->turn_changed_target = game.year;
  pcity->changed_from_id = 0;
  pcity->changed_from_is_unit = 0;
  pcity->before_change_shields = 0;
  pcity->anarchy=0;
  pcity->rapture=0;

  pcity->city_options = CITYOPT_DEFAULT;
  
  pcity->ai.ai_role = AICITY_NONE;
  pcity->ai.trade_want = TRADE_WEIGHTING; 
  memset(pcity->ai.building_want, 0, sizeof(pcity->ai.building_want));
  pcity->ai.workremain = 1; /* there's always work to be done! */
  pcity->ai.danger = -1; /* flag, may come in handy later */
  pcity->corruption = 0;
  pcity->shield_bonus = 100;
  pcity->tax_bonus = 100;
  pcity->science_bonus = 100;

  /* Before arranging workers to show unknown land */
  map_unfog_pseudo_city_area(pplayer, x, y);

  map_set_city(x, y, pcity);

  unit_list_init(&pcity->units_supported);
  city_list_insert(&pplayer->cities, pcity);
  add_city_to_minimap(x, y);

/* it is possible to build a city on a tile that is already worked */
/* this will displace the worker on the newly-built city's tile -- Syela */
  city_map_iterate(x_itr, y_itr) {
    othercity = map_get_city(x+x_itr-2, y+y_itr-2);
    if (othercity && othercity != pcity) {
      if (get_worker_city(othercity, 4 - x_itr, 4 - y_itr) == C_TILE_WORKER) {
        set_worker_city(othercity, 4 - x_itr, 4 - y_itr, C_TILE_UNAVAILABLE);
        add_adjust_workers(othercity); /* will place the displaced */
        city_refresh(othercity); /* may be unnecessary; can't hurt */
      } else set_worker_city(othercity, 4 - x_itr, 4 - y_itr, C_TILE_UNAVAILABLE);
      send_city_info(city_owner(othercity), othercity);
    }
  }

  city_check_workers(pplayer, pcity);
  auto_arrange_workers(pcity); /* forces a worker onto (2,2), thus the above */

  city_refresh(pcity);

  city_incite_cost(pcity);
  initialize_infrastructure_cache(pcity);
  reset_move_costs(x, y);
/* I stupidly thought that setting S_ROAD took care of this, but of course
the city_id isn't set when S_ROAD is set, so reset_move_costs doesn't allow
sea movement at the point it's called.  This led to a problem with the
warmap (but not the GOTOmap warmap) which meant the AI was very reluctant
to use ferryboats.  I really should have identified this sooner. -- Syela */

  send_adjacent_cities(pcity);
  send_city_info(0, pcity);
  maybe_make_first_contact(x, y, pcity->owner);

  /* if anyone changing to ocean here, stop them */
  pcitytile = get_tile_type(map_get_terrain(x, y));
  if (pcitytile->mining_result == T_OCEAN) {
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
      if (punit->activity == ACTIVITY_MINE)
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
    } unit_list_iterate_end;
  }
  if (pcitytile->irrigation_result == T_OCEAN) {
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
      if (punit->activity == ACTIVITY_IRRIGATE)
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
    } unit_list_iterate_end;
  }
  if (pcitytile->transform_result == T_OCEAN) {
    unit_list_iterate(map_get_tile(x, y)->units, punit) {
      if (punit->activity == ACTIVITY_TRANSFORM)
	handle_unit_activity_request(punit, ACTIVITY_IDLE);
    } unit_list_iterate_end;
  }
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

  copy_worklist(pcity->worklist, &preq->worklist);

  send_city_info(pplayer, pcity);
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
    send_city_info(pplayer, pcity);
  } else if (!preq->value) global_city_refresh(pplayer);
}

/**************************************************************************
  Change the build target.  Added when adding worklists; target is encoded
  as it is for worklists (0..B_LAST-1 are building and wonders, 
  B_LAST..B_LAST+U_LAST-1 are units+B_LAST).
**************************************************************************/
void change_build_target(struct player *pplayer, struct city *pcity, 
			 int target, int is_unit, int event)
{
  char *name;
  char *source;

  /* If the city is already building this thing, don't do anything */
  if (pcity->is_building_unit == is_unit &&
      pcity->currently_building == target)
    return;

  /* Is the city no longer building a wonder? */
  if(!pcity->is_building_unit && is_wonder(pcity->currently_building) &&
     (event != E_IMP_AUTO && event != E_WORKLIST)) {
    /* If the build target is changed because of an advisor's suggestion or
       because the worklist advances, then the wonder was completed -- 
       don't announce that the player has *stopped* building that wonder. 
       */
    notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STOPPED,
		     _("Game: The %s have stopped building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->currently_building),
		     pcity->name);
  }

  /* Manage the city change-production penalty.
     (May penalize, restore or do nothing to the shield_stock.) */
  city_change_production_penalty(pcity, target, is_unit, TRUE);

  /* Change build target. */
  pcity->currently_building = target;
  pcity->is_building_unit = is_unit;

  /* What's the name of the target? */
  if (is_unit)
    name = unit_types[pcity->currently_building].name;
  else
    name = improvement_types[pcity->currently_building].name;

  switch (event) {
    case E_WORKLIST: source = _(" from the worklist"); break;
/* Should we give the AI auto code credit?
    case E_IMP_AUTO: source = _(" as suggested by the AI advisor"); break;
*/
    default: source = ""; break;
  }

  /* Tell the player what's up. */
  if (event)
    notify_player_ex(pplayer, pcity->x, pcity->y, event,
		     _("Game: %s is building %s%s."),
		     pcity->name, name, source);
  else
    notify_player_ex(pplayer, pcity->x, pcity->y, E_UNIT_BUILD,
		     _("Game: %s is building %s."), 
		     pcity->name, name);

  /* If the city is building a wonder, tell the rest of the world
     about it. */
  if (!pcity->is_building_unit && is_wonder(pcity->currently_building))
    notify_player_ex(0, pcity->x, pcity->y, E_WONDER_STARTED,
		     _("Game: The %s have started building The %s in %s."),
		     get_nation_name_plural(pplayer->nation),
		     get_impr_name_ex(pcity, pcity->currently_building),
		     pcity->name);
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
    freelog(LOG_NORMAL, "Pcity null in handle_city_change"
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
  
  pcity=find_city_by_id(preq->city_id);

  if(!player_owns_city(pplayer, pcity))
    return;

  if((cp=get_sane_name(preq->name))) {
    /* more sanity tests! any existing city with that name? */
    sz_strlcpy(pcity->name, cp);
    city_refresh(pcity);
    send_city_info(0, pcity);
  }
  else
    notify_player(pplayer, _("Game: %s is not a valid name."), preq->name);
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
    freelog(LOG_NORMAL, "City-name suggestion request from non-player %s",
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
void send_player_cities(struct player *pplayer)
{
  city_list_iterate(pplayer->cities, pcity) {
    city_refresh(pcity);
    send_city_info(pplayer, pcity);
  }
  city_list_iterate_end;
}

/**************************************************************************
...
**************************************************************************/
void send_adjacent_cities(struct city *pcity)
{
  int x1,x2,y1,y2;
  int i;

  x1=pcity->x-4; x2=pcity->x+4; y1=pcity->y-4; y2=pcity->y+4;
  x1=map_adjust_x(x1); x2=map_adjust_x(x2);
  y1=map_adjust_y(y1); y2=map_adjust_y(y2);
  if(x1>x2)  {
    for (i=0;i<game.nplayers;i++) {
      city_list_iterate(game.players[i].cities, pcity2)
	if(pcity2!=pcity)
	  if((pcity2->x <= x2 || pcity2->x >= x1) && 
	     (pcity2->y >= y1 && pcity2->y <= y2) )  {
	    city_check_workers(city_owner(pcity2),pcity2);
	    send_city_info(city_owner(pcity2), pcity2);
	  }
      city_list_iterate_end;
    }
  } else {
    for (i=0;i<game.nplayers;i++) {
      city_list_iterate(game.players[i].cities, pcity2)
	if(pcity2!=pcity)
	  if((pcity2->x <= x2 && pcity2->x >= x1) && 
	     (pcity2->y >= y1 && pcity2->y <= y2) )  {
	    city_check_workers(city_owner(pcity2),pcity2);
	    send_city_info(city_owner(pcity2), pcity2);
	  }
      city_list_iterate_end;
    }
  }
}

/**************************************************************************
  Broadcast info about a city to all players who observe the tile. 
  If the player can see the city we update the city info first.
  If not we just use the info from the players private map.
  See also comments to send_city_info_at_tile().
  (Split off from send_city_info_at_tile() because that was getting
  too difficult for me to understand... --dwp)
**************************************************************************/
static void broadcast_city_info(struct city *pcity)
{
  int o;
  struct player *powner = city_owner(pcity);
  struct packet_city_info packet;

  /* nocity_send is used to inhibit sending cities to the owner between
   * turn updates
   */
  if (!nocity_send) {    /* first send all info to the owner */
    update_dumb_city(powner, pcity);
    package_city(pcity, &packet, FALSE);
    lsend_packet_city_info(&powner->connections, &packet);
  }

  /* send to all others who can see the city: */
  for(o=0; o<game.nplayers; o++) {
    struct player *pplayer = &game.players[o];
    if(pcity->owner==o) continue; /* already sent above */
    if(map_get_known_and_seen(pcity->x, pcity->y, o)) {
      update_dumb_city(pplayer, pcity);
      package_dumb_city(pplayer, pcity->x, pcity->y, &packet);
      lsend_packet_city_info(&pplayer->connections, &packet);
    }
  }
  /* send to non-player observers:
   * should these only get dumb_city type info?
   */
  conn_list_iterate(game.game_connections, pconn) {
    if (pconn->player==NULL && pconn->observer) {
      package_city(pcity, &packet, FALSE);
      send_packet_city_info(pconn, &packet);
    }
  }
  conn_list_iterate_end;
}

/**************************************************************************
  A wrapper, accessing either broadcast_city_info() (dest==NULL),
  or a convenience case of send_city_info_at_tile().
  Must specify non-NULL pcity.
**************************************************************************/
void send_city_info(struct player *dest, struct city *pcity)
{
  assert(pcity);
  if (dest==NULL) {
    broadcast_city_info(pcity);
  } else {
    send_city_info_at_tile(dest, &dest->connections, pcity, pcity->x, pcity->y);
  }
}

/**************************************************************************
Send info about a city, as seen by pviewer, to dest (usually dest will
be pviewer->connections). If pplayer can see the city we update the city
info first. If not we just use the info from the players private map.

If (pviewer == NULL) this is for observers, who see everything (?)
For this function dest may not be NULL.  See send_city_info() and
broadcast_city_info().

If pcity is non-NULL it should be same as map_get_city(x,y); if pcity
is NULL, this function calls map_get_city(x,y) (it is ok if this
returns NULL).

Sometimes a player's map contain a city that doesn't actually exist. Use
reality_check_city(pplayer, x,y) to update that. Remember to NOT send info
about a city to a player who thinks the tile contains another city. If you
want to update the clients info of the tile you must use
reality_check_city(pplayer, x, y) first. This is generally taken care of
automatically when a tile becomes visible.
**************************************************************************/
void send_city_info_at_tile(struct player *pviewer, struct conn_list *dest,
			    struct city *pcity, int x, int y)
{
  struct player *powner = NULL;
  struct packet_city_info packet;
  struct dumb_city *pdcity;

  if (pcity==NULL)
    pcity = map_get_city(x,y);
  if (pcity)
    powner = city_owner(pcity);

  if (powner == pviewer) {
    /* send info to owner */
    /* This case implies powner non-NULL which means pcity non-NULL */
    /* nocity_send is used to inhibit sending cities to the owner between
       turn updates */
    if (!nocity_send) {
      /* send all info to the owner */
      update_dumb_city(powner, pcity);
      package_city(pcity, &packet, FALSE);
      lsend_packet_city_info(dest, &packet);
    }
  }
  else {
    /* send info to non-owner */
    if (pviewer==NULL) {	/* observer */
      if (pcity) {
	package_city(pcity, &packet, FALSE);   /* should be dumb_city info? */
	lsend_packet_city_info(dest, &packet);
      }
    } else if (map_get_known_and_seen(x, y, pviewer->player_no)) {
      if (pcity) { /* it's there and we see it; update and send */
	update_dumb_city(pviewer, pcity);
	package_dumb_city(pviewer, x, y, &packet);
	lsend_packet_city_info(dest, &packet);
      }
    } else { /* not seen; send old info */
      pdcity = map_get_player_tile(x, y, pviewer->player_no)->city;
      if (pdcity) {
	package_dumb_city(pviewer, x, y, &packet);
	lsend_packet_city_info(dest, &packet);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void remove_trade_route(int c1, int c2) 
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
FIXME: should be in the same file as transfer_city
**************************************************************************/
void remove_city(struct city *pcity)
{
  int o, x, y;
  struct player *pplayer = city_owner(pcity);

  gamelog(GAMELOG_LOSEC,"%s lose %s (%i,%i)",
          get_nation_name_plural(pplayer->nation),
          pcity->name,pcity->x,pcity->y);

  /* This is cutpasted with modifications from transfer_city_units. Yes, it is ugly.
     But I couldn't see a nice way to make them use the same code */
  unit_list_iterate(pcity->units_supported, punit) {
    struct city *new_home_city = map_get_city(punit->x, punit->y);
    x = punit->x; y = punit->y;
    if(new_home_city && new_home_city != pcity) {
      /* unit is in another city: make that the new homecity,
	 unless that city is actually the same city (happens if disbanding) */
      freelog(LOG_VERBOSE, "Changed homecity of %s in %s",
	      unit_name(punit->type), new_home_city->name);
      notify_player(pplayer, _("Game: Changed homecity of %s in %s."),
		    unit_name(punit->type), new_home_city->name);
      create_unit_full(pplayer, x, y,
		       punit->type, punit->veteran, new_home_city->id,
		       punit->moves_left, punit->hp);
    }

    wipe_unit_safe(punit, &myiter);
  }
  unit_list_iterate_end;

  for (o=0; o<4; o++)
    remove_trade_route(pcity->trade[o], pcity->id); 

  x = pcity->x;
  y = pcity->y;

  /* idex_unregister_city() is called in game_remove_city() below */

  /* dealloc_id(pcity->id); We do NOT dealloc cityID's as the cities may still be
     alive in the client. As the number of romoved cities is small the leak is
     acceptable. */

/* DO NOT remove city from minimap here. -- Syela */
  
  game_remove_city(pcity);

  for(o=0; o<game.nplayers; o++)           /* dests */
    if (map_get_known_and_seen(x, y, o))
      reality_check_city(&game.players[o], x, y);
  map_fog_pseudo_city_area(pplayer, x, y);

  reset_move_costs(x, y);
}


/**************************************************************************
The following has to be called every time a city, pcity, has changed 
owner to update the tile->worked pointer.
**************************************************************************/

void update_map_with_city_workers(struct city *pcity)
{
  int x, y;
    city_map_iterate(x,y) {
	if (pcity->city_map[x][y] == C_TILE_WORKER)
	       set_worker_city(pcity, x, y, C_TILE_WORKER);
       }
}

/**************************************************************************
The following has to be called every time a city, pcity, has changed
owner to update the city's traderoutes.
**************************************************************************/
void reestablish_city_trade_routes(struct city *pcity) 
{
  int i;
  struct city *oldtradecity;

  for (i=0;i<4;i++) {
    if (pcity->trade[i]) {
      oldtradecity=find_city_by_id(pcity->trade[i]);
      pcity->trade[i]=0;
      if (can_establish_trade_route(pcity, oldtradecity)) {   
         establish_trade_route(pcity, oldtradecity);
      }
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void package_city(struct city *pcity, struct packet_city_info *packet,
		  int dipl_invest)
{
  int i, x, y;
  char *p;
  packet->id=pcity->id;
  packet->owner=pcity->owner;
  packet->x=pcity->x;
  packet->y=pcity->y;
  sz_strlcpy(packet->name, pcity->name);

  packet->size=pcity->size;
  packet->ppl_happy=pcity->ppl_happy[4];
  packet->ppl_content=pcity->ppl_content[4];
  packet->ppl_unhappy=pcity->ppl_unhappy[4];
  packet->ppl_elvis=pcity->ppl_elvis;
  packet->ppl_scientist=pcity->ppl_scientist;
  packet->ppl_taxman=pcity->ppl_taxman;
  for (i=0;i<4;i++)  {
    packet->trade[i]=pcity->trade[i];
    packet->trade_value[i]=pcity->trade_value[i];
  }

  packet->food_prod=pcity->food_prod;
  packet->food_surplus=pcity->food_surplus;
  packet->shield_prod=pcity->shield_prod;
  packet->shield_surplus=pcity->shield_surplus;
  packet->trade_prod=pcity->trade_prod;
  packet->corruption=pcity->corruption;
  
  packet->luxury_total=pcity->luxury_total;
  packet->tax_total=pcity->tax_total;
  packet->science_total=pcity->science_total;
  
  packet->food_stock=pcity->food_stock;
  packet->shield_stock=pcity->shield_stock;
  packet->pollution=pcity->pollution;

  packet->city_options=pcity->city_options;
  
  packet->is_building_unit=pcity->is_building_unit;
  packet->currently_building=pcity->currently_building;

  packet->turn_last_built=pcity->turn_last_built;
  packet->turn_changed_target=pcity->turn_changed_target;
  packet->changed_from_id=pcity->changed_from_id;
  packet->changed_from_is_unit=pcity->changed_from_is_unit;
  packet->before_change_shields=pcity->before_change_shields;

  copy_worklist(&packet->worklist, pcity->worklist);
  packet->diplomat_investigate=dipl_invest;

  packet->airlift=pcity->airlift;
  packet->did_buy=pcity->did_buy;
  packet->did_sell=pcity->did_sell;
  packet->was_happy=pcity->was_happy;
  p=packet->city_map;
  for(y=0; y<CITY_MAP_SIZE; y++)
    for(x=0; x<CITY_MAP_SIZE; x++)
      *p++=get_worker_city(pcity, x, y)+'0';
  *p='\0';

  p=packet->improvements;
  for(i=0; i<B_LAST; i++)
    *p++=(pcity->improvements[i]) ? '1' : '0';
  *p='\0';
}

/**************************************************************************
This fills out a package from a players dumb_city.
FIXME: we should make a new package and let the client fill in the dummy
info itself
**************************************************************************/
static void package_dumb_city(struct player* pplayer, int x, int y,
			      struct packet_city_info *packet)
{
  int i;
  char *p;
  struct dumb_city *pdcity = map_get_player_tile(x, y, pplayer->player_no)->city;
  struct city *pcity;
  packet->id=pdcity->id;
  packet->owner=pdcity->owner;
  packet->x=x;
  packet->y=y;
  sz_strlcpy(packet->name, pdcity->name);

  packet->size=pdcity->size;
  packet->ppl_happy=0;
  if (map_get_known_and_seen(x, y, pplayer->player_no)) {
    /* Since the tile is visible the player can see the tile,
       and if it didn't actually have a city pdcity would be NULL */
    pcity = map_get_tile(x,y)->city;
    if (pcity->ppl_happy[4]>=pcity->ppl_unhappy[4]) {
      packet->ppl_content=pdcity->size;
      packet->ppl_unhappy=0;
    } else {
      packet->ppl_content=0;
      packet->ppl_unhappy=pdcity->size;
    }
  } else {
    packet->ppl_content=pdcity->size;
    packet->ppl_unhappy=0;
  }
  packet->ppl_elvis=pdcity->size;
  packet->ppl_scientist=0;
  packet->ppl_taxman=0;
  for (i=0;i<4;i++)  {
    packet->trade[i]=0;
    packet->trade_value[i]=0;
  }

  packet->food_prod=0;
  packet->food_surplus=0;
  packet->shield_prod=0;
  packet->shield_surplus=0;
  packet->trade_prod=0;
  packet->corruption=0;
  
  packet->luxury_total=0;
  packet->tax_total=0;
  packet->science_total=0;
  
  packet->food_stock=0;
  packet->shield_stock=0;
  packet->pollution=0;

  packet->city_options=0;
  
  packet->is_building_unit=0;
  packet->currently_building=0;
  init_worklist(&packet->worklist);
  packet->diplomat_investigate=0;

  packet->airlift=0;
  packet->did_buy=0;
  packet->did_sell=0;
  packet->was_happy=0;

  p=packet->improvements;

  for(i=0; i<B_LAST; i++)
    *p++ = '0';

  if ((pcity = map_get_city(x,y)) && pcity->id == pdcity->id &&
      city_got_building(pcity,  B_PALACE))
    packet->improvements[B_PALACE] = '1';

  if (pdcity->has_walls)
    packet->improvements[B_CITY] = '1';

  *p='\0';

  p=packet->city_map;
  for(y=0; y<CITY_MAP_SIZE; y++) /* (Mis)use of function parameters */
    for(x=0; x<CITY_MAP_SIZE; x++)
      *p++=C_TILE_EMPTY+'0';
  *p='\0';
}

/**************************************************************************
updates a players knowledge about a city. If the player_tile already
contains a city it must be the same city (avoid problems by always calling
reality_check city first)
**************************************************************************/
void update_dumb_city(struct player *pplayer, struct city *pcity)
{
  struct player_tile *plrtile = map_get_player_tile(pcity->x, pcity->y,
						    pplayer->player_no);
  struct dumb_city *pdcity;
  if (!plrtile->city) {
    plrtile->city = fc_malloc(sizeof(struct dumb_city));
    plrtile->city->id = pcity->id;
  }
  pdcity = plrtile->city;
  if (pdcity->id != pcity->id)
    freelog(LOG_FATAL, "Trying to update old city (wrong ID) at %i,%i for player %s",
	    pcity->x, pcity->y, pplayer->name);
  sz_strlcpy(pdcity->name, pcity->name);
  pdcity->size = pcity->size;
  pdcity->has_walls = city_got_citywalls(pcity);
  pdcity->owner = pcity->owner;
}

/**************************************************************************
Removes outdated (nonexistant) cities from a player
**************************************************************************/
void reality_check_city(struct player *pplayer,int x, int y)
{
  struct packet_generic_integer packet;
  struct city *pcity;
  struct dumb_city *pdcity = map_get_player_tile(x, y, pplayer->player_no)->city;

  if (pdcity) {
    pcity = map_get_tile(x,y)->city;
    if (!pcity || (pcity && pcity->id != pdcity->id)) {
      packet.value=pdcity->id;
      lsend_packet_generic_integer(&pplayer->connections, PACKET_REMOVE_CITY,
				   &packet);
      free(pdcity);
      map_get_player_tile(x, y, pplayer->player_no)->city = NULL;
    }
  }
}

/**************************************************************************
  Send to each client information about the cities it knows about.
  dest may not be NULL
**************************************************************************/
void send_all_known_cities(struct conn_list *dest)
{
  conn_list_do_buffer(dest);
  conn_list_iterate(*dest, pconn) {
    int x, y;
    struct player *pplayer = pconn->player;
    if (pplayer==NULL && !pconn->observer) {
      continue;
    }
    for(y=0; y<map.ysize; y++) {
      for(x=0; x<map.xsize; x++) {
	if (pplayer==NULL
	    || map_get_player_tile(x, y, pplayer->player_no)->city) {
	  send_city_info_at_tile(pplayer, &pconn->self, NULL, x, y);
	}
      }
    }
  }
  conn_list_iterate_end;
  conn_list_do_unbuffer(dest);
}
