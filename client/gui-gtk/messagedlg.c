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

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "events.h"
#include "fcintl.h"

#include "options.h"

#include "gui_main.h"

#include "messagedlg.h"

/*************************************************************************/
static GtkWidget *create_messageopt_dialog(void);
static void messageopt_ok_command_callback(GtkWidget *w, gpointer data);
static void messageopt_cancel_command_callback(GtkWidget *w, gpointer data);
static int messageopt_delete_callback(GtkWidget *w, GdkEvent *ev, 
                                      gpointer data);
static GtkWidget *messageopt_toggles[E_LAST][NUM_MW];

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  GtkWidget *shell;
  int i, j, state;

  shell = create_messageopt_dialog();

  /* Doing this here makes the "No"'s centered consistently */
  for(i = 0; i < E_LAST; i++) {
    for(j = 0; j < NUM_MW; j++) {
      state = messages_where[i] & (1 << j);
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
				  (messageopt_toggles[i][j]), state);
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
  GtkWidget *shell, *table, *scrolled, *explanation, *ok, *cancel;
  GtkWidget *label;
  GtkWidget *toggle = NULL;
  int i, j;
  GtkAccelGroup *accel = gtk_accel_group_new();
  
  shell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(shell), "delete_event",
                  GTK_SIGNAL_FUNC(messageopt_delete_callback), shell);

  gtk_window_set_title(GTK_WINDOW(shell), _("Message Options"));
  gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), 5);
  gtk_accel_group_attach(accel, GTK_OBJECT(shell));
  gtk_widget_set_usize(shell, 0, 500);

  explanation = gtk_label_new(_("Where to Display Messages\n"
                                 "Out = Output window, Mes = Messages window," 
                                 "\nPop = Popup individual window"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), explanation,
		FALSE, FALSE, 0);

  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), scrolled, 
                     TRUE, TRUE, 0);

  table = gtk_table_new(E_LAST + 1, NUM_MW + 1, FALSE);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), table);

  label = gtk_label_new(_("Out"));
  gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 0, 1);
  label = gtk_label_new(_("Mes"));
  gtk_table_attach_defaults(GTK_TABLE(table), label, 2, 3, 0, 1);
  label = gtk_label_new(_("Pop"));
  gtk_table_attach_defaults(GTK_TABLE(table), label, 3, 4, 0, 1);

  for(i = 0; i < E_LAST; i++)  {
    label = gtk_label_new(get_message_text(sorted_events[i]));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, i + 1, i + 2);

    for(j = 0; j < NUM_MW; j++) {
      toggle = gtk_check_button_new();
      gtk_table_attach_defaults(GTK_TABLE(table), toggle,
                                j + 1, j + 2, i + 1, i + 2);
      messageopt_toggles[sorted_events[i]][j] = toggle;
    }
  }

  ok = gtk_button_new_with_label(_("Ok"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), ok,
                     TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(ok);
  gtk_widget_add_accelerator(ok, "clicked", accel, GDK_Escape, 0,
                             GTK_ACCEL_VISIBLE);

  cancel = gtk_button_new_with_label(_("Cancel"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), cancel,
                     TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
  
  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
                  messageopt_ok_command_callback, shell);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
                  messageopt_cancel_command_callback, shell);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
static void messageopt_cancel_command_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy((GtkWidget *)data);
}

/**************************************************************************
...
**************************************************************************/
static int messageopt_delete_callback(GtkWidget * w, GdkEvent * ev,
				      gpointer data)
{
  messageopt_cancel_command_callback(NULL, data);
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static void messageopt_ok_command_callback(GtkWidget *w, gpointer data)
{
  int i, j;

  gtk_widget_set_sensitive(top_vbox, TRUE);

  for(i = 0; i < E_LAST; i++)  {
    messages_where[i] = 0;
    for (j = 0; j < NUM_MW; j++) {
      if (GTK_TOGGLE_BUTTON(messageopt_toggles[i][j])->active) {
	messages_where[i] |= (1 << j);
      }
    }
  }

  gtk_widget_destroy((GtkWidget *)data);
}
