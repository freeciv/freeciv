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
struct t_popup_pos {int xroot, yroot;};

void key_city_workers(GtkWidget *w, GdkEventKey *ev);

gint butt_release_mapcanvas(GtkWidget *w, GdkEventButton *ev);
gint butt_down_mapcanvas(GtkWidget *w, GdkEventButton *ev);
gint butt_down_overviewcanvas(GtkWidget *w, GdkEventButton *ev);
gint move_mapcanvas(GtkWidget *widget, GdkEventButton *event);
gint move_overviewcanvas(GtkWidget *widget, GdkEventButton *event);

void center_on_unit(void);
void popupinfo_popdown_callback(GtkWidget *w, gpointer data);
void popupinfo_positioning_callback(GtkWidget *w, GtkAllocation *alloc, 
                                    gpointer user_data);

/* Color to use to display the workers */
extern int city_workers_color;

#endif  /* FC__MAPCTRL_H */
