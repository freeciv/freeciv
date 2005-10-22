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
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "player.h"

#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "colors.h"
#include "control.h" /* get_unit_in_focus() */
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "options.h"
#include "tilespec.h"
#include "text.h"
#include "wldlg.h"

#include "citydlg.h" /* For reset_city_dialogs() */
#include "mapview.h"

#define map_canvas_store (mapview_canvas.store->v.pixmap)

static void pixmap_put_overlay_tile(GdkDrawable *pixmap,
				    int canvas_x, int canvas_y,
				    struct Sprite *ssprite);

static void pixmap_put_overlay_tile_draw(GdkDrawable *pixmap,
					 int canvas_x, int canvas_y,
					 struct Sprite *ssprite,
					 bool fog);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
static SPRITE *scaled_intro_sprite = NULL;

static GtkObject *map_hadj, *map_vadj;


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
  gtk_label_set_text(GTK_LABEL(timeout_label), get_timeout_label_text());
}

/**************************************************************************
...
**************************************************************************/
void update_info_label( void )
{
  int  d;
  int  sol, flake;
  GtkWidget *label;

  label = gtk_frame_get_label_widget(GTK_FRAME(main_frame_civ_name));
  gtk_label_set_text(GTK_LABEL(label),
		     get_nation_name(game.player_ptr->nation));

  gtk_label_set_text(GTK_LABEL(main_label_info), get_info_label_text());

  sol = client_warming_sprite();
  flake = client_cooling_sprite();
  set_indicator_icons(client_research_sprite(),
		      sol,
		      flake,
		      game.player_ptr->government);

  d=0;
  for (; d < game.player_ptr->economic.luxury /10; d++) {
    struct Sprite *sprite = sprites.tax_luxury;

    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  for (; d < (game.player_ptr->economic.science
	     + game.player_ptr->economic.luxury) / 10; d++) {
    struct Sprite *sprite = sprites.tax_science;

    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  for (; d < 10; d++) {
    struct Sprite *sprite = sprites.tax_gold;

    gtk_image_set_from_pixmap(GTK_IMAGE(econ_label[d]),
			      sprite->pixmap, sprite->mask);
  }
 
  update_timeout_label();

  /* update tooltips. */
  gtk_tooltips_set_tip(main_tips, econ_ebox,
		       _("Shows your current luxury/science/tax rates;"
			 "click to toggle them."), "");

  gtk_tooltips_set_tip(main_tips, bulb_ebox, get_bulb_tooltip(), "");
  gtk_tooltips_set_tip(main_tips, sun_ebox, get_global_warming_tooltip(),
		       "");
  gtk_tooltips_set_tip(main_tips, flake_ebox, get_nuclear_winter_tooltip(),
		       "");
  gtk_tooltips_set_tip(main_tips, government_ebox, get_government_tooltip(),
		       "");
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
  GtkWidget *label;

  label = gtk_frame_get_label_widget(GTK_FRAME(unit_info_frame));
  gtk_label_set_text(GTK_LABEL(label),
		     get_unit_info_label_text1(punit));

  gtk_label_set_text(GTK_LABEL(unit_info_label),
		     get_unit_info_label_text2(punit));

  if(punit) {
    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST);

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
    /* HACK: the UNHAPPY citizen is used for the government
     * when we don't know any better. */
    struct citizen_type c = {.type = CITIZEN_UNHAPPY};

    gov_sprite = get_citizen_sprite(c, 0, NULL);
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  gtk_image_set_from_pixmap(GTK_IMAGE(government_label),
			    gov_sprite->pixmap, NULL);
}

/**************************************************************************
...
**************************************************************************/
void map_size_changed(void)
{
  gtk_widget_set_size_request(overview_canvas,
			      overview.width, overview.height);
  update_map_canvas_scrollbars_size();
}

/**************************************************************************
...
**************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  struct canvas *result = fc_malloc(sizeof(*result));

  result->type = CANVAS_PIXMAP;
  result->v.pixmap = gdk_pixmap_new(root_window, width, height, -1);
  return result;
}

/**************************************************************************
...
**************************************************************************/
void canvas_free(struct canvas *store)
{
  if (store->type == CANVAS_PIXMAP) {
    g_object_unref(store->v.pixmap);
  }
  free(store);
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  static struct canvas store;

  store.type = CANVAS_PIXMAP;
  store.v.pixmap = overview_canvas->window;
  return &store;
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
  
  refresh_overview_canvas();
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool map_center = TRUE;
static bool map_configure = FALSE;

gboolean map_canvas_configure(GtkWidget * w, GdkEventConfigure * ev,
			      gpointer data)
{
  if (map_canvas_resized(ev->width, ev->height)) {
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

#define MAX_DIRTY_RECTS 20
static int num_dirty_rects = 0;
static GdkRectangle dirty_rects[MAX_DIRTY_RECTS];
static bool is_flush_queued = FALSE;

/**************************************************************************
  A callback invoked as a result of gtk_idle_add, this function simply
  flushes the mapview canvas.
**************************************************************************/
static gint unqueue_flush(gpointer data)
{
  flush_dirty();
  redraw_selection_rectangle();
  is_flush_queued = FALSE;
  return 0;
}

/**************************************************************************
  Called when a region is marked dirty, this function queues a flush event
  to be handled later by GTK.  The flush may end up being done
  by freeciv before then, in which case it will be a wasted call.
**************************************************************************/
static void queue_flush(void)
{
  if (!is_flush_queued) {
    gtk_idle_add(unqueue_flush, NULL);
    is_flush_queued = TRUE;
  }
}

/**************************************************************************
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
**************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height)
{
  if (num_dirty_rects < MAX_DIRTY_RECTS) {
    dirty_rects[num_dirty_rects].x = canvas_x;
    dirty_rects[num_dirty_rects].y = canvas_y;
    dirty_rects[num_dirty_rects].width = pixel_width;
    dirty_rects[num_dirty_rects].height = pixel_height;
    num_dirty_rects++;
    queue_flush();
  }
}

/**************************************************************************
  Mark the entire screen area as "dirty" so that we can flush it later.
**************************************************************************/
void dirty_all(void)
{
  num_dirty_rects = MAX_DIRTY_RECTS;
  queue_flush();
}

/**************************************************************************
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect and dirty_all.  This function is generally called after we've
  processed a batch of drawing operations.
**************************************************************************/
void flush_dirty(void)
{
  if (num_dirty_rects == MAX_DIRTY_RECTS) {
    flush_mapcanvas(0, 0, map_canvas->allocation.width,
		    map_canvas->allocation.height);
  } else {
    int i;

    for (i = 0; i < num_dirty_rects; i++) {
      flush_mapcanvas(dirty_rects[i].x, dirty_rects[i].y,
		      dirty_rects[i].width, dirty_rects[i].height);
    }
  }
  num_dirty_rects = 0;
}

/****************************************************************************
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
  gdk_flush();
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

/****************************************************************************
  Draw a description for the given city.  This description may include the
  name, turns-to-grow, production, and city turns-to-build (depending on
  client options).

  (canvas_x, canvas_y) gives the location on the given canvas at which to
  draw the description.  This is the location of the city itself so the
  text must be drawn underneath it.  pcity gives the city to be drawn,
  while (*width, *height) should be set by show_ctiy_desc to contain the
  width and height of the text block (centered directly underneath the
  city's tile).
****************************************************************************/
void show_city_desc(struct canvas *pcanvas, int canvas_x, int canvas_y,
		    struct city *pcity, int *width, int *height)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
  static char buffer[512], buffer2[32];
  PangoRectangle rect, rect2;
  enum color_std color;
  int extra_width = 0;
  static PangoLayout *layout;

  if (!layout) {
    layout = pango_layout_new(gdk_pango_context_get());
  }

  *width = *height = 0;

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

    gtk_draw_shadowed_string(pcanvas->v.pixmap,
			     toplevel->style->black_gc,
			     toplevel->style->white_gc,
			     canvas_x - (rect.width + rect2.width) / 2,
			     canvas_y + PANGO_ASCENT(rect), layout);

    if (draw_city_growth && pcity->owner == game.player_idx) {
      pango_layout_set_font_description(layout, city_productions_font);
      pango_layout_set_text(layout, buffer2, -1);
      gdk_gc_set_foreground(civ_gc, colors_standard[color]);
      gtk_draw_shadowed_string(pcanvas->v.pixmap,
			       toplevel->style->black_gc,
			       civ_gc,
			       canvas_x - (rect.width + rect2.width) / 2
			       + rect.width,
			       canvas_y + PANGO_ASCENT(rect)
			       + rect.height / 2 - rect2.height / 2,
			       layout);
    }

    canvas_y += rect.height + 3;

    *width = rect.width + rect2.width;
    *height += rect.height + 3;
  }

  if (draw_city_productions && (pcity->owner==game.player_idx)) {
    get_city_mapview_production(pcity, buffer, sizeof(buffer));

    pango_layout_set_font_description(layout, city_productions_font);
    pango_layout_set_text(layout, buffer, -1);

    pango_layout_get_pixel_extents(layout, &rect, NULL);
    gtk_draw_shadowed_string(pcanvas->v.pixmap,
			     toplevel->style->black_gc,
			     toplevel->style->white_gc,
			     canvas_x - rect.width / 2,
			     canvas_y + PANGO_ASCENT(rect), layout);

    *width = MAX(*width, rect.width);
    *height += rect.height;
  }
  }
}

/**************************************************************************
...
**************************************************************************/
void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p)
{
  struct canvas canvas_store;

  canvas_store.type = CANVAS_PIXCOMM;
  canvas_store.v.pixcomm = p;

  gtk_pixcomm_freeze(p);
  gtk_pixcomm_clear(p);

  put_unit(punit, &canvas_store, 0, 0);

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
  struct canvas store;
 
  store.type = CANVAS_PIXCOMM;
  store.v.pixcomm = p;

  gtk_pixcomm_freeze(p);

  put_unit_city_overlays(punit, &store, 0, NORMAL_TILE_HEIGHT);

  gtk_pixcomm_thaw(p);
}

/**************************************************************************
...
**************************************************************************/
static void pixmap_put_overlay_tile(GdkDrawable *pixmap,
				    int canvas_x, int canvas_y,
				    struct Sprite *ssprite)
{
  if (!ssprite) {
    return;
  }
      
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
void canvas_put_sprite(struct canvas *pcanvas,
		       int canvas_x, int canvas_y,
		       struct Sprite *sprite,
		       int offset_x, int offset_y, int width, int height)
{
  switch (pcanvas->type) {
    case CANVAS_PIXMAP:
      pixmap_put_sprite(pcanvas->v.pixmap, canvas_x, canvas_y,
	  sprite, offset_x, offset_y, width, height);
      break;
    case CANVAS_PIXCOMM:
      gtk_pixcomm_copyto(pcanvas->v.pixcomm, sprite, canvas_x, canvas_y);
      break;
    case CANVAS_PIXBUF:
      {
	GdkPixbuf *src, *dst;

	/* FIXME: is this right??? */
	if (canvas_x < 0) {
	  offset_x -= canvas_x;
	  canvas_x = 0;
	}
	if (canvas_y < 0) {
	  offset_y -= canvas_y;
	  canvas_y = 0;
	}


	src = sprite_get_pixbuf(sprite);
	dst = pcanvas->v.pixbuf;
	gdk_pixbuf_composite(src, dst, canvas_x, canvas_y,
	    MIN(width,
	      MIN(gdk_pixbuf_get_width(dst), gdk_pixbuf_get_width(src))),
	    MIN(height,
	      MIN(gdk_pixbuf_get_height(dst), gdk_pixbuf_get_height(src))),
	    canvas_x - offset_x, canvas_y - offset_y,
	    1.0, 1.0, GDK_INTERP_NEAREST, 255);
      }
      break;
    default:
      break;
  } 
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
			    int canvas_x, int canvas_y,
			    struct Sprite *sprite)
{
  canvas_put_sprite(pcanvas, canvas_x, canvas_y, sprite,
		    0, 0, sprite->width, sprite->height);
}

/****************************************************************************
  Draw a full sprite onto the canvas.  If "fog" is specified draw it with
  fog.
****************************************************************************/
void canvas_put_sprite_fogged(struct canvas *pcanvas,
			      int canvas_x, int canvas_y,
			      struct Sprite *psprite,
			      bool fog, int fog_x, int fog_y)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
    pixmap_put_overlay_tile_draw(pcanvas->v.pixmap, canvas_x, canvas_y,
				 psprite, fog);
  }
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height)
{
  GdkColor *col = colors_standard[color];

  switch (pcanvas->type) {
    case CANVAS_PIXMAP:
      gdk_gc_set_foreground(fill_bg_gc, col);
      gdk_draw_rectangle(pcanvas->v.pixmap, fill_bg_gc, TRUE,
	  canvas_x, canvas_y, width, height);
      break;
    case CANVAS_PIXCOMM:
      gtk_pixcomm_fill(pcanvas->v.pixcomm, col);
      break;
    case CANVAS_PIXBUF:
      gdk_pixbuf_fill(pcanvas->v.pixbuf,
	  ((guint32)(col->red & 0xff00) << 16)
	  | ((col->green & 0xff00) << 8) | (col->blue & 0xff00) | 0xff);
      break;
    default:
      break;
  }
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
    gdk_gc_set_clip_origin(fill_bg_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_bg_gc, psprite->mask);
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[color]);

    gdk_draw_rectangle(pcanvas->v.pixmap, fill_bg_gc, TRUE,
		       canvas_x, canvas_y, psprite->width, psprite->height);

    gdk_gc_set_clip_mask(fill_bg_gc, NULL);
  }
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, psprite->mask);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_gc_set_stipple(fill_tile_gc, black50);
    gdk_gc_set_ts_origin(fill_tile_gc, canvas_x, canvas_y);

    gdk_draw_rectangle(pcanvas->v.pixmap, fill_tile_gc, TRUE,
		       canvas_x, canvas_y, psprite->width, psprite->height);

    gdk_gc_set_clip_mask(fill_tile_gc, NULL); 
  }
}

/**************************************************************************
  Draw a colored line onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		     enum line_type ltype, int start_x, int start_y,
		     int dx, int dy)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
    GdkGC *gc = NULL;

    switch (ltype) {
    case LINE_NORMAL:
      gc = thin_line_gc;
      break;
    case LINE_BORDER:
      gc = border_line_gc;
      break;
    case LINE_TILE_FRAME:
      gc = thick_line_gc;
      break;
    case LINE_GOTO:
      gc = thick_line_gc;
      break;
    }

    gdk_gc_set_foreground(gc, colors_standard[color]);
    gdk_draw_line(pcanvas->v.pixmap, gc,
		  start_x, start_y, start_x + dx, start_y + dy);
  }
}

/**************************************************************************
...
**************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		 int src_x, int src_y, int dest_x, int dest_y,
		 int width, int height)
{
  if (dest->type == src->type) {
    if (src->type == CANVAS_PIXMAP) {
      gdk_draw_drawable(dest->v.pixmap, fill_bg_gc, src->v.pixmap,
			src_x, src_y, dest_x, dest_y, width, height);
    }
  }
}

/**************************************************************************
  Created a fogged version of the sprite.  This can fail on older systems
  in which case the callers needs a fallback.
**************************************************************************/
static void fog_sprite(struct Sprite *sprite)
{
  int x, y;
  GdkPixbuf *fogged;
  guchar *pixel;
  const int bright = 65; /* Brightness percentage */

  fogged = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
			  sprite->width, sprite->height);
  gdk_pixbuf_get_from_drawable(fogged, sprite->pixmap, NULL,
			       0, 0, 0, 0, sprite->width, sprite->height);

  /* Iterate over all pixels, reducing brightness by 50%. */
  for (x = 0; x < sprite->width; x++) {
    for (y = 0; y < sprite->height; y++) {
      pixel = gdk_pixbuf_get_pixels(fogged)
	+ y * gdk_pixbuf_get_rowstride(fogged)
	+ x * gdk_pixbuf_get_n_channels(fogged);

      pixel[0] = pixel[0] * bright / 100;
      pixel[1] = pixel[1] * bright / 100;
      pixel[2] = pixel[2] * bright / 100;
    }
  }

  gdk_pixbuf_render_pixmap_and_mask(fogged, &sprite->fogged,
				    NULL, 0);
  g_object_unref(fogged);
}

/**************************************************************************
Only used for isometric view.
**************************************************************************/
static void pixmap_put_overlay_tile_draw(GdkDrawable *pixmap,
					 int canvas_x, int canvas_y,
					 struct Sprite *ssprite,
					 bool fog)
{
  if (!ssprite) {
    return;
  }

  if (fog && better_fog && !ssprite->fogged) {
    fog_sprite(ssprite);
    if (!ssprite->fogged) {
      freelog(LOG_NORMAL,
	      _("Better fog will only work in truecolor.  Disabling it"));
      better_fog = FALSE;
    }
  }

  if (fog && better_fog) {
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, ssprite->mask);

    gdk_draw_drawable(pixmap, fill_tile_gc,
		      ssprite->fogged,
		      0, 0,
		      canvas_x, canvas_y,
		      ssprite->width, ssprite->height);
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);

    return;
  }

  pixmap_put_sprite(pixmap, canvas_x, canvas_y, ssprite,
		    0, 0, ssprite->width, ssprite->height);

  /* I imagine this could be done more efficiently. Some pixels We first
     draw from the sprite, and then draw black afterwards. It would be much
     faster to just draw every second pixel black in the first place. */
  if (fog) {
    gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_clip_mask(fill_tile_gc, ssprite->mask);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
    gdk_gc_set_ts_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_stipple(fill_tile_gc, black50);

    gdk_draw_rectangle(pixmap, fill_tile_gc, TRUE,
		       canvas_x, canvas_y, ssprite->width, ssprite->height);
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);
  }
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  int canvas_x, canvas_y;

  if (tile_to_canvas_pos(&canvas_x, &canvas_y, ptile)) {
    pixmap_put_overlay_tile(map_canvas->window,
			    canvas_x, canvas_y,
			    sprites.user.attention);
  }
}

/****************************************************************************
  Draw a single tile of the citymap onto the mapview.  The tile is drawn
  as the given color with the given worker on it.  The exact method of
  drawing is left up to the GUI.
****************************************************************************/
void put_city_worker(struct canvas *pcanvas,
		     enum color_std color, enum city_tile_type worker,
		     int canvas_x, int canvas_y)
{
  if (pcanvas->type == CANVAS_PIXMAP) {
    if (worker == C_TILE_EMPTY) {
      gdk_gc_set_stipple(fill_tile_gc, gray25);
    } else if (worker == C_TILE_WORKER) {
      gdk_gc_set_stipple(fill_tile_gc, gray50);
    } else {
      return;
    }

    gdk_gc_set_ts_origin(fill_tile_gc, canvas_x, canvas_y);
    gdk_gc_set_foreground(fill_tile_gc, colors_standard[color]);

    if (is_isometric) {
      gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
      gdk_gc_set_clip_mask(fill_tile_gc, sprites.black_tile->mask);
    }

    gdk_draw_rectangle(pcanvas->v.pixmap, fill_tile_gc, TRUE,
		       canvas_x, canvas_y,
		       NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

    if (is_isometric) {
      gdk_gc_set_clip_mask(fill_tile_gc, NULL);
    }
  }
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
  int xmin, ymin, xmax, ymax, xsize, ysize, xstep, ystep;

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  get_mapview_scroll_step(&xstep, &ystep);

  map_hadj = gtk_adjustment_new(-1, xmin, xmax, xstep, xsize, xsize);
  map_vadj = gtk_adjustment_new(-1, ymin, ymax, ystep, ysize, ysize);

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
 Area Selection
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  GdkPoint points[5];

  gdk_gc_set_foreground(civ_gc, colors_standard[COLOR_STD_YELLOW]);

  /* gdk_draw_rectangle() must start top-left.. */
  points[0].x = canvas_x;
  points[0].y = canvas_y;

  points[1].x = canvas_x + w;
  points[1].y = canvas_y;

  points[2].x = canvas_x + w;
  points[2].y = canvas_y + h;

  points[3].x = canvas_x;
  points[3].y = canvas_y + h;

  points[4].x = canvas_x;
  points[4].y = canvas_y;
  gdk_draw_lines(map_canvas->window, civ_gc, points, ARRAY_SIZE(points));

  rectangle_active = TRUE;
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  reset_city_dialogs();
  reset_unit_table();
  blank_max_unit_size();
}
