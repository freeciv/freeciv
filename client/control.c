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

#include "capability.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "dialogs_g.h"
#include "gui_main_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"

#include "civclient.h"
#include "clinet.h"
#include "goto.h"
#include "options.h"
#include "tilespec.h"

#include "control.h"

/* gui-dep code may adjust depending on tile size etc: */
int num_units_below = MAX_NUM_UNITS_BELOW;


/* unit_focus points to the current unit in focus */
static struct unit *punit_focus;

/* These should be set via set_hover_state() */
int hover_unit = 0; /* id of unit hover_state applies to */
enum cursor_hover_state hover_state = HOVER_NONE;
/* This may only be here until client goto is fully implemented.
   It is reset each time the hower_state is reset. */
int draw_goto_line = 1;

/* units involved in current combat */
static struct unit *punit_attacking;
static struct unit *punit_defending;

/*************************************************************************/

static struct unit *find_best_focus_candidate(void);

/**************************************************************************
...
**************************************************************************/
void set_hover_state(struct unit *punit, enum cursor_hover_state state)
{
  assert(punit || state==HOVER_NONE);
  draw_goto_line = 1;
  if (punit)
    hover_unit = punit->id;
  else
    hover_unit = 0;
  hover_state = state;
  exit_goto_state();
}

/**************************************************************************
...
**************************************************************************/
void handle_advance_focus(struct packet_generic_integer *packet)
{
  struct unit *punit = find_unit_by_id(packet->value);
  if (punit && punit_focus == punit)
    advance_unit_focus();
}

/**************************************************************************
Center on the focus unit, if off-screen and auto_center_on_unit is true.
**************************************************************************/
void auto_center_on_focus_unit(void)
{
  if (punit_focus &&
      auto_center_on_unit &&
      !tile_visible_and_not_on_border_mapcanvas(punit_focus->x, punit_focus->y))
    center_tile_mapcanvas(punit_focus->x, punit_focus->y);
}

/**************************************************************************
note: punit can be NULL
We make sure that the previous focus unit is refreshed, if necessary,
_after_ setting the new focus unit (otherwise if the previous unit is
in a city, the refresh code draws the previous unit instead of the city).
**************************************************************************/
void set_unit_focus(struct unit *punit)
{
  struct unit *punit_old_focus=punit_focus;

  punit_focus=punit;

  if(punit) {
    auto_center_on_focus_unit();

    punit->focus_status=FOCUS_AVAIL;
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
  }
  
  /* avoid the old focus unit disappearing: */
  if (punit_old_focus!=NULL
      && (punit==NULL || !same_pos(punit_old_focus->x, punit_old_focus->y,
				   punit->x, punit->y))) {
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, 1);
  }

  update_unit_info_label(punit);
  update_menus();
}

/**************************************************************************
note: punit can be NULL
Here we don't bother making sure the old focus unit is
refreshed, as this is only used in special cases where
thats not necessary.  (I think...) --dwp
**************************************************************************/
void set_unit_focus_no_center(struct unit *punit)
{
  punit_focus=punit;

  if(punit) {
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
    punit->focus_status=FOCUS_AVAIL;
  }
}

/**************************************************************************
The only difference is that here we draw the "cross".
**************************************************************************/
void set_unit_focus_and_select(struct unit *punit)
{
  set_unit_focus(punit);
  if (punit) {
    put_cross_overlay_tile(punit->x, punit->y);
  }
}

/**************************************************************************
If there is no unit currently in focus, or if the current unit in
focus should not be in focus, then get a new focus unit.
We let GOTO-ing units stay in focus, so that if they have moves left
at the end of the goto, then they are still in focus.
**************************************************************************/
void update_unit_focus(void)
{
  if(punit_focus==NULL
     || (punit_focus->activity!=ACTIVITY_IDLE
	 && punit_focus->activity!=ACTIVITY_GOTO)
     || punit_focus->moves_left==0 
     || punit_focus->ai.control) {
    advance_unit_focus();
  }
}

/**************************************************************************
...
**************************************************************************/
struct unit *get_unit_in_focus(void)
{
  return punit_focus;
}

/**************************************************************************
...
**************************************************************************/
void advance_unit_focus(void)
{
  struct unit *punit_old_focus=punit_focus;

  punit_focus=find_best_focus_candidate();

  set_hover_state(NULL, HOVER_NONE);

  if(!punit_focus) {
    unit_list_iterate(game.player_ptr->units, punit) {
      if(punit->focus_status==FOCUS_WAIT)
	punit->focus_status=FOCUS_AVAIL;
    }
    unit_list_iterate_end;
    punit_focus=find_best_focus_candidate();
    if (punit_focus == punit_old_focus) {
      /* we don't want to same unit as before if there are any others */
      punit_focus=find_best_focus_candidate();
      if(!punit_focus) {
	/* but if that is the only choice, take it: */
	punit_focus=find_best_focus_candidate();
      }
    }
  }

  /* We have to do this ourselves, and not rely on set_unit_focus(),
   * because above we change punit_focus directly.
   */
  if(punit_old_focus!=NULL && punit_old_focus!=punit_focus)
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, 1);

  set_unit_focus(punit_focus);

  /* Handle auto-turn-done mode:  If a unit was in focus (did move),
   * but now none are (no more to move), then fake a Turn Done keypress.
   */
  if(auto_turn_done && punit_old_focus!=NULL && punit_focus==NULL)
    key_end_turn();
}

/**************************************************************************
Find the nearest available unit for focus, excluding the current unit
in focus (if any).  If the current focus unit is the only possible
unit, or if there is no possible unit, returns NULL.
**************************************************************************/
static struct unit *find_best_focus_candidate(void)
{
  struct unit *best_candidate;
  int best_dist=99999;
  int x,y;

  if(punit_focus)  {
    x=punit_focus->x; y=punit_focus->y;
  } else {
    get_center_tile_mapcanvas(&x,&y);
  }
    
  best_candidate=NULL;
  unit_list_iterate(game.player_ptr->units, punit) {
    if(punit!=punit_focus) {
      if(punit->focus_status==FOCUS_AVAIL && punit->activity==ACTIVITY_IDLE &&
	 punit->moves_left && !punit->ai.control) {
        int d;
	d=sq_map_distance(punit->x, punit->y, x, y);
	if(d<best_dist) {
	  best_candidate=punit;
	  best_dist=d;
	}
      }
    }
  }
  unit_list_iterate_end;
  return best_candidate;
}

/**************************************************************************
Return a pointer to a visible unit, if there is one.
**************************************************************************/
struct unit *find_visible_unit(struct tile *ptile)
{
  struct unit *panyowned = NULL, *panyother = NULL, *ptptother = NULL;

  /* If no units here, return nothing. */
  if (unit_list_size(&ptile->units)==0) {
    return NULL;
  }

  /* If a unit is attacking we should show that on top */
  if (punit_attacking && map_get_tile(punit_attacking->x,punit_attacking->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_attacking) return punit;
    unit_list_iterate_end;
  }

  /* If a unit is defending we should show that on top */
  if (punit_defending && map_get_tile(punit_defending->x,punit_defending->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_defending) return punit;
    unit_list_iterate_end;
  }

  /* If the unit in focus is at this tile, show that on top */
  if (punit_focus && map_get_tile(punit_focus->x,punit_focus->y) == ptile) {
    unit_list_iterate(ptile->units, punit)
      if(punit == punit_focus) return punit;
    unit_list_iterate_end;
  }

  /* If a city is here, return nothing (unit hidden by city). */
  if (ptile->city) {
    return NULL;
  }

  /* Iterate through the units to find the best one we prioritize this way:
       1: owned transporter.
       2: any owned unit
       3: any transporter
       4: any unit
     (always return first in stack). */
  unit_list_iterate(ptile->units, punit)
    if (unit_owner(punit) == game.player_ptr) {
      if (get_transporter_capacity(punit)) {
	return punit;
      } else if (!panyowned) {
	panyowned = punit;
      }
    } else if (!ptptother &&
	       player_can_see_unit(game.player_ptr, punit)) {
      if (get_transporter_capacity(punit)) {
	ptptother = punit;
      } else if (!panyother) {
	panyother = punit;
      }
    }
  unit_list_iterate_end;

  return (panyowned ? panyowned : (ptptother ? ptptother : panyother));
}

/**************************************************************************
...
**************************************************************************/
void set_units_in_combat(struct unit *pattacker, struct unit *pdefender)
{
  punit_attacking = pattacker;
  punit_defending = pdefender;
}

/**************************************************************************
...
**************************************************************************/
void blink_active_unit(void)
{
  static int is_shown;
  struct unit *punit;

  if((punit=get_unit_in_focus())) {
    if(is_shown) {
      set_focus_unit_hidden_state(1);
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
      set_focus_unit_hidden_state(0);
    } else {
      refresh_tile_mapcanvas(punit->x, punit->y, 1);
    }
    is_shown=!is_shown;
  }
}

/**************************************************************************
  Add punit to queue of caravan arrivals, and popup a window for the
  next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_caravan_arrival(struct unit *punit)
{
  static struct genlist arrival_queue;
  static int is_init_arrival_queue = 0;
  int *p_id;

  /* arrival_queue is a list of individually malloc-ed ints with
     punit.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = 1;
  }

  if (punit) {
    p_id = fc_malloc(sizeof(int));
    *p_id = punit->id;
    genlist_insert(&arrival_queue, p_id, -1);
  }

  /* There can only be one dialog at a time: */
  if (caravan_dialog_is_open()) {
    return;
  }
  
  while (genlist_size(&arrival_queue)) {
    int id;
    
    p_id = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_id);
    id = *p_id;
    free(p_id);
    punit = player_find_unit_by_id(game.player_ptr, id);

    if (punit && (unit_can_help_build_wonder_here(punit)
		  || unit_can_est_traderoute_here(punit))
	&& (!game.player_ptr->ai.control || ai_popup_windows)) {
      struct city *pcity_dest = map_get_city(punit->x, punit->y);
      struct city *pcity_homecity = find_city_by_id(punit->homecity);
      if (pcity_dest && pcity_homecity) {
	popup_caravan_dialog(punit, pcity_homecity, pcity_dest);
	return;
      }
    }
  }
}

/**************************************************************************
  Add punit/pcity to queue of diplomat arrivals, and popup a window for
  the next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_diplomat_arrival(struct unit *pdiplomat, int victim_id)
{
  static struct genlist arrival_queue;
  static int is_init_arrival_queue = 0;
  int *p_ids;

  /* arrival_queue is a list of individually malloc-ed int[2]s with
     punit.id and pcity.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = 1;
  }

  if (pdiplomat && victim_id) {
    p_ids = fc_malloc(2*sizeof(int));
    p_ids[0] = pdiplomat->id;
    p_ids[1] = victim_id;
    genlist_insert(&arrival_queue, p_ids, -1);
  }

  /* There can only be one dialog at a time: */
  if (diplomat_dialog_is_open()) {
    return;
  }

  while (genlist_size(&arrival_queue)) {
    int diplomat_id, victim_id;
    struct city *pcity;
    struct unit *punit;

    p_ids = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_ids);
    diplomat_id = p_ids[0];
    victim_id = p_ids[1];
    free(p_ids);
    pdiplomat = player_find_unit_by_id(game.player_ptr, diplomat_id);
    pcity = find_city_by_id(victim_id);
    punit = find_unit_by_id(victim_id);

    if (!pdiplomat || !unit_flag(pdiplomat->type, F_DIPLOMAT))
      continue;

    if (punit
	&& is_diplomat_action_available(pdiplomat, DIPLOMAT_ANY_ACTION,
					punit->x, punit->y)
	&& diplomat_can_do_action(pdiplomat, DIPLOMAT_ANY_ACTION,
				  punit->x, punit->y)) {
      popup_diplomat_dialog(pdiplomat, punit->x, punit->y);
      return;
    } else if (pcity
	       && is_diplomat_action_available(pdiplomat, DIPLOMAT_ANY_ACTION,
					       pcity->x, pcity->y)
	       && diplomat_can_do_action(pdiplomat, DIPLOMAT_ANY_ACTION,
					 pcity->x, pcity->y)) {
      popup_diplomat_dialog(pdiplomat, pcity->x, pcity->y);
      return;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_goto(void)
{
  struct unit *punit = get_unit_in_focus();

  if (!punit)
    return;

  if (hover_state != HOVER_GOTO) {
    set_hover_state(punit, HOVER_GOTO);
    update_unit_info_label(punit);
    /* Not yet implemented for air units */
    if (is_air_unit(punit)) {
      draw_goto_line = 0;
    } else {
      enter_goto_state(punit);
      create_line_at_mouse_pos();
    }
  } else if (!is_air_unit(punit)) {
    assert(goto_is_active());
    goto_add_waypoint();
  }
}

/**************************************************************************
prompt player for entering destination point for unit connect
(e.g. connecting with roads)
**************************************************************************/
void request_unit_connect(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if (punit && can_unit_do_connect (punit, ACTIVITY_IDLE)) {
    set_hover_state(punit, HOVER_CONNECT);
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_unload(struct unit *punit)
{
  struct packet_unit_request req;

  if(!get_transporter_capacity(punit)) {
    append_output_window(_("Game: Only transporter units can be unloaded."));
    return;
  }

  request_unit_wait(punit);    /* RP: unfocus the ship */
  
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_UNLOAD);
}

/**************************************************************************
(RP:) un-sentry all my own sentried units on punit's tile
**************************************************************************/
void request_unit_wakeup(struct unit *punit)
{
  wakeup_sentried_units(punit->x,punit->y);
}

void wakeup_sentried_units(int x, int y)
{
  unit_list_iterate(map_get_tile(x,y)->units, punit) {
    if(punit->activity==ACTIVITY_SENTRY && game.player_idx==punit->owner) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  }
  unit_list_iterate_end;
}

/**************************************************************************
Player pressed 'b' or otherwise instructed unit to build or add to city.
If the unit can build a city, we popup the appropriate dialog.
Otherwise, we just send a packet to the server.
If this action is not appropriate, the server will respond
with an appropriate message.  (This is to avoid duplicating
all the server checks and messages here.)
**************************************************************************/
void request_unit_build_city(struct unit *punit)
{
  if(can_unit_build_city(punit)) {
    struct packet_generic_integer req;
    req.value = punit->id;
    send_packet_generic_integer(&aconnection,
				PACKET_CITY_NAME_SUGGEST_REQ, &req);
    /* the reply will trigger a dialog to name the new city */
  }
  else {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
    return;
  }
}

/**************************************************************************
  This function is called whenever the player pressed an arrow key.

  We do NOT take into account that punit might be a caravan or a diplomat
  trying to move into a city, or a diplomat going into a tile with a unit;
  the server will catch those cases and send the client a package to pop up
  a dialog. (the server code has to be there anyway as goto's are entirely
  in the server)
**************************************************************************/
void request_move_unit_direction(struct unit *punit, int dx, int dy)
{
  int dest_x, dest_y;
  struct unit req_unit;

  dest_x = map_adjust_x(punit->x+dx);
  dest_y = punit->y+dy; /* Not adjusted since if it needed to be adjusted it
			   would mean that we tried to move off the map... */

  /* Catches attempts to move off map */
  if (!is_real_tile(dest_x, dest_y))
    return;

  req_unit = *punit;
  req_unit.x = dest_x;
  req_unit.y = dest_y;
  send_move_unit(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity(struct unit *punit, enum unit_activity act)
{
  struct unit req_unit;
  req_unit=*punit;
  req_unit.activity=act;
  req_unit.activity_target=0;
  send_unit_info(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity_targeted(struct unit *punit, enum unit_activity act,
					int tgt)
{
  struct unit req_unit;
  req_unit=*punit;
  req_unit.activity=act;
  req_unit.activity_target=tgt;
  send_unit_info(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_selected(struct unit *punit)
{
  struct packet_unit_info info;

  info.id=punit->id;
  info.owner=punit->owner;
  info.x=punit->x;
  info.y=punit->y;
  info.homecity=punit->homecity;
  info.veteran=punit->veteran;
  info.type=punit->type;
  info.movesleft=punit->moves_left;
  info.activity=ACTIVITY_IDLE;
  info.activity_target=0;
  info.select_it=1;
  info.packet_use = UNIT_INFO_IDENTITY;

  send_packet_unit_info(&aconnection, &info);
}

/****************************************************************
...
*****************************************************************/
void request_unit_sentry(struct unit *punit)
{
  if(punit->activity!=ACTIVITY_SENTRY &&
     can_unit_do_activity(punit, ACTIVITY_SENTRY))
    request_new_unit_activity(punit, ACTIVITY_SENTRY);
}

/****************************************************************
...
*****************************************************************/
void request_unit_fortify(struct unit *punit)
{
  if(punit->activity!=ACTIVITY_FORTIFYING &&
     can_unit_do_activity(punit, ACTIVITY_FORTIFYING))
    request_new_unit_activity(punit, ACTIVITY_FORTIFYING);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_pillage(struct unit *punit)
{
  struct tile * ptile;
  int pspresent;
  int psworking;
  int what;
  int would;

  ptile = map_get_tile(punit->x, punit->y);
  pspresent = get_tile_infrastructure_set(ptile);
  psworking = get_unit_tile_pillage_set(punit->x, punit->y);
  what = get_preferred_pillage(pspresent & (~psworking));
  would = what | map_get_infrastructure_prerequisite (what);

  if ((game.rgame.pillage_select) &&
      ((pspresent & (~(psworking | would))) != S_NO_SPECIAL)) {
    popup_pillage_dialog(punit, pspresent & (~psworking));
  } else {
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, what);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_disband(struct unit *punit)
{
  struct packet_unit_request req;
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_DISBAND);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_change_homecity(struct unit *punit)
{
  struct city *pcity;
  
  if((pcity=map_get_city(punit->x, punit->y))) {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.city_id=pcity->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_CHANGE_HOMECITY);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_upgrade(struct unit *punit)
{
  struct city *pcity;

  if((pcity=map_get_city(punit->x, punit->y)))  {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.city_id=pcity->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_UPGRADE);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_auto(struct unit *punit)
{
  if (can_unit_do_auto(punit)) {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_AUTO);
  } else {
    append_output_window(_("Game: Only settler units and military units"
			   " in cities can be put in auto-mode."));
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_caravan_action(struct unit *punit, enum packet_type action)
{
  struct packet_unit_request req;
  struct city *pcity = map_get_city(punit->x, punit->y);

  if (!pcity) return;
  if (!(action==PACKET_UNIT_ESTABLISH_TRADE
	||(action==PACKET_UNIT_HELP_BUILD_WONDER))) {
    freelog(LOG_ERROR, "Bad action (%d) in request_unit_caravan_action",
	    action);
    return;
  }
  
  req.unit_id = punit->id;
  req.city_id = pcity->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, action);
}

/**************************************************************************
 Explode nuclear at a tile without enemy units
**************************************************************************/
void request_unit_nuke(struct unit *punit)
{
  if(!unit_flag(punit->type, F_NUCLEAR)) {
    append_output_window(_("Game: Only nuclear units can do this."));
    return;
  }
  if(!(punit->moves_left))
    do_unit_nuke(punit);
  else {
    set_hover_state(punit, HOVER_NUKE);
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_paradrop(struct unit *punit)
{
  if(!unit_flag(punit->type, F_PARATROOPERS)) {
    append_output_window(_("Game: Only paratrooper units can do this."));
    return;
  }
  if(!can_unit_paradrop(punit))
    return;

  set_hover_state(punit, HOVER_PARADROP);
  update_unit_info_label(punit);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_patrol(void)
{
  struct unit *punit = get_unit_in_focus();

  if (!punit || !has_capability("activity_patrol", aconnection.capability))
    return;

  if (hover_state != HOVER_PATROL) {
    set_hover_state(punit, HOVER_PATROL);
    update_unit_info_label(punit);
    /* Not yet implemented for air units */
    if (is_air_unit(punit)) {
      draw_goto_line = 0;
    } else {
      enter_goto_state(punit);
      create_line_at_mouse_pos();
    }
  } else {
    assert(goto_is_active());
    goto_add_waypoint();
  }
}

/**************************************************************************
 Toggle display of grid lines on the map
**************************************************************************/
void request_toggle_map_grid(void) 
{
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE) return;

  draw_map_grid^=1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city names
**************************************************************************/
void request_toggle_city_names(void)
{
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return;

  draw_city_names ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city productions
**************************************************************************/
void request_toggle_city_productions(void)
{
  if (get_client_state() != CLIENT_GAME_RUNNING_STATE)
    return;

  draw_city_productions ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
...
**************************************************************************/
void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo)
{
  int x, y, was_teleported;
  
  was_teleported=!is_tiles_adjacent(punit->x, punit->y, pinfo->x, pinfo->y);
  x=punit->x;
  y=punit->y;
  punit->x=-1;  /* focus hack - if we're moving the unit in focus, it wont
		 * be redrawn on top of the city */

  unit_list_unlink(&map_get_tile(x, y)->units, punit);

  if(!pinfo->carried)
    refresh_tile_mapcanvas(x, y, was_teleported);
  
  if(game.player_idx==punit->owner && punit->activity!=ACTIVITY_GOTO && 
     auto_center_on_unit &&
     !tile_visible_and_not_on_border_mapcanvas(pinfo->x, pinfo->y))
    center_tile_mapcanvas(pinfo->x, pinfo->y);

  if(!pinfo->carried && !was_teleported) {
    int dx=pinfo->x - x;
    if(dx>1) dx=-1;
    else if(dx<-1)
      dx=1;
    if(smooth_move_units)
      move_unit_map_canvas(punit, x, y, dx, pinfo->y - punit->y);
    refresh_tile_mapcanvas(x, y, 1);
  }
    
  punit->x=pinfo->x;
  punit->y=pinfo->y;
  punit->fuel=pinfo->fuel;
  punit->hp=pinfo->hp;
  unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

  for(y=punit->y-2; y<punit->y+3; ++y) { 
    if(y<0 || y>=map.ysize)
      continue;
    for(x=punit->x-2; x<punit->x+3; ++x) { 
      unit_list_iterate(map_get_tile(x, y)->units, pu)
	if(unit_flag(pu->type, F_PARTIAL_INVIS)) {
	  refresh_tile_mapcanvas(map_adjust_x(pu->x), y, 1);
	}
      unit_list_iterate_end
    }
  }
  
  if(!pinfo->carried && tile_is_known(punit->x,punit->y) == TILE_KNOWN)
    refresh_tile_mapcanvas(punit->x, punit->y, 1);

  if(get_unit_in_focus()==punit) update_menus();
}

/**************************************************************************
 Handles everything when the user clicked a tile
**************************************************************************/
void do_map_click(int xtile, int ytile)
{
  struct city *pcity = map_get_city(xtile, ytile);
  struct tile *ptile = map_get_tile(xtile, ytile);
  struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

  if (punit && hover_state != HOVER_NONE) {
    switch (hover_state) {
    case HOVER_NONE:
      abort(); /* well; shouldn't get here :) */
      break;
    case HOVER_GOTO:
      do_unit_goto(xtile, ytile);
      break;
    case HOVER_NUKE:
      if (3 * real_map_distance(punit->x, punit->y, xtile, ytile)
	  > punit->moves_left) {
        append_output_window(_("Game: Too far for this unit."));
      } else {
	do_unit_goto(xtile, ytile);
	/* note that this will be executed by the server after the goto */
	if (!pcity)
	  do_unit_nuke(punit);
      }
      break;
    case HOVER_PARADROP:
      do_unit_paradrop_to(punit, xtile, ytile);
      break;
    case HOVER_CONNECT:
      popup_unit_connect_dialog(punit, xtile, ytile);
      break;
    case HOVER_PATROL:
      do_unit_patrol_to(punit, xtile, ytile);
      break;	
    }
    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);
    return;
  }
  
  if (pcity && game.player_idx==pcity->owner) {
    popup_city_dialog(pcity, 0);
    return;
  }
  
  if(unit_list_size(&ptile->units)==1) {
    struct unit *punit=unit_list_get(&ptile->units, 0);
    if(game.player_idx==punit->owner) {
      if(can_unit_do_activity(punit, ACTIVITY_IDLE)) {
	request_unit_selected(punit);
      }
    }
  }
  else if(unit_list_size(&ptile->units)>=2) {
    unit_list_iterate(ptile->units, punit)
      if (punit->owner == game.player_idx) {
	popup_unit_select_dialog(ptile);
	return;
      }
    unit_list_iterate_end;
  }
}

/**************************************************************************
  Update unit icons (and arrow) in the information display, for specified
  punit as the active unit and other units on the same square.  In practice
  punit is almost always (or maybe strictly always?) the focus unit.
  
  Static vars store some info on current (ie previous) state, to avoid
  unnecessary redraws; initialise to "flag" values to always redraw first
  time.  In principle we _might_ need more info (eg ai.control, connecting),
  but in practice this is enough?
  
  Used to store unit_ids for below units, to use for callbacks (now done
  inside gui-dep set_unit_icon()), but even with ids here they would not
  be enough information to know whether to redraw -- instead redraw every
  time.  (Could store enough info to know, but is it worth it?)
**************************************************************************/
void update_unit_pix_label(struct unit *punit)
{
  static enum unit_activity prev_activity = ACTIVITY_UNKNOWN;
  static Unit_Type_id prev_unit_type = U_LAST;
  static int prev_hp = -1;	         /* or could store ihp cf tilespec.c */
  
  int i;
  
  if(punit) {
    if(punit->type != prev_unit_type
       || punit->activity != prev_activity
       || punit->hp != prev_hp) {
      set_unit_icon(-1, punit);
      prev_unit_type = punit->type;
      prev_activity = punit->activity;
      prev_hp = punit->hp;
    }

    i = 0;			/* index into unit_below_canvas */
    unit_list_iterate(map_get_tile(punit->x, punit->y)->units, aunit) {
      if (aunit != punit) {
	if (i < num_units_below) {
	  set_unit_icon(i, aunit);
	}
	i++;
      }
    }
    unit_list_iterate_end;
    
    if (i > num_units_below) {
      set_unit_icons_more_arrow(1);
    } else {
      set_unit_icons_more_arrow(0);
      for(; i < num_units_below; i++) {
	set_unit_icon(i, NULL);
      }
    }
  }
  else {
    prev_unit_type = U_LAST;
    prev_activity = ACTIVITY_UNKNOWN;
    prev_hp = -1;
    for(i=-1; i<num_units_below; i++) {
      set_unit_icon(i, NULL);
    }
    set_unit_icons_more_arrow(0);
  }
}


/**************************************************************************
 Finish the goto mode and let the unit which is stored in hover_unit move
 to a given location.
 returns 1 if goto mode was activated before calling this function
 otherwise 0 (then this function does nothing)
**************************************************************************/
void do_unit_goto(int x, int y)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

  if (!hover_unit || hover_state != HOVER_GOTO)
    return;

  if (punit) {
    if (!draw_goto_line) {
      send_goto_unit(punit, x, y);
    } else if (!has_capability("activity_patrol", aconnection.capability)){
      send_goto_unit(punit, x, y);
    } else {
      int dest_x, dest_y;
      draw_line(x, y);
      get_line_dest(&dest_x, &dest_y);
      if (dest_x == x && dest_y == y) {
	send_goto_route(punit);
      } else {
	append_output_window(_("Game: Didn't find a route to the destination!"));
      }
    }
  }

  set_hover_state(NULL, HOVER_NONE);
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void do_unit_nuke(struct unit *punit)
{
  struct packet_unit_request req;
 
  req.unit_id=punit->id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_NUKE);
}

/**************************************************************************
Paradrop to a location
**************************************************************************/
void do_unit_paradrop_to(struct unit *punit, int x, int y)
{
  struct packet_unit_request req;

  req.unit_id=punit->id;
  req.x = x;
  req.y = y;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_PARADROP_TO);
}
 
/**************************************************************************
Paradrop to a location
**************************************************************************/
void do_unit_patrol_to(struct unit *punit, int x, int y)
{
if (has_capability("activity_patrol", aconnection.capability)) {
  if (is_air_unit(punit)) {
    append_output_window(_("Game: Sorry, airunit patrol not yet implemented."));
    return;
  } else {
    int dest_x, dest_y;
    draw_line(x, y);
    get_line_dest(&dest_x, &dest_y);
    if (dest_x == x && dest_y == y) {
      send_patrol_route(punit);
    } else {
      append_output_window(_("Game: Didn't find a route to the destination!"));
    }
  }

  set_hover_state(NULL, HOVER_NONE);
} else {
  /* nothing */
}
}
 
/**************************************************************************
...
**************************************************************************/
void request_unit_wait(struct unit *punit)
{
  punit->focus_status=FOCUS_WAIT;
  if(punit==get_unit_in_focus()) {
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_move_done(void)
{
  if(get_unit_in_focus()) {
    get_unit_in_focus()->focus_status=FOCUS_DONE;
    advance_unit_focus();
    /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
  }
}

/**************************************************************************
...
**************************************************************************/
void request_center_focus_unit(void)
{
  struct unit *punit;
  
  if((punit=get_unit_in_focus()))
    center_tile_mapcanvas(punit->x, punit->y);
}

/**************************************************************************
...
**************************************************************************/
void key_cancel_action(void)
{
  int popped = 0;
  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
    popped = goto_pop_waypoint();

  if (hover_state != HOVER_NONE && !popped) {
    struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

    set_hover_state(NULL, HOVER_NONE);

    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_city_names_toggle(void)
{
  request_toggle_city_names();
}

/**************************************************************************
...
**************************************************************************/
void key_city_productions_toggle(void)
{
  request_toggle_city_productions();
}

/**************************************************************************
...
**************************************************************************/
void key_end_turn(void)
{
  if(!game.player_ptr->turn_done) {
    struct packet_generic_message gen_packet;
    gen_packet.message[0]='\0';
    send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
    set_turn_done_button_state(FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_map_grid_toggle(void)
{
  request_toggle_map_grid();
}

/**************************************************************************
...
**************************************************************************/
void key_move_north(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 0, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_north_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south_east(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 0, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_south_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_move_west(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, -1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_move_north_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_airbase(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_AIRBASE))
      request_new_unit_activity(punit_focus, ACTIVITY_AIRBASE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_attack(void)
{
  if(get_unit_in_focus())
    if(!unit_flag(punit_focus->type, F_SETTLERS) &&
       can_unit_do_auto(punit_focus))
      request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_explore(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_EXPLORE))
      request_new_unit_activity(punit_focus, ACTIVITY_EXPLORE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto_settle(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_SETTLERS) &&
       can_unit_do_auto(punit_focus))
      request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_city(void)
{
  if(get_unit_in_focus())
    request_unit_build_city(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_wonder(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_CARAVAN))
      request_unit_caravan_action(punit_focus, PACKET_UNIT_HELP_BUILD_WONDER);
}

/**************************************************************************
handle user pressing key for 'Connect' command
**************************************************************************/
void key_unit_connect(void)
{
  request_unit_connect();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_diplomat_actions(void)
{
  struct city *pcity;		/* need pcity->id */
  if(get_unit_in_focus()
     && is_diplomat_unit(punit_focus)
     && (pcity = map_get_city(punit_focus->x, punit_focus->y))
     && !diplomat_dialog_is_open()    /* confusing otherwise? */
     && diplomat_can_do_action(punit_focus, DIPLOMAT_ANY_ACTION,
			       punit_focus->x, punit_focus->y))
     process_diplomat_arrival(punit_focus, pcity->id);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_disband(void)
{
  if(get_unit_in_focus())
    request_unit_disband(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_done(void)
{
  request_unit_move_done();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fallout(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_FALLOUT))
      request_new_unit_activity(punit_focus, ACTIVITY_FALLOUT);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortify(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_FORTIFYING))
      request_new_unit_activity(punit_focus, ACTIVITY_FORTIFYING);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortress(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_FORTRESS))
      request_new_unit_activity(punit_focus, ACTIVITY_FORTRESS);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_goto(void)
{
  request_unit_goto();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_homecity(void)
{
  if(get_unit_in_focus())
    request_unit_change_homecity(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_irrigate(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_IRRIGATE))
      request_new_unit_activity(punit_focus, ACTIVITY_IRRIGATE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_mine(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_MINE))
      request_new_unit_activity(punit_focus, ACTIVITY_MINE);
}

/**************************************************************************
Explode nuclear at a tile without enemy units
**************************************************************************/
void key_unit_nuke(void)
{
  if(get_unit_in_focus())
    request_unit_nuke(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_paradrop(void)
{
  if(get_unit_in_focus())
    if(can_unit_paradrop(punit_focus))
      request_unit_paradrop(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_patrol(void)
{
  request_unit_patrol();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pillage(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_PILLAGE))
      request_unit_pillage(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pollution(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_POLLUTION))
      request_new_unit_activity(punit_focus, ACTIVITY_POLLUTION);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_road(void)
{
  if(get_unit_in_focus()) {
    if(can_unit_do_activity(punit_focus, ACTIVITY_ROAD))
      request_new_unit_activity(punit_focus, ACTIVITY_ROAD);
    else if(can_unit_do_activity(punit_focus, ACTIVITY_RAILROAD))
      request_new_unit_activity(punit_focus, ACTIVITY_RAILROAD);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_sentry(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_SENTRY))
      request_new_unit_activity(punit_focus, ACTIVITY_SENTRY);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_traderoute(void)
{
  if(get_unit_in_focus())
    if(unit_flag(punit_focus->type, F_CARAVAN))
      request_unit_caravan_action(punit_focus, PACKET_UNIT_ESTABLISH_TRADE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_transform(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_TRANSFORM))
      request_new_unit_activity(punit_focus, ACTIVITY_TRANSFORM);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_unload(void)
{
  if(get_unit_in_focus())
    request_unit_unload(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_wait(void)
{
  if(get_unit_in_focus())
    request_unit_wait(punit_focus);
}

/**************************************************************************
...
***************************************************************************/
void key_unit_wakeup_others(void)
{
  if(get_unit_in_focus())
    request_unit_wakeup(punit_focus);
}
