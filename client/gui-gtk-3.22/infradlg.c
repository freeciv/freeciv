/***********************************************************************
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
#include <fc_config.h>
#endif

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"

/* common */
#include "extras.h"
#include "game.h"

/* client */
#include "client_main.h"
#include "dialogs_g.h"
#include "mapview_common.h"

/* client/gui-gtk-3.22 */
#include "gui_main.h"
#include "gui_stuff.h"

#include "infradlg.h"

static GtkWidget *infra_list_grid = NULL;
static GtkWidget *instruction_label = NULL;
static GtkWidget *points_label = NULL;
static int infra_rows = 0;

struct infra_cb_data {
  struct tile *ptile;
  struct extra_type *pextra;
};

/************************************************************************//**
  Is infra dialog currently open?
****************************************************************************/
static bool infra_dialog_open(void)
{
  return infra_list_grid != NULL;
}

/************************************************************************//**
  Handle infra dialog closing.
****************************************************************************/
static void infra_response_callback(GtkWidget *dlg, gint arg)
{
  infra_list_grid = NULL;
  instruction_label = NULL;
  points_label = NULL;
  infra_rows = 0;

  client_infratile_set(NULL);

  gtk_widget_destroy(dlg);
}

/************************************************************************//**
  Handle user infra selection.
****************************************************************************/
static void infra_selected_callback(GtkButton *but, gpointer userdata)
{
  struct infra_cb_data *cbdata = (struct infra_cb_data *)userdata;

  dsend_packet_player_place_infra(&client.conn, cbdata->ptile->index,
                                  cbdata->pextra->id);
}

/************************************************************************//**
  Open infra placement dialog
****************************************************************************/
void infra_dialog_popup(void)
{
  GtkWidget *dlg;
  GtkWidget *main_box;
  GtkWidget *sep;

  if (infra_dialog_open()) {
    /* One infra dialog already open. */
    return;
  }

  dlg = gtk_dialog_new_with_buttons(_("Place infrastructure"), NULL, 0,
                                    _("Close"), GTK_RESPONSE_NO,
                                    NULL);

  setup_dialog(dlg, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_NO);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);

  main_box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(main_box),
                                 GTK_ORIENTATION_VERTICAL);

  instruction_label = gtk_label_new(_("First click a tile."));
  gtk_container_add(GTK_CONTAINER(main_box), instruction_label);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(main_box), sep);

  points_label = gtk_label_new(_("- infrapoints"));
  gtk_container_add(GTK_CONTAINER(main_box), points_label);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(main_box), sep);

  infra_list_grid = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(main_box), infra_list_grid);

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                    main_box);

  g_signal_connect(dlg, "destroy", G_CALLBACK(infra_response_callback), NULL);
  g_signal_connect(dlg, "response", G_CALLBACK(infra_response_callback), NULL);

  gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dlg)));
  gtk_widget_show(dlg);

  update_infra_dialog();
}

/************************************************************************//**
  Refresh infra dialog
****************************************************************************/
void update_infra_dialog(void)
{
  if (infra_dialog_open()) {
    char buffer[100];

    fc_snprintf(buffer, sizeof(buffer),
                PL_("%d infrapoint", "%d infrapoints",
                    client.conn.playing->economic.infra_points),
                client.conn.playing->economic.infra_points);

    gtk_label_set_text(GTK_LABEL(points_label), buffer);
  }
}

/************************************************************************//**
  Are we in infra placement mode at the moment?
****************************************************************************/
bool infra_placement_mode(void)
{
  return infra_list_grid != NULL;
}

/************************************************************************//**
  Set tile for the infra placement.
****************************************************************************/
void infra_placement_set_tile(struct tile *ptile)
{
  while (infra_rows > 0) {
    gtk_grid_remove_row(GTK_GRID(infra_list_grid), --infra_rows);
  }

  if (!client_map_is_known_and_seen(ptile, client.conn.playing, V_MAIN)) {
    return;
  }

  client_infratile_set(ptile);

  extra_type_iterate(pextra) {
    if (player_can_place_extra(pextra, client.conn.playing, ptile)) {
      GtkWidget *but = gtk_button_new_with_label(extra_name_translation(pextra));
      struct infra_cb_data *cbdata = fc_malloc(sizeof(struct infra_cb_data));

      cbdata->ptile = ptile;
      cbdata->pextra = pextra;

      g_signal_connect(but, "clicked",
                       G_CALLBACK(infra_selected_callback), cbdata);
      gtk_grid_attach(GTK_GRID(infra_list_grid), but, 1, infra_rows++, 1, 1);
    }
  } extra_type_iterate_end;

  if (infra_rows <= 0) {
    gtk_label_set_text(GTK_LABEL(instruction_label),
                       _("No infra possible. Select another tile."));
  } else {
    gtk_label_set_text(GTK_LABEL(instruction_label),
                       _("Select infra for the tile, or another tile."));
  }

  gtk_widget_show_all(infra_list_grid);
}
