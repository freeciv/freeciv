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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <shared.h>
#include <gui_stuff.h>
#include <game.h>
#include <unit.h>
#include <city.h>
#include <packets.h>
#include <repodlgs.h>
#include <chatline.h>
#include <mapview.h>
#include <citydlg.h>
#include <optiondlg.h>
#include <log.h>
#include <cityrep.h>
#include <options.h>

extern GtkWidget *toplevel;
extern GdkWindow *root_window;

extern struct connection aconnection;
extern int delay_report_update;

/* abbreviate long city names to this length in the city report: */
#define REPORT_CITYNAME_ABBREV 15 

/************************************************************************
 cr_entry = return an entry (one column for one city) for the city report
 These return ptrs to filled in static strings.
 Note the returned string may not be exactly the right length; that
 is handled later.
*************************************************************************/

static char *cr_entry_cityname(struct city *pcity)
{
  static char buf[REPORT_CITYNAME_ABBREV+1];
  if (strlen(pcity->name) <= REPORT_CITYNAME_ABBREV) {
    return pcity->name;
  } else {
    strncpy(buf, pcity->name, REPORT_CITYNAME_ABBREV-1);
    buf[REPORT_CITYNAME_ABBREV-1] = '.';
    buf[REPORT_CITYNAME_ABBREV] = '\0';
    return buf;
  }
}

static char *cr_entry_size(struct city *pcity)
{
  static char buf[8];
  sprintf(buf, "%2d", pcity->size);
  return buf;
}

static char *cr_entry_hstate_concise(struct city *pcity)
{
  static char buf[4];
  sprintf(buf, "%s", (city_celebrating(pcity) ? "*" :
		     (city_unhappy(pcity) ? "X" : " ")));
  return buf;
}

static char *cr_entry_hstate_verbose(struct city *pcity)
{
  static char buf[16];
  sprintf(buf, "%s", (city_celebrating(pcity) ? "Rapture" :
		     (city_unhappy(pcity) ? "Disorder" : "Peace")));
  return buf;
}

static char *cr_entry_workers(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	 pcity->ppl_happy[4],
	 pcity->ppl_content[4],
	 pcity->ppl_unhappy[4]);
  return buf;
}

static char *cr_entry_specialists(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	 pcity->ppl_elvis,
	  pcity->ppl_scientist,
	  pcity->ppl_taxman);
  return buf;
}

static char *cr_entry_resources(struct city *pcity)
{
  static char buf[32];
  sprintf(buf, "%d/%d/%d",
	 pcity->food_surplus, 
	 pcity->shield_surplus, 
	 pcity->trade_prod);
  return buf;
}

static char *cr_entry_output(struct city *pcity)
{
  static char buf[32];
  int goldie;

  goldie = city_gold_surplus(pcity);
  sprintf(buf, "%s%d/%d/%d",
	  (goldie < 0) ? "-" : (goldie > 0) ? "+" : "",
	  (goldie < 0) ? (-goldie) : goldie,
	 pcity->luxury_total,
	 pcity->science_total);
  return buf;
}

static char *cr_entry_food(struct city *pcity)
{
  static char buf[32];
  sprintf(buf,"%d/%d",
	 pcity->food_stock,
	 pcity->size * game.foodbox);
  return buf;
}

static char *cr_entry_pollution(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%3d", pcity->pollution);
  return buf;
}

static char *cr_entry_num_trade(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%d", city_num_trade_routes(pcity));
  return buf;
}

static char *cr_entry_building(struct city *pcity)
{
  static char buf[64];
  if(pcity->is_building_unit)
    sprintf(buf, "%s(%d/%d/%d)", 
	    get_unit_type(pcity->currently_building)->name,
	   pcity->shield_stock,
	   get_unit_type(pcity->currently_building)->build_cost,
	   city_buy_cost(pcity));
  else
    sprintf(buf, "%s(%d/%d/%d)", 
	   get_imp_name_ex(pcity, pcity->currently_building),
	   pcity->shield_stock,
	   get_improvement_type(pcity->currently_building)->build_cost,
	   city_buy_cost(pcity));
  return buf;
}

static char *cr_entry_corruption(struct city *pcity)
{
  static char buf[8];
  sprintf(buf,"%3d", pcity->corruption);
  return buf;
}

/* City report options (which columns get shown)
 * To add a new entry, you should just have to:
 * - add a function like those above
 * - add an entry in the city_report_specs[] table
 */

struct city_report_spec {
  int show;		       /* modify this to customize */
  int width;		       /* 0 means variable; rightmost only */
  int space;		       /* number of leading spaces (see below) */
  char *title1;
  char *title2;
  char *explanation;
  char *(*func)(struct city*);
  char *tagname;	       /* for save_options */
};

/* This generates the function name and the tagname: */
#define FUNC_TAG(var)  cr_entry_##var, #var 

/* Use tagname rather than index for load/save, because later
   additions won't necessarily be at the end.
*/

/* Note on space: you can do spacing and alignment in various ways;
   you can avoid explicit space between columns if they are bracketted,
   but the problem is that with a configurable report you don't know
   what's going to be next to what.
*/

static struct city_report_spec city_report_specs[] = {
  { 1,-15, 0, "",  "Name",	      "City Name",
				      FUNC_TAG(cityname) },
  { 0, -2, 1, "",  "Sz",	      "Size",
				      FUNC_TAG(size) },
  { 1, -8, 1, "",  "State",	      "Rapture/Peace/Disorder",
				      FUNC_TAG(hstate_verbose) },
  { 0, -1, 1, "",  "",  	      "Concise *=Rapture, X=Disorder",
				      FUNC_TAG(hstate_concise) },
  { 1, -8, 1, "Workers", "H/C/U",     "Workers: Happy, Content, Unhappy",
				      FUNC_TAG(workers) },
  { 0, -7, 1, "Special", "E/S/T",     "Entertainers, Scientists, Taxmen",
				      FUNC_TAG(specialists) },
  { 1,-10, 1, "Surplus", "F/P/T",     "Surplus: Food, Production, Trade",
				      FUNC_TAG(resources) },
  { 1,-10, 1, "Economy", "G/L/S",     "Economy: Gold, Luxuries, Science",
				      FUNC_TAG(output) },
  { 0, -1, 1, "n", "T", 	      "Number of Trade Routes",
				      FUNC_TAG(num_trade) },
  { 1, -7, 1, "Food", "Stock",        "Food Stock",
				      FUNC_TAG(food) },
  { 0, -3, 1, "", "Pol",	      "Pollution",
				      FUNC_TAG(pollution) },
  { 0, -3, 1, "", "Cor",              "Corruption",
                                      FUNC_TAG(corruption) },
  { 1,  0, 1, "Currently Building",   "(Stock,Target,Buy Cost)",
				      "Currently Building",
				      FUNC_TAG(building) }
};

#define NUM_CREPORT_COLS \
	 sizeof(city_report_specs)/sizeof(city_report_specs[0])
     
/******************************************************************
Some simple wrappers:
******************************************************************/
int num_city_report_spec(void)
{
  return NUM_CREPORT_COLS;
}
int *city_report_spec_show_ptr(int i)
{
  return &(city_report_specs[i].show);
}
char *city_report_spec_tagname(int i)
{
  return city_report_specs[i].tagname;
}

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
void city_change_callback(GtkWidget *widget, gpointer data);
void city_list_callback(GtkWidget *w, gint row, gint column);
void city_list_ucallback(GtkWidget *w, gint row, gint column);
void city_config_callback(GtkWidget *widget, gpointer data);

GtkWidget *city_dialog_shell=NULL;
GtkWidget *city_label;
GtkWidget *city_list;
GtkWidget *city_center_command, *city_popup_command, *city_buy_command,
	  *city_refresh_command, *city_config_command;
GtkWidget *city_change_command, *city_popupmenu;

int city_dialog_shell_is_modal;


/****************************************************************
 Create the text for a line in the city report
*****************************************************************/
static void get_city_text(struct city *pcity, char *buf[])
{
  struct city_report_spec *spec;
  int i;

  for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    buf[i][0]='\0';
    if(!spec->show) continue;

    sprintf(buf[i], "%*s", spec->width, (spec->func)(pcity));
  }
}

/****************************************************************
 Return text line for the column headers for the city report
*****************************************************************/
static void get_city_table_header(char *text[])
{
  struct city_report_spec *spec;
  int i;

  for(i=0, spec=city_report_specs; i<NUM_CREPORT_COLS; i++, spec++) {
    sprintf(text[i], "%*s\n%*s",
            spec->width, spec->title1, spec->width, spec->title2);
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
static gint city_change_mbutton_callback(GtkWidget *w, GdkEvent *event)
{
  GdkEventButton *bevent = (GdkEventButton *)event;
  
  if ( event->type != GDK_BUTTON_PRESS )
    return FALSE;

  gtk_menu_popup(GTK_MENU(city_popupmenu),
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

  gtk_window_set_title(GTK_WINDOW(city_dialog_shell),"City Report");

  report_title=get_report_title("City Advisor");
  city_label = gtk_label_new(report_title);
  free(report_title);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->vbox ),
        city_label, FALSE, FALSE, 0 );

  for (i=0;i<NUM_CREPORT_COLS;i++)
    titles[i]=buf[i];

  get_city_table_header(titles);

  city_list = gtk_clist_new_with_titles(NUM_CREPORT_COLS,titles);
  gtk_clist_column_titles_passive(GTK_CLIST(city_list));
  gtk_clist_set_auto_sort (GTK_CLIST (city_list), TRUE);
  scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), city_list);
  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrolled ),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
  gtk_widget_set_usize(city_list, 620, 250);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->vbox ),
        scrolled, TRUE, TRUE, 0 );

  for (i=0;i<NUM_CREPORT_COLS;i++)
    gtk_clist_set_column_auto_resize (GTK_CLIST (city_list), i, TRUE);

  close_command		= gtk_accelbutton_new("C_lose", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( close_command );

  city_center_command	= gtk_accelbutton_new("Cen_ter", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_center_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_center_command, GTK_CAN_DEFAULT );

  city_popup_command	= gtk_accelbutton_new("_Popup", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_popup_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_popup_command, GTK_CAN_DEFAULT );

  city_buy_command	= gtk_accelbutton_new("_Buy", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_buy_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_buy_command, GTK_CAN_DEFAULT );

  city_change_command	= gtk_accelbutton_new("_Change", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_change_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_change_command, GTK_CAN_DEFAULT );

  city_popupmenu=gtk_menu_new();

  gtk_signal_connect( GTK_OBJECT( city_change_command ), "event",
	GTK_SIGNAL_FUNC( city_change_mbutton_callback ), NULL );

  city_refresh_command	= gtk_accelbutton_new("_Refresh", accel);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(city_dialog_shell)->action_area ),
        city_refresh_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( city_refresh_command, GTK_CAN_DEFAULT );
  
  city_config_command	= gtk_accelbutton_new("Con_figure", accel);
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
  gtk_signal_connect(GTK_OBJECT(city_list), "select_row",
	GTK_SIGNAL_FUNC(city_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(city_list), "unselect_row",
	GTK_SIGNAL_FUNC(city_list_ucallback), NULL);

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
  struct city *pcity;

  if(row!=-1 && (pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row)))
  {
    int flag;
    size_t i;
    char buf[512];

    gtk_widget_set_sensitive(city_change_command, TRUE);
    gtk_widget_set_sensitive(city_center_command, TRUE);
    gtk_widget_set_sensitive(city_popup_command, TRUE);
    gtk_widget_set_sensitive(city_buy_command, TRUE);
    if (city_popupmenu)
      gtk_widget_destroy(city_popupmenu);

    city_popupmenu=gtk_menu_new();

    flag = 0;

    for(i=0; i<B_LAST; i++)
      if(can_build_improvement(pcity, i)) 
      {
        GtkWidget *item;

        sprintf(buf,"%s (%d)",get_imp_name_ex(pcity, i),get_improvement_type(i)->build_cost);
        item=gtk_menu_item_new_with_label( buf );
        gtk_menu_append(GTK_MENU(city_popupmenu),item);

        gtk_signal_connect(GTK_OBJECT(item),"activate",
            GTK_SIGNAL_FUNC(city_change_callback),
            (gpointer)i);
        flag=1;
      }

    for(i=0; i<U_LAST; i++)
      if(can_build_unit(pcity, i)) {
        GtkWidget *item;

        sprintf(buf,"%s (%d)", 
                get_unit_name(i),get_unit_type(i)->build_cost);
        item=gtk_menu_item_new_with_label( buf );
        gtk_menu_append(GTK_MENU(city_popupmenu),item);

        gtk_signal_connect(GTK_OBJECT(item),"activate",
            GTK_SIGNAL_FUNC(city_change_callback),
            (gpointer)(i+B_LAST));
        flag = 1;
      }

    gtk_widget_show_all(city_popupmenu);

    if(!flag)
      gtk_widget_set_sensitive(city_change_command, FALSE);
  }
  else
  {
    gtk_widget_set_sensitive(city_change_command, FALSE);
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
  }
}

void city_list_ucallback(GtkWidget *w, gint row, gint column)
{
      gtk_widget_set_sensitive(city_change_command, FALSE);
      gtk_widget_set_sensitive(city_center_command, FALSE);
      gtk_widget_set_sensitive(city_popup_command, FALSE);
      gtk_widget_set_sensitive(city_buy_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
void city_refresh_callback(GtkWidget *w, gpointer data)

{ /* added by Syela - I find this very useful */
  GList *selection;
  struct city *pcity;
  struct packet_generic_integer packet;

  if ( !( selection = GTK_CLIST( city_list )->selection ) )
  {
    packet.value = 0;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
  }
  else
  {
    gint row = (gint)selection->data;

    if(!(pcity = gtk_clist_get_row_data(GTK_CLIST(city_list),row)))
      return;

    packet.value = pcity->id;
    send_packet_generic_integer(&aconnection, PACKET_CITY_REFRESH, &packet);
  }
}

/****************************************************************
...
*****************************************************************/
void city_change_callback(GtkWidget *w, gpointer data)
{
  GList              *selection;
  gint                row;
  struct city *pcity;

  if ( !( selection = GTK_CLIST( city_list )->selection ) )
      return;

  row = (gint)selection->data;

  if((pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row)))
  {
      struct packet_city_request packet;
      int build_nr;
      int unit;
      
      build_nr = (size_t) data;

      if (build_nr >= B_LAST)
	{
	  build_nr -= B_LAST;
	  unit = 1;
	}
      else
	unit = 0;

      packet.city_id=pcity->id;
      packet.name[0]='\0';
      packet.build_id=build_nr;
      packet.is_build_id_unit_id=unit;
      send_packet_city_request(&aconnection, &packet, PACKET_CITY_CHANGE);
  }
}

/****************************************************************
...
*****************************************************************/
void city_buy_callback(GtkWidget *w, gpointer data)
{
  GList              *selection;
  gint                row;
  struct city *pcity;

  if ( !( selection = GTK_CLIST( city_list )->selection ) )
      return;

  row = (gint)selection->data;

  if((pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row)))
  {
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
	  sprintf(buf, "Game: %s costs %d gold and you only have %d gold.",
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
  GList              *selection;
  gint                row;
  struct city        *pcity;

  if ( !( selection = GTK_CLIST( city_list )->selection ) )
      return;

  row = (gint)selection->data;

  if((pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row)))
    center_tile_mapcanvas(pcity->x, pcity->y);
}

/****************************************************************
...
*****************************************************************/
void city_popup_callback(GtkWidget *w, gpointer data)
{
  GList              *selection;
  gint                row;
  struct city        *pcity;

  if ( !( selection = GTK_CLIST( city_list )->selection ) )
      return;

  row = (gint)selection->data;

  if((pcity=gtk_clist_get_row_data(GTK_CLIST(city_list),row))){
    center_tile_mapcanvas(pcity->x, pcity->y);
    popup_city_dialog(pcity, 0);
  }
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
  if(city_dialog_shell) {
    char *row	[NUM_CREPORT_COLS];
    char  buf	[NUM_CREPORT_COLS][64];
    int   i;
    char *report_title;
    struct city_report_spec *spec;

  if(delay_report_update) return;
  if(!city_dialog_shell) return;

    report_title=get_report_title("City Advisor");
    gtk_set_label(city_label, report_title);
    free(report_title);

    if (city_popupmenu)
      {
	gtk_widget_destroy(city_popupmenu);
	city_popupmenu = 0;
      }    

    for (i=0, spec=city_report_specs;i<NUM_CREPORT_COLS;i++, spec++)
    {
      row[i] = buf[i];
      gtk_clist_set_column_visibility(GTK_CLIST(city_list), i, spec->show);
    }

    gtk_clist_freeze(GTK_CLIST(city_list));
    gtk_clist_clear(GTK_CLIST(city_list));

    city_list_iterate(game.player_ptr->cities, pcity) {
      get_city_text(pcity,row);
      i=gtk_clist_append(GTK_CLIST(city_list), row);
      gtk_clist_set_row_data (GTK_CLIST(city_list), i, pcity);
    } city_list_iterate_end;
    gtk_clist_thaw(GTK_CLIST(city_list));
    gtk_widget_show_all(city_list);

    gtk_widget_set_sensitive(city_change_command, FALSE);
    gtk_widget_set_sensitive(city_center_command, FALSE);
    gtk_widget_set_sensitive(city_popup_command, FALSE);
    gtk_widget_set_sensitive(city_buy_command, FALSE);
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

    gtk_clist_get_text(GTK_CLIST(city_list),i,0,&text);

    get_city_text(pcity,row);

    gtk_clist_freeze(GTK_CLIST(city_list));
    gtk_clist_remove(GTK_CLIST(city_list),i);
    i=gtk_clist_append(GTK_CLIST(city_list),row);
    gtk_clist_set_row_data (GTK_CLIST(city_list), i, pcity);
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
  gtk_window_set_title (GTK_WINDOW(config_shell), "Configure City Report");
  gtk_window_set_position (GTK_WINDOW(config_shell), GTK_WIN_POS_MOUSE);

  config_label = gtk_label_new("Set columns shown");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), config_label,
			FALSE, FALSE, 0);

  for(i=1, spec=city_report_specs+i; i<NUM_CREPORT_COLS; i++, spec++) {
    config_toggle[i]=gtk_check_button_new_with_label(spec->explanation);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(config_shell)->vbox), config_toggle[i],
			FALSE, FALSE, 0);
  }

  config_ok_command = gtk_button_new_with_label("Close");
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
