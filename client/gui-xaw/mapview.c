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
#include <stdlib.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Scrollbar.h>

#include "canvas.h"
#include "pixcomm.h"

#include "fcintl.h"
#include "game.h"
#include "government.h"		/* government_graphic() */
#include "map.h"
#include "mem.h"
#include "player.h"
#include "rand.h"
#include "support.h"
#include "timing.h"
#include "unit.h"

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
#include "text.h"
#include "tilespec.h"

#include "mapview.h"

#define map_canvas_store (mapview_canvas.store->pixmap)

static void pixmap_put_overlay_tile(Pixmap pixmap, int x, int y,
 				    struct Sprite *ssprite);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
Pixmap scaled_intro_pixmap;
int scaled_intro_pixmap_width, scaled_intro_pixmap_height;


/**************************************************************************
...
**************************************************************************/
void map_size_changed(void)
{
  Dimension h, w;

  XtVaSetValues(overview_canvas, XtNwidth, overview.width, XtNheight,
		overview.height, NULL);

  XtVaGetValues(left_column_form, XtNheight, &h, NULL);
  XtVaSetValues(map_form, XtNheight, h, NULL);

  XtVaGetValues(below_menu_form, XtNwidth, &w, NULL);
  XtVaSetValues(menu_form, XtNwidth, w, NULL);
  XtVaSetValues(bottom_form, XtNwidth, w, NULL);
}

/**************************************************************************
...
**************************************************************************/
struct canvas *canvas_create(int width, int height)
{
  struct canvas *result = fc_malloc(sizeof(*result));

  result->pixmap =
      XCreatePixmap(display, root_window, width, height, display_depth);
  return result;
}

/**************************************************************************
...
**************************************************************************/
void canvas_free(struct canvas *store)
{
  XFreePixmap(display, store->pixmap);
  free(store);
}

/****************************************************************************
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  static struct canvas store;

  store.pixmap = XtWindow(overview_canvas);

  return &store;
}

/**************************************************************************
...
**************************************************************************/
void update_turn_done_button(bool do_restore)
{
  static bool flip = FALSE;
 
  if (!get_turn_done_button_state()) {
    return;
  }

  if ((do_restore && flip) || !do_restore) {
    Pixel fore, back;

    XtVaGetValues(turn_done_button, XtNforeground, &fore,
		  XtNbackground, &back, NULL);

    XtVaSetValues(turn_done_button, XtNforeground, back,
		  XtNbackground, fore, NULL);

    flip = !flip;
  }
}


/**************************************************************************
...
**************************************************************************/
void update_timeout_label(void)
{
  xaw_set_label(timeout_label, get_timeout_label_text());
}


/**************************************************************************
...
**************************************************************************/
void update_info_label(void)
{
  char buffer[512]; int d;
  
  my_snprintf(buffer, sizeof(buffer),
	      _("%s People\n"
		"Year: %s\n"
		"Gold: %d\n"
		"Tax:%d Lux:%d Sci:%d"),
	  population_to_text(civ_population(game.player_ptr)),
	  textyear(game.year),
	  game.player_ptr->economic.gold,
	  game.player_ptr->economic.tax,
	  game.player_ptr->economic.luxury,
	  game.player_ptr->economic.science);
  xaw_set_label(info_command, buffer);

  set_indicator_icons(client_research_sprite(),
		      client_warming_sprite(),
		      client_cooling_sprite(),
		      game.player_ptr->government);

  d=0;
  for(;d<(game.player_ptr->economic.luxury)/10;d++)
    xaw_set_bitmap(econ_label[d], sprites.tax_luxury->pixmap);
 
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    xaw_set_bitmap(econ_label[d], sprites.tax_science->pixmap);
 
   for(;d<10;d++)
     xaw_set_bitmap(econ_label[d], sprites.tax_gold->pixmap);
 
  update_timeout_label();
}


/**************************************************************************
  Update the information label which gives info on the current unit and the
  square under the current unit, for specified unit.  Note that in practice
  punit is almost always (or maybe strictly always?) the focus unit.
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
    my_snprintf(buffer, sizeof(buffer), "%s\n%s",
		get_unit_info_label_text1(punit),
		get_unit_info_label_text2(punit));
    xaw_set_label(unit_info_label, buffer);

    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST);

    switch (hover_state) {
    case HOVER_NONE:
      XUndefineCursor(display, XtWindow(map_canvas));
      break;
    case HOVER_PATROL:
      XDefineCursor(display, XtWindow(map_canvas), patrol_cursor);
      break;
    case HOVER_GOTO:
    case HOVER_CONNECT:
      XDefineCursor(display, XtWindow(map_canvas), goto_cursor);
      break;
    case HOVER_NUKE:
      XDefineCursor(display, XtWindow(map_canvas), nuke_cursor);
      break;
    case HOVER_PARADROP:
      XDefineCursor(display, XtWindow(map_canvas), drop_cursor);
      break;
    }
  } else {
    xaw_set_label(unit_info_label, "");
    XUndefineCursor(display, XtWindow(map_canvas));
  }

  update_unit_pix_label(punit);
}

/**************************************************************************
...
**************************************************************************/
Pixmap get_thumb_pixmap(int onoff)
{
  return sprites.treaty_thumb[BOOL_VAL(onoff)]->pixmap;
}

/**************************************************************************
...
**************************************************************************/
Pixmap get_citizen_pixmap(struct citizen_type type, int cnum,
			  struct city *pcity)
{
  return get_citizen_sprite(type, cnum, pcity)->pixmap;
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

  xaw_set_bitmap(bulb_label, sprites.bulb[bulb]->pixmap);
  xaw_set_bitmap(sun_label, sprites.warming[sol]->pixmap);
  xaw_set_bitmap(flake_label, sprites.cooling[flake]->pixmap);

  if (game.government_count==0) {
    /* HACK: the UNHAPPY citizen is used for the government
     * when we don't know any better. */
    struct citizen_type c = {.type = CITIZEN_UNHAPPY};

    gov_sprite = get_citizen_sprite(c, 0, NULL);
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  xaw_set_bitmap(government_label, gov_sprite->pixmap);
}

/**************************************************************************
...
**************************************************************************/
void overview_canvas_expose(Widget w, XEvent *event, Region exposed, 
			    void *client_data)
{
  Dimension height, width;
  
  if (!can_client_change_view()) {
    if (radar_gfx_sprite) {
      XCopyArea(display, radar_gfx_sprite->pixmap, XtWindow(overview_canvas),
                 civ_gc,
                 event->xexpose.x, event->xexpose.y,
                 event->xexpose.width, event->xexpose.height,
                 event->xexpose.x, event->xexpose.y);
    }
    return;
  }

  XtVaGetValues(w, XtNheight, &height, XtNwidth, &width, NULL);
  
  refresh_overview_canvas();
}

/**************************************************************************
...
**************************************************************************/
void canvas_copy(struct canvas *dest, struct canvas *src,
		 int src_x, int src_y, int dest_x, int dest_y,
		 int width, int height)
{
  XCopyArea(display, src->pixmap, dest->pixmap, civ_gc, src_x, src_y, width,
	    height, dest_x, dest_y);
}

/**************************************************************************
...
**************************************************************************/
void map_canvas_expose(Widget w, XEvent *event, Region exposed, 
		       void *client_data)
{
  Dimension width, height;
  bool map_resized;

  XtVaGetValues(w, XtNwidth, &width, XtNheight, &height, NULL);

  map_resized = map_canvas_resized(width, height);

  if (!can_client_change_view()) {
    if (!intro_gfx_sprite) {
      load_intro_gfx();
    }
    if (height != scaled_intro_pixmap_height
        || width != scaled_intro_pixmap_width) {
      if (scaled_intro_pixmap) {
	XFreePixmap(display, scaled_intro_pixmap);
      }

      scaled_intro_pixmap=x_scale_pixmap(intro_gfx_sprite->pixmap,
					 intro_gfx_sprite->width,
					 intro_gfx_sprite->height, 
					 width, height, root_window);
      scaled_intro_pixmap_width=width;
      scaled_intro_pixmap_height=height;
    }

    if(scaled_intro_pixmap)
       XCopyArea(display, scaled_intro_pixmap, XtWindow(map_canvas),
		 civ_gc,
		 event->xexpose.x, event->xexpose.y,
		 event->xexpose.width, event->xexpose.height,
		 event->xexpose.x, event->xexpose.y);
    return;
  }
  if(scaled_intro_pixmap) {
    XFreePixmap(display, scaled_intro_pixmap);
    scaled_intro_pixmap=0; scaled_intro_pixmap_height=0;
  }

  if (map_exists() && !map_resized) {
    XCopyArea(display, map_canvas_store, XtWindow(map_canvas),
	      civ_gc,
	      event->xexpose.x, event->xexpose.y,
	      event->xexpose.width, event->xexpose.height,
	      event->xexpose.x, event->xexpose.y);
  }
  refresh_overview_canvas();
}

/**************************************************************************
  Draw a single masked sprite to the pixmap.
**************************************************************************/
static void pixmap_put_sprite(Pixmap pixmap,
			      int canvas_x, int canvas_y,
			      struct Sprite *sprite,
			      int offset_x, int offset_y,
			      int width, int height)
{
  if (sprite->has_mask) {
    XSetClipOrigin(display, civ_gc, canvas_x, canvas_y);
    XSetClipMask(display, civ_gc, sprite->mask);
  }

  XCopyArea(display, sprite->pixmap, pixmap, 
	    civ_gc,
	    offset_x, offset_y,
	    width, height, 
	    canvas_x, canvas_y);

  if (sprite->has_mask) {
    XSetClipMask(display, civ_gc, None);
  }
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite(struct canvas *pcanvas,
		       int canvas_x, int canvas_y,
		       struct Sprite *sprite,
		       int offset_x, int offset_y, int width, int height)
{
  pixmap_put_sprite(pcanvas->pixmap, canvas_x, canvas_y,
		    sprite, offset_x, offset_y, width, height);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_sprite_full(struct canvas *pcanvas,
			    int canvas_x, int canvas_y,
			    struct Sprite *sprite)
{
  canvas_put_sprite(pcanvas, canvas_x, canvas_y,
		    sprite, 0, 0, sprite->width, sprite->height);
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
  canvas_put_sprite_full(pcanvas, canvas_x, canvas_y, psprite);

  if (fog) {
    canvas_fog_sprite_area(pcanvas, psprite, canvas_x, canvas_y);
  }
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height)
{
  XSetForeground(display, fill_bg_gc, colors_standard[color]);
  XFillRectangle(display, pcanvas->pixmap, fill_bg_gc,
		 canvas_x, canvas_y, width, height);
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y)
{
  if (psprite->has_mask) {
    XSetClipOrigin(display, fill_tile_gc, canvas_x, canvas_y);
    XSetClipMask(display, fill_tile_gc, psprite->mask);
  }
  XSetForeground(display, fill_tile_gc, colors_standard[color]);

  XFillRectangle(display, pcanvas->pixmap, fill_tile_gc,
		 canvas_x, canvas_y, psprite->width, psprite->height);

  if (psprite->has_mask) {
    XSetClipMask(display, fill_tile_gc, None);
  }
}

/****************************************************************************
  Fill the area covered by the sprite with the given color.
****************************************************************************/
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y)
{
  if (psprite->has_mask) {
    XSetClipOrigin(display, fill_tile_gc, canvas_x, canvas_y);
    XSetClipMask(display, fill_tile_gc, psprite->mask);
  }
  XSetStipple(display, fill_tile_gc, gray50);
  XSetTSOrigin(display, fill_tile_gc, canvas_x, canvas_y);
  XSetForeground(display, fill_tile_gc, colors_standard[COLOR_STD_BLACK]);

  XFillRectangle(display, pcanvas->pixmap, fill_tile_gc,
		 canvas_x, canvas_y, psprite->width, psprite->height);

  if (psprite->has_mask) {
    XSetClipMask(display, fill_tile_gc, None);
  }
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		     enum line_type ltype, int start_x, int start_y,
		     int dx, int dy)
{
  GC gc = NULL;

  switch (ltype) {
  case LINE_NORMAL:
    gc = civ_gc;
    break;
  case LINE_BORDER:
    gc = border_line_gc;
    break;
  case LINE_TILE_FRAME:
  case LINE_GOTO:
    /* TODO: differentiate these. */
    gc = civ_gc;
    break;
  }

  XSetForeground(display, gc, colors_standard[color]);
  XDrawLine(display, pcanvas->pixmap, gc,
	    start_x, start_y, start_x + dx, start_y + dy);
}

/**************************************************************************
  Flush the given part of the canvas buffer (if there is one) to the
  screen.
**************************************************************************/
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height)
{
  XCopyArea(display, map_canvas_store, XtWindow(map_canvas), 
	    civ_gc, 
	    canvas_x, canvas_y, pixel_width, pixel_height,
	    canvas_x, canvas_y);
}

#define MAX_DIRTY_RECTS 20
static int num_dirty_rects = 0;
static XRectangle dirty_rects[MAX_DIRTY_RECTS];
bool is_flush_queued = FALSE;

/**************************************************************************
  A callback invoked as a result of a 0-length timer, this function simply
  flushes the mapview canvas.
**************************************************************************/
static void unqueue_flush(XtPointer client_data, XtIntervalId * id)
{
  flush_dirty();
  redraw_selection_rectangle();
  is_flush_queued = FALSE;
}

/**************************************************************************
  Called when a region is marked dirty, this function queues a flush event
  to be handled later by Xaw.  The flush may end up being done
  by freeciv before then, in which case it will be a wasted call.
**************************************************************************/
static void queue_flush(void)
{
  if (!is_flush_queued) {
    (void) XtAppAddTimeOut(app_context, 0, unqueue_flush, NULL);
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
    Dimension width, height;

    XtVaGetValues(map_canvas, XtNwidth, &width, XtNheight, &height, NULL);
    flush_mapcanvas(0, 0, width, height);
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
  XSync(display, 0);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars(void)
{
  float shown_h, top_h, shown_v, top_v;
  int xmin, ymin, xmax, ymax, xsize, ysize;
  int scroll_x, scroll_y;

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  get_mapview_scroll_pos(&scroll_x, &scroll_y);

  top_h = (float)(scroll_x - xmin) / (float)(xmax - xmin);
  top_v = (float)(scroll_y - ymin) / (float)(ymax - ymin);

  shown_h = (float)xsize / (float)(xmax - xmin);
  shown_v = (float)ysize / (float)(ymax - ymin);

  XawScrollbarSetThumb(map_horizontal_scrollbar, top_h, shown_h);
  XawScrollbarSetThumb(map_vertical_scrollbar, top_v, shown_v);
}

/**************************************************************************
...
**************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  /* Nothing */
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
Draw at x = left of string, y = top of string.
**************************************************************************/
static void draw_shadowed_string(struct canvas *pcanvas,
				 XFontSet fontset, GC font_gc,
				 enum color_std foreground,
				 enum color_std shadow,
				 int x, int y, const char *string)
{
  size_t len = strlen(string);

  y -= XExtentsOfFontSet(fontset)->max_logical_extent.y;

  XSetForeground(display, font_gc, colors_standard[shadow]);
  XmbDrawString(display, pcanvas->pixmap, fontset, font_gc,
      x + 1, y + 1, string, len);

  XSetForeground(display, font_gc, colors_standard[foreground]);
  XmbDrawString(display, pcanvas->pixmap, fontset, font_gc,
      x, y, string, len);
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
  char buffer[512], buffer2[512];
  enum color_std color;
  int w, w2;
  XFontSetExtents *main_exts = XExtentsOfFontSet(main_font_set);
  XFontSetExtents *prod_exts = XExtentsOfFontSet(prod_font_set);

  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT;

  get_city_mapview_name_and_growth(pcity, buffer, sizeof(buffer),
				   buffer2, sizeof(buffer2), &color);

  w = XmbTextEscapement(main_font_set, buffer, strlen(buffer));
  if (buffer2[0] != '\0') {
    /* HACK: put a character's worth of space between the two strings. */
    w += XmbTextEscapement(main_font_set, "M", 1);
  }
  w2 = XmbTextEscapement(main_font_set, buffer2, strlen(buffer2));

  draw_shadowed_string(pcanvas, main_font_set, font_gc,
		       COLOR_STD_WHITE, COLOR_STD_BLACK,
		       canvas_x - (w + w2) / 2,
		       canvas_y, buffer);

  draw_shadowed_string(pcanvas, prod_font_set, prod_font_gc, color,
		       COLOR_STD_BLACK,
		       canvas_x - (w + w2) / 2 + w,
		       canvas_y, buffer2);

  *width = w + w2;
  *height = main_exts->max_logical_extent.height;

  if (draw_city_productions && (pcity->owner == game.player_idx)) {
    if (draw_city_names) {
      canvas_y += main_exts->max_logical_extent.height;
    }

    get_city_mapview_production(pcity, buffer, sizeof(buffer));
    w = XmbTextEscapement(prod_font_set, buffer, strlen(buffer));

    draw_shadowed_string(pcanvas, prod_font_set, prod_font_gc,
			 COLOR_STD_WHITE, COLOR_STD_BLACK,
			 canvas_x - w / 2,
			 canvas_y, buffer);

    *width = MAX(*width, w);
    *height += prod_exts->max_logical_extent.height;
  }
}

/**************************************************************************
  FIXME: 
  For now only two food, one shield and two masks can be drawn per unit,
  the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm)
{
  struct canvas store = {pm};
 
  /* wipe the slate clean */
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  XFillRectangle(display, pm, fill_bg_gc, 0, NORMAL_TILE_WIDTH, 
		 NORMAL_TILE_HEIGHT, NORMAL_TILE_HEIGHT+SMALL_TILE_HEIGHT);

  put_unit_city_overlays(punit, &store, 0, NORMAL_TILE_HEIGHT);
}

/**************************************************************************
...
**************************************************************************/
static void pixmap_put_overlay_tile(Pixmap pixmap, int canvas_x, int canvas_y,
 				    struct Sprite *ssprite)
{
  if (!ssprite) return;

  pixmap_put_sprite(pixmap, canvas_x, canvas_y,
		    ssprite, 0, 0, ssprite->width, ssprite->height);
}

/**************************************************************************
 Draws a cross-hair overlay on a tile
**************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  int canvas_x, canvas_y;

  if (tile_to_canvas_pos(&canvas_x, &canvas_y, ptile)) {
    pixmap_put_overlay_tile(XtWindow(map_canvas), canvas_x, canvas_y,
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
    XSetStipple(display, fill_tile_gc, gray25);
  } else if (worker == C_TILE_WORKER) {
    XSetStipple(display, fill_tile_gc, gray50);
  } else {
    return;
  }

  XSetTSOrigin(display, fill_tile_gc, canvas_x, canvas_y);
  XSetForeground(display, fill_tile_gc, colors_standard[color]);
  XFillRectangle(display, pcanvas->pixmap, fill_tile_gc,
		 canvas_x, canvas_y,
		 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
}

/**************************************************************************
...
**************************************************************************/
void scrollbar_jump_callback(Widget w, XtPointer client_data,
			     XtPointer percent_ptr)
{
  float percent=*(float*)percent_ptr;
  int xmin, ymin, xmax, ymax, xsize, ysize;
  int scroll_x, scroll_y;

  if (!can_client_change_view()) {
    return;
  }

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  get_mapview_scroll_pos(&scroll_x, &scroll_y);

  if(w==map_horizontal_scrollbar) {
    scroll_x = xmin + (percent * (xmax - xmin));
  } else {
    scroll_y = ymin + (percent * (ymax - ymin));
  }

  set_mapview_scroll_pos(scroll_x, scroll_y);
}


/**************************************************************************
...
**************************************************************************/
void scrollbar_scroll_callback(Widget w, XtPointer client_data,
			     XtPointer position_val)
{
  int position = XTPOINTER_TO_INT(position_val);
  int scroll_x, scroll_y, xstep, ystep;

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  get_mapview_scroll_step(&xstep, &ystep);

  if (!can_client_change_view()) {
    return;
  }

  if(w==map_horizontal_scrollbar) {
    if (position > 0) {
      scroll_x += xstep;
    } else {
      scroll_x -= xstep;
    }
  }
  else {
    if (position > 0) {
      scroll_y += ystep;
    } else {
      scroll_y -= ystep;
    }
  }

  set_mapview_scroll_pos(scroll_x, scroll_y);
  update_map_canvas_scrollbars();
}

/**************************************************************************
 Area Selection
**************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  /* PORTME */
}

/**************************************************************************
  This function is called when the tileset is changed.
**************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized).
   */
   reset_econ_label_pixmaps();
   update_info_label();
   reset_unit_below_pixmaps();
}
