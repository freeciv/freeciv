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
#include <ctype.h>

#include <gtk/gtk.h>


#include <player.h>
#include <packets.h>
#include <game.h>
#include <shared.h>
#include <help.h>
#include <dialogs.h>
#include <log.h>
#include <repodlgs.h>

#define MAX_CITIES_SHOWN 256

extern GtkWidget *toplevel, *map_canvas;

extern struct connection aconnection;
extern struct advance advances[];

extern int did_advance_tech_this_year;


void select_help_item_string( char *item );


void create_science_dialog(int make_modal);
void science_close_callback(GtkWidget *widget, gpointer data);
void science_help_callback(GtkWidget *widget, gpointer data);
void science_change_callback(GtkWidget *widget, gpointer data);
void science_goal_callback(GtkWidget *widget, gpointer data);

/******************************************************************/
GtkWidget *science_dialog_shell=NULL;
GtkWidget *science_label;
GtkWidget *science_current_label, *science_goal_label;
GtkWidget *science_change_menu_button, *science_goal_menu_button;
GtkWidget *science_list;
int science_dialog_shell_is_modal;
GtkWidget *popupmenu, *goalmenu;


/******************************************************************/
void create_city_report_dialog(int make_modal);
void city_close_callback(GtkWidget *widget, gpointer data);
void city_center_callback(GtkWidget *widget, gpointer data);
void city_popup_callback(GtkWidget *widget, gpointer data);
void city_buy_callback(GtkWidget *widget, gpointer data);
void city_change_callback(GtkWidget *widget, gpointer data);
void city_list_callback(GtkWidget *widget, gpointer data);

GtkWidget *city_dialog_shell=NULL;
GtkWidget *city_label;
GtkWidget *city_list, *city_list_label;
GtkWidget *city_center_command, *city_popup_command, *city_buy_command;
GtkWidget *city_change_command, *city_popupmenu;

int city_dialog_shell_is_modal;
int cities_in_list[MAX_CITIES_SHOWN];


/******************************************************************/
void create_trade_report_dialog(int make_modal);
void trade_close_callback(GtkWidget *widget, gpointer data);
void trade_selloff_callback(GtkWidget *widget, gpointer data);
void trade_list_callback(GtkWidget *widget, gpointer data);
int trade_improvement_type[B_LAST];

GtkWidget *trade_dialog_shell=NULL;
GtkWidget *trade_label, *trade_label2;
GtkWidget *trade_list, *trade_list_label;
GtkWidget *sellall_command, *sellobsolete_command;
int trade_dialog_shell_is_modal;

/******************************************************************/
void create_activeunits_report_dialog(int make_modal);
void activeunits_close_callback(GtkWidget *widget, gpointer data);
void activeunits_upgrade_callback(GtkWidget *widget, gpointer data);
void activeunits_list_callback(GtkWidget *widget, gpointer data);
int activeunits_type[U_LAST];

GtkWidget *activeunits_dialog_shell=NULL;
GtkWidget *activeunits_label, *activeunits_label2;
GtkWidget *activeunits_list, *activeunits_list_label;
GtkWidget *upgrade_command;

int activeunits_dialog_shell_is_modal;

/******************************************************************
...
*******************************************************************/
void update_report_dialogs(void)
{
/*
  activeunits_report_dialog_update();
  trade_report_dialog_update();
  city_report_dialog_update(); 
*/
  science_dialog_update();
}

/****************************************************************
...
****************************************************************/
char *get_report_title(char *report_name)
{
  char buf[512];
  
  sprintf(buf, "%s\n%s of the %s\n%s %s: %s",
	  report_name,
	  get_government_name(game.player_ptr->government),
	  get_race_name_plural(game.player_ptr->race),
	  get_ruler_title(game.player_ptr->government),
	  game.player_ptr->name,
	  textyear(game.year));

  return create_centered_string(buf);
}




























/****************************************************************
...
************************ ***************************************/
void popup_science_dialog(int make_modal)
{

  if(!science_dialog_shell) {
    science_dialog_shell_is_modal=make_modal;
    
    if(make_modal)
      gtk_widget_set_sensitive(toplevel, FALSE);
    
    create_science_dialog(make_modal);

    gtk_widget_show( science_dialog_shell );
  }

}


/****************************************************************
...
*****************************************************************/
void create_science_dialog(int make_modal)
{
  GtkWidget *close_command;
  static char *row	[1];
  int i, j, flag, k, t=0;
  char current_text[512];
  char goal_text[512];
  char buf[64];
  GSList *group;
  char *report_title;
  GtkWidget *frame, *hbox;
  
  if (game.player_ptr->research.researching!=A_NONE)
    sprintf(current_text, "%d/%d",
           game.player_ptr->research.researched,
           research_time(game.player_ptr));
  else
    sprintf(current_text, "%d/%d",
           game.player_ptr->research.researched,
           research_time(game.player_ptr));

  sprintf(goal_text, "(%d steps)",
          tech_goal_turns(game.player_ptr, game.player_ptr->ai.tech_goal));
  
  science_list = gtk_clist_new( 1 );

  gtk_clist_set_policy( GTK_CLIST( science_list ),
  			  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  for(i=1, j=0; i<A_LAST; i++)
    if(get_invention(game.player_ptr, i)==TECH_KNOWN) {
      row[0] = advances[i].name;

      gtk_clist_append( GTK_CLIST( science_list ), row );

      j++;
    }
  
  science_dialog_shell = gtk_dialog_new();
  gtk_window_set_title( GTK_WINDOW(science_dialog_shell), "Science Report" );

  report_title=get_report_title("Science Advisor");

  science_label = gtk_label_new(report_title);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(science_dialog_shell)->vbox ),
        science_label, TRUE, TRUE, 0 );

  if (game.player_ptr->research.researching!=A_NONE)
    sprintf(buf, "Researching" );
  else
    sprintf(buf, "Researching Future Tech. %d",game.player_ptr->future_tech+1 );

  frame = gtk_frame_new(buf);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(science_dialog_shell)->vbox ),
        frame, TRUE, TRUE, 0 );

  hbox = gtk_hbox_new( TRUE, 5 );
  gtk_container_add(GTK_CONTAINER(frame),hbox);

  science_change_menu_button = gtk_option_menu_new();
  popupmenu= gtk_menu_new();

  group = NULL;

  science_current_label = gtk_label_new(current_text);

  for(i=1, k=0, flag=0; i<A_LAST; i++)
  {
    GtkWidget *item;

    if(get_invention(game.player_ptr, i)==TECH_REACHABLE)
    {
	item = gtk_radio_menu_item_new_with_label( group, advances[i].name );
	group = gtk_radio_menu_item_group( GTK_RADIO_MENU_ITEM( item ) );
	gtk_menu_append( GTK_MENU( popupmenu ), item );

	if ( i == game.player_ptr->research.researching )
	{    gtk_menu_item_activate( GTK_MENU_ITEM( item ) ); t=k;}

	gtk_signal_connect( GTK_OBJECT( item ), "activate",
	    GTK_SIGNAL_FUNC( science_change_callback ), (gpointer)i );

	flag=1;
	k++;
    }
  }
  gtk_option_menu_set_menu( GTK_OPTION_MENU( science_change_menu_button ),
				popupmenu );

  if (flag)
    gtk_option_menu_set_history( GTK_OPTION_MENU( science_change_menu_button ), t );

  gtk_box_pack_start( GTK_BOX( hbox ), science_change_menu_button,TRUE, TRUE, 0 );

  if(!flag)
    gtk_widget_set_sensitive(science_change_menu_button, FALSE);

  gtk_box_pack_start( GTK_BOX( hbox ), science_current_label,FALSE, FALSE, 0 );
  gtk_widget_set_usize(science_current_label, 0,25);




  frame = gtk_frame_new( "Goal");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(science_dialog_shell)->vbox ),
        frame, TRUE, TRUE, 0 );

  hbox = gtk_hbox_new( TRUE, 5 );
  gtk_container_add(GTK_CONTAINER(frame),hbox);

  science_goal_menu_button = gtk_option_menu_new();
  goalmenu= gtk_menu_new();

  group = NULL;

  science_goal_label = gtk_label_new(goal_text);

  for(i=1, k=0, flag=0; i<A_LAST; i++)
  {
	GtkWidget *item;

    if(get_invention(game.player_ptr, i) != TECH_KNOWN &&
       advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
       tech_goal_turns(game.player_ptr, i) < 11)
    {
	item = gtk_radio_menu_item_new_with_label( group, advances[i].name );
	group = gtk_radio_menu_item_group( GTK_RADIO_MENU_ITEM( item ) );
	gtk_menu_append( GTK_MENU( goalmenu ), item );

	if ( i == game.player_ptr->ai.tech_goal )
	{    gtk_menu_item_activate( GTK_MENU_ITEM( item ) ); t=k;}

	gtk_signal_connect( GTK_OBJECT( item ), "activate",
	    GTK_SIGNAL_FUNC( science_goal_callback ), (gpointer)i );

	flag=1;
	k++;
    }
  }
  gtk_option_menu_set_menu( GTK_OPTION_MENU( science_goal_menu_button ),
				goalmenu );

  if (flag)
    gtk_option_menu_set_history( GTK_OPTION_MENU( science_goal_menu_button ), t );

  gtk_box_pack_start( GTK_BOX( hbox ), science_goal_menu_button,TRUE, TRUE, 0 );

  if(!flag)
    gtk_widget_set_sensitive(science_goal_menu_button, FALSE);

  gtk_box_pack_start( GTK_BOX( hbox ), science_goal_label, FALSE, FALSE, 0 );
  gtk_widget_set_usize(science_goal_label, 0,25);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(science_dialog_shell)->vbox ),
		science_list, FALSE, FALSE, 0 );

  close_command = gtk_button_new_with_label("Close");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(science_dialog_shell)->action_area ),
        close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( close_command );


  gtk_signal_connect( GTK_OBJECT( close_command ), "clicked",
        GTK_SIGNAL_FUNC( science_close_callback ), NULL);
  

/*
  XtAddCallback(science_list, XtNcallback, science_help_callback, NULL);
*/

  gtk_widget_set_usize(science_label, 500,0);
  
  gtk_widget_show_all( GTK_DIALOG(science_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(science_dialog_shell)->action_area );
}

/****************************************************************
...
*****************************************************************/
void science_change_callback( GtkWidget *widget, gpointer data )
{
  char current_text[512];
  struct packet_player_request packet;
  int to;
  int b=FALSE;

  to=(int)data;

  if (b == TRUE)
    select_help_item_string(advances[to].name);
  else
    {  
      sprintf(current_text, "%d/%d",
	      game.player_ptr->research.researched, 
	      research_time(game.player_ptr));
  
      gtk_label_set(GTK_LABEL(science_current_label),current_text);
  
      packet.tech=to;
      send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RESEARCH);
    }
}

/****************************************************************
...
*****************************************************************/
void science_goal_callback(GtkWidget *widget, gpointer data)
{
  char goal_text[512];
  struct packet_player_request packet;
  int to;
  int b=FALSE;

  to=(int)data;

  if (b == TRUE)
     select_help_item_string(advances[to].name);
  else {  
    sprintf(goal_text, "(%d steps)",
	    tech_goal_turns(game.player_ptr, to));

    gtk_label_set(GTK_LABEL(science_goal_label),goal_text);

    packet.tech=to;
    send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_TECH_GOAL);
  }
}

/****************************************************************
...
*****************************************************************/
void science_close_callback(GtkWidget *widget, gpointer data)
{

  if(science_dialog_shell_is_modal)
    gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(science_dialog_shell);
  science_dialog_shell=NULL;
}

#if 0
/****************************************************************
...
*****************************************************************/

void science_help_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(science_list);
  Boolean b;

  XtVaGetValues(science_help_toggle, XtNstate, &b, NULL);
  if (b == TRUE)
    {
      if(ret->list_index!=XAW_LIST_NONE)
	popup_help_dialog_string(ret->string);
      else
	popup_help_dialog_string("Technology research");
    }
}
#endif

/****************************************************************
...
*****************************************************************/
void science_dialog_update(void)
{
  if(science_dialog_shell) {
  char current_text[512];
  char goal_text[512];
    int i, j, flag, k, t=0;
    char *report_title;
    GSList *group;
    static char *row	[1];
    
    gtk_widget_hide( science_dialog_shell );

    report_title=get_report_title("Science Advisor");
    gtk_label_set(GTK_LABEL(science_label), report_title);

    
  if (game.player_ptr->research.researching!=A_NONE)
    sprintf(current_text, "%d/%d",
           game.player_ptr->research.researched,
           research_time(game.player_ptr));
  else
    sprintf(current_text, "%d/%d",
           game.player_ptr->research.researched,
           research_time(game.player_ptr));

  sprintf(goal_text, "(%d steps)",
          tech_goal_turns(game.player_ptr, game.player_ptr->ai.tech_goal));
  
  gtk_clist_freeze( GTK_CLIST(science_list) );

  gtk_clist_clear( GTK_CLIST(science_list) );

  for(i=1, j=0; i<A_LAST; i++)
    if(get_invention(game.player_ptr, i)==TECH_KNOWN) {
      row[0] = advances[i].name;

      gtk_clist_append( GTK_CLIST( science_list ), row );

      j++;
    }
  gtk_clist_thaw( GTK_CLIST(science_list) );

  gtk_option_menu_remove_menu(GTK_OPTION_MENU(science_change_menu_button));
    
  popupmenu= gtk_menu_new();

  group = NULL;

  gtk_label_set(GTK_LABEL(science_current_label),current_text);

  for(i=1, k=0, flag=0; i<A_LAST; i++)
  {
    GtkWidget *item;

    if(get_invention(game.player_ptr, i)==TECH_REACHABLE)
    {
	item = gtk_radio_menu_item_new_with_label( group, advances[i].name );
	group = gtk_radio_menu_item_group( GTK_RADIO_MENU_ITEM( item ) );
	gtk_menu_append( GTK_MENU( popupmenu ), item );

	if ( i == game.player_ptr->research.researching )
	{    gtk_menu_item_activate( GTK_MENU_ITEM( item ) ); t=k;}

	gtk_signal_connect( GTK_OBJECT( item ), "activate",
	    GTK_SIGNAL_FUNC( science_change_callback ), (gpointer)i );

	flag=1;
	k++;
    }
  }
  gtk_option_menu_set_menu( GTK_OPTION_MENU( science_change_menu_button ),
				popupmenu );

  if (flag)
    gtk_option_menu_set_history( GTK_OPTION_MENU( science_change_menu_button ), t );

  if(!flag)
    gtk_widget_set_sensitive(science_change_menu_button, FALSE);

  gtk_option_menu_remove_menu(GTK_OPTION_MENU(science_goal_menu_button));

  goalmenu= gtk_menu_new();

  group = NULL;

  gtk_label_set(GTK_LABEL(science_goal_label),goal_text);

  for(i=1, k=0, flag=0; i<A_LAST; i++)
  {
	GtkWidget *item;

    if(get_invention(game.player_ptr, i) != TECH_KNOWN &&
       advances[i].req[0] != A_LAST && advances[i].req[1] != A_LAST &&
       tech_goal_turns(game.player_ptr, i) < 11)
    {
	item = gtk_radio_menu_item_new_with_label( group, advances[i].name );
	group = gtk_radio_menu_item_group( GTK_RADIO_MENU_ITEM( item ) );
	gtk_menu_append( GTK_MENU( goalmenu ), item );

	if ( i == game.player_ptr->ai.tech_goal )
	{    gtk_menu_item_activate( GTK_MENU_ITEM( item ) ); t=k;}

	gtk_signal_connect( GTK_OBJECT( item ), "activate",
	    GTK_SIGNAL_FUNC( science_goal_callback ), (gpointer)i );

	flag=1;
	k++;
    }
  }
  gtk_option_menu_set_menu( GTK_OPTION_MENU( science_goal_menu_button ),
				goalmenu );

  if (flag)
    gtk_option_menu_set_history( GTK_OPTION_MENU( science_goal_menu_button ), t );

  if(!flag)
    gtk_widget_set_sensitive(science_goal_menu_button, FALSE);

  gtk_widget_show_all( GTK_DIALOG(science_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(science_dialog_shell)->action_area );

  gtk_widget_show( science_dialog_shell );
  }
  
}

#if 0
/****************************************************************

                      CITY REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_city_report_dialog(int make_modal)
{
  if(!city_dialog_shell) {
      Position x, y;
      Dimension width, height;
      
      city_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	XtSetSensitive(main_form, FALSE);
      
      create_city_report_dialog(make_modal);
      
      XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
      
      XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
			&x, &y);
      XtVaSetValues(city_dialog_shell, XtNx, x, XtNy, y, NULL);
      
      XtPopup(city_dialog_shell, XtGrabNone);
   }
}


/****************************************************************
...
*****************************************************************/
void create_city_report_dialog(int make_modal)
{
  Widget city_form;
  Widget close_command;
  char *report_title;
  
  city_dialog_shell = XtVaCreatePopupShell("reportcitypopup", 
					      make_modal ? 
					      transientShellWidgetClass :
					      topLevelShellWidgetClass,
					      toplevel, 
					      0);

  city_form = XtVaCreateManagedWidget("reportcityform", 
					 formWidgetClass,
					 city_dialog_shell,
					 NULL);   

  report_title=get_report_title("City Advisor");
  city_label = XtVaCreateManagedWidget("reportcitylabel", 
				       labelWidgetClass, 
				       city_form,
				       XtNlabel, 
				       report_title,
				       NULL);
  free(report_title);

  city_list_label = XtVaCreateManagedWidget("reportcitylistlabel", 
				       labelWidgetClass, 
				       city_form,
				       NULL);
  
  city_list = XtVaCreateManagedWidget("reportcitylist", 
				      listWidgetClass,
				      city_form,
				      NULL);

  close_command = XtVaCreateManagedWidget("reportcityclosecommand", 
					  commandWidgetClass,
					  city_form,
					  NULL);
  
  city_center_command = XtVaCreateManagedWidget("reportcitycentercommand", 
						commandWidgetClass,
						city_form,
						NULL);

  city_popup_command = XtVaCreateManagedWidget("reportcitypopupcommand", 
					       commandWidgetClass,
					       city_form,
					       NULL);

  city_popupmenu = 0;

  city_buy_command = XtVaCreateManagedWidget("reportcitybuycommand", 
					     commandWidgetClass,
					     city_form,
					     NULL);

  city_change_command = XtVaCreateManagedWidget("reportcitychangemenubutton", 
						menuButtonWidgetClass,
						city_form,
						NULL);

  XtAddCallback(close_command, XtNcallback, city_close_callback, NULL);
  XtAddCallback(city_center_command, XtNcallback, city_center_callback, NULL);
  XtAddCallback(city_popup_command, XtNcallback, city_popup_callback, NULL);
  XtAddCallback(city_buy_command, XtNcallback, city_buy_callback, NULL);
  XtAddCallback(city_list, XtNcallback, city_list_callback, NULL);
  
  XtRealizeWidget(city_dialog_shell);
  city_report_dialog_update();
}

/****************************************************************
...
*****************************************************************/
void city_list_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);
  struct city *pcity;

  if(ret->list_index!=XAW_LIST_NONE && (pcity=find_city_by_id(cities_in_list[ret->list_index])))
    {
      int flag,i;
      char buf[512];

      XtSetSensitive(city_change_command, TRUE);
      XtSetSensitive(city_center_command, TRUE);
      XtSetSensitive(city_popup_command, TRUE);
      XtSetSensitive(city_buy_command, TRUE);
      if (city_popupmenu)
	XtDestroyWidget(city_popupmenu);

      city_popupmenu=XtVaCreatePopupShell("menu", 
				     simpleMenuWidgetClass, 
				     city_change_command,
				     NULL);
      flag = 0;
      for(i=0; i<B_LAST; i++)
	if(can_build_improvement(pcity, i)) 
	  {
	    Widget entry;
	    sprintf(buf,"%s (%d)", get_imp_name_ex(pcity, i),get_improvement_type(i)->build_cost);
	    entry = XtVaCreateManagedWidget(buf, smeBSBObjectClass, city_popupmenu, NULL);
	    XtAddCallback(entry, XtNcallback, city_change_callback, (XtPointer) i);
	    flag=1;
	  }

      for(i=0; i<U_LAST; i++)
	if(can_build_unit(pcity, i)) {
	    Widget entry;
	    sprintf(buf,"%s (%d)", 
		    get_unit_name(i),get_unit_type(i)->build_cost);
	    entry = XtVaCreateManagedWidget(buf, smeBSBObjectClass, 
					    city_popupmenu, NULL);
	    XtAddCallback(entry, XtNcallback, city_change_callback, 
			  (XtPointer) (i+B_LAST));
	    flag = 1;
	}

      if(!flag)
	XtSetSensitive(city_change_command, FALSE);

    }
  else
    {
      XtSetSensitive(city_change_command, FALSE);
      XtSetSensitive(city_center_command, FALSE);
      XtSetSensitive(city_popup_command, FALSE);
      XtSetSensitive(city_buy_command, FALSE);
    }
}


/****************************************************************
...
*****************************************************************/
void city_change_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);
  struct city *pcity;



  if(ret->list_index!=XAW_LIST_NONE && (pcity=find_city_by_id(cities_in_list[ret->list_index])))
    {
      struct packet_city_request packet;
      int build_nr;
      Boolean unit;
      
      build_nr = (int) client_data;

      if (build_nr >= B_LAST)
	{
	  build_nr -= B_LAST;
	  unit = TRUE;
	}
      else
	unit = FALSE;

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
void city_buy_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=find_city_by_id(cities_in_list[ret->list_index]))) {
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
}

/****************************************************************
...
*****************************************************************/
void city_close_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{

  if(city_dialog_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
   XtDestroyWidget(city_dialog_shell);
   city_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void city_center_callback(Widget w, XtPointer client_data, 
			  XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=find_city_by_id(cities_in_list[ret->list_index])))
      center_tile_mapcanvas(pcity->x, pcity->y);
  }
}

/****************************************************************
...
*****************************************************************/
void city_popup_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret=XawListShowCurrent(city_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    struct city *pcity;
    if((pcity=find_city_by_id(cities_in_list[ret->list_index]))) {
      center_tile_mapcanvas(pcity->x, pcity->y);
      popup_city_dialog(pcity, 0);
    }
  }
}


/****************************************************************
...
*****************************************************************/
void city_report_dialog_update(void)
{
  if(city_dialog_shell) {
    int i;
    Dimension width; 
    static char *city_list_names_ptrs[MAX_CITIES_SHOWN+1];
    static char city_list_names[MAX_CITIES_SHOWN][200];
    struct genlist_iterator myiter;
    char *report_title;
    char happytext[32];
    char statetext[32];
    char foodtext[32];
    report_title=get_report_title("City Advisor");
    xaw_set_label(city_label, report_title);
    free(report_title);
    if (city_popupmenu)
      {
	XtDestroyWidget(city_popupmenu);
	city_popupmenu = 0;
      }    
    genlist_iterator_init(&myiter, &game.player_ptr->cities.list, 0);
    
     for(i=0; ITERATOR_PTR(myiter) && i<MAX_CITIES_SHOWN; 
	 i++, ITERATOR_NEXT(myiter)) {
       char impro[64];
       struct city *pcity=(struct city *)ITERATOR_PTR(myiter);

       if(pcity->is_building_unit)
	 sprintf(impro, "%s(%d/%d/%d)", 
		 get_unit_type(pcity->currently_building)->name,
		 pcity->shield_stock,
		 get_unit_type(pcity->currently_building)->build_cost,
		 city_buy_cost(pcity));
       else
	 sprintf(impro, "%s(%d/%d/%d)", 
		 get_imp_name_ex(pcity, pcity->currently_building),
		 pcity->shield_stock,
		 get_improvement_type(pcity->currently_building)->build_cost,
		 city_buy_cost(pcity));
              
       sprintf(happytext, "%s(%d/%d/%d)",
	       city_unhappy(pcity) ? "Disorder" : "Peace",
	       pcity->ppl_happy[4],
	       pcity->ppl_content[4],
	       pcity->ppl_unhappy[4]);
 
       sprintf(statetext, "(%d/%d/%d)",
	       pcity->food_surplus, 
	       pcity->shield_surplus, 
	       pcity->trade_prod);

       sprintf(foodtext,"(%d/%d)",
               pcity->food_stock,
	       pcity->size * game.foodbox);

       sprintf(city_list_names[i], "%-15s %-16s%-12s%-10s%s", 
	       pcity->name,
	       happytext,
	       statetext,
	       foodtext,
	       impro);
       city_list_names_ptrs[i]=city_list_names[i];
       cities_in_list[i]=pcity->id;
     }
    if(i==0) {
      strcpy(city_list_names[0], 
	     "                                                             ");
      city_list_names_ptrs[0]=city_list_names[0];
      i=1;
      cities_in_list[0]=0;
    }
    city_list_names_ptrs[i]=0;

    XawListChange(city_list, city_list_names_ptrs, 0, 0, 1);

    XtVaGetValues(city_list, XtNwidth, &width, NULL);
    XtVaSetValues(city_list_label, XtNwidth, width, NULL); 
    XtSetSensitive(city_change_command, FALSE);
    XtSetSensitive(city_center_command, FALSE);
    XtSetSensitive(city_popup_command, FALSE);
    XtSetSensitive(city_buy_command, FALSE);
  }
  
}

/****************************************************************

                      TRADE REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_trade_report_dialog(int make_modal)
{
  if(!trade_dialog_shell) {
      Position x, y;
      Dimension width, height;
      
      trade_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	XtSetSensitive(main_form, FALSE);
      
      create_trade_report_dialog(make_modal);
      
      XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
      
      XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
			&x, &y);
      XtVaSetValues(trade_dialog_shell, XtNx, x, XtNy, y, NULL);
      
      XtPopup(trade_dialog_shell, XtGrabNone);
   }
}


/****************************************************************
...
*****************************************************************/
void create_trade_report_dialog(int make_modal)
{
  Widget trade_form;
  Widget close_command;
  char *report_title;
  
  trade_dialog_shell = XtVaCreatePopupShell("reporttradepopup", 
					      make_modal ? 
					      transientShellWidgetClass :
					      topLevelShellWidgetClass,
					      toplevel, 
					      0);

  trade_form = XtVaCreateManagedWidget("reporttradeform", 
					 formWidgetClass,
					 trade_dialog_shell,
					 NULL);   

  report_title=get_report_title("Trade Advisor");
  trade_label = XtVaCreateManagedWidget("reporttradelabel", 
				       labelWidgetClass, 
				       trade_form,
				       XtNlabel, 
				       report_title,
				       NULL);
  free(report_title);

  trade_list_label = XtVaCreateManagedWidget("reporttradelistlabel", 
				       labelWidgetClass, 
				       trade_form,
				       NULL);
  
  trade_list = XtVaCreateManagedWidget("reporttradelist", 
				      listWidgetClass,
				      trade_form,
				      NULL);

  trade_label2 = XtVaCreateManagedWidget("reporttradelabel2", 
				       labelWidgetClass, 
				       trade_form,					 			      XtNlabel, 
				       "Total Cost:", 
				       NULL);

  close_command = XtVaCreateManagedWidget("reporttradeclosecommand", 
					  commandWidgetClass,
					  trade_form,
					  NULL);

  sellobsolete_command = XtVaCreateManagedWidget("reporttradesellobsoletecommand", 
					         commandWidgetClass,
					         trade_form,
					         XtNsensitive, False,
					         NULL);

  sellall_command  = XtVaCreateManagedWidget("reporttradesellallcommand", 
					     commandWidgetClass,
					     trade_form,
					     XtNsensitive, False,
					     NULL);
  XtAddCallback(trade_list, XtNcallback, trade_list_callback, NULL);
  XtAddCallback(close_command, XtNcallback, trade_close_callback, NULL);
  XtAddCallback(sellobsolete_command, XtNcallback, trade_selloff_callback, (XtPointer)0);
  XtAddCallback(sellall_command, XtNcallback, trade_selloff_callback, (XtPointer)1);
  XtRealizeWidget(trade_dialog_shell);
  trade_report_dialog_update();
}



/****************************************************************
...
*****************************************************************/
void trade_list_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret;
  int i;
  ret=XawListShowCurrent(trade_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    i=trade_improvement_type[ret->list_index];
    if(i>=0 && i<B_LAST && !is_wonder(i))
      XtSetSensitive(sellobsolete_command, TRUE);
      XtSetSensitive(sellall_command, TRUE);
    return;
  }
  XtSetSensitive(sellobsolete_command, FALSE);
  XtSetSensitive(sellall_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
void trade_close_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{

  if(trade_dialog_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
  XtDestroyWidget(trade_dialog_shell);
  trade_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void trade_selloff_callback(Widget w, XtPointer client_data, 
			    XtPointer call_data)
{
  int i,count=0,gold=0;
  struct genlist_iterator myiter;
  struct city *pcity;
  struct packet_city_request packet;
  char str[64];
  XawListReturnStruct *ret=XawListShowCurrent(trade_list);

  if(ret->list_index==XAW_LIST_NONE) return;

  i=trade_improvement_type[ret->list_index];

  genlist_iterator_init(&myiter, &game.player_ptr->cities.list, 0);
  for(; ITERATOR_PTR(myiter);ITERATOR_NEXT(myiter)) {
    pcity=(struct city *)ITERATOR_PTR(myiter);
    if(city_got_building(pcity, i) &&
       (client_data ||
	improvement_obsolete(game.player_ptr,i) ||
        wonder_replacement(pcity, i) ))  {
	count++; gold+=improvement_value(i);
        packet.city_id=pcity->id;
        packet.build_id=i;
        packet.name[0]='\0';
        send_packet_city_request(&aconnection, &packet, PACKET_CITY_SELL);
    };
  };
  if(count)  {
    sprintf(str,"Sold %d %s for %d gold",count,get_improvement_name(i),gold);
  } else {
    sprintf(str,"No %s obsolete",get_improvement_name(i));
  };
  popup_notify_dialog("Sell-Off Results",str);
  return;
}

/****************************************************************
...
*****************************************************************/
void trade_report_dialog_update(void)
{
  if(trade_dialog_shell) {
    int j, k, count, cost, total;
    Dimension width; 
    static char *trade_list_names_ptrs[B_LAST+1];
    static char trade_list_names[B_LAST][200];
    struct genlist_iterator myiter;
    char *report_title;
    char trade_total[100];
    struct city *pcity;
    
    report_title=get_report_title("Trade Advisor");
    xaw_set_label(trade_label, report_title);
    free(report_title);
    total = 0;
    k = 0;
    for (j=0;j<B_LAST;j++) {
      count = 0;
      pcity = NULL;
      genlist_iterator_init(&myiter, &game.player_ptr->cities.list, 0);
      for(; ITERATOR_PTR(myiter);ITERATOR_NEXT(myiter)) {
	pcity=(struct city *)ITERATOR_PTR(myiter);
	if (city_got_building(pcity, j)) count++;
      }
      if (pcity) {
	cost = count * improvement_upkeep(pcity, j);
	if (cost) {
	  sprintf(trade_list_names[k], "%-20s%5d%5d%6d", get_improvement_name(j), count, improvement_upkeep(pcity, j), cost);
	  total+=cost;
	  trade_list_names_ptrs[k]=trade_list_names[k];
	  trade_improvement_type[k]=j;
	  k++;
	}
      }
    }
    
    if(k==0) {
      strcpy(trade_list_names[0], "                                          ");
      trade_list_names_ptrs[0]=trade_list_names[0];
      k=1;
    }

    sprintf(trade_total, "Income:%6d    Total Costs: %6d", total+turn_gold_difference, total); 
     xaw_set_label(trade_label2, trade_total); 
    trade_list_names_ptrs[k]=0;
    
    XawListChange(trade_list, trade_list_names_ptrs, 0, 0, 1);

    XtVaGetValues(trade_list, XtNwidth, &width, NULL);
    XtVaSetValues(trade_list_label, XtNwidth, width, NULL); 

    XtVaSetValues(trade_label2, XtNwidth, width, NULL); 

    XtVaSetValues(trade_label, XtNwidth, width, NULL); 

  }
  
}

/****************************************************************

                      ACTIVE UNITS REPORT DIALOG
 
****************************************************************/

/****************************************************************
...
****************************************************************/
void popup_activeunits_report_dialog(int make_modal)
{
  if(!activeunits_dialog_shell) {
      Position x, y;
      Dimension width, height;
      
      activeunits_dialog_shell_is_modal=make_modal;
    
      if(make_modal)
	XtSetSensitive(main_form, FALSE);
      
      create_activeunits_report_dialog(make_modal);
      
      XtVaGetValues(toplevel, XtNwidth, &width, XtNheight, &height, NULL);
      
      XtTranslateCoords(toplevel, (Position) width/10, (Position) height/10,
			&x, &y);
      XtVaSetValues(activeunits_dialog_shell, XtNx, x, XtNy, y, NULL);
      
      XtPopup(activeunits_dialog_shell, XtGrabNone);
   }
}


/****************************************************************
...
*****************************************************************/
void create_activeunits_report_dialog(int make_modal)
{
  Widget activeunits_form;
  Widget close_command;
  char *report_title;
  
  activeunits_dialog_shell = XtVaCreatePopupShell("reportactiveunitspopup", 
					      make_modal ? 
					      transientShellWidgetClass :
					      topLevelShellWidgetClass,
					      toplevel, 
					      0);

  activeunits_form = XtVaCreateManagedWidget("reportactiveunitsform", 
					 formWidgetClass,
					 activeunits_dialog_shell,
					 NULL);   

  report_title=get_report_title("Active Units");
  activeunits_label = XtVaCreateManagedWidget("reportactiveunitslabel", 
				       labelWidgetClass, 
				       activeunits_form,
				       XtNlabel, 
				       report_title,
				       NULL);
  free(report_title);

  activeunits_list_label = XtVaCreateManagedWidget("reportactiveunitslistlabel", 
				       labelWidgetClass, 
				       activeunits_form,
				       NULL);
  
  activeunits_list = XtVaCreateManagedWidget("reportactiveunitslist", 
				      listWidgetClass,
				      activeunits_form,
				      NULL);

  activeunits_label2 = XtVaCreateManagedWidget("reportactiveunitslabel2", 
				       labelWidgetClass, 
				       activeunits_form,
                                       XtNlabel, 
				       "Total Cost:", 
				       NULL);

  close_command = XtVaCreateManagedWidget("reportactiveunitsclosecommand", 
					  commandWidgetClass,
					  activeunits_form,
					  NULL);

  upgrade_command = XtVaCreateManagedWidget("reportactiveunitsupgradecommand", 
					  commandWidgetClass,
					  activeunits_form,
					  XtNsensitive, False,
					  NULL);
  XtAddCallback(activeunits_list, XtNcallback, activeunits_list_callback, NULL);
  XtAddCallback(close_command, XtNcallback, activeunits_close_callback, NULL);
  XtAddCallback(upgrade_command, XtNcallback, activeunits_upgrade_callback, NULL);
  XtRealizeWidget(activeunits_dialog_shell);
  activeunits_report_dialog_update();
}

/****************************************************************
...
*****************************************************************/
void activeunits_list_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  XawListReturnStruct *ret;
  ret=XawListShowCurrent(activeunits_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    if (can_upgrade_unittype(game.player_ptr, activeunits_type[ret->list_index]) != -1) 
      XtSetSensitive(upgrade_command, TRUE);
    return;
  }
  XtSetSensitive(upgrade_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
void upgrade_callback_yes(Widget w, XtPointer client_data, 
                                 XtPointer call_data)
{
  send_packet_unittype_info(&aconnection, (int)client_data, PACKET_UNIT_UPGRADE);
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void upgrade_callback_no(Widget w, XtPointer client_data, 
                                XtPointer call_data)
{
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void activeunits_upgrade_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{
  char buf[512];
  int ut1,ut2;

  XawListReturnStruct *ret;
  ret=XawListShowCurrent(activeunits_list);

  if(ret->list_index!=XAW_LIST_NONE) {
    ut1 = activeunits_type[ret->list_index];
    puts(unit_types[ut1].name);
    
    ut2 = can_upgrade_unittype(game.player_ptr, activeunits_type[ret->list_index]);
    
    sprintf(buf, "upgrade as many %s to %s as possible for %d gold each?\nTreasure %d gold.", unit_types[ut1].name, unit_types[ut2].name, unit_upgrade_price(game.player_ptr, ut1, ut2), game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, "upgradedialog", buf,
			 upgrade_callback_yes, (XtPointer)(activeunits_type[ret->list_index]),
			 upgrade_callback_no, 0, 0);
  }
}

/****************************************************************
...
*****************************************************************/
void activeunits_close_callback(Widget w, XtPointer client_data, 
			 XtPointer call_data)
{

  if(activeunits_dialog_shell_is_modal)
     XtSetSensitive(main_form, TRUE);
   XtDestroyWidget(activeunits_dialog_shell);
   activeunits_dialog_shell=0;
}

/****************************************************************
...
*****************************************************************/
void activeunits_report_dialog_update(void)
{
  if(activeunits_dialog_shell) {
    int i, k, total;
    Dimension width; 
    static char *activeunits_list_names_ptrs[U_LAST+1];
    static char activeunits_list_names[U_LAST][200];
    int unit_count[U_LAST];
    char *report_title;
    char activeunits_total[100];
    
    report_title=get_report_title("Active Units");
    xaw_set_label(activeunits_label, report_title);
    free(report_title);
    for (i=0;i <U_LAST;i++) 
      unit_count[i]=0;
    unit_list_iterate(game.player_ptr->units, punit) 
      unit_count[punit->type]++;
    unit_list_iterate_end;
    k = 0;
    total = 0;
    for (i=0;i<U_LAST;i++) {
      if (unit_count[i] > 0) {
	sprintf(activeunits_list_names[k], "%-27s%c%5d", unit_name(i), can_upgrade_unittype(game.player_ptr, i) != -1 ? '*': '-', unit_count[i]);
	activeunits_list_names_ptrs[k]=activeunits_list_names[k];
	activeunits_type[k]=i;
	k++;
	total+=unit_count[i];
      }
    }
    if (k==0) {
      strcpy(activeunits_list_names[0], "                                ");
      activeunits_list_names_ptrs[0]=activeunits_list_names[0];
      k=1;
    }

    sprintf(activeunits_total, "Total units:%21d",total); 

    xaw_set_label(activeunits_label2, activeunits_total); 
    activeunits_list_names_ptrs[k]=0;
    
    XawListChange(activeunits_list, activeunits_list_names_ptrs, 0, 0, 1);

    XtVaGetValues(activeunits_list, XtNwidth, &width, NULL);
    XtVaSetValues(activeunits_list_label, XtNwidth, width, NULL); 

    XtVaSetValues(activeunits_label2, XtNwidth, width, NULL); 

    XtVaSetValues(activeunits_label, XtNwidth, width, NULL); 
  }
}
#endif
