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

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>  
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/SmeLine.h>

#include <xstuff.h>
#include <civclient.h>
#include <unit.h>
#include <game.h>
#include <player.h>
#include <mapview.h>
#include <mapctrl.h>
#include <map.h>
#include <dialogs.h>
#include <citydlg.h>
#include <clinet.h>
#include <xstuff.h>
#include <inputdlg.h>
#include <chatline.h>
#include <menu.h>
#include <graphics.h>
#include <colors.h>

extern Display	*display;
extern GC fill_tile_gc;
extern Pixmap gray50,gray25;

extern int map_view_x0, map_view_y0;
extern int map_canvas_store_twidth, map_canvas_store_theight;
extern int overview_canvas_store_width, overview_canvas_store_height;

/* unit_focus points to the current unit in focus */
struct unit *punit_focus;

/* set high, if the player has selected goto */
/* actually, set to id of unit goto-ing (id is non-zero) */
int goto_state;

void request_move_unit_direction(struct unit *punit, int dx, int dy);
struct unit *find_best_focus_candidate(void);
void wakeup_sentried_units(int x, int y);

extern Widget toplevel, main_form, map_canvas;
extern Widget turn_done_button;
extern int smooth_move_units;
extern int auto_center_on_unit;

/**************************************************************************
...
**************************************************************************/
void name_new_city_callback(Widget w, XtPointer client_data, 
			   XtPointer call_data)
{
  int unit_id;
  
  if((unit_id=(int)client_data)) {
    struct packet_unit_request req;
    req.unit_id=unit_id;
    strncpy(req.name, input_dialog_get_input(w), MAX_LENGTH_NAME);
    req.name[MAX_LENGTH_NAME-1]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  }
    
  input_dialog_destroy(w);
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
    append_output_window("Game: You can only unload transporter units.");
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
...
**************************************************************************/
void request_unit_build_city(struct unit *punit)
{
  struct city *pcity;
  if((pcity=map_get_city(punit->x, punit->y))) {
    if (pcity->size>8) {
      append_output_window("Game: Sir, the city is already big.");
      return;
    }
    else {
     struct packet_unit_request req;
     req.unit_id=punit->id;
     req.name[0]='\0';
     send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
     return;
    }
  }
  
  if(can_unit_build_city(punit)) {
    input_dialog_create(toplevel, "shellnewcityname", 
			"What should we call our new city?",
			city_name_suggestion(game.player_ptr),
			name_new_city_callback, (XtPointer)punit->id,
			name_new_city_callback, (XtPointer)0);
  }
  else 
    append_output_window("Game: Sir, we can't build a city here.");
}

/**************************************************************************
...
**************************************************************************/
void request_move_unit_direction(struct unit *punit, int dx, int dy)
{
  int dest_x, dest_y;
  struct unit req_unit;

  dest_x=map_adjust_x(punit->x+dx);
  dest_y=punit->y+dy;   /* not adjusting on purpose*/

  if(unit_flag(punit->type, F_CARAVAN)) {
    struct city *pcity, *phomecity;

    if((pcity=map_get_city(dest_x, dest_y)) &&
       (phomecity=find_city_by_id(punit->homecity))) {
      if(can_establish_trade_route(phomecity, pcity) ||
	 unit_can_help_build_wonder(punit, pcity)) {
	popup_caravan_dialog(punit, phomecity, pcity);
	return;
      }
    }
  }
  else if(unit_flag(punit->type, F_DIPLOMAT) &&
          is_diplomat_action_available(punit, DIPLOMAT_ANY_ACTION,
                                       dest_x, dest_y)) {
    if (diplomat_can_do_action(punit, DIPLOMAT_ANY_ACTION, dest_x, dest_y)) {
      popup_diplomat_dialog(punit, dest_x, dest_y);
    } else {
      append_output_window("Game: You don't have enough movement left");
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
  send_unit_info(&req_unit);
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
  if(unit_flag(punit->type, F_SETTLERS)) {
    struct packet_unit_request req;
    req.unit_id=punit->id;
    req.name[0]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_AUTO);
  }
  else
    append_output_window("Game: Only settlers can be put in auto-mode.");
}

/**************************************************************************
...
**************************************************************************/
void key_unit_disband(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct unit *punit=get_unit_in_focus();
  
  if(punit)
    request_unit_disband(punit);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_homecity(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct unit *punit=get_unit_in_focus();
  
  if(punit)
    request_unit_change_homecity(punit);
}

/**************************************************************************
...
**************************************************************************/
void key_end_turn(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(/*!get_unit_in_focus() &&*/ !game.player_ptr->turn_done) {
      struct packet_generic_message gen_packet;
    gen_packet.message[0]='\0';
    send_packet_generic_message(&aconnection, PACKET_TURN_DONE, &gen_packet);
    XtSetSensitive(turn_done_button, FALSE);
  }
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
    refresh_tile_mapcanvas(x, y, 0);
  
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
...
**************************************************************************/
void key_unit_fortify(Widget w, XEvent *event, String *argv, Cardinal *argc)
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
void key_unit_goto(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  request_unit_goto();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_build_city(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct unit *punit=get_unit_in_focus();
  if(punit)
    request_unit_build_city(punit);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_unload(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_unit_unload(punit_focus);
}

/**************************************************************************
...
+**************************************************************************/
void key_unit_wakeup(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_unit_wakeup(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_sentry(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_SENTRY);
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
void key_unit_clean_pollution(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_POLLUTION);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_pillage(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_PILLAGE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_explore(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_EXPLORE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_wait(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_unit_wait(get_unit_in_focus());
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
void key_unit_done(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  request_unit_move_done();
}

/**************************************************************************
...
**************************************************************************/
void key_unit_mine(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
      request_new_unit_activity(punit_focus, ACTIVITY_MINE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_road(Widget w, XEvent *event, String *argv, Cardinal *argc)
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
void key_unit_irrigate(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_new_unit_activity(punit_focus, ACTIVITY_IRRIGATE);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_auto(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_unit_auto(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 0, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, -1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, 1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, 0, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_south_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, 1);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
    request_move_unit_direction(punit_focus, -1, 0);
}

/**************************************************************************
...
**************************************************************************/
void key_unit_north_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  if(get_unit_in_focus())
     request_move_unit_direction(punit_focus, -1, -1);
}

/**************************************************************************
...
**************************************************************************/
void popit(int xin, int yin, int xtile, int ytile)
{
  Position x, y;
  char s[512];
  struct city *pcity;
  struct tile *ptile=map_get_tile(xtile, ytile);

  
  if(ptile->known>=TILE_KNOWN) {
    Widget p=XtCreatePopupShell("popupinfo", simpleMenuWidgetClass, map_canvas, NULL, 0);
    XtAddCallback(p,XtNpopdownCallback,destroy_me_callback,NULL);
    sprintf(s, "Terrain: %s", map_get_tile_info_text(xtile, ytile));
    XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);

    if(ptile->special&S_HUT) {
      XtCreateManagedWidget("Minor Tribe Village", smeBSBObjectClass,
			    p, NULL, 0);
    }
    
    if((pcity=map_get_city(xtile, ytile))) {
      sprintf(s, "City: %s(%s)", pcity->name, 
	      get_race_name(game.players[pcity->owner].race));
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }

    if(ptile->special&S_ROAD || ptile->special&S_IRRIGATION ||
       ptile->special&S_RAILROAD || ptile->special&S_MINE ||
       ptile->special&S_FORTRESS) {
      sprintf(s, "Infrastructure: ");
      if(ptile->special&S_ROAD)
	strcat(s, "Road/");
      if(ptile->special&S_RAILROAD)
	strcat(s, "Railroad/");
      if(ptile->special&S_IRRIGATION)
	strcat(s, "Irrigation/");
      if(ptile->special&S_MINE)
	strcat(s, "Mine/");
      if(ptile->special&S_FORTRESS)
	strcat(s, "Fortress/");
      *(s+strlen(s)-1)='\0';
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }
    
    if(unit_list_size(&ptile->units) && !pcity) {
      char cn[64];
      struct unit *punit=unit_list_get(&ptile->units, 0);
      struct unit_type *ptype=get_unit_type(punit->type);
      cn[0]='\0';
      if(punit->owner==game.player_idx) {
	struct city *pcity;
	pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);
	if(pcity)
	  sprintf(cn, "/%s", pcity->name);
      }
      sprintf(s, "Unit: %s(%s%s)", ptype->name, 
	      get_race_name(game.players[punit->owner].race), cn);
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);

      if(punit->owner==game.player_idx)  {
        sprintf(s, "A:%d D:%d FP:%d HP:%d/%d%s", ptype->attack_strength, 
	        ptype->defense_strength, ptype->firepower, punit->hp, 
	        ptype->hp, punit->veteran?" V":"");

        if(punit->activity==ACTIVITY_GOTO)  {
          put_cross_overlay_tile(punit->goto_dest_x,punit->goto_dest_y);
          XtAddCallback(p,XtNpopdownCallback,popupinfo_popdown_callback,
	  	        (XtPointer)punit);
        }
      } else {
        sprintf(s, "A:%d D:%d FP:%d HP:%d0%%", ptype->attack_strength, 
	  ptype->defense_strength, ptype->firepower, 
	  (punit->hp*100/ptype->hp + 9)/10 );
      };
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }
    
    XtTranslateCoords(map_canvas, xin, yin, &x, &y);
    
    XtVaSetValues(p, XtNx, x, XtNy, y, NULL);
    XtPopupSpringLoaded(p);
  }
  
}

/**************************************************************************
...
**************************************************************************/
void popupinfo_popdown_callback(Widget w, XtPointer client_data,
				XtPointer call_data)
{
  struct unit *punit=(struct unit *)client_data;

  refresh_tile_mapcanvas(punit->goto_dest_x,punit->goto_dest_y,1);
}

/**************************************************************************
(RP:) wake up my own sentried units on the tile that was clicked
**************************************************************************/
void butt_down_wakeup(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  int xtile, ytile;
  XButtonEvent *ev=&event->xbutton;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;

  xtile=map_adjust_x(map_view_x0+ev->x/NORMAL_TILE_WIDTH);
  ytile=map_adjust_y(map_view_y0+ev->y/NORMAL_TILE_HEIGHT);

  wakeup_sentried_units(xtile,ytile);
}

/**************************************************************************
...
**************************************************************************/
void butt_down_mapcanvas(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  int xtile, ytile;
  XButtonEvent *ev=&event->xbutton;
  struct city *pcity;
  struct tile *ptile;
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  
  xtile=map_adjust_x(map_view_x0+ev->x/NORMAL_TILE_WIDTH);
  ytile=map_adjust_y(map_view_y0+ev->y/NORMAL_TILE_HEIGHT);

  ptile=map_get_tile(xtile, ytile);
  pcity=map_get_city(xtile, ytile);

  if(ev->button==Button1 && goto_state) { 
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

    return;
  }
  
  if(pcity && ev->button==Button1 && game.player_idx==pcity->owner) {
    popup_city_dialog(pcity, 0);
    return;
  }
  
  if(ev->button==Button1) {
    if(unit_list_size(&ptile->units)==1) {
      struct unit *punit=unit_list_get(&ptile->units, 0);
      if(game.player_idx==punit->owner && (punit->moves_left>0 || 
					   punit->activity==ACTIVITY_GOTO ||
                                           punit->activity==ACTIVITY_EXPLORE ||
					   punit->ai.control==ACTIVITY_GOTO)) {
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
  else if(ev->button==Button2)
    popit(ev->x, ev->y, xtile, ytile);
  else
    center_tile_mapcanvas(xtile, ytile);
}

/**************************************************************************
...
**************************************************************************/
void butt_down_overviewcanvas(Widget w, XEvent *event, String *argv, 
			      Cardinal *argc)
{
  int xtile, ytile;
  XButtonEvent *ev=&event->xbutton;

  xtile=ev->x/2-(map.xsize/2-(map_view_x0+map_canvas_store_twidth/2));
  ytile=ev->y/2;
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return;

  if(ev->button==Button1 && goto_state) {
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

    return;
  }
  
  if(ev->button==Button3)
    center_tile_mapcanvas(xtile, ytile);
}

/**************************************************************************
...
**************************************************************************/
void center_on_unit(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  struct unit *punit;
  
  if((punit=get_unit_in_focus()))
    center_tile_mapcanvas(punit->x, punit->y);
}

/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
void key_city_workers(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  int x,y;
  XButtonEvent *ev=&event->xbutton;
  struct city *pcity;
  struct tile *ptile;
  int i,j;
  static int color=COLOR_STD_WHITE;
  static enum city_tile_type last_t=-1;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  
  x=ev->x/NORMAL_TILE_WIDTH; y=ev->y/NORMAL_TILE_HEIGHT;
  ptile=map_get_tile(map_adjust_x(map_view_x0+x), map_adjust_y(map_view_y0+y));
  if(ptile->worked==NULL || 
     (pcity=ptile->worked)->owner!=game.player_idx) return;
  x=map_canvas_adjust_x(pcity->x); y=map_canvas_adjust_y(pcity->y);

  color = (color%3)+1;
  XSetForeground(display,fill_tile_gc,colors_standard[color]);

  city_map_iterate(i, j)  {
    enum city_tile_type t=get_worker_city(pcity, i, j);
    if(i==2 && j==2) continue;
    if(t==C_TILE_EMPTY) {
      if(last_t!=t) XSetStipple(display,fill_tile_gc,gray25);
    } else if(t==C_TILE_WORKER) {
      if(last_t!=t) XSetStipple(display,fill_tile_gc,gray50);
    } else continue;
    last_t=t;
    XFillRectangle(display, XtWindow(map_canvas), fill_tile_gc,
		   (x+i-2)*NORMAL_TILE_WIDTH, (y+j-2)*NORMAL_TILE_HEIGHT,
		   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  }
}

/**************************************************************************
...
**************************************************************************/
void focus_to_next_unit(Widget w, XEvent *event, String *argv, 
			Cardinal *argc)
{
  advance_unit_focus();
  /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
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

  if(!punit_focus) {
    unit_list_iterate(game.player_ptr->units, punit) 
      if(punit->focus_status==FOCUS_WAIT)
	punit->focus_status=FOCUS_AVAIL;
    punit_focus=find_best_focus_candidate();
    unit_list_iterate_end;
  }
  
  /* We have to do this ourselves, and not rely on set_unit_focus(),
   * because above we change punit_focus directly.
   */
  if(punit_old_focus!=NULL && punit_old_focus!=punit_focus)
    refresh_tile_mapcanvas(punit_old_focus->x, punit_old_focus->y, 1);

  set_unit_focus(punit_focus);
}

/**************************************************************************
...
**************************************************************************/
struct unit *find_best_focus_candidate(void)
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
