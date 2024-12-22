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
#include "zoom.h"

/* client/agents */
#include "cma_core.h"

/* client/gui-gtk-5.0 */
#include "chatline.h"
#include "citydlg.h"
#include "colors.h"
#include "dialogs.h"
#include "editgui.h"
#include "graphics.h"
#include "gui_main.h"
#include "infradlg.h"
#include "inputdlg.h"
#include "mapview.h"
#include "menu.h"
#include "rallypointdlg.h"

#include "mapctrl.h"

extern gint cur_x, cur_y;

/**********************************************************************//**
  Popup a label with information about the tile, unit, city, when the user
  used the middle mouse button on the map.
**************************************************************************/
static void popit(struct tile *ptile)
{
  if (TILE_UNKNOWN != client_tile_get_known(ptile)) {
    GtkWidget *p;
    struct unit *punit;
    GdkRectangle rect;
    GtkAllocation alloc;
    float canvas_x, canvas_y;

    tile_to_canvas_pos(&canvas_x, &canvas_y, map_zoom, ptile);
    gtk_widget_get_allocation(map_canvas, &alloc);
    rect.x = (canvas_x * mouse_zoom / map_zoom) - alloc.x;
    rect.y = (canvas_y * mouse_zoom / map_zoom) - alloc.y;
    rect.width = tileset_full_tile_width(tileset);
    rect.height = tileset_tile_height(tileset);

    p = gtk_popover_new();

    gtk_widget_set_parent(p, map_canvas);
    gtk_popover_set_pointing_to(GTK_POPOVER(p), &rect);
    gtk_popover_set_child(GTK_POPOVER(p), gtk_label_new(popup_info_text(ptile)));

    punit = find_visible_unit(ptile);

    if (punit) {
      mapdeco_set_gotoroute(punit);
      if (punit->goto_tile) {
        mapdeco_set_crosshair(punit->goto_tile, TRUE);
      }
    }
    mapdeco_set_crosshair(ptile, TRUE);

    g_signal_connect(p, "closed",
                     G_CALLBACK(popupinfo_popdown_callback), NULL);

    gtk_popover_popup(GTK_POPOVER(p));
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
  suggestname = suggestion of the new city's name
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

  update_turn_done_tooltip();
}

/**********************************************************************//**
  Handle 'Left mouse button released'. Because of the quickselect feature,
  the release of both left and right mousebutton can launch the goto.
**************************************************************************/
gboolean left_butt_up_mapcanvas(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer data)
{
  if (editor_is_active()) {
    return handle_edit_mouse_button_release(gesture, MOUSE_BUTTON_LEFT,
                                            x, y);
  }

  release_goto_button(x, y);

  return TRUE;
}

/**********************************************************************//**
  Handle 'Right mouse button released'. Because of the quickselect feature,
  the release of both left and right mousebutton can launch the goto.
**************************************************************************/
gboolean right_butt_up_mapcanvas(GtkGestureClick *gesture, int n_press,
                                 double x, double y, gpointer data)
{
  if (editor_is_active()) {
    return handle_edit_mouse_button_release(gesture, MOUSE_BUTTON_RIGHT,
                                            x, y);
  }

  release_goto_button(x, y);

  if (rbutton_down || hover_state != HOVER_NONE)  {
    GdkModifierType state;

    state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    release_right_button(x, y,
                         (state & GDK_SHIFT_MASK) != 0);
  }

  return TRUE;
}

/**********************************************************************//**
  Handle all left mouse button presses on canvas.
  Future feature: User-configurable mouse clicks.
**************************************************************************/
gboolean left_butt_down_mapcanvas(GtkGestureClick *gesture, int n_press,
                                  double x, double y, gpointer data)
{
  struct tile *ptile = NULL;
  GdkModifierType state;

  if (editor_is_active()) {
    return handle_edit_mouse_button_press(gesture, MOUSE_BUTTON_LEFT,
                                          x, y);
  }

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(map_canvas);
  ptile = canvas_pos_to_tile(x, y, mouse_zoom);

  state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

  /* <SHIFT> + <CONTROL> + LMB : Adjust workers. */
  if ((state & GDK_SHIFT_MASK) && (state & GDK_CONTROL_MASK)) {
    adjust_workers_button_pressed(x, y);
  } else if (state & GDK_CONTROL_MASK) {
    /* <CONTROL> + LMB : Quickselect a sea unit. */
    action_button_pressed(x, y, SELECT_SEA);
  } else if (ptile && (state & GDK_SHIFT_MASK)) {
    /* <SHIFT> + LMB: Append focus unit. */
    action_button_pressed(x, y, SELECT_APPEND);
  } else if (ptile && (state & GDK_ALT_MASK)) {
    /* <ALT> + LMB: popit (same as middle-click) */
    /* FIXME: we need a general mechanism for letting freeciv work with
     * 1- or 2-button mice. */
    popit(ptile);
  } else if (tiles_hilited_cities) {
    /* LMB in Area Selection mode. */
    if (ptile) {
      toggle_tile_hilite(ptile);
    }
  } else if (rally_set_tile(ptile)) {
    /* Nothing here, rally_set_tile() already did what we wanted */
  } else if (infra_placement_mode()) {
    infra_placement_set_tile(ptile);
  } else {
    /* Plain LMB click. */
    action_button_pressed(x, y, SELECT_POPUP);
  }

  return TRUE;
}

/**********************************************************************//**
  Handle all right mouse button presses on canvas.
  Future feature: User-configurable mouse clicks.
**************************************************************************/
gboolean right_butt_down_mapcanvas(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer data)
{
  struct city *pcity = NULL;
  struct tile *ptile = NULL;
  GdkModifierType state;

  if (editor_is_active()) {
    return handle_edit_mouse_button_press(gesture, MOUSE_BUTTON_RIGHT,
                                          x, y);
  }

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(map_canvas);
  ptile = canvas_pos_to_tile(x, y, mouse_zoom);
  pcity = ptile ? tile_city(ptile) : NULL;

  state = gtk_event_controller_get_current_event_state(
                                    GTK_EVENT_CONTROLLER(gesture));

  /* <CONTROL> + <ALT> + RMB : insert city or tile chat link. */
  /* <CONTROL> + <ALT> + <SHIFT> + RMB : insert unit chat link. */
  if (ptile && (state & GDK_ALT_MASK)
      && (state & GDK_CONTROL_MASK)) {
    inputline_make_chat_link(ptile, (state & GDK_SHIFT_MASK) != 0);
  } else if ((state & GDK_SHIFT_MASK) && (state & GDK_ALT_MASK)) {
    /* <SHIFT> + <ALT> + RMB : Show/hide workers. */
    key_city_overlay(x, y);
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
    action_button_pressed(x, y, SELECT_LAND);
  } else {
    /* Plain RMB click. Area selection. */
    /*  A foolproof user will depress button on canvas,
     *  release it on another widget, and return to canvas
     *  to find rectangle still active.
     */
    if (rectangle_active) {
      release_right_button(x, y,
                           (state & GDK_SHIFT_MASK) != 0);
      return TRUE;
    }
    if (hover_state == HOVER_NONE) {
      anchor_selection_rectangle(x, y);
      rbutton_down = TRUE; /* Causes rectangle updates */
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Handle all middle mouse button presses on canvas.
  Future feature: User-configurable mouse clicks.
**************************************************************************/
gboolean middle_butt_down_mapcanvas(GtkGestureClick *gesture, int n_press,
                                    double x, double y, gpointer data)
{
  struct tile *ptile = NULL;
  GdkModifierType state;

  if (editor_is_active()) {
    return handle_edit_mouse_button_press(gesture, MOUSE_BUTTON_MIDDLE,
                                          x, y);
  }

  if (!can_client_change_view()) {
    return TRUE;
  }

  gtk_widget_grab_focus(map_canvas);
  ptile = canvas_pos_to_tile(x, y, mouse_zoom);

  state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

  /* <CONTROL> + MMB: Wake up sentries. */
  if (state & GDK_CONTROL_MASK) {
    wakeup_button_pressed(x, y);
  } else if (ptile) {
    /* Plain Middle click. */
    popit(ptile);
  }

  return TRUE;
}

/**********************************************************************//**
  Update goto line so that destination is at current mouse pointer location.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  double x, y;
  GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(toplevel));
  GdkDevice *pointer = gdk_seat_get_pointer(seat);
  GdkSurface *window;

  if (!pointer) {
    return;
  }

  window = gdk_device_get_surface_at_position(pointer, &x, &y);
  if (window) {
    if (window == gtk_native_get_surface(gtk_widget_get_native(map_canvas))) {
      update_line(x, y);
    } else if (window == gtk_native_get_surface(gtk_widget_get_native(overview_canvas))) {
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
  double x, y;
  GdkSurface *window;
  GdkDevice *pointer;
  GdkDevice *keyboard;
  GdkModifierType mask;
  GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(toplevel));

  pointer = gdk_seat_get_pointer(seat);
  if (!pointer) {
    return;
  }

  window = gdk_device_get_surface_at_position(pointer, &x, &y);
  if (window
      && window == gtk_native_get_surface(gtk_widget_get_native(map_canvas))) {
    keyboard = gdk_seat_get_keyboard(seat);
    mask = gdk_device_get_modifier_state(keyboard);
    if (mask & GDK_BUTTON3_MASK) {
      update_selection_rectangle(x, y);
    }
  }
}

/**********************************************************************//**
  Triggered by the mouse moving on the mapcanvas, this function will
  update the mouse cursor and goto lines.
**************************************************************************/
gboolean move_mapcanvas(GtkEventControllerMotion *controller,
                        gdouble x, gdouble y, gpointer data)
{
  GdkModifierType state;

  if (GUI_GTK_OPTION(mouse_over_map_focus)
      && !gtk_widget_has_focus(map_canvas)) {
    gtk_widget_grab_focus(map_canvas);
  }

  if (editor_is_active()) {
    return handle_edit_mouse_move(controller, x, y);
  }

  cur_x = x;
  cur_y = y;
  update_line(x, y);
  state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
  if (rbutton_down && (state & GDK_BUTTON3_MASK)) {
    update_selection_rectangle(x, y);
  }

  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(x, y);
  }
  control_mouse_cursor(canvas_pos_to_tile(x, y, mouse_zoom));

  return TRUE;
}

/**********************************************************************//**
  This function will reset the mouse cursor if it leaves the map.
**************************************************************************/
gboolean leave_mapcanvas(GtkEventControllerMotion *controller,
                         gpointer data)
{
  update_mouse_cursor(CURSOR_DEFAULT);
  update_unit_info_label(get_units_in_focus());

  return TRUE;
}

/**********************************************************************//**
  Overview canvas moved
**************************************************************************/
gboolean move_overviewcanvas(GtkEventControllerMotion *controller,
                             gdouble x, gdouble y, gpointer data)
{
  overview_update_line(x, y);

  return TRUE;
}

/**********************************************************************//**
  Left button pressed at overview
**************************************************************************/
gboolean left_butt_down_overviewcanvas(GtkGestureClick *gesture, int n_press,
                                       double x, double y, gpointer data)
{
  int xtile, ytile;

  if (n_press != 1) {
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */
  }

  overview_to_map_pos(&xtile, &ytile, x, y);

  if (can_client_issue_orders()) {
    GdkModifierType state;

    state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    do_map_click(map_pos_to_tile(&(wld.map), xtile, ytile),
                 (state & GDK_SHIFT_MASK) ? SELECT_APPEND : SELECT_POPUP);
  }

  return TRUE;
}

/**********************************************************************//**
  Right button pressed at overview
**************************************************************************/
gboolean right_butt_down_overviewcanvas(GtkGestureClick *gesture, int n_press,
                                        double x, double y, gpointer data)
{
  int xtile, ytile;

  if (n_press != 1) {
    return TRUE; /* Double-clicks? Triple-clicks? No thanks! */
  }

  overview_to_map_pos(&xtile, &ytile, x, y);

  if (can_client_change_view()) {
    center_tile_mapcanvas(map_pos_to_tile(&(wld.map), xtile, ytile));
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
