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
#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "colors.h"
#include "control.h" /* get_unit_in_focus() */
#include "goto.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"

#include "citydlg.h" /* For reset_city_dialogs() */
#include "mapview.h"

static void pixmap_put_overlay_tile(GdkDrawable *pixmap,
				    int canvas_x, int canvas_y,
				    struct Sprite *ssprite);
static void put_overlay_tile_gpixmap(GtkPixcomm *p,
				     int canvas_x, int canvas_y,
				     struct Sprite *ssprite);
static void put_line(GdkDrawable *pm, int x, int y, int dir);

static void pixmap_put_overlay_tile_draw(GdkDrawable *pixmap,
					 int canvas_x, int canvas_y,
					 struct Sprite *ssprite,
					 int offset_x, int offset_y,
					 int width, int height,
					 bool fog);
static void really_draw_segment(int src_x, int src_y, int dir,
				bool write_to_screen, bool force);
static void pixmap_put_tile_iso(GdkDrawable *pm, int x, int y,
				int canvas_x, int canvas_y,
				int citymode,
				int offset_x, int offset_y, int offset_y_unit,
				int width, int height, int height_unit,
				enum draw_type draw);
static void pixmap_put_black_tile_iso(GdkDrawable *pm,
				      int canvas_x, int canvas_y,
				      int offset_x, int offset_y,
				      int width, int height);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
static SPRITE *scaled_intro_sprite = NULL;

static GtkObject *map_hadj, *map_vadj;


/**************************************************************************
 This function is called to decrease a unit's HP smoothly in battle
 when combat_animation is turned on.
**************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1)
{
  static struct timer *anim_timer = NULL; 
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int i;

  set_units_in_combat(punit0, punit1);

  do {
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (punit0->hp > hp0
	&& myrand((punit0->hp - hp0) + (punit1->hp - hp1)) < punit0->hp - hp0)
      punit0->hp--;
    else if (punit1->hp > hp1)
      punit1->hp--;
    else
      punit0->hp--;

    refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
    refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);

    gdk_flush();
    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);

  for (i = 0; i < num_tiles_explode_unit; i++) {
    int canvas_x, canvas_y;
    get_canvas_xy(losing_unit->x, losing_unit->y, &canvas_x, &canvas_y);
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);
    if (is_isometric) {
      /* We first draw the explosion onto the unit and draw draw the
	 complete thing onto the map canvas window. This avoids flickering. */
      gdk_draw_drawable(single_tile_pixmap, civ_gc, map_canvas_store,
			canvas_x, canvas_y,
			0, 0,
			NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      pixmap_put_overlay_tile(single_tile_pixmap,
			      NORMAL_TILE_WIDTH/4, 0,
			      sprites.explode.unit[i]);
      gdk_draw_drawable(map_canvas->window, civ_gc, single_tile_pixmap,
			0, 0,
			canvas_x, canvas_y,
			NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    } else { /* is_isometric */
      /* FIXME: maybe do as described in the above comment. */
      struct canvas_store store = {single_tile_pixmap};

      put_one_tile(&store, losing_unit->x, losing_unit->y,
		   0, 0, FALSE);
      put_unit_full(losing_unit, &store, 0, 0);
      pixmap_put_overlay_tile(single_tile_pixmap, 0, 0,
			      sprites.explode.unit[i]);

      gdk_draw_drawable(map_canvas->window, civ_gc, single_tile_pixmap,
			0, 0,
			canvas_x, canvas_y,
			UNIT_TILE_WIDTH,
			UNIT_TILE_HEIGHT);
    }
    gdk_flush();
    usleep_since_timer_start(anim_timer, 20000);
  }

  set_units_in_combat(NULL, NULL);
  refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
  refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
}

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
    GdkGC *fore = turn_done_button->style->bg_gc[GTK_STATE_NORMAL];
    GdkGC *back = turn_done_button->style->light_gc[GTK_STATE_NORMAL];

    turn_done_button->style->bg_gc[GTK_STATE_NORMAL] = back;
    turn_done_button->style->light_gc[GTK_STATE_NORMAL] = fore;

    gtk_expose_now(turn_done_button);

    flip = !flip;
  }
}

/**************************************************************************
...
**************************************************************************/
void update_timeout_label(void)
{
  char buffer[512];

  if (game.timeout <= 0)
    sz_strlcpy(buffer, Q_("?timeout:off"));
  else
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);
  gtk_set_label(timeout_label, buffer);
}

/**************************************************************************
...
**************************************************************************/
void update_info_label( void )
{
  char buffer	[512];
  int  d;
  int  sol, flake;

  gtk_frame_set_label(GTK_FRAME(main_frame_civ_name),
		      get_nation_name(game.player_ptr->nation));

  my_snprintf(buffer, sizeof(buffer),
	      _("Population: %s\nYear: %s\n"
		"Gold: %d\nTax: %d Lux: %d Sci: %d"),
	      population_to_text(civ_population(game.player_ptr)),
	      textyear(game.year), game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);

  gtk_set_label(main_label_info, buffer);

  sol = client_warming_sprite();
  flake = client_cooling_sprite();
  set_indicator_icons(client_research_sprite(),
		      sol,
		      flake,
		      game.player_ptr->government);

  d=0;
  for (; d < game.player_ptr->economic.luxury /10; d++) {
    struct Sprite *sprite = get_citizen_sprite(CITIZEN_ELVIS, d, NULL);
    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  for (; d < (game.player_ptr->economic.science
	     + game.player_ptr->economic.luxury) / 10; d++) {
    struct Sprite *sprite = get_citizen_sprite(CITIZEN_SCIENTIST, d, NULL);
    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  for (; d < 10; d++) {
    struct Sprite *sprite = get_citizen_sprite(CITIZEN_TAXMAN, d, NULL);
    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  update_timeout_label();

  /* update tooltips. */
  gtk_tooltips_set_tip(main_tips, econ_ebox,
		       _("Shows your current luxury/science/tax rates;"
			 "click to toggle them."), "");

  my_snprintf(buffer, sizeof(buffer),
	      _("Shows your progress in researching "
		"the current technology.\n"
		"%s: %d/%d."),
		advances[game.player_ptr->research.researching].name,
		game.player_ptr->research.bulbs_researched,
		total_bulbs_required(game.player_ptr));
  gtk_tooltips_set_tip(main_tips, bulb_ebox, buffer, "");
  
  my_snprintf(buffer, sizeof(buffer),
	      _("Shows the progress of global warming:\n"
		"%d."),
	      sol);
  gtk_tooltips_set_tip(main_tips, sun_ebox, buffer, "");

  my_snprintf(buffer, sizeof(buffer),
	      _("Shows the progress of nuclear winter:\n"
		"%d."),
	      flake);
  gtk_tooltips_set_tip(main_tips, flake_ebox, buffer, "");

  my_snprintf(buffer, sizeof(buffer),
	      _("Shows your current government:\n"
		"%s."),
	      get_government_name(game.player_ptr->government));
  gtk_tooltips_set_tip(main_tips, government_ebox, buffer, "");
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
void update_unit_info_label(struct unit *punit)
{
  if(punit) {
    char buffer[512];
    struct city *pcity =
	player_find_city_by_id(game.player_ptr, punit->homecity);
    int infrastructure =
	get_tile_infrastructure_set(map_get_tile(punit->x, punit->y));

    my_snprintf(buffer, sizeof(buffer), "%s %s", 
            unit_type(punit)->name,
            (punit->veteran) ? _("(veteran)") : "" );
    gtk_frame_set_label( GTK_FRAME(unit_info_frame), buffer);


    my_snprintf(buffer, sizeof(buffer), "%s\n%s\n%s%s%s%s",
		(hover_unit == punit->id) ?
		_("Select destination") : unit_activity_text(punit),
		map_get_tile_info_text(punit->x, punit->y),
		infrastructure ?
		map_get_infrastructure_text(infrastructure) : "",
		infrastructure ? "\n" : "", pcity ? pcity->name : "",
		infrastructure ? "" : "\n");
    gtk_set_label( unit_info_label, buffer);

    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE);

    switch (hover_state) {
    case HOVER_NONE:
      gdk_window_set_cursor (root_window, NULL);
      break;
    case HOVER_PATROL:
      gdk_window_set_cursor (root_window, patrol_cursor);
      break;
    case HOVER_GOTO:
    case HOVER_CONNECT:
      gdk_window_set_cursor (root_window, goto_cursor);
      break;
    case HOVER_NUKE:
      gdk_window_set_cursor (root_window, nuke_cursor);
      break;
    case HOVER_PARADROP:
      gdk_window_set_cursor (root_window, drop_cursor);
      break;
    }
  } else {
    gtk_frame_set_label( GTK_FRAME(unit_info_frame),"");
    gtk_set_label(unit_info_label,"\n\n\n");
    gdk_window_set_cursor(root_window, NULL);
  }
  update_unit_pix_label(punit);
}


/**************************************************************************
...
**************************************************************************/
GdkPixmap *get_thumb_pixmap(int onoff)
{
  return sprites.treaty_thumb[BOOL_VAL(onoff)]->pixmap;
}

/**************************************************************************
...
**************************************************************************/
void set_indicator_icons(int bulb, int sol, int flake, int gov)
{
  struct Sprite *gov_sprite;

  bulb = CLIP(0, bulb, NUM_TILES_PROGRESS-1);
  sol = CLIP(0, sol, NUM_TILES_PROGRESS-1);
  flake = CLIP(0, flake, NUM_TILES_PROGRESS-1);

  gtk_image_set_from_pixmap(GTK_IMAGE(bulb_label),
			    sprites.bulb[bulb]->pixmap, NULL);
  gtk_image_set_from_pixmap(GTK_IMAGE(sun_label),
			    sprites.warming[sol]->pixmap, NULL);
  gtk_image_set_from_pixmap(GTK_IMAGE(flake_label),
			    sprites.cooling[flake]->pixmap, NULL);

  if (game.government_count==0) {
    /* not sure what to do here */
    gov_sprite = get_citizen_sprite(CITIZEN_UNHAPPY, 0, NULL);
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  gtk_image_set_from_pixmap(GTK_IMAGE(government_label),
			    gov_sprite->pixmap, NULL);
}

/**************************************************************************
  Draw a single frame of animation.  This function needs to clear the old
  image and draw the new one.  It must flush output to the display.
**************************************************************************/
void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y)
{
  struct canvas_store store = {single_tile_pixmap};

  /* Clear old sprite. */
  gdk_draw_drawable(map_canvas->window, civ_gc, map_canvas_store, old_canvas_x,
		    old_canvas_y, old_canvas_x, old_canvas_y, UNIT_TILE_WIDTH,
		    UNIT_TILE_HEIGHT);

  /* Draw the new sprite. */
  gdk_draw_drawable(single_tile_pixmap, civ_gc, map_canvas_store, new_canvas_x,
		    new_canvas_y, 0, 0, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
  put_unit_full(punit, &store, 0, 0);

  /* Write to screen. */
  gdk_draw_drawable(map_canvas->window, civ_gc, single_tile_pixmap, 0, 0,
		    new_canvas_x, new_canvas_y, UNIT_TILE_WIDTH,
		    UNIT_TILE_HEIGHT);

  /* Flush. */
  gdk_flush();
}

/**************************************************************************
...
**************************************************************************/
void set_overview_dimensions(int x, int y)
{
  overview_canvas_store_width = OVERVIEW_TILE_WIDTH * x;
  overview_canvas_store_height = OVERVIEW_TILE_HEIGHT * y;

  if (overview_canvas_store)
    g_object_unref(overview_canvas_store);
  
  overview_canvas_store	= gdk_pixmap_new(root_window,
			  overview_canvas_store_width,
			  overview_canvas_store_height, -1);

  gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
  gdk_draw_rectangle(overview_canvas_store, fill_bg_gc, TRUE,
		     0, 0,
		     overview_canvas_store_width, overview_canvas_store_height);

  gtk_widget_set_size_request(overview_canvas,
			      OVERVIEW_TILE_WIDTH  * x,
			      OVERVIEW_TILE_HEIGHT * y);
  update_map_canvas_scrollbars_size();
}

/**************************************************************************
...
**************************************************************************/
gboolean overview_canvas_expose(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
  if (!can_client_change_view()) {
    if (radar_gfx_sprite) {
      gdk_draw_drawable(overview_canvas->window, civ_gc,
			radar_gfx_sprite->pixmap, ev->area.x, ev->area.y,
			ev->area.x, ev->area.y,
			ev->area.width, ev->area.height);
    }
    return TRUE;
  }
  
  refresh_overview_viewrect();
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static void set_overview_tile_foreground_color(int x, int y)
{
  gdk_gc_set_foreground(fill_bg_gc,
			colors_standard[overview_tile_color(x, y)]);
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_canvas(void)
{
  struct canvas_store store = {overview_canvas_store};

  base_refresh_overview_canvas(&store);
}


/**************************************************************************
...
**************************************************************************/
void overview_update_tile(int x, int y)
{
  int overview_x, overview_y, base_x, base_y;

  map_to_overview_pos(&overview_x, &overview_y, x, y);
  map_to_base_overview_pos(&base_x, &base_y, x, y);

  set_overview_tile_foreground_color(x, y);
  gdk_draw_rectangle(overview_canvas_store, fill_bg_gc, TRUE,
		     base_x, base_y,
		     OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT);
  
  gdk_draw_rectangle(overview_canvas->window, fill_bg_gc, TRUE,
		     overview_x, overview_y,
		     OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT);
}

/**************************************************************************
...
**************************************************************************/
void refresh_overview_viewrect(void)
{
  int x0 = OVERVIEW_TILE_WIDTH * map_overview_x0;
  int x1 = OVERVIEW_TILE_WIDTH * (map.xsize - map_overview_x0);
  int y0 = OVERVIEW_TILE_HEIGHT * map_overview_y0;
  int y1 = OVERVIEW_TILE_HEIGHT * (map.ysize - map_overview_y0);
  int gui_x[4], gui_y[4], i;

  /* (map_overview_x0, map_overview_y0) splits the map into four
   * rectangles.  Draw each of these rectangles to the screen, in turn. */
  gdk_draw_drawable(overview_canvas->window, civ_gc, overview_canvas_store,
		    x0, y0, 0, 0, x1, y1);
  gdk_draw_drawable(overview_canvas->window, civ_gc, overview_canvas_store,
		    0, y0, x1, 0, x0, y1);
  gdk_draw_drawable(overview_canvas->window, civ_gc, overview_canvas_store,
		    x0, 0, 0, y1, x1, y0);
  gdk_draw_drawable(overview_canvas->window, civ_gc, overview_canvas_store,
		    0, 0, x1, y1, x0, y0);

  /* Now draw the mapview window rectangle onto the overview. */
  gdk_gc_set_foreground(civ_gc, colors_standard[COLOR_STD_WHITE]);
  get_mapview_corners(gui_x, gui_y);
  for (i = 0; i < 4; i++) {
    int src_x = gui_x[i];
    int src_y = gui_y[i];
    int dest_x = gui_x[(i + 1) % 4];
    int dest_y = gui_y[(i + 1) % 4];

    gdk_draw_line(overview_canvas->window, civ_gc,
		  src_x, src_y, dest_x, dest_y);
  }
}

/**************************************************************************
...
**************************************************************************/
static bool map_center = TRUE;
static bool map_configure = FALSE;

gboolean map_canvas_configure(GtkWidget *w, GdkEventConfigure *ev,
			      gpointer data)
{
  int tile_width, tile_height;

  tile_width = (ev->width + NORMAL_TILE_WIDTH - 1) / NORMAL_TILE_WIDTH;
  tile_height = (ev->height + NORMAL_TILE_HEIGHT - 1) / NORMAL_TILE_HEIGHT;

  mapview_canvas.width = ev->width;
  mapview_canvas.height = ev->height;

  if (map_canvas_store_twidth !=tile_width ||
      map_canvas_store_theight!=tile_height) { /* resized? */

    if (map_canvas_store) {
      g_object_unref(map_canvas_store);
    }

    map_canvas_store_twidth  = tile_width;
    map_canvas_store_theight = tile_height;

    map_canvas_store = gdk_pixmap_new(ev->window,
				      tile_width  * NORMAL_TILE_WIDTH,
				      tile_height * NORMAL_TILE_HEIGHT,
				      -1);

    if (!mapview_canvas.store) {
      mapview_canvas.store = fc_malloc(sizeof(*mapview_canvas.store));
    }
    mapview_canvas.store->pixmap = map_canvas_store;
    mapview_canvas.store->pixcomm = NULL;

    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_draw_rectangle(map_canvas_store, fill_bg_gc, TRUE, 0, 0, -1, -1);
    update_map_canvas_scrollbars_size();

    if (can_client_change_view()) {
      if (map_exists()) { /* do we have a map at all */
        update_map_canvas_visible();
        update_map_canvas_scrollbars();
        refresh_overview_viewrect();
      }
    }
    
    map_configure = TRUE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
gboolean map_canvas_expose(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
  static bool cleared = FALSE;

  if (!can_client_change_view()) {
    if (map_configure || !scaled_intro_sprite) {

      if (!intro_gfx_sprite) {
        load_intro_gfx();
      }

      if (scaled_intro_sprite) {
        free_sprite(scaled_intro_sprite);
      }

      scaled_intro_sprite = sprite_scale(intro_gfx_sprite,
					 w->allocation.width,
					 w->allocation.height);
    }

    if (scaled_intro_sprite) {
      gdk_draw_drawable(map_canvas->window, civ_gc,
			scaled_intro_sprite->pixmap,
			ev->area.x, ev->area.y, ev->area.x, ev->area.y,
			ev->area.width, ev->area.height);
      gtk_widget_queue_draw(overview_canvas);
      cleared = FALSE;
    } else {
      if (!cleared) {
	gtk_widget_queue_draw(w);
	cleared = TRUE;
      }
    }
    map_center = TRUE;
  }
  else
  {
    if (scaled_intro_sprite) {
      free_sprite(scaled_intro_sprite);
      scaled_intro_sprite = NULL;
    }

    if (map_exists()) { /* do we have a map at all */
      gdk_draw_drawable(map_canvas->window, civ_gc, map_canvas_store,
			ev->area.x, ev->area.y, ev->area.x, ev->area.y,
			ev->area.width, ev->area.height);
      cleared = FALSE;
    } else {
      if (!cleared) {
        gtk_widget_queue_draw(w);
	cleared = TRUE;
      }
    }

    if (!map_center) {
      center_on_something();
      map_center = FALSE;
    }
  }

  map_configure = FALSE;

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void pixmap_put_black_tile(GdkDrawable *pm,
			   int canvas_x, int canvas_y)
{
  gdk_gc_set_foreground( fill_bg_gc, colors_standard[COLOR_STD_BLACK] );
  gdk_draw_rectangle(pm, fill_bg_gc, TRUE,
		     canvas_x, canvas_y,
		     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
void put_one_tile_full(GdkDrawable *pm, int x, int y,
		       int canvas_x, int canvas_y, int citymode)
{
  pixmap_put_tile_iso(pm, x, y, canvas_x, canvas_y, citymode,
		      0, 0, 0,
		      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, UNIT_TILE_HEIGHT,
		      D_FULL);
}

/**************************************************************************
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  pixmap_put_tile_iso(map_canvas_store,
		      map_x, map_y, canvas_x, canvas_y,
		      FALSE,
		      offset_x, offset_y, offset_y_unit,
		      width, height, height_unit, draw);
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  gdk_draw_drawable(map_canvas->window, civ_gc, map_canvas_store,
		    canvas_x, canvas_y, canvas_x, canvas_y,
		    pixel_width, pixel_height);
}

/**************************************************************************
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
**************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height)
{
  /* GDK gives an error if we invalidate out-of-bounds parts of the
     window. */
  GdkRectangle rect = {MAX(canvas_x, 0), MAX(canvas_y, 0),
		       MIN(pixel_width,
			   map_canvas->allocation.width - canvas_x),
		       MIN(pixel_height,
			   map_canvas->allocation.height - canvas_y)};

  gdk_window_invalidate_rect(map_canvas->window, &rect, FALSE);
}

/**************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
**************************************************************************/
void dirty_all(void)
{
  GdkRectangle rect = {0, 0, map_canvas->allocation.width,
		       map_canvas->allocation.height};

  gdk_window_invalidate_rect(map_canvas->window, &rect, FALSE);
}

/**************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
**************************************************************************/
void flush_dirty(void)
{
  gdk_window_process_updates(map_canvas->window, FALSE);
}

/**************************************************************************
 Update display of descriptions associated with cities on the main map.
**************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/**************************************************************************
  If necessary, clear the city descriptions out of the buffer.
**************************************************************************/
void prepare_show_city_descriptions(void)
{
  /* Nothing to do */
}

/**************************************************************************
...
**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  static char buffer[512], buffer2[32];
  PangoRectangle rect, rect2;
  enum color_std color;
  int extra_width = 0;
  static PangoLayout *layout;

  if (!layout) {
    layout = pango_layout_new(gdk_pango_context_get());
  }

  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT;

  if (draw_city_names) {
    get_city_mapview_name_and_growth(pcity, buffer, sizeof(buffer),
				     buffer2, sizeof(buffer2), &color);

    pango_layout_set_font_description(layout, main_font);
    if (buffer2[0] != '\0') {
      /* HACK: put a character's worth of space between the two strings. */
      pango_layout_set_text(layout, "M", -1);
      pango_layout_get_pixel_extents(layout, &rect, NULL);
      extra_width = rect.width;
    }
    pango_layout_set_text(layout, buffer, -1);
    pango_layout_get_pixel_extents(layout, &rect, NULL);
    rect.width += extra_width;

    if (draw_city_growth && pcity->owner == game.player_idx) {
      /* We need to know the size of the growth text before
	 drawing anything. */
      pango_layout_set_font_description(layout, city_productions_font);
      pango_layout_set_text(layout, buffer2, -1);
      pango_layout_get_pixel_extents(layout, &rect2, NULL);

      /* Now return the layout to its previous state. */
      pango_layout_set_font_description(layout, main_font);
      pango_layout_set_text(layout, buffer, -1);
    } else {
      rect2.width = 0;
    }

    gtk_draw_shadowed_string(map_canvas_store,
			     toplevel->style->black_gc,
			     toplevel->style->white_gc,
			     canvas_x - (rect.width + rect2.width) / 2,
			     canvas_y + PANGO_ASCENT(rect), layout);

    if (draw_city_growth && pcity->owner == game.player_idx) {
      pango_layout_set_font_description(layout, city_productions_font);
      pango_layout_set_text(layout, buffer2, -1);
      gdk_gc_set_foreground(civ_gc, colors_standard[color]);
      gtk_draw_shadowed_string(map_canvas_store,
			       toplevel->style->black_gc,
			       civ_gc,
			       canvas_x - (rect.width + rect2.width) / 2
			       + rect.width,
			       canvas_y + PANGO_ASCENT(rect)
			       + rect.height / 2 - rect2.height / 2,
			       layout);
    }

    canvas_y += rect.height + 3;
  }

  if (draw_city_productions && (pcity->owner==game.player_idx)) {
    get_city_mapview_production(pcity, buffer, sizeof(buffer));

    pango_layout_set_font_description(layout, city_productions_font);
    pango_layout_set_text(layout, buffer, -1);

    pango_layout_get_pixel_extents(layout, &rect, NULL);
    gtk_draw_shadowed_string(map_canvas_store,
			     toplevel->style->black_gc,
			     toplevel->style->white_gc,
			     canvas_x - rect.width / 2,
			     canvas_y + PANGO_ASCENT(rect), layout);
  }
}

/**************************************************************************
...
**************************************************************************/
void put_city_tile_output(GdkDrawable *pm, int canvas_x, int canvas_y, 
			  int food, int shield, int trade)
{
  food = CLIP(0, food, NUM_TILES_DIGITS-1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS-1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS-1);
  
  if (is_isometric) {
    canvas_x += NORMAL_TILE_WIDTH/3;
    canvas_y -= NORMAL_TILE_HEIGHT/3;
  }

  pixmap_put_overlay_tile(pm, canvas_x, canvas_y,
			  sprites.city.tile_foodnum[food]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y,
			  sprites.city.tile_shieldnum[shield]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y,
			  sprites.city.tile_tradenum[trade]);
}

/**************************************************************************
...
**************************************************************************/
void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p)
{
  struct canvas_store canvas_store = {NULL, p};

  gtk_pixcomm_freeze(p);
  gtk_pixcomm_clear(p);

  put_unit_full(punit, &canvas_store, 0, 0);

  gtk_pixcomm_thaw(p);
}


/**************************************************************************
  FIXME:
  For now only two food, two gold one shield and two masks can be drawn per
  unit, the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_gpixmap_city_overlays(struct unit *punit, GtkPixcomm *p)
{
  int upkeep_food = CLIP(0, punit->upkeep_food, 2);
  int upkeep_gold = CLIP(0, punit->upkeep_gold, 2);
  int unhappy = CLIP(0, punit->unhappiness, 2);
 
  gtk_pixcomm_freeze(p);

  /* draw overlay pixmaps */
  if (punit->upkeep > 0)
    put_overlay_tile_gpixmap(p, 0, NORMAL_TILE_HEIGHT, sprites.upkeep.shield);
  if (upkeep_food > 0)
    put_overlay_tile_gpixmap(p, 0, NORMAL_TILE_HEIGHT, sprites.upkeep.food[upkeep_food-1]);
  if (upkeep_gold > 0)
    put_overlay_tile_gpixmap(p, 0, NORMAL_TILE_HEIGHT, sprites.upkeep.gold[upkeep_gold-1]);
  if (unhappy > 0)
    put_overlay_tile_gpixmap(p, 0, NORMAL_TILE_HEIGHT, sprites.upkeep.unhappy[unhappy-1]);

  gtk_pixcomm_thaw(p);
}

/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  if (is_isometric) {
    int canvas_x, canvas_y;
    struct Sprite *mysprite = sprites.explode.iso_nuke;

    get_canvas_xy(x, y, &canvas_x, &canvas_y);
    canvas_x += NORMAL_TILE_WIDTH/2 - mysprite->width/2;
    canvas_y += NORMAL_TILE_HEIGHT/2 - mysprite->height/2;

    pixmap_put_overlay_tile(map_canvas->window, canvas_x, canvas_y,
			    mysprite);

    gdk_flush();
    myusleep(1000000);

    update_map_canvas_visible();
  } else {
    int x_itr, y_itr;
    int canvas_x, canvas_y;

    for (y_itr=0; y_itr<3; y_itr++) {
      for (x_itr=0; x_itr<3; x_itr++) {
	struct Sprite *mysprite = sprites.explode.nuke[y_itr][x_itr];
	get_canvas_xy(x + x_itr - 1, y + y_itr - 1, &canvas_x, &canvas_y);

	gdk_draw_drawable(single_tile_pixmap, civ_gc, map_canvas_store,
			  canvas_x, canvas_y, 0, 0,
			  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
	pixmap_put_overlay_tile(single_tile_pixmap, 0, 0, mysprite);
	gdk_draw_drawable(map_canvas->window, civ_gc, single_tile_pixmap,
			  0, 0, canvas_x, canvas_y,
			  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      }
    }

    gdk_flush();
    myusleep(1000000);

    update_map_canvas(x-1, y-1, 3, 3, TRUE);
  }
}

/**************************************************************************
canvas_x, canvas_y is the top left corner of the pixmap.
**************************************************************************/
void pixmap_frame_tile_red(GdkDrawable *pm,
			   int canvas_x, int canvas_y)
{
  if (is_isometric) {
    gdk_gc_set_foreground(thick_line_gc, colors_standard[COLOR_STD_RED]);

    gdk_draw_line(pm, thick_line_gc,
		  canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y,
		  canvas_x+NORMAL_TILE_WIDTH-1, canvas_y+NORMAL_TILE_HEIGHT/2-1);
    gdk_draw_line(pm, thick_line_gc,
		  canvas_x+NORMAL_TILE_WIDTH-1, canvas_y+NORMAL_TILE_HEIGHT/2-1,
		  canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y+NORMAL_TILE_HEIGHT-1);
    gdk_draw_line(pm, thick_line_gc,
		  canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y+NORMAL_TILE_HEIGHT-1,
		  canvas_x, canvas_y + NORMAL_TILE_HEIGHT/2-1);
    gdk_draw_line(pm, thick_line_gc,
		  canvas_x, canvas_y + NORMAL_TILE_HEIGHT/2-1,
		  canvas_x+NORMAL_TILE_WIDTH/2-1, canvas_y);
  } else {
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_RED]);

    gdk_draw_rectangle(pm, fill_bg_gc, FALSE,
		       canvas_x, canvas_y,
		       NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1);
  }
}

/**************************************************************************
...
**************************************************************************/
static void put_overlay_tile_gpixmap(GtkPixcomm *p, int canvas_x, int canvas_y,
				     struct Sprite *ssprite)
{
  if (!ssprite)
    return;

  gtk_pixcomm_copyto(p, ssprite, canvas_x, canvas_y);
}

/**************************************************************************
...
**************************************************************************/
static void pixmap_put_overlay_tile(GdkDrawable *pixmap,
				    int canvas_x, int canvas_y,
				    struct Sprite *ssprite)
{
  if (!ssprite)
    return;
      
  gdk_gc_set_clip_origin(civ_gc, canvas_x, canvas_y);
  gdk_gc_set_clip_mask(civ_gc, ssprite->mask);

  gdk_draw_drawable(pixmap, civ_gc, ssprite->pixmap,
		    0, 0,
		    canvas_x, canvas_y,
		    ssprite->width, ssprite->height);
  gdk_gc_set_clip_mask(civ_gc, NULL);
}

/**************************************************************************
  Place part of a (possibly masked) sprite on a pixmap.
**************************************************************************/
static void pixmap_put_sprite(GdkDrawable *pixmap,
			      int pixmap_x, int pixmap_y,
			      struct Sprite *ssprite,
			      int offset_x, int offset_y,
			      int width, int height)
{
  if (ssprite->mask) {
    gdk_gc_set_clip_origin(civ_gc, pixmap_x, pixmap_y);
    gdk_gc_set_clip_mask(civ_gc, ssprite->mask);
  }

  gdk_draw_drawable(pixmap, civ_gc, ssprite->pixmap,
		    offset_x, offset_y,
		    pixmap_x + offset_x, pixmap_y + offset_y,
		    MIN(width, MAX(0, ssprite->width - offset_x)),
		    MIN(height, MAX(0, ssprite->height - offset_y)));

  gdk_gc_set_clip_mask(civ_gc, NULL);
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite(struct canvas_store *pcanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  if (pcanvas_store->pixmap) {
    pixmap_put_sprite(pcanvas_store->pixmap, canvas_x, canvas_y,
		      sprite, offset_x, offset_y, width, height);
  } else {
    gtk_pixcomm_copyto(pcanvas_store->pixcomm, sprite, canvas_x, canvas_y);
  }
} 

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite_full(struct canvas_store *pcanvas_store,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
    assert(sprite->pixmap);
  gui_put_sprite(pcanvas_store, canvas_x, canvas_y,
		 sprite,
		 0, 0, sprite->width, sprite->height);
}

/**************************************************************************
  Place a (possibly masked) sprite on a pixmap.
**************************************************************************/
void pixmap_put_sprite_full(GdkDrawable *pixmap,
			    int pixmap_x, int pixmap_y,
			    struct Sprite *ssprite)
{
  pixmap_put_sprite(pixmap, pixmap_x, pixmap_y, ssprite,
		    0, 0, ssprite->width, ssprite->height);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_rectangle(struct canvas_store *pcanvas_store,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  if (pcanvas_store->pixmap) {
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[color]);
    gdk_draw_rectangle(pcanvas_store->pixmap, fill_bg_gc, TRUE,
		       canvas_x, canvas_y, width, height);
  } else {
    gtk_pixcomm_fill(pcanvas_store->pixcomm, colors_standard[color]);
  }
}

/**************************************************************************
  Draw a colored line onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_line(struct canvas_store *pcanvas_store, enum color_std color,
		  enum line_type ltype, int start_x, int start_y,
		  int dx, int dy)
{
  GdkGC *gc;

  gc = (ltype == LINE_BORDER ? border_line_gc : civ_gc);
  gdk_gc_set_foreground(gc, colors_standard[color]);
  gdk_draw_line(pcanvas_store->pixmap, gc,
		start_x, start_y, start_x + dx, start_y + dy);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_overlay_tile_draw(GdkDrawable *pixmap,
					 int canvas_x, int canvas_y,
					 struct Sprite *ssprite,
					 int offset_x, int offset_y,
					 int width, int height,
					 bool fog)
{
  if (!ssprite || !width || !height)
    return;

  pixmap_put_sprite(pixmap, canvas_x, canvas_y, ssprite,
		    offset_x, offset_y, width, height);

  /* I imagine this could be done more efficiently. Some pixels We first
     draw from the sprite, and then draw black afterwards. It would be much
     faster to just draw every second pixel black in the first place. */
  if (fog) {
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, ssprite->mask);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_gc_set_stipple(fill_tile_gc, black50);

    gdk_draw_rectangle(pixmap, fill_tile_gc, TRUE,
		       canvas_x+offset_x, canvas_y+offset_y,
		       MIN(width, MAX(0, ssprite->width-offset_x)),
		       MIN(height, MAX(0, ssprite->height-offset_y)));
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);
  }
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(int x, int y)
{
  int canvas_x, canvas_y;
  get_canvas_xy(x, y, &canvas_x, &canvas_y);

  if (tile_visible_mapcanvas(x, y)) {
    pixmap_put_overlay_tile(map_canvas->window,
			    canvas_x, canvas_y,
			    sprites.user.attention);
  }
}

/**************************************************************************
...
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  int canvas_x, canvas_y;
  static struct city *last_pcity=NULL;

  if (color==-1) {
    if (pcity!=last_pcity)
      city_workers_color = city_workers_color%3 + 1;
    color=city_workers_color;
  }
  gdk_gc_set_foreground(fill_tile_gc, colors_standard[color]);

  city_map_checked_iterate(pcity->x, pcity->y, i, j, x, y) {
    enum city_tile_type worked = get_worker_city(pcity, i, j);

    get_canvas_xy(x, y, &canvas_x, &canvas_y);

    /* stipple the area */
    if (!is_city_center(i, j)) {
      if (worked == C_TILE_EMPTY) {
	gdk_gc_set_stipple(fill_tile_gc, gray25);
      } else if (worked == C_TILE_WORKER) {
	gdk_gc_set_stipple(fill_tile_gc, gray50);
      } else
	continue;

      if (is_isometric) {
	gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
	gdk_gc_set_clip_mask(fill_tile_gc, sprites.black_tile->mask);
	gdk_draw_drawable(map_canvas->window, fill_tile_gc, map_canvas_store,
			  canvas_x, canvas_y,
			  canvas_x, canvas_y,
			  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
	gdk_draw_rectangle(map_canvas->window, fill_tile_gc, TRUE,
			   canvas_x, canvas_y,
			   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
	gdk_gc_set_clip_mask(fill_tile_gc, NULL);
      } else {
	gdk_draw_drawable(map_canvas->window, civ_gc, map_canvas_store,
			  canvas_x, canvas_y,
			  canvas_x, canvas_y,
			  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
	gdk_draw_rectangle(map_canvas->window, fill_tile_gc, TRUE,
			   canvas_x, canvas_y,
			   NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      }
    }

    /* draw tile output */
    if (worked == C_TILE_WORKER) {
      put_city_tile_output(map_canvas->window,
			   canvas_x, canvas_y,
			   city_get_food_tile(i, j, pcity),
			   city_get_shields_tile(i, j, pcity),
			   city_get_trade_tile(i, j, pcity));
    }
  } city_map_checked_iterate_end;

  last_pcity=pcity;
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  int scroll_x, scroll_y;

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_hadj), scroll_x);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(map_vadj), scroll_y);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  int xmin, ymin, xmax, ymax, xsize, ysize;

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);

  map_hadj = gtk_adjustment_new(-1, xmin, xmax, 1, xsize, xsize);
  map_vadj = gtk_adjustment_new(-1, ymin, ymax, 1, ysize, ysize);

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
...
**************************************************************************/
void scrollbar_jump_callback(GtkAdjustment *adj, gpointer hscrollbar)
{
  int scroll_x, scroll_y;

  if (!can_client_change_view()) {
    return;
  }

  get_mapview_scroll_pos(&scroll_x, &scroll_y);

  if (hscrollbar) {
    scroll_x = adj->value;
  } else {
    scroll_y = adj->value;
  }

  set_mapview_scroll_pos(scroll_x, scroll_y);
}

  
/**************************************************************************
draw a line from src_x,src_y -> dest_x,dest_y on both map_canvas and
map_canvas_store
FIXME: We currently always draw the line.
Only used for isometric view.
**************************************************************************/
static void really_draw_segment(int src_x, int src_y, int dir,
				bool write_to_screen, bool force)
{
  int dest_x, dest_y, is_real;
  int canvas_start_x, canvas_start_y;
  int canvas_end_x, canvas_end_y;

  gdk_gc_set_foreground(thick_line_gc, colors_standard[COLOR_STD_CYAN]);

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  /* Find middle of tiles. y-1 to not undraw the the middle pixel of a
     horizontal line when we refresh the tile below-between. */
  get_canvas_xy(src_x, src_y, &canvas_start_x, &canvas_start_y);
  get_canvas_xy(dest_x, dest_y, &canvas_end_x, &canvas_end_y);
  canvas_start_x += NORMAL_TILE_WIDTH/2;
  canvas_start_y += NORMAL_TILE_HEIGHT/2-1;
  canvas_end_x += NORMAL_TILE_WIDTH/2;
  canvas_end_y += NORMAL_TILE_HEIGHT/2-1;

  /* somewhat hackish way of solving the problem where draw from a tile on
     one side of the screen out of the screen, and the tile we draw to is
     found to be on the other side of the screen. */
  if (abs(canvas_end_x - canvas_start_x) > NORMAL_TILE_WIDTH
      || abs(canvas_end_y - canvas_start_y) > NORMAL_TILE_HEIGHT)
    return;

  /* draw it! */
  gdk_draw_line(map_canvas_store, thick_line_gc,
		canvas_start_x, canvas_start_y, canvas_end_x, canvas_end_y);
  if (write_to_screen)
    gdk_draw_line(map_canvas->window, thick_line_gc,
		  canvas_start_x, canvas_start_y, canvas_end_x, canvas_end_y);
  return;
}

/**************************************************************************
...
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  assert(get_drawn(src_x, src_y, dir) > 0);

  if (is_isometric) {
    really_draw_segment(src_x, src_y, dir, TRUE, FALSE);
  } else {
    int dest_x, dest_y, is_real;

    is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
    assert(is_real);

    if (tile_visible_mapcanvas(src_x, src_y)) {
      put_line(map_canvas_store, src_x, src_y, dir);
      put_line(map_canvas->window, src_x, src_y, dir);
    }
    if (tile_visible_mapcanvas(dest_x, dest_y)) {
      put_line(map_canvas_store, dest_x, dest_y, DIR_REVERSE(dir));
      put_line(map_canvas->window, dest_x, dest_y, DIR_REVERSE(dir));
    }
  }
}

/**************************************************************************
 Area Selection
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  gdk_gc_set_foreground(civ_gc, colors_standard[COLOR_STD_YELLOW]);

  /* gdk_draw_rectangle() must start top-left.. */
  gdk_draw_line(map_canvas->window, civ_gc,
            canvas_x, canvas_y, canvas_x + w, canvas_y);
  gdk_draw_line(map_canvas->window, civ_gc,
            canvas_x, canvas_y, canvas_x, canvas_y + h);
  gdk_draw_line(map_canvas->window, civ_gc,
            canvas_x, canvas_y + h, canvas_x + w, canvas_y + h);
  gdk_draw_line(map_canvas->window, civ_gc,
            canvas_x + w, canvas_y, canvas_x + w, canvas_y + h);

  rectangle_active = TRUE;
}

/**************************************************************************
Not used in isometric view.
**************************************************************************/
static void put_line(GdkDrawable *pm, int x, int y, int dir)
{
  int canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y;
  get_canvas_xy(x, y, &canvas_src_x, &canvas_src_y);
  canvas_src_x += NORMAL_TILE_WIDTH/2;
  canvas_src_y += NORMAL_TILE_HEIGHT/2;
  DIRSTEP(canvas_dest_x, canvas_dest_y, dir);
  canvas_dest_x = canvas_src_x + (NORMAL_TILE_WIDTH * canvas_dest_x) / 2;
  canvas_dest_y = canvas_src_y + (NORMAL_TILE_WIDTH * canvas_dest_y) / 2;

  gdk_gc_set_foreground(civ_gc, colors_standard[COLOR_STD_CYAN]);

  gdk_draw_line(pm, civ_gc,
		canvas_src_x, canvas_src_y,
		canvas_dest_x, canvas_dest_y);
}

/**************************************************************************
  Put a drawn sprite (with given offset) onto the pixmap.
**************************************************************************/
static void pixmap_put_drawn_sprite(GdkDrawable *pixmap,
				    int canvas_x, int canvas_y,
				    struct drawn_sprite *pdsprite,
				    int offset_x, int offset_y,
				    int width, int height,
				    bool fog)
{
  int ox = pdsprite->offset_x, oy = pdsprite->offset_y;

  pixmap_put_overlay_tile_draw(pixmap, canvas_x + ox, canvas_y + oy,
			       pdsprite->sprite,
			       offset_x - ox, offset_y - oy,
			       width - ox, height - oy,
			       fog);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void put_city_pixmap_draw(struct city *pcity, GdkPixmap *pm,
				 int canvas_x, int canvas_y,
				 int offset_x, int offset_y_unit,
				 int width, int height_unit,
				 bool fog)
{
  struct drawn_sprite sprites[80];
  int count = fill_city_sprite_array_iso(sprites, pcity);
  int i;

  for (i=0; i<count; i++) {
    if (sprites[i].sprite) {
      pixmap_put_drawn_sprite(pm, canvas_x, canvas_y, &sprites[i],
			      offset_x, offset_y_unit, width, height_unit,
			      fog);
    }
  }
}
/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_black_tile_iso(GdkDrawable *pm,
				      int canvas_x, int canvas_y,
				      int offset_x, int offset_y,
				      int width, int height)
{
  gdk_gc_set_clip_origin(civ_gc, canvas_x, canvas_y);
  gdk_gc_set_clip_mask(civ_gc, sprites.black_tile->mask);

  assert(width <= NORMAL_TILE_WIDTH);
  assert(height <= NORMAL_TILE_HEIGHT);
  gdk_draw_drawable(pm, civ_gc, sprites.black_tile->pixmap,
		    offset_x, offset_y,
		    canvas_x+offset_x, canvas_y+offset_y,
		    width, height);

  gdk_gc_set_clip_mask(civ_gc, NULL);
}

/**************************************************************************
Blend the tile with neighboring tiles.
Only used for isometric view.
**************************************************************************/
static void dither_tile(GdkDrawable *pixmap, struct Sprite **dither,
			int canvas_x, int canvas_y,
			int offset_x, int offset_y,
			int width, int height, bool fog)
{
  if (!width || !height)
    return;

  gdk_gc_set_clip_mask(civ_gc, sprites.dither_tile->mask);
  gdk_gc_set_clip_origin(civ_gc, canvas_x, canvas_y);
  assert(offset_x == 0 || offset_x == NORMAL_TILE_WIDTH/2);
  assert(offset_y == 0 || offset_y == NORMAL_TILE_HEIGHT/2);
  assert(width == NORMAL_TILE_WIDTH || width == NORMAL_TILE_WIDTH/2);
  assert(height == NORMAL_TILE_HEIGHT || height == NORMAL_TILE_HEIGHT/2);

  /* north */
  if (dither[0]
      && (offset_x != 0 || width == NORMAL_TILE_WIDTH)
      && (offset_y == 0)) {
    gdk_draw_drawable(pixmap, civ_gc, dither[0]->pixmap,
		      NORMAL_TILE_WIDTH/2, 0,
		      canvas_x + NORMAL_TILE_WIDTH/2, canvas_y,
		      NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2);
  }

  /* south */
  if (dither[1] && offset_x == 0
      && (offset_y == NORMAL_TILE_HEIGHT/2 || height == NORMAL_TILE_HEIGHT)) {
    gdk_draw_drawable(pixmap, civ_gc, dither[1]->pixmap,
		      0, NORMAL_TILE_HEIGHT/2,
		      canvas_x,
		      canvas_y + NORMAL_TILE_HEIGHT/2,
		      NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2);
  }

  /* east */
  if (dither[2]
      && (offset_x != 0 || width == NORMAL_TILE_WIDTH)
      && (offset_y != 0 || height == NORMAL_TILE_HEIGHT)) {
    gdk_draw_drawable(pixmap, civ_gc, dither[2]->pixmap,
		      NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2,
		      canvas_x + NORMAL_TILE_WIDTH/2,
		      canvas_y + NORMAL_TILE_HEIGHT/2,
		      NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2);
  }

  /* west */
  if (dither[3] && offset_x == 0 && offset_y == 0) {
    gdk_draw_drawable(pixmap, civ_gc, dither[3]->pixmap,
		      0, 0,
		      canvas_x,
		      canvas_y,
		      NORMAL_TILE_WIDTH/2, NORMAL_TILE_HEIGHT/2);
  }

  gdk_gc_set_clip_mask(civ_gc, NULL);

  if (fog) {
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, sprites.dither_tile->mask);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_gc_set_stipple(fill_tile_gc, black50);

    gdk_draw_rectangle(pixmap, fill_tile_gc, TRUE,
		       canvas_x+offset_x, canvas_y+offset_y,
		       MIN(width, MAX(0, NORMAL_TILE_WIDTH-offset_x)),
		       MIN(height, MAX(0, NORMAL_TILE_HEIGHT-offset_y)));
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);
  }
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_tile_iso(GdkDrawable *pm, int x, int y,
				int canvas_x, int canvas_y,
				int citymode,
				int offset_x, int offset_y, int offset_y_unit,
				int width, int height, int height_unit,
				enum draw_type draw)
{
  struct drawn_sprite tile_sprs[80];
  struct Sprite *coasts[4];
  struct Sprite *dither[4];
  struct city *pcity;
  struct unit *punit, *pfocus;
  enum tile_special_type special;
  int count, i = 0;
  bool solid_bg, fog, tile_hilited;
  struct canvas_store canvas_store = {pm};

  if (!width || !(height || height_unit))
    return;

  count = fill_tile_sprite_array_iso(tile_sprs, coasts, dither,
				     x, y, citymode, &solid_bg);

  if (count == -1) { /* tile is unknown */
    pixmap_put_black_tile_iso(pm, canvas_x, canvas_y,
			      offset_x, offset_y, width, height);
    return;
  }

  /* Replace with check for is_normal_tile later */
  assert(is_real_map_pos(x, y));
  normalize_map_pos(&x, &y);

  fog = tile_get_known(x, y) == TILE_KNOWN_FOGGED && draw_fog_of_war;
  pcity = map_get_city(x, y);
  punit = get_drawable_unit(x, y, citymode);
  pfocus = get_unit_in_focus();
  special = map_get_special(x, y);
  tile_hilited = (map_get_tile(x,y)->client.hilite != HILITE_NONE);

  if (solid_bg) {
    gdk_gc_set_clip_origin(fill_bg_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_bg_gc, sprites.black_tile->mask);
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_BACKGROUND]);

    gdk_draw_rectangle(pm, fill_bg_gc, TRUE,
		       canvas_x+offset_x, canvas_y+offset_y,
		       MIN(width, MAX(0, sprites.black_tile->width-offset_x)),
		       MIN(height, MAX(0, sprites.black_tile->height-offset_y)));
    gdk_gc_set_clip_mask(fill_bg_gc, NULL);
    if (fog) {
      gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
      gdk_gc_set_clip_mask(fill_tile_gc, sprites.black_tile->mask);
      gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
      gdk_gc_set_stipple(fill_tile_gc, black50);

      gdk_draw_rectangle(pm, fill_tile_gc, TRUE,
			 canvas_x+offset_x, canvas_y+offset_y,
			 MIN(width, MAX(0, sprites.black_tile->width-offset_x)),
			 MIN(height, MAX(0, sprites.black_tile->height-offset_y)));
      gdk_gc_set_clip_mask(fill_tile_gc, NULL);
    }
  }

  if (draw_terrain) {
    if (is_ocean(map_get_terrain(x, y))) { /* coasts */
      int dx, dy;
      /* top */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      pixmap_put_overlay_tile_draw(pm, canvas_x + NORMAL_TILE_WIDTH/4,
				   canvas_y, coasts[0],
				   MAX(0, dx),
				   offset_y,
				   MAX(0, width-MAX(0, -dx)),
				   height,
				   fog);
      /* bottom */
      dx = offset_x-NORMAL_TILE_WIDTH/4;
      dy = offset_y-NORMAL_TILE_HEIGHT/2;
      pixmap_put_overlay_tile_draw(pm, canvas_x + NORMAL_TILE_WIDTH/4,
				   canvas_y + NORMAL_TILE_HEIGHT/2, coasts[1],
				   MAX(0, dx),
				   MAX(0, dy),
				   MAX(0, width-MAX(0, -dx)),
				   MAX(0, height-MAX(0, -dy)),
				   fog);
      /* left */
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      pixmap_put_overlay_tile_draw(pm, canvas_x,
				   canvas_y + NORMAL_TILE_HEIGHT/4, coasts[2],
				   offset_x,
				   MAX(0, dy),
				   width,
				   MAX(0, height-MAX(0, -dy)),
				   fog);
      /* right */
      dx = offset_x-NORMAL_TILE_WIDTH/2;
      dy = offset_y-NORMAL_TILE_HEIGHT/4;
      pixmap_put_overlay_tile_draw(pm, canvas_x + NORMAL_TILE_WIDTH/2,
				   canvas_y + NORMAL_TILE_HEIGHT/4, coasts[3],
				   MAX(0, dx),
				   MAX(0, dy),
				   MAX(0, width-MAX(0, -dx)),
				   MAX(0, height-MAX(0, -dy)),
				   fog);
    } else {
      pixmap_put_drawn_sprite(pm, canvas_x, canvas_y, &tile_sprs[0],
			      offset_x, offset_y, width, height, fog);
      i++;
    }

    /*** Dither base terrain ***/
    if (draw_terrain)
      dither_tile(pm, dither, canvas_x, canvas_y,
		  offset_x, offset_y, width, height, fog);
  }

  /*** Rest of terrain and specials ***/
  for (; i<count; i++) {
    if (tile_sprs[i].sprite)
      pixmap_put_drawn_sprite(pm, canvas_x, canvas_y, &tile_sprs[i],
			      offset_x, offset_y, width, height, fog);
    else
      freelog(LOG_ERROR, "sprite is NULL");
  }

  /*** Area Selection hiliting ***/
  if (tile_hilited) {
    gdk_gc_set_foreground(thin_line_gc, colors_standard[COLOR_STD_YELLOW]);

    if (draw & D_M_R) {
      gdk_draw_line(pm, thin_line_gc,
            canvas_x + NORMAL_TILE_WIDTH / 2,
            canvas_y,
            canvas_x + NORMAL_TILE_WIDTH - 1,
            canvas_y + NORMAL_TILE_HEIGHT / 2 - 1);
    }
    if (draw & D_M_L) {
      gdk_draw_line(pm, thin_line_gc,
            canvas_x,
            canvas_y + NORMAL_TILE_HEIGHT / 2 - 1,
            canvas_x + NORMAL_TILE_WIDTH / 2 - 1,
            canvas_y);
    }
    /* The lower lines do not quite reach the tile's bottom;
     * they would be too obscured by overlapping tiles' terrain. */
    if (draw & D_B_R) {
      gdk_draw_line(pm, thin_line_gc,
            canvas_x + NORMAL_TILE_WIDTH / 2,
            canvas_y + NORMAL_TILE_HEIGHT - 3,
            canvas_x + NORMAL_TILE_WIDTH - 1,
            canvas_y + NORMAL_TILE_HEIGHT / 2 - 1);
    }
    if (draw & D_B_L) {
      gdk_draw_line(pm, thin_line_gc,
            canvas_x,
            canvas_y + NORMAL_TILE_HEIGHT / 2 - 1,
            canvas_x + NORMAL_TILE_WIDTH / 2 - 1,
            canvas_y + NORMAL_TILE_HEIGHT - 3);
    }
  }

  /*** Map grid ***/
  if (draw_map_grid && !tile_hilited) {
    /* we draw the 2 lines on top of the tile; the buttom lines will be
       drawn by the tiles underneath. */
    if (draw & D_M_R) {
      gdk_gc_set_foreground(thin_line_gc,
			    colors_standard[get_grid_color
					    (x, y, x, y - 1)]);
      gdk_draw_line(pm, thin_line_gc,
		    canvas_x + NORMAL_TILE_WIDTH / 2, canvas_y,
		    canvas_x + NORMAL_TILE_WIDTH,
		    canvas_y + NORMAL_TILE_HEIGHT / 2);
    }

    if (draw & D_M_L) {
      gdk_gc_set_foreground(thin_line_gc,
			    colors_standard[get_grid_color
					    (x, y, x - 1, y)]);
      gdk_draw_line(pm, thin_line_gc,
		    canvas_x, canvas_y + NORMAL_TILE_HEIGHT / 2,
		    canvas_x + NORMAL_TILE_WIDTH / 2, canvas_y);
    }
  }

  /* National borders */
  tile_draw_borders_iso(&canvas_store, x, y, canvas_x, canvas_y, draw);

  if (draw_coastline && !draw_terrain) {
    enum tile_terrain_type t1 = map_get_terrain(x, y), t2;
    int x1, y1;
    gdk_gc_set_foreground(thin_line_gc, colors_standard[COLOR_STD_OCEAN]);
    x1 = x; y1 = y-1;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_R && (is_ocean(t1) ^ is_ocean(t2))) {
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x+NORMAL_TILE_WIDTH/2, canvas_y,
		      canvas_x+NORMAL_TILE_WIDTH, canvas_y+NORMAL_TILE_HEIGHT/2);
      }
    }
    x1 = x-1; y1 = y;
    if (normalize_map_pos(&x1, &y1)) {
      t2 = map_get_terrain(x1, y1);
      if (draw & D_M_L && (is_ocean(t1) ^ is_ocean(t2))) {
	gdk_draw_line(pm, thin_line_gc,
		      canvas_x, canvas_y + NORMAL_TILE_HEIGHT/2,
		      canvas_x+NORMAL_TILE_WIDTH/2, canvas_y);
      }
    }
  }

  /*** City and various terrain improvements ***/
  if (pcity && draw_cities) {
    put_city_pixmap_draw(pcity, pm,
			 canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
			 offset_x, offset_y_unit,
			 width, height_unit, fog);
  }
  if (contains_special(special, S_AIRBASE) && draw_fortress_airbase)
    pixmap_put_overlay_tile_draw(pm,
				 canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				 sprites.tx.airbase,
				 offset_x, offset_y_unit,
				 width, height_unit, fog);
  if (contains_special(special, S_FALLOUT) && draw_pollution)
    pixmap_put_overlay_tile_draw(pm,
				 canvas_x, canvas_y,
				 sprites.tx.fallout,
				 offset_x, offset_y,
				 width, height, fog);
  if (contains_special(special, S_POLLUTION) && draw_pollution)
    pixmap_put_overlay_tile_draw(pm,
				 canvas_x, canvas_y,
				 sprites.tx.pollution,
				 offset_x, offset_y,
				 width, height, fog);

  /*** city size ***/
  /* Not fogged as it would be unreadable */
  if (pcity && draw_cities) {
    if (pcity->size>=10)
      pixmap_put_overlay_tile_draw(pm, canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				   sprites.city.size_tens[pcity->size/10],
				   offset_x, offset_y_unit,
				   width, height_unit, 0);

    pixmap_put_overlay_tile_draw(pm, canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				 sprites.city.size[pcity->size%10],
				 offset_x, offset_y_unit,
				 width, height_unit, 0);
  }

  /*** Unit ***/
  if (punit && (draw_units || (punit == pfocus && draw_focus_unit))) {
    put_unit(punit, &canvas_store,
	     canvas_x, canvas_y - NORMAL_TILE_HEIGHT/2,
	     offset_x, offset_y_unit,
	     width, height_unit);
    if (!pcity && unit_list_size(&map_get_tile(x, y)->units) > 1)
      pixmap_put_overlay_tile_draw(pm,
				   canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				   sprites.unit.stack,
				   offset_x, offset_y_unit,
				   width, height_unit, fog);
  }

  if (contains_special(special, S_FORTRESS) && draw_fortress_airbase)
    pixmap_put_overlay_tile_draw(pm,
				 canvas_x, canvas_y-NORMAL_TILE_HEIGHT/2,
				 sprites.tx.fortress,
				 offset_x, offset_y_unit,
				 width, height_unit, fog);
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  reset_city_dialogs();
  reset_unit_table();

  /* single_tile is originally allocated in gui_main.c. */
  g_object_unref(single_tile_pixmap);
  single_tile_pixmap = gdk_pixmap_new(root_window, 
				      UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, -1);
}
