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

#include "mapview_common.h"
#include "shared.h"		/* bool type */

struct unit;
struct city;
struct Sprite;

void update_info_label(void);
void update_unit_info_label(struct unit *punit);
void update_timeout_label(void);
void update_turn_done_button(bool do_restore);
void update_city_descriptions(void);
void set_indicator_icons(int bulb, int sol, int flake, int gov);

void set_overview_dimensions(int x, int y);
void overview_update_tile(int x, int y);

void show_city_desc(struct city *pcity, int canvas_x, int canvas_y);
void prepare_show_city_descriptions(void);

void gui_map_put_tile_iso(int map_x, int map_y,
			  int canvas_x, int canvas_y,
			  int offset_x, int offset_y, int offset_y_unit,
			  int width, int height, int height_unit,
			  enum draw_type draw);
void gui_put_sprite(struct canvas_store *pcanvas_store,
		    int canvas_x, int canvas_y,
		    struct Sprite *sprite,
		    int offset_x, int offset_y,
		    int width, int height);
void gui_put_sprite_full(struct canvas_store *pcanvas_store,
			 int canvas_x, int canvas_y,
			 struct Sprite *sprite);
void gui_put_rectangle(struct canvas_store *pcanvas_store,
		       enum color_std color,
		       int canvas_x, int canvas_y, int width, int height);
void gui_put_line(struct canvas_store *pcanvas_store, enum color_std color,
		  int start_x, int start_y, int dx, int dy);
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height);
void dirty_rect(int canvas_x, int canvas_y,
		int pixel_width, int pixel_height);
void dirty_all(void);
void flush_dirty(void);

void update_map_canvas_scrollbars(void);

void put_cross_overlay_tile(int x,int y);
void put_city_workers(struct city *pcity, int color);

void draw_unit_animation_frame(struct unit *punit,
			       bool first_frame, bool last_frame,
			       int old_canvas_x, int old_canvas_y,
			       int new_canvas_x, int new_canvas_y);
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
void put_nuke_mushroom_pixmaps(int x, int y);

void refresh_overview_canvas(void);
void refresh_overview_viewrect(void);

void draw_segment(int src_x, int src_y, int dir);

void tileset_changed(void);

#endif  /* FC__MAPVIEW_G_H */
