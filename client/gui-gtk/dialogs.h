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
#ifndef FC__DIALOGS_H
#define FC__DIALOGS_H

#include <gtk/gtk.h>

#include "dialogs_g.h"

struct tile;

void message_dialog_button_set_sensitive(GtkWidget *shl, char *bname, int state);
GtkWidget *popup_message_dialog(GtkWidget *parent, char *shellname, char *text,
				...);
void destroy_message_dialog(GtkWidget *button);

void destroy_me_callback(GtkWidget *w, gpointer data);
void taxrates_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);

gint deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data);

#endif  /* FC__DIALOGS_H */
