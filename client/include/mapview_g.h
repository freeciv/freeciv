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

struct unit;
struct city;

int map_canvas_adjust_x(int x);
int map_canvas_adjust_y(int y);

int tile_visible_mapcanvas(int x, int y);
int tile_visible_and_not_on_border_mapcanvas(int x, int y);

void update_info_label(void);
void update_unit_info_label(struct unit *punit);
void update_timeout_label(void);
void update_turn_done_button(int do_restore);
void update_city_descriptions(void);
void set_indicator_icons(int bulb, int sol, int flake, int gov);

void set_overview_dimensions(int x, int y);
void overview_update_tile(int x, int y);

void center_tile_mapcanvas(int x, int y);
void get_center_tile_mapcanvas(int *x, int *y);

void update_map_canvas(int tile_x, int tile_y, int width, int height,
		       int write_to_screen);
void update_map_canvas_visible(void);
void update_map_canvas_scrollbars(void);

void put_cross_overlay_tile(int x,int y);
void put_city_workers(struct city *pcity, int color);

void move_unit_map_canvas(struct unit *punit, int x0, int y0, int x1, int y1);
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
void put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0);

void refresh_overview_canvas(void);
void refresh_overview_viewrect(void);
void refresh_tile_mapcanvas(int x, int y, int write_to_screen);

void undraw_segment(int src_x, int src_y, int dir);
void draw_segment(int src_x, int src_y, int dir);

#endif  /* FC__MAPVIEW_G_H */
