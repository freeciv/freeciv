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
#include "gui_stuff.h"
#include "mapview.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "climisc.h"

#include "cityrep.h"

extern GtkWidget *toplevel;

extern struct connection aconnection;
extern int delay_report_update;

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))

/******************************************************************/
GtkWidget *config_shell;
GtkWidget *config_toggle[NUM_CREPORT_COLS];

void create_city_report_config_dialog(void);
void popup_city_report_config_dialog(void);
void config_ok_command_callback(GtkWidget *widget, gpointer data);



/******************************************************************/
void create_city_report_dialog(int make_modal);
void city_close_callback(GtkWidget *widget, gpointer data);
void city_center_callback(GtkWidget *widget, gpointer data);
void city_popup_callback(GtkWidget *widget, gpointer data);
void city_buy_callback(GtkWidget *widget, gpointer data);
void city_refresh_callback(GtkWidget *widget, gpointer data);
void city_change_all_dialog_callback(GtkWidget *w, gpointer data);
void city_change_all_callback(GtkWidget *w, gpointer data);
void city_list_callback(GtkWidget *w, gint row, gint column);
void city_config_callback(GtkWidget *widget, gpointer data);
static gboolean city_change_callback(GtkWidget *w, GdkEvent *event, gpointer data);
static gboolean city_select_callback(GtkWidget *w, GdkEvent *event, gpointer data);

GtkWidget *city_dialog_shell=NULL;
GtkWidget *city_label;
GtkWidget *city_list;
GtkWidget *city_center_command, *city_popup_command, *city_buy_command,
	  *city_refresh_command, *city_config_command;
GtkWidget *city_change_command;
GtkWidget *city_change_all_command;
GtkWidget *city_change_all_dialog_shell;
GtkWidget *city_change_all_from_list;
GtkWidget *city_change_all_to_list;
GtkWidget *city_select_command;

int city_dialog_shell_is_modal;

/*
 * Sort cities by column...
 */
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
            NEG_VAL(spec->width), _(spec->title1),
	    NEG_VAL(spec->width), _(spec->title2));
  }
}

/****************************************************************

                      CITY REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(int make_modal)
{
  if(!city_dialog_shell) {
      city_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	gtk_widget_set_sensitive(toplevel, FALSE);
      
      create_city_report_dialog(make_modal);
      gtk_set_relative_position(toplevel, city_dialog_shell, 10, 10);

      gtk_widget_show(city_dialog_shell);
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

typedef gboolean TestCityFunc(struct city *, gint);

static gboolean city_can_build_imp_or_unit(struct city *pcity, gint number)
{
  if(number >= B_LAST)
    return can_build_unit(pcity, number - B_LAST);
  else
    return can_build_improvement(pcity, number);
}

static void
append_imp_or_unit_to_menu_sub(GtkWidget *menu,
			       gchar *nothing_appended_text,
			       gboolean append_units,
			       gboolean append_wonders,
			       gboolean change_prod,
			       TestCityFunc test_func,
			       GtkSignalFunc callback)
{  
  gint i;
  gint first = append_units ? B_LAST : 0;
  gint last = append_units ? game.num_unit_types + B_LAST : B_LAST;
  gboolean something_appended = FALSE;
  GtkWidget *item; 

  for(i=first; i<last; i++)
    {
      gboolean append = FALSE;

      /* Those many ! are to ensure TRUE is 1 and FALSE is 0 */
      if( !append_units && ( !append_wonders != !is_wonder(i) ) )
	continue;

      if(!change_prod)
	{  
	  city_list_iterate(game.player_ptr->cities, pcity) 
	    {
	      append |= test_func(pcity, i);    
	    } city_list_iterate_end;
	}
      else
	{
	  GList *selection;

	  g_assert (GTK_CLIST(city_list)->selection);
	  selection = GTK_CLIST(city_list)->selection;
	  for(; selection; selection = g_list_next(selection))
	    append |= test_func(city_from_glist(selection), i);
	}
    
      if(append) 
	{
	  gint cost;
	  gchar* name;
	  
	  if(append_units)
	    {
	      cost = get_unit_type(i-B_LAST)->build_cost;
	      name = get_unit_name(i-B_LAST);
	    }
	  else
	    {
	      cost = (i==B_CAPITAL) ? -1 : get_improvement_type(i)->build_cost;
	      if(append_wonders)
		{
		  /* We need a city to get the right name for wonders */
		  struct city *pcity = GTK_CLIST(city_list)->row_list->data;
		  name = get_imp_name_ex(pcity, i);
		}
	      else
		name = get_improvement_name(i);
	    }
	  something_appended = TRUE;

	  if (change_prod)
	    {
	      gchar *label;
	      if (cost < 0) {
		label = g_strdup_printf("%s (XX)",name);
	      } else {
		label = g_strdup_printf("%s (%d)",name, cost);
	      }
	      item=gtk_menu_item_new_with_label( label );
	      g_free (label);
	    }
	  else
	    item=gtk_menu_item_new_with_label(name);
	  
	  gtk_menu_append(GTK_MENU(menu),item);
	  
	  gtk_signal_connect(GTK_OBJECT(item),"activate", callback, 
			     GINT_TO_POINTER(i));
	}
    }

  if(!something_appended)
    {
      item=gtk_menu_item_new_with_label( nothing_appended_text );
      gtk_widget_set_sensitive(item, FALSE);
      gtk_menu_append(GTK_MENU(menu),item);  
    }
}

static
void select_imp_or_unit_callback(GtkWidget *w, gpointer data)
{
  gint number = GPOINTER_TO_INT(data);
  gint i;
  GtkObject *parent = GTK_OBJECT(w->parent);
  TestCityFunc *test_func = gtk_object_get_data(parent, "freeciv_test_func");
  gboolean change_prod = 
    GPOINTER_TO_INT(gtk_object_get_data(parent, "freeciv_change_prod"));

  /* If this is not the change production button */
  if(!change_prod)
    {
      gtk_clist_unselect_all( GTK_CLIST(city_list));

      for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
	{
	  struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list),i);
	  if (test_func(pcity, number))
	    gtk_clist_select_row(GTK_CLIST(city_list),i,0);
	}
    }
  else
    {
      gboolean is_unit = number >= B_LAST;
      GList* selection = GTK_CLIST(city_list)->selection;

      g_assert(selection);
  
      for(; selection; selection = g_list_next(selection))
	{
	  struct packet_city_request packet;

	  if (is_unit)
	    number -= B_LAST;
	  
	  packet.city_id=city_from_glist(selection)->id;
	  packet.name[0]='\0';
	  packet.build_id=number;
	  packet.is_build_id_unit_id=is_unit;
	  send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
	}
    }
}

static void
append_imp_or_unit_to_menu(GtkWidget *menu,
			   gboolean change_prod,
			   gboolean append_improvements,
			   gboolean append_units,
			   TestCityFunc test_func)
{
  if(append_improvements)
    {
      /* Add all buildings */
      append_imp_or_unit_to_menu_sub(menu, _("No Buildings Available"),
				     FALSE, FALSE, change_prod,
				     (gboolean (*)(struct city*,gint))
				     test_func,
				     select_imp_or_unit_callback);
      /* Add a separator */
      gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());  
  
      /* Add all wonders */
      append_imp_or_unit_to_menu_sub(menu, _("No Wonders Available"),
				     FALSE, TRUE, change_prod,
				     (gboolean (*)(struct city*,gint))
				     test_func,
				     select_imp_or_unit_callback);
      /* Add a separator */
      if(append_units)
	gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());   
    }
  
  if(append_units)
    {
      /* Add all units */
      append_imp_or_unit_to_menu_sub(menu, _("No Units Available"),
				     TRUE, FALSE, change_prod,
				     test_func,
				     select_imp_or_unit_callback);
    }
  
  gtk_object_set_data(GTK_OBJECT(menu), "freeciv_test_func", test_func);
  gtk_object_set_data(GTK_OBJECT(menu), "freeciv_change_prod", 
		      GINT_TO_POINTER(change_prod));
}


static gint 
city_change_callback(GtkWidget *w, GdkEvent *event, gpointer data)
{
  static GtkWidget* menu = NULL;
  GdkEventButton *bevent = (GdkEventButton *)event;
  
  if ( event->type != GDK_BUTTON_PRESS )
    return FALSE;

  /* This migth happen, whenever a selection is still in progress, while 
     "Changed" is pressed (e.g. when holding the shift key) */
  if(!GTK_CLIST(city_list)->selection)
    return FALSE;

  if (menu)
    gtk_widget_destroy(menu);
  
  menu=gtk_menu_new();
  
  append_imp_or_unit_to_menu(menu, TRUE, TRUE, TRUE, 
			     city_can_build_imp_or_unit);

  gtk_widget_show_all(menu);
  
  gtk_menu_popup(GTK_MENU(menu),
	NULL,NULL,NULL,NULL,bevent->button,bevent->time);

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
void create_city_report_dialog(int make_modal)
{
  static char *titles	[NUM_CREPORT_COLS];
  static char  buf	[NUM_CREPORT_COLS][64];

  GtkWidget *close_command, *scrolled;
  char      *report_title;
  int        i;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  city_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(city_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(city_close_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(city_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(city_dialog_shell),_("City Report"));

  report_title=get_report_title(_("City Advisor"));
  city_label = gtk_label_new(report_title);
  free(report_title);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->vbox ),
        city_label, FALSE, FALSE, 0 );

  for (i=0;i<NUM_CREPORT_COLS;i++)
    titles[i]=buf[i];

  get_city_table_header(titles, sizeof(buf[0]));

  city_list = gtk_clist_new_with_titles(NUM_CREPORT_COLS,titles);
  gtk_clist_column_titles_active(GTK_CLIST(city_list));
  gtk_clist_set_auto_sort (GTK_CLIST (city_list), TRUE);
  gtk_clist_set_selection_mode(GTK_CLIST (city_list), GTK_SELECTION_EXTENDED);
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), city_list);
  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrolled ),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
  gtk_widget_set_usize(city_list, 620, 250);

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

  city_change_command	= gtk_accelbutton_new(_("_Change"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_change_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_change_command, GTK_CAN_DEFAULT );

  gtk_signal_connect( GTK_OBJECT( city_change_command ), "event",
	GTK_SIGNAL_FUNC( city_change_callback ), NULL );

  city_change_all_command = gtk_accelbutton_new(_("Change _All"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_change_all_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_change_all_command, GTK_CAN_DEFAULT );

  city_refresh_command	= gtk_accelbutton_new(_("_Refresh"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_refresh_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_refresh_command, GTK_CAN_DEFAULT );
  
  city_select_command	= gtk_accelbutton_new(_("_Select"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_select_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_select_command, GTK_CAN_DEFAULT );

  gtk_signal_connect(GTK_OBJECT(city_select_command), "event",
	GTK_SIGNAL_FUNC(city_select_callback), NULL);

  city_config_command	= gtk_accelbutton_new(_("Con_figure"), accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_config_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_config_command, GTK_CAN_DEFAULT );

  gtk_signal_connect(GTK_OBJECT(close_command), "clicked",
	GTK_SIGNAL_FUNC(city_close_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_center_command), "clicked",
	GTK_SIGNAL_FUNC(city_center_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_popup_command), "clicked",
	GTK_SIGNAL_FUNC(city_popup_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_buy_command), "clicked",
	GTK_SIGNAL_FUNC(city_buy_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_refresh_command), "clicked",
	GTK_SIGNAL_FUNC(city_refresh_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_config_command), "clicked",
	GTK_SIGNAL_FUNC(city_config_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_change_all_command), "clicked",
	GTK_SIGNAL_FUNC(city_change_all_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_list), "select_row",
	GTK_SIGNAL_FUNC(city_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_list), "unselect_row",
	GTK_SIGNAL_FUNC(city_list_callback), NULL);

  for ( i = 0;i < GTK_CLIST(city_list)->columns; i++ )
    gtk_signal_connect(GTK_OBJECT(GTK_CLIST(city_list)->column[i].button),
      "clicked", GTK_SIGNAL_FUNC(sort_cities_callback), GINT_TO_POINTER(i) );

  gtk_widget_show_all( GTK_DIALOG(city_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(city_dialog_shell)->action_area );

  gtk_widget_add_accelerator(close_command, "clicked",
	accel, GDK_Escape, 0, 0);

  city_report_dialog_update();
}

/****************************************************************
...
*****************************************************************/
void city_list_callback(GtkWidget *w, gint row, gint column)
{
  if (GTK_CLIST(city_list)->selection)
  {
    gtk_widget_set_sensitive(city_change_command, TRUE);
    gtk_widget_set_sensitive(city_center_command, TRUE);
    gtk_widget_set_sensitive(city_popup_command, TRUE);
    gtk_widget_set_sensitive(city_buy_command, TRUE);
  }
  else
  {
    gtk_widget_set_sensitive(city_change_command, FALSE);
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
  }
}

static gboolean
city_select_all_callback(GtkWidget *w, gpointer data)
{
  gtk_clist_select_all( GTK_CLIST(city_list));
  return TRUE;
}

static gboolean
city_unselect_all_callback(GtkWidget *w, gpointer data)
{
  gtk_clist_unselect_all( GTK_CLIST(city_list));
  return TRUE;
}

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

static gboolean
city_select_coastal_callback(GtkWidget *w, gpointer data)
{
  gint i;

  gtk_clist_unselect_all( GTK_CLIST(city_list));

  for(i = 0; i < GTK_CLIST(city_list)->rows; i++)
  {
      struct city* pcity = gtk_clist_get_row_data(GTK_CLIST(city_list),i);

      if (is_terrain_near_tile(pcity->x, pcity->y, T_OCEAN))
	gtk_clist_select_row(GTK_CLIST(city_list),i,0);
  }

  return TRUE;
}

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
	  if (map_same_continent(pcity->x,pcity->y,
				 selectedcity->x,selectedcity->y))
	    {
	      gtk_clist_select_row(GTK_CLIST(city_list),i,0);
	      break;
	    }
	}
  }

  g_list_free(copy);
  return TRUE;
}

static gboolean
city_unit_supported(struct city *pcity, gint unit)
{
  struct unit_list *punit_list = &pcity->units_supported;

  unit_list_iterate((*punit_list), punit) 
  {
      if(punit->type == unit - B_LAST)
	return TRUE;
    } unit_list_iterate_end;
  return FALSE;
}

static gboolean
city_unit_present(struct city *pcity, gint unit)
{
  struct unit_list *punit_list = &map_get_tile(pcity->x,pcity->y)->units;

  unit_list_iterate((*punit_list), punit) 
  {
      if(punit->type == unit - B_LAST)
	return TRUE;
    } unit_list_iterate_end;
  return FALSE;
}
      
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
    gtk_widget_set_sensitive(item,FALSE);

  /* Add a separator */
  gtk_menu_append(GTK_MENU(menu),gtk_menu_item_new ());  

  item=gtk_menu_item_new_with_label( _("Supported Units") );
  gtk_menu_append(GTK_MENU(menu),item);  
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  append_imp_or_unit_to_menu(submenu, FALSE, FALSE, TRUE, city_unit_supported);

  item=gtk_menu_item_new_with_label( _("Units Present") );
  gtk_menu_append(GTK_MENU(menu),item);  
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  append_imp_or_unit_to_menu(submenu, FALSE, FALSE, TRUE, city_unit_present);

  item=gtk_menu_item_new_with_label( _("Available To Build") );
  gtk_menu_append(GTK_MENU(menu),item);  
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  append_imp_or_unit_to_menu(submenu, FALSE, TRUE, TRUE,
			     city_can_build_imp_or_unit);

  item=gtk_menu_item_new_with_label( _("Improvements in City") );
  gtk_menu_append(GTK_MENU(menu),item);  
  submenu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
  append_imp_or_unit_to_menu(submenu, FALSE, TRUE, FALSE,
			     (TestCityFunc*)city_got_building);

  gtk_widget_show_all(menu);
  
  gtk_menu_popup(GTK_MENU(menu), NULL,NULL,NULL,NULL,
		 bevent->button,bevent->time);

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
void city_refresh_callback(GtkWidget *w, gpointer data)

{ /* added by Syela - I find this very useful */
  GList *selection = GTK_CLIST(city_list)->selection;
  struct packet_generic_integer packet;

  if ( !selection )
  {
    packet.value = 0;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
    }
  else
    for(; selection; selection = g_list_next(selection))
      {
	packet.value = city_from_glist(selection)->id;
	send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, 
				    &packet);
  }
}

/****************************************************************
Handle callbacks from the "change all" dialog.
*****************************************************************/
void city_change_all_dialog_callback(GtkWidget *w, gpointer data)
{
    GList *selection;
    gint row;
    int from, to;
    char *cmd = (char *)data;
    char buf[512];

    if (cmd != NULL) {
	if ( !( selection = GTK_CLIST( city_change_all_from_list )->selection ) ) {
	    append_output_window(_("Game: Select a unit or improvement"
				   " to change production from."));
	    return;
	}
	else {
	    row = (gint)selection->data;
	    from = (int)gtk_clist_get_row_data(GTK_CLIST(city_change_all_from_list),
					       row);
	}
	if ( !( selection = GTK_CLIST( city_change_all_to_list )->selection ) ) {
	    append_output_window(_("Game: Select a unit or improvement"
				   " to change production to."));
	    return;
	}
	else {
	    row = (gint)selection->data;
	    to = (int)gtk_clist_get_row_data(GTK_CLIST(city_change_all_to_list),
					       row);
	}
	if (from==to) {
	    append_output_window(_("Game: That's the same thing!"));
	    return;
	}
	my_snprintf(buf, sizeof(buf),
		    _("Game: Changing production of every %s into %s."),
		(from >= B_LAST) ?
  		  get_unit_type(from-B_LAST)->name : get_improvement_name(from),
		(to >= B_LAST) ?
		  get_unit_type(to-B_LAST)->name : get_improvement_name(to));

	append_output_window(buf);
	
	client_change_all(from,to);
    }
    gtk_widget_destroy(city_change_all_dialog_shell);
    city_change_all_dialog_shell = NULL;
}

/****************************************************************
Change all cities building one type of thing to another thing.
This is a callback for the "change all" button.
*****************************************************************/
#define MAX_LEN_BUF 256
void city_change_all_callback(GtkWidget *w, gpointer data)
{
    GList              *selection;
    gint                row;
    struct city *pcity;
    static gchar *title_[2][1] = {{N_("From:")},
				 {N_("To:")}};
    static gchar **title[2];
    gchar *buf[1];
    int i,j;
    int *is_building;
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *scrollpane;

    if (!title[0]) {
      title[0] = intl_slist(1, title_[0]);
      title[1] = intl_slist(1, title_[1]);
    }
  
    if (city_change_all_dialog_shell == NULL) {
	city_change_all_dialog_shell = gtk_dialog_new();
	gtk_set_relative_position(city_dialog_shell,
				  city_change_all_dialog_shell, 10, 10);
	
	gtk_signal_connect( GTK_OBJECT(city_change_all_dialog_shell),"delete_event",
			    GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
			    NULL);
  
	gtk_window_set_title(GTK_WINDOW(city_change_all_dialog_shell),
			     _("Change Production Everywhere"));

	box = gtk_hbox_new(FALSE, 10);

	/* make a list of everything we're currently building */
	city_change_all_from_list = gtk_clist_new_with_titles(1, title[0]);
	gtk_clist_column_titles_passive(GTK_CLIST(city_change_all_from_list));
	gtk_clist_set_selection_mode(GTK_CLIST(city_change_all_from_list),
				     GTK_SELECTION_SINGLE);
	scrollpane = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scrollpane),
			  city_change_all_from_list);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrollpane ),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC );
	gtk_widget_set_usize(city_change_all_from_list, 200, 200);
	
	buf[0] = g_malloc(MAX_LEN_BUF);
	is_building = g_malloc0((B_LAST+U_LAST) * sizeof(int));

	city_list_iterate(game.player_ptr->cities, pcity) {
	    if (pcity->is_building_unit)
		is_building[pcity->currently_building + B_LAST] = 1;
	    else
		is_building[pcity->currently_building] = 1;
	} city_list_iterate_end;

	/* if a city was selected when "change all" was clicked on,
	   hilight that item so user doesn't have to click it */
	if ( ( selection = GTK_CLIST( city_list )->selection ) ) {
	    row = (gint)selection->data;
	    if((pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row))) {
		if (pcity->is_building_unit)
		    is_building[pcity->currently_building + B_LAST] = 2;
		else
		    is_building[pcity->currently_building] = 2;
	    }
	}

	for(i=0; i<B_LAST; i++)
	    if (is_building[i]) {
		mystrlcpy(buf[0], get_improvement_name(i), MAX_LEN_BUF);
		j = gtk_clist_append(GTK_CLIST(city_change_all_from_list), buf);
		gtk_clist_set_row_data(GTK_CLIST(city_change_all_from_list),
				       j, (gpointer)i);
		if (is_building[i] == 2) {
		    gtk_clist_select_row(GTK_CLIST(city_change_all_from_list),
					  j, 0);
		}
	    }

	for(i=0; i<U_LAST; i++)
	    if (is_building[B_LAST+i]) {
		mystrlcpy(buf[0], get_unit_name(i), MAX_LEN_BUF);
		j = gtk_clist_append(GTK_CLIST(city_change_all_from_list), buf);
		gtk_clist_set_row_data(GTK_CLIST(city_change_all_from_list),
				       j, (gpointer)(i+B_LAST));
		if (is_building[i] == 2) {
		    gtk_clist_select_row(GTK_CLIST(city_change_all_from_list),
					  j, 0);
		}
	    }

	g_free(is_building);

	gtk_box_pack_start( GTK_BOX (box),
			    scrollpane, TRUE, TRUE, 10);
	
	gtk_widget_show(scrollpane);
	gtk_widget_show(city_change_all_from_list);

	/* make a list of everything we could build */
	city_change_all_to_list = gtk_clist_new_with_titles(1, title[1]);
	gtk_clist_column_titles_passive(GTK_CLIST(city_change_all_to_list));
	gtk_clist_set_selection_mode(GTK_CLIST(city_change_all_from_list),
				     GTK_SELECTION_SINGLE);
	scrollpane = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scrollpane),
			  city_change_all_to_list);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrollpane ),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC );
	gtk_widget_set_usize(city_change_all_to_list, 200, 200);

	for(i=0; i<B_LAST; i++)
	    if(can_player_build_improvement(game.player_ptr, i)) {
		my_snprintf(buf[0], MAX_LEN_BUF, "%s (%d)",
			get_improvement_name(i),
			get_improvement_type(i)->build_cost);
		j = gtk_clist_append(GTK_CLIST(city_change_all_to_list), buf);
		gtk_clist_set_row_data(GTK_CLIST(city_change_all_to_list),
				       j, (gpointer)i);
	    }

	for(i=0; i<U_LAST; i++)
	    if(can_player_build_unit(game.player_ptr, i)) {
		my_snprintf(buf[0], MAX_LEN_BUF, "%s (%d)",
			get_unit_name(i),
			get_unit_type(i)->build_cost);
		j = gtk_clist_append(GTK_CLIST(city_change_all_to_list), buf);
		gtk_clist_set_row_data(GTK_CLIST(city_change_all_to_list),
				       j, (gpointer)(i+B_LAST));
	    }

	g_free(buf[0]);

	gtk_box_pack_start( GTK_BOX (box),
			    scrollpane, TRUE, TRUE, 10);
	gtk_widget_show(scrollpane);
	
	gtk_box_pack_start( GTK_BOX (GTK_DIALOG (city_change_all_dialog_shell)->vbox),
			    box, TRUE, TRUE, 0);
	gtk_widget_show(city_change_all_to_list);

	gtk_widget_show(box);

	button = gtk_button_new_with_label(_("Change"));
	gtk_box_pack_start( GTK_BOX (GTK_DIALOG (city_change_all_dialog_shell)->action_area),
			    button, TRUE, FALSE, 0);
	gtk_signal_connect( GTK_OBJECT(button),"clicked",
			    GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
			    "change" );
	gtk_widget_show(button);
  
	button = gtk_button_new_with_label(_("Cancel"));
	gtk_box_pack_start( GTK_BOX (GTK_DIALOG (city_change_all_dialog_shell)->action_area),
			    button, TRUE, FALSE, 0);
	gtk_signal_connect( GTK_OBJECT(button),"clicked",
			    GTK_SIGNAL_FUNC(city_change_all_dialog_callback),
			    NULL );
	gtk_widget_show(button);
  
	gtk_widget_show(city_change_all_dialog_shell);
    }
}

/****************************************************************
...
*****************************************************************/
void city_buy_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;

  g_assert(current);

  for(; current; current = g_list_next(current))
  {
      struct city *pcity = city_from_glist (current);
      int value;
      char *name;
      char buf[512];

      value=city_buy_cost(pcity);    
      if(pcity->is_building_unit)
	name=get_unit_type(pcity->currently_building)->name;
      else
	name=get_imp_name_ex(pcity, pcity->currently_building);

      if (game.player_ptr->economic.gold >= value)
	{
	  struct packet_city_request packet;
	  packet.city_id=pcity->id;
	  packet.name[0]='\0';
	  send_packet_city_request(&aconnection, &packet, PACKET_CITY_BUY);
	}
      else
	{
	  my_snprintf(buf, sizeof(buf),
		      _("Game: %s costs %d gold and you only have %d gold."),
		      name,value,game.player_ptr->economic.gold);
	  append_output_window(buf);
	}
  }
}

/****************************************************************
...
*****************************************************************/
void city_close_callback(GtkWidget *w, gpointer data)
{

  if(city_dialog_shell_is_modal)
     gtk_widget_set_sensitive(toplevel, TRUE);
   gtk_widget_destroy(city_dialog_shell);
   city_dialog_shell=NULL;
}

/****************************************************************
...
*****************************************************************/
void city_center_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;
  struct city        *pcity;

  if (!current)
      return;

  pcity = city_from_glist (current);
    center_tile_mapcanvas(pcity->x, pcity->y);
}

/****************************************************************
...
*****************************************************************/
void city_popup_callback(GtkWidget *w, gpointer data)
{
  GList *current = GTK_CLIST(city_list)->selection;
  GList *copy = NULL;
  struct city        *pcity;

  if (!current)
    return;

  pcity = city_from_glist (current);
  if (center_when_popup_city) {
    center_tile_mapcanvas(pcity->x, pcity->y);
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
void city_config_callback(GtkWidget *w, gpointer data)
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
    char *report_title;
    struct city_report_spec *spec;

  if(delay_report_update) return;
  if(!city_dialog_shell) return;

    report_title=get_report_title(_("City Advisor"));
    gtk_set_label(city_label, report_title);
    free(report_title);

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

  if(delay_report_update) return;
  if(!city_dialog_shell) return;

  for (i=0, spec=city_report_specs;i<NUM_CREPORT_COLS;i++, spec++)
  {
    row[i] = buf[i];
    gtk_clist_set_column_visibility(GTK_CLIST(city_list), i, spec->show);
  }

  if((i=gtk_clist_find_row_from_data(GTK_CLIST(city_list), pcity))!=-1)  {
    char *text;
    gboolean selected = (gboolean) g_list_find(GTK_CLIST(city_list)->selection,
					       (gpointer)i);

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
  }
  else
    city_report_dialog_update();
}

/****************************************************************

		      CITY REPORT CONFIGURE DIALOG
 
****************************************************************/

/****************************************************************
... 
*****************************************************************/
void popup_city_report_config_dialog(void)
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
void create_city_report_config_dialog(void)
{
  GtkWidget *config_label, *config_ok_command;
  struct city_report_spec *spec;
  int i;
  
  config_shell = gtk_dialog_new();
  gtk_window_set_title (GTK_WINDOW(config_shell), _("Configure City Report"));
  gtk_window_set_position (GTK_WINDOW(config_shell), GTK_WIN_POS_MOUSE);

  config_label = gtk_label_new(_("Set columns shown"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), config_label,
			FALSE, FALSE, 0);

  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    config_toggle[i]=gtk_check_button_new_with_label(_(spec->explanation));

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), config_toggle[i],
			FALSE, FALSE, 0);
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
void config_ok_command_callback(GtkWidget *w, gpointer data)
{
  struct city_report_spec *spec;
  gboolean b;
  int i;
  
  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    b=GTK_TOGGLE_BUTTON(config_toggle[i])->active;
    spec->show = b;
  }
  gtk_widget_destroy(config_shell);

  config_shell=0;
  city_report_dialog_update();
}
