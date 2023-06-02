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

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "city.h"
#include "packets.h"
#include "worklist.h"

/* client */
#include "citydlg_common.h"
#include "client_main.h"
#include "climisc.h"
#include "global_worklist.h"
#include "options.h"
#include "text.h"
#include "tilespec.h"

/* client/gui-gtk-4.0 */
#include "canvas.h"
#include "citydlg.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"

#include "wldlg.h"

static GtkWidget *worklists_shell;
static GtkWidget *worklists_list;

enum {
  WORKLISTS_NEW,
  WORKLISTS_DELETE,
  WORKLISTS_PROPERTIES,
  WORKLISTS_CLOSE
};

static GtkListStore *worklists_store;

static int max_unit_height = -1, max_unit_width = -1;

static void reset_global_worklist(GtkWidget *editor,
                                  struct global_worklist *pgwl);
static void popup_worklist(struct global_worklist *pgwl);
static void popdown_worklist(struct global_worklist *pgwl);
static void dst_row_callback(GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer data);

/************************************************************************//**
  Illegal initialization value for max unit size variables
****************************************************************************/
void blank_max_unit_size(void)
{
  max_unit_height = -1;
  max_unit_width = -1;
}

/************************************************************************//**
  Setup max unit sprite size.
****************************************************************************/
static void update_max_unit_size(void)
{
  max_unit_height = 0;
  max_unit_width = 0;

  unit_type_iterate(i) {
    int x1, x2, y1, y2;
    struct sprite *sprite = get_unittype_sprite(tileset, i,
                                                ACTIVITY_LAST,
                                                direction8_invalid());

    sprite_get_bounding_box(sprite, &x1, &y1, &x2, &y2);
    max_unit_width = MAX(max_unit_width, x2 - x1);
    max_unit_height = MAX(max_unit_height, y2 - y1);
  } unit_type_iterate_end;
}

/************************************************************************//**
  Worklists dialog being destroyed
****************************************************************************/
static void worklists_destroy_callback(GtkWidget *w, gpointer data)
{
  worklists_shell = NULL;
}

/************************************************************************//**
  Refresh worklists, both global and those of open city dialogs.
****************************************************************************/
void update_worklist_report_dialog(void)
{
  GtkTreeIter it;

  gtk_list_store_clear(worklists_store);
  global_worklists_iterate(pgwl) {
    gtk_list_store_append(worklists_store, &it);

    gtk_list_store_set(worklists_store, &it,
                       0, global_worklist_name(pgwl),
                       1, global_worklist_id(pgwl),
                       -1);
  } global_worklists_iterate_end;

  refresh_all_city_worklists();
}

/************************************************************************//**
  User has responded to worklist report
****************************************************************************/
static void worklists_response(GtkWidget *w, gint response)
{
  struct global_worklist *pgwl;
  int id;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter it;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(worklists_list));

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &id, -1);
    pgwl = global_worklist_by_id(id);
  } else {
    pgwl = NULL;
    id = -1;
  }

  switch (response) {
  case WORKLISTS_NEW:
    global_worklist_new(_("new"));
    update_worklist_report_dialog();
    return;

  case WORKLISTS_DELETE:
    if (!pgwl) {
      return;
    }

    popdown_worklist(pgwl);
    global_worklist_destroy(pgwl);
    update_worklist_report_dialog();
    return;

  case WORKLISTS_PROPERTIES:
    if (!pgwl) {
      return;
    }

    popup_worklist(pgwl);
    return;

  default:
    gtk_window_destroy(GTK_WINDOW(worklists_shell));
    return;
  }
}

/************************************************************************//**
  Worklist cell edited
****************************************************************************/
static void cell_edited(GtkCellRendererText *cell,
                        const gchar *spath,
                        const gchar *text, gpointer data)
{
  GtkTreePath *path;
  GtkTreeIter it;
  struct global_worklist *pgwl;
  int id;

  path = gtk_tree_path_new_from_string(spath);
  gtk_tree_model_get_iter(GTK_TREE_MODEL(worklists_store), &it, path);
  gtk_tree_path_free(path);

  gtk_tree_model_get(GTK_TREE_MODEL(worklists_store), &it, 1, &id, -1);
  pgwl = global_worklist_by_id(id);

  if (!pgwl) {
    gtk_list_store_remove(worklists_store, &it);
    return;
  }

  global_worklist_set_name(pgwl, text);
  gtk_list_store_set(worklists_store, &it, 0, text, -1);

  refresh_all_city_worklists();
}

/************************************************************************//**
  Bring up the global worklist report.
****************************************************************************/
static GtkWidget *create_worklists_report(void)
{
  GtkWidget *shell, *list;
  GtkWidget *vbox, *label, *sw;
  GtkCellRenderer *rend;

  shell = gtk_dialog_new_with_buttons(_("Edit worklists"),
                                      NULL,
                                      0,
                                      _("_New"),
                                      WORKLISTS_NEW,
                                      _("_Delete"),
                                      WORKLISTS_DELETE,
                                      _("_Properties"),
                                      WORKLISTS_PROPERTIES,
                                      _("_Close"),
                                      WORKLISTS_CLOSE,
                                      NULL);
  setup_dialog(shell, toplevel);

  g_signal_connect(shell, "response",
                   G_CALLBACK(worklists_response), NULL);
  g_signal_connect(shell, "destroy",
                   G_CALLBACK(worklists_destroy_callback), NULL);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(shell))),
                 vbox);

  worklists_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(worklists_store));
  gtk_widget_set_hexpand(list, TRUE);
  gtk_widget_set_vexpand(list, TRUE);

  g_object_unref(worklists_store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);

  worklists_list = list;

  rend = gtk_cell_renderer_text_new();
  g_object_set(rend, "editable", TRUE, NULL);
  g_signal_connect(rend, "edited",
                   G_CALLBACK(cell_edited), NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1, NULL,
                                              rend, "text", 0, NULL);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 200);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), list);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", list,
                       "label", _("_Worklists:"),
                       "xalign", 0.0, "yalign", 0.5, NULL);

  gtk_box_append(GTK_BOX(vbox), label);
  gtk_box_append(GTK_BOX(vbox), sw);
  gtk_widget_set_visible(vbox, TRUE);

  return shell;
}

/************************************************************************//**
  Open worklists report
****************************************************************************/
void popup_worklists_report(void)
{
  if (worklists_shell == NULL) {
    worklists_shell = create_worklists_report();

    update_worklist_report_dialog();
  }

  gtk_window_present(GTK_WINDOW(worklists_shell));
}

/****************************************************************
  ...
*****************************************************************/
struct worklist_data {
  int global_worklist_id;
  struct city *pcity;

  GtkWidget *editor;

  GtkListStore *src, *dst;
  GtkWidget *src_view, *dst_view;
  GMenu *menu;
  int menu_size;
  GActionGroup *group;
  GtkTreeSelection *src_selection, *dst_selection;

  GtkTreeViewColumn *src_col, *dst_col;

  GtkWidget *change_cmd, *help_cmd;
  GtkWidget *up_cmd, *down_cmd, *prepend_cmd, *append_cmd, *remove_cmd;

  bool future;
};

static GHashTable *hash;

static void commit_worklist(struct worklist_data *ptr);

/************************************************************************//**
  Add drag&drop target
****************************************************************************/
void add_worklist_dnd_target(GtkWidget *w,
                             gboolean (drag_drop_cb)
                               (GtkDropTarget *target, const GValue *value,
                                double x, double y, gpointer data),
                             gpointer data)
{
  GtkDropTarget *dnd_tgt;

  dnd_tgt = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);

  g_signal_connect(dnd_tgt, "drop", G_CALLBACK(drag_drop_cb), data);

  gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(dnd_tgt));
}

/************************************************************************//**
  Get worklist by id
****************************************************************************/
static GtkWidget *get_worklist(int global_worklist_id)
{
  if (hash) {
    gpointer ret;

    ret = g_hash_table_lookup(hash, GINT_TO_POINTER(global_worklist_id));
    return ret;
  } else {
    return NULL;
  }
}

/************************************************************************//**
  Insert worklist to editor
****************************************************************************/
static void insert_worklist(int global_worklist_id, GtkWidget *editor)
{
  if (!hash) {
    hash = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
  g_hash_table_insert(hash, GINT_TO_POINTER(global_worklist_id), editor);
}

/************************************************************************//**
  Remove worklist from hash
****************************************************************************/
static void delete_worklist(int global_worklist_id)
{
  if (hash) {
    g_hash_table_remove(hash, GINT_TO_POINTER(global_worklist_id));
  }
}

/************************************************************************//**
  Worklist editor window used by the global worklist report.
****************************************************************************/
static void popup_worklist(struct global_worklist *pgwl)
{
  GtkWidget *shell;

  if (!(shell = get_worklist(global_worklist_id(pgwl)))) {
    GtkWidget *editor;

    shell = gtk_dialog_new_with_buttons(global_worklist_name(pgwl),
                                        GTK_WINDOW(worklists_shell),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("_Close"),
                                        GTK_RESPONSE_CLOSE,
                                        NULL);
    g_signal_connect(shell, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_window_set_default_size(GTK_WINDOW(shell), 500, 400);

    editor = create_worklist();
    reset_global_worklist(editor, pgwl);
    insert_worklist(global_worklist_id(pgwl), editor);

    gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(shell))),
                   editor);
    gtk_widget_set_visible(editor, TRUE);

    refresh_worklist(editor);
  }

  gtk_window_present(GTK_WINDOW(shell));
}

/************************************************************************//**
  Close worklist
****************************************************************************/
static void popdown_worklist(struct global_worklist *pgwl)
{
  GtkWidget *shell;

  if ((shell = get_worklist(global_worklist_id(pgwl)))) {
    GtkWidget *parent;

    parent = gtk_widget_get_ancestor(shell, GTK_TYPE_WINDOW);
    gtk_window_destroy(GTK_WINDOW(parent));
  }
}

/************************************************************************//**
  Destroy worklist
****************************************************************************/
static void worklist_destroy(GtkWidget *editor, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;

  if (ptr->global_worklist_id != -1) {
    delete_worklist(ptr->global_worklist_id);
  }

  free(ptr);
}

/************************************************************************//**
  Item activated from menu
****************************************************************************/
static void menu_item_callback(GSimpleAction *action, GVariant *parameter,
                               gpointer data)
{
  struct global_worklist *pgwl;
  const struct worklist *pwl;
  struct worklist_data *ptr = (struct worklist_data *)data;
  size_t i;

  if (NULL == client.conn.playing) {
    return;
  }

  pgwl = global_worklist_by_id(GPOINTER_TO_INT
                               (g_object_get_data(G_OBJECT(action), "id")));
  if (pgwl == NULL) {
    return;
  }

  pwl = global_worklist_get(pgwl);

  for (i = 0; i < (size_t) worklist_length(pwl); i++) {
    GtkTreeIter it;
    cid id;
    char buf[8192];

    id = cid_encode(pwl->entries[i]);

    gtk_list_store_append(ptr->dst, &it);
    gtk_list_store_set(ptr->dst, &it, 0, (gint)id,
                       1, production_help(&(pwl->entries[i]),
                                          buf, sizeof(buf)), -1);
  }

  commit_worklist(ptr);
}

/************************************************************************//**
  Open menu for adding items to worklist
****************************************************************************/
static GMenu *create_wl_menu(struct worklist_data *ptr)
{
  GSimpleAction *act;
  int current_size = 0;

  if (ptr->menu == NULL) {
    ptr->menu = g_menu_new();
    ptr->menu_size = 0;
  }

  global_worklists_iterate(pgwl) {
    int id = global_worklist_id(pgwl);
    char act_name[60];

    fc_snprintf(act_name, sizeof(act_name), "wl%d", id);
    act = g_simple_action_new(act_name, NULL);

    g_object_set_data(G_OBJECT(act), "id",
                      GINT_TO_POINTER(global_worklist_id(pgwl)));
    g_action_map_add_action(G_ACTION_MAP(ptr->group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(menu_item_callback), ptr);

    fc_snprintf(act_name, sizeof(act_name), "win.wl%d", id);

    if (ptr->menu_size > current_size) {
      g_menu_remove(ptr->menu, current_size);
    }
    menu_item_insert_unref(ptr->menu, current_size++,
                           g_menu_item_new(global_worklist_name(pgwl), act_name));
  } global_worklists_iterate_end;

  act = g_simple_action_new("wledit", NULL);
  g_action_map_add_action(G_ACTION_MAP(ptr->group), G_ACTION(act));
  g_signal_connect(act, "activate",
                   G_CALLBACK(popup_worklists_report), NULL);

  if (ptr->menu_size > current_size) {
    g_menu_remove(ptr->menu, current_size);
  }

  menu_item_insert_unref(ptr->menu, current_size++,
                         g_menu_item_new(_("Edit Global _Worklists"), "win.wledit"));

  if (ptr->menu_size < current_size) {
    ptr->menu_size = current_size;
  } else {
    while (ptr->menu_size > current_size) {
      g_menu_remove(ptr->menu, --ptr->menu_size);
    }
  }

  return ptr->menu;
}

/************************************************************************//**
  Open help dialog for the cid pointed by iterator.
****************************************************************************/
static void wl_help_from_iter(GtkTreeModel *model, GtkTreeIter *it)
{
  gint id;
  struct universal target;

  gtk_tree_model_get(model, it, 0, &id, -1);
  target = cid_decode(id);

  if (VUT_UTYPE == target.kind) {
    popup_help_dialog_typed(utype_name_translation(target.value.utype),
                            HELP_UNIT);
  } else if (is_great_wonder(target.value.building)) {
    popup_help_dialog_typed(improvement_name_translation(target.value.building),
                            HELP_WONDER);
  } else {
    popup_help_dialog_typed(improvement_name_translation(target.value.building),
                            HELP_IMPROVEMENT);
  }
}

/************************************************************************//**
  Help button clicked
****************************************************************************/
static void help_callback(GtkWidget *w, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter it;

  ptr = data;
  selection = ptr->src_selection;

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    wl_help_from_iter(model, &it);
  } else {
    popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
  }
}

/************************************************************************//**
  "Change Production" clicked
****************************************************************************/
static void change_callback(GtkWidget *w, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter it;

  ptr = data;
  selection = ptr->src_selection;

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    gint id;
    struct universal univ;

    gtk_tree_model_get(model, &it, 0, &id, -1);
    univ = cid_production(id);
    city_change_production(ptr->pcity, &univ);
  }
}

/************************************************************************//**
  Showing of future targets toggled
****************************************************************************/
static void future_callback(GtkToggleButton *toggle, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;
  ptr->future = !ptr->future;

  refresh_worklist(ptr->editor);
}

/************************************************************************//**
  Move item up in worklist
****************************************************************************/
static void queue_bubble_up(struct worklist_data *ptr)
{
  GtkTreePath *path;
  GtkTreeViewColumn *col;
  GtkTreeModel *model;

  if (!gtk_widget_is_sensitive(ptr->dst_view)) {
    return;
  }

  model = GTK_TREE_MODEL(ptr->dst);
  gtk_tree_view_get_cursor(GTK_TREE_VIEW(ptr->dst_view), &path, &col);
  if (path) {
    GtkTreeIter it, it_prev;

    if (gtk_tree_path_prev(path)) {
      gtk_tree_model_get_iter(model, &it_prev, path);
      it = it_prev;
      gtk_tree_model_iter_next(model, &it);

      gtk_list_store_swap(GTK_LIST_STORE(model), &it, &it_prev);

      gtk_tree_view_set_cursor(GTK_TREE_VIEW(ptr->dst_view), path, col, FALSE);
      commit_worklist(ptr);
    }
  }
  gtk_tree_path_free(path);
}

/************************************************************************//**
  Removal of the item requested
****************************************************************************/
static void queue_remove(struct worklist_data *ptr)
{
  GtkTreePath *path;
  GtkTreeViewColumn *col;

  gtk_tree_view_get_cursor(GTK_TREE_VIEW(ptr->dst_view), &path, &col);
  if (path) {
    dst_row_callback(GTK_TREE_VIEW(ptr->dst_view), path, col, ptr);
    gtk_tree_path_free(path);
  }
}

/************************************************************************//**
  Move item down in queue
****************************************************************************/
static void queue_bubble_down(struct worklist_data *ptr)
{
  GtkTreePath *path;
  GtkTreeViewColumn *col;
  GtkTreeModel *model;

  if (!gtk_widget_is_sensitive(ptr->dst_view)) {
    return;
  }

  model = GTK_TREE_MODEL(ptr->dst);
  gtk_tree_view_get_cursor(GTK_TREE_VIEW(ptr->dst_view), &path, &col);
  if (path) {
    GtkTreeIter it, it_next;

    gtk_tree_model_get_iter(model, &it, path);
    it_next = it;
    if (gtk_tree_model_iter_next(model, &it_next)) {
      gtk_list_store_swap(GTK_LIST_STORE(model), &it, &it_next);

      gtk_tree_path_next(path);
      gtk_tree_view_set_cursor(GTK_TREE_VIEW(ptr->dst_view), path, col, FALSE);
      commit_worklist(ptr);
    }
  }
  gtk_tree_path_free(path);
}

/************************************************************************//**
  Insert item to queue
****************************************************************************/
static void queue_insert(struct worklist_data *ptr, bool prepend)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  GtkTreePath *path;
  GtkTreeModel *src_model, *dst_model;
  GtkTreeIter src_it, dst_it;
  gint i, ncols;

  if (!gtk_widget_is_sensitive(ptr->dst_view)) {
    return;
  }

  if (!gtk_tree_selection_get_selected(ptr->src_selection, &model, &it)) {
    return;
  }

  path = gtk_tree_model_get_path(model, &it);

  src_model = GTK_TREE_MODEL(ptr->src);
  dst_model = GTK_TREE_MODEL(ptr->dst);

  gtk_tree_model_get_iter(src_model, &src_it, path);
  if (prepend) {
    gtk_list_store_prepend(GTK_LIST_STORE(dst_model), &dst_it);
  } else {
    gtk_list_store_append(GTK_LIST_STORE(dst_model), &dst_it);
  }

  ncols = gtk_tree_model_get_n_columns(src_model);

  for (i = 0; i < ncols; i++) {
    GValue value = { 0, };

    gtk_tree_model_get_value(src_model, &src_it, i, &value);
    gtk_list_store_set_value(GTK_LIST_STORE(dst_model), &dst_it, i, &value);
  }
  commit_worklist(ptr);

  gtk_tree_path_free(path);
}

/************************************************************************//**
  Prepend item to worklist
****************************************************************************/
static void queue_prepend(struct worklist_data *ptr)
{
  queue_insert(ptr, TRUE);
}

/************************************************************************//**
  Append item to worklist
****************************************************************************/
static void queue_append(struct worklist_data *ptr)
{
  queue_insert(ptr, FALSE);
}

/************************************************************************//**
  Source row activated
****************************************************************************/
static void src_row_callback(GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeModel *src_model, *dst_model;
  GtkTreeIter src_it, dst_it;
  gint i, ncols;

  ptr = data;

  if (!gtk_widget_is_sensitive(ptr->dst_view)) {
    return;
  }

  src_model = GTK_TREE_MODEL(ptr->src);
  dst_model = GTK_TREE_MODEL(ptr->dst);

  gtk_tree_model_get_iter(src_model, &src_it, path);
  gtk_list_store_append(GTK_LIST_STORE(dst_model), &dst_it);

  ncols = gtk_tree_model_get_n_columns(src_model);

  for (i = 0; i < ncols; i++) {
    GValue value = { 0, };

    gtk_tree_model_get_value(src_model, &src_it, i, &value);
    gtk_list_store_set_value(GTK_LIST_STORE(dst_model), &dst_it, i, &value);
  }
  commit_worklist(ptr);
}

/************************************************************************//**
  Destination row activated
****************************************************************************/
static void dst_row_callback(GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *col, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeModel *dst_model;
  GtkTreeIter it;

  ptr = data;
  dst_model = GTK_TREE_MODEL(ptr->dst);

  gtk_tree_model_get_iter(dst_model, &it, path);

  gtk_list_store_remove(GTK_LIST_STORE(dst_model), &it);
  commit_worklist(ptr);
}

/************************************************************************//**
  Key press for source
****************************************************************************/
static gboolean src_key_press_callback(GtkEventControllerKey *controller,
                                       guint keyval, guint keycode,
                                       GdkModifierType state, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;

  if (!gtk_widget_is_sensitive(ptr->dst_view)) {
    return FALSE;
  }

  if ((state & GDK_SHIFT_MASK) && keyval == GDK_KEY_Insert) {
    queue_prepend(ptr);
    return TRUE;
  } else if (keyval == GDK_KEY_Insert) {
    queue_append(ptr);
    return TRUE;
  } else {
    return FALSE;
  }
}

/************************************************************************//**
  Key press for destination
****************************************************************************/
static gboolean dst_key_press_callback(GtkEventControllerKey *controller,
                                       guint keyval, guint keycode,
                                       GdkModifierType state, gpointer data)
{
  GtkTreeModel *model;
  struct worklist_data *ptr;

  ptr = data;
  model = GTK_TREE_MODEL(ptr->dst);

  if (keyval == GDK_KEY_Delete) {
    GtkTreeIter it, it_next;
    bool deleted = FALSE;

    if (gtk_tree_model_get_iter_first(model, &it)) {
      bool more;

      do {
        it_next = it;
        more = gtk_tree_model_iter_next(model, &it_next);

        if (gtk_tree_selection_iter_is_selected(ptr->dst_selection, &it)) {
          gtk_list_store_remove(GTK_LIST_STORE(model), &it);
          deleted = TRUE;
        }
        it = it_next;

      } while (more);
    }

    if (deleted) {
      commit_worklist(ptr);
    }

    return TRUE;
  } else if ((state & GDK_ALT_MASK) && keyval == GDK_KEY_Up) {
    queue_bubble_up(ptr);

    return TRUE;
  } else if ((state & GDK_ALT_MASK) && keyval == GDK_KEY_Down) {
    queue_bubble_down(ptr);

    return TRUE;
  } else {
    return FALSE;
  }
}

/************************************************************************//**
  Selection from source
****************************************************************************/
static void src_selection_callback(GtkTreeSelection *selection, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;

  /* Update widget sensitivity. */
  if (gtk_tree_selection_get_selected(selection, NULL, NULL)) {
    if (can_client_issue_orders()
        && (!ptr->pcity || city_owner(ptr->pcity) == client.conn.playing)) {
      /* If ptr->pcity is NULL, this is a global worklist */
      gtk_widget_set_sensitive(ptr->change_cmd, TRUE);
      gtk_widget_set_sensitive(ptr->prepend_cmd, TRUE);
      gtk_widget_set_sensitive(ptr->append_cmd, TRUE);
    } else {
      gtk_widget_set_sensitive(ptr->change_cmd, FALSE);
      gtk_widget_set_sensitive(ptr->prepend_cmd, FALSE);
      gtk_widget_set_sensitive(ptr->append_cmd, FALSE);
    }
    gtk_widget_set_sensitive(ptr->help_cmd, TRUE);
  } else {
    gtk_widget_set_sensitive(ptr->change_cmd, FALSE);
    gtk_widget_set_sensitive(ptr->help_cmd, FALSE);
    gtk_widget_set_sensitive(ptr->prepend_cmd, FALSE);
    gtk_widget_set_sensitive(ptr->append_cmd, FALSE);
  }
}

/************************************************************************//**
  Selection from destination
****************************************************************************/
static void dst_selection_callback(GtkTreeSelection *selection, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;

  /* Update widget sensitivity. */
  if (gtk_tree_selection_count_selected_rows(selection) > 0) {
    int num_rows = 0;
    GtkTreeIter it;

    gtk_widget_set_sensitive(ptr->up_cmd, TRUE);
    gtk_widget_set_sensitive(ptr->down_cmd, TRUE);
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ptr->dst), &it)) {
      do {
        num_rows++;
      } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ptr->dst), &it));
    }
    if (num_rows > 1) {
      gtk_widget_set_sensitive(ptr->remove_cmd, TRUE);
    } else {
      gtk_widget_set_sensitive(ptr->remove_cmd, FALSE);
    }
  } else {
    gtk_widget_set_sensitive(ptr->up_cmd, FALSE);
    gtk_widget_set_sensitive(ptr->down_cmd, FALSE);
    gtk_widget_set_sensitive(ptr->remove_cmd, FALSE);
  }
}

/************************************************************************//**
  Render worklist cell
****************************************************************************/
static void cell_render_func(GtkTreeViewColumn *col, GtkCellRenderer *rend,
                             GtkTreeModel *model, GtkTreeIter *it,
                             gpointer data)
{
  gint id;
  struct universal target;

  gtk_tree_model_get(model, it, 0, &id, -1);
  target = cid_production(id);

  if (GTK_IS_CELL_RENDERER_PIXBUF(rend)) {
    GdkPixbuf *pix;
    struct sprite *sprite;

    if (VUT_UTYPE == target.kind) {
      sprite = sprite_scale(get_unittype_sprite(tileset, target.value.utype,
                                                ACTIVITY_LAST,
                                                direction8_invalid()),
                            max_unit_width, max_unit_height);
    } else {
      sprite = get_building_sprite(tileset, target.value.building);
    }
    pix = sprite_get_pixbuf(sprite);
    g_object_set(rend, "pixbuf", pix, NULL);
    g_object_unref(G_OBJECT(pix));
    if (VUT_UTYPE == target.kind) {
      free_sprite(sprite);
    }
  } else {
    struct city **pcity = data;
    gint column;
    char *row[4];
    char buf[4][64];
    guint i;
    gboolean useless;

    for (i = 0; i < ARRAY_SIZE(row); i++) {
      row[i] = buf[i];
    }
    column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(rend), "column"));

    get_city_dialog_production_row(row, sizeof(buf[0]), &target, *pcity);
    g_object_set(rend, "text", row[column], NULL);

    if (NULL != *pcity && VUT_IMPROVEMENT == target.kind) {
      useless = is_improvement_redundant(*pcity, target.value.building);
      /* Mark building redundant if we are really certain that there is
       * no use for it. */
      g_object_set(rend, "strikethrough", useless, NULL);
    } else {
      g_object_set(rend, "strikethrough", FALSE, NULL);
    }
  }
}

/************************************************************************//**
  Populate view with buildable item information
****************************************************************************/
static void populate_view(GtkTreeView *view, struct city **ppcity,
                          GtkTreeViewColumn **pcol)
{
  static const char *titles[] =
  { N_("Type"), N_("Name"), N_("Info"), N_("Cost"), N_("Turns") };

  static bool titles_done;
  guint i;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  intl_slist(ARRAY_SIZE(titles), titles, &titles_done);

  /* Case i == 0 taken out of the loop to workaround gcc-4.2.1 bug
   * https://gcc.gnu.org/PR33381
   * Some values would 'stick' from i == 0 round. */
  i = 0;

  rend = gtk_cell_renderer_pixbuf_new();

  gtk_tree_view_insert_column_with_data_func(view, i, titles[i], rend,
                                             cell_render_func, ppcity, NULL);
  col = gtk_tree_view_get_column(view, i);

  if (GUI_GTK_OPTION(show_task_icons)) {
    if (max_unit_width == -1 || max_unit_height == -1) {
      update_max_unit_size();
    }
  } else {
    g_object_set(col, "visible", FALSE, NULL);
  }
  if (GUI_GTK_OPTION(show_task_icons)) {
    g_object_set(rend, "height", max_unit_height, NULL);
  }

  for (i = 1; i < ARRAY_SIZE(titles); i++) {
    gint pos = i - 1;

    rend = gtk_cell_renderer_text_new();
    g_object_set_data(G_OBJECT(rend), "column", GINT_TO_POINTER(pos));

    gtk_tree_view_insert_column_with_data_func(view,
                                               i, titles[i], rend,
                                               cell_render_func, ppcity, NULL);
    col = gtk_tree_view_get_column(view, i);

    if (pos >= 2) {
      g_object_set(G_OBJECT(rend), "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
    }

    if (pos == 3) {
      *pcol = col;
    }
    if (GUI_GTK_OPTION(show_task_icons)) {
      g_object_set(rend, "height", max_unit_height, NULL);
    }
  }
}

/************************************************************************//**
  Open help dialog for the worklist item.
****************************************************************************/
static gboolean wl_right_button_up(GtkGestureClick *gesture,
                                   int n_press,
                                   double x, double y)
{
  GtkEventController *controller = GTK_EVENT_CONTROLLER(gesture);
  GtkWidget *w = gtk_event_controller_get_widget(controller);
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
  GtkTreePath *path;
  int bx, by;

  gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y, &bx, &by);

  if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bx, by, &path, NULL, NULL, NULL)) {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
      wl_help_from_iter(model, &iter);
    }
  }

  return TRUE;
}

/************************************************************************//**
  Receive drag&drop
****************************************************************************/
static gboolean drag_drop(GtkDropTarget *target, const GValue *value,
                          double x, double y, gpointer data)
{
  struct worklist_data *ptr = (struct worklist_data *)data;
  GtkTreeIter it;
  char buf[8192];
  cid id = g_value_get_int(value);
  struct universal univ;

  univ = cid_production(id);
  gtk_list_store_append(ptr->dst, &it);
  gtk_list_store_set(ptr->dst, &it, 0, id, 1,
                     production_help(&univ, buf, sizeof(buf)), -1);

  commit_worklist(ptr);

  return TRUE;
}

/************************************************************************//**
  Worklist editor shell.
****************************************************************************/
GtkWidget *create_worklist(void)
{
  GtkWidget *editor, *table, *sw, *bbox;
  GtkWidget *src_view, *dst_view, *label, *button;
  GtkWidget *aux_menu;
  GMenu *menu;
  GtkWidget *table2, *arrow, *check;
  GtkSizeGroup *sgroup;
  GtkListStore *src_store, *dst_store;
  struct worklist_data *ptr;
  int editor_row = 0;
  GtkEventController *controller;
  GtkGesture *gesture;
  GtkDropTarget *dnd_tgt;

  ptr = fc_malloc(sizeof(*ptr));

  src_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
  dst_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);

  ptr->global_worklist_id = -1;
  ptr->pcity = NULL;
  ptr->src = src_store;
  ptr->dst = dst_store;
  ptr->future = FALSE;

  /* Create shell. */
  editor = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(editor), 6);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(editor),
                                 GTK_ORIENTATION_VERTICAL);
  g_signal_connect(editor, "destroy", G_CALLBACK(worklist_destroy), ptr);
  g_object_set_data(G_OBJECT(editor), "data", ptr);

  ptr->editor = editor;

  /* Add source and target lists.  */
  table = gtk_grid_new();
  gtk_grid_attach(GTK_GRID(editor), table, 0, editor_row++, 1, 1);

  sgroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_grid_attach(GTK_GRID(table), sw, 3, 1, 2, 1);

  src_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(src_store));
  gtk_widget_set_hexpand(src_view, TRUE);
  gtk_widget_set_vexpand(src_view, TRUE);
  g_object_unref(src_store);
  gtk_size_group_add_widget(sgroup, src_view);
  gtk_widget_set_name(src_view, "small_font");
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(src_view), 1);

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(wl_right_button_up), NULL);
  gtk_widget_add_controller(src_view, controller);

  populate_view(GTK_TREE_VIEW(src_view), &ptr->pcity, &ptr->src_col);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), src_view);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", src_view,
                       "label", _("Source _Tasks:"),
                       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_grid_attach(GTK_GRID(table), label, 3, 0, 1, 1);

  check = gtk_check_button_new_with_mnemonic(_("Show _Future Targets"));
  gtk_grid_attach(GTK_GRID(table), check, 4, 0, 1, 1);
  g_signal_connect(check, "toggled", G_CALLBACK(future_callback), ptr);

  table2 = gtk_grid_new();
  gtk_grid_attach(GTK_GRID(table), table2, 2, 1, 1, 1);

  button = gtk_button_new();
  gtk_widget_set_margin_top(button, 24);
  gtk_widget_set_margin_bottom(button, 24);
  ptr->prepend_cmd = button;
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_grid_attach(GTK_GRID(table2), button, 0, 0, 1, 1);

  arrow = gtk_image_new_from_icon_name("pan-start-symbolic");
  gtk_button_set_child(GTK_BUTTON(button), arrow);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(queue_prepend), ptr);
  gtk_widget_set_sensitive(ptr->prepend_cmd, FALSE);

  button = gtk_button_new();
  ptr->up_cmd = button;
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_grid_attach(GTK_GRID(table2), button, 0, 1, 1, 1);

  arrow = gtk_image_new_from_icon_name("pan-up-symbolic");
  gtk_button_set_child(GTK_BUTTON(button), arrow);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(queue_bubble_up), ptr);
  gtk_widget_set_sensitive(ptr->up_cmd, FALSE);

  button = gtk_button_new();
  ptr->down_cmd = button;
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_grid_attach(GTK_GRID(table2), button, 0, 2, 1, 1);

  arrow = gtk_image_new_from_icon_name("pan-down-symbolic");
  gtk_button_set_child(GTK_BUTTON(button), arrow);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(queue_bubble_down), ptr);
  gtk_widget_set_sensitive(ptr->down_cmd, FALSE);

  button = gtk_button_new();
  gtk_widget_set_margin_top(button, 24);
  gtk_widget_set_margin_bottom(button, 24);
  ptr->append_cmd = button;
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_grid_attach(GTK_GRID(table2), button, 0, 3, 1, 1);

  arrow = gtk_image_new_from_icon_name("pan-start-symbolic");
  gtk_button_set_child(GTK_BUTTON(button), arrow);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(queue_append), ptr);
  gtk_widget_set_sensitive(ptr->append_cmd, FALSE);

  button = gtk_button_new();
  gtk_widget_set_margin_top(button, 24);
  gtk_widget_set_margin_bottom(button, 24);
  ptr->remove_cmd = button;
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_grid_attach(GTK_GRID(table2), button, 0, 4, 1, 1);

  arrow = gtk_image_new_from_icon_name("pan-end-symbolic");
  gtk_button_set_child(GTK_BUTTON(button), arrow);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(queue_remove), ptr);
  gtk_widget_set_sensitive(ptr->remove_cmd, FALSE);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_grid_attach(GTK_GRID(table), sw, 0, 1, 2, 1);

  dst_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dst_store));
  gtk_widget_set_hexpand(dst_view, TRUE);
  gtk_widget_set_vexpand(dst_view, TRUE);
  g_object_unref(dst_store);
  gtk_size_group_add_widget(sgroup, dst_view);
  gtk_widget_set_name(dst_view, "small_font");
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(dst_view), 1);

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
  controller = GTK_EVENT_CONTROLLER(gesture);
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(wl_right_button_up), NULL);
  gtk_widget_add_controller(dst_view, controller);

  populate_view(GTK_TREE_VIEW(dst_view), &ptr->pcity, &ptr->dst_col);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), dst_view);

  label = g_object_new(GTK_TYPE_LABEL,
                       "use-underline", TRUE,
                       "mnemonic-widget", dst_view,
                       "label", _("Target _Worklist:"),
                       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_grid_attach(GTK_GRID(table), label, 0, 0, 1, 1);

  /* Add bottom menu and buttons. */
  bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_set_spacing(GTK_BOX(bbox), 10);
  gtk_grid_attach(GTK_GRID(editor), bbox, 0, editor_row++, 1, 1);

  ptr->menu = NULL;
  aux_menu = aux_menu_new();
  ptr->group = G_ACTION_GROUP(g_simple_action_group_new());
  menu = create_wl_menu(ptr);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(aux_menu), G_MENU_MODEL(menu));

  gtk_box_append(GTK_BOX(bbox), aux_menu);
  gtk_widget_insert_action_group(aux_menu, "win", ptr->group);

  button = icon_label_button_new("help-browser", _("Help"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(help_callback), ptr);
  ptr->help_cmd = button;
  gtk_widget_set_sensitive(ptr->help_cmd, FALSE);

  button = gtk_button_new_with_mnemonic(_("Change Prod_uction"));
  gtk_box_append(GTK_BOX(bbox), button);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(change_callback), ptr);
  ptr->change_cmd = button;
  gtk_widget_set_sensitive(ptr->change_cmd, FALSE);

  ptr->src_view = src_view;
  ptr->dst_view = dst_view;
  ptr->src_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(src_view));
  ptr->dst_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dst_view));
  gtk_tree_selection_set_mode(ptr->dst_selection, GTK_SELECTION_MULTIPLE);

  /* DND and other state changing callbacks. */
  gtk_tree_view_set_reorderable(GTK_TREE_VIEW(dst_view), TRUE);

  dnd_tgt = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);

  g_signal_connect(dnd_tgt, "drop", G_CALLBACK(drag_drop), ptr);
  gtk_widget_add_controller(GTK_WIDGET(dst_view), GTK_EVENT_CONTROLLER(dnd_tgt));

  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed",
                   G_CALLBACK(src_key_press_callback), ptr);
  gtk_widget_add_controller(src_view, controller);

  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed",
                   G_CALLBACK(dst_key_press_callback), ptr);
  gtk_widget_add_controller(dst_view, controller);

  g_signal_connect(src_view, "row_activated",
                   G_CALLBACK(src_row_callback), ptr);

  g_signal_connect(dst_view, "row_activated",
                   G_CALLBACK(dst_row_callback), ptr);

  g_signal_connect(ptr->src_selection, "changed",
                   G_CALLBACK(src_selection_callback), ptr);
  g_signal_connect(ptr->dst_selection, "changed",
                   G_CALLBACK(dst_selection_callback), ptr);


  gtk_widget_set_visible(table, TRUE);
  gtk_widget_set_visible(bbox, TRUE);

  return editor;
}

/************************************************************************//**
  Prepare data for drag&drop
****************************************************************************/
static GdkContentProvider *drag_prepare(GtkDragSource *source,
                                        double x, double y,
                                        gpointer data)
{
  GtkTreeIter it;
  struct worklist_data *ptr = (struct worklist_data *)data;

  if (gtk_tree_selection_get_selected(ptr->src_selection, NULL, &it)) {
    gint id;
    GdkContentProvider *provider;

    gtk_tree_model_get(GTK_TREE_MODEL(ptr->src), &it, 0, &id, -1);

    provider = gdk_content_provider_new_typed(G_TYPE_INT, id);

    gtk_drag_source_set_content(source, provider);

    return provider;
  }

  return NULL;
}

/************************************************************************//**
  Set drag icon
****************************************************************************/
static void drag_begin(GtkDragSource *source, GdkDrag *drag,
                       gpointer *data)
{
  GdkContentProvider *content = gtk_drag_source_get_content(source);
  GValue val = { 0, };
  GdkPaintable *paintable;
  cid id;
  struct universal target;
  struct sprite *sprite;
  GdkPixbuf *pix;
  GtkWidget *img;

  g_value_init(&val, G_TYPE_INT);
  if (gdk_content_provider_get_value(content, &val, NULL)) {
    id = g_value_get_int(&val);
    target = cid_production(id);

    if (VUT_UTYPE == target.kind) {
      sprite = sprite_scale(get_unittype_sprite(tileset, target.value.utype,
                                                ACTIVITY_LAST,
                                                direction8_invalid()),
                            max_unit_width, max_unit_height);
    } else {
      sprite = get_building_sprite(tileset, target.value.building);
    }
    pix = sprite_get_pixbuf(sprite);
    img = gtk_image_new_from_pixbuf(pix);

    paintable = gtk_image_get_paintable(GTK_IMAGE(img));

    gtk_drag_source_set_icon(source, paintable, 0, 0);
    g_object_unref(paintable);
  }
}

/************************************************************************//**
  Reset worklist for city
****************************************************************************/
void reset_city_worklist(GtkWidget *editor, struct city *pcity)
{
  struct worklist_data *ptr;
  GtkDragSource *dnd_src;

  ptr = g_object_get_data(G_OBJECT(editor), "data");

  ptr->global_worklist_id = -1;
  ptr->pcity = pcity;

  gtk_list_store_clear(ptr->src);
  gtk_list_store_clear(ptr->dst);

  g_object_set(ptr->src_col, "visible", TRUE, NULL);
  g_object_set(ptr->dst_col, "visible", TRUE, NULL);

  dnd_src = gtk_drag_source_new();

  g_signal_connect(dnd_src, "prepare", G_CALLBACK(drag_prepare), ptr);
  g_signal_connect(dnd_src, "drag-begin", G_CALLBACK(drag_begin), ptr);

  gtk_widget_add_controller(GTK_WIDGET(ptr->src_view),
                            GTK_EVENT_CONTROLLER(dnd_src));
}

/************************************************************************//**
  Reset one of the global worklists
****************************************************************************/
static void reset_global_worklist(GtkWidget *editor,
                                  struct global_worklist *pgwl)
{
  struct worklist_data *ptr;

  ptr = g_object_get_data(G_OBJECT(editor), "data");

  ptr->global_worklist_id = global_worklist_id(pgwl);
  ptr->pcity = nullptr;

  gtk_list_store_clear(ptr->src);
  gtk_list_store_clear(ptr->dst);

  gtk_widget_set_visible(ptr->change_cmd, FALSE);
  g_object_set(ptr->src_col, "visible", FALSE, NULL);
  g_object_set(ptr->dst_col, "visible", FALSE, NULL);

  gtk_tree_view_unset_rows_drag_source(GTK_TREE_VIEW(ptr->src_view));
}

/************************************************************************//**
  Refresh worklist info
****************************************************************************/
void refresh_worklist(GtkWidget *editor)
{
  struct worklist_data *ptr;
  struct worklist queue;
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  int i, targets_used;
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  bool selected;
  gint id;
  GtkTreeIter it;
  GtkTreePath *path;
  GtkTreeModel *model;
  gboolean exists;

  ptr = g_object_get_data(G_OBJECT(editor), "data");

  /* Refresh source tasks. */
  if (gtk_tree_selection_get_selected(ptr->src_selection, NULL, &it)) {
    gtk_tree_model_get(GTK_TREE_MODEL(ptr->src), &it, 0, &id, -1);
    selected = TRUE;
  } else {
    selected = FALSE;
  }

  /* These behave just right if ptr->pcity is NULL -> in case of global
   * worklist. */
  targets_used = collect_eventually_buildable_targets(targets, ptr->pcity,
                                                      ptr->future);
  name_and_sort_items(targets, targets_used, items, FALSE, ptr->pcity);

  /* Re-purpose existing items in the list store -- this avoids the
   * UI jumping around (especially as the set of source tasks doesn't
   * actually change much in practice). */
  model = GTK_TREE_MODEL(ptr->src);
  exists = gtk_tree_model_get_iter_first(model, &it);

  path = NULL;
  for (i = 0; i < targets_used; i++) {
    char buf[8192];

    if (!exists) {
      gtk_list_store_append(ptr->src, &it);
    }

    gtk_list_store_set(ptr->src, &it, 0, (gint)cid_encode(items[i].item),
                       1, production_help(&(items[i].item),
                                          buf, sizeof(buf)), -1);

    if (selected && cid_encode(items[i].item) == id) {
      path = gtk_tree_model_get_path(GTK_TREE_MODEL(ptr->src), &it);
    }

    if (exists) {
      exists = gtk_tree_model_iter_next(model, &it);
    }
  }

  /* If the list got shorter, delete any excess items. */
  if (exists) {
    GtkTreeIter it_next;
    bool more;

    do {
      it_next = it;
      more = gtk_tree_model_iter_next(model, &it_next);

      gtk_list_store_remove(ptr->src, &it);
      it = it_next;
    } while (more);
  }

  /* Select the same item that was previously selected, if any. */
  if (path) {
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(ptr->src_view), path, NULL, FALSE);
    gtk_tree_path_free(path);
  }

  /* Refresh target worklist. */
  model = GTK_TREE_MODEL(ptr->dst);
  exists = gtk_tree_model_get_iter_first(model, &it);

  /* Dance around worklist braindamage. */
  if (ptr->pcity != NULL) {
    city_get_queue(ptr->pcity, &queue);
  } else {
    const struct global_worklist *pgwl;

    pgwl = global_worklist_by_id(ptr->global_worklist_id);

    fc_assert(NULL != pgwl);

    worklist_copy(&queue, global_worklist_get(pgwl));
  }

  for (i = 0; i < worklist_length(&queue); i++) {
    struct universal target = queue.entries[i];
    char buf[8192];

    if (!exists) {
      gtk_list_store_append(ptr->dst, &it);
    }

    gtk_list_store_set(ptr->dst, &it, 0, (gint)cid_encode(target),
                       1, production_help(&target,
                                          buf, sizeof(buf)), -1);

    if (exists) {
      exists = gtk_tree_model_iter_next(model, &it);
    }
  }

  if (exists) {
    GtkTreeIter it_next;
    bool more;

    do {
      it_next = it;
      more = gtk_tree_model_iter_next(model, &it_next);

      gtk_list_store_remove(ptr->dst, &it);
      it = it_next;
    } while (more);
  }

  create_wl_menu(ptr);

  /* Update widget sensitivity. */
  if (ptr->pcity) {
    if ((can_client_issue_orders()
         && city_owner(ptr->pcity) == client.conn.playing)) {
      gtk_widget_set_sensitive(ptr->dst_view, TRUE);
    } else {
      gtk_widget_set_sensitive(ptr->dst_view, FALSE);
    }
  } else {
    gtk_widget_set_sensitive(ptr->dst_view, TRUE);
  }
}

/************************************************************************//**
  Commit worklist data to worklist
****************************************************************************/
static void commit_worklist(struct worklist_data *ptr)
{
  struct worklist queue;
  GtkTreeModel *model;
  GtkTreeIter it;
  size_t i;

  model = GTK_TREE_MODEL(ptr->dst);

  worklist_init(&queue);

  i = 0;
  if (gtk_tree_model_get_iter_first(model, &it)) {
    do {
      gint id;
      struct universal univ;

      /* Oops, the player has a worklist longer than what we can store. */
      if (i >= MAX_LEN_WORKLIST) {
        break;
      }

      gtk_tree_model_get(model, &it, 0, &id, -1);
      univ = cid_production(id);
      worklist_append(&queue, &univ);

      i++;
    } while (gtk_tree_model_iter_next(model, &it));
  }

  /* Dance around worklist braindamage. */
  if (ptr->pcity) {
    if (!city_set_queue(ptr->pcity, &queue)) {
      /* Failed to change worklist. This means worklist visible
       * on screen is not true. */
      refresh_worklist(ptr->editor);
    }
  } else {
    struct global_worklist *pgwl;

    pgwl = global_worklist_by_id(ptr->global_worklist_id);
    if (pgwl) {
      global_worklist_set(pgwl, &queue);
    }
  }
}
