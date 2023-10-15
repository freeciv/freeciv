/***********************************************************************
 Freeciv - Copyright (C) 1996-2005 - Freeciv Development Team
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
#include <fc_config.h>
#endif

#include <stdarg.h>

#include <gtk/gtk.h>

/* utility */
#include "support.h"

/* gui-gtk-4.0 */
#include "gui_main.h"
#include "gui_stuff.h"

#include "choice_dialog.h"

/***********************************************************************
  Choice dialog: A dialog with a label and a list of buttons
  placed vertically
***********************************************************************/

/*******************************************************************//**
  Get the number of buttons in the choice dialog.
***********************************************************************/
int choice_dialog_get_number_of_buttons(GtkWidget *cd)
{
  return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cd), "nbuttons"));
}

/*******************************************************************//**
  Get nth button widget from dialog
***********************************************************************/
static GtkWidget* choice_dialog_get_nth_button(GtkWidget *cd,
                                               int button)
{
  char button_name[512];
  GtkWidget *b;

  fc_snprintf(button_name, sizeof(button_name), "button%d", button);

  b = g_object_get_data(G_OBJECT(cd), button_name);

  return b;
}

/*******************************************************************//**
  Set sensitivity state of choice dialog button.
***********************************************************************/
void choice_dialog_button_set_sensitive(GtkWidget *cd, int button,
                                        gboolean state)
{
  gtk_widget_set_sensitive(choice_dialog_get_nth_button(cd, button), state);
}

/*******************************************************************//**
  Set label for choice dialog button.
***********************************************************************/
void choice_dialog_button_set_label(GtkWidget *cd, int number,
                                    const char *label)
{
  GtkWidget* button = choice_dialog_get_nth_button(cd, number);
  gtk_button_set_label(GTK_BUTTON(button), label);
}

/*******************************************************************//**
  Set tool tip for choice dialog button.
***********************************************************************/
void choice_dialog_button_set_tooltip(GtkWidget *cd, int number,
                                      const char *tool_tip)
{
  GtkWidget* button = choice_dialog_get_nth_button(cd, number);
  gtk_widget_set_tooltip_text(button, tool_tip);
}

/*******************************************************************//**
  Move the specified button to the end.
***********************************************************************/
void choice_dialog_button_move_to_the_end(GtkWidget *cd,
                                          const int number)
{
  GtkWidget *button = choice_dialog_get_nth_button(cd, number);
  GtkWidget *bbox = g_object_get_data(G_OBJECT(cd), "bbox");

  gtk_box_reorder_child_after(GTK_BOX(bbox), button, NULL);
}

/*******************************************************************//**
  Create choice dialog
***********************************************************************/
GtkWidget *choice_dialog_start(GtkWindow *parent, const gchar *name,
                               const gchar *text)
{
  GtkWidget *dshell, *dlabel, *vbox, *bbox;

  dshell = gtk_window_new();
  setup_dialog(dshell, toplevel);

  gtk_window_set_title(GTK_WINDOW(dshell), name);

  gtk_window_set_transient_for(GTK_WINDOW(dshell), parent);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dshell), TRUE);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_window_set_child(GTK_WINDOW(dshell), vbox);

  gtk_widget_set_margin_start(vbox, 5);
  gtk_widget_set_margin_end(vbox, 5);
  gtk_widget_set_margin_top(vbox, 5);
  gtk_widget_set_margin_bottom(vbox, 5);

  dlabel = gtk_label_new(text);
  gtk_box_append(GTK_BOX(vbox), dlabel);

  bbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(vbox), bbox);

  g_object_set_data(G_OBJECT(dshell), "bbox", bbox);
  g_object_set_data(G_OBJECT(dshell), "nbuttons", GINT_TO_POINTER(0));
  g_object_set_data(G_OBJECT(dshell), "hide", GINT_TO_POINTER(FALSE));

  gtk_widget_set_visible(vbox, TRUE);
  gtk_widget_set_visible(dlabel, TRUE);

  return dshell;
}

/*******************************************************************//**
  Choice dialog has been clicked and primary handling has
  taken place already.
***********************************************************************/
static void choice_dialog_clicked(GtkWidget *w, gpointer data)
{
  if (g_object_get_data(G_OBJECT(data), "hide")) {
    gtk_widget_set_visible(GTK_WIDGET(data), FALSE);
  } else {
    gtk_window_destroy(GTK_WINDOW(data));
  }
}

/*******************************************************************//**
  Add button to choice dialog.
***********************************************************************/
void choice_dialog_add(GtkWidget *dshell, const gchar *label,
                       GCallback handler, gpointer data,
                       bool meta, const gchar *tool_tip)
{
  GtkWidget *button, *bbox;
  char name[512];
  int nbuttons;

  bbox = g_object_get_data(G_OBJECT(dshell), "bbox");
  nbuttons = choice_dialog_get_number_of_buttons(dshell);
  g_object_set_data(G_OBJECT(dshell), "nbuttons", GINT_TO_POINTER(nbuttons+1));

  fc_snprintf(name, sizeof(name), "button%d", nbuttons);

  button = gtk_button_new_with_mnemonic(label);
  gtk_box_append(GTK_BOX(bbox), button);
  g_object_set_data(G_OBJECT(dshell), name, button);

  if (handler) {
    g_signal_connect(button, "clicked", handler, data);
  }

  if (!meta) {
    /* This button makes the choice. */
    g_signal_connect_after(button, "clicked",
                           G_CALLBACK(choice_dialog_clicked), dshell);
  }

  if (tool_tip != NULL) {
    gtk_widget_set_tooltip_text(button, tool_tip);
  }
}

/*******************************************************************//**
  Choice dialog construction ready
***********************************************************************/
void choice_dialog_end(GtkWidget *dshell)
{
  GtkWidget *bbox;

  bbox = g_object_get_data(G_OBJECT(dshell), "bbox");

  gtk_widget_set_visible(bbox, TRUE);
  gtk_widget_set_visible(dshell, TRUE);
}

/*******************************************************************//**
  Set hide property of choice dialog
***********************************************************************/
void choice_dialog_set_hide(GtkWidget *dshell, gboolean setting)
{
  g_object_set_data(G_OBJECT(dshell), "hide", GINT_TO_POINTER(setting));
}

/*******************************************************************//**
  Open new choice dialog.
***********************************************************************/
GtkWidget *popup_choice_dialog(GtkWindow *parent, const gchar *dialogname,
                               const gchar *text, ...)
{
  GtkWidget *dshell;
  va_list args;
  gchar *name;

  dshell = choice_dialog_start(parent, dialogname, text);

  va_start(args, text);

  while ((name = va_arg(args, gchar *))) {
    GCallback handler;
    gpointer data;

    handler = va_arg(args, GCallback);
    data = va_arg(args, gpointer);

    choice_dialog_add(dshell, name, handler, data, FALSE, NULL);
  }

  va_end(args);

  choice_dialog_end(dshell);

  return dshell;
}

/*******************************************************************//**
  Free choice dialog.
***********************************************************************/
void choice_dialog_destroy(GtkWidget *dlg)
{
  if (dlg != NULL) {
    gtk_window_destroy(GTK_WINDOW(dlg));
  }
}
