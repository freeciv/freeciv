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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> /* sqrt */

#include <gtk/gtk.h>

/* utility */
#include "fcintl.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "government.h"
#include "movement.h"
#include "nation.h"
#include "specialist.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "research.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "helpdata.h"
#include "options.h"
#include "tilespec.h"

/* client/gui-gtk-4.0 */
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_stuff.h"

#include "helpdlg.h"

#define TECH_TREE_DEPTH         20

/*
 * Globals.
 */
static GtkWidget *help_dialog_shell;
static GtkWidget *help_view_sw;

static GtkWidget *help_view;

static GtkWidget *help_frame;
static GtkTextBuffer *help_text;
static GtkWidget *help_text_sw;
static GtkWidget *help_vbox;
static GtkWidget *help_tile;
static GtkWidget *help_box;
static GtkWidget *help_itable;
static GtkWidget *help_wtable;
static GtkWidget *help_utable;
static GtkWidget *help_ttable;
static GtkWidget *help_etable;
static GtkWidget *help_tree;
static GtkTreeStore *tstore;

static GtkWidget *help_tree_sw;
static GtkWidget *help_tree_expand;
static GtkWidget *help_tree_collapse;
static GtkWidget *help_tree_buttons_hbox;
static GtkWidget *help_ilabel[6];
static GtkWidget *help_wlabel[6];
static GtkWidget *help_ulabel[5][5];
static GtkWidget *help_tlabel[2][5];
static GtkWidget *help_elabel[6];

static bool help_advances[A_LAST];

static GPtrArray *help_history;
static int        help_history_pos;

struct help_page_selection
{
  enum { HPAGE_SRC_ENUM, HPAGE_SRC_REQ } type;
  union {
    enum help_page_type page;
    const struct requirement *req;
  } u;
};

static const char *help_ilabel_name[6] =
{ N_("Base Cost:"), NULL, N_("Upkeep:"), NULL, N_("Requirement:"), NULL };
static struct help_page_selection help_impr_req;

static const char *help_wlabel_name[6] =
{ N_("Base Cost:"), NULL, N_("Requirement:"), NULL, N_("Obsolete by:"), NULL };
static struct help_page_selection help_wndr_req;

static struct help_page_selection page_selections[HELP_LAST];

static const char *help_ulabel_name[5][5] =
{
    { N_("Cost:"),              NULL, NULL, N_("Attack:"),      NULL },
    { N_("Defense:"),           NULL, NULL, N_("Move:"),        NULL },
    { N_("Firepower:"),         NULL, NULL, N_("Hitpoints:"),   NULL },
    { N_("Basic Upkeep:"),      NULL, NULL, N_("Vision:"),      NULL },
    { N_("Requirement:"),       NULL, NULL, N_("Obsolete by:"), NULL }
};

static const char *help_tlabel_name[2][5] =
{
    { N_("Move/Defense:"),      NULL, NULL, N_("Food/Res/Trade:"),      NULL },
    { N_("Resources:"),         NULL, NULL, NULL,                       NULL }
};

static const char *help_elabel_name[6] =
/* TRANS: Label for build cost for extras in help. Will be followed by
 * something like "3 MP" (where MP = Movement Points) */
{ N_("Build:"), NULL,
/* TRANS: Extra conflicts in help. Will be followed by a list of extras
 * that can't be built on the same tile as this one. */
  N_("Conflicts with:"), NULL,
/* TRANS: Extra bonus in help. Will be followed by food/production/trade
 * stats like "0/0/+1", "0/+50%/0" */
  N_("Bonus (F/P/T):"), NULL };

#define REQ_LABEL_NONE _("?tech:None")
#define REQ_LABEL_NEVER _("(Never)")

static void create_help_dialog(void);
static void help_update_dialog(const struct help_item *pitem);

static void select_help_item_string(const char *item,
                                    enum help_page_type htype);
static void help_command_update(void);
static void help_command_callback(GtkWidget *w, gint response_id);

/**********************************************************************//**
  Initialize help system.
**************************************************************************/
void help_system_init(void)
{
  enum help_page_type page;

  for (page = 0; page < HELP_LAST; page++) {
    page_selections[page].type = HPAGE_SRC_ENUM;
    page_selections[page].u.page = page;
  }

  help_impr_req.type = HPAGE_SRC_REQ;
  help_wndr_req.type = HPAGE_SRC_REQ;
}

/**********************************************************************//**
  Set topic specific title for help_frame
**************************************************************************/
static void set_title_topic(char *topic)
{
  if (strcmp(topic, _(HELP_ABOUT_ITEM)) == 0) {
    gtk_frame_set_label(GTK_FRAME(help_frame), freeciv_name_version());
  } else {
    gtk_frame_set_label(GTK_FRAME(help_frame), topic);
  }
}

/**********************************************************************//**
  Close help dialog.
**************************************************************************/
void popdown_help_dialog(void)
{
  if (help_dialog_shell) {
    gtk_window_destroy(GTK_WINDOW(help_dialog_shell));
  }
}

/**********************************************************************//**
  Popup help dialog for given item of given type.
**************************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type htype)
{
  if (!help_dialog_shell) {
    create_help_dialog();
  }
  gtk_window_present(GTK_WINDOW(help_dialog_shell));

  select_help_item_string(item, htype);
}

/**********************************************************************//**
  Not sure if this should call Q_(item) as it does, or whether all
  callers of this function should do so themselves... --dwp
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(Q_(item), HELP_ANY);
}

/**********************************************************************//**
  Called by help_update_tech and itself
  Creates a node in the given tree for the given tech, and creates child
  nodes for any children it has up to levels deep. These are then expanded
  if they are less than expanded_levels deep. Avoids generating redundant
  subtrees, so that if Literacy occurs twice in a tech tree, only the first
  will have children. Color codes the node based on when it will be
  discovered: red >2 turns, yellow 1 turn, green 0 turns (discovered).
**************************************************************************/
static void create_tech_tree(int tech, int levels, GtkTreeIter *parent)
{
  const struct research *presearch;
  int           bg;
  int           turns_to_tech;
  bool          original;
  GtkTreeIter   l;
  GValue        value = { 0, };
  enum tech_state state;

  if (advance_required(tech, AR_ONE) == A_LAST
      && advance_required(tech, AR_TWO) == A_LAST) {
    bg = COLOR_REQTREE_UNKNOWN;

    gtk_tree_store_append(tstore, &l, parent);
    help_advances[tech] = TRUE;

    g_value_init(&value, G_TYPE_STRING);
    g_value_set_static_string(&value, _("Removed"));
    gtk_tree_store_set_value(tstore, &l, 0, &value);
    g_value_unset(&value);

    gtk_tree_store_set(tstore, &l,
                       1, -1,
                       2, tech,
                       3, &get_color(tileset, bg)->color
                       -1);
    return;
  }

  presearch = research_get(client_player());

  state = research_invention_state(presearch, tech);
  if (tech_state_is_valid(state)) {
    switch (state) {
    case TECH_UNKNOWN:
      bg = COLOR_REQTREE_UNKNOWN;
      break;
    case TECH_KNOWN:
      bg = COLOR_REQTREE_KNOWN;
      break;
    case TECH_PREREQS_KNOWN:
      bg = COLOR_REQTREE_PREREQS_KNOWN;
      break;
    }
  } else {
    bg = COLOR_REQTREE_BACKGROUND;
  }
  turns_to_tech = research_goal_unknown_techs(presearch, tech);

  /* l is the original in the tree. */
  original = !help_advances[tech];

  gtk_tree_store_append(tstore, &l, parent);
  help_advances[tech] = TRUE;

  g_value_init(&value, G_TYPE_STRING);
  g_value_set_static_string(&value,
                            research_advance_name_translation(presearch,
                                                              tech));
  gtk_tree_store_set_value(tstore, &l, 0, &value);
  g_value_unset(&value);

  gtk_tree_store_set(tstore, &l,
                     1, turns_to_tech,
                     2, tech,
                     3, &get_color(tileset, bg)->color,
                     -1);

  if (--levels <= 0) {
    return;
  }

  if (original) {
    /* Only add children to orginals */
    if (advance_required(tech, AR_ONE) != A_NONE)
      create_tech_tree(advance_required(tech, AR_ONE), levels, &l);
    if (advance_required(tech, AR_TWO) != A_NONE)
      create_tech_tree(advance_required(tech, AR_TWO), levels, &l);
  }
}

/**********************************************************************//**
  Selects the help page for the tech in the tree that was double clicked.
**************************************************************************/
static void help_tech_tree_activated_callback(GtkTreeView *view,
                                              GtkTreePath *path,
                                              GtkTreeViewColumn *col,
                                              gpointer data)
{
  GtkTreeIter it;
  gint tech;

  gtk_tree_model_get_iter(GTK_TREE_MODEL(tstore), &it, path);
  gtk_tree_model_get(GTK_TREE_MODEL(tstore), &it, 2, &tech, -1);
  select_help_item_string(advance_name_translation(advance_by_number(tech)),
                          HELP_TECH);
}

/**********************************************************************//**
  Called when "Expand All" button is clicked
**************************************************************************/
static void help_tech_tree_expand_callback(GtkWidget *w, gpointer data)
{
  gtk_tree_view_expand_all(GTK_TREE_VIEW(data));
}

/**********************************************************************//**
  Called when "Collapse All" button is clicked
**************************************************************************/
static void help_tech_tree_collapse_callback(GtkWidget *w, gpointer data)
{
  gtk_tree_view_collapse_all(GTK_TREE_VIEW(data));
}

/**********************************************************************//**
  Hyperlink clicked
**************************************************************************/
static void help_hyperlink_callback(GtkWidget *w)
{
  const char *s;
  struct help_page_selection *select;
  enum help_page_type type = HELP_LAST;

  s = gtk_label_get_text(GTK_LABEL(w));
  select = (struct help_page_selection *)(g_object_get_data(G_OBJECT(w),
                                                            "page_type"));

  switch (select->type) {
  case HPAGE_SRC_ENUM:
    type = select->u.page;
    break;
  case HPAGE_SRC_REQ:
    type = help_type_by_requirement(select->u.req);
    break;
  }

  if (type == HELP_LAST) {
    return;
  }

  /* FIXME: May be able to skip, or may need to modify, advances[A_NONE]
     below, depending on which i18n is done elsewhere.
  */
  if (strcmp(s, REQ_LABEL_NEVER) != 0
      && strcmp(s, skip_intl_qualifier_prefix(REQ_LABEL_NONE)) != 0
      && strcmp(s, advance_name_translation(advance_by_number(A_NONE))) != 0) {
    select_help_item_string(s, type);
  }
}

/**********************************************************************//**
  Create new hyperlink button.
**************************************************************************/
static GtkWidget *help_hyperlink_new(GtkWidget *label,
                                     struct help_page_selection *select)
{
  GtkWidget *button;

  button = gtk_button_new();
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_name(label, "help_link");
  gtk_button_set_child(GTK_BUTTON(button), label);
  gtk_widget_set_visible(button, TRUE);
  g_signal_connect_swapped(button, "clicked",
                           G_CALLBACK(help_hyperlink_callback), label);
  g_object_set_data(G_OBJECT(label), "page_type", select);

  return button;
}

/**********************************************************************//**
  Create new hyperlink button for a known page.
**************************************************************************/
static GtkWidget *help_hyperlink_new_page(GtkWidget *label,
                                          enum help_page_type page)
{
  return help_hyperlink_new(label, &(page_selections[page]));
}

/**********************************************************************//**
  Create new hyperlink button with text.
**************************************************************************/
static GtkWidget *help_slink_new(const gchar *txt,
                                 struct help_page_selection *select)
{
  GtkWidget *button, *label;

  label = gtk_label_new(txt);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  button = help_hyperlink_new(label, select);

  return button;
}

/**********************************************************************//**
  Create new hyperlink button with text for known page.
**************************************************************************/
static GtkWidget *help_slink_new_page(const gchar *txt,
                                      enum help_page_type page)
{
  return help_slink_new(txt, &(page_selections[page]));
}

/**********************************************************************//**
  Hide help box.
**************************************************************************/
static void help_box_hide(void)
{
  gtk_widget_set_visible(help_box, FALSE);

  gtk_widget_set_visible(help_tile, FALSE);

  gtk_widget_set_visible(help_itable, FALSE);
  gtk_widget_set_visible(help_wtable, FALSE);
  gtk_widget_set_visible(help_utable, FALSE);
  gtk_widget_set_visible(help_ttable, FALSE);
  gtk_widget_set_visible(help_etable, FALSE);

  gtk_widget_set_visible(help_vbox, FALSE);
  gtk_widget_set_visible(help_text_sw, FALSE);

  gtk_widget_set_visible(help_tree_sw, FALSE);
  gtk_widget_set_visible(help_tree_buttons_hbox, FALSE);
}

/**********************************************************************//**
  Add widget to help box.
**************************************************************************/
static void help_box_add(GtkWidget *wdg)
{
  gtk_box_append(GTK_BOX(help_vbox), wdg);
}

/**********************************************************************//**
  Clear help box.
**************************************************************************/
static void help_box_clear(void)
{
  GtkWidget *child = gtk_widget_get_first_child(help_vbox);

  while (child != NULL) {
    gtk_box_remove(GTK_BOX(help_vbox), child);
    child = gtk_widget_get_first_child(help_vbox);
  }
}

/**********************************************************************//**
  Completely destroy help dialog.
**************************************************************************/
static void help_destroy_callback(GtkWidget *w, gpointer data)
{
  g_ptr_array_free(help_history, TRUE);
  help_dialog_shell = NULL;
}

/**********************************************************************//**
  New topic activated from help dialog.
**************************************************************************/
static void activated_topic(GtkTreeView *view, gpointer data)
{
  GtkTreePath *path;
  GtkTreeViewColumn *col;
  GtkTreeModel *model;
  GtkTreeIter it;
  struct help_item *pitem;

  model = gtk_tree_view_get_model(view);

  if (model == NULL) {
    return;
  }

  gtk_tree_view_get_cursor(view, &path, &col);
  gtk_tree_model_get_iter(model, &it, path);
  gtk_tree_path_free(path);

  if (path == NULL) {
    return;
  }

  gtk_tree_model_get(model, &it, 1, &pitem, -1);

  if (help_history_pos >= 0
      && g_ptr_array_index(help_history, help_history_pos) == (gpointer) pitem) {
    return;
  }

  help_update_dialog(pitem);

  /* Add to history. */
  if (help_history_pos < help_history->len - 1) {
    g_ptr_array_set_size(help_history, help_history_pos + 1);
  }
  help_history_pos++;

  g_ptr_array_add(help_history, (gpointer)pitem);
  help_command_update();
}

/**********************************************************************//**
  Create help dialog.
**************************************************************************/
static void create_help_dialog(void)
{
  GtkWidget *hbox;
  GtkWidget *button;
  GtkWidget *text;
  int        i, j;
  GtkCellRenderer   *rend;
  GtkTreeViewColumn *col;
  GArray            *array;
  GtkTreeStore      *store;
  GtkTreeSelection  *selection;
  GtkWidget *tile_sw;

  help_history = g_ptr_array_new();
  help_history_pos = -1;

  help_dialog_shell = gtk_dialog_new_with_buttons(_("Freeciv Help Browser"),
                                                  NULL,
                                                  0,
                                                  _("_Back"),
                                                  1,
                                                  _("_Forward"),
                                                  2,
                                                  _("_Close"),
                                                  GTK_RESPONSE_CLOSE,
                                                  NULL);
  setup_dialog(help_dialog_shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(help_dialog_shell),
                                  GTK_RESPONSE_CLOSE);
  gtk_widget_set_name(help_dialog_shell, "Freeciv");

  g_signal_connect(help_dialog_shell, "response",
                   G_CALLBACK(help_command_callback), NULL);
  g_signal_connect(help_dialog_shell, "destroy",
                   G_CALLBACK(help_destroy_callback), NULL);
  gtk_window_set_default_size(GTK_WINDOW(help_dialog_shell),
                              GUI_GTK_OPTION(helpdlg_xsize),
                              GUI_GTK_OPTION(helpdlg_ysize));

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(help_dialog_shell))),
                 hbox);
  gtk_widget_set_visible(hbox, TRUE);

  /* Build tree store. */
  store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

  array = g_array_new(FALSE, FALSE, sizeof(GtkTreeIter));
  help_items_iterate(pitem) {
    GtkTreeIter *it, *parent;
    const char *s;
    int depth;

    for (s = pitem->topic; *s == ' '; s++) {
      /* Nothing */
    }
    depth = s - pitem->topic;

    array = g_array_set_size(array, depth+1);

    if (depth > 0) {
      parent = &g_array_index(array, GtkTreeIter, depth-1);
    } else {
      parent = NULL;
    }

    it = &g_array_index(array, GtkTreeIter, depth);
    gtk_tree_store_append(store, it, parent);

    gtk_tree_store_set(store, it, 0, pitem->topic, 1, pitem, -1);
  } help_items_iterate_end;

  /* Create tree view. */
  help_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(help_view));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(help_view), FALSE);

  g_signal_connect(help_view, "cursor-changed",
                   G_CALLBACK(activated_topic), NULL);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(help_view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend, "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(help_view), col);

  help_view_sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_view_sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(help_view_sw, 190, -1);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(help_view_sw), help_view);
  gtk_widget_set_visible(help_view, TRUE);
  gtk_box_append(GTK_BOX(hbox), help_view_sw);
  gtk_widget_set_visible(help_view_sw, TRUE);

  help_frame = gtk_frame_new("");
  gtk_box_append(GTK_BOX(hbox), help_frame);
  gtk_widget_set_hexpand(help_frame, TRUE);
  gtk_widget_set_visible(help_frame, TRUE);

  help_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_frame_set_child(GTK_FRAME(help_frame), help_box);

  help_tile = gtk_picture_new();
  gtk_picture_set_can_shrink(GTK_PICTURE(help_tile), FALSE);
  gtk_widget_set_valign(help_tile, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(help_tile, GTK_ALIGN_CENTER);
  tile_sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tile_sw),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_NEVER);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tile_sw), help_tile);
  gtk_box_append(GTK_BOX(help_box), tile_sw);

  help_itable = gtk_grid_new();
  gtk_box_append(GTK_BOX(help_box), help_itable);

  for (i = 0; i < 6; i++) {
    help_ilabel[i] =
      gtk_label_new(help_ilabel_name[i] != NULL ? _(help_ilabel_name[i]) : "");
    gtk_widget_set_hexpand(help_ilabel[i], TRUE);

    if (i == 5) {
      button = help_hyperlink_new(help_ilabel[i], &help_impr_req);
      gtk_grid_attach(GTK_GRID(help_itable), button, i, 0, 1, 1);
    } else {
      gtk_grid_attach(GTK_GRID(help_itable), help_ilabel[i], i, 0, 1, 1);
      gtk_widget_set_name(help_ilabel[i], "help_label");
    }

    gtk_widget_set_visible(help_ilabel[i], TRUE);
  }

  help_wtable = gtk_grid_new();
  gtk_box_append(GTK_BOX(help_box), help_wtable);

  for (i = 0; i < 6; i++) {
    help_wlabel[i] =
      gtk_label_new(help_wlabel_name[i] ? _(help_wlabel_name[i]) : "");
    gtk_widget_set_hexpand(help_wlabel[i], TRUE);

    if (i == 3 || i == 5) {
      button = help_hyperlink_new(help_wlabel[i], &help_wndr_req);
      gtk_grid_attach(GTK_GRID(help_wtable), button, i, 0, 1, 1);
    } else {
      gtk_grid_attach(GTK_GRID(help_wtable), help_wlabel[i], i, 0, 1, 1);
      gtk_widget_set_name(help_wlabel[i], "help_label");
    }

    gtk_widget_set_visible(help_wlabel[i], TRUE);
  }

  help_utable = gtk_grid_new();
  gtk_box_append(GTK_BOX(help_box), help_utable);

  for (i = 0; i < 5; i++) {
    for (j = 0; j < 5; j++) {
      help_ulabel[j][i] =
        gtk_label_new(help_ulabel_name[j][i] ? _(help_ulabel_name[j][i]) : "");
      gtk_widget_set_hexpand(help_ulabel[j][i], TRUE);

      if (j == 4 && (i == 1 || i == 4)) {
        if (i == 1) {
          button = help_hyperlink_new_page(help_ulabel[j][i], HELP_TECH);
        } else {
          button = help_hyperlink_new_page(help_ulabel[j][i], HELP_UNIT);
        }

        gtk_grid_attach(GTK_GRID(help_utable), button, i, j, 1, 1);
      } else {
        gtk_grid_attach(GTK_GRID(help_utable), help_ulabel[j][i],
                        i, j, 1, 1);
        gtk_widget_set_name(help_ulabel[j][i], "help_label");
      }

      gtk_widget_set_visible(help_ulabel[j][i], TRUE);
    }
  }

  help_ttable = gtk_grid_new();
  gtk_box_append(GTK_BOX(help_box), help_ttable);

  for (j = 0; j < 2; j++) {
    for (i = 0; i < 5; i++) {
      help_tlabel[j][i] =
        gtk_label_new(help_tlabel_name[j][i] ? _(help_tlabel_name[j][i]) : "");
      gtk_widget_set_hexpand(help_tlabel[j][i], TRUE);
      gtk_widget_set_name(help_tlabel[j][i], "help_label");

      /* Ugly (but these numbers are hardcoded in help_update_terrain() too) */
      if (j == 1 && i == 1) {
        /* Extra wide cell for terrain specials */
        gtk_grid_attach(GTK_GRID(help_ttable), help_tlabel[j][i],
                        i, j, 4, 1);
        gtk_widget_set_visible(help_tlabel[j][i], TRUE);
        break; /* Skip rest of row */
      } else {
        gtk_grid_attach(GTK_GRID(help_ttable), help_tlabel[j][i],
                        i, j, 1, 1);
        gtk_widget_set_visible(help_tlabel[j][i], TRUE);
      }
    }
  }

  help_etable = gtk_grid_new();
  gtk_box_append(GTK_BOX(help_box), help_etable);

  for (i = 0; i < 6; i++) {
    help_elabel[i]
      = gtk_label_new(help_elabel_name[i] ? _(help_elabel_name[i]) : "");
    gtk_widget_set_hexpand(help_elabel[i], TRUE);
    gtk_grid_attach(GTK_GRID(help_etable), help_elabel[i], i % 4, i / 4, 1, 1);
    gtk_widget_set_name(help_elabel[i], "help_label");
    gtk_widget_set_visible(help_elabel[i], TRUE);
  }

  help_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  gtk_widget_set_margin_start(help_vbox, 5);
  gtk_widget_set_margin_end(help_vbox, 5);
  gtk_widget_set_margin_top(help_vbox, 5);
  gtk_widget_set_margin_bottom(help_vbox, 5);
  gtk_box_append(GTK_BOX(help_box), help_vbox);

  text = gtk_text_view_new();
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_widget_set_margin_start(text, 5);
  gtk_widget_set_margin_end(text, 5);
  gtk_widget_set_margin_top(text, 5);
  gtk_widget_set_margin_bottom(text, 5);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_widget_set_name(text, "help_text");
  help_text = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
  gtk_widget_set_visible(text, TRUE);

  help_text_sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_text_sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(help_text_sw), text);
  gtk_box_append(GTK_BOX(help_box), help_text_sw);

  /* Build tech store. */
  tstore = gtk_tree_store_new(4,
                              G_TYPE_STRING,    /* Tech name */
                              G_TYPE_INT,       /* Turns to tech */
                              G_TYPE_INT,       /* Tech id */
                              GDK_TYPE_RGBA);   /* Color */
  help_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tstore));
  gtk_widget_set_hexpand(help_tree, TRUE);
  gtk_widget_set_vexpand(help_tree, TRUE);
  g_object_unref(tstore);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(help_tree), FALSE);

  g_signal_connect(help_tree, "row_activated",
                   G_CALLBACK(help_tech_tree_activated_callback), NULL);


  col = gtk_tree_view_column_new();

  rend = gtk_cell_renderer_text_new();
  g_object_set(rend, "weight", PANGO_WEIGHT_BOLD, NULL);
  gtk_tree_view_column_pack_start(col, rend, TRUE);
  gtk_tree_view_column_set_attributes(col, rend,
                                      "text", 0,
                                      "background-rgba", 3,
                                      NULL);
  rend = gtk_cell_renderer_text_new();
  g_object_set(rend, "weight", PANGO_WEIGHT_BOLD, "xalign", 1.0, NULL);
  gtk_tree_view_column_pack_start(col, rend, FALSE);
  gtk_tree_view_column_set_attributes(col, rend,
                                      "text", 1,
                                      "background-rgba", 3,
                                      NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(help_tree), col);

  help_tree_sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(help_tree_sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(help_tree_sw), help_tree);
  gtk_widget_set_size_request(help_tree_sw, -1, 200);
  gtk_widget_set_visible(help_tree, TRUE);
  gtk_box_append(GTK_BOX(help_box), help_tree_sw);

  help_tree_expand = gtk_button_new_with_label(_("Expand All"));
  help_tree_collapse = gtk_button_new_with_label(_("Collapse All"));

  g_signal_connect(help_tree_expand, "clicked",
                   G_CALLBACK(help_tech_tree_expand_callback), help_tree);
  g_signal_connect(help_tree_collapse, "clicked",
                   G_CALLBACK(help_tech_tree_collapse_callback), help_tree);

  help_tree_buttons_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_append(GTK_BOX(help_tree_buttons_hbox), help_tree_expand);
  gtk_box_append(GTK_BOX(help_tree_buttons_hbox), help_tree_collapse);
  gtk_box_append(GTK_BOX(help_box), help_tree_buttons_hbox);
  gtk_widget_set_visible(help_tree_buttons_hbox, TRUE);
}

/**********************************************************************//**
  Set sprite to show for current help item.
**************************************************************************/
static void set_help_tile_from_sprite(struct sprite *spr)
{
  if (spr == nullptr) {
    return;
  }

  picture_set_from_surface(GTK_PICTURE(help_tile), spr->surface);
  gtk_widget_set_visible(help_tile, TRUE);
}

/**********************************************************************//**
  Set sprite to show for current terrain.
**************************************************************************/
static void set_help_tile_from_terrain(struct terrain *pterr)
{
  struct canvas canvas = FC_STATIC_CANVAS_INIT;
  cairo_t *cr;
  int i;

  canvas.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                              tileset_tile_width(tileset),
                                              tileset_tile_height(tileset));

  cr = cairo_create(canvas.surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);

  for (i = 0; i < 3; i++) {
    struct drawn_sprite sprs[80];
    int count = fill_basic_terrain_layer_sprite_array(tileset, sprs,
                                                      i, pterr);

    put_drawn_sprites(&canvas, 1.0, 0, 0, count, sprs, FALSE);
  }

  picture_set_from_surface(GTK_PICTURE(help_tile), canvas.surface);
  gtk_widget_set_visible(help_tile, TRUE);
  cairo_surface_destroy(canvas.surface);
}

/**********************************************************************//**
  Set sprite to show for current extra.
**************************************************************************/
static void set_help_tile_from_extra(const struct extra_type *pextra)
{
  struct canvas canvas = FC_STATIC_CANVAS_INIT;
  cairo_t *cr;
  struct drawn_sprite sprs[80];
  int count;

  canvas.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                              tileset_tile_width(tileset),
                                              tileset_tile_height(tileset));

  cr = cairo_create(canvas.surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);

  count = fill_basic_extra_sprite_array(tileset, sprs, pextra);

  put_drawn_sprites(&canvas, 1.0, 0, 0, count, sprs, FALSE);

  picture_set_from_surface(GTK_PICTURE(help_tile), canvas.surface);
  gtk_widget_set_visible(help_tile, TRUE);
  cairo_surface_destroy(canvas.surface);
}

/**********************************************************************//**
  Display updated help about improvement
**************************************************************************/
static void help_update_improvement(const struct help_item *pitem,
                                    char *title)
{
  char buf[8192];
  struct impr_type *imp = improvement_by_translated_name(title);

  if (imp != NULL && !is_great_wonder(imp)) {
    const char *req = skip_intl_qualifier_prefix(REQ_LABEL_NONE);
    char req_buf[512];

    sprintf(buf, "%d", impr_base_build_shield_cost(imp));
    gtk_label_set_text(GTK_LABEL(help_ilabel[1]), buf);
    sprintf(buf, "%d", imp->upkeep);
    gtk_label_set_text(GTK_LABEL(help_ilabel[3]), buf);

    /* FIXME: this should show ranges, negated reqs, and all the reqs.
     * Currently it's limited to 1 req but this code is partially prepared
     * to be extended. */
    help_impr_req.u.req = NULL;
    requirement_vector_iterate(&imp->reqs, preq) {
      if (!preq->present) {
        continue;
      }
      req = universal_name_translation(&preq->source, req_buf, sizeof(req_buf));
      help_impr_req.u.req = preq;
      break;
    } requirement_vector_iterate_end;
    gtk_label_set_text(GTK_LABEL(help_ilabel[5]), req);
/*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3); */
  } else {
    gtk_label_set_text(GTK_LABEL(help_ilabel[1]), "0");
    gtk_label_set_text(GTK_LABEL(help_ilabel[3]), "0");
    gtk_label_set_text(GTK_LABEL(help_ilabel[5]), REQ_LABEL_NEVER);
/*    create_tech_tree(help_improvement_tree, 0, advance_count(), 3); */
  }

  set_help_tile_from_sprite(get_building_sprite(tileset, imp));

  gtk_widget_set_visible(help_itable, TRUE);

  helptext_building(buf, sizeof(buf), client_player(), pitem->text, imp);
  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  Display updated help about wonder.
**************************************************************************/
static void help_update_wonder(const struct help_item *pitem,
                               char *title)
{
  char buf[8192];
  struct impr_type *imp = improvement_by_translated_name(title);

  if (imp != NULL && is_great_wonder(imp)) {
    int i;
    char req_buf[512];

    sprintf(buf, "%d", impr_base_build_shield_cost(imp));
    gtk_label_set_text(GTK_LABEL(help_wlabel[1]), buf);

    /* FIXME: this should show ranges, negated reqs, and all the reqs.
     * Currently it's limited to 1 req but this code is partially prepared
     * to be extended. */
    i = 0;
    help_wndr_req.u.req = NULL;
    requirement_vector_iterate(&imp->reqs, preq) {
      if (!preq->present) {
        continue;
      }
      gtk_label_set_text(GTK_LABEL(help_wlabel[3 + i]),
                         universal_name_translation(&preq->source,
                                                    req_buf, sizeof(req_buf)));
      help_wndr_req.u.req = preq;
      i++;
      break;
    } requirement_vector_iterate_end;

    gtk_label_set_text(GTK_LABEL(help_wlabel[5]), REQ_LABEL_NEVER);
    requirement_vector_iterate(&imp->obsolete_by, pobs) {
      if (pobs->source.kind == VUT_ADVANCE && pobs->present) {
        gtk_label_set_text(GTK_LABEL(help_wlabel[5]),
                           advance_name_translation
                               (pobs->source.value.advance));

        break;
      }
    } requirement_vector_iterate_end;

/*    create_tech_tree(help_improvement_tree, 0, imp->tech_req, 3); */
  } else {
    /* Can't find wonder */
    gtk_label_set_text(GTK_LABEL(help_wlabel[1]), "0");
    gtk_label_set_text(GTK_LABEL(help_wlabel[3]), REQ_LABEL_NEVER);
    gtk_label_set_text(GTK_LABEL(help_wlabel[5]), skip_intl_qualifier_prefix(REQ_LABEL_NONE));
/*    create_tech_tree(help_improvement_tree, 0, advance_count(), 3); */
  }

  set_help_tile_from_sprite(get_building_sprite(tileset, imp));

  gtk_widget_set_visible(help_wtable, TRUE);

  helptext_building(buf, sizeof(buf), client_player(), pitem->text, imp);
  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  Display updated help about unit type.
**************************************************************************/
static void help_update_unit_type(const struct help_item *pitem,
                                  char *title)
{
  char buf[8192];
  struct unit_type *utype = unit_type_by_translated_name(title);

  if (utype != NULL) {
    sprintf(buf, "%d", utype_build_shield_cost_base(utype));
    gtk_label_set_text(GTK_LABEL(help_ulabel[0][1]), buf);
    sprintf(buf, "%d", utype->attack_strength);
    gtk_label_set_text(GTK_LABEL(help_ulabel[0][4]), buf);
    sprintf(buf, "%d", utype->defense_strength);
    gtk_label_set_text(GTK_LABEL(help_ulabel[1][1]), buf);
    sprintf(buf, "%s", move_points_text(utype->move_rate, TRUE));
    gtk_label_set_text(GTK_LABEL(help_ulabel[1][4]), buf);
    sprintf(buf, "%d", utype->firepower);
    gtk_label_set_text(GTK_LABEL(help_ulabel[2][1]), buf);
    sprintf(buf, "%d", utype->hp);
    gtk_label_set_text(GTK_LABEL(help_ulabel[2][4]), buf);
    gtk_label_set_text(GTK_LABEL(help_ulabel[3][1]),
                       helptext_unit_upkeep_str(utype));
    sprintf(buf, "%d", (int)sqrt((double)utype->vision_radius_sq));
    gtk_label_set_text(GTK_LABEL(help_ulabel[3][4]), buf);
    gtk_label_set_text(GTK_LABEL(help_ulabel[4][1]),
                       advance_name_translation(utype_primary_tech_req(utype)));

#if 0
    create_tech_tree(help_improvement_tree, 0,
                     advance_number(utype->require_advance), 3);
#endif

    if (U_NOT_OBSOLETED == utype->obsoleted_by) {
      gtk_label_set_text(GTK_LABEL(help_ulabel[4][4]),
                         skip_intl_qualifier_prefix(REQ_LABEL_NONE));
    } else {
      gtk_label_set_text(GTK_LABEL(help_ulabel[4][4]),
                         utype_name_translation(utype->obsoleted_by));
    }

    helptext_unit(buf, sizeof(buf), client_player(), pitem->text, utype,
                  TRUE);

    gtk_text_buffer_set_text(help_text, buf, -1);
    gtk_widget_set_visible(help_text_sw, TRUE);

    set_help_tile_from_sprite(get_unittype_sprite(tileset, utype,
                                                  ACTIVITY_LAST,
                                                  direction8_invalid()));
  } else {
    gtk_label_set_text(GTK_LABEL(help_ulabel[0][1]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[0][4]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[1][1]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[1][4]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[2][1]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[2][4]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[3][1]), "0");
    gtk_label_set_text(GTK_LABEL(help_ulabel[3][4]), "0");

    gtk_label_set_text(GTK_LABEL(help_ulabel[4][1]), REQ_LABEL_NEVER);
/*    create_tech_tree(help_improvement_tree, 0, A_LAST, 3); */
    gtk_label_set_text(GTK_LABEL(help_ulabel[4][4]),
                       skip_intl_qualifier_prefix(REQ_LABEL_NONE));

    gtk_text_buffer_set_text(help_text, buf, -1);
    gtk_widget_set_visible(help_text_sw, TRUE);
  }

  gtk_widget_set_visible(help_utable, TRUE);
}

/**********************************************************************//**
  Cut str to at max len bytes in a utf8 friendly way.
**************************************************************************/
static char *fc_chomp(char *str, size_t len)
{
  gchar *i;

  if (str == NULL || !*str) {
    return str;
  }

  i = str + len;
  for (i = g_utf8_find_prev_char(str, i);
       (i != NULL && g_unichar_isspace(g_utf8_get_char(i)));
       i = g_utf8_find_prev_char(str, i)) {
    *i = '\0';
  }

  return str;
}

/**********************************************************************//**
  Display updated help about tech.
**************************************************************************/
static void help_update_tech(const struct help_item *pitem, char *title)
{
  int j;
  GtkWidget *w, *hbox;
  char buf[8192];
  struct advance *padvance = advance_by_translated_name(title);

  if (is_regular_advance(padvance)) {
    GtkTextBuffer *txt;
    size_t len;
    Tech_type_id i = advance_number(padvance);

    help_box_clear();

    for (j = 0; j < ARRAY_SIZE(help_advances); j++) {
      help_advances[j] = FALSE;
    }

    gtk_tree_store_clear(tstore);
    create_tech_tree(i, TECH_TREE_DEPTH, NULL);
    gtk_widget_set_visible(help_tree_sw, TRUE);
    gtk_widget_set_visible(help_tree_buttons_hbox, TRUE);

    helptext_advance(buf, sizeof(buf), client.conn.playing, pitem->text, i);
    len = strlen(buf);
    fc_chomp(buf, len);

    set_help_tile_from_sprite(get_tech_sprite(tileset, i));

    w = gtk_text_view_new();
    gtk_widget_set_hexpand(w, TRUE);
    gtk_widget_set_vexpand(w, TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(w), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w), GTK_WRAP_WORD);
    gtk_widget_set_name(w, "help_text");
    gtk_widget_set_margin_start(w, 5);
    gtk_widget_set_margin_end(w, 5);
    gtk_widget_set_margin_top(w, 5);
    gtk_widget_set_margin_bottom(w, 5);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w), FALSE);
    help_box_add(w);
    gtk_widget_set_visible(w, TRUE);

    txt = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    if (txt != nullptr) {
      gtk_text_buffer_set_text(txt, buf, -1);
    }

    w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_bottom(w, 5);
    gtk_widget_set_margin_end(w, 5);
    gtk_widget_set_margin_start(w, 5);
    gtk_widget_set_margin_top(w, 5);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_widget_set_vexpand(w, TRUE);
    help_box_add(w);
    gtk_widget_set_visible(w, TRUE);

    governments_iterate(pgov) {
      /* FIXME: need a more general mechanism for this, since this
       * helptext needs to be shown in all possible req source types. */
      requirement_vector_iterate(&pgov->reqs, preq) {
        if (VUT_ADVANCE == preq->source.kind
            && preq->source.value.advance == padvance) {
          hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
          help_box_add(hbox);
          w = gtk_label_new(_("Allows"));
          gtk_box_append(GTK_BOX(hbox), w);
          w = help_slink_new_page(government_name_translation(pgov),
                                  HELP_GOVERNMENT);
          gtk_box_append(GTK_BOX(hbox), w);
          gtk_widget_set_visible(hbox, TRUE);
        }
      } requirement_vector_iterate_end;
    } governments_iterate_end;

    improvement_iterate(pimprove) {
      if (valid_improvement(pimprove)) {
        requirement_vector_iterate(&pimprove->reqs, preq) {
          if (VUT_ADVANCE == preq->source.kind
              && preq->source.value.advance == padvance) {
            hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
            help_box_add(hbox);
            w = gtk_label_new(_("Allows"));
            gtk_box_append(GTK_BOX(hbox), w);
            w = help_slink_new_page(improvement_name_translation(pimprove),
                                    is_great_wonder(pimprove)
                                    ? HELP_WONDER
                                    : HELP_IMPROVEMENT);
            gtk_box_append(GTK_BOX(hbox), w);
            gtk_widget_set_visible(hbox, TRUE);
          }
        } requirement_vector_iterate_end;
        requirement_vector_iterate(&pimprove->obsolete_by, pobs) {
          if (pobs->source.kind == VUT_ADVANCE
              && pobs->source.value.advance == padvance
              && pobs->present) {
            hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
            help_box_add(hbox);
            w = gtk_label_new(_("Obsoletes"));
            gtk_box_append(GTK_BOX(hbox), w);
            w = help_slink_new_page(improvement_name_translation(pimprove),
                                    is_great_wonder(pimprove)
                                    ? HELP_WONDER
                                    : HELP_IMPROVEMENT);
            gtk_box_append(GTK_BOX(hbox), w);
            gtk_widget_set_visible(hbox, TRUE);
          }
        } requirement_vector_iterate_end;
      }
    } improvement_iterate_end;

    unit_type_iterate(punittype) {
      if (!is_tech_req_for_utype(punittype, padvance)) {
        continue;
      }

      hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
      help_box_add(hbox);
      w = gtk_label_new(_("Allows"));
      gtk_box_append(GTK_BOX(hbox), w);
      w = help_slink_new_page(utype_name_translation(punittype), HELP_UNIT);
      gtk_box_append(GTK_BOX(hbox), w);
      gtk_widget_set_visible(hbox, TRUE);
    } unit_type_iterate_end;

    advance_iterate_all(ptest) {
      if (padvance == advance_requires(ptest, AR_ONE)) {
        if (advance_by_number(A_NONE) == advance_requires(ptest, AR_TWO)) {
          hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
          help_box_add(hbox);
          w = gtk_label_new(_("Allows"));
          gtk_box_append(GTK_BOX(hbox), w);
          w = help_slink_new_page(advance_name_translation(ptest), HELP_TECH);
          gtk_box_append(GTK_BOX(hbox), w);
          gtk_widget_set_visible(hbox, TRUE);
        } else {
          hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
          help_box_add(hbox);
          w = gtk_label_new(_("Allows"));
          gtk_box_append(GTK_BOX(hbox), w);
          w = help_slink_new_page(advance_name_translation(ptest), HELP_TECH);
          gtk_box_append(GTK_BOX(hbox), w);
          w = gtk_label_new(_("with"));
          gtk_box_append(GTK_BOX(hbox), w);
          w = help_slink_new_page(advance_name_translation(advance_requires(ptest,
                                                                            AR_TWO)),
                                  HELP_TECH);
          gtk_box_append(GTK_BOX(hbox), w);
          w = gtk_label_new(Q_("?techhelp:"));
          gtk_box_append(GTK_BOX(hbox), w);
          gtk_widget_set_visible(hbox, TRUE);
        }
      }
      if (padvance == advance_requires(ptest, AR_TWO)) {
        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        help_box_add(hbox);
        w = gtk_label_new(_("Allows"));
        gtk_box_append(GTK_BOX(hbox), w);
        w = help_slink_new_page(advance_name_translation(ptest), HELP_TECH);
        gtk_box_append(GTK_BOX(hbox), w);
        w = gtk_label_new(_("with"));
        gtk_box_append(GTK_BOX(hbox), w);
        w = help_slink_new_page(advance_name_translation(advance_requires(ptest,
                                                                          AR_ONE)),
                                HELP_TECH);
        gtk_box_append(GTK_BOX(hbox), w);
        w = gtk_label_new(Q_("?techhelp:"));
        gtk_box_append(GTK_BOX(hbox), w);
        gtk_widget_set_visible(hbox, TRUE);
      }
    } advance_iterate_all_end;

    gtk_widget_set_visible(help_vbox, TRUE);
  }
}

/**********************************************************************//**
  Add a line for an activity linking to help for result.
**************************************************************************/
static void add_act_help_for_terrain(const char *act_label,
                                     const char *result_link_label,
                                     enum help_page_type result_link_type,
                                     const char *descr_label)
{
  GtkWidget *w;
  GtkWidget *hbox;

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  help_box_add(hbox);
  w = gtk_label_new(act_label);
  gtk_box_append(GTK_BOX(hbox), w);
  w = help_slink_new_page(result_link_label, result_link_type);
  gtk_box_append(GTK_BOX(hbox), w);
  w = gtk_label_new(descr_label);
  gtk_box_append(GTK_BOX(hbox), w);

  gtk_widget_set_visible(hbox, TRUE);
}

/**********************************************************************//**
  Create widgets about all extras of one cause activity to the terrain.
**************************************************************************/
static void help_extras_of_act_for_terrain(struct terrain *pterr,
                                           enum unit_activity act,
                                           char *label)
{
  enum extra_cause cause = activity_to_extra_cause(act);

  extra_type_by_cause_iterate(cause, pextra) {
    if (pextra->buildable
        && requirement_fulfilled_by_terrain(pterr, &(pextra->reqs))) {
      add_act_help_for_terrain(label,
                               extra_name_translation(pextra), HELP_EXTRA,
                               helptext_extra_for_terrain_str(pextra, pterr,
                                                              act));
    }
  } extra_type_by_cause_iterate_end;
}

/**********************************************************************//**
  Display updated help about terrain.
**************************************************************************/
static void help_update_terrain(const struct help_item *pitem,
                                char *title)
{
  char buf[8192];
  struct terrain *pterrain = terrain_by_translated_name(title);

  if (pterrain != NULL) {
    struct universal for_terr = { .kind = VUT_TERRAIN, .value = { .terrain = pterrain }};

    set_help_tile_from_terrain(pterrain);

    {
      /* 25 => "1.25"; 50 => "1.5"; 100 => "2.0" */
      int defbonus = pterrain->defense_bonus + 100;
      int frac = defbonus % 100;

      if ((frac % 10) == 0) {
        frac /= 10;
      }
      sprintf(buf, "%d/%d.%d",
              pterrain->movement_cost, defbonus / 100, frac);
    }
    gtk_label_set_text(GTK_LABEL(help_tlabel[0][1]), buf);

    sprintf(buf, "%d/%d/%d",
            pterrain->output[O_FOOD],
            pterrain->output[O_SHIELD],
            pterrain->output[O_TRADE]);
    gtk_label_set_text(GTK_LABEL(help_tlabel[0][4]), buf);

    buf[0] = '\0';
    if (*(pterrain->resources)) {
      struct extra_type **r;

      /* TODO: include resource frequency information */
      for (r = pterrain->resources; *r; r++) {
        /* TRANS: " Whales (2/1/2)," */
        sprintf (buf + strlen (buf), " %s (%d/%d/%d),",
                 extra_name_translation(*r),
                 pterrain->output[O_FOOD]   + (*r)->data.resource->output[O_FOOD],
                 pterrain->output[O_SHIELD] + (*r)->data.resource->output[O_SHIELD],
                 pterrain->output[O_TRADE]  + (*r)->data.resource->output[O_TRADE]);
      }
      buf[strlen (buf) - 1] = '.';
    } else {
      /* TRANS: "Resources: (none)" */
      sprintf (buf + strlen (buf), _("(none)"));
    }
    gtk_label_set_text(GTK_LABEL(help_tlabel[1][1]), buf);

    help_box_clear();

    if (pterrain->cultivate_result != T_NONE
        && action_id_univs_not_blocking(ACTION_CULTIVATE,
                                        NULL, &for_terr)) {
      fc_snprintf(buf, sizeof(buf),
                  PL_("%d turn", "%d turns", pterrain->cultivate_time),
                  pterrain->cultivate_time);
      add_act_help_for_terrain(_("Cultivate Rslt/Time"),
                               terrain_name_translation(pterrain->cultivate_result),
                               HELP_TERRAIN, buf);
    }

    if (pterrain->plant_result != T_NONE
        && action_id_univs_not_blocking(ACTION_PLANT, NULL, &for_terr)) {
      fc_snprintf(buf, sizeof(buf),
                  PL_("%d turn", "%d turns", pterrain->plant_time),
                  pterrain->plant_time);
      add_act_help_for_terrain(_("Plant Rslt/Time"),
                               terrain_name_translation(pterrain->plant_result),
                               HELP_TERRAIN, buf);
    }

    if (pterrain->transform_result != T_NONE
        && action_id_univs_not_blocking(ACTION_TRANSFORM_TERRAIN,
                                        NULL, &for_terr)) {
      fc_snprintf(buf, sizeof(buf),
                  PL_("%d turn", "%d turns", pterrain->transform_time),
                  pterrain->transform_time);
      add_act_help_for_terrain(_("Trans. Rslt/Time"),
                               terrain_name_translation(pterrain->transform_result),
                               HELP_TERRAIN, buf);
    }

    if (action_id_univs_not_blocking(ACTION_IRRIGATE, NULL, &for_terr)) {
      help_extras_of_act_for_terrain(pterrain, ACTIVITY_IRRIGATE,
                                     _("Build as irrigation"));
    }
    if (action_id_univs_not_blocking(ACTION_MINE, NULL, &for_terr)) {
      help_extras_of_act_for_terrain(pterrain, ACTIVITY_MINE,
                                     _("Build as mine"));
    }
    if (action_id_univs_not_blocking(ACTION_ROAD, NULL, &for_terr)) {
      help_extras_of_act_for_terrain(pterrain, ACTIVITY_GEN_ROAD,
                                     _("Build as road"));
    }
    if (action_id_univs_not_blocking(ACTION_BASE, NULL, &for_terr)) {
      help_extras_of_act_for_terrain(pterrain, ACTIVITY_BASE,
                                     _("Build as base"));
    }
    gtk_widget_set_visible(help_vbox, TRUE);
  }

  helptext_terrain(buf, sizeof(buf), client.conn.playing,
                   pitem->text, pterrain);

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);

  gtk_widget_set_visible(help_ttable, TRUE);
}

/**********************************************************************//**
  Help page for extras.
**************************************************************************/
static void help_update_extra(const struct help_item *pitem, char *title)
{
  char buf[8192];
  struct extra_type *pextra = extra_type_by_translated_name(title);

  buf[0] = '\0';
  if (pextra == NULL) {
    strcat(buf, pitem->text);
  } else {
    struct road_type *proad = extra_road_get(pextra);
    bool is_resource = is_extra_caused_by(pextra, EC_RESOURCE);

    set_help_tile_from_extra(pextra);

    /* Cost to build */
    if (pextra->buildable) {
      if (pextra->build_time != 0) {
        /* TRANS: "MP" = movement points */
        sprintf(buf, _("%d MP"), pextra->build_time);
      } else {
        /* TRANS: Build time depends on terrain. */
        sprintf(buf, _("Terrain specific"));
      }
    } else {
      sprintf(buf, "-");
    }
    gtk_label_set_text(GTK_LABEL(help_elabel[1]), buf);
    /* Conflicting extras */
    buf[0] = '\0';
    if (is_resource) {
      /* TRANS: (Resource extra) Conflicts with: */
      strcat(buf, _("Other Resources"));
    }
    extra_type_iterate(pextra2) {
      if (!can_extras_coexist(pextra, pextra2)
          && (!is_resource || !is_extra_caused_by(pextra2, EC_RESOURCE))) {
        if (buf[0] != '\0') {
          strcat(buf, "/");
        }
        strcat(buf, extra_name_translation(pextra2));
      }
    } extra_type_iterate_end;
    /* TRANS: "Conflicts with: (none)" (extras) */
    gtk_label_set_text(GTK_LABEL(help_elabel[3]), buf[0] ? buf : _("(none)"));

    /* Bonus */
    if (proad != nullptr) {
      const char *bonus = nullptr;

      output_type_iterate(o) {
        if (proad->tile_incr[o] > 0) {
          /* TRANS: Road bonus depends on terrain. */
          bonus = _("Terrain specific");
          break;
        }
      } output_type_iterate_end;
      if (bonus == nullptr) {
        bonus = helptext_road_bonus_str(nullptr, proad);

        if (bonus == nullptr) {
          /* TRANS: No output bonus from a road */
          bonus = Q_("?bonus:None");
        }
      }
      gtk_label_set_text(GTK_LABEL(help_elabel[5]), bonus);
      gtk_widget_set_visible(GTK_WIDGET(help_elabel[4]), TRUE);
      gtk_widget_set_visible(GTK_WIDGET(help_elabel[5]), TRUE);
    } else {
      gtk_widget_set_visible(GTK_WIDGET(help_elabel[4]), FALSE);
      gtk_widget_set_visible(GTK_WIDGET(help_elabel[5]), FALSE);
    }

    helptext_extra(buf, sizeof(buf), client.conn.playing, pitem->text, pextra);
  }
  gtk_widget_set_visible(help_etable, TRUE);

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  This is currently just a text page, with special text.
**************************************************************************/
static void help_update_goods(const struct help_item *pitem,
                              char *title)
{
  char buf[8192];
  struct goods_type *pgood = goods_by_translated_name(title);

  if (pgood == nullptr) {
    strcat(buf, pitem->text);
  } else {
    helptext_goods(buf, sizeof(buf), client.conn.playing, pitem->text,
                   pgood);
  }

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  This is currently just a text page, with special text.
**************************************************************************/
static void help_update_specialist(const struct help_item *pitem,
                                   char *title)
{
  char buf[8192];
  struct specialist *pspec = specialist_by_translated_name(title);

  if (pspec == nullptr) {
    strcat(buf, pitem->text);
  } else {
    helptext_specialist(buf, sizeof(buf), client.conn.playing, pitem->text,
                        pspec);
  }

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  This is currently just a text page, with special text.
**************************************************************************/
static void help_update_government(const struct help_item *pitem,
                                   char *title)
{
  char buf[8192];
  struct government *gov = government_by_translated_name(title);

  if (gov == nullptr) {
    strcat(buf, pitem->text);
  } else {
    helptext_government(buf, sizeof(buf), client.conn.playing,
                        pitem->text, gov);
  }

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  Show nation flag and legend.
**************************************************************************/
static void help_update_nation(const struct help_item *pitem, char *title,
                               struct nation_type *pnation)
{
  char buf[4096];

  if (pnation == nullptr) {
    strcat(buf, pitem->text);
  } else {
    helptext_nation(buf, sizeof(buf), pnation, pitem->text);

    set_help_tile_from_sprite(get_nation_flag_sprite(tileset, pnation));
  }

  gtk_text_buffer_set_text(help_text, buf, -1);
  gtk_widget_set_visible(help_text_sw, TRUE);
}

/**********************************************************************//**
  Display updated help dialog.
**************************************************************************/
static void help_update_dialog(const struct help_item *pitem)
{
  char *top;

  /* Figure out what kind of item is required for pitem ingo */

  for (top = pitem->topic; *top == ' '; top++) {
    /* Nothing */
  }

  help_box_hide();
  gtk_text_buffer_set_text(help_text, "", -1);

  switch (pitem->type) {
  case HELP_IMPROVEMENT:
    help_update_improvement(pitem, top);
    break;
  case HELP_WONDER:
    help_update_wonder(pitem, top);
    break;
  case HELP_UNIT:
    help_update_unit_type(pitem, top);
    break;
  case HELP_TECH:
    help_update_tech(pitem, top);
    break;
  case HELP_TERRAIN:
    help_update_terrain(pitem, top);
    break;
  case HELP_EXTRA:
    help_update_extra(pitem, top);
    break;
  case HELP_GOODS:
    help_update_goods(pitem, top);
    break;
  case HELP_SPECIALIST:
    help_update_specialist(pitem, top);
    break;
  case HELP_GOVERNMENT:
    help_update_government(pitem, top);
    break;
  case HELP_NATIONS:
    help_update_nation(pitem, top, nation_by_translated_plural(top));
    break;
  case HELP_TEXT:
  default:
    /* It was a pure text item */
    gtk_text_buffer_set_text(help_text, pitem->text, -1);
    gtk_widget_set_visible(help_text_sw, TRUE);
    break;
  }
  set_title_topic(pitem->topic);

  gtk_widget_set_visible(help_box, TRUE);
}

/**********************************************************************//**
  Add item at path to selection and scroll to its cell.
**************************************************************************/
static void help_item_zoom(GtkTreePath *path)
{
  GtkTreeModel *model;
  GtkTreeIter   it, child, item;
  GtkTreeSelection *selection;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(help_view));
  gtk_tree_model_get_iter(model, &item, path);

  for (child = item; gtk_tree_model_iter_parent(model, &it, &child); child = it) {
    GtkTreePath *it_path;

    it_path = gtk_tree_model_get_path(model, &it);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(help_view), it_path, TRUE);
    gtk_tree_path_free(it_path);
  }

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(help_view));
  gtk_tree_selection_select_iter(selection, &item);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(help_view), path, NULL,
                               TRUE, 0.0, 0.0);
}

/**********************************************************************//**
  Return path to help item.
**************************************************************************/
static GtkTreePath *help_item_path(const struct help_item *pitem)
{
  GtkTreePath *path;
  bool next;

  path = gtk_tree_path_new_first();
  next = FALSE;
  help_items_iterate(pitem2) {
    const char *s;
    int depth;

    for (s = pitem2->topic; *s == ' '; s++) {
      /* Nothing */
    }
    depth = s - pitem2->topic + 1;

    while (depth < gtk_tree_path_get_depth(path)) {
      gtk_tree_path_up(path);
      gtk_tree_path_next(path);
      next = FALSE;
    }
    while (depth > gtk_tree_path_get_depth(path)) {
      gtk_tree_path_down(path);
      next = FALSE;
    }

    if (next) {
      gtk_tree_path_next(path);
    }

    if (pitem == pitem2) {
      break;
    }

    next = TRUE;
  } help_items_iterate_end;

  return path;
}

/**********************************************************************//**
  Add item to selection.
**************************************************************************/
static void select_help_item_string(const char *item, enum help_page_type htype)
{
  const struct help_item *pitem;
  int idx;
  GtkTreePath *path;
  GtkTreeViewColumn *col;

  if (!(pitem = get_help_item_spec(item, htype, &idx))) {
    return;
  }

  path = help_item_path(pitem);
  help_item_zoom(path);

  col = gtk_tree_view_get_column(GTK_TREE_VIEW(help_view), 0);
  gtk_tree_view_set_cursor(GTK_TREE_VIEW(help_view), path, col, FALSE);
  gtk_tree_path_free(path);
}

/**********************************************************************//**
  Set sensitivity of help dialog response buttons.
**************************************************************************/
static void help_command_update(void)
{
  GtkDialog *dialog = GTK_DIALOG(help_dialog_shell);

  if (help_history_pos < 0) {
    gtk_dialog_set_response_sensitive(dialog, 1, FALSE);
    gtk_dialog_set_response_sensitive(dialog, 2, FALSE);
  } else {
    gtk_dialog_set_response_sensitive(dialog, 1, TRUE);
    gtk_dialog_set_response_sensitive(dialog, 2, TRUE);

    if (help_history_pos == 0) {
      gtk_dialog_set_response_sensitive(dialog, 1, FALSE);
    }
    if (help_history_pos >= help_history->len - 1) {
      gtk_dialog_set_response_sensitive(dialog, 2, FALSE);
    }
  }
}

/**********************************************************************//**
  User gave response to help dialog.
**************************************************************************/
static void help_command_callback(GtkWidget *w, gint response_id)
{
  GtkTreePath *path;
  const struct help_item *pitem;

  if (response_id == 1) {
    if (help_history_pos > 0) {
      help_history_pos--;

      pitem = g_ptr_array_index(help_history, help_history_pos);
      path = help_item_path(pitem);
      help_item_zoom(path);
      help_update_dialog(pitem);
      help_command_update();
    }
  } else if (response_id == 2) {
    if (help_history_pos < help_history->len - 1) {
      help_history_pos++;

      pitem = g_ptr_array_index(help_history, help_history_pos);
      path = help_item_path(pitem);
      help_item_zoom(path);
      help_update_dialog(pitem);
      help_command_update();
    }
  } else {
    /* Save size of the dialog. */
    gtk_window_get_default_size(GTK_WINDOW(help_dialog_shell),
                                &GUI_GTK_OPTION(helpdlg_xsize),
                                &GUI_GTK_OPTION(helpdlg_ysize));
    gtk_window_destroy(GTK_WINDOW(help_dialog_shell));
  }
}
