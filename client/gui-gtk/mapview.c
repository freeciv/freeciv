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

#include <gtk/gtk.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "log.h"
#include "map.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

#include "civclient.h"
#include "climap.h"
#include "climisc.h"
#include "control.h"		/* get_unit_in_focus() */
#include "goto.h"
#include "options.h"
#include "text.h"
#include "tilespec.h"

#include "citydlg.h"		/* For reset_city_dialogs() */
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapctrl.h"

#include "mapview.h"

#define map_canvas_store (mapview_canvas.store->pixmap)

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
  gtk_set_label(timeout_label, get_timeout_label_text());
}

/**************************************************************************
...
**************************************************************************/
void update_info_label( void )
{
  char buffer	[512];
  int  d;

  gtk_frame_set_label( GTK_FRAME( main_frame_civ_name ), get_nation_name(game.player_ptr->nation) );

  my_snprintf(buffer, sizeof(buffer),
	      _("Population: %s\nYear: %s\n"
		"Gold: %d\nTax: %d Lux: %d Sci: %d"),
	      population_to_text(civ_population(game.player_ptr)),
	      textyear(game.year), game.player_ptr->economic.gold,
	      game.player_ptr->economic.tax,
	      game.player_ptr->economic.luxury,
	      game.player_ptr->economic.science);

  gtk_set_label(main_label_info, buffer);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  d=0;
  for (; d < game.player_ptr->economic.luxury /10; d++) {
    struct Sprite *sprite = sprite = sprites.tax_luxury;
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]), sprite->pixmap, sprite->mask);
  }
 
  for (; d < (game.player_ptr->economic.science
	     + game.player_ptr->economic.luxury) / 10; d++) {
    struct Sprite *sprite = sprites.tax_science;
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]), sprite->pixmap, sprite->mask);
  }
 
  for (; d < 10; d++) {
    struct Sprite *sprite = sprites.tax_gold;
    gtk_pixmap_set(GTK_PIXMAP(econ_label[d]), sprite->pixmap, sprite->mask);
  }
 
  update_timeout_label();
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
  if (punit && get_client_state() != CLIENT_GAME_OVER_STATE) {
    char buffer[512];
    struct city *pcity =
	player_find_city_by_id(game.player_ptr, punit->homecity);
    int infrastructure =
	get_tile_infrastructure_set(punit->tile);
    struct unit_type *ptype = unit_type(punit);

    my_snprintf(buffer, sizeof(buffer), "%s", ptype->name);

    if (ptype->veteran[punit->veteran].name[0] != '\0') {
      sz_strlcat(buffer, " (");
      sz_strlcat(buffer, ptype->veteran[punit->veteran].name);
      sz_strlcat(buffer, ")");
    }

    gtk_frame_set_label(GTK_FRAME(unit_info_frame), buffer);


    my_snprintf(buffer, sizeof(buffer), "%s\n%s\n%s%s%s",
		(hover_unit == punit->id) ?
		_("Select destination") : unit_activity_text(punit),
		map_get_tile_info_text(punit->tile),
		infrastructure ?
		map_get_infrastructure_text(infrastructure) : "",
		infrastructure ? "\n" : "", pcity ? pcity->name : "");
    gtk_set_label(unit_info_label, buffer);

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
    gtk_frame_set_label( GTK_FRAME(unit_info_frame),"");
    gtk_set_label(unit_info_label,"\n\n");
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

  gtk_pixmap_set(GTK_PIXMAP(bulb_label), sprites.bulb[bulb]->pixmap, NULL);
  gtk_pixmap_set(GTK_PIXMAP(sun_label), sprites.warming[sol]->pixmap, NULL);
  gtk_pixmap_set(GTK_PIXMAP(flake_label), sprites.cooling[flake]->pixmap, NULL);

  if (game.government_count==0) {
    /* HACK: the UNHAPPY citizen is used for the government
     * when we don't know any better. */
    struct citizen_type c = {.type = CITIZEN_UNHAPPY};

    gov_sprite = get_citizen_sprite(c, 0, NULL);
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  gtk_pixmap_set(GTK_PIXMAP(government_label), gov_sprite->pixmap, NULL);
}

/**************************************************************************
...
**************************************************************************/
void map_size_changed(void)
{
  gtk_widget_set_usize(overview_canvas, overview.width, overview.height);
}

/**************************************************************************
...
**************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  struct canvas *result = fc_malloc(sizeof(*result));

  result->pixmap = gdk_pixmap_new(root_window, width, height, -1);
  result->pixcomm = NULL;
  return result;
}

/**************************************************************************
...
**************************************************************************/
void canvas_free(struct canvas *store)
{
  gdk_pixmap_unref(store->pixmap);
  assert(store->pixcomm == NULL);
  free(store);
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  static struct canvas store;

  store.pixmap = overview_canvas->window;

  return &store;
}

/**************************************************************************
...
**************************************************************************/
gint overview_canvas_expose(GtkWidget *w, GdkEventExpose *ev)
{
  if (!can_client_change_view()) {
    if(radar_gfx_sprite)
      gdk_draw_pixmap(overview_canvas->window, civ_gc,
		      radar_gfx_sprite->pixmap, ev->area.x, ev->area.y,
		      ev->area.x, ev->area.y, ev->area.width, ev->area.height);
    return TRUE;
  }
  
  refresh_overview_canvas();
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		 int src_x, int src_y, int dest_x, int dest_y,
		 int width, int height)
{
  gdk_draw_pixmap(dest->pixmap, fill_bg_gc, src->pixmap,
		  src_x, src_y, dest_x, dest_y, width, height);
}

/**************************************************************************
...
**************************************************************************/
gint map_canvas_expose(GtkWidget *w, GdkEventExpose *ev)
{
  gint height, width;
  gboolean map_resized;
  static int exposed_once = 0;

  gdk_window_get_size(w->window, &width, &height);

  map_resized = map_canvas_resized(width, height);

  if (!can_client_change_view()) {
    if (!intro_gfx_sprite) {
      load_intro_gfx();
    }
    if (!scaled_intro_sprite || height != scaled_intro_sprite->height
	|| width != scaled_intro_sprite->width) {
      if (scaled_intro_sprite) {
	free_sprite(scaled_intro_sprite);
      }
	
      scaled_intro_sprite = sprite_scale(intro_gfx_sprite, width, height);
    }
    
    if (scaled_intro_sprite) {
      gdk_draw_pixmap(map_canvas->window, civ_gc,
		      scaled_intro_sprite->pixmap, ev->area.x,
		      ev->area.y, ev->area.x, ev->area.y,
		      ev->area.width, ev->area.height);
    }
  }
  else
  {
    if (scaled_intro_sprite) {
      free_sprite(scaled_intro_sprite);
      scaled_intro_sprite = NULL;
    }

    if (map_exists() && !map_resized) {
      gdk_draw_pixmap(map_canvas->window, civ_gc, map_canvas_store,
		      ev->area.x, ev->area.y, ev->area.x, ev->area.y,
		      ev->area.width, ev->area.height);
    }
    refresh_overview_canvas();
  }

  if (!exposed_once) {
    center_on_something();
    exposed_once = 1;
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
  gdk_draw_pixmap(map_canvas->window, civ_gc, map_canvas_store,
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
  Mark the rectangular region as 'dirty' so that we know to flush it
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
  static char buffer[512], buffer2[32];
  int w, w2, ascent;
  enum color_std color;

  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT;

  *width = *height = 0;

  get_city_mapview_name_and_growth(pcity, buffer, sizeof(buffer),
				   buffer2, sizeof(buffer2), &color);

  gdk_string_extents(main_fontset, buffer, NULL, NULL, &w, &ascent, NULL);
  if (buffer2[0] != '\0') {
    /* HACK: put a character's worth of space between the two strings. */
    w += gdk_string_width(main_fontset, "M");
  }
  w2 = gdk_string_width(prod_fontset, buffer2);

  gtk_draw_shadowed_string(pcanvas->pixmap, main_fontset,
			   toplevel->style->black_gc,
			   toplevel->style->white_gc,
			   canvas_x - (w + w2) / 2,
			   canvas_y + ascent,
			   buffer);
  gdk_gc_set_foreground(civ_gc, colors_standard[color]);
  gtk_draw_shadowed_string(pcanvas->pixmap, prod_fontset,
			   toplevel->style->black_gc,
			   civ_gc,
			   canvas_x - (w + w2) / 2 + w,
			   canvas_y + ascent,
			   buffer2);

  *width = w + w2;
  *height = gdk_string_height(main_fontset, buffer) + 3;

  if (draw_city_productions && (pcity->owner==game.player_idx)) {
    if (draw_city_names) {
      canvas_y += *height;
    }

    get_city_mapview_production(pcity, buffer, sizeof(buffer));

    gdk_string_extents(prod_fontset, buffer, NULL, NULL, &w, &ascent, NULL);
    gtk_draw_shadowed_string(pcanvas->pixmap, prod_fontset,
			     toplevel->style->black_gc,
			     toplevel->style->white_gc, canvas_x - w / 2,
			     canvas_y + ascent, buffer);

    *height += gdk_string_height(prod_fontset, buffer);
  }
}

/**************************************************************************
...
**************************************************************************/
void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p)
{
  struct canvas canvas_store = {NULL, p};

  put_unit(punit, &canvas_store, 0, 0);

  gtk_pixcomm_changed(GTK_PIXCOMM(p));
}

/**************************************************************************
  FIXME: For now only two food, two golds, one shield and two masks can be 
  drawn per unit, the proper way to do this is probably something like 
  what Civ II does. (One food/shield/mask drawn N times, possibly one top 
  of itself.)
**************************************************************************/
void put_unit_gpixmap_city_overlays(struct unit *punit, GtkPixcomm *p)
{
  struct canvas store = {NULL, p};

  put_unit_city_overlays(punit, &store, 0, NORMAL_TILE_HEIGHT);
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

  gdk_draw_pixmap(pixmap, civ_gc, ssprite->pixmap,
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

  gdk_draw_pixmap(pixmap, civ_gc, ssprite->pixmap,
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
  if (pcanvas->pixmap) {
    pixmap_put_sprite(pcanvas->pixmap, canvas_x, canvas_y,
		      sprite, offset_x, offset_y, width, height);
  } else {
    gtk_pixcomm_copyto(pcanvas->pixcomm, sprite,
		       canvas_x, canvas_y, FALSE);
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
  pixmap_put_overlay_tile_draw(pcanvas->pixmap, canvas_x, canvas_y,
			       psprite, fog);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height)
{
  if (pcanvas->pixmap) {
    gdk_gc_set_foreground(fill_bg_gc, colors_standard[color]);
    gdk_draw_rectangle(pcanvas->pixmap, fill_bg_gc, TRUE,
		       canvas_x, canvas_y, width, height);
  } else {
    gtk_pixcomm_fill(pcanvas->pixcomm, colors_standard[color], FALSE);
  }
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  gdk_gc_set_clip_origin(fill_bg_gc, canvas_x, canvas_y);
  gdk_gc_set_clip_mask(fill_bg_gc, psprite->mask);
  gdk_gc_set_foreground(fill_bg_gc, colors_standard[color]);

  gdk_draw_rectangle(pcanvas->pixmap, fill_bg_gc, TRUE,
		     canvas_x, canvas_y, psprite->width, psprite->height);

  gdk_gc_set_clip_mask(fill_bg_gc, NULL);
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y)
{
  gdk_gc_set_clip_origin(fill_tile_gc, canvas_x, canvas_y);
  gdk_gc_set_clip_mask(fill_tile_gc, psprite->mask);
  gdk_gc_set_foreground(fill_tile_gc, colors_standard[COLOR_STD_BLACK]);
  gdk_gc_set_stipple(fill_tile_gc, black50);
  gdk_gc_set_ts_origin(fill_tile_gc, canvas_x, canvas_y);

  gdk_draw_rectangle(pcanvas->pixmap, fill_tile_gc, TRUE,
		     canvas_x, canvas_y, psprite->width, psprite->height);

  gdk_gc_set_clip_mask(fill_tile_gc, NULL); 
}

/**************************************************************************
  Draw a colored line onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		     enum line_type ltype, int start_x, int start_y,
		     int dx, int dy)
{
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
  gdk_draw_line(pcanvas->pixmap, gc,
		start_x, start_y, start_x + dx, start_y + dy);
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

  gdk_draw_rectangle(pcanvas->pixmap, fill_tile_gc, TRUE,
		     canvas_x, canvas_y,
		     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

  if (is_isometric) {
    gdk_gc_set_clip_mask(fill_tile_gc, NULL);
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

  gtk_signal_connect(GTK_OBJECT(map_hadj), "value_changed",
		     GTK_SIGNAL_FUNC(scrollbar_jump_callback),
		     GINT_TO_POINTER(TRUE));
  gtk_signal_connect(GTK_OBJECT(map_vadj), "value_changed",
		     GTK_SIGNAL_FUNC(scrollbar_jump_callback),
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
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  reset_city_dialogs();
  reset_unit_table();
}
