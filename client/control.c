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

#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "mem.h"

#include "audio.h"
#include "chatline_g.h"
#include "citydlg_g.h"
#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "clinet.h"
#include "dialogs_g.h"
#include "goto.h"
#include "gui_main_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"
#include "options.h"
#include "tilespec.h"

#include "control.h"

/* gui-dep code may adjust depending on tile size etc: */
int num_units_below = MAX_NUM_UNITS_BELOW;

/* unit_focus points to the current unit in focus */
static struct unit *punit_focus;

/* The previously focused unit.  Focus can generally be recalled on this
 * unit with keypad 5.  FIXME: this is not reset when the client
 * disconnects. */
static int previous_focus_id = -1;

/* These should be set via set_hover_state() */
int hover_unit = 0; /* id of unit hover_state applies to */
enum cursor_hover_state hover_state = HOVER_NONE;
/* This may only be here until client goto is fully implemented.
   It is reset each time the hower_state is reset. */
bool draw_goto_line = TRUE;

/* units involved in current combat */
static struct unit *punit_attacking = NULL;
static struct unit *punit_defending = NULL;

/*
 * This variable is TRUE iff a NON-AI controlled unit was focused this
 * turn.
 */
bool non_ai_unit_focus;

/*************************************************************************/

static struct unit *find_best_focus_candidate(void);
static void store_focus(void);

/**************************************************************************
...
**************************************************************************/
void set_hover_state(struct unit *punit, enum cursor_hover_state state)
{
  assert(punit || state==HOVER_NONE);
  draw_goto_line = TRUE;
  if (punit)
    hover_unit = punit->id;
  else
    hover_unit = 0;
  hover_state = state;
  exit_goto_state();
}

/**************************************************************************
Center on the focus unit, if off-screen and auto_center_on_unit is true.
**************************************************************************/
void auto_center_on_focus_unit(void)
{
  if (punit_focus && auto_center_on_unit &&
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

  if (punit != punit_focus) {
    store_focus();
  }

  punit_focus=punit;

  if(punit) {
    auto_center_on_focus_unit();

    punit->focus_status=FOCUS_AVAIL;
    refresh_tile_mapcanvas(punit->x, punit->y, FALSE);

    if (punit->activity != ACTIVITY_IDLE || punit->ai.control)  {
      punit->activity = ACTIVITY_IDLE;
      punit->ai.control = FALSE;
      refresh_unit_city_dialogs(punit);
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  }
  
  /* avoid the old focus unit disappearing: */
  if (punit_old_focus
      && (!punit || !same_pos(punit_old_focus->x, punit_old_focus->y,
				   punit->x, punit->y))) {
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, FALSE);
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
  if (punit != punit_focus) {
    store_focus();
  }

  punit_focus=punit;

  if(punit) {
    refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
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
  Store the focus unit.  This is used so that we can return to the
  previously focused unit with an appropriate keypress.
**************************************************************************/
static void store_focus(void)
{
  if (punit_focus) {
    previous_focus_id = punit_focus->id;
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
  if(!punit_focus
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

  store_focus();
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
  if(punit_old_focus && punit_old_focus!=punit_focus)
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, FALSE);

  set_unit_focus(punit_focus);

  /* 
   * Is the unit which just lost focus a non-AI unit? If yes this
   * enables the auto end turn. 
   */
  if (punit_old_focus && !punit_old_focus->ai.control) {
    non_ai_unit_focus = TRUE;
  }

  /* 
   * Handle auto-turn-done mode: If a unit was in focus (did move),
   * but now none are (no more to move) and there was at least one
   * non-AI unit this turn which was focused, then fake a Turn Done
   * keypress.
   */
  if (auto_turn_done && punit_old_focus && !punit_focus && non_ai_unit_focus) {
    key_end_turn();
  }
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
	 punit->moves_left > 0 && !punit->ai.control) {
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
      if (get_transporter_capacity(punit) > 0) {
	return punit;
      } else if (!panyowned) {
	panyowned = punit;
      }
    } else if (!ptptother &&
	       player_can_see_unit(game.player_ptr, punit)) {
      if (get_transporter_capacity(punit) > 0) {
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
void blink_active_unit(void)
{
  static bool is_shown;
  static struct unit *pblinking_unit;
  struct unit *punit;

  if((punit=get_unit_in_focus())) {
    if (punit != pblinking_unit) {
      /* When the focus unit changes, we reset the is_shown flag. */
      pblinking_unit = punit;
      is_shown = TRUE;
    }
    if(is_shown) {
      set_focus_unit_hidden_state(TRUE);
      refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
      set_focus_unit_hidden_state(FALSE);
    } else {
      refresh_tile_mapcanvas(punit->x, punit->y, TRUE);
    }
    is_shown=!is_shown;
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
  
  if (punit && get_client_state() != CLIENT_GAME_OVER_STATE) {
    if (punit->type != prev_unit_type
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
      set_unit_icons_more_arrow(TRUE);
    } else {
      set_unit_icons_more_arrow(FALSE);
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
    set_unit_icons_more_arrow(FALSE);
  }
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
  Add punit to queue of caravan arrivals, and popup a window for the
  next arrival in the queue, if there is not already a popup, and
  re-checking that a popup is appropriate.
  If punit is NULL, just do for the next arrival in the queue.
**************************************************************************/
void process_caravan_arrival(struct unit *punit)
{
  static struct genlist arrival_queue;
  static bool is_init_arrival_queue = FALSE;
  int *p_id;

  /* arrival_queue is a list of individually malloc-ed ints with
     punit.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = TRUE;
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
  
  while (genlist_size(&arrival_queue) > 0) {
    int id;
    
    p_id = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_id);
    id = *p_id;
    free(p_id);
    p_id = NULL;
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
  static bool is_init_arrival_queue = FALSE;
  int *p_ids;

  /* arrival_queue is a list of individually malloc-ed int[2]s with
     punit.id and pcity.id values, for units which have arrived. */

  if (!is_init_arrival_queue) {
    genlist_init(&arrival_queue);
    is_init_arrival_queue = TRUE;
  }

  if (pdiplomat && victim_id != 0) {
    p_ids = fc_malloc(2*sizeof(int));
    p_ids[0] = pdiplomat->id;
    p_ids[1] = victim_id;
    genlist_insert(&arrival_queue, p_ids, -1);
  }

  /* There can only be one dialog at a time: */
  if (diplomat_dialog_is_open()) {
    return;
  }

  while (genlist_size(&arrival_queue) > 0) {
    int diplomat_id, victim_id;
    struct city *pcity;
    struct unit *punit;

    p_ids = genlist_get(&arrival_queue, 0);
    genlist_unlink(&arrival_queue, p_ids);
    diplomat_id = p_ids[0];
    victim_id = p_ids[1];
    free(p_ids);
    p_ids = NULL;
    pdiplomat = player_find_unit_by_id(game.player_ptr, diplomat_id);
    pcity = find_city_by_id(victim_id);
    punit = find_unit_by_id(victim_id);

    if (!pdiplomat || !unit_flag(pdiplomat, F_DIPLOMAT))
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
    /* Not yet implemented for air units, including helicopters. */
    if (is_air_unit(punit) || is_heli_unit(punit)) {
      draw_goto_line = FALSE;
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

  if(get_transporter_capacity(punit) == 0) {
    append_output_window(_("Game: Only transporter units can be unloaded."));
    return;
  }

  request_unit_wait(punit);    /* RP: unfocus the ship */
  
  req.unit_id=punit->id;
  req.city_id = req.x = req.y = -1;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_UNLOAD);
}

/**************************************************************************
...
**************************************************************************/
void request_unit_airlift(struct unit *punit, struct city *pcity)
{
  struct packet_unit_request p;
  p.unit_id = punit->id;
  p.city_id = -1;
  p.x = pcity->x;
  p.y = pcity->y;
  p.name[0] = '\0';
  send_packet_unit_request(&aconnection, &p, PACKET_UNIT_AIRLIFT);
}

/**************************************************************************
  Return-and-recover for a particular unit.  This sets the unit to GOTO
  the nearest city.
**************************************************************************/
void request_unit_return(struct unit *punit)
{
  struct city *pcity;

  enter_goto_state(punit);

  if ((pcity = find_nearest_allied_city(punit))) {
    draw_line(pcity->x, pcity->y);
    send_goto_route(punit);
  }

  exit_goto_state();
}

/**************************************************************************
(RP:) un-sentry all my own sentried units on punit's tile
**************************************************************************/
void request_unit_wakeup(struct unit *punit)
{
  wakeup_sentried_units(punit->x,punit->y);
}

/**************************************************************************
  Request a diplomat to do a specific action.
  - action : The action to be requested.
  - dipl_id : The unit ID of the diplomatic unit.
  - target_id : The ID of the target unit or city.
  - value : For DIPLOMAT_STEAL or DIPLOMAT_SABOTAGE, the technology
            or building to aim for (spies only).
**************************************************************************/
void request_diplomat_action(enum diplomat_actions action, int dipl_id,
			     int target_id, int value)
{
  struct packet_diplomat_action req;

  req.action_type = action;
  req.diplomat_id = dipl_id;
  req.target_id = target_id;
  req.value = value;

  send_packet_diplomat_action(&aconnection, &req);
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
    req.city_id = req.x = req.y = -1;
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
void request_move_unit_direction(struct unit *punit, int dir)
{
  int dest_x, dest_y;
  struct unit req_unit;

  /* Catches attempts to move off map */
  if (!MAPSTEP(dest_x, dest_y, punit->x, punit->y, dir)) {
    return;
  }

  if (punit->moves_left > 0) {
    /* Move the unit! */
    req_unit = *punit;
    req_unit.x = dest_x;
    req_unit.y = dest_y;
    send_move_unit(&req_unit);
  } else {
    /* Initiate a "goto" with direction keys for exhausted units. */
    send_goto_unit(punit, dest_x, dest_y);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity(struct unit *punit, enum unit_activity act)
{
  struct unit req_unit;
  req_unit=*punit;
  req_unit.activity=act;
  req_unit.activity_target = S_NO_SPECIAL;
  send_unit_info(&req_unit);
}

/**************************************************************************
...
**************************************************************************/
void request_new_unit_activity_targeted(struct unit *punit, enum unit_activity act,
					enum tile_special_type tgt)
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
void request_unit_disband(struct unit *punit)
{
  struct packet_unit_request req;
  req.unit_id=punit->id;
  req.city_id = req.x = req.y = -1;
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
    req.x = req.y = -1;
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
    req.x = req.y = -1;
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
    req.x = req.y = -1;
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
  req.x = req.y = -1;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, action);
}

/**************************************************************************
 Explode nuclear at a tile without enemy units
**************************************************************************/
void request_unit_nuke(struct unit *punit)
{
  if(!unit_flag(punit, F_NUCLEAR)) {
    append_output_window(_("Game: Only nuclear units can do this."));
    return;
  }
  if(punit->moves_left == 0)
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
  if(!unit_flag(punit, F_PARATROOPERS)) {
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

  if (!punit)
    return;

  if (hover_state != HOVER_PATROL) {
    set_hover_state(punit, HOVER_PATROL);
    update_unit_info_label(punit);
    /* Not yet implemented for air units, including helicopters. */
    if (is_air_unit(punit) || is_heli_unit(punit)) {
      draw_goto_line = FALSE;
    } else {
      enter_goto_state(punit);
      create_line_at_mouse_pos();
    }
  } else {
    assert(goto_is_active());
    goto_add_waypoint();
  }
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
  struct tile *ptile = map_get_tile(punit->x, punit->y);
  enum tile_special_type pspresent = get_tile_infrastructure_set(ptile);
  enum tile_special_type psworking =
      get_unit_tile_pillage_set(punit->x, punit->y);
  enum tile_special_type what =
      get_preferred_pillage(pspresent & (~psworking));
  enum tile_special_type would =
      what | map_get_infrastructure_prerequisite(what);

  if ((game.rgame.pillage_select) &&
      ((pspresent & (~(psworking | would))) != S_NO_SPECIAL)) {
    popup_pillage_dialog(punit, (pspresent & (~psworking)));
  } else {
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, what);
  }
}

/**************************************************************************
 Toggle display of grid lines on the map
**************************************************************************/
void request_toggle_map_grid(void) 
{
  if (!can_client_change_view()) {
    return;
  }

  draw_map_grid^=1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city names
**************************************************************************/
void request_toggle_city_names(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_names ^= 1;
  update_map_canvas_visible();
}
 
 /**************************************************************************
 Toggle display of city growth (turns-to-grow)
**************************************************************************/
void request_toggle_city_growth(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_growth ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of city productions
**************************************************************************/
void request_toggle_city_productions(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_city_productions ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of terrain
**************************************************************************/
void request_toggle_terrain(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_terrain ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of coastline
**************************************************************************/
void request_toggle_coastline(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_coastline ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of roads and rails
**************************************************************************/
void request_toggle_roads_rails(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_roads_rails ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of irrigation
**************************************************************************/
void request_toggle_irrigation(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_irrigation ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of mines
**************************************************************************/
void request_toggle_mines(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_mines ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of fortress and airbase
**************************************************************************/
void request_toggle_fortress_airbase(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_fortress_airbase ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of specials
**************************************************************************/
void request_toggle_specials(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_specials ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of pollution
**************************************************************************/
void request_toggle_pollution(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_pollution ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of cities
**************************************************************************/
void request_toggle_cities(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_cities ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of units
**************************************************************************/
void request_toggle_units(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_units ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of focus unit
**************************************************************************/
void request_toggle_focus_unit(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_focus_unit ^= 1;
  update_map_canvas_visible();
}

/**************************************************************************
 Toggle display of fog of war
**************************************************************************/
void request_toggle_fog_of_war(void)
{
  if (!can_client_change_view()) {
    return;
  }

  draw_fog_of_war ^= 1;
  update_map_canvas_visible();
  refresh_overview_canvas();
  refresh_overview_viewrect();
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
void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo)
{
  int x, y;
  bool was_teleported;
  
  was_teleported=!is_tiles_adjacent(punit->x, punit->y, pinfo->x, pinfo->y);
  x=punit->x;
  y=punit->y;

  if (!was_teleported && punit->activity != ACTIVITY_SENTRY && !pinfo->carried) {
    audio_play_sound(unit_type(punit)->sound_move,
		     unit_type(punit)->sound_move_alt);
  }

  unit_list_unlink(&map_get_tile(x, y)->units, punit);

  if(!pinfo->carried)
    refresh_tile_mapcanvas(x, y, FALSE);
  
  if(game.player_idx==punit->owner && punit->activity!=ACTIVITY_GOTO && 
     auto_center_on_unit && punit->activity!=ACTIVITY_SENTRY &&
     !tile_visible_and_not_on_border_mapcanvas(pinfo->x, pinfo->y))
    center_tile_mapcanvas(pinfo->x, pinfo->y);

  if(!pinfo->carried && !was_teleported) {
    int dx, dy;

    map_distance_vector(&dx, &dy, punit->x, punit->y, pinfo->x, pinfo->y);
    if(smooth_move_units)
      move_unit_map_canvas(punit, x, y, dx, dy);
    refresh_tile_mapcanvas(x, y, FALSE);
  }
    
  punit->x=pinfo->x;
  punit->y=pinfo->y;
  punit->fuel=pinfo->fuel;
  punit->hp=pinfo->hp;
  unit_list_insert(&map_get_tile(punit->x, punit->y)->units, punit);

  square_iterate(punit->x, punit->y, 2, x, y) {
    bool refresh = FALSE;
    unit_list_iterate(map_get_tile(x, y)->units, pu) {
      if (unit_flag(pu, F_PARTIAL_INVIS)) {
	refresh = TRUE;
	goto out;
      }
    } unit_list_iterate_end;
  out:
    if (refresh) {
      refresh_tile_mapcanvas(x, y, FALSE);
    }
  } square_iterate_end;
  
  if(!pinfo->carried && tile_get_known(punit->x,punit->y) == TILE_KNOWN)
    refresh_tile_mapcanvas(punit->x, punit->y, FALSE);

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
      die("well; shouldn't get here :)");
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
    popup_city_dialog(pcity, FALSE);
    return;
  }
  
  if(unit_list_size(&ptile->units) == 1) {
    struct unit *punit=unit_list_get(&ptile->units, 0);
    if(game.player_idx==punit->owner) {
      if(can_unit_do_activity(punit, ACTIVITY_IDLE)) {
	set_unit_focus_and_select(punit);
      }
    }
  } else if(unit_list_size(&ptile->units) >= 2) {
    unit_list_iterate(ptile->units, punit)
      if (punit->owner == game.player_idx) {
	popup_unit_select_dialog(ptile);
	return;
      }
    unit_list_iterate_end;
  }
}

/**************************************************************************
 Finish the goto mode and let the unit which is stored in hover_unit move
 to a given location.
**************************************************************************/
void do_unit_goto(int x, int y)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

  if (hover_unit == 0 || hover_state != HOVER_GOTO)
    return;

  if (punit) {
    if (!draw_goto_line) {
      send_goto_unit(punit, x, y);
    } else {
      int dest_x, dest_y;
      draw_line(x, y);
      get_line_dest(&dest_x, &dest_y);
      if (same_pos(dest_x, dest_y, x, y)) {
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
  req.city_id = req.x = req.y = -1;
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
  req.city_id = -1;
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
  if (is_air_unit(punit)) {
    append_output_window(_("Game: Sorry, airunit patrol not yet implemented."));
    return;
  } else {
    int dest_x, dest_y;
    draw_line(x, y);
    get_line_dest(&dest_x, &dest_y);
    if (same_pos(dest_x, dest_y, x, y)) {
      send_patrol_route(punit);
    } else {
      append_output_window(_("Game: Didn't find a route to the destination!"));
    }
  }

  set_hover_state(NULL, HOVER_NONE);
}
 
/**************************************************************************
...
**************************************************************************/
void key_cancel_action(void)
{
  bool popped = FALSE;
  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL)
    if (draw_goto_line)
      popped = goto_pop_waypoint();

  if (hover_state != HOVER_NONE && !popped) {
    struct unit *punit = player_find_unit_by_id(game.player_ptr, hover_unit);

    set_hover_state(NULL, HOVER_NONE);

    update_unit_info_label(punit);
  }
}

/**************************************************************************
  Center the mapview on the player's capital, or print a failure message.
**************************************************************************/
void key_center_capital(void)
{
  struct city *capital = find_palace(game.player_ptr);

  if (capital)  {
    /* Center on the tile, and pop up the crosshair overlay. */
    center_tile_mapcanvas(capital->x, capital->y);
    put_cross_overlay_tile(capital->x, capital->y);
  } else {
    append_output_window(_("Game: Oh my! You seem to have no capital!"));
  }
}

/**************************************************************************
...
**************************************************************************/
void key_end_turn(void)
{
  send_turn_done();
}

/**************************************************************************
  Recall the previous focus unit and focus on it.  See store_focus().
**************************************************************************/
void key_recall_previous_focus_unit(void)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr,
                                              previous_focus_id);
  if (punit) {
    set_unit_focus_and_select(punit);
  }
}

/**************************************************************************
  Move the focus unit in the given direction.  Here directions are
  defined according to the GUI, so that north is "up" in the interface.
**************************************************************************/
void key_unit_move(enum direction8 gui_dir)
{
  if (get_unit_in_focus()) {
    enum direction8 map_dir = gui_to_map_dir(gui_dir);
    request_move_unit_direction(get_unit_in_focus(), map_dir);
  }
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
  if (get_unit_in_focus() && unit_flag(punit_focus, F_HELP_WONDER))
    request_unit_caravan_action(punit_focus, PACKET_UNIT_HELP_BUILD_WONDER);
}

/**************************************************************************
handle user pressing key for 'Connect' command
**************************************************************************/
void key_unit_connect(void)
{
  if(get_unit_in_focus())
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
void key_unit_done(void)
{
  if(get_unit_in_focus())
    request_unit_move_done();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_goto(void)
{
  if(get_unit_in_focus())
    request_unit_goto();
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
  if(get_unit_in_focus())
    request_unit_patrol();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_traderoute(void)
{
  if (get_unit_in_focus() && unit_flag(punit_focus, F_TRADE_ROUTE))
    request_unit_caravan_action(punit_focus, PACKET_UNIT_ESTABLISH_TRADE);
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
    if(!unit_flag(punit_focus, F_SETTLERS) &&
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
    if(unit_flag(punit_focus, F_SETTLERS) &&
       can_unit_do_auto(punit_focus))
      request_unit_auto(punit_focus);
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
void key_unit_transform(void)
{
  if(get_unit_in_focus())
    if(can_unit_do_activity(punit_focus, ACTIVITY_TRANSFORM))
      request_new_unit_activity(punit_focus, ACTIVITY_TRANSFORM);
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
void key_city_names_toggle(void)
{
  request_toggle_city_names();
}

/**************************************************************************
  Toggles the "show city growth turns" option by passing off the
  request to another function...
**************************************************************************/
void key_city_growth_toggle(void)
{
  request_toggle_city_growth();
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
void key_terrain_toggle(void)
{
  request_toggle_terrain();
}

/**************************************************************************
...
**************************************************************************/
void key_coastline_toggle(void)
{
  request_toggle_coastline();
}

/**************************************************************************
...
**************************************************************************/
void key_roads_rails_toggle(void)
{
  request_toggle_roads_rails();
}

/**************************************************************************
...
**************************************************************************/
void key_irrigation_toggle(void)
{
  request_toggle_irrigation();
}

/**************************************************************************
...
**************************************************************************/
void key_mines_toggle(void)
{
  request_toggle_mines();
}

/**************************************************************************
...
**************************************************************************/
void key_fortress_airbase_toggle(void)
{
  request_toggle_fortress_airbase();
}

/**************************************************************************
...
**************************************************************************/
void key_specials_toggle(void)
{
  request_toggle_specials();
}

/**************************************************************************
...
**************************************************************************/
void key_pollution_toggle(void)
{
  request_toggle_pollution();
}

/**************************************************************************
...
**************************************************************************/
void key_cities_toggle(void)
{
  request_toggle_cities();
}

/**************************************************************************
...
**************************************************************************/
void key_units_toggle(void)
{
  request_toggle_units();
}

/**************************************************************************
...
**************************************************************************/
void key_focus_unit_toggle(void)
{
  request_toggle_focus_unit();
}

/**************************************************************************
...
**************************************************************************/
void key_fog_of_war_toggle(void)
{
  request_toggle_fog_of_war();
}
