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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "chatline.h"
#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "connectdlg.h"

static GtkWidget *iname, *ihost, *iport;
static GtkListStore *store;

static GtkWidget *dialog;

/* meta Server */
static bool update_meta_dialog(void);
static void meta_list_callback(GtkTreeSelection *select, GtkTreeModel *model);
static void meta_update_callback(GtkWidget *w, gpointer data);

static int get_meta_list(char *errbuf, int n_errbuf);

/**************************************************************************
...
**************************************************************************/
static void connect_callback(GtkWidget *w, gpointer data)
{
  char errbuf [512];

  sz_strlcpy(player_name, gtk_entry_get_text(GTK_ENTRY(iname)));
  sz_strlcpy(server_host, gtk_entry_get_text(GTK_ENTRY(ihost)));
  sscanf(gtk_entry_get_text(GTK_ENTRY(iport)), "%d", &server_port);
  
  if(connect_to_server(player_name, server_host, server_port,
		       errbuf, sizeof(errbuf))!=-1) {
    gtk_widget_destroy(dialog);
  }
  else
    append_output_window(errbuf);
}

/**************************************************************************
...
**************************************************************************/
static bool update_meta_dialog(void)
{
  char errbuf[128];

  if(get_meta_list(errbuf, sizeof(errbuf))!=-1)  {
    return TRUE;
  } else {
    append_output_window(errbuf);
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
static void meta_update_callback(GtkWidget *w, gpointer data)
{
  update_meta_dialog();
}

/**************************************************************************
...
**************************************************************************/
static void meta_list_callback(GtkTreeSelection *select, GtkTreeModel *dummy)
{
  GtkTreeIter it;
  char *name, *port;

  if (!gtk_tree_selection_get_selected(select, NULL, &it))
    return;

  gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 0, &name, 1, &port, -1);

  gtk_entry_set_text(GTK_ENTRY(ihost), name);
  gtk_entry_set_text(GTK_ENTRY(iport), port);
}

/**************************************************************************
...
***************************************************************************/
static gboolean meta_click_callback(GtkWidget *w, GdkEventButton *event, gpointer data)
{
  if (event->type==GDK_2BUTTON_PRESS) connect_callback(w, data);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static void connect_destroy_callback(GtkWidget *w, gpointer data)
{
  dialog = NULL;
}

/**************************************************************************
...
**************************************************************************/
static void connect_command_callback(GtkWidget *w, gint response_id)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    connect_callback(w, NULL);
  } else {
    gtk_main_quit();
  }
}

/**************************************************************************
...
**************************************************************************/
void gui_server_connect(void)
{
  GtkWidget *label, *table, *book, *scrolled, *list, *vbox, *update;
  char buf [256];
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;

  dialog = gtk_dialog_new_with_buttons(_(" Connect to Freeciv Server"),
    GTK_WINDOW(toplevel),
    GTK_DIALOG_MODAL,
    GTK_STOCK_JUMP_TO,
    GTK_RESPONSE_ACCEPT,
    GTK_STOCK_QUIT,
    GTK_RESPONSE_REJECT,
    NULL);

  g_signal_connect(dialog, "destroy",
		   G_CALLBACK(connect_destroy_callback), NULL);
  g_signal_connect(dialog, "response",
		   G_CALLBACK(connect_command_callback), NULL);
  
  book = gtk_notebook_new ();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), book, TRUE, TRUE, 0);


  label=gtk_label_new(_("Freeciv Server Selection"));

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  table = gtk_table_new (4, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 5);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 0);

  label=gtk_label_new(_("Name:"));
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  iname=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iname), player_name);
  gtk_table_attach_defaults (GTK_TABLE (table), iname, 1, 2, 0, 1);

  label=gtk_label_new(_("Host:"));
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  ihost=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(ihost), server_host);
  gtk_table_attach_defaults (GTK_TABLE (table), ihost, 1, 2, 1, 2);

  label=gtk_label_new(_("Port:"));
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, 0, 0, 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  my_snprintf(buf, sizeof(buf), "%d", server_port);

  iport=gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(iport), buf);
  gtk_table_attach_defaults (GTK_TABLE (table), iport, 1, 2, 2, 3);

#if IS_BETA_VERSION
  {
    GtkWidget *label2;

    label2=gtk_label_new (beta_message());
    gtk_widget_modify_fg(label2, GTK_STATE_NORMAL,
	colors_standard[COLOR_STD_RED]);
    gtk_table_attach_defaults (GTK_TABLE (table), label2, 0, 2, 3, 4);
  }
#endif

  label=gtk_label_new(_("Metaserver"));

  vbox=gtk_vbox_new(FALSE, 2);
  gtk_notebook_append_page (GTK_NOTEBOOK (book), vbox, label);

  store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
  g_object_unref(store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(list));

  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Server Name"), renderer, "text", 0, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Port"), renderer, "text", 1, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Version"), renderer, "text", 2, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Status"), renderer, "text", 3, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Players"), renderer, "text", 4, NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list),
	-1, _("Comment"), renderer, "text", 5, NULL);

  scrolled=gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

  update=gtk_button_new_with_label(_("Update"));
  gtk_box_pack_start(GTK_BOX(vbox), update, FALSE, FALSE, 2);

  g_signal_connect(list, "button_press_event",
		   G_CALLBACK(meta_click_callback), NULL);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(meta_list_callback), NULL);
  g_signal_connect(update, "clicked",
		   G_CALLBACK(meta_update_callback), NULL);

  gtk_window_set_focus(GTK_WINDOW(dialog), iname);

  gtk_signal_connect(GTK_OBJECT(iname), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(ihost), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(iport), "activate",
        	      GTK_SIGNAL_FUNC(connect_callback), NULL);

  gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

  gtk_widget_set_usize(dialog, 450, 250);
  gtk_set_relative_position(toplevel, dialog, 50, 50);
  gtk_widget_show(dialog);
}

/**************************************************************************
  Get the list of servers from the metaserver
**************************************************************************/
static int get_meta_list(char *errbuf, int n_errbuf)
{
  struct server_list *server_list = create_server_list(errbuf, n_errbuf);
  gchar *row[6];

  if(!server_list) {
    return -1;
  }

  gtk_list_store_clear(store);

  server_list_iterate(*server_list, pserver) {
    GtkTreeIter it;
    int i;

    row[0] = g_locale_to_utf8(pserver->name, -1, NULL, NULL, NULL);
    row[1] = g_locale_to_utf8(pserver->port, -1, NULL, NULL, NULL);
    row[2] = g_locale_to_utf8(pserver->version,	-1, NULL, NULL, NULL);
    row[3] = g_locale_to_utf8(_(pserver->status), -1, NULL, NULL, NULL);
    row[4] = g_locale_to_utf8(pserver->players,	-1, NULL, NULL, NULL);
    row[5] = g_locale_to_utf8(pserver->metastring, -1, NULL, NULL, NULL);

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
		       0, row[0], 1, row[1], 2, row[2],
		       3, row[3], 4, row[4], 5, row[5], -1);

    for (i=0; i<6; i++) {
      g_free(row[i]);
    }
  }
  server_list_iterate_end;

  delete_server_list(server_list);
  return 0;
}

/**************************************************************************
  Make an attempt to autoconnect to the server.
  (server_autoconnect() gets GTK to call this function every so often.)
**************************************************************************/
static int try_to_autoconnect(gpointer data)
{
  char errbuf[512];
  static int count = 0;
  static int warning_shown = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, player_name, count);
    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(player_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! */
    return FALSE;		/*  Tells GTK not to call this
				   function again */
  case ECONNREFUSED:		/* Server not available (yet) */
    if (!warning_shown) {
      freelog(LOG_NORMAL, _("Connection to server refused. "
			    "Please start the server."));
      append_output_window(_("Connection to server refused. "
			     "Please start the server."));
      warning_shown = 1;
    }
    return TRUE;		/*  Tells GTK to keep calling this function */
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, errbuf);
    gtk_exit(EXIT_FAILURE);
    exit(EXIT_FAILURE);			/* Suppresses a gcc warning */
  }
}

/**************************************************************************
  Start trying to autoconnect to civserver.  Calls
  get_server_address(), then arranges for try_to_autoconnect(), which
  calls try_to_connect(), to be called roughly every
  AUTOCONNECT_INTERVAL milliseconds, until success, fatal error or
  user intervention.  (Doesn't use widgets, but is GTK-specific
  because it calls gtk_timeout_add().)
**************************************************************************/
void server_autoconnect()
{
  char buf[512];

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, player_name,
	      AUTOCONNECT_INTERVAL / 1000,AUTOCONNECT_INTERVAL % 1000, 
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, buf);
    gtk_exit(EXIT_FAILURE);
  }
  if (try_to_autoconnect(NULL)) {
    gtk_timeout_add(AUTOCONNECT_INTERVAL, try_to_autoconnect, NULL);
  }
}
