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

#include <gtk/gtk.h>

#include "events.h"
#include "fcintl.h"

#include "gui_main.h"
#include "gui_stuff.h"
#include "options.h"

#include "messagedlg.h"

#define NUM_LISTS 1

/*************************************************************************/
static GtkWidget *shell;
static GtkTreeStore *model[NUM_LISTS];

static GtkWidget *create_messageopt_dialog(void);
static void messageopt_command_callback(GtkWidget *w, gint response_id);
static void messageopt_destroy_callback(GtkWidget *w, gpointer data);
static void item_toggled(GtkCellRendererToggle *cell,
			 gchar *spath, gpointer data);

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  if (!shell)
    shell = create_messageopt_dialog();

  gtk_widget_show(shell);
}

/**************************************************************************
...
**************************************************************************/
GtkWidget *create_messageopt_dialog(void)
{
  GtkWidget *shell, *form, *explanation;
  int n, i, j;
  
  shell = gtk_dialog_new_with_buttons(_("Message Options"),
  	GTK_WINDOW(toplevel),
	0,
	GTK_STOCK_OK,
	GTK_RESPONSE_ACCEPT,
	GTK_STOCK_CANCEL,
	GTK_RESPONSE_REJECT,
	NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_ACCEPT);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);

  explanation = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(explanation),
    _("<span foreground=\"blue\"><i>Where to display messages?\n"
      "<b>Out</b>put window ; "
      "<b>Mes</b>sages window ; "
      "<b>Pop</b>up individual window</i></span>"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), explanation,
	FALSE, FALSE, 0);
  gtk_widget_show(explanation);	

  form = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), form, TRUE, TRUE, 5);

  for (n=0; n<NUM_LISTS; n++) {
    model[n] = gtk_tree_store_new(5,
     G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_INT);
  }

  for (i=0; i<E_LAST; i++)  {
    GtkTreeIter it;

    n = (i % NUM_LISTS);

    gtk_tree_store_append(model[n], &it, NULL);
    gtk_tree_store_set(model[n], &it,
    	3, get_message_text(sorted_events[i]), 4, sorted_events[i], -1);

    for (j=0; j<NUM_MW; j++) {
      gtk_tree_store_set(model[n], &it,
        j, messages_where[sorted_events[i]] & (1<<j), -1);
    }
  }

  for (n=0; n<NUM_LISTS; n++) {
    GtkWidget *view, *sw;
    GtkCellRenderer *renderer;
    gint col;

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model[n]));
    g_object_unref(G_OBJECT(model[n]));

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
	-1, _("Event"), renderer, "text", 3, NULL);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(renderer), "column", GINT_TO_POINTER(0));
    g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(item_toggled),
		     model[n]);

    col = gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
	-1, _("Out"), renderer, "active", 0, NULL);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(renderer), "column", GINT_TO_POINTER(1));
    g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(item_toggled),
		     model[n]);

    col = gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
  	-1, _("Mes"), renderer, "active", 1, NULL);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(renderer), "column", GINT_TO_POINTER(2));
    g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(item_toggled),
		     model[n]);

    col = gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
  	-1, _("Pop"), renderer, "active", 2, NULL);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), view);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_usize(sw, -1, 425);
    gtk_box_pack_start(GTK_BOX(form), sw, TRUE, TRUE, 0);
  }

  g_signal_connect(shell, "response",
		   G_CALLBACK(messageopt_command_callback), NULL);
  g_signal_connect(shell, "destroy",
		   G_CALLBACK(messageopt_destroy_callback), NULL);
  gtk_widget_show_all(form);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
static void messageopt_destroy_callback(GtkWidget *w, gpointer data)
{
  shell = NULL;
}

/**************************************************************************
...
**************************************************************************/
static void messageopt_command_callback(GtkWidget *w, gint response_id)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    ITree it;
    gint n, j, i;
    gboolean toggle;

    for (i=0; i<E_LAST; i++) {
      messages_where[i] = 0;
    }

    for (n=0; n<NUM_LISTS; n++) {
      for (itree_begin(model[n], &it); !itree_end(&it); itree_next(&it)) {
        for (j=0; j<NUM_MW; j++) {
          itree_get(&it, j, &toggle, 4, &i, -1);

	  if (toggle)
	    messages_where[i] |= (1<<j);
	}
      }
    }
  }
  gtk_widget_destroy(shell);
}

/**************************************************************************
...
**************************************************************************/
static void item_toggled(GtkCellRendererToggle *cell,
			 gchar *spath, gpointer data)
{
  GtkTreeModel *model = GTK_TREE_MODEL(data);
  GtkTreePath *path;
  GtkTreeIter it;
  gboolean toggle;
  gint column;

  path = gtk_tree_path_new_from_string(spath);

  column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column"));

  gtk_tree_model_get_iter(model, &it, path);
  gtk_tree_model_get(model, &it, column, &toggle, -1);
  toggle ^= 1;
  gtk_tree_store_set(GTK_TREE_STORE(model), &it, column, toggle, -1);

  gtk_tree_path_free(path);
}
