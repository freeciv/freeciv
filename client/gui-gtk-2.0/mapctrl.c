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

#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "climap.h"
#include "clinet.h"
#include "climisc.h"
#include "colors.h"
#include "control.h"
#include "dialogs.h"
#include "goto.h"
#include "graphics.h"
#include "gui_main.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "tilespec.h"
#include "cma_core.h"
#include "text.h"

#include "mapctrl.h"

/* Color to use to display the workers */
int city_workers_color=COLOR_STD_WHITE;

/**************************************************************************
...
**************************************************************************/
static gboolean popit_button_release(GtkWidget *w, GdkEventButton *event)
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
  GtkWidget *p;
  static struct map_position cross_list[2 + 1];
  struct map_position *cross_head = cross_list;
  int i;
  int popx, popy;
  struct unit *punit;
  bool is_orders;

  if(tile_get_known(xtile, ytile) >= TILE_KNOWN_FOGGED) {
    p=gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_app_paintable(p, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(p), 4);
    gtk_container_add(GTK_CONTAINER(p), gtk_label_new(popup_info_text(xtile, ytile)));
    
    punit = find_visible_unit(map_get_tile(xtile, ytile));

    is_orders = show_unit_orders(punit);

    if (punit && is_goto_dest_set(punit)) {
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
    g_signal_connect(p, "destroy",
		     G_CALLBACK(popupinfo_popdown_callback),
		     GINT_TO_POINTER(is_orders));

    /* displace popup so as not to obscure it by the mouse cursor */
    popx= event->x_root + 16;
    popy= event->y_root;
    if (popy < 0)
      popy = 0;      
    gtk_window_move(GTK_WINDOW(p), popx, popy);
    gtk_widget_show_all(p);
    gdk_pointer_grab(p->window, TRUE, GDK_BUTTON_RELEASE_MASK,
		     NULL, NULL, event->time);
    gtk_grab_add(p);

    g_signal_connect_after(p, "button_release_event",
                           G_CALLBACK(popit_button_release), NULL);
  }
}

/**************************************************************************
...
**************************************************************************/
void popupinfo_popdown_callback(GtkWidget *w, gpointer data)
{
  bool full = GPOINTER_TO_INT(data);

  if (full) {
    update_map_canvas_visible();
  } else {
    dirty_all();
  }
}

 /**************************************************************************
...
**************************************************************************/
static void name_new_city_callback(GtkWidget * w, gpointer data)
{
  dsend_packet_unit_build_city(&aconnection, GPOINTER_TO_INT(data),
			       input_dialog_get_input(w));
  input_dialog_destroy(w);
}

/**************************************************************************
 Popup dialog where the user choose the name of the new city
 punit = (settler) unit which builds the city
 suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, char *suggestname)
{
  input_dialog_create(GTK_WINDOW(toplevel), /*"shellnewcityname" */
		     _("Build New City"),
		     _("What should we call our new city?"), suggestname,
		     G_CALLBACK(name_new_city_callback),
                     GINT_TO_POINTER(punit->id),
		     G_CALLBACK(name_new_city_callback),
                     GINT_TO_POINTER(0));
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
 Handle 'Mouse button released'. Because of the quickselect feature,
 the release of both left and right mousebutton can launch the goto.
**************************************************************************/
gboolean butt_release_mapcanvas(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  if (ev->button == 1 || ev->button == 3) {
    release_goto_button(ev->x, ev->y);
  }
  if(ev->button == 3 && (rbutton_down || hover_state != HOVER_NONE))  {
    release_right_button(ev->x, ev->y);
  }

  return TRUE;
}

/**************************************************************************
 Handle all mouse button press on canvas.
 Future feature: User-configurable mouse clicks.
**************************************************************************/
gboolean butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  int xtile, ytile;
  bool is_real;
  struct city *pcity = NULL;

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(map_canvas);
  is_real = canvas_to_map_pos(&xtile, &ytile, ev->x, ev->y);
  if (is_real) {
    pcity = map_get_city(xtile, ytile);
  }

  switch (ev->button) {

  case 1: /* LEFT mouse button */

    /* <SHIFT> + <CONTROL> + LMB : Adjust workers. */
    if ((ev->state & GDK_SHIFT_MASK) && (ev->state & GDK_CONTROL_MASK)) {
      adjust_workers_button_pressed(ev->x, ev->y);
    }
    /* <CONTROL> + LMB : Quickselect a sea unit. */
    else if (ev->state & GDK_CONTROL_MASK) {
      action_button_pressed(ev->x, ev->y, SELECT_SEA);
    }
    /* <SHIFT> + LMB: Copy Production. */
    else if(is_real && (ev->state & GDK_SHIFT_MASK)) {
      clipboard_copy_production(xtile, ytile);
    }
    /* LMB in Area Selection mode. */
    else if(tiles_hilited_cities) {
      if (is_real) {
        toggle_tile_hilite(xtile, ytile);
      }
    }
    /* Plain LMB click. */
    else {
      action_button_pressed(ev->x, ev->y, SELECT_POPUP);
    }
    break;

  case 2: /* MIDDLE mouse button */

    /* <CONTROL> + MMB: Wake up sentries. */
    if (ev->state & GDK_CONTROL_MASK) {
      wakeup_button_pressed(ev->x, ev->y);
    }
    /* Plain Middle click. */
    else if(is_real) {
      popit(ev, xtile, ytile);
    }
    break;

  case 3: /* RIGHT mouse button */

    /* <CONTROL> + RMB : Quickselect a land unit. */
    if (ev->state & GDK_CONTROL_MASK) {
      action_button_pressed(ev->x, ev->y, SELECT_LAND);
    }
    /* <SHIFT> + RMB: Paste Production. */
    else if(ev->state & GDK_SHIFT_MASK) {
      clipboard_paste_production(pcity);
      cancel_tile_hiliting();
    }
    /* Plain RMB click. */
    else {
      /*  A foolproof user will depress button on canvas,
       *  release it on another widget, and return to canvas
       *  to find rectangle still active.
       */
      if (rectangle_active) {
        release_right_button(ev->x, ev->y);
        return TRUE;
      }
      cancel_tile_hiliting();
      if (hover_state == HOVER_NONE) {
        anchor_selection_rectangle(ev->x, ev->y);
        rbutton_down = TRUE; /* causes rectangle updates */
      }
    }
    break;

  default:
    break;
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
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  int canvas_x, canvas_y;

  if (!rbutton_down) {
    return;
  }

  /* Reading the mouse pos here saves event queueing. */
  gdk_window_get_pointer(map_canvas->window, &canvas_x, &canvas_y, NULL);
  update_selection_rectangle(canvas_x, canvas_y);
}

/**************************************************************************
...
**************************************************************************/
gboolean move_mapcanvas(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
  update_line(ev->x, ev->y);
  update_rect_at_mouse_pos();
  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(ev->x, ev->y);
  }
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gboolean move_overviewcanvas(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
  overview_update_line(ev->x, ev->y);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gboolean butt_down_overviewcanvas(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  int xtile, ytile;

  if (ev->type != GDK_BUTTON_PRESS)
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */

  overview_to_map_pos(&xtile, &ytile, ev->x, ev->y);

  if (can_client_change_view() && (ev->button == 3)) {
    center_tile_mapcanvas(xtile, ytile);
  } else if (can_client_issue_orders() && (ev->button == 1)) {
    do_map_click(xtile, ytile, SELECT_POPUP);
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
  int x, y;
  
  gdk_window_get_pointer(map_canvas->window, &x, &y, NULL);
  key_city_overlay(x, y);
}
