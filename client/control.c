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

#include "capability.h"
#include "fcintl.h"
#include "log.h"

#include "chatline_g.h"
#include "citydlg_g.h"
#include "dialogs_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "menu_g.h"

#include "civclient.h"
#include "options.h"

#include "control.h"

extern struct connection aconnection;

/* unit_focus points to the current unit in focus */
struct unit *punit_focus;

/* set high, if the player has selected goto */
/* actually, set to id of unit goto-ing (id is non-zero) */
int goto_state;

/* set high, if the player has selected nuke */
int nuke_state;

/* set high, if the player has selected paradropping */
int paradrop_state;

static struct unit *find_best_focus_candidate(void);

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
    raise_unit_top(punit);
    if(auto_center_on_unit && 
       !tile_visible_and_not_on_border_mapcanvas(punit->x, punit->y))
      center_tile_mapcanvas(punit->x, punit->y);

    punit->focus_status=FOCUS_AVAIL;
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
    put_cross_overlay_tile(punit->x, punit->y);
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
    raise_unit_top(punit);
    refresh_tile_mapcanvas(punit->x, punit->y, 1);
    punit->focus_status=FOCUS_AVAIL;
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

  goto_state=0;
  nuke_state=0;
  paradrop_state=0;

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
  };
    
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
...
**************************************************************************/
void request_unit_goto(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if(punit) {
    goto_state=punit->id;
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
    append_output_window(_("Game: You can only unload transporter units."));
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
...

  No need to do caravan-specific stuff here any more: server does trade
  routes to enemy cities automatically, and for friendly cities we wait
  for the unit to enter the city.  --dwp
  
**************************************************************************/
void request_move_unit_direction(struct unit *punit, int dx, int dy)
{
  int dest_x, dest_y;
  struct unit req_unit;

  dest_x=map_adjust_x(punit->x+dx);
  dest_y=punit->y+dy;   /* not adjusting on purpose*/   /* why? --dwp */
 
  if(unit_flag(punit->type, F_DIPLOMAT) &&
          is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION,
                                       dest_x, dest_y)) {
    if (diplomat_can_do_action(punit, DIPLOMAT_ANY_ACTION, dest_x, dest_y)) {
      popup_diplomat_dialog(punit, dest_x, dest_y);
    } else {
      append_output_window(_("Game: You don't have enough movement left"));
    }
    return;
  }
  
  req_unit=*punit;
  req_unit.x=dest_x;
  req_unit.y=dest_y;
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

  if ((game.civstyle==2) &&
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
    append_output_window(_("Game: Only settlers, and military units"
			 " in cities, can be put in auto-mode."));
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
    freelog(LOG_NORMAL, "Bad action (%d) in request_unit_caravan_action",
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
    nuke_state=1;
    goto_state=punit->id;
    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void request_unit_paradrop(struct unit *punit)
{
  if(!unit_flag(punit->type, F_PARATROOPERS)) {
    append_output_window("Game: Only paratrooper units can do this.");
    return;
  }
  if(!can_unit_paradropped(punit))
    return;

  paradrop_state=1;
  goto_state=punit->id;
  update_unit_info_label(punit);
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
...
**************************************************************************/
void do_move_unit(struct unit *punit, struct packet_unit_info *pinfo)
{
  int x, y, was_carried, was_teleported;
     
  was_carried=(pinfo->movesleft==punit->moves_left && 
	       (map_get_terrain(punit->x, punit->y)==T_OCEAN ||
		map_get_terrain(pinfo->x, pinfo->y)==T_OCEAN));
  
  was_teleported=!is_tiles_adjacent(punit->x, punit->y, pinfo->x, pinfo->y);
  x=punit->x;
  y=punit->y;
  punit->x=-1;  /* focus hack - if we're moving the unit in focus, it wont
		 * be redrawn on top of the city */

  unit_list_unlink(&map_get_tile(x, y)->units, punit);

  if(!was_carried)
    refresh_tile_mapcanvas(x, y, was_teleported);
  
  if(game.player_idx==punit->owner && punit->activity!=ACTIVITY_GOTO && 
     auto_center_on_unit &&
     !tile_visible_and_not_on_border_mapcanvas(pinfo->x, pinfo->y))
    center_tile_mapcanvas(pinfo->x, pinfo->y);

  if(!was_carried && !was_teleported) {
    int dx=pinfo->x - x;
    if(dx>1) dx=-1;
    else if(dx<-1)
      dx=1;
    if(smooth_move_units)
      move_unit_map_canvas(punit, x, y, dx, pinfo->y - punit->y);
    /*else*/
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
	if(unit_flag(pu->type, F_SUBMARINE)) {
	  refresh_tile_mapcanvas(map_adjust_x(pu->x), y, 1);
	}
      unit_list_iterate_end
    }
  }
  
  if(!was_carried)
    refresh_tile_mapcanvas(punit->x, punit->y, 1);

  if(get_unit_in_focus()==punit) update_menus();
}

/**************************************************************************
 Finish the goto mode and let the unit which is stored in goto_state move
 to a given location.
 returns 1 if goto mode was activated before calling this function
 otherwise 0 (then this function does nothing)
**************************************************************************/
int do_goto(int xtile, int ytile)
{
  if(goto_state) {
    struct unit *punit;

    if((punit=unit_list_find(&game.player_ptr->units, goto_state))) {
      struct packet_unit_request req;
      req.unit_id=punit->id;
      req.name[0]='\0';
      req.x=xtile;
      req.y=ytile;
      send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
    }

    goto_state=0;
    nuke_state=0;
    paradrop_state=0;

    return 1;
  }
  return 0;
}

/**************************************************************************
 Handles everything when the user clicked a tile
**************************************************************************/
void do_map_click(int xtile, int ytile)
{
  struct city *pcity;
  struct tile *ptile;

  ptile=map_get_tile(xtile, ytile);
  pcity=map_get_city(xtile, ytile);

  if(goto_state) { 
    struct unit *punit;

    if((punit=unit_list_find(&game.player_ptr->units, goto_state))) {
      struct packet_unit_request req;

      if(paradrop_state) {
        do_unit_paradrop_to(punit, xtile, ytile);
        goto_state=0;
        nuke_state=0;
        paradrop_state=0;
        return;
      }

      if(nuke_state && 3*real_map_distance(punit->x,punit->y,xtile,ytile) > punit->moves_left) {
        append_output_window(_("Game: Too far for this unit."));
        goto_state=0;
        nuke_state=0;
        update_unit_info_label(punit);
      } else {
        req.unit_id=punit->id;
        req.name[0]='\0';
        req.x=xtile;
        req.y=ytile;
        send_packet_unit_request(&aconnection, &req, PACKET_UNIT_GOTO_TILE);
        if(nuke_state && (!pcity))
          do_unit_nuke(punit);
        goto_state=0;
        nuke_state=0;
      }
    }
    return;
  }
  
  if(pcity && game.player_idx==pcity->owner) {
    popup_city_dialog(pcity, 0);
    return;
  }
  
  if(unit_list_size(&ptile->units)==1) {
    struct unit *punit=unit_list_get(&ptile->units, 0);
    if(game.player_idx==punit->owner) {
      if(can_unit_do_activity(punit, ACTIVITY_IDLE)) {
        /* struct unit *old_focus=get_unit_in_focus(); */
        request_new_unit_activity(punit, ACTIVITY_IDLE);
        /* this is now done in set_unit_focus: --dwp */
        /* if(old_focus)
             refresh_tile_mapcanvas(old_focus->x, old_focus->y, 1); */
        set_unit_focus(punit);
      }
    }
  }
  else if(unit_list_size(&ptile->units)>=2) {
    if(unit_list_get(&ptile->units, 0)->owner==game.player_idx)
      popup_unit_select_dialog(ptile);
  }
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
void key_unit_disband(void)
{
  struct unit *punit=get_unit_in_focus();
  
  if(punit)
    request_unit_disband(punit);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_homecity(void)
{
  struct unit *punit=get_unit_in_focus();
  
  if(punit)
    request_unit_change_homecity(punit);
}

/**************************************************************************
...
**************************************************************************/
void key_end_turn(void)
{
  if(/*!get_unit_in_focus() &&*/ !game.player_ptr->turn_done) {
      struct packet_generic_message gen_packet;
    gen_packet.message[0]='\0';
    send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
    set_turn_done_button_state(FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_cancel_action(void)
{
  if(goto_state) {
    struct unit *punit;

    punit=unit_list_find(&game.player_ptr->units, goto_state);

    goto_state=0;
    nuke_state=0;
    paradrop_state=0;

    update_unit_info_label(punit);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_fortify(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if(punit) {
    if(unit_flag(punit->type, F_SETTLERS))
      request_new_unit_activity(punit_focus, ACTIVITY_FORTRESS);
    else 
      request_new_unit_activity(punit_focus, ACTIVITY_FORTIFY);
  }
 
}

/**************************************************************************
...
**************************************************************************/
void key_unit_airbase(void)
{
  struct unit *punit=get_unit_in_focus();
     
  if(punit) {
    if(unit_flag(punit->type, F_AIRBASE))
      request_new_unit_activity(punit_focus, ACTIVITY_AIRBASE);
  }
 
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
void key_unit_build_city(void)
{
  struct unit *punit=get_unit_in_focus();
  if (!punit) return;
  if (unit_flag(punit->type, F_CARAVAN)) {
    request_unit_caravan_action(punit, PACKET_UNIT_HELP_BUILD_WONDER);
  } else {
    request_unit_build_city(punit);
  }
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
+**************************************************************************/
void key_unit_wakeup(void)
{
  if(get_unit_in_focus())
    request_unit_wakeup(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_sentry(void)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_SENTRY);
}

/**************************************************************************
...
**************************************************************************/
void key_map_grid(void)
{
  request_toggle_map_grid();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_clean_pollution(void)
{
  struct unit *punit=get_unit_in_focus();
  
  if (!punit) return;

  if (unit_flag(punit->type, F_PARATROOPERS)) {
    request_unit_paradrop(punit);
  }
  else {
    request_new_unit_activity(punit, ACTIVITY_POLLUTION);
  }

}

/**************************************************************************
...
**************************************************************************/
void key_unit_pillage(void)
{
  if(get_unit_in_focus())
    request_unit_pillage(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_explore(void)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_EXPLORE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_wait(void)
{
  if(get_unit_in_focus())
    request_unit_wait(get_unit_in_focus());
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
void key_unit_mine(void)
{
  if(get_unit_in_focus())
      request_new_unit_activity(punit_focus, ACTIVITY_MINE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_transform(void)
{
  if(get_unit_in_focus())
      request_new_unit_activity(punit_focus, ACTIVITY_TRANSFORM);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_road(void)
{
  struct unit *punit=get_unit_in_focus();
  
  if (!punit) return;

  if (unit_flag(punit->type, F_CARAVAN)) {
    request_unit_caravan_action(punit, PACKET_UNIT_ESTABLISH_TRADE);
  }
  else {
    if(can_unit_do_activity(punit, ACTIVITY_ROAD))
      request_new_unit_activity(punit, ACTIVITY_ROAD);
    else if(can_unit_do_activity(punit, ACTIVITY_RAILROAD))
      request_new_unit_activity(punit, ACTIVITY_RAILROAD);
  }
}

/**************************************************************************
...
**************************************************************************/
void key_unit_irrigate(void)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_IRRIGATE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto(void)
{
  if(get_unit_in_focus())
    request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 0, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_east(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south_east(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 0, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_west(void)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, -1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north_west(void)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, -1);
}

