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
#include <ctype.h>
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

/* get 'struct city *' functions: */
#define SPECVEC_TAG pcity
#define SPECVEC_TYPE struct city *
#include "specvec.h"

#define SPECVEC_TAG pcity
#define SPECVEC_TYPE struct city *
#include "specvec_c.h"

/******************************************************************/
static void create_city_report_dialog(bool make_modal);
static void city_model_init(void);
static void city_destroy_callback(GtkWidget *w, gpointer data);
static void city_center_callback(GtkWidget *w, gpointer data);
static void city_popup_callback(GtkWidget *w, gpointer data);
static void city_buy_callback(GtkWidget *w, gpointer data);
static void city_refresh_callback(GtkWidget *w, gpointer data);
static void city_change_all_dialog_callback(GtkWidget *w, gpointer data);
static void city_change_all_callback(GtkWidget *w, gpointer data);
static void city_selection_changed_callback(GtkTreeSelection *selection);

static void create_select_menu(GtkWidget *item);
static void create_change_menu(GtkWidget *item);

static GtkWidget *city_dialog_shell=NULL;

static GtkWidget *city_view;
static GtkTreeSelection *city_selection;
static GtkTreeStore *city_model;
static GType model_types[NUM_CREPORT_COLS+2];


#define POINTER_COLUMN	NUM_CREPORT_COLS+0
#define ID_COLUMN	NUM_CREPORT_COLS+1

static void popup_select_menu(GtkMenuShell *menu, gpointer data);
static void popup_change_menu(GtkMenuShell *menu, gpointer data);

static GtkWidget *city_center_command, *city_popup_command, *city_buy_command;
static GtkWidget *city_change_command;
static GtkWidget *city_change_all_command;
static GtkWidget *city_change_all_dialog_shell;
static GtkWidget *city_change_all_from_list;
static GtkWidget *city_change_all_to_list;

static GtkWidget *change_improvements_item;
static GtkWidget *change_units_item;
static GtkWidget *change_wonders_item;
static GtkWidget *change_cma_item;

static GtkWidget *select_island_item;

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
      city_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	gtk_widget_set_sensitive(top_vbox, FALSE);
      
      create_city_report_dialog(make_modal);
      gtk_set_relative_position(toplevel, city_dialog_shell, 10, 10);

      gtk_widget_show(city_dialog_shell);

      select_menu_cached = FALSE;
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
  gtk_widget_set_name(menu, "prodmenu");
  gtk_menu_item_set_submenu(parent_item, menu);

  if (change_prod) {
    struct pcity_vector selected;
    ITree it;
    int num_selected = 0;

    pcity_vector_init(&selected);
    pcity_vector_reserve(&selected, size);

    for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity, **pdest;
    
      if (!itree_is_selected(city_selection, &it))
    	continue;

      itree_get(&it, POINTER_COLUMN, &pcity, -1);

      pdest = pcity_vector_get(&selected, num_selected);
      *pdest = pcity;
      num_selected++;
    }

    assert(num_selected == size);

    cids_used = collect_cids1(cids, pcity_vector_get(&selected, 0),
        		      num_selected, append_units,
        		      append_wonders, change_prod, test_func);
    pcity_vector_free(&selected);
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
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(menu_item, "activate", callback,
		     GINT_TO_POINTER(items[item].cid));
  }
  gtk_widget_show_all(menu);

  if (cids_used == 0) {
    gtk_widget_set_sensitive(GTK_WIDGET(parent_item), FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void impr_or_unit_iterate(GtkTreeModel *model, GtkTreePath *path,
				 GtkTreeIter *it, gpointer data)
{
  struct packet_city_request packet;
  struct city *pcity;

  packet = *(struct packet_city_request *)data;
  gtk_tree_model_get(model, it, POINTER_COLUMN, &pcity, -1);

  packet.city_id = pcity->id;
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

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity;
      
      itree_get(&it, POINTER_COLUMN, &pcity, -1);

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

    gtk_tree_selection_selected_foreach(city_selection, impr_or_unit_iterate,
	&packet);
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

  gtk_tree_model_get(GTK_TREE_MODEL(city_model),it,POINTER_COLUMN,&pcity,-1);

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

    gtk_tree_selection_unselect_all(city_selection);
    for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pcity;
      int controlled;
      bool select;

      itree_get(&it, POINTER_COLUMN, &pcity, -1);
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
    gtk_tree_selection_selected_foreach(city_selection,
	cma_iterate, GINT_TO_POINTER(idx));
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
  gtk_widget_set_name(menu, "prodmenu");
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
	    cma_are_parameter_equal(&parameter,
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
static void create_city_report_dialog(bool make_modal)
{
  static char *titles [NUM_CREPORT_COLS];
  static char  buf    [NUM_CREPORT_COLS][64];
  struct city_report_spec *spec;

  GtkWidget *w, *sw, *menubar, *toolbar, *stock, *hbox, *vbox;
  int i;
  GtkAccelGroup *group = gtk_accel_group_new();
  
  city_dialog_shell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(city_dialog_shell), _("Cities"));
  gtk_window_add_accel_group(GTK_WINDOW(city_dialog_shell), group);
  g_signal_connect(city_dialog_shell, "destroy",
		   G_CALLBACK(city_destroy_callback), NULL);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(city_dialog_shell), vbox);

  gtk_window_set_title(GTK_WINDOW(city_dialog_shell),_("Cities"));

  /* menubar */
  menubar = create_city_report_menubar();
  gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);


  /* toolbar */
  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  toolbar = gtk_toolbar_new();
  gtk_box_pack_start(GTK_BOX(hbox), toolbar, FALSE, FALSE, 0);
  
  stock = gtk_image_new_from_stock(GTK_STOCK_ZOOM_FIT,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Center"), _("Center"), NULL, stock, NULL, NULL);
  g_signal_connect(w, "clicked", G_CALLBACK(city_center_callback), NULL);
  city_center_command = w;

  stock = gtk_image_new_from_stock(GTK_STOCK_ZOOM_IN,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Popup"), _("Popup"), NULL, stock, NULL, NULL);
  g_signal_connect(w, "clicked", G_CALLBACK(city_popup_callback), NULL);
  city_popup_command = w;

  gtk_toolbar_append_space(GTK_TOOLBAR(toolbar));

  stock = gtk_image_new_from_stock(GTK_STOCK_EXECUTE,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Buy"), _("Buy"), NULL, stock, NULL, NULL);
  g_signal_connect(w, "clicked", G_CALLBACK(city_buy_callback), NULL);
  city_buy_command = w;

  stock = gtk_image_new_from_stock(GTK_STOCK_CONVERT,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Change All"), _("Change All"), NULL, stock, NULL, NULL);
  g_signal_connect(w, "clicked", G_CALLBACK(city_change_all_callback), NULL);
  city_change_all_command = w;

  gtk_toolbar_append_space(GTK_TOOLBAR(toolbar));

  stock = gtk_image_new_from_stock(GTK_STOCK_REFRESH,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Refresh"), _("Refresh"), NULL, stock, NULL, NULL);
  g_signal_connect(w, "clicked", G_CALLBACK(city_refresh_callback), NULL);

  toolbar = gtk_toolbar_new();
  gtk_box_pack_end(GTK_BOX(hbox), toolbar, FALSE, FALSE, 0);
  
  stock = gtk_image_new_from_stock(GTK_STOCK_CLOSE,
				   GTK_ICON_SIZE_SMALL_TOOLBAR);
  w = gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
    _("Close"), _("Close"), NULL, stock, NULL, NULL);
  g_signal_connect_swapped(w, "clicked",
  	G_CALLBACK(gtk_widget_destroy), city_dialog_shell);

  gtk_widget_add_accelerator(w, "clicked", group, GDK_Escape, 0, 0);


  /* tree view */
  for (i=0; i<NUM_CREPORT_COLS; i++)
    titles[i] = buf[i];

  get_city_table_header(titles, sizeof(buf[0]));

  for (i=0; i<NUM_CREPORT_COLS; i++) {
    model_types[i] = G_TYPE_STRING;
  }
  model_types[POINTER_COLUMN] = G_TYPE_POINTER;
  model_types[ID_COLUMN] = G_TYPE_INT;

  city_model = gtk_tree_store_newv(NUM_CREPORT_COLS+2, model_types);
  city_model_init();

  city_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(city_model));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(city_view), TRUE);
  g_object_unref(city_model);
  city_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(city_view));
  gtk_tree_selection_set_mode(city_selection, GTK_SELECTION_MULTIPLE);
  g_signal_connect(city_selection, "changed",
	G_CALLBACK(city_selection_changed_callback), NULL);

  for (i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,
	"text", i, NULL);
    gtk_tree_view_column_set_visible(col, spec->show);
    gtk_tree_view_append_column(GTK_TREE_VIEW(city_view), col);
    gtk_tree_view_column_set_sort_column_id(col, i);
  }

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(sw, -1, 350);
  gtk_container_add(GTK_CONTAINER(sw), city_view);

  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

  gtk_widget_show_all(vbox);

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

  for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
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

  gtk_tree_selection_unselect_all(city_selection);

  for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;

    itree_get(&it, POINTER_COLUMN, &pcity, -1);

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

  gtk_tree_model_get(GTK_TREE_MODEL(city_model), iter,
		     POINTER_COLUMN, &selectedcity, -1);

  for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
    struct city *pcity;

    itree_get(&it, POINTER_COLUMN, &pcity, -1);

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
static void refresh_iterate(GtkTreeModel *model, GtkTreePath *path,
			    GtkTreeIter *it, gpointer data)
{
  struct city *pcity;
  struct packet_generic_integer packet;

  *(gboolean *)data = TRUE;
  gtk_tree_model_get(model, it, POINTER_COLUMN, &pcity, -1);

  packet.value = pcity->id;
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
Handle callbacks from the "change all" dialog.
*****************************************************************/
static void city_change_all_dialog_callback(GtkWidget *w, gpointer data)
{
  char *cmd = (char *)data;

  if (cmd) {
    GList *selection_from, *selection_to;
    gint row;
    int from, to;

    /* What are we changing to? */
    selection_to = GTK_CLIST(city_change_all_to_list)->selection;
    if (!selection_to) {
      append_output_window(_("Game: Select a unit or improvement"
			     " to change production to."));
      return;
    } else {
      row = GPOINTER_TO_INT(selection_to->data);
      to = GPOINTER_TO_INT(gtk_clist_get_row_data
			  (GTK_CLIST(city_change_all_to_list), row));
    }

    /* Iterate over the items we change from */
    selection_from = GTK_CLIST(city_change_all_from_list)->selection;
    for (; selection_from; selection_from = g_list_next(selection_from)) {
      row = GPOINTER_TO_INT(selection_from->data);
      from = GPOINTER_TO_INT(gtk_clist_get_row_data
			    (GTK_CLIST(city_change_all_from_list), row));
      if (from == to) {
	continue;
      }
      client_change_all(from, to);
    }
  }
  gtk_widget_destroy(city_change_all_dialog_shell);
  city_change_all_dialog_shell = NULL;
}

/****************************************************************
Change all cities building one type of thing to another thing.
This is a callback for the "change all" button.
*****************************************************************/
static void city_change_all_callback(GtkWidget * w, gpointer data)
{
  static gchar *title_[2][1] = { {N_("From:")},
				 {N_("To:")}
  };
  static gchar **title[2];
  int i, j;
  cid cids[B_LAST + U_LAST];
  int cids_used;
  cid selected_cid;
  struct item items[U_LAST + B_LAST];
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *scrollpane;

  if (!title[0]) {
    title[0] = intl_slist(1, title_[0]);
    title[1] = intl_slist(1, title_[1]);
  }

  if (!city_change_all_dialog_shell) {
    city_change_all_dialog_shell = gtk_dialog_new();
    gtk_set_relative_position(city_dialog_shell,
			      city_change_all_dialog_shell, 10, 10);

    gtk_signal_connect(GTK_OBJECT(city_change_all_dialog_shell),
		       "delete_event",
		       GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
		       NULL);

    gtk_window_set_title(GTK_WINDOW(city_change_all_dialog_shell),
			 _("Change Production Everywhere"));

    box = gtk_hbox_new(FALSE, 10);

    /* make a list of everything we're currently building */
    city_change_all_from_list = gtk_clist_new_with_titles(1, title[0]);
    gtk_clist_column_titles_passive(GTK_CLIST(city_change_all_from_list));
    gtk_clist_set_selection_mode(GTK_CLIST(city_change_all_from_list),
				 GTK_SELECTION_EXTENDED);

    scrollpane = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrollpane),
		      city_change_all_from_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollpane),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_usize(city_change_all_from_list, -2, 300);

    /* if a city was selected when "change all" was clicked on,
       hilight that item so user doesn't have to click it */
    selected_cid = -1;
    /*
    if ((selection = GTK_CLIST(city_list)->selection)) {
      row = GPOINTER_TO_INT(selection->data);
      if ((pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), row))) {
	selected_cid = cid_encode_from_city(pcity);
      }
    }*/

    cids_used = collect_cids2(cids);
    name_and_sort_items(cids, cids_used, items, FALSE, NULL);

    for (i = 0; i < cids_used; i++) {
      char *buf[1];

      buf[0] = items[i].descr;

      j = gtk_clist_append(GTK_CLIST(city_change_all_from_list), buf);
      gtk_clist_set_row_data(GTK_CLIST(city_change_all_from_list),
			     j, GINT_TO_POINTER(items[i].cid));
      if (selected_cid == items[i].cid) {
	gtk_clist_select_row(GTK_CLIST(city_change_all_from_list), j, 0);
      }
    }

    gtk_box_pack_start(GTK_BOX(box), scrollpane, TRUE, TRUE, 10);

    gtk_widget_show(scrollpane);
    gtk_widget_show(city_change_all_from_list);

    /* 1.5 of optimal width is necessary for unknown reasons */
    gtk_clist_set_column_width(GTK_CLIST(city_change_all_from_list), 0,
			       (3*gtk_clist_optimal_column_width(GTK_CLIST
							      (city_change_all_from_list),
							      0))/2);


    /* make a list of everything we could build */
    city_change_all_to_list = gtk_clist_new_with_titles(1, title[1]);
    gtk_clist_column_titles_passive(GTK_CLIST(city_change_all_to_list));
    gtk_clist_set_selection_mode(GTK_CLIST(city_change_all_to_list),
				 GTK_SELECTION_SINGLE);
    gtk_clist_set_column_auto_resize(GTK_CLIST(city_change_all_to_list), 0,
				     1);

    scrollpane = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrollpane), city_change_all_to_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollpane),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_usize(city_change_all_to_list, -2, 300);

    cids_used = collect_cids3(cids);
    name_and_sort_items(cids, cids_used, items, TRUE, NULL);

    for (i = 0; i < cids_used; i++) {
      char *buf[1];

      buf[0] = items[i].descr;

      j = gtk_clist_append(GTK_CLIST(city_change_all_to_list), buf);
      gtk_clist_set_row_data(GTK_CLIST(city_change_all_to_list), j,
			     GINT_TO_POINTER(items[i].cid));
    }

    gtk_box_pack_start(GTK_BOX(box), scrollpane, TRUE, TRUE, 10);
    gtk_widget_show(scrollpane);

    /* 1.5 of optimal width is necessary for unknown reasons */
    gtk_clist_set_column_width(GTK_CLIST(city_change_all_to_list), 0,
			       (3*gtk_clist_optimal_column_width(GTK_CLIST
							      (city_change_all_to_list),
							      0))/2);

    gtk_box_pack_start(GTK_BOX
		       (GTK_DIALOG(city_change_all_dialog_shell)->vbox),
		       box, TRUE, TRUE, 0);
    gtk_widget_show(city_change_all_to_list);

    gtk_widget_show(box);

    button = gtk_button_new_with_label(_("Change"));
    gtk_box_pack_start(GTK_BOX
		       (GTK_DIALOG(city_change_all_dialog_shell)->
			action_area), button, TRUE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
		       "change");
    gtk_widget_show(button);

    button = gtk_button_new_with_label(_("Cancel"));
    gtk_box_pack_start(GTK_BOX
		       (GTK_DIALOG(city_change_all_dialog_shell)->
			action_area), button, TRUE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
		       NULL);
    gtk_widget_show(button);

    gtk_widget_show(city_change_all_dialog_shell);
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
  char *name;
  char buf[512];

  gtk_tree_model_get(model, it, POINTER_COLUMN, &pcity, -1);

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

  gtk_tree_model_get(model, it, POINTER_COLUMN, &pcity, -1);
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

  gtk_tree_model_get(model, it, POINTER_COLUMN, &pcity, -1);

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
static void city_report_list_callback(GtkWidget *w, gint row, gint col,
				      GdkEvent *ev, gpointer data)
{
  /* Pops up a city when it's double-clicked */
  if(ev && ev->type==GDK_2BUTTON_PRESS)
    city_popup_callback(w, data);
}

/****************************************************************
...
*****************************************************************/
static void update_row(ITree *row, struct city *pcity)
{
  struct city_report_spec *spec;
  char buf [64];
  int i;

  for (i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    my_snprintf(buf, sizeof(buf), "%*s",
  		NEG_VAL(spec->width), (spec->func)(pcity));
    itree_set(row, i, buf, -1);
  }
  itree_set(row, POINTER_COLUMN, pcity, ID_COLUMN, pcity->id, -1);
}

/****************************************************************
 Optimized version of city_report_dialog_update() for 1st popup.
*****************************************************************/
static void city_model_init(void)
{
  if (city_dialog_shell && !delay_report_update) {

    city_list_iterate(game.player_ptr->cities, pcity) {
      ITree it;

      tstore_append(city_model, &it, NULL);
      update_row(&it, pcity);
    } city_list_iterate_end;
  }
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if (city_dialog_shell && !delay_report_update) {
    ITree it, it_next;

    /* remove cities which were taken from us */
    for (itree_begin(city_model, &it); !itree_end(&it); it=it_next) {
      struct city *pcity;
      gint id;

      it_next = it;
      itree_next(&it_next);

      itree_get(&it, POINTER_COLUMN, &pcity, ID_COLUMN, &id, -1);

      if (find_city_by_id(id)!=pcity || city_owner(pcity)!=game.player_ptr) {
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
  if (city_dialog_shell && !delay_report_update) {
    ITree it;
    bool found;

    /* search for pcity in the current store */
    found = FALSE;
    for (itree_begin(city_model, &it); !itree_end(&it); itree_next(&it)) {
      struct city *pfound;

      itree_get(&it, POINTER_COLUMN, &pfound, -1);

      if (pfound == pcity) {
	found = TRUE;
	break;
      }
    }

    /* if pcity is not in the store, create an entry for it */
    if (!found) {
      tstore_append(city_model, &it, NULL);
    }

    update_row(&it, pcity);

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
static void popup_select_menu(GtkMenuShell *menu, gpointer data)
{
  int n;

  if (select_menu_cached)
    return;

  n = 0;
  gtk_tree_selection_selected_foreach(city_selection, selected_iterate, &n);
  gtk_widget_set_sensitive(select_island_item, (n > 0));

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
