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
#ifndef FC__MAPVIEW_H
#define FC__MAPVIEW_H

#include <X11/Intrinsic.h>

#include "mapview_g.h"

#include "graphics.h"

struct unit;
struct city;

Pixmap get_thumb_pixmap(int onoff);

void put_unit_pixmap(struct unit *punit, Pixmap pm, int xtile, int ytile);

void put_unit_pixmap_city_overlays(struct unit *punit, Pixmap pm,
				   int unhappiness, int upkeep);

void put_city_pixmap(struct city *pcity, Pixmap pm, int xtile, int ytile);
void put_city_tile_output(Pixmap pm, int x, int y, 
			  int food, int shield, int trade);

void overview_canvas_expose(Widget w, XEvent *event, Region exposed, 
			    void *client_data);
void map_canvas_expose(Widget w, XEvent *event, Region exposed, 
		       void *client_data);

void pixmap_put_tile(Pixmap pm, int x, int y, int map_x, int map_y, 
		     int citymode);
void pixmap_put_black_tile(Pixmap pm, int x, int y);
void pixmap_frame_tile_red(Pixmap pm, int x, int y);

void my_XawScrollbarSetThumb(Widget w, float top, float shown);
void scrollbar_jump_callback(Widget scrollbar, XtPointer client_data,
			     XtPointer percent_ptr);
void scrollbar_scroll_callback(Widget w, XtPointer client_data,
			     XtPointer position_ptr);

Pixmap get_thumb_pixmap(int onoff);
Pixmap get_citizen_pixmap(int frame);
void timer_callback(caddr_t client_data, XtIntervalId *id);

#endif  /* FC__MAPVIEW_H */
