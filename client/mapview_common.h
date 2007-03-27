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

struct mapview_decoration {
  /* For client Area Selection */
  enum tile_hilite {
    HILITE_NONE, HILITE_CITY
  } hilite;

  int crosshair; /* A refcount */
};

struct view {
  int gui_x0, gui_y0;
  int width, height;		/* Size in pixels. */
  int tile_width, tile_height;	/* Size in tiles. Rounded up. */
  int store_width, store_height;
  bool can_do_cached_drawing; /* TRUE if cached drawing is possible. */
  struct canvas *store, *tmp_store;
};

extern struct mapview_decoration *map_deco;
extern struct view mapview;

/* HACK: Callers can set this to FALSE to disable sliding.  It should be
 * reenabled afterwards. */
extern bool can_slide;

#define BORDER_WIDTH 2
#define GOTO_WIDTH 2

/*
 * Iterate over all map tiles that intersect with the given GUI rectangle.
 * The order of iteration is guaranteed to satisfy the painter's algorithm.
 * The iteration covers not only tiles but tile edges and corners.
 *
 * gui_x0, gui_y0: gives the GUI origin of the rectangle.
 * width, height: gives the GUI width and height of the rectangle.  These
 * values may be negative.
 *
 * ptile, pedge, pcorner: gives the tile, edge, or corner that is being
 * iterated over.  These are declared inside the macro.  Usually only
 * one of them will be non-NULL at a time.  These values may be passed in
 * directly to fill_sprite_array.
 *
 * canvas_x, canvas_y: the canvas position of the current element.  Each
 * element is assumed to be tileset_tile_width(tileset) * tileset_tile_height(tileset) in
 * size.  If an element is larger the caller needs to use a larger rectangle
 * of iteration.
 *
 * The grid of iteration is rather complicated.  For a picture of it see
 * http://bugs.freeciv.org/Ticket/Attachment/89565/56824/newgrid.png
 * or the other text in PR#12085.
 */
#define gui_rect_iterate(GRI_gui_x0, GRI_gui_y0, width, height,		    \
			 ptile, pedge, pcorner, gui_x, gui_y)		    \
{									    \
  int _gui_x0 = (GRI_gui_x0), _gui_y0 = (GRI_gui_y0);			    \
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
    const int _ratio = (tileset_is_isometric(tileset) ? 2 : 1);		    \
    const int _r = _ratio * 2;						    \
    const int _Wr = tileset_tile_width(tileset);					    \
    const int _Hr = tileset_tile_height(tileset);					    \
    /* Don't divide by _r yet, to avoid integer rounding errors. */	    \
    const int GRI_x0 = DIVIDE(_gui_x0 * _r, _Wr) - _ratio / 2;		\
    const int GRI_y0 = DIVIDE(_gui_y0 * _r, _Hr) - _ratio / 2;		\
    const int GRI_x1 = DIVIDE((_gui_x0 + _width) * _r + _Wr - 1,	    \
			      _Wr) + _ratio;				    \
    const int GRI_y1 = DIVIDE((_gui_y0 + _height) * _r + _Hr - 1,	    \
			      _Hr) + _ratio;				    \
    const int _count = (GRI_x1 - GRI_x0) * (GRI_y1 - GRI_y0);		    \
    int GRI_itr, GRI_x_itr, GRI_y_itr, GRI_sum, GRI_diff;		    \
									    \
    freelog(LOG_DEBUG, "Iterating over %d-%d x %d-%d rectangle.",	    \
	    GRI_x1, GRI_x0, GRI_y1, GRI_y0);				    \
    for (GRI_itr = 0; GRI_itr < _count; GRI_itr++) {			    \
      struct tile *ptile = NULL;					    \
      struct tile_edge *pedge = NULL;					    \
      struct tile_corner *pcorner = NULL;				    \
      struct tile_edge GRI_edge;					    \
      struct tile_corner GRI_corner;					    \
      int gui_x, gui_y;							    \
									    \
      GRI_x_itr = GRI_x0 + (GRI_itr % (GRI_x1 - GRI_x0));		    \
      GRI_y_itr = GRI_y0 + (GRI_itr / (GRI_x1 - GRI_x0));		    \
      GRI_sum = GRI_x_itr + GRI_y_itr;					    \
      GRI_diff = GRI_y_itr - GRI_x_itr;					    \
      if (tileset_is_isometric(tileset)) {				    \
	if ((GRI_x_itr + GRI_y_itr) % 2 != 0) {				    \
	  continue;							    \
	}								    \
	if (GRI_x_itr % 2 == 0 && GRI_y_itr % 2 == 0) {			    \
	  if ((GRI_x_itr + GRI_y_itr) % 4 == 0) {			    \
	    /* Tile */							    \
	    ptile = map_pos_to_tile(GRI_sum / 4 - 1, GRI_diff / 4);	    \
	  } else {							    \
	    /* Corner */						    \
	    pcorner = &GRI_corner;					    \
	    pcorner->tile[0] = map_pos_to_tile((GRI_sum - 6) / 4,	    \
					       (GRI_diff - 2) / 4);	    \
	    pcorner->tile[1] = map_pos_to_tile((GRI_sum - 2) / 4,	    \
					       (GRI_diff - 2) / 4);	    \
	    pcorner->tile[2] = map_pos_to_tile((GRI_sum - 2) / 4,	    \
					       (GRI_diff + 2) / 4);	    \
	    pcorner->tile[3] = map_pos_to_tile((GRI_sum - 6) / 4,	    \
					       (GRI_diff + 2) / 4);	    \
	    if (tileset_hex_width(tileset) > 0) {			    \
	      pedge = &GRI_edge;					    \
	      pedge->type = EDGE_UD;					    \
	      pedge->tile[0] = pcorner->tile[0];			    \
	      pedge->tile[1] = pcorner->tile[2];			    \
	    } else if (tileset_hex_height(tileset) > 0) {		    \
	      pedge = &GRI_edge;					    \
	      pedge->type = EDGE_LR;					    \
	      pedge->tile[0] = pcorner->tile[1];			    \
	      pedge->tile[1] = pcorner->tile[3];			    \
	    }								    \
	  }								    \
	} else {							    \
	  /* Edge. */							    \
	  pedge = &GRI_edge; 						    \
	  if (GRI_sum % 4 == 0) {					    \
	    pedge->type = EDGE_NS;					    \
	    pedge->tile[0] = map_pos_to_tile((GRI_sum - 4) / 4, /* N */	    \
					     (GRI_diff - 2) / 4);	    \
	    pedge->tile[1] = map_pos_to_tile((GRI_sum - 4) / 4, /* S */	    \
					     (GRI_diff + 2) / 4);	    \
	  } else {							    \
	    pedge->type = EDGE_WE;					    \
	    pedge->tile[0] = map_pos_to_tile((GRI_sum - 6) / 4,		    \
					     GRI_diff / 4); /* W */	    \
	    pedge->tile[1] = map_pos_to_tile((GRI_sum - 2) / 4,		    \
					     GRI_diff / 4); /* E */	    \
	  }								    \
	}								    \
      } else {								    \
	if (GRI_sum % 2 == 0) {						    \
	  if (GRI_x_itr % 2 == 0) {					    \
	    /* Corner. */						    \
	    pcorner = &GRI_corner;					    \
	    pcorner->tile[0] = map_pos_to_tile(GRI_x_itr / 2 - 1,	    \
					       GRI_y_itr / 2 - 1); /* NW */ \
	    pcorner->tile[1] = map_pos_to_tile(GRI_x_itr / 2,		    \
					       GRI_y_itr / 2 - 1); /* NE */ \
	    pcorner->tile[2] = map_pos_to_tile(GRI_x_itr / 2,		    \
					       GRI_y_itr / 2); /* SE */	    \
	    pcorner->tile[3] = map_pos_to_tile(GRI_x_itr / 2 - 1,	    \
					       GRI_y_itr / 2); /* SW */	    \
	  } else {							    \
	    /* Tile. */							    \
	    ptile = map_pos_to_tile((GRI_x_itr - 1) / 2,		    \
				    (GRI_y_itr - 1) / 2);		    \
	  }								    \
	} else {							    \
	  /* Edge. */							    \
	  pedge = &GRI_edge;						    \
	  if (GRI_y_itr % 2 == 0) {					    \
	    pedge->type = EDGE_NS;					    \
	    pedge->tile[0] = map_pos_to_tile((GRI_x_itr - 1) / 2, /* N */   \
					     GRI_y_itr / 2 - 1);	    \
	    pedge->tile[1] = map_pos_to_tile((GRI_x_itr - 1) / 2, /* S */   \
					     GRI_y_itr / 2);		    \
	  } else {							    \
	    pedge->type = EDGE_WE;					    \
	    pedge->tile[0] = map_pos_to_tile(GRI_x_itr / 2 - 1,	/* W */	    \
					     (GRI_y_itr - 1) / 2);	    \
	    pedge->tile[1] = map_pos_to_tile(GRI_x_itr / 2, /* E */	    \
					     (GRI_y_itr - 1) / 2);	    \
	  }								    \
	}								    \
      }									    \
      gui_x = GRI_x_itr * _Wr / _r - tileset_tile_width(tileset) / 2;		    \
      gui_y = GRI_y_itr * _Hr / _r - tileset_tile_height(tileset) / 2;

#define gui_rect_iterate_end						    \
    }									    \
  }									    \
}

void refresh_tile_mapcanvas(struct tile *ptile,
			    bool full_refresh, bool write_to_screen);
void refresh_unit_mapcanvas(struct unit *punit, struct tile *ptile,
			    bool full_refresh, bool write_to_screen);
void refresh_city_mapcanvas(struct city *pcity, struct tile *ptile,
			    bool full_refresh, bool write_to_screen);

void unqueue_mapview_updates(bool write_to_screen);

void map_to_gui_vector(const struct tileset *t,
		       int *gui_dx, int *gui_dy, int map_dx, int map_dy);
bool tile_to_canvas_pos(int *canvas_x, int *canvas_y, struct tile *ptile);
struct tile *canvas_pos_to_tile(int canvas_x, int canvas_y);
struct tile *canvas_pos_to_nearest_tile(int canvas_x, int canvas_y);

void get_mapview_scroll_window(int *xmin, int *ymin,
			       int *xmax, int *ymax,
			       int *xsize, int *ysize);
void get_mapview_scroll_step(int *xstep, int *ystep);
void get_mapview_scroll_pos(int *scroll_x, int *scroll_y);
void set_mapview_scroll_pos(int scroll_x, int scroll_y);

void set_mapview_origin(int gui_x0, int gui_y0);
struct tile *get_center_tile_mapcanvas(void);
void center_tile_mapcanvas(struct tile *ptile);

bool tile_visible_mapcanvas(struct tile *ptile);
bool tile_visible_and_not_on_border_mapcanvas(struct tile *ptile);

void put_unit(const struct unit *punit,
	      struct canvas *pcanvas, int canvas_x, int canvas_y);
void put_city(struct city *pcity,
	      struct canvas *pcanvas, int canvas_x, int canvas_y);
void put_terrain(struct tile *ptile,
		 struct canvas *pcanvas, int canvas_x, int canvas_y);

void put_unit_city_overlays(struct unit *punit,
                            struct canvas *pcanvas,
                            int canvas_x, int canvas_y, int *upkeep_cost,
                            int happy_cost);
void toggle_city_color(struct city *pcity);
void toggle_unit_color(struct unit *punit);

void put_nuke_mushroom_pixmaps(struct tile *ptile);

void put_one_element(struct canvas *pcanvas, enum mapview_layer layer,
		     struct tile *ptile,
		     const struct tile_edge *pedge,
		     const struct tile_corner *pcorner,
		     const struct unit *punit, struct city *pcity,
		     int canvas_x, int canvas_y,
		     const struct city *citymode);

void update_map_canvas(int canvas_x, int canvas_y, int width, int height);
void update_map_canvas_visible(void);
void update_city_description(struct city *pcity);

void show_city_descriptions(int canvas_x, int canvas_y,
			    int width, int height);
bool show_unit_orders(struct unit *punit);

void draw_segment(struct tile *ptile, enum direction8 dir);
void undraw_segment(struct tile *ptile, enum direction8 dir);

void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
void move_unit_map_canvas(struct unit *punit,
			  struct tile *ptile, int dx, int dy);

struct city *find_city_or_settler_near_tile(const struct tile *ptile,
					    struct unit **punit);
struct city *find_city_near_tile(const struct tile *ptile);

void get_city_mapview_production(struct city *pcity,
                                 char *buf, size_t buf_len);
void get_city_mapview_name_and_growth(struct city *pcity,
				      char *name_buffer,
				      size_t name_buffer_len,
				      char *growth_buffer,
				      size_t growth_buffer_len,
				      enum color_std *grwoth_color);

void init_mapview_decorations(void);
bool map_canvas_resized(int width, int height);
void init_mapcanvas_and_overview(void);

void get_spaceship_dimensions(int *width, int *height);
void put_spaceship(struct canvas *pcanvas, int canvas_x, int canvas_y,
		   const struct player *pplayer);

#endif /* FC__MAPVIEW_COMMON_H */
