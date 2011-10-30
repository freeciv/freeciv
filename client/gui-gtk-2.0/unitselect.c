/*****************************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"

/* common */
#include "game.h"
#include "player.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "goto.h"
#include "tilespec.h"

/* client/gui-gtk-2.0 */
#include "graphics.h"
#include "gui_stuff.h"
#include "gui_main.h"

#include "unitselect.h"


#define NUM_UNIT_SELECT_COLUMNS 2

#define SELECT_UNIT_READY  1
#define SELECT_UNIT_SENTRY 2
#define SELECT_UNIT_ALL    3

static GtkWidget *unit_select_dialog_shell;
static GtkTreeStore *unit_select_store;
static GtkWidget *unit_select_view;
static GtkTreePath *unit_select_path;
static struct tile *unit_select_ptile;


/*****************************************************************************
  Row from unit select dialog activated
*****************************************************************************/
static void unit_select_row_activated(GtkTreeView *view, GtkTreePath *path)
{
  GtkTreeIter it;
  struct unit *punit;
  gint id;

  gtk_tree_model_get_iter(GTK_TREE_MODEL(unit_select_store), &it, path);
  gtk_tree_model_get(GTK_TREE_MODEL(unit_select_store), &it, 0, &id, -1);
 
  if ((punit = player_unit_by_number(client_player(), id))) {
    unit_focus_set(punit);
  }

  gtk_widget_destroy(unit_select_dialog_shell);
}

/*****************************************************************************
  Add unit to unit select dialog
*****************************************************************************/
static void unit_select_append(struct unit *punit, GtkTreeIter *it,
                               GtkTreeIter *parent)
{
  GdkPixbuf *pix;
  char buf[512];

  pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
      tileset_full_tile_width(tileset), tileset_full_tile_height(tileset));

  {
    struct canvas canvas_store;

    canvas_store.type = CANVAS_PIXBUF;
    canvas_store.v.pixbuf = pix;

    gdk_pixbuf_fill(pix, 0x00000000);
    put_unit(punit, &canvas_store, 0, 0);
  }

  {
    struct city *phome = game_city_by_number(punit->homecity);
#ifdef DEBUG
    fc_snprintf(buf, sizeof(buf), "%s [Unit ID %d]\n(%s)",
                unit_name_translation(punit), punit->id,
                phome == NULL ? "no home city" : city_name(phome));
#else  /* DEBUG */
    fc_snprintf(buf, sizeof(buf), "%s\n(%s)", unit_name_translation(punit),
                phome == NULL ? "no home city" : city_name(phome));
#endif /* DEBUG */
  }

  gtk_tree_store_append(unit_select_store, it, parent);
  gtk_tree_store_set(unit_select_store, it,
      0, punit->id,
      1, pix,
      2, buf,
      -1);
  g_object_unref(pix);

  if (unit_is_in_focus(punit)) {
    unit_select_path =
      gtk_tree_model_get_path(GTK_TREE_MODEL(unit_select_store), it);
  }
}

/*****************************************************************************
  Recursively select units that transport already selected units, starting
  from root_id unit.
*****************************************************************************/
static void unit_select_recurse(const struct unit *ptrans,
                                GtkTreeIter *it_root)
{
  unit_list_iterate(unit_select_ptile->units, pleaf) {
    GtkTreeIter it_leaf;

    if (unit_transport_get(pleaf) == ptrans) {
      unit_select_append(pleaf, &it_leaf, it_root);
      if (get_transporter_occupancy(pleaf) > 0) {
        unit_select_recurse(pleaf, &it_leaf);
      }
    }
  } unit_list_iterate_end;
}

/*****************************************************************************
  Refresh unit selection dialog
*****************************************************************************/
static void unit_select_dialog_refresh(void)
{
  if (unit_select_dialog_shell) {
    gtk_tree_store_clear(unit_select_store);

    unit_select_recurse(NULL, NULL);
    gtk_tree_view_expand_all(GTK_TREE_VIEW(unit_select_view));

    if (unit_select_path) {
      gtk_tree_view_set_cursor(GTK_TREE_VIEW(unit_select_view),
          unit_select_path, NULL, FALSE);
      gtk_tree_path_free(unit_select_path);
      unit_select_path = NULL;
    }
  }
}

/*****************************************************************************
  Unit selection dialog being destroyed
*****************************************************************************/
static void unit_select_destroy_callback(GtkObject *object, gpointer data)
{
  unit_select_dialog_shell = NULL;
}

/*****************************************************************************
  User responded to unit selection dialog
*****************************************************************************/
static void unit_select_cmd_callback(GtkWidget *w, gint rid, gpointer data)
{
  struct tile *ptile = unit_select_ptile;

  switch (rid) {
  case SELECT_UNIT_READY:
    {
      struct unit *pmyunit = NULL;

      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) == client.conn.playing) {
          pmyunit = punit;

          /* Activate this unit. */
          punit->client.focus_status = FOCUS_AVAIL;
          if (unit_has_orders(punit)) {
            request_orders_cleared(punit);
          }
          if (punit->activity != ACTIVITY_IDLE || punit->ai_controlled) {
            punit->ai_controlled = FALSE;
            request_new_unit_activity(punit, ACTIVITY_IDLE);
          }
        }
      } unit_list_iterate_end;

      if (pmyunit) {
        /* Put the focus on one of the activated units. */
        unit_focus_set(pmyunit);
      }
    }
    break;

  case SELECT_UNIT_SENTRY:
    {
      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) == client.conn.playing) {
          if ((punit->activity == ACTIVITY_IDLE) &&
              !punit->ai_controlled &&
              can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
            request_new_unit_activity(punit, ACTIVITY_SENTRY);
          }
        }
      } unit_list_iterate_end;
    }
    break;

  case SELECT_UNIT_ALL:
    {
      unit_list_iterate(ptile->units, punit) {
        if (unit_owner(punit) == client.conn.playing) {
          if (punit->activity == ACTIVITY_IDLE &&
              !punit->ai_controlled) {
            /* Give focus to it */
            unit_focus_add(punit);
          }
        }
      } unit_list_iterate_end;
    }
    break;

  default:
    break;
  }
  
  gtk_widget_destroy(unit_select_dialog_shell);
}

/*****************************************************************************
  Popup unit select dialog
*****************************************************************************/
void unit_select_dialog_popup_main(struct tile *ptile)
{
  /* First check for a valid tile. */
  if (ptile == NULL) {
    struct unit *punit = head_of_units_in_focus();
    if (punit == NULL) {
      log_error("No unit in focus - no data to create the dialog!");
      return;
    }
    unit_select_ptile = unit_tile(punit);
  } else {
    unit_select_ptile = ptile;
  }

  if (!unit_select_dialog_shell) {
    GtkTreeStore *store;
    GtkWidget *shell, *view, *sw, *hbox;
    GtkWidget *ready_cmd, *sentry_cmd, *select_all_cmd;

    static const char *titles[NUM_UNIT_SELECT_COLUMNS] = {
      N_("Unit"),
      N_("Name")
    };
    static bool titles_done;

    GType types[NUM_UNIT_SELECT_COLUMNS+1] = {
      G_TYPE_INT,
      GDK_TYPE_PIXBUF,
      G_TYPE_STRING
    };
    int i;


    shell = gtk_dialog_new_with_buttons(_("Unit selection"),
      NULL,
      0,
      NULL);
    unit_select_dialog_shell = shell;
    setup_dialog(shell, toplevel);
    g_signal_connect(shell, "destroy",
      G_CALLBACK(unit_select_destroy_callback), NULL);
    gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
    g_signal_connect(shell, "response",
      G_CALLBACK(unit_select_cmd_callback), NULL);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), hbox);

    intl_slist(ARRAY_SIZE(titles), titles, &titles_done);

    store = gtk_tree_store_newv(ARRAY_SIZE(types), types);
    unit_select_store = store;

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    unit_select_view = view;
    g_object_unref(store);
 
    for (i = 1; i < ARRAY_SIZE(types); i++) {
      GtkTreeViewColumn *column;
      GtkCellRenderer *render;

      column = gtk_tree_view_column_new();
      gtk_tree_view_column_set_title(column, titles[i-1]);

      switch (types[i]) {
        case G_TYPE_STRING:
          render = gtk_cell_renderer_text_new();
          gtk_tree_view_column_pack_start(column, render, TRUE);
          gtk_tree_view_column_set_attributes(column, render, "text", i,
                                              NULL);
          break;
        default:
          render = gtk_cell_renderer_pixbuf_new();
          gtk_tree_view_column_pack_start(column, render, FALSE);
          gtk_tree_view_column_set_attributes(column, render,
              "pixbuf", i, NULL);
          break;
      }
      gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    }

    g_signal_connect(view, "row_activated",
        G_CALLBACK(unit_select_row_activated), NULL);


    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 300);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
        GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), view);
    gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);


    ready_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      _("_Ready all"), SELECT_UNIT_READY);

    gtk_button_box_set_child_secondary(
      GTK_BUTTON_BOX(GTK_DIALOG(shell)->action_area),
      ready_cmd, TRUE);

    sentry_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      _("_Sentry idle"), SELECT_UNIT_SENTRY);

    gtk_button_box_set_child_secondary(
      GTK_BUTTON_BOX(GTK_DIALOG(shell)->action_area),
      sentry_cmd, TRUE);

    select_all_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      _("Select _all"), SELECT_UNIT_ALL);

    gtk_button_box_set_child_secondary(
      GTK_BUTTON_BOX(GTK_DIALOG(shell)->action_area),
      select_all_cmd, TRUE);

    gtk_dialog_add_button(GTK_DIALOG(shell),
      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

    gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CLOSE);

    gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
    gtk_widget_show_all(GTK_DIALOG(shell)->action_area);
  }

  unit_select_dialog_refresh();

  gtk_window_present(GTK_WINDOW(unit_select_dialog_shell));
}

/*****************************************************************************
   Closing unit selection dialog
*****************************************************************************/
void unit_select_dialog_popdown(void)
{
  /* Nothing at the moment. */
}
