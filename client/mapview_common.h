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

#include "fc_types.h"
#include "map.h"

#include "colors_g.h"

#include "tilespec.h"

struct canvas_store;		/* opaque type, real type is gui-dep */

struct mapview_canvas {
  int gui_x0, gui_y0;
  int width, height;		/* Size in pixels. */
  int tile_width, tile_height;	/* Size in tiles. Rounded up. */
  int store_width, store_height;
  struct canvas *store, *tmp_store;
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

#define BORDER_WIDTH 2
#define GOTO_WIDTH 2

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
#define gui_rect_iterate(gui_x0, gui_y0, width, height, map_x, map_y)	    \
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
    int count;								    \
									    \
    if (is_isometric) {							    \
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
	map_x = (GRI_x_itr + GRI_y_itr) / 2;				    \
	map_y = (GRI_y_itr - GRI_x_itr) / 2;				    \
      } else {								    \
	map_x = GRI_x_itr;						    \
	map_y = GRI_y_itr;						    \
      }

#define gui_rect_iterate_end						    \
    }									    \
  }									    \
}

void refresh_tile_mapcanvas(int x, int y, bool write_to_screen);
enum color_std get_grid_color(int x1, int y1, int x2, int y2);

void map_to_gui_vector(int *gui_dx, int *gui_dy, int map_dx, int map_dy);
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

void put_unit(struct unit *punit,
	      struct canvas *pcanvas, int canvas_x, int canvas_y);
void put_city(struct city *pcity,
	      struct canvas *pcanvas, int canvas_x, int canvas_y);
void put_terrain(int map_x, int map_y,
		 struct canvas *pcanvas, int canvas_x, int canvas_y);

void put_city_tile_output(struct city *pcity, int city_x, int city_y,
			  struct canvas *pcanvas,
			  int canvas_x, int canvas_y);
void put_unit_city_overlays(struct unit *punit,
			    struct canvas *pcanvas,
			    int canvas_x, int canvas_y);
void toggle_city_color(struct city *pcity);
void toggle_unit_color(struct unit *punit);
void put_red_frame_tile(struct canvas *pcanvas,
			int canvas_x, int canvas_y);

void put_nuke_mushroom_pixmaps(int map_x, int map_y);

void put_one_tile(struct canvas *pcanvas, int map_x, int map_y,
		  int canvas_x, int canvas_y, bool citymode);
void put_one_tile_iso(struct canvas *pcanvas, int map_x, int map_y,
		      int canvas_x, int canvas_y, bool citymode);
void tile_draw_grid(struct canvas *pcanvas, int map_x, int map_y,
		    int canvas_x, int canvas_y, bool citymode);

void update_map_canvas(int canvas_x, int canvas_y, int width, int height);
void update_map_canvas_visible(void);
void update_city_description(struct city *pcity);

void show_city_descriptions(int canvas_x, int canvas_y,
			    int width, int height);
bool show_unit_orders(struct unit *punit);

void draw_segment(int src_x, int src_y, enum direction8 dir);
void undraw_segment(int src_x, int src_y, int dir);

void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
void move_unit_map_canvas(struct unit *punit,
			  int map_x, int map_y, int dx, int dy);

struct city *find_city_or_settler_near_tile(int x, int y,
					    struct unit **punit);
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
