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

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "mem.h"
#include "packets.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "cityrepdata.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "cma_fec.h"
#include "options.h"
#include "packhand.h"

#include "chatline.h"
#include "citydlg.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "mapctrl.h"    /* is_city_hilited() */
#include "optiondlg.h"
#include "repodlgs.h"

#include "cityrep.h"

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))
#define CMA_NONE	(-1)
#define CMA_CUSTOM	(-2)

/******************************************************************/
static GtkWidget *config_shell;
static GtkWidget *config_toggle[NUM_CREPORT_COLS];

static void create_city_report_config_dialog(void);
static void popup_city_report_config_dialog(void);
static void config_ok_command_callback(GtkWidget *w, gpointer data);

/******************************************************************/
static void create_city_report_dialog(bool make_modal);
static void city_close_callback(GtkWidget *w, gpointer data);
static void city_center_callback(GtkWidget *w, gpointer data);
static void city_popup_callback(GtkWidget *w, gpointer data);
static void city_buy_callback(GtkWidget *w, gpointer data);
static void city_change_all_dialog_callback(GtkWidget *w, gpointer data);
static void city_change_all_callback(GtkWidget *w, gpointer data);
static void city_list_callback(GtkWidget *w, gint row, gint column);
static void city_config_callback(GtkWidget *w, gpointer data);
static gboolean city_change_callback(GtkWidget *w, GdkEvent *event, gpointer data);
static gboolean city_select_callback(GtkWidget *w, GdkEvent *event, gpointer data);
static void city_report_list_callback(GtkWidget *w, gint row, gint col, GdkEvent *ev, gpointer data);

static GtkWidget *city_dialog_shell=NULL;
static GtkWidget *city_list;
static GtkWidget *city_center_command, *city_popup_command, *city_buy_command,
                 *city_config_command;
static GtkWidget *city_change_command;
static GtkWidget *city_change_all_dialog_shell;
static GtkWidget *city_change_all_from_list;
static GtkWidget *city_change_all_to_list;
static GtkWidget *city_select_command;

static int city_dialog_shell_is_modal;

/*******************************************************************
 Replacement for the Gtk+ standard compare function for CLISTs,
 to enable correct sorting of numbers included in text strings.
*******************************************************************/
static gint report_sort(GtkCList *report,
			gconstpointer ptr1, gconstpointer ptr2)
{
  const GtkCListRow *row1 = ptr1;
  const GtkCListRow *row2 = ptr2;
  const char *buf1, *buf2;

  /* Retrieve the text of the fields... */
  buf1 = GTK_CELL_TEXT(row1->cell[report->sort_column])->text;
  buf2 = GTK_CELL_TEXT(row2->cell[report->sort_column])->text;

  /* ...and perform a comparison. */
  return cityrepfield_compare(buf1, buf2);
}

/****************************************************************
 Sort cities by column...
*****************************************************************/
static void sort_cities_callback( GtkButton *button, gpointer *data )
{
  int sort_column = GPOINTER_TO_INT( data );

  if ( sort_column == GTK_CLIST( city_list )->sort_column )
  {
    if ( GTK_CLIST( city_list )->sort_type == GTK_SORT_ASCENDING )
      gtk_clist_set_sort_type( GTK_CLIST( city_list ), GTK_SORT_DESCENDING );
    else
      gtk_clist_set_sort_type( GTK_CLIST( city_list ), GTK_SORT_ASCENDING );
    gtk_clist_sort( GTK_CLIST( city_list ) );
  }
  else
  {
    gtk_clist_set_sort_column( GTK_CLIST( city_list ), sort_column );
    gtk_clist_sort( GTK_CLIST( city_list ) );
  }
}

/****************************************************************
 Create the text for a line in the city report
*****************************************************************/
static void get_city_text(struct city *pcity, char *buf[], int n)
{
  struct city_report_spec *spec;
  int i;

  for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    buf[i][0]='\0';
    if(!spec->show) continue;

    my_snprintf(buf[i], n, "%*s", NEG_VAL(spec->width), (spec->func)(pcity));
  }
}

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
  if (!city_dialog_shell) {
    city_dialog_shell_is_modal = make_modal;

    if (make_modal) {
      gtk_widget_set_sensitive(top_vbox, FALSE);
    }

    create_city_report_dialog(make_modal);
    gtk_set_relative_position(toplevel, city_dialog_shell, 10, 10);
  }
  gtk_window_show(GTK_WINDOW(city_dialog_shell));
  hilite_cities_from_canvas();
}

/****************************************************************
 Closes the city report dialog.
****************************************************************/
void popdown_city_report_dialog(void)
{
  if (city_dialog_shell) {
    if (city_dialog_shell_is_modal) {
      gtk_widget_set_sensitive(top_vbox, TRUE);
    }
    gtk_window_hide(GTK_WINDOW(city_dialog_shell));
  }
}

/****************************************************************
...
*****************************************************************/
static struct city* 
city_from_glist(GList* list)
{
  struct city* retval;

  g_assert (list);
  
  retval = gtk_clist_get_row_data(GTK_CLIST(city_list),
				  GPOINTER_TO_INT(list->data));

  g_assert (retval);

  return retval;
}

/****************************************************************
...
*****************************************************************/
typedef bool (*TestCityFunc)(struct city *, gint);

/****************************************************************
...
*****************************************************************/
static void append_impr_or_unit_to_menu_sub(GtkWidget * menu,
					    gchar * nothing_appended_text,
					    bool append_units,
					    bool append_wonders,
					    gboolean change_prod,
					    TestCityFunc test_func,
					    GtkSignalFunc callback)
{
  cid cids[U_LAST + B_LAST];
  struct item items[U_LAST + B_LAST];
  int item, cids_used, num_selected_cities = 0;
  struct city **selected_cities = NULL;

  if (change_prod) {
    GList *selection = GTK_CLIST(city_list)->selection;
    int i;

    g_assert(selection);
    
    num_selected_cities = g_list_length(selection);
    selected_cities =
	fc_malloc(sizeof(*selected_cities) * num_selected_cities);

    for (i = 0; i < num_selected_cities; i++) {
      selected_cities[i] = city_from_glist(selection);
      selection = g_list_next(selection);
    }
  }

  cids_used = collect_cids1(cids, selected_cities,
			    num_selected_cities, append_units,
			    append_wonders, change_prod, test_func);
  if (selected_cities) {
    free(selected_cities);
  }
  name_and_sort_items(cids, cids_used, items, change_prod, NULL);

  for (item = 0; item < cids_used; item++) {
    GtkWidget *w = gtk_menu_item_new_with_label(items[item].descr);

    gtk_menu_append(GTK_MENU(menu), w);

    gtk_signal_connect(GTK_OBJECT(w), "activate", callback,
		       GINT_TO_POINTER(items[item].cid));
  }

  if (cids_used == 0) {
    GtkWidget *w = gtk_menu_item_new_with_label(nothing_appended_text);
    gtk_widget_set_sensitive(w, FALSE);
    gtk_menu_append(GTK_MENU(menu), w);
  }
}

/****************************************************************
...
*****************************************************************/
static void select_impr_or_unit_callback(GtkWidget *w, gpointer data)
{
  cid cid = GPOINTER_TO_INT(data);
  gint i;
  GtkObject *parent = GTK_OBJECT(w->parent);
  TestCityFunc test_func =
      (TestCityFunc) gtk_object_get_data(parent, "freeciv_test_func");
  bool change_prod = 
    GPOINTER_TO_INT(gtk_object_get_data(parent, "freeciv_change_prod"));

  /* If this is not the change production button */
  if(!change_prod)
    {
      gtk_clist_unselect_all( GTK_CLIST(city_list));

      for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
	{
	  struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list),i);
	  if (test_func(pcity, cid))
	    gtk_clist_select_row(GTK_CLIST(city_list),i,0);
	}
    }
  else
    {
      bool is_unit = cid_is_unit(cid);
      int last_request_id = 0, id = cid_id(cid);
      GList* selection = GTK_CLIST(city_list)->selection;

      g_assert(selection);

      connection_do_buffer(&aconnection);  
      for (; selection; selection = g_list_next(selection)) {
	last_request_id = city_change_production(city_from_glist(selection),
						 is_unit, id);
      }

      connection_do_unbuffer(&aconnection);
      reports_freeze_till(last_request_id);
    }
}

/****************************************************************
...
*****************************************************************/
static void append_impr_or_unit_to_menu(GtkWidget * menu, char *label,
					bool change_prod,
					bool append_improvements,
					bool append_units,
					bool append_wonders,
					TestCityFunc test_func,
					bool needs_a_selected_city)
{
  GtkWidget *item = gtk_menu_item_new_with_label(label);
  GtkWidget *submenu = gtk_menu_new();

  gtk_menu_append(GTK_MENU(menu), item);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

  if (needs_a_selected_city && !GTK_CLIST(city_list)->selection) {
    gtk_widget_set_sensitive(item, FALSE);
    return;
  }

  if (append_improvements) {
    /* Add all buildings */
    append_impr_or_unit_to_menu_sub(submenu, _("No Buildings Available"),
				    FALSE, FALSE, change_prod,
				    (bool(*)(struct city *, gint))
				    test_func, select_impr_or_unit_callback);
    /* Add a separator */
    if (append_units || append_wonders) {
      gtk_menu_append(GTK_MENU(submenu), gtk_menu_item_new());
    }
  }

  if (append_units) {
    /* Add all units */
    append_impr_or_unit_to_menu_sub(submenu, _("No Units Available"),
				    TRUE, FALSE, change_prod,
				    test_func, select_impr_or_unit_callback);
  }

  if (append_wonders) {
    /* Add a separator */
    if (append_units) {
      gtk_menu_append(GTK_MENU(submenu), gtk_menu_item_new());
    }

    /* Add all wonders */
    append_impr_or_unit_to_menu_sub(submenu, _("No Wonders Available"),
				    FALSE, TRUE, change_prod,
				    (bool(*)(struct city *, gint))
				    test_func, select_impr_or_unit_callback);
  }

  gtk_object_set_data(GTK_OBJECT(submenu), "freeciv_test_func", test_func);
  gtk_object_set_data(GTK_OBJECT(submenu), "freeciv_change_prod",
		      GINT_TO_POINTER(change_prod));
}

/****************************************************************
 Called when one clicks on an CMA item to make a selection or to
 change a selection's preset.
*****************************************************************/
static void select_cma_callback(GtkWidget * w, gpointer data)
{
  int i, index = GPOINTER_TO_INT(data);
  GtkObject *parent = GTK_OBJECT(w->parent);
  bool change_cma =
      GPOINTER_TO_INT(gtk_object_get_data(parent, "freeciv_change_cma"));
  struct cm_parameter parameter;

  /* If this is not the change button but the select cities button. */
  if (!change_cma) {
    gtk_clist_unselect_all(GTK_CLIST(city_list));

    for (i = 0; i < GTK_CLIST(city_list)->rows; i++) {
      struct city *pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), i);
      int controlled = cma_is_city_under_agent(pcity, &parameter);
      int select = 0;

      if (index == CMA_CUSTOM && controlled
	  && cmafec_preset_get_index_of_parameter(&parameter) == -1) {
	select = 1;
      } else if (index == CMA_NONE && !controlled) {
	select = 1;
      } else if (index >= 0 && controlled &&
		 cm_are_parameter_equal(&parameter,
					 cmafec_preset_get_parameter(index))) {
	select = 1;
      }
      if (select) {
	gtk_clist_select_row(GTK_CLIST(city_list), i, 0);
      }
    }
  } else {
    GList *selection = GTK_CLIST(city_list)->selection;
    GList *copy = NULL;

    g_assert(selection);

    /* must copy the list as refresh_city_dialog() corrupts the selection */
    for (; selection; selection = g_list_next(selection)) {
      copy = g_list_append(copy, city_from_glist(selection));
    }

    reports_freeze();

    for (; copy; copy = g_list_next(copy)) {
      struct city *pcity = copy->data;

      if (index == CMA_NONE) {
	cma_release_city(pcity);
      } else {
	cma_put_city_under_agent(pcity, cmafec_preset_get_parameter(index));
      }
      refresh_city_dialog(pcity);
    }
    reports_thaw();

    g_list_free(copy);
  }
}

/****************************************************************
 Create the cma entries in the change menu and the select menu. The
 indices CMA_NONE (aka -1) and CMA_CUSTOM (aka -2) are
 special. CMA_NONE signifies a preset of "none" and CMA_CUSTOM a
 "custom" preset.
*****************************************************************/
static void append_cma_to_menu(GtkWidget * menu, bool change_cma)
{
  int i;
  struct cm_parameter parameter;
  GtkWidget *w;

  if (change_cma) {
    for (i = -1; i < cmafec_preset_num(); i++) {
      w = (i == -1 ? gtk_menu_item_new_with_label(_("none"))
	   : gtk_menu_item_new_with_label(cmafec_preset_get_descr(i)));
      gtk_menu_append(GTK_MENU(menu), w);
      gtk_signal_connect(GTK_OBJECT(w), "activate", select_cma_callback,
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
      gtk_menu_append(GTK_MENU(menu), w);
      gtk_signal_connect(GTK_OBJECT(w), "activate", select_cma_callback,
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

      gtk_menu_append(GTK_MENU(menu), w);
      gtk_signal_connect(GTK_OBJECT(w), "activate",
			 select_cma_callback, GINT_TO_POINTER(CMA_CUSTOM));
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

	gtk_menu_append(GTK_MENU(menu), w);
	gtk_signal_connect(GTK_OBJECT(w), "activate", select_cma_callback,
			   GINT_TO_POINTER(i));
      }
    }
  }

  gtk_object_set_data(GTK_OBJECT(menu), "freeciv_change_cma",
		      GINT_TO_POINTER(change_cma));
}

/****************************************************************
...
*****************************************************************/
static gint city_change_callback(GtkWidget *w, GdkEvent *event, gpointer data)
{
  static GtkWidget* menu = NULL;
  GtkWidget* submenu = NULL;
  GtkWidget* item;
  GdkEventButton *bevent = (GdkEventButton *)event;

  
  if ( event->type != GDK_BUTTON_PRESS )
    return FALSE;

  if (menu)
    gtk_widget_destroy(menu);
  
  menu = gtk_menu_new();
  
  append_impr_or_unit_to_menu(menu, _("Improvements"), TRUE, TRUE, FALSE,
			      FALSE, city_can_build_impr_or_unit, TRUE);

  append_impr_or_unit_to_menu(menu, _("Units"), TRUE, FALSE, TRUE, FALSE,
                              city_can_build_impr_or_unit, TRUE);

  append_impr_or_unit_to_menu(menu, _("Wonders"), TRUE, FALSE, FALSE, TRUE,
                              city_can_build_impr_or_unit, TRUE);

  item = gtk_menu_item_new_with_label(_("CMA"));
  gtk_menu_append(GTK_MENU(menu), item);
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  if (!GTK_CLIST(city_list)->selection) {
    gtk_widget_set_sensitive(item, FALSE);
  } else {
    append_cma_to_menu(submenu, TRUE);
  }

  item = gtk_menu_item_new_with_label(_("Change All..."));
  gtk_menu_append(GTK_MENU(menu), item);
  gtk_signal_connect(GTK_OBJECT(item), "activate",
                     city_change_all_callback, NULL);


  gtk_widget_show_all(menu);
  
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                 bevent->button, bevent->time);

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void create_city_report_dialog(bool make_modal)
{
  static char *titles	[NUM_CREPORT_COLS];
  static char  buf	[NUM_CREPORT_COLS][64];

  GtkWidget *close_command, *scrolled;
  int        i;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  city_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(city_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(city_close_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(city_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(city_dialog_shell),_("Cities"));

  for (i=0;i<NUM_CREPORT_COLS;i++)
    titles[i]=buf[i];

  get_city_table_header(titles, sizeof(buf[0]));

  city_list = gtk_clist_new_with_titles(NUM_CREPORT_COLS,titles);
  gtk_clist_column_titles_active(GTK_CLIST(city_list));
  gtk_clist_set_auto_sort (GTK_CLIST (city_list), TRUE);
  gtk_clist_set_selection_mode(GTK_CLIST (city_list), GTK_SELECTION_EXTENDED);
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), city_list);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize(scrolled, -2, 350);

  gtk_signal_connect(GTK_OBJECT(city_list), "select_row",
		     GTK_SIGNAL_FUNC(city_report_list_callback), NULL);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->vbox ),
        scrolled, TRUE, TRUE, 0 );

  for (i=0;i<NUM_CREPORT_COLS;i++)
    gtk_clist_set_column_auto_resize (GTK_CLIST (city_list), i, TRUE);

  close_command		= gtk_accelbutton_new(_("C_lose"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( close_command );

  city_center_command	= gtk_accelbutton_new(_("Cen_ter"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_center_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_center_command, GTK_CAN_DEFAULT );

  city_popup_command	= gtk_accelbutton_new(_("_Popup"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_popup_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_popup_command, GTK_CAN_DEFAULT );

  city_buy_command	= gtk_accelbutton_new(_("_Buy"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_buy_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_buy_command, GTK_CAN_DEFAULT );

  city_change_command	= gtk_accelbutton_new(_("_Change..."), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_change_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_change_command, GTK_CAN_DEFAULT );

  city_select_command	= gtk_accelbutton_new(_("_Select..."), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_select_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_select_command, GTK_CAN_DEFAULT );

  city_config_command	= gtk_accelbutton_new(_("Con_figure..."), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_config_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_config_command, GTK_CAN_DEFAULT );

  gtk_signal_connect(GTK_OBJECT(city_select_command), "event",
	GTK_SIGNAL_FUNC(city_select_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(close_command), "clicked",
	GTK_SIGNAL_FUNC(city_close_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_center_command), "clicked",
	GTK_SIGNAL_FUNC(city_center_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_popup_command), "clicked",
	GTK_SIGNAL_FUNC(city_popup_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_buy_command), "clicked",
	GTK_SIGNAL_FUNC(city_buy_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_config_command), "clicked",
	GTK_SIGNAL_FUNC(city_config_callback), NULL);
  gtk_signal_connect( GTK_OBJECT( city_change_command ), "event",
	GTK_SIGNAL_FUNC(city_change_callback), NULL );
  gtk_signal_connect(GTK_OBJECT(city_list), "select_row",
	GTK_SIGNAL_FUNC(city_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_list), "unselect_row",
	GTK_SIGNAL_FUNC(city_list_callback), NULL);

  for ( i = 0;i < GTK_CLIST(city_list)->columns; i++ )
    gtk_signal_connect(GTK_OBJECT(GTK_CLIST(city_list)->column[i].button),
      "clicked", GTK_SIGNAL_FUNC(sort_cities_callback), GINT_TO_POINTER(i) );

  gtk_widget_show_all( GTK_DIALOG(city_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(city_dialog_shell)->action_area );

  gtk_clist_set_compare_func(GTK_CLIST(city_list), report_sort);

  gtk_widget_add_accelerator(close_command, "clicked",
	accel, GDK_Escape, 0, 0);

  city_report_dialog_update();
}

/****************************************************************
...
*****************************************************************/
static void city_list_callback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(city_change_command, can_client_issue_orders());
  gtk_widget_set_sensitive(city_select_command, can_client_issue_orders());
  if (GTK_CLIST(city_list)->selection) {
    gtk_widget_set_sensitive(city_center_command, TRUE);
    gtk_widget_set_sensitive(city_popup_command, TRUE);
    gtk_widget_set_sensitive(city_buy_command, can_client_issue_orders());
  } else {
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_select_all_callback(GtkWidget *w, gpointer data)
{
  gtk_clist_select_all( GTK_CLIST(city_list));
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_unselect_all_callback(GtkWidget *w, gpointer data)
{
  gtk_clist_unselect_all( GTK_CLIST(city_list));
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_invert_selection_callback(GtkWidget *w, gpointer data)
{
  gint i;
  GList* row_list = GTK_CLIST(city_list)->row_list;
  for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
  {
      GtkCListRow* row = GTK_CLIST_ROW(g_list_nth(row_list,i));
      if(row->state == GTK_STATE_SELECTED)
	gtk_clist_unselect_row(GTK_CLIST(city_list),i,0);
      else
	gtk_clist_select_row(GTK_CLIST(city_list),i,0);     
  }
  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_select_coastal_callback(GtkWidget *w, gpointer data)
{
  gint i;

  gtk_clist_unselect_all( GTK_CLIST(city_list));

  for (i = 0; i < GTK_CLIST(city_list)->rows; i++) {
    struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), i);
    
    if (is_ocean_near_tile(pcity->tile)) {
      gtk_clist_select_row(GTK_CLIST(city_list), i, 0);
    }
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_select_same_island_callback(GtkWidget *w, gpointer data)
{
  gint i;
  GList *selection = GTK_CLIST(city_list)->selection;
  GList *copy = NULL;

  for(; selection; selection = g_list_next(selection))
    copy = g_list_append (copy, city_from_glist(selection));

  for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
    {
      struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list),i);
      GList *current = copy;
      
      for(; current; current = g_list_next(current))
	{
	  struct city* selectedcity = current->data;
          if (map_get_continent(pcity->tile)
              == map_get_continent(selectedcity->tile))
	    {
	      gtk_clist_select_row(GTK_CLIST(city_list),i,0);
	      break;
	    }
	}
  }

  g_list_free(copy);
  return TRUE;
}
      
/****************************************************************
...
*****************************************************************/
static gboolean city_select_building_callback(GtkWidget *w, gpointer data)
{
  enum production_class_type which = GPOINTER_TO_INT(data);
  gint i;

  gtk_clist_unselect_all( GTK_CLIST(city_list));

  for(i = 0; i < GTK_CLIST(city_list)->rows; i++) {
    struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), i);

    if ( (which == TYPE_UNIT && pcity->is_building_unit)
         || (which == TYPE_NORMAL_IMPROVEMENT && !pcity->is_building_unit
             && !is_wonder(pcity->currently_building))
         || (which == TYPE_WONDER && !pcity->is_building_unit
             && is_wonder(pcity->currently_building)) ) {
      gtk_clist_select_row(GTK_CLIST(city_list), i, 0);
    }
  }

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static gboolean
city_select_callback(GtkWidget *w, GdkEvent *event, gpointer data)
{
  static GtkWidget* menu = NULL;
  GtkWidget* submenu = NULL;

  GtkWidget* item;
  GdkEventButton *bevent = (GdkEventButton *)event;

  if ( event->type != GDK_BUTTON_PRESS )
    return FALSE;

  if (menu)
    gtk_widget_destroy(menu);

  menu=gtk_menu_new();
  
  item=gtk_menu_item_new_with_label( _("All Cities") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_all_callback),
		     NULL);

  item=gtk_menu_item_new_with_label( _("No Cities") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_unselect_all_callback),
		     NULL);

  item=gtk_menu_item_new_with_label( _("Invert Selection") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_invert_selection_callback),
		     NULL);

  /* Add a separator */
  gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());  
  
  item=gtk_menu_item_new_with_label( _("Coastal Cities") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_coastal_callback),
		     NULL);

  item=gtk_menu_item_new_with_label( _("Same Island") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_same_island_callback),
		     NULL);
  if(!GTK_CLIST(city_list)->selection)
    gtk_widget_set_sensitive(item, FALSE);

  /* Add a separator */
  gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());  

  item=gtk_menu_item_new_with_label( _("Building Improvements") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_building_callback),
		     GINT_TO_POINTER(TYPE_NORMAL_IMPROVEMENT));

  item=gtk_menu_item_new_with_label( _("Building Units") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_building_callback),
		     GINT_TO_POINTER(TYPE_UNIT));

  item=gtk_menu_item_new_with_label( _("Building Wonders") );
  gtk_menu_append(GTK_MENU(menu),item);  
  gtk_signal_connect(GTK_OBJECT(item),"activate",
		     GTK_SIGNAL_FUNC(city_select_building_callback),
		     GINT_TO_POINTER(TYPE_WONDER));


  /* Add a separator */
  gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());  

  append_impr_or_unit_to_menu(menu, _("Supported Units"), FALSE, FALSE, 
                              TRUE, FALSE, city_unit_supported, FALSE);

  append_impr_or_unit_to_menu(menu, _("Units Present"), FALSE, FALSE, 
                              TRUE, FALSE, city_unit_present, FALSE);

  append_impr_or_unit_to_menu(menu, _("Improvements in City"), FALSE, TRUE, 
                              FALSE, TRUE, city_building_present, FALSE);

  append_impr_or_unit_to_menu(menu, _("Available Improvements"), FALSE, TRUE,
			      FALSE, FALSE, city_can_build_impr_or_unit,
			      FALSE);

  append_impr_or_unit_to_menu(menu, _("Available Units"), FALSE, FALSE,
			      TRUE, FALSE, city_can_build_impr_or_unit,
			      FALSE);

  append_impr_or_unit_to_menu(menu, _("Available Wonders"), FALSE, FALSE,
			      FALSE, TRUE, city_can_build_impr_or_unit,
			      FALSE);

  item=gtk_menu_item_new_with_label( _("CMA") );
  gtk_menu_append(GTK_MENU(menu),item);  
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  append_cma_to_menu(submenu, FALSE);

  gtk_widget_show_all(menu);
  
  gtk_menu_popup(GTK_MENU(menu), NULL,NULL,NULL,NULL,
		 bevent->button,bevent->time);

  return TRUE;
}

/****************************************************************
Handle callbacks from the "change all" dialog.
*****************************************************************/
static void city_change_all_dialog_callback(GtkWidget *w, gpointer data)
{
  const char *cmd = (const char *)data;

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
  GList *selection;
  gint row;
  struct city *pcity;
  static const char *title_[2][1] = { {N_("From:")},
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
    if ((selection = GTK_CLIST(city_list)->selection)) {
      row = GPOINTER_TO_INT(selection->data);
      if ((pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), row))) {
	selected_cid = cid_encode_from_city(pcity);
      }
    }

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
		       (gpointer)"change");
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
static void city_buy_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;

  g_assert(current);

  for (; current; current = g_list_next(current)) {
    cityrep_buy(city_from_glist(current));
  }
}

/****************************************************************
...
*****************************************************************/
static void city_close_callback(GtkWidget *w, gpointer data)
{
  popdown_city_report_dialog();
}

/****************************************************************
...
*****************************************************************/
static void city_center_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;
  struct city        *pcity;

  if (!current)
      return;

  pcity = city_from_glist (current);
    center_tile_mapcanvas(pcity->tile);
}

/****************************************************************
...
*****************************************************************/
static void city_popup_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;
  GList *copy = NULL;
  struct city        *pcity;

  if (!current)
    return;

  pcity = city_from_glist (current);
  if (center_when_popup_city) {
    center_tile_mapcanvas(pcity->tile);
  }

  /* We have to copy the list as the popup_city_dialog destroys the data */
  for(; current; current = g_list_next(current))
    copy = g_list_append (copy, city_from_glist(current));
  
  for(; copy; copy = g_list_next(copy))
    popup_city_dialog(copy->data, 0);

  g_list_free(copy);
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
static void city_config_callback(GtkWidget *w, gpointer data)
{
  popup_city_report_config_dialog();
}

/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  GList *selection = NULL;
  GList *copy = NULL;
  if(city_dialog_shell) {
    char *row	[NUM_CREPORT_COLS];
    char  buf	[NUM_CREPORT_COLS][64];
    int   i;
    struct city_report_spec *spec;

  if(is_report_dialogs_frozen()) return;
  if(!city_dialog_shell) return;

    for (i=0, spec=city_report_specs;i<NUM_CREPORT_COLS;i++, spec++)
    {
      row[i] = buf[i];
      gtk_clist_set_column_visibility(GTK_CLIST(city_list), i, spec->show);
    }

    for(selection = GTK_CLIST(city_list)->selection; 
	selection; selection = g_list_next(selection))
      copy = g_list_append (copy, city_from_glist(selection));

    gtk_clist_freeze(GTK_CLIST(city_list));
    gtk_clist_clear(GTK_CLIST(city_list));

    city_list_iterate(game.player_ptr->cities, pcity) {
      get_city_text(pcity, row, sizeof(buf[0]));
      i=gtk_clist_append(GTK_CLIST(city_list), row);
      gtk_clist_set_row_data (GTK_CLIST(city_list), i, pcity);
      if(g_list_find(copy,pcity))
	gtk_clist_select_row(GTK_CLIST(city_list), i, -1);
    } city_list_iterate_end;
    gtk_clist_thaw(GTK_CLIST(city_list));
    gtk_widget_show_all(city_list);

    g_list_free(copy);

    /* Set sensitivity right. */
    city_list_callback(NULL,0,0);
  }
}

/****************************************************************
  Update the text for a single city in the city report
*****************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  char *row [NUM_CREPORT_COLS];
  char  buf [NUM_CREPORT_COLS][64];
  int   i;
  struct city_report_spec *spec;

  if(is_report_dialogs_frozen()) return;
  if(!city_dialog_shell) return;

  for (i=0, spec=city_report_specs;i<NUM_CREPORT_COLS;i++, spec++)
  {
    row[i] = buf[i];
    gtk_clist_set_column_visibility(GTK_CLIST(city_list), i, spec->show);
  }

  if((i=gtk_clist_find_row_from_data(GTK_CLIST(city_list), pcity))!=-1)  {
#if 1
    /* This method avoids removing and re-adding the entry, because
       that seemed to cause problems in some cases, when the list
       is sorted by one of the columns.  Doing "Popup" for the top
       entry when sorted could cause core dumps or memory corruption,
       and in other cases could change the ordering (confusing) when
       multiple cities have the same string value in the column being
       sorted by.  -- dwp
    */
    int j;
    get_city_text(pcity, row, sizeof(buf[0]));
    gtk_clist_freeze(GTK_CLIST(city_list));
    for (j=0; j<NUM_CREPORT_COLS; j++) {
      gtk_clist_set_text(GTK_CLIST(city_list), i, j, row[j]);
    }
    gtk_clist_thaw(GTK_CLIST(city_list));
    gtk_clist_sort(GTK_CLIST(city_list));
    gtk_widget_show_all(city_list);
#else
    /* Old method, see above. */
    char *text;
    gboolean selected = (gboolean) g_list_find(GTK_CLIST(city_list)->selection,
					       GINT_TO_POINTER(i));

    gtk_clist_get_text(GTK_CLIST(city_list),i,0,&text);

    get_city_text(pcity, row, sizeof(buf[0]));

    gtk_clist_freeze(GTK_CLIST(city_list));
    gtk_clist_remove(GTK_CLIST(city_list),i);
    i=gtk_clist_append(GTK_CLIST(city_list),row);
    gtk_clist_set_row_data (GTK_CLIST(city_list), i, pcity);
    if (selected)
      gtk_clist_select_row(GTK_CLIST(city_list), i, -1);
    gtk_clist_thaw(GTK_CLIST(city_list));
    gtk_widget_show_all(city_list);
#endif
  }
  else
    city_report_dialog_update();
}

/****************************************************************
 After a selection rectangle is defined, make the cities that
 are hilited on the canvas exclusively hilited in the
 City List window.
*****************************************************************/
void hilite_cities_from_canvas(void)
{
  gint i;

  if (!city_dialog_shell) return;

  gtk_clist_unselect_all(GTK_CLIST(city_list));

  for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
  {
    struct city *pcity = gtk_clist_get_row_data(GTK_CLIST(city_list), i);

    if (is_city_hilited(pcity)) {
      gtk_clist_select_row(GTK_CLIST(city_list), i, 0);
    }
  }
}

/****************************************************************
 Toggle a city's hilited status.
*****************************************************************/
void toggle_city_hilite(struct city *pcity, bool on_off)
{
  gint i;

  if (!city_dialog_shell) return;

  i = gtk_clist_find_row_from_data(GTK_CLIST(city_list), pcity);

  on_off ?
	gtk_clist_select_row(GTK_CLIST(city_list), i, 0):
	gtk_clist_unselect_row(GTK_CLIST(city_list), i, 0);
}

/****************************************************************

		      CITY REPORT CONFIGURE DIALOG
 
****************************************************************/

/****************************************************************
... 
*****************************************************************/
static void popup_city_report_config_dialog(void)
{
  int i;

  if(config_shell)
    return;
  
  create_city_report_config_dialog();

  for(i=1; i<NUM_CREPORT_COLS; i++) {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(config_toggle[i]),
        city_report_specs[i].show);
  }

  gtk_set_relative_position(toplevel, config_shell, 25, 25);
  gtk_widget_show(config_shell);
  /* XtSetSensitive(main_form, FALSE); */
}

/****************************************************************
...
*****************************************************************/
static void create_city_report_config_dialog(void)
{
  GtkWidget *config_label, *config_ok_command, *box, *vbox;
  struct city_report_spec *spec;
  int i;
  
  config_shell = gtk_dialog_new();
  gtk_window_set_title (GTK_WINDOW(config_shell), _("Configure Cities Report"));
  gtk_window_set_position (GTK_WINDOW(config_shell), GTK_WIN_POS_MOUSE);

  config_label = gtk_label_new(_("Set columns shown"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), config_label,
			FALSE, FALSE, 0);

  box = gtk_hbox_new(TRUE,0);
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), box,
		     FALSE, FALSE, 0);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_widget_show(vbox);
  gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, FALSE, 0);

  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    if (i == NUM_CREPORT_COLS / 2 + 1) {
      vbox = gtk_vbox_new(FALSE, 0);
      gtk_widget_show(vbox);
      gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, FALSE, 0);
    }
    config_toggle[i] = gtk_check_button_new_with_label(spec->explanation);

    gtk_box_pack_start(GTK_BOX(vbox), config_toggle[i], FALSE, FALSE, 0);
  }

  config_ok_command = gtk_button_new_with_label(_("Close"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->action_area),
			config_ok_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(config_ok_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default(config_ok_command);
  
  gtk_signal_connect(GTK_OBJECT(config_ok_command), "clicked",
	GTK_SIGNAL_FUNC(config_ok_command_callback), NULL);

  gtk_widget_show_all( GTK_DIALOG(config_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(config_shell)->action_area );

/*  xaw_horiz_center(config_label);*/
}

/**************************************************************************
...
**************************************************************************/
static void config_ok_command_callback(GtkWidget *w, gpointer data)
{
  struct city_report_spec *spec;
  int i;
  
  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    spec->show = (bool) GTK_TOGGLE_BUTTON(config_toggle[i])->active;
  }
  gtk_widget_destroy(config_shell);

  config_shell = NULL;
  city_report_dialog_update();
}
