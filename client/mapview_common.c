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

#include "log.h"
#include "map.h"
#include "support.h"
#include "timing.h"

#include "climap.h"
#include "control.h"
#include "goto.h"
#include "mapctrl_g.h"
#include "mapview_g.h"
#include "tilespec.h"

#include "mapview_common.h"

struct canvas mapview_canvas;

/* Coordinates of the upper left corner of the map overview. */
int map_overview_x0;

static void center_tile_overviewcanvas(int map_x, int map_y);

/**************************************************************************
 Refreshes a single tile on the map canvas.
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, bool write_to_screen)
{
  assert(is_real_map_pos(x, y));
  if (!normalize_map_pos(&x, &y)) {
    return;
  }

  if (tile_visible_mapcanvas(x, y)) {
    update_map_canvas(x, y, 1, 1, FALSE);

    if (update_city_text_in_refresh_tile
	&& (draw_city_names || draw_city_productions)) {
      /* FIXME: update_map_canvas() will overwrite the city descriptions.
       * This is a workaround that redraws the city descriptions (most of
       * the time).  Although it seems inefficient to redraw the
       * descriptions for so many tiles, remember that most of them don't
       * have cities on them.
       *
       * This workaround is unnecessary for clients that use a separate
       * buffer for the city descriptions, and will not work well for
       * anti-aliased text (since it uses partial transparency).  Thus some
       * clients may turn it off by setting
       * update_city_text_in_refresh_tile. */
      int canvas_x, canvas_y;
      struct city *pcity;

      if (is_isometric) {
	/* We assume the city description will be directly below the city,
	 * with a width of 1-2 tiles and a height of less than one tile.
	 * Remember that units are 50% taller than the normal tile height.
	 *      9
	 *     7 8
	 *    6 4 5
	 *     2 3
	 *      1
	 * Tile 1 is the one being updated; we redraw the city description
	 * for tiles 2-8 (actually we end up drawing 1 as well). */
	rectangle_iterate(x - 2, y - 2, 3, 3, city_x, city_y) {
	  if ((pcity = map_get_city(city_x, city_y))) {
	    map_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y);
	    show_city_desc(pcity, canvas_x, canvas_y);
	  }
	} rectangle_iterate_end;
      } else {
	/* We assume the city description will be held in the three tiles
	 * right below the city.
	 *       234
	 *        1
	 * Tile 1 is the one being updated; we redraw the city description
	 * for tiles 2, 3, and 4. */
	rectangle_iterate(x - 1, y - 1, 3, 1, city_x, city_y) {
	  if ((pcity = map_get_city(city_x, city_y))) {
	    map_to_canvas_pos(&canvas_x, &canvas_y, city_x, city_y);
	    show_city_desc(pcity, canvas_x, canvas_y);
	  }
	} rectangle_iterate_end;
      }
    }

    if (write_to_screen) {
      flush_dirty();
    }
  }
  overview_update_tile(x, y);
}

/**************************************************************************
Returns the color the grid should have between tile (x1,y1) and
(x2,y2).
**************************************************************************/
enum color_std get_grid_color(int x1, int y1, int x2, int y2)
{
  enum city_tile_type city_tile_type1, city_tile_type2;
  struct city *dummy_pcity;
  bool pos1_is_in_city_radius =
      player_in_city_radius(game.player_ptr, x1, y1);
  bool pos2_is_in_city_radius = FALSE;

  assert(is_real_map_pos(x1, y1));

  if (is_real_map_pos(x2, y2)) {
    normalize_map_pos(&x2, &y2);
    assert(is_tiles_adjacent(x1, y1, x2, y2));

    if (map_get_tile(x2, y2)->known == TILE_UNKNOWN) {
      return COLOR_STD_BLACK;
    }

    pos2_is_in_city_radius =
	player_in_city_radius(game.player_ptr, x2, y2);
    get_worker_on_map_position(x2, y2, &city_tile_type2, &dummy_pcity);
  } else {
    city_tile_type2 = C_TILE_UNAVAILABLE;
  }

  if (!pos1_is_in_city_radius && !pos2_is_in_city_radius) {
    return COLOR_STD_BLACK;
  }

  get_worker_on_map_position(x1, y1, &city_tile_type1, &dummy_pcity);

  if (city_tile_type1 == C_TILE_WORKER || city_tile_type2 == C_TILE_WORKER) {
    return COLOR_STD_RED;
  } else {
    return COLOR_STD_WHITE;
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
    map_to_canvas_pos(&canvas_x, &canvas_y, map_x, map_y);
    canvas_x += NORMAL_TILE_WIDTH / 2;
    canvas_y += NORMAL_TILE_HEIGHT / 2;
  }

  This pixel is one position closer to the lower right, which may be
  important to remember when doing some round-off operations. Other
  parts of the code assume NORMAL_TILE_WIDTH and NORMAL_TILE_HEIGHT
  to be even numbers.
**************************************************************************/
bool map_to_canvas_pos(int *canvas_x, int *canvas_y, int map_x, int map_y)
{
  if (is_isometric) {
    /* For a simpler example of this math, see
       city_pos_to_canvas_pos(). */
    int iso_x, iso_y;

    /*
     * First we wrap the coordinates to hopefully be within the the
     * GUI window.  This isn't perfect; notice that when the mapview
     * approaches the size of the map some tiles won't be shown at
     * all.
     */
    map_x %= map.xsize;
    if (map_x < mapview_canvas.map_x0) {
      map_x += map.xsize;
    }

    /*
     * Next we convert the flat GUI coordinates to isometric GUI
     * coordinates.  We'll make tile (x0, y0) be the origin, and
     * transform like this:
     * 
     *                     3
     * 123                2 6
     * 456 -> becomes -> 1 5 9
     * 789                4 8
     *                     7
     */
    iso_x = (map_x - map_y)
      - (mapview_canvas.map_x0 - mapview_canvas.map_y0);
    iso_y = (map_x + map_y)
      - (mapview_canvas.map_x0 + mapview_canvas.map_y0);

    /*
     * As the above picture shows, each isometric-coordinate unit
     * corresponds to a half-tile on the canvas.  Since the (x0, y0)
     * tile actually has its top corner (of the diamond-shaped tile)
     * located right at the corner of the canvas, to find the top-left
     * corner of the surrounding rectangle we must subtract off an
     * additional half-tile in the X direction.
     */
    *canvas_x = (iso_x - 1) * NORMAL_TILE_WIDTH / 2;
    *canvas_y = iso_y * NORMAL_TILE_HEIGHT / 2;

    /*
     * Finally we clip; checking to see if _any part_ of the tile is
     * visible on the canvas.
     */
    return (*canvas_x > -NORMAL_TILE_WIDTH)
	&& *canvas_x < (mapview_canvas.width + NORMAL_TILE_WIDTH / 2)
	&& (*canvas_y > -NORMAL_TILE_HEIGHT)
	&& *canvas_y < mapview_canvas.height;
  } else {			/* is_isometric */
    if (mapview_canvas.map_x0 + mapview_canvas.tile_width <= map.xsize) {
      *canvas_x = map_x - mapview_canvas.map_x0;
    } else if (map_x >= mapview_canvas.map_x0) {
      *canvas_x = map_x - mapview_canvas.map_x0;
    } else if (map_x < map_adjust_x(mapview_canvas.map_x0
				    + mapview_canvas.tile_width)) {
      *canvas_x = map_x + map.xsize - mapview_canvas.map_x0;
    } else {
      *canvas_x = map_x - mapview_canvas.map_x0;
    }

    *canvas_y = map_y - mapview_canvas.map_y0;

    *canvas_x *= NORMAL_TILE_WIDTH;
    *canvas_y *= NORMAL_TILE_HEIGHT;

    return *canvas_x >= 0 && *canvas_x < mapview_canvas.width
        && *canvas_y >= 0 && *canvas_y < mapview_canvas.height;
  }
}

/**************************************************************************
  Finds the map coordinates corresponding to pixel coordinates.  Returns
  TRUE if the position is real; in this case it will be normalized. Returns
  FALSE if the tile is unreal - caller may use nearest_real_pos() if
  required.
**************************************************************************/
bool canvas_to_map_pos(int *map_x, int *map_y, int canvas_x, int canvas_y)
{
  const int W = NORMAL_TILE_WIDTH, H = NORMAL_TILE_HEIGHT;

  if (is_isometric) {
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
     * http://rt.freeciv.org/Ticket/Attachment/16782/9982/grid1.png.
     *
     * The calculation is complicated somewhat because of two things: we
     * only use integer math, and C integer division rounds toward zero
     * instead of rounding down.
     *
     * For another example of this math, see canvas_pos_to_city_pos().
     */
    *map_x = DIVIDE(canvas_x * H + canvas_y * W, W * H);
    *map_y = DIVIDE(canvas_y * W - canvas_x * H, W * H);
  } else {			/* is_isometric */
    /* We use DIVIDE so that we will get the correct result even
     * for negative (off-canvas) coordinates. */
    *map_x = DIVIDE(canvas_x, W);
    *map_y = DIVIDE(canvas_y, H);
  }

  *map_x += mapview_canvas.map_x0;
  *map_y += mapview_canvas.map_y0;

  return normalize_map_pos(map_x, map_y);
}

/**************************************************************************
  Finds the current center tile of the mapcanvas.
**************************************************************************/
void get_center_tile_mapcanvas(int *map_x, int *map_y)
{
  /* This sets the pointers map_x and map_y */
  if (!canvas_to_map_pos(map_x, map_y,
          mapview_canvas.width / 2, mapview_canvas.height / 2)) {
    nearest_real_pos(map_x, map_y);
  }
}

/**************************************************************************
  Centers the mapview around (map_x, map_y).
**************************************************************************/
void center_tile_mapcanvas(int map_center_x, int map_center_y)
{
  int map_x = map_center_x, map_y = map_center_y;

  /* Find top-left corner. */
  if (is_isometric) {
    map_x -= mapview_canvas.tile_width / 2;
    map_y += mapview_canvas.tile_width / 2;
    map_x -= mapview_canvas.tile_height / 2;
    map_y -= mapview_canvas.tile_height / 2;
  } else {
    map_x -= mapview_canvas.tile_width / 2;
    map_y -= mapview_canvas.tile_height / 2;
  }

  /* Wrap. */
  map_x = map_adjust_x(map_x);

  /* Clip. */
  map_y = map_adjust_y(map_y);
  map_y = MIN(map_y,
	      map.ysize + EXTRA_BOTTOM_ROW - mapview_canvas.tile_height);

  /* Now that we've determined the new origin, update everything. */
  mapview_canvas.map_x0 = map_x;
  mapview_canvas.map_y0 = map_y;
  center_tile_overviewcanvas(map_center_x, map_center_y);
  update_map_canvas_visible();
  update_map_canvas_scrollbars();
  refresh_overview_viewrect();
  if (hover_state == HOVER_GOTO || hover_state == HOVER_PATROL) {
    create_line_at_mouse_pos();
  }
}

/**************************************************************************
  Return TRUE iff the given map position has a tile visible on the
  map canvas.
**************************************************************************/
bool tile_visible_mapcanvas(int map_x, int map_y)
{
  int dummy_x, dummy_y;		/* well, it needs two pointers... */

  return map_to_canvas_pos(&dummy_x, &dummy_y, map_x, map_y);
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
bool tile_visible_and_not_on_border_mapcanvas(int map_x, int map_y)
{
  if (is_isometric) {
    int canvas_x, canvas_y;

    /* The border consists of the half-tile on the left and top of the
     * screen, and the 1.5-tiles on the right and bottom. */
    return (map_to_canvas_pos(&canvas_x, &canvas_y, map_x, map_y)
	    && canvas_x > NORMAL_TILE_WIDTH / 2
	    && canvas_x < mapview_canvas.width - 3 * NORMAL_TILE_WIDTH / 2
	    && canvas_y >= NORMAL_TILE_HEIGHT
	    && canvas_y < mapview_canvas.height - 3 * NORMAL_TILE_HEIGHT / 2);
  } else {
    int x0 = mapview_canvas.map_x0, y0 = mapview_canvas.map_y0;
    int twidth = mapview_canvas.tile_width;
    int theight = mapview_canvas.tile_height;

    /* The border consists of the two tiles on the edge of the
     * mapview.  But we take into account the border of the map. */
    return ((map_y > y0 + 2 || (map_y >= y0 && y0 == 0))
	    && (map_y < y0 + theight - 2
		|| (map_y < y0 + theight
		    && (y0 + theight - EXTRA_BOTTOM_ROW == map.ysize)))
	    && ((map_x >= x0 + 2 && map_x < x0 + twidth - 2)
		|| (map_x + map.xsize >= x0 + 2
		    && (map_x + map.xsize < x0 + twidth - 2))));
  }
}


/**************************************************************************
  Draw the given map tile at the given canvas position in non-isometric
  view.
**************************************************************************/
void put_one_tile(struct canvas_store *pcanvas_store, int map_x, int map_y,
		  int canvas_x, int canvas_y, bool citymode)
{
  struct Sprite *tile_sprs[80];
  int fill_bg; /* FIXME: should be bool */
  struct player *pplayer;
  bool is_real = normalize_map_pos(&map_x, &map_y);

  if (is_real && tile_get_known(map_x, map_y)) {
    int count = fill_tile_sprite_array(tile_sprs, map_x, map_y, citymode,
				       &fill_bg, &pplayer);
    int i = 0;

    if (fill_bg) {
      enum color_std color = pplayer ? player_color(pplayer)
	      : COLOR_STD_BACKGROUND;
      gui_put_rectangle(pcanvas_store, color, canvas_x, canvas_y,
			 NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    }

    for (i = 0; i < count; i++) {
      if (tile_sprs[i]) {
	gui_put_sprite_full(pcanvas_store, canvas_x, canvas_y, tile_sprs[i]);
      }
    }

    if (draw_map_grid && !citymode) {
      /* left side... */
      gui_put_line(pcanvas_store,
		   get_grid_color(map_x, map_y, map_x - 1, map_y),
		   canvas_x, canvas_y, 0, NORMAL_TILE_HEIGHT);

      /* top side... */
      gui_put_line(pcanvas_store,
		   get_grid_color(map_x, map_y, map_x, map_y - 1),
		   canvas_x, canvas_y, NORMAL_TILE_WIDTH, 0);
    }

    if (draw_coastline && !draw_terrain) {
      enum tile_terrain_type t1 = map_get_terrain(map_x, map_y), t2;
      int x1, y1;

      /* left side */
      if (MAPSTEP(x1, y1, map_x, map_y, DIR8_WEST)) {
	t2 = map_get_terrain(x1, y1);
	if (is_ocean(t1) ^ is_ocean(t2)) {
	  gui_put_line(pcanvas_store, COLOR_STD_OCEAN,
		       canvas_x, canvas_y, 0, NORMAL_TILE_HEIGHT);
	}
      }

      /* top side */
      if (MAPSTEP(x1, y1, map_x, map_y, DIR8_NORTH)) {
	t2 = map_get_terrain(x1, y1);
	if (is_ocean(t1) ^ is_ocean(t2)) {
	  gui_put_line(pcanvas_store, COLOR_STD_OCEAN,
		       canvas_x, canvas_y, NORMAL_TILE_WIDTH, 0);
	}
      }
    }
  } else {
    /* tile is unknown */
    gui_put_rectangle(pcanvas_store, COLOR_STD_BLACK,
		      canvas_x, canvas_y,
		      NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
  }

  if (!citymode) {
    /* put any goto lines on the tile. */
    if (is_real) {
      enum direction8 dir;

      for (dir = 0; dir < 8; dir++) {
	if (get_drawn(map_x, map_y, dir)) {
	  draw_segment(map_x, map_y, dir);
	}
      }
    }

    /* Some goto lines overlap onto the tile... */
    if (NORMAL_TILE_WIDTH % 2 == 0 || NORMAL_TILE_HEIGHT % 2 == 0) {
      int line_x = map_x - 1, line_y = map_y;

      if (normalize_map_pos(&line_x, &line_y)
	  && get_drawn(line_x, line_y, DIR8_NORTHEAST)) {
	draw_segment(line_x, line_y, DIR8_NORTHEAST);
      }
    }
  }
}

/**************************************************************************
  Draw the unique tile for the given map position, in non-isometric view.
  The coordinates have not been normalized, and are not guaranteed to be
  real (we have to draw unreal tiles too).
**************************************************************************/
static void put_tile(int map_x, int map_y)
{
  int canvas_x, canvas_y;

  if (map_to_canvas_pos(&canvas_x, &canvas_y, map_x, map_y)) {
    freelog(LOG_DEBUG, "putting (%d,%d) at (%d,%d)",
	    map_x, map_y, canvas_x, canvas_y);
    put_one_tile(mapview_canvas.store, map_x, map_y,
		 canvas_x, canvas_y, FALSE);
  }
}

/**************************************************************************
  Draw the unique tile for the given map position, in isometric view.
  The coordinates have not been normalized, and are not guaranteed to be
  real (we have to draw unreal tiles too).
**************************************************************************/
static void put_tile_iso(int map_x, int map_y, enum draw_type draw)
{
  int canvas_x, canvas_y;

  if (map_to_canvas_pos(&canvas_x, &canvas_y, map_x, map_y)) {
    int height, width, height_unit;
    int offset_x, offset_y, offset_y_unit;

    freelog(LOG_DEBUG, "putting (%d,%d) at (%d,%d), draw %x",
	    map_x, map_y, canvas_x, canvas_y, draw);

    if ((draw & D_TMB_L) && (draw & D_TMB_R)) {
      width = NORMAL_TILE_WIDTH;
    } else {
      width = NORMAL_TILE_WIDTH / 2;
    }

    if (draw & D_TMB_L) {
      offset_x = 0;
    } else {
      offset_x = NORMAL_TILE_WIDTH / 2;
    }

    height = 0;
    if (draw & D_M_LR) {
      height += NORMAL_TILE_HEIGHT / 2;
    }
    if (draw & D_B_LR) {
      height += NORMAL_TILE_HEIGHT / 2;
    }

    height_unit = height;
    if (draw & D_T_LR) {
      height_unit += NORMAL_TILE_HEIGHT / 2;
    }

    offset_y = (draw & D_M_LR) ? 0 : NORMAL_TILE_HEIGHT / 2;

    if (draw & D_T_LR) {
      offset_y_unit = 0;
    } else if (draw & D_M_LR) {
      offset_y_unit = NORMAL_TILE_HEIGHT / 2;
    } else {
      offset_y_unit = NORMAL_TILE_HEIGHT;
    }

    if (normalize_map_pos(&map_x, &map_y)) {
      gui_map_put_tile_iso(map_x, map_y, canvas_x, canvas_y,
			   offset_x, offset_y, offset_y_unit,
			   width, height, height_unit,
			   draw);
    } else {
      gui_put_sprite(mapview_canvas.store, canvas_x, canvas_y,
		     sprites.black_tile, offset_x, offset_y, width, height);
    }
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
void update_map_canvas(int x, int y, int width, int height, 
		       bool write_to_screen)
{
  int canvas_start_x, canvas_start_y;

  freelog(LOG_DEBUG,
	  "update_map_canvas(pos=(%d,%d), size=(%d,%d), write_to_screen=%d)",
	  x, y, width, height, write_to_screen);

  if (is_isometric) {
    int x_itr, y_itr, i;

    /* First refresh the tiles above the area to remove the old tiles'
     * overlapping graphics. */
    put_tile_iso(x - 1, y - 1, D_B_LR); /* top_left corner */

    for (i = 0; i < height - 1; i++) { /* left side - last tile. */
      put_tile_iso(x - 1, y + i, D_MB_LR);
    }
    put_tile_iso(x - 1, y + height - 1, D_TMB_R); /* last tile left side. */

    for (i = 0; i < width - 1; i++) {
      /* top side */
      put_tile_iso(x + i, y - 1, D_MB_LR);
    }
    if (width > 1) {
      /* last tile top side. */
      put_tile_iso(x + width - 1, y - 1, D_TMB_L);
    } else {
      put_tile_iso(x + width - 1, y - 1, D_MB_L);
    }

    /* Now draw the tiles to be refreshed, from the top down to get the
     * overlapping areas correct. */
    for (x_itr = x; x_itr < x + width; x_itr++) {
      for (y_itr = y; y_itr < y + height; y_itr++) {
	put_tile_iso(x_itr, y_itr, D_FULL);
      }
    }

    /* Then draw the tiles underneath to refresh the parts of them that
     * overlap onto the area just drawn. */
    put_tile_iso(x, y + height, D_TM_R);  /* bottom side */
    for (i = 1; i < width; i++) {
      int x1 = x + i;
      int y1 = y + height;
      put_tile_iso(x1, y1, D_TM_R);
      put_tile_iso(x1, y1, D_T_L);
    }

    put_tile_iso(x + width, y, D_TM_L); /* right side */
    for (i=1; i < height; i++) {
      int x1 = x + width;
      int y1 = y + i;
      put_tile_iso(x1, y1, D_TM_L);
      put_tile_iso(x1, y1, D_T_R);
    }

    put_tile_iso(x + width, y + height, D_T_LR); /* right-bottom corner */


    /* Draw the goto lines on top of the whole thing. This is done last as
     * we want it completely on top. */
    for (x_itr = x - 1; x_itr <= x + width; x_itr++) {
      for (y_itr = y - 1; y_itr <= y + height; y_itr++) {
	int x1 = x_itr;
	int y1 = y_itr;
	if (normalize_map_pos(&x1, &y1)) {
	  adjc_dir_iterate(x1, y1, x2, y2, dir) {
	    if (get_drawn(x1, y1, dir)) {
	      draw_segment(x1, y1, dir);
	    }
	  } adjc_dir_iterate_end;
	}
      }
    }


    /* Lastly draw our changes to the screen. */
    /* top left corner */
    map_to_canvas_pos(&canvas_start_x, &canvas_start_y, x, y);

    /* top left corner in isometric view */
    canvas_start_x -= height * NORMAL_TILE_WIDTH / 2;

    /* because of where get_canvas_xy() sets canvas_x */
    canvas_start_x += NORMAL_TILE_WIDTH / 2;

    /* And because units fill a little extra */
    canvas_start_y += NORMAL_TILE_HEIGHT - UNIT_TILE_HEIGHT;

    /* Here we draw a rectangle that includes the updated tiles.  This
     * method can fail if the area wraps off one side of the screen and
     * back to the other. */
    dirty_rect(canvas_start_x, canvas_start_y,
	       (height + width) * NORMAL_TILE_WIDTH / 2,
	       (height + width) * NORMAL_TILE_HEIGHT / 2
	       + NORMAL_TILE_HEIGHT / 2);
  } else {
    /* not isometric */
    int map_x, map_y;

    for (map_y = y; map_y < y + height; map_y++) {
      for (map_x = x; map_x < x + width; map_x++) {
	/*
	 * We don't normalize until later because we want to draw
	 * black tiles for unreal positions.
	 */
	put_tile(map_x, map_y);
      }
    }

    /* Here we draw a rectangle that includes the updated tiles.  This
     * method can fail if the area wraps off one side of the screen and
     * back to the other. */
    map_to_canvas_pos(&canvas_start_x, &canvas_start_y, x, y);
    dirty_rect(canvas_start_x, canvas_start_y,
	       width * NORMAL_TILE_WIDTH,
	       height * NORMAL_TILE_HEIGHT);
  }

  if (write_to_screen) {
    /* We never want a partial flush; that would leave the screen in an
     * inconsistent state.  If the caller tells us to write_to_screen we
     * simply flush everything immediately. */
    flush_dirty();
  }
}

/**************************************************************************
 Update (only) the visible part of the map
**************************************************************************/
void update_map_canvas_visible(void)
{
  dirty_all();

  if (is_isometric) {
    /* just find a big rectangle that includes the whole visible area. The
       invisible tiles will not be drawn. */
    int width, height;

    width = height = mapview_canvas.tile_width + mapview_canvas.tile_height;
    update_map_canvas(mapview_canvas.map_x0,
		      mapview_canvas.map_y0 - mapview_canvas.tile_width,
		      width, height, FALSE);
  } else {
    update_map_canvas(mapview_canvas.map_x0, mapview_canvas.map_y0,
		      mapview_canvas.tile_width, mapview_canvas.tile_height,
		      FALSE);
  }

  show_city_descriptions();
}

/**************************************************************************
  Show descriptions for all cities visible on the map canvas.
**************************************************************************/
void show_city_descriptions(void)
{
  int canvas_x, canvas_y;

  if (!draw_city_names && !draw_city_productions) {
    return;
  }

  prepare_show_city_descriptions();

  if (is_isometric) {
    int w, h;

    for (h = -1; h < mapview_canvas.tile_height * 2; h++) {
      int x_base = mapview_canvas.map_x0 + h / 2 + (h != -1 ? h % 2 : 0);
      int y_base = mapview_canvas.map_y0 + h / 2 + (h == -1 ? -1 : 0);

      for (w = 0; w <= mapview_canvas.tile_width; w++) {
	int x = x_base + w;
	int y = y_base - w;
	struct city *pcity;

	if (normalize_map_pos(&x, &y)
	    && (pcity = map_get_city(x, y))) {
	  map_to_canvas_pos(&canvas_x, &canvas_y, x, y);
	  show_city_desc(pcity, canvas_x, canvas_y);
	}
      }
    }
  } else {			/* is_isometric */
    int x1, y1;

    for (x1 = 0; x1 < mapview_canvas.tile_width; x1++) {
      for (y1 = 0; y1 < mapview_canvas.tile_height; y1++) {
	int x = mapview_canvas.map_x0 + x1;
	int y = mapview_canvas.map_y0 + y1;
	struct city *pcity;

	if (normalize_map_pos(&x, &y)
	    && (pcity = map_get_city(x, y))) {
	  map_to_canvas_pos(&canvas_x, &canvas_y, x, y);
	  show_city_desc(pcity, canvas_x, canvas_y);
	}
      }
    }
  }
}

/**************************************************************************
  Remove the line from src_x, src_y in the given direction, and redraw
  the change if necessary.
**************************************************************************/
void undraw_segment(int src_x, int src_y, int dir)
{
  int dest_x, dest_y;

  assert(get_drawn(src_x, src_y, dir) == 0);

  if (!MAPSTEP(dest_x, dest_y, src_x, src_y, dir)) {
    assert(0);
  }

  refresh_tile_mapcanvas(src_x, src_y, FALSE);
  refresh_tile_mapcanvas(dest_x, dest_y, FALSE);

  if (!is_isometric) {
    if (NORMAL_TILE_WIDTH % 2 == 0 || NORMAL_TILE_HEIGHT % 2 == 0) {
      if (dir == DIR8_NORTHEAST) {
	/* Since the tile doesn't have a middle we draw an extra pixel
	 * on the adjacent tile when drawing in this direction. */
	if (!MAPSTEP(dest_x, dest_y, src_x, src_y, DIR8_EAST)) {
	  assert(0);
	}
	refresh_tile_mapcanvas(dest_x, dest_y, FALSE);
      } else if (dir == DIR8_SOUTHWEST) {	/* the same */
	if (!MAPSTEP(dest_x, dest_y, src_x, src_y, DIR8_SOUTH)) {
	  assert(0);
	}
	refresh_tile_mapcanvas(dest_x, dest_y, FALSE);
      }
    }
  }
}

/**************************************************************************
  Animates punit's "smooth" move from (x0, y0) to (x0+dx, y0+dy).
  Note: Works only for adjacent-tile moves.
**************************************************************************/
void move_unit_map_canvas(struct unit *punit,
			  int map_x, int map_y, int dx, int dy)
{
  static struct timer *anim_timer = NULL; 
  int dest_x, dest_y;

  /* only works for adjacent-square moves */
  if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0)) {
    return;
  }

  if (punit == get_unit_in_focus() && hover_state != HOVER_NONE) {
    set_hover_state(NULL, HOVER_NONE);
    update_unit_info_label(punit);
  }

  dest_x = map_x + dx;
  dest_y = map_y + dy;
  if (!normalize_map_pos(&dest_x, &dest_y)) {
    assert(0);
  }

  flush_dirty();

  if (player_can_see_unit(game.player_ptr, punit) &&
      (tile_visible_mapcanvas(map_x, map_y) ||
       tile_visible_mapcanvas(dest_x, dest_y))) {
    int i, steps;
    int start_x, start_y;
    int this_x, this_y;
    int canvas_dx, canvas_dy;

    if (is_isometric) {
      if (dx == 0) {
	canvas_dx = -NORMAL_TILE_WIDTH / 2 * dy;
	canvas_dy = NORMAL_TILE_HEIGHT / 2 * dy;
      } else if (dy == 0) {
	canvas_dx = NORMAL_TILE_WIDTH / 2 * dx;
	canvas_dy = NORMAL_TILE_HEIGHT / 2 * dx;
      } else {
	if (dx > 0) {
	  if (dy > 0) {
	    canvas_dx = 0;
	    canvas_dy = NORMAL_TILE_HEIGHT;
	  } else { /* dy < 0 */
	    canvas_dx = NORMAL_TILE_WIDTH;
	    canvas_dy = 0;
	  }
	} else { /* dx < 0 */
	  if (dy > 0) {
	    canvas_dx = -NORMAL_TILE_WIDTH;
	    canvas_dy = 0;
	  } else { /* dy < 0 */
	    canvas_dx = 0;
	    canvas_dy = -NORMAL_TILE_HEIGHT;
	  }
	}
      }
    } else {
      canvas_dx = NORMAL_TILE_WIDTH * dx;
      canvas_dy = NORMAL_TILE_HEIGHT * dy;
    }

    /* Sanity check on the number of steps. */
    if (smooth_move_unit_steps < 2) {
      steps = 2;
    } else if (smooth_move_unit_steps > MAX(abs(canvas_dx),
					    abs(canvas_dy))) {
      steps = MAX(abs(canvas_dx), abs(canvas_dy));
    } else {
      steps = smooth_move_unit_steps;
    }

    map_to_canvas_pos(&start_x, &start_y, map_x, map_y);
    if (is_isometric) {
      start_y -= NORMAL_TILE_HEIGHT / 2;
    }

    this_x = start_x;
    this_y = start_y;

    for (i = 1; i <= steps; i++) {
      int new_x, new_y;

      anim_timer = renew_timer_start(anim_timer, TIMER_USER, TIMER_ACTIVE);

      new_x = start_x + (i * canvas_dx) / steps;
      new_y = start_y + (i * canvas_dy) / steps;

      draw_unit_animation_frame(punit, i == 1, i == steps,
				this_x, this_y, new_x, new_y);

      this_x = new_x;
      this_y = new_y;

      if (i < steps) {
	usleep_since_timer_start(anim_timer, 10000);
      }
    }
  }
}

/**************************************************************************
  Find the "best" city to associate with the selected tile.
    a.  A city working the tile is the best
    b.  If another player is working the tile, return NULL.
    c.  If no city is working the tile, choose a city that could work
        the tile.
    d.  If multiple cities could work it, choose the most recently
        "looked at".
    e.  If none of the cities were looked at last, choose "randomly".
    f.  If no cities can work it, return NULL.
**************************************************************************/
struct city *find_city_near_tile(int x, int y)
{
  struct city *pcity = map_get_tile(x, y)->worked, *pcity2;
  static struct city *last_pcity = NULL;

  if (pcity) {
    if (pcity->owner == game.player_idx) {
      /* rule a */
      last_pcity = pcity;
      return pcity;
    } else {
      /* rule b */
      return NULL;
    }
  }

  pcity2 = NULL;		/* rule f */
  city_map_checked_iterate(x, y, city_x, city_y, map_x, map_y) {
    pcity = map_get_city(map_x, map_y);
    if (pcity && pcity->owner == game.player_idx
	&& get_worker_city(pcity, CITY_MAP_SIZE - 1 - city_x,
			   CITY_MAP_SIZE - 1 - city_y) == C_TILE_EMPTY) {
      /* rule c */
      /*
       * Note, we must explicitly check if the tile is workable (with
       * get_worker_city(), above) since it is possible that another
       * city (perhaps an unseen enemy city) may be working it,
       * causing it to be marked as C_TILE_UNAVAILABLE.
       */
      if (pcity == last_pcity) {
	return pcity;		/* rule d */
      }
      pcity2 = pcity;
    }
  }
  city_map_checked_iterate_end;

  /* rule e */
  last_pcity = pcity2;
  return pcity2;
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
    if (pcity->currently_building == B_CAPITAL) {
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
}

/**************************************************************************
  See comment for queue_mapview_update().
**************************************************************************/
void unqueue_mapview_updates(void)
{
  freelog(LOG_DEBUG, "unqueue_mapview_update: needed_updates=%d",
	  needed_updates);

  if (needed_updates & UPDATE_MAP_CANVAS_VISIBLE) {
    update_map_canvas_visible();
  } else if (needed_updates & UPDATE_CITY_DESCRIPTIONS) {
    update_city_descriptions();
  }
  needed_updates = UPDATE_NONE;

  flush_dirty();
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
  Center the overview around the mapview.
**************************************************************************/
static void center_tile_overviewcanvas(int map_x, int map_y)
{
  int screen_width;

  if (is_isometric) {
    screen_width = mapview_canvas.tile_width + mapview_canvas.tile_height;
  } else {
    screen_width = mapview_canvas.tile_width;
  }

  /* Currently we just center the overview canvas around the tile. */
  map_overview_x0 = map_adjust_x(map_x + screen_width / 2 - map.xsize / 2);
}

/**************************************************************************
  Finds the overview (canvas) coordinates for a given map position.
**************************************************************************/
void map_to_overview_pos(int *overview_x, int *overview_y,
			 int map_x, int map_y)
{
  *overview_x = OVERVIEW_TILE_WIDTH * map_adjust_x(map_x - map_overview_x0);
  *overview_y = OVERVIEW_TILE_HEIGHT * map_y;
}

/**************************************************************************
  Finds the map coordinates for a given overview (canvas) position.
**************************************************************************/
void overview_to_map_pos(int *map_x, int *map_y,
			 int overview_x, int overview_y)
{
  *map_x = map_adjust_x(overview_x / OVERVIEW_TILE_WIDTH + map_overview_x0);
  *map_y = overview_y / OVERVIEW_TILE_HEIGHT;
}
