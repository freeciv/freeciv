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
#ifndef __MAPVIEW_H
#define __MAPVIEW_H

#include <sys/types.h>
#include <X11/Intrinsic.h>
struct unit;
struct city;

#define CORNER_TILES     0

#define NUKE_TILE0        1*20+17
#define NUKE_TILE1        1*20+18
#define NUKE_TILE2        1*20+19
#define NUKE_TILE3        2*20+17
#define NUKE_TILE4        2*20+18
#define NUKE_TILE5        2*20+19
#define NUKE_TILE6        3*20+17
#define NUKE_TILE7        3*20+18
#define NUKE_TILE8        3*20+19

#define GRASSLAND_TILES  1*20
#define DESERT_TILES     2*20
#define ARCTIC_TILES     3*20
#define JUNGLE_TILES     4*20
#define PLAINS_TILES     5*20
#define SWAMP_TILES      6*20
#define TUNDRA_TILES     7*20

/* jjm@codewell.com 30dec1998a
   Moved road and rail tiles to roads.xpm; added tiles for diagonals.
   (Lots of rearranging of tile indices.)
*/

extern int ROAD_TILES;
extern int RAIL_TILES;

#define RIVER_TILES      8*20
#define OUTLET_TILES     8*20+16
#define OCEAN_TILES      9*20
#define HILLS_TILES      10*20
#define FOREST_TILES     10*20+4
#define MOUNTAINS_TILES  10*20+8
#define DENMARK_TILES    10*20+12
#define X_TILE           10*20+19

#define SPECIAL_TILES    11*20
#define S_TILE           11*20+13
#define G_TILE           11*20+14
#define M_TILE           11*20+15
#define P_TILE           11*20+16
#define R_TILE           11*20+17
#define I_TILE           11*20+18
#define F_TILE           11*20+19

#define FLAG_TILES       12*20
#define CROSS_TILE       12*20+17
#define AUTO_TILE        12*20+18
#define PLUS_TILE        12*20+19

/*
#define UNIT_TILES       15*20
The tiles for the units are now stored in units.xpm
*/
extern int UNIT_TILES;

#define IRRIGATION_TILE  13*20+8
#define HILLMINE_TILE    13*20+9
#define DESERTMINE_TILE  13*20+10
#define POLLUTION_TILE   13*20+11
#define CITY_TILE        13*20+12
#define CITY_WALLS_TILE  13*20+13
#define HUT_TILE         13*20+14
#define FORTRESS_TILE    13*20+15

#define BORDER_TILES     14*20

#define NUMBER_TILES     15*20
#define NUMBER_MSD_TILES 15*20+9

#define SHIELD_NUMBERS   16*20
#define TRADE_NUMBERS    16*20+10

#define HP_BAR_TILES     17*20

#define CITY_FLASH_TILE  17*20+14
#define CITY_FOOD_TILES  17*20+15
#define CITY_MASK_TILES  17*20+17
#define CITY_SHIELD_TILE 17*20+19

#define FOOD_NUMBERS     18*20

#define BULB_TILES       19*20
#define GOVERNMENT_TILES 19*20+8
#define SUN_TILES        19*20+14
#define PEOPLE_TILES     19*20+22
#define RIGHT_ARROW_TILE 19*20+30

#define THUMB_TILES      19*20+31

Pixmap get_thumb_pixmap(int onoff);
void decrease_unit_hp_smooth(struct unit *punit0, int hp0, 
			     struct unit *punit1, int hp1);
void blink_active_unit(void);
void update_unit_info_label(struct unit *punit);
void update_timeout_label(void);
void update_info_label(void);
void set_bulb_sol_government(int bulb, int sol, int government);
void update_unit_pix_label(struct unit *punit);
void update_turn_done_button(int do_restore);

void set_overview_dimensions(int x, int y);

void put_unit_pixmap(struct unit *punit, Pixmap pm, int xtile, int ytile);

void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm,
				   int unhappiness, int upkeep);

void put_city_pixmap(struct city *pcity, Pixmap pm, int xtile, int ytile);
void put_city_tile_output(Pixmap pm, int x, int y, 
			  int food, int shield, int trade);
void overview_update_tile(int x, int y);

void move_unit_map_canvas(struct unit *punit, int x0, int y0, int x1, int y1);

int tile_visible_mapcanvas(int x, int y);
int tile_visible_and_not_on_border_mapcanvas(int x, int y);

void center_tile_mapcanvas(int x, int y);
void get_center_tile_mapcanvas(int *x, int *y);

void overview_canvas_expose(Widget w, XEvent *event, Region exposed, 
			    void *client_data);
void map_canvas_expose(Widget w, XEvent *event, Region exposed, 
		       void *client_data);
void update_map_canvas(int tile_x, int tile_y, int width, int height, int write_to_screen);

void pixmap_put_tile(Pixmap pm, int x, int y, int map_x, int map_y, 
		     int citymode);
void pixmap_put_black_tile(Pixmap pm, int x, int y);
void pixmap_frame_tile_red(Pixmap pm, int x, int y);

void put_nuke_mushroom_pixmaps(int abs_x0, int abs_y0);

void put_cross_overlay_tile(int x,int y);

void my_XawScrollbarSetThumb(Widget w, float top, float shown);
void update_map_canvas_scrollbars(void);
void scrollbar_jump_callback(Widget scrollbar, XtPointer client_data,
			     XtPointer percent_ptr);
void scrollbar_scroll_callback(Widget w, XtPointer client_data,
			     XtPointer position_ptr);

void refresh_overview_canvas(void);
void refresh_overview_viewrect(void);
void refresh_tile_mapcanvas(int x, int y, int write_to_screen);

int map_canvas_adjust_x(int x);
int map_canvas_adjust_y(int y);
Pixmap get_thumb_pixmap(int onoff);
Pixmap get_citizen_pixmap(int frame);
void timer_callback(caddr_t client_data, XtIntervalId *id);

#endif

