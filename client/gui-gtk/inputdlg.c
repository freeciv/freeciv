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

#include "gui_stuff.h"

#include "inputdlg.h"

extern GtkWidget *toplevel;

/****************************************************************
...
*****************************************************************/
char *input_dialog_get_input(GtkWidget *button)
{
  char *dp;
  GtkWidget *winput;
      
  winput=gtk_object_get_data(GTK_OBJECT(button->parent->parent->parent),
	"iinput");
  
  dp=gtk_entry_get_text(GTK_ENTRY(winput));
 
  return dp;
}


/****************************************************************
...
*****************************************************************/
void input_dialog_destroy(GtkWidget *button)
{
  GtkWidget *parent;

  parent=gtk_object_get_data(GTK_OBJECT(button->parent->parent->parent),
	"parent");

  gtk_widget_set_sensitive(parent, TRUE);

  gtk_widget_destroy(button->parent->parent->parent);
}


/****************************************************************
...
*****************************************************************/
static gint input_dialog_del_callback(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  gtk_widget_set_sensitive((GtkWidget *)data, TRUE);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
GtkWidget *input_dialog_create(GtkWidget *parent, char *dialogname, 
			   char *text, char *postinputtest,
			   void *ok_callback, gpointer ok_cli_data, 
			   void *cancel_callback, gpointer cancel_cli_data)
{
  GtkWidget *shell, *label, *input, *ok, *cancel;
  
  gtk_widget_set_sensitive(parent, FALSE);
  
  shell=gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(shell),"delete_event",
	GTK_SIGNAL_FUNC(input_dialog_del_callback), parent);

  gtk_window_set_title(GTK_WINDOW(shell), dialogname);
  
  gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), 5);

  label=gtk_frame_new(text);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), label, TRUE, TRUE, 0);

  input=gtk_entry_new();
  gtk_container_add(GTK_CONTAINER(label), input);
  gtk_entry_set_text(GTK_ENTRY(input),postinputtest);
  
  gtk_signal_connect(GTK_OBJECT(input), "activate",
	GTK_SIGNAL_FUNC(ok_callback), ok_cli_data);

  ok=gtk_button_new_with_label(_("Ok"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area),
	ok, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
	GTK_SIGNAL_FUNC(ok_callback), ok_cli_data);

  cancel=gtk_button_new_with_label(_("Cancel"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area),
	cancel, TRUE, TRUE, 0);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
	GTK_SIGNAL_FUNC(cancel_callback), cancel_cli_data);

  gtk_widget_grab_focus(input);
    
  gtk_set_relative_position(parent, shell, 10, 10);

  gtk_object_set_data(GTK_OBJECT(shell), "iinput",input);
  gtk_object_set_data(GTK_OBJECT(shell), "parent",parent);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);
  gtk_widget_show(shell);
  
  return shell;
}
