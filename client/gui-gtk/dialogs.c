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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <dialogs.h>
#include <player.h>
#include <packets.h>
#include <chatline.h>
#include <map.h>
#include <mapview.h>
#include <game.h>
#include <mapctrl.h>
#include <gui_stuff.h>
#include <civclient.h>
#include <mem.h>


extern GtkWidget *toplevel;
extern GdkWindow *root_window;
extern struct connection aconnection;
extern int flags_are_transparent;
extern GdkGC *fill_bg_gc;


/******************************************************************/
GtkWidget	*races_dialog_shell=NULL;
GtkWidget	*races_toggles_form;
GtkWidget	*races_ok_command;		/* ok button */
GtkWidget	*races_toggles		[14],	/* toggle race */
		*races_name;			/* leader name */
/******************************************************************/
GtkWidget *spy_tech_shell;
GtkWidget *spy_advances_list, *spy_advances_list_label;
GtkWidget *spy_steal_command;

int spy_tech_shell_is_modal;
int advance_type[A_LAST];
int steal_advance = 0;

/******************************************************************/
GtkWidget *spy_sabotage_shell;
GtkWidget *spy_improvements_list;
GtkWidget *spy_sabotage_command;

int spy_sabotage_shell_is_modal;
int improvement_type[B_LAST];
int sabotage_improvement = 0;

/******************************************************************/

GtkWidget *unit_select_dialog_shell;
GtkWidget *unit_select_commands[100];
GtkWidget *unit_select_labels[100];
int unit_select_ids[100];
int unit_select_no;


int races_buttons_get_current(void);

void create_races_dialog	(void);
void races_buttons_callback	( GtkWidget *w, gpointer data );
void races_toggles_callback	( GtkWidget *w, gpointer data );

int race_selected;


int is_showing_government_dialog;

int caravan_city_id;
int caravan_unit_id;

int diplomat_id;
int diplomat_target_id;

struct city *pcity_caravan_dest;
struct unit *punit_caravan;

extern GtkStyle *notify_dialog_style;


/****************************************************************
...
*****************************************************************/
void notify_command_callback(GtkWidget *w, GtkWidget *t)
{
  gtk_widget_destroy( t );
  gtk_widget_set_sensitive( toplevel, TRUE );
}

gint deleted_callback( GtkWidget *widget, GdkEvent *event, gpointer data )
{
  gtk_widget_set_sensitive( toplevel, TRUE );
  return FALSE;
}


/****************************************************************
...
*****************************************************************/
void popup_notify_dialog(char *headline, char *lines)
{
  GtkWidget *notify_dialog_shell, *notify_command;
  GtkWidget *notify_label, *notify_headline, *notify_scrolled;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  notify_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(notify_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(notify_dialog_shell));

  gtk_window_set_title( GTK_WINDOW( notify_dialog_shell ), "Notify dialog" );
  
  gtk_container_border_width( GTK_CONTAINER(GTK_DIALOG(notify_dialog_shell)->vbox), 5 );

  notify_headline = gtk_label_new( headline);   
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_headline, FALSE, FALSE, 0 );

  gtk_widget_set_style( notify_headline, notify_dialog_style );
  gtk_label_set_justify( GTK_LABEL( notify_headline ), GTK_JUSTIFY_LEFT );
  gtk_misc_set_alignment(GTK_MISC(notify_headline), 0.0, 0.0);

  notify_scrolled=gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notify_scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  notify_label = gtk_label_new( lines );  
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (notify_scrolled),
					notify_label);

  gtk_widget_set_style( notify_label, notify_dialog_style );
  gtk_label_set_justify( GTK_LABEL( notify_label ), GTK_JUSTIFY_LEFT );
  gtk_misc_set_alignment(GTK_MISC(notify_label), 0.0, 0.0);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_scrolled, TRUE, TRUE, 0 );


  notify_command = gtk_button_new_with_label( "Close" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( notify_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( notify_command );
  gtk_widget_add_accelerator(notify_command, "clicked",
	accel, GDK_Escape, 0, 0);

  gtk_signal_connect( GTK_OBJECT( notify_command ), "clicked",
	GTK_SIGNAL_FUNC( notify_command_callback ), notify_dialog_shell );

  
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->action_area );

  gtk_widget_set_usize(notify_dialog_shell, 0, 265);
  gtk_set_relative_position (toplevel, notify_dialog_shell, 10, 10);
  gtk_widget_show( notify_dialog_shell );

  gtk_widget_set_sensitive( toplevel, FALSE );
}

/****************************************************************
...
*****************************************************************/

struct widget_list {
  GtkWidget *w;
  int x,y;
  struct widget_list *next;
};
struct widget_list *notify_goto_widget_list = NULL;

void notify_goto_widget_remove(GtkWidget *w)
{
  struct widget_list *cur, *tmp;
  cur=notify_goto_widget_list;
  if (!cur)
    return;
  if (cur && cur->w == w) {
    cur = cur->next;
    free(notify_goto_widget_list);
    notify_goto_widget_list = cur;
    return;
  }
  for (; cur->next && cur->next->w!= w; cur=cur->next);
  if (cur->next) {
    tmp = cur->next;
    cur->next = cur->next->next;
    free(tmp);
  }
}
void notify_goto_find_widget(GtkWidget *w, int *x, int *y)
{
  struct widget_list *cur;
  *x=0;
  *y=0;
  for (cur = notify_goto_widget_list; cur && cur->w !=w; cur = cur->next);
  if (cur) {
    *x = cur->x;
    *y = cur->y;
  }
}


void notify_goto_add_widget_coords(GtkWidget *w, int  x, int y)
{
  struct widget_list *newwidget;
  newwidget = fc_malloc(sizeof(struct widget_list));
  newwidget->w = w;
  newwidget->x = x;
  newwidget->y = y;
  newwidget->next = notify_goto_widget_list;
  notify_goto_widget_list = newwidget;
}

void notify_goto_command_callback(GtkWidget *w, gpointer data)
{
  int x,y;
  notify_goto_find_widget(w, &x, &y);
  center_tile_mapcanvas(x, y);
  notify_goto_widget_remove(w);

  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(toplevel, TRUE);
}

void notify_no_goto_command_callback(GtkWidget *w, gpointer data)
{
  notify_goto_widget_remove(w);
  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(toplevel, TRUE);
}

gint notify_deleted_callback(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  notify_goto_widget_remove(widget);
  gtk_widget_set_sensitive(toplevel, TRUE);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
void popup_notify_goto_dialog(char *headline, char *lines,int x, int y)
{
  GtkWidget *notify_dialog_shell, *notify_command, *notify_goto_command;
  GtkWidget *notify_label;
  
  if (x == 0 && y == 0) {
    popup_notify_dialog(headline, lines);
    return;
  }
  notify_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(notify_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(notify_deleted_callback),NULL );

  gtk_window_set_title( GTK_WINDOW( notify_dialog_shell ), headline );

  notify_label=gtk_label_new(lines);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_label, TRUE, TRUE, 0 );

  notify_command = gtk_button_new_with_label("Close");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_command, TRUE, TRUE, 0 );

  notify_goto_command = gtk_button_new_with_label("Goto and Close");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_goto_command, TRUE, TRUE, 0 );
  
  gtk_signal_connect(GTK_OBJECT(notify_command), "clicked",
		GTK_SIGNAL_FUNC(notify_no_goto_command_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(notify_goto_command), "clicked",
		GTK_SIGNAL_FUNC(notify_goto_command_callback), NULL);
  notify_goto_add_widget_coords(notify_goto_command, x, y);

  gtk_set_relative_position(toplevel, notify_dialog_shell, 25, 25);

  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->action_area );
  gtk_widget_show(notify_dialog_shell);

  gtk_widget_set_sensitive(toplevel, FALSE);
}


/****************************************************************
...
*****************************************************************/
void diplomat_bribe_yes_callback(GtkWidget *w, gpointer data)
{
  struct packet_diplomat_action req;

  req.action_type=DIPLOMAT_BRIBE;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;

  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void diplomat_bribe_no_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}



/****************************************************************
...  Ask the server how much the bribe is
*****************************************************************/
void diplomat_bribe_callback(GtkWidget *w, gpointer data)
{
  struct packet_generic_integer packet;

  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     find_unit_by_id(diplomat_target_id)) { 
    packet.value = diplomat_target_id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
   }

}
 
/****************************************************************
...
*****************************************************************/
void popup_bribe_dialog(struct unit *punit)
{
  char buf[128];
  
  if(game.player_ptr->economic.gold>=punit->bribe_cost) {
    sprintf(buf, "Bribe unit for %d gold?\nTreasury contains %d gold.", 
	   punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, /*"diplomatbribedialog"*/"Bribe Enemy Unit", buf,
			"Yes", diplomat_bribe_yes_callback, 0,
			"No", diplomat_bribe_no_callback, 0, 0);
  } else {
    sprintf(buf, "Bribing the unit costs %d gold.\nTreasury contains %d gold.", 
	   punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(toplevel, /*"diplomatnogolddialog"*/"Traitors Demand Too Much!", buf,
			"Darn", diplomat_bribe_no_callback, 0, 
			0);
  }
}

/****************************************************************
...
*****************************************************************/
void diplomat_sabotage_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value = -1;

    send_packet_diplomat_action(&aconnection, &req);
  }

}
/****************************************************************
...
*****************************************************************/
void diplomat_investigate_callback(GtkWidget *w, gpointer data)
{
 
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_INVESTIGATE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}
/****************************************************************
...
*****************************************************************/
void spy_sabotage_unit_callback(GtkWidget *w, gpointer data)
{

  struct packet_diplomat_action req;
  
  req.action_type=SPY_SABOTAGE_UNIT;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;
  
  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void diplomat_embassy_callback(GtkWidget *w, gpointer data)
{
 
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_EMBASSY;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
void spy_poison_callback(GtkWidget *w, gpointer data)
{

  destroy_message_dialog(w);

  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    struct packet_diplomat_action req;

    req.action_type=SPY_POISON;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
void diplomat_steal_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_STEAL;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value=0;
    
    send_packet_diplomat_action(&aconnection, &req);
  }

}

/****************************************************************
...
*****************************************************************/
void spy_close_tech_callback(GtkWidget *w, gpointer data)
{

  if(spy_tech_shell_is_modal)
     gtk_widget_set_sensitive(toplevel, TRUE);
   gtk_widget_destroy(spy_tech_shell);
   spy_tech_shell=0;
}

/****************************************************************
...
*****************************************************************/
void spy_close_sabotage_callback(GtkWidget *w, gpointer data)
{

  if(spy_sabotage_shell_is_modal)
     gtk_widget_set_sensitive(toplevel, TRUE);
   gtk_widget_destroy(spy_sabotage_shell);
   spy_sabotage_shell=0;
}

/****************************************************************
...
*****************************************************************/
void spy_select_tech_callback(GtkWidget *w, gint row, gint column)
{
  if (advance_type[row] != -1){
    steal_advance = advance_type[row];
    gtk_widget_set_sensitive(spy_steal_command, TRUE);
    return;
  }
}

/****************************************************************
...
*****************************************************************/
void spy_uselect_tech_callback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(spy_steal_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
void spy_select_improvement_callback(GtkWidget *w, gint row, gint column)
{
  sabotage_improvement = improvement_type[row];
  gtk_widget_set_sensitive(spy_sabotage_command, TRUE);
}

/****************************************************************
...
*****************************************************************/
void spy_uselect_improvement_callback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(spy_sabotage_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
void spy_steal_callback(GtkWidget *w, gpointer data)
{  
  gtk_widget_destroy(spy_tech_shell);
  spy_tech_shell = 0l;
  
  if(!steal_advance){
    printf("Bug in spy steal tech code\n");
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_STEAL;
    req.value=steal_advance;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
...
*****************************************************************/
void spy_sabotage_callback(GtkWidget *w, gpointer data)
{  
  gtk_widget_destroy(spy_sabotage_shell);
  spy_sabotage_shell = 0l;
  
  if(!sabotage_improvement){
    printf("Bug in spy sabotage code\n");
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.value=sabotage_improvement+1;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
...
*****************************************************************/

int create_advances_list(struct player *pplayer, struct player *pvictim, int make_modal)
{  
  GtkWidget *close_command, *scrolled;
  int i, j;
  gchar *title[1] = { "Select Advance to Steal" };
  GtkAccelGroup *accel=gtk_accel_group_new();

  spy_tech_shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(spy_tech_shell),"Steal Technology");
  gtk_window_set_position (GTK_WINDOW(spy_tech_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(spy_tech_shell));
  
  spy_advances_list = gtk_clist_new_with_titles(1,title);
  gtk_clist_column_titles_passive(GTK_CLIST(spy_advances_list));

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled),spy_advances_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_widget_set_usize( scrolled, 180, 250 );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_tech_shell)->vbox ),
	scrolled, TRUE, TRUE, 0 );
  gtk_clist_set_column_width(GTK_CLIST(spy_advances_list), 0,
        GTK_CLIST(spy_advances_list)->clist_window_width);

  close_command = gtk_button_new_with_label("Close");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_tech_shell)->action_area ),
	close_command, TRUE, TRUE, 0 );
  gtk_widget_add_accelerator(close_command, "clicked", accel, GDK_Escape, 0, 0);

  spy_steal_command = gtk_button_new_with_label("Steal");
  gtk_widget_set_sensitive(spy_steal_command, FALSE);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_tech_shell)->action_area ),
	spy_steal_command, TRUE, TRUE, 0 );


  gtk_signal_connect(GTK_OBJECT(spy_advances_list), "select_row",
	GTK_SIGNAL_FUNC(spy_select_tech_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(spy_advances_list), "unselect_row",
	GTK_SIGNAL_FUNC(spy_uselect_tech_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(close_command), "clicked",
	GTK_SIGNAL_FUNC(spy_close_tech_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(spy_steal_command), "clicked",
	GTK_SIGNAL_FUNC(spy_steal_callback), NULL);

  /* Now populate the list */
  gtk_clist_freeze(GTK_CLIST(spy_advances_list));

  j = 0;
  advance_type[j] = -1;

  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    for(i=1; i<A_LAST; i++) {
      gchar *row[1];

      if(get_invention(pvictim, i)==TECH_KNOWN && 
	 (get_invention(pplayer, i)==TECH_UNKNOWN || 
	  get_invention(pplayer, i)==TECH_REACHABLE)) {

	row[0] = advances[i].name;
	gtk_clist_append(GTK_CLIST(spy_advances_list), row);
        advance_type[j++] = i;
      }
    }
  }

  if(j == 0) {
    gchar *row[1] = { "NONE" };

    gtk_clist_append(GTK_CLIST(spy_advances_list),row);
    j++;
  }
  gtk_clist_thaw(GTK_CLIST(spy_advances_list));

  gtk_widget_set_sensitive(spy_steal_command, FALSE);
  
  gtk_widget_show_all(GTK_DIALOG(spy_tech_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(spy_tech_shell)->action_area);
  return j;
}

/****************************************************************
...
*****************************************************************/

int create_improvements_list(struct player *pplayer, struct city *pcity, int make_modal)
{  
  GtkWidget *close_command, *scrolled;
  int i, j;
  gchar *title[1] = { "Select Improvement to Sabotage" };
  GtkAccelGroup *accel=gtk_accel_group_new();

  spy_sabotage_shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(spy_sabotage_shell),"Sabotage Improvements");
  gtk_window_set_position (GTK_WINDOW(spy_sabotage_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(spy_sabotage_shell));
  
  spy_improvements_list = gtk_clist_new_with_titles(1,title);
  gtk_clist_column_titles_passive(GTK_CLIST(spy_improvements_list));
  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled), spy_improvements_list);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_widget_set_usize( scrolled, 180, 250 );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_sabotage_shell)->vbox ),
	scrolled, TRUE, TRUE, 0 );
  gtk_clist_set_column_width(GTK_CLIST(spy_improvements_list), 0,
        GTK_CLIST(spy_improvements_list)->clist_window_width);

  close_command = gtk_button_new_with_label("Close");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_sabotage_shell)->action_area ),
	close_command, TRUE, TRUE, 0 );
  gtk_widget_add_accelerator(close_command, "clicked", accel, GDK_Escape, 0, 0);
  
  spy_sabotage_command = gtk_button_new_with_label("Sabotage");
  gtk_widget_set_sensitive(spy_sabotage_command, FALSE);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_sabotage_shell)->action_area ),
	spy_sabotage_command, TRUE, TRUE, 0 );
  

  gtk_signal_connect(GTK_OBJECT(spy_improvements_list), "select_row",
	GTK_SIGNAL_FUNC(spy_select_improvement_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(spy_improvements_list), "unselect_row",
	GTK_SIGNAL_FUNC(spy_uselect_improvement_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(close_command), "clicked",
	GTK_SIGNAL_FUNC(spy_close_sabotage_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(spy_sabotage_command), "clicked",
	GTK_SIGNAL_FUNC(spy_sabotage_callback), NULL);

  /* Now populate the list */
  gtk_clist_freeze(GTK_CLIST(spy_improvements_list));

  j = 0;
  improvement_type[j++] = -1;

  for(i=0; i<B_LAST; i++) {
    gchar *row[1];

    if(i != B_PALACE && pcity->improvements[i] && !is_wonder(i)) {
      row[0] = get_imp_name_ex(pcity, i);

      gtk_clist_append(GTK_CLIST(spy_improvements_list), row);
      improvement_type[j++] = i;
    }  
  }
  gtk_clist_thaw(GTK_CLIST(spy_improvements_list));

  gtk_widget_set_sensitive(spy_sabotage_command, FALSE);

  gtk_widget_show_all(GTK_DIALOG(spy_sabotage_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(spy_sabotage_shell)->action_area);
  return j;
}

/****************************************************************
...
*****************************************************************/
void spy_steal_popup(GtkWidget *w, gpointer data)
{
  struct city *pvcity = find_city_by_id(diplomat_target_id);
  struct player *pvictim = NULL;

  if(pvcity)
    pvictim = city_owner(pvcity);

/* it is concievable that pvcity will not be found, because something
has happened to the city during latency.  Therefore we must initialize
pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */
  
  destroy_message_dialog(w);

  if(!spy_tech_shell){
    spy_tech_shell_is_modal=1;

    create_advances_list(game.player_ptr, pvictim, spy_tech_shell_is_modal);
    gtk_set_relative_position (toplevel, spy_tech_shell, 10, 10);

    gtk_widget_show(spy_tech_shell);
  }
}

/****************************************************************
...
*****************************************************************/
void spy_sabotage_popup(GtkWidget *w, gpointer data)
{
  struct city *pvcity = find_city_by_id(diplomat_target_id);
  
  destroy_message_dialog(w);

  if(!spy_sabotage_shell){
    spy_sabotage_shell_is_modal=1;

    create_improvements_list(game.player_ptr, pvcity, spy_sabotage_shell_is_modal);
    gtk_set_relative_position (toplevel, spy_sabotage_shell, 10, 10);

    gtk_widget_show(spy_sabotage_shell);
  }
}

/****************************************************************
...
*****************************************************************/
void diplomat_incite_yes_callback(GtkWidget *w, gpointer data)
{
  struct packet_diplomat_action req;

  req.action_type=DIPLOMAT_INCITE;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;

  send_packet_diplomat_action(&aconnection, &req);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void diplomat_incite_no_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}



/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
void diplomat_incite_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity;
  struct packet_generic_integer packet;

  destroy_message_dialog(w);
  
  if(find_unit_by_id(diplomat_id) && 
     (pcity=find_city_by_id(diplomat_target_id))) { 
    packet.value = diplomat_target_id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }

}

/****************************************************************
...  Popup the yes/no dialog for inciting, since we know the cost now
*****************************************************************/
void popup_incite_dialog(struct city *pcity)
{
  char buf[128];

  if(game.player_ptr->economic.gold>=pcity->incite_revolt_cost) {
   sprintf(buf, "Incite a revolt for %d gold?\nTreasury contains %d gold.", 
	  pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   diplomat_target_id = pcity->id;
   popup_message_dialog(toplevel, /*"diplomatrevoltdialog"*/"Incite a Revolt!", buf,
		       "Yes", diplomat_incite_yes_callback, 0,
		       "No", diplomat_incite_no_callback, 0, 0);
  } else {
   sprintf(buf, "Inciting a revolt costs %d gold.\nTreasury contains %d gold.", 
	     pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   popup_message_dialog(toplevel, /*"diplomatnogolddialog"*/"Traitors Demand Too Much!", buf,
		       "Darn", diplomat_incite_no_callback, 0, 
		       0);
  }
}


/****************************************************************
...
*****************************************************************/
void diplomat_cancel_callback(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}



/****************************************************************
...
*****************************************************************/
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
  struct city *pcity;
  struct unit *ptunit;
  GtkWidget *shl;

  diplomat_id=punit->id;
  
  if((pcity=map_get_city(dest_x, dest_y))){
    
    /* Spy/Diplomat acting against a city */ 
    
    diplomat_target_id=pcity->id;
    if(!unit_flag(punit->type, F_SPY)){
      shl=popup_message_dialog(toplevel, /*"diplomatdialog"*/" Choose Your Diplomat's Strategy", 
         		     "Sir, the diplomat is waiting for your command",
         		     "Establish embassy", diplomat_embassy_callback, 0,
         		     "Investigate City", diplomat_investigate_callback, 0,
         		     "Sabotage city", diplomat_sabotage_callback, 0,
         		     "Steal technology", diplomat_steal_callback, 0,
         		     "Incite a revolt", diplomat_incite_callback, 0,
         		     "Cancel", diplomat_cancel_callback, 0,
         		     0);
      
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button0",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button1",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button2",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button3",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button4",FALSE);
    }else{
       shl=popup_message_dialog(toplevel, /*"spydialog"*/"Choose Your Spy's Strategy",
 			      "Sir, the spy is waiting for your command",
 			      "Establish Embassy", diplomat_embassy_callback, 0,
 			      "Investigate City (free)", diplomat_investigate_callback, 0,
 			      "Poison City", spy_poison_callback,0,
 			      "Industrial Sabotage", spy_sabotage_popup, 0,
 			      "Steal Technology", spy_steal_popup, 0,
 			      "Incite a Revolt", diplomat_incite_callback, 0,
 			      "Cancel", diplomat_cancel_callback, 0,
 			      0);
 
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button0",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button1",FALSE);
      if(!diplomat_can_do_action(punit, SPY_POISON, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button2",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button3",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button4",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
       message_dialog_button_set_sensitive(shl,"button5",FALSE);
     }
   }else{ 
     if((ptunit=unit_list_get(&map_get_tile(dest_x, dest_y)->units, 0))){
       /* Spy/Diplomat acting against a unit */ 
       
       diplomat_target_id=ptunit->id;
 
       shl=popup_message_dialog(toplevel, /*"spybribedialog"*/"Subvert Enemy Unit",
                              (!unit_flag(punit->type, F_SPY))?
 			      "Sir, the diplomat is waiting for your command":
 			      "Sir, the spy is waiting for your command",
 			      "Bribe Enemy Unit", diplomat_bribe_callback, 0,
 			      "Sabotage Enemy Unit", spy_sabotage_unit_callback, 0,
 			      "Cancel", diplomat_cancel_callback, 0,
 			      0);
        
       if(!diplomat_can_do_action(punit, DIPLOMAT_BRIBE, dest_x, dest_y))
        message_dialog_button_set_sensitive(shl,"button0",FALSE);
       if(!diplomat_can_do_action(punit, SPY_SABOTAGE_UNIT, dest_x, dest_y))
        message_dialog_button_set_sensitive(shl,"button1",FALSE);
    }
  }
}


/****************************************************************
...
*****************************************************************/
void caravan_establish_trade_callback(GtkWidget *w, gpointer data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_ESTABLISH_TRADE);
    
  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void caravan_help_build_wonder_callback(GtkWidget *w, gpointer data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_HELP_BUILD_WONDER);

  destroy_message_dialog(w);
}


/****************************************************************
...
*****************************************************************/
void caravan_keep_moving_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  
  if((punit=find_unit_by_id(caravan_unit_id)) && 
     (pcity=find_city_by_id(caravan_city_id))) {
    struct unit req_unit;

    req_unit=*punit;
    req_unit.x=pcity->x;
    req_unit.y=pcity->y;
    send_unit_info(&req_unit);
  }
  destroy_message_dialog(w);
}



/****************************************************************
...
*****************************************************************/
void popup_caravan_dialog(struct unit *punit,
			  struct city *phomecity, struct city *pdestcity)
{
  GtkWidget *shl, *b;
  char buf[128];
  
  sprintf(buf, "Your caravan from %s reaches the city of %s.\nWhat now?",
	  phomecity->name, pdestcity->name);
  
  caravan_city_id=pdestcity->id; /* callbacks need these */
  caravan_unit_id=punit->id;
  
  shl=popup_message_dialog(toplevel, /*"caravandialog"*/"Your Caravan Has Arrived", 
			   buf,
			   "Establish traderoute",caravan_establish_trade_callback, 0,
			   "Help build Wonder",caravan_help_build_wonder_callback, 0,
			   "Keep moving",caravan_keep_moving_callback, 0,
			   0);
  
  if(!can_establish_trade_route(phomecity, pdestcity))
  {
    b = gtk_object_get_data( GTK_OBJECT( shl ), "button0" );
    gtk_widget_set_sensitive( b, FALSE );
  }
  
  if(!unit_can_help_build_wonder(punit, pdestcity))
  {
    b = gtk_object_get_data( GTK_OBJECT( shl ), "button1" );
    gtk_widget_set_sensitive( b, FALSE );
  }
}


/****************************************************************
...
*****************************************************************/
void government_callback(GtkWidget *w, gpointer data)
{
  struct packet_player_request packet;

  packet.government=(size_t)data;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_GOVERNMENT);

  destroy_message_dialog(w);
  is_showing_government_dialog=0;
}


/****************************************************************
...
*****************************************************************/
void popup_government_dialog(void)
{
  if(!is_showing_government_dialog) {
    GtkWidget *shl, *button;

    is_showing_government_dialog=1;
  
    shl=popup_message_dialog(toplevel, /*"governmentdialog"*/"Choose Your New Government", 
			     "Select government type:",
			     "Despotism", government_callback, G_DESPOTISM,
			     "Monarchy", government_callback, G_MONARCHY,
			     "Communism", government_callback, G_COMMUNISM,
			     "Republic", government_callback, G_REPUBLIC,
			     "Democracy", government_callback, G_DEMOCRACY, 0);
    if(!can_change_to_government(game.player_ptr, G_DESPOTISM))
    {
	button = gtk_object_get_data( GTK_OBJECT( shl ), "button0" );
	gtk_widget_set_sensitive( button, FALSE );
    }
    if(!can_change_to_government(game.player_ptr, G_MONARCHY))
    {
	button = gtk_object_get_data( GTK_OBJECT( shl ), "button1" );
	gtk_widget_set_sensitive( button, FALSE );
    }
    if(!can_change_to_government(game.player_ptr, G_COMMUNISM))
    {
	button = gtk_object_get_data( GTK_OBJECT( shl ), "button2" );
	gtk_widget_set_sensitive( button, FALSE );
    }
    if(!can_change_to_government(game.player_ptr, G_REPUBLIC))
    {
	button = gtk_object_get_data( GTK_OBJECT( shl ), "button3" );
	gtk_widget_set_sensitive( button, FALSE );
    }
    if(!can_change_to_government(game.player_ptr, G_DEMOCRACY))
    {
	button = gtk_object_get_data( GTK_OBJECT( shl ), "button4" );
	gtk_widget_set_sensitive( button, FALSE );
    }
  }
}



/****************************************************************
...
*****************************************************************/
void revolution_callback_yes(GtkWidget *w, gpointer data)
{
  struct packet_player_request packet;

  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_REVOLUTION);
  
  destroy_message_dialog(w);
}

/****************************************************************
...
*****************************************************************/
void revolution_callback_no(GtkWidget *w, gpointer data)
{
  destroy_message_dialog(w);
}



/****************************************************************
...
*****************************************************************/
void popup_revolution_dialog(void)
{
  popup_message_dialog(toplevel, /*"revolutiondialog"*/"Revolution!", 
		       "You say you wanna revolution?",
		       "Yes",revolution_callback_yes, 0,
		       "No",revolution_callback_no, 0, 
		       0);
}




/****************************************************************
...
*****************************************************************/
gint popup_mes_del_callback(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  gtk_widget_set_sensitive( (GtkWidget *)data, TRUE );
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
void message_dialog_button_set_sensitive(GtkWidget *shl, char *bname,int state)
{
  GtkWidget *b;

  b = gtk_object_get_data( GTK_OBJECT( shl ), bname );
  gtk_widget_set_sensitive( b, state );
}

/****************************************************************
...
*****************************************************************/
GtkWidget *popup_message_dialog(GtkWidget *parent, char *dialogname,
								char *text, ...)
{
  va_list args;
  GtkWidget *dshell, *button, *dlabel, *vbox;
  void (*fcb)(GtkWidget *, gpointer);
  gpointer data;
  char *name;
  char button_name[512];
  int i;

  gtk_widget_set_sensitive(parent, FALSE);
  
  dshell=gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position (GTK_WINDOW(dshell), GTK_WIN_POS_MOUSE);

  gtk_signal_connect( GTK_OBJECT(dshell),"delete_event",
	GTK_SIGNAL_FUNC(popup_mes_del_callback),(gpointer)parent );

  gtk_window_set_title( GTK_WINDOW(dshell), dialogname );

  vbox = gtk_vbox_new(0,TRUE);
  gtk_container_add(GTK_CONTAINER(dshell),vbox);

  gtk_container_border_width(GTK_CONTAINER(vbox),5);

  dlabel = gtk_label_new(text);
  gtk_box_pack_start( GTK_BOX( vbox ), dlabel, TRUE, FALSE, 0 );

  gtk_object_set_data(GTK_OBJECT(vbox), "parent",(gpointer)parent);

  i=0;
  va_start(args, text);
  
  while((name=va_arg(args, char *))) {
    fcb=va_arg(args, void *);
    data=va_arg(args, gpointer);
    sprintf(button_name, "button%d", i++);
    
    button=gtk_button_new_with_label(name);
    gtk_box_pack_start( GTK_BOX( vbox ), button, TRUE, FALSE, 0 );
    
    gtk_object_set_data( GTK_OBJECT( dshell ), button_name, button );

    gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(fcb),data );
  }
  
  va_end(args);

  gtk_widget_show_all( vbox );

  gtk_widget_show(dshell);  

  return dshell;
}

/****************************************************************
...
*****************************************************************/
void destroy_message_dialog(GtkWidget *button)
{
  GtkWidget *parent;

  parent = gtk_object_get_data(GTK_OBJECT(button->parent),"parent");
  gtk_widget_set_sensitive(parent, TRUE);

  gtk_widget_destroy(button->parent->parent);
}


/**************************************************************************
...
**************************************************************************/
void unit_select_all_callback(GtkWidget *w, gpointer data)
{
  int i;

  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(unit_select_dialog_shell);
  unit_select_dialog_shell=0;
  
  for(i=0; i<unit_select_no; i++) {
    struct unit *punit=unit_list_find(&game.player_ptr->units, 
				      unit_select_ids[i]);
    if(punit) {
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      set_unit_focus(punit);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void unit_select_callback(GtkWidget *w, struct unit *punit)
{
  if(punit) {
    request_new_unit_activity(punit, ACTIVITY_IDLE);
    set_unit_focus(punit);
  }

  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(unit_select_dialog_shell);
  unit_select_dialog_shell=0;
}

static int number_of_columns(int n)
{
#if 0
  /* This would require libm, which isn't worth it for this one little
   * function.  Since the number of units is limited to 100 already, the ifs
   * work fine.  */
  double sqrt(); double ceil();
  return ceil(sqrt((double)n/5.0));
#else
  if(n<=5) return 1;
  else if(n<=20) return 2;
  else if(n<=45) return 3;
  else if(n<=80) return 4;
  else return 5;
#endif
}
static int number_of_rows(int n)
{
  int c=number_of_columns(n);
  return (n+c-1)/c;
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_unit_select_dialog(struct tile *ptile)
{
  int i,n,r;
  char buffer[512];
  GtkWidget *pix, *hbox, *table;
  GtkWidget *unit_select_all_command, *unit_select_close_command;

  if (!unit_select_dialog_shell){
  gtk_widget_set_sensitive(toplevel, FALSE);

  unit_select_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(unit_select_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_window_set_position (GTK_WINDOW(unit_select_dialog_shell), GTK_WIN_POS_MOUSE);

  gtk_window_set_title(GTK_WINDOW(unit_select_dialog_shell),
	"Unit selection" );

  n=unit_list_size(&ptile->units);
  r=number_of_rows(n);

  table=gtk_table_new(r, number_of_columns(n), FALSE);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(unit_select_dialog_shell)->vbox),
	table);  

  for(i=0; i<n; i++) {
    struct unit *punit=unit_list_get(&ptile->units, i);
    struct unit_type *punittemp=get_unit_type(punit->type);
    struct city *pcity;

    hbox = gtk_hbox_new(FALSE,10);
    gtk_table_attach_defaults(GTK_TABLE(table), hbox, 
				(i/r), (i/r)+1,
				(i%r), (i%r)+1);
    gtk_container_border_width(GTK_CONTAINER(hbox),5);

    unit_select_ids[i]=punit->id;

    pcity=city_list_find_id(&game.player_ptr->cities, punit->homecity);

    sprintf(buffer, "%s(%s)\n%s",
	    punittemp->name, 
	    pcity ? pcity->name : "",
	    unit_activity_text(punit));

    pix = gtk_new_pixmap(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

    unit_select_commands[i]=gtk_button_new();
    gtk_widget_set_sensitive(unit_select_commands[i],
       can_unit_do_activity(punit, ACTIVITY_IDLE) );
    gtk_widget_set_usize(unit_select_commands[i],
       NORMAL_TILE_WIDTH+4, NORMAL_TILE_HEIGHT+4);
    gtk_container_add(GTK_CONTAINER(unit_select_commands[i]), pix);
    gtk_box_pack_start(GTK_BOX(hbox),unit_select_commands[i],
       FALSE, FALSE, 0);

    if (flags_are_transparent) {
       gtk_clear_pixmap(pix);
    }
    put_unit_gpixmap(punit, GTK_PIXMAP(pix), 0, 0);
    gtk_expose_now(pix);

    unit_select_labels[i]=gtk_label_new(buffer);
    gtk_box_pack_start(GTK_BOX(hbox),unit_select_labels[i],
       FALSE, FALSE, 0);

    gtk_signal_connect(GTK_OBJECT(unit_select_commands[i]), "clicked",
       GTK_SIGNAL_FUNC(unit_select_callback), (gpointer)punit);
  }
  unit_select_no=i;


  unit_select_close_command=gtk_button_new_with_label("Close");
  gtk_box_pack_start( GTK_BOX(GTK_DIALOG(unit_select_dialog_shell)->action_area),
	unit_select_close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( unit_select_close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( unit_select_close_command );

  unit_select_all_command=gtk_button_new_with_label("Ready all");
  gtk_box_pack_start( GTK_BOX(GTK_DIALOG(unit_select_dialog_shell)->action_area),
	unit_select_all_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( unit_select_all_command, GTK_CAN_DEFAULT );

  gtk_signal_connect(GTK_OBJECT(unit_select_close_command), "clicked",
	GTK_SIGNAL_FUNC(unit_select_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(unit_select_all_command), "clicked",
	GTK_SIGNAL_FUNC(unit_select_all_callback), NULL);

  gtk_widget_show_all( GTK_DIALOG(unit_select_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(unit_select_dialog_shell)->action_area );

  gtk_set_relative_position(toplevel, unit_select_dialog_shell, 15, 10);
  gtk_widget_show(unit_select_dialog_shell);
  }
}




/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_races_dialog(void)
{
  gtk_widget_set_sensitive (toplevel, FALSE);

  create_races_dialog ();

  gtk_widget_show (races_dialog_shell);
}

/****************************************************************
...
*****************************************************************/
void popdown_races_dialog(void)
{
  if (races_dialog_shell) {
    gtk_widget_set_sensitive (toplevel, TRUE);
    gtk_widget_destroy (races_dialog_shell);
    races_dialog_shell = NULL;
  } /* else there is no dialog shell to destroy */
}


/****************************************************************
...
*****************************************************************/
void create_races_dialog(void)
{
  int       i;
  GSList    *group = NULL;
  GtkWidget *f;

  races_dialog_shell = gtk_dialog_new();
    gtk_signal_connect( GTK_OBJECT(races_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(deleted_callback),NULL );

  gtk_window_set_title( GTK_WINDOW( races_dialog_shell ), " What Race Will You Be?" );

  f = gtk_frame_new("Select race and name");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	f, FALSE, FALSE, 0 );

  races_toggles_form = gtk_table_new( 2, 7, FALSE );
  gtk_container_add( GTK_CONTAINER( f ), races_toggles_form );

  for(i=0; i<R_LAST; i++) {

    races_toggles[i]= gtk_radio_button_new_with_label( group, get_race_name(i) );

    gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_toggles[i] ), FALSE );

    group = gtk_radio_button_group( GTK_RADIO_BUTTON( races_toggles[i] ) );

    gtk_table_attach_defaults( GTK_TABLE(races_toggles_form), races_toggles[i],
			i%2,i%2+1,i/2,i/2+1 );
  }


  races_name = gtk_entry_new();

  gtk_entry_set_text(GTK_ENTRY(races_name), default_race_leader_names[0]);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	races_name, FALSE, FALSE, 0 );
  GTK_WIDGET_SET_FLAGS( races_name, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( races_name );

  races_ok_command = gtk_button_new_with_label( "Ok" );
  GTK_WIDGET_SET_FLAGS( races_ok_command, GTK_CAN_DEFAULT );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->action_area ),
	races_ok_command, TRUE, TRUE, 0 );

  for(i=0; i<R_LAST; i++)
	gtk_signal_connect( GTK_OBJECT( races_toggles[i] ), "toggled",
	    GTK_SIGNAL_FUNC( races_toggles_callback ), NULL );

  gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_toggles[0] ), TRUE );

  gtk_signal_connect( GTK_OBJECT( races_ok_command ), "clicked",
			GTK_SIGNAL_FUNC( races_buttons_callback ), NULL );

  gtk_widget_show_all( GTK_DIALOG(races_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(races_dialog_shell)->action_area );
}

/****************************************************************
...
*****************************************************************/
/*
void races_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  x_simulate_button_click(XtNameToWidget(XtParent(w), "racesokcommand"));
}
*/

/**************************************************************************
...
**************************************************************************/
void races_toggles_set_sensitive(int bits)
{
  int i, selected, mybits;

  mybits=bits;

  for(i=0; i<R_LAST; i++) {
    if(mybits&1)
      gtk_widget_set_sensitive( races_toggles[i], FALSE );
    else
      gtk_widget_set_sensitive( races_toggles[i], TRUE );
    mybits>>=1;
  }

  if((selected=races_buttons_get_current())==-1)
     return;
}



/**************************************************************************
...
**************************************************************************/
void races_toggles_callback( GtkWidget *w, gpointer data )
{
  int i;

  for(i=0; i<R_LAST; i++)
    if(w==races_toggles[i]) {
      gtk_entry_set_text(GTK_ENTRY(races_name), default_race_leader_names[i]);

      race_selected = i;
      return;
    }
}

/**************************************************************************
...
**************************************************************************/
int races_buttons_get_current(void)
{
  return race_selected;
}


/**************************************************************************
...
**************************************************************************/
void races_buttons_callback( GtkWidget *w, gpointer data )
{
  int selected;
  char *s;

  struct packet_alloc_race packet;

  if((selected=races_buttons_get_current())==-1) {
    append_output_window("You must select a race.");
    return;
  }

  s = gtk_entry_get_text(GTK_ENTRY(races_name));

  /* perform a minimum of sanity test on the name */
  packet.race_no=selected;
  strncpy(packet.name, (char*)s, NAME_SIZE);
  packet.name[NAME_SIZE-1]='\0';
  
  if(!get_sane_name(packet.name)) {
    append_output_window("You must type a legal name.");
    return;
  }

  packet.name[0]=toupper(packet.name[0]);

  send_packet_alloc_race(&aconnection, &packet);
}



/**************************************************************************
  Destroys its widget.  Usefull for a popdown callback on pop-ups that
  won't get resused.
**************************************************************************/
void destroy_me_callback( GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(w);
}

/**************************************************************************
  Adjust tax rates from main window
**************************************************************************/
void taxrates_callback( GtkWidget *w, GdkEventButton *event, gpointer data )
{
  int tax_end,lux_end,sci_end;
  size_t i;
  int delta=10;

  struct packet_player_request packet;
  i= (size_t)data;
  
  lux_end= game.player_ptr->economic.luxury;
  sci_end= lux_end + game.player_ptr->economic.science;
  tax_end= 100;

  packet.luxury= game.player_ptr->economic.luxury;
  packet.science= game.player_ptr->economic.science;
  packet.tax= game.player_ptr->economic.tax;

  i*= 10;
  if(i<lux_end){
    packet.luxury-= delta; packet.science+= delta;
  }else if(i<sci_end){
    packet.science-= delta; packet.tax+= delta;
   }else{
    packet.tax-= delta; packet.luxury+= delta;
  }
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);

}
