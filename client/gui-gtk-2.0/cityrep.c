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
#include <stdarg.h>
#include <assert.h>

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
#include "citydlg.h"
#include "cityrepdata.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "mapview_common.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "climisc.h"

#include "cityrep.h"
#include "cma_fec.h"

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))
#define CMA_NONE	(-1)
#define CMA_CUSTOM	(-2)

/******************************************************************/
static void create_city_report_dialog(bool make_modal);
static void city_model_init(void);
static void city_destroy_callback(GtkWidget *w, gpointer data);
static void city_center_callback(GtkWidget *w, gpointer data);
static void city_popup_callback(GtkWidget *w, gpointer data);
static void city_buy_callback(GtkWidget *w, gpointer data);
static void city_refresh_callback(GtkWidget *w, gpointer data);
static void city_selection_changed_callback(GtkTreeSelection *selection);

static void create_select_menu(GtkWidget *item);
static void create_change_menu(GtkWidget *item);

static GtkWidget *city_dialog_shell=NULL;

static GtkWidget *city_view;
static GtkTreeSelection *city_selection;
static GtkListStore *city_model;

static void popup_select_menu(GtkMenuShell *menu, gpointer data);
static void popup_change_menu(GtkMenuShell *menu, gpointer data);

static GtkWidget *city_center_command, *city_popup_command, *city_buy_command;
static GtkWidget *city_change_command;

static GtkWidget *change_improvements_item;
static GtkWidget *change_units_item;
static GtkWidget *change_wonders_item;
static GtkWidget *change_cma_item;

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
	    NEG_VAL(spec->width), spec->title1 ? _(spec->title1) : "",
	    NEG_VAL(spec->width), spec->title2 ? _(spec->title2) : "");
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
    gtk_set_relative_position(toplevel, city_dialog_shell, 10, 10);

    select_menu_cached = FALSE;
  }

  gtk_window_present(GTK_WINDOW(city_dialog_shell));
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
					     bool change_prod,
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

  gtk_menu_item_remove_submenu(parent_item);
  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(parent_item, menu);
  gtk_widget_set_name(menu, "Freeciv");

  if (change_prod) {
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
        		      append_wonders, change_prod, test_func);
    g_free(data);
  } else {
    cids_used = collect_cids1(cids, NULL, 0, append_units,
			      append_wonders, change_prod, test_func);
  }

  name_and_sort_items(cids, cids_used, items, change_prod, NULL);

  for (i = 0; i < 4; i++) {
    row[i] = buf[i];
  }

  g_object_set_data(G_OBJECT(menu), "freeciv_test_func", test_func);
  g_object_set_data(G_OBJECT(menu), "freeciv_change_prod",
		    GINT_TO_POINTER(change_prod));

  for (item = 0; item < cids_used; item++) {
    cid cid = items[item].cid;
    GtkWidget *menu_item;
    char txt[64];

    get_city_dialog_production_row(row, sizeof(buf[0]), cid_id(cid),
				   cid_is_unit(cid), NULL);
    my_snprintf(txt, sizeof(txt), "%-30s %-12s %4s", row[0], row[1], row[2]);

    menu_item = gtk_menu_item_new_with_label(txt);
    gtk_widget_set_name(menu_item, "monomenu");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(menu_item, "activate", callback,
		     GINT_TO_POINTER(items[item].cid));
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
  struct packet_city_request packet;
  gint id;

  packet = *(struct packet_city_request *)data;
  gtk_tree_model_get(model, it, 1, &id, -1);

  packet.city_id = id;
  send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
}

/****************************************************************
...
*****************************************************************/
static void select_impr_or_unit_callback(GtkWidget *w, gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  GObject *parent = G_OBJECT(w->parent);
  TestCityFunc test_func = g_object_get_data(parent, "freeciv_test_func");
  bool change_prod = 
    GPOINTER_TO_INT(g_object_get_data(parent, "freeciv_change_prod"));

  /* If this is not the change production button */
  if (!change_prod) {
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
    bool is_unit = cid_is_unit(cid);
    int id = cid_id(cid);
    struct packet_city_request packet;

    packet.build_id = id;
    packet.is_build_id_unit_id = is_unit;

    connection_do_buffer(&aconnection);
    gtk_tree_selection_selected_foreach(city_selection, impr_or_unit_iterate,
	&packet);
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
  struct cma_parameter parameter;

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
        	 cma_are_parameter_equal(&parameter,
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
  struct cma_parameter parameter;
  GtkWidget *w;

  gtk_menu_item_remove_submenu(parent_item);
  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(parent_item, menu);
  gtk_widget_set_name(menu, "Freeciv");

  if (change_cma) {
    for (i = -1; i < cmafec_preset_num(); i++) {
      w = (i == -1 ? gtk_menu_item_new_with_label(_("none"))
	   : gtk_menu_item_new_with_label(cmafec_preset_get_descr(i)));
      gtk_widget_set_name(w, "monomenu");
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
      gtk_widget_set_name(w, "monomenu");
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
      gtk_widget_set_name(w, "monomenu");

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
      g_signal_connect(w, "activate",
	G_CALLBACK(select_cma_callback), GINT_TO_POINTER(CMA_CUSTOM));
    }

    /* only fill in presets that are being used. */
    for (i = 0; i < cmafec_preset_num(); i++) {
      found = 0;
      city_list_iterate(game.player_ptr->cities, pcity) {
	if (cma_is_city_under_agent(pcity, &parameter) &&
	    cma_are_parameter_equal(&parameter,
				    cmafec_preset_get_parameter(i))) {
	  found = 1;
	  break;
	}
      } city_list_iterate_end;
      if (found) {
	w = gtk_menu_item_new_with_label(cmafec_preset_get_descr(i));
        gtk_widget_set_name(w, "monomenu");

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
    item = gtk_check_menu_item_new_with_label(_(spec->explanation));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), spec->show);
    g_signal_connect(item, "toggled", G_CALLBACK(toggle_view), (gpointer)spec);
  }
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(show_item), menu);
}

/****************************************************************
...
*****************************************************************/
static GtkWidget *create_city_report_menubar()
{
  GtkWidget *menubar, *item;

  menubar = gtk_menu_bar_new();
  
  item = gtk_menu_item_new_with_mnemonic(_("_Change..."));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  city_change_command = item;
  create_change_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("_Select..."));
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
  create_select_menu(item);

  item = gtk_menu_item_new_with_mnemonic(_("S_how..."));
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
gint cityrep_sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
		       gpointer data)
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

  return strcmp(buf1, buf2);
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
  
  city_dialog_shell = gtk_dialog_new_with_buttons(_("Cities"),
  	NULL,
	0,
	GTK_STOCK_CLOSE,
	GTK_RESPONSE_CLOSE,
	NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(city_dialog_shell),
	GTK_RESPONSE_CLOSE);

  if (make_modal) {
    gtk_window_set_transient_for(GTK_WINDOW(city_dialog_shell),
				 GTK_WINDOW(toplevel));
    gtk_window_set_modal(GTK_WINDOW(city_dialog_shell), TRUE);
  }

  g_signal_connect(city_dialog_shell, "response",
		   G_CALLBACK(gtk_widget_destroy), NULL);
  g_signal_connect(city_dialog_shell, "destroy",
		   G_CALLBACK(city_destroy_callback), NULL);

  /* menubar */
  menubar = create_city_report_menubar();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(city_dialog_shell)->vbox),
	menubar, FALSE, FALSE, 0);

  /* buttons */
  w = gtk_stockbutton_new(GTK_STOCK_ZOOM_FIT, _("Cen_ter"));
  gtk_box_pack_end(GTK_BOX(GTK_DIALOG(city_dialog_shell)->action_area),
	w, FALSE, TRUE, 0);
  g_signal_connect(w, "clicked", G_CALLBACK(city_center_callback), NULL);
  city_center_command = w;

  w = gtk_stockbutton_new(GTK_STOCK_ZOOM_IN, _("_Popup"));
  gtk_box_pack_end(GTK_BOX(GTK_DIALOG(city_dialog_shell)->action_area),
	w, FALSE, TRUE, 0);
  g_signal_connect(w, "clicked", G_CALLBACK(city_popup_callback), NULL);
  city_popup_command = w;

  w = gtk_stockbutton_new(GTK_STOCK_EXECUTE, _("_Buy"));
  gtk_box_pack_end(GTK_BOX(GTK_DIALOG(city_dialog_shell)->action_area),
	w, FALSE, TRUE, 0);
  g_signal_connect(w, "clicked", G_CALLBACK(city_buy_callback), NULL);
  city_buy_command = w;

  w = gtk_stockbutton_new(GTK_STOCK_REFRESH, _("_Refresh"));
  gtk_box_pack_end(GTK_BOX(GTK_DIALOG(city_dialog_shell)->action_area),
	w, FALSE, TRUE, 0);
  g_signal_connect(w, "clicked", G_CALLBACK(city_refresh_callback), NULL);

  /* tree view */
  for (i=0; i<NUM_CREPORT_COLS; i++)
    titles[i] = buf[i];

  get_city_table_header(titles, sizeof(buf[0]));

  city_model = gtk_list_store_new(2, G_TYPE_POINTER, G_TYPE_INT);

  city_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(city_model));
  g_object_unref(city_model);
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
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(sw, -1, 350);
  gtk_container_add(GTK_CONTAINER(sw), city_view);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(city_dialog_shell)->vbox),
	sw, TRUE, TRUE, 0);

  gtk_widget_show_all(GTK_DIALOG(city_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(city_dialog_shell)->action_area);
  city_model_init();

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

    if (is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN)) {
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

    if (map_get_continent(pcity->x, pcity->y)
    	    == map_get_continent(selectedcity->x, selectedcity->y)) {
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
static void refresh_iterate(GtkTreeModel *model, GtkTreePath *path,
			    GtkTreeIter *it, gpointer data)
{
  struct packet_generic_integer packet;
  gint id;

  *(gboolean *)data = TRUE;
  gtk_tree_model_get(model, it, 1, &id, -1);

  packet.value = id;
  send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
}

/****************************************************************
...
*****************************************************************/
static void city_refresh_callback(GtkWidget *w, gpointer data)

{ /* added by Syela - I find this very useful */
  struct packet_generic_integer packet;
  gboolean found;

  found = FALSE;
  gtk_tree_selection_selected_foreach(city_selection, refresh_iterate, &found);

  if (!found) {
    packet.value = 0;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
  }
}

/****************************************************************
...
*****************************************************************/
static void buy_iterate(GtkTreeModel *model, GtkTreePath *path,
			GtkTreeIter *it, gpointer data)
{
  struct city *pcity;
  int value;
  const char *name;
  char buf[512];
  gpointer res;

  gtk_tree_model_get(model, it, 0, &res, -1);
  pcity = res;

  value = city_buy_cost(pcity);	 
  if (pcity->is_building_unit)
    name = get_unit_type(pcity->currently_building)->name;
  else
    name = get_impr_name_ex(pcity, pcity->currently_building);

  if (game.player_ptr->economic.gold >= value) {
      struct packet_city_request packet;

      packet.city_id = pcity->id;
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);
  } else {
    my_snprintf(buf, sizeof(buf),
        	_("Game: %s costs %d gold and you only have %d gold."),
        	name,value, game.player_ptr->economic.gold);
    append_output_window(buf);
  }
}

/****************************************************************
...
*****************************************************************/
static void city_buy_callback(GtkWidget *w, gpointer data)
{
  gtk_tree_selection_selected_foreach(city_selection, buy_iterate, NULL);
}

/****************************************************************
...
*****************************************************************/
static void city_destroy_callback(GtkWidget *w, gpointer data)
{
  city_dialog_shell = NULL;
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
  center_tile_mapcanvas(pcity->x, pcity->y);
}

/****************************************************************
...
*****************************************************************/
static void city_center_callback(GtkWidget *w, gpointer data)
{
  gtk_tree_selection_selected_foreach(city_selection, center_iterate, NULL);
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
    center_tile_mapcanvas(pcity->x, pcity->y);
  }

  popup_city_dialog(pcity, 0);
}

/****************************************************************
...
*****************************************************************/
static void city_popup_callback(GtkWidget *w, gpointer data)
{
  gtk_tree_selection_selected_foreach(city_selection, popup_iterate, NULL);
}

/****************************************************************
...
*****************************************************************/
static void update_row(GtkTreeIter *row, struct city *pcity, bool init)
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

  if (!init) {
    GtkTreePath *path;

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(city_model), row);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(city_model), path, row);
    gtk_tree_path_free(path);
  }
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
      update_row(&it, pcity, TRUE);
    } city_list_iterate_end;
  }
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if (city_dialog_shell && !is_report_dialogs_frozen()) {
    ITree it, it_next;
    GtkTreeModel *model = GTK_TREE_MODEL(city_model);

    /* remove cities which were taken from us */
    for (itree_begin(model, &it); !itree_end(&it); it=it_next) {
      struct city *pcity;
      gint id;

      it_next = it;
      itree_next(&it_next);

      itree_get(&it, 1, &id, -1);

      if (!(pcity=find_city_by_id(id)) || city_owner(pcity)!=game.player_ptr) {
        tstore_remove(&it);
      }
    }

    /* update */
    city_list_iterate(game.player_ptr->cities, pcity) {
      city_report_dialog_update_city(pcity);
    } city_list_iterate_end;

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

    /* search for pcity in the current store */
    found = FALSE;
    for (itree_begin(model, &it); !itree_end(&it); itree_next(&it)) {
      gint id;

      itree_get(&it, 1, &id, -1);

      if (id == pcity->id) {
	found = TRUE;
	break;
      }
    }

    /* if pcity is not in the store, create an entry for it */
    if (!found) {
      gtk_list_store_append(city_model, TREE_ITER_PTR(it));
    }

    update_row(TREE_ITER_PTR(it), pcity, FALSE);

    select_menu_cached = FALSE;
  }
}

/****************************************************************
...
*****************************************************************/
static void selected_iterate(GtkTreeModel *model, GtkTreePath *path,
			     GtkTreeIter *it, gpointer data)
{
  ++(*(int *)data);
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
...
*****************************************************************/
static void popup_change_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  n = 0;
  gtk_tree_selection_selected_foreach(city_selection, selected_iterate, &n);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_improvements_item),
				  FALSE, FALSE, TRUE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_units_item),
				  TRUE, FALSE, TRUE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(change_wonders_item),
				  FALSE, TRUE, TRUE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), n);
  append_cma_to_menu_item(GTK_MENU_ITEM(change_cma_item), TRUE);
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
bool city_building_impr_or_unit(struct city *pcity, cid cid)
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

  n = 0;
  gtk_tree_selection_selected_foreach(city_selection, selected_iterate, &n);
  gtk_widget_set_sensitive(select_island_item, (n > 0));

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bunit_item),
				  TRUE, FALSE, FALSE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bimprovement_item),
				  FALSE, FALSE, FALSE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_bwonder_item),
				  FALSE, TRUE, FALSE,
				  city_building_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_supported_item),
				  TRUE, FALSE, FALSE,
				  city_unit_supported,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_present_item),
				  TRUE, FALSE, FALSE,
				  city_unit_present,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_built_improvements_item),
				  FALSE, FALSE, FALSE,
				  city_got_building,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_built_wonders_item),
				  FALSE, TRUE, FALSE,
				  city_got_building,
				  G_CALLBACK(select_impr_or_unit_callback), -1);

  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_improvements_item),
				  FALSE, FALSE, FALSE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_units_item),
				  TRUE, FALSE, FALSE,
				  city_can_build_impr_or_unit,
				  G_CALLBACK(select_impr_or_unit_callback), -1);
  append_impr_or_unit_to_menu_item(GTK_MENU_ITEM(select_wonders_item),
				  FALSE, TRUE, FALSE,
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

  n = 0;
  gtk_tree_selection_selected_foreach(selection, selected_iterate, &n);

  if (n == 0) {
    gtk_widget_set_sensitive(city_change_command, FALSE);
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
  } else {
    gtk_widget_set_sensitive(city_change_command, TRUE);
    gtk_widget_set_sensitive(city_center_command, TRUE);
    gtk_widget_set_sensitive(city_popup_command, TRUE);
    gtk_widget_set_sensitive(city_buy_command, TRUE);
  }
}
