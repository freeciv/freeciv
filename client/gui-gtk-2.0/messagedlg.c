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
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "clinet.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"

#include "messagedlg.h"

/*************************************************************************/
static GtkWidget *create_messageopt_dialog(void);
static void messageopt_ok_command_callback(GtkWidget * w, gpointer data);
static void messageopt_cancel_command_callback(GtkWidget * w, gpointer data);
static GtkWidget *messageopt_toggles[E_LAST][NUM_MW];

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  GtkWidget *shell;
  int i, j, state;

  shell=create_messageopt_dialog();

  /* Doing this here makes the "No"'s centered consistently */
  for(i=0; i<E_LAST; i++) {
    for(j=0; j<NUM_MW; j++) {
      state = messages_where[i] & (1<<j);
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(messageopt_toggles[i][j]),
	state);
    }
  }
  
/*  gtk_set_relative_position(toplevel, shell, 15, 0);*/
  gtk_window_set_position (GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  gtk_widget_show(shell);
  gtk_widget_set_sensitive(top_vbox, FALSE);
}

/**************************************************************************
...
**************************************************************************/
GtkWidget *create_messageopt_dialog(void)
{
  GtkWidget *shell, *form, *explanation, *ok, *cancel, *col1, *col2;
  GtkWidget *colhead1, *colhead2;
  GtkWidget *label;
  GtkWidget *toggle=0;
  int i, j;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  shell=gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(shell), _("Message Options"));
  gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), 5);
  gtk_accel_group_attach(accel, GTK_OBJECT(shell));

  explanation=gtk_label_new(_("Where to Display Messages\nOut = Output window,"
		      " Mes = Messages window, Pop = Popup individual window"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), explanation,
		FALSE, FALSE, 0);

  form=gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), form, FALSE, FALSE, 0);

  colhead1=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(form), colhead1, TRUE, TRUE, 0);

  col1=gtk_table_new(E_LAST/2+2, NUM_MW+1, FALSE);
  gtk_container_add(GTK_CONTAINER(colhead1), col1);

  colhead2=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(form), colhead2, TRUE, TRUE, 0);

  col2=gtk_table_new(E_LAST/2+2, NUM_MW+1, FALSE);
  gtk_container_add(GTK_CONTAINER(colhead2), col2);

  label = gtk_label_new(_("Out:"));
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 1, 2, 0, 1);
  label = gtk_label_new(_("Mes:"));
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 2, 3, 0, 1);
  label = gtk_label_new(_("Pop:"));
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 3, 4, 0, 1);
  label = gtk_label_new(_("Out:"));
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 1, 2, 0, 1);
  label = gtk_label_new(_("Mes:"));
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 2, 3, 0, 1);
  label = gtk_label_new(_("Pop:"));
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 3, 4, 0, 1);

  for(i=0; i<E_LAST; i++)  {
    int line = (i%E_LAST);
    int is_col1 = i<(E_LAST/2);

    label = gtk_label_new(get_message_text(sorted_events[i]));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach_defaults(GTK_TABLE(is_col1?col1:col2), label,
               0, 1, line+1, line+2);
    for(j=0; j<NUM_MW; j++) {
      toggle = gtk_check_button_new();
      gtk_table_attach_defaults(GTK_TABLE(is_col1?col1:col2), toggle,
               j+1, j+2, line+1, line+2);
      messageopt_toggles[sorted_events[i]][j]=toggle;
    }
  }

  ok=gtk_button_new_with_label(_("Ok"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), ok,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(ok);
  gtk_widget_add_accelerator(ok, "clicked", accel, GDK_Escape, 0,
	GTK_ACCEL_VISIBLE);

  cancel=gtk_button_new_with_label(_("Cancel"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), cancel,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
  
  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
	messageopt_ok_command_callback, shell);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
	messageopt_cancel_command_callback, shell);
  gtk_signal_connect(GTK_OBJECT(shell), "delete_event",
        GTK_SIGNAL_FUNC(messageopt_cancel_command_callback), shell);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
void messageopt_cancel_command_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy((GtkWidget *)data);
}

/**************************************************************************
...
**************************************************************************/
void messageopt_ok_command_callback(GtkWidget *w, gpointer data)
{
  int i, j;

  gtk_widget_set_sensitive(top_vbox, TRUE);

  for(i=0;i<E_LAST;i++)  {
    messages_where[i] = 0;
    for(j=0; j<NUM_MW; j++) {
      if (GTK_TOGGLE_BUTTON(messageopt_toggles[i][j])->active)
        messages_where[i] |= (1<<j);
    }
  }

  gtk_widget_destroy((GtkWidget *)data);
}
