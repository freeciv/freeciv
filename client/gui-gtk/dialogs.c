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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "control.h"
#include "mapctrl_common.h"
#include "options.h"
#include "packhand.h"
#include "tilespec.h"

#include "chatline.h"
#include "cityrep.h"	/* for popdown_city_report_dialog */
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "messagewin.h"	/* for popdown_meswin_dialog */
#include "plrdlg.h"	/* for popdown_players_dialog */
#include "repodlgs.h"	/* for popdown_xxx_dialog */

#include "dialogs.h"

/******************************************************************/
static gint popup_mes_del_callback(GtkWidget *widget, GdkEvent *event,
				   gpointer data);

/******************************************************************/
static GtkWidget  *races_dialog_shell=NULL;
static GtkWidget  *races_toggles_form[MAX_NUM_ITEMS];
static GtkWidget  *races_by_name[MAX_NUM_ITEMS];
static GtkWidget  *class[MAX_NUM_ITEMS];
static GtkWidget  *legend[MAX_NUM_ITEMS];
static GtkWidget  *legend_frame[MAX_NUM_ITEMS];
static GtkWidget  *races_sex_toggles_form;
static GtkWidget  *city_style_toggles_form;
static GtkWidget  *races_ok_command;            /* ok button */
static GtkWidget  *races_disc_command=NULL;     /* disc button */
static GtkWidget  *races_quit_command=NULL;     /* quit button */
 /* toggle race */
static GtkWidget **races_toggles[MAX_NUM_ITEMS];
static GtkWidget *races_sex_toggles[2];		/* Male/Female */
static GtkWidget **city_style_toggles = NULL;
static GtkWidget *leader_name;			/* leader name */
static GtkWidget *notebook;

static int       num_classes;
static char      *class_names[MAX_NUM_ITEMS];

static GList *leader_strings = NULL;

/*
 * Contains a list of race ids sorted by the race name. Is valid as
 * long as the races_dialog is poped up. 
 */
static GList *sorted_races_list[MAX_NUM_ITEMS];

/******************************************************************/
static GtkWidget  *notify_dialog_shell;
static GtkWidget  *notify_headline;
static GtkWidget  *notify_label;

/******************************************************************/
static GtkWidget  *spy_tech_shell;
static GtkWidget  *spy_advances_list;
static GtkWidget  *spy_steal_command;

static int         spy_tech_shell_is_modal;
static int         advance_type[A_LAST+1];
static int         steal_advance = 0;

/******************************************************************/
static GtkWidget  *spy_sabotage_shell;
static GtkWidget  *spy_improvements_list;
static GtkWidget  *spy_sabotage_command;

static int         spy_sabotage_shell_is_modal;
static int         improvement_type[B_LAST+1];
static int         sabotage_improvement = 0;

/******************************************************************/

#define MAX_SELECT_UNITS 100
static GtkWidget  *unit_select_dialog_shell;
static GtkWidget  *unit_select_commands[MAX_SELECT_UNITS];
static GtkWidget  *unit_select_labels[MAX_SELECT_UNITS];
static int         unit_select_ids[MAX_SELECT_UNITS];
static int         unit_select_no;

static int races_buttons_get_current(void);
static int sex_buttons_get_current(void);
static int city_style_get_current(void);

static void create_races_dialog(void);
static void races_buttons_callback(GtkWidget *w, gpointer data);
static void races_toggles_callback(GtkWidget *w, gpointer race_id_p);
static void races_sex_toggles_callback(GtkWidget *w, gpointer data);
static void races_by_name_callback(GtkWidget *w, gpointer data);
static void leader_name_callback(GtkWidget *w, gpointer data);
static void city_style_toggles_callback(GtkWidget *w, gpointer data);
static void switch_page_callback(GtkNotebook * notebook,
				 GtkNotebookPage * page, gint page_num,
				 gpointer data);

static int selected_nation;
static int selected_leader;
static bool is_name_unique = FALSE;
static int selected_sex;
static int selected_city_style;
static int selected_class;
static int city_style_idx[64];  /* translation table basic style->city_style */
static int city_style_ridx[64]; /* translation table the other way. they     */
                                /* in fact limit the num of styles to 64     */
static int b_s_num; /* number of basic city styles, 
                     * i.e. those that you can start with */

static int caravan_city_id;
static int caravan_unit_id;

static int diplomat_dialog_open = 0;
static int diplomat_id;
static int diplomat_target_id;

static GtkWidget *caravan_dialog;

struct pillage_data {
  int unit_id;
  enum tile_special_type what;
};

/****************************************************************
...
*****************************************************************/
static void notify_command_callback(GtkWidget *w, GtkWidget *t)
{
  popdown_notify_dialog();
}

/****************************************************************
...
*****************************************************************/
gint deleted_callback(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  gtk_widget_set_sensitive( top_vbox, TRUE );
  return FALSE;
}


/****************************************************************
...
*****************************************************************/
static void create_notify_dialog(void)
{
  GtkWidget *notify_command;
  GtkWidget *notify_scrolled;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  notify_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(notify_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(notify_dialog_shell));
  gtk_widget_set_name(notify_dialog_shell, "Freeciv");

  gtk_container_border_width( GTK_CONTAINER(GTK_DIALOG(notify_dialog_shell)->vbox), 5 );

  notify_headline = gtk_label_new(NULL);   
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_headline, FALSE, FALSE, 0 );
  gtk_widget_set_name(notify_headline, "notify label");

  gtk_label_set_justify( GTK_LABEL( notify_headline ), GTK_JUSTIFY_LEFT );
  gtk_misc_set_alignment(GTK_MISC(notify_headline), 0.0, 0.0);

  notify_scrolled=gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notify_scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  notify_label = gtk_label_new(NULL);  
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (notify_scrolled),
					notify_label);

  gtk_widget_set_name(notify_label, "notify label");
  gtk_label_set_justify( GTK_LABEL( notify_label ), GTK_JUSTIFY_LEFT );
  gtk_misc_set_alignment(GTK_MISC(notify_label), 0.0, 0.0);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_scrolled, TRUE, TRUE, 0 );


  notify_command = gtk_button_new_with_label( _("Close") );
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
}

/****************************************************************
...
*****************************************************************/
static void notify_dialog_update(const char *caption, const char *headline,
				 const char *lines)
{
  gtk_window_set_title(GTK_WINDOW(notify_dialog_shell), caption);
  gtk_label_set_text(GTK_LABEL(notify_headline), headline);
  gtk_label_set_text(GTK_LABEL(notify_label), lines);
}

/**************************************************************************
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
			 const char *lines)
{
  if (!notify_dialog_shell) {
    create_notify_dialog();
    
    gtk_set_relative_position(toplevel, notify_dialog_shell, 10, 10);
  }

  notify_dialog_update(caption, headline, lines);
  gtk_window_show(GTK_WINDOW(notify_dialog_shell));
}

/****************************************************************
 Closes the notify dialog.
*****************************************************************/
void popdown_notify_dialog(void)
{
  if (notify_dialog_shell) {
    gtk_widget_destroy(notify_dialog_shell);
    notify_dialog_shell = NULL;
  }
}

/****************************************************************
...
*****************************************************************/

/* surely this should use genlists??  --dwp */
struct widget_list {
  GtkWidget *w;
  struct tile *tile;
  struct widget_list *next;
};
static struct widget_list *notify_goto_widget_list = NULL;

static void notify_goto_widget_remove(GtkWidget *w)
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

static struct tile *notify_goto_find_widget(GtkWidget *w)
{
  struct widget_list *cur;

  for (cur = notify_goto_widget_list; cur && cur->w !=w; cur = cur->next) {
    /* Nothing */
  }

  if (cur) {
    return cur->tile;
  } else {
    return NULL;
  }
}

static void notify_goto_add_widget_tile(GtkWidget *w, struct tile *ptile)
{
  struct widget_list *newwidget;
  newwidget = fc_malloc(sizeof(struct widget_list));
  newwidget->w = w;
  newwidget->tile = ptile;
  newwidget->next = notify_goto_widget_list;
  notify_goto_widget_list = newwidget;
}

static void notify_goto_command_callback(GtkWidget *w, gpointer data)
{
  struct tile *ptile =  notify_goto_find_widget(w);

  center_tile_mapcanvas(ptile);
  notify_goto_widget_remove(w);

  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(top_vbox, TRUE);
}

static void notify_no_goto_command_callback(GtkWidget *w, gpointer data)
{
  notify_goto_widget_remove(w);
  gtk_widget_destroy(w->parent->parent->parent);
  gtk_widget_set_sensitive(top_vbox, TRUE);
}

static gint notify_deleted_callback(GtkWidget *widget, GdkEvent *event,
				    gpointer data)
{
  notify_goto_widget_remove(widget);
  gtk_widget_set_sensitive(top_vbox, TRUE);
  return FALSE;
}

/**************************************************************************
  Popup a dialog to display information about an event that has a
  specific location.  The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(const char *headline, const char *lines,
			      struct tile *ptile)
{
  GtkWidget *notify_dialog_shell, *notify_command, *notify_goto_command;
  GtkWidget *notify_label;
  
  if (!ptile) {
    popup_notify_dialog(_("Message:"), headline, lines);
    return;
  }
  notify_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(notify_dialog_shell),"delete_event",
	GTK_SIGNAL_FUNC(notify_deleted_callback),NULL );

  gtk_window_set_title( GTK_WINDOW( notify_dialog_shell ), headline );

  notify_label=gtk_label_new(lines);
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_label, TRUE, TRUE, 0 );

  notify_command = gtk_button_new_with_label(_("Close"));
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_command, TRUE, TRUE, 0 );

  notify_goto_command = gtk_button_new_with_label(_("Goto and Close"));
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_goto_command, TRUE, TRUE, 0 );
  
  gtk_signal_connect(GTK_OBJECT(notify_command), "clicked",
		GTK_SIGNAL_FUNC(notify_no_goto_command_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(notify_goto_command), "clicked",
		GTK_SIGNAL_FUNC(notify_goto_command_callback), NULL);
  notify_goto_add_widget_tile(notify_goto_command, ptile);

  gtk_set_relative_position(toplevel, notify_dialog_shell, 25, 25);

  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->action_area );
  gtk_widget_show(notify_dialog_shell);

  gtk_widget_set_sensitive(top_vbox, FALSE);
}


/****************************************************************
...
*****************************************************************/
static void diplomat_bribe_yes_callback(gpointer data)
{
  request_diplomat_action(DIPLOMAT_BRIBE, diplomat_id,
			  diplomat_target_id, 0);
}


/****************************************************************
...  Ask the server how much the bribe is
*****************************************************************/
static void diplomat_bribe_callback(gpointer data)
{
  if (find_unit_by_id(diplomat_id) && find_unit_by_id(diplomat_target_id)) {
    dsend_packet_unit_bribe_inq(&aconnection, diplomat_target_id);
  }
}
 
/****************************************************************
...
*****************************************************************/
void popup_bribe_dialog(struct unit *punit)
{
  char buf[128];
  
  if (unit_flag(punit, F_UNBRIBABLE)) {
    popup_message_dialog(top_vbox, _("Ooops..."),
			 _("This unit cannot be bribed!"),
			 dummy_close_callback, NULL, _("Darn"), NULL, 0, 0);
  } else if(game.player_ptr->economic.gold>=punit->bribe_cost) {
    my_snprintf(buf, sizeof(buf),
		_("Bribe unit for %d gold?\nTreasury contains %d gold."), 
		punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(top_vbox, _("Bribe Enemy Unit"), buf,
			 dummy_close_callback, NULL,
			 _("_Yes"), diplomat_bribe_yes_callback, 0, 
			 _("_No"), NULL, 0, 0);
  } else {
    my_snprintf(buf, sizeof(buf),
		_("Bribing the unit costs %d gold.\n"
		  "Treasury contains %d gold."), 
		punit->bribe_cost, game.player_ptr->economic.gold);
    popup_message_dialog(top_vbox, _("Traitors Demand Too Much!"), buf,
			 dummy_close_callback, NULL, _("Darn"), NULL, 0, 0);
  }
}

/****************************************************************
...
*****************************************************************/
static void diplomat_sabotage_callback(gpointer data)
{
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    request_diplomat_action(DIPLOMAT_SABOTAGE, diplomat_id,
			    diplomat_target_id, -1);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_investigate_callback(gpointer data)
{
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    request_diplomat_action(DIPLOMAT_INVESTIGATE, diplomat_id,
			    diplomat_target_id, 0);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_unit_callback(gpointer data)
{
  request_diplomat_action(SPY_SABOTAGE_UNIT, diplomat_id,
			  diplomat_target_id, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_embassy_callback(gpointer data)
{
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    request_diplomat_action(DIPLOMAT_EMBASSY, diplomat_id,
			    diplomat_target_id, 0);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_poison_callback(gpointer data)
{
  diplomat_dialog_open=0;

  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    request_diplomat_action(SPY_POISON, diplomat_id, diplomat_target_id, 0);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_steal_callback(gpointer data)
{
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    request_diplomat_action(DIPLOMAT_STEAL,
			    diplomat_id, diplomat_target_id, 0);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_close_tech_callback(GtkWidget *w, gpointer data)
{

  if(spy_tech_shell_is_modal)
     gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy(spy_tech_shell);
  spy_tech_shell = NULL;

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_close_sabotage_callback(GtkWidget *w, gpointer data)
{

  if(spy_sabotage_shell_is_modal)
     gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy(spy_sabotage_shell);
  spy_sabotage_shell = NULL;

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_select_tech_callback(GtkWidget *w, gint row, gint column)
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
static void spy_uselect_tech_callback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(spy_steal_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
static void spy_select_improvement_callback(GtkWidget *w, gint row,
					    gint column)
{
  sabotage_improvement = improvement_type[row];
  gtk_widget_set_sensitive(spy_sabotage_command, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void spy_uselect_improvement_callback(GtkWidget *w, gint row,
					     gint column)
{
  gtk_widget_set_sensitive(spy_sabotage_command, FALSE);
}

/****************************************************************
...
*****************************************************************/
static void spy_steal_callback(gpointer data)
{  
  gtk_widget_destroy(spy_tech_shell);
  spy_tech_shell = NULL;
  
  if(!steal_advance){
    freelog(LOG_ERROR, "Bug in spy steal tech code.");
    process_diplomat_arrival(NULL, 0);
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    request_diplomat_action(DIPLOMAT_STEAL, diplomat_id,
			    diplomat_target_id, steal_advance);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void spy_steal_button_callback(GtkWidget * w, gpointer data)
{
  spy_steal_callback(data);
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_callback(GtkWidget *w, gpointer data)
{  
  gtk_widget_destroy(spy_sabotage_shell);
  spy_sabotage_shell = NULL;
  
  if(!sabotage_improvement){
    freelog(LOG_ERROR, "Bug in spy sabotage code");
    process_diplomat_arrival(NULL, 0);
    return;
  }
  
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    request_diplomat_action(DIPLOMAT_SABOTAGE, diplomat_id,
			    diplomat_target_id, sabotage_improvement + 1);
  }

  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static int create_advances_list(struct player *pplayer,
				struct player *pvictim, bool make_modal)
{  
  GtkWidget *close_command, *scrolled;
  int i, j;
  static const char *title_[1] = { N_("Select Advance to Steal") };
  static gchar **title;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!title) title = intl_slist(1, title_);
  
  spy_tech_shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(spy_tech_shell),_("Steal Technology"));
  gtk_window_set_position (GTK_WINDOW(spy_tech_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(spy_tech_shell));
  
  spy_advances_list = gtk_clist_new_with_titles(1, title);
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

  close_command = gtk_button_new_with_label(_("Close"));
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_tech_shell)->action_area ),
	close_command, TRUE, TRUE, 0 );
  gtk_widget_add_accelerator(close_command, "clicked", accel, GDK_Escape, 0, 0);

  spy_steal_command = gtk_button_new_with_label(_("Steal"));
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
	GTK_SIGNAL_FUNC(spy_steal_button_callback), NULL);

  /* Now populate the list */
  gtk_clist_freeze(GTK_CLIST(spy_advances_list));

  j = 0;
  advance_type[j] = -1;

  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    const gchar *row[1];

    for(i=A_FIRST; i<game.num_tech_types; i++) {
      if(get_invention(pvictim, i)==TECH_KNOWN && 
	 (get_invention(pplayer, i)==TECH_UNKNOWN || 
	  get_invention(pplayer, i)==TECH_REACHABLE)) {

	row[0] = advances[i].name;
	gtk_clist_append(GTK_CLIST(spy_advances_list), (gchar **)row);
        advance_type[j++] = i;
      }
    }

    if(j > 0) {
      row[0] = _("At Spy's Discretion");
      gtk_clist_append(GTK_CLIST(spy_advances_list), (gchar **)row);
      advance_type[j++] = game.num_tech_types;
    }
  }

  if(j == 0) {
    static const char *row_[1] = { N_("NONE") };
    static gchar **row;
    
    if (!row) row = intl_slist(1, row_);
  
    gtk_clist_append(GTK_CLIST(spy_advances_list), row);
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
static int create_improvements_list(struct player *pplayer,
				    struct city *pcity, bool make_modal)
{  
  GtkWidget *close_command, *scrolled;
  int j;
  gchar *row[1];
  static const char *title_[1] = { N_("Select Improvement to Sabotage") };
  static gchar **title;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!title) title = intl_slist(1, title_);
  
  spy_sabotage_shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(spy_sabotage_shell),_("Sabotage Improvements"));
  gtk_window_set_position (GTK_WINDOW(spy_sabotage_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(spy_sabotage_shell));
  
  spy_improvements_list = gtk_clist_new_with_titles(1, title);
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

  close_command = gtk_button_new_with_label(_("Close"));
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(spy_sabotage_shell)->action_area ),
	close_command, TRUE, TRUE, 0 );
  gtk_widget_add_accelerator(close_command, "clicked", accel, GDK_Escape, 0, 0);
  
  spy_sabotage_command = gtk_button_new_with_label(_("Sabotage"));
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
  row[0] = _("City Production");
  gtk_clist_append(GTK_CLIST(spy_improvements_list), row);
  improvement_type[j++] = -1;

  built_impr_iterate(pcity, i) {
    if (get_improvement_type(i)->sabotage > 0) {
      row[0] = (char *) get_impr_name_ex(pcity, i);
      gtk_clist_append(GTK_CLIST(spy_improvements_list), row);
      improvement_type[j++] = i;
    }  
  } built_impr_iterate_end;

  if(j > 1) {
    row[0] = _("At Spy's Discretion");
    gtk_clist_append(GTK_CLIST(spy_improvements_list), row);
    improvement_type[j++] = B_LAST;
  } else {
    improvement_type[0] = B_LAST; /* fake "discretion", since must be production */
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
static void spy_steal_popup(GtkWidget *w, gpointer data)
{
  struct city *pvcity = find_city_by_id(diplomat_target_id);
  struct player *pvictim = NULL;

  if(pvcity)
    pvictim = city_owner(pvcity);

/* it is concievable that pvcity will not be found, because something
has happened to the city during latency.  Therefore we must initialize
pvictim to NULL and account for !pvictim in create_advances_list. -- Syela */
  
  diplomat_dialog_open=0;

  if(!spy_tech_shell){
    spy_tech_shell_is_modal=1;

    create_advances_list(game.player_ptr, pvictim, spy_tech_shell_is_modal);
    gtk_set_relative_position (toplevel, spy_tech_shell, 10, 10);

    gtk_widget_show(spy_tech_shell);
  }
}

/****************************************************************
 Requests up-to-date list of improvements, the return of
 which will trigger the popup_sabotage_dialog() function.
*****************************************************************/
static void spy_request_sabotage_list(gpointer data)
{
  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    request_diplomat_action(SPY_GET_SABOTAGE_LIST, diplomat_id,
			    diplomat_target_id, 0);
  }
}

/****************************************************************
 Pops-up the Spy sabotage dialog, upon return of list of
 available improvements requested by the above function.
*****************************************************************/
void popup_sabotage_dialog(struct city *pcity)
{
  if(!spy_sabotage_shell){
    spy_sabotage_shell_is_modal=1;

    create_improvements_list(game.player_ptr, pcity, spy_sabotage_shell_is_modal);
    gtk_set_relative_position (toplevel, spy_sabotage_shell, 10, 10);

    gtk_widget_show(spy_sabotage_shell);
  }
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_yes_callback(gpointer data)
{
  request_diplomat_action(DIPLOMAT_INCITE, diplomat_id,
			  diplomat_target_id, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_incite_close_callback(gpointer data)
{
  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
static void diplomat_incite_callback(gpointer data)
{
  if (find_unit_by_id(diplomat_id) && find_city_by_id(diplomat_target_id)) {
    dsend_packet_city_incite_inq(&aconnection, diplomat_target_id);
  }
}

/****************************************************************
Popup the yes/no dialog for inciting, since we know the cost now
*****************************************************************/
void popup_incite_dialog(struct city *pcity)
{
  char buf[128];

  if (pcity->incite_revolt_cost == INCITE_IMPOSSIBLE_COST) {
    my_snprintf(buf, sizeof(buf), _("You can't incite a revolt in %s."),
		pcity->name);
    popup_message_dialog(top_vbox, _("City can't be incited!"), buf,
			 diplomat_incite_close_callback, NULL,
			 _("Darn"), NULL, 0, 0);
  } else if (game.player_ptr->economic.gold >= pcity->incite_revolt_cost) {
    my_snprintf(buf, sizeof(buf),
		_("Incite a revolt for %d gold?\nTreasury contains %d gold."), 
		pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   popup_message_dialog(top_vbox, _("Incite a Revolt!"), buf,
			diplomat_incite_close_callback, NULL,
		       _("_Yes"), diplomat_incite_yes_callback, 0,
		       _("_No"), NULL, 0, 0);
  } else {
    my_snprintf(buf, sizeof(buf),
		_("Inciting a revolt costs %d gold.\n"
		  "Treasury contains %d gold."), 
		pcity->incite_revolt_cost, game.player_ptr->economic.gold);
   popup_message_dialog(top_vbox, _("Traitors Demand Too Much!"), buf,
			diplomat_incite_close_callback, NULL,
		       _("Darn"), NULL, 0, 
		       0);
  }
}


/****************************************************************
  Callback from diplomat/spy dialog for "keep moving".
  (This should only occur when entering allied city.)
*****************************************************************/
static void diplomat_keep_moving_callback(gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  
  if( (punit=find_unit_by_id(diplomat_id))
      && (pcity=find_city_by_id(diplomat_target_id))
      && !same_pos(punit->tile, pcity->tile)) {
    request_diplomat_action(DIPLOMAT_MOVE, diplomat_id,
			    diplomat_target_id, 0);
  }
  process_diplomat_arrival(NULL, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_close_callback(gpointer data)
{
  diplomat_dialog_open = 0;

  process_diplomat_arrival(NULL, 0);
}


/****************************************************************
...
*****************************************************************/
void popup_diplomat_dialog(struct unit *punit, struct tile *dest_tile)
{
  struct city *pcity;
  struct unit *ptunit;
  GtkWidget *shl;
  char buf[128];

  diplomat_id=punit->id;

  if((pcity=map_get_city(dest_tile))){
    /* Spy/Diplomat acting against a city */

    diplomat_target_id=pcity->id;
    my_snprintf(buf, sizeof(buf),
		_("Your %s has arrived at %s.\nWhat is your command?"),
		unit_name(punit->type), pcity->name);

    if(!unit_flag(punit, F_SPY)){
      shl=popup_message_dialog(top_vbox, /*"diplomatdialog"*/
			       _(" Choose Your Diplomat's Strategy"), buf,
			       diplomat_close_callback, NULL,
         		     _("Establish _Embassy"), diplomat_embassy_callback, 0,
         		     _("_Investigate City"), diplomat_investigate_callback, 0,
         		     _("_Sabotage City"), diplomat_sabotage_callback, 0,
         		     _("Steal _Technology"), diplomat_steal_callback, 0,
         		     _("Incite a _Revolt"), diplomat_incite_callback, 0,
         		     _("_Keep moving"), diplomat_keep_moving_callback, 0,
         		     _("_Cancel"), NULL, 0,
         		     0);
      
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_tile))
       message_dialog_button_set_sensitive(shl,"button0",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button1",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button2",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_tile))
       message_dialog_button_set_sensitive(shl,"button3",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button4",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_MOVE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button5",FALSE);
    }else{
       shl = popup_message_dialog(top_vbox, /*"spydialog"*/
		_("Choose Your Spy's Strategy"), buf,
		diplomat_close_callback, NULL,
 		_("Establish _Embassy"), diplomat_embassy_callback, 0,
 		_("_Investigate City"), diplomat_investigate_callback, 0,
 		_("_Poison City"), spy_poison_callback,0,
 		_("Industrial _Sabotage"), spy_request_sabotage_list, 0,
 		_("Steal _Technology"), spy_steal_popup, 0,
 		_("Incite a _Revolt"), diplomat_incite_callback, 0,
 		_("_Keep moving"), diplomat_keep_moving_callback, 0,
 		_("_Cancel"), NULL, 0,
		0);
 
      if(!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_tile))
       message_dialog_button_set_sensitive(shl,"button0",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button1",FALSE);
      if(!diplomat_can_do_action(punit, SPY_POISON, dest_tile))
       message_dialog_button_set_sensitive(shl,"button2",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button3",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_tile))
       message_dialog_button_set_sensitive(shl,"button4",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button5",FALSE);
      if(!diplomat_can_do_action(punit, DIPLOMAT_MOVE, dest_tile))
       message_dialog_button_set_sensitive(shl,"button6",FALSE);
     }

    diplomat_dialog_open=1;
   }else{ 
     if((ptunit=unit_list_get(&dest_tile->units, 0))){
       /* Spy/Diplomat acting against a unit */ 
       
       diplomat_target_id=ptunit->id;
 
       shl=popup_message_dialog(top_vbox, /*"spybribedialog"*/_("Subvert Enemy Unit"),
                              (!unit_flag(punit, F_SPY))?
 			      _("Sir, the diplomat is waiting for your command"):
 			      _("Sir, the spy is waiting for your command"),
				diplomat_close_callback, NULL,
 			      _("_Bribe Enemy Unit"), diplomat_bribe_callback, 0,
 			      _("_Sabotage Enemy Unit"), spy_sabotage_unit_callback, 0,
 			      _("_Cancel"), NULL, 0,
 			      0);
        
       if(!diplomat_can_do_action(punit, DIPLOMAT_BRIBE, dest_tile))
        message_dialog_button_set_sensitive(shl,"button0",FALSE);
       if(!diplomat_can_do_action(punit, SPY_SABOTAGE_UNIT, dest_tile))
        message_dialog_button_set_sensitive(shl,"button1",FALSE);
    }
  }
}

/****************************************************************
...
*****************************************************************/
bool diplomat_dialog_is_open(void)
{
  return diplomat_dialog_open;
}

/****************************************************************
...
*****************************************************************/
static void caravan_establish_trade_callback(gpointer data)
{
  dsend_packet_unit_establish_trade(&aconnection, caravan_unit_id);
}


/****************************************************************
...
*****************************************************************/
static void caravan_help_build_wonder_callback(gpointer data)
{
  dsend_packet_unit_help_build_wonder(&aconnection, caravan_unit_id);
}

/****************************************************************
...
*****************************************************************/
static void caravan_close_callback(gpointer data)
{
  caravan_dialog = NULL;
  process_caravan_arrival(NULL);
}

/****************************************************************
...
*****************************************************************/
void popup_caravan_dialog(struct unit *punit,
			  struct city *phomecity, struct city *pdestcity)
{
  char buf[128];
  bool can_establish, can_trade;
  
  my_snprintf(buf, sizeof(buf),
	      _("Your caravan from %s reaches the city of %s.\nWhat now?"),
	      phomecity->name, pdestcity->name);
  
  caravan_city_id=pdestcity->id; /* callbacks need these */
  caravan_unit_id=punit->id;
  
  can_trade = can_cities_trade(phomecity, pdestcity);
  can_establish = can_trade
		 && can_establish_trade_route(phomecity, pdestcity);
  
  caravan_dialog = popup_message_dialog(top_vbox,
					_("Your Caravan Has Arrived"), buf,
					caravan_close_callback, NULL,
			   (can_establish ? _("Establish _Traderoute") :
  			   _("Enter Marketplace")),caravan_establish_trade_callback, 0,
			   _("Help build _Wonder"),caravan_help_build_wonder_callback, 0,
			   _("_Keep moving"),NULL, 0,
			   0);
  
  if (!can_trade) {
    message_dialog_button_set_sensitive(caravan_dialog, "button0", FALSE);
  }
  
  if (!unit_can_help_build_wonder(punit, pdestcity)) {
    message_dialog_button_set_sensitive(caravan_dialog, "button1", FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
bool caravan_dialog_is_open(void)
{
  return caravan_dialog != NULL;
}

/****************************************************************
...
*****************************************************************/
static void revolution_callback_yes(gpointer data)
{
  start_revolution();
}

/****************************************************************
...
*****************************************************************/
void popup_revolution_dialog(void)
{
  popup_message_dialog(top_vbox, /*"revolutiondialog"*/_("Revolution!"), 
		       _("You say you wanna revolution?"),
		       dummy_close_callback, NULL, 
		       _("_Yes"),revolution_callback_yes, 0,
		       _("_No"),NULL, 0, 
		       0);
}

/****************************************************************
...
*****************************************************************/
static void pillage_callback(gpointer data)
{
  struct pillage_data *pdata = data;
  struct unit *punit = find_unit_by_id(pdata->unit_id);

  if (!punit) {
    return;
  }

  request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, pdata->what);
}

/****************************************************************
...
*****************************************************************/
static void pillage_close_callback(gpointer data)
{
  struct pillage_data *pillage_datas = data;

  free(pillage_datas);
}

/****************************************************************
...
*****************************************************************/
void popup_pillage_dialog(struct unit *punit,
			  enum tile_special_type may_pillage)
{
  /* +1 for cancel button */
  int i, num = 1;
  enum tile_special_type tmp = may_pillage;
  struct button_descr *buttons;
  struct pillage_data *datas;

  while (tmp != S_NO_SPECIAL) {
    enum tile_special_type what = get_preferred_pillage(tmp);

    tmp &= (~(what | map_get_infrastructure_prerequisite(what)));
    num++;
  }
  buttons = fc_malloc(sizeof(struct button_descr) * num);
  datas = fc_malloc(sizeof(struct pillage_data) * num);

  for (i = 0; i < num - 1; i++) {
    enum tile_special_type what = get_preferred_pillage(may_pillage);

    datas[i].unit_id = punit->id;
    datas[i].what = what;

    buttons[i].text = get_special_name(what);
    buttons[i].callback = pillage_callback;
    buttons[i].data = &datas[i];
    buttons[i].sensitive = TRUE;

    may_pillage &= (~(what | map_get_infrastructure_prerequisite(what)));
  }
  buttons[num - 1].text = _("Cancel");
  buttons[num - 1].callback = NULL;

  base_popup_message_dialog(top_vbox, _("What To Pillage"),
			    _("Select what to pillage:"),
			    pillage_close_callback, datas, num, buttons);
}

/****************************************************************
...
*****************************************************************/
static void popup_mes_close(GtkWidget *dialog_shell)
{
  GtkWidget *parent =
      gtk_object_get_data(GTK_OBJECT(dialog_shell), "parent");
  void (*close_callback) (gpointer) =
      (void (*)(gpointer)) gtk_object_get_data(GTK_OBJECT(dialog_shell),
					       "close_callback");
  gpointer close_callback_data =
      gtk_object_get_data(GTK_OBJECT(dialog_shell), "close_callback_data");
  struct button_descr *buttons =
      gtk_object_get_data(GTK_OBJECT(dialog_shell), "buttons");

  gtk_widget_set_sensitive(parent, TRUE);
  gtk_widget_unref(parent);

  if (close_callback) {
    (*close_callback)(close_callback_data);
  }

  free(buttons);
}

/****************************************************************
...
*****************************************************************/
static gint popup_mes_del_callback(GtkWidget * widget, GdkEvent * event,
				   gpointer data)
{
  GtkWidget *dialog_shell = GTK_WIDGET(data);
  void (*close_callback) (gpointer) =
      (void (*)(gpointer)) gtk_object_get_data(GTK_OBJECT(dialog_shell),
					       "close_callback");

  if (close_callback) {
    popup_mes_close(dialog_shell);
    return FALSE;
  } else {
    return TRUE;
  }
}

/****************************************************************
...
*****************************************************************/
static void popup_mes_handle_callback(GtkWidget * widget, gpointer data)
{
  GtkWidget *dialog_shell = GTK_WIDGET(data);
  int button =
	GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(widget), "button"));
  struct button_descr *buttons =
      gtk_object_get_data(GTK_OBJECT(dialog_shell), "buttons");

  if (buttons[button].callback) {
    (*buttons[button].callback)(buttons[button].data);
  }

  popup_mes_close(dialog_shell);

  gtk_widget_destroy(dialog_shell);
}

/****************************************************************
...
*****************************************************************/
void message_dialog_button_set_sensitive(GtkWidget * shl, const char *bname,
					 bool state)
{
  GtkWidget *button = gtk_object_get_data(GTK_OBJECT(shl), bname);
  gtk_widget_set_sensitive(button, state);
}

/****************************************************************
  Displays a dialog which shows several textual choices. The parent
  widget is deactivated during the time the dialog is shown.

  If the user clicks on one of the buttons the corresponding callback
  is called. If the close_callback is set it is called afterwards.

  If close_callback is unset the dialog can't be closed with the X
  button. If close_callback is set and the user closes the dialog with
  the X button the close_callback is called.

  The dialog is automatically destroyed after use (button click or X
  button).
*****************************************************************/
GtkWidget *base_popup_message_dialog(GtkWidget * parent, 
				     const char *dialogname,
				     const char *text,
				     void (*close_callback) (gpointer),
				     gpointer close_callback_data,
				     int num_buttons,
				     const struct button_descr *buttons)
{
  GtkWidget *dshell, *dlabel, *vbox;
  GtkAccelGroup *accel = gtk_accel_group_new();
  int i;

  /* 
   * To restore the sensitivity later we have to make sure that parent
   * still exists via a reference.
   */
  gtk_widget_ref(parent);
  gtk_widget_set_sensitive(parent, FALSE);
  
  dshell=gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position (GTK_WINDOW(dshell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(dshell));

  gtk_object_set_data(GTK_OBJECT(dshell), "close_callback",
		      (gpointer) close_callback);
  gtk_object_set_data(GTK_OBJECT(dshell), "close_callback_data",
		      (gpointer) close_callback_data);

  gtk_signal_connect(GTK_OBJECT(dshell), "delete_event",
		     GTK_SIGNAL_FUNC(popup_mes_del_callback),
		     (gpointer) dshell);
  gtk_window_set_title( GTK_WINDOW(dshell), dialogname );

  vbox = gtk_vbox_new(0,TRUE);
  gtk_container_add(GTK_CONTAINER(dshell),vbox);

  gtk_container_border_width(GTK_CONTAINER(vbox),5);

  dlabel = gtk_label_new(text);
  gtk_box_pack_start( GTK_BOX( vbox ), dlabel, TRUE, FALSE, 0 );

  gtk_object_set_data(GTK_OBJECT(dshell), "parent",(gpointer)parent);
  gtk_object_set_data(GTK_OBJECT(dshell), "buttons", (gpointer) buttons);

  for (i = 0; i < num_buttons; i++) {
    GtkWidget *button;
    char button_name[512];

    button = gtk_accelbutton_new(buttons[i].text, accel);
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, FALSE, 0);

    my_snprintf(button_name, sizeof(button_name), "button%d", i);
    gtk_object_set_data(GTK_OBJECT(dshell), button_name, button);

    gtk_object_set_data(GTK_OBJECT(button), "button", GINT_TO_POINTER(i));
    
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(popup_mes_handle_callback),
		       (gpointer) dshell);

    gtk_widget_set_sensitive(GTK_WIDGET(button), buttons[i].sensitive);

    if (i == 0) {
      gtk_widget_grab_focus(button);
    }
  }
  
  gtk_widget_show_all( vbox );

  gtk_widget_show(dshell);  

  return dshell;
}

/****************************************************************
 Wrapper for base_popup_message_dialog.

 See also message_dialog_button_set_sensitive.
*****************************************************************/
GtkWidget *popup_message_dialog(GtkWidget * parent, const char *dialogname,
				const char *text,
				void (*close_callback) (gpointer),
				gpointer close_callback_data, ...)
{
  va_list args;
  int i, num = 0;
  struct button_descr *buttons;

  va_start(args, close_callback_data);
  while (va_arg(args, char *)) {
    (void) va_arg(args, void *);
    (void) va_arg(args, gpointer);
    num++;
  }
  va_end(args);

  buttons = fc_malloc(sizeof(struct button_descr) * num);

  va_start(args, close_callback_data);
  for (i = 0; i < num; i++) {
    buttons[i].text = va_arg(args, char *);
    buttons[i].callback = va_arg(args, void *);
    buttons[i].data = va_arg(args, gpointer);
    buttons[i].sensitive = TRUE;
  }
  va_end(args);

  return base_popup_message_dialog(parent, dialogname, text, close_callback,
				   close_callback_data, num, buttons);
}

/**************************************************************************
...
**************************************************************************/
static void unit_select_all_callback(GtkWidget *w, gpointer data)
{
  int i;

  gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy(unit_select_dialog_shell);
  unit_select_dialog_shell = NULL;
  
  for(i=0; i<unit_select_no; i++) {
    struct unit *punit = player_find_unit_by_id(game.player_ptr,
						unit_select_ids[i]);
    if(punit) {
      set_unit_focus(punit);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void unit_select_callback(GtkWidget *w, int id)
{
  struct unit *punit = player_find_unit_by_id(game.player_ptr, id);

  if (punit) {
    set_unit_focus(punit);
  }

  gtk_widget_set_sensitive(top_vbox, TRUE);
  gtk_widget_destroy(unit_select_dialog_shell);
  unit_select_dialog_shell = NULL;
}

static int number_of_columns(int n)
{
#if 0
  /* This would require libm, which isn't worth it for this one little
   * function.  Since MAX_SELECT_UNITS is 100 already, the ifs
   * work fine.  */
  double sqrt(); double ceil();
  return ceil(sqrt((double)n/5.0));
#else
  assert(MAX_SELECT_UNITS == 100);
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
  bool can_ready = FALSE;
  struct unit *unit_list[unit_list_size(&ptile->units)];

  if (!unit_select_dialog_shell){
  gtk_widget_set_sensitive(top_vbox, FALSE);

  unit_select_dialog_shell = gtk_dialog_new();
  gtk_signal_connect(GTK_OBJECT(unit_select_dialog_shell), "delete_event",
		     unit_select_callback, NULL);
  gtk_window_set_position (GTK_WINDOW(unit_select_dialog_shell), GTK_WIN_POS_MOUSE);

  gtk_window_set_title(GTK_WINDOW(unit_select_dialog_shell),
	_("Unit selection") );

  n = MIN(MAX_SELECT_UNITS, unit_list_size(&ptile->units));
  r = number_of_rows(n);

  table=gtk_table_new(r, number_of_columns(n), FALSE);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(unit_select_dialog_shell)->vbox),
	table);  

  fill_tile_unit_list(ptile, unit_list);

  for(i=0; i<n; i++) {
    struct unit *punit = unit_list[i];
    struct unit_type *punittemp=unit_type(punit);
    struct city *pcity;

    /* The "Ready all" button is activated if any units are owned by us. */
    can_ready = can_ready || (unit_owner(punit) == game.player_ptr);

    hbox = gtk_hbox_new(FALSE,10);
    gtk_table_attach_defaults(GTK_TABLE(table), hbox, 
				(i/r), (i/r)+1,
				(i%r), (i%r)+1);
    gtk_container_border_width(GTK_CONTAINER(hbox),5);

    unit_select_ids[i]=punit->id;

    pcity=player_find_city_by_id(game.player_ptr, punit->homecity);

    if (pcity) {
      my_snprintf(buffer, sizeof(buffer), "%s (%s)\n%s",
		  punittemp->name, pcity->name, unit_activity_text(punit));
    } else {
      my_snprintf(buffer, sizeof(buffer), "%s\n%s",
		  punittemp->name, unit_activity_text(punit));
    }

    pix = gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);

    unit_select_commands[i]=gtk_button_new();
    gtk_widget_set_sensitive(unit_select_commands[i],
       can_unit_do_activity(punit, ACTIVITY_IDLE) );
    gtk_widget_set_usize(unit_select_commands[i],
			 UNIT_TILE_WIDTH+4, UNIT_TILE_HEIGHT+4);
    gtk_container_add(GTK_CONTAINER(unit_select_commands[i]), pix);
    gtk_box_pack_start(GTK_BOX(hbox),unit_select_commands[i],
       FALSE, FALSE, 0);

    gtk_pixcomm_clear(GTK_PIXCOMM(pix), FALSE);
    put_unit_gpixmap(punit, GTK_PIXCOMM(pix));

    unit_select_labels[i]=gtk_label_new(buffer);
    gtk_box_pack_start(GTK_BOX(hbox),unit_select_labels[i],
       FALSE, FALSE, 0);

    gtk_signal_connect(GTK_OBJECT(unit_select_commands[i]), "clicked",
       GTK_SIGNAL_FUNC(unit_select_callback), GINT_TO_POINTER(punit->id));
  }
  unit_select_no=i;


  unit_select_close_command=gtk_button_new_with_label(_("Close"));
  gtk_box_pack_start( GTK_BOX(GTK_DIALOG(unit_select_dialog_shell)->action_area),
	unit_select_close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( unit_select_close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( unit_select_close_command );

  unit_select_all_command=gtk_button_new_with_label(_("Ready all"));
  gtk_box_pack_start( GTK_BOX(GTK_DIALOG(unit_select_dialog_shell)->action_area),
	unit_select_all_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( unit_select_all_command, GTK_CAN_DEFAULT );
  gtk_widget_set_sensitive(unit_select_all_command, can_ready);

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
  int class_id;
  int width, height;

  gtk_widget_set_sensitive(top_vbox, FALSE);

  create_races_dialog();

  gtk_widget_show(races_dialog_shell);

  /* The first tab is visible. The others aren't. */
  width = legend_frame[0]->allocation.width;
  height = legend_frame[0]->allocation.height;

  /* 
   * This is a hack to expand the legend label to take all the
   * available space. 
   */
  for (class_id = 0; class_id < num_classes; class_id++) {
    gtk_widget_set_usize(legend[class_id], width, -1);
  }
}

/****************************************************************
...
*****************************************************************/
void popdown_races_dialog(void)
{
  if (races_dialog_shell) {
    int class_id;

    gtk_widget_set_sensitive(top_vbox, TRUE);

    /* 
     * While dialog is being destroyed, it will toggle the race_toggle
     * buttons in turn. eventually races_by_name_callback() will call
     * select_random_race() will try to activate a now-destroyed
     * button with typical results. Let's avoid that. (this is one of
     * the reasons I hate GTK).
     */

    gtk_signal_disconnect_by_func(GTK_OBJECT(notebook),
				  GTK_SIGNAL_FUNC(switch_page_callback),
				  NULL);

    for (class_id = 0; class_id < num_classes; class_id++) {
      gtk_signal_disconnect_by_func(GTK_OBJECT
				    (GTK_COMBO(races_by_name[class_id])->
				     list),
				    GTK_SIGNAL_FUNC(races_by_name_callback),
				    NULL);

      g_list_free(sorted_races_list[class_id]);
      sorted_races_list[class_id] = NULL;
      free(races_toggles[class_id]);
      races_toggles[class_id] = NULL;
    }
    gtk_widget_destroy(races_dialog_shell);
    races_dialog_shell = NULL;
  }
}

/****************************************************************
...
*****************************************************************/
static gint cmp_func(gconstpointer a_p, gconstpointer b_p)
{
  return strcmp(get_nation_name(GPOINTER_TO_INT(a_p)),
		get_nation_name(GPOINTER_TO_INT(b_p)));
}

/****************************************************************
Selectes a leader and the appropriate sex.
Updates the gui elements and the selected_* variables.
*****************************************************************/
static void select_random_leader(void)
{
  int j, leader_num;
  struct leader *leaders;
  char unique_name[MAX_LEN_NAME];
  
  /* weirdness happens by not doing it this way */
  sz_strlcpy(unique_name, 
             gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(leader_name)->entry)));

  gtk_signal_handler_block_by_func(GTK_OBJECT(GTK_COMBO(leader_name)->list), 
                                   leader_name_callback, NULL);
  g_list_free(leader_strings);
  leader_strings = NULL;

  /* fill leader names combo box */
  leaders = get_nation_leaders(selected_nation, &leader_num);
  for(j = 0; j < leader_num; j++) {
    leader_strings = g_list_append(leader_strings, leaders[j].name);
  }
  gtk_combo_set_value_in_list(GTK_COMBO(leader_name), FALSE, FALSE);
  gtk_combo_set_popdown_strings(GTK_COMBO(leader_name), leader_strings);

  gtk_signal_handler_unblock_by_func(GTK_OBJECT(GTK_COMBO(leader_name)->list), 
                                     leader_name_callback, NULL);
  if (!is_name_unique) {
    /* initialize leader names */
    selected_leader = myrand(leader_num);
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(leader_name)->entry),
		       leaders[selected_leader].name);

    /* initialize leader sex */
    selected_sex = leaders[selected_leader].is_male;
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(
                              races_sex_toggles[selected_sex ? 0 : 1]), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(leader_name)->entry), unique_name);
  }
}

/****************************************************************
Selectes a random race and the appropriate city style.
Updates the gui elements and the selected_* variables.
*****************************************************************/
static void select_random_race(void)
{
  int class_id = selected_class;
  int nations_in_class = g_list_length(sorted_races_list[class_id]);
  int index;
  int tries = 0;
  
  /* try to find a free nation */
  while (TRUE) {
    index = myrand(nations_in_class);
    selected_nation =
	GPOINTER_TO_INT(g_list_nth_data(sorted_races_list[class_id], index));
    if (GTK_WIDGET_SENSITIVE(races_toggles[class_id][index])) {
      break;
    }
    if (tries++ > 1000) return;
  }

  /* initialize nation toggle array */
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
			      (races_toggles[class_id][index]), TRUE);

  /* initialize city style */
  selected_city_style =
                      city_style_ridx[get_nation_city_style(selected_nation)];
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(
			city_style_toggles[selected_city_style]), TRUE);
}

/****************************************************************
...
*****************************************************************/
static void switch_page_callback(GtkNotebook * notebook,
				 GtkNotebookPage * page, gint page_num,
				 gpointer data)
{
  selected_class = page_num;
  select_random_race();
}

/****************************************************************
...
*****************************************************************/
void create_races_dialog(void)
{
  int       i, class_id;
  GSList    *sgroup = NULL;
  GSList    *cgroup = NULL;
  GtkWidget *frame, *label;
 
  races_dialog_shell = gtk_dialog_new();
  gtk_window_set_default_size(GTK_WINDOW(races_dialog_shell), 10, 650);
  
  gtk_signal_connect(GTK_OBJECT(races_dialog_shell), "delete_event", 
                     GTK_SIGNAL_FUNC(deleted_callback), NULL);

  gtk_window_set_title(GTK_WINDOW(races_dialog_shell), 
                       _("What Nation Will You Be?"));

  frame = gtk_frame_new(_("Select a nation"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(races_dialog_shell)->vbox),
                     frame, TRUE, TRUE, 0);

  /* ------- Add each nation to one of the class lists ------- */

  num_classes = 1;
  class_names[0] = _("All");

  for (i = 0; i < game.playable_nation_count; i++) {
    bool found = FALSE;
    struct nation_type *nation = get_nation_by_idx(i);

    /* Find the nation's class. */
    for (class_id = 1; class_id < num_classes; class_id++) {
      if (strcmp(nation->class, class_names[class_id]) == 0) {
	found = TRUE;
	break;
      }
    }

    /* Append a new class. */
    if (!found && num_classes < MAX_NUM_ITEMS) {
      class_id = num_classes++;
      class_names[class_id] = nation->class;
    }

    /* Add the nation to the class list. */
    sorted_races_list[class_id] =
      g_list_append(sorted_races_list[class_id], GINT_TO_POINTER(i));

    /* Add the nation to the "All" class. */
    sorted_races_list[0] =
	g_list_append(sorted_races_list[0], GINT_TO_POINTER(i));
  }

  /* ------- create class notebook and add pages ------- */

  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
  gtk_container_add(GTK_CONTAINER(frame), notebook);

  for (class_id = 0; class_id < num_classes; class_id++) {
    GtkWidget *page, *label, *hbox, *scrolledwin;
    int nations_in_class = g_list_length(sorted_races_list[class_id]);
    int per_row, rows;
    GList *race_names = NULL;
    GSList *group = NULL;

    freelog(LOG_DEBUG, "  %s[%d] has %d nations",
	    skip_intl_qualifier_prefix(class_names[class_id]), class_id,
	    nations_in_class);
    sorted_races_list[class_id] =
	g_list_sort(sorted_races_list[class_id], cmp_func);

    for (i = 0; i < nations_in_class; i++) {
      race_names =
	  g_list_append(race_names,
		(gchar *)get_nation_by_idx(GPOINTER_TO_INT(g_list_nth_data
			(sorted_races_list[class_id], i)))->name);
    }

    per_row = 8;

    if (nations_in_class == 0) {
      rows = 0;
    } else {
      rows = ((nations_in_class - 1) / per_row) + 1;
    }

    page = gtk_vbox_new(FALSE, 1);
    label = gtk_label_new(Q_(class_names[class_id]));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);

    scrolledwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(page), scrolledwin,1,1,0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    races_toggles_form[class_id] = gtk_table_new(per_row, rows, FALSE);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (scrolledwin),
					  races_toggles_form[class_id]);

    races_toggles[class_id] =
	fc_calloc(nations_in_class, sizeof(GtkWidget *));

    /* ------ add nation flag array to page ------ */

    for (i = 0; i < g_list_length(sorted_races_list[class_id]); i++) {
      gint nat_id =
	  GPOINTER_TO_INT(g_list_nth_data(sorted_races_list[class_id], i));
      SPRITE *s = crop_blankspace(get_nation_by_idx(nat_id)->flag_sprite);
      GtkWidget *flag = gtk_pixmap_new(s->pixmap, s->mask);

      races_toggles[class_id][i] = gtk_radio_button_new(group);
      gtk_misc_set_alignment(GTK_MISC(flag), 0, 0.5);
      gtk_misc_set_padding(GTK_MISC(flag), 6, 4);

      gtk_container_add(GTK_CONTAINER(races_toggles[class_id][i]), flag);
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
				  (races_toggles[class_id][i]), FALSE);

      group =
	  gtk_radio_button_group(GTK_RADIO_BUTTON
				 (races_toggles[class_id][i]));
      gtk_table_attach_defaults(GTK_TABLE(races_toggles_form[class_id]),
				races_toggles[class_id][i], i % per_row,
				i % per_row + 1, i / per_row,
				i / per_row + 1);
    }

    /* ------ add combobox to choose nation by name to page ------ */

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 5);

    label = gtk_label_new(_("Nation:"));
    races_by_name[class_id] = gtk_combo_new();
    gtk_editable_set_editable(GTK_EDITABLE
			      (GTK_COMBO(races_by_name[class_id])->entry),
			      FALSE);

    gtk_combo_set_popdown_strings(GTK_COMBO(races_by_name[class_id]),
				  race_names);
    gtk_combo_set_value_in_list(GTK_COMBO(races_by_name[class_id]), TRUE,
				FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), races_by_name[class_id],
		       FALSE, FALSE, 0);

    /* ------ add info about class and legend to page ------ */

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 5);
    label = gtk_label_new(_("Class:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    class[class_id] = gtk_label_new("content");
    gtk_box_pack_start(GTK_BOX(hbox), class[class_id], FALSE, FALSE, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 5);
    legend[class_id] = gtk_label_new("content");
    gtk_label_set_line_wrap(GTK_LABEL(legend[class_id]), TRUE);
    gtk_label_set_justify(GTK_LABEL(legend[class_id]), GTK_JUSTIFY_FILL);

    legend_frame[class_id] = gtk_frame_new(_("Description"));
    gtk_box_pack_start(GTK_BOX(hbox), legend_frame[class_id], TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(legend_frame[class_id]), legend[class_id]);
    /* ------- contruction of one page finished ------- */
  }

  selected_class = 0;

  /* ------- leader sex toggles ------- */

  frame = gtk_frame_new(_("Leader"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(races_dialog_shell)->vbox),
                     frame, FALSE, FALSE, 0);

  races_sex_toggles_form = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), races_sex_toggles_form); 

  races_sex_toggles[0] = gtk_radio_button_new_with_label(sgroup, _("Male"));
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON( races_sex_toggles[0]),
                              FALSE);
  sgroup = gtk_radio_button_group(GTK_RADIO_BUTTON(races_sex_toggles[0]));
  gtk_box_pack_end(GTK_BOX(races_sex_toggles_form), races_sex_toggles[0],
		   FALSE, FALSE, 0);
  races_sex_toggles[1] = gtk_radio_button_new_with_label(sgroup, _("Female"));
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(races_sex_toggles[1]),
                              FALSE);
  sgroup = gtk_radio_button_group(GTK_RADIO_BUTTON(races_sex_toggles[1]));
  gtk_box_pack_end(GTK_BOX(races_sex_toggles_form), races_sex_toggles[1],
		   FALSE, FALSE, 0);
  leader_name = gtk_combo_new();

  label = gtk_label_new(_("Leader:"));
  gtk_box_pack_start(GTK_BOX(races_sex_toggles_form), label, FALSE, FALSE,
		     0);
  gtk_box_pack_start(GTK_BOX(races_sex_toggles_form), leader_name, FALSE,
		     FALSE, 4);
 
  GTK_WIDGET_SET_FLAGS(leader_name, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(leader_name);

  /* ------- city style toggles ------- */

  /* find out styles that can be used at the game beginning */
   
  for(i = 0, b_s_num = 0; i < game.styles_count && i < 64; i++) {
    if(city_styles[i].techreq == A_NONE) {
      city_style_idx[b_s_num] = i;
      city_style_ridx[i] = b_s_num;
      b_s_num++;
    }
  }

  free(city_style_toggles);
  city_style_toggles = fc_calloc(b_s_num, sizeof(struct GtkWidget*));
  
  frame = gtk_frame_new(_("Select your city style"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(races_dialog_shell)->vbox),
                     frame, FALSE, FALSE, 0);

  city_style_toggles_form = gtk_table_new(1, b_s_num, TRUE);
  gtk_container_add(GTK_CONTAINER(frame), city_style_toggles_form); 

  for(i = 0; i < b_s_num; i++) {
    GtkWidget *box, *sub_box;
    SPRITE *s;

    city_style_toggles[i] = gtk_radio_button_new(cgroup);
    box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(city_style_toggles[i]), box);
    gtk_box_pack_start(GTK_BOX(box),
                       gtk_label_new(get_city_style_name(city_style_idx[i])),
                       FALSE, FALSE, 4);
    sub_box = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(box), sub_box);
    s = crop_blankspace(sprites.city.tile[i][0]);
    gtk_box_pack_start(GTK_BOX(sub_box), gtk_pixmap_new(s->pixmap, s->mask),
 		       FALSE, FALSE, 4);
    if ((s->width < 80) && (city_styles[i].tiles_num > 1)){
      s = crop_blankspace(sprites.city.tile[i][1]);
      gtk_box_pack_start(GTK_BOX(sub_box), gtk_pixmap_new(s->pixmap, s->mask),
 			 FALSE, FALSE, 4);
    }
    if ((s->width < 40) && (city_styles[i].tiles_num > 2)){
      s = crop_blankspace(sprites.city.tile[i][2]);
      gtk_box_pack_start(GTK_BOX(sub_box), gtk_pixmap_new(s->pixmap, s->mask),
 			 FALSE, FALSE, 4);
    }

    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(city_style_toggles[i]), 
                                FALSE);
    cgroup = gtk_radio_button_group(GTK_RADIO_BUTTON(city_style_toggles[i]));
    gtk_table_attach_defaults( GTK_TABLE(city_style_toggles_form), 
 			       city_style_toggles[i],
 			       i, i+1, 0, 1);
  }

  /* ------- OK/Disc/Quit buttons ------- */

  races_ok_command = gtk_button_new_with_label(_("Ok"));
  GTK_WIDGET_SET_FLAGS(races_ok_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(races_dialog_shell)->action_area),
                     races_ok_command, TRUE, TRUE, 0);

  races_disc_command = gtk_button_new_with_label(_("Disconnect"));
  GTK_WIDGET_SET_FLAGS(races_disc_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX( GTK_DIALOG(races_dialog_shell)->action_area),
		     races_disc_command, TRUE, TRUE, 0);

  races_quit_command = gtk_button_new_with_label(_("Quit"));
  GTK_WIDGET_SET_FLAGS(races_quit_command, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX( GTK_DIALOG(races_dialog_shell)->action_area),
		      races_quit_command, TRUE, TRUE, 0);

  /* ------- connect callback functions ------- */

  for (class_id = 0; class_id < num_classes; class_id++) {
    for (i = 0; i < g_list_length(sorted_races_list[class_id]); i++) {
      gtk_signal_connect(GTK_OBJECT(races_toggles[class_id][i]), "toggled",
			 GTK_SIGNAL_FUNC(races_toggles_callback),
			 g_list_nth_data(sorted_races_list[class_id], i));
    }

    gtk_signal_connect(GTK_OBJECT(GTK_COMBO(races_by_name[class_id])->list),
		       "selection_changed",
		       GTK_SIGNAL_FUNC(races_by_name_callback), NULL);
  }

  for(i = 0; i < 2; i++) {
    gtk_signal_connect(GTK_OBJECT(races_sex_toggles[i]), "toggled",
                       GTK_SIGNAL_FUNC(races_sex_toggles_callback), NULL);
  }

  for(i = 0; i < b_s_num; i++) {
     gtk_signal_connect(GTK_OBJECT(city_style_toggles[i]), "toggled",
	                GTK_SIGNAL_FUNC(city_style_toggles_callback), NULL);
  }

  gtk_signal_connect(GTK_OBJECT( GTK_COMBO(leader_name)->list), 
		      "selection_changed",
		      GTK_SIGNAL_FUNC(leader_name_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(races_ok_command), "clicked",
			GTK_SIGNAL_FUNC(races_buttons_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(races_disc_command), "clicked",
		      GTK_SIGNAL_FUNC(races_buttons_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(races_quit_command), "clicked",
		      GTK_SIGNAL_FUNC(races_buttons_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(notebook), "switch-page",
		     GTK_SIGNAL_FUNC(switch_page_callback), NULL);

 
  /* ------- set initial selections ------- */

  select_random_race();
  select_random_leader();

  gtk_widget_grab_default(races_ok_command);

  gtk_widget_show_all(GTK_DIALOG(races_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(races_dialog_shell)->action_area);
}

/**************************************************************************
...
**************************************************************************/ 
static void races_by_name_callback(GtkWidget * w, gpointer data)
{
  int i, class_id = selected_class;
  char *chosen =
      gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(races_by_name[class_id])->
				   entry));

  for (i = 0; i < g_list_length(sorted_races_list[class_id]); i++) {
    if (strcmp(chosen,
	       get_nation_by_idx(GPOINTER_TO_INT
				 (g_list_nth_data
				  (sorted_races_list[class_id],
				   i)))->name) == 0) {
      if (GTK_WIDGET_SENSITIVE(races_toggles[class_id][i])) {
	gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
				    (races_toggles[class_id][i]), TRUE);
	break;
      } else {
	/* That one's taken */
	select_random_race();
      }
    }
  }
} 
 
/**************************************************************************
 ...
**************************************************************************/
void races_toggles_set_sensitive(bool *nations_used)
{
  int i, class_id;

  for (class_id = 0; class_id < num_classes; class_id++) {
    int nations_in_class = g_list_length(sorted_races_list[class_id]);

    for (i = 0; i < nations_in_class; i++) {
      gtk_widget_set_sensitive(races_toggles[class_id][i], TRUE);
    }

    for (i = 0; i < game.playable_nation_count; i++) {
      if (nations_used[i]) {
	int index =
	  g_list_index(sorted_races_list[class_id], GINT_TO_POINTER(i));

	if (index != -1) {
	  gtk_widget_set_sensitive(races_toggles[class_id][index], FALSE);
	}
      }
    }
  }

  if (nations_used[selected_nation]) {
    select_random_race();
  }
}

/**************************************************************************
...
**************************************************************************/
static void leader_name_callback(GtkWidget *w, gpointer data)
{
  Nation_Type_id nation = races_buttons_get_current();
  const char *leader =
                  gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(leader_name)->entry));

  if (check_nation_leader_name(nation, leader)) {
    is_name_unique = FALSE;
    selected_sex = get_nation_leader_sex(nation, leader);
    gtk_toggle_button_set_state(
           GTK_TOGGLE_BUTTON(races_sex_toggles[selected_sex ? 0 : 1]), TRUE);
  } else {
    is_name_unique = TRUE;
  }
}

/**************************************************************************
...
**************************************************************************/
static void races_toggles_callback(GtkWidget * w, gpointer race_id_p)
{
  int class_id = selected_class;

  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
    /* don't do anything if signal is untoggling the button */
    return;
  }

  selected_nation = GPOINTER_TO_INT(race_id_p);

  gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(races_by_name[class_id])->entry),
		     get_nation_by_idx(selected_nation)->name);
  gtk_label_set_text(GTK_LABEL(class[class_id]),
		     get_nation_by_idx(selected_nation)->class);
  gtk_label_set_text(GTK_LABEL(legend[class_id]),
		     get_nation_by_idx(selected_nation)->legend);

  select_random_leader();

  selected_city_style =
      city_style_ridx[get_nation_city_style(selected_nation)];
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
			      (city_style_toggles[selected_city_style]),
			      TRUE);
}

/**************************************************************************
...
**************************************************************************/
static void races_sex_toggles_callback(GtkWidget *w, gpointer data)
{
  selected_sex = (w == races_sex_toggles[0]) ? 1 : 0;
}

/**************************************************************************
...
**************************************************************************/
static void city_style_toggles_callback(GtkWidget *w, gpointer data)
{
  int i;

  for(i = 0; i < b_s_num; i++) {
    if(w == city_style_toggles[i]) {
      selected_city_style = i;
      return;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static int races_buttons_get_current(void)
{
  return selected_nation;
}

/**************************************************************************
...
**************************************************************************/
static int sex_buttons_get_current(void)
{
  return selected_sex;
}

/**************************************************************************
...
**************************************************************************/
static int city_style_get_current(void)
{
  return selected_city_style;
}

/**************************************************************************
...
**************************************************************************/
static void races_buttons_callback(GtkWidget *w, gpointer data)
{
  int selected, selected_sex, selected_style;
  char *s;

  if(w == races_quit_command) {
    exit(EXIT_SUCCESS);
  } else if(w == races_disc_command) {
    popdown_races_dialog();
    disconnect_from_server();
    return;
  }

  if((selected = races_buttons_get_current()) == -1) {
    append_output_window(_("You must select a nation."));
    return;
  }

  if((selected_sex = sex_buttons_get_current()) == -1) {
    append_output_window(_("You must select your sex."));
    return;
  }

  if((selected_style = city_style_get_current()) == -1) {
    append_output_window(_("You must select your city style."));
    return;
  }

  s = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(leader_name)->entry));

  /* perform a minimum of sanity test on the name */
  if (strlen(s) == 0) {
    append_output_window(_("You must type a legal name."));
    return;
  }

  dsend_packet_nation_select_req(&aconnection, selected,
				 selected_sex, s,
				 city_style_idx[selected_style]);
  /* reset this variable */
  is_name_unique = FALSE;  
}

/**************************************************************************
  Destroys its widget.  Usefull for a popdown callback on pop-ups that
  won't get resused.
**************************************************************************/
void destroy_me_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(w);
}

/**************************************************************************
  Adjust tax rates from main window
**************************************************************************/
void taxrates_callback(GtkWidget * w, GdkEventButton * ev, gpointer data)
{
  common_taxrates_callback((size_t) data);
}

void dummy_close_callback(gpointer data){}

/********************************************************************** 
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
***********************************************************************/
void popdown_all_game_dialogs(void)
{
  popdown_city_report_dialog();
  popdown_meswin_dialog();
  popdown_science_dialog();
  popdown_economy_report_dialog();
  popdown_activeunits_report_dialog();
  popdown_players_dialog();
  popdown_notify_dialog();
}
