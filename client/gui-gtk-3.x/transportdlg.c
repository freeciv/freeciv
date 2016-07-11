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
#include "game.h"
#include "movement.h"
#include "unit.h"

/* client */
#include "control.h"
#include "tilespec.h"

/* client/gui-gtk-3.x */
#include "gui_main.h"
#include "gui_stuff.h"
#include "sprite.h"

#include "transportdlg.h"

struct transport_radio_cb_data {
  GtkWidget *dlg;
  int tp_id;
};

/****************************************************************
  Handle user response to transport dialog.
*****************************************************************/
static void transport_response_callback(GtkWidget *dlg, gint arg)
{
  if (arg == GTK_RESPONSE_YES) {
    struct unit *pcargo =
      game_unit_by_number(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg), "cargo")));

    if (pcargo != NULL) {
      int tp_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dlg), "transport"));
      struct tile *ptile = g_object_get_data(G_OBJECT(dlg), "tile");

      if (tp_id == 0) {
        /* Load to any */
        request_unit_load(pcargo, NULL, ptile);
      } else {
        struct unit *ptransport = game_unit_by_number(tp_id);

        if (ptransport != NULL) {
          /* Still exist */
          request_unit_load(pcargo, ptransport, ptile);
        }
      }
    }
  }

  gtk_widget_destroy(dlg);
}

/****************************************************************************
  Callback to handle toggling of one of the transporter buttons.
****************************************************************************/
static void transport_radio_toggled(GtkToggleButton *tb,
                                    gpointer userdata)
{
  struct transport_radio_cb_data *cbdata = (struct transport_radio_cb_data *)userdata;

  if (gtk_toggle_button_get_active(tb)) {
    g_object_set_data(G_OBJECT(cbdata->dlg), "transport",
                      GINT_TO_POINTER(cbdata->tp_id));
  }
}

/****************************************************************************
  Callback to handle destruction of one of the transporter buttons.
****************************************************************************/
static void transport_radio_destroyed(GtkWidget *radio,
                                      gpointer userdata)
{
  free(userdata);
}

/****************************************************************************
  Handle transport request automatically when there's nothing to
  choose from. Otherwise open up transport dialog for the unit
****************************************************************************/
bool request_transport(struct unit *cargo, struct tile *ptile)
{
  GtkWidget *dlg;
  GtkWidget *main_box;
  GtkWidget *box;
  GtkWidget *icon;
  GtkWidget *lbl;
  GtkWidget *sep;
  GtkWidget *radio;
  GSList *radio_group = NULL;
  struct sprite *spr;
  struct unit_type *cargo_type = unit_type_get(cargo);
  int tcount;
  struct unit_list *potential_transports = unit_list_new();

  unit_list_iterate(ptile->units, ptransport) {
    if (can_unit_transport(ptransport, cargo)
        && get_transporter_occupancy(ptransport) < get_transporter_capacity(ptransport)) {
      unit_list_append(potential_transports, ptransport);
    }
  } unit_list_iterate_end;

  tcount = unit_list_size(potential_transports);

  if (tcount == 0) {
    unit_list_destroy(potential_transports);

    return FALSE; /* Unit was not handled here. */
  } else if (tcount == 1) {
    /* There's exactly one potential transport - use it automatically */
    request_unit_load(cargo, unit_list_get(potential_transports, 0), ptile);

    unit_list_destroy(potential_transports);

    return TRUE;
  }

  dlg = gtk_dialog_new_with_buttons(_("Transport selection"),
                                    NULL, 0,
                                    _("Load"), GTK_RESPONSE_YES,
                                    _("Close"), GTK_RESPONSE_NO,
                                    NULL);
  setup_dialog(dlg, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_NO);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);

  main_box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(main_box),
                                 GTK_ORIENTATION_VERTICAL);
  box = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(box),
                                 GTK_ORIENTATION_HORIZONTAL);

  lbl = gtk_label_new(_("Looking for transport:"));
  gtk_grid_attach(GTK_GRID(box), lbl, 0, 0, 1, 1);

  spr = get_unittype_sprite(tileset, cargo_type, direction8_invalid());
  if (spr != NULL) {
    icon = gtk_image_new_from_pixbuf(sprite_get_pixbuf(spr));
  } else {
    icon = gtk_image_new();
  }
  gtk_grid_attach(GTK_GRID(box), icon, 1, 0, 1, 1);

  lbl = gtk_label_new(utype_name_translation(cargo_type));
  gtk_grid_attach(GTK_GRID(box), lbl, 2, 0, 1, 1);

  gtk_container_add(GTK_CONTAINER(main_box), box);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(main_box), sep);

  lbl = gtk_label_new(_("Transports available:"));
  gtk_container_add(GTK_CONTAINER(main_box), lbl);

  box = gtk_grid_new();

  tcount = 0;
  unit_list_iterate(potential_transports, ptransport) {
    struct unit_type *trans_type = unit_type_get(ptransport);
    struct transport_radio_cb_data *cbdata = fc_malloc(sizeof(struct transport_radio_cb_data));

    cbdata->tp_id = ptransport->id;
    cbdata->dlg = dlg;

    radio = gtk_radio_button_new(radio_group);
    if (radio_group == NULL) {
      radio_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
      transport_radio_toggled(GTK_TOGGLE_BUTTON(radio), cbdata);
    }
    g_signal_connect(radio, "toggled",
                     G_CALLBACK(transport_radio_toggled), cbdata);
    g_signal_connect(radio, "destroy",
                     G_CALLBACK(transport_radio_destroyed), cbdata);
    gtk_grid_attach(GTK_GRID(box), radio, 0, tcount, 1, 1);

    spr = get_unittype_sprite(tileset, trans_type, direction8_invalid());
    if (spr != NULL) {
      icon = gtk_image_new_from_pixbuf(sprite_get_pixbuf(spr));
    } else {
      icon = gtk_image_new();
    }
    gtk_grid_attach(GTK_GRID(box), icon, 1, tcount, 1, 1);
    
    lbl = gtk_label_new(utype_name_translation(trans_type));
    gtk_grid_attach(GTK_GRID(box), lbl, 2, tcount, 1, 1);

    tcount++;
  } unit_list_iterate_end;
  gtk_container_add(GTK_CONTAINER(main_box), box);

  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), main_box);

  g_object_set_data(G_OBJECT(dlg), "cargo", GINT_TO_POINTER(cargo->id));
  g_object_set_data(G_OBJECT(dlg), "tile", ptile);

  g_signal_connect(dlg, "response",
                   G_CALLBACK(transport_response_callback), cargo);

  gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dlg)));
  gtk_widget_show(dlg);

  return TRUE;
}
