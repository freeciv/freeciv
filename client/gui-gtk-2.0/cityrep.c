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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "chatline.h"
#include "citydlg_common.h"
#include "cityrepdata.h"
#include "civclient.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "mapctrl.h"    /* is_city_hilited() */
#include "mapview_common.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "climisc.h"

#include "cma_fec.h"

#include "citydlg.h"
#include "cityrep.h"

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))
#define CMA_NONE	(-1)
#define CMA_CUSTOM	(-2)

enum city_operation_type {
  CO_CHANGE, CO_LAST, CO_NEXT, CO_FIRST, CO_NONE
};

/******************************************************************/
static void create_city_report_dialog(bool make_modal);
static void city_model_init(void);

static void city_activated_callback(GtkTreeView *view, GtkTreePath *path,
				    GtkTreeViewColumn *col, gpointer data);

static void city_command_callback(struct gui_dialog *dlg, int response);

static void city_selection_changed_callback(GtkTreeSelection *selection);

static void create_select_menu(GtkWidget *item);
static void create_change_menu(GtkWidget *item);
static void create_last_menu(GtkWidget *item);
static void create_first_menu(GtkWidget *item);
static void create_next_menu(GtkWidget *item);

static struct gui_dialog *city_dialog_shell = NULL;

enum {
  CITY_CENTER = 1, CITY_POPUP, CITY_BUY
};

static GtkWidget *city_view;
static GtkTreeSelection *city_selection;
static GtkListStore *city_model;

static void popup_select_menu(GtkMenuShell *menu, gpointer data);
static void popup_change_menu(GtkMenuShell *menu, gpointer data);
static void popup_last_menu(GtkMenuShell *menu, gpointer data);
static void popup_first_menu(GtkMenuShell *menu, gpointer data);
static void popup_next_menu(GtkMenuShell *menu, gpointer data);

static GtkWidget *city_center_command, *city_popup_command, *city_buy_command;
static GtkWidget *city_change_command;
static GtkWidget *city_last_command, *city_first_command, *city_next_command;


static GtkWidget *change_improvements_item;
static GtkWidget *change_units_item;
static GtkWidget *change_wonders_item;
static GtkWidget *change_cma_item;

static GtkWidget *last_improvements_item;
static GtkWidget *last_units_item;
static GtkWidget *last_wonders_item;

static GtkWidget *first_improvements_item;
static GtkWidget *first_units_item;
static GtkWidget *first_wonders_item;

static GtkWidget *next_improvements_item;
static GtkWidget *next_units_item;
static GtkWidget *next_wonders_item;


static GtkWidget *select_island_item;

static GtkWidget *select_bunit_item;
static GtkWidget *select_bimprovement_item;
static GtkWidget *select_bwonder_item;

static GtkWidget *select_supported_item;
static GtkWidget *select_present_item;
static GtkWidget *select_built_improvements_item;
static GtkWidget *select_built_wonders_item;

static GtkWidget *select_improvements_item;
static GtkWidget *select_units_item;
static GtkWidget *select_wonders_item;
static GtkWidget *select_cma_item;

static int city_dialog_shell_is_modal;

bool select_menu_cached;

/****************************************************************
 Return text line for the column headers for the city report
*****************************************************************/
static void get_city_table_header(char *text[], int n)
{
  struct city_report_spec *spec;
  int i;

  for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    my_snprintf(text[i], n, "%*s\n%*s",
	    NEG_VAL(spec->width), spec->title1 ? spec->title1 : "",
	    NEG_VAL(spec->width), spec->title2 ? spec->title2 : "");
  }
}

/****************************************************************

                      CITY REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(bool make_modal)
{
  if(!city_dialog_shell) {
    city_dialog_shell_is_modal = make_modal;
    
    create_city_report_dialog(make_modal);

    select_menu_cached = FALSE;
  }

  gui_dialog_present(city_dialog_shell);
  hilite_cities_from_canvas();
}

/****************************************************************
 Raises the city report dialog.
****************************************************************/
void raise_city_report_dialog(void)
{
  popup_city_report_dialog(FALSE);
  gui_dialog_raise(city_dialog_shell);
}

/****************************************************************
 Closes the city report dialog.
****************************************************************/
void popdown_city_report_dialog(void)
{
  if (city_dialog_shell) {
    gui_dialog_destroy(city_dialog_shell);
  }
}


/****************************************************************
...
*****************************************************************/
typedef bool (*TestCityFunc)(struct city *, gint);

/****************************************************************
...
*****************************************************************/
static void append_impr_or_unit_to_menu_item(GtkMenuItem *parent_item,
					     bool append_units,
					     bool append_wonders,
					     enum city_operation_type 
					     city_operation, 
					     TestCityFunc test_func,
					     GCallback callback,
					     int size)
{
  GtkWidget *menu;
  cid cids[U_LAST + B_LAST];
  struct item items[U_LAST + B_LAST];
  int i, item, cids_used;
  char *row[4];
  char buf[4][64];
  
  GtkSizeGroup *group[3];
  const char *markup[3] = {
    "weight=\"bold\"",
    "",
    ""
  };

  gtk_menu_item_remove_submenu(parent_item);
  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(parent_item, menu);

  if (city_operation != CO_NONE) {
    GPtrArray *selected;
    ITree it;
    int num_selected = 0;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);
    struct city **data;

    selected = g_ptr_array_sized_new(size);

    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      gpointer res;
    
      if (!itree_is_selected(city_selection, &it))
    	continue;

      itree_get(&it, 0, &res, -1);
      g_ptr_array_add(selected, res);
      num_selected++;
    }

    data = (struct city **)g_ptr_array_free(selected, FALSE);
    cids_used = collect_cids1(cids, data, num_selected, append_units,
        		      append_wonders, TRUE, test_func);
    g_free(data);
  } else {
    cids_used = collect_cids1(cids, NULL, 0, append_units,
			      append_wonders, FALSE, test_func);
  }

  name_and_sort_items(cids, cids_used, items, city_operation != CO_NONE, NULL);

  for (i = 0; i < 4; i++) {
    row[i] = buf[i];
  }

  g_object_set_data(G_OBJECT(menu), "freeciv_test_func", test_func);
  g_object_set_data(G_OBJECT(menu), "freeciv_city_operation",
		    GINT_TO_POINTER(city_operation));

  for (i = 0; i < 3; i++) {
    group[i] = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  }
  
  for (item = 0; item < cids_used; item++) {
    cid cid = items[item].cid;
    GtkWidget *menu_item, *hbox, *label;
    char txt[256];

    get_city_dialog_production_row(row, sizeof(buf[0]), cid_id(cid),
				   cid_is_unit(cid), NULL);

    menu_item = gtk_menu_item_new();
    hbox = gtk_hbox_new(FALSE, 18);
    gtk_container_add(GTK_CONTAINER(menu_item), hbox);

    for (i = 0; i < 3; i++) {
      if (row[i][0] == '\0') {
	continue;
      }

      my_snprintf(txt, ARRAY_SIZE(txt), "<span %s>%s</span>",
		  markup[i], row[i]);

      label = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(label), txt);

      if (i < 2) {
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
      } else {
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
      }

      gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 0);
      gtk_size_group_add_widget(group[i], label);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(menu_item, "activate", callback,
		     GINT_TO_POINTER(items[item].cid));
  }
  
  for (i = 0; i < 3; i++) {
    g_object_unref(group[i]);
  }
  
  gtk_widget_show_all(menu);

  gtk_widget_set_sensitive(GTK_WIDGET(parent_item), (cids_used > 0));
}

/****************************************************************
...
*****************************************************************/
static void impr_or_unit_iterate(GtkTreeModel *model, GtkTreePath *path,
				 GtkTreeIter *it, gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  gint id;

  gtk_tree_model_get(model, it, 1, &id, -1);

  city_change_production(find_city_by_id(id), cid_is_unit(cid), cid_id(cid));
}

/****************************************************************
 Called by select_impr_or_unit_callback for each city that 
 is selected in the city list dialog to have a object appended
 to the worklist.  Sends a packet adding the item to the 
 end of the worklist.
*****************************************************************/
static void worklist_last_impr_or_unit_iterate(GtkTreeModel *model, 
						 GtkTreePath *path,
						 GtkTreeIter *it, 
						 gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  gint id;
  struct city *pcity;  

  gtk_tree_model_get(model, it, 1, &id, -1);
  pcity = find_city_by_id(id);

  (void) city_queue_insert(pcity, -1, cid_is_unit(cid), cid_id(cid));
  /* perhaps should warn the user if not successful? */
}

/****************************************************************
 Called by select_impr_or_unit_callback for each city that 
 is selected in the city list dialog to have a object inserted
 first to the worklist.  Sends a packet adding the current
 production to the first place after the current production of the
 worklist.
 Then changes the production to the requested item.
*****************************************************************/
static void worklist_first_impr_or_unit_iterate(GtkTreeModel *model, 
						 GtkTreePath *path,
						 GtkTreeIter *it, 
						 gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  gint id;
  struct city *pcity;  

  gtk_tree_model_get(model, it, 1, &id, -1);
  pcity = find_city_by_id(id);

  (void) city_queue_insert(pcity, 0, cid_is_unit(cid), cid_id(cid));
  /* perhaps should warn the user if not successful? */
}

/****************************************************************
 Called by select_impr_or_unit_callback for each city that 
 is selected in the city list dialog to have a object added next
 to the worklist.  Sends a packet adding the item to the 
 first place after the current production of the worklist.
*****************************************************************/
static void worklist_next_impr_or_unit_iterate(GtkTreeModel *model, 
						 GtkTreePath *path,
						 GtkTreeIter *it, 
						 gpointer data)
{
  struct city *pcity;
  gint id;
  cid cid = GPOINTER_TO_INT(data);

  gtk_tree_model_get(model, it, 1, &id, -1);
  pcity = find_city_by_id(id);

  (void) city_queue_insert(pcity, 1, cid_is_unit(cid), cid_id(cid));
  /* perhaps should warn the user if not successful? */
}

/****************************************************************
...
*****************************************************************/
static void select_impr_or_unit_callback(GtkWidget *w, gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  GObject *parent = G_OBJECT(w->parent);
  TestCityFunc test_func = g_object_get_data(parent, "freeciv_test_func");
  enum city_operation_type city_operation = 
    GPOINTER_TO_INT(g_object_get_data(parent, "freeciv_city_operation"));  

  /* if this is not a city operation: */
  if (city_operation == CO_NONE) {
    ITree it;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity;
      gpointer res;
      
      itree_get(&it, 0, &res, -1);
      pcity = res;

      if (test_func(pcity, cid)) {
	itree_select(city_selection, &it);
      }
    }
  } else {
    connection_do_buffer(&aconnection);
    switch (city_operation) {
    case CO_LAST:
      gtk_tree_selection_selected_foreach(city_selection,
					  worklist_last_impr_or_unit_iterate,
					  GINT_TO_POINTER(cid));
      break;
    case CO_CHANGE:
      gtk_tree_selection_selected_foreach(city_selection,
					  impr_or_unit_iterate, GINT_TO_POINTER(cid));
      break;
    case CO_FIRST:
      gtk_tree_selection_selected_foreach(city_selection,
					  worklist_first_impr_or_unit_iterate,
					  GINT_TO_POINTER(cid));
      break;
    case CO_NEXT:
      gtk_tree_selection_selected_foreach(city_selection,
					  worklist_next_impr_or_unit_iterate,
					  GINT_TO_POINTER(cid));
      break;
    default:
      assert(FALSE); /* should never get here. */
    }
    connection_do_unbuffer(&aconnection);
  }
}

/****************************************************************
...
*****************************************************************/
static void cma_iterate(GtkTreeModel *model, GtkTreePath *path,
			GtkTreeIter *it, gpointer data)
{
  struct city *pcity;
  int idx = GPOINTER_TO_INT(data);
  gpointer res;

  gtk_tree_model_get(GTK_TREE_MODEL(city_model), it, 0, &res, -1);
  pcity = res;

   if (idx == CMA_NONE) {
     cma_release_city(pcity);
   } else {
     cma_put_city_under_agent(pcity, cmafec_preset_get_parameter(idx));
   }
   refresh_city_dialog(pcity);
}

/****************************************************************
 Called when one clicks on an CMA item to make a selection or to
 change a selection's preset.
*****************************************************************/
static void select_cma_callback(GtkWidget * w, gpointer data)
{
  int idx = GPOINTER_TO_INT(data);
  GObject *parent = G_OBJECT(w->parent);
  bool change_cma =
      GPOINTER_TO_INT(g_object_get_data(parent, "freeciv_change_cma"));
  struct cm_parameter parameter;

  /* If this is not the change button but the select cities button. */
  if (!change_cma) {
    ITree it;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity;
      int controlled;
      bool select;
      gpointer res;

      itree_get(&it, 0, &res, -1);
      pcity = res;
      controlled = cma_is_city_under_agent(pcity, &parameter);
      select = FALSE;

      if (idx == CMA_CUSTOM && controlled
          && cmafec_preset_get_index_of_parameter(&parameter) == -1) {
        select = TRUE;
      } else if (idx == CMA_NONE && !controlled) {
        select = TRUE;
      } else if (idx >= 0 && controlled &&
        	 cm_are_parameter_equal(&parameter,
        				cmafec_preset_get_parameter(idx))) {
        select = TRUE;
      }

      if (select) {
	itree_select(city_selection, &it);
      }
    }
  } else {
    reports_freeze();
    gtk_tree_selection_selected_foreach(city_selection,
					cma_iterate, GINT_TO_POINTER(idx));
    reports_thaw();
  }
}

/****************************************************************
 Create the cma entries in the change menu and the select menu. The
 indices CMA_NONE (aka -1) and CMA_CUSTOM (aka -2) are
 special. CMA_NONE signifies a preset of "none" and CMA_CUSTOM a
 "custom" preset.
*****************************************************************/
static void append_cma_to_menu_item(GtkMenuItem *parent_item, bool change_cma)
{
  GtkWidget *menu;
  int i;
  struct cm_parameter parameter;
  GtkWidget *w;

  gtk_menu_item_remove_submenu(parent_item);
  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(parent_item, menu);

  if (change_cma) {
    for (i = -1; i < cmafec_preset_num(); i++) {
      w = (i == -1 ? gtk_menu_item_new_with_label(_("none"))
	   : gtk_menu_item_new_with_label(cmafec_preset_get_descr(i)));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
      g_signal_connect(w, "activate", G_CALLBACK(select_cma_callback),
		       GINT_TO_POINTER(i));
    }
  } else {
    /* search for a "none" */
    int found;

    found = 0;
    city_list_iterate(game.player_ptr->cities, pcity) {
      if (!cma_is_city_under_agent(pcity, NULL)) {
	found = 1;
	break;
      }
    } city_list_iterate_end;

    if (found) {
      w = gtk_menu_item_new_with_label(_("none"));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
      g_signal_connect(w, "activate", G_CALLBACK(select_cma_callback),
		       GINT_TO_POINTER(CMA_NONE));
    }

    /* 
     * Search for a city that's under custom (not preset) agent. Might
     * take a lonnggg time.
     */
    found = 0;
    city_list_iterate(game.player_ptr->cities, pcity) {
      if (cma_is_city_under_agent(pcity, &parameter) &&
	  cmafec_preset_get_index_of_parameter(&parameter) == -1) {
	found = 1;
	break;
      }
    } city_list_iterate_end;

    if (found) {
      /* we found city that's under agent but not a preset */
      w = gtk_menu_item_new_with_label(_("custom"));

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
      g_signal_connect(w, "activate",
	G_CALLBACK(select_cma_callback), GINT_TO_POINTER(CMA_CUSTOM));
    }

    /* only fill in presets that are being used. */
    for (i = 0; i < cmafec_preset_num(); i++) {
      found = 0;
      city_list_iterate(game.player_ptr->cities, pcity) {
	if (cma_is_city_under_agent(pcity, &parameter) &&
	    cm_are_parameter_equal(&parameter,
				   cmafec_preset_get_parameter(i))) {
	  found = 1;
	  break;
	}
      } city_list_iterate_end;
      if (found) {
	w = gtk_menu_item_new_with_label(cmafec_preset_get_descr(i));

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
	g_signal_connect(w, "activate",
	  G_CALLBACK(select_cma_callback), GINT_TO_POINTER(i));
      }
    }
  }

  g_object_set_data(G_OBJECT(menu), "freeciv_change_cma",
		    GINT_TO_POINTER(change_cma));
  gtk_widget_show_all(menu);
}

/****************************************************************
...
*****************************************************************/
static void city_report_update_views(void)
{
  struct city_report_spec *spec;
  int i;

  for (i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    GtkTreeViewColumn *col;

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(city_view), i);
    gtk_tree_view_column_set_visible(col, spec->show);
  }
}

/****************************************************************
...
*****************************************************************/
static void toggle_view(GtkCheckMenuItem *item, gpointer data)
{
  struct city_report_spec *spec = data;

  spec->show ^= 1;
  city_report_update_views();
}

/****************************************************************
...
*****************************************************************/
static void update_view_menu(GtkWidget *show_item)
{
  GtkWidget *menu, *item;
  struct city_report_spec *spec;
  int i;

  menu = gtk_menu_new();
  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    item = gtk_check_menu_item_new_with_label(spec->explanation);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), spec->show);
    g_signal_connect(item, "toggled", G_CALLBACK(toggle_view), (gpointer)spec);
  }
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(show_item), menu);
}

/****************************************************************
...
*****************************************************************/
static GtkWidget *create_city_report_menubar(void)
{
  GtkWidget *menubar, *item;

  menubar = gtk_menu_bar_new();
  
  item = gtk_menu_item_new_with_mnemonic(_("Chan_ge"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  city_change_command = item;
  create_change_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("Add _First"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  city_first_command = item;
  create_first_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("Add _Next"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  city_next_command = item;
  create_next_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("Add _Last"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  city_last_command = item;
  create_last_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("_Select"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  create_select_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("S_how"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  update_view_menu(item);
  return menubar;
}

/****************************************************************
...
*****************************************************************/
static void cityrep_cell_data_func(GtkTreeViewColumn *col,
				   GtkCellRenderer *cell,
				   GtkTreeModel *model, GtkTreeIter *it,
				   gpointer data)
{
  struct city_report_spec *sp;
  struct city             *pcity;
  GValue                   value = { 0, };
  gint                     n;
  static char              buf[64];

  n = GPOINTER_TO_INT(data);

  gtk_tree_model_get_value(model, it, 0, &value);
  pcity = g_value_get_pointer(&value);
  g_value_unset(&value);

  sp = &city_report_specs[n];
  my_snprintf(buf, sizeof(buf), "%*s", NEG_VAL(sp->width), (sp->func)(pcity));

  g_value_init(&value, G_TYPE_STRING);
  g_value_set_string(&value, buf);
  g_object_set_property(G_OBJECT(cell), "text", &value);
  g_value_unset(&value);
}

/****************************************************************
...
*****************************************************************/
static gint cityrep_sort_func(GtkTreeModel *model,
			      GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
  struct city_report_spec *sp;
  GValue                   value = { 0, };
  struct city             *pcity1;
  struct city             *pcity2;
  static char              buf1[64];
  static char              buf2[64];
  gint                     n;

  n = GPOINTER_TO_INT(data);

  gtk_tree_model_get_value(model, a, 0, &value);
  pcity1 = g_value_get_pointer(&value);
  g_value_unset(&value);
  gtk_tree_model_get_value(model, b, 0, &value);
  pcity2 = g_value_get_pointer(&value);
  g_value_unset(&value);

  sp = &city_report_specs[n];
  my_snprintf(buf1, sizeof(buf1), "%*s",NEG_VAL(sp->width),(sp->func)(pcity1));
  my_snprintf(buf2, sizeof(buf2), "%*s",NEG_VAL(sp->width),(sp->func)(pcity2));

  return cityrepfield_compare(buf1, buf2);
}

/****************************************************************
...
*****************************************************************/
static void create_city_report_dialog(bool make_modal)
{
  static char *titles [NUM_CREPORT_COLS];
  static char  buf    [NUM_CREPORT_COLS][64];
  struct city_report_spec *spec;

  GtkWidget *w, *sw, *menubar;
  int i;
  
  gui_dialog_new(&city_dialog_shell, GTK_NOTEBOOK(top_notebook));
  gui_dialog_set_title(city_dialog_shell, _("Cities"));

  gui_dialog_set_default_size(city_dialog_shell, -1, 420);

  gui_dialog_response_set_callback(city_dialog_shell,
      city_command_callback);

  /* menubar */
  menubar = create_city_report_menubar();
  gtk_box_pack_start(GTK_BOX(city_dialog_shell->vbox),
	menubar, FALSE, FALSE, 0);

  /* buttons */
  w = gui_dialog_add_stockbutton(city_dialog_shell, GTK_STOCK_ZOOM_FIT,
      _("Cen_ter"), CITY_CENTER);
  city_center_command = w;

  w = gui_dialog_add_stockbutton(city_dialog_shell, GTK_STOCK_ZOOM_IN,
      _("_Popup"), CITY_POPUP);
  city_popup_command = w;

  w = gui_dialog_add_stockbutton(city_dialog_shell, GTK_STOCK_EXECUTE,
      _("_Buy"), CITY_BUY);
  city_buy_command = w;

  gui_dialog_add_button(city_dialog_shell,
			GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

  gui_dialog_set_default_response(city_dialog_shell,
				  GTK_RESPONSE_CLOSE);

  /* tree view */
  for (i=0; i<NUM_CREPORT_COLS; i++)
    titles[i] = buf[i];

  get_city_table_header(titles, sizeof(buf[0]));

  city_model = gtk_list_store_new(2, G_TYPE_POINTER, G_TYPE_INT);

  city_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(city_model));
  g_object_unref(city_model);
  gtk_widget_set_name(city_view, "small font");
  g_signal_connect(city_view, "row_activated",
		   G_CALLBACK(city_activated_callback), NULL);
  city_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(city_view));
  gtk_tree_selection_set_mode(city_selection, GTK_SELECTION_MULTIPLE);
  g_signal_connect(city_selection, "changed",
	G_CALLBACK(city_selection_changed_callback), NULL);
  
  for (i=0; i<NUM_CREPORT_COLS; i++) {
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(city_model), i,
	cityrep_sort_func, GINT_TO_POINTER(i), NULL);
  }

  for (i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,NULL);
    gtk_tree_view_column_set_visible(col, spec->show);
    gtk_tree_view_column_set_sort_column_id(col, i);
    gtk_tree_view_append_column(GTK_TREE_VIEW(city_view), col);
    gtk_tree_view_column_set_cell_data_func(col, renderer,
      cityrep_cell_data_func, GINT_TO_POINTER(i), NULL);
  }

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(sw), city_view);

  gtk_box_pack_start(GTK_BOX(city_dialog_shell->vbox),
	sw, TRUE, TRUE, 0);

  city_model_init();
  gui_dialog_show_all(city_dialog_shell);

  city_selection_changed_callback(city_selection);
}

/****************************************************************
...
*****************************************************************/
static void city_select_all_callback(GtkMenuItem *item, gpointer data)
{
  gtk_tree_selection_select_all(city_selection);
}

/****************************************************************
...
*****************************************************************/
static void city_unselect_all_callback(GtkMenuItem *item, gpointer data)
{
  gtk_tree_selection_unselect_all(city_selection);
}

/****************************************************************
...
*****************************************************************/
static void city_invert_selection_callback(GtkMenuItem *item, gpointer data)
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

/****************************************************************
...
*****************************************************************/
static void city_select_coastal_callback(GtkMenuItem *item, gpointer data)
{
  ITree it;
  GtkTreeModel *model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;
    gpointer res;

    itree_get(&it, 0, &res, -1);
    pcity = res;

    if (is_ocean_near_tile(pcity->tile)) {
      itree_select(city_selection, &it);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void same_island_iterate(GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, gpointer data)
{
  ITree it;
  struct city *selectedcity;
  gpointer res;

  gtk_tree_model_get(model, iter, 0, &res, -1);
  selectedcity = res;

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;

    itree_get(&it, 0, &res, -1);
    pcity = res;

    if (map_get_continent(pcity->tile)
	== map_get_continent(selectedcity->tile)) {
      itree_select(city_selection, &it);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void city_select_same_island_callback(GtkMenuItem *item, gpointer data)
{
  gtk_tree_selection_selected_foreach(city_selection,same_island_iterate,NULL);
}
      
/****************************************************************
...
*****************************************************************/
static void city_select_building_callback(GtkMenuItem *item, gpointer data)
{
  enum production_class_type which = GPOINTER_TO_INT(data);
  ITree it;
  GtkTreeModel *model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;
    gpointer res;

    itree_get(&it, 0, &res, -1);
    pcity = res;

    if ( (which == TYPE_UNIT && pcity->is_building_unit)
         || (which == TYPE_NORMAL_IMPROVEMENT && !pcity->is_building_unit
             && !is_wonder(pcity->currently_building))
         || (which == TYPE_WONDER && !pcity->is_building_unit
             && is_wonder(pcity->currently_building)) ) {
      itree_select(city_selection, &it);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void buy_iterate(GtkTreeModel *model, GtkTreePath *path,
			GtkTreeIter *it, gpointer data)
{
  gpointer res;

  gtk_tree_model_get(model, it, 0, &res, -1);

  cityrep_buy(res);
}

/****************************************************************
...
*****************************************************************/
static void center_iterate(GtkTreeModel *model, GtkTreePath *path,
			   GtkTreeIter *it, gpointer data)
{
  struct city *pcity;
  gpointer res;

  gtk_tree_model_get(model, it, 0, &res, -1);
  pcity = res;
  center_tile_mapcanvas(pcity->tile);
}

/****************************************************************
...
*****************************************************************/
static void popup_iterate(GtkTreeModel *model, GtkTreePath *path,
			  GtkTreeIter *it, gpointer data)
{
  struct city *pcity;
  gpointer res;

  gtk_tree_model_get(model, it, 0, &res, -1);
  pcity = res;

  if (center_when_popup_city) {
    center_tile_mapcanvas(pcity->tile);
  }

  popup_city_dialog(pcity, 0);
}

/****************************************************************
...
*****************************************************************/
static void city_command_callback(struct gui_dialog *dlg, int response)
{
  switch (response) {
  case CITY_CENTER:
    gtk_tree_selection_selected_foreach(city_selection, center_iterate, NULL);
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

/****************************************************************
...
*****************************************************************/
static void city_activated_callback(GtkTreeView *view, GtkTreePath *path,
				    GtkTreeViewColumn *col, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  GdkModifierType mask;

  model = gtk_tree_view_get_model(view);

  if (!gtk_tree_model_get_iter(model, &it, path)) {
    return;
  }

  gdk_window_get_pointer(NULL, NULL, NULL, &mask);

  if (!(mask & GDK_CONTROL_MASK)) {
    popup_iterate(model, path, &it, NULL);
  } else {
    center_iterate(model, path, &it, NULL);
  }
}

/****************************************************************
...
*****************************************************************/
static void update_row(GtkTreeIter *row, struct city *pcity)
{
  GValue value = { 0, };

  g_value_init(&value, G_TYPE_POINTER);
  g_value_set_pointer(&value, pcity);
  gtk_list_store_set_value(city_model, row, 0, &value);
  g_value_unset(&value);

  g_value_init(&value, G_TYPE_INT);
  g_value_set_int(&value, pcity->id);
  gtk_list_store_set_value(city_model, row, 1, &value);
  g_value_unset(&value);
}

/****************************************************************
 Optimized version of city_report_dialog_update() for 1st popup.
*****************************************************************/
static void city_model_init(void)
{
  if (city_dialog_shell && !is_report_dialogs_frozen()) {

    city_list_iterate(game.player_ptr->cities, pcity) {
      GtkTreeIter it;

      gtk_list_store_append(city_model, &it);
      update_row(&it, pcity);
    } city_list_iterate_end;
  }
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if (city_dialog_shell && !is_report_dialogs_frozen()) {
    GtkTreeModel *model;
    GtkTreeIter it;
    GHashTable *copy;

    model = GTK_TREE_MODEL(city_model);

    /* copy the selection. */
    copy = g_hash_table_new(NULL, NULL);
    if (gtk_tree_model_get_iter_first(model, &it)) {
      do {
	if (gtk_tree_selection_iter_is_selected(city_selection, &it)) {
	  gpointer pcity;

	  gtk_tree_model_get(model, &it, 0, &pcity, -1);
	  g_hash_table_insert(copy, pcity, NULL);
	}
      } while (gtk_tree_model_iter_next(model, &it));
    }
    
    /* update. */
    gtk_list_store_clear(city_model);

    city_list_iterate(game.player_ptr->cities, pcity) {
      gtk_list_store_append(city_model, &it);
      update_row(&it, pcity);

      if (g_hash_table_remove(copy, pcity)) {
	gtk_tree_selection_select_iter(city_selection, &it);
      }
    } city_list_iterate_end;

    /* free the selection. */
    g_hash_table_destroy(copy);

    city_selection_changed_callback(city_selection);

    select_menu_cached = FALSE;
  }
}

/****************************************************************
  Update the text for a single city in the city report
*****************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  if (city_dialog_shell && !is_report_dialogs_frozen()) {
    ITree it;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);
    bool found;

    /* search for pcity in the current store. */
    found = FALSE;
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *iter;

      itree_get(&it, 0, &iter, -1);

      if (pcity == iter) {
	found = TRUE;
	break;
      }
    }

    /* update. */
    if (found) {
      update_row(TREE_ITER_PTR(it), pcity);

      select_menu_cached = FALSE;
    } else {
      city_report_dialog_update();
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void create_change_menu(GtkWidget *item)
{
  GtkWidget *menu;

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_change_menu), NULL);

  change_units_item = gtk_menu_item_new_with_label(_("Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), change_units_item);
  change_improvements_item = gtk_menu_item_new_with_label(_("Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), change_improvements_item);
  change_wonders_item = gtk_menu_item_new_with_label(_("Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), change_wonders_item);
  change_cma_item = gtk_menu_item_new_with_label(_("CMA"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), change_cma_item);
}

/****************************************************************
Creates the last menu.
*****************************************************************/
static void create_last_menu(GtkWidget *item)
{
  GtkWidget *menu;

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_last_menu), NULL);

  last_units_item = gtk_menu_item_new_with_label(_("Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), last_units_item);
  last_improvements_item = gtk_menu_item_new_with_label(_("Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), last_improvements_item);
  last_wonders_item = gtk_menu_item_new_with_label(_("Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), last_wonders_item);
}

/****************************************************************
Creates the first menu.
*****************************************************************/
static void create_first_menu(GtkWidget *item)
{
  GtkWidget *menu;

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_first_menu), NULL);

  first_units_item = gtk_menu_item_new_with_label(_("Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), first_units_item);
  first_improvements_item = gtk_menu_item_new_with_label(_("Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), first_improvements_item);
  first_wonders_item = gtk_menu_item_new_with_label(_("Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), first_wonders_item);
}

/****************************************************************
Creates the next menu.
*****************************************************************/
static void create_next_menu(GtkWidget *item)
{
  GtkWidget *menu;

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_next_menu), NULL);

  next_units_item = gtk_menu_item_new_with_label(_("Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), next_units_item);
  next_improvements_item = gtk_menu_item_new_with_label(_("Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), next_improvements_item);
  next_wonders_item = gtk_menu_item_new_with_label(_("Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), next_wonders_item);
}



/****************************************************************
...
*****************************************************************/
static void popup_change_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  n = gtk_tree_selection_count_selected_rows(city_selection);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_improvements_item),
				  FALSE, FALSE, CO_CHANGE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_units_item),
				  TRUE, FALSE, CO_CHANGE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_wonders_item),
				  FALSE, TRUE, CO_CHANGE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_cma_to_menu_item(GTK_MENU_ITEM(change_cma_item), TRUE);
}

/****************************************************************
pops up the last menu.
*****************************************************************/
static void popup_last_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  n = gtk_tree_selection_count_selected_rows(city_selection);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(last_improvements_item),
				  FALSE, FALSE, CO_LAST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(last_units_item),
				  TRUE, FALSE, CO_LAST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(last_wonders_item),
				  FALSE, TRUE, CO_LAST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
}

/****************************************************************
pops up the first menu.
*****************************************************************/
static void popup_first_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  n = gtk_tree_selection_count_selected_rows(city_selection);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(first_improvements_item),
				  FALSE, FALSE, CO_FIRST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(first_units_item),
				  TRUE, FALSE, CO_FIRST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(first_wonders_item),
				  FALSE, TRUE, CO_FIRST,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
}

/****************************************************************
pops up the next menu.
*****************************************************************/
static void popup_next_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  n = gtk_tree_selection_count_selected_rows(city_selection);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(next_improvements_item),
				  FALSE, FALSE, CO_NEXT,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(next_units_item),
				  TRUE, FALSE, CO_NEXT,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(next_wonders_item),
				  FALSE, TRUE, CO_NEXT,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
}



/****************************************************************
...
*****************************************************************/
static void create_select_menu(GtkWidget *item)
{
  GtkWidget *menu;

  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  g_signal_connect(menu, "show", G_CALLBACK(popup_select_menu), NULL);

  item = gtk_menu_item_new_with_label(_("All Cities"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
		   G_CALLBACK(city_select_all_callback), NULL);

  item = gtk_menu_item_new_with_label(_("No Cities"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
  		   G_CALLBACK(city_unselect_all_callback), NULL);

  item = gtk_menu_item_new_with_label(_("Invert Selection"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
		   G_CALLBACK(city_invert_selection_callback), NULL);


  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);


  item = gtk_menu_item_new_with_label(_("Building Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
  		   G_CALLBACK(city_select_building_callback),
		   GINT_TO_POINTER(TYPE_UNIT));

  item = gtk_menu_item_new_with_label( _("Building Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
  		   G_CALLBACK(city_select_building_callback),
		   GINT_TO_POINTER(TYPE_NORMAL_IMPROVEMENT));

  item = gtk_menu_item_new_with_label(_("Building Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
  		   G_CALLBACK(city_select_building_callback),
		   GINT_TO_POINTER(TYPE_WONDER));


  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);


  select_bunit_item =
	gtk_menu_item_new_with_label(_("Building Unit"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_bunit_item);

  select_bimprovement_item =
	gtk_menu_item_new_with_label( _("Building Improvement"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_bimprovement_item);

  select_bwonder_item =
	gtk_menu_item_new_with_label(_("Building Wonder"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_bwonder_item);


  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);


  item = gtk_menu_item_new_with_label(_("Coastal Cities"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate",
		   G_CALLBACK(city_select_coastal_callback), NULL);

  select_island_item = gtk_menu_item_new_with_label(_("Same Island"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_island_item);
  g_signal_connect(select_island_item, "activate",
		   G_CALLBACK(city_select_same_island_callback), NULL);


  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);


  select_supported_item = gtk_menu_item_new_with_label(_("Supported Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_supported_item);

  select_present_item = gtk_menu_item_new_with_label(_("Units Present"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_present_item);

  select_built_improvements_item =
	gtk_menu_item_new_with_label(_("Improvements in City"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_built_improvements_item);

  select_built_wonders_item =
	gtk_menu_item_new_with_label(_("Wonders in City"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_built_wonders_item);


  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

  
  select_units_item =
	gtk_menu_item_new_with_label(_("Available Units"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_units_item);
  select_improvements_item =
	gtk_menu_item_new_with_label(_("Available Improvements"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_improvements_item);
  select_wonders_item =
	gtk_menu_item_new_with_label(_("Available Wonders"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_wonders_item);
  select_cma_item =
	gtk_menu_item_new_with_label(_("CMA"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), select_cma_item);
}

/****************************************************************
...
*****************************************************************/
static bool city_building_impr_or_unit(struct city *pcity, cid cid)
{
  return (cid == cid_encode_from_city(pcity));
}

/****************************************************************
...
*****************************************************************/
static void popup_select_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  if (select_menu_cached)
    return;

  n = gtk_tree_selection_count_selected_rows(city_selection);
  gtk_widget_set_sensitive(select_island_item, (n > 0));

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bunit_item),
				  TRUE, FALSE, CO_NONE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bimprovement_item),
				  FALSE, FALSE, CO_NONE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bwonder_item),
				  FALSE, TRUE, CO_NONE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_supported_item),
				  TRUE, FALSE, CO_NONE,
				  city_unit_supported,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_present_item),
				  TRUE, FALSE, CO_NONE,
				  city_unit_present,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_built_improvements_item),
				  FALSE, FALSE, CO_NONE,
				   city_building_present,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_built_wonders_item),
				  FALSE, TRUE, CO_NONE,
				   city_building_present,
				  G_CALLBACK(select_impr_or_unit_callback), -1);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_improvements_item),
				  FALSE, FALSE, CO_NONE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_units_item),
				  TRUE, FALSE, CO_NONE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_wonders_item),
				  FALSE, TRUE, CO_NONE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_cma_to_menu_item(GTK_MENU_ITEM(select_cma_item), FALSE);

  select_menu_cached = TRUE;
}

/****************************************************************
...
*****************************************************************/
static void city_selection_changed_callback(GtkTreeSelection *selection)
{
  int n;

  n = gtk_tree_selection_count_selected_rows(selection);

  if (n == 0) {
    gtk_widget_set_sensitive(city_change_command, FALSE);
    gtk_widget_set_sensitive(city_last_command, FALSE);
    gtk_widget_set_sensitive(city_first_command, FALSE);
    gtk_widget_set_sensitive(city_next_command, FALSE);
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
  } else {
    gtk_widget_set_sensitive(city_change_command, can_client_issue_orders());
    gtk_widget_set_sensitive(city_last_command, can_client_issue_orders());
    gtk_widget_set_sensitive(city_first_command, can_client_issue_orders());
    gtk_widget_set_sensitive(city_next_command, can_client_issue_orders());
    gtk_widget_set_sensitive(city_center_command, TRUE);
    gtk_widget_set_sensitive(city_popup_command, TRUE);
    gtk_widget_set_sensitive(city_buy_command, can_client_issue_orders());
  }
}

/****************************************************************
 After a selection rectangle is defined, make the cities that
 are hilited on the canvas exclusively hilited in the
 City List window.
*****************************************************************/
void hilite_cities_from_canvas(void)
{
  ITree it;
  GtkTreeModel *model;

  if (!city_dialog_shell) return;

  model = GTK_TREE_MODEL(city_model);

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;
    gpointer res;

    itree_get(&it, 0, &res, -1);
    pcity = res;

    if (is_city_hilited(pcity))  {
      itree_select(city_selection, &it);
    }
  }
}

/****************************************************************
 Toggle a city's hilited status.
*****************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  ITree it;
  GtkTreeModel *model;

  if (!city_dialog_shell) return;

  model = GTK_TREE_MODEL(city_model);

  for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
    gint id;
    itree_get(&it, 1, &id, -1);

    if (id == pcity->id) {
      on_off ?
        itree_select(city_selection, &it):
        itree_unselect(city_selection, &it);
      break;
    }
  }
}
