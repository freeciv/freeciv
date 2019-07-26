/***********************************************************************
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
#include <fc_config.h>
#endif

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "combat.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "unit.h"

#include "overview_common.h"

/* client */
#include "client_main.h"
#include "climap.h"
#include "climisc.h"
#include "control.h"
#include "editor.h"
#include "tilespec.h"
#include "text.h"

/* client/agents */
#include "cma_core.h"

/* client/gui-gtk-4.0 */
#include "chatline.h"
#include "citydlg.h"
#include "colors.h"
#include "dialogs.h"
#include "editgui.h"
#include "graphics.h"
#include "gui_main.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"

#include "mapctrl.h"

struct tmousepos { int x, y; };
extern gint cur_x, cur_y;

/**********************************************************************//**
  Button released when showing info label
**************************************************************************/
static gboolean popit_button_release(GtkWidget *w, GdkEvent *ev)
{
  gtk_grab_remove(w);
  gdk_seat_ungrab(gdk_device_get_seat(gdk_event_get_device(ev)));
  gtk_widget_destroy(w);

  return FALSE;
}

/**********************************************************************//**
  Put the popup on a smart position, after the real size of the widget is
  known: left of the cursor if within the right half of the map, and vice
  versa; displace the popup so as not to obscure it by the mouse cursor;
  stay always within the map if possible. 
**************************************************************************/
static void popupinfo_positioning_callback(GtkWidget *w, GtkAllocation *alloc, 
					   gpointer data)
{
  struct tmousepos *mousepos = data;
  float x, y;
  struct tile *ptile;

  ptile = canvas_pos_to_tile(mousepos->x, mousepos->y);
  if (tile_to_canvas_pos(&x, &y, ptile)) {
    gint minx, miny, maxy;

    gdk_surface_get_origin(gtk_widget_get_surface(map_canvas), &minx, &miny);
    maxy = miny + gtk_widget_get_allocated_height(map_canvas);

    if (x > mapview.width / 2) {
      /* right part of the map */
      x += minx;
      y += miny + (tileset_tile_height(tileset) - alloc->height) / 2;

      y = CLIP(miny, y, maxy - alloc->height);

      gtk_window_move(GTK_WINDOW(w), x - alloc->width, y);
    } else {
      /* left part of the map */
      x += minx + tileset_tile_width(tileset);
      y += miny + (tileset_tile_height(tileset) - alloc->height) / 2;

      y = CLIP(miny, y, maxy - alloc->height);

      gtk_window_move(GTK_WINDOW(w), x, y);
    }
  }
}

/**********************************************************************//**
  Popup a label with information about the tile, unit, city, when the user
  used the middle mouse button on the map.
**************************************************************************/
static void popit(GdkEvent *ev, struct tile *ptile)
{
  GtkWidget *p;
  static struct tmousepos mousepos;
  struct unit *punit;

  if (TILE_UNKNOWN != client_tile_get_known(ptile)) {
    gdouble e_x, e_y;

    p = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_margin_start(p, 4);
    gtk_widget_set_margin_end(p, 4);
    gtk_widget_set_margin_top(p, 4);
    gtk_widget_set_margin_bottom(p, 4);
    gtk_window_set_transient_for(GTK_WINDOW(p), GTK_WINDOW(toplevel));
    gtk_container_add(GTK_CONTAINER(p), gtk_label_new(popup_info_text(ptile)));

    punit = find_visible_unit(ptile);

    if (punit) {
      mapdeco_set_gotoroute(punit);
      if (punit->goto_tile) {
        mapdeco_set_crosshair(punit->goto_tile, TRUE);
      }
    }
    mapdeco_set_crosshair(ptile, TRUE);

    g_signal_connect(p, "destroy",
                     G_CALLBACK(popupinfo_popdown_callback), NULL);


    gdk_event_get_coords(ev, &e_x, &e_y);
    mousepos.x = e_x;
    mousepos.y = e_y;

    g_signal_connect(p, "size-allocate",
                     G_CALLBACK(popupinfo_positioning_callback),
                     &mousepos);

    gtk_widget_show(p);
    gdk_seat_grab(gdk_device_get_seat(gdk_event_get_device(ev)),
                  gtk_widget_get_surface(p),
                  GDK_SEAT_CAPABILITY_ALL_POINTING,
                  TRUE, NULL, (GdkEvent *)ev, NULL, NULL);
    gtk_grab_add(p);

    g_signal_connect_after(p, "button_release_event",
                           G_CALLBACK(popit_button_release), NULL);
  }
}

/**********************************************************************//**
  Information label destruction requested
**************************************************************************/
void popupinfo_popdown_callback(GtkWidget *w, gpointer data)
{
  mapdeco_clear_crosshairs();
  mapdeco_clear_gotoroutes();
}

/**********************************************************************//**
  Callback from city name dialog for new city.
**************************************************************************/
static void name_new_city_popup_callback(gpointer data, gint response,
                                         const char *input)
{
  int idx = GPOINTER_TO_INT(data);

  switch (response) {
  case GTK_RESPONSE_OK:
    finish_city(index_to_tile(&(wld.map), idx), input);
    break;
  case GTK_RESPONSE_CANCEL:
  case GTK_RESPONSE_DELETE_EVENT:
    cancel_city(index_to_tile(&(wld.map), idx));
    break;
  }
}

/**********************************************************************//**
  Popup dialog where the user choose the name of the new city
  punit = (settler) unit which builds the city
  suggestname = suggetion of the new city's name
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, const char *suggestname)
{
  input_dialog_create(GTK_WINDOW(toplevel), /*"shellnewcityname" */
                      _("Build New City"),
                      _("What should we call our new city?"), suggestname,
                      name_new_city_popup_callback,
                      GINT_TO_POINTER(tile_index(unit_tile(punit))));
}

/**********************************************************************//**
  Enable or disable the turn done button.
  Should probably some where else.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  gtk_widget_set_sensitive(turn_done_button, state);
}

/**********************************************************************//**
  Handle 'Mouse button released'. Because of the quickselect feature,
  the release of both left and right mousebutton can launch the goto.
**************************************************************************/
gboolean butt_release_mapcanvas(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  guint button;
  gdouble e_x, e_y;
  
  if (editor_is_active()) {
    return handle_edit_mouse_button_release(ev);
  }

  gdk_event_get_button(ev, &button);
  gdk_event_get_coords(ev, &e_x, &e_y);
  if (button == 1 || button == 3) {
    release_goto_button(e_x, e_y);
  }
  if (button == 3 && (rbutton_down || hover_state != HOVER_NONE))  {
    GdkModifierType state;

    gdk_event_get_state(ev, &state);
    release_right_button(e_x, e_y,
                         (state & GDK_SHIFT_MASK) != 0);
  }

  return TRUE;
}

/**********************************************************************//**
  Handle all mouse button press on canvas.
  Future feature: User-configurable mouse clicks.
**************************************************************************/
gboolean butt_down_mapcanvas(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  struct city *pcity = NULL;
  struct tile *ptile = NULL;
  gdouble e_x, e_y;
  guint button;
  GdkModifierType state;

  if (editor_is_active()) {
    return handle_edit_mouse_button_press(ev);
  }

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(map_canvas);
  gdk_event_get_coords(ev, &e_x, &e_y);
  ptile = canvas_pos_to_tile(e_x, e_y);
  pcity = ptile ? tile_city(ptile) : NULL;

  gdk_event_get_button(ev, &button);
  gdk_event_get_state(ev, &state);

  switch (button) {

  case 1: /* LEFT mouse button */

    /* <SHIFT> + <CONTROL> + LMB : Adjust workers. */
    if ((state & GDK_SHIFT_MASK) && (state & GDK_CONTROL_MASK)) {
      adjust_workers_button_pressed(e_x, e_y);
    } else if (state & GDK_CONTROL_MASK) {
      /* <CONTROL> + LMB : Quickselect a sea unit. */
      action_button_pressed(e_x, e_y, SELECT_SEA);
    } else if (ptile && (state & GDK_SHIFT_MASK)) {
      /* <SHIFT> + LMB: Append focus unit. */
      action_button_pressed(e_x, e_y, SELECT_APPEND);
    } else if (ptile && (state & GDK_MOD1_MASK)) {
      /* <ALT> + LMB: popit (same as middle-click) */
      /* FIXME: we need a general mechanism for letting freeciv work with
       * 1- or 2-button mice. */
      popit(ev, ptile);
    } else if (tiles_hilited_cities) {
      /* LMB in Area Selection mode. */
      if (ptile) {
        toggle_tile_hilite(ptile);
      }
    } else {
      /* Plain LMB click. */
      action_button_pressed(e_x, e_y, SELECT_POPUP);
    }
    break;

  case 2: /* MIDDLE mouse button */

    /* <CONTROL> + MMB: Wake up sentries. */
    if (state & GDK_CONTROL_MASK) {
      wakeup_button_pressed(e_x, e_y);
    } else if (ptile) {
      /* Plain Middle click. */
      popit(ev, ptile);
    }
    break;

  case 3: /* RIGHT mouse button */

    /* <CONTROL> + <ALT> + RMB : insert city or tile chat link. */
    /* <CONTROL> + <ALT> + <SHIFT> + RMB : insert unit chat link. */
    if (ptile && (state & GDK_MOD1_MASK)
        && (state & GDK_CONTROL_MASK)) {
      inputline_make_chat_link(ptile, (state & GDK_SHIFT_MASK) != 0);
    } else if ((state & GDK_SHIFT_MASK) && (state & GDK_MOD1_MASK)) {
      /* <SHIFT> + <ALT> + RMB : Show/hide workers. */
      key_city_overlay(e_x, e_y);
    } else if ((state & GDK_SHIFT_MASK) && (state & GDK_CONTROL_MASK)
               && pcity != NULL) {
      /* <SHIFT + CONTROL> + RMB: Paste Production. */
      clipboard_paste_production(pcity);
      cancel_tile_hiliting();
    } else if (state & GDK_SHIFT_MASK
               && clipboard_copy_production(ptile)) {
      /* <SHIFT> + RMB on city/unit: Copy Production. */
      /* If nothing to copy, fall through to rectangle selection. */
      
      /* Already done the copy */
    } else if (state & GDK_CONTROL_MASK) {
      /* <CONTROL> + RMB : Quickselect a land unit. */
      action_button_pressed(e_x, e_y, SELECT_LAND);
    } else {
      /* Plain RMB click. Area selection. */
      /*  A foolproof user will depress button on canvas,
       *  release it on another widget, and return to canvas
       *  to find rectangle still active.
       */
      if (rectangle_active) {
        release_right_button(e_x, e_y,
                             (state & GDK_SHIFT_MASK) != 0);
        return TRUE;
      }
      if (hover_state == HOVER_NONE) {
        anchor_selection_rectangle(e_x, e_y);
        rbutton_down = TRUE; /* causes rectangle updates */
      }
    }
    break;

  default:
    break;
  }

  return TRUE;
}

/**********************************************************************//**
  Update goto line so that destination is at current mouse pointer location.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  int x, y;
  GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(toplevel));
  GdkDevice *pointer = gdk_seat_get_pointer(seat);
  GdkSurface *window;

  if (!pointer) {
    return;
  }

  window = gdk_device_get_surface_at_position(pointer, &x, &y);
  if (window) {
    if (window == gtk_widget_get_surface(map_canvas)) {
      update_line(x, y);
    } else if (window == gtk_widget_get_surface(overview_canvas)) {
      overview_update_line(x, y);
    }
  }
}

/**********************************************************************//**
  The Area Selection rectangle. Called by center_tile_mapcanvas() and
  when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  int x, y;
  GdkSurface *window;
  GdkDevice *pointer;
  GdkModifierType mask;
  GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(toplevel));

  pointer = gdk_seat_get_pointer(seat);
  if (!pointer) {
    return;
  }

  window = gdk_device_get_surface_at_position(pointer, &x, &y);
  if (window && window == gtk_widget_get_surface(map_canvas)) {
    gdk_device_get_state(pointer, window, NULL, &mask);
    if (mask & GDK_BUTTON3_MASK) {
      update_selection_rectangle(x, y);
    }
  }
}

/**********************************************************************//**
  Triggered by the mouse moving on the mapcanvas, this function will
  update the mouse cursor and goto lines.
**************************************************************************/
gboolean move_mapcanvas(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  GdkModifierType state;
  gdouble e_x, e_y;

  if (GUI_GTK_OPTION(mouse_over_map_focus)
      && !gtk_widget_has_focus(map_canvas)) {
    gtk_widget_grab_focus(map_canvas);
  }

  if (editor_is_active()) {
    return handle_edit_mouse_move(ev);
  }

  gdk_event_get_coords(ev, &e_x, &e_y);
  cur_x = e_x;
  cur_y = e_y;
  update_line(e_x, e_y);
  gdk_event_get_state(ev, &state);
  if (rbutton_down && (state & GDK_BUTTON3_MASK)) {
    update_selection_rectangle(e_x, e_y);
  }

  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(e_x, e_y);
  }
  control_mouse_cursor(canvas_pos_to_tile(e_x, e_y));

  return TRUE;
}

/**********************************************************************//**
  This function will reset the mouse cursor if it leaves the map.
**************************************************************************/
gboolean leave_mapcanvas(GtkWidget *widget, GdkEvent *ev)
{
  gdouble e_x, e_y;

  if (gtk_notebook_get_current_page(GTK_NOTEBOOK(top_notebook))
      != gtk_notebook_page_num(GTK_NOTEBOOK(top_notebook), map_widget)) {
    /* Map is not currently topmost tab. Do not use tile specific cursors. */
    update_mouse_cursor(CURSOR_DEFAULT);
    return TRUE;
  }

  /* Bizarrely, this function can be called even when we don't "leave"
   * the map canvas, for instance, it gets called any time the mouse is
   * clicked. */
  gdk_event_get_coords(ev, &e_x, &e_y);
  if (!map_is_empty()
      && e_x >= 0 && e_y >= 0
      && e_x < mapview.width && e_y < mapview.height) {
    control_mouse_cursor(canvas_pos_to_tile(e_x, e_y));
  } else {
    update_mouse_cursor(CURSOR_DEFAULT);
  }

  update_unit_info_label(get_units_in_focus());
  return TRUE;
}

/**********************************************************************//**
  Overview canvas moved
**************************************************************************/
gboolean move_overviewcanvas(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  gdouble e_x, e_y;

  gdk_event_get_coords(ev, &e_x, &e_y);
  overview_update_line(e_x, e_y);

  return TRUE;
}

/**********************************************************************//**
  Button pressed at overview
**************************************************************************/
gboolean butt_down_overviewcanvas(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  int xtile, ytile;
  gdouble e_x, e_y;
  guint button;

  if (gdk_event_get_event_type(ev) != GDK_BUTTON_PRESS) {
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */
  }

  gdk_event_get_coords(ev, &e_x, &e_y);
  overview_to_map_pos(&xtile, &ytile, e_x, e_y);

  gdk_event_get_button(ev, &button);
  if (can_client_change_view() && (button == 3)) {
    center_tile_mapcanvas(map_pos_to_tile(&(wld.map), xtile, ytile));
  } else if (can_client_issue_orders() && (button == 1)) {
    GdkModifierType state;

    gdk_event_get_state(ev, &state);
    do_map_click(map_pos_to_tile(&(wld.map), xtile, ytile),
                 (state & GDK_SHIFT_MASK) ? SELECT_APPEND : SELECT_POPUP);
  }

  return TRUE;
}

/**********************************************************************//**
  Best effort to center the map on the currently selected unit(s)
**************************************************************************/
void center_on_unit(void)
{
  request_center_focus_unit();
}
