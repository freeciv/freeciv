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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "packets.h"
#include "unit.h"

/* client/agents */
#include "cma_fec.h"

/* client */
#include "citydlg_common.h"
#include "cityrepdata.h"
#include "client_main.h"
#include "climisc.h"
#include "global_worklist.h"
#include "mapctrl_common.h"    /* is_city_hilited() */
#include "mapview_common.h"
#include "options.h"

/* client/gui-gtk-4.0 */
#include "chatline.h"
#include "citydlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "repodlgs.h"

#include "cityrep.h"

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))

/* Some versions of gcc have problems with negative values here (PR#39722). */
#define CMA_NONE        (10000)
#define CMA_CUSTOM      (10001)

struct sell_data {
  int count;                    /* Number of cities. */
  int gold;                     /* Amount of gold. */
  const struct impr_type *target;     /* The target for selling. */
};

enum city_operation_type {
  CO_CHANGE, CO_LAST, CO_NEXT, CO_FIRST, CO_NEXT_TO_LAST, CO_SELL, CO_NONE
};

/******************************************************************/
static void create_city_report_dialog(bool make_modal);

static void city_activated_callback(GtkTreeView *view, GtkTreePath *path,
                                    GtkTreeViewColumn *col, gpointer data);

static void city_command_callback(struct gui_dialog *dlg, int response,
                                  gpointer data);

static void city_selection_changed_callback(GtkTreeSelection *selection);
static void city_clear_worklist_callback(GSimpleAction *action, GVariant *parameter,
                                         gpointer data);
static void update_total_buy_cost(void);

static GMenu *create_production_menu(GActionGroup *group);
static GMenu *create_select_menu(GActionGroup *group);

static GMenu *create_change_menu(GActionGroup *group, const char *mname,
                                 const char *human_mname,
                                 enum city_operation_type oper);

static struct gui_dialog *city_dialog_shell = NULL;

enum {
  CITY_CENTER = 1, CITY_POPUP, CITY_BUY
};

static GtkWidget *city_view;
static GtkTreeSelection *city_selection;
static GtkListStore *city_model;
#define CRD_COL_CITY_ID (0 + NUM_CREPORT_COLS)

#ifdef MENUS_GTK3
static void popup_select_menu(GtkMenuShell *menu, gpointer data);
#endif /* MENUS_GTK3 */

static void recreate_production_menu(GActionGroup *group);
static void recreate_select_menu(GActionGroup *group);
static void recreate_sell_menu(GActionGroup *group);

static GtkWidget *city_center_command;
static GtkWidget *city_popup_command;
static GtkWidget *city_buy_command;
#ifdef MENUS_GTK3
static GtkWidget *city_sell_command;
#endif /* MENUS_GTK3 */
static GtkWidget *city_total_buy_cost_label;

static GMenu *prod_menu = NULL;
static GMenu *change_menu;
static GMenu *add_first_menu;
static GMenu *add_last_menu;
static GMenu *add_next_menu;
static GMenu *add_2ndlast_menu;
static GMenu *wl_set_menu;
static GMenu *wl_append_menu;

static GMenu *select_menu;
static GMenu *unit_b_select_menu = NULL;
static GMenu *impr_b_select_menu;
static GMenu *wndr_b_select_menu;
static GMenu *unit_s_select_menu;
static GMenu *unit_p_select_menu;
static GMenu *impr_p_select_menu;
static GMenu *wndr_p_select_menu;
static GMenu *unit_a_select_menu;
static GMenu *impr_a_select_menu;
static GMenu *wndr_a_select_menu;
static GMenu *governor_select_menu;

static int city_dialog_shell_is_modal;

static GMenu *cityrep_menu;
static GActionGroup *cityrep_group;
static GMenu *display_menu;

/************************************************************************//**
  Return text line for the column headers for the city report
****************************************************************************/
static void get_city_table_header(char **text, int n)
{
  struct city_report_spec *spec;
  int i;

  for (i = 0, spec = city_report_specs; i < NUM_CREPORT_COLS; i++, spec++) {
    fc_snprintf(text[i], n, "%*s\n%*s",
                NEG_VAL(spec->width), spec->title1 ? spec->title1 : "",
                NEG_VAL(spec->width), spec->title2 ? spec->title2 : "");
  }
}

/****************************************************************************
                        CITY REPORT DIALOG
****************************************************************************/

/************************************************************************//**
  Returns a new tree model for the city report.
****************************************************************************/
static GtkListStore *city_report_dialog_store_new(void)
{
  GType model_types[NUM_CREPORT_COLS + 1];
  gint i;

  /* City report data. */
  for (i = 0; i < NUM_CREPORT_COLS; i++) {
    model_types[i] = G_TYPE_STRING;
  }

  /* Specific gtk client data. */
  model_types[i++] = G_TYPE_INT;        /* CRD_COL_CITY_ID */

  return gtk_list_store_newv(i, model_types);
}

/************************************************************************//**
  Set the values of the iterator.
****************************************************************************/
static void city_model_set(GtkListStore *store, GtkTreeIter *iter,
                           struct city *pcity)
{
  struct city_report_spec *spec;
  char buf[64];
  gint i;

  for (i = 0; i < NUM_CREPORT_COLS; i++) {
    spec = city_report_specs + i;
    fc_snprintf(buf, sizeof(buf), "%*s", NEG_VAL(spec->width),
                spec->func(pcity, spec->data));
    gtk_list_store_set(store, iter, i, buf, -1);
  }
  gtk_list_store_set(store, iter, CRD_COL_CITY_ID, pcity->id, -1);
}

/************************************************************************//**
  Set the values of the iterator.
****************************************************************************/
static struct city *city_model_get(GtkTreeModel *model, GtkTreeIter *iter)
{
  struct city *pcity;
  int id;

  gtk_tree_model_get(model, iter, CRD_COL_CITY_ID, &id, -1);
  pcity = game_city_by_number(id);
  return ((NULL != pcity
           && client_has_player()
           && city_owner(pcity) != client_player())
          ? NULL : pcity);
}

/************************************************************************//**
  Return TRUE if 'iter' has been set to the city row.
****************************************************************************/
static gboolean city_model_find(GtkTreeModel *model, GtkTreeIter *iter,
                                const struct city *pcity)
{
  const int searched = pcity->id;
  int id;

  if (gtk_tree_model_get_iter_first(model, iter)) {
    do {
      gtk_tree_model_get(model, iter, CRD_COL_CITY_ID, &id, -1);
      if (searched == id) {
        return TRUE;
      }
    } while (gtk_tree_model_iter_next(model, iter));
  }
  return FALSE;
}

/************************************************************************//**
  Fill the model with the current configuration.
****************************************************************************/
static void city_model_fill(GtkListStore *store,
                            GtkTreeSelection *selection, GHashTable *select)
{
  GtkTreeIter iter;

  if (client_has_player()) {
    city_list_iterate(client_player()->cities, pcity) {
      gtk_list_store_append(store, &iter);
      city_model_set(store, &iter, pcity);
      if (NULL != select
          && g_hash_table_remove(select, GINT_TO_POINTER(pcity->id))) {
        gtk_tree_selection_select_iter(selection, &iter);
      }
    } city_list_iterate_end;
  } else {
    /* Global observer case. */
    cities_iterate(pcity) {
      gtk_list_store_append(store, &iter);
      city_model_set(store, &iter, pcity);
      if (NULL != select
          && g_hash_table_remove(select, GINT_TO_POINTER(pcity->id))) {
        gtk_tree_selection_select_iter(selection, &iter);
      }
    } cities_iterate_end;
  }
}

/************************************************************************//**
  Popup the city report dialog, and optionally raise it.
****************************************************************************/
void city_report_dialog_popup(bool raise)
{
  if (!city_dialog_shell) {
    city_dialog_shell_is_modal = FALSE;

    create_city_report_dialog(FALSE);
  }

  gui_dialog_present(city_dialog_shell);
  hilite_cities_from_canvas();
  if (raise) {
    gui_dialog_raise(city_dialog_shell);
  }
}

/************************************************************************//**
  Closes the city report dialog.
****************************************************************************/
void city_report_dialog_popdown(void)
{
  if (city_dialog_shell) {
    gui_dialog_destroy(city_dialog_shell);
  }
}

/************************************************************************//**
  Make submenu listing possible build targets
****************************************************************************/
static void append_impr_or_unit_to_menu(GMenu *menu,
                                        GActionGroup *act_group,
                                        const char *act_pfx,
                                        const char *act_pfx2,
                                        bool append_units,
                                        bool append_wonders,
                                        enum city_operation_type
                                        city_operation,
                                        TestCityFunc test_func,
                                        GCallback callback,
                                        int size)
{
  struct universal targets[MAX_NUM_PRODUCTION_TARGETS];
  struct item items[MAX_NUM_PRODUCTION_TARGETS];
  int i, item, targets_used;
  char *row[4];
  char buf[4][64];

  GtkSizeGroup *size_group[3];

#ifdef MENUS_GTK3
  const char *markup[3] = {
    "weight=\"bold\"",
    "",
    ""
  };
#endif

  if (city_operation != CO_NONE) {
    GPtrArray *selected;
    ITree it;
    int num_selected = 0;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);
    struct city **data;

    selected = g_ptr_array_sized_new(size);

    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity;

      if (!itree_is_selected(city_selection, &it)
          || !(pcity = city_model_get(model, TREE_ITER_PTR(it)))) {
        continue;
      }

      g_ptr_array_add(selected, pcity);
      num_selected++;
    }

    data = (struct city **)g_ptr_array_free(selected, FALSE);
    targets_used
      = collect_production_targets(targets, data, num_selected, append_units,
                                   append_wonders, TRUE, test_func);
    g_free(data);
  } else {
    targets_used = collect_production_targets(targets, NULL, 0, append_units,
                                              append_wonders, FALSE,
                                              test_func);
  }

  name_and_sort_items(targets, targets_used, items,
                      city_operation != CO_NONE, NULL);

  for (i = 0; i < 4; i++) {
    row[i] = buf[i];
  }


  for (i = 0; i < 3; i++) {
    size_group[i] = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  }

  for (item = 0; item < targets_used; item++) {
    struct universal target = items[item].item;
    GMenuItem *menu_item;
    char actbuf[256];
    GSimpleAction *act;
#ifdef MENUS_GTK3
    char txt[256];
    GtkWidget *hgrid, *label;
    int grid_col = 0;
#endif /* MENUS_GTK3 */

    get_city_dialog_production_row(row, sizeof(buf[0]), &target, NULL);

    fc_snprintf(actbuf, sizeof(actbuf), "win.%s%s%d", act_pfx, act_pfx2, item);

    menu_item = g_menu_item_new(buf[0], actbuf);

    fc_snprintf(actbuf, sizeof(actbuf), "%s%s%d", act_pfx, act_pfx2, item);
    act = g_simple_action_new(actbuf, NULL);
    g_object_set_data(G_OBJECT(act), "freeciv_test_func", test_func);
    g_object_set_data(G_OBJECT(act), "freeciv_city_operation",
                      GINT_TO_POINTER(city_operation));
    g_action_map_add_action(G_ACTION_MAP(act_group), G_ACTION(act));
    g_signal_connect(act, "activate", callback,
                     GINT_TO_POINTER(cid_encode(target)));
    menu_item_append_unref(menu, menu_item);

#ifdef MENUS_GTK3
    hgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(hgrid), 18);
    gtk_container_add(GTK_CONTAINER(menu_item), hgrid);

    for (i = 0; i < 3; i++) {
      if (row[i][0] == '\0') {
        continue;
      }

      if (city_operation == CO_SELL && i != 0) {
        continue;
      }

      fc_snprintf(txt, ARRAY_SIZE(txt), "<span %s>%s</span>",
                  markup[i], row[i]);

      label = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(label), txt);

      switch (i) {
        case 0:
          gtk_widget_set_halign(label, GTK_ALIGN_START);
          gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        break;
        case 2:
          gtk_widget_set_halign(label, GTK_ALIGN_END);
          gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
          break;
        default:
          break;
      }

      gtk_grid_attach(GTK_GRID(hgrid), label, grid_col++, 0, 1, 1);
      gtk_size_group_add_widget(size_group[i], label);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(menu_item, "activate", callback,
                     GINT_TO_POINTER(cid_encode(target)));
#endif /* MENUS_GTK3 */
  }

  for (i = 0; i < 3; i++) {
    g_object_unref(size_group[i]);
  }

#ifdef MENUS_GTK3
  gtk_widget_set_sensitive(GTK_WIDGET(parent_item), (targets_used > 0));
#endif
}

/************************************************************************//**
  Change the production of one single selected city.
****************************************************************************/
static void impr_or_unit_iterate(GtkTreeModel *model, GtkTreePath *path,
                                 GtkTreeIter *iter, gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    city_change_production(pcity, &target);
  }
}

/************************************************************************//**
  Called by select_impr_or_unit_callback() for each city that is selected in
  the city list dialog to have a object appended to the worklist. Sends a
  packet adding the item to the end of the worklist.
****************************************************************************/
static void worklist_last_impr_or_unit_iterate(GtkTreeModel *model,
                                               GtkTreePath *path,
                                               GtkTreeIter *iter,
                                               gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    (void) city_queue_insert(pcity, -1, &target);
  }
  /* Perhaps should warn the user if not successful? */
}

/************************************************************************//**
  Called by select_impr_or_unit_callback() for each city that is selected in
  the city list dialog to have a object inserted first to the worklist.
  Sends a packet adding the current production to the first place after the
  current production of the worklist. Then changes the production to the
  requested item.
****************************************************************************/
static void worklist_first_impr_or_unit_iterate(GtkTreeModel *model,
                                                GtkTreePath *path,
                                                GtkTreeIter *iter,
                                                gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    (void) city_queue_insert(pcity, 0, &target);
  }
  /* Perhaps should warn the user if not successful? */
}

/************************************************************************//**
  Called by select_impr_or_unit_callback() for each city that is selected in
  the city list dialog to have a object added next to the worklist. Sends a
  packet adding the item to the first place after the current production of
  the worklist.
****************************************************************************/
static void worklist_next_impr_or_unit_iterate(GtkTreeModel *model,
                                               GtkTreePath *path,
                                               GtkTreeIter *iter,
                                               gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    (void) city_queue_insert(pcity, 1, &target);
  }
  /* Perhaps should warn the user if not successful? */
}

/************************************************************************//**
  Called by select_impr_or_unit_callback() for each city that is selected in
  the city list dialog to have an object added before the last position in
  the worklist.
****************************************************************************/
static void worklist_next_to_last_impr_or_unit_iterate(GtkTreeModel *model,
                                                       GtkTreePath *path,
                                                       GtkTreeIter *iter,
                                                       gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    city_queue_insert(pcity, worklist_length(&pcity->worklist), &target);
  }
}

/************************************************************************//**
  Iterate the cities going to sell.
****************************************************************************/
static void sell_impr_iterate(GtkTreeModel *model, GtkTreePath *path,
                              GtkTreeIter *iter, gpointer data)
{
  struct sell_data *sd = (struct sell_data *) data;
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity
      && !pcity->did_sell
      && city_has_building(pcity, sd->target)) {
    sd->count++;
    sd->gold += impr_sell_gold(sd->target);
    city_sell_improvement(pcity, improvement_number(sd->target));
  }
}

/************************************************************************//**
  Some build target, either improvement or unit, has been selected from
  some menu.
****************************************************************************/
static void select_impr_or_unit_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data)
{
  struct universal target = cid_decode(GPOINTER_TO_INT(data));
  TestCityFunc test_func = g_object_get_data(G_OBJECT(action), "freeciv_test_func");
  enum city_operation_type city_operation =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "freeciv_city_operation"));

  /* If this is not a city operation: */
  if (city_operation == CO_NONE) {
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);
    ITree it;

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));

      if (NULL != pcity && test_func(pcity, &target)) {
        itree_select(city_selection, &it);
      }
    }
  } else {
    GtkTreeSelectionForeachFunc foreach_func;

    connection_do_buffer(&client.conn);
    switch (city_operation) {
    case CO_LAST:
      gtk_tree_selection_selected_foreach(city_selection,
                                          worklist_last_impr_or_unit_iterate,
                                          GINT_TO_POINTER(cid_encode(target)));
      break;
    case CO_CHANGE:
      gtk_tree_selection_selected_foreach(city_selection,
                                          impr_or_unit_iterate,
                                          GINT_TO_POINTER(cid_encode(target)));
      break;
    case CO_FIRST:
      gtk_tree_selection_selected_foreach(city_selection,
                                          worklist_first_impr_or_unit_iterate,
                                          GINT_TO_POINTER(cid_encode(target)));
      break;
    case CO_NEXT:
      gtk_tree_selection_selected_foreach(city_selection,
                                          worklist_next_impr_or_unit_iterate,
                                          GINT_TO_POINTER(cid_encode(target)));
      break;
    case CO_NEXT_TO_LAST:
      foreach_func = worklist_next_to_last_impr_or_unit_iterate;
      gtk_tree_selection_selected_foreach(city_selection, foreach_func,
                                          GINT_TO_POINTER(cid_encode(target)));
      break;
    case CO_SELL:
      fc_assert_action(target.kind == VUT_IMPROVEMENT, break);
      {
        const struct impr_type *building = target.value.building;
        struct sell_data sd = { 0, 0, building };
        GtkWidget *w;
        gint res;
        gchar *buf;
        const char *imprname = improvement_name_translation(building);

        /* Ask confirmation */
        buf = g_strdup_printf(_("Are you sure you want to sell those %s?"), imprname);
        w = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_YES_NO, "%s", buf);
        g_free(buf);
        res = blocking_dialog(w);    /* Synchron. */
        gtk_window_destroy(GTK_WINDOW(w));
        if (res == GTK_RESPONSE_NO) {
          break;
        }

        gtk_tree_selection_selected_foreach(city_selection,
                                            sell_impr_iterate, &sd);
        if (sd.count > 0) {
          /* FIXME: plurality of sd.count is ignored! */
          /* TRANS: "Sold 3 Harbor for 90 gold." (Pluralisation is in gold --
           * second %d -- not in buildings.) */
          w = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                     PL_("Sold %d %s for %d gold.",
                                         "Sold %d %s for %d gold.",
                                         sd.gold),
                                     sd.count, imprname, sd.gold);
        } else {
          w = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                     _("No %s could be sold."),
                                     imprname);
        }

        g_signal_connect(w, "response",
                         G_CALLBACK(gtk_window_destroy), NULL);
        gtk_window_present(GTK_WINDOW(w));      /* Asynchron. */
      }
      break;
    case CO_NONE:
      break;
    }
    connection_do_unbuffer(&client.conn);
  }
}

/************************************************************************//**
  Governors iterating callback.
****************************************************************************/
static void governors_iterate(GtkTreeModel *model, GtkTreePath *path,
                              GtkTreeIter *iter, gpointer data)
{
  struct city *pcity = city_model_get(model, iter);
  int idx = GPOINTER_TO_INT(data);

  if (NULL != pcity) {
    if (CMA_NONE == idx) {
      cma_release_city(pcity);
    } else {
      cma_put_city_under_agent(pcity, cmafec_preset_get_parameter(idx));
    }
    refresh_city_dialog(pcity);
  }
}

/************************************************************************//**
  Called when one clicks on an governor item to make a selection or to
  change a selection's preset.
****************************************************************************/
static void select_governor_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  int idx = GPOINTER_TO_INT(data);
  bool change_governor =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "governor"));
  struct cm_parameter cm;

  /* If this is not the change button but the select cities button. */
  if (!change_governor) {
    ITree it;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));
      int controlled;
      bool select;

      if (NULL == pcity) {
        continue;
      }
      controlled = cma_is_city_under_agent(pcity, &cm);
      select = FALSE;

      if (idx == CMA_NONE) {
        /* CMA_NONE selects not-controlled, all others require controlled */
        if (!controlled) {
          select = TRUE;
        }
      } else if (controlled) {
        if (idx == CMA_CUSTOM) {
          if (cmafec_preset_get_index_of_parameter(&cm) == -1) {
            select = TRUE;
          }
        } else if (cm_are_parameter_equal(&cm,
                                          cmafec_preset_get_parameter(idx))) {
          select = TRUE;
        }
      }

      if (select) {
        itree_select(city_selection, &it);
      }
    }
  } else {
    gtk_tree_selection_selected_foreach(city_selection,
                                        governors_iterate,
                                        GINT_TO_POINTER(idx));
  }
}

/************************************************************************//**
  Create the governor entries in the change menu and the select menu.
  The indices CMA_NONE and CMA_CUSTOM are special.
  CMA_NONE signifies a preset of "none" and CMA_CUSTOM a
  "custom" preset.
****************************************************************************/
static GMenu *create_governor_menu(GActionGroup *group,
                                   bool change_governor)
{
  GMenu *menu;
  int i;
  struct cm_parameter cm;

  menu = g_menu_new();

  if (!can_client_issue_orders()) {
    return menu;
  }

  if (change_governor) {
    GSimpleAction *act;

    act = g_simple_action_new("chg_governor_none", NULL);
    g_object_set_data(G_OBJECT(act), "governor",
                      GINT_TO_POINTER(change_governor));
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", G_CALLBACK(select_governor_callback),
                     GINT_TO_POINTER(CMA_NONE));
    menu_item_append_unref(menu,
                           g_menu_item_new(Q_("?cma:none"),
                                           "win.chg_governor_none"));

    for (i = 0; i < cmafec_preset_num(); i++) {
      char buf[128];

      fc_snprintf(buf, sizeof(buf), "chg_governor_%d", i);
      act = g_simple_action_new(buf, NULL);
      g_object_set_data(G_OBJECT(act), "governor",
                        GINT_TO_POINTER(change_governor));
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(select_governor_callback),
                       GINT_TO_POINTER(i));
      fc_snprintf(buf, sizeof(buf), "win.chg_governor_%d", i);
      menu_item_append_unref(menu,
                             g_menu_item_new(cmafec_preset_get_descr(i), buf));
    }
  } else {
    /* Search for a "none" */
    bool found;

    found = FALSE;
    city_list_iterate(client.conn.playing->cities, pcity) {
      if (!cma_is_city_under_agent(pcity, NULL)) {
        found = TRUE;
        break;
      }
    } city_list_iterate_end;

    if (found) {
      GSimpleAction *act;

      act = g_simple_action_new("sel_governor_none", NULL);
      g_object_set_data(G_OBJECT(act), "governor",
                        GINT_TO_POINTER(change_governor));
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(select_governor_callback),
                       GINT_TO_POINTER(CMA_NONE));
      menu_item_append_unref(menu,
                             g_menu_item_new(Q_("?cma:none"),
                                             "win.sel_governor_none"));
    }

    /*
     * Search for a city that's under custom (not preset) agent. Might
     * take a lonnggg time.
     */
    found = FALSE;
    city_list_iterate(client.conn.playing->cities, pcity) {
      if (cma_is_city_under_agent(pcity, &cm)
          && cmafec_preset_get_index_of_parameter(&cm) == -1) {
        found = TRUE;
        break;
      }
    } city_list_iterate_end;

    if (found) {
      /* We found city that's under agent but not a preset */
      GSimpleAction *act;

      act = g_simple_action_new("sel_governor_custom", NULL);
      g_object_set_data(G_OBJECT(act), "governor",
                        GINT_TO_POINTER(change_governor));
      g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
      g_signal_connect(act, "activate", G_CALLBACK(select_governor_callback),
                       GINT_TO_POINTER(CMA_CUSTOM));
      menu_item_append_unref(menu,
                             g_menu_item_new(Q_("?cma:custom"),
                                             "win.sel_governor_custom"));
    }

    /* Only fill in presets that are being used. */
    for (i = 0; i < cmafec_preset_num(); i++) {
      found = FALSE;
      city_list_iterate(client.conn.playing->cities, pcity) {
        if (cma_is_city_under_agent(pcity, &cm)
            && cm_are_parameter_equal(&cm,
                                      cmafec_preset_get_parameter(i))) {
          found = TRUE;
          break;
        }
      } city_list_iterate_end;

      if (found) {
        GSimpleAction *act;
        char buf[128];

        fc_snprintf(buf, sizeof(buf), "sel_governor_%d", i);

        act = g_simple_action_new(buf, NULL);
        g_object_set_data(G_OBJECT(act), "governor",
                          GINT_TO_POINTER(change_governor));
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_signal_connect(act, "activate", G_CALLBACK(select_governor_callback),
                         GINT_TO_POINTER(i));
        fc_snprintf(buf, sizeof(buf), "win.sel_governor_%d", i);
        menu_item_append_unref(menu,
                               g_menu_item_new(cmafec_preset_get_descr(i),
                                               buf));
      }
    }
  }

  return menu;
}

/************************************************************************//**
  Recreate governor menu
****************************************************************************/
static void update_governor_menu(void)
{
  g_menu_remove(cityrep_menu, 1);
  submenu_insert_unref(cityrep_menu, 1, _("Gover_nor"),
                       G_MENU_MODEL(create_governor_menu(cityrep_group, TRUE)));
}

/************************************************************************//**
  Helper function to append a worklist to the current work list of one city
  in the city report. This function is called over all selected rows in the
  list view.
****************************************************************************/
static void append_worklist_foreach(GtkTreeModel *model, GtkTreePath *path,
                                    GtkTreeIter *iter, gpointer data)
{
  const struct worklist *pwl = data;
  struct city *pcity = city_model_get(model, iter);

  fc_assert_ret(pwl != NULL);

  if (NULL != pcity) {
    city_queue_insert_worklist(pcity, -1, pwl);
  }
}

/************************************************************************//**
  Menu item callback to append the global worklist associated with this
  item to the worklists of all selected cities. The worklist pointer is
  passed in 'data'.
****************************************************************************/
static void append_worklist_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  struct global_worklist *pgwl =
    global_worklist_by_id(GPOINTER_TO_INT(data));

  fc_assert_ret(city_selection != NULL);

  if (!pgwl) {
    /* Maybe removed by an other way, not an error. */
    return;
  }

  gtk_tree_selection_selected_foreach(city_selection,
                                      append_worklist_foreach,
                                      (gpointer) global_worklist_get(pgwl));
}

/************************************************************************//**
  Helper function to set a worklist for one city in the city report. This
  function is called over all selected rows in the list view.
****************************************************************************/
static void set_worklist_foreach(GtkTreeModel *model, GtkTreePath *path,
                                 GtkTreeIter *iter, gpointer data)
{
  const struct worklist *pwl = data;
  struct city *pcity = city_model_get(model, iter);

  fc_assert_ret(pwl != NULL);

  if (NULL != pcity) {
    city_set_queue(pcity, pwl);
  }
}

/************************************************************************//**
  Menu item callback to set a city's worklist to the global worklist
  associated with this menu item. The worklist pointer is passed in 'data'.
****************************************************************************/
static void set_worklist_callback(GSimpleAction *action,
                                  GVariant *parameter,
                                  gpointer data)
{
  struct global_worklist *pgwl =
    global_worklist_by_id(GPOINTER_TO_INT(data));

  fc_assert_ret(city_selection != NULL);
  gtk_tree_selection_selected_foreach(city_selection, set_worklist_foreach,
                                      (gpointer) global_worklist_get(pgwl));

  if (!pgwl) {
    /* Maybe removed by an other way, not an error. */
    return;
  }

  gtk_tree_selection_selected_foreach(city_selection,
                                      set_worklist_foreach,
                                      (gpointer) global_worklist_get(pgwl));
}

/************************************************************************//**
  Create submenu based on global worklist.
****************************************************************************/
static GMenu *create_wl_menu(GActionGroup *group, const char *act_pfx,
                             GCallback cb)
{
  GMenu *menu;
  GSimpleAction *act;
  int count = 0;

  menu = g_menu_new();

  if (!can_client_issue_orders()) {
    return NULL;
  }

  global_worklists_iterate(pgwl) {
    char buf[128];

    fc_snprintf(buf, sizeof(buf), "wl%s%d", act_pfx, count);
    act = g_simple_action_new(buf, NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "activate", cb,
                     GINT_TO_POINTER(global_worklist_id(pgwl)));
    fc_snprintf(buf, sizeof(buf), "win.wl%s%d", act_pfx, count);
    menu_item_append_unref(menu,
                           g_menu_item_new(global_worklist_name(pgwl),
                                           buf));
    count++;
  } global_worklists_iterate_end;

  if (count == 0) {
    char buf[64];

    fc_snprintf(buf, sizeof(buf), "win.wl%s_dummy", act_pfx);
    menu_item_append_unref(menu,
                           g_menu_item_new(_("(no worklists defined)"), buf));
  }

  return menu;
}

/************************************************************************//**
  Update city report views
****************************************************************************/
static void city_report_update_views(void)
{
  struct city_report_spec *spec;
  GtkTreeView *view;
  GtkTreeViewColumn *col;
  GList *columns, *p;

  view = GTK_TREE_VIEW(city_view);
  fc_assert_ret(view != NULL);

  columns = gtk_tree_view_get_columns(view);

  for (p = columns; p != NULL; p = p->next) {
    col = p->data;
    spec = g_object_get_data(G_OBJECT(col), "city_report_spec");
    gtk_tree_view_column_set_visible(col, spec->show);
  }

  g_list_free(columns);
}

/************************************************************************//**
  Create up-to-date menu item for the display menu.
  Caller need to g_object_unref() returned item.
****************************************************************************/
static GMenuItem *create_display_menu_item(int pos)
{
  GMenuItem *item;
  char act_name[50];
  struct city_report_spec *spec = city_report_specs + pos;

  fc_snprintf(act_name, sizeof(act_name), "win.display%d(%s)",
              pos, spec->show ? "true" : "false");
  item = g_menu_item_new(spec->explanation, NULL);
  g_menu_item_set_detailed_action(item, act_name);

  return item;
}

/************************************************************************//**
  User has toggled some column viewing option
****************************************************************************/
static void toggle_view(GSimpleAction *act, GVariant *value, gpointer data)
{
  struct city_report_spec *spec = data;
  int idx = spec - city_report_specs;

  spec->show ^= 1;
  city_report_update_views();

  g_menu_remove(display_menu, idx);
  menu_item_insert_unref(display_menu, idx, create_display_menu_item(idx));
}

/************************************************************************//**
  Create columns selection menu for the city report.
****************************************************************************/
static GMenu *create_display_menu(GActionGroup *group)
{
  struct city_report_spec *spec;
  int i;
  GVariantType *bvart = g_variant_type_new("b");

  display_menu = g_menu_new();
  for (i = 0, spec = city_report_specs; i < NUM_CREPORT_COLS; i++, spec++) {
    GSimpleAction *act;
    char act_name[50];
    GVariant *var = g_variant_new("b", TRUE);

    fc_snprintf(act_name, sizeof(act_name), "display%d", i);
    act = g_simple_action_new_stateful(act_name, bvart, var);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_signal_connect(act, "change-state", G_CALLBACK(toggle_view), (gpointer)spec);

    menu_item_insert_unref(display_menu, i, create_display_menu_item(i));
  }

  g_variant_type_free(bvart);

  return display_menu;
}

/************************************************************************//**
  Create menu for city report
****************************************************************************/
static GtkWidget *create_city_report_menu(void)
{
  GtkWidget *vbox, *sep;
  GtkWidget *aux_menu;
  GMenu *submenu;

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(vbox), sep);

  aux_menu = aux_menu_new();
  cityrep_group = G_ACTION_GROUP(g_simple_action_group_new());

  cityrep_menu = g_menu_new();

  /* Placeholder */
  submenu_append_unref(cityrep_menu, _("_Production"),
                       G_MENU_MODEL(g_menu_new()));

  submenu_append_unref(cityrep_menu, _("Gover_nor"),
                       G_MENU_MODEL(create_governor_menu(cityrep_group, TRUE)));

  /* Placeholders */
  submenu_append_unref(cityrep_menu, _("S_ell"), G_MENU_MODEL(g_menu_new()));
  submenu_append_unref(cityrep_menu, _("_Select"), G_MENU_MODEL(g_menu_new()));

  submenu = create_display_menu(cityrep_group);
  submenu_append_unref(cityrep_menu, _("_Display"), G_MENU_MODEL(submenu));

  gtk_widget_insert_action_group(aux_menu, "win", cityrep_group);
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(aux_menu), G_MENU_MODEL(cityrep_menu));
  gtk_box_append(GTK_BOX(vbox), aux_menu);

  return vbox;
}

/************************************************************************//**
  Sort callback.
****************************************************************************/
static gint cityrep_sort_func(GtkTreeModel *model, GtkTreeIter *a,
                              GtkTreeIter *b, gpointer data)
{
  gint col = GPOINTER_TO_INT(data);
  gchar *str1, *str2;
  int i;

  gtk_tree_model_get(model, a, col, &str1, -1);
  gtk_tree_model_get(model, b, col, &str2, -1);

  i = cityrepfield_compare(str1, str2);
  g_free(str1);
  g_free(str2);
  return i;
}

/************************************************************************//**
  Create city report dialog.
****************************************************************************/
static void create_city_report_dialog(bool make_modal)
{
  static char **titles;
  static char (*buf)[128];
  struct city_report_spec *spec;

  GtkWidget *w, *sw, *aux_menu;
  int i;

  gui_dialog_new(&city_dialog_shell, GTK_NOTEBOOK(top_notebook), NULL, TRUE);
  gui_dialog_set_title(city_dialog_shell, _("Cities"));

  gui_dialog_set_default_size(city_dialog_shell, -1, 420);

  gui_dialog_response_set_callback(city_dialog_shell,
                                   city_command_callback);

  /* Menu */
  aux_menu = create_city_report_menu();
  gui_dialog_add_action_widget(city_dialog_shell, aux_menu);

  /* Buttons */
  city_total_buy_cost_label = gtk_label_new(NULL);
  gtk_widget_set_hexpand(city_total_buy_cost_label, TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(city_total_buy_cost_label),
                          PANGO_ELLIPSIZE_START);
  gui_dialog_add_action_widget(city_dialog_shell,
                               city_total_buy_cost_label);

  w = gui_dialog_add_button(city_dialog_shell, NULL,
                            _("_Buy"), CITY_BUY);
  city_buy_command = w;

  w = gui_dialog_add_button(city_dialog_shell, NULL,
                            _("_Inspect"), CITY_POPUP);
  city_popup_command = w;

  w = gui_dialog_add_button(city_dialog_shell, NULL,
                            _("Cen_ter"), CITY_CENTER);
  city_center_command = w;

  /* Tree view */
  buf = fc_realloc(buf, NUM_CREPORT_COLS * sizeof(buf[0]));
  titles = fc_realloc(titles, NUM_CREPORT_COLS * sizeof(titles[0]));
  for (i = 0; i < NUM_CREPORT_COLS; i++) {
    titles[i] = buf[i];
  }
  get_city_table_header(titles, sizeof(buf[0]));

  city_model = city_report_dialog_store_new();

  city_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(city_model));
  gtk_widget_set_hexpand(city_view, TRUE);
  gtk_widget_set_vexpand(city_view, TRUE);
  g_object_unref(city_model);
  gtk_widget_set_name(city_view, "small_font");
  g_signal_connect(city_view, "row_activated",
                   G_CALLBACK(city_activated_callback), NULL);
  city_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(city_view));
  gtk_tree_selection_set_mode(city_selection, GTK_SELECTION_MULTIPLE);
  g_signal_connect(city_selection, "changed",
                   G_CALLBACK(city_selection_changed_callback), NULL);

  for (i = 0, spec = city_report_specs; i < NUM_CREPORT_COLS; i++, spec++) {
    GtkWidget *header;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                   "text", i, NULL);
    header = gtk_label_new(titles[i]);
    gtk_widget_set_tooltip_text(header, spec->explanation);
    gtk_widget_set_visible(header, TRUE);
    gtk_tree_view_column_set_widget(col, header);
    gtk_tree_view_column_set_visible(col, spec->show);
    gtk_tree_view_column_set_sort_column_id(col, i);
    gtk_tree_view_column_set_reorderable(col, TRUE);
    g_object_set_data(G_OBJECT(col), "city_report_spec", spec);
    gtk_tree_view_append_column(GTK_TREE_VIEW(city_view), col);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(city_model), i,
                                    cityrep_sort_func, GINT_TO_POINTER(i),
                                    NULL);
  }

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), city_view);

  gui_dialog_add_content_widget(city_dialog_shell, sw);

  city_model_fill(city_model, NULL, NULL);
  gui_dialog_show_all(city_dialog_shell);

  /* Real menus */
  recreate_production_menu(cityrep_group);
  recreate_select_menu(cityrep_group);
  recreate_sell_menu(cityrep_group);

  city_selection_changed_callback(city_selection);
}

/************************************************************************//**
  User has chosen to select all cities
****************************************************************************/
static void city_select_all_callback(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer data)
{
  gtk_tree_selection_select_all(city_selection);
}

/************************************************************************//**
  User has chosen to unselect all cities
****************************************************************************/
static void city_unselect_all_callback(GSimpleAction *action,
                                       GVariant *parameter,
                                       gpointer data)
{
  gtk_tree_selection_unselect_all(city_selection);
}

/************************************************************************//**
  User has chosen to invert selection
****************************************************************************/
static void city_invert_selection_callback(GSimpleAction *action,
                                           GVariant *parameter,
                                           gpointer data)
{
  ITree it;
  GtkTreeModel *model = GTK_TREE_MODEL(city_model);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    if (itree_is_selected(city_selection, &it)) {
      itree_unselect(city_selection, &it);
    } else {
      itree_select(city_selection, &it);
    }
  }
}

/************************************************************************//**
  User has chosen to select coastal cities
****************************************************************************/
static void city_select_coastal_callback(GSimpleAction *action,
                                         GVariant *parameter,
                                         gpointer data)
{
  ITree it;
  GtkTreeModel *model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));

    if (pcity != NULL
        && is_terrain_class_near_tile(&(wld.map), pcity->tile, TC_OCEAN)) {
      itree_select(city_selection, &it);
    }
  }
}

/************************************************************************//**
  Select all cities on the same continent.
****************************************************************************/
static void same_island_iterate(GtkTreeModel *model, GtkTreePath *path,
                                GtkTreeIter *iter, gpointer data)
{
  struct city *selected_pcity = city_model_get(model, iter);
  ITree it;

  if (NULL == selected_pcity) {
    return;
  }

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));

    if (NULL != pcity
        && (tile_continent(pcity->tile)
            == tile_continent(selected_pcity->tile))) {
      itree_select(city_selection, &it);
    }
  }
}

/************************************************************************//**
  User has chosen to select all cities on same island
****************************************************************************/
static void city_select_island_callback(GSimpleAction *action,
                                        GVariant *parameter,
                                        gpointer data)
{
  gtk_tree_selection_selected_foreach(city_selection,
                                      same_island_iterate, NULL);
}

/************************************************************************//**
  User has chosen to select cities with certain target in production
****************************************************************************/
static void city_select_building_callback(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer data)
{
  enum production_class_type which = GPOINTER_TO_INT(data);
  ITree it;
  GtkTreeModel *model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));

    if (NULL != pcity
        && ((which == PCT_UNIT && VUT_UTYPE == pcity->production.kind)
            || (which == PCT_IMPROVEMENT
                && VUT_IMPROVEMENT == pcity->production.kind
                && !is_wonder(pcity->production.value.building))
            || (which == PCT_WONDER
                && VUT_IMPROVEMENT == pcity->production.kind
                && is_wonder(pcity->production.value.building)))) {
      itree_select(city_selection, &it);
    }
  }
}

/************************************************************************//**
  Buy the production in one single city.
****************************************************************************/
static void buy_iterate(GtkTreeModel *model, GtkTreePath *path,
                        GtkTreeIter *iter, gpointer data)
{
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    cityrep_buy(pcity);
  }
}

/************************************************************************//**
  Center to one single city.
****************************************************************************/
static void center_iterate(GtkTreeModel *model, GtkTreePath *path,
                           GtkTreeIter *iter, gpointer data)
{
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    center_tile_mapcanvas(pcity->tile);
  }
}

/************************************************************************//**
  Popup the dialog of a single city.
****************************************************************************/
static void popup_iterate(GtkTreeModel *model, GtkTreePath *path,
                          GtkTreeIter *iter, gpointer data)
{
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    if (gui_options.center_when_popup_city) {
      center_tile_mapcanvas(pcity->tile);
    }
    popup_city_dialog(pcity);
  }
}

/************************************************************************//**
  gui_dialog response callback.
****************************************************************************/
static void city_command_callback(struct gui_dialog *dlg, int response,
                                  gpointer data)
{
  switch (response) {
  case CITY_CENTER:
    if (1 == gtk_tree_selection_count_selected_rows(city_selection)) {
      /* Center to city doesn't make sense if many city are selected. */
      gtk_tree_selection_selected_foreach(city_selection, center_iterate,
                                          NULL);
    }
    break;
  case CITY_POPUP:
    gtk_tree_selection_selected_foreach(city_selection, popup_iterate, NULL);
    break;
  case CITY_BUY:
    gtk_tree_selection_selected_foreach(city_selection, buy_iterate, NULL);
    break;
  default:
    gui_dialog_destroy(dlg);
    break;
  }
}

/************************************************************************//**
  User has selected city row from city report.
****************************************************************************/
static void city_activated_callback(GtkTreeView *view, GtkTreePath *path,
                                    GtkTreeViewColumn *col, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GdkSurface *win;
  GdkSeat *seat;
  GdkModifierType mask;

  model = gtk_tree_view_get_model(view);

  if (!gtk_tree_model_get_iter(model, &iter, path)) {
    return;
  }

  win = gtk_native_get_surface(gtk_widget_get_native(GTK_WIDGET(view)));
  seat = gdk_display_get_default_seat(gdk_surface_get_display(win));

  gdk_surface_get_device_position(win, gdk_seat_get_pointer(seat),
                                  NULL, NULL, &mask);

  if (!(mask & GDK_CONTROL_MASK)) {
    popup_iterate(model, path, &iter, NULL);
  } else {
    center_iterate(model, path, &iter, NULL);
  }
}

/************************************************************************//**
  Update the city report dialog
****************************************************************************/
void real_city_report_dialog_update(void *unused)
{
  GHashTable *selected;
  ITree iter;
  gint city_id;

  if (NULL == city_dialog_shell) {
    return;
  }

  /* Save the selection. */
  selected = g_hash_table_new(NULL, NULL);
  for (itree_begin(GTK_TREE_MODEL(city_model), &iter);
       !itree_end(&iter); itree_next(&iter)) {
    if (itree_is_selected(city_selection, &iter)) {
      itree_get(&iter, CRD_COL_CITY_ID, &city_id, -1);
      g_hash_table_insert(selected, GINT_TO_POINTER(city_id), NULL);
    }
  }

  /* Update and restore the selection. */
  gtk_list_store_clear(city_model);
  city_model_fill(city_model, city_selection, selected);
  g_hash_table_destroy(selected);

  update_governor_menu();
}

/************************************************************************//**
  Update the text for a single city in the city report.
****************************************************************************/
void real_city_report_update_city(struct city *pcity)
{
  GtkTreeIter iter;

  if (NULL == city_dialog_shell) {
    return;
  }

  if (!city_model_find(GTK_TREE_MODEL(city_model), &iter, pcity)) {
    gtk_list_store_prepend(city_model, &iter);
  }
  city_model_set(city_model, &iter, pcity);

  update_total_buy_cost();
}

/************************************************************************//**
  Create submenu for changing production target.

  @param group       Group to add actions to
  @param mname       Menu name part of the action identifier
  @param human_mname Format of the human visible menu name
  @param oper        Operation to do when user selects an entry
  @return Created menu
****************************************************************************/
static GMenu *create_change_menu(GActionGroup *group, const char *mname,
                                 const char *human_mname,
                                 enum city_operation_type oper)
{
  GMenu *menu = g_menu_new();
  GMenu *submenu;
  int n;
  char buf[128];

  n = gtk_tree_selection_count_selected_rows(city_selection);

  submenu = g_menu_new();
  append_impr_or_unit_to_menu(submenu, group, mname, "_u",
                              TRUE, FALSE, oper,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), n);
  fc_snprintf(buf, sizeof(buf), human_mname, _("Unit"));
  submenu_append_unref(menu, buf, G_MENU_MODEL(submenu));

  submenu = g_menu_new();
  append_impr_or_unit_to_menu(submenu, group, mname, "_i",
                              FALSE, FALSE, oper,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), n);
  fc_snprintf(buf, sizeof(buf), human_mname, _("Improvement"));
  submenu_append_unref(menu, buf, G_MENU_MODEL(submenu));

  submenu = g_menu_new();
  append_impr_or_unit_to_menu(submenu, group, mname, "_w",
                              FALSE, TRUE, oper,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), n);
  fc_snprintf(buf, sizeof(buf), human_mname, _("Wonder"));
  submenu_append_unref(menu, buf, G_MENU_MODEL(submenu));

  return menu;
}

/************************************************************************//**
  Update the sell menu.
****************************************************************************/
static void recreate_sell_menu(GActionGroup *group)
{
  int n;
  GMenu *menu = g_menu_new();

  n = gtk_tree_selection_count_selected_rows(city_selection);

  append_impr_or_unit_to_menu(menu, group, "sell", "",
                              FALSE, FALSE, CO_SELL,
                              can_city_sell_universal,
                              G_CALLBACK(select_impr_or_unit_callback),
                              n);

  g_menu_remove(cityrep_menu, 2);
  submenu_insert_unref(cityrep_menu, 2, _("S_ell"), G_MENU_MODEL(menu));
}

/************************************************************************//**
  Creates production menu
****************************************************************************/
static GMenu *create_production_menu(GActionGroup *group)
{
  GSimpleAction *act;

  prod_menu = g_menu_new();

  /* TRANS: Menu name part to be used like "Change to Improvement"
   *        This is about changing current production. */
  change_menu = create_change_menu(group, "change", _("Change to %s"), CO_CHANGE);
  submenu_append_unref(prod_menu, _("Chan_ge"), G_MENU_MODEL(change_menu));

  add_first_menu = g_menu_new();

  /* TRANS: Menu name to be used like "Set Improvement first"
   *        This is about adding item to the beginning of the worklist. */
  add_first_menu = create_change_menu(group, "first", _("Set %s first"), CO_FIRST);
  submenu_append_unref(prod_menu, _("Add _First"),
                       G_MENU_MODEL(add_first_menu));

  add_last_menu = g_menu_new();

  /* TRANS: Menu name to be used like "Set Improvement last"
   *        This is about adding item to the end of the worklist. */
  add_last_menu = create_change_menu(group, "last", _("Set %s last"), CO_LAST);
  submenu_append_unref(prod_menu, _("Add _Last"),
                       G_MENU_MODEL(add_last_menu));

  add_next_menu = g_menu_new();

  /* TRANS: Menu name to be used like "Set Improvement next"
   *        This is about adding item after current one on the worklist. */
  add_next_menu = create_change_menu(group, "next", _("Set %s next"), CO_NEXT);
  submenu_append_unref(prod_menu, _("Add _Next"),
                       G_MENU_MODEL(add_next_menu));

  add_2ndlast_menu = g_menu_new();

  /* TRANS: Menu name to be used like "Set Improvement 2nd last"
   *        This is about adding item as second last on the worklist. */
  add_2ndlast_menu = create_change_menu(group, "2ndlast", _("Set %s 2nd last"), CO_NEXT_TO_LAST);
  submenu_append_unref(prod_menu, _("Add _2nd Last"),
                       G_MENU_MODEL(add_2ndlast_menu));

  wl_set_menu = create_wl_menu(group, "set", G_CALLBACK(set_worklist_callback));
  submenu_append_unref(prod_menu, _("Set Worklist"),
                       G_MENU_MODEL(wl_set_menu));

  wl_append_menu = create_wl_menu(group, "append", G_CALLBACK(append_worklist_callback));
  submenu_append_unref(prod_menu, _("Append Worklist"),
                       G_MENU_MODEL(wl_append_menu));

  act = g_simple_action_new("clear_worklist", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_clear_worklist_callback),
                   NULL);
  menu_item_append_unref(prod_menu, g_menu_item_new(_("Clear _Worklist"),
                                                    "win.clear_worklist"));

  return prod_menu;
}

/************************************************************************//**
  Recreates production menu
****************************************************************************/
static void recreate_production_menu(GActionGroup *group)
{
  if (prod_menu != NULL) {
    g_menu_remove_all(change_menu);
    g_menu_remove_all(add_first_menu);
    g_menu_remove_all(add_last_menu);
    g_menu_remove_all(add_next_menu);
    g_menu_remove_all(add_2ndlast_menu);
    g_menu_remove_all(wl_set_menu);
    g_menu_remove_all(wl_append_menu);
    g_menu_remove_all(prod_menu);
  }
  g_menu_remove(cityrep_menu, 0);
  submenu_insert_unref(cityrep_menu, 0, _("_Production"),
                       G_MENU_MODEL(create_production_menu(group)));
}

/************************************************************************//**
  Returns whether city is building given target
****************************************************************************/
static bool city_building_impr_or_unit(const struct city *pcity,
                                       const struct universal *target)
{
  return are_universals_equal(&pcity->production, target);
}

/************************************************************************//**
  Creates select menu
****************************************************************************/
static GMenu *create_select_menu(GActionGroup *group)
{
  GSimpleAction *act;
  char buf[128];

  select_menu = g_menu_new();

#if 0
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_select_menu), NULL);
#endif

  act = g_simple_action_new("select_all", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_all_callback),
                   NULL);
  menu_item_append_unref(select_menu, g_menu_item_new(_("All Cities"),
                                                      "win.select_all"));

  act = g_simple_action_new("select_none", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_unselect_all_callback),
                   NULL);
  menu_item_append_unref(select_menu, g_menu_item_new(_("No Cities"),
                                                      "win.select_none"));

  act = g_simple_action_new("select_invert", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_invert_selection_callback),
                   NULL);
  menu_item_append_unref(select_menu, g_menu_item_new(_("Invert Selection"),
                                                      "win.select_invert"));

  act = g_simple_action_new("select_build_unit", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_building_callback),
                   GINT_TO_POINTER(PCT_UNIT));
  menu_item_append_unref(select_menu, g_menu_item_new(_("Building Units"),
                                                      "win.select_build_unit"));

  act = g_simple_action_new("select_build_impr", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_building_callback),
                   GINT_TO_POINTER(PCT_IMPROVEMENT));
  menu_item_append_unref(select_menu, g_menu_item_new(_("Building Improvements"),
                                                      "win.select_build_impr"));

  act = g_simple_action_new("select_build_wonder", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_building_callback),
                   GINT_TO_POINTER(PCT_WONDER));
  menu_item_append_unref(select_menu, g_menu_item_new(_("Building Wonders"),
                                                      "win.select_build_wonder"));

  unit_b_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(unit_b_select_menu, group, "sel", "_b_u",
                              TRUE, FALSE, CO_NONE,
                              city_building_impr_or_unit,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Building %s"), _("Unit"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(unit_b_select_menu));

  impr_b_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(impr_b_select_menu, group, "sel", "_b_b",
                              FALSE, FALSE, CO_NONE,
                              city_building_impr_or_unit,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Building %s"), _("Improvement"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(impr_b_select_menu));

  wndr_b_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(wndr_b_select_menu, group, "sel", "_b_w",
                              FALSE, TRUE, CO_NONE,
                              city_building_impr_or_unit,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Building %s"), _("Wonder"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(wndr_b_select_menu));

  act = g_simple_action_new("select_coastal", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_coastal_callback),
                   NULL);
  menu_item_append_unref(select_menu, g_menu_item_new(_("Coastal Cities"),
                                                      "win.select_coastal"));

  act = g_simple_action_new("select_island", NULL);
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
  g_signal_connect(act, "activate", G_CALLBACK(city_select_island_callback),
                   NULL);
  menu_item_append_unref(select_menu, g_menu_item_new(_("Same Island"),
                                                      "win.select_island"));

  unit_s_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(unit_s_select_menu, group, "sel", "_s_u",
                              TRUE, FALSE, CO_NONE,
                              city_unit_supported,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Supported %s"), _("Unit"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(unit_s_select_menu));

  unit_p_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(unit_p_select_menu, group, "sel", "_p_u",
                              TRUE, FALSE, CO_NONE,
                              city_unit_present,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Present %s"), _("Unit"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(unit_p_select_menu));

  impr_p_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(impr_p_select_menu, group, "sel", "_p_b",
                              FALSE, FALSE, CO_NONE,
                              city_building_present,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Present %s"), _("Improvement"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(impr_p_select_menu));

  wndr_p_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(wndr_p_select_menu, group, "sel", "_p_w",
                              FALSE, TRUE, CO_NONE,
                              city_building_present,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Present %s"), _("Wonder"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(wndr_p_select_menu));

  unit_a_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(unit_a_select_menu, group, "sel", "_a_u",
                              TRUE, FALSE, CO_NONE,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Available %s"), _("Unit"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(unit_a_select_menu));

  impr_a_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(impr_a_select_menu, group, "sel", "_a_b",
                              FALSE, FALSE, CO_NONE,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Available %s"), _("Improvement"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(impr_a_select_menu));

  wndr_a_select_menu = g_menu_new();
  append_impr_or_unit_to_menu(wndr_a_select_menu, group, "sel", "_a_w",
                              FALSE, TRUE, CO_NONE,
                              can_city_build_now_client,
                              G_CALLBACK(select_impr_or_unit_callback), -1);
  fc_snprintf(buf, sizeof(buf), _("Available %s"), _("Wonder"));
  submenu_append_unref(select_menu, buf, G_MENU_MODEL(wndr_a_select_menu));

  governor_select_menu = create_governor_menu(cityrep_group, FALSE);
  submenu_append_unref(select_menu, _("Citizen Governor"),
                       G_MENU_MODEL(governor_select_menu));

  return select_menu;
}

/************************************************************************//**
  Recreates production menu
****************************************************************************/
static void recreate_select_menu(GActionGroup *group)
{
  if (unit_b_select_menu != NULL) {
    g_menu_remove_all(unit_b_select_menu);
    g_menu_remove_all(impr_b_select_menu);
    g_menu_remove_all(wndr_b_select_menu);
    g_menu_remove_all(unit_s_select_menu);
    g_menu_remove_all(unit_p_select_menu);
    g_menu_remove_all(impr_p_select_menu);
    g_menu_remove_all(wndr_p_select_menu);
    g_menu_remove_all(unit_a_select_menu);
    g_menu_remove_all(impr_a_select_menu);
    g_menu_remove_all(wndr_a_select_menu);
    g_menu_remove_all(governor_select_menu);
    g_menu_remove_all(select_menu);
  }
  g_menu_remove(cityrep_menu, 3);
  submenu_insert_unref(cityrep_menu, 3, _("_Select"),
                       G_MENU_MODEL(create_select_menu(group)));
}

/************************************************************************//**
  Update the value displayed by the "total buy cost" label in the city
  report, or make it blank if nothing can be bought.
****************************************************************************/
static void update_total_buy_cost(void)
{
  GtkWidget *label, *view;
  GList *rows, *p;
  GtkTreeModel *model;
  GtkTreeSelection *sel;
  GtkTreePath *path;
  GtkTreeIter iter;
  struct city *pcity;
  int total = 0;

  view = city_view;
  label = city_total_buy_cost_label;

  if (!view || !label) {
    return;
  }

  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  rows = gtk_tree_selection_get_selected_rows(sel, &model);

  for (p = rows; p != NULL; p = p->next) {
    path = p->data;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
      if ((pcity = city_model_get(model, &iter))) {
        total += pcity->client.buy_cost;
      }
    }
    gtk_tree_path_free(path);
  }
  g_list_free(rows);

  if (total > 0) {
    gchar *buf = g_strdup_printf(_("Total Buy Cost: %d"), total);

    gtk_label_set_text(GTK_LABEL(label), buf);
    g_free(buf);
  } else {
    gtk_label_set_text(GTK_LABEL(label), NULL);
  }
}

/************************************************************************//**
  Update city report button sensitivity and total buy cost label when the
  user makes a change in the selection of cities.
****************************************************************************/
static void city_selection_changed_callback(GtkTreeSelection *selection)
{
#ifdef MENUS_GTK3
  int n;
  bool obs_may, plr_may;

  n = gtk_tree_selection_count_selected_rows(selection);
  obs_may = n > 0;
  plr_may = obs_may && can_client_issue_orders();
#endif /* MENUS_GTK3 */

  update_governor_menu();

#ifdef MENUS_GTK3
  gtk_widget_set_sensitive(city_center_command, obs_may);
  gtk_widget_set_sensitive(city_popup_command, obs_may);
  gtk_widget_set_sensitive(city_buy_command, plr_may);
#endif /* MENUS_GTK3 */

  recreate_production_menu(cityrep_group);
  recreate_select_menu(cityrep_group);
  recreate_sell_menu(cityrep_group);

#ifdef MENUS_GTK3
  if (!plr_may) {
    gtk_widget_set_sensitive(city_sell_command, FALSE);
  }
#endif /* MENUS_GTK3 */

  update_total_buy_cost();
}

/************************************************************************//**
  Clear the worklist in one selected city in the city report.
****************************************************************************/
static void clear_worklist_foreach_func(GtkTreeModel *model,
                                        GtkTreePath *path,
                                        GtkTreeIter *iter,
                                        gpointer data)
{
  struct city *pcity = city_model_get(model, iter);

  if (NULL != pcity) {
    struct worklist empty;

    worklist_init(&empty);
    city_set_worklist(pcity, &empty);
  }
}

/************************************************************************//**
  Called when the "clear worklist" menu item is activated.
****************************************************************************/
static void city_clear_worklist_callback(GSimpleAction *action, GVariant *parameter,
                                         gpointer data)
{
  struct connection *pconn = &client.conn;

  fc_assert_ret(city_selection != NULL);

  connection_do_buffer(pconn);
  gtk_tree_selection_selected_foreach(city_selection,
                                      clear_worklist_foreach_func, NULL);
  connection_do_unbuffer(pconn);
}

/************************************************************************//**
  After a selection rectangle is defined, make the cities that
  are hilited on the canvas exclusively hilited in the
  City List window.
****************************************************************************/
void hilite_cities_from_canvas(void)
{
  ITree it;
  GtkTreeModel *model;

  if (!city_dialog_shell) {
    return;
  }

  model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity = city_model_get(model, TREE_ITER_PTR(it));

    if (NULL != pcity && is_city_hilited(pcity)) {
      itree_select(city_selection, &it);
    }
  }
}

/************************************************************************//**
  Toggle a city's hilited status.
****************************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  GtkTreeIter iter;

  if (NULL == city_dialog_shell) {
    return;
  }

  if (city_model_find(GTK_TREE_MODEL(city_model), &iter, pcity)) {
    if (on_off) {
      gtk_tree_selection_select_iter(city_selection, &iter);
    } else {
      gtk_tree_selection_unselect_iter(city_selection, &iter);
    }
  }
}
