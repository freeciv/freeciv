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

#include <gtk/gtk.h>

#include "combat.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "support.h"
#include "unit.h"

#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "clinet.h"
#include "cma_core.h"
#include "control.h"
#include "goto.h"
#include "tilespec.h"

#include "chatline.h"
#include "citydlg.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"

#include "mapctrl.h"

/* Color to use to display the workers */
int city_workers_color = COLOR_STD_WHITE;

/**************************************************************************
...
**************************************************************************/
static gint popit_button_release(GtkWidget *w, GdkEventButton *event)
{
  gtk_grab_remove(w);
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
  Popup a label with information about the tile, unit, city, when the user
  used the middle mouse button on the map.
**************************************************************************/
static void popit(GdkEventButton *event, int xtile, int ytile)
{
  GtkWidget *p;
  static struct map_position cross_list[2 + 1];
  struct map_position *cross_head = cross_list;
  int i;
  static struct t_popup_pos popup_pos;
  struct unit *punit;

  if(tile_get_known(xtile, ytile) >= TILE_KNOWN_FOGGED) {
    p=gtk_window_new(GTK_WINDOW_POPUP);
    gtk_container_add(GTK_CONTAINER(p), gtk_label_new(popup_info_text(xtile, ytile)));
    
    punit = find_visible_unit(map_get_tile(xtile, ytile));
    if (punit && (punit->activity == ACTIVITY_GOTO || punit->connecting))  {
      cross_head->x = goto_dest_x(punit);
      cross_head->y = goto_dest_y(punit);
      cross_head++;
    }
    cross_head->x = xtile;
    cross_head->y = ytile;
    cross_head++;


    cross_head->x = -1;
    for (i = 0; cross_list[i].x >= 0; i++) {
      put_cross_overlay_tile(cross_list[i].x, cross_list[i].y);
    }
    gtk_signal_connect(GTK_OBJECT(p),"destroy",
		       GTK_SIGNAL_FUNC(popupinfo_popdown_callback),
		       cross_list);

    popup_pos.xroot = event->x_root;
    popup_pos.yroot = event->y_root;

    gtk_signal_connect(GTK_OBJECT(p), "size-allocate",
                       GTK_SIGNAL_FUNC(popupinfo_positioning_callback), 
                       &popup_pos);

    gtk_widget_show_all(p);
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
  struct map_position *cross_list=(struct map_position *)data;

  while (cross_list->x >= 0) {
    refresh_tile_mapcanvas(cross_list->x, cross_list->y, TRUE);
    cross_list++;
  }
}

/**************************************************************************
  Put the popup on the correct position, after the real size is allocated 
  to the widget. The correct position is left beneath the cursor if within
  the right half of the map, and vice versa. Also, displace the popup so 
  as not to obscure it by the mouse cursor. 
**************************************************************************/
void popupinfo_positioning_callback(GtkWidget *w, GtkAllocation *alloc, 
                                    gpointer data)
{
  struct t_popup_pos *popup_pos = (struct t_popup_pos *)data;
  gint x, y;
  
  gdk_window_get_origin(map_canvas->window, &x, &y);
  if ((map_canvas->allocation.width / 2 + x) > popup_pos->xroot) {
    gtk_widget_set_uposition(w, popup_pos->xroot + 16,
                             popup_pos->yroot - (alloc->height / 2));
  } else {
    gtk_widget_set_uposition(w, popup_pos->xroot - alloc->width - 16, 
                             popup_pos->yroot - (alloc->height / 2));
  }
}

/**************************************************************************
...
**************************************************************************/
static void name_new_city_callback(const char *input, gpointer data)
{
  int unit_id = GPOINTER_TO_INT(data);
  struct packet_unit_request req;

  req.unit_id = unit_id;
  sz_strlcpy(req.name, input);
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_BUILD_CITY);
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(toplevel, /*"shellnewcityname" */
		      _("Build New City"),
		      _("What should we call our new city?"), suggestname,
		      name_new_city_callback, GINT_TO_POINTER(punit->id),
		      NULL, NULL);
}

/**************************************************************************
 Enable or disable the turn done button.
 Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  gtk_widget_set_sensitive(turn_done_button, state);
}

/**************************************************************************
(RP:) wake up my own sentried units on the tile that was clicked
**************************************************************************/
gint butt_down_wakeup(GtkWidget *w, GdkEventButton *ev)
{
  /* when you get a <SHIFT>+<LMB> pow! */
  if (ev->state & GDK_SHIFT_MASK) {
    wakeup_button_pressed(ev->x, ev->y);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gint butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev)
{
  int xtile, ytile;

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(turn_done_button);

  if (ev->button == 1 && (ev->state & GDK_SHIFT_MASK)) {
    adjust_workers_button_pressed(ev->x, ev->y);
  } else if (ev->button == 1) {
    action_button_pressed(ev->x, ev->y);
  } else if (canvas_to_map_pos(&xtile, &ytile, ev->x, ev->y)
	     && (ev->button == 2 || (ev->state & GDK_CONTROL_MASK))) {
    popit(ev, xtile, ytile);
  } else if (ev->button == 3) {
    recenter_button_pressed(ev->x, ev->y);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  int x, y;

  gdk_window_get_pointer(map_canvas->window, &x, &y, 0);
  if (x >= 0 && y >= 0
      && x < mapview_canvas.width && y < mapview_canvas.width) {
    update_line(x, y);
  } else {
    gdk_window_get_pointer(overview_canvas->window, &x, &y, 0);
    if (x >= 0 && y >= 0
	&& x < OVERVIEW_TILE_WIDTH * map.xsize
	&& y < OVERVIEW_TILE_WIDTH * map.ysize) {
      overview_update_line(x, y);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
gint move_mapcanvas(GtkWidget *widget, GdkEventButton *event)
{
  update_line(event->x, event->y);
  return TRUE;
}

/**************************************************************************
  Draw a goto line when the mouse moves over the overview canvas.
**************************************************************************/
gint move_overviewcanvas(GtkWidget *widget, GdkEventButton *event)
{
  overview_update_line(event->x, event->y);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gint butt_down_overviewcanvas(GtkWidget *w, GdkEventButton *ev)
{
  int xtile, ytile;

  if (ev->type != GDK_BUTTON_PRESS)
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */

  overview_to_map_pos(&xtile, &ytile, ev->x, ev->y);
  
  if (can_client_change_view() && ev->button == 3) {
    center_tile_mapcanvas(xtile, ytile);
  } else if (can_client_issue_orders() && ev->button == 1) {
    do_unit_goto(xtile, ytile);
  }

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
void key_city_workers(GtkWidget *w, GdkEventKey *ev)
{
  int x,y;
  struct city *pcity;

  if (!can_client_change_view()) {
    return;
  }
  
  gdk_window_get_pointer(map_canvas->window, &x, &y, NULL);
  if (!canvas_to_map_pos(&x, &y, x, y)) {
    nearest_real_pos(&x, &y);
  }

  pcity = find_city_near_tile(x, y);
  if (!pcity) {
    return;
  }

  /* Shade tiles on usage */
  city_workers_color = (city_workers_color % 3) + 1;
  put_city_workers(pcity, city_workers_color);
}


