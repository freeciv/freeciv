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

#include "diptreaty.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "climisc.h"
#include "clinet.h"

#include "chatline.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "diplodlg.h"

#define MAX_NUM_CLAUSES 64

struct Diplomacy_dialog {
  struct Treaty treaty;
  
  GtkWidget *dip_dialog_shell;
  GtkWidget *dip_hbox, *dip_vbox0, *dip_vboxm, *dip_vbox1;
  
  GtkWidget *dip_frame0;
  GtkWidget *dip_labelm;
  GtkWidget *dip_frame1;

  GtkWidget *dip_map_menu0;
  GtkWidget *dip_map_menu1;
  GtkWidget *dip_tech_menu0;
  GtkWidget *dip_tech_menu1;
  GtkWidget *dip_city_menu0;
  GtkWidget *dip_city_menu1;
  GtkWidget *dip_gold_frame0;
  GtkWidget *dip_gold_frame1;
  GtkWidget *dip_gold_entry0;
  GtkWidget *dip_gold_entry1;
  GtkWidget *dip_pact_menu;
  GtkWidget *dip_vision_button0;
  GtkWidget *dip_vision_button1;
  GtkWidget *dip_embassy_button0;
  GtkWidget *dip_embassy_button1;

  GtkWidget *dip_label;
  GtkWidget *dip_clauselabel;
  GtkWidget *dip_clauselist;
  GtkWidget *dip_acceptthumb0;
  GtkWidget *dip_acceptthumb1;
  
  GtkWidget *dip_accept_command;
  GtkWidget *dip_close_command;

  GtkWidget *dip_erase_clause_command;
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
static void diplomacy_dialog_close_callback(GtkWidget *w, gpointer data);
static gint diplomacy_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					     gpointer data);
static void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_erase_clause_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_accept_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_tech_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_city_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_ceasefire_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_peace_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_alliance_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_vision_callback(GtkWidget *w, gpointer data);
static void diplomacy_dialog_embassy_callback(GtkWidget *w, gpointer data);
static void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static gint diplomacy_dialog_mbutton_callback(GtkWidget *w, GdkEvent *event);
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
    gtk_set_relative_position(toplevel, pdialog->dip_dialog_shell, 0, 0);
  }

  gtk_widget_show(pdialog->dip_dialog_shell);
}

/****************************************************************
...
*****************************************************************/
static int fill_diplomacy_tech_menu(GtkWidget *popupmenu, 
				    struct player *plr0, struct player *plr1)
{
  int i, flag;

  for(i=1, flag=0; i<game.num_tech_types; i++) {
    if (get_invention(plr0, i) == TECH_KNOWN
        && (get_invention(plr1, i) == TECH_UNKNOWN
	    || get_invention(plr1, i) == TECH_REACHABLE)
        && tech_is_available(plr1, i)) {
	  GtkWidget *item=gtk_menu_item_new_with_label(advances[i].name);

	  gtk_menu_append(GTK_MENU(popupmenu),item);
	  gtk_signal_connect(GTK_OBJECT(item), "activate",
			     GTK_SIGNAL_FUNC(diplomacy_dialog_tech_callback),
			 GINT_TO_POINTER((plr0->player_no << 24) |
					 (plr1->player_no << 16) |
					 i));
      flag=1;
    }
  }

  return flag;
}

/****************************************************************
Creates a sorted list of plr0's cities, excluding the capital and
any cities not visible to plr1.  This means that you can only trade 
cities visible to requesting player.  

			    - Kris Bubendorfer
*****************************************************************/
static int fill_diplomacy_city_menu(GtkWidget * popupmenu,
				    struct player *pgiver,
				    struct player *pdest)
{
  int i = 0, j = 0, n = city_list_size(&pgiver->cities);
  struct city **city_list_ptrs;

  if (n > 0) {
    city_list_ptrs = fc_malloc(sizeof(struct city *) * n);
  } else {
    city_list_ptrs = NULL;
  }

  city_list_iterate(pgiver->cities, pcity) {
    if (!is_capital(pcity)) {
      city_list_ptrs[i] = pcity;
      i++;
    }
  } city_list_iterate_end;

  qsort(city_list_ptrs, i, sizeof(struct city *), city_name_compare);
  
  for (j = 0; j < i; j++) {
      GtkWidget *item =
	  gtk_menu_item_new_with_label(city_list_ptrs[j]->name);

      gtk_menu_append(GTK_MENU(popupmenu),item);
      gtk_signal_connect(GTK_OBJECT(item), "activate",
			 GTK_SIGNAL_FUNC(diplomacy_dialog_city_callback),
			 GINT_TO_POINTER((pgiver->player_no << 24) |
					 (pdest->player_no << 16) |
					 city_list_ptrs[j]->id));
  }
  free(city_list_ptrs);
  return i;
}

/****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
							struct player *plr1)
{
  char buf[512];
  static const char *titles_[1]
      = { N_("The following clauses have been agreed upon:") };
  static gchar **titles;
  struct Diplomacy_dialog *pdialog;
  GtkWidget *button,*label,*item,*table,*scrolled;

  if (!titles) titles = intl_slist(1, titles_);
  
  pdialog=fc_malloc(sizeof(struct Diplomacy_dialog));
  dialog_list_insert(&dialog_list, pdialog);
  
  init_treaty(&pdialog->treaty, plr0, plr1);
  
  pdialog->dip_dialog_shell = gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(pdialog->dip_dialog_shell),_("Diplomacy meeting"));
  gtk_container_border_width(GTK_CONTAINER(pdialog->dip_dialog_shell),5);

  pdialog->dip_hbox = gtk_hbox_new(FALSE,0);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(pdialog->dip_dialog_shell)->vbox),
	pdialog->dip_hbox);


  my_snprintf(buf, sizeof(buf),
	      _("The %s offerings"), get_nation_name(plr0->nation));
  pdialog->dip_frame0=gtk_frame_new(buf);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_hbox),pdialog->dip_frame0, TRUE, FALSE, 2);

  pdialog->dip_vboxm = gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_hbox),pdialog->dip_vboxm, TRUE, FALSE, 2);

  my_snprintf(buf, sizeof(buf),
	      _("The %s offerings"), get_nation_name(plr1->nation));
  pdialog->dip_frame1=gtk_frame_new(buf);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_hbox),pdialog->dip_frame1, TRUE, FALSE, 2);


  pdialog->dip_vbox0 = gtk_vbox_new(FALSE,0);
  gtk_container_add(GTK_CONTAINER(pdialog->dip_frame0), pdialog->dip_vbox0);

  pdialog->dip_vbox1 = gtk_vbox_new(FALSE,0);
  gtk_container_add(GTK_CONTAINER(pdialog->dip_frame1), pdialog->dip_vbox1);


  pdialog->dip_map_menu0=gtk_menu_new();
  item=gtk_menu_item_new_with_label(_("World-map"));
  gtk_menu_append(GTK_MENU(pdialog->dip_map_menu0),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_map_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(_("Sea-map"));
  gtk_menu_append(GTK_MENU(pdialog->dip_map_menu0),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_seamap_callback),(gpointer)pdialog);
  gtk_widget_show_all(pdialog->dip_map_menu0);

  button=gtk_button_new_with_label(_("Maps"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_map_menu0));
  
  pdialog->dip_map_menu1=gtk_menu_new();
  item=gtk_menu_item_new_with_label(_("World-map"));
  gtk_menu_append(GTK_MENU(pdialog->dip_map_menu1),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_map_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(_("Sea-map"));
  gtk_menu_append(GTK_MENU(pdialog->dip_map_menu1),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_seamap_callback),(gpointer)pdialog);
  gtk_widget_show_all(pdialog->dip_map_menu1);

  button=gtk_button_new_with_label(_("Maps"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_map_menu1));


  pdialog->dip_tech_menu0=gtk_menu_new();
  button=gtk_button_new_with_label(_("Advances"));

  if(!fill_diplomacy_tech_menu(pdialog->dip_tech_menu0, plr0, plr1))
    gtk_widget_set_sensitive(button, FALSE);
  gtk_widget_show_all(pdialog->dip_tech_menu0);

  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_tech_menu0));

  pdialog->dip_tech_menu1=gtk_menu_new();
  button=gtk_button_new_with_label(_("Advances"));

  if(!fill_diplomacy_tech_menu(pdialog->dip_tech_menu1, plr1, plr0))
    gtk_widget_set_sensitive(button, FALSE);
  gtk_widget_show_all(pdialog->dip_tech_menu1);

  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_tech_menu1));

  /* Start of trade city code - Kris Bubendorfer */

  pdialog->dip_city_menu0=gtk_menu_new();
  button=gtk_button_new_with_label(_("Cities"));

  gtk_widget_set_sensitive(button,
	fill_diplomacy_city_menu(pdialog->dip_city_menu0, plr0, plr1));
  gtk_widget_show_all(pdialog->dip_city_menu0);

  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_city_menu0));

  pdialog->dip_city_menu1=gtk_menu_new();
  button=gtk_button_new_with_label(_("Cities"));

  gtk_widget_set_sensitive(button,
	fill_diplomacy_city_menu(pdialog->dip_city_menu1, plr1, plr0));
  gtk_widget_show_all(pdialog->dip_city_menu1);

  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_city_menu1));
  
  /* End of trade city code */

  pdialog->dip_gold_entry0=gtk_entry_new();

  pdialog->dip_gold_entry1=gtk_entry_new();
  
  my_snprintf(buf, sizeof(buf), _("Gold(max %d)"), plr0->economic.gold);
  pdialog->dip_gold_frame0=gtk_frame_new(buf);
  gtk_container_add(GTK_CONTAINER(pdialog->dip_gold_frame0),
	pdialog->dip_gold_entry0);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),
	pdialog->dip_gold_frame0, FALSE,FALSE,2);

  my_snprintf(buf, sizeof(buf), _("Gold(max %d)"), plr1->economic.gold);
  pdialog->dip_gold_frame1=gtk_frame_new(buf);
  gtk_container_add(GTK_CONTAINER(pdialog->dip_gold_frame1),
	pdialog->dip_gold_entry1);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1),
	pdialog->dip_gold_frame1, FALSE,FALSE,2);


  pdialog->dip_vision_button0=gtk_button_new_with_label(_("Give shared vision"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0), pdialog->dip_vision_button0,
		     FALSE, FALSE, 2);
  gtk_signal_connect(GTK_OBJECT(pdialog->dip_vision_button0), "clicked",
		     GTK_SIGNAL_FUNC(diplomacy_dialog_vision_callback),
		     (gpointer)pdialog);
  if (gives_shared_vision(plr0, plr1))
    gtk_widget_set_sensitive(pdialog->dip_vision_button0, FALSE);

  pdialog->dip_vision_button1=gtk_button_new_with_label(_("Give shared vision"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1), pdialog->dip_vision_button1,
		     FALSE, FALSE, 2);
  gtk_signal_connect(GTK_OBJECT(pdialog->dip_vision_button1), "clicked",
		     GTK_SIGNAL_FUNC(diplomacy_dialog_vision_callback),
		     (gpointer)pdialog);
  if (gives_shared_vision(plr1, plr0))
    gtk_widget_set_sensitive(pdialog->dip_vision_button1, FALSE);

  pdialog->dip_embassy_button0 = gtk_button_new_with_label(_("Give embassy"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),
		     pdialog->dip_embassy_button0,
		     FALSE, FALSE, 2);
  gtk_signal_connect(GTK_OBJECT(pdialog->dip_embassy_button0), "clicked",
                     GTK_SIGNAL_FUNC(diplomacy_dialog_embassy_callback),
		     (gpointer)pdialog);
  if (player_has_embassy(plr1, plr0)) {
    gtk_widget_set_sensitive(pdialog->dip_embassy_button0, FALSE);
  }

  pdialog->dip_embassy_button1 = gtk_button_new_with_label(_("Give embassy"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox1),
		     pdialog->dip_embassy_button1,
		     FALSE, FALSE, 2);
  gtk_signal_connect(GTK_OBJECT(pdialog->dip_embassy_button1), "clicked",
                     GTK_SIGNAL_FUNC(diplomacy_dialog_embassy_callback),
		     (gpointer)pdialog);
  if (player_has_embassy(plr0, plr1)) {
    gtk_widget_set_sensitive(pdialog->dip_embassy_button1, FALSE);
  }

  /* Start of pact button insertion */

  pdialog->dip_pact_menu=gtk_menu_new();

  item=gtk_menu_item_new_with_label(Q_("?diplomatic_state:Cease-fire"));
  gtk_menu_append(GTK_MENU(pdialog->dip_pact_menu),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_ceasefire_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(Q_("?diplomatic_state:Peace"));
  gtk_menu_append(GTK_MENU(pdialog->dip_pact_menu),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_peace_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(Q_("?diplomatic_state:Alliance"));
  gtk_menu_append(GTK_MENU(pdialog->dip_pact_menu),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_alliance_callback),(gpointer)pdialog);

  gtk_widget_show_all(pdialog->dip_pact_menu);

  button=gtk_button_new_with_label(_("Pacts"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vbox0),button, FALSE,FALSE,2);
  gtk_signal_connect_object(GTK_OBJECT(button), "event",
	GTK_SIGNAL_FUNC(diplomacy_dialog_mbutton_callback),
	GTK_OBJECT(pdialog->dip_pact_menu));

  /* End of pact button insertion */

  my_snprintf(buf, sizeof(buf),
	       _("This Eternal Treaty\n"
		 "marks the results of the diplomatic work between\n"
		 "The %s %s %s\nand\nThe %s %s %s"),
	  get_nation_name(plr0->nation),
	  get_ruler_title(plr0->government, plr0->is_male, plr0->nation),
	  plr0->name,
	  get_nation_name(plr1->nation),
	  get_ruler_title(plr1->government, plr1->is_male, plr1->nation),
	  plr1->name);
  
  pdialog->dip_labelm=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vboxm),
		pdialog->dip_labelm, TRUE, FALSE, 2);


  pdialog->dip_clauselist = gtk_clist_new_with_titles(1, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(pdialog->dip_clauselist));
  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled),pdialog->dip_clauselist);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
  gtk_widget_set_usize(scrolled, 350, 90);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vboxm),
		scrolled, TRUE, FALSE, 2);


  table=gtk_table_new(1,4,FALSE);
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vboxm), table, TRUE, FALSE, 2);

  my_snprintf(buf, sizeof(buf), _("%s view:"), get_nation_name(plr0->nation));
  label=gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table),label,0,1,0,1);

  pdialog->dip_acceptthumb0=gtk_pixmap_new(get_thumb_pixmap(0),NULL);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(pdialog->dip_acceptthumb0),FALSE);
  gtk_table_attach_defaults(GTK_TABLE(table),pdialog->dip_acceptthumb0,1,2,0,1);
  gtk_widget_show(pdialog->dip_acceptthumb0);

  my_snprintf(buf, sizeof(buf), _("%s view:"), get_nation_name(plr1->nation));
  label=gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table),label,2,3,0,1);

  pdialog->dip_acceptthumb1=gtk_pixmap_new(get_thumb_pixmap(0),NULL);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(pdialog->dip_acceptthumb1),FALSE);
  gtk_table_attach_defaults(GTK_TABLE(table),pdialog->dip_acceptthumb1,3,4,0,1);
  gtk_widget_show(pdialog->dip_acceptthumb1);
  
  pdialog->dip_erase_clause_command=gtk_button_new_with_label(_("Erase clause"));
  gtk_box_pack_start(GTK_BOX(pdialog->dip_vboxm),
	pdialog->dip_erase_clause_command, TRUE, FALSE, 2 );

  pdialog->dip_accept_command=gtk_button_new_with_label(_("Accept treaty"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->dip_dialog_shell)->action_area),
	pdialog->dip_accept_command, TRUE, TRUE, 2 );


  pdialog->dip_close_command=gtk_button_new_with_label(_("Cancel meeting"));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pdialog->dip_dialog_shell)->action_area),
	pdialog->dip_close_command, TRUE, TRUE, 2 );

  gtk_signal_connect(GTK_OBJECT(pdialog->dip_dialog_shell), "delete_event",
		     GTK_SIGNAL_FUNC(diplomacy_dialog_delete_callback),
		     pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->dip_close_command), "clicked",
	GTK_SIGNAL_FUNC(diplomacy_dialog_close_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->dip_erase_clause_command), "clicked", 
	GTK_SIGNAL_FUNC(diplomacy_dialog_erase_clause_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->dip_accept_command), "clicked",
	GTK_SIGNAL_FUNC(diplomacy_dialog_accept_callback), pdialog);

  gtk_signal_connect(GTK_OBJECT(pdialog->dip_gold_entry0),"activate",
	GTK_SIGNAL_FUNC(diplo_dialog_returnkey),NULL);
  gtk_signal_connect(GTK_OBJECT(pdialog->dip_gold_entry1),"activate",
	GTK_SIGNAL_FUNC(diplo_dialog_returnkey),NULL);

  gtk_widget_realize(pdialog->dip_dialog_shell);

  gtk_widget_show_all(GTK_DIALOG(pdialog->dip_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(pdialog->dip_dialog_shell)->action_area);

  update_diplomacy_dialog(pdialog);

  return pdialog;
}


/**************************************************************************
...
**************************************************************************/
static void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
         char              buf		[64];
  static char             *row		[1];
  
  row[0]=buf;

  gtk_clist_freeze(GTK_CLIST(pdialog->dip_clauselist));
  gtk_clist_clear(GTK_CLIST(pdialog->dip_clauselist));

  clause_list_iterate(pdialog->treaty.clauses, pclause) {
    client_diplomacy_clause_string(buf, sizeof(buf), pclause);
    gtk_clist_append(GTK_CLIST(pdialog->dip_clauselist),row);
  } clause_list_iterate_end;

  gtk_clist_thaw(GTK_CLIST(pdialog->dip_clauselist));
  gtk_widget_show_all(pdialog->dip_clauselist);

  gtk_set_bitmap(pdialog->dip_acceptthumb0,
		 get_thumb_pixmap(pdialog->treaty.accept0));
  gtk_set_bitmap(pdialog->dip_acceptthumb1, 
		 get_thumb_pixmap(pdialog->treaty.accept1));
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_tech_callback(GtkWidget * w, gpointer data)
{
  int choice = GPOINTER_TO_INT(data);
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
  int choice = GPOINTER_TO_INT(data);
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
static void diplomacy_dialog_erase_clause_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  GList              *selection;

  if ( (selection=GTK_CLIST(pdialog->dip_clauselist)->selection) ) {
    int i = 0, row;
    
    row = GPOINTER_TO_INT(selection->data);
  
    clause_list_iterate(pdialog->treaty.clauses, pclause) {
      if (i == row) {
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
}

/****************************************************************
...
*****************************************************************/
static gint diplomacy_dialog_mbutton_callback(GtkWidget *w, GdkEvent *event)
{
  GdkEventButton *bevent = (GdkEventButton *)event;
  
  if ( event->type != GDK_BUTTON_PRESS )
    return FALSE;

  gtk_menu_popup(GTK_MENU(w),NULL,NULL,NULL,NULL,bevent->button,bevent->time);

  return TRUE;
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver = ((w->parent) == pdialog->dip_map_menu0) ?
      pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_MAP, 0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver = ((w->parent) == pdialog->dip_map_menu0) ?
      pdialog->treaty.plr0 : pdialog->treaty.plr1;

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_SEAMAP,
					   0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_add_pact_clause(GtkWidget * w, gpointer data,
					     int type)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;
  struct player *pgiver = ((w->parent) == pdialog->dip_map_menu0) ?
      pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, type, 0);
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
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct player *pgiver = (w == pdialog->dip_vision_button0) ?
      pdialog->treaty.plr0 : pdialog->treaty.plr1;

  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_VISION,
					   0);
}

/****************************************************************
  Called whenever give embassy button is clicked
*****************************************************************/
static void diplomacy_dialog_embassy_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;    
  struct player *pgiver = (w == pdialog->dip_embassy_button0) ?
      pdialog->treaty.plr0 : pdialog->treaty.plr1;
  dsend_packet_diplomacy_create_clause_req(&aconnection,
					   pdialog->treaty.plr1->player_no,
					   pgiver->player_no, CLAUSE_EMBASSY,
					   0);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_close_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog = (struct Diplomacy_dialog *) data;

  dsend_packet_diplomacy_cancel_meeting_req(&aconnection,
					    pdialog->treaty.plr1->player_no);
  
  close_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
static gint diplomacy_dialog_delete_callback(GtkWidget * w, GdkEvent * ev,
					     gpointer data)
{
  diplomacy_dialog_close_callback(NULL, data);
  return FALSE;
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_accept_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;

  dsend_packet_diplomacy_accept_treaty_req(&aconnection,
					   pdialog->treaty.plr1->player_no);
}


/*****************************************************************
...
*****************************************************************/
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  gtk_widget_destroy(pdialog->dip_dialog_shell);
  
  dialog_list_unlink(&dialog_list, pdialog);
  free(pdialog);
}

/*****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *find_diplomacy_dialog(int other_player_id)
{
  struct player *plr0 = game.player_ptr, *plr1 = get_player(other_player_id);

  if (!dialog_list_list_has_been_initialised) {
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
static struct Diplomacy_dialog *find_diplomacy_by_input(GtkWidget *w)
{
  dialog_list_iterate(dialog_list, pdialog) {
    if ((pdialog->dip_gold_entry0 == w) || (pdialog->dip_gold_entry1 == w)) {
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
  struct Diplomacy_dialog *pdialog = find_diplomacy_by_input(w);
  
  if (pdialog) {
    struct player *pgiver = (w == pdialog->dip_gold_entry0) ?
	pdialog->treaty.plr0 : pdialog->treaty.plr1;
    char *dp = gtk_entry_get_text(GTK_ENTRY(w));
    int amount;
    
    if (sscanf(dp, "%d", &amount) == 1 && amount >= 0
	&& amount <= pgiver->economic.gold) {
      dsend_packet_diplomacy_create_clause_req(&aconnection,
					       pdialog->treaty.plr1->
					       player_no, pgiver->player_no,
					       CLAUSE_GOLD, amount);
      gtk_entry_set_text(GTK_ENTRY(w), "");
    } else {
      append_output_window(_("Game: Invalid amount of gold specified."));
    }
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
