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

#include <gtk/gtk.h>

#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "diptreaty.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "diplodlg.h"

#define MAX_NUM_CLAUSES 64

struct Diplomacy_dialog {
  struct Treaty treaty;
  
  GtkWidget *shell;

  GtkWidget *menu0;
  GtkWidget *menu1;

  GtkWidget *image0;
  GtkWidget *image1;

  GtkListStore *store;
};

#define SPECLIST_TAG dialog
#define SPECLIST_TYPE struct Diplomacy_dialog
#include "speclist.h"

#define dialog_list_iterate(dialoglist, pdialog) \
    TYPED_LIST_ITERATE(struct Diplomacy_dialog, dialoglist, pdialog)
#define dialog_list_iterate_end  LIST_ITERATE_END

static struct dialog_list dialog_list;
static bool dialog_list_list_has_been_initialised = FALSE;

static struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1);

static struct Diplomacy_dialog *find_diplomacy_dialog(int other_player_id);
static void popup_diplomacy_dialog(int other_player_id);
static void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_tech_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_city_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_ceasefire_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_peace_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_alliance_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_vision_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_embassy_callback(GtkWidget *w, gpointer data);
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void diplo_dialog_returnkey(GtkWidget *w, gpointer data);

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_accept_treaty(int counterpart, bool I_accepted,
				    bool other_accepted)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  pdialog->treaty.accept0 = I_accepted;
  pdialog->treaty.accept1 = other_accepted;

  update_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_init_meeting(int counterpart, int initiated_from)
{
  popup_diplomacy_dialog(counterpart);
}


/****************************************************************
...
*****************************************************************/
void handle_diplomacy_cancel_meeting(int counterpart, int initiated_from)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  close_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_create_clause(int counterpart, int giver,
				    enum clause_type type, int value)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  add_clause(&pdialog->treaty, get_player(giver), type, value);
  update_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_remove_clause(int counterpart, int giver,
				    enum clause_type type, int value)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(counterpart);

  if (!pdialog) {
    return;
  }

  remove_clause(&pdialog->treaty, get_player(giver), type, value);
  update_diplomacy_dialog(pdialog);
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
static void popup_diplomacy_dialog(int other_player_id)
{
  struct Diplomacy_dialog *pdialog = find_diplomacy_dialog(other_player_id);

  if (game.player_ptr->ai.control) {
    return;			/* Don't show if we are AI controlled. */
  }

  if (!pdialog) {
    pdialog =
	create_diplomacy_dialog(game.player_ptr,
				get_player(other_player_id));
  }

  gtk_window_present(GTK_WINDOW(pdialog->shell));
}

/****************************************************************
...
*****************************************************************/
static void popup_add_menu(GtkMenuShell *parent, gpointer data)
{
  struct Diplomacy_dialog *pdialog;
  
  gpointer plr;
  struct player *plr0, *plr1;

  GtkWidget *item, *menu;


  /* init. */
  gtk_container_foreach(GTK_CONTAINER(parent),
                        (GtkCallback) gtk_widget_destroy, NULL);

  pdialog = (struct Diplomacy_dialog *)data;
  plr	  = g_object_get_data(G_OBJECT(parent), "plr");

  plr0	  = pdialog->treaty.plr0;
  plr1	  = pdialog->treaty.plr1;

  if (plr == plr1) {
    plr1  = plr0;
    plr0  = plr;
  }


  /* Maps. */
  menu = gtk_menu_new();
  item = gtk_menu_item_new_with_mnemonic(_("World-map"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
  g_object_set_data(G_OBJECT(item), "plr", plr);
  g_signal_connect(item, "activate",
		   G_CALLBACK(diplomacy_dialog_map_callback), pdialog);

  item = gtk_menu_item_new_with_mnemonic(_("Sea-map"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_object_set_data(G_OBJECT(item), "plr", plr);
  g_signal_connect(item, "activate",
		   G_CALLBACK(diplomacy_dialog_seamap_callback), pdialog);

  item = gtk_menu_item_new_with_mnemonic(_("_Maps"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
  gtk_widget_show_all(item);


  /* Advances. */
  {
    bool flag;
    int i;

    menu = gtk_menu_new();

    for (i = 1, flag = FALSE; i < game.num_tech_types; i++) {
      if (get_invention(plr0, i) == TECH_KNOWN
	  && (get_invention(plr1, i) == TECH_UNKNOWN
	      || get_invention(plr1, i) == TECH_REACHABLE)
          && tech_is_available(plr1, i)) {
	item
	  = gtk_menu_item_new_with_label(get_tech_name(game.player_ptr, i));

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_signal_connect(item, "activate",
			 G_CALLBACK(diplomacy_dialog_tech_callback),
			 GINT_TO_POINTER((plr0->player_no << 24) |
					 (plr1->player_no << 16) |
					 i));
	flag = TRUE;
      }
    }

    item = gtk_menu_item_new_with_mnemonic(_("_Advances"));
    gtk_widget_set_sensitive(item, flag);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    gtk_widget_show_all(item);
  }


  /* Cities. */

  /****************************************************************
  Creates a sorted list of plr0's cities, excluding the capital and
  any cities not visible to plr1.  This means that you can only trade 
  cities visible to requesting player.  

			      - Kris Bubendorfer
  *****************************************************************/
  {
    int i = 0, j = 0, n = city_list_size(&plr0->cities);
    struct city **city_list_ptrs;

    if (n > 0) {
      city_list_ptrs = fc_malloc(sizeof(struct city *) * n);
    } else {
      city_list_ptrs = NULL;
    }

    city_list_iterate(plr0->cities, pcity) {
      if (!is_capital(pcity)) {
	city_list_ptrs[i] = pcity;
	i++;
      }
    } city_list_iterate_end;

    qsort(city_list_ptrs, i, sizeof(struct city *), city_name_compare);
    
    menu = gtk_menu_new();

    for (j = 0; j < i; j++) {
      item = gtk_menu_item_new_with_label(city_list_ptrs[j]->name);

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      g_signal_connect(item, "activate",
		       G_CALLBACK(diplomacy_dialog_city_callback),
			 GINT_TO_POINTER((plr0->player_no << 24) |
					 (plr1->player_no << 16) |
					 city_list_ptrs[j]->id));
    }
    free(city_list_ptrs);

    item = gtk_menu_item_new_with_mnemonic(_("_Cities"));
    gtk_widget_set_sensitive(item, (i > 0));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    gtk_widget_show_all(item);
  }


  /* Give shared vision. */
  item = gtk_menu_item_new_with_mnemonic(_("_Give shared vision"));
  g_object_set_data(G_OBJECT(item), "plr", plr);
  g_signal_connect(item, "activate",
		   G_CALLBACK(diplomacy_dialog_vision_callback), pdialog);

  if (gives_shared_vision(plr0, plr1)) {
    gtk_widget_set_sensitive(item, FALSE);
  }
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
  gtk_widget_show(item);


  /* Give embassy. */
  item = gtk_menu_item_new_with_mnemonic(_("Give _embassy"));
  g_object_set_data(G_OBJECT(item), "plr", plr);
  g_signal_connect(item, "activate",
		   G_CALLBACK(diplomacy_dialog_embassy_callback), pdialog);

  if (player_has_embassy(plr1, plr0)) {
    gtk_widget_set_sensitive(item, FALSE);
  }
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
  gtk_widget_show(item);


  /* Pacts. */
  if (plr == pdialog->treaty.plr0) {
    menu = gtk_menu_new();
    item = gtk_menu_item_new_with_mnemonic(Q_("?diplomatic_state:Cease-fire"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
    g_signal_connect(item, "activate",
		     G_CALLBACK(diplomacy_dialog_ceasefire_callback), pdialog);

    item = gtk_menu_item_new_with_mnemonic(Q_("?diplomatic_state:Peace"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate",
		     G_CALLBACK(diplomacy_dialog_peace_callback), pdialog);

    item = gtk_menu_item_new_with_mnemonic(Q_("?diplomatic_state:Alliance"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate",
		     G_CALLBACK(diplomacy_dialog_alliance_callback), pdialog);

    item = gtk_menu_item_new_with_mnemonic(_("_Pacts"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    gtk_widget_show_all(item);
  }
}

/****************************************************************
...
*****************************************************************/
static void row_callback(GtkTreeView *view, GtkTreePath *path,
			 GtkTreeViewColumn *col, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  gint i;
  gint *index;

  index = gtk_tree_path_get_indices(path);

  i = 0; 
  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    if (i == index[0]) {
      dsend_packet_diplomacy_remove_clause_req(&aconnection,
					       pdialog->treaty.plr1->
					       player_no,
					       pclause->from->player_no,
					       pclause->type,
					       pclause->value);
      return;
    }
    i++;
  } clause_list_iterate_end;
}


/****************************************************************
...
*****************************************************************/
static void diplomacy_destroy(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;

  dialog_list_unlink(&dialog_list, pdialog);
  free(pdialog);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_response(GtkWidget *w, gint response, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;

  switch (response) {
  case GTK_RESPONSE_ACCEPT:


    dsend_packet_diplomacy_accept_treaty_req(&aconnection,
					     pdialog->treaty.plr1->
					     player_no);
    break;

  default:
    dsend_packet_diplomacy_cancel_meeting_req(&aconnection,
					      pdialog->treaty.plr1->
					      player_no);
    gtk_widget_destroy(w);
    break; 
  }
}

/****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
							struct player *plr1)
{
  GtkWidget *shell, *vbox, *bottom, *hbox, *table;
  GtkWidget *label, *sw, *view, *image, *spin;
  GtkWidget *menubar, *menuitem, *menu;
  GtkListStore *store;
  GtkCellRenderer *rend;

  struct Diplomacy_dialog *pdialog;
  char buf[256];

  pdialog = fc_malloc(sizeof(*pdialog));

  dialog_list_insert(&dialog_list, pdialog);
  init_treaty(&pdialog->treaty, plr0, plr1);

  shell = gtk_dialog_new_with_buttons(_("Diplomacy meeting"),
				      NULL,
				      0,
				      NULL);
  pdialog->shell = shell;
  setup_dialog(shell, toplevel);
  g_signal_connect(shell, "destroy",
		   G_CALLBACK(diplomacy_destroy), pdialog);
  g_signal_connect(shell, "response",
		   G_CALLBACK(diplomacy_response), pdialog);

  gtk_dialog_add_button(GTK_DIALOG(shell), _("_Cancel meeting"),
			GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(shell), _("Accept _treaty"),
			GTK_RESPONSE_ACCEPT);

  vbox = GTK_DIALOG(shell)->vbox;


  /* clauses. */
  store = gtk_list_store_new(1, G_TYPE_STRING);
  pdialog->store = store;

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
  g_object_unref(store);
  gtk_widget_set_size_request(view, 320, 100);

  rend = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, NULL,
    rend, "text", 0, NULL);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sw), view);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", view,
    "label", _("C_lauses:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 2);
  gtk_widget_show_all(vbox);


  /* bottom area. */
  bottom = gtk_hbox_new(TRUE, 18);
  gtk_box_pack_start(GTK_BOX(vbox), bottom, FALSE, FALSE, 10);

  /* us. */
  vbox = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 2);
  gtk_box_pack_start(GTK_BOX(bottom), vbox, TRUE, TRUE, 0);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  my_snprintf(buf, sizeof(buf),
	      "<span size=\"large\" weight=\"bold\">%s</span>",
	      get_nation_name(plr0->nation));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

  image = gtk_image_new();
  pdialog->image0 = image;
  gtk_box_pack_end(GTK_BOX(hbox), image, FALSE, FALSE, 0);

  table = gtk_table_new(2, 2, FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table), 6);
  gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

  spin = gtk_spin_button_new_with_range(0.0, plr0->economic.gold + 0.1, 1.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
  gtk_table_attach_defaults(GTK_TABLE(table), spin, 1, 2, 0, 1);
  g_object_set_data(G_OBJECT(spin), "plr", plr0);
  g_signal_connect_after(spin, "activate",
      			 G_CALLBACK(diplo_dialog_returnkey), pdialog);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", spin,
    "label", _("_Gold:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

  menubar = gtk_menu_bar_new();
  gtk_table_attach_defaults(GTK_TABLE(table), menubar, 1, 2, 1, 2);

  menu = gtk_menu_new();
  pdialog->menu0 = menu;

  menuitem = gtk_image_menu_item_new_with_mnemonic(_("_Add Clause..."));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem),
		  		gtk_image_new_from_stock(GTK_STOCK_ADD,
							 GTK_ICON_SIZE_MENU));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuitem);
  g_object_set_data(G_OBJECT(menu), "plr", plr0);
  g_signal_connect(menu, "show", G_CALLBACK(popup_add_menu), pdialog);


  /* them. */
  vbox = gtk_vbox_new(FALSE, 18);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 2);
  gtk_box_pack_start(GTK_BOX(bottom), vbox, TRUE, TRUE, 0);

  hbox = gtk_hbox_new(FALSE, 12);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

  label = gtk_label_new(NULL);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  my_snprintf(buf, sizeof(buf),
	      "<span size=\"large\" weight=\"bold\">%s</span>",
	      get_nation_name(plr1->nation));
  gtk_label_set_markup(GTK_LABEL(label), buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

  image = gtk_image_new();
  pdialog->image1 = image;
  gtk_box_pack_end(GTK_BOX(hbox), image, FALSE, FALSE, 0);

  table = gtk_table_new(2, 2, FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table), 6);
  gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

  spin = gtk_spin_button_new_with_range(0.0, plr1->economic.gold + 0.1, 1.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
  gtk_table_attach_defaults(GTK_TABLE(table), spin, 1, 2, 0, 1);
  g_object_set_data(G_OBJECT(spin), "plr", plr1);
  g_signal_connect_after(spin, "activate",
      			 G_CALLBACK(diplo_dialog_returnkey), pdialog);

  label = g_object_new(GTK_TYPE_LABEL,
    "use-underline", TRUE,
    "mnemonic-widget", spin,
    "label", _("_Gold:"),
    "xalign", 0.0,
    "yalign", 0.5,
    NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

  menubar = gtk_menu_bar_new();
  gtk_table_attach_defaults(GTK_TABLE(table), menubar, 1, 2, 1, 2);

  menu = gtk_menu_new();
  pdialog->menu1 = menu;

  menuitem = gtk_image_menu_item_new_with_mnemonic(_("_Add Clause..."));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem),
		  		gtk_image_new_from_stock(GTK_STOCK_ADD,
							 GTK_ICON_SIZE_MENU));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuitem);
  g_object_set_data(G_OBJECT(menu), "plr", plr1);
  g_signal_connect(menu, "show", G_CALLBACK(popup_add_menu), pdialog);
  
  gtk_widget_show_all(bottom);

  g_signal_connect(view, "row_activated", G_CALLBACK(row_callback), pdialog);

  update_diplomacy_dialog(pdialog);

  return pdialog;
}

/**************************************************************************
...
**************************************************************************/
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  GtkListStore *store;
  GtkTreeIter it;

  store = pdialog->store;

  gtk_list_store_clear(store);
  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    char buf[64];

    client_diplomacy_clause_string(buf, sizeof(buf), pclause);

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it, 0, buf, -1);
  } clause_list_iterate_end;

  gtk_image_set_from_pixmap(GTK_IMAGE(pdialog->image0),
			    get_thumb_pixmap(pdialog->treaty.accept0),
			    NULL);
  gtk_image_set_from_pixmap(GTK_IMAGE(pdialog->image1),
			    get_thumb_pixmap(pdialog->treaty.accept1),
			    NULL);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_tech_callback(GtkWidget *w, gpointer data)
{
  size_t choice = GPOINTER_TO_UINT(data);
  int giver = (choice >> 24) & 0xff, dest = (choice >> 16) & 0xff, other;
  int tech = choice & 0xffff;

  if (giver == game.player_idx) {
    other = dest;
  } else {
    other = giver;
  }

  dsend_packet_diplomacy_create_clause_req(&aconnection, other, giver,
					   CLAUSE_ADVANCE, tech);
}

/****************************************************************
Callback for trading cities
			      - Kris Bubendorfer
*****************************************************************/
static void diplomacy_dialog_city_callback(GtkWidget * w, gpointer data)
{
  size_t choice = GPOINTER_TO_UINT(data);
  int giver = (choice >> 24) & 0xff, dest = (choice >> 16) & 0xff, other;
  int city = choice & 0xffff;

  if (giver == game.player_idx) {
    other = dest;
  } else {
    other = giver;
  }

  dsend_packet_diplomacy_create_clause_req(&aconnection, other, giver,
					   CLAUSE_CITY, city);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  struct player *pgiver;
  
  pgiver = (struct player *)g_object_get_data(G_OBJECT(w), "plr");

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_MAP, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;
  struct player *pgiver;
  
  pgiver = (struct player *)g_object_get_data(G_OBJECT(w), "plr");

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_SEAMAP,
					   0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_add_pact_clause(GtkWidget *w, gpointer data,
					     int type)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *)data;

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pdialog->treaty.plr0->player_no,
					   type, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_ceasefire_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_CEASEFIRE);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_peace_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_PEACE);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_alliance_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_ALLIANCE);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_vision_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver =
      (struct player *) g_object_get_data(G_OBJECT(w), "plr");

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_VISION,
					   0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_embassy_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver =
      (struct player *) g_object_get_data(G_OBJECT(w), "plr");

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_EMBASSY,
					   0);
}


/*****************************************************************
...
*****************************************************************/
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  gtk_widget_destroy(pdialog->shell);
}

/*****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *find_diplomacy_dialog(int other_player_id)
{
  struct player *plr0 = game.player_ptr, *plr1 = get_player(other_player_id);

  if(!dialog_list_list_has_been_initialised) {
    dialog_list_init(&dialog_list);
    dialog_list_list_has_been_initialised = TRUE;
  }

  dialog_list_iterate(dialog_list, pdialog) {
    if ((pdialog->treaty.plr0 == plr0 && pdialog->treaty.plr1 == plr1) ||
	(pdialog->treaty.plr0 == plr1 && pdialog->treaty.plr1 == plr0)) {
      return pdialog;
    }
  } dialog_list_iterate_end;

  return NULL;
}

/*****************************************************************
...
*****************************************************************/
static void diplo_dialog_returnkey(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver =
      (struct player *) g_object_get_data(G_OBJECT(w), "plr");
  int amount = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
  
  if (amount >= 0 && amount <= pgiver->economic.gold) {
    dsend_packet_diplomacy_create_clause_req(&aconnection,
					     pdialog->treaty.plr1->
					     player_no, pgiver->player_no,
					     CLAUSE_GOLD, amount);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), 0.0);
  } else {
    append_output_window(_("Game: Invalid amount of gold specified."));
  }
}

/*****************************************************************
  Close all dialogs, for when client disconnects from game.
*****************************************************************/
void close_all_diplomacy_dialogs(void)
{
  if (!dialog_list_list_has_been_initialised) {
    return;
  }

  while (dialog_list_size(&dialog_list) > 0) {
    close_diplomacy_dialog(dialog_list_get(&dialog_list, 0));
  }
}
