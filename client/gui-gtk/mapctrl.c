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
#include "text.h"
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
static void popit(GdkEventButton *event, struct tile *ptile)
{
  GtkWidget *p;
  static struct tile *cross_list[2 + 1];
  struct tile **cross_head = cross_list;
  int i;
  static struct t_popup_pos popup_pos;
  struct unit *punit;
  bool is_orders;

  if(tile_get_known(ptile) >= TILE_KNOWN_FOGGED) {
    p=gtk_window_new(GTK_WINDOW_POPUP);
    gtk_container_add(GTK_CONTAINER(p), gtk_label_new(popup_info_text(ptile)));

    punit = find_visible_unit(ptile);

    is_orders = show_unit_orders(punit);

    if (punit && punit->goto_tile)  {
      *cross_head = punit->goto_tile;
      cross_head++;
    }
    *cross_head = ptile;
    cross_head++;


    *cross_head = NULL;
    for (i = 0; cross_list[i]; i++) {
      put_cross_overlay_tile(cross_list[i]);
    }
    gtk_signal_connect(GTK_OBJECT(p),"destroy",
		       GTK_SIGNAL_FUNC(popupinfo_popdown_callback),
		       GINT_TO_POINTER(is_orders));

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
  bool full = GPOINTER_TO_INT(data);

  if (full) {
    update_map_canvas_visible();
  } else {
    dirty_all();
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
  dsend_packet_unit_build_city(&aconnection, GPOINTER_TO_INT(data), input);
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
 Handle 'Mouse button released'. Because of the quickselect feature,
 the release of both left and right mousebutton can launch the goto.
**************************************************************************/
gint butt_release_mapcanvas(GtkWidget *w, GdkEventButton *ev)
{
  if (ev->button == 1 || ev->button == 3) {
    release_goto_button(ev->x, ev->y);
  }
  if (ev->button == 3 && (rbutton_down || hover_state != HOVER_NONE))  {
    release_right_button(ev->x, ev->y);
  }

  return TRUE;
}

/**************************************************************************
 Handle all mouse button press on canvas.
 Future feature: User-configurable mouse clicks.
**************************************************************************/
gint butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev)
{
  struct tile *ptile = NULL;
  struct city *pcity = NULL;

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(turn_done_button);
  ptile = canvas_pos_to_tile(ev->x, ev->y);
  pcity = ptile ? ptile->city : NULL;

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
    else if (ptile && (ev->state & GDK_SHIFT_MASK)) {
      clipboard_copy_production(ptile);
    }
    /* LMB in Area Selection mode. */
    else if(tiles_hilited_cities) {
      if (ptile) {
        toggle_tile_hilite(ptile);
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
    else if(ptile) {
      popit(ev, ptile);
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
	&& y < OVERVIEW_TILE_HEIGHT * map.ysize) {
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

  /* Reading the mouse pos here saves queueing. */
  gdk_window_get_pointer(map_canvas->window, &canvas_x, &canvas_y, NULL);
  update_selection_rectangle(canvas_x, canvas_y);
}

/**************************************************************************
...
**************************************************************************/
gint move_mapcanvas(GtkWidget *widget, GdkEventButton *event)
{
  update_line(event->x, event->y);
  update_rect_at_mouse_pos();
  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(event->x, event->y);
  }
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
  struct tile *ptile;

  if (ev->type != GDK_BUTTON_PRESS)
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */

  overview_to_map_pos(&xtile, &ytile, ev->x, ev->y);
  ptile = map_pos_to_tile(xtile, ytile);
  if (!ptile) {
    return TRUE;
  }
  
  if (can_client_change_view() && ev->button == 3) {
    center_tile_mapcanvas(ptile);
  } else if (can_client_issue_orders() && ev->button == 1) {
    do_map_click(ptile, SELECT_POPUP);
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
