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
#ifndef FC__MAPCTRL_H
#define FC__MAPCTRL_H

#include <gtk/gtk.h>

#include "mapctrl_g.h"

struct unit;

gint key_city_workers(GtkWidget *w, GdkEventKey *ev);
gint adjust_workers(GtkWidget *widget, GdkEventButton *ev);

gint butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev);
gint butt_down_wakeup(GtkWidget *w, GdkEventButton *ev);
gint butt_down_overviewcanvas(GtkWidget *w, GdkEventButton *ev);

void center_on_unit(void);
void focus_to_next_unit(void);
void popupinfo_popdown_callback(GtkWidget *w, gpointer data);

#endif  /* FC__MAPCTRL_H */
