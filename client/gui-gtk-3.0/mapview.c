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
#include <fc_config.h>
#endif

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

/* common */
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "player.h"

/* client */
#include "client_main.h"
#include "climap.h"
#include "climisc.h"
#include "colors.h"
#include "control.h" /* get_unit_in_focus() */
#include "editor.h"
#include "options.h"
#include "overview_common.h"
#include "tilespec.h"
#include "text.h"

/* client/gui-gtk-3.0 */
#include "citydlg.h" /* For reset_city_dialogs() */
#include "editgui.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "repodlgs.h"
#include "wldlg.h"

#include "mapview.h"

static GtkAdjustment *map_hadj, *map_vadj;
static int cursor_timer_id = 0, cursor_type = -1, cursor_frame = 0;
static int mapview_frozen_level = 0;

/**************************************************************************
  If do_restore is FALSE it will invert the turn done button style. If
  called regularly from a timer this will give a blinking turn done
  button. If do_restore is TRUE this will reset the turn done button
  to the default style.
**************************************************************************/
void update_turn_done_button(bool do_restore)
{
  static bool flip = FALSE;

  if (!get_turn_done_button_state()) {
    return;
  }

  if ((do_restore && flip) || !do_restore) {
    GdkColor *fore = &gtk_widget_get_style(turn_done_button)->bg[GTK_STATE_NORMAL];
    GdkColor *back = &gtk_widget_get_style(turn_done_button)->light[GTK_STATE_NORMAL];

    gtk_widget_get_style(turn_done_button)->bg[GTK_STATE_NORMAL] = *back;
    gtk_widget_get_style(turn_done_button)->light[GTK_STATE_NORMAL] = *fore;

    gtk_expose_now(turn_done_button);

    flip = !flip;
  }
}

/**************************************************************************
  Timeout label requires refreshing
**************************************************************************/
void update_timeout_label(void)
{
  gtk_label_set_text(GTK_LABEL(timeout_label), get_timeout_label_text());
}

/**************************************************************************
  Refresh info label
**************************************************************************/
void update_info_label(void)
{
  GtkWidget *label;
  const struct player *pplayer = client.conn.playing;

  label = gtk_frame_get_label_widget(GTK_FRAME(main_frame_civ_name));
  if (pplayer != NULL) {
    const gchar *name;
    gunichar c;

    /* Capitalize the first character of the translated nation
     * plural name so that the frame label looks good. */
    name = nation_plural_for_player(pplayer);
    c = g_utf8_get_char_validated(name, -1);
    if ((gunichar) -1 != c && (gunichar) -2 != c) {
      gchar nation[MAX_LEN_NAME];
      gchar *next;
      gint len;

      len = g_unichar_to_utf8(g_unichar_toupper(c), nation);
      nation[len] = '\0';
      next = g_utf8_find_next_char(name, NULL);
      if (NULL != next) {
        sz_strlcat(nation, next);
      }
      gtk_label_set_text(GTK_LABEL(label), nation);
    } else {
      gtk_label_set_text(GTK_LABEL(label), name);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(label), "-");
  }

  gtk_label_set_text(GTK_LABEL(main_label_info),
                     get_info_label_text(!gui_gtk3_small_display_layout));

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      client_government_sprite());

  if (NULL != client.conn.playing) {
    int d = 0;

    for (; d < client.conn.playing->economic.luxury /10; d++) {
      struct sprite *sprite = get_tax_sprite(tileset, O_LUXURY);

      gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(econ_label[d]), sprite);
    }
 
    for (; d < (client.conn.playing->economic.science
		+ client.conn.playing->economic.luxury) / 10; d++) {
      struct sprite *sprite = get_tax_sprite(tileset, O_SCIENCE);

      gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(econ_label[d]), sprite);
    }
 
    for (; d < 10; d++) {
      struct sprite *sprite = get_tax_sprite(tileset, O_GOLD);

      gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(econ_label[d]), sprite);
    }
  }
 
  update_timeout_label();

  /* update tooltips. */
  gtk_widget_set_tooltip_text(econ_ebox,
		       _("Shows your current luxury/science/tax rates; "
			 "click to toggle them."));

  gtk_widget_set_tooltip_text(bulb_ebox, get_bulb_tooltip());
  gtk_widget_set_tooltip_text(sun_ebox, get_global_warming_tooltip());
  gtk_widget_set_tooltip_text(flake_ebox, get_nuclear_winter_tooltip());
  gtk_widget_set_tooltip_text(government_ebox, get_government_tooltip());
}

/**************************************************************************
  This function is used to animate the mouse cursor. 
**************************************************************************/
static gboolean anim_cursor_cb(gpointer data)
{
  if (!cursor_timer_id) {
    return FALSE;
  }

  cursor_frame++;
  if (cursor_frame == NUM_CURSOR_FRAMES) {
    cursor_frame = 0;
  }

  if (cursor_type == CURSOR_DEFAULT) {
    gdk_window_set_cursor(root_window, NULL);
    cursor_timer_id = 0;
    return FALSE; 
  }

  gdk_window_set_cursor(root_window,
                fc_cursors[cursor_type][cursor_frame]);
  control_mouse_cursor(NULL);
  return TRUE;
}

/**************************************************************************
  This function will change the current mouse cursor.
**************************************************************************/
void update_mouse_cursor(enum cursor_type new_cursor_type)
{
  cursor_type = new_cursor_type;
  if (!cursor_timer_id) {
    cursor_timer_id = g_timeout_add(CURSOR_INTERVAL, anim_cursor_cb, NULL);
  }
}

/**************************************************************************
  Update the information label which gives info on the current unit and the
  square under the current unit, for specified unit.  Note that in practice
  punit is always the focus unit.
  Clears label if punit is NULL.
  Also updates the cursor for the map_canvas (this is related because the
  info label includes a "select destination" prompt etc).
  Also calls update_unit_pix_label() to update the icons for units on this
  square.
**************************************************************************/
void update_unit_info_label(struct unit_list *punits)
{
  GtkWidget *label;

  label = gtk_frame_get_label_widget(GTK_FRAME(unit_info_frame));
  gtk_label_set_text(GTK_LABEL(label),
		     get_unit_info_label_text1(punits));

  gtk_label_set_text(GTK_LABEL(unit_info_label),
		     get_unit_info_label_text2(punits, 0));

  update_unit_pix_label(punits);
}


/**************************************************************************
  Get sprite for treaty acceptance or rejection.
**************************************************************************/
GdkPixbuf *get_thumb_pixbuf(int onoff)
{
  return sprite_get_pixbuf(get_treaty_thumb_sprite(tileset, BOOL_VAL(onoff)));
}

/****************************************************************************
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
****************************************************************************/
void set_indicator_icons(struct sprite *bulb, struct sprite *sol,
			 struct sprite *flake, struct sprite *gov)
{
  gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(bulb_label), bulb);
  gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(sun_label), sol);
  gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(flake_label), flake);
  gtk_pixcomm_set_from_sprite(GTK_PIXCOMM(government_label), gov);
}

/****************************************************************************
  Return the maximum dimensions of the area (container widget) for the
  overview. Due to the fact that the scaling factor is at least 1, the real
  size could be larger. The calculation in calculate_overview_dimensions()
  limit it to the smallest possible size.
****************************************************************************/
void get_overview_area_dimensions(int *width, int *height)
{
  *width = GUI_GTK_OVERVIEW_MIN_XSIZE;
  *height = GUI_GTK_OVERVIEW_MIN_YSIZE;
}

/**************************************************************************
  Size of overview changed
**************************************************************************/
void overview_size_changed(void)
{
  gtk_widget_set_size_request(overview_canvas,
			      overview.width, overview.height);
  update_map_canvas_scrollbars_size();
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
#if 0
  static struct canvas store;

  store.surface = NULL;
  store.drawable = gdk_cairo_create(gtk_widget_get_window(overview_canvas));

  return &store;
#endif /* 0 */

  return NULL;
}

/**************************************************************************
  Redraw overview canvas
**************************************************************************/
gboolean overview_canvas_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
  gpointer source = (can_client_change_view()) ?
                     (gpointer)overview.window : (gpointer)radar_gfx_sprite;

  if (source) {
    cairo_surface_t *surface = (can_client_change_view()) ?
                                overview.window->surface :
                                radar_gfx_sprite->surface;

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
  }
  return TRUE;
}

/****************************************************************************
  Freeze the drawing of the map.
****************************************************************************/
void mapview_freeze(void)
{
  mapview_frozen_level++;
}

/****************************************************************************
  Thaw the drawing of the map.
****************************************************************************/
void mapview_thaw(void)
{
  if (1 < mapview_frozen_level) {
    mapview_frozen_level--;
  } else {
    fc_assert(0 < mapview_frozen_level);
    mapview_frozen_level = 0;
    dirty_all();
  }
}

/****************************************************************************
  Return whether the map should be drawn or not.
****************************************************************************/
bool mapview_is_frozen(void)
{
  return (0 < mapview_frozen_level);
}

/**************************************************************************
  Update on canvas widget size change
**************************************************************************/
gboolean map_canvas_configure(GtkWidget *w, GdkEventConfigure *ev,
                              gpointer data)
{
  map_canvas_resized(ev->width, ev->height);
  return TRUE;
}

/**************************************************************************
  Redraw map canvas.
**************************************************************************/
gboolean map_canvas_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
  if (can_client_change_view() && map_exists() && !mapview_is_frozen()) {
    /* First we mark the area to be updated as dirty.  Then we unqueue
     * any pending updates, to make sure only the most up-to-date data
     * is written (otherwise drawing bugs happen when old data is copied
     * to screen).  Then we draw all changed areas to the screen. */
    unqueue_mapview_updates(FALSE);
    cairo_set_source_surface(cr, mapview.store->surface, 0, 0);
    cairo_paint(cr);
  }
  return TRUE;
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
                     int pixel_width, int pixel_height)
{
  GdkRectangle rectangle = {canvas_x, canvas_y, pixel_width, pixel_height};
  if (gtk_widget_get_realized(map_canvas) && !mapview_is_frozen()) {
    gdk_window_invalidate_rect(gtk_widget_get_window(map_canvas), &rectangle, FALSE);
  }
}

/**************************************************************************
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
**************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
                int pixel_width, int pixel_height)
{
  GdkRectangle rectangle = {canvas_x, canvas_y, pixel_width, pixel_height};
  if (gtk_widget_get_realized(map_canvas)) {
    gdk_window_invalidate_rect(gtk_widget_get_window(map_canvas), &rectangle, FALSE);
  }
}

/**************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
**************************************************************************/
void dirty_all(void)
{
  if (gtk_widget_get_realized(map_canvas)) {
    gdk_window_invalidate_rect(gtk_widget_get_window(map_canvas), NULL, FALSE);
  }
}

/**************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
**************************************************************************/
void flush_dirty(void)
{
  gdk_window_process_updates(gtk_widget_get_window(map_canvas), FALSE);
}

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
}

/**************************************************************************
 Update display of descriptions associated with cities on the main map.
**************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
  Fill pixcomm with unit gfx
**************************************************************************/
void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p)
{
  struct canvas canvas_store = FC_STATIC_CANVAS_INIT;

  canvas_store.surface = gtk_pixcomm_get_surface(p);

  gtk_pixcomm_clear(p);

  put_unit(punit, &canvas_store, 0, 0);
}


/**************************************************************************
  FIXME:
  For now only two food, two gold one shield and two masks can be drawn per
  unit, the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_gpixmap_city_overlays(struct unit *punit, GtkPixcomm *p,
                                    int *upkeep_cost, int happy_cost)
{
  struct canvas store = FC_STATIC_CANVAS_INIT;
 
  store.surface = gtk_pixcomm_get_surface(p);

  put_unit_city_overlays(punit, &store, 0, tileset_tile_height(tileset),
                         upkeep_cost, happy_cost);
}

/**************************************************************************
  Put overlay tile to pixmap
**************************************************************************/
void pixmap_put_overlay_tile(GdkWindow *pixmap,
			     int canvas_x, int canvas_y,
			     struct sprite *ssprite)
{
  cairo_t *cr;

  if (!ssprite) {
    return;
  }

  cr = gdk_cairo_create(pixmap);
  cairo_set_source_surface(cr, ssprite->surface, canvas_x, canvas_y);
  cairo_paint(cr);
  cairo_destroy(cr);
}

/**************************************************************************
  Only used for isometric view.
**************************************************************************/
void pixmap_put_overlay_tile_draw(struct canvas *pcanvas,
				  int canvas_x, int canvas_y,
				  struct sprite *ssprite,
				  bool fog)
{
  cairo_t *cr;
  int sswidth, ssheight;

  if (!ssprite) {
    return;
  }

  get_sprite_dimensions(ssprite, &sswidth, &ssheight);
  canvas_put_sprite(pcanvas, canvas_x, canvas_y, ssprite,
		    0, 0, sswidth, ssheight);

  if (fog) {
    if (!pcanvas->drawable) {
      cr = cairo_create(pcanvas->surface);
    } else {
      cr = pcanvas->drawable;
    }

    if (pcanvas->drawable) {
      cairo_save(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_HSL_COLOR);
    cairo_set_source_rgb(cr, 0.65, 0.65, 0.65);
    cairo_fill(cr);

    if (!pcanvas->drawable) {
      cairo_destroy(cr);
    } else {
      cairo_restore(cr);
    }
  }
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  int canvas_x, canvas_y;

  if (tile_to_canvas_pos(&canvas_x, &canvas_y, ptile)) {
    pixmap_put_overlay_tile(gtk_widget_get_window(map_canvas),
			    canvas_x, canvas_y,
			    get_attention_crosshair_sprite(tileset));
  }
}

/*****************************************************************************
 Sets the position of the overview scroll window based on mapview position.
*****************************************************************************/
void update_overview_scroll_window_pos(int x, int y)
{
  gdouble ov_scroll_x, ov_scroll_y;
  GtkAdjustment *ov_hadj, *ov_vadj;

  ov_hadj = gtk_scrolled_window_get_hadjustment(
    GTK_SCROLLED_WINDOW(overview_scrolled_window));
  ov_vadj = gtk_scrolled_window_get_vadjustment(
    GTK_SCROLLED_WINDOW(overview_scrolled_window));

  ov_scroll_x = MIN(x - (overview_canvas_store_width / 2),
                    gtk_adjustment_get_upper(ov_hadj)
                    - gtk_adjustment_get_page_size(ov_hadj));
  ov_scroll_y = MIN(y - (overview_canvas_store_height / 2),
                    gtk_adjustment_get_upper(ov_vadj)
                    - gtk_adjustment_get_page_size(ov_vadj));

  gtk_adjustment_set_value(GTK_ADJUSTMENT(ov_hadj), ov_scroll_x);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(ov_vadj), ov_scroll_y);
}

/**************************************************************************
  Refresh map canvas scrollbars
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  int scroll_x, scroll_y;

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_hadj), scroll_x);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_vadj), scroll_y);
  if (can_client_change_view()) {
    gtk_widget_queue_draw(overview_canvas);
  }
}

/**************************************************************************
  Refresh map canvas scrollbar as canvas size changes
**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  int xmin, ymin, xmax, ymax, xsize, ysize, xstep, ystep;

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  get_mapview_scroll_step(&xstep, &ystep);

  map_hadj = (GtkAdjustment*)gtk_adjustment_new(-1, xmin, xmax, xstep, xsize, xsize);
  map_vadj = (GtkAdjustment*)gtk_adjustment_new(-1, ymin, ymax, ystep, ysize, ysize);

  gtk_range_set_adjustment(GTK_RANGE(map_horizontal_scrollbar),
	GTK_ADJUSTMENT(map_hadj));
  gtk_range_set_adjustment(GTK_RANGE(map_vertical_scrollbar),
	GTK_ADJUSTMENT(map_vadj));

  g_signal_connect(map_hadj, "value_changed",
	G_CALLBACK(scrollbar_jump_callback),
	GINT_TO_POINTER(TRUE));
  g_signal_connect(map_vadj, "value_changed",
	G_CALLBACK(scrollbar_jump_callback),
	GINT_TO_POINTER(FALSE));
}

/**************************************************************************
  Scrollbar has moved
**************************************************************************/
void scrollbar_jump_callback(GtkAdjustment *adj, gpointer hscrollbar)
{
  int scroll_x, scroll_y;

  if (!can_client_change_view()) {
    return;
  }

  get_mapview_scroll_pos(&scroll_x, &scroll_y);

  if (hscrollbar) {
    scroll_x = gtk_adjustment_get_value(adj);
  } else {
    scroll_y = gtk_adjustment_get_value(adj);
  }

  set_mapview_scroll_pos(scroll_x, scroll_y);
}

/**************************************************************************
  Draws a rectangle with top left corner at (canvas_x, canvas_y), and
  width 'w' and height 'h'. It is drawn using the 'selection_gc' context,
  so the pixel combining function is XOR. This means that drawing twice
  in the same place will restore the image to its original state.

  NB: A side effect of this function is to set the 'selection_gc' color
  to COLOR_MAPVIEW_SELECTION.
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  double dashes[2] = {4.0, 4.0};
  struct color *pcolor;
  cairo_t *cr;

  if (w == 0 || h == 0) {
    return;
  }

  pcolor = get_color(tileset, COLOR_MAPVIEW_SELECTION);
  if (!pcolor) {
    return;
  }

  cr = gdk_cairo_create(gtk_widget_get_window(map_canvas));
  gdk_cairo_set_source_rgba(cr, &pcolor->color);
  cairo_set_line_width(cr, 2.0);
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
  cairo_rectangle(cr, canvas_x, canvas_y, w, h);
  cairo_stroke(cr);
  cairo_destroy(cr);
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  science_report_dialog_redraw();
  reset_city_dialogs();
  reset_unit_table();
  blank_max_unit_size();
  editgui_tileset_changed();

  /* keep the icon of the executable on Windows (see PR#36491) */
#ifndef WIN32_NATIVE
  gtk_window_set_icon(GTK_WINDOW(toplevel),
		sprite_get_pixbuf(get_icon_sprite(tileset, ICON_FREECIV)));
#endif
}
