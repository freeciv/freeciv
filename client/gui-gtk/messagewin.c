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

#include "clinet.h"
#include "options.h"

#include "chatline.h"
#include "citydlg.h"
#include "colors.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "messagewin.h"

static GtkWidget *meswin_dialog_shell;
static GtkWidget *meswin_list;
static GtkWidget *meswin_close_command;
static GtkWidget *meswin_goto_command;
static GtkWidget *meswin_popcity_command;
static GtkStyle *meswin_visited_style;
static GtkStyle *meswin_not_visited_style;

static void create_meswin_dialog(void);
static void meswin_scroll_down(void);
static void meswin_close_callback(GtkWidget * w, gpointer data);
static void meswin_list_callback(GtkWidget * w, gint row, gint column,
				 GdkEvent * ev);
static void meswin_list_ucallback(GtkWidget * w, gint row, gint column);
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
    updated = 1;	       /* create_ calls update_ */
  }

  gtk_set_relative_position(toplevel, meswin_dialog_shell, 25, 25);
  gtk_widget_show(meswin_dialog_shell);
  if(!updated) 
    update_meswin_dialog();
  
  meswin_scroll_down();
}

/****************************************************************
...
*****************************************************************/
bool is_meswin_open(void)
{
  return meswin_dialog_shell != NULL;
}

/**************************************************************************
 Closes the message window dialog.
**************************************************************************/
void popdown_meswin_dialog(void)
{
  if (meswin_dialog_shell) {
    gtk_widget_destroy(meswin_dialog_shell);
    meswin_dialog_shell = NULL;
    gtk_style_unref(meswin_visited_style);
    gtk_style_unref(meswin_not_visited_style);
  }
}

/****************************************************************
...
*****************************************************************/
static void meswin_visited_item (gint n)
{
  gtk_clist_set_row_style (GTK_CLIST (meswin_list), n,
						meswin_visited_style);
  set_message_visited_state(n, TRUE);
}

/****************************************************************
...
*****************************************************************/
static void meswin_not_visited_item (gint n)
{
  gtk_clist_set_row_style (GTK_CLIST (meswin_list), n,
						meswin_not_visited_style);
  set_message_visited_state(n, FALSE);
}

/****************************************************************
...
*****************************************************************/
void create_meswin_dialog(void)
{
  static const char *titles_[1] = { N_("Messages") };
  static gchar **titles;
  GtkWidget *scrolled;
  GtkAccelGroup *accel = gtk_accel_group_new();

  if (!titles) titles = intl_slist(1, titles_);

  meswin_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(meswin_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(meswin_close_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(meswin_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(meswin_dialog_shell), _("Messages"));
  gtk_window_set_default_size(GTK_WINDOW(meswin_dialog_shell), 400, 250);

  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->vbox),
		     scrolled, TRUE, TRUE, 0);

  meswin_list = gtk_clist_new_with_titles(1, titles);
  gtk_container_add(GTK_CONTAINER(scrolled),meswin_list);
  gtk_clist_column_titles_passive(GTK_CLIST(meswin_list));
  gtk_clist_set_column_auto_resize(GTK_CLIST(meswin_list), 0, TRUE);

  meswin_close_command = gtk_accelbutton_new(_("_Close"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->action_area),
		     meswin_close_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( meswin_close_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( meswin_close_command );

  meswin_goto_command = gtk_accelbutton_new(_("_Goto location"), accel);
  gtk_widget_set_sensitive( meswin_goto_command, FALSE );
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->action_area),
		     meswin_goto_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( meswin_goto_command, GTK_CAN_DEFAULT );

  meswin_popcity_command = gtk_accelbutton_new(_("_Popup City"), accel);
  gtk_widget_set_sensitive( meswin_popcity_command, FALSE );
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->action_area),
		     meswin_popcity_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( meswin_popcity_command, GTK_CAN_DEFAULT );

  gtk_signal_connect(GTK_OBJECT(meswin_list), "select_row",
		GTK_SIGNAL_FUNC(meswin_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(meswin_list), "unselect_row",
		GTK_SIGNAL_FUNC(meswin_list_ucallback), NULL);

  gtk_signal_connect(GTK_OBJECT(meswin_close_command), "clicked",
		GTK_SIGNAL_FUNC(meswin_close_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(meswin_goto_command), "clicked",
		GTK_SIGNAL_FUNC(meswin_goto_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(meswin_popcity_command), "clicked",
		GTK_SIGNAL_FUNC(meswin_popcity_callback), NULL);

  if (gtk_rc_get_style(meswin_list))
    meswin_visited_style = gtk_style_copy(gtk_rc_get_style(meswin_list));
  else
    meswin_visited_style = gtk_style_new();
  meswin_visited_style->fg[GTK_STATE_NORMAL]=
					*colors_standard[COLOR_STD_RACE8];
  meswin_visited_style->fg[GTK_STATE_SELECTED]=
					*colors_standard[COLOR_STD_RACE8];
  meswin_visited_style->bg[GTK_STATE_SELECTED]=
					*colors_standard[COLOR_STD_RACE13];

  meswin_not_visited_style = gtk_style_copy(meswin_visited_style);
  meswin_not_visited_style->fg[GTK_STATE_NORMAL]=
					*colors_standard[COLOR_STD_OCEAN];
  meswin_not_visited_style->fg[GTK_STATE_SELECTED]=
					*colors_standard[COLOR_STD_OCEAN];
  meswin_not_visited_style->bg[GTK_STATE_SELECTED]=
					*colors_standard[COLOR_STD_RACE13];

  real_update_meswin_dialog();

  gtk_widget_show_all( GTK_DIALOG(meswin_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(meswin_dialog_shell)->action_area );
}

/**************************************************************************
 This scrolls the messages window down to the bottom.
 NOTE: it seems this must not be called until _after_ meswin_dialog_shell
 is ...? realized, popped up, ... something.
 Its a toss-up whether we _should_ scroll the window down:
 Against: user will likely want to read from the top and scroll down manually.
 For: if we don't scroll down, new messages which appear at the bottom
 (including combat results etc) will be easily missed.
**************************************************************************/
static void meswin_scroll_down(void)
{
  if (get_num_messages() <= N_MSG_VIEW) {
    return;
  }

  gtk_clist_moveto(GTK_CLIST(meswin_list), get_num_messages() - 1, 0, 0.0,
		   0.0);
}

/**************************************************************************
...
**************************************************************************/
void real_update_meswin_dialog(void)
{
  int i, num = get_num_messages();

  gtk_clist_freeze(GTK_CLIST(meswin_list));
  gtk_clist_clear(GTK_CLIST(meswin_list));

  for (i = 0; i < num; i++) {
    gtk_clist_append(GTK_CLIST(meswin_list), &get_message(i)->descr);
    if(get_message(i)->visited) {
      meswin_visited_item(i);
    } else {
      meswin_not_visited_item(i);
    }
  }

  gtk_clist_thaw(GTK_CLIST(meswin_list));
  gtk_widget_show_all(meswin_list);

  meswin_scroll_down();

  gtk_widget_set_sensitive(meswin_goto_command, FALSE);
  gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_list_callback(GtkWidget * w, gint row, gint column,
				 GdkEvent * ev)
{
  struct message *message = get_message(row);

  if (ev && ev->type == GDK_2BUTTON_PRESS) {
    /* since we added a gtk_clist_select_row() there may be no event */
    meswin_double_click(row);
  }
  meswin_visited_item(row);

  gtk_widget_set_sensitive(meswin_goto_command, message->location_ok);
  gtk_widget_set_sensitive(meswin_popcity_command, message->city_ok);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_list_ucallback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(meswin_goto_command, FALSE);
  gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_close_callback(GtkWidget *w, gpointer data)
{
  popdown_meswin_dialog();
}

/**************************************************************************
...
**************************************************************************/
static void meswin_goto_callback(GtkWidget * w, gpointer data)
{
  GList *selection = selection = GTK_CLIST(meswin_list)->selection;
  gint row;

  if (!selection) {
    return;
  }

  row = GPOINTER_TO_INT(selection->data);
  meswin_goto(row);
  meswin_visited_item(row);
}

/**************************************************************************
...
**************************************************************************/
static void meswin_popcity_callback(GtkWidget * w, gpointer data)
{
  GList *selection = selection = GTK_CLIST(meswin_list)->selection;
  gint row;

  if (!selection) {
    return;
  }

  row = GPOINTER_TO_INT(selection->data);
  meswin_popup_city(row);
  meswin_visited_item(row);
}
