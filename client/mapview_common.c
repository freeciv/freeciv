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

#include "map.h"
#include "mapview_g.h"

#include "mapview_common.h"
#include "tilespec.h"

/**************************************************************************
 Refreshes a single tile on the map canvas.
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, int write_to_screen)
{
  assert(is_real_tile(x, y));
  if (!normalize_map_pos(&x, &y)) {
    return;
  }

  if (tile_visible_mapcanvas(x, y)) {
    update_map_canvas(x, y, 1, 1, write_to_screen);
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
  int pos1_is_in_city_radius =
      player_in_city_radius(game.player_ptr, x1, y1);
  int pos2_is_in_city_radius = 0;

  assert(is_real_tile(x1, y1));

  if (is_real_tile(x2, y2)) {
    normalize_map_pos(&x2, &y2);
    assert(is_tiles_adjacent(x1, y1, x2, y2));

    if (!map_get_tile(x2, y2)->known) {
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
  Finds the pixel coordinates of a tile.  Beside setting the results
  in canvas_x,canvas_y it returns whether the tile is inside the
  visible map.
**************************************************************************/
int map_pos_to_canvas_pos(int map_x, int map_y,
			  int *canvas_x, int *canvas_y,
			  int map_view_topleft_map_x,
			  int map_view_topleft_map_y,
			  int map_view_pixel_width,
			  int map_view_pixel_height)
{
  if (is_isometric) {
    /* 
     * canvas_x,canvas_y is the top corner of the actual tile, not the
     * pixmap.  This function also handles non-adjusted tile coords
     * (like -1, -2) as if they were adjusted.  You might want to
     * first take a look at the simpler city_get_canvas() for basic
     * understanding. 
     */
    int diff_xy;
    int diff_x, diff_y;

    map_x %= map.xsize;
    if (map_x < map_view_topleft_map_x) {
      map_x += map.xsize;
    }
    diff_xy =
	(map_x + map_y) - (map_view_topleft_map_x +
			   map_view_topleft_map_y);
    /* one diff_xy value defines a line parallel with the top of the
       isometric view. */
    *canvas_y =
	diff_xy / 2 * NORMAL_TILE_HEIGHT +
	(diff_xy % 2) * (NORMAL_TILE_HEIGHT / 2);

    /* changing both x and y with the same amount doesn't change the
       isometric x value. (draw a grid to see it!) */
    map_x -= diff_xy / 2;
    map_y -= diff_xy / 2;
    diff_x = map_x - map_view_topleft_map_x;
    diff_y = map_view_topleft_map_y - map_y;

    *canvas_x = (diff_x - 1) * NORMAL_TILE_WIDTH
	+ (diff_x == diff_y ? NORMAL_TILE_WIDTH : NORMAL_TILE_WIDTH / 2)
	/* tiles starting above the visible area */
	+ (diff_y > diff_x ? NORMAL_TILE_WIDTH : 0);

    /* We now have the corner of the sprite. For drawing we move
       it. */
    *canvas_x -= NORMAL_TILE_WIDTH / 2;

    return (*canvas_x > -NORMAL_TILE_WIDTH)
	&& *canvas_x < (map_view_pixel_width + NORMAL_TILE_WIDTH / 2)
	&& (*canvas_y > -NORMAL_TILE_HEIGHT)
	&& (*canvas_y < map_view_pixel_height);
  } else {			/* is_isometric */
    /* map_view_map_width is the width in tiles/map positions */
    int map_view_map_width =
	(map_view_pixel_width + NORMAL_TILE_WIDTH - 1) / NORMAL_TILE_WIDTH;

    if (map_view_topleft_map_x + map_view_map_width <= map.xsize) {
      *canvas_x = map_x - map_view_topleft_map_x;
    } else if (map_x >= map_view_topleft_map_x) {
      *canvas_x = map_x - map_view_topleft_map_x;
    }
      else if (map_x <
	       map_adjust_x(map_view_topleft_map_x +
			    map_view_map_width)) {*canvas_x =
	  map_x + map.xsize - map_view_topleft_map_x;
    } else {
      *canvas_x = -1;
    }

    *canvas_y = map_y - map_view_topleft_map_y;

    *canvas_x *= NORMAL_TILE_WIDTH;
    *canvas_y *= NORMAL_TILE_HEIGHT;

    return *canvas_x >= 0
	&& *canvas_x < map_view_pixel_width
	&& *canvas_y >= 0 && *canvas_y < map_view_pixel_height;
  }
}

/**************************************************************************
  Finds the map coordinates corresponding to pixel coordinates.
**************************************************************************/
void canvas_pos_to_map_pos(int canvas_x, int canvas_y,
			   int *map_x, int *map_y,
			   int map_view_topleft_map_x,
			   int map_view_topleft_map_y)
{
  if (is_isometric) {
    *map_x = map_view_topleft_map_x;
    *map_y = map_view_topleft_map_y;

    /* first find an equivalent position on the left side of the screen. */
    *map_x += canvas_x / NORMAL_TILE_WIDTH;
    *map_y -= canvas_x / NORMAL_TILE_WIDTH;
    canvas_x %= NORMAL_TILE_WIDTH;

    /* Then move op to the top corner. */
    *map_x += canvas_y / NORMAL_TILE_HEIGHT;
    *map_y += canvas_y / NORMAL_TILE_HEIGHT;
    canvas_y %= NORMAL_TILE_HEIGHT;

    /* We are inside a rectangle, with 2 half tiles starting in the
       corner, and two tiles further out. Draw a grid to see how this
       works :). */
    assert(NORMAL_TILE_WIDTH == 2 * NORMAL_TILE_HEIGHT);
    canvas_y *= 2;		/* now we have a square. */
    if (canvas_x > canvas_y) {
      *map_y -= 1;
    }
    if (canvas_x + canvas_y > NORMAL_TILE_WIDTH) {
      *map_x += 1;
    }

    /* If we are outside the map find the nearest tile, with distance
       as seen on the map. */
    nearest_real_pos(map_x, map_y);
  } else {			/* is_isometric */
    *map_x =
	map_adjust_x(map_view_topleft_map_x +
		     canvas_x / NORMAL_TILE_WIDTH);
    *map_y =
	map_adjust_y(map_view_topleft_map_y +
		     canvas_y / NORMAL_TILE_HEIGHT);
  }
}

/**************************************************************************
  Centers the mapview around (map_x, map_y).  (map_view_x0,
  map_view_y0) is the upper-left coordinates of the mapview; these
  should be (but aren't) stored globally - each GUI has separate
  storate structs for them.
**************************************************************************/
void base_center_tile_mapcanvas(int map_x, int map_y,
				int *map_view_topleft_map_x,
				int *map_view_topleft_map_y,
				int map_view_map_width,
				int map_view_map_height)
{
  if (is_isometric) {
    map_x -= map_view_map_width / 2;
    map_y += map_view_map_width / 2;
    map_x -= map_view_map_height / 2;
    map_y -= map_view_map_height / 2;

    *map_view_topleft_map_x = map_adjust_x(map_x);
    *map_view_topleft_map_y = map_adjust_y(map_y);

    *map_view_topleft_map_y =
	(*map_view_topleft_map_y >
	 map.ysize + EXTRA_BOTTOM_ROW - map_view_map_height) ? map.ysize +
	EXTRA_BOTTOM_ROW - map_view_map_height : *map_view_topleft_map_y;
  } else {
    int new_map_view_x0, new_map_view_y0;

    new_map_view_x0 = map_adjust_x(map_x - map_view_map_width / 2);
    new_map_view_y0 = map_adjust_y(map_y - map_view_map_height / 2);
    if (new_map_view_y0 >
	map.ysize + EXTRA_BOTTOM_ROW - map_view_map_height) {
      new_map_view_y0 =
	  map_adjust_y(map.ysize + EXTRA_BOTTOM_ROW - map_view_map_height);
    }

    *map_view_topleft_map_x = new_map_view_x0;
    *map_view_topleft_map_y = new_map_view_y0;
  }
}
