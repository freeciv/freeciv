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
  GtkWidget *notebook, *label, *sw, *view, *table, *alignment;
  GtkCellRenderer *rend;
  GtkTreeViewColumn *col;

  GtkListStore *store;

  GtkTreeIter diplstates[DS_LAST];
  GtkTreeStore *tree;

  char buf[64];
  struct city *pcity;

  int i;
  static const char *table_text[] = {
    N_("Ruler:"),
    N_("Government:"),
    N_("Capital:"),
    N_("Gold:"),
    NULL,
    N_("Tax:"),
    N_("Science:"),
    N_("Luxury:"),
    NULL,
    N_("Researching:")
  };
  enum table_label {
    LABEL_RULER,
    LABEL_GOVERNMENT,
    LABEL_CAPITAL,
    LABEL_GOLD,
    LABEL_SEP1,
    LABEL_TAX,
    LABEL_SCIENCE,
    LABEL_LUXURY,
    LABEL_SEP2,
    LABEL_RESEARCHING
  };
 
 
  my_snprintf(buf, sizeof(buf),
	      _("Foreign Intelligence: %s Empire"), get_nation_name(p->nation));

  intel_dialog_shell =
    gtk_dialog_new_with_buttons(buf,
      NULL,
      0,
      GTK_STOCK_CLOSE,
      GTK_RESPONSE_CLOSE,
      NULL);
  gtk_window_set_default_size(GTK_WINDOW(intel_dialog_shell), 350, 350);
  setup_dialog(intel_dialog_shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(intel_dialog_shell),
        GTK_RESPONSE_CLOSE);

  g_signal_connect(intel_dialog_shell, "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  g_signal_connect(intel_dialog_shell, "destroy",
                   G_CALLBACK(gtk_widget_destroyed), &intel_dialog_shell);

  notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_BOTTOM);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(intel_dialog_shell)->vbox),
      notebook);
 
  /* overview tab. */
  table = gtk_table_new(ARRAY_SIZE(table_text), 2, FALSE);

  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);

  alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
  gtk_container_set_border_width(GTK_CONTAINER(alignment), 6);
  gtk_container_add(GTK_CONTAINER(alignment), table);

  label = gtk_label_new_with_mnemonic(_("_Overview"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), alignment, label);

  for (i = 0; i < ARRAY_SIZE(table_text); i++) {
    if (table_text[i]) {
      label = gtk_label_new(_(table_text[i]));
      gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
      gtk_table_attach(GTK_TABLE(table), label,
	  0, 1, i, i+1, GTK_FILL, GTK_FILL|GTK_EXPAND, 0, 0);

      switch (i) {
	case LABEL_RULER:
	  my_snprintf(buf, sizeof(buf), "%s %s", 
	      get_ruler_title(p->government, p->is_male, p->nation), p->name);
	  break;
	case LABEL_GOVERNMENT:
	  sz_strlcpy(buf, get_government_name(p->government));
	  break;
	case LABEL_CAPITAL:
	  pcity = find_palace(p);
	  sz_strlcpy(buf, (!pcity) ? _("(Unknown)") : pcity->name);
	  break;
	case LABEL_GOLD:
	  my_snprintf(buf, sizeof(buf), "%d", p->economic.gold);
	  break;
	case LABEL_TAX:
	  my_snprintf(buf, sizeof(buf), "%d%%", p->economic.tax);
	  break;
	case LABEL_SCIENCE:
	  my_snprintf(buf, sizeof(buf), "%d%%", p->economic.science);
	  break;
	case LABEL_LUXURY:
	  my_snprintf(buf, sizeof(buf), "%d%%", p->economic.luxury);
	  break;
	case LABEL_RESEARCHING:
	  if (p->research.researching != A_NOINFO) {
	    my_snprintf(buf, sizeof(buf), "%s(%d/%d)",
		get_tech_name(p, p->research.researching),
		p->research.bulbs_researched, total_bulbs_required(p));
	  } else {
	    my_snprintf(buf, sizeof(buf), _("(Unknown)"));
	  }
	  break;
	default:
	  buf[0] = '\0';
	  break;
      }

      if (buf[0] != '\0') {
	label = gtk_label_new(buf);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(table), label,
	    1, 2, i, i+1, GTK_FILL, GTK_FILL|GTK_EXPAND, 0, 0);
      }
    } else {
      gtk_table_set_row_spacing(GTK_TABLE(table), i, 12);
    }
  }

  /* diplomacy tab. */
  tree = gtk_tree_store_new(1, G_TYPE_STRING);

  for (i = 0; i < ARRAY_SIZE(diplstates); i++) {
    GtkTreeIter it;
    GValue v = { 0, };

    gtk_tree_store_append(tree, &it, NULL);
    g_value_init(&v, G_TYPE_STRING);
    g_value_set_static_string(&v, diplstate_text(i));
    gtk_tree_store_set_value(tree, &it, 0, &v);
    g_value_unset(&v);
    diplstates[i] = it;
  }

  players_iterate(other) {
    const struct player_diplstate *state;
    GtkTreeIter it;
    GValue v = { 0, };

    state = pplayer_get_diplstate(p, other);
    gtk_tree_store_append(tree, &it, &diplstates[state->type]);
    g_value_init(&v, G_TYPE_STRING);
    g_value_set_static_string(&v, other->name);
    gtk_tree_store_set_value(tree, &it, 0, &v);
    g_value_unset(&v);
  } players_iterate_end;

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree));
  g_object_unref(tree);
  gtk_container_set_border_width(GTK_CONTAINER(view), 6);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

  rend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(NULL, rend,
    "text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

  sw = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(sw), view);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  alignment = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_container_set_border_width(GTK_CONTAINER(alignment), 6);
  gtk_container_add(GTK_CONTAINER(alignment), sw);

  label = gtk_label_new_with_mnemonic(_("_Diplomacy"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), alignment, label);

  /* techs tab. */
  store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);

  for(i=A_FIRST; i<game.num_tech_types; i++)
    if(get_invention(p, i)==TECH_KNOWN) {
      GtkTreeIter it;

      gtk_list_store_append(store, &it);

      gtk_list_store_set(store, &it,
        0, (get_invention(game.player_ptr, i)!=TECH_KNOWN),
	1, advances[i].name,
        -1);
    }

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_container_set_border_width(GTK_CONTAINER(view), 6);
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

  alignment = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  gtk_container_set_border_width(GTK_CONTAINER(alignment), 6);
  gtk_container_add(GTK_CONTAINER(alignment), sw);

  label = gtk_label_new_with_mnemonic(_("_Techs"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), alignment, label);

  gtk_widget_show_all(GTK_DIALOG(intel_dialog_shell)->vbox);
}

