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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

/* common */
#include "fc_types.h" /* LINE_BREAK */
#include "game.h"
#include "government.h"
#include "packets.h"
#include "research.h"
#include "unitlist.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "mapview_common.h"
#include "options.h"
#include "packhand_gen.h"
#include "control.h"
#include "reqtree.h"
#include "text.h"

/* client/gui-gtk-2.0 */
#include "canvas.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "plrdlg.h"

#include "repodlgs.h"


/******************************************************************/

static void create_science_dialog(bool make_modal);
static void science_change_callback(GtkWidget * widget, gpointer data);
static void science_goal_callback(GtkWidget * widget, gpointer data);
/******************************************************************/
static struct gui_dialog *science_dialog_shell = NULL;
static GtkWidget *science_label;
static GtkWidget *science_current_label, *science_goal_label;
static GtkWidget *science_change_menu_button, *science_goal_menu_button;
static GtkWidget *science_help_toggle;
static GtkWidget *science_drawing_area;
static int science_dialog_shell_is_modal;
static GtkWidget *popupmenu, *goalmenu;

/******************************************************************/
static void create_endgame_report(struct packet_endgame_report *packet);

static struct gui_dialog *endgame_report_shell = NULL;

/******************************************************************
...
*******************************************************************/
void update_report_dialogs(void)
{
  if(is_report_dialogs_frozen()) return;
  city_report_dialog_update(); 
  science_dialog_update();
}


/****************************************************************
...
*****************************************************************/
void popup_science_dialog(bool raise)
{
  if(!science_dialog_shell) {
    science_dialog_shell_is_modal = FALSE;
    
    create_science_dialog(FALSE);
  }

  if (can_client_issue_orders()
      && A_UNSET == player_research_get(client.conn.playing)->tech_goal
      && A_UNSET == player_research_get(client.conn.playing)->researching) {
    gui_dialog_alert(science_dialog_shell);
  } else {
    gui_dialog_present(science_dialog_shell);
  }

  if (raise) {
    gui_dialog_raise(science_dialog_shell);
  }
}


/****************************************************************
 Closes the science dialog.
*****************************************************************/
void popdown_science_dialog(void)
{
  if (science_dialog_shell) {
    gui_dialog_destroy(science_dialog_shell);
  }
}

/****************************************************************************
 Change tech goal, research or open help dialog
****************************************************************************/
static void button_release_event_callback(GtkWidget *widget,
					  GdkEventButton *event,
                                          gpointer *data)
{
  struct reqtree *tree = g_object_get_data(G_OBJECT(widget), "reqtree");
  int x = event->x, y = event->y;
  Tech_type_id tech = get_tech_on_reqtree(tree, x, y);

  if (tech == A_NONE) {
    return;
  }

  if (event->button == 3) {
    /* RMB: get help */
    /* FIXME: this should work for ctrl+LMB or shift+LMB (?) too */
    popup_help_dialog_typed(advance_name_for_player(client.conn.playing, tech), HELP_TECH);
  } else {
    if (event->button == 1 && can_client_issue_orders()) {
      /* LMB: set research or research goal */
      switch (player_invention_state(client.conn.playing, tech)) {
       case TECH_PREREQS_KNOWN:
         dsend_packet_player_research(&client.conn, tech);
         break;
       case TECH_UNKNOWN:
         dsend_packet_player_tech_goal(&client.conn, tech);
         break;
       case TECH_KNOWN:
         break;
      }
    }
  } 
}

/****************************************************************************
  Draw the invalidated portion of the reqtree.
****************************************************************************/
static void update_science_drawing_area(GtkWidget *widget, gpointer data)
{
  /* FIXME: this currently redraws everything! */
  struct canvas canvas = {.type = CANVAS_PIXMAP,
			  .v.pixmap = GTK_LAYOUT(widget)->bin_window};
  struct reqtree *reqtree = g_object_get_data(G_OBJECT(widget), "reqtree");
  int width, height;

  get_reqtree_dimensions(reqtree, &width, &height);
  draw_reqtree(reqtree, &canvas, 0, 0, 0, 0, width, height);
}

/****************************************************************************
  Return main widget of new technology diagram.
  This is currently GtkScrolledWindow 
****************************************************************************/
static GtkWidget *create_reqtree_diagram(void)
{
  GtkWidget *sw;
  struct reqtree *reqtree;
  GtkAdjustment* adjustment;
  int width, height;
  int x;
  Tech_type_id researching;

  if (can_conn_edit(&client.conn)) {
    /* Show all techs in editor mode, not only currently reachable ones */
    reqtree = create_reqtree(NULL);
  } else {
    /* Show only at some point reachable techs */
    reqtree = create_reqtree(client.conn.playing);
  }

  get_reqtree_dimensions(reqtree, &width, &height);

  sw = gtk_scrolled_window_new(NULL, NULL);
  science_drawing_area = gtk_layout_new(NULL, NULL);
  g_object_set_data_full(G_OBJECT(science_drawing_area), "reqtree", reqtree,
			 (GDestroyNotify)destroy_reqtree);
  g_signal_connect(G_OBJECT(science_drawing_area), "expose_event",
		   G_CALLBACK(update_science_drawing_area), NULL);
  g_signal_connect(G_OBJECT(science_drawing_area), "button-release-event",
                   G_CALLBACK(button_release_event_callback), NULL);
  gtk_widget_add_events(science_drawing_area,
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                        | GDK_BUTTON2_MOTION_MASK | GDK_BUTTON3_MOTION_MASK);

  gtk_layout_set_size(GTK_LAYOUT(science_drawing_area), width, height);

  gtk_container_add(GTK_CONTAINER(sw), science_drawing_area);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);

  adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw));
  
  /* Center on currently researched node */
  if (NULL != client.conn.playing) {
    researching = player_research_get(client.conn.playing)->researching;
  } else {
    researching = A_UNSET;
  }
  if (find_tech_on_reqtree(reqtree, researching,
			   &x, NULL, NULL, NULL)) {
    /* FIXME: this is just an approximation */
    gtk_adjustment_set_value(adjustment, x - 100);
  }

  return sw;
}

/****************************************************************
...
*****************************************************************/
void create_science_dialog(bool make_modal)
{
  GtkWidget *frame, *table, *w;
  GtkWidget *science_diagram;

  gui_dialog_new(&science_dialog_shell, GTK_NOTEBOOK(top_notebook), NULL);
  /* TRANS: Research report title */
  gui_dialog_set_title(science_dialog_shell, _("Research"));

  gui_dialog_add_button(science_dialog_shell,
      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
  gui_dialog_set_default_response(science_dialog_shell,
      GTK_RESPONSE_CLOSE);

  science_label = gtk_label_new("no text set yet");

  gtk_box_pack_start(GTK_BOX(science_dialog_shell->vbox),
        science_label, FALSE, FALSE, 0);

  frame = gtk_frame_new(_("Researching"));
  gtk_box_pack_start(GTK_BOX(science_dialog_shell->vbox),
        frame, FALSE, FALSE, 0);

  table = gtk_table_new(1, 6, TRUE);
  gtk_table_set_col_spacings(GTK_TABLE(table), 4);
  gtk_container_add(GTK_CONTAINER(frame), table);

  science_change_menu_button = gtk_option_menu_new();
  gtk_table_attach_defaults(GTK_TABLE(table), science_change_menu_button, 0,
                            2, 0, 1);

  popupmenu = gtk_menu_new();
  gtk_widget_show_all(popupmenu);

  science_current_label=gtk_progress_bar_new();
  gtk_table_attach_defaults(GTK_TABLE(table), science_current_label,
                            2, 5, 0, 1);
  gtk_widget_set_size_request(science_current_label, -1, 25);

  science_help_toggle = gtk_check_button_new_with_label (_("Help"));
  gtk_table_attach(GTK_TABLE(table), science_help_toggle,
                   5, 6, 0, 1, 0, 0, 0, 0);

  frame = gtk_frame_new( _("Goal"));
  gtk_box_pack_start(GTK_BOX(science_dialog_shell->vbox),
        frame, FALSE, FALSE, 0);

  table = gtk_table_new(1, 6, TRUE);
  gtk_table_set_col_spacings(GTK_TABLE(table), 4);
  gtk_container_add(GTK_CONTAINER(frame),table);

  science_goal_menu_button = gtk_option_menu_new();
  gtk_table_attach_defaults(GTK_TABLE(table), science_goal_menu_button,
                            0, 2, 0, 1);

  goalmenu = gtk_menu_new();
  gtk_widget_show_all(goalmenu);

  science_goal_label = gtk_label_new("");
  gtk_table_attach_defaults(GTK_TABLE(table), science_goal_label,
                            2, 5, 0, 1);
  gtk_widget_set_size_request(science_goal_label, -1, 25);

  w = gtk_label_new("");
  gtk_table_attach_defaults(GTK_TABLE(table), w, 5, 6, 0, 1);

  science_diagram = create_reqtree_diagram();
  gtk_box_pack_start(GTK_BOX(science_dialog_shell->vbox), science_diagram,
                     TRUE, TRUE, 0);

  gui_dialog_show_all(science_dialog_shell);

  science_dialog_update();
  gtk_widget_grab_focus(science_change_menu_button);
}

/****************************************************************************
  Resize and redraw the requirement tree.
****************************************************************************/
void science_dialog_redraw(void)
{
  struct reqtree *reqtree;
  int width, height;

  if (NULL == science_dialog_shell) {
    /* Not existant. */
    return;
  }

  if (can_conn_edit(&client.conn)) {
    /* Show all techs in editor mode, not only currently reachable ones */
    reqtree = create_reqtree(NULL);
  } else {
    /* Show only at some point reachable techs */
    reqtree = create_reqtree(client_player());
  }

  get_reqtree_dimensions(reqtree, &width, &height);
  gtk_layout_set_size(GTK_LAYOUT(science_drawing_area), width, height);
  g_object_set_data_full(G_OBJECT(science_drawing_area), "reqtree", reqtree,
                         (GDestroyNotify) destroy_reqtree);

  gtk_widget_queue_draw(science_drawing_area);
}

/****************************************************************************
  Called to set several texts in the science dialog.
****************************************************************************/
static void update_science_text(void)
{
  double pct;
  const char *text = get_science_target_text(&pct);

  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(science_current_label), text);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(science_current_label), pct);

  /* work around GTK+ refresh bug. */
  gtk_widget_queue_resize(science_current_label);
}

/****************************************************************
...
*****************************************************************/
void science_change_callback(GtkWidget *widget, gpointer data)
{
  size_t to = (size_t) data;

  if (GTK_TOGGLE_BUTTON(science_help_toggle)->active) {
    popup_help_dialog_typed(advance_name_for_player(client.conn.playing, to), HELP_TECH);
    /* Following is to make the menu go back to the current research;
     * there may be a better way to do this?  --dwp */
    science_dialog_update();
  } else {

    gtk_widget_set_sensitive(science_change_menu_button,
			     can_client_issue_orders());
    update_science_text();
    
    dsend_packet_player_research(&client.conn, to);
  }
}

/****************************************************************
...
*****************************************************************/
void science_goal_callback(GtkWidget *widget, gpointer data)
{
  size_t to = (size_t) data;

  if (GTK_TOGGLE_BUTTON(science_help_toggle)->active) {
    popup_help_dialog_typed(advance_name_for_player(client.conn.playing, to), HELP_TECH);
    /* Following is to make the menu go back to the current goal;
     * there may be a better way to do this?  --dwp */
    science_dialog_update();
  }
  else {  
    gtk_label_set_text(GTK_LABEL(science_goal_label),
		       get_science_goal_text(to));
    dsend_packet_player_tech_goal(&client.conn, to);
  }
}

/****************************************************************
...
*****************************************************************/
static gint cmp_func(gconstpointer a_p, gconstpointer b_p)
{
  const gchar *a_str, *b_str;
  gint a = GPOINTER_TO_INT(a_p), b = GPOINTER_TO_INT(b_p);

  a_str = advance_name_for_player(client.conn.playing, a);
  b_str = advance_name_for_player(client.conn.playing, b);

  return fc_strcoll(a_str, b_str);
}

/****************************************************************
...
*****************************************************************/
void science_dialog_update(void)
{
  if(science_dialog_shell) {
  int i, hist;
  char text[512];
  GtkWidget *item;
  GList *sorting_list = NULL, *it;
  GtkSizeGroup *group1, *group2;
  struct player_research *research = player_research_get(client.conn.playing);

  if (!research || is_report_dialogs_frozen()) {
    return;
  }

  gtk_widget_queue_draw(science_drawing_area);

  gtk_label_set_text(GTK_LABEL(science_label), science_dialog_text());
  
  /* collect all researched techs in sorting_list */
  advance_index_iterate(A_FIRST, i) {
    if (TECH_KNOWN == player_invention_state(client.conn.playing, i)) {
      sorting_list = g_list_prepend(sorting_list, GINT_TO_POINTER(i));
    }
  } advance_index_iterate_end;

  /* sort them, and install them in the list */
  sorting_list = g_list_sort(sorting_list, cmp_func);
  g_list_free(sorting_list);
  sorting_list = NULL;

  gtk_widget_destroy(popupmenu);
  popupmenu = gtk_menu_new();
  gtk_option_menu_set_menu(GTK_OPTION_MENU(science_change_menu_button),
	popupmenu);
  gtk_widget_set_sensitive(science_change_menu_button,
			   can_client_issue_orders());

  update_science_text();

  /* work around GTK+ refresh bug. */
  gtk_widget_queue_resize(science_current_label);
 
  if (research->researching == A_UNSET) {
    item = gtk_menu_item_new_with_label(advance_name_for_player(client.conn.playing,
						      A_NONE));
    gtk_menu_shell_append(GTK_MENU_SHELL(popupmenu), item);
  }

  /* collect all techs which are reachable in the next step
   * hist will hold afterwards the techid of the current choice
   */
  hist=0;
  if (!is_future_tech(research->researching)) {
    advance_index_iterate(A_FIRST, i) {
      if (TECH_PREREQS_KNOWN !=
            player_invention_state(client.conn.playing, i)) {
	continue;
      }

      if (i == research->researching)
	hist=i;
      sorting_list = g_list_prepend(sorting_list, GINT_TO_POINTER(i));
    } advance_index_iterate_end;
  } else {
    int value = (advance_count() + research->future_tech + 1);

    sorting_list = g_list_prepend(sorting_list, GINT_TO_POINTER(value));
  }

  /* sort the list and build from it the menu */
  sorting_list = g_list_sort(sorting_list, cmp_func);
  for (i = 0; i < g_list_length(sorting_list); i++) {
    const gchar *data;

    if (GPOINTER_TO_INT(g_list_nth_data(sorting_list, i)) < advance_count()) {
      data = advance_name_for_player(client.conn.playing,
			GPOINTER_TO_INT(g_list_nth_data(sorting_list, i)));
    } else {
      fc_snprintf(text, sizeof(text), _("Future Tech. %d"),
                  GPOINTER_TO_INT(g_list_nth_data(sorting_list, i))
                  - advance_count());
      data=text;
    }

    item = gtk_menu_item_new_with_label(data);
    gtk_menu_shell_append(GTK_MENU_SHELL(popupmenu), item);
    if (strlen(data) > 0)
      g_signal_connect(item, "activate",
		       G_CALLBACK(science_change_callback),
		       g_list_nth_data(sorting_list, i));
  }

  gtk_widget_show_all(popupmenu);
  gtk_option_menu_set_history(GTK_OPTION_MENU(science_change_menu_button),
	g_list_index(sorting_list, GINT_TO_POINTER(hist)));
  g_list_free(sorting_list);
  sorting_list = NULL;

  gtk_widget_destroy(goalmenu);
  goalmenu = gtk_menu_new();
  gtk_option_menu_set_menu(GTK_OPTION_MENU(science_goal_menu_button),
	goalmenu);
  gtk_widget_set_sensitive(science_goal_menu_button,
			   can_client_issue_orders());
  
  gtk_label_set_text(GTK_LABEL(science_goal_label),
		get_science_goal_text(
		  research->tech_goal));

  if (research->tech_goal == A_UNSET) {
    item = gtk_menu_item_new_with_label(advance_name_for_player(client.conn.playing,
						      A_NONE));
    gtk_menu_shell_append(GTK_MENU_SHELL(goalmenu), item);
  }

  /* collect all techs which are reachable in under 11 steps
   * hist will hold afterwards the techid of the current choice
   */
  hist=0;
  advance_index_iterate(A_FIRST, i) {
    if (player_invention_reachable(client.conn.playing, i)
        && TECH_KNOWN != player_invention_state(client.conn.playing, i)
        && (11 > num_unknown_techs_for_goal(client.conn.playing, i)
	    || i == research->tech_goal)) {
      if (i == research->tech_goal) {
	hist = i;
      }
      sorting_list = g_list_prepend(sorting_list, GINT_TO_POINTER(i));
    }
  } advance_index_iterate_end;

  group1 = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  group2 = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  /* sort the list and build from it the menu */
  sorting_list = g_list_sort(sorting_list, cmp_func);
  for (it = g_list_first(sorting_list); it; it = g_list_next(it)) {
    GtkWidget *hbox, *label;
    char text[512];
    gint tech = GPOINTER_TO_INT(g_list_nth_data(it, 0));

    item = gtk_menu_item_new();
    hbox = gtk_hbox_new(FALSE, 18);
    gtk_container_add(GTK_CONTAINER(item), hbox);

    label = gtk_label_new(advance_name_for_player(client.conn.playing, tech));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_size_group_add_widget(group1, label);

    fc_snprintf(text, sizeof(text), "%d",
                num_unknown_techs_for_goal(client_player(), tech));

    label = gtk_label_new(text);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_size_group_add_widget(group2, label);

    gtk_menu_shell_append(GTK_MENU_SHELL(goalmenu), item);
    g_signal_connect(item, "activate",
		     G_CALLBACK(science_goal_callback),
		     GINT_TO_POINTER(tech));
  }

  gtk_widget_show_all(goalmenu);
  gtk_option_menu_set_history(GTK_OPTION_MENU(science_goal_menu_button),
	g_list_index(sorting_list, GINT_TO_POINTER(hist)));
  g_list_free(sorting_list);
  sorting_list = NULL;
 
  }
}


/****************************************************************************
                      ECONOMY REPORT DIALOG
****************************************************************************/
static struct gui_dialog *economy_report_dialog_shell = NULL;
static GtkListStore *economy_report_store = NULL;
static GtkLabel *economy_report_label = NULL;

enum economy_report_response {
  ERD_RES_SELL_OBSOLETE = 1,
  ERD_RES_SELL_ALL,
  ERD_RES_DISBAND_UNITS
};

/* Those values must match the functions economy_report_store_new() and
 * economy_report_column_name(). */
enum economy_report_columns {
  ERD_COL_SPRITE,
  ERD_COL_NAME,
  ERD_COL_OBSOLETE,
  ERD_COL_COUNT,
  ERD_COL_COST,
  ERD_COL_TOTAL_COST,

  /* Not visible. */
  ERD_COL_BOOL_VISIBLE,
  ERD_COL_CID,

  ERD_COL_NUM
};

/****************************************************************************
  Create a new economy report list store.
****************************************************************************/
static GtkListStore *economy_report_store_new(void)
{
  return gtk_list_store_new(ERD_COL_NUM,
                            GDK_TYPE_PIXBUF,    /* ERD_COL_SPRITE */
                            G_TYPE_STRING,      /* ERD_COL_NAME */
                            G_TYPE_BOOLEAN,     /* ERD_COL_OBSOLETE */
                            G_TYPE_INT,         /* ERD_COL_COUNT */
                            G_TYPE_INT,         /* ERD_COL_COST */
                            G_TYPE_INT,         /* ERD_COL_TOTAL_COST */
                            G_TYPE_BOOLEAN,     /* ERD_COL_BOOL_VISIBLE */
                            G_TYPE_INT,         /* ERD_COL_UNI_KIND */
                            G_TYPE_INT);        /* ERD_COL_UNI_VALUE_ID */
}

/****************************************************************************
  Returns the title of the column (translated).
****************************************************************************/
static const char *
economy_report_column_name(enum economy_report_columns col)
{
  switch (col) {
  case ERD_COL_SPRITE:
    /* TRANS: Image header */
    return _("Type");
  case ERD_COL_NAME:
    return Q_("?Building or Unit type:Name");
  case ERD_COL_OBSOLETE:
    return _("Obsolete");
  case ERD_COL_COUNT:
    return _("Count");
  case ERD_COL_COST:
    return _("Cost");
  case ERD_COL_TOTAL_COST:
    /* TRANS: Upkeep total, count*cost. */
    return _("U Total");
  case ERD_COL_BOOL_VISIBLE:
  case ERD_COL_CID:
  case ERD_COL_NUM:
    break;
  }

  return NULL;
}

/****************************************************************************
  Update the economy report dialog.
****************************************************************************/
static void economy_report_update(GtkListStore *dest_store, GtkLabel *label)
{
  GtkListStore *store = economy_report_store_new();
  GtkTreeIter iter;
  struct improvement_entry building_entries[B_LAST];
  struct unit_entry unit_entries[U_LAST];
  int entries_used, building_total, unit_total, tax, i;

  /* Buildings. */
  get_economy_report_data(building_entries, &entries_used,
                          &building_total, &tax);
  for (i = 0; i < entries_used; i++) {
    struct improvement_entry *pentry = building_entries + i;
    struct impr_type *pimprove = pentry->type;
    struct sprite *sprite = get_building_sprite(tileset, pimprove);

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       ERD_COL_SPRITE, sprite_get_pixbuf(sprite),
                       ERD_COL_NAME, improvement_name_translation(pimprove),
                       ERD_COL_OBSOLETE, client_has_player()
                       && improvement_obsolete(client_player(), pimprove),
                       ERD_COL_COUNT, pentry->count,
                       ERD_COL_COST, pentry->cost,
                       ERD_COL_TOTAL_COST, pentry->total_cost,
                       ERD_COL_BOOL_VISIBLE, TRUE,
                       ERD_COL_CID, cid_encode_building(pimprove),
                       -1);
  }

  /* Units. */
  get_economy_report_units_data(unit_entries, &entries_used, &unit_total);
  for (i = 0; i < entries_used; i++) {
    struct unit_entry *pentry = unit_entries + i;
    struct unit_type *putype = pentry->type;
    struct sprite *sprite = get_unittype_sprite(tileset, putype);

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       ERD_COL_SPRITE, sprite_get_pixbuf(sprite),
                       ERD_COL_NAME, utype_name_translation(putype),
                       ERD_COL_COUNT, pentry->count,
                       ERD_COL_COST, pentry->cost,
                       ERD_COL_TOTAL_COST, pentry->total_cost,
                       ERD_COL_BOOL_VISIBLE, FALSE,
                       ERD_COL_CID, cid_encode_unit(putype),
                       -1);
  }

  /* Merge stores. */
  merge_list_stores(dest_store, store, ERD_COL_CID);
  g_object_unref(G_OBJECT(store));

  /* Update the label. */
  if (NULL != label) {
    char buf[256];

    fc_snprintf(buf, sizeof(buf), _("Income: %d    Total Costs: %d"),
                tax, building_total + unit_total);
    gtk_label_set_text(label, buf);
  }
}

/****************************************************************************
  Issue a command on the economy report.
****************************************************************************/
static void economy_report_command_callback(struct gui_dialog *pdialog,
                                            int response,
                                            gpointer data)
{
  GtkTreeSelection *selection = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkWidget *shell;
  struct universal selected;
  cid cid;
  char buf[256] = "";

  switch (response) {
  case ERD_RES_SELL_OBSOLETE:
  case ERD_RES_SELL_ALL:
  case ERD_RES_DISBAND_UNITS:
    break;
  default:
    gui_dialog_destroy(pdialog);
    return;
  }

  if (!can_client_issue_orders()
      || !gtk_tree_selection_get_selected(selection, &model, &iter)) {
    return;
  }

  gtk_tree_model_get(model, &iter, ERD_COL_CID, &cid, -1);
  selected = cid_decode(cid);

  switch (selected.kind) {
  case VUT_IMPROVEMENT:
    {
      struct impr_type *pimprove = selected.value.building;

      if (can_sell_building(pimprove)
          && (ERD_RES_SELL_ALL == response
              || (ERD_RES_SELL_OBSOLETE == response
                  && improvement_obsolete(client_player(), pimprove)))) {
        shell = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL
                                       | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_QUESTION,
                                       GTK_BUTTONS_YES_NO,
                                       _("Do you really wish to sell "
                                         "your %s?\n"),
                                       improvement_name_translation(pimprove));
        setup_dialog(shell, gui_dialog_get_toplevel(pdialog));
        gtk_window_set_title(GTK_WINDOW(shell), _("Sell Improvements"));

        if (GTK_RESPONSE_YES == gtk_dialog_run(GTK_DIALOG(shell))) {
          sell_all_improvements(pimprove, ERD_RES_SELL_OBSOLETE == response,
                                buf, sizeof(buf));
        }
        gtk_widget_destroy(shell);
      }
    }
    break;
  case VUT_UTYPE:
    {
      if (ERD_RES_DISBAND_UNITS == response) {
        struct unit_type *putype = selected.value.utype;

        shell = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL
                                       | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_QUESTION,
                                       GTK_BUTTONS_YES_NO,
                                       _("Do you really wish to disband "
                                         "your %s?\n"),
                                       utype_name_translation(putype));
        setup_dialog(shell, gui_dialog_get_toplevel(pdialog));
        gtk_window_set_title(GTK_WINDOW(shell), _("Disband Units"));

        if (GTK_RESPONSE_YES == gtk_dialog_run(GTK_DIALOG(shell))) {
          disband_all_units(putype, FALSE, buf, sizeof(buf));
        }
        gtk_widget_destroy(shell);
      }
    }
    break;
  default:
    log_error("Not supported type: %d.", selected.kind);
  }

  if ('\0' != buf[0]) {
    shell = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                   "%s", buf);
    setup_dialog(shell, gui_dialog_get_toplevel(pdialog));
    g_signal_connect(shell, "response", G_CALLBACK(gtk_widget_destroy),
                     NULL);
    gtk_window_set_title(GTK_WINDOW(shell), _("Sell-Off: Results"));
    gtk_window_present(GTK_WINDOW(shell));
  }
}

/****************************************************************************
  Called when a building or a unit type is selected in the economy list.
****************************************************************************/
static void economy_report_selection_callback(GtkTreeSelection *selection,
                                              gpointer data)
{
  struct gui_dialog *pdialog = data;
  GtkTreeModel *model;
  GtkTreeIter iter;

  if (can_client_issue_orders()
      && gtk_tree_selection_get_selected(selection, &model, &iter)) {
    struct universal selected;
    cid cid;

    gtk_tree_model_get(model, &iter, ERD_COL_CID, &cid, -1);
    selected = cid_decode(cid);
    switch (selected.kind) {
    case VUT_IMPROVEMENT:
      {
        bool can_sell = can_sell_building(selected.value.building);

        gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_OBSOLETE,
            can_sell && improvement_obsolete(client_player(),
                                             selected.value.building));
        gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_ALL, can_sell);
        gui_dialog_set_response_sensitive(pdialog, ERD_RES_DISBAND_UNITS,
                                          FALSE);
      }
      return;
    case VUT_UTYPE:
      gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_OBSOLETE,
                                        FALSE);
      gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_ALL, FALSE);
      gui_dialog_set_response_sensitive(pdialog, ERD_RES_DISBAND_UNITS,
                                        TRUE);
      return;
    default:
      log_error("Not supported type: %d.", selected.kind);
      break;
    }
  }

  gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_OBSOLETE, FALSE);
  gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_ALL, FALSE);
  gui_dialog_set_response_sensitive(pdialog, ERD_RES_DISBAND_UNITS, FALSE);
}

/****************************************************************************
  Create a new economy report.
****************************************************************************/
static void economy_report_dialog_new(struct gui_dialog **ppdialog,
                                      GtkListStore **pstore,
                                      GtkLabel **plabel)
{
  GtkWidget *view, *sw, *align;
  GtkListStore *store;
  GtkTreeSelection *selection;
  struct gui_dialog *pdialog;
  const char *title;
  enum economy_report_columns i;

  store = economy_report_store_new();
  if (NULL != pstore) {
    *pstore = store;
  }

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_widget_set_name(view, "small_font");
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gui_dialog_new(ppdialog, GTK_NOTEBOOK(top_notebook), selection);
  pdialog = *ppdialog;
  gui_dialog_set_title(pdialog, _("Economy"));

  g_signal_connect(selection, "changed",
                   G_CALLBACK(economy_report_selection_callback), pdialog);

  for (i = 0; (title = economy_report_column_name(i)); i++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    GType type = gtk_tree_model_get_column_type(GTK_TREE_MODEL(store), i);

    if (GDK_TYPE_PIXBUF == type) {
      renderer = gtk_cell_renderer_pixbuf_new();
      col = gtk_tree_view_column_new_with_attributes(title, renderer,
                                                     "pixbuf", i, NULL);
    } else if (G_TYPE_BOOLEAN == type) {
      renderer = gtk_cell_renderer_toggle_new();
      col = gtk_tree_view_column_new_with_attributes(title, renderer,
                                                     "active", i, "visible",
                                                     ERD_COL_BOOL_VISIBLE,
                                                     NULL);
    } else {
      renderer = gtk_cell_renderer_text_new();
      col = gtk_tree_view_column_new_with_attributes(title, renderer,
                                                     "text", i, NULL);
    }

    if (i > 1) {
      g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
    }

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  }
  gtk_container_add(GTK_CONTAINER(sw), view);

  align = gtk_alignment_new(0.5, 0.0, 0.0, 1.0);
  gtk_box_pack_start(GTK_BOX(pdialog->vbox), align, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(align), sw);

  if (NULL != plabel) {
    *plabel = GTK_LABEL(gtk_label_new(NULL));
    gtk_box_pack_start(GTK_BOX(pdialog->vbox), GTK_WIDGET(*plabel),
                       FALSE, FALSE, 0);
    gtk_misc_set_padding(GTK_MISC(*plabel), 5, 5);
  }

  gui_dialog_add_button(pdialog, _("Sell _Obsolete"), ERD_RES_SELL_OBSOLETE);
  gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_OBSOLETE, FALSE);

  gui_dialog_add_button(pdialog, _("Sell _All"), ERD_RES_SELL_ALL);
  gui_dialog_set_response_sensitive(pdialog, ERD_RES_SELL_ALL, FALSE);

  gui_dialog_add_button(pdialog, _("_Disband"), ERD_RES_DISBAND_UNITS);
  gui_dialog_set_response_sensitive(pdialog, ERD_RES_DISBAND_UNITS, FALSE);

  gui_dialog_add_button(pdialog, GTK_STOCK_CLOSE,
                        GTK_RESPONSE_CLOSE);

  gui_dialog_set_default_response(pdialog, GTK_RESPONSE_CLOSE);
  gui_dialog_response_set_callback(pdialog, economy_report_command_callback);

  gui_dialog_set_default_size(pdialog, -1, 350);
  gui_dialog_show_all(pdialog);

  economy_report_dialog_update();

  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************************
  Create the economy report if needed.
****************************************************************************/
void economy_report_dialog_popup(bool raise)
{
  if (NULL == economy_report_dialog_shell) {
    economy_report_dialog_new(&economy_report_dialog_shell,
                              &economy_report_store,
                              &economy_report_label);
  }

  gui_dialog_present(economy_report_dialog_shell);
  if (raise) {
    gui_dialog_raise(economy_report_dialog_shell);
  }
}

/****************************************************************************
  Close the economy report dialog.
****************************************************************************/
void economy_report_dialog_popdown(void)
{
  if (NULL != economy_report_dialog_shell) {
    gui_dialog_destroy(economy_report_dialog_shell);
    fc_assert(NULL == economy_report_dialog_shell);
    economy_report_store = NULL;
    economy_report_label = NULL;
  }
}

/****************************************************************************
  Update the economy report dialog.
****************************************************************************/
void real_economy_report_dialog_update(void)
{
  if (NULL != economy_report_store) {
    economy_report_update(economy_report_store, economy_report_label);
  }
}


/****************************************************************************
                           UNITS REPORT DIALOG
****************************************************************************/
static struct gui_dialog *units_report_dialog_shell = NULL;
static GtkListStore *units_report_dialog_store = NULL;

enum units_report_response {
  URD_RES_NEAREST = 1,
  URD_RES_UPGRADE
};

/* Those values must match the functions units_report_store_new() and
 * units_report_column_name(). */
enum units_report_columns {
  URD_COL_UTYPE_NAME,
  URD_COL_UPGRADABLE,
  URD_COL_IN_PROJECT,
  URD_COL_ACTIVE,
  URD_COL_SHIELD,
  URD_COL_FOOD,
  URD_COL_GOLD,

  /* Not visible. */
  URD_COL_TEXT_WEIGHT,
  URD_COL_BOOL_VISIBLE,
  URD_COL_UTYPE_ID,

  URD_COL_NUM
};

/****************************************************************************
  Create a new units report list store.
****************************************************************************/
static GtkListStore *units_report_store_new(void)
{
  return gtk_list_store_new(URD_COL_NUM,
                            G_TYPE_STRING,      /* URD_COL_UTYPE_NAME */
                            G_TYPE_BOOLEAN,     /* URD_COL_UPGRADABLE */
                            G_TYPE_INT,         /* URD_COL_IN_PROJECT */
                            G_TYPE_INT,         /* URD_COL_ACTIVE */
                            G_TYPE_INT,         /* URD_COL_SHIELD */
                            G_TYPE_INT,         /* URD_COL_FOOD */
                            G_TYPE_INT,         /* URD_COL_GOLD */
                            G_TYPE_INT,         /* URD_COL_TEXT_WEIGHT */
                            G_TYPE_BOOLEAN,     /* URD_COL_BOOL_VISIBLE */
                            G_TYPE_INT);        /* URD_COL_UTYPE_ID */
}

/****************************************************************************
  Returns the title of the column (translated).
****************************************************************************/
static const char *units_report_column_name(enum units_report_columns col)
{
  switch (col) {
  case URD_COL_UTYPE_NAME:
    return _("Unit Type");
  case URD_COL_UPGRADABLE:
    return Q_("?Upgradable unit [short]:U");
  case URD_COL_IN_PROJECT:
    /* TRANS: "In project" abbreviation. */
    return _("In-Prog");
  case URD_COL_ACTIVE:
    return _("Active");
  case URD_COL_SHIELD:
    return _("Shield");
  case URD_COL_FOOD:
    return _("Food");
  case URD_COL_GOLD:
    return _("Gold");
  case URD_COL_TEXT_WEIGHT:
  case URD_COL_BOOL_VISIBLE:
  case URD_COL_UTYPE_ID:
  case URD_COL_NUM:
    break;
  }

  return NULL;
}

/****************************************************************************
  Update the units report store.
****************************************************************************/
static void units_report_store_update(GtkListStore *dest_store)
{
  struct urd_info {
    int active_count;
    int building_count;
    int upkeep[O_LAST];
  };

  struct urd_info unit_array[U_LAST];
  struct urd_info unit_totals;
  struct urd_info *info;
  GtkListStore *store = units_report_store_new();
  GtkTreeIter iter;

  memset(unit_array, '\0', sizeof(unit_array));
  memset(&unit_totals, '\0', sizeof(unit_totals));

  /* Count units. */
  players_iterate(pplayer) {
    if (client_has_player() && pplayer != client_player()) {
      continue;
    }

    unit_list_iterate(pplayer->units, punit) {
      info = unit_array + utype_index(unit_type(punit));

      if (0 != punit->homecity) {
        output_type_iterate(o) {
          info->upkeep[o] += punit->upkeep[o];
        } output_type_iterate_end;
      }
      info->active_count++;
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      if (VUT_UTYPE == pcity->production.kind) {
        info = unit_array + utype_index(pcity->production.value.utype);
        info->building_count++;
      }
    } city_list_iterate_end;
  } players_iterate_end;

  /* Make the store. */
  unit_type_iterate(utype) {
    info = unit_array + utype_index(utype);

    if (0 == info->active_count && 0 == info->building_count) {
      continue;         /* We don't need a row for this type. */
    }

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       URD_COL_UTYPE_NAME, utype_name_translation(utype),
                       URD_COL_UPGRADABLE, (client_has_player()
                           && NULL != can_upgrade_unittype(client_player(),
                                                           utype)),
                       URD_COL_IN_PROJECT, info->building_count,
                       URD_COL_ACTIVE, info->active_count,
                       URD_COL_SHIELD, info->upkeep[O_SHIELD],
                       URD_COL_FOOD, info->upkeep[O_FOOD],
                       URD_COL_GOLD, info->upkeep[O_GOLD],
                       URD_COL_TEXT_WEIGHT, PANGO_WEIGHT_NORMAL,
                       URD_COL_BOOL_VISIBLE, TRUE,
                       URD_COL_UTYPE_ID, (info->active_count
                                          ? utype_number(utype) : U_LAST),
                       -1);

    /* Update totals. */
    unit_totals.active_count += info->active_count;
    output_type_iterate(o) {
      unit_totals.upkeep[o] += info->upkeep[o];
    } output_type_iterate_end;
    unit_totals.building_count += info->building_count;
  } unit_type_iterate_end;

  /* Add the total row. */
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter,
                     URD_COL_UTYPE_NAME, _("Totals:"),
                     URD_COL_UPGRADABLE, FALSE,
                     URD_COL_IN_PROJECT, unit_totals.building_count,
                     URD_COL_ACTIVE, unit_totals.active_count,
                     URD_COL_SHIELD, unit_totals.upkeep[O_SHIELD],
                     URD_COL_FOOD, unit_totals.upkeep[O_FOOD],
                     URD_COL_GOLD, unit_totals.upkeep[O_GOLD],
                     URD_COL_TEXT_WEIGHT, PANGO_WEIGHT_BOLD,
                     URD_COL_BOOL_VISIBLE, FALSE,
                     URD_COL_UTYPE_ID, U_LAST,
                     -1);

  merge_list_stores(dest_store, store, URD_COL_UTYPE_ID);
  g_object_unref(G_OBJECT(store));
}

/****************************************************************************
  GtkTreeSelection "changed" signal handler.
****************************************************************************/
static void units_report_selection_callback(GtkTreeSelection *selection,
                                            gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter it;
  struct gui_dialog *pdialog = data;
  struct unit_type *utype = NULL;

  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    int ut;

    gtk_tree_model_get(model, &it, URD_COL_UTYPE_ID, &ut, -1);
    utype = utype_by_number(ut);
  }

  if (NULL == utype) {
    gui_dialog_set_response_sensitive(pdialog, URD_RES_NEAREST, FALSE);
    gui_dialog_set_response_sensitive(pdialog, URD_RES_UPGRADE, FALSE);
  } else {
    gui_dialog_set_response_sensitive(pdialog, URD_RES_NEAREST, TRUE);
    gui_dialog_set_response_sensitive(pdialog, URD_RES_UPGRADE,
        (can_client_issue_orders()
         && NULL != can_upgrade_unittype(client_player(), utype)));
  }
}

/****************************************************************************
  Returns the nearest unit of the type 'utype'.
****************************************************************************/
static struct unit *find_nearest_unit(const struct unit_type *utype,
                                      struct tile *ptile)
{
  struct unit *best_candidate = NULL;
  int best_dist = FC_INFINITY, dist;

  players_iterate(pplayer) {
    if (client_has_player() && pplayer != client_player()) {
      continue;
    }

    unit_list_iterate(pplayer->units, punit) {
      if (utype == unit_type(punit)
          && FOCUS_AVAIL == punit->client.focus_status
          && 0 < punit->moves_left
          && !punit->done_moving
          && !punit->ai_controlled) {
        dist = sq_map_distance(unit_tile(punit), ptile);
        if (dist < best_dist) {
          best_candidate = punit;
          best_dist = dist;
        }
      }
    } unit_list_iterate_end;
  } players_iterate_end;

  return best_candidate;
}

/****************************************************************************
  Gui dialog handler.
****************************************************************************/
static void units_report_command_callback(struct gui_dialog *pdialog,
                                          int response,
                                          gpointer data)
{
  struct unit_type *utype = NULL;
  GtkTreeSelection *selection = data;
  GtkTreeModel *model;
  GtkTreeIter it;

  switch (response) {
  case URD_RES_NEAREST:
  case URD_RES_UPGRADE:
    break;
  default:
    gui_dialog_destroy(pdialog);
    return;
  }

  /* Nearest & upgrade commands. */
  if (gtk_tree_selection_get_selected(selection, &model, &it)) {
    int ut;

    gtk_tree_model_get(model, &it, URD_COL_UTYPE_ID, &ut, -1);
    utype = utype_by_number(ut);
  }

  if (response == URD_RES_NEAREST) {
    struct tile *ptile;
    struct unit *punit;

    ptile = get_center_tile_mapcanvas();
    if ((punit = find_nearest_unit(utype, ptile))) {
      center_tile_mapcanvas(unit_tile(punit));

      if (ACTIVITY_IDLE == punit->activity
          || ACTIVITY_SENTRY == punit->activity) {
        if (can_unit_do_activity(punit, ACTIVITY_IDLE)) {
          set_unit_focus_and_select(punit);
        }
      }
    }
  } else if (can_client_issue_orders()) {
    GtkWidget *shell;
    struct unit_type *upgrade = can_upgrade_unittype(client_player(), utype);

    shell = gtk_message_dialog_new(NULL,
                                   GTK_DIALOG_MODAL
                                   | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   _("Upgrade as many %s to %s as possible "
                                     "for %d gold each?\nTreasury contains "
                                     "%d gold."),
                                   utype_name_translation(utype),
                                   utype_name_translation(upgrade),
                                   unit_upgrade_price(client_player(),
                                                      utype, upgrade),
                                   client_player()->economic.gold);
    setup_dialog(shell, gui_dialog_get_toplevel(pdialog));

    gtk_window_set_title(GTK_WINDOW(shell), _("Upgrade Obsolete Units"));

    if (GTK_RESPONSE_YES == gtk_dialog_run(GTK_DIALOG(shell))) {
      dsend_packet_unit_type_upgrade(&client.conn, utype_number(utype));
    }

    gtk_widget_destroy(shell);
  }
}

/****************************************************************************
  Create a units report dialog.
****************************************************************************/
static void units_report_dialog_new(struct gui_dialog **ppdialog,
                                    GtkListStore **pstore)
{
  GtkWidget *view, *sw, *align;
  GtkListStore *store;
  GtkTreeSelection *selection;
  struct gui_dialog *pdialog;
  const char *title;
  enum units_report_columns i;

  store = units_report_store_new();
  if (NULL != pstore) {
    *pstore = store;
  }

  sw = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_widget_set_name(view, "small_font");
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gui_dialog_new(ppdialog, GTK_NOTEBOOK(top_notebook), selection);
  pdialog = *ppdialog;
  gui_dialog_set_title(pdialog, _("Units"));

  g_signal_connect(selection, "changed",
                   G_CALLBACK(units_report_selection_callback), pdialog);

  for (i = 0; (title = units_report_column_name(i)); i++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    if (G_TYPE_BOOLEAN
        == gtk_tree_model_get_column_type(GTK_TREE_MODEL(store), i)) {
      renderer = gtk_cell_renderer_toggle_new();
      col = gtk_tree_view_column_new_with_attributes(title, renderer,
                                                     "active", i, "visible",
                                                     URD_COL_BOOL_VISIBLE,
                                                     NULL);
    } else {
      renderer = gtk_cell_renderer_text_new();
      col = gtk_tree_view_column_new_with_attributes(title, renderer,
                                                     "text", i, "weight",
                                                     URD_COL_TEXT_WEIGHT,
                                                     NULL);
    }

    if (i > 0) {
      g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
      gtk_tree_view_column_set_alignment(col, 1.0);
    }

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  }
  gtk_container_add(GTK_CONTAINER(sw), view);

  align = gtk_alignment_new(0.5, 0.0, 0.0, 1.0);
  gtk_box_pack_start(GTK_BOX(pdialog->vbox), align, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(align), sw);

  gui_dialog_add_stockbutton(pdialog, GTK_STOCK_FIND,
                             _("Find _Nearest"), URD_RES_NEAREST);
  gui_dialog_set_response_sensitive(pdialog, URD_RES_NEAREST, FALSE);

  gui_dialog_add_button(pdialog, _("_Upgrade"), URD_RES_UPGRADE);
  gui_dialog_set_response_sensitive(pdialog, URD_RES_UPGRADE, FALSE);

  gui_dialog_add_button(pdialog, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

  gui_dialog_set_default_response(pdialog, GTK_RESPONSE_CLOSE);
  gui_dialog_response_set_callback(pdialog, units_report_command_callback);

  gui_dialog_set_default_size(pdialog, -1, 350);
  gui_dialog_show_all(pdialog);

  units_report_store_update(store);
  gtk_tree_view_focus(GTK_TREE_VIEW(view));
}

/****************************************************************************
  Create the units report if needed.
****************************************************************************/
void units_report_dialog_popup(bool raise)
{
  if (NULL == units_report_dialog_shell) {
    units_report_dialog_new(&units_report_dialog_shell,
                            &units_report_dialog_store);
  }

  gui_dialog_present(units_report_dialog_shell);
  if (raise) {
    gui_dialog_raise(units_report_dialog_shell);
  }
}

/****************************************************************************
  Closes the units report dialog.
****************************************************************************/
void units_report_dialog_popdown(void)
{
  if (units_report_dialog_shell) {
    gui_dialog_destroy(units_report_dialog_shell);
    fc_assert(NULL == units_report_dialog_shell);
    units_report_dialog_store = NULL;
  }
}

/****************************************************************************
  Update the units report dialog.
****************************************************************************/
void real_units_report_dialog_update(void)
{
  if (NULL != units_report_dialog_store) {
    units_report_store_update(units_report_dialog_store);
  }
}


/****************************************************************

                      FINAL REPORT DIALOG
 
****************************************************************/

/****************************************************************************
  Prepare the Final Report dialog, and fill it with 
  statistics for each player.
****************************************************************************/
static void create_endgame_report(struct packet_endgame_report *packet)
{
  enum { COL_PLAYER, COL_NATION, COL_SCORE, COL_LAST };
  const char *col_names[COL_LAST] = {
    N_("Player\n"),
    N_("Nation\n"),
    N_("Score\n")
  };
  const size_t col_num = packet->category_num + COL_LAST;
  GType col_types[col_num];
  GtkListStore *store;
  GtkWidget *sw, *view;
  GtkTreeIter it;
  int i, j;

  col_types[COL_PLAYER] = G_TYPE_STRING;
  col_types[COL_NATION] = GDK_TYPE_PIXBUF;
  col_types[COL_SCORE] = G_TYPE_INT;
  for (i = COL_LAST; i < col_num; i++) {
    col_types[i] = G_TYPE_INT;
  }

  gui_dialog_new(&endgame_report_shell, GTK_NOTEBOOK(top_notebook), NULL);
  gui_dialog_set_title(endgame_report_shell, _("Score"));
  gui_dialog_add_button(endgame_report_shell, GTK_STOCK_CLOSE,
                        GTK_RESPONSE_CLOSE);

  gui_dialog_set_default_size(endgame_report_shell, 700, 420);

  store = gtk_list_store_newv(col_num, col_types);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);
  gtk_widget_set_name(view, "small_font");

  for (i = 0; i < col_num; i++) {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;
    const char *title;
    const char *attribute;

    if (GDK_TYPE_PIXBUF == col_types[i]) {
      renderer = gtk_cell_renderer_pixbuf_new();
      attribute = "pixbuf";
    } else {
      renderer = gtk_cell_renderer_text_new();
      attribute = "text";
    }

    if (i < COL_LAST) {
      title = col_names[i];
    } else {
      title = packet->category_name[i - COL_LAST];
    }

    col = gtk_tree_view_column_new_with_attributes(Q_(title), renderer,
                                                   attribute, i, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    if (GDK_TYPE_PIXBUF != col_types[i]) {
      gtk_tree_view_column_set_sort_column_id(col, i);
    }
  }

  /* Setup the layout. */
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(sw), view);
  gtk_box_pack_start(GTK_BOX(endgame_report_shell->vbox), sw, TRUE, TRUE, 0);
  gui_dialog_set_default_response(endgame_report_shell, GTK_RESPONSE_CLOSE);
  gui_dialog_show_all(endgame_report_shell);

  /* Insert score statistics into table.  */
  for (i = 0; i < packet->player_num; i++) {
    const struct player *pplayer = player_by_number(packet->player_id[i]);

    gtk_list_store_append(store, &it);
    gtk_list_store_set(store, &it,
                       COL_PLAYER, player_name(pplayer),
                       COL_NATION, get_flag(nation_of_player(pplayer)),
                       COL_SCORE, packet->score[i],
                       -1);
    for (j = 0; j < packet->category_num; j++) {
      gtk_list_store_set(store, &it,
                         j + COL_LAST, packet->category_score[j][i],
                         -1);
    }
  }
}

/**************************************************************************
  Show a dialog with player statistics at endgame.
**************************************************************************/
void popup_endgame_report_dialog(struct packet_endgame_report *packet)
{
  if (!endgame_report_shell){
    create_endgame_report(packet);
  }
  gui_dialog_present(endgame_report_shell);
}
