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

struct button_descr {
  const char *text;
  void (*callback) (gpointer);
  gpointer data;
  bool sensitive;
};

void popup_revolution_dialog(void);
void message_dialog_button_set_sensitive(GtkWidget * shl, const char *bname,
					 bool state);

void popdown_notify_dialog(void);

GtkWidget *base_popup_message_dialog(GtkWidget * parent,
				     const char *dialogname,
				     const char *text,
				     void (*close_callback) (gpointer),
				     gpointer close_callback_data,
				     int num_buttons,
				     const struct button_descr *buttons);

GtkWidget *popup_message_dialog(GtkWidget * parent, const char *dialogname,
				const char *text,
				void (*close_callback) (gpointer),
				gpointer close_callback_data, ...);

void dummy_close_callback(gpointer data);

void destroy_me_callback(GtkWidget *w, gpointer data);
void taxrates_callback(GtkWidget *w, GdkEventButton *ev, gpointer data);

gint deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data);

#endif  /* FC__DIALOGS_H */
