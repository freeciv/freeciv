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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <gtk/gtk.h>  

#include "fcintl.h"

#include "gui_main.h"
#include "gui_stuff.h"

#include "inputdlg.h"

/****************************************************************
...
*****************************************************************/
static void input_dialog_close(GtkWidget * shell)
{
  GtkWidget *parent = gtk_object_get_data(GTK_OBJECT(shell),
					  "parent");
  void (*cancel_callback) (gpointer) =
      (void (*)(gpointer)) gtk_object_get_data(GTK_OBJECT(shell),
					       "cancel_callback");
  gpointer cancel_data = gtk_object_get_data(GTK_OBJECT(shell),
					     "cancel_data");

  if (cancel_callback) {
    cancel_callback(cancel_data);
  }

  gtk_widget_set_sensitive(parent, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void input_dialog_ok_callback(GtkWidget * w, gpointer data)
{
  GtkWidget *shell = (GtkWidget *) data;
  GtkWidget *parent = gtk_object_get_data(GTK_OBJECT(shell),
					  "parent");
  void (*ok_callback) (const char *, gpointer) =
      (void (*)(const char *, gpointer))
      gtk_object_get_data(GTK_OBJECT(shell),
			  "ok_callback");
  gpointer ok_data = gtk_object_get_data(GTK_OBJECT(shell),
					 "ok_data");
  GtkWidget *input = gtk_object_get_data(GTK_OBJECT(shell), "iinput");

  ok_callback(gtk_entry_get_text(GTK_ENTRY(input)), ok_data);

  gtk_widget_set_sensitive(parent, TRUE);

  gtk_widget_destroy(shell);
}

/****************************************************************
...
*****************************************************************/
static void input_dialog_cancel_callback(GtkWidget * w, gpointer data)
{
  GtkWidget *shell = (GtkWidget *) data;
  
  input_dialog_close(shell);

  gtk_widget_destroy(shell);
}

/****************************************************************
...
*****************************************************************/
static gint input_dialog_del_callback(GtkWidget * w, GdkEvent * ev,
				      gpointer data)
{
  GtkWidget *shell = (GtkWidget *) data;

  input_dialog_close(shell);

  return FALSE;
}

/****************************************************************
...
*****************************************************************/
GtkWidget *input_dialog_create(GtkWidget * parent, char *dialogname,
			       char *text, char *postinputtest,
			       void (*ok_callback) (const char *, gpointer),
			       gpointer ok_data,
			       void (*cancel_callback) (gpointer),
			       gpointer cancel_data)
{
  GtkWidget *shell, *label, *input, *ok, *cancel;
  
  gtk_widget_set_sensitive(parent, FALSE);
  
  shell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(shell),"delete_event",
	GTK_SIGNAL_FUNC(input_dialog_del_callback), shell);

  gtk_window_set_title(GTK_WINDOW(shell), dialogname);
  
  gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), 5);

  label=gtk_frame_new(text);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), label, TRUE, TRUE, 0);

  input=gtk_entry_new();
  gtk_container_add(GTK_CONTAINER(label), input);
  gtk_entry_set_text(GTK_ENTRY(input),postinputtest);
  
  gtk_signal_connect(GTK_OBJECT(input), "activate",
		     GTK_SIGNAL_FUNC(input_dialog_ok_callback), shell);

  ok=gtk_button_new_with_label(_("Ok"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area),
	ok, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
		     GTK_SIGNAL_FUNC(input_dialog_ok_callback), shell);

  cancel=gtk_button_new_with_label(_("Cancel"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area),
	cancel, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
	GTK_SIGNAL_FUNC(input_dialog_cancel_callback), shell);

  gtk_widget_grab_focus(input);
    
  gtk_set_relative_position(parent, shell, 10, 10);

  gtk_object_set_data(GTK_OBJECT(shell), "iinput", input);
  gtk_object_set_data(GTK_OBJECT(shell), "parent", parent);
  gtk_object_set_data(GTK_OBJECT(shell), "ok_callback", ok_callback);
  gtk_object_set_data(GTK_OBJECT(shell), "ok_data", ok_data);
  gtk_object_set_data(GTK_OBJECT(shell), "cancel_callback", cancel_callback);
  gtk_object_set_data(GTK_OBJECT(shell), "cancel_data", cancel_data);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);
  gtk_widget_show(shell);
  
  return shell;
}
