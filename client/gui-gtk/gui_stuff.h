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
#ifndef FC__GUI_STUFF_H
#define FC__GUI_STUFF_H

#include <gtk/gtk.h>

GtkWidget *gtk_accelbutton_new(const gchar *label, GtkAccelGroup *accel);
void gtk_set_label(GtkWidget *w, const char *text);
void gtk_set_bitmap(GtkWidget *w, GdkPixmap *pm);
void gtk_expose_now(GtkWidget *w);
void gtk_set_relative_position(GtkWidget *ref, GtkWidget *w, int px, int py);
void gtk_window_hide(GtkWindow *window);
void gtk_window_show(GtkWindow *window);

char **intl_slist(int n, const char **s);

#endif  /* FC__GUI_STUFF_H */
