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
#include <string.h>

#include <gtk/gtk.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "log.h"
#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "colors.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "messagewin.h"

static GtkWidget *meswin_dialog_shell;
static GtkListStore *meswin_store;
static GtkWidget *meswin_goto_command;
static GtkWidget *meswin_popcity_command;
static GtkTreeSelection *meswin_selection;

static void create_meswin_dialog(void);
static void meswin_destroy_callback(GtkWidget *w, gpointer data);
static void meswin_command_callback(GtkWidget *w, gint response_id);

static void meswin_selection_callback(GtkTreeSelection *selection,
                                      gpointer data);
static void meswin_row_activated_callback(GtkTreeView *view,
					  GtkTreePath *path,
					  GtkTreeViewColumn *col,
					  gpointer data);
static void meswin_goto_callback(GtkWidget * w, gpointer data);
static void meswin_popcity_callback(GtkWidget * w, gpointer data);

#define N_MSG_VIEW 24	       /* max before scrolling happens */

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_meswin_dialog(void)
{
  int updated = 0;
  
  if(!meswin_dialog_shell) {
    create_meswin_dialog();
    gtk_set_relative_position(toplevel, meswin_dialog_shell, 25, 25);
    updated = 1;	       /* create_ calls update_ */
  }

  gtk_window_present(GTK_WINDOW(meswin_dialog_shell));

  if(!updated) 
    update_meswin_dialog();
}

/****************************************************************
...
*****************************************************************/
bool is_meswin_open(void)
{
  return meswin_dialog_shell != NULL;
}

/****************************************************************
...
*****************************************************************/
static void meswin_visited_item(gint n)
{
  GtkTreeIter it;

  if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(meswin_store),&it,NULL,n)) {
    gtk_list_store_set(meswin_store, &it, 1, (gint)TRUE, -1);
  }
}

/****************************************************************
...
*****************************************************************/
static void meswin_not_visited_item(gint n)
{
  GtkTreeIter it;

  if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(meswin_store),&it,NULL,n)) {
    gtk_list_store_set(meswin_store, &it, 1, (gint)FALSE, -1);
  }
}

/****************************************************************
...
*****************************************************************/
static void meswin_cell_data_func(GtkTreeViewColumn *col,
				  GtkCellRenderer *cell,
				  GtkTreeModel *model, GtkTreeIter *it,
				  gpointer data)
{
  gboolean b;

  gtk_tree_model_get(model, it, 1, &b, -1);

  if (b) {
    g_object_set(G_OBJECT(cell), "style", PANGO_STYLE_ITALIC, NULL);
    g_object_set(G_OBJECT(cell), "foreground", "blue", NULL);
    g_object_set(G_OBJECT(cell), "weight", PANGO_WEIGHT_NORMAL, NULL);
  } else {
    g_object_set(G_OBJECT(cell), "style", PANGO_STYLE_NORMAL, NULL);
    g_object_set(G_OBJECT(cell), "foreground", "black", NULL);
    g_object_set(G_OBJECT(cell), "weight", PANGO_WEIGHT_BOLD, NULL);
  }
}
					     
/****************************************************************
...
*****************************************************************/
static void create_meswin_dialog(void)
{
  static gchar *titles_[1] = { N_("Messages") };
  static gchar **titles;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *col;
  GtkWidget *view, *sw;

  if (!titles)
    titles = intl_slist(1, titles_);

  meswin_dialog_shell = gtk_dialog_new_with_buttons(_("Messages"),
  	NULL,
	0,
	GTK_STOCK_CLOSE,
	GTK_RESPONSE_CLOSE,
	NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(meswin_dialog_shell),
	GTK_RESPONSE_CLOSE);
  gtk_window_set_default_size(GTK_WINDOW(meswin_dialog_shell), 400, 250);

  meswin_goto_command = gtk_stockbutton_new(GTK_STOCK_JUMP_TO,
	_("_Goto location"));
  gtk_widget_set_sensitive(meswin_goto_command, FALSE);
  gtk_dialog_add_action_widget(GTK_DIALOG(meswin_dialog_shell),
			       meswin_goto_command, 1);

  meswin_popcity_command = gtk_stockbutton_new(GTK_STOCK_ZOOM_IN,
	_("_Popup City"));
  gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
  gtk_dialog_add_action_widget(GTK_DIALOG(meswin_dialog_shell),
			       meswin_popcity_command, 2);

  gtk_signal_connect(GTK_OBJECT(meswin_goto_command), "clicked",
		GTK_SIGNAL_FUNC(meswin_goto_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(meswin_popcity_command), "clicked",
		GTK_SIGNAL_FUNC(meswin_popcity_callback), NULL);

  meswin_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_BOOLEAN);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                          GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->vbox),
		     sw, TRUE, TRUE, 0);

  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(meswin_store));
  meswin_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  g_object_unref(meswin_store);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));

  renderer = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(titles[0], renderer,
  	"text", 0, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
  gtk_tree_view_column_set_cell_data_func(col, renderer,
	meswin_cell_data_func, NULL, NULL);
  gtk_container_add(GTK_CONTAINER(sw), view);

  g_signal_connect(meswin_selection, "changed",
		   G_CALLBACK(meswin_selection_callback), NULL);
  g_signal_connect(view, "row_activated",
		   G_CALLBACK(meswin_row_activated_callback), NULL);
  g_signal_connect(meswin_dialog_shell, "response",
		   G_CALLBACK(meswin_command_callback), NULL);
  g_signal_connect(meswin_dialog_shell, "destroy",
		   G_CALLBACK(meswin_destroy_callback), NULL);

  gtk_widget_show_all(GTK_DIALOG(meswin_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(meswin_dialog_shell)->action_area);

  real_update_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/

static int messages_total = 0; /* current total number of message lines */
static int messages_alloc = 0; /* number allocated for */
static char **string_ptrs = NULL;
static int *xpos = NULL;
static int *ypos = NULL;
static int *event = NULL;

/**************************************************************************
 This makes sure that the next two elements in string_ptrs etc are
 allocated for.  Two = one to be able to grow, and one for the sentinel
 in string_ptrs.
 Note update_meswin_dialog should always be called soon after this since
 it contains pointers to the memory we're reallocing here.
**************************************************************************/
static void meswin_allocate(void)
{
  int i;
  
  if (messages_total+2 > messages_alloc) {
    messages_alloc = messages_total + 32;
    string_ptrs = fc_realloc(string_ptrs, messages_alloc*sizeof(char*));
    xpos = fc_realloc(xpos, messages_alloc*sizeof(int));
    ypos = fc_realloc(ypos, messages_alloc*sizeof(int));
    event = fc_realloc(event, messages_alloc*sizeof(int));
    for( i=messages_total; i<messages_alloc; i++ ) {
      string_ptrs[i] = NULL;
      xpos[i] = 0;
      ypos[i] = 0;
      event[i] = E_NOEVENT;
    }
  }
}

/**************************************************************************
...
**************************************************************************/
void real_clear_notify_window(void)
{
  int i;
  meswin_allocate();
  for (i = 0; i <messages_total; i++) {
    free(string_ptrs[i]);
    string_ptrs[i] = NULL;
    xpos[i] = 0;
    ypos[i] = 0;
    event[i] = E_NOEVENT;
  }
  string_ptrs[0] = NULL;
  messages_total = 0;

  if(meswin_dialog_shell) {
    gtk_widget_set_sensitive(meswin_goto_command, FALSE);
    gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void real_add_notify_window(struct packet_generic_message *packet)
{
  char *s;
  int nspc;
  char *game_prefix1 = "Game: ";
  char *game_prefix2 = _("Game: ");
  int gp_len1 = strlen(game_prefix1);
  int gp_len2 = strlen(game_prefix2);

  meswin_allocate();
  s = fc_malloc(strlen(packet->message) + 50);
  if (strncmp(packet->message, game_prefix1, gp_len1) == 0) {
    strcpy(s, packet->message + gp_len1);
  } else if(strncmp(packet->message, game_prefix2, gp_len2) == 0) {
    strcpy(s, packet->message + gp_len2);
  } else {
    strcpy(s, packet->message);
  }

  nspc=50-strlen(s);
  if(nspc>0)
    strncat(s, "                                                  ", nspc);
  
  xpos[messages_total] = packet->x;
  ypos[messages_total] = packet->y;
  event[messages_total]= packet->event;
  string_ptrs[messages_total] = s;
  messages_total++;
  string_ptrs[messages_total] = NULL;
}

/**************************************************************************
...
**************************************************************************/
void real_update_meswin_dialog(void)
{
  int i;
  GtkTreeIter it;

  gtk_list_store_clear(meswin_store);

  for (i = 0; i < messages_total; i++) {
    GValue value = { 0, };

    gtk_list_store_append(meswin_store, &it);

    g_value_init(&value, G_TYPE_STRING);
    g_value_set_static_string(&value, string_ptrs[i]);
    gtk_list_store_set_value(meswin_store, &it, 0, &value);
    g_value_unset(&value);

    meswin_not_visited_item(i);
  }
}

/**************************************************************************
...
**************************************************************************/
static void meswin_selection_callback(GtkTreeSelection *selection,
                                      gpointer data)
{
  gint row;

  if ((row = gtk_tree_selection_get_row(selection)) != -1) {
    struct city *pcity;
    int x, y;
    bool location_ok, city_ok;

    x = xpos[row];
    y = ypos[row];
    location_ok = (x != -1 && y != -1);
    city_ok = (location_ok && (pcity = map_get_city(x, y))
	       && (pcity->owner == game.player_idx));

    gtk_widget_set_sensitive(meswin_goto_command, location_ok);
    gtk_widget_set_sensitive(meswin_popcity_command, city_ok);
  }
}

/**************************************************************************
...
**************************************************************************/
static void meswin_row_activated_callback(GtkTreeView *view,
					  GtkTreePath *path,
					  GtkTreeViewColumn *col,
					  gpointer data)
{
  struct city *pcity;
  int x, y;
  bool location_ok, city_ok;
  gint row;

  row = gtk_tree_path_get_indices(path)[0];

  x = xpos[row];
  y = ypos[row];
  location_ok = (x != -1 && y != -1);
  city_ok = (location_ok && (pcity = map_get_city(x, y))
	     && (pcity->owner == game.player_idx));

  if (city_ok && is_city_event(event[row])) {
    meswin_popcity_callback(meswin_popcity_command, NULL);
  } else if (location_ok) {
    meswin_goto_callback(meswin_goto_command, NULL);
  }

  meswin_visited_item(row);

  gtk_widget_set_sensitive(meswin_goto_command, location_ok);
  gtk_widget_set_sensitive(meswin_popcity_command, city_ok);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_destroy_callback(GtkWidget *w, gpointer data)
{
  meswin_dialog_shell = NULL;
}

/**************************************************************************
...
**************************************************************************/
static void meswin_command_callback(GtkWidget *w, gint response_id)
{
  if (response_id <= 0)
    gtk_widget_destroy(w);
}

/**************************************************************************
...
**************************************************************************/
void meswin_goto_callback(GtkWidget *w, gpointer data)
{
  gint row;

  if((row = gtk_tree_selection_get_row(meswin_selection)) == -1)
    return;

  if(xpos[row]!=-1 && ypos[row]!=-1)
    center_tile_mapcanvas(xpos[row], ypos[row]);
  meswin_visited_item(row);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_popcity_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity;
  int x, y;
  gint row;

  if((row = gtk_tree_selection_get_row(meswin_selection)) == -1)
    return;

  x = xpos[row];
  y = ypos[row];
  if((x!=-1 && y!=-1) && (pcity=map_get_city(x,y))
     && (pcity->owner == game.player_idx)) {
      if (center_when_popup_city) {
       center_tile_mapcanvas(x,y);
      }
    popup_city_dialog(pcity, 0);
  }
  meswin_visited_item(row);
}
