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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "city.h"
#include "citydlg_common.h"
#include "fcintl.h"
#include "game.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mem.h"
#include "packets.h"
#include "worklist.h"
#include "support.h"
#include "climisc.h"
#include "clinet.h"

#include "wldlg.h"
#include "citydlg.h"
#include "civclient.h"

static GtkWidget *worklists_shell;
static GtkWidget *worklists_list;

enum {
  WORKLISTS_NEW,
  WORKLISTS_DELETE,
  WORKLISTS_PROPERTIES,
  WORKLISTS_CLOSE
};

static GtkListStore *worklists_store;


static void popup_worklist(struct worklist *pwl);
static void popdown_worklist(struct worklist *pwl);


/****************************************************************
...
*****************************************************************/
static void worklists_destroy_callback(GtkWidget *w, gpointer data)
{
  worklists_shell = NULL;
}


/****************************************************************
...
*****************************************************************/
void update_worklist_report_dialog(void)
{
  struct player *plr;
  int i;
  GtkTreeIter it;

  if (!worklists_shell) {
    return;
  }

  plr = game.player_ptr;

  gtk_list_store_clear(worklists_store);
  
  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    if (plr->worklists[i].is_valid) {
      gtk_list_store_append(worklists_store, &it);
      
       gtk_list_store_set(worklists_store, &it,
			  0, plr->worklists[i].name,
			  1, i,
			  -1); 
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void worklists_response(GtkWidget *w, gint response)
{
  struct player *plr;
  int i, pos;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter it;

  plr = game.player_ptr;

  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    if (!plr->worklists[i].is_valid) {
      break;
    }
  }
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(worklists_list));

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &pos, -1);
  } else {
    pos = -1;
  }

 switch (response) {
    case WORKLISTS_NEW:
      /* No more worklist slots free.  (!!!Maybe we should tell the user?) */
      if (i == MAX_NUM_WORKLISTS) {
        return;
      }

      /* Validate this slot. */
      init_worklist(&plr->worklists[i]);
      plr->worklists[i].is_valid = TRUE;
      strcpy(plr->worklists[i].name, _("new"));

      update_worklist_report_dialog();
      return;

    case WORKLISTS_DELETE:
      if (pos == -1) {
	return;
      }

      popdown_worklist(&plr->worklists[pos]);

      plr->worklists[pos].is_valid = FALSE;
      plr->worklists[pos].name[0] = '\0';

      update_worklist_report_dialog();
      return;

    case WORKLISTS_PROPERTIES:
      if (pos == -1) {
	return;
      }

      popup_worklist(&plr->worklists[pos]);
      return;

    default:
      gtk_widget_destroy(worklists_shell);
      return;
  }
}

/****************************************************************
...
*****************************************************************/
static void cell_edited(GtkCellRendererText *cell,
			const gchar *spath,
			const gchar *text, gpointer data)
{
  GtkTreePath *path;
  GtkTreeIter it;
  int pos;
  struct player *plr;

  path = gtk_tree_path_new_from_string(spath);
  gtk_tree_model_get_iter(GTK_TREE_MODEL(worklists_store), &it, path);
  
  gtk_tree_model_get(GTK_TREE_MODEL(worklists_store), &it, 1, &pos, -1);
  plr = game.player_ptr;

  sz_strlcpy(plr->worklists[pos].name, text);
  gtk_list_store_set(worklists_store, &it, 0, text, -1);

  gtk_tree_path_free(path);
}

/****************************************************************
  Bring up the global worklist report.
*****************************************************************/
static GtkWidget *create_worklists_report(void)
{
  GtkWidget *shell, *list;
  GtkWidget *vbox, *label, *sw;
  GtkCellRenderer *rend;

  shell = gtk_dialog_new_with_buttons(_("Edit worklists"),
				      NULL,
				      0,
				      GTK_STOCK_NEW,
				      WORKLISTS_NEW,
				      GTK_STOCK_DELETE,
				      WORKLISTS_DELETE,
				      GTK_STOCK_PROPERTIES,
				      WORKLISTS_PROPERTIES,
				      GTK_STOCK_CLOSE,
				      WORKLISTS_CLOSE,
				      NULL);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  
  g_signal_connect(shell, "response",
		   G_CALLBACK(worklists_response), NULL);
  g_signal_connect(shell, "destroy",
		   G_CALLBACK(worklists_destroy_callback), NULL);

  vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), vbox);

  worklists_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(worklists_store));
  g_object_unref(worklists_store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);

  worklists_list = list;
  
  rend = gtk_cell_renderer_text_new();
  g_object_set(rend, "editable", TRUE, NULL);
  g_signal_connect(rend, "edited",
		   G_CALLBACK(cell_edited), NULL);
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1, NULL,
    rend, "text", 0, NULL);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(sw), list);

  gtk_widget_set_size_request(sw, -1, 200);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", list,
		       "label", _("_Worklists:"),
		       "xalign", 0.0, "yalign", 0.5, NULL);

  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
  gtk_widget_show_all(vbox);

  return shell;
}

/****************************************************************
...
*****************************************************************/
void popup_worklists_report(void)
{
  if (!worklists_shell) {
    worklists_shell = create_worklists_report();

    update_worklist_report_dialog();
  }

  gtk_window_present(GTK_WINDOW(worklists_shell));
}




/****************************************************************
...
*****************************************************************/
struct worklist_data {
  struct worklist *pwl;
  struct city *pcity;

  GtkListStore *src;
  GtkListStore *dst;

  GtkTreeSelection *src_selection;
  GtkTreeSelection *dst_selection;
  
  bool advanced;
};

static GHashTable *hash;

static void commit_worklist(struct worklist_data *ptr);


enum {
  TARGET_GTK_TREE_MODEL_ROW
};

static GtkTargetEntry wl_dnd_targets[] = {
  { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, TARGET_GTK_TREE_MODEL_ROW },
};




/****************************************************************
...
*****************************************************************/
void add_worklist_dnd_target(GtkWidget *w)
{
  gtk_drag_dest_set(w, GTK_DEST_DEFAULT_ALL,
		    wl_dnd_targets, G_N_ELEMENTS(wl_dnd_targets),
		    GDK_ACTION_COPY);
}

/****************************************************************
...
*****************************************************************/
static GtkWidget *get_worklist(struct worklist *pwl)
{
  if (hash) {
    gpointer ret;
    
    ret = g_hash_table_lookup(hash, pwl);
    return ret;
  } else {
    return NULL;
  }
}

/****************************************************************
...
*****************************************************************/
static void insert_worklist(struct worklist *pwl, GtkWidget *editor)
{
  if (!hash) {
    hash = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
  g_hash_table_insert(hash, pwl, editor);
}

/****************************************************************
...
*****************************************************************/
static void delete_worklist(struct worklist *pwl)
{
  if (hash) {
    g_hash_table_remove(hash, pwl);
  }
}

/****************************************************************
...
*****************************************************************/
static void worklist_response(GtkWidget *shell, gint response)
{
  gtk_widget_destroy(shell);
}

/****************************************************************
...
*****************************************************************/
static void popup_worklist(struct worklist *pwl)
{
  GtkWidget *shell;

  if (!(shell = get_worklist(pwl))) {
    GtkWidget *editor;
    
    shell = gtk_dialog_new_with_buttons(pwl->name,
					GTK_WINDOW(worklists_shell),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_STOCK_CLOSE,
					GTK_RESPONSE_CLOSE,
					NULL);
    gtk_window_set_role(GTK_WINDOW(shell), "worklist");
    gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
    g_signal_connect(shell, "response", G_CALLBACK(worklist_response), NULL);
    gtk_window_set_default_size(GTK_WINDOW(shell), 500, 400);
  
    editor = create_worklist(pwl, NULL);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), editor);
    gtk_widget_show_all(editor);
  }

  gtk_window_present(GTK_WINDOW(shell));
}

/****************************************************************
...
*****************************************************************/
static void popdown_worklist(struct worklist *pwl)
{
  GtkWidget *shell;
  
  if ((shell = get_worklist(pwl))) {
    GtkWidget *parent;
    
    parent = gtk_widget_get_toplevel(shell);
    gtk_widget_destroy(parent);
  }
}

/****************************************************************
...
*****************************************************************/
static void worklist_destroy(GtkWidget *editor, gpointer data)
{
  struct worklist_data *ptr;
  
  delete_worklist((struct worklist *) data);

  ptr = g_object_get_data(G_OBJECT(editor), data);
  free(ptr);
}

/****************************************************************
...
*****************************************************************/
static void menu_item_callback(GtkMenuItem *item, struct worklist_data *ptr)
{
  struct player *plr;
  gint pos;
  struct worklist *pwl;
  
  plr = game.player_ptr;
  pos = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "pos"));

  pwl = &plr->worklists[pos];

  if (pwl->is_valid) {
    int i;

    for (i = 0; i < MAX_LEN_WORKLIST; i++) {
      GtkTreeIter it;
      cid cid;

      if (pwl->wlefs[i] == WEF_END) {
	break;
      }

      cid = cid_encode(pwl->wlefs[i] == WEF_UNIT, pwl->wlids[i]);
      
      gtk_list_store_append(ptr->dst, &it);
      gtk_list_store_set(ptr->dst, &it, 0, (gint) cid, -1);
    }
  }

  commit_worklist(ptr);
}

/****************************************************************
...
*****************************************************************/
static void popup_add_menu(GtkMenuShell *menu, gpointer data)
{
  struct player *plr;
  int i;

  gtk_container_foreach(GTK_CONTAINER(menu),
			(GtkCallback) gtk_widget_destroy, NULL);
  plr = game.player_ptr;

  for (i = 0; i < MAX_NUM_WORKLISTS; i++) {
    if (plr->worklists[i].is_valid) {
      GtkWidget *item;

      item = gtk_menu_item_new_with_label(plr->worklists[i].name);
      g_object_set_data(G_OBJECT(item), "pos", GINT_TO_POINTER(i));
      gtk_widget_show(item);

      gtk_container_add(GTK_CONTAINER(menu), item);
      g_signal_connect(item, "activate",
		       G_CALLBACK(menu_item_callback), data);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void help_callback(GtkWidget *w, gpointer data)
{
  GtkTreeView *view;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter it;

  view = GTK_TREE_VIEW(data);
  selection = gtk_tree_view_get_selection(view);

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    gint cid;
    int id;
    bool is_unit;

    gtk_tree_model_get(model, &it, 0, &cid, -1);
    is_unit = cid_is_unit(cid);
    id = cid_id(cid);

    if (is_unit) {
      popup_help_dialog_typed(get_unit_type(id)->name, HELP_UNIT);
    } else if (is_wonder(id)) {
      popup_help_dialog_typed(get_improvement_name(id), HELP_WONDER);
    } else {
      popup_help_dialog_typed(get_improvement_name(id), HELP_IMPROVEMENT);
    }
  } else {
    popup_help_dialog_string(HELP_WORKLIST_EDITOR_ITEM);
  }
}

/****************************************************************
...
*****************************************************************/
static void advanced_callback(GtkToggleButton *toggle, gpointer data)
{
  struct worklist_data *ptr;

  ptr = data;
  ptr->advanced = !ptr->advanced;

  refresh_worklist(ptr->pwl);
}

/****************************************************************
...
*****************************************************************/
static void src_row_callback(GtkTreeView *src_view, GtkTreePath *path,
			     GtkTreeViewColumn *col, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeModel *src_model, *dst_model;
  GtkTreeIter src_it, dst_it;
  gint i, ncols;
  
  ptr = data;

  src_model = gtk_tree_view_get_model(src_view);
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

/****************************************************************
...
*****************************************************************/
static void dst_row_callback(GtkTreeView *dst_view, GtkTreePath *path,
			     GtkTreeViewColumn *col, gpointer data)
{
  struct worklist_data *ptr;
  GtkTreeModel *dst_model;
  GtkTreeIter it;

  ptr = data;
  dst_model = gtk_tree_view_get_model(dst_view);
  
  gtk_tree_model_get_iter(dst_model, &it, path);
  gtk_list_store_remove(GTK_LIST_STORE(dst_model), &it);
  commit_worklist(ptr);
}

/****************************************************************
...
*****************************************************************/
static gboolean dst_dnd_callback(GtkWidget *w, GdkDragContext *context,
				 struct worklist_data *ptr)
{
  commit_worklist(ptr);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void cell_render_func(GtkTreeViewColumn *col, GtkCellRenderer *rend,
			     GtkTreeModel *model, GtkTreeIter *it,
			     gpointer data)
{
  struct city *pcity;
  gint column;
  char *row[4];
  char  buf[4][64];
  gint  cid;
  int   i;
  bool  is_unit;
 
  pcity = (struct city *) data;
  column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(rend), "column"));

  for (i = 0; i < ARRAY_SIZE(row); i++) {
    row[i] = buf[i];
  }

  gtk_tree_model_get(model, it, 0, &cid, -1);
  is_unit = cid_is_unit(cid);
  
  get_city_dialog_production_row(row, sizeof(buf[0]), cid_id(cid),
				 is_unit, pcity);
  g_object_set(rend, "text", row[column], NULL);
}

/****************************************************************
...
*****************************************************************/
GtkWidget *create_worklist(struct worklist *pwl, struct city *pcity)
{
  GtkWidget *editor, *table, *sw, *bbox;
  GtkWidget *src_view, *dst_view, *label, *button;
  GtkWidget *menubar, *menuitem, *menu;
  GtkWidget *align, *arrow, *check;
  GtkSizeGroup *group;

  GtkListStore *src_store, *dst_store;

  static char *titles[] =
    { N_("Type"), N_("Info"), N_("Cost"), N_("Turns") };

  static bool titles_done;
  gint i;

  int ntitles;

  struct worklist_data *ptr;

  
  ptr = fc_malloc(sizeof(*ptr));

  src_store = gtk_list_store_new(1, G_TYPE_INT);
  dst_store = gtk_list_store_new(1, G_TYPE_INT);
  
  ptr->pwl = pwl;
  ptr->pcity = pcity;
  ptr->src = src_store;
  ptr->dst = dst_store;
  ptr->advanced = FALSE;

  
  intl_slist(ARRAY_SIZE(titles), titles, &titles_done);
  ntitles = ARRAY_SIZE(titles) - (pcity ? 0 : 1);
  
  /* worklist editor. */
  editor = gtk_vbox_new(FALSE, 0);
  g_signal_connect(editor, "destroy", G_CALLBACK(worklist_destroy), pwl);
  g_object_set_data(G_OBJECT(editor), "data", ptr);

  table = gtk_table_new(2, 5, FALSE);
  gtk_box_pack_start(GTK_BOX(editor), table, TRUE, TRUE, 0);

  group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_table_attach(GTK_TABLE(table), sw, 3, 5, 1, 2,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  src_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(src_store));
  g_object_unref(src_store);
  gtk_size_group_add_widget(group, src_view);

  for (i = 0; i < ntitles; i++) {
    GtkCellRenderer *rend;
    GtkTreeViewColumn *col;
  
    rend = gtk_cell_renderer_text_new();
    g_object_set_data(G_OBJECT(rend), "column", GINT_TO_POINTER(i));

    gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(src_view),
      i, titles[i], rend, cell_render_func, pcity, NULL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(src_view), i);

    if (i >= 2) {
      g_object_set(G_OBJECT(rend), "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
    }
  }
  gtk_container_add(GTK_CONTAINER(sw), src_view);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", src_view,
		       "label", _("Source _Tasks:"),
		       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_table_attach(GTK_TABLE(table), label, 3, 4, 0, 1,
		   GTK_FILL, GTK_FILL, 0, 0);

  check = gtk_check_button_new_with_mnemonic(_("Ad_vanced"));
  gtk_table_attach(GTK_TABLE(table), check, 4, 5, 0, 1,
		   0, GTK_FILL, 0, 0);
  g_signal_connect(check, "toggled", G_CALLBACK(advanced_callback), ptr);


  align = gtk_alignment_new(0.5, 0.5, 6.0, 1.0);
  gtk_table_attach(GTK_TABLE(table), align, 2, 3, 1, 2,
		   GTK_FILL, GTK_FILL, 0, 0);
  
  arrow = gtk_arrow_new(GTK_ARROW_LEFT, GTK_SHADOW_NONE);
  gtk_container_add(GTK_CONTAINER(align), arrow);


  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_table_attach(GTK_TABLE(table), sw, 0, 2, 1, 2,
		   GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  dst_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dst_store));
  g_object_unref(dst_store);
  gtk_size_group_add_widget(group, dst_view);

  for (i = 0; i < ntitles; i++) {
    GtkCellRenderer *rend;
    GtkTreeViewColumn *col;
  
    rend = gtk_cell_renderer_text_new();
    g_object_set_data(G_OBJECT(rend), "column", GINT_TO_POINTER(i));

    gtk_tree_view_insert_column_with_data_func(GTK_TREE_VIEW(dst_view),
      i, titles[i], rend, cell_render_func, pcity, NULL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(dst_view), i);

    if (i >= 2) {
      g_object_set(G_OBJECT(rend), "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
    }
  }
  gtk_container_add(GTK_CONTAINER(sw), dst_view);

  label = g_object_new(GTK_TYPE_LABEL,
		       "use-underline", TRUE,
		       "mnemonic-widget", dst_view,
		       "label", _("Target _Worklist:"),
		       "xalign", 0.0, "yalign", 0.5, NULL);
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
		   GTK_FILL, GTK_FILL, 0, 0);

  bbox = gtk_hbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(bbox), 6);
  gtk_box_pack_start(GTK_BOX(editor), bbox, FALSE, FALSE, 0);
  
  menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(bbox), menubar, FALSE, FALSE, 0);

  menu = gtk_menu_new();
  
  menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ADD, NULL);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuitem);
  g_signal_connect(menu, "show", G_CALLBACK(popup_add_menu), ptr);

  button = gtk_button_new_from_stock(GTK_STOCK_HELP);
  gtk_box_pack_end(GTK_BOX(bbox), button, FALSE, FALSE, 0);
  g_signal_connect(button, "clicked",
		   G_CALLBACK(help_callback), src_view);

  
  ptr->src_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(src_view));
  ptr->dst_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dst_view));
  
  
  if (!pcity || (can_client_issue_orders() &&
		 city_owner(pcity) == game.player_ptr)) {
    gtk_tree_view_set_reorderable(GTK_TREE_VIEW(dst_view), TRUE);

    g_signal_connect(src_view, "row_activated",
		     G_CALLBACK(src_row_callback), ptr);
    g_signal_connect(dst_view, "row_activated",
		     G_CALLBACK(dst_row_callback), ptr);
    g_signal_connect(dst_view, "drag_end",
		     G_CALLBACK(dst_dnd_callback), ptr);

    if (pcity) {
      gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(src_view),
					     GDK_BUTTON1_MASK,
					     wl_dnd_targets,
					     G_N_ELEMENTS(wl_dnd_targets),
					     GDK_ACTION_COPY);
    }
  } else {
    gtk_widget_set_sensitive(menuitem, FALSE);
  }

  
  insert_worklist(pwl, editor);
  refresh_worklist(pwl);

  return editor;
}


/****************************************************************
...
*****************************************************************/
void refresh_worklist(struct worklist *pwl)
{
  GtkWidget *editor;
  
  if ((editor = get_worklist(pwl))) {
    struct worklist_data *ptr;

    cid cids[U_LAST + B_LAST];
    int i, cids_used;
    struct item items[U_LAST + B_LAST];

    bool selected;
    gint id;
    GtkTreeIter it;


    ptr = g_object_get_data(G_OBJECT(editor), "data");

    /* refresh source tasks. */
    if (gtk_tree_selection_get_selected(ptr->src_selection, NULL, &it)) {
      gtk_tree_model_get(GTK_TREE_MODEL(ptr->src), &it, 0, &id, -1);
      selected = TRUE;
    } else {
      selected = FALSE;
    }
    gtk_list_store_clear(ptr->src);

    cids_used = collect_cids4(cids, ptr->pcity, ptr->advanced);
    name_and_sort_items(cids, cids_used, items, TRUE, ptr->pcity);

    for (i = 0; i < cids_used; i++) {
      gtk_list_store_append(ptr->src, &it);
      gtk_list_store_set(ptr->src, &it, 0, (gint) items[i].cid, -1);

      if (selected && items[i].cid == id) {
	gtk_tree_selection_select_iter(ptr->src_selection, &it);
      }
    }


    /* refresh target worklist. */
    gtk_list_store_clear(ptr->dst);

    for (i = 0; i < MAX_LEN_WORKLIST; i++) {
      cid cid;

      if (pwl->wlefs[i] == WEF_END) {
	break;
      }

      cid = cid_encode(pwl->wlefs[i] == WEF_UNIT, pwl->wlids[i]);
      
      gtk_list_store_append(ptr->dst, &it);
      gtk_list_store_set(ptr->dst, &it, 0, (gint) cid, -1);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void commit_worklist(struct worklist_data *ptr)
{
  struct worklist *pwl;
  GtkTreeModel *model;
  GtkTreeIter it;
  int i;
  char name[MAX_LEN_NAME];

  pwl = ptr->pwl;
  model = GTK_TREE_MODEL(ptr->dst);
  
  strcpy(name, pwl->name);
  init_worklist(pwl);

  i = 0;
  if (gtk_tree_model_get_iter_first(model, &it)) {
    do {
      gint cid;
      
      gtk_tree_model_get(model, &it, 0, &cid, -1);
      pwl->wlefs[i] = cid_is_unit(cid) ? WEF_UNIT : WEF_IMPR;
      pwl->wlids[i] = cid_id(cid);

      i++;

      /* oops, the player has a worklist longer than what we can store. */
      if (i == MAX_LEN_WORKLIST) {
	break;
      }
      
    } while (gtk_tree_model_iter_next(model, &it));
  }

  strcpy(pwl->name, name);

  if (ptr->pcity) {
    struct packet_city_request packet;

    packet.city_id = ptr->pcity->id;
    copy_worklist(&packet.worklist, pwl);
    packet.worklist.name[0] = '\0';
    
    send_packet_city_request(&aconnection, &packet, PACKET_CITY_WORKLIST);
  }
}

