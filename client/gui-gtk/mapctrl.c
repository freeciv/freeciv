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

#include <gtk/gtk.h>

#include "capability.h"
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
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"

#include "mapctrl.h"

extern int map_view_x0, map_view_y0;
extern int map_canvas_store_twidth, map_canvas_store_theight;
extern int overview_canvas_store_width, overview_canvas_store_height;

/* Update the workers for a city on the map, when the update is received */
struct city *city_workers_display = NULL;
/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

extern GtkWidget *map_canvas;
extern GtkWidget *toplevel;
extern GtkWidget *turn_done_button;
extern int smooth_move_units; /* from options.o */
extern int auto_center_on_unit;
extern int draw_map_grid;

extern int goto_state; /* from control.c */
extern int nuke_state;
extern struct unit *punit_focus;

/**************************************************************************
...
**************************************************************************/
static void name_new_city_callback(GtkWidget *w, gpointer data)
{
  size_t unit_id;
  
  if((unit_id=(size_t)data)) {
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
  input_dialog_create(toplevel, /*"shellnewcityname"*/"Build New City",
			"What should we call our new city?",
			city_name_suggestion(game.player_ptr),
			name_new_city_callback, (gpointer)punit->id,
			name_new_city_callback, (gpointer)0);
}

/**************************************************************************
...
**************************************************************************/
static gint
popit_button_release(GtkWidget *w, GdkEventButton *event)
{
  gtk_grab_remove(w);
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static void popit(GdkEventButton *event, int xtile, int ytile)
{
  GtkWidget *p, *b;
  char s[512];
  struct city *pcity;
  struct unit *punit;
  struct tile *ptile=map_get_tile(xtile, ytile);

  if(ptile->known>=TILE_KNOWN) {
    p=gtk_window_new(GTK_WINDOW_POPUP);
    b=gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(p), b);
    sprintf(s, "Terrain: %s", map_get_tile_info_text(xtile, ytile));
    gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				   "GtkLabel::label", s,
				   NULL);

    if(ptile->special&S_HUT) {
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", "Minor Tribe Village",
				     NULL);
    }
    
    if((pcity=map_get_city(xtile, ytile))) {
      sprintf(s, "City: %s(%s)", pcity->name, 
	      get_race_name(game.players[pcity->owner].race));
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s,
				     NULL);
    }

    if(ptile->special&S_ROAD || ptile->special&S_IRRIGATION ||
       ptile->special&S_RAILROAD || ptile->special&S_MINE ||
       ptile->special&S_FORTRESS || ptile->special&S_FARMLAND) {
      sprintf(s, "Infrastructure: ");
      if(ptile->special&S_ROAD)
	strcat(s, "Road/");
      if(ptile->special&S_RAILROAD)
	strcat(s, "Railroad/");
      if(ptile->special&S_IRRIGATION)
	strcat(s, "Irrigation/");
      if(ptile->special&S_FARMLAND)
	strcat(s, "Farmland/");
      if(ptile->special&S_MINE)
	strcat(s, "Mine/");
      if(ptile->special&S_FORTRESS)
	strcat(s, "Fortress/");
      *(s+strlen(s)-1)='\0';
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s,
				     NULL);
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
      sprintf(s, "Unit: %s(%s%s)", ptype->name, 
	      get_race_name(game.players[punit->owner].race), cn);
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s,
				     NULL);

      if(punit->owner==game.player_idx)  {
        sprintf(s, "A:%d D:%d FP:%d HP:%d/%d%s", ptype->attack_strength, 
	        ptype->defense_strength, ptype->firepower, punit->hp, 
	        ptype->hp, punit->veteran?" V":"");

        if(punit->activity==ACTIVITY_GOTO)  {
          put_cross_overlay_tile(punit->goto_dest_x,punit->goto_dest_y);

	  gtk_signal_connect(GTK_OBJECT(p),"destroy",
		GTK_SIGNAL_FUNC(popupinfo_popdown_callback),punit);
        }
      } else {
        sprintf(s, "A:%d D:%d FP:%d HP:%d0%%", ptype->attack_strength, 
	  ptype->defense_strength, ptype->firepower, 
	  (punit->hp*100/ptype->hp + 9)/10 );
      }
      gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", b,
				     "GtkLabel::label", s,
				     NULL);
    }
    gtk_widget_show_all(b);

    gtk_widget_popup(p, event->x_root, event->y_root);
    gdk_pointer_grab(p->window, TRUE, GDK_BUTTON_RELEASE_MASK,
		     NULL, NULL, event->time);
    gtk_grab_add(p);

    gtk_signal_connect_after(GTK_OBJECT(p), "button_release_event",
                             GTK_SIGNAL_FUNC(popit_button_release), NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
void popupinfo_popdown_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit=(struct unit *)data;

  refresh_tile_mapcanvas(punit->goto_dest_x,punit->goto_dest_y,1);
}

/**************************************************************************
(RP:) wake up my own sentried units on the tile that was clicked
**************************************************************************/
gint butt_down_wakeup(GtkWidget *widget, GdkEventButton *event)
{
  int xtile, ytile;

  /* when you get a <SHIFT>+<LMB> pow! */
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE||!(event->state & GDK_SHIFT_MASK))
    return TRUE;

  xtile=map_adjust_x(map_view_x0+event->x/NORMAL_TILE_WIDTH);
  ytile=map_adjust_y(map_view_y0+event->y/NORMAL_TILE_HEIGHT);

  wakeup_sentried_units(xtile,ytile);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gint butt_down_mapcanvas(GtkWidget *widget, GdkEventButton *event)
{
  int xtile, ytile;
  struct city *pcity;
  struct tile *ptile;
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return TRUE;
  
  if((event->button==1)&&(event->state&GDK_SHIFT_MASK)) {
    adjust_workers(widget, event);
    return TRUE;
  }

  xtile=map_adjust_x(map_view_x0+event->x/NORMAL_TILE_WIDTH);
  ytile=map_adjust_y(map_view_y0+event->y/NORMAL_TILE_HEIGHT);

  ptile=map_get_tile(xtile, ytile);
  pcity=map_get_city(xtile, ytile);

  if((event->button==1) && goto_state) { 
    struct unit *punit;

    if((punit=unit_list_find(&game.player_ptr->units, goto_state))) {
      struct packet_unit_request req;
      if(nuke_state && 3*real_map_distance(punit->x,punit->y,xtile,ytile) > punit->moves_left) {
        append_output_window("Game: Too far for this unit.");
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
    return TRUE;
  }

  if(pcity && (event->button==1) && (game.player_idx==pcity->owner)) {
    popup_city_dialog(pcity, 0);
    return TRUE;
  }

  if(event->button==1) {
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
  else if((event->button==2)||(event->state&GDK_CONTROL_MASK))
    popit(event, xtile, ytile);
  else
    center_tile_mapcanvas(xtile, ytile);
  return TRUE;
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
    else return NULL;	 /* rule e */
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
gint adjust_workers(GtkWidget *widget, GdkEventButton *ev)
{
  int x,y;
  struct city *pcity;
  struct packet_city_request packet;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return TRUE;

  x=ev->x/NORMAL_TILE_WIDTH; y=ev->y/NORMAL_TILE_HEIGHT;
  x=map_adjust_x(map_view_x0+x); y=map_adjust_y(map_view_y0+y);

  if(!(pcity = find_city_near_tile(x,y)))  return TRUE;

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
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
gint butt_down_overviewcanvas(GtkWidget *widget, GdkEventButton *event)
{
  int xtile, ytile;

  xtile=event->x/2-(map.xsize/2-(map_view_x0+map_canvas_store_twidth/2));
  ytile=event->y/2;
  
  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
     return TRUE;

  if(event->button==1 && goto_state) {
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

    return TRUE;
  }
  
  if(event->button==3)
    center_tile_mapcanvas(xtile, ytile);
  return TRUE;
}

/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
void center_on_unit(void)
{
  request_center_focus_unit();
}

/**************************************************************************
  Draws the on the map the tiles the given city is using
**************************************************************************/
gint key_city_workers(GtkWidget *widget, GdkEventKey *ev)
{
  int x,y;
  struct city *pcity;

  if(get_client_state()!=CLIENT_GAME_RUNNING_STATE)
    return TRUE;
  
  gdk_window_get_pointer(map_canvas->window, &x, &y, NULL);

  x/=NORMAL_TILE_WIDTH; y/=NORMAL_TILE_HEIGHT;
  x=map_adjust_x(map_view_x0+x); y=map_adjust_y(map_view_y0+y);

  pcity = find_city_near_tile(x,y);
  if(pcity==NULL) return TRUE;


  /* Shade tiles on usage */
  city_workers_color = (city_workers_color%3)+1;
  put_city_workers(pcity, city_workers_color);
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
void focus_to_next_unit(void)
{
  advance_unit_focus();

  goto_state=0;
  nuke_state=0;
  /* set_unit_focus(punit_focus); */  /* done in advance_unit_focus */
}

/**************************************************************************
 Enable or disable the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(int state)
{
  gtk_widget_set_sensitive(turn_done_button, FALSE);
}
