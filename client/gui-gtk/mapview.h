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

#include <gtk/gtk.h>

#include "citydlg_common.h"
#include "mapview_common.h"
#include "mapview_g.h"

#include "graphics.h"
#include "gtkpixcomm.h"

struct unit;
struct city;

GdkPixmap *get_thumb_pixmap(int onoff);

gint overview_canvas_expose(GtkWidget *w, GdkEventExpose *ev);
gint map_canvas_expose(GtkWidget *w, GdkEventExpose *ev);

void put_unit_gpixmap(struct unit *punit, GtkPixcomm *p);

void put_unit_gpixmap_city_overlays(struct unit *punit, GtkPixcomm *p);

void scrollbar_jump_callback(GtkAdjustment *adj, gpointer hscrollbar);
void update_map_canvas_scrollbars_size(void);

#endif  /* FC__MAPVIEW_H */
