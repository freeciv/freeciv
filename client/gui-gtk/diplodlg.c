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
#include <ctype.h>

#include <gtk/gtk.h>

#include "capability.h"
#include "fcintl.h"
#include "game.h"
#include "genlist.h"
#include "government.h"
#include "map.h"
#include "mem.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "diptreaty.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "diplodlg.h"

extern GtkWidget *toplevel;

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

  GtkWidget *dip_label;
  GtkWidget *dip_clauselabel;
  GtkWidget *dip_clauselist;
  GtkWidget *dip_acceptthumb0;
  GtkWidget *dip_acceptthumb1;
  
  GtkWidget *dip_accept_command;
  GtkWidget *dip_close_command;

  GtkWidget *dip_erase_clause_command;
};

char *dummy_clause_list_strings[]={ "\n", "\n", "\n", "\n", "\n", "\n", 0};

struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1);

struct genlist diplomacy_dialogs;
int diplomacy_dialogs_list_has_been_initialised;

struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
					       struct player *plr1);
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1);
void diplomacy_dialog_close_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_erase_clause_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_accept_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_tech_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_city_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_ceasefire_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_peace_callback(GtkWidget *w, gpointer data);
void diplomacy_dialog_alliance_callback(GtkWidget *w, gpointer data);
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog);
static gint diplomacy_dialog_mbutton_callback(GtkWidget *w, GdkEvent *event);


/****************************************************************
...
*****************************************************************/
void handle_diplomacy_accept_treaty(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				&game.players[pa->plrno1]))) {
    if(pa->plrno_from==game.player_idx)
      pdialog->treaty.accept0=!pdialog->treaty.accept0;
    else
      pdialog->treaty.accept1=!pdialog->treaty.accept1;
    update_diplomacy_dialog(pdialog);
  }
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_init_meeting(struct packet_diplomacy_info *pa)
{
  popup_diplomacy_dialog(&game.players[pa->plrno0], 
			 &game.players[pa->plrno1]);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_cancel_meeting(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				    &game.players[pa->plrno1])))
    close_diplomacy_dialog(pdialog);
}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_create_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				&game.players[pa->plrno1]))) {
    add_clause(&pdialog->treaty, &game.players[pa->plrno_from],
	       pa->clause_type, pa->value);
    update_diplomacy_dialog(pdialog);
  }

}

/****************************************************************
...
*****************************************************************/
void handle_diplomacy_remove_clause(struct packet_diplomacy_info *pa)
{
  struct Diplomacy_dialog *pdialog;

  if((pdialog=find_diplomacy_dialog(&game.players[pa->plrno0],
				&game.players[pa->plrno1]))) {
    remove_clause(&pdialog->treaty, &game.players[pa->plrno_from],
		  pa->clause_type, pa->value);
    
    update_diplomacy_dialog(pdialog);
  }

}




/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_diplomacy_dialog(struct player *plr0, struct player *plr1)
{
  struct Diplomacy_dialog *pdialog;
  
  if(!(pdialog=find_diplomacy_dialog(plr0, plr1))) {
    pdialog=create_diplomacy_dialog(plr0, plr1);
    gtk_set_relative_position(toplevel,pdialog->dip_dialog_shell,0,0);
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
    if(get_invention(plr0, i)==TECH_KNOWN && 
       (get_invention(plr1, i)==TECH_UNKNOWN || 
	get_invention(plr1, i)==TECH_REACHABLE))
	{
	  GtkWidget *item=gtk_menu_item_new_with_label(advances[i].name);

	  gtk_menu_append(GTK_MENU(popupmenu),item);
	  gtk_signal_connect(GTK_OBJECT(item),"activate",
	    GTK_SIGNAL_FUNC(diplomacy_dialog_tech_callback),
	    (gpointer)(plr0->player_no*10000+plr1->player_no*100+i));
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
/* FIXME - this one should be sorted -vasco */
static int fill_diplomacy_city_menu(GtkWidget *popupmenu, 
				    struct player *plr0, struct player *plr1)
{
  int flag=0;

  city_list_iterate(plr0->cities, pcity) {
    if(!city_got_effect(pcity, B_PALACE)){
      GtkWidget *item=gtk_menu_item_new_with_label(pcity->name);

      gtk_menu_append(GTK_MENU(popupmenu),item);
      gtk_signal_connect(GTK_OBJECT(item),"activate",
        GTK_SIGNAL_FUNC(diplomacy_dialog_city_callback),
        (gpointer)(pcity->id*1024 + plr0->player_no*32 + plr1->player_no));
      flag=1;
    }
  } city_list_iterate_end;

  return flag;
}


/****************************************************************
...
*****************************************************************/
struct Diplomacy_dialog *create_diplomacy_dialog(struct player *plr0, 
						 struct player *plr1)
{
  char buf[512];
  static gchar *titles_[1]
    = { N_("The following clauses have been agreed upon:") };
  static gchar **titles;
  struct Diplomacy_dialog *pdialog;
  GtkWidget *button,*label,*item,*table,*scrolled;

  if (!titles) titles = intl_slist(1, titles_);
  
  pdialog=fc_malloc(sizeof(struct Diplomacy_dialog));
  genlist_insert(&diplomacy_dialogs, pdialog, 0);
  
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


  /* Start of pact button insertion */

  pdialog->dip_pact_menu=gtk_menu_new();

  item=gtk_menu_item_new_with_label(_("Cease-fire"));
  gtk_menu_append(GTK_MENU(pdialog->dip_pact_menu),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_ceasefire_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(_("Peace"));
  gtk_menu_append(GTK_MENU(pdialog->dip_pact_menu),item);
  gtk_signal_connect(GTK_OBJECT(item),"activate",
	GTK_SIGNAL_FUNC(diplomacy_dialog_peace_callback),(gpointer)pdialog);

  item=gtk_menu_item_new_with_label(_("Alliance"));
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
  gtk_table_attach_defaults(GTK_TABLE(table),pdialog->dip_acceptthumb0,1,2,0,1);
  gtk_widget_show(pdialog->dip_acceptthumb0);

  my_snprintf(buf, sizeof(buf), _("%s view:"), get_nation_name(plr1->nation));
  label=gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table),label,2,3,0,1);

  pdialog->dip_acceptthumb1=gtk_pixmap_new(get_thumb_pixmap(0),NULL);
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
void update_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
         int               i;
         char              buf		[64];
  static char             *row		[1];
  struct genlist_iterator  myiter;
  
  row[0]=buf;

  gtk_clist_freeze(GTK_CLIST(pdialog->dip_clauselist));
  gtk_clist_clear(GTK_CLIST(pdialog->dip_clauselist));

  genlist_iterator_init(&myiter, &pdialog->treaty.clauses, 0);
  
  for(i=0; i<MAX_NUM_CLAUSES && ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);
    client_diplomacy_clause_string(buf, sizeof(buf), pclause);
    gtk_clist_append(GTK_CLIST(pdialog->dip_clauselist),row);
    i++;
  }

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
void diplomacy_dialog_tech_callback(GtkWidget *w, gpointer data)
{
  size_t choice;
  struct packet_diplomacy_info pa;
  
  choice=(size_t)data;

  pa.plrno0=choice/10000;
  pa.plrno1=(choice/100)%100;
  pa.clause_type=CLAUSE_ADVANCE;
  pa.plrno_from=pa.plrno0;
  pa.value=choice%100;
    
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			    &pa);
}

/****************************************************************
Callback for trading cities
			      - Kris Bubendorfer
*****************************************************************/
void diplomacy_dialog_city_callback(GtkWidget *w, gpointer data)
{
  size_t choice;
  struct packet_diplomacy_info pa;
  
  choice=(size_t)data;

  pa.value = choice/1024;
  choice -= pa.value * 1024;
  pa.plrno0 = choice/32;
  choice -= pa.plrno0 * 32;
  pa.plrno1 = choice;
 
  pa.clause_type=CLAUSE_CITY;
  pa.plrno_from=pa.plrno0;

  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}


/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_erase_clause_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  GList              *selection;

  if ( (selection=GTK_CLIST(pdialog->dip_clauselist)->selection) ) {
    int i,row;
    struct genlist_iterator myiter;
    
    row = (int)selection->data;
  
    genlist_iterator_init(&myiter, &pdialog->treaty.clauses, 0);

    for(i=0; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter), i++) {
      if(i==row) {
	struct packet_diplomacy_info pa;
	struct Clause *pclause=(struct Clause *)ITERATOR_PTR(myiter);

	pa.plrno0=pdialog->treaty.plr0->player_no;
	pa.plrno1=pdialog->treaty.plr1->player_no;
	pa.plrno_from=pclause->from->player_no;
	pa.clause_type=pclause->type;
	pa.value=pclause->value;
	send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_REMOVE_CLAUSE,
				   &pa);
	return;
      }
    }
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
void diplomacy_dialog_map_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  
  pgiver=((w->parent)==pdialog->dip_map_menu0) ? 
    pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_MAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_seamap_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct packet_diplomacy_info pa;
  struct player *pgiver;
  
  pgiver=((w->parent)==pdialog->dip_map_menu0) ? 
    pdialog->treaty.plr0 : pdialog->treaty.plr1;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.clause_type=CLAUSE_SEAMAP;
  pa.plrno_from=pgiver->player_no;
  pa.value=0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

/****************************************************************
...
*****************************************************************/
static void diplomacy_dialog_add_pact_clause(GtkWidget *w, gpointer data,
					     int type)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct packet_diplomacy_info pa;
  
  pa.plrno0 = pdialog->treaty.plr0->player_no;
  pa.plrno1 = pdialog->treaty.plr1->player_no;
  pa.clause_type = type;
  pa.plrno_from = pdialog->treaty.plr0->player_no;
  pa.value = 0;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
			     &pa);
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_ceasefire_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_CEASEFIRE);
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_peace_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_PEACE);
}

/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_alliance_callback(GtkWidget *w, gpointer data)
{
  diplomacy_dialog_add_pact_clause(w, data, CLAUSE_ALLIANCE);
}



/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_close_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct packet_diplomacy_info pa;

  pa.plrno0=game.player_idx;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CANCEL_MEETING, 
			     &pa);
  
  close_diplomacy_dialog(pdialog);
}


/****************************************************************
...
*****************************************************************/
void diplomacy_dialog_accept_callback(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog=(struct Diplomacy_dialog *)data;
  struct packet_diplomacy_info pa;
  
  pa.plrno0=pdialog->treaty.plr0->player_no;
  pa.plrno1=pdialog->treaty.plr1->player_no;
  pa.plrno_from=game.player_idx;
  send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_ACCEPT_TREATY,
			     &pa);
}


/*****************************************************************
...
*****************************************************************/
void close_diplomacy_dialog(struct Diplomacy_dialog *pdialog)
{
  gtk_widget_destroy(pdialog->dip_dialog_shell);
  
  genlist_unlink(&diplomacy_dialogs, pdialog);
  free(pdialog);
}

/*****************************************************************
...
*****************************************************************/
struct Diplomacy_dialog *find_diplomacy_dialog(struct player *plr0, 
					       struct player *plr1)
{
  struct genlist_iterator myiter;

  if(!diplomacy_dialogs_list_has_been_initialised) {
    genlist_init(&diplomacy_dialogs);
    diplomacy_dialogs_list_has_been_initialised=1;
  }
  
  genlist_iterator_init(&myiter, &diplomacy_dialogs, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Diplomacy_dialog *pdialog=
      (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->treaty.plr0==plr0 && pdialog->treaty.plr1==plr1) ||
       (pdialog->treaty.plr0==plr1 && pdialog->treaty.plr1==plr0))
      return pdialog;
  }
  return 0;
}

/*****************************************************************
...
*****************************************************************/
static struct Diplomacy_dialog *find_diplomacy_by_input(GtkWidget *w)
{
  struct genlist_iterator myiter;
  
  genlist_iterator_init(&myiter, &diplomacy_dialogs, 0);
    
  for(; ITERATOR_PTR(myiter); ITERATOR_NEXT(myiter)) {
    struct Diplomacy_dialog *pdialog=
      (struct Diplomacy_dialog *)ITERATOR_PTR(myiter);
    if((pdialog->dip_gold_entry0==w) || (pdialog->dip_gold_entry1==w)) {
      return pdialog;
    }
  }
  return 0;
}

/*****************************************************************
...
*****************************************************************/
void diplo_dialog_returnkey(GtkWidget *w, gpointer data)
{
  struct Diplomacy_dialog *pdialog;
  
  if((pdialog=find_diplomacy_by_input(w))) {
    struct player *pgiver;
    char *dp;
    int amount;
    
    pgiver=(w==pdialog->dip_gold_entry0) ? 
      pdialog->treaty.plr0 : pdialog->treaty.plr1;
    
    dp=gtk_entry_get_text(GTK_ENTRY(w));
    amount=atoi(dp);
    
    if(amount>=0 && amount<=pgiver->economic.gold) {
      struct packet_diplomacy_info pa;
      pa.plrno0=pdialog->treaty.plr0->player_no;
      pa.plrno1=pdialog->treaty.plr1->player_no;
      pa.clause_type=CLAUSE_GOLD;
      pa.plrno_from=pgiver->player_no;
      pa.value=amount;
      send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_CREATE_CLAUSE,
				 &pa);
      gtk_entry_set_text(GTK_ENTRY(w),"");
    }
    else
      append_output_window(_("Game: Invalid amount of gold specified."));
  }
}
