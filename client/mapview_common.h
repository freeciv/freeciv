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

#ifndef FC__MAPVIEW_COMMON_H
#define FC__MAPVIEW_COMMON_H

#include "shared.h"		/* bool type */

#include "colors_g.h"

struct unit;

struct canvas_store;		/* opaque type, real type is gui-dep */

struct canvas {
  int map_x0, map_y0;
  int width, height;		/* Size in pixels. */
  int tile_width, tile_height;	/* Size in tiles. Rounded up. */
  struct canvas_store *store;
};

/* Holds all information about the overview aka minimap. */
struct overview {
  /* The following fields are controlled by mapview_common.c. */
  int map_x0, map_y0;
  int width, height;		/* Size in pixels. */
  struct canvas_store *store;

  /* The following fields are controlled by gui-.../mapview.c. */
  struct canvas_store *window;
};

extern struct canvas mapview_canvas;
extern struct overview overview;

/*
The bottom row of the map was sometimes hidden.

As of now the top left corner is always aligned with the tiles. This
is what causes the problem in the first place. The ideal solution
would be to align the window with the bottom left tiles if you tried
to center the window on a tile closer than (screen_tiles_height/2-1)
to the south pole.

But, for now, I just grepped for occurences where the ysize (or the
values derived from it) were used, and those places that had relevance
to drawing the map, and I added 1 (using the EXTRA_BOTTOM_ROW
constant).

-Thue
*/

/* 
 * When drawing a tile (currently only in isometric mode), there are
 * six relevant parts we can draw.  Each of these is a rectangle of
 * size NORMAL_TILE_WIDTH/2 x NORMAL_TILE_HEIGHT/2.
 *
 *   Left    Right
 *
 * -----------------
 * |       |       |
 * | D_T_L | D_T_R |  Top, above the actual tile
 * |       |       |
 * -----------------
 * |       |       |
 * | D_M_L | D_M_R |  Middle, upper half of the actual tile
 * |       |       |
 * -----------------
 * |       |       |
 * | D_B_L | D_B_R |  Bottom, lower half of the actual tile
 * |       |       |
 * -----------------
 *
 * The figure above shows the six drawing areas.  The tile itself
 * occupies the bottom four rectangles (if it is isometric it will
 * actually fill only half of each rectangle).  But the sprites for
 * the tile may extend up above it an additional NORMAL_TILE_HEIGHT/2
 * pixels.  To get the Painter's Algorithm (objects are then painted
 * from back-to-front) to work, sometimes we only draw some of these
 * rectangles.  For instance, in isometric view after drawing D_B_L
 * for one tile we have to draw D_M_R for the tile just down-left from
 * it, then D_T_L for the tile below it.
 *
 * This concept currently only applies to the isometric drawing code,
 * and currently the D_T_L and D_T_R rectangles are not used.  But
 * either of these could change in the future.
 *
 * These values are used as a mask; see enum draw_type.
 */
enum draw_part {
  D_T_L = 1,
  D_T_R = 2,
  D_M_L = 4,
  D_M_R = 8,
  D_B_L = 16,
  D_B_R = 32
};

/* 
 * As explained above, when drawing a tile we will sometimes only draw
 * parts of the tile.  This is an enumeration of which sets of
 * rectangles can be used together in isometric view.  If
 * non-isometric view were to use a similar system it would have a
 * smaller set of rectangles.
 *
 * Format (regexp): D_[TMB]+_[LR]+.
 *
 * Note that each of these sets of rectangles must itelf make up a
 * larger rectangle.  However, not all 18 possible sub-rectangles are
 * needed.
 */
enum draw_type {
  D_FULL = D_T_L | D_T_R | D_M_L | D_M_R | D_B_L | D_B_R,
  D_B_LR = D_B_L | D_B_R,
  D_MB_L = D_M_L | D_B_L,
  D_MB_R = D_M_R | D_B_R,
  D_TM_L = D_T_L | D_M_L,
  D_TM_R = D_T_R | D_M_R,
  D_T_LR = D_T_L | D_T_R,
  D_TMB_L = D_T_L | D_M_L | D_B_L,
  D_TMB_R = D_T_R | D_M_R | D_B_R,
  D_M_LR = D_M_L | D_M_R,
  D_MB_LR = D_M_L | D_M_R | D_B_L | D_B_R
};

enum update_type {
  /* Masks */
  UPDATE_NONE = 0,
  UPDATE_CITY_DESCRIPTIONS = 1,
  UPDATE_MAP_CANVAS_VISIBLE = 2
};

void refresh_tile_mapcanvas(int x, int y, bool write_to_screen);
enum color_std get_grid_color(int x1, int y1, int x2, int y2);

bool map_to_canvas_pos(int *canvas_x, int *canvas_y, int map_x, int map_y);
bool canvas_to_map_pos(int *map_x, int *map_y, int canvas_x, int canvas_y);

void get_mapview_scroll_window(int *xmin, int *ymin,
			       int *xmax, int *ymax,
			       int *xsize, int *ysize);
void get_mapview_scroll_pos(int *scroll_x, int *scroll_y);
void set_mapview_scroll_pos(int scroll_x, int scroll_y);

void get_center_tile_mapcanvas(int *map_x, int *map_y);
void center_tile_mapcanvas(int map_x, int map_y);

bool tile_visible_mapcanvas(int map_x, int map_y);
bool tile_visible_and_not_on_border_mapcanvas(int map_x, int map_y);

void put_unit(struct unit *punit, struct canvas_store *pcanvas_store,
	      int canvas_x, int canvas_y,
	      int unit_offset_x, int unit_offset_y,
	      int unit_width, int unit_height);
void put_unit_full(struct unit *punit, struct canvas_store *pcanvas_store,
		   int canvas_x, int canvas_y);

void put_one_tile(struct canvas_store *pcanvas_store, int map_x, int map_y,
		  int canvas_x, int canvas_y, bool citymode);
void tile_draw_borders_iso(struct canvas_store *pcanvas_store,
			   int map_x, int map_y,
			   int canvas_x, int canvas_y,
			   enum draw_type draw);

void update_map_canvas(int x, int y, int width, int height,
		       bool write_to_screen);
void update_map_canvas_visible(void);

void show_city_descriptions(void);
bool show_unit_orders(struct unit *punit);

void undraw_segment(int src_x, int src_y, int dir);

void move_unit_map_canvas(struct unit *punit,
			  int map_x, int map_y, int dx, int dy);
				
struct city *find_city_near_tile(int x, int y);

void get_city_mapview_production(struct city *pcity,
                                 char *buf, size_t buf_len);
void get_city_mapview_name_and_growth(struct city *pcity,
				      char *name_buffer,
				      size_t name_buffer_len,
				      char *growth_buffer,
				      size_t growth_buffer_len,
				      enum color_std *grwoth_color);

void queue_mapview_update(enum update_type update);
void unqueue_mapview_updates(void);

void map_to_overview_pos(int *overview_x, int *overview_y,
			 int map_x, int map_y);
void overview_to_map_pos(int *map_x, int *map_y,
			 int overview_x, int overview_y);

void refresh_overview_canvas(void);
void overview_update_tile(int x, int y);
void set_overview_dimensions(int width, int height);

#endif /* FC__MAPVIEW_COMMON_H */
