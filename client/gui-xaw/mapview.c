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
#include "tilespec.h"

#include "mapview.h"

#define map_canvas_store (mapview_canvas.store->pixmap)

static void pixmap_put_overlay_tile(Pixmap pixmap, int x, int y,
 				    struct Sprite *ssprite);
static void put_line(Pixmap pm, int x, int y, int dir);

/* the intro picture is held in this pixmap, which is scaled to
   the screen size */
Pixmap scaled_intro_pixmap;
int scaled_intro_pixmap_width, scaled_intro_pixmap_height;


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
  int canvas_x, canvas_y;

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

    XSync(display, 0);
    usleep_since_timer_start(anim_timer, 10000);

  } while (punit0->hp > hp0 || punit1->hp > hp1);

  if (map_to_canvas_pos(&canvas_x, &canvas_y,
			losing_unit->x, losing_unit->y)) {
    for (i = 0; i < num_tiles_explode_unit; i++) {
      struct canvas_store store = {single_tile_pixmap};

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      put_one_tile(&store, 0, 0, losing_unit->x, losing_unit->y, FALSE);
      put_unit_full(losing_unit, &store, 0, 0);
      pixmap_put_overlay_tile(single_tile_pixmap, 0, 0,
			      sprites.explode.unit[i]);

      XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc,
		0, 0,
		UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT,
		canvas_x, canvas_y);

      XSync(display, 0);
      usleep_since_timer_start(anim_timer, 20000);
    }
  }

  set_units_in_combat(NULL, NULL);
  refresh_tile_mapcanvas(punit0->x, punit0->y, TRUE);
  refresh_tile_mapcanvas(punit1->x, punit1->y, TRUE);
}

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
struct canvas_store *canvas_store_create(int width, int height)
{
  struct canvas_store *result = fc_malloc(sizeof(*result));

  result->pixmap =
      XCreatePixmap(display, root_window, width, height, display_depth);
  return result;
}

/**************************************************************************
...
**************************************************************************/
void canvas_store_free(struct canvas_store *store)
{
  XFreePixmap(display, store->pixmap);
  free(store);
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
  char buffer[512];

  if (game.timeout <= 0)
    sz_strlcpy(buffer, Q_("?timeout:off"));
  else
    format_duration(buffer, sizeof(buffer), seconds_to_turndone);
  xaw_set_label(timeout_label, buffer);
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
    xaw_set_bitmap(econ_label[d],
		   get_citizen_pixmap(CITIZEN_ELVIS, d, NULL));
 
  for(;d<(game.player_ptr->economic.science+game.player_ptr->economic.luxury)/10;d++)
    xaw_set_bitmap(econ_label[d],
		   get_citizen_pixmap(CITIZEN_SCIENTIST, d, NULL));
 
   for(;d<10;d++)
    xaw_set_bitmap(econ_label[d],
		   get_citizen_pixmap(CITIZEN_TAXMAN, d, NULL));
 
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
    struct city *pcity;
    pcity=player_find_city_by_id(game.player_ptr, punit->homecity);
    my_snprintf(buffer, sizeof(buffer), "%s %s\n%s\n%s\n%s", 
		unit_type(punit)->name,
		(punit->veteran) ? _("(veteran)") : "",
		(hover_unit==punit->id) ? 
		_("Select destination") : unit_activity_text(punit), 
		map_get_tile_info_text(punit->x, punit->y),
		pcity ? pcity->name : "");
    xaw_set_label(unit_info_label, buffer);

    if (hover_unit != punit->id)
      set_hover_state(NULL, HOVER_NONE);

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
Pixmap get_citizen_pixmap(enum citizen_type type, int cnum,
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
    /* not sure what to do here */
    gov_sprite = get_citizen_sprite(CITIZEN_UNHAPPY, 0, NULL);
  } else {
    gov_sprite = get_government(gov)->sprite;
  }
  xaw_set_bitmap(government_label, gov_sprite->pixmap);
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
  XCopyArea(display, map_canvas_store, XtWindow(map_canvas), civ_gc,
	    old_canvas_x, old_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT,
	    old_canvas_x, old_canvas_y);

  /* Draw the new sprite. */
  XCopyArea(display, map_canvas_store, single_tile_pixmap, civ_gc,
	    new_canvas_x, new_canvas_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, 0,
	    0);
  put_unit_full(punit, &store, 0, 0);

  /* Write to screen. */
  XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc, 0, 0,
	    UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT, new_canvas_x, new_canvas_y);

  /* Flush. */
  XSync(display, 0);
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
void gui_copy_canvas(struct canvas_store *dest, struct canvas_store *src,
		     int src_x, int src_y, int dest_x, int dest_y, int width,
		     int height)
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
  Draw some or all of a tile onto the mapview canvas.
**************************************************************************/
void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw)
{
  /* PORTME */
  assert(0);
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
  if (sprite->mask) {
    XSetClipOrigin(display, civ_gc, canvas_x, canvas_y);
    XSetClipMask(display, civ_gc, sprite->mask);
  }

  XCopyArea(display, sprite->pixmap, pixmap, 
	    civ_gc,
	    offset_x, offset_y,
	    width, height, 
	    canvas_x, canvas_y);

  if (sprite->mask) {
    XSetClipMask(display, civ_gc, None);
  }
}

/**************************************************************************
  Draw some or all of a sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite(struct canvas_store *pcanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y, int width, int height)
{
  pixmap_put_sprite(pcanvas_store->pixmap, canvas_x, canvas_y,
		    sprite, offset_x, offset_y, width, height);
}

/**************************************************************************
  Draw a full sprite onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_sprite_full(struct canvas_store *pcanvas_store,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite)
{
  gui_put_sprite(pcanvas_store, canvas_x, canvas_y,
		 sprite, 0, 0, sprite->width, sprite->height);
}

/**************************************************************************
  Draw a filled-in colored rectangle onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_rectangle(struct canvas_store *pcanvas_store,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height)
{
  XSetForeground(display, fill_bg_gc, colors_standard[color]);
  XFillRectangle(display, pcanvas_store->pixmap, fill_bg_gc,
		 canvas_x, canvas_y, width, height);
}

/**************************************************************************
  Draw a 1-pixel-width colored line onto the mapview or citydialog canvas.
**************************************************************************/
void gui_put_line(struct canvas_store *pcanvas_store, enum color_std color,
		  enum line_type ltype, int start_x, int start_y,
		  int dx, int dy)
{
  GC gc;

  gc = (ltype == LINE_BORDER ? border_line_gc : civ_gc);
  XSetForeground(display, gc, colors_standard[color]);
  XDrawLine(display, pcanvas_store->pixmap, gc,
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
static void draw_shadowed_string(XFontStruct * font, GC font_gc,
				 enum color_std foreground,
				 enum color_std shadow,
				 int x, int y, const char *string)
{
  size_t len = strlen(string);

  y += font->ascent;

  XSetForeground(display, font_gc, colors_standard[shadow]);
  XDrawString(display, map_canvas_store, font_gc, x + 1, y + 1, string, len);

  XSetForeground(display, font_gc, colors_standard[foreground]);
  XDrawString(display, map_canvas_store, font_gc, x, y, string, len);
}

/**************************************************************************
...
**************************************************************************/
void show_city_desc(struct city *pcity, int canvas_x, int canvas_y)
{
  char buffer[512], buffer2[512];
  enum color_std color;
  int w, w2;

  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT;

  get_city_mapview_name_and_growth(pcity, buffer, sizeof(buffer),
				   buffer2, sizeof(buffer2), &color);

  w = XTextWidth(main_font_struct, buffer, strlen(buffer));
  if (buffer2[0] != '\0') {
    /* HACK: put a character's worth of space between the two strings. */
    w += XTextWidth(main_font_struct, "M", 1);
  }
  w2 = XTextWidth(main_font_struct, buffer2, strlen(buffer2));

  draw_shadowed_string(main_font_struct, font_gc,
		       COLOR_STD_WHITE, COLOR_STD_BLACK,
		       canvas_x - (w + w2) / 2,
		       canvas_y, buffer);

  draw_shadowed_string(prod_font_struct, prod_font_gc, color,
		       COLOR_STD_BLACK,
		       canvas_x - (w + w2) / 2 + w,
		       canvas_y, buffer2);

  if (draw_city_productions && (pcity->owner == game.player_idx)) {
    if (draw_city_names) {
      canvas_y += main_font_struct->ascent + main_font_struct->descent;
    }

    get_city_mapview_production(pcity, buffer, sizeof(buffer));
    w = XTextWidth(prod_font_struct, buffer, strlen(buffer));

    draw_shadowed_string(prod_font_struct, prod_font_gc,
			 COLOR_STD_WHITE, COLOR_STD_BLACK,
			 canvas_x - w / 2,
			 canvas_y, buffer);
  }
}

/**************************************************************************
...
**************************************************************************/
void put_city_tile_output(Pixmap pm, int canvas_x, int canvas_y, 
			  int food, int shield, int trade)
{
  food = CLIP(0, food, NUM_TILES_DIGITS-1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS-1);
  shield = CLIP(0, shield, NUM_TILES_DIGITS-1);
  
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_foodnum[food]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_shieldnum[shield]);
  pixmap_put_overlay_tile(pm, canvas_x, canvas_y, sprites.city.tile_tradenum[trade]);
}

/**************************************************************************
  FIXME: 
  For now only two food, one shield and two masks can be drawn per unit,
  the proper way to do this is probably something like what Civ II does.
  (One food/shield/mask drawn N times, possibly one top of itself. -- SKi 
**************************************************************************/
void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm)
{
  int upkeep_food = CLIP(0, punit->upkeep_food, 2);
  int unhappy = CLIP(0, punit->unhappiness, 2);
 
  /* wipe the slate clean */
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  XFillRectangle(display, pm, fill_bg_gc, 0, NORMAL_TILE_WIDTH, 
		 NORMAL_TILE_HEIGHT, NORMAL_TILE_HEIGHT+SMALL_TILE_HEIGHT);

  /* draw overlay pixmaps */
  if (punit->upkeep > 0)
    pixmap_put_overlay_tile(pm, 0, NORMAL_TILE_HEIGHT,
			    sprites.upkeep.shield);
  if (upkeep_food > 0)
    pixmap_put_overlay_tile(pm, 0, NORMAL_TILE_HEIGHT,
			    sprites.upkeep.food[upkeep_food-1]);
  if (unhappy > 0)
    pixmap_put_overlay_tile(pm, 0, NORMAL_TILE_HEIGHT,
			    sprites.upkeep.unhappy[unhappy-1]);
}

/**************************************************************************
...
**************************************************************************/
void put_nuke_mushroom_pixmaps(int x, int y)
{
  int x_itr, y_itr;

  for (x_itr = 0; x_itr<3; x_itr++) {
    for (y_itr = 0; y_itr<3; y_itr++) {
      int x1 = x + x_itr -1;
      int y1 = y + y_itr -1;
      if (normalize_map_pos(&x1, &y1)) {
	int canvas_x, canvas_y;
	struct Sprite *mysprite = sprites.explode.nuke[y_itr][x_itr];

	if (!map_to_canvas_pos(&canvas_x, &canvas_y, x1, y1)) {
	  continue;
	}
	XCopyArea(display, map_canvas_store, single_tile_pixmap, civ_gc,
		  canvas_x, canvas_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		  0, 0);
	pixmap_put_overlay_tile(single_tile_pixmap, 0, 0, mysprite);
	XCopyArea(display, single_tile_pixmap, XtWindow(map_canvas), civ_gc,
		  0, 0, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT,
		  canvas_x, canvas_y);
      }
    }
  }
  XSync(display, 0);
  myusleep(1000000);

  update_map_canvas(x-1, y-1, 3, 3, TRUE);
}

/**************************************************************************
...
**************************************************************************/
void pixmap_put_black_tile(Pixmap pm, int canvas_x, int canvas_y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_BLACK]);

  XFillRectangle(display, pm, fill_bg_gc,  
		 canvas_x, canvas_y,
		 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
}
		     

/**************************************************************************
...
**************************************************************************/
void pixmap_frame_tile_red(Pixmap pm, int canvas_x, int canvas_y)
{
  XSetForeground(display, fill_bg_gc, colors_standard[COLOR_STD_RED]);

  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x, canvas_y,
		 NORMAL_TILE_WIDTH-1, NORMAL_TILE_HEIGHT-1);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x+1, canvas_y+1,
		 NORMAL_TILE_WIDTH-3, NORMAL_TILE_HEIGHT-3);
  XDrawRectangle(display, pm, fill_bg_gc,  
		 canvas_x+2, canvas_y+2,
		 NORMAL_TILE_WIDTH-5, NORMAL_TILE_HEIGHT-5);
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
void put_cross_overlay_tile(int x,int y)
{
  int canvas_x, canvas_y;

  if (map_to_canvas_pos(&canvas_x, &canvas_y, x, y)) {
    pixmap_put_overlay_tile(XtWindow(map_canvas), canvas_x, canvas_y,
			    sprites.user.attention);
  }
}


/**************************************************************************
 Shade the tiles around a city to indicate the location of workers
**************************************************************************/
void put_city_workers(struct city *pcity, int color)
{
  int canvas_x, canvas_y;
  static struct city *last_pcity = NULL;

  if (color == -1) {
    if (pcity != last_pcity)
      city_workers_color = (city_workers_color%3)+1;
    color = city_workers_color;
  }

  XSetForeground(display, fill_tile_gc, colors_standard[color]);
  city_map_checked_iterate(pcity->x, pcity->y, i, j, x, y) {
    enum city_tile_type worked = get_worker_city(pcity, i, j);

    if (!map_to_canvas_pos(&canvas_x, &canvas_y, x, y)) {
      continue;
    }
    if (!is_city_center(i, j)) {
      if (worked == C_TILE_EMPTY) {
	XSetStipple(display, fill_tile_gc, gray25);
      } else if (worked == C_TILE_WORKER) {
	XSetStipple(display, fill_tile_gc, gray50);
      } else
	continue;
      XCopyArea(display, map_canvas_store, XtWindow(map_canvas), civ_gc,
		canvas_x, canvas_y,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, canvas_x, canvas_y);
      XFillRectangle(display, XtWindow(map_canvas), fill_tile_gc,
		     canvas_x, canvas_y,
		     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }
    if (worked == C_TILE_WORKER) {
      put_city_tile_output(XtWindow(map_canvas),
			   canvas_x, canvas_y,
			   city_get_food_tile(i, j, pcity),
			   city_get_shields_tile(i, j, pcity),
			   city_get_trade_tile(i, j, pcity));
    }
  } city_map_checked_iterate_end;

  last_pcity = pcity;
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
...
**************************************************************************/
static void put_line(Pixmap pm, int x, int y, int dir)
{
  int canvas_src_x, canvas_src_y, canvas_dest_x, canvas_dest_y;
  (void) map_to_canvas_pos(&canvas_src_x, &canvas_src_y, x, y);
  canvas_src_x += NORMAL_TILE_WIDTH/2;
  canvas_src_y += NORMAL_TILE_HEIGHT/2;
  canvas_dest_x = canvas_src_x + (NORMAL_TILE_WIDTH * DIR_DX[dir])/2;
  canvas_dest_y = canvas_src_y + (NORMAL_TILE_WIDTH * DIR_DY[dir])/2;

  XSetForeground(display, civ_gc, colors_standard[COLOR_STD_CYAN]);

  XDrawLine(display, pm, civ_gc, canvas_src_x, canvas_src_y,
	    canvas_dest_x, canvas_dest_y);
}

/**************************************************************************
...
**************************************************************************/
void draw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y, is_real;

  is_real = MAPSTEP(dest_x, dest_y, src_x, src_y, dir);
  assert(is_real);

  if (tile_visible_mapcanvas(src_x, src_y)) {
    put_line(map_canvas_store, src_x, src_y, dir);
    put_line(XtWindow(map_canvas), src_x, src_y, dir);
  }

  if (tile_visible_mapcanvas(dest_x, dest_y)) {
    put_line(map_canvas_store, dest_x, dest_y, DIR_REVERSE(dir));
    put_line(XtWindow(map_canvas), dest_x, dest_y, DIR_REVERSE(dir));
  }
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
}
