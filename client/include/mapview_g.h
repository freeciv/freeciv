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
#ifndef FC__MAPVIEW_G_H
#define FC__MAPVIEW_G_H

#include "shared.h"		/* bool type */

#include "fc_types.h"

#include "mapview_common.h"

struct Sprite;

void update_info_label(void);
void update_unit_info_label(struct unit *punit);
void update_timeout_label(void);
void update_turn_done_button(bool do_restore);
void update_city_descriptions(void);
void set_indicator_icons(int bulb, int sol, int flake, int gov);

void map_size_changed(void);
struct canvas *canvas_create(int width, int height);
void canvas_free(struct canvas *store);
struct canvas *get_overview_window(void);

void show_city_desc(struct canvas *pcanvas, int canvas_x, int canvas_y,
		    struct city *pcity, int *width, int *height);
void prepare_show_city_descriptions(void);

void canvas_put_sprite(struct canvas *pcanvas,
		       int canvas_x, int canvas_y, struct Sprite *sprite,
		       int offset_x, int offset_y, int width, int height);
void canvas_put_sprite_full(struct canvas *pcanvas, 
			    int canvas_x, int canvas_y,
			    struct Sprite *sprite);
void canvas_put_sprite_fogged(struct canvas *pcanvas,
			      int canvas_x, int canvas_y,
			      struct Sprite *psprite,
			      bool fog, int fog_x, int fog_y);
void canvas_put_rectangle(struct canvas *pcanvas,
			  enum color_std color,
			  int canvas_x, int canvas_y, int width, int height);
void canvas_fill_sprite_area(struct canvas *pcanvas,
			     struct Sprite *psprite, enum color_std color,
			     int canvas_x, int canvas_y);
void canvas_fog_sprite_area(struct canvas *pcanvas, struct Sprite *psprite,
			    int canvas_x, int canvas_y);
void canvas_put_line(struct canvas *pcanvas, enum color_std color,
		     enum line_type ltype, int start_x, int start_y,
		     int dx, int dy);
void canvas_copy(struct canvas *dest, struct canvas *src,
		 int src_x, int src_y, int dest_x, int dest_y,
		 int width, int height);

void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height);
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height);
void dirty_all(void);
void flush_dirty(void);
void gui_flush(void);

void update_map_canvas_scrollbars(void);
void update_map_canvas_scrollbars_size(void);

void put_cross_overlay_tile(struct tile *ptile);
void put_city_worker(struct canvas *pcanvas,
		     enum color_std color, enum city_tile_type worker,
		     int canvas_x, int canvas_y);

void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h);
void tileset_changed(void);

#endif  /* FC__MAPVIEW_G_H */
