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
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "game.h"
#include "map.h"
#include "packets.h"
#include "player.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "goto.h"
#include "options.h"

/* clien/gui-gtk-2.0 */
#include "plrdlg.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "gotodlg.h"


static GtkWidget *dshell;
static GtkWidget *view;
static GtkWidget *all_toggle;
static GtkListStore *store;
static GtkTreeSelection *selection;
struct tile *original_tile;

static void update_goto_dialog(GtkToggleButton *button);
static void goto_selection_callback(GtkTreeSelection *selection, gpointer data);

static struct city *get_selected_city(void);

enum {
  CMD_AIRLIFT = 1, CMD_GOTO
};

enum {
  GD_COL_CITY_ID = 0,   /* Not shown if not compiled with --enable-debug. */
  GD_COL_CITY_NAME,
  GD_COL_FLAG,
  GD_COL_NATION,

  GD_COL_NUM
};

/****************************************************************
...
*****************************************************************/
void popup_goto_dialog_action(void)
{
  popup_goto_dialog();
}

/**************************************************************************
...
**************************************************************************/
static void goto_cmd_callback(GtkWidget *dlg, gint arg)
{
  switch (arg) {
  case GTK_RESPONSE_CANCEL:
    center_tile_mapcanvas(original_tile);
    break;

  case CMD_AIRLIFT:
    {
      struct city *pdestcity = get_selected_city();

      if (pdestcity) {
        unit_list_iterate(get_units_in_focus(), punit) {
          if (base_unit_can_airlift_to(client_player(), punit, pdestcity)) {
            request_unit_airlift(punit, pdestcity);
          }
        } unit_list_iterate_end;
      }
    }
    break;

  case CMD_GOTO:
    {
      struct city *pdestcity = get_selected_city();

      if (pdestcity) {
	unit_list_iterate(get_units_in_focus(), punit) {
          send_goto_tile(punit, pdestcity->tile);
        } unit_list_iterate_end;
      }
    }
    break;

  default:
    break;
  }

  gtk_widget_destroy(dlg);
}


/**************************************************************************
...
**************************************************************************/
static void create_goto_dialog(void)
{
  GtkWidget *sw, *label, *vbox;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  dshell = gtk_dialog_new_with_buttons(_("Goto/Airlift Unit"),
    NULL,
    0,
    GTK_STOCK_CANCEL,
    GTK_RESPONSE_CANCEL,
    _("Air_lift"),
    CMD_AIRLIFT,
    _("_Goto"),
    CMD_GOTO,
    NULL);
  setup_dialog(dshell, toplevel);
  gtk_window_set_position(GTK_WINDOW(dshell), GTK_WIN_POS_MOUSE);
  gtk_dialog_set_default_response(GTK_DIALOG(dshell), CMD_GOTO);
  g_signal_connect(dshell, "destroy",
		   G_CALLBACK(gtk_widget_destroyed), &dshell);
  g_signal_connect(dshell, "response",
                   G_CALLBACK(goto_cmd_callback), NULL);

  label = gtk_frame_new(_("Select destination"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dshell)->vbox),
	label, TRUE, TRUE, 0);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(label), vbox);

  store = gtk_list_store_new(GD_COL_NUM, G_TYPE_INT, G_TYPE_STRING,
                             GDK_TYPE_PIXBUF, G_TYPE_STRING);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
    GD_COL_CITY_NAME, GTK_SORT_ASCENDING);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(view), GD_COL_CITY_NAME);
  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), TRUE);

#ifdef DEBUG
  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(_("Id"), rend,
    "text", GD_COL_CITY_ID, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  gtk_tree_view_column_set_sort_column_id(col, GD_COL_CITY_ID);
#endif

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(_("City"), rend,
    "text", GD_COL_CITY_NAME, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  gtk_tree_view_column_set_sort_column_id(col, GD_COL_CITY_NAME);

  rend = gtk_cell_renderer_pixbuf_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
    "pixbuf", GD_COL_FLAG, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(_("Nation"), rend,
    "text", GD_COL_NATION, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  gtk_tree_view_column_set_sort_column_id(col, GD_COL_NATION);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("Ci_ties:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

  all_toggle = gtk_check_button_new_with_mnemonic(_("Show _All Cities"));
  gtk_box_pack_start(GTK_BOX(vbox), all_toggle, FALSE, FALSE, 0);

  g_signal_connect(all_toggle, "toggled", G_CALLBACK(update_goto_dialog), NULL);

  g_signal_connect(selection, "changed",
    G_CALLBACK(goto_selection_callback), NULL);

  gtk_widget_show_all(GTK_DIALOG(dshell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(dshell)->action_area);


  original_tile = get_center_tile_mapcanvas();

  update_goto_dialog(GTK_TOGGLE_BUTTON(all_toggle));
  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************
popup the dialog
*****************************************************************/
void popup_goto_dialog(void)
{
  if (!can_client_issue_orders() || get_num_units_in_focus() == 0) {
    return;
  }

  if (!dshell) {
    create_goto_dialog();
  }

  gtk_window_present(GTK_WINDOW(dshell));
}

/**************************************************************************
...
**************************************************************************/
static struct city *get_selected_city(void)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  int city_id;

  if (!gtk_tree_selection_get_selected(selection, NULL, &it)) {
    return NULL;
  }

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  gtk_tree_model_get(model, &it, GD_COL_CITY_ID, &city_id, -1);
  return game_city_by_number(city_id);
}

/**************************************************************************
  Appends the list of the city owned by the player in the goto dialog.
**************************************************************************/
static void list_store_append_player_cities(GtkListStore *store,
                                            const struct player *pplayer)
{
  GtkTreeIter it;
  struct nation_type *pnation = nation_of_player(pplayer);
  const char *nation = nation_adjective_translation(pnation);
  GdkPixbuf *pixbuf = get_flag(pnation);

  city_list_iterate(pplayer->cities, pcity) {
    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
                       GD_COL_CITY_ID, pcity->id,
                       GD_COL_CITY_NAME, city_name(pcity),
                       GD_COL_FLAG, pixbuf,
                       GD_COL_NATION, nation,
                       -1);
  } city_list_iterate_end;
  g_object_unref(pixbuf);
}

/**************************************************************************
...
**************************************************************************/
static void update_goto_dialog(GtkToggleButton *button)
{
  gtk_list_store_clear(store);

  if (!client_has_player()) {
    /* Case global observer. */
    return;
  }

  if (gtk_toggle_button_get_active(button)) {
    players_iterate(pplayer) {
      list_store_append_player_cities(store, pplayer);
    } players_iterate_end;
  } else {
    list_store_append_player_cities(store, client_player());
  }
}

/**************************************************************************
...
**************************************************************************/
static void goto_selection_callback(GtkTreeSelection *selection,
                                    gpointer data)
{
  struct city *pdestcity = get_selected_city();

  if (NULL != pdestcity) {
    bool can_airlift = FALSE;

    center_tile_mapcanvas(city_tile(pdestcity));

    unit_list_iterate(get_units_in_focus(), punit) {
      if (base_unit_can_airlift_to(client_player(), punit, pdestcity)) {
        can_airlift = TRUE;
        break;
      }
    } unit_list_iterate_end;

    if (can_airlift) {
      gtk_dialog_set_response_sensitive(GTK_DIALOG(dshell),
                                        CMD_AIRLIFT, TRUE);
      return;
    }
  }
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dshell), CMD_AIRLIFT, FALSE);
}
