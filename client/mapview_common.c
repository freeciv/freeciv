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
#include "support.h"

#include "mapview_g.h"

#include "mapview_common.h"
#include "tilespec.h"

/**************************************************************************
 Refreshes a single tile on the map canvas.
**************************************************************************/
void refresh_tile_mapcanvas(int x, int y, bool write_to_screen)
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
  bool pos1_is_in_city_radius =
      player_in_city_radius(game.player_ptr, x1, y1);
  bool pos2_is_in_city_radius = FALSE;

  assert(is_real_tile(x1, y1));

  if (is_real_tile(x2, y2)) {
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
  Finds the pixel coordinates of a tile.  Beside setting the results
  in canvas_x,canvas_y it returns whether the tile is inside the
  visible map.
**************************************************************************/
bool map_pos_to_canvas_pos(int map_x, int map_y,
			  int *canvas_x, int *canvas_y,
			  int map_view_topleft_map_x,
			  int map_view_topleft_map_y,
			  int map_view_pixel_width,
			  int map_view_pixel_height)
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
    if (map_x < map_view_topleft_map_x) {
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
    iso_x =
	(map_x - map_y) - (map_view_topleft_map_x -
			   map_view_topleft_map_y);
    iso_y =
	(map_x + map_y) - (map_view_topleft_map_x +
			   map_view_topleft_map_y);

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
  } else {			/* is_isometric */
    *map_x = map_view_topleft_map_x + canvas_x / NORMAL_TILE_WIDTH;
    *map_y = map_view_topleft_map_y + canvas_y / NORMAL_TILE_HEIGHT;
  }

  /*
   * If we are outside the map find the nearest tile, with distance as
   * seen on the map.
   */
  nearest_real_pos(map_x, map_y);
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
