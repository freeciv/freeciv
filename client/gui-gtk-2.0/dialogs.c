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
#include <stdarg.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "log.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "rand.h"
#include "support.h"

#include "chatline.h"
#include "citydlg.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "control.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"
#include "tilespec.h"

#include "dialogs.h"

/******************************************************************/
GtkWidget *message_dialog_start(GtkWindow *parent, const gchar *name,
				const gchar *text);
void message_dialog_add(GtkWidget *dshell, const gchar *label,
			GCallback handler, gpointer data);
void message_dialog_end(GtkWidget *dshell);

void message_dialog_set_hide(GtkWidget *dshell, gboolean setting);

/******************************************************************/
static GtkWidget  *races_dialog_shell=NULL;
static GtkWidget  *races_toggles_form;
static GtkWidget  *races_by_name;
static GtkWidget  *races_sex_toggles_form;
static GtkWidget  *city_style_toggles_form;
static GtkWidget **races_toggles=NULL,          /* toggle race */
                  *races_sex_toggles[2],        /* Male/Female */
                 **city_style_toggles = NULL,
                  *races_name;                  /* leader name */
static GList      *leader_strings = NULL;
static GList      *sorted_races_list = NULL; /* contains a list of race
					      ids sorted by the race
					      name. Is valid as long
					      as the races_dialog is
					      poped up. */
/******************************************************************/
static GtkWidget  *spy_tech_shell;
static int         steal_advance;

/******************************************************************/
static GtkWidget  *spy_sabotage_shell;
static int         sabotage_improvement;

/******************************************************************/
#define NUM_SELECT_UNIT_COLS 4
#define SELECT_UNIT_READY  1
#define SELECT_UNIT_SENTRY 2

struct unit_select_node {
  GtkWidget *cmd;
  GtkWidget *pix;
};

static GtkWidget *unit_select_dialog_shell;
static GtkWidget *unit_select_table;
static struct unit_select_node *unit_select_nodes;
static int unit_select_no;
static struct tile *unit_select_ptile;
static GtkTooltips *unit_select_tips;

static int races_buttons_get_current(void);
static int sex_buttons_get_current(void);
static int city_style_get_current(void);

static void create_races_dialog	(void);
static void races_command_callback(GtkWidget *w, gint response_id);
static void races_toggles_callback(GtkWidget * w, gpointer race_id_p);
static void races_sex_toggles_callback ( GtkWidget *w, gpointer data );
static void races_by_name_callback(GtkWidget *w, gpointer data);
static void races_name_callback	( GtkWidget *w, gpointer data );
static void city_style_toggles_callback ( GtkWidget *w, gpointer data );

static int selected_nation;
static int selected_leader;
static bool is_name_unique = FALSE;
static int selected_sex;
static int selected_city_style;
static int city_style_idx[64];        /* translation table basic style->city_style  */
static int city_style_ridx[64];       /* translation table the other way            */
                               /* they in fact limit the num of styles to 64 */
static int b_s_num; /* number of basic city styles, i.e. those that you can start with */

static int is_showing_government_dialog;

static int is_showing_pillage_dialog = FALSE;
static int unit_to_use_to_pillage;

static int caravan_city_id;
static int caravan_unit_id;

static GtkWidget *diplomat_dialog;
static int diplomat_id;
static int diplomat_target_id;

static GtkWidget *caravan_dialog;

static int is_showing_unit_connect_dialog = FALSE;
static int unit_to_use_to_connect;
static int connect_unit_x;
static int connect_unit_y;

/****************************************************************
...
*****************************************************************/
void popup_notify_dialog(char *caption, char *headline, char *lines)
{
  GtkWidget *shell, *label, *headline_label, *sw;

  shell = gtk_dialog_new_with_buttons(caption,
	GTK_WINDOW(toplevel),
	0,
	GTK_STOCK_CLOSE,
	GTK_RESPONSE_CLOSE,
	NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(shell),
    GTK_RESPONSE_CLOSE);
  g_signal_connect(shell, "response", G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_set_name(shell, "Freeciv");

  headline_label = gtk_label_new(headline);   
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox),
		     headline_label, FALSE, FALSE, 0);
  gtk_widget_set_name(headline_label, "notify label");

  gtk_label_set_justify(GTK_LABEL(headline_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(headline_label), 0.0, 0.0);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  label = gtk_label_new(lines);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw), label);

  gtk_widget_set_name(label, "notify label");
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), sw,
    TRUE, TRUE, 0);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);

  gtk_widget_set_size_request(shell, -1, 265);
  gtk_window_set_position(GTK_WINDOW(shell),
    GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_present(GTK_WINDOW(shell));
}

/****************************************************************
...
*****************************************************************/
static void notify_goto_response(GtkWidget *w, gint response)
{
  struct city *pcity = NULL;
  int x, y;

  x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "x"));
  y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "y"));

  switch (response) {
  case 1:
    center_tile_mapcanvas(x, y);
    break;
  case 2:
    pcity = map_get_city(x, y);

    if (center_when_popup_city) {
      center_tile_mapcanvas(x, y);
    }

    if (pcity) {
      popup_city_dialog(pcity, 0);
    }
    break;
  }
  gtk_widget_destroy(w);
}

/****************************************************************
...
*****************************************************************/
void popup_notify_goto_dialog(char *headline, char *lines, int x, int y)
{
  GtkWidget *shell, *label, *goto_command, *popcity_command;
  
  shell = gtk_dialog_new_with_buttons(headline,
        GTK_WINDOW(toplevel),
        0,
        GTK_STOCK_CLOSE,
        GTK_RESPONSE_CLOSE,
        NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_ACCEPT);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_CENTER_ON_PARENT);

  label = gtk_label_new(lines);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), label);
  gtk_widget_show(label);
  
  goto_command = gtk_stockbutton_new(GTK_STOCK_JUMP_TO,
	_("_Goto location"));
  gtk_dialog_add_action_widget(GTK_DIALOG(shell), goto_command, 1);
  gtk_widget_show(goto_command);

  popcity_command = gtk_stockbutton_new(GTK_STOCK_ZOOM_IN,
	_("_Popup City"));
  gtk_dialog_add_action_widget(GTK_DIALOG(shell), popcity_command, 2);
  gtk_widget_show(popcity_command);


  if (x == -1 || y == -1) {
    gtk_widget_set_sensitive(goto_command, FALSE);
    gtk_widget_set_sensitive(popcity_command, FALSE);
  } else {
    struct city *pcity;

    pcity = map_get_city(x, y);
    gtk_widget_set_sensitive(popcity_command,
      (pcity && city_owner(pcity) == game.player_ptr));
  }

  g_object_set_data(G_OBJECT(shell), "x", GINT_TO_POINTER(x));
  g_object_set_data(G_OBJECT(shell), "y", GINT_TO_POINTER(y));

  g_signal_connect(shell, "response", G_CALLBACK(notify_goto_response), NULL);
  gtk_widget_show(shell);
}


/****************************************************************
...
*****************************************************************/
static void bribe_response(GtkWidget *w, gint response)
{
  if (response == GTK_RESPONSE_YES) {
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_BRIBE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(w);
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...  Ask the server how much the bribe is
*****************************************************************/
static void diplomat_bribe_callback(GtkWidget *w, gpointer data)
{
  struct packet_generic_integer packet;

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
  GtkWidget *shell;
  
  if(game.player_ptr->economic.gold>=punit->bribe_cost) {
    shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
      0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
      _("Bribe unit for %d gold?\nTreasury contains %d gold."),
      punit->bribe_cost, game.player_ptr->economic.gold);
    gtk_window_set_title(GTK_WINDOW(shell), _("Bribe Enemy Unit"));
  } else {
    shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
      0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      _("Bribing the unit costs %d gold.\nTreasury contains %d gold."),
      punit->bribe_cost, game.player_ptr->economic.gold);
    gtk_window_set_title(GTK_WINDOW(shell), _("Traitors Demand Too Much!"));
  }
  gtk_window_present(GTK_WINDOW(shell));
  
  g_signal_connect(shell, "response", G_CALLBACK(bribe_response), NULL);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_sabotage_callback(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;
    
    req.action_type=DIPLOMAT_SABOTAGE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value = -1;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_investigate_callback(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_INVESTIGATE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void spy_sabotage_unit_callback(GtkWidget *w, gpointer data)
{
  struct packet_diplomat_action req;
  
  req.action_type=SPY_SABOTAGE_UNIT;
  req.diplomat_id=diplomat_id;
  req.target_id=diplomat_target_id;
  
  send_packet_diplomat_action(&aconnection, &req);

  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_embassy_callback(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) && 
     (find_city_by_id(diplomat_target_id))) { 
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_EMBASSY;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void spy_poison_callback(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    struct packet_diplomat_action req;

    req.action_type=SPY_POISON;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_steal_callback(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) && 
     find_city_by_id(diplomat_target_id)) { 
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_STEAL;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;
    req.value=0;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void spy_advances_response(GtkWidget *w, gint response, gpointer data)
{
  if (response == GTK_RESPONSE_ACCEPT && steal_advance > 0) {
    if (find_unit_by_id(diplomat_id) && 
        find_city_by_id(diplomat_target_id)) { 
      struct packet_diplomat_action req;
    
      req.action_type = DIPLOMAT_STEAL;
      req.value = steal_advance;
      req.diplomat_id = diplomat_id;
      req.target_id = diplomat_target_id;

      send_packet_diplomat_action(&aconnection, &req);
    }
  }
  gtk_widget_destroy(spy_tech_shell);
  spy_tech_shell = NULL;

  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void spy_advances_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &steal_advance, -1);
    
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    steal_advance = 0;
	  
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void create_advances_list(struct player *pplayer,
				 struct player *pvictim)
{  
  GtkWidget *sw, *label, *vbox, *view;
  int i, j;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  spy_tech_shell = gtk_dialog_new_with_buttons(_("Steal Technology"),
    GTK_WINDOW(toplevel),
    0,
    GTK_STOCK_CANCEL,
    GTK_RESPONSE_CANCEL,
    _("_Steal"),
    GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_window_set_position(GTK_WINDOW(spy_tech_shell), GTK_WIN_POS_MOUSE);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_tech_shell),
				  GTK_RESPONSE_ACCEPT);

  label = gtk_frame_new(_("Select Advance to Steal"));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(spy_tech_shell)->vbox), label);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(label), vbox);
      
  store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
						 "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("_Advances:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_container_add(GTK_CONTAINER(vbox), label);
  
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  
  gtk_container_add(GTK_CONTAINER(vbox), sw);

  /* Now populate the list */
  j = 0;

  if (pvictim) { /* you don't want to know what lag can do -- Syela */
    GtkTreeIter it;
    GValue value = { 0, };

    for(i=A_FIRST; i<game.num_tech_types; i++) {
      if(get_invention(pvictim, i)==TECH_KNOWN && 
	 (get_invention(pplayer, i)==TECH_UNKNOWN || 
	  get_invention(pplayer, i)==TECH_REACHABLE)) {
	gtk_list_store_append(store, &it);

	g_value_init(&value, G_TYPE_STRING);
	g_value_set_static_string(&value, advances[i].name);
	gtk_list_store_set_value(store, &it, 0, &value);
	g_value_unset(&value);
	gtk_list_store_set(store, &it, 1, i, -1);
        j++;
      }
    }

    if(j > 0) {
      gtk_list_store_append(store, &it);

      g_value_init(&value, G_TYPE_STRING);
      g_value_set_static_string(&value, _("At Spy's Discretion"));
      gtk_list_store_set_value(store, &it, 0, &value);
      g_value_unset(&value);
      gtk_list_store_set(store, &it, 1, game.num_tech_types, -1);
    }
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_tech_shell),
    GTK_RESPONSE_ACCEPT, FALSE);
  
  gtk_widget_show_all(GTK_DIALOG(spy_tech_shell)->vbox);

  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), "changed",
		   G_CALLBACK(spy_advances_callback), NULL);
  g_signal_connect(spy_tech_shell, "response",
		   G_CALLBACK(spy_advances_response), NULL);
  
  steal_advance = 0;

  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************
...
*****************************************************************/
static void spy_improvements_response(GtkWidget *w, gint response, gpointer data)
{
  if (response == GTK_RESPONSE_ACCEPT && sabotage_improvement > -2) {
    if (find_unit_by_id(diplomat_id) && 
        find_city_by_id(diplomat_target_id)) { 
      struct packet_diplomat_action req;
    
      req.action_type = DIPLOMAT_SABOTAGE;
      req.value = sabotage_improvement+1;
      req.diplomat_id = diplomat_id;
      req.target_id = diplomat_target_id;

      send_packet_diplomat_action(&aconnection, &req);
    }
  }
  gtk_widget_destroy(spy_sabotage_shell);
  spy_sabotage_shell = NULL;

  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void spy_improvements_callback(GtkTreeSelection *select, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;

  if (gtk_tree_selection_get_selected(select, &model, &it)) {
    gtk_tree_model_get(model, &it, 1, &sabotage_improvement, -1);
    
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
      GTK_RESPONSE_ACCEPT, TRUE);
  } else {
    sabotage_improvement = -2;
	  
    gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
      GTK_RESPONSE_ACCEPT, FALSE);
  }
}

/****************************************************************
...
*****************************************************************/
static void create_improvements_list(struct player *pplayer,
				     struct city *pcity)
{  
  GtkWidget *sw, *label, *vbox, *view;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;
  GtkTreeIter it;
  
  spy_sabotage_shell = gtk_dialog_new_with_buttons(_("Sabotage Improvements"),
    GTK_WINDOW(toplevel),
    0,
    GTK_STOCK_CANCEL,
    GTK_RESPONSE_CANCEL,
    _("_Sabotage"), 
    GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_window_set_position(GTK_WINDOW(spy_sabotage_shell), GTK_WIN_POS_MOUSE);

  gtk_dialog_set_default_response(GTK_DIALOG(spy_sabotage_shell),
				  GTK_RESPONSE_ACCEPT);

  label = gtk_frame_new(_("Select Improvement to Sabotage"));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(spy_sabotage_shell)->vbox), label);

  vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(label), vbox);
      
  store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
						 "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("_Improvements:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_container_add(GTK_CONTAINER(vbox), label);
  
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_widget_set_size_request(sw, -1, 200);
  
  gtk_container_add(GTK_CONTAINER(vbox), sw);

  /* Now populate the list */
  gtk_list_store_append(store, &it);
  gtk_list_store_set(store, &it, 0, _("City Production"), 1, -1, -1);

  built_impr_iterate(pcity, i) {
    if (i != B_PALACE && !is_wonder(i)) {
      gtk_list_store_append(store, &it);
      gtk_list_store_set(store, &it, 0, get_impr_name_ex(pcity, i), 1, i, -1);
    }  
  } built_impr_iterate_end;

  gtk_list_store_append(store, &it);
  gtk_list_store_set(store, &it, 0, _("At Spy's Discretion"), 1, B_LAST, -1);

  gtk_dialog_set_response_sensitive(GTK_DIALOG(spy_sabotage_shell),
    GTK_RESPONSE_ACCEPT, FALSE);
  
  gtk_widget_show_all(GTK_DIALOG(spy_sabotage_shell)->vbox);

  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), "changed",
		   G_CALLBACK(spy_improvements_callback), NULL);
  g_signal_connect(spy_sabotage_shell, "response",
		   G_CALLBACK(spy_improvements_response), NULL);

  sabotage_improvement = -2;
	  
  gtk_tree_view_focus(GTK_TREE_VIEW(view));
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
  
  if(!spy_tech_shell){
    create_advances_list(game.player_ptr, pvictim);
    gtk_window_present(GTK_WINDOW(spy_tech_shell));
  }
}

/****************************************************************
 Requests up-to-date list of improvements, the return of
 which will trigger the popup_sabotage_dialog() function.
*****************************************************************/
static void spy_request_sabotage_list(GtkWidget *w, gpointer data)
{
  if(find_unit_by_id(diplomat_id) &&
     (find_city_by_id(diplomat_target_id))) {
    struct packet_diplomat_action req;

    req.action_type = SPY_GET_SABOTAGE_LIST;
    req.diplomat_id = diplomat_id;
    req.target_id = diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
}

/****************************************************************
 Pops-up the Spy sabotage dialog, upon return of list of
 available improvements requested by the above function.
*****************************************************************/
void popup_sabotage_dialog(struct city *pcity)
{
  if(!spy_sabotage_shell){
    create_improvements_list(game.player_ptr, pcity);
    gtk_window_present(GTK_WINDOW(spy_sabotage_shell));
  }
}

/****************************************************************
...  Ask the server how much the revolt is going to cost us
*****************************************************************/
static void diplomat_incite_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity;
  struct packet_generic_integer packet;

  if(find_unit_by_id(diplomat_id) && 
     (pcity=find_city_by_id(diplomat_target_id))) { 
    packet.value = diplomat_target_id;
    send_packet_generic_integer(&aconnection, PACKET_INCITE_INQ, &packet);
  }
}

/****************************************************************
...
*****************************************************************/
static void incite_response(GtkWidget *w, gint response)
{
  if (response == GTK_RESPONSE_YES) {
    struct packet_diplomat_action req;

    req.action_type=DIPLOMAT_INCITE;
    req.diplomat_id=diplomat_id;
    req.target_id=diplomat_target_id;

    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(w);
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
Popup the yes/no dialog for inciting, since we know the cost now
*****************************************************************/
void popup_incite_dialog(struct city *pcity)
{
  GtkWidget *shell;
  
  if (pcity->incite_revolt_cost == INCITE_IMPOSSIBLE_COST) {
    shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
      0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      _("You can't incite a revolt in %s."),
      pcity->name);
    gtk_window_set_title(GTK_WINDOW(shell), _("City can't be incited!"));
  } else if (game.player_ptr->economic.gold >= pcity->incite_revolt_cost) {
    shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
      0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
      _("Incite a revolt for %d gold?\nTreasury contains %d gold."),
      pcity->incite_revolt_cost, game.player_ptr->economic.gold);
    gtk_window_set_title(GTK_WINDOW(shell), _("Incite a Revolt!"));
  } else {
    shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
      0,
      GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
      _("Inciting a revolt costs %d gold.\nTreasury contains %d gold."),
      pcity->incite_revolt_cost, game.player_ptr->economic.gold);
    gtk_window_set_title(GTK_WINDOW(shell), _("Traitors Demand Too Much!"));
  }
  gtk_window_present(GTK_WINDOW(shell));
  
  g_signal_connect(shell, "response", G_CALLBACK(incite_response), NULL);
}


/****************************************************************
  Callback from diplomat/spy dialog for "keep moving".
  (This should only occur when entering allied city.)
*****************************************************************/
static void diplomat_keep_moving_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  struct city *pcity;
  
  if( (punit=find_unit_by_id(diplomat_id))
      && (pcity=find_city_by_id(diplomat_target_id))
      && !same_pos(punit->x, punit->y, pcity->x, pcity->y)) {
    struct packet_diplomat_action req;
    req.action_type = DIPLOMAT_MOVE;
    req.diplomat_id = diplomat_id;
    req.target_id = diplomat_target_id;
    send_packet_diplomat_action(&aconnection, &req);
  }
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
static void diplomat_destroy_callback(GtkWidget *w, gpointer data)
{
  diplomat_dialog = NULL;
  process_diplomat_arrival(NULL, 0);
}


/****************************************************************
...
*****************************************************************/
static void diplomat_cancel_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(diplomat_dialog);
}

/****************************************************************
...
*****************************************************************/
void popup_diplomat_dialog(struct unit *punit, int dest_x, int dest_y)
{
  struct city *pcity;
  struct unit *ptunit;
  GtkWidget *shl;
  char buf[128];

  diplomat_id = punit->id;

  if ((pcity = map_get_city(dest_x, dest_y))) {
    /* Spy/Diplomat acting against a city */

    diplomat_target_id = pcity->id;
    my_snprintf(buf, sizeof(buf),
		_("Your %s has arrived at %s.\nWhat is your command?"),
		unit_name(punit->type), pcity->name);

    if (!unit_flag(punit, F_SPY)){
      shl = popup_message_dialog(GTK_WINDOW(toplevel),
	_(" Choose Your Diplomat's Strategy"), buf,
	_("Establish _Embassy"), diplomat_embassy_callback, NULL,
	_("_Investigate City"), diplomat_investigate_callback, NULL,
	_("_Sabotage City"), diplomat_sabotage_callback, NULL,
	_("Steal _Technology"), diplomat_steal_callback, NULL,
	_("Incite a _Revolt"), diplomat_incite_callback, NULL,
	_("_Keep moving"), diplomat_keep_moving_callback, NULL,
	GTK_STOCK_CANCEL, diplomat_cancel_callback, NULL,
	NULL);

      if (!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 0, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 1, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 2, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 3, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 4, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_MOVE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 5, FALSE);
    } else {
       shl = popup_message_dialog(GTK_WINDOW(toplevel),
	_("Choose Your Spy's Strategy"), buf,
	_("Establish _Embassy"), diplomat_embassy_callback, NULL,
	_("_Investigate City (free)"), diplomat_investigate_callback, NULL,
	_("_Poison City"), spy_poison_callback, NULL,
	_("Industrial _Sabotage"), spy_request_sabotage_list, NULL,
	_("Steal _Technology"), spy_steal_popup, NULL,
	_("Incite a _Revolt"), diplomat_incite_callback, NULL,
	_("_Keep moving"), diplomat_keep_moving_callback, NULL,
	GTK_STOCK_CANCEL, diplomat_cancel_callback, NULL,
	NULL);

      if (!diplomat_can_do_action(punit, DIPLOMAT_EMBASSY, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 0, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_INVESTIGATE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 1, FALSE);
      if (!diplomat_can_do_action(punit, SPY_POISON, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 2, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_SABOTAGE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 3, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_STEAL, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 4, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_INCITE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 5, FALSE);
      if (!diplomat_can_do_action(punit, DIPLOMAT_MOVE, dest_x, dest_y))
	message_dialog_button_set_sensitive(shl, 6, FALSE);
     }

    diplomat_dialog = shl;

    message_dialog_set_hide(shl, TRUE);
    g_signal_connect(shl, "destroy",
		     G_CALLBACK(diplomat_destroy_callback), NULL);
    g_signal_connect(shl, "delete_event",
		     G_CALLBACK(diplomat_cancel_callback), NULL);
  } else { 
    if ((ptunit = unit_list_get(&map_get_tile(dest_x, dest_y)->units, 0))){
      /* Spy/Diplomat acting against a unit */ 
       
      diplomat_target_id = ptunit->id;
 
      shl = popup_message_dialog(GTK_WINDOW(toplevel),
	_("Subvert Enemy Unit"),
	(!unit_flag(punit, F_SPY))?
	_("Sir, the diplomat is waiting for your command"):
	_("Sir, the spy is waiting for your command"),
	_("_Bribe Enemy Unit"), diplomat_bribe_callback, NULL,
	_("_Sabotage Enemy Unit"), spy_sabotage_unit_callback, NULL,
	GTK_STOCK_CANCEL, diplomat_cancel_callback, NULL,
	NULL);

      if (!diplomat_can_do_action(punit, DIPLOMAT_BRIBE, dest_x, dest_y)) {
	message_dialog_button_set_sensitive(shl, 0, FALSE);
      }
      if (!diplomat_can_do_action(punit, SPY_SABOTAGE_UNIT, dest_x, dest_y)) {
	message_dialog_button_set_sensitive(shl, 1, FALSE);
      }

      diplomat_dialog = shl;

      message_dialog_set_hide(shl, TRUE);
      g_signal_connect(shl, "destroy",
		       G_CALLBACK(diplomat_destroy_callback), NULL);
      g_signal_connect(shl, "delete_event",
		       G_CALLBACK(diplomat_cancel_callback), NULL);
    }
  }
}

/****************************************************************
...
*****************************************************************/
bool diplomat_dialog_is_open(void)
{
  return diplomat_dialog != NULL;
}

/****************************************************************
...
*****************************************************************/
static void caravan_establish_trade_callback(GtkWidget *w, gpointer data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_ESTABLISH_TRADE);
}


/****************************************************************
...
*****************************************************************/
static void caravan_help_build_wonder_callback(GtkWidget *w, gpointer data)
{
  struct packet_unit_request req;
  req.unit_id=caravan_unit_id;
  req.city_id=caravan_city_id;
  req.name[0]='\0';
  send_packet_unit_request(&aconnection, &req, PACKET_UNIT_HELP_BUILD_WONDER);
}


/****************************************************************
...
*****************************************************************/
static void caravan_destroy_callback(GtkWidget *w, gpointer data)
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
  
  my_snprintf(buf, sizeof(buf),
	      _("Your caravan from %s reaches the city of %s.\nWhat now?"),
	      phomecity->name, pdestcity->name);
  
  caravan_city_id=pdestcity->id; /* callbacks need these */
  caravan_unit_id=punit->id;
  
  caravan_dialog = popup_message_dialog(GTK_WINDOW(toplevel),
    _("Your Caravan Has Arrived"), 
    buf,
    _("Establish _Traderoute"),caravan_establish_trade_callback, NULL,
    _("Help build _Wonder"),caravan_help_build_wonder_callback, NULL,
    _("_Keep moving"), NULL, NULL,
    NULL);

  g_signal_connect(caravan_dialog, "destroy",
		   G_CALLBACK(caravan_destroy_callback), NULL);
  
  if (!can_establish_trade_route(phomecity, pdestcity)) {
    message_dialog_button_set_sensitive(caravan_dialog, 0, FALSE);
  }
  
  if (!unit_can_help_build_wonder(punit, pdestcity)) {
    message_dialog_button_set_sensitive(caravan_dialog, 1, FALSE);
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
static void government_callback(GtkWidget *w, gpointer data)
{
  struct packet_player_request packet;

  packet.government=GPOINTER_TO_INT(data);
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_GOVERNMENT);

  is_showing_government_dialog=0;
}


/****************************************************************
...
*****************************************************************/
void popup_government_dialog(void)
{
  int i;
  GtkWidget *dshell, *dlabel, *vbox;

  if(!is_showing_government_dialog) {
    is_showing_government_dialog=1;
  
    dshell=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(dshell), GTK_WINDOW(toplevel));
    g_object_set(GTK_WINDOW(dshell),
      "title", _("Choose Your New Government"),
      "window-position", GTK_WIN_POS_CENTER_ON_PARENT,
      "modal", TRUE,
      NULL);

    gtk_signal_connect(
      GTK_OBJECT(dshell),
      "delete_event",
      GTK_SIGNAL_FUNC(gtk_true),
      GINT_TO_POINTER(toplevel)
    );

    dlabel = gtk_frame_new(_("Select government type:"));
    gtk_container_add(GTK_CONTAINER(dshell), dlabel);

    vbox = gtk_vbutton_box_new();
    gtk_container_add(GTK_CONTAINER(dlabel), vbox);
    gtk_container_border_width(GTK_CONTAINER(vbox), 5);

    for (i = 0; i < game.government_count; i++) {
      struct government *g = &governments[i];

      if (i != game.government_when_anarchy) {
        GtkWidget *label, *image, *hbox, *align, *button;
	struct Sprite *gsprite;

      	/* create button. */
        button = gtk_button_new();

        label = gtk_label_new_with_mnemonic(g->name);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

      	gsprite = get_government(g->index)->sprite;

        image = gtk_image_new_from_pixmap(gsprite->pixmap, gsprite->mask);
        hbox = gtk_hbox_new(FALSE, 2);

      	align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

        gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, FALSE, 5);

        gtk_container_add(GTK_CONTAINER(align), label);
        gtk_container_add(GTK_CONTAINER(button), hbox);

      	/* tidy up. */
        gtk_container_add(GTK_CONTAINER(vbox), button);
        g_signal_connect(
          button,
          "clicked",
          G_CALLBACK(government_callback),
          GINT_TO_POINTER(g->index)
        );
        g_signal_connect_swapped(
          button,
          "clicked",
          G_CALLBACK(gtk_widget_destroy),
          dshell
        );

        if (!can_change_to_government(game.player_ptr, i))
    	  gtk_widget_set_sensitive(button, FALSE);
      }
    }
 
    gtk_widget_show_all(dlabel);
    gtk_widget_show(dshell);  
  }
}



/****************************************************************
...
*****************************************************************/
void popup_revolution_dialog(void)
{
  GtkWidget *shell;

  shell = gtk_message_dialog_new(GTK_WINDOW(toplevel),
    GTK_DIALOG_MODAL,
    GTK_MESSAGE_WARNING,
    GTK_BUTTONS_YES_NO,
    _("You say you wanna revolution?"));
  gtk_window_set_title(GTK_WINDOW(shell), _("Revolution!"));

  if (gtk_dialog_run(GTK_DIALOG(shell)) == GTK_RESPONSE_YES) {
    struct packet_player_request packet;

    send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_REVOLUTION);
  }
  gtk_widget_destroy(shell);
}


/****************************************************************
...
*****************************************************************/
static void pillage_callback(GtkWidget *w, gpointer data)
{
  if (data) {
    struct unit *punit = find_unit_by_id(unit_to_use_to_pillage);
    if (punit) {
      request_new_unit_activity_targeted(punit,
					 ACTIVITY_PILLAGE,
					 GPOINTER_TO_INT(data));
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void pillage_destroy_callback(GtkWidget *w, gpointer data)
{
  is_showing_pillage_dialog = FALSE;
}

/****************************************************************
...
*****************************************************************/
void popup_pillage_dialog(struct unit *punit,
			  enum tile_special_type may_pillage)
{
  GtkWidget *shl;

  if (!is_showing_pillage_dialog) {
    is_showing_pillage_dialog = TRUE;
    unit_to_use_to_pillage = punit->id;

    shl = message_dialog_start(GTK_WINDOW(toplevel),
			       _("What To Pillage"),
			       _("Select what to pillage:"));

    while (may_pillage != S_NO_SPECIAL) {
      enum tile_special_type what = get_preferred_pillage(may_pillage);

      message_dialog_add(shl, map_get_infrastructure_text(what),
			 G_CALLBACK(pillage_callback), GINT_TO_POINTER(what));

      may_pillage &= (~(what | map_get_infrastructure_prerequisite(what)));
    }

    message_dialog_add(shl, GTK_STOCK_CANCEL, 0, 0);

    message_dialog_end(shl);

    g_signal_connect(shl, "destroy", G_CALLBACK(pillage_destroy_callback),
		     NULL);   
  }
}

/****************************************************************
handle buttons in unit connect dialog
*****************************************************************/
static void unit_connect_callback(GtkWidget *w, gpointer data)
{
  struct unit *punit;
  int activity = GPOINTER_TO_INT(data);

  punit = find_unit_by_id(unit_to_use_to_connect);

  if (punit) {
    if (activity != ACTIVITY_IDLE) {
      struct packet_unit_connect req;
      req.activity_type = activity;
      req.unit_id = punit->id;
      req.dest_x = connect_unit_x;
      req.dest_y = connect_unit_y;
      send_packet_unit_connect(&aconnection, &req);
    }
    else {
      update_unit_info_label(punit);
    }
  }
}

/****************************************************************
...
*****************************************************************/
static void unit_connect_destroy_callback(GtkWidget *w, gpointer data)
{
  is_showing_unit_connect_dialog = FALSE;
}

/****************************************************************
popup dialog which prompts for activity type (unit connect)
*****************************************************************/
void popup_unit_connect_dialog(struct unit *punit, int dest_x, int dest_y)
{
  GtkWidget *shl;
  int activity;

  if (is_showing_unit_connect_dialog) 
    return;

  is_showing_unit_connect_dialog = TRUE;
  unit_to_use_to_connect = punit->id;
  connect_unit_x = dest_x;
  connect_unit_y = dest_y;

  shl = message_dialog_start(GTK_WINDOW(toplevel),
			     _("Connect"),
			     _("Choose unit activity:"));

  for (activity = ACTIVITY_IDLE + 1; activity < ACTIVITY_LAST; activity++) {
    if (! can_unit_do_connect (punit, activity)) continue;

    message_dialog_add(shl, get_activity_text(activity),
		       G_CALLBACK(unit_connect_callback),
		       GINT_TO_POINTER(activity));
  }

  message_dialog_add(shl, GTK_STOCK_CANCEL, 
		     G_CALLBACK(unit_connect_callback),
		     GINT_TO_POINTER(ACTIVITY_IDLE));

  message_dialog_end(shl);

  g_signal_connect(shl, "destroy",
		   G_CALLBACK(unit_connect_destroy_callback), NULL);
}

/****************************************************************
...
*****************************************************************/
void message_dialog_button_set_sensitive(GtkWidget *shl, int button,
					 gboolean state)
{
  char button_name[512];
  GtkWidget *b;

  my_snprintf(button_name, sizeof(button_name), "button%d", button);

  b = g_object_get_data(G_OBJECT(shl), button_name);
  gtk_widget_set_sensitive(b, state);
}

/****************************************************************
...
*****************************************************************/
GtkWidget *message_dialog_start(GtkWindow *parent, const gchar *name,
				const gchar *text)
{
  GtkWidget *dshell, *dlabel, *vbox, *bbox;

  dshell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position (GTK_WINDOW(dshell), GTK_WIN_POS_MOUSE);

  gtk_window_set_title(GTK_WINDOW(dshell), name);

  gtk_window_set_transient_for(GTK_WINDOW(dshell), parent);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dshell), TRUE);

  vbox = gtk_vbox_new(FALSE, 5);
  gtk_container_add(GTK_CONTAINER(dshell),vbox);

  gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

  dlabel = gtk_label_new(text);
  gtk_container_add(GTK_CONTAINER(vbox), dlabel);

  bbox = gtk_vbutton_box_new();
  gtk_box_set_spacing(GTK_BOX(bbox), 2);
  gtk_container_add(GTK_CONTAINER(vbox), bbox);
  
  g_object_set_data(G_OBJECT(dshell), "bbox", bbox);
  g_object_set_data(G_OBJECT(dshell), "nbuttons", GINT_TO_POINTER(0));
  g_object_set_data(G_OBJECT(dshell), "hide", GINT_TO_POINTER(FALSE));
  
  gtk_widget_show(vbox);
  gtk_widget_show(dlabel);
  
  return dshell;
}

/****************************************************************
...
*****************************************************************/
static void message_dialog_clicked(GtkWidget *w, gpointer data)
{
  if (g_object_get_data(G_OBJECT(data), "hide")) {
    gtk_widget_hide(GTK_WIDGET(data));
  } else {
    gtk_widget_destroy(GTK_WIDGET(data));
  }
}

/****************************************************************
...
*****************************************************************/
void message_dialog_add(GtkWidget *dshell, const gchar *label,
			GCallback handler, gpointer data)
{
  GtkWidget *button, *bbox;
  char name[512];
  int nbuttons;

  bbox = g_object_get_data(G_OBJECT(dshell), "bbox");
  nbuttons = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dshell), "nbuttons"));
  g_object_set_data(G_OBJECT(dshell), "nbuttons", GINT_TO_POINTER(nbuttons+1));

  my_snprintf(name, sizeof(name), "button%d", nbuttons);

  button = gtk_button_new_from_stock(label);
  gtk_container_add(GTK_CONTAINER(bbox), button);
  g_object_set_data(G_OBJECT(dshell), name, button);

  if (handler) {
    g_signal_connect(button, "clicked", handler, data);
  }

  g_signal_connect_after(button, "clicked",
			 G_CALLBACK(message_dialog_clicked), dshell);
}

/****************************************************************
...
*****************************************************************/
void message_dialog_end(GtkWidget *dshell)
{
  GtkWidget *bbox;
  
  bbox = g_object_get_data(G_OBJECT(dshell), "bbox");
  
  gtk_widget_show_all(bbox);
  gtk_widget_show(dshell);  
}

/****************************************************************
...
*****************************************************************/
void message_dialog_set_hide(GtkWidget *dshell, gboolean setting)
{
  g_object_set_data(G_OBJECT(dshell), "hide", GINT_TO_POINTER(setting));
}

/****************************************************************
...
*****************************************************************/
GtkWidget *popup_message_dialog(GtkWindow *parent, const gchar *dialogname,
				const gchar *text, ...)
{
  GtkWidget *dshell;
  va_list args;
  gchar *name;
  int i;

  dshell = message_dialog_start(parent, dialogname, text);
  
  i = 0;
  va_start(args, text);

  while ((name = va_arg(args, gchar *))) {
    GCallback handler;
    gpointer data;

    handler = va_arg(args, GCallback);
    data = va_arg(args, gpointer);

    message_dialog_add(dshell, name, handler, data);
  }

  va_end(args);

  message_dialog_end(dshell);

  return dshell;
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

  gtk_widget_destroy(unit_select_dialog_shell);
}

/**************************************************************************
...
**************************************************************************/
static int number_of_rows(int n)
{
  return (n-1)/NUM_SELECT_UNIT_COLS+1;
}

/**************************************************************************
...
**************************************************************************/
static void refresh_unit_select_dialog(void)
{
  if (unit_select_dialog_shell) {
    struct tile *ptile;
    int i, n, r;

    ptile = unit_select_ptile;

    n = unit_list_size(&ptile->units);
    r = number_of_rows(n);

    for (i=n; i<unit_select_no; i++) {
      gtk_widget_destroy(unit_select_nodes[i].cmd);
    }

    gtk_table_resize(GTK_TABLE(unit_select_table), r, NUM_SELECT_UNIT_COLS);

    unit_select_nodes =
      fc_realloc(unit_select_nodes, n * sizeof(*unit_select_nodes));

    for (i=unit_select_no; i<n; i++) {
      GtkWidget *cmd, *pix;

      cmd = gtk_button_new();
      unit_select_nodes[i].cmd = cmd;

      pix = gtk_pixcomm_new(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
      unit_select_nodes[i].pix = pix;

      gtk_container_add(GTK_CONTAINER(cmd), pix);
      gtk_table_attach_defaults(GTK_TABLE(unit_select_table),
                                cmd,
				(i/r), (i/r)+1,
				(i%r), (i%r)+1);
    }

    gtk_tooltips_disable(unit_select_tips);

    for (i=0; i<n; i++) {
      GtkWidget *cmd, *pix;
      struct unit *punit = unit_list_get(&ptile->units, i);

      cmd = unit_select_nodes[i].cmd;
      pix = unit_select_nodes[i].pix;

      put_unit_gpixmap(punit, GTK_PIXCOMM(pix));

      g_signal_handlers_disconnect_matched(cmd,
        G_SIGNAL_MATCH_FUNC,
        0, 0, NULL, unit_select_callback, NULL);

      g_signal_connect(cmd, "clicked",
        G_CALLBACK(unit_select_callback), GINT_TO_POINTER(punit->id));

      gtk_tooltips_set_tip(unit_select_tips,
        cmd, unit_description(punit), "");

      gtk_widget_show(pix);
      gtk_widget_show(cmd);
    }

    gtk_tooltips_enable(unit_select_tips);

    unit_select_no = n;
  }
}

/****************************************************************
...
*****************************************************************/
static void unit_select_destroy_callback(GtkObject *object, gpointer data)
{
  if (unit_select_tips) {
    g_object_unref(unit_select_tips);
  }
  unit_select_tips = NULL;

  if (unit_select_nodes) {
    free(unit_select_nodes);
  }
  unit_select_nodes = NULL;

  unit_select_no = 0;

  unit_select_dialog_shell = NULL;
}

/****************************************************************
...
*****************************************************************/
static void unit_select_cmd_callback(GtkWidget *w, gint rid, gpointer data)
{
  struct tile *ptile = unit_select_ptile;

  switch (rid) {
  case SELECT_UNIT_READY:
    {
      struct unit *pmyunit = NULL;

      unit_list_iterate(ptile->units, punit) {
        if (game.player_idx == punit->owner) {
          /* Activate this unit. */
          pmyunit = punit;
          request_new_unit_activity(punit, ACTIVITY_IDLE);
        }
      } unit_list_iterate_end;

      if (pmyunit) {
        /* Put the focus on one of the activated units. */
        set_unit_focus(pmyunit);
      }
    }
    break;

  case SELECT_UNIT_SENTRY:
    {
      unit_list_iterate(ptile->units, punit) {
        if (game.player_idx == punit->owner) {
          if ((punit->activity == ACTIVITY_IDLE) &&
              !punit->ai.control &&
              can_unit_do_activity(punit, ACTIVITY_SENTRY)) {
            request_new_unit_activity(punit, ACTIVITY_SENTRY);
          }
        }
      } unit_list_iterate_end;
    }
    break;

  default:
    break;
  }
  
  gtk_widget_destroy(unit_select_dialog_shell);
}

/****************************************************************
...
*****************************************************************/
void popup_unit_select_dialog(struct tile *ptile)
{
  if (!unit_select_dialog_shell) {
    GtkWidget *shell, *align, *table;
    GtkWidget *ready_cmd, *sentry_cmd, *close_cmd;

    shell = gtk_dialog_new_with_buttons(_("Unit selection"),
      GTK_WINDOW(toplevel),
      0,
      NULL);
    unit_select_dialog_shell = shell;

    unit_select_tips = gtk_tooltips_new();
    g_object_ref(unit_select_tips);
    gtk_object_sink(GTK_OBJECT(unit_select_tips));

    g_signal_connect(shell, "destroy",
      G_CALLBACK(unit_select_destroy_callback), NULL);
    gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
    g_signal_connect(shell, "response",
      G_CALLBACK(unit_select_cmd_callback), NULL);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), align);

    table = gtk_table_new(NUM_SELECT_UNIT_COLS, 0, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);
    gtk_container_add(GTK_CONTAINER(align), table);
    unit_select_table = table;

    gtk_widget_show(align);
    gtk_widget_show(table);

    ready_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      _("_Ready all"), SELECT_UNIT_READY);

    gtk_button_box_set_child_secondary(
      GTK_BUTTON_BOX(GTK_DIALOG(shell)->action_area),
      ready_cmd, TRUE);

    sentry_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      _("_Sentry idle"), SELECT_UNIT_SENTRY);

    gtk_button_box_set_child_secondary(
      GTK_BUTTON_BOX(GTK_DIALOG(shell)->action_area),
      sentry_cmd, TRUE);

    close_cmd =
    gtk_dialog_add_button(GTK_DIALOG(shell),
      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

    gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_CLOSE);

    gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
    gtk_widget_show_all(GTK_DIALOG(shell)->action_area);
  }

  unit_select_ptile = ptile;
  refresh_unit_select_dialog();

  gtk_window_present(GTK_WINDOW(unit_select_dialog_shell));
}


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_races_dialog(void)
{
  create_races_dialog ();
  gtk_window_present(GTK_WINDOW(races_dialog_shell));
}

/****************************************************************
...
*****************************************************************/
void popdown_races_dialog(void)
{
  if (races_dialog_shell) {
    gtk_widget_destroy(races_dialog_shell);
    races_dialog_shell = NULL;
    g_list_free(sorted_races_list);
    sorted_races_list = NULL;
  } /* else there is no dialog shell to destroy */
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
             gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(races_name)->entry)));
 
  g_signal_handlers_block_by_func(GTK_COMBO(races_name)->list,
	races_name_callback, NULL);

  g_list_free(leader_strings);
  leader_strings = NULL;

  /* fill leader names combo box */
  leaders = get_nation_leaders(selected_nation, &leader_num);
  for(j = 0; j < leader_num; j++) {
    leader_strings = g_list_append(leader_strings, leaders[j].name);
  }
  gtk_combo_set_value_in_list(GTK_COMBO(races_name), FALSE, FALSE);
  gtk_combo_set_popdown_strings(GTK_COMBO(races_name), leader_strings);

  g_signal_handlers_unblock_by_func(GTK_COMBO(races_name)->list,
	races_name_callback, NULL);

  if (!is_name_unique) {
    /* initialize leader names */
    selected_leader = myrand(leader_num);
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(races_name)->entry),
                      leaders[selected_leader].name);

    /* initialize leader sex */
    selected_sex = leaders[selected_leader].is_male;
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(
                              races_sex_toggles[selected_sex ? 0 : 1]), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(races_name)->entry), unique_name);
  }
}

/****************************************************************
Selectes a random race and the appropriate city style.
Updates the gui elements and the selected_* variables.
*****************************************************************/
static void select_random_race(void)
{
  /* try to find a free nation */
  while(TRUE) {
    selected_nation = myrand(game.playable_nation_count);
    if(GTK_WIDGET_SENSITIVE(races_toggles[g_list_index(sorted_races_list,
				       GINT_TO_POINTER(selected_nation))]))
      break;
  }

  /* initialize nation toggle array */
  gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON(
		 races_toggles[g_list_index(sorted_races_list,
			    GINT_TO_POINTER(selected_nation))] ), TRUE );

  /* initialize city style */
  selected_city_style =
    city_style_ridx[get_nation_city_style(selected_nation)];
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(
			city_style_toggles[selected_city_style] ), TRUE );
}

/****************************************************************
...
*****************************************************************/
void create_races_dialog(void)
{
  int       per_row;
  int       i;
  GSList    *group = NULL;
  GSList    *sgroup = NULL;
  GSList    *cgroup = NULL;
  GtkWidget *f, *fs, *fa, *label, *pop_downs_box;
  GtkWidget *disc_command, *quit_command;
  GList     *race_names;
 
  /* Makes the flag box a nicely proportioned rectangle,
   * whatever the total size may be */
  if (game.playable_nation_count > 8) {
    per_row = sqrt(game.playable_nation_count) * 18 / 15;
  } else {
    /* Multiple rows would look silly here */ 
    per_row = game.playable_nation_count;
  }

  races_dialog_shell = gtk_dialog_new_with_buttons(
	_(" What Nation Will You Be?"),
	GTK_WINDOW(toplevel),
	GTK_DIALOG_MODAL,
	NULL);

  g_signal_connect(races_dialog_shell, "response",
		   G_CALLBACK(races_command_callback), NULL);

  f = gtk_frame_new(_("Select nation and name"));
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	f, FALSE, FALSE, 0 );

  /* ------- nation name toggles ------- */

  races_toggles_form =
    gtk_table_new( per_row, ((game.playable_nation_count-1)/per_row)+1, FALSE );
  gtk_container_add( GTK_CONTAINER( f ), races_toggles_form );

  free(races_toggles);
  races_toggles = fc_calloc( game.playable_nation_count, sizeof(GtkWidget*) );

  for(i=0; i<game.playable_nation_count; i++) {
    sorted_races_list=g_list_append(sorted_races_list,GINT_TO_POINTER(i));
  }
  sorted_races_list=g_list_sort(sorted_races_list,cmp_func);
  for(i=0; i<g_list_length(sorted_races_list); i++) {
    gint nat_id=GPOINTER_TO_INT(g_list_nth_data(sorted_races_list,i));
    GtkWidget *flag;
    SPRITE *s;

    races_toggles[i] = gtk_radio_button_new(group);
    s = crop_blankspace(get_nation_by_idx(nat_id)->flag_sprite);
    flag = gtk_image_new_from_pixmap(s->pixmap, s->mask);
    gtk_misc_set_alignment(GTK_MISC(flag), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(flag), 6, 4);

    gtk_container_add(GTK_CONTAINER(races_toggles[i]), flag);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(races_toggles[i]), FALSE);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(races_toggles[i]));
    gtk_table_attach_defaults(GTK_TABLE(races_toggles_form),races_toggles[i],
			      i%per_row, i%per_row+1, i/per_row, i/per_row+1);
  }

  /* ------- chose nation/leader by name ------- */
 
  pop_downs_box = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(races_dialog_shell)->vbox),
		     pop_downs_box, FALSE, FALSE, 5);
 
  label = gtk_label_new(_("Nation:"));
  races_by_name = gtk_combo_new();
  gtk_editable_set_editable(GTK_EDITABLE(GTK_COMBO(races_by_name)->entry), 
			    FALSE);
  race_names = NULL;

  for(i = 0; i < game.playable_nation_count; i++) {
    race_names = g_list_append(race_names, get_nation_by_idx(
	  GPOINTER_TO_INT(g_list_nth_data(sorted_races_list, i)))->name);
  }

  gtk_combo_set_popdown_strings(GTK_COMBO(races_by_name), race_names);
  gtk_combo_set_value_in_list(GTK_COMBO(races_by_name), TRUE, FALSE);
  gtk_box_pack_start(GTK_BOX(pop_downs_box), label, FALSE, FALSE, 4);
  gtk_box_pack_start(GTK_BOX(pop_downs_box), races_by_name, FALSE, FALSE, 0);

  races_name = gtk_combo_new();

  label = gtk_label_new(_("Leader:"));
  gtk_box_pack_end(GTK_BOX(pop_downs_box), races_name, FALSE, FALSE, 4);
  gtk_box_pack_end(GTK_BOX(pop_downs_box), label, FALSE, FALSE, 0);

  GTK_WIDGET_SET_FLAGS( races_name, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( races_name );

  /* ------- leader sex toggles ------- */

  fs = gtk_frame_new(_("Select your sex"));

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	fs, FALSE, FALSE, 0 );
  races_sex_toggles_form = gtk_table_new( 1, 2, TRUE );
  gtk_container_add( GTK_CONTAINER( fs ), races_sex_toggles_form ); 

  races_sex_toggles[0]= gtk_radio_button_new_with_label( sgroup, _("Male") );
  gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_sex_toggles[0] ),
			       FALSE );
  sgroup = gtk_radio_button_group( GTK_RADIO_BUTTON( races_sex_toggles[0] ) );
  gtk_table_attach_defaults( GTK_TABLE(races_sex_toggles_form),
			     races_sex_toggles[0],0,1,0,1); 
  races_sex_toggles[1]= gtk_radio_button_new_with_label( sgroup, _("Female") );
  gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_sex_toggles[1] ),
			       FALSE );
  sgroup = gtk_radio_button_group( GTK_RADIO_BUTTON( races_sex_toggles[1] ) );
  gtk_table_attach_defaults( GTK_TABLE(races_sex_toggles_form),
			     races_sex_toggles[1],1,2,0,1); 

  /* ------- city style toggles ------- */

  /* find out styles that can be used at the game beginning */
   
  for(i=0,b_s_num=0; i<game.styles_count && i<64; i++) {
    if(city_styles[i].techreq == A_NONE) {
      city_style_idx[b_s_num] = i;
      city_style_ridx[i] = b_s_num;
      b_s_num++;
    }
  }
  free(city_style_toggles);
  city_style_toggles = fc_calloc( b_s_num, sizeof(struct GtkWidget*) );

  fa = gtk_frame_new( _("Select your city style") );
  gtk_box_pack_start(GTK_BOX( GTK_DIALOG(races_dialog_shell)->vbox),
                     fa, FALSE, FALSE, 0);

  city_style_toggles_form = gtk_table_new(1, b_s_num, TRUE);
  gtk_container_add(GTK_CONTAINER(fa), city_style_toggles_form);

  for(i = 0; i < b_s_num; i++) {
    GtkWidget *box, *sub_box, *img;
    SPRITE *s;

    city_style_toggles[i] = gtk_radio_button_new(cgroup);
    box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(city_style_toggles[i]), box);
    gtk_box_pack_start(GTK_BOX(box),
                       gtk_label_new(city_styles[city_style_idx[i]].name),
                       FALSE, FALSE, 4);
    sub_box = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(box), sub_box);
    s = crop_blankspace(sprites.city.tile[i][0]);
    img = gtk_image_new_from_pixmap(s->pixmap, s->mask);
    gtk_box_pack_start(GTK_BOX(sub_box), img, FALSE, FALSE, 4);

    if ((s->width < 80) && (city_styles[i].tiles_num > 1)){
      s = crop_blankspace(sprites.city.tile[i][1]);
      img = gtk_image_new_from_pixmap(s->pixmap, s->mask);
      gtk_box_pack_start(GTK_BOX(sub_box), img, FALSE, FALSE, 4);
    }
    if ((s->width < 40) && (city_styles[i].tiles_num > 2)){
      s = crop_blankspace(sprites.city.tile[i][2]);
      img = gtk_image_new_from_pixmap(s->pixmap, s->mask);
      gtk_box_pack_start(GTK_BOX(sub_box), img, FALSE, FALSE, 4);
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(city_style_toggles[i]),
                                 FALSE);
    cgroup=gtk_radio_button_get_group(GTK_RADIO_BUTTON(city_style_toggles[i]));
    gtk_table_attach_defaults(GTK_TABLE(city_style_toggles_form),
                              city_style_toggles[i],
                              i, i+1, 0, 1);
  }

  /* ------- Disc/Quit buttons ------- */

  disc_command = gtk_stockbutton_new(GTK_STOCK_CANCEL, _("_Disconnect"));
  gtk_dialog_add_action_widget(GTK_DIALOG(races_dialog_shell),
			       disc_command, GTK_RESPONSE_CANCEL);

  quit_command = gtk_button_new_from_stock(GTK_STOCK_QUIT);
  gtk_dialog_add_action_widget(GTK_DIALOG(races_dialog_shell),
			       quit_command, GTK_RESPONSE_CLOSE);

  gtk_dialog_add_button(GTK_DIALOG(races_dialog_shell),
      	      	      	GTK_STOCK_OK, GTK_RESPONSE_OK);

  /* ------- connect callback functions ------- */

   for(i=0; i<g_list_length(sorted_races_list); i++)
	gtk_signal_connect( GTK_OBJECT( races_toggles[i] ), "toggled",
                           GTK_SIGNAL_FUNC( races_toggles_callback ),
                           g_list_nth_data(sorted_races_list, i));

  for(i=0; i<2; i++)
        gtk_signal_connect( GTK_OBJECT( races_sex_toggles[i] ), "toggled",
	    GTK_SIGNAL_FUNC( races_sex_toggles_callback ), NULL );

  for(i=0; i<b_s_num; i++)
        gtk_signal_connect( GTK_OBJECT( city_style_toggles[i] ), "toggled",
	    GTK_SIGNAL_FUNC( city_style_toggles_callback ), NULL );

  gtk_signal_connect( GTK_OBJECT( GTK_COMBO(races_name)->list ), 
		      "selection_changed",
		      GTK_SIGNAL_FUNC( races_name_callback ), NULL );

  gtk_signal_connect(GTK_OBJECT(GTK_COMBO(races_by_name)->list), 
 		      "selection_changed",
 		      GTK_SIGNAL_FUNC(races_by_name_callback), NULL);

  /* ------- set initial selections ------- */

  select_random_race();
  select_random_leader();

  gtk_dialog_set_default_response(GTK_DIALOG(races_dialog_shell),
	GTK_RESPONSE_OK);

  gtk_widget_show_all( GTK_DIALOG(races_dialog_shell)->vbox );
}

/**************************************************************************
...
**************************************************************************/ 
static void races_by_name_callback(GtkWidget *w, gpointer data)
{
  int i;

  for(i = 0; i < g_list_length(sorted_races_list); i++) {
    if (strcmp(gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(races_by_name)->entry)),
	       get_nation_by_idx(GPOINTER_TO_INT(
	       g_list_nth_data(sorted_races_list, i)))->name) == 0) {
      if (!GTK_IS_OBJECT(races_toggles[i])) break;
      if (GTK_WIDGET_SENSITIVE(races_toggles[i])) {
       gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(races_toggles[i]), TRUE);
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
void races_toggles_set_sensitive(struct packet_nations_used *packet)
{
  int i;

  for (i = 0; i < game.playable_nation_count; i++) {
    gtk_widget_set_sensitive(races_toggles[g_list_index
					   (sorted_races_list,
					    GINT_TO_POINTER(i))], TRUE);
  }

  freelog(LOG_DEBUG, "%d nations used:", packet->num_nations_used);
  for (i = 0; i < packet->num_nations_used; i++) {
    int nation = packet->nations_used[i];

    freelog(LOG_DEBUG, "  [%d]: %d = %s", i, nation,
	    get_nation_name(nation));

    gtk_widget_set_sensitive(races_toggles[g_list_index
					   (sorted_races_list,
					    GINT_TO_POINTER(nation))],
			     FALSE);

    if (nation == selected_nation) {
      select_random_race();
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void races_name_callback(GtkWidget * w, gpointer data)
{
  Nation_Type_id nation = races_buttons_get_current();
  const char *leader =
                  gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(races_name)->entry));

  if (check_nation_leader_name(nation, leader)) {
    is_name_unique = FALSE;
    selected_sex = get_nation_leader_sex(nation, leader);
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON
				(races_sex_toggles[selected_sex ? 0 : 1]),
				TRUE);
  } else {
    is_name_unique = TRUE;
  }

}

/**************************************************************************
...
**************************************************************************/
static void races_toggles_callback( GtkWidget *w, gpointer race_id_p )
{
  /* don't do anything if signal is untoggling the button */
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
    selected_nation = GPOINTER_TO_INT(race_id_p);
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(races_by_name)->entry),
 		       get_nation_by_idx(selected_nation)->name);
    select_random_leader();

    selected_city_style = 
                      city_style_ridx[get_nation_city_style(selected_nation)];
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(
                              city_style_toggles[selected_city_style]), TRUE);
  }
}

/**************************************************************************
...
**************************************************************************/
static void races_sex_toggles_callback( GtkWidget *w, gpointer data )
{
  if(w==races_sex_toggles[0]) 
      selected_sex = 1;
  else 
      selected_sex = 0;
}

/**************************************************************************
...
**************************************************************************/
static void city_style_toggles_callback( GtkWidget *w, gpointer data )
{
  int i;

  for(i=0; i<b_s_num; i++)
    if(w==city_style_toggles[i]) {
      selected_city_style = i;
      return;
    }
}

/**************************************************************************
...
**************************************************************************/
static int races_buttons_get_current(void)
{
  return selected_nation;
}

static int sex_buttons_get_current(void)
{
  return selected_sex;
}

static int city_style_get_current(void)
{
  return selected_city_style;
}

/**************************************************************************
...
**************************************************************************/
static void races_command_callback(GtkWidget *w, gint response_id)
{
  int selected, selected_sex, selected_style;
  const char *s;

  struct packet_alloc_nation packet;

  if(response_id == GTK_RESPONSE_CLOSE) {
    exit(EXIT_SUCCESS);
  } else if(response_id != GTK_RESPONSE_OK) {
    popdown_races_dialog();
    disconnect_from_server();
    return;
  }

  if((selected=races_buttons_get_current())==-1) {
    append_output_window(_("You must select a nation."));
    return;
  }

  if((selected_sex=sex_buttons_get_current())==-1) {
    append_output_window(_("You must select your sex."));
    return;
  }

  if((selected_style=city_style_get_current())==-1) {
    append_output_window(_("You must select your city style."));
    return;
  }

  s = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(races_name)->entry));

  /* perform a minimum of sanity test on the name */
  packet.nation_no=selected;
  packet.is_male = selected_sex;
  packet.city_style = city_style_idx[selected_style];
  sz_strlcpy(packet.name, (char*)s);
  
  if (!is_sane_name(packet.name)) {
    append_output_window(_("You must type a legal name."));
    return;
  }

  send_packet_alloc_nation(&aconnection, &packet);

  /* reset this variable */
  is_name_unique = FALSE;
}


/**************************************************************************
  Adjust tax rates from main window
**************************************************************************/
gboolean taxrates_callback(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  int tax_end,lux_end,sci_end;
  size_t i;
  int delta=10;
  struct packet_player_request packet;
  
  if (!can_client_issue_orders()) {
    return TRUE;
  }
  
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
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
static void nuke_children(gpointer data, gpointer user_data)
{
  if (data != user_data) {
    if (GTK_IS_WINDOW(data) && GTK_WINDOW(data)->type == GTK_WINDOW_TOPLEVEL) {
      gtk_widget_destroy(GTK_WIDGET(data));
    }
  }
}

/********************************************************************** 
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
***********************************************************************/
void popdown_all_game_dialogs(void)
{
  GList *res;

  res = gtk_window_list_toplevels();
  g_list_foreach(res, nuke_children, toplevel);
  g_list_free(res);
}
