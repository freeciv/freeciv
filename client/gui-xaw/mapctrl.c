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

#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "unit.h"

#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "clinet.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "tilespec.h"

#include "mapctrl.h"

extern Display	*display;
extern int screen_number;

extern int map_view_x0, map_view_y0;
extern int map_canvas_store_twidth, map_canvas_store_theight;
extern int overview_canvas_store_width, overview_canvas_store_height;

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;
/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

extern Widget toplevel, main_form, map_canvas;
extern Widget turn_done_button;
extern int smooth_move_units; /* from options.c */
extern int auto_center_on_unit;
extern int draw_map_grid;

/**************************************************************************
...
**************************************************************************/
static void name_new_city_callback(Widget w, XtPointer client_data, 
				   XtPointer call_data)
{
  size_t unit_id;
  
  if((unit_id=(size_t)client_data)) {
    struct packet_unit_request req;
    req.unit_id=unit_id;
    strncpy(req.name, input_dialog_get_input(w), MAX_LEN_NAME);
    req.name[MAX_LEN_NAME-1]='\0';
    send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
  }
    
  input_dialog_destroy(w);
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(toplevel, "shellnewcityname",
    _("What should we call our new city?"), suggestname,
    (void*)name_new_city_callback, (XtPointer)punit->id,
    (void*)name_new_city_callback, (XtPointer)0);
}

/**************************************************************************
...
**************************************************************************/
static void popit(int xin, int yin, int xtile, int ytile)
{
  Position x, y;
  int dw, dh;
  Dimension w, h, b;
  static struct map_position cross_list[2+1];
  struct map_position *cross_head = cross_list;
  int i;
  char s[512];
  struct city *pcity;
  struct unit *punit;
  struct tile *ptile=map_get_tile(xtile, ytile);

  
  if(ptile->known>=TILE_KNOWN) {
    Widget p=XtCreatePopupShell("popupinfo", simpleMenuWidgetClass,
				map_canvas, NULL, 0);
    sprintf(s, _("Terrain: %s"), map_get_tile_info_text(xtile, ytile));
    XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);

    if(ptile->special&S_HUT) {
      XtCreateManagedWidget(_("Minor Tribe Village"), smeBSBObjectClass,
			    p, NULL, 0);
    }
    
    if((pcity=map_get_city(xtile, ytile))) {
      sprintf(s, _("City: %s(%s) %s"), pcity->name, 
	      get_nation_name(game.players[pcity->owner].nation),
	      city_got_citywalls(pcity) ? _("with City Walls") : "");
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }

    if(get_tile_infrastructure_set(ptile)) {
      strcpy(s, _("Infrastructure: "));
      strcat(s, map_get_infrastructure_text(ptile->special));
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }

    if((punit=player_find_visible_unit(game.player_ptr, ptile)) && !pcity) {
      char cn[64];
      struct unit_type *ptype=get_unit_type(punit->type);
      cn[0]='\0';
      if(punit->owner==game.player_idx) {
	struct city *pcity;
	pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);
	if(pcity)
	  sprintf(cn, "/%s", pcity->name);
      }
      sprintf(s, _("Unit: %s(%s%s)"), ptype->name, 
	      get_nation_name(game.players[punit->owner].nation), cn);
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);

      if(punit->owner==game.player_idx)  {
	char uc[64] = "";
	if(unit_list_size(&ptile->units)>=2)
	  sprintf(uc, _("  (%d more)"), unit_list_size(&ptile->units) - 1);
        sprintf(s, _("A:%d D:%d FP:%d HP:%d/%d%s%s"), ptype->attack_strength, 
	        ptype->defense_strength, ptype->firepower, punit->hp, 
	        ptype->hp, punit->veteran?_(" V"):"", uc);

        if(punit->activity==ACTIVITY_GOTO)  {
	  cross_head->x = punit->goto_dest_x;
	  cross_head->y = punit->goto_dest_y;
	  cross_head++;
        }
      } else {
        sprintf(s, _("A:%d D:%d FP:%d HP:%d0%%"), ptype->attack_strength, 
	  ptype->defense_strength, ptype->firepower, 
	  (punit->hp*100/ptype->hp + 9)/10 );
      };
      XtCreateManagedWidget(s, smeBSBObjectClass, p, NULL, 0);
    }

    cross_head->x = xtile;
    cross_head->y = ytile;
    cross_head++;

    xin /= NORMAL_TILE_WIDTH;
    xin *= NORMAL_TILE_WIDTH;
    yin /= NORMAL_TILE_HEIGHT;
    yin *= NORMAL_TILE_HEIGHT;
    xin += (NORMAL_TILE_WIDTH / 2);
    XtTranslateCoords(map_canvas, xin, yin, &x, &y);
    dw = XDisplayWidth (display, screen_number);
    dh = XDisplayHeight (display, screen_number);
    XtRealizeWidget(p);
    XtVaGetValues(p, XtNwidth, &w, XtNheight, &h, XtNborderWidth, &b, NULL);
    w += (2 * b);
    h += (2 * b);
    x -= (w / 2);
    y -= h;
    if ((x + w) > dw) x = dw - w;
    if (x < 0) x = 0;
    if ((y + h) > dh) y = dh - h;
    if (y < 0) y = 0;
    XtVaSetValues(p, XtNx, x, XtNy, y, NULL);

    cross_head->x = -1;
    for (i = 0; cross_list[i].x >= 0; i++) {
      put_cross_overlay_tile(cross_list[i].x,cross_list[i].y);
    }
    XtAddCallback(p,XtNpopdownCallback,popupinfo_popdown_callback,
		  (XtPointer)cross_list);

    XtPopupSpringLoaded(p);
  }
  
}

/**************************************************************************
...
**************************************************************************/
void popupinfo_popdown_callback(Widget w, XtPointer client_data,
        XtPointer call_data)
{
  struct map_position *cross_list=(struct map_position *)client_data;

  while (cross_list->x >= 0) {
    refresh_tile_mapcanvas(cross_list->x,cross_list->y,1);
    cross_list++;
  }

  XtDestroyWidget(w);
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
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  
  xtile=map_adjust_x(map_view_x0+ev->x/NORMAL_TILE_WIDTH);
  ytile=map_adjust_y(map_view_y0+ev->y/NORMAL_TILE_HEIGHT);

  if(ev->button==Button1)
    do_map_click(xtile, ytile);
  else if(ev->button==Button2||ev->state&ControlMask)
    popit(ev->x, ev->y, xtile, ytile);
  else
    center_tile_mapcanvas(xtile, ytile);
}

/**************************************************************************
  Find the "best" city to associate with the selected tile.  
    a.  A city working the tile is the best
    b.  If no city is working the tile, choose a city that could work the tile
    c.  If multiple cities could work it, choose the most recently "looked at"
    d.  If none of the cities were looked at last, choose "randomly"
    e.  If another player is working the tile, or no cities can work it,
        return NULL
**************************************************************************/
static struct city *find_city_near_tile(int x, int y)
{
  struct tile *ptile=map_get_tile(x, y);
  struct city *pcity, *pcity2;
  int i,j;
  static struct city *last_pcity=NULL;

  if((pcity=ptile->worked))  {
    if(pcity->owner==game.player_idx)  return last_pcity=pcity;   /* rule a */
    else return NULL;    /* rule e */
  }

  pcity2 = NULL;
  city_map_iterate(i, j)  {
    pcity = map_get_city(x+i-2, y+j-2);
    if(pcity && pcity->owner==game.player_idx && 
       get_worker_city(pcity,4-i,4-j)==C_TILE_EMPTY)  {  /* rule b */
      if(pcity==last_pcity) return pcity;  /* rule c */
      pcity2 = pcity;
    }
  }
  return last_pcity = pcity2;
}

/**************************************************************************
  Adjust the position of city workers from the mapcanvas
**************************************************************************/
void adjust_workers(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  int x,y;
  XButtonEvent *ev=&event->xbutton;
  struct city *pcity;
  struct packet_city_request packet;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;

  x=ev->x/NORMAL_TILE_WIDTH; y=ev->y/NORMAL_TILE_HEIGHT;
  x=map_adjust_x(map_view_x0+x); y=map_adjust_y(map_view_y0+y);

  if(!(pcity = find_city_near_tile(x,y)))  return;

  x = x-pcity->x+2; y = y-pcity->y+2;
  packet.city_id=pcity->id;
  packet.worker_x=x;
  packet.worker_y=y;
  packet.name[0]='\0';
  
  if(pcity->city_map[x][y]==C_TILE_WORKER)
    send_packet_city_request(&aconnection, &packet, 
			     PACKET_CITY_MAKE_SPECIALIST);
  else if(pcity->city_map[x][y]==C_TILE_EMPTY)
    send_packet_city_request(&aconnection, &packet, 
			     PACKET_CITY_MAKE_WORKER);

  /* When the city info packet is received, update the workers on the map*/
  city_workers_display = pcity;

  return;
}


/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
void key_city_workers(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  int x,y;
  XButtonEvent *ev=&event->xbutton;
  struct city *pcity;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return;
  
  x=ev->x/NORMAL_TILE_WIDTH; y=ev->y/NORMAL_TILE_HEIGHT;
  x=map_adjust_x(map_view_x0+x); y=map_adjust_y(map_view_y0+y);

  pcity = find_city_near_tile(x,y);
  if(pcity==NULL) return;


  /* Shade tiles on usage */
  city_workers_color = (city_workers_color%3)+1;
  put_city_workers(pcity, city_workers_color);
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

  if(ev->button==Button1)
    do_goto(xtile,ytile);
  else if(ev->button==Button3)
    center_tile_mapcanvas(xtile, ytile);
}

/**************************************************************************
...
**************************************************************************/
void center_on_unit(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  request_center_focus_unit();
}

/**************************************************************************
 Enable or disable the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state( int state )
{
  XtSetSensitive(turn_done_button, state);
}

/**************************************************************************
...
**************************************************************************/
void xaw_key_end_turn(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_end_turn();
}
void xaw_key_cancel_action(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_cancel_action();
}
void xaw_key_map_grid(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_map_grid();
}
void xaw_key_unit_auto(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_auto();
}
void xaw_key_unit_build_city(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_build_city();
}
void xaw_key_unit_clean_pollution(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_clean_pollution();
}
void xaw_key_unit_disband(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_disband();
}
void xaw_key_unit_done(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_done();
}
void xaw_key_unit_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_east();
}
void xaw_key_unit_explore(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_explore();
}
void xaw_key_unit_fortify(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_fortify();
}
void xaw_key_unit_airbase(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_airbase();
}
void xaw_key_unit_goto(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_goto();
}
void xaw_key_unit_homecity(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_homecity();
}
void xaw_key_unit_irrigate(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_irrigate();
}
void xaw_key_unit_mine(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_mine();
}
void xaw_key_unit_north_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_north_east();
}
void xaw_key_unit_north_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_north_west();
}
void xaw_key_unit_north(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_north();
}
void xaw_key_unit_nuke(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_nuke();
}
void xaw_key_unit_pillage(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_pillage();
}
void xaw_key_unit_road(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_road();
}
void xaw_key_unit_sentry(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_sentry();
}
void xaw_key_unit_south_east(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_south_east();
}
void xaw_key_unit_south_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_south_west();
}
void xaw_key_unit_south(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_south();
}
void xaw_key_unit_transform(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_transform();
}
void xaw_key_unit_unload(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_unload();
}
void xaw_key_unit_wait(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_wait();
}
void xaw_key_unit_wakeup(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_wakeup();
}
void xaw_key_unit_west(Widget w, XEvent *event, String *argv, Cardinal *argc)
{
  key_unit_west();
}

