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
#include "city.h"
#include "game.h"
#include "tile.h"

/* client */
#include "client_main.h"
#include "goto.h"

/* client/gui-gtk-3.22 */
#include "gui_main.h"
#include "gui_stuff.h"

#include "rallypointdlg.h"

bool rally_dialog = FALSE;
static GtkWidget *instruction_label = NULL;
static GtkWidget *persistent;

static int rally_city_id = -1;

/************************************************************************//**
  Is rally point dialog currently open?
****************************************************************************/
static bool rally_dialog_open(void)
{
  return rally_dialog;
}

/************************************************************************//**
  Handle rally point dialog closing.
****************************************************************************/
static void rally_response_callback(GtkWidget *dlg, gint arg)
{
  rally_dialog = FALSE;
  instruction_label = NULL;

  gtk_widget_destroy(dlg);
}

/************************************************************************//**
  Open rally point placement dialog
****************************************************************************/
void rally_dialog_popup(void)
{
  GtkWidget *dlg;
  GtkWidget *main_box;
  GtkWidget *sep;

  if (rally_dialog_open()) {
    /* One rally point dialog already open. */
    return;
  }

  dlg = gtk_dialog_new_with_buttons(_("Place Rally Point"), NULL, 0,
                                    _("Close"), GTK_RESPONSE_NO,
                                    NULL);

  setup_dialog(dlg, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_NO);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);

  main_box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(main_box),
                                 GTK_ORIENTATION_VERTICAL);

  instruction_label = gtk_label_new(_("First click a city."));
  gtk_container_add(GTK_CONTAINER(main_box), instruction_label);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(main_box), sep);

  persistent = gtk_check_button_new_with_label(_("Persistent rallypoint"));
  gtk_container_add(GTK_CONTAINER(main_box), persistent);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(main_box), sep);

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                    main_box);

  g_signal_connect(dlg, "destroy", G_CALLBACK(rally_response_callback), NULL);
  g_signal_connect(dlg, "response", G_CALLBACK(rally_response_callback), NULL);

  gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dlg)));
  gtk_widget_show(dlg);

  rally_dialog = TRUE;
}

/************************************************************************//**
  Which rally point placement phace we are at the moment.
****************************************************************************/
enum rally_phase rally_placement_phase(void)
{
  if (!rally_dialog) {
    return RALLY_NONE;
  }

  if (rally_city_id > 0) {
    return RALLY_TILE;
  }

  return RALLY_CITY;
}

/************************************************************************//**
  Set city or tile for the infra placement.
  Returns whether the click was considered to be one for rally dialog.
****************************************************************************/
bool rally_set_tile(struct tile *ptile)
{
  struct city *pcity = NULL;
  enum rally_phase phase = rally_placement_phase();

  if (phase == RALLY_NONE) {
    return FALSE;
  }

  if (ptile == NULL) {
    return TRUE;
  }

  if (rally_city_id > 0) {
    pcity = game_city_by_number(rally_city_id);

    if (pcity == NULL || city_owner(pcity) != client_player()) {
      /* City destroyed or captured while we've
       * been setting the rally point? */
      rally_city_id = -1;
      phase = RALLY_CITY;

      gtk_label_set_text(GTK_LABEL(instruction_label),
                         _("Select another city."));
    }
  }

  if (phase == RALLY_CITY) {
    char buffer[100];

    pcity = tile_city(ptile);

    if (pcity == NULL || city_owner(pcity) != client_player()) {
      return TRUE;
    }

    rally_city_id = pcity->id;

    fc_snprintf(buffer, sizeof(buffer), _("Now select rally point for %s"),
                city_name_get(pcity));
    gtk_label_set_text(GTK_LABEL(instruction_label), buffer);
  } else {
    char buffer[100];
    bool psist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(persistent));

    fc_assert(pcity != NULL);

    rally_city_id = -1;

    if (send_rally_tile(pcity, ptile, psist)) {
      fc_snprintf(buffer, sizeof(buffer),
                  _("%s rally point set. Select another city."),
                  city_name_get(pcity));
    } else {
      fc_snprintf(buffer, sizeof(buffer),
                  _("%s rally point setting failed. Select next city."),
                  city_name_get(pcity));
    }

    gtk_label_set_text(GTK_LABEL(instruction_label), buffer);
  }

  return TRUE;
}
