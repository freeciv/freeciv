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

void get_mapview_dimensions(int *map_view_topleft_map_x,
			    int *map_view_topleft_map_y,
			    int *map_view_pixel_width,
			    int *map_view_pixel_height);

void update_info_label(void);
void update_unit_info_label(struct unit *punit);
void update_timeout_label(void);
void update_turn_done_button(bool do_restore);
void update_city_descriptions(void);
void set_indicator_icons(int bulb, int sol, int flake, int gov);

void set_overview_dimensions(int x, int y);
void overview_update_tile(int x, int y);

void center_tile_mapcanvas(int x, int y);

void show_city_desc(struct city *pcity, int canvas_x, int canvas_y);

void put_one_tile(int map_x, int map_y, int canvas_x, int canvas_y);
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
void flush_mapcanvas(int canvas_x, int canvas_y,
		     int pixel_width, int pixel_height);

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
