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
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "inteldlg.h"

/******************************************************************/
static GtkWidget *intel_dialog_shell;
/******************************************************************/


static void intel_create_dialog(struct player *p);

/****************************************************************
... 
*****************************************************************/
void popup_intel_dialog(struct player *p)
{
  if(!intel_dialog_shell) {
    intel_create_dialog(p);
  }

  gtk_window_present(GTK_WINDOW(intel_dialog_shell));
}



/****************************************************************
...
*****************************************************************/
void intel_create_dialog(struct player *p)
{
  GtkWidget *label, *hbox, *vbox, *sw, *view;
  GtkListStore *store;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  char buf[64];
  struct city *pcity;

  int i;
  
  intel_dialog_shell =
    gtk_dialog_new_with_buttons(_("Foreign Intelligence Report"),
      NULL,
      0,
      GTK_STOCK_CLOSE,
      GTK_RESPONSE_CLOSE,
      NULL);
  if (dialogs_on_top) {
    gtk_window_set_transient_for(GTK_WINDOW(intel_dialog_shell),
				 GTK_WINDOW(toplevel));
  }
  gtk_dialog_set_default_response(GTK_DIALOG(intel_dialog_shell),
        GTK_RESPONSE_CLOSE);

  g_signal_connect(intel_dialog_shell, "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  g_signal_connect(intel_dialog_shell, "destroy",
                   G_CALLBACK(gtk_widget_destroyed), &intel_dialog_shell);

  my_snprintf(buf, sizeof(buf),
	      _("Intelligence Information for the %s Empire"), 
	      get_nation_name(p->nation));

  label=gtk_frame_new(buf);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(intel_dialog_shell)->vbox),
	label, TRUE, FALSE, 2);

  vbox=gtk_vbox_new(FALSE,0);
  gtk_container_add(GTK_CONTAINER(label), vbox);

  hbox=gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 2);

  my_snprintf(buf, sizeof(buf), _("Ruler: %s %s"), 
	      get_ruler_title(p->government, p->is_male, p->nation), p->name);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);
  
  my_snprintf(buf, sizeof(buf), _("Government: %s"),
	      get_government_name(p->government));
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

  hbox=gtk_hbox_new(FALSE,5);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 2);

  my_snprintf(buf, sizeof(buf), _("Gold: %d"), p->economic.gold);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

  my_snprintf(buf, sizeof(buf), _("Tax: %d%%"), p->economic.tax);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

  my_snprintf(buf, sizeof(buf), _("Science: %d%%"), p->economic.science);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

  my_snprintf(buf, sizeof(buf), _("Luxury: %d%%"), p->economic.luxury);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

  hbox=gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 2);

  my_snprintf(buf, sizeof(buf), _("Researching: %s(%d/%d)"),
	      get_tech_name(p, p->research.researching),
	      p->research.bulbs_researched, total_bulbs_required(p));

  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 2);

  pcity = find_palace(p);
  my_snprintf(buf, sizeof(buf), _("Capital: %s"),
	      (!pcity)?_("(Unknown)"):pcity->name);
  label=gtk_label_new(buf);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 2);

  store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);

  for(i=A_FIRST; i<game.num_tech_types; i++)
    if(get_invention(p, i)==TECH_KNOWN) {
      GtkTreeIter it;
      GValue v = { 0, };

      gtk_list_store_append(store, &it);

      g_value_init(&v, G_TYPE_STRING);
      g_value_set_static_string(&v, advances[i].name);
      gtk_list_store_set_value(store, &it, 1, &v);
      g_value_unset(&v);

      gtk_list_store_set(store, &it,
        0, (get_invention(game.player_ptr, i)!=TECH_KNOWN),
        -1);
    }

  view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_toggle_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
    "active", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
    "text", 1, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  sw = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(sw, -1, 300);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(intel_dialog_shell)->vbox),
	sw, TRUE, FALSE, 2);

  gtk_widget_show_all(GTK_DIALOG(intel_dialog_shell)->vbox);
}
