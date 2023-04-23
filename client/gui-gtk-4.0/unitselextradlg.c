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
#include "movement.h"
#include "nation.h"
#include "unit.h"

/* client */
#include "control.h"
#include "tilespec.h"

/* client/gui-gtk-4.0 */
#include "gui_main.h"
#include "gui_stuff.h"
#include "sprite.h"

#include "unitselextradlg.h"

struct unit_sel_extra_cb_data {
  GtkWidget *dlg;
  int tp_id;
};

/***********************************************************************//**
  Get an extra selection list item suitable description of the specified
  extra at the specified tile.
***************************************************************************/
static const char *tgt_extra_descr(const struct extra_type *tgt_extra,
                                   const struct tile *tgt_tile)
{
  static char buf[248] = "";
  static char buf2[248] = "";

  if (tile_has_extra(tgt_tile, tgt_extra)) {
    if (extra_owner(tgt_tile)) {
      /* TRANS: nation adjective for extra owner used below if the target
       * tile has the target extra and it has an owner. */
      fc_snprintf(buf2, sizeof(buf2), Q_("?eowner:%s"),
                  nation_adjective_for_player(extra_owner(tgt_tile)));
    } else {
      /* TRANS: used below if the target tile has the target extra but it
       * doesn't have an owner. */
      sz_strlcpy(buf2, _("target"));
    }
  } else {
    /* TRANS: used below if the target tile doesn't have the target
     * extra (so it is assumed that it will be created). */
    sz_strlcpy(buf2, _("create"));
  }

  /* TRANS: extra name ... one of the above strings depending on if the
   * target extra currently exists at the target tile and if it has an
   * owner. */
  fc_snprintf(buf, sizeof(buf), _("%s\n(%s)"),
              extra_name_translation(tgt_extra), buf2);

  return buf;
}

/************************************************************************//**
  Callback to handle toggling of one of the target extra buttons.
****************************************************************************/
static void unit_sel_extra_toggled(GtkToggleButton *tb, gpointer userdata)
{
  struct unit_sel_extra_cb_data *cbdata
          = (struct unit_sel_extra_cb_data *)userdata;

  if (gtk_toggle_button_get_active(tb)) {
    g_object_set_data(G_OBJECT(cbdata->dlg), "target",
                      GINT_TO_POINTER(cbdata->tp_id));
  }
}

/************************************************************************//**
  Callback to handle destruction of one of the target extra buttons.
****************************************************************************/
static void unit_sel_extra_destroyed(GtkWidget *radio, gpointer userdata)
{
  free(userdata);
}

/************************************************************************//**
  Create a dialog where a unit select what extra to act on.
****************************************************************************/
bool select_tgt_extra(struct unit *actor, struct tile *ptile,
                      bv_extras potential_tgt_extras,
                      struct extra_type *suggested_tgt_extra,
                      const gchar *dlg_title,
                      const gchar *actor_label,
                      const gchar *tgt_label,
                      const gchar *do_label,
                      GCallback do_callback)
{
  GtkWidget *dlg;
  GtkWidget *main_box;
  GtkWidget *box, *grid;
  GtkWidget *icon;
  GtkWidget *lbl;
  GtkWidget *sep;
  GtkWidget *radio;
  GtkWidget *default_option = NULL;
  GtkWidget *first_option = NULL;
  struct sprite *spr;
  const struct unit_type *actor_type = unit_type_get(actor);
  int tcount;
  const struct extra_type *default_extra = NULL;
  int tuw = tileset_unit_width(tileset);
  int tuh = tileset_unit_height(tileset);
  int tew = tileset_tile_width(tileset);
  int teh = tileset_tile_height(tileset);

  dlg = gtk_dialog_new_with_buttons(dlg_title, NULL, 0,
                                    _("Close"), GTK_RESPONSE_NO,
                                    do_label, GTK_RESPONSE_YES,
                                    NULL);
  setup_dialog(dlg, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_NO);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);

  main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

  lbl = gtk_label_new(actor_label);
  gtk_box_append(GTK_BOX(box), lbl);

  spr = get_unittype_sprite(tileset, actor_type,
                            actor->activity, direction8_invalid());
  if (spr != NULL) {
    icon = gtk_image_new_from_pixbuf(sprite_get_pixbuf(spr));
  } else {
    icon = gtk_image_new();
  }
  gtk_widget_set_size_request(icon, tuw, tuh);
  gtk_box_append(GTK_BOX(box), icon);

  lbl = gtk_label_new(utype_name_translation(actor_type));
  gtk_box_append(GTK_BOX(box), lbl);

  gtk_box_append(GTK_BOX(main_box), box);

  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(main_box), sep);

  lbl = gtk_label_new(tgt_label);
  gtk_box_append(GTK_BOX(main_box), lbl);

  grid = gtk_grid_new();

  tcount = 0;
  extra_type_re_active_iterate(ptgt) {
    GdkPixbuf *tubuf;

    if (!BV_ISSET(potential_tgt_extras, extra_number(ptgt))) {
      continue;
    }

    struct unit_sel_extra_cb_data *cbdata
            = fc_malloc(sizeof(struct unit_sel_extra_cb_data));

    cbdata->tp_id = ptgt->id;
    cbdata->dlg = dlg;

    radio = gtk_check_button_new();
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio),
                               GTK_CHECK_BUTTON(first_option));
    if (first_option == NULL) {
      first_option = radio;
      default_option = first_option;
      default_extra = ptgt;
    }
    g_signal_connect(radio, "toggled",
                     G_CALLBACK(unit_sel_extra_toggled), cbdata);
    g_signal_connect(radio, "destroy",
                     G_CALLBACK(unit_sel_extra_destroyed), cbdata);
    if (ptgt == suggested_tgt_extra) {
      default_option = radio;
      default_extra = suggested_tgt_extra;
    }
    gtk_grid_attach(GTK_GRID(grid), radio, 0, tcount, 1, 1);

    tubuf = create_extra_pixbuf(ptgt);
    if (tubuf != NULL) {
      icon = gtk_image_new_from_pixbuf(tubuf);
      g_object_unref(tubuf);
    } else {
      icon = gtk_image_new();
    }
    gtk_widget_set_size_request(icon, tew, teh);
    gtk_grid_attach(GTK_GRID(grid), icon, 1, tcount, 1, 1);

    lbl = gtk_label_new(tgt_extra_descr(ptgt, ptile));
    gtk_grid_attach(GTK_GRID(grid), lbl, 2, tcount, 1, 1);

    tcount++;
  } extra_type_re_active_iterate_end;
  gtk_box_append(GTK_BOX(main_box), grid);

  fc_assert_ret_val(default_option, FALSE);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(default_option), TRUE);

  gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                 main_box);

  g_object_set_data(G_OBJECT(dlg), "actor", GINT_TO_POINTER(actor->id));
  g_object_set_data(G_OBJECT(dlg), "tile", ptile);

  /* This function should never be called so that there would be no extra
   * to select, and where there is extra to select, one of them gets
   * selected as the default. */
  fc_assert(default_extra != nullptr);
  if (default_extra != nullptr) { /* Compiler still wants this */
    g_object_set_data(G_OBJECT(dlg), "target",
                      GINT_TO_POINTER(default_extra->id));
  }

  g_signal_connect(dlg, "response", do_callback, actor);

  gtk_widget_set_visible(gtk_dialog_get_content_area(GTK_DIALOG(dlg)), TRUE);
  gtk_widget_set_visible(dlg, TRUE);

  return TRUE;
}
