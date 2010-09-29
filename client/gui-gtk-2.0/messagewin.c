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

/* utility */
#include "fcintl.h"
#include "log.h"

/* common */
#include "events.h"
#include "game.h"
#include "map.h"
#include "player.h"

/* client */
#include "options.h"

/* client/gui-gtk-2.0 */
#include "chatline.h"
#include "citydlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "messagewin.h"


struct meswin_dialog {
  struct gui_dialog *shell;
  GtkListStore *store;
};

/* Those values must match meswin_dialog_store_new(). */
enum meswin_columns {
  MESWIN_COL_MESSAGE,

  /* Not visible. */
  MESWIN_COL_WEIGHT,
  MESWIN_COL_STYLE,
  MESWIN_COL_ID,

  MESWIN_COL_NUM
};

enum meswin_responses {
  MESWIN_RES_GOTO = 1,
  MESWIN_RES_POPUP_CITY
};

static struct meswin_dialog meswin = { NULL, NULL };

/****************************************************************************
  Create a tree model for the message window.
****************************************************************************/
static GtkListStore *meswin_dialog_store_new(void)
{
  return gtk_list_store_new(MESWIN_COL_NUM,
                            G_TYPE_STRING,      /* MESWIN_COL_MESSAGE */
                            G_TYPE_INT,         /* MESWIN_COL_WEIGHT */
                            G_TYPE_INT,         /* MESWIN_COL_STYLE */
                            G_TYPE_INT);        /* MESWIN_COL_ID */
}

/****************************************************************************
  Get the pango attributes for the visited state.
****************************************************************************/
static void meswin_dialog_visited_get_attr(bool visited, gint *weight,
                                           gint *style)
{
  if (NULL != weight) {
    *weight = (visited ? PANGO_WEIGHT_NORMAL : PANGO_WEIGHT_BOLD);
  }
  if (NULL != style) {
    *style = (visited ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
  }
}

/****************************************************************************
  Set the visited state of the store.
****************************************************************************/
static void meswin_dialog_set_visited(GtkTreeModel *model,
                                      GtkTreeIter *iter, bool visited)
{
  gint row, weight, style;

  gtk_tree_model_get(model, iter, MESWIN_COL_ID, &row, -1);
  meswin_dialog_visited_get_attr(visited, &weight, &style);
  gtk_list_store_set(GTK_LIST_STORE(model), iter,
                     MESWIN_COL_WEIGHT, weight,
                     MESWIN_COL_STYLE, style,
                     -1);
  meswin_set_visited_state(row, visited);
}

/****************************************************************************
  Refresh a message window dialog.
****************************************************************************/
static void meswin_dialog_refresh(struct meswin_dialog *pdialog)
{
  GtkListStore *store;
  GtkTreeIter iter;
  const struct message *pmsg;
  gint weight, style;
  int i, num;
  bool need_alert = FALSE;

  fc_assert_ret(NULL != pdialog);

  store = meswin_dialog_store_new();
  num = meswin_get_num_messages();

  for (i = 0; i < num; i++) {
    pmsg = meswin_get_message(i);

    if (gui_gtk2_new_messages_go_to_top) {
      gtk_list_store_prepend(store, &iter);
    } else {
      gtk_list_store_append(store, &iter);
    }

    meswin_dialog_visited_get_attr(pmsg->visited, &weight, &style);
    gtk_list_store_set(store, &iter,
                       MESWIN_COL_MESSAGE, pmsg->descr,
                       MESWIN_COL_WEIGHT, weight,
                       MESWIN_COL_STYLE, style,
                       MESWIN_COL_ID, i,
                       -1);

    if (!pmsg->visited) {
      need_alert = TRUE;
    }
  }

  merge_list_stores(pdialog->store, store, MESWIN_COL_ID);
  g_object_unref(G_OBJECT(store));

  if (need_alert) {
    gui_dialog_alert(pdialog->shell);
  }
}

/**************************************************************************
  Selection changed callback.
**************************************************************************/
static void meswin_dialog_selection_callback(GtkTreeSelection *selection,
                                             gpointer data)
{
  struct gui_dialog *pdialog = data;
  const struct message *pmsg;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gint row;

  if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
    return;
  }

  gtk_tree_model_get(model, &iter, MESWIN_COL_ID, &row, -1);
  pmsg = meswin_get_message(row);

  gui_dialog_set_response_sensitive(pdialog, MESWIN_RES_GOTO,
                                    NULL != pmsg && pmsg->location_ok);
  gui_dialog_set_response_sensitive(pdialog, MESWIN_RES_POPUP_CITY,
                                    NULL != pmsg && pmsg->city_ok);
}

/**************************************************************************
  A row has been activated by the user.
**************************************************************************/
static void meswin_dialog_row_activated_callback(GtkTreeView *view,
                                                 GtkTreePath *path,
                                                 GtkTreeViewColumn *col,
                                                 gpointer data)
{
  GtkTreeModel *model = gtk_tree_view_get_model(view);
  GtkTreeIter iter;
  gint row;

  if (!gtk_tree_model_get_iter(model, &iter, path)) {
    return;
  }

  gtk_tree_model_get(model, &iter, MESWIN_COL_ID, &row, -1);

  if (NULL != meswin_get_message(row)) {
    meswin_double_click(row);
    meswin_dialog_set_visited(model, &iter, TRUE);
  }
}

/****************************************************************************
  Mouse button press handler for the message window treeview. We only
  care about right clicks on a row; this action centers on the tile
  associated with the event at that row (if applicable).
****************************************************************************/
static gboolean meswin_dialog_button_press_callback(GtkWidget *widget,
                                                    GdkEventButton *ev,
                                                    gpointer data)
{
  GtkTreePath *path = NULL;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gint row;

  fc_assert_ret_val(GTK_IS_TREE_VIEW(widget), FALSE);

  if (GDK_BUTTON_PRESS  != ev->type || 3 != ev->button) {
    return FALSE;
  }

  if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), ev->x, ev->y,
                                     &path, NULL, NULL, NULL)) {
    return TRUE;
  }

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
  if (gtk_tree_model_get_iter(model, &iter, path)) {
    gtk_tree_model_get(model, &iter, MESWIN_COL_ID, &row, -1);
    meswin_goto(row);
  }
  gtk_tree_path_free(path);

  return TRUE;
}

/**************************************************************************
  Dialog response callback.
**************************************************************************/
static void meswin_dialog_response_callback(struct gui_dialog *pdialog,
                                            int response, gpointer data)
{
  GtkTreeSelection *selection = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gint row;

  switch (response) {
  case MESWIN_RES_GOTO:
  case MESWIN_RES_POPUP_CITY:
    break;
  default:
    gui_dialog_destroy(pdialog);
    return;
  }

  selection = GTK_TREE_SELECTION(data);
  if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
    return;
  }

  gtk_tree_model_get(model, &iter, MESWIN_COL_ID, &row, -1);

  switch (response) {
  case MESWIN_RES_GOTO:
    meswin_goto(row);
    break;
  case MESWIN_RES_POPUP_CITY:
    meswin_popup_city(row);
    break;
  }
  meswin_dialog_set_visited(model, &iter, TRUE);
}

/****************************************************************************
  Initilialize a message window dialog.
****************************************************************************/
static void meswin_dialog_init(struct meswin_dialog *pdialog)
{
  GtkWidget *view, *sw, *cmd, *notebook;
  GtkBox *vbox;
  GtkListStore *store;
  GtkTreeSelection *selection;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *col;

  fc_assert_ret(NULL != pdialog);

  if (gui_gtk2_message_chat_location == GUI_GTK2_MSGCHAT_SPLIT) {
    notebook = right_notebook;
  } else {
    notebook = bottom_notebook;
  }

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

  store = meswin_dialog_store_new();
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  pdialog->store = store;

  renderer = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                 "text", MESWIN_COL_MESSAGE,
                                                 "weight", MESWIN_COL_WEIGHT,
                                                 "style", MESWIN_COL_STYLE,
                                                 NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  gtk_container_add(GTK_CONTAINER(sw), view);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gui_dialog_new(&pdialog->shell, GTK_NOTEBOOK(notebook), selection);
  gui_dialog_set_title(pdialog->shell, _("Messages"));
  vbox = GTK_BOX(pdialog->shell->vbox);
  gtk_box_pack_start(vbox, sw, TRUE, TRUE, 0);

  g_signal_connect(selection, "changed",
                   G_CALLBACK(meswin_dialog_selection_callback),
                   pdialog->shell);
  g_signal_connect(view, "row_activated",
                   G_CALLBACK(meswin_dialog_row_activated_callback), NULL);
  g_signal_connect(view, "button-press-event",
                   G_CALLBACK(meswin_dialog_button_press_callback), NULL);

  if (gui_gtk2_show_message_window_buttons) {
    cmd = gui_dialog_add_stockbutton(pdialog->shell, GTK_STOCK_JUMP_TO,
                                     _("Goto _Location"), MESWIN_RES_GOTO);
    gtk_widget_set_sensitive(cmd, FALSE);

    cmd = gui_dialog_add_stockbutton(pdialog->shell, GTK_STOCK_ZOOM_IN,
                                     _("Inspect _City"),
                                     MESWIN_RES_POPUP_CITY);
    gtk_widget_set_sensitive(cmd, FALSE);
  }

  gui_dialog_add_button(pdialog->shell, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
  gui_dialog_response_set_callback(pdialog->shell,
                                   meswin_dialog_response_callback);
  gui_dialog_set_default_size(pdialog->shell, 520, 300);

  meswin_dialog_refresh(pdialog);
  gui_dialog_show_all(pdialog->shell);
}

/****************************************************************************
  Closes a message window dialog.
****************************************************************************/
static void meswin_dialog_free(struct meswin_dialog *pdialog)
{
  fc_assert_ret(NULL != pdialog);

  gui_dialog_destroy(pdialog->shell);
  fc_assert(NULL == pdialog->shell);
  pdialog->store = NULL;
}

/****************************************************************************
  Popup the dialog inside the main-window, and optionally raise it.
****************************************************************************/
void meswin_dialog_popup(bool raise)
{
  if (NULL == meswin.shell) {
    meswin_dialog_init(&meswin);
  }

  gui_dialog_present(meswin.shell);
  if (raise) {
    gui_dialog_raise(meswin.shell);
  }
}

/****************************************************************************
  Closes the message window dialog.
****************************************************************************/
void meswin_dialog_popdown(void)
{
  if (NULL != meswin.shell) {
    meswin_dialog_free(&meswin);
  }
}

/****************************************************************************
  Return TRUE iff the message window is open.
****************************************************************************/
bool meswin_dialog_is_open(void)
{
  return (NULL != meswin.shell);
}

/****************************************************************************
  Update the message window dialog.
****************************************************************************/
void real_meswin_dialog_update(void)
{
  if (NULL != meswin.shell) {
    meswin_dialog_refresh(&meswin);
  }
}
