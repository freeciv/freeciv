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

#include "fcintl.h"
#include "log.h"
#include "map.h"
#include "rand.h"
#include "support.h"
#include "timing.h"

#include "graphics_g.h"
#include "gui_main_g.h"
#include "mapctrl_g.h"
#include "mapview_g.h"

#include "civclient.h"
#include "climap.h"
#include "control.h"
#include "goto.h"
#include "mapview_common.h"
#include "tilespec.h"

struct mapview_decoration *map_deco;
struct view mapview;
struct overview overview;
bool can_slide = TRUE;

/* Arbitrary estimated maximums for width and height of a city description
 * text.  Eventually this may be determined dynamically. */
#define MAX_CITY_DESC_WIDTH 128
#define MAX_CITY_DESC_HEIGHT 32

/*
 * Set to TRUE if the backing store is more recent than the version
 * drawn into overview.window.
 */
static bool overview_dirty = FALSE;

static void base_canvas_to_map_pos(int *map_x, int *map_y,
				   int canvas_x, int canvas_y);
static void center_tile_overviewcanvas(struct tile *ptile);
static void get_mapview_corners(int x[4], int y[4]);
static void redraw_overview(void);
static void dirty_overview(void);
static void flush_dirty_overview(void);

enum update_type {
  /* Masks */
  UPDATE_NONE = 0,
  UPDATE_CITY_DESCRIPTIONS = 1,
  UPDATE_MAP_CANVAS_VISIBLE = 2
};

/* A tile update has a tile associated with it as well as an area type.
 * See unqueue_mapview_updates for a thorough explanation. */
enum tile_update_type {
  TILE_UPDATE_TILE_SINGLE,
  TILE_UPDATE_TILE_FULL,
  TILE_UPDATE_UNIT,
  TILE_UPDATE_CITY_DESC,
  TILE_UPDATE_CITYMAP,
  TILE_UPDATE_COUNT
};
static void queue_mapview_update(enum update_type update);
static void queue_mapview_tile_update(struct tile *ptile,
				      enum tile_update_type type);
static void unqueue_mapview_updates(bool write_to_screen);

/**************************************************************************
 Refreshes a single tile on the map canvas.
**************************************************************************/
void refresh_tile_mapcanvas(struct tile *ptile,
			    bool full_refresh, bool write_to_screen)
{
  if (full_refresh) {
    queue_mapview_tile_update(ptile, TILE_UPDATE_TILE_FULL);
  } else {
    queue_mapview_tile_update(ptile, TILE_UPDATE_TILE_SINGLE);
  }
  if (write_to_screen) {
    unqueue_mapview_updates(TRUE);
  }
}

/**************************************************************************
 Refreshes a single unit on the map canvas.
**************************************************************************/
void refresh_unit_mapcanvas(struct unit *punit, struct tile *ptile,
			    bool full_refresh, bool write_to_screen)
{
  if (full_refresh && unit_type_flag(punit->type, F_CITIES)) {
    queue_mapview_tile_update(ptile, TILE_UPDATE_CITYMAP);
  } else {
    queue_mapview_tile_update(ptile, TILE_UPDATE_UNIT);
  }
  if (write_to_screen) {
    unqueue_mapview_updates(TRUE);
  }
}

/**************************************************************************
  Refreshes a single city on the map canvas.

  If full_refresh is given then the citymap area and the city text will
  also be refreshed.  Otherwise only the base city sprite is refreshed.
**************************************************************************/
void refresh_city_mapcanvas(struct city *pcity, struct tile *ptile,
			    bool full_refresh, bool write_to_screen)
{
  if (full_refresh && (draw_map_grid || draw_borders)) {
    queue_mapview_tile_update(ptile, TILE_UPDATE_CITYMAP);
  } else {
    queue_mapview_tile_update(ptile, TILE_UPDATE_UNIT);
  }
  if (write_to_screen) {
    unqueue_mapview_updates(TRUE);
  }
}

/**************************************************************************
Returns the color the grid should have between tile (x1,y1) and
(x2,y2).
**************************************************************************/
enum color_std get_grid_color(const struct tile *tile1, enum direction8 dir)
{
  enum city_tile_type city_tile_type1, city_tile_type2;
  struct city *dummy_pcity;
  bool pos1_is_in_city_radius;
  bool pos2_is_in_city_radius = FALSE;
  struct tile *tile2;

  if (!(tile2 = mapstep(tile1, dir))) {
    return COLOR_STD_BLACK;
  }

  pos1_is_in_city_radius = player_in_city_radius(game.player_ptr, tile1);
  pos2_is_in_city_radius = player_in_city_radius(game.player_ptr, tile2);

  if (!pos1_is_in_city_radius && !pos2_is_in_city_radius) {
    return COLOR_STD_BLACK;
  }

  get_worker_on_map_position(tile1, &city_tile_type1, &dummy_pcity);
  get_worker_on_map_position(tile2, &city_tile_type2, &dummy_pcity);

  if (city_tile_type1 == C_TILE_WORKER || city_tile_type2 == C_TILE_WORKER) {
    return COLOR_STD_RED;
  } else {
    return COLOR_STD_WHITE;
  }
}

/****************************************************************************
  Translate from a cartesian system to the GUI system.  This function works
  on vectors, meaning it can be passed a (dx,dy) pair and will return the
  change in GUI coordinates corresponding to this vector.  It is thus more
  general than map_to_gui_pos.

  Note that a gui_to_map_vector function is not possible, since the
  resulting map vector may differ based on the origin of the gui vector.
****************************************************************************/
void map_to_gui_vector(int *gui_dx, int *gui_dy, int map_dx, int map_dy)
{
  if (tileset_is_isometric()) {
    /*
     * Convert the map coordinates to isometric GUI
     * coordinates.  We'll make tile map(0,0) be the origin, and
     * transform like this:
     * 
     *                     3
     * 123                2 6
     * 456 -> becomes -> 1 5 9
     * 789                4 8
     *                     7
     */
    *gui_dx = (map_dx - map_dy) * NORMAL_TILE_WIDTH / 2;
    *gui_dy = (map_dx + map_dy) * NORMAL_TILE_HEIGHT / 2;
  } else {
    *gui_dx = map_dx * NORMAL_TILE_HEIGHT;
    *gui_dy = map_dy * NORMAL_TILE_WIDTH;
  }
}

/****************************************************************************
  Translate from map to gui coordinate systems.

  GUI coordinates are comparable to canvas coordinates but extend in all
  directions.  gui(0,0) == map(0,0).
****************************************************************************/
static void map_to_gui_pos(int *gui_x, int *gui_y, int map_x, int map_y)
{
  /* Since the GUI origin is the same as the map origin we can just do a
   * vector conversion. */
  map_to_gui_vector(gui_x, gui_y, map_x, map_y);
}

/****************************************************************************
  Translate from gui to map coordinate systems.  See map_to_gui_pos().

  Note that you lose some information in this conversion.  If you convert
  from a gui position to a map position and back, you will probably not get
  the same value you started with.
****************************************************************************/
static void gui_to_map_pos(int *map_x, int *map_y, int gui_x, int gui_y)
{
  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
  const int HH = tileset_hex_height(), HW = tileset_hex_width();

  if (HH > 0 || HW > 0) {
    /* To handle hexagonal cases we have to revert to a less elegant method
     * of calculation. */
    int x, y, dx, dy;
    int xmult, ymult, mod, compar;

    assert(tileset_is_isometric());

    x = DIVIDE(gui_x, W);
    y = DIVIDE(gui_y, H);
    dx = gui_x - x * W;
    dy = gui_y - y * H;
    assert(dx >= 0 && dx < W);
    assert(dy >= 0 && dy < H);

    /* Now fold so we consider only one-quarter tile. */
    xmult = (dx >= W / 2) ? -1 : 1;
    ymult = (dy >= H / 2) ? -1 : 1;
    dx = (dx >= W / 2) ? (W - 1 - dx) : dx;
    dy = (dy >= H / 2) ? (H - 1 - dy) : dy;

    /* Next compare to see if we're across onto the next tile. */
    if (HW > 0) {
      compar = (dx - HW / 2) * (H / 2) - (H / 2 - 1 - dy) * (W / 2 - HW);
    } else {
      compar = (dy - HH / 2) * (W / 2) - (W / 2 - 1 - dx) * (H / 2 - HH);
    }
    mod = (compar < 0) ? -1 : 0;

    *map_x = (x + y) + mod * (xmult + ymult) / 2;
    *map_y = (y - x) + mod * (ymult - xmult) / 2;
  } else if (tileset_is_isometric()) {
    /* The basic operation here is a simple pi/4 rotation; however, we
     * have to first scale because the tiles have different width and
     * height.  Mathematically, this looks like
     *   | 1/W  1/H | |x|    |x`|
     *   |          | | | -> |  |
     *   |-1/W  1/H | |y|    |y`|
     *
     * Where W is the tile width and H the height.
     *
     * In simple terms, this is
     *   map_x = [   x / W + y / H ]
     *   map_y = [ - x / W + y / H ]
     * where [q] stands for integer part of q.
     *
     * Here the division is proper mathematical floating point division.
     *
     * A picture demonstrating this can be seen at
     * http://bugs.freeciv.org/Ticket/Attachment/16782/9982/grid1.png.
     *
     * We have to subtract off a half-tile in the X direction before doing
     * the transformation.  This is because, although the origin of the tile
     * is the top-left corner of the bounding box, after the transformation
     * the top corner of the diamond-shaped tile moves into this position.
     *
     * The calculation is complicated somewhat because of two things: we
     * only use integer math, and C integer division rounds toward zero
     * instead of rounding down.
     *
     * For another example of this math, see canvas_to_city_pos().
     */
    gui_x -= W / 2;
    *map_x = DIVIDE(gui_x * H + gui_y * W, W * H);
    *map_y = DIVIDE(gui_y * W - gui_x * H, W * H);
  } else {			/* tileset_is_isometric() */
    /* We use DIVIDE so that we will get the correct result even
     * for negative coordinates. */
    *map_x = DIVIDE(gui_x, W);
    *map_y = DIVIDE(gui_y, H);
  }
}

/**************************************************************************
  Finds the canvas coordinates for a map position. Beside setting the results
  in canvas_x, canvas_y it returns whether the tile is inside the
  visible mapview canvas.

  The result represents the upper left pixel (origin) of the bounding box of
  the tile.  Note that in iso-view this origin is not a part of the tile
  itself - so to make the operation reversible you would have to call
  canvas_to_map_pos on the center of the tile, not the origin.

  The center of a tile is defined as:
  {
    tile_to_canvas_pos(&canvas_x, &canvas_y, ptile);
    canvas_x += NORMAL_TILE_WIDTH / 2;
    canvas_y += NORMAL_TILE_HEIGHT / 2;
  }

  This pixel is one position closer to the lower right, which may be
  important to remember when doing some round-off operations. Other
  parts of the code assume NORMAL_TILE_WIDTH and NORMAL_TILE_HEIGHT
  to be even numbers.
**************************************************************************/
bool tile_to_canvas_pos(int *canvas_x, int *canvas_y, struct tile *ptile)
{
  int center_map_x, center_map_y, dx, dy;

  /*
   * First we wrap the coordinates to hopefully be within the the mapview
   * window.  We do this by finding the position closest to the center
   * of the window.
   */
  /* TODO: Cache the value of this position */
  base_canvas_to_map_pos(&center_map_x, &center_map_y,
			 mapview.width / 2,
			 mapview.height / 2);
  base_map_distance_vector(&dx, &dy,
			   center_map_x, center_map_y, ptile->x, ptile->y);

  map_to_gui_pos(canvas_x, canvas_y, center_map_x + dx, center_map_y + dy);
  *canvas_x -= mapview.gui_x0;
  *canvas_y -= mapview.gui_y0;

  /*
   * Finally we clip.
   *
   * This check is tailored to work for both iso-view and classic view.  Note
   * that (canvas_x, canvas_y) need not be aligned to a tile boundary, and
   * that the position is at the top-left of the NORMAL (not UNIT) tile.
   * This checks to see if _any part_ of the tile is present on the backing
   * store.  Even if it's not visible on the canvas, if it's present on the
   * backing store we need to draw it in case the canvas is resized.
   */
  return (*canvas_x > -NORMAL_TILE_WIDTH
	  && *canvas_x < mapview.store_width
	  && *canvas_y > -NORMAL_TILE_HEIGHT
	  && *canvas_y < (mapview.store_height
			  + UNIT_TILE_HEIGHT - NORMAL_TILE_HEIGHT));
}

/****************************************************************************
  Finds the map coordinates corresponding to pixel coordinates.  The
  resulting position is unwrapped and may be unreal.
****************************************************************************/
static void base_canvas_to_map_pos(int *map_x, int *map_y,
				   int canvas_x, int canvas_y)
{
  gui_to_map_pos(map_x, map_y,
		 canvas_x + mapview.gui_x0,
		 canvas_y + mapview.gui_y0);
}

/**************************************************************************
  Finds the tile corresponding to pixel coordinates.  Returns that tile,
  or NULL if the position is off the map.
**************************************************************************/
struct tile *canvas_pos_to_tile(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  base_canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y);
  if (normalize_map_pos(&map_x, &map_y)) {
    return map_pos_to_tile(map_x, map_y);
  } else {
    return NULL;
  }
}

/**************************************************************************
  Finds the tile corresponding to pixel coordinates.  Returns that tile,
  or the one nearest is the position is off the map.  Will never return NULL.
**************************************************************************/
struct tile *canvas_pos_to_nearest_tile(int canvas_x, int canvas_y)
{
  int map_x, map_y;

  base_canvas_to_map_pos(&map_x, &map_y, canvas_x, canvas_y);
  return nearest_real_tile(map_x, map_y);
}

/****************************************************************************
  Normalize (wrap) the GUI position.  This is equivalent to a map wrapping,
  but in GUI coordinates so that pixel accuracy is preserved.
****************************************************************************/
static void normalize_gui_pos(int *gui_x, int *gui_y)
{
  int map_x, map_y, nat_x, nat_y, gui_x0, gui_y0, diff_x, diff_y;

  /* Convert the (gui_x, gui_y) into a (map_x, map_y) plus a GUI offset
   * from this tile. */
  gui_to_map_pos(&map_x, &map_y, *gui_x, *gui_y);
  map_to_gui_pos(&gui_x0, &gui_y0, map_x, map_y);
  diff_x = *gui_x - gui_x0;
  diff_y = *gui_y - gui_y0;

  /* Perform wrapping without any realness check.  It's important that
   * we wrap even if the map position is unreal, which normalize_map_pos
   * doesn't necessarily do. */
  MAP_TO_NATIVE_POS(&nat_x, &nat_y, map_x, map_y);
  if (topo_has_flag(TF_WRAPX)) {
    nat_x = FC_WRAP(nat_x, map.xsize);
  }
  if (topo_has_flag(TF_WRAPY)) {
    nat_y = FC_WRAP(nat_y, map.ysize);
  }
  NATIVE_TO_MAP_POS(&map_x, &map_y, nat_x, nat_y);

  /* Now convert the wrapped map position back to a GUI position and add the
   * offset back on. */
  map_to_gui_pos(gui_x, gui_y, map_x, map_y);
  *gui_x += diff_x;
  *gui_y += diff_y;
}

/****************************************************************************
  Find the vector with minimum "real" distance between two GUI positions.
  This corresponds to map_to_distance_vector but works for GUI coordinates.
****************************************************************************/
static void gui_distance_vector(int *gui_dx, int *gui_dy,
				int gui_x0, int gui_y0,
				int gui_x1, int gui_y1)
{
  int map_x0, map_y0, map_x1, map_y1;
  int gui_x0_base, gui_y0_base, gui_x1_base, gui_y1_base;
  int gui_x0_diff, gui_y0_diff, gui_x1_diff, gui_y1_diff;
  int map_dx, map_dy;

  /* Make sure positions are canonical.  Yes, this is the only way. */
  normalize_gui_pos(&gui_x0, &gui_y0);
  normalize_gui_pos(&gui_x1, &gui_y1);

  /* Now we have to find the offset of each GUI position from its tile
   * origin.  This is complicated: it means converting to a map position and
   * then back to the GUI position to find the tile origin, then subtracting
   * to get the offset. */
  gui_to_map_pos(&map_x0, &map_y0, gui_x0, gui_y0);
  gui_to_map_pos(&map_x1, &map_y1, gui_x1, gui_y1);

  map_to_gui_pos(&gui_x0_base, &gui_y0_base, map_x0, map_y0);
  map_to_gui_pos(&gui_x1_base, &gui_y1_base, map_x1, map_y1);

  gui_x0_diff = gui_x0 - gui_x0_base;
  gui_y0_diff = gui_y0 - gui_y0_base;
  gui_x1_diff = gui_x1 - gui_x1_base;
  gui_y1_diff = gui_y1 - gui_y1_base;

  /* Next we find the map distance vector and convert this into a GUI
   * vector. */
  base_map_distance_vector(&map_dx, &map_dy, map_x0, map_y0, map_x1, map_y1);
  map_to_gui_pos(gui_dx, gui_dy, map_dx, map_dy);

  /* Finally we add on the difference in offsets to retain pixel
   * resolution. */
  *gui_dx += gui_x1_diff - gui_x0_diff;
  *gui_dy += gui_y1_diff - gui_y0_diff;
}

/****************************************************************************
  Move the GUI origin to the given normalized, clipped origin.  This may
  be called many times when sliding the mapview.
****************************************************************************/
static void base_set_mapview_origin(int gui_x0, int gui_y0)
{
  int old_gui_x0, old_gui_y0, dx, dy;
  const int width = mapview.width, height = mapview.height;
  int common_x0, common_x1, common_y0, common_y1;
  int update_x0, update_x1, update_y0, update_y1;
  struct tile *map_center;

  /* Then update everything.  This does some tricky math to avoid having
   * to do unnecessary redraws in update_map_canvas.  This makes for ugly
   * code but speeds up the mapview by a large factor. */

  /* We need to calculate the vector of movement of the mapview.  So
   * we find the GUI distance vector and then use this to calculate
   * the original mapview origin relative to the current position.  Thus
   * if we move one tile to the left, even if this causes GUI positions
   * to wrap the distance vector is only one tile. */
  normalize_gui_pos(&gui_x0, &gui_y0);
  gui_distance_vector(&dx, &dy,
		      mapview.gui_x0, mapview.gui_y0,
		      gui_x0, gui_y0);
  old_gui_x0 = gui_x0 - dx;
  old_gui_y0 = gui_y0 - dy;

  mapview.gui_x0 = gui_x0;
  mapview.gui_y0 = gui_y0;

  /* Find the overlapping area of the new and old mapview.  This is
   * done in GUI coordinates.  Note that if the GUI coordinates wrap
   * no overlap will be found. */
  common_x0 = MAX(old_gui_x0, gui_x0);
  common_x1 = MIN(old_gui_x0, gui_x0) + width;
  common_y0 = MAX(old_gui_y0, gui_y0);
  common_y1 = MIN(old_gui_y0, gui_y0) + height;

  if (mapview.can_do_cached_drawing
      && common_x1 > common_x0 && common_y1 > common_y0) {
    /* Do a partial redraw only.  This means the area of overlap (a
     * rectangle) is copied.  Then the remaining areas (two rectangles)
     * are updated through update_map_canvas. */
    struct canvas *target = mapview.tmp_store;

    if (old_gui_x0 < gui_x0) {
      update_x0 = MAX(old_gui_x0 + width, gui_x0);
      update_x1 = gui_x0 + width;
    } else {
      update_x0 = gui_x0;
      update_x1 = MIN(old_gui_x0, gui_x0 + width);
    }
    if (old_gui_y0 < gui_y0) {
      update_y0 = MAX(old_gui_y0 + height, gui_y0);
      update_y1 = gui_y0 + height;
    } else {
      update_y0 = gui_y0;
      update_y1 = MIN(old_gui_y0, gui_y0 + height);
    }

    dirty_all();
    canvas_copy(target, mapview.store,
		common_x0 - old_gui_x0,
		common_y0 - old_gui_y0,
		common_x0 - gui_x0, common_y0 - gui_y0,
		common_x1 - common_x0, common_y1 - common_y0);
    mapview.tmp_store = mapview.store;
    mapview.store = target;

    if (update_y1 > update_y0) {
      update_map_canvas(0, update_y0 - gui_y0,
			width, update_y1 - update_y0);
    }
    if (update_x1 > update_x0) {
      update_map_canvas(update_x0 - gui_x0, common_y0 - gui_y0,
			update_x1 - update_x0, common_y1 - common_y0);
    }
  } else {
    dirty_all();
    update_map_canvas(0, 0, mapview.store_width, mapview.store_height);
  }

  map_center = get_center_tile_mapcanvas();
  center_tile_overviewcanvas(map_center);
  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
  if (rectangle_active) {
    update_rect_at_mouse_pos();
  }
}

/****************************************************************************
  Change the mapview origin, clip it, and update everything.
****************************************************************************/
void set_mapview_origin(int gui_x0, int gui_y0)
{
  int xmin, xmax, ymin, ymax, xsize, ysize;

  /* Normalize (wrap) the mapview origin. */
  normalize_gui_pos(&gui_x0, &gui_y0);

  /* First wrap/clip the position.  Wrapping is done in native positions
   * while clipping is done in scroll (native) positions. */
  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);

  if (!topo_has_flag(TF_WRAPX)) {
    gui_x0 = CLIP(xmin, gui_x0, xmax - xsize);
  }

  if (!topo_has_flag(TF_WRAPY)) {
    gui_y0 = CLIP(ymin, gui_y0, ymax - ysize);
  }

  if (mapview.gui_x0 == gui_x0 && mapview.gui_y0 == gui_y0) {
    return;
  }

  if (can_slide && smooth_center_slide_msec > 0) {
    int start_x = mapview.gui_x0, start_y = mapview.gui_y0;
    int diff_x, diff_y;
    double timing_sec = (double)smooth_center_slide_msec / 1000.0, mytime;
    static struct timer *anim_timer;
    int frames = 0;

    gui_distance_vector(&diff_x, &diff_y, start_x, start_y, gui_x0, gui_y0);
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    unqueue_mapview_updates(TRUE);

    do {
      mytime = MIN(read_timer_seconds(anim_timer), timing_sec);

      base_set_mapview_origin(start_x + diff_x * (mytime / timing_sec),
			      start_y + diff_y * (mytime / timing_sec));
      flush_dirty();
      gui_flush();
      frames++;
    } while (mytime < timing_sec);

    mytime = read_timer_seconds(anim_timer);
    freelog(LOG_DEBUG, "Got %d frames in %f seconds: %f FPS.",
	    frames, mytime, (double)frames / mytime);
  } else {
    base_set_mapview_origin(gui_x0, gui_y0);
  }

  update_map_canvas_scrollbars();
}

/****************************************************************************
  Return the scroll dimensions of the clipping window for the mapview window..

  Imagine the entire map in scroll coordinates.  It is a rectangle.  Now
  imagine the mapview "window" sliding around through this rectangle.  How
  far can it slide?  In most cases it has to be able to slide past the
  ends of the map rectangle so that it's capable of reaching the whole
  area.

  This function gives constraints on how far the window is allowed to
  slide.  xmin and ymin are the minimum values for the window origin.
  xsize and ysize give the scroll dimensions of the mapview window.
  xmax and ymax give the maximum values that the bottom/left ends of the
  window may reach.  The constraints, therefore, are that:

    get_mapview_scroll_pos(&scroll_x, &scroll_y);
    xmin <= scroll_x < xmax - xsize
    ymin <= scroll_y < ymax - ysize

  This function should be used anywhere and everywhere that scrolling is
  constrained.

  Note that scroll coordinates, not map coordinates, are used.  Currently
  these correspond to native coordinates.
****************************************************************************/
void get_mapview_scroll_window(int *xmin, int *ymin, int *xmax, int *ymax,
			       int *xsize, int *ysize)
{
  int diff;

  *xsize = mapview.width;
  *ysize = mapview.height;

  if (MAP_IS_ISOMETRIC == tileset_is_isometric()) {
    /* If the map and view line up, it's easy. */
    NATIVE_TO_MAP_POS(xmin, ymin, 0, 0);
    map_to_gui_pos(xmin, ymin, *xmin, *ymin);

    NATIVE_TO_MAP_POS(xmax, ymax, map.xsize - 1, map.ysize - 1);
    map_to_gui_pos(xmax, ymax, *xmax, *ymax);
    *xmax += NORMAL_TILE_WIDTH;
    *ymax += NORMAL_TILE_HEIGHT;

    /* To be able to center on positions near the edges, we have to be
     * allowed to scroll all the way to those edges.  To allow wrapping the
     * clipping boundary needs to extend past the edge - a half-tile in
     * iso-view or a full tile in non-iso view.  The above math already has
     * taken care of some of this so all that's left is to fix the corner
     * cases. */
    if (topo_has_flag(TF_WRAPX)) {
      *xmax += *xsize;

      /* We need to be able to scroll a little further to the left. */
      *xmin -= NORMAL_TILE_WIDTH;
    }
    if (topo_has_flag(TF_WRAPY)) {
      *ymax += *ysize;

      /* We need to be able to scroll a little further up. */
      *ymin -= NORMAL_TILE_HEIGHT;
    }
  } else {
    /* Otherwise it's hard.  Very hard.  Impossible, in fact.  This is just
     * an approximation - a huge bounding box. */
    int gui_x1, gui_y1, gui_x2, gui_y2, gui_x3, gui_y3, gui_x4, gui_y4;
    int map_x, map_y;

    NATIVE_TO_MAP_POS(&map_x, &map_y, 0, 0);
    map_to_gui_pos(&gui_x1, &gui_y1, map_x, map_y);

    NATIVE_TO_MAP_POS(&map_x, &map_y, map.xsize - 1, 0);
    map_to_gui_pos(&gui_x2, &gui_y2, map_x, map_y);

    NATIVE_TO_MAP_POS(&map_x, &map_y, 0, map.ysize - 1);
    map_to_gui_pos(&gui_x3, &gui_y3, map_x, map_y);

    NATIVE_TO_MAP_POS(&map_x, &map_y, map.xsize - 1, map.ysize - 1);
    map_to_gui_pos(&gui_x4, &gui_y4, map_x, map_y);

    *xmin = MIN(gui_x1, MIN(gui_x2, gui_x3)) - mapview.width / 2;
    *ymin = MIN(gui_y1, MIN(gui_y2, gui_y3)) - mapview.height / 2;

    *xmax = MAX(gui_x4, MAX(gui_x2, gui_x3)) + mapview.width / 2;
    *ymax = MAX(gui_y4, MAX(gui_y2, gui_y3)) + mapview.height / 2;
  }

  /* Make sure the scroll window is big enough to hold the mapview.  If
   * not scrolling will be very ugly and the GUI may become confused. */
  diff = *xsize - (*xmax - *xmin);
  if (diff > 0) {
    *xmin -= diff / 2;
    *xmax += (diff + 1) / 2;
  }

  diff = *ysize - (*ymax - *ymin);
  if (diff > 0) {
    *ymin -= diff / 2;
    *ymax += (diff + 1) / 2;
  }

  freelog(LOG_DEBUG, "x: %d<-%d->%d; y: %d<-%d->%d",
	  *xmin, *xsize, *xmax, *ymin, *ymax, *ysize);
}

/****************************************************************************
  Find the scroll step for the mapview.  This is the amount to scroll (in
  scroll coordinates) on each "step".  See also get_mapview_scroll_window.
****************************************************************************/
void get_mapview_scroll_step(int *xstep, int *ystep)
{
  *xstep = NORMAL_TILE_WIDTH;
  *ystep = NORMAL_TILE_HEIGHT;

  if (tileset_is_isometric()) {
    *xstep /= 2;
    *ystep /= 2;
  }
}

/****************************************************************************
  Find the current scroll position (origin) of the mapview.
****************************************************************************/
void get_mapview_scroll_pos(int *scroll_x, int *scroll_y)
{
  *scroll_x = mapview.gui_x0;
  *scroll_y = mapview.gui_y0;
}

/****************************************************************************
  Set the scroll position (origin) of the mapview, and update the GUI.
****************************************************************************/
void set_mapview_scroll_pos(int scroll_x, int scroll_y)
{
  int gui_x0 = scroll_x, gui_y0 = scroll_y;

  can_slide = FALSE;
  set_mapview_origin(gui_x0, gui_y0);
  can_slide = TRUE;
}

/**************************************************************************
  Finds the current center tile of the mapcanvas.
**************************************************************************/
struct tile *get_center_tile_mapcanvas(void)
{
  return canvas_pos_to_nearest_tile(mapview.width / 2,
				    mapview.height / 2);
}

/**************************************************************************
  Centers the mapview around (map_x, map_y).
**************************************************************************/
void center_tile_mapcanvas(struct tile *ptile)
{
  int gui_x, gui_y;
  static bool first = TRUE;

  assert(!first || !can_slide);
  first = FALSE;

  map_to_gui_pos(&gui_x, &gui_y, ptile->x, ptile->y);

  /* Put the center pixel of the tile at the exact center of the mapview. */
  gui_x -= (mapview.width - NORMAL_TILE_WIDTH) / 2;
  gui_y -= (mapview.height - NORMAL_TILE_HEIGHT) / 2;

  set_mapview_origin(gui_x, gui_y);
}

/**************************************************************************
  Return TRUE iff the given map position has a tile visible on the
  map canvas.
**************************************************************************/
bool tile_visible_mapcanvas(struct tile *ptile)
{
  int dummy_x, dummy_y;		/* well, it needs two pointers... */

  return tile_to_canvas_pos(&dummy_x, &dummy_y, ptile);
}

/**************************************************************************
  Return TRUE iff the given map position has a tile visible within the
  interior of the map canvas. This information is used to determine
  when we need to recenter the map canvas.

  The logic of this function is simple: if a tile is within 1.5 tiles
  of a border of the canvas and that border is not aligned with the
  edge of the map, then the tile is on the "border" of the map canvas.

  This function is only correct for the current topology.
**************************************************************************/
bool tile_visible_and_not_on_border_mapcanvas(struct tile *ptile)
{
  int canvas_x, canvas_y;
  int xmin, ymin, xmax, ymax, xsize, ysize, scroll_x, scroll_y;
  const int border_x = (tileset_is_isometric() ? NORMAL_TILE_WIDTH / 2
			: 2 * NORMAL_TILE_WIDTH);
  const int border_y = (tileset_is_isometric() ? NORMAL_TILE_HEIGHT / 2
			: 2 * NORMAL_TILE_HEIGHT);
  bool same = (tileset_is_isometric() == MAP_IS_ISOMETRIC);

  get_mapview_scroll_window(&xmin, &ymin, &xmax, &ymax, &xsize, &ysize);
  get_mapview_scroll_pos(&scroll_x, &scroll_y);

  if (!tile_to_canvas_pos(&canvas_x, &canvas_y, ptile)) {
    /* The tile isn't visible at all. */
    return FALSE;
  }

  /* For each direction: if the tile is too close to the mapview border
   * in that direction, and scrolling can get us any closer to the
   * border, then it's a border tile.  We can only really check the
   * scrolling when the mapview window lines up with the map. */
  if (canvas_x < border_x
      && (!same || scroll_x > xmin || topo_has_flag(TF_WRAPX))) {
    return FALSE;
  }
  if (canvas_y < border_y
      && (!same || scroll_y > ymin || topo_has_flag(TF_WRAPY))) {
    return FALSE;
  }
  if (canvas_x + NORMAL_TILE_WIDTH > mapview.width - border_x
      && (!same || scroll_x + xsize < xmax || topo_has_flag(TF_WRAPX))) {
    return FALSE;
  }
  if (canvas_y + NORMAL_TILE_HEIGHT > mapview.height - border_y
      && (!same || scroll_y + ysize < ymax || topo_has_flag(TF_WRAPY))) {
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
  Draw an array of drawn sprites onto the canvas.
**************************************************************************/
static void put_drawn_sprites(struct canvas *pcanvas,
			      int canvas_x, int canvas_y,
			      int count, struct drawn_sprite *pdrawn,
			      bool fog)
{
  int i;

  for (i = 0; i < count; i++) {
    int ox, oy, dx, dy;

    switch (pdrawn[i].type) {
    case DRAWN_SPRITE:
      if (!pdrawn[i].data.sprite.sprite) {
	/* This can happen, although it should probably be avoided. */
	break;
      }
      ox = pdrawn[i].data.sprite.offset_x;
      oy = pdrawn[i].data.sprite.offset_y;
      if (pdrawn[i].data.sprite.style == DRAW_FULL) {
	dx = UNIT_TILE_WIDTH - NORMAL_TILE_WIDTH;
	dy = UNIT_TILE_HEIGHT - NORMAL_TILE_HEIGHT;
      } else {
	dx = dy = 0;
      }
      if (fog && pdrawn[i].data.sprite.foggable) {
	canvas_put_sprite_fogged(pcanvas,
				 canvas_x + ox - dx, canvas_y + oy - dy,
				 pdrawn[i].data.sprite.sprite,
				 TRUE,
				 canvas_x, canvas_y);
      } else {
	/* We avoid calling canvas_put_sprite_fogged, even though it
	 * should be a valid thing to do, because gui-gtk-2.0 doesn't have
	 * a full implementation. */
	canvas_put_sprite_full(pcanvas,
			       canvas_x + ox - dx, canvas_y + oy - dy,
			       pdrawn[i].data.sprite.sprite);
      }
      break;
    case DRAWN_BG:
      /*** Background color. ***/
      if (tileset_is_isometric()) {
	canvas_fill_sprite_area(pcanvas, sprites.mask.tile,
				pdrawn[i].data.bg.color,
				canvas_x, canvas_y);
	if (fog) {
	  canvas_fog_sprite_area(pcanvas, sprites.mask.tile,
				 canvas_x, canvas_y);
	}
      } else {
	canvas_put_rectangle(pcanvas, pdrawn[i].data.bg.color,
			     canvas_x, canvas_y,
			     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      }
      break;
    }
  }
}

/**************************************************************************
  Draw one layer of a tile, edge, corner, unit, and/or city onto the
  canvas at the given position.
**************************************************************************/
void put_one_element(struct canvas *pcanvas, enum mapview_layer layer,
		     struct tile *ptile,
		     const struct tile_edge *pedge,
		     const struct tile_corner *pcorner,
		     const struct unit *punit, struct city *pcity,
		     int canvas_x, int canvas_y,
		     const struct city *citymode)
{
  struct drawn_sprite tile_sprs[80];
  int count = fill_sprite_array(tileset, tile_sprs, layer,
				ptile, pedge, pcorner,
				punit, pcity, citymode);
  bool fog = (ptile && draw_fog_of_war
	      && tile_get_known(ptile) == TILE_KNOWN_FOGGED);

  /*** Draw terrain and specials ***/
  put_drawn_sprites(pcanvas, canvas_x, canvas_y, count, tile_sprs, fog);
}

/**************************************************************************
  Draw the given unit onto the canvas store at the given location.  The
  area of drawing is UNIT_TILE_HEIGHT x UNIT_TILE_WIDTH.
**************************************************************************/
void put_unit(const struct unit *punit,
	      struct canvas *pcanvas, int canvas_x, int canvas_y)
{
  canvas_y += (UNIT_TILE_HEIGHT - NORMAL_TILE_HEIGHT);
  mapview_layer_iterate(layer) {
    put_one_element(pcanvas, layer, NULL, NULL, NULL,
		    punit, NULL, canvas_x, canvas_y, NULL);
  } mapview_layer_iterate_end;
}

/**************************************************************************
  Draw the given city onto the canvas store at the given location.  The
  area of drawing is UNIT_TILE_HEIGHT x UNIT_TILE_WIDTH.
**************************************************************************/
void put_city(struct city *pcity,
	      struct canvas *pcanvas, int canvas_x, int canvas_y)
{
  canvas_y += (UNIT_TILE_HEIGHT - NORMAL_TILE_HEIGHT);
  mapview_layer_iterate(layer) {
    put_one_element(pcanvas, layer,
		    NULL, NULL, NULL, NULL, pcity,
		    canvas_x, canvas_y, NULL);
  } mapview_layer_iterate_end;
}

/**************************************************************************
  Draw the given tile terrain onto the canvas store at the given location.
  The area of drawing is UNIT_TILE_HEIGHT x UNIT_TILE_WIDTH (even though
  most tiles are not this tall).
**************************************************************************/
void put_terrain(struct tile *ptile,
		 struct canvas *pcanvas, int canvas_x, int canvas_y)
{
  /* Use full tile height, even for terrains. */
  canvas_y += (UNIT_TILE_HEIGHT - NORMAL_TILE_HEIGHT);
  mapview_layer_iterate(layer) {
    put_one_element(pcanvas, layer, ptile, NULL, NULL, NULL, NULL,
		    canvas_x, canvas_y, NULL);
  } mapview_layer_iterate_end;
}

/****************************************************************************
  Draw food, shield, and trade output values on the tile.

  The proper way to do this is probably something like what Civ II does
  (one sprite drawn N times on top of itself), but we just use separate
  sprites (limiting the number of combinations).
****************************************************************************/
void put_city_tile_output(struct city *pcity, int city_x, int city_y,
			  struct canvas *pcanvas,
			  int canvas_x, int canvas_y)
{
  int food = city_get_output_tile(city_x, city_y, pcity, O_FOOD);
  int shields = city_get_output_tile(city_x, city_y, pcity, O_SHIELD);
  int trade = city_get_output_tile(city_x, city_y, pcity, O_TRADE);

  food = CLIP(0, food, NUM_TILES_DIGITS - 1);
  shields = CLIP(0, shields, NUM_TILES_DIGITS - 1);
  trade = CLIP(0, trade, NUM_TILES_DIGITS - 1);

  /* In iso-view the output sprite is a bit smaller than the tile, so we
   * have to use an offset. */
  if (tileset_is_isometric()) {
    canvas_x += NORMAL_TILE_WIDTH / 3;
    canvas_y -= NORMAL_TILE_HEIGHT / 3;
  }

  canvas_put_sprite_full(pcanvas, canvas_x, canvas_y,
			 sprites.city.tile_foodnum[food]);
  canvas_put_sprite_full(pcanvas, canvas_x, canvas_y,
			 sprites.city.tile_shieldnum[shields]);
  canvas_put_sprite_full(pcanvas, canvas_x, canvas_y,
			 sprites.city.tile_tradenum[trade]);
}

/****************************************************************************
  Draw food, gold, and shield upkeep values on the unit.

  The proper way to do this is probably something like what Civ II does
  (one sprite drawn N times on top of itself), but we just use separate
  sprites (limiting the number of combinations).
****************************************************************************/
void put_unit_city_overlays(struct unit *punit,
			    struct canvas *pcanvas,
			    int canvas_x, int canvas_y)
{
  int unhappy = CLIP(0, punit->unhappiness, 2);

  /* draw overlay pixmaps */
  if (unhappy > 0) {
    canvas_put_sprite_full(pcanvas, canvas_x, canvas_y,
			   sprites.upkeep.unhappy[unhappy - 1]);
  }

  output_type_iterate(o) {
    int upkeep = CLIP(0, punit->upkeep[o], 2);

    if (upkeep > 0 && sprites.upkeep.output[o][upkeep - 1]) {
      canvas_put_sprite_full(pcanvas, canvas_x, canvas_y,
			     sprites.upkeep.output[o][upkeep - 1]);
    }
  } output_type_iterate_end;
}

/*
 * pcity->client.color_index is an index into the city_colors array.
 * When toggle_city_color is called the city's coloration is toggled.  When
 * a city is newly colored its color is taken from color_index and
 * color_index is moved forward one position. Each color in the array
 * tells what color the citymap will be drawn on the mapview.
 *
 * This array can be added to without breaking anything elsewhere.
 */
static int color_index = 0;
#define NUM_CITY_COLORS (sprites.city.worked_tile_overlay.size)


/****************************************************************************
  Toggle the city color.  This cycles through the possible colors for the
  citymap as shown on the mapview.  These colors are listed in the
  city_colors array; above.
****************************************************************************/
void toggle_city_color(struct city *pcity)
{
  if (pcity->client.colored) {
    pcity->client.colored = FALSE;
  } else {
    pcity->client.colored = TRUE;
    pcity->client.color_index = color_index;
    color_index = (color_index + 1) % NUM_CITY_COLORS;
  }

  refresh_city_mapcanvas(pcity, pcity->tile, TRUE, FALSE);
}

/****************************************************************************
  Toggle the unit color.  This cycles through the possible colors for the
  citymap as shown on the mapview.  These colors are listed in the
  city_colors array; above.
****************************************************************************/
void toggle_unit_color(struct unit *punit)
{
  if (punit->client.colored) {
    punit->client.colored = FALSE;
  } else {
    punit->client.colored = TRUE;
    punit->client.color_index = color_index;
    color_index = (color_index + 1) % NUM_CITY_COLORS;
  }

  refresh_unit_mapcanvas(punit, punit->tile, TRUE, FALSE);
}

/****************************************************************************
  Animate the nuke explosion at map(x, y).
****************************************************************************/
void put_nuke_mushroom_pixmaps(struct tile *ptile)
{
  int canvas_x, canvas_y;
  struct Sprite *mysprite = sprites.explode.nuke;
  int width, height;

  /* We can't count on the return value of tile_to_canvas_pos since the
   * sprite may span multiple tiles. */
  (void) tile_to_canvas_pos(&canvas_x, &canvas_y, ptile);
  get_sprite_dimensions(mysprite, &width, &height);

  canvas_x += (NORMAL_TILE_WIDTH - width) / 2;
  canvas_y += (NORMAL_TILE_HEIGHT - height) / 2;

  /* Make sure everything is flushed and synced before proceeding.  First
   * we update everything to the store, but don't write this to screen.
   * Then add the nuke graphic to the store.  Finally flush everything to
   * the screen and wait 1 second. */
  unqueue_mapview_updates(FALSE);

  canvas_put_sprite_full(mapview.store, canvas_x, canvas_y, mysprite);
  dirty_rect(canvas_x, canvas_y, width, height);

  flush_dirty();
  gui_flush();

  myusleep(1000000);

  update_map_canvas_visible();
}

/**************************************************************************
  Draw some or all of a tile onto the canvas.
**************************************************************************/
static void put_one_tile(struct canvas *pcanvas, enum mapview_layer layer,
			 struct tile *ptile, int canvas_x, int canvas_y,
			 const struct city *citymode)
{
  if (tile_get_known(ptile) != TILE_UNKNOWN) {
    put_one_element(pcanvas, layer, ptile, NULL, NULL,
		    get_drawable_unit(ptile, citymode),
		    ptile->city, canvas_x, canvas_y, citymode);
  }
}

/**************************************************************************
  Update (refresh) the map canvas starting at the given tile (in map
  coordinates) and with the given dimensions (also in map coordinates).

  In non-iso view, this is easy.  In iso view, we have to use the
  Painter's Algorithm to draw the tiles in back first.  When we draw
  a tile, we tell the GUI which part of the tile to draw - which is
  necessary unless we have an extra buffering step.

  After refreshing the backing store tile-by-tile, we write the store
  out to the display if write_to_screen is specified.

  x, y, width, and height are in map coordinates; they need not be
  normalized or even real.
**************************************************************************/
void update_map_canvas(int canvas_x, int canvas_y, int width, int height)
{
  int gui_x0, gui_y0;
  bool full;
  struct canvas *tmp;

  canvas_x = MAX(canvas_x, 0);
  canvas_y = MAX(canvas_y, 0);
  width = MIN(mapview.store_width - canvas_x, width);
  height = MIN(mapview.store_height - canvas_y, height);

  gui_x0 = mapview.gui_x0 + canvas_x;
  gui_y0 = mapview.gui_y0 + canvas_y;
  full = (canvas_x == 0 && canvas_y == 0
	  && width == mapview.store_width
	  && height == mapview.store_height);

  freelog(LOG_DEBUG,
	  "update_map_canvas(pos=(%d,%d), size=(%d,%d))",
	  canvas_x, canvas_y, width, height);

  /* If a full redraw is done, we just draw everything onto the canvas.
   * However if a partial redraw is done we draw everything onto the
   * tmp_canvas then copy *just* the area of update onto the canvas. */
  if (!full) {
    /* Swap store and tmp_store. */
    tmp = mapview.store;
    mapview.store = mapview.tmp_store;
    mapview.tmp_store = tmp;
  }

  /* Clear the area.  This is necessary since some parts of the rectangle
   * may not actually have any tiles drawn on them.  This will happen when
   * the mapview is large enough so that the tile is visible in multiple
   * locations.  In this case it will only be drawn in one place.
   *
   * Of course it's necessary to draw to the whole area to cover up any old
   * drawing that was done there. */
  canvas_put_rectangle(mapview.store, COLOR_STD_BLACK,
		       canvas_x, canvas_y, width, height);

  mapview_layer_iterate(layer) {
    gui_rect_iterate(gui_x0, gui_y0, width,
		     height + (tileset_is_isometric()
			       ? (NORMAL_TILE_HEIGHT / 2) : 0),
		     ptile, pedge, pcorner, gui_x, gui_y) {
      const int cx = gui_x - mapview.gui_x0, cy = gui_y - mapview.gui_y0;

      if (ptile) {
	put_one_tile(mapview.store, layer, ptile, cx, cy, NULL);
      } else if (pedge) {
	put_one_element(mapview.store, layer, NULL, pedge, NULL,
			NULL, NULL, cx, cy, NULL);
      } else if (pcorner) {
	put_one_element(mapview.store, layer, NULL, NULL, pcorner,
			NULL, NULL, cx, cy, NULL);
      } else {
	/* This can happen, for instance for unreal tiles. */
      }
    } gui_rect_iterate_end;
  } mapview_layer_iterate_end;

  /* Draw the goto lines on top of the whole thing. This is done last as
   * we want it completely on top.
   *
   * Note that a pixel right on the border of a tile may actually contain a
   * goto line from an adjacent tile.  Thus we draw any extra goto lines
   * from adjacent tiles (if they're close enough). */
  gui_rect_iterate(gui_x0 - GOTO_WIDTH, gui_y0 - GOTO_WIDTH,
		   width + 2 * GOTO_WIDTH, height + 2 * GOTO_WIDTH,
		   ptile, pedge, pcorner, gui_x, gui_y) {
    if (!ptile) {
      continue;
    }
    adjc_dir_iterate(ptile, adjc_tile, dir) {
      if (is_drawn_line(ptile, dir)) {
	draw_segment(ptile, dir);
      }
    } adjc_dir_iterate_end;
  } gui_rect_iterate_end;

  /* Draw citymap overlays on top. */
  gui_rect_iterate(gui_x0, gui_y0, width, height,
		   ptile, pedge, pcorner, gui_x, gui_y) {
    const int canvas_x2 = gui_x - mapview.gui_x0;
    const int canvas_y2 = gui_y - mapview.gui_y0;

    if (ptile && tile_get_known(ptile) != TILE_UNKNOWN) {
      struct unit *punit;
      struct city *pcity;
      int city_x, city_y;

      pcity = find_city_or_settler_near_tile(ptile, &punit);
      if (pcity
	  && pcity->client.colored
	  && map_to_city_map(&city_x, &city_y, pcity, ptile)) {
	enum city_tile_type worker = get_worker_city(pcity, city_x, city_y);
	int index = pcity->client.color_index % NUM_CITY_COLORS;

	switch (worker) {
	case C_TILE_EMPTY:
	  canvas_put_sprite_full(mapview.store,
				 canvas_x2, canvas_y2,
				 sprites.city.unworked_tile_overlay.p[index]);
	  break;
	case C_TILE_WORKER:
	  canvas_put_sprite_full(mapview.store,
				 canvas_x2, canvas_y2,
				 sprites.city.worked_tile_overlay.p[index]);
	  put_city_tile_output(pcity, city_x, city_y,
			       mapview.store, canvas_x2, canvas_y2);
	  break;
	case C_TILE_UNAVAILABLE:
	  break;
	}
      } else if (punit && punit->client.colored) {
	int index = punit->client.color_index % NUM_CITY_COLORS;

	/* Draw citymap overlay for settlers. */
	canvas_put_sprite_full(mapview.store,
			       canvas_x2, canvas_y2,
			       sprites.city.unworked_tile_overlay.p[index]);
      }
    }
  } gui_rect_iterate_end;

  show_city_descriptions(canvas_x, canvas_y, width, height);

  if (!full) {
    /* Swap store and tmp_store back. */
    tmp = mapview.store;
    mapview.store = mapview.tmp_store;
    mapview.tmp_store = tmp;

    /* And copy store to tmp_store. */
    canvas_copy(mapview.store, mapview.tmp_store,
		canvas_x, canvas_y, canvas_x, canvas_y, width, height);
  }

  dirty_rect(canvas_x, canvas_y, width, height);
}

/**************************************************************************
 Update (only) the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  queue_mapview_update(UPDATE_MAP_CANVAS_VISIBLE);
}

/* The maximum city description width and height.  This gives the dimensions
 * of a rectangle centered directly beneath the tile a city is on, that
 * contains the city description.
 *
 * These values are increased when drawing is done.  This may mean that
 * the change (from increasing the value) won't take place until the
 * next redraw. */
static int max_desc_width = 0, max_desc_height = 0;

/**************************************************************************
  Update the city description for the given city.
**************************************************************************/
void update_city_description(struct city *pcity)
{
  queue_mapview_tile_update(pcity->tile, TILE_UPDATE_CITY_DESC);
}

/**************************************************************************
  Show descriptions for all cities visible on the map canvas.
**************************************************************************/
void show_city_descriptions(int canvas_x, int canvas_y,
			    int width, int height)
{
  const int dx = max_desc_width - NORMAL_TILE_WIDTH, dy = max_desc_height;

  if (!draw_city_names && !draw_city_productions) {
    return;
  }

  prepare_show_city_descriptions();

  /* A city description is shown below the city.  It has a specified
   * maximum width and height (although these are only estimates).  Thus
   * we need to update some tiles above the mapview and some to the left
   * and right.
   *
   *                    /--W1--\   (W1 = NORMAL_TILE_WIDTH)
   *                    -------- \
   *                    | CITY | H1 (H1 = NORMAL_TILE_HEIGHT)
   *                    |      | /
   *               ------------------ \
   *               |  DESCRIPTION   | H2  (H2 = MAX_CITY_DESC_HEIGHT)
   *               |                | /
   *               ------------------
   *               \-------W2-------/    (W2 = MAX_CITY_DESC_WIDTH)
   *
   * We must draw H2 extra pixels above and (W2 - W1) / 2 extra pixels
   * to each side of the mapview.
   */
  gui_rect_iterate(mapview.gui_x0 + canvas_x - dx / 2,
		   mapview.gui_y0 + canvas_y - dy,
		   width + dx, height + dy - NORMAL_TILE_HEIGHT,
		   ptile, pedge, pcorner, gui_x, gui_y) {
    const int canvas_x = gui_x - mapview.gui_x0;
    const int canvas_y = gui_y - mapview.gui_y0;

    if (ptile && ptile->city) {
      int width = 0, height = 0;
      struct city *pcity = ptile->city;

      show_city_desc(mapview.store, canvas_x, canvas_y,
		     pcity, &width, &height);

      max_desc_width = MAX(width, max_desc_width);
      max_desc_height = MAX(height, max_desc_height);
    }
  } gui_rect_iterate_end;
}

/****************************************************************************
  Draw the goto route for the unit.  Return TRUE if anything is drawn.

  This duplicates drawing code that is run during the hover state.
****************************************************************************/
bool show_unit_orders(struct unit *punit)
{
  if (punit && unit_has_orders(punit)) {
    struct tile *ptile = punit->tile;
    int i;

    for (i = 0; i < punit->orders.length; i++) {
      int index = (punit->orders.index + i) % punit->orders.length;
      struct unit_order *order;

      if (punit->orders.index + i >= punit->orders.length
	  && !punit->orders.repeat) {
	break;
      }

      order = &punit->orders.list[index];

      switch (order->order) {
      case ORDER_MOVE:
	draw_segment(ptile, order->dir);
	ptile = mapstep(ptile, order->dir);
	if (!ptile) {
	  /* This shouldn't happen unless the server gives us invalid
	   * data.  To avoid disaster we need to break out of the
	   * switch and the enclosing for loop. */
	  assert(0);
	  i = punit->orders.length;
	}
	break;
      default:
	/* TODO: graphics for other orders. */
	break;
      }
    }
    return TRUE;
  } else {
    return FALSE;
  }
}

#define ABS(x) (((x) >= 0) ? (x) : -(x))

/****************************************************************************
  Draw a goto line at the given location and direction.  The line goes from
  the source tile to the adjacent tile in the given direction.
****************************************************************************/
void draw_segment(struct tile *src_tile, enum direction8 dir)
{
  int canvas_x, canvas_y, canvas_dx, canvas_dy;

  /* Determine the source position of the segment. */
  (void) tile_to_canvas_pos(&canvas_x, &canvas_y, src_tile);
  canvas_x += NORMAL_TILE_WIDTH / 2;
  canvas_y += NORMAL_TILE_HEIGHT / 2;

  /* Determine the vector of the segment. */
  map_to_gui_vector(&canvas_dx, &canvas_dy, DIR_DX[dir], DIR_DY[dir]);

  /* Draw the segment. */
  canvas_put_line(mapview.store, COLOR_STD_CYAN, LINE_GOTO,
		  canvas_x, canvas_y, canvas_dx, canvas_dy);

  /* The actual area drawn will extend beyond the base rectangle, since
   * the goto lines have width. */
  dirty_rect(MIN(canvas_x, canvas_x + canvas_dx) - GOTO_WIDTH,
	     MIN(canvas_y, canvas_y + canvas_dy) - GOTO_WIDTH,
	     ABS(canvas_dx) + 2 * GOTO_WIDTH,
	     ABS(canvas_dy) + 2 * GOTO_WIDTH);

  /* It is possible that the mapview wraps between the source and dest
   * tiles.  In this case they will not be next to each other; they'll be
   * on the opposite sides of the screen.  If this happens then the dest
   * tile will not be updated.  This is consistent with the mapview design
   * which fails when the size of the mapview approaches that of the map. */
}

/**************************************************************************
  Remove the line from src_x, src_y in the given direction, and redraw
  the change if necessary.
**************************************************************************/
void undraw_segment(struct tile *src_tile, enum direction8 dir)
{
  struct tile *dst_tile = mapstep(src_tile, dir);

  if (is_drawn_line(src_tile, dir) || !dst_tile) {
    assert(0);
    return;
  }
  refresh_tile_mapcanvas(src_tile, FALSE, FALSE);
  refresh_tile_mapcanvas(dst_tile, FALSE, FALSE);
}

/****************************************************************************
  This function is called to decrease a unit's HP smoothly in battle
  when combat_animation is turned on.
****************************************************************************/
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1)
{
  static struct timer *anim_timer = NULL; 
  struct sprite_vector *anim = get_unit_explode_animation();
  const int num_tiles_explode_unit = sprite_vector_size(anim);
  struct unit *losing_unit = (hp0 == 0 ? punit0 : punit1);
  int canvas_x, canvas_y, i;

  set_units_in_combat(punit0, punit1);

  unqueue_mapview_updates(TRUE);
  while (punit0->hp > hp0 || punit1->hp > hp1) {
    const int diff0 = punit0->hp - hp0, diff1 = punit1->hp - hp1;

    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    if (myrand(diff0 + diff1) < diff0) {
      punit0->hp--;
      refresh_unit_mapcanvas(punit0, punit0->tile, FALSE, FALSE);
    } else {
      punit1->hp--;
      refresh_unit_mapcanvas(punit1, punit1->tile, FALSE, FALSE);
    }

    unqueue_mapview_updates(TRUE);
    gui_flush();

    usleep_since_timer_start(anim_timer, 10000);
  }

  if (num_tiles_explode_unit > 0
      && tile_to_canvas_pos(&canvas_x, &canvas_y,
			   losing_unit->tile)) {
    refresh_unit_mapcanvas(losing_unit, losing_unit->tile, FALSE, FALSE);
    unqueue_mapview_updates(FALSE);
    canvas_copy(mapview.tmp_store, mapview.store,
		canvas_x, canvas_y, canvas_x, canvas_y,
		NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

    for (i = 0; i < num_tiles_explode_unit; i++) {
      int w, h;
      struct Sprite *sprite = *sprite_vector_get(anim, i);

      get_sprite_dimensions(sprite, &w, &h);
      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      /* We first draw the explosion onto the unit and draw draw the
       * complete thing onto the map canvas window. This avoids
       * flickering. */
      canvas_copy(mapview.store, mapview.tmp_store,
		  canvas_x, canvas_y, canvas_x, canvas_y,
		  NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      canvas_put_sprite_full(mapview.store,
			     canvas_x + NORMAL_TILE_WIDTH / 2 - w / 2,
			     canvas_y + NORMAL_TILE_HEIGHT / 2 - h / 2,
			     sprite);
      dirty_rect(canvas_x, canvas_y, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

      flush_dirty();
      gui_flush();

      usleep_since_timer_start(anim_timer, 20000);
    }
  }

  set_units_in_combat(NULL, NULL);
  refresh_unit_mapcanvas(punit0, punit0->tile, TRUE, FALSE);
  refresh_unit_mapcanvas(punit1, punit1->tile, TRUE, FALSE);
}

/**************************************************************************
  Animates punit's "smooth" move from (x0, y0) to (x0+dx, y0+dy).
  Note: Works only for adjacent-tile moves.
**************************************************************************/
void move_unit_map_canvas(struct unit *punit,
			  struct tile *src_tile, int dx, int dy)
{
  static struct timer *anim_timer = NULL;
  struct tile *dest_tile;
  int dest_x, dest_y;

  /* only works for adjacent-square moves */
  if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0)) {
    return;
  }

  if (punit == get_unit_in_focus() && hover_state != HOVER_NONE) {
    set_hover_state(NULL, HOVER_NONE, ACTIVITY_LAST, ORDER_LAST);
    update_unit_info_label(punit);
  }

  dest_x = src_tile->x + dx;
  dest_y = src_tile->y + dy;
  dest_tile = map_pos_to_tile(dest_x, dest_y);
  if (!dest_tile) {
    return;
  }

  if (tile_visible_mapcanvas(src_tile)
      || tile_visible_mapcanvas(dest_tile)) {
    int start_x, start_y;
    int canvas_dx, canvas_dy;
    double timing_sec = (double)smooth_move_unit_msec / 1000.0, mytime;

    assert(smooth_move_unit_msec > 0);

    map_to_gui_vector(&canvas_dx, &canvas_dy, dx, dy);

    tile_to_canvas_pos(&start_x, &start_y, src_tile);
    if (tileset_is_isometric()) {
      start_y -= NORMAL_TILE_HEIGHT / 2;
    }

    /* Bring the backing store up to date, but don't flush. */
    unqueue_mapview_updates(FALSE);

    /* Start the timer (AFTER the unqueue above). */
    anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

    do {
      int new_x, new_y;

      mytime = MIN(read_timer_seconds(anim_timer), timing_sec);

      new_x = start_x + canvas_dx * (mytime / timing_sec);
      new_y = start_y + canvas_dy * (mytime / timing_sec);

      /* Backup the canvas store to the temp store. */
      canvas_copy(mapview.tmp_store, mapview.store,
		  new_x, new_y, new_x, new_y,
		  UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);

      /* Draw */
      put_unit(punit, mapview.store, new_x, new_y);
      dirty_rect(new_x, new_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);

      /* Flush. */
      flush_dirty();
      gui_flush();

      /* Restore the backup.  It won't take effect until the next flush. */
      canvas_copy(mapview.store, mapview.tmp_store,
		  new_x, new_y, new_x, new_y,
		  UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
      dirty_rect(new_x, new_y, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    } while (mytime < timing_sec);
  }
}

/**************************************************************************
  Find the "best" city/settlers to associate with the selected tile.
    a.  If a city is working the tile, return that city.
    b.  If another player's city is working the tile, return NULL.
    c.  If any selected cities are within range, return the closest one.
    d.  If any cities are within range, return the closest one.
    e.  If any active (with color) settler could work it if they founded a
        city, choose the closest one (only if punit != NULL).
    f.  If any settler could work it if they founded a city, choose the
        closest one (only if punit != NULL).
    g.  If nobody can work it, return NULL.
**************************************************************************/
struct city *find_city_or_settler_near_tile(struct tile *ptile,
					    struct unit **punit)
{
  struct city *pcity = ptile->worked, *closest_city;
  struct unit *closest_settler = NULL, *best_settler = NULL;

  if (punit) {
    *punit = NULL;
  }

  if (pcity) {
    if (pcity->owner == game.player_idx) {
      /* rule a */
      return pcity;
    } else {
      /* rule b */
      return NULL;
    }
  }

  /* rule e */
  closest_city = NULL;

  city_map_checked_iterate(ptile, city_x, city_y, tile1) {
    pcity = map_get_city(tile1);
    if (pcity && pcity->owner == game.player_idx
	&& get_worker_city(pcity, CITY_MAP_SIZE - 1 - city_x,
			   CITY_MAP_SIZE - 1 - city_y) == C_TILE_EMPTY) {
      /*
       * Note, we must explicitly check if the tile is workable (with
       * get_worker_city(), above) since it is possible that another
       * city (perhaps an unseen enemy city) may be working it,
       * causing it to be marked as C_TILE_UNAVAILABLE.
       */
      
      if (map_deco[pcity->tile->index].hilite == HILITE_CITY) {
	/* rule c */
	return pcity;
      }
      if (!closest_city) {
	closest_city = pcity;
      }
    }
  } city_map_checked_iterate_end;

  /* rule d */
  if (closest_city || !punit) {
    return closest_city;
  }

  city_map_iterate_outwards(city_x, city_y) {
    struct tile *tile1 = base_city_map_to_map(ptile, city_x, city_y);;

    if (tile1) {
      unit_list_iterate(tile1->units, psettler) {
	if (psettler->owner == game.player_idx
	    && unit_flag(psettler, F_CITIES)
	    && city_can_be_built_here(psettler->tile, psettler)) {
	  if (!closest_settler) {
	    closest_settler = psettler;
	  }
	  if (!best_settler && psettler->client.colored) {
	    best_settler = psettler;
	  }
	}
      } unit_list_iterate_end;
    }
  } city_map_iterate_outwards_end;

  if (best_settler) {
    /* Rule e */
    *punit = best_settler;
  } else if (closest_settler) {
    /* Rule f */
    *punit = closest_settler;
  }

  /* rule g */
  return NULL;
}

/**************************************************************************
  Find the nearest/best city that owns the tile.
**************************************************************************/
struct city *find_city_near_tile(struct tile *ptile)
{
  return find_city_or_settler_near_tile(ptile, NULL);
}

/**************************************************************************
  Find the mapview city production text for the given city, and place it
  into the buffer.
**************************************************************************/
void get_city_mapview_production(struct city *pcity,
                                 char *buffer, size_t buffer_len)
{
  int turns = city_turns_to_build(pcity, pcity->currently_building,
				  pcity->is_building_unit, TRUE);
				
  if (pcity->is_building_unit) {
    struct unit_type *punit_type =
		get_unit_type(pcity->currently_building);
    if (turns < 999) {
      my_snprintf(buffer, buffer_len, "%s %d",
                  punit_type->name, turns);
    } else {
      my_snprintf(buffer, buffer_len, "%s -",
                  punit_type->name);
    }
  } else {
    struct impr_type *pimprovement_type =
		get_improvement_type(pcity->currently_building);
    if (get_current_construction_bonus(pcity, EFT_PROD_TO_GOLD) > 0) {
      my_snprintf(buffer, buffer_len, "%s", pimprovement_type->name);
    } else if (turns < 999) {
      my_snprintf(buffer, buffer_len, "%s %d",
		  pimprovement_type->name, turns);
    } else {
      my_snprintf(buffer, buffer_len, "%s -",
                  pimprovement_type->name);
    }
  }
}

static enum update_type needed_updates = UPDATE_NONE;
static bool callback_queued = FALSE;

/* These values hold the tiles that need city, unit, or tile updates.
 * These different types of updates just tell what area need to be updated,
 * not necessarily what's sitting on the tile.  A city update covers the
 * whole citymap area.  A unit update covers just the "full" unit tile
 * area.  A tile update covers the base tile plus half a tile in each
 * direction. */
struct tile_list *tile_updates[TILE_UPDATE_COUNT];

/****************************************************************************
  This callback is called during an idle moment to unqueue any pending
  mapview updates.
****************************************************************************/
static void queue_callback(void *data)
{
  callback_queued = FALSE;
  unqueue_mapview_updates(TRUE);
}

/****************************************************************************
  When a mapview update is queued this function should be called to prepare
  an idle-time callback to unqueue the updates.
****************************************************************************/
static void queue_add_callback(void)
{
  if (!callback_queued) {
    callback_queued = TRUE;
    add_idle_callback(queue_callback, NULL);
  }
}

/**************************************************************************
  This function, along with unqueue_mapview_update(), helps in updating
  the mapview when a packet is received.  Previously, we just called
  update_map_canvas when (for instance) a city update was received.
  Not only would this often end up with a lot of duplicated work, but it
  would also draw over the city descriptions, which would then just
  "disappear" from the mapview.  The hack is to instead call
  queue_mapview_update in place of this update, and later (after all
  packets have been read) call unqueue_mapview_update.  The functions
  don't track which areas of the screen need updating, rather when the
  unqueue is done we just update the whole visible mapqueue, and redraw
  the city descriptions.

  Using these functions, updates are done correctly, and are probably
  faster too.  But it's a bit of a hack to insert this code into the
  packet-handling code.
**************************************************************************/
void queue_mapview_update(enum update_type update)
{
  needed_updates |= update;
  queue_add_callback();
}

/**************************************************************************
  Queue this tile to be refreshed.  The refresh will be done some time
  soon thereafter, and grouped with other needed refreshes.

  Note this should only be called for tiles.  For cities or units use
  queue_mapview_xxx_update instead.
**************************************************************************/
void queue_mapview_tile_update(struct tile *ptile,
			       enum tile_update_type type)
{
  if (!tile_updates[type]) {
    tile_updates[type] = tile_list_new();
  }
  tile_list_append(tile_updates[type], ptile);
  queue_add_callback();
}

/**************************************************************************
  See comment for queue_mapview_update().
**************************************************************************/
void unqueue_mapview_updates(bool write_to_screen)
{
  /* Calculate the area covered by each update type.  The area array gives
   * the offset from the tile origin as well as the width and height of the
   * area to be updated.  This is initialized each time when entering the
   * function from the existing tileset variables.
   *
   * A TILE update covers the base tile (W x H) plus a half-tile in each
   * direction (for edge/corner graphics), making its area 2W x 2H.
   *
   * A UNIT update covers a UW x UH area.  This is centered horizontally
   * over the tile but extends up above the tile (e.g., units in iso-view).
   *
   * A CITYMAP update covers the whole citymap of a tile.  This includes
   * the citymap area itself plus an extra half-tile in each direction (for
   * edge/corner graphics).
   */
  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
  const int UW = UNIT_TILE_WIDTH, UH = UNIT_TILE_HEIGHT;
  const int city_width = get_citydlg_canvas_width() + W;
  const int city_height = get_citydlg_canvas_height() + H;
  const struct {
    int dx, dy, w, h;
  } area[TILE_UPDATE_COUNT] = {
    {0, 0, W, H},
    {-W / 2, -H / 2, 2 * W, 2 * H},
    {(W - UW) / 2, H - UH, UW, UH},
    {-(max_desc_width - W) / 2, H, max_desc_width, max_desc_height},
    {-(city_width - W) / 2, -(city_height - H) / 2, city_width, city_height}
  };

  int i;

  freelog(LOG_DEBUG, "unqueue_mapview_update: needed_updates=%d",
	  needed_updates);

  if (map_exists()) {
    if ((needed_updates & UPDATE_MAP_CANVAS_VISIBLE)
	|| (needed_updates & UPDATE_CITY_DESCRIPTIONS)) {
      dirty_all();
      update_map_canvas(0, 0, mapview.store_width,
			mapview.store_height);
    } else {
      int min_x = mapview.width, min_y = mapview.height;
      int max_x = 0, max_y = 0;
      int i;

      for (i = 0; i < TILE_UPDATE_COUNT; i++) {
	if (tile_updates[i]) {
	  tile_list_iterate(tile_updates[i], ptile) {
	    int x0, y0, x1, y1;

	    (void) tile_to_canvas_pos(&x0, &y0, ptile);

	    x0 += area[i].dx;
	    y0 += area[i].dy;
	    x1 = x0 + area[i].w;
	    y1 = y0 + area[i].h;

	    if (x1 > 0 && x0 < mapview.width
		&& y1 > 0 && y0 < mapview.height) {
	      min_x = MIN(min_x, x0);
	      min_y = MIN(min_y, y0);
	      max_x = MAX(max_x, x1);
	      max_y = MAX(max_y, y1);
	    }

	    /* FIXME: These overview updates should be batched as well.
	     * Right now they account for as much as 90% of the runtime of
	     * the unqueue. */
	    overview_update_tile(ptile);
	  } tile_list_iterate_end;
	}
      }

      if (min_x < max_x && min_y < max_y) {
	update_map_canvas(min_x, min_y, max_x - min_x, max_y - min_y);
      }
    }
  }
  for (i = 0; i < TILE_UPDATE_COUNT; i++) {
    if (tile_updates[i]) {
      tile_list_unlink_all(tile_updates[i]);
    }
  }
  needed_updates = UPDATE_NONE;

  if (write_to_screen) {
    flush_dirty();
    flush_dirty_overview();
  }
}

/**************************************************************************
  Fill the two buffers which information about the city which is shown
  below it. It takes draw_city_names and draw_city_growth into account.
**************************************************************************/
void get_city_mapview_name_and_growth(struct city *pcity,
				      char *name_buffer,
				      size_t name_buffer_len,
				      char *growth_buffer,
				      size_t growth_buffer_len,
				      enum color_std *growth_color)
{
  if (!draw_city_names) {
    name_buffer[0] = '\0';
    growth_buffer[0] = '\0';
    *growth_color = COLOR_STD_WHITE;
    return;
  }

  my_snprintf(name_buffer, name_buffer_len, pcity->name);

  if (draw_city_growth && pcity->owner == game.player_idx) {
    int turns = city_turns_to_grow(pcity);

    if (turns == 0) {
      my_snprintf(growth_buffer, growth_buffer_len, "X");
    } else if (turns == FC_INFINITY) {
      my_snprintf(growth_buffer, growth_buffer_len, "-");
    } else {
      /* Negative turns means we're shrinking, but that's handled
         down below. */
      my_snprintf(growth_buffer, growth_buffer_len, "%d", abs(turns));
    }

    if (turns <= 0) {
      /* A blocked or shrinking city has its growth status shown in red. */
      *growth_color = COLOR_STD_RED;
    } else {
      *growth_color = COLOR_STD_WHITE;
    }
  } else {
    growth_buffer[0] = '\0';
    *growth_color = COLOR_STD_WHITE;
  }
}

/**************************************************************************
  Copies the overview image from the backing store to the window and
  draws the viewrect on top of it.
**************************************************************************/
static void redraw_overview(void)
{
  struct canvas *dest = get_overview_window();

  if (!dest || !overview.store) {
    return;
  }

  {
    struct canvas *src = overview.store;
    int x = overview.map_x0 * OVERVIEW_TILE_SIZE;
    int y = overview.map_y0 * OVERVIEW_TILE_SIZE;
    int ix = overview.width - x;
    int iy = overview.height - y;

    canvas_copy(dest, src, 0, 0, ix, iy, x, y);
    canvas_copy(dest, src, 0, y, ix, 0, x, iy);
    canvas_copy(dest, src, x, 0, 0, iy, ix, y);
    canvas_copy(dest, src, x, y, 0, 0, ix, iy);
  }

  {
    int i;
    int x[4], y[4];

    get_mapview_corners(x, y);

    for (i = 0; i < 4; i++) {
      int src_x = x[i];
      int src_y = y[i];
      int dest_x = x[(i + 1) % 4];
      int dest_y = y[(i + 1) % 4];

      canvas_put_line(dest, COLOR_STD_WHITE, LINE_NORMAL, src_x, src_y,
		      dest_x - src_x, dest_y - src_y);
    }
  }

  overview_dirty = FALSE;
}

/****************************************************************************
  Mark the overview as "dirty" so that it will be redrawn soon.
****************************************************************************/
static void dirty_overview(void)
{
  overview_dirty = TRUE;
}

/****************************************************************************
  Redraw the overview if it is "dirty".
****************************************************************************/
static void flush_dirty_overview(void)
{
  if (overview_dirty) {
    redraw_overview();
  }
}

/**************************************************************************
  Center the overview around the mapview.
**************************************************************************/
static void center_tile_overviewcanvas(struct tile *ptile)
{
  /* The overview coordinates are equivalent to (scaled) natural
   * coordinates. */
  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    /* NOTE: this embeds the map wrapping in the overview code.  This is
     * basically necessary for the overview to be efficiently
     * updated. */
    if (topo_has_flag(TF_WRAPX)) {
      overview.map_x0 = FC_WRAP(ntl_x - NATURAL_WIDTH / 2, NATURAL_WIDTH);
    } else {
      overview.map_x0 = 0;
    }
    if (topo_has_flag(TF_WRAPY)) {
      overview.map_y0 = FC_WRAP(ntl_y - NATURAL_HEIGHT / 2, NATURAL_HEIGHT);
    } else {
      overview.map_y0 = 0;
    }
    redraw_overview();
  } do_in_natural_pos_end;
}

/**************************************************************************
  Finds the overview (canvas) coordinates for a given map position.
**************************************************************************/
void map_to_overview_pos(int *overview_x, int *overview_y,
			 int map_x, int map_y)
{
  /* The map position may not be normal, for instance when the mapview
   * origin is not a normal position.
   *
   * NOTE: this embeds the map wrapping in the overview code. */
  do_in_natural_pos(ntl_x, ntl_y, map_x, map_y) {
    int ovr_x = ntl_x - overview.map_x0, ovr_y = ntl_y - overview.map_y0;

    if (topo_has_flag(TF_WRAPX)) {
      ovr_x = FC_WRAP(ovr_x, NATURAL_WIDTH);
    } else {
      if (MAP_IS_ISOMETRIC) {
	/* HACK: For iso-maps that don't wrap in the X direction we clip
	 * a half-tile off of the left and right of the overview.  This
	 * means some tiles only are halfway shown.  However it means we
	 * don't show any unreal tiles, which we'd otherwise be doing.  The
	 * rest of the code can't handle unreal tiles in the overview. */
	ovr_x--;
      }
    }
    if (topo_has_flag(TF_WRAPY)) {
      ovr_y = FC_WRAP(ovr_y, NATURAL_HEIGHT);
    }
    *overview_x = OVERVIEW_TILE_SIZE * ovr_x;
    *overview_y = OVERVIEW_TILE_SIZE * ovr_y;
  } do_in_natural_pos_end;
}

/**************************************************************************
  Finds the map coordinates for a given overview (canvas) position.
**************************************************************************/
void overview_to_map_pos(int *map_x, int *map_y,
			 int overview_x, int overview_y)
{
  int ntl_x = overview_x / OVERVIEW_TILE_SIZE + overview.map_x0;
  int ntl_y = overview_y / OVERVIEW_TILE_SIZE + overview.map_y0;

  if (MAP_IS_ISOMETRIC && !topo_has_flag(TF_WRAPX)) {
    /* Clip half tile left and right.  See comment in map_to_overview_pos. */
    ntl_x++;
  }

  NATURAL_TO_MAP_POS(map_x, map_y, ntl_x, ntl_y);
  if (!normalize_map_pos(map_x, map_y)) {
    /* All positions on the overview should be valid. */
    assert(FALSE);
  }
}

/**************************************************************************
  Find the corners of the mapview, in overview coordinates.  Used to draw
  the "mapview window" rectangle onto the overview.
**************************************************************************/
static void get_mapview_corners(int x[4], int y[4])
{
  int map_x0, map_y0;

  base_canvas_to_map_pos(&map_x0, &map_y0, 0, 0);
  map_to_overview_pos(&x[0], &y[0], map_x0, map_y0);

  /* Note: these calculations operate on overview coordinates as if they
   * are natural.  Corners may be off by one tile, however. */

  if (tileset_is_isometric() && !MAP_IS_ISOMETRIC) {
    /* We start with the west corner. */

    /* North */
    x[1] = x[0] + OVERVIEW_TILE_WIDTH * mapview.tile_width;
    y[1] = y[0] - OVERVIEW_TILE_HEIGHT * mapview.tile_width;

    /* East */
    x[2] = x[1] + OVERVIEW_TILE_WIDTH * mapview.tile_height;
    y[2] = y[1] + OVERVIEW_TILE_HEIGHT * mapview.tile_height;

    /* South */
    x[3] = x[0] + OVERVIEW_TILE_WIDTH * mapview.tile_height;
    y[3] = y[0] + OVERVIEW_TILE_HEIGHT * mapview.tile_height;
  } else if (!tileset_is_isometric() && MAP_IS_ISOMETRIC) {
    /* We start with the west corner.  Note the X scale is smaller. */

    /* North */
    x[1] = x[0] + OVERVIEW_TILE_WIDTH * mapview.tile_width / 2;
    y[1] = y[0] + OVERVIEW_TILE_HEIGHT * mapview.tile_width;

    /* East */
    x[2] = x[1] - OVERVIEW_TILE_WIDTH * mapview.tile_height / 2;
    y[2] = y[1] + OVERVIEW_TILE_HEIGHT * mapview.tile_height;

    /* South */
    x[3] = x[2] - OVERVIEW_TILE_WIDTH * mapview.tile_width / 2;
    y[3] = y[2] - OVERVIEW_TILE_HEIGHT * mapview.tile_width;
  } else {
    /* We start with the northwest corner. */
    int screen_width = mapview.tile_width;
    int screen_height = mapview.tile_height * (tileset_is_isometric()
					       ? 2 : 1);

    /* Northeast */
    x[1] = x[0] + OVERVIEW_TILE_WIDTH * screen_width - 1;
    y[1] = y[0];

    /* Southeast */
    x[2] = x[1];
    y[2] = y[0] + OVERVIEW_TILE_HEIGHT * screen_height - 1;

    /* Southwest */
    x[3] = x[0];
    y[3] = y[2];
  }

  freelog(LOG_DEBUG, "(%d,%d)->(%d,%x)->(%d,%d)->(%d,%d)",
	  x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
}

/**************************************************************************
  Redraw the entire backing store for the overview minimap.
**************************************************************************/
void refresh_overview_canvas(void)
{
  whole_map_iterate(ptile) {
    overview_update_tile(ptile);
  } whole_map_iterate_end;
  redraw_overview();
}

/**************************************************************************
  Redraw the given map position in the overview canvas.
**************************************************************************/
void overview_update_tile(struct tile *ptile)
{
  /* Base overview positions are just like natural positions, but scaled to
   * the overview tile dimensions. */
  do_in_natural_pos(ntl_x, ntl_y, ptile->x, ptile->y) {
    int overview_y = ntl_y * OVERVIEW_TILE_SIZE;
    int overview_x = ntl_x * OVERVIEW_TILE_SIZE;

    if (MAP_IS_ISOMETRIC) {
      if (topo_has_flag(TF_WRAPX)) {
	if (overview_x > overview.width - OVERVIEW_TILE_WIDTH) {
	  /* This tile is shown half on the left and half on the right
	   * side of the overview.  So we have to draw it in two parts. */
	  canvas_put_rectangle(overview.store, 
			       overview_tile_color(ptile),
			       overview_x - overview.width, overview_y,
			       OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT); 
	}     
      } else {
	/* Clip half tile left and right.
	 * See comment in map_to_overview_pos. */
	overview_x -= OVERVIEW_TILE_SIZE;
      }
    } 
    
    canvas_put_rectangle(overview.store,
			 overview_tile_color(ptile),
			 overview_x, overview_y,
			 OVERVIEW_TILE_WIDTH, OVERVIEW_TILE_HEIGHT);

    dirty_overview();
  } do_in_natural_pos_end;
}

/**************************************************************************
  Returns TRUE if cached drawing is possible.  If the mapview is too large
  we have to turn it off.
**************************************************************************/
static bool can_do_cached_drawing(void)
{
  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;
  int w = mapview.store_width, h = mapview.store_height;

  /* If the mapview window is too large, cached drawing is not possible.
   *
   * BACKGROUND: cached drawing occurrs when the mapview is scrolled just
   * a short distance.  The majority of the mapview window can simply be
   * copied while the newly visible areas must be drawn from scratch.  This
   * speeds up drawing significantly, especially when using the scrollbars
   * or mapview sliding.
   *
   * When the mapview is larger than the map, however, some tiles may become
   * visible twice.  In this case one instance of the tile will be drawn
   * while all others are drawn black.  When this happens the cached drawing
   * system breaks since it assumes the mapview canvas is an "ideal" window
   * over the map.  So black tiles may be scrolled from the edges of the
   * mapview into the center, while drawn tiles may be scrolled from the
   * center of the mapview out to the edges.  The result is very bad.
   *
   * There are a few different ways this could be solved.  One way is simply
   * to turn off cached drawing, which is what we do now.  If the mapview
   * window gets to be too large, the caching is disabled.  Another would
   * be to prevent the window from getting too large in the first place -
   * but because the window boundaries aren't at an even tile this would
   * mean the entire map could never be shown.  Yet another way would be
   * to draw tiles more than once if they are visible in multiple locations
   * on the mapview.
   *
   * The logic below is complicated and determined in part by
   * trial-and-error. */
  if (!topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
    /* An unwrapping map: no limitation.  On an unwrapping map no tile can
     * be visible twice so there's no problem. */
    return TRUE;
  }
  if (XOR(topo_has_flag(TF_ISO) || topo_has_flag(TF_HEX),
	  tileset_is_isometric())) {
    /* Non-matching.  In this case the mapview does not line up with the
     * map's axis of wrapping.  This will give very bad results for the
     * player!
     * We can never show more than half of the map.
     *
     * We divide by 4 below because we have to divide by 2 twice.  The
     * first division by 2 is because the square must be half the size
     * of the (width+height).  The second division by two is because for
     * an iso-map, NATURAL_XXX has a scale of 2, whereas for iso-view
     * NORMAL_TILE_XXX has a scale of 2. */
    return (w <= (NATURAL_WIDTH + NATURAL_HEIGHT) * W / 4
	    && h <= (NATURAL_WIDTH + NATURAL_HEIGHT) * H / 4);
  } else {
    /* Matching. */
    const int isofactor = (tileset_is_isometric() ? 2 : 1);
    const int isodiff = (tileset_is_isometric() ? 6 : 2);

    /* Now we can use the full width and height, with the exception of a small
     * area on each side. */
    if (topo_has_flag(TF_WRAPX)
	&& w > (NATURAL_WIDTH - isodiff) * W / isofactor) {
      return FALSE;
    }
    if (topo_has_flag(TF_WRAPY)
	&& h > (NATURAL_HEIGHT - isodiff) * H / isofactor) {
      return FALSE;
    }
    return TRUE;
  }
}

/**************************************************************************
  Called if the map size is know or changes.
**************************************************************************/
void set_overview_dimensions(int width, int height)
{
  int shift = 0; /* used to calculate shift in iso view */

  /* Set the scale of the overview map.  This attempts to limit the overview
   * to 120 pixels wide or high. */
  if (MAP_IS_ISOMETRIC) {
    OVERVIEW_TILE_SIZE = MIN(MAX(120 / width, 1), 120 / height + 1);

    /* Clip half tile left and right.  See comment in map_to_overview_pos. */
    shift = (!topo_has_flag(TF_WRAPX) ? -OVERVIEW_TILE_SIZE : 0);
  } else {
    OVERVIEW_TILE_SIZE = MIN(120 / width + 1, 120 / height + 1);
  }

  overview.height = OVERVIEW_TILE_HEIGHT * height;
  overview.width = OVERVIEW_TILE_WIDTH * width + shift; 

  if (overview.store) {
    canvas_free(overview.store);
  }
  overview.store = canvas_create(overview.width, overview.height);
  canvas_put_rectangle(overview.store, COLOR_STD_BLACK,
		       0, 0, overview.width, overview.height);
  update_map_canvas_scrollbars_size();

  mapview.can_do_cached_drawing = can_do_cached_drawing();

  /* Call gui specific function. */
  map_size_changed();
}

/**************************************************************************
  Called when we receive map dimensions.  It initialized the mapview
  decorations.
**************************************************************************/
void init_mapview_decorations(void)
{
  map_deco = fc_realloc(map_deco, MAP_INDEX_SIZE * sizeof(*map_deco));
  whole_map_iterate(ptile) {
    map_deco[ptile->index].hilite = HILITE_NONE;
    map_deco[ptile->index].crosshair = 0;
  } whole_map_iterate_end;
}

/**************************************************************************
  Called if the map in the GUI is resized.

  Returns TRUE iff the canvas was redrawn.
**************************************************************************/
bool map_canvas_resized(int width, int height)
{
  int old_tile_width = mapview.tile_width;
  int old_tile_height = mapview.tile_height;
  int old_width = mapview.width, old_height = mapview.height;
  int tile_width = (width + NORMAL_TILE_WIDTH - 1) / NORMAL_TILE_WIDTH;
  int tile_height = (height + NORMAL_TILE_HEIGHT - 1) / NORMAL_TILE_HEIGHT;
  int full_width = tile_width * NORMAL_TILE_WIDTH;
  int full_height = tile_height * NORMAL_TILE_HEIGHT;
  bool tile_size_changed, size_changed, redrawn = FALSE;

  /* Resized */

  /* Since a resize is only triggered when the tile_*** changes, the canvas
   * width and height must include the entire backing store - otherwise
   * small resizings may lead to undrawn tiles. */
  mapview.tile_width = tile_width;
  mapview.tile_height = tile_height;
  mapview.width = width;
  mapview.height = height;
  mapview.store_width = full_width;
  mapview.store_height = full_height;

  /* Check for what's changed. */
  tile_size_changed = (tile_width != old_tile_width
 		       || tile_height != old_tile_height);
  size_changed = (width != old_width || height != old_height);

  /* If the tile size has changed, resize the canvas. */
  if (tile_size_changed) {
    if (mapview.store) {
      canvas_free(mapview.store);
      canvas_free(mapview.tmp_store);
    }
    mapview.store = canvas_create(full_width, full_height);
    canvas_put_rectangle(mapview.store, COLOR_STD_BLACK, 0, 0,
			 full_width, full_height);

    mapview.tmp_store = canvas_create(full_width, full_height);
  }

  if (map_exists() && can_client_change_view()) {
    if (tile_size_changed) {
      update_map_canvas_visible();
      center_tile_overviewcanvas(get_center_tile_mapcanvas());
      unqueue_mapview_updates(TRUE);
      redrawn = TRUE;
    }

    /* If the width/height has changed, update the scrollbars even if
     * the backing store is not resized. */
    if (size_changed) {
      update_map_canvas_scrollbars_size();
      update_map_canvas_scrollbars();
    }
  }

  mapview.can_do_cached_drawing = can_do_cached_drawing();

  return redrawn;
}

/**************************************************************************
  Sets up data for the mapview and overview.
**************************************************************************/
void init_mapcanvas_and_overview(void)
{
  /* Create a dummy map to make sure mapview.store is never NULL. */
  map_canvas_resized(1, 1);
}

/****************************************************************************
  Return the desired width of the spaceship canvas.
****************************************************************************/
void get_spaceship_dimensions(int *width, int *height)
{
  get_sprite_dimensions(sprites.spaceship.habitation, width, height);
  *width *= 7;
  *height *= 7;
}

/****************************************************************************
  Draw the spaceship onto the canvas.
****************************************************************************/
void put_spaceship(struct canvas *pcanvas, int canvas_x, int canvas_y,
		   const struct player *pplayer)
{
  int i, x, y;  
  const struct player_spaceship *ship = &pplayer->spaceship;
  int w, h;

  get_sprite_dimensions(sprites.spaceship.habitation, &w, &h);

  canvas_put_rectangle(pcanvas, COLOR_STD_BLACK, 0, 0, w * 7, h * 7);

  for (i = 0; i < NUM_SS_MODULES; i++) {
    const int j = i / 3;
    const int k = i % 3;
    struct Sprite *sprite;

    if ((k == 0 && j >= ship->habitation)
	|| (k == 1 && j >= ship->life_support)
	|| (k == 2 && j >= ship->solar_panels)) {
      continue;
    }
    x = modules_info[i].x * w / 4 - w / 2;
    y = modules_info[i].y * h / 4 - h / 2;

    sprite = (k == 0 ? sprites.spaceship.habitation
	      : k == 1 ? sprites.spaceship.life_support
	      : sprites.spaceship.solar_panels);
    canvas_put_sprite_full(pcanvas, x, y, sprite);
  }

  for (i=0; i < NUM_SS_COMPONENTS; i++) {
    const int j = i / 2;
    const int k = i % 2;
    struct Sprite *sprite;

    if ((k == 0 && j >= ship->fuel)
	|| (k == 1 && j >= ship->propulsion)) {
      continue;
    }
    x = components_info[i].x * w / 4 - w / 2;
    y = components_info[i].y * h / 4 - h / 2;

    sprite = ((k == 0) ? sprites.spaceship.fuel
	      : sprites.spaceship.propulsion);

    canvas_put_sprite_full(pcanvas, x, y, sprite);
  }

  for (i = 0; i < NUM_SS_STRUCTURALS; i++) {
    if (!ship->structure[i]) {
      continue;
    }
    x = structurals_info[i].x * w / 4 - w / 2;
    y = structurals_info[i].y * h / 4 - h / 2;

    canvas_put_sprite_full(pcanvas, x, y, sprites.spaceship.structural);
  }
}
