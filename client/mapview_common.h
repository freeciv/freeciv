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

#include "map.h"
#include "shared.h"		/* bool type */

#include "colors_g.h"

struct unit;

struct canvas_store;		/* opaque type, real type is gui-dep */

struct mapview_canvas {
  int gui_x0, gui_y0;
  int width, height;		/* Size in pixels. */
  int tile_width, tile_height;	/* Size in tiles. Rounded up. */
  int store_width, store_height;
  struct canvas *store, *tmp_store, *single_tile;
};

/* Holds all information about the overview aka minimap. */
struct overview {
  /* The following fields are controlled by mapview_common.c. */
  int map_x0, map_y0;
  int width, height;		/* Size in pixels. */
  struct canvas *store;
};

extern struct mapview_canvas mapview_canvas;
extern struct overview overview;

/* 
 * When drawing a tile (currently only in isometric mode), there are
 * 5 relevant bits: top, middle, bottom, left, and right.  A drawing area
 * is comprised of an OR-ing of these.  Note that if you must have a vertical
 * (D_T, D_M, D_B) and horizontal (D_L, D_R) element to get any actual
 * area.  There are therefore six parts to the tile:
 *
 *
 * Left:D_L Right:D_R
 *
 * -----------------
 * |       |       |
 * | D_T_L | D_T_R |  Top, above the actual tile : D_T
 * |       |       |
 * -----------------
 * |       |       |
 * | D_M_L | D_M_R |  Middle, upper half of the actual tile : D_M
 * |       |       |
 * -----------------
 * |       |       |
 * | D_B_L | D_B_R |  Bottom, lower half of the actual tile : D_B
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
 * This concept only applies to the isometric drawing code.  Non-isometric
 * code may pass around a draw_type variable but it's generally ignored.
 *
 * These values are used as a mask; see enum draw_type.
 */
enum draw_part {
  D_T = 1, /* Draw top. */
  D_M = 2, /* Draw middle. */
  D_B = 4, /* Draw bottom. */
  D_L = 8, /* Draw left. */
  D_R = 16 /* Draw right. */
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
 * larger rectangle.  There are 18 such rectangles (C(3, 1) * C(4, 2)).
 * However not all of these rectangles are necessarily used.  The other
 * 14 possible bit combinations are not rectangles - 11 of them have no
 * area (D___, D__[LR]+, and D_[TMB]+_), and 3 of them are divided
 * (D_TB_[LR]+).
 */
enum draw_type {
  D_T_L = D_T | D_L,
  D_T_R = D_T | D_R,
  D_M_L = D_M | D_L,
  D_M_R = D_M | D_R,
  D_B_L = D_B | D_L,
  D_B_R = D_B | D_R,
  D_TM_L = D_T | D_M | D_L,
  D_TM_R = D_T | D_M | D_R,
  D_MB_L = D_M | D_B | D_L,
  D_MB_R = D_M | D_B | D_R,
  D_TMB_L = D_T | D_M | D_B | D_L,
  D_TMB_R = D_T | D_M | D_B | D_R,
  D_T_LR = D_T | D_L | D_R,
  D_M_LR = D_M | D_L | D_R,
  D_B_LR = D_B | D_L | D_R,
  D_TM_LR = D_T | D_M | D_L | D_R,
  D_MB_LR = D_M | D_B | D_L | D_R,
  D_TMB_LR = D_T | D_M | D_B | D_L | D_R
};
#define D_FULL D_TMB_LR

#define BORDER_WIDTH 2

enum update_type {
  /* Masks */
  UPDATE_NONE = 0,
  UPDATE_CITY_DESCRIPTIONS = 1,
  UPDATE_MAP_CANVAS_VISIBLE = 2
};

/*
 * Iterate over all map tiles that intersect with the given GUI rectangle.
 * The order of iteration is guaranteed to satisfy the painter's algorithm.
 *
 * gui_x0, gui_y0: gives the GUI origin of the rectangle.
 * width, height: gives the GUI width and height of the rectangle.  These
 * values may be negative.
 *
 * map_x, map_y: variables that will give the current tile of iteration.
 * These coordinates are unnormalized.
 *
 * draw: A variable that tells which parts of the tiles overlap with the
 * GUI rectangle.  Only applies in iso-view.
 *
 * The classic-view iteration is pretty simple.  Documentation of the
 * iso-view iteration is at
 * http://rt.freeciv.org/Ticket/Attachment/51374/37363/isogrid.png.
 */
#define gui_rect_iterate(gui_x0, gui_y0, width, height, map_x, map_y, draw) \
{									    \
  int _gui_x0 = (gui_x0), _gui_y0 = (gui_y0);				    \
  int _width = (width), _height = (height);				    \
									    \
  if (_width < 0) {							    \
    _gui_x0 += _width;							    \
    _width = -_width;							    \
  }									    \
  if (_height < 0) {							    \
    _gui_y0 += _height;							    \
    _height = -_height;							    \
  }									    \
  if (_width > 0 && _height > 0) {					    \
    int W = (is_isometric ? (NORMAL_TILE_WIDTH / 2) : NORMAL_TILE_WIDTH);   \
    int H = (is_isometric ? (NORMAL_TILE_HEIGHT / 2) : NORMAL_TILE_HEIGHT); \
    int GRI_x0 = DIVIDE(_gui_x0, W), GRI_y0 = DIVIDE(_gui_y0, H);	    \
    int GRI_x1 = DIVIDE(_gui_x0 + _width + W - 1, W);			    \
    int GRI_y1 = DIVIDE(_gui_y0 + _height + H - 1, H);			    \
    int GRI_itr, GRI_x_itr, GRI_y_itr, map_x, map_y;			    \
    enum draw_type draw;						    \
    int count;								    \
									    \
    if (is_isometric) {							    \
      /* Extra half-tile of UNIT_TILE_HEIGHT. */			    \
      GRI_y1++;								    \
      /* Tiles to the left/above overlap with us. */			    \
      GRI_x0--;								    \
      GRI_y0--;								    \
    }									    \
    count = (GRI_x1 - GRI_x0) * (GRI_y1 - GRI_y0);			    \
    for (GRI_itr = 0; GRI_itr < count; GRI_itr++) {			    \
      GRI_x_itr = GRI_x0 + (GRI_itr % (GRI_x1 - GRI_x0));		    \
      GRI_y_itr = GRI_y0 + (GRI_itr / (GRI_x1 - GRI_x0));		    \
      if (is_isometric) {						    \
	if ((GRI_x_itr + GRI_y_itr) % 2 != 0) {				    \
	  continue;							    \
	}								    \
	draw = 0;							    \
	if (GRI_x_itr > GRI_x0) {					    \
	  draw |= D_L;							    \
	}								    \
	if (GRI_x_itr < GRI_x1 - 1) {					    \
	  draw |= D_R;							    \
	}								    \
	if (GRI_y_itr > GRI_y0) {					    \
	  draw |= D_M;							    \
	}								    \
	if (GRI_y_itr > GRI_y0 + 1) {					    \
	  draw |= D_T;							    \
	}								    \
	if (GRI_y_itr < GRI_y1 - 2) {					    \
	  draw |= D_B;							    \
	}								    \
	assert((draw & (D_L | D_R)) && (draw & (D_T | D_M | D_B)));	    \
	assert((draw & D_M) || !((draw & D_T) && (draw & D_B)));	    \
	map_x = (GRI_x_itr + GRI_y_itr) / 2;				    \
	map_y = (GRI_y_itr - GRI_x_itr) / 2;				    \
      } else {								    \
	draw = D_FULL;							    \
	map_x = GRI_x_itr;						    \
	map_y = GRI_y_itr;						    \
      }

#define gui_rect_iterate_end						    \
    }									    \
  }									    \
}

void refresh_tile_mapcanvas(int x, int y, bool write_to_screen);
enum color_std get_grid_color(int x1, int y1, int x2, int y2);

bool map_to_canvas_pos(int *canvas_x, int *canvas_y, int map_x, int map_y);
bool canvas_to_map_pos(int *map_x, int *map_y, int canvas_x, int canvas_y);

void get_mapview_scroll_window(int *xmin, int *ymin,
			       int *xmax, int *ymax,
			       int *xsize, int *ysize);
void get_mapview_scroll_step(int *xstep, int *ystep);
void get_mapview_scroll_pos(int *scroll_x, int *scroll_y);
void set_mapview_scroll_pos(int scroll_x, int scroll_y);

void set_mapview_origin(int gui_x0, int gui_y0);
void get_center_tile_mapcanvas(int *map_x, int *map_y);
void center_tile_mapcanvas(int map_x, int map_y);

bool tile_visible_mapcanvas(int map_x, int map_y);
bool tile_visible_and_not_on_border_mapcanvas(int map_x, int map_y);

void put_unit(struct unit *punit, bool stacked, bool backdrop,
	      struct canvas *pcanvas,
	      int canvas_x, int canvas_y,
	      int unit_offset_x, int unit_offset_y,
	      int unit_width, int unit_height);
void put_unit_full(struct unit *punit, struct canvas *pcanvas,
		   int canvas_x, int canvas_y);

void put_city_tile_output(struct city *pcity, int city_x, int city_y,
			  struct canvas *pcanvas,
			  int canvas_x, int canvas_y);
void put_unit_city_overlays(struct unit *punit,
			    struct canvas *pcanvas,
			    int canvas_x, int canvas_y);
void put_red_frame_tile(struct canvas *pcanvas,
			int canvas_x, int canvas_y);

void put_nuke_mushroom_pixmaps(int map_x, int map_y);

void put_one_tile(struct canvas *pcanvas, int map_x, int map_y,
		  int canvas_x, int canvas_y, bool citymode);
void tile_draw_grid(struct canvas *pcanvas, int map_x, int map_y,
		    int canvas_x, int canvas_y,
		    enum draw_type draw, bool citymode);

void update_map_canvas(int canvas_x, int canvas_y, int width, int height);
void update_map_canvas_visible(void);

void show_city_descriptions(int canvas_x, int canvas_y,
			    int width, int height);
bool show_unit_orders(struct unit *punit);

void draw_segment(int src_x, int src_y, enum direction8 dir);
void undraw_segment(int src_x, int src_y, int dir);

void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
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

bool map_canvas_resized(int width, int height);
void init_mapcanvas_and_overview(void);

#endif /* FC__MAPVIEW_COMMON_H */
