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
#include <ctype.h>

#include <gtk/gtk.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"

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

static int delay_meswin_update=0;

/******************************************************************
 Turn off updating of message window
*******************************************************************/
void meswin_update_delay_on(void)
{
  delay_meswin_update=1;
}

/******************************************************************
 Turn on updating of message window
*******************************************************************/
void meswin_update_delay_off(void)
{
  delay_meswin_update=0;
}


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
static void meswin_visited_item (gint n)
{
  gtk_clist_set_row_style (GTK_CLIST (meswin_list), n,
						meswin_visited_style);
}

/****************************************************************
...
*****************************************************************/
static void meswin_not_visited_item (gint n)
{
  gtk_clist_set_row_style (GTK_CLIST (meswin_list), n,
						meswin_not_visited_style);
}

/****************************************************************
...
*****************************************************************/
void create_meswin_dialog(void)
{
  static gchar *titles_[1] = { N_("Messages") };
  static gchar **titles;
  GtkWidget *scrolled;
  GtkAccelGroup *accel = gtk_accel_group_new();

  if (!titles) titles = intl_slist(1, titles_);

  meswin_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(meswin_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(meswin_close_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(meswin_dialog_shell));

  gtk_window_set_title( GTK_WINDOW(meswin_dialog_shell), _("Messages") );

  meswin_list = gtk_clist_new_with_titles(1, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(meswin_list));
  scrolled = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scrolled),meswin_list);
  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrolled ),
                          GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
  gtk_widget_set_usize(scrolled, 500, 250);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(meswin_dialog_shell)->vbox),
		     scrolled, TRUE, TRUE, 0 );

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

  update_meswin_dialog();

  gtk_widget_show_all( GTK_DIALOG(meswin_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(meswin_dialog_shell)->action_area );
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

void clear_notify_window(void)
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
  update_meswin_dialog();
  if(meswin_dialog_shell) {
    gtk_widget_set_sensitive(meswin_goto_command, FALSE);
    gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
  }
}

/**************************************************************************
...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
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
  if (!delay_meswin_update) {
    update_meswin_dialog();
    meswin_scroll_down();
  }
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
void meswin_scroll_down(void)
{
  if (!meswin_dialog_shell)
    return;
  if (messages_total <= N_MSG_VIEW)
    return;
  
   gtk_clist_moveto(GTK_CLIST(meswin_list), messages_total-1, 0, 0.0, 0.0);
}

/**************************************************************************
...
**************************************************************************/
void update_meswin_dialog(void)
{
  if (!meswin_dialog_shell) { 
    if (messages_total > 0 &&
        (!game.player_ptr->ai.control || ai_popup_windows)) {
      popup_meswin_dialog();
      /* Can return here because popup_meswin_dialog will call
       * this very function again.
       */
      return;
    }
  }
   if(meswin_dialog_shell) {
     int i;

     gtk_clist_freeze(GTK_CLIST(meswin_list));
     gtk_clist_clear(GTK_CLIST(meswin_list));

     for (i=0; i<messages_total; i++)
     {
       gtk_clist_append (GTK_CLIST (meswin_list), &string_ptrs[i]);
       meswin_not_visited_item (i);
     }

     gtk_clist_thaw(GTK_CLIST(meswin_list));
     gtk_widget_show_all(meswin_list);
   }

}

/**************************************************************************
...
**************************************************************************/
void meswin_list_callback (GtkWidget *w, gint row, gint column, GdkEvent *ev)
{
  struct city *pcity;
  int x, y;
  int location_ok;
  int city_ok;

  x = xpos[row];
  y = ypos[row];
  location_ok = (x != -1 && y != -1);
  city_ok = (location_ok && (pcity=map_get_city(x,y))
	    && (pcity->owner == game.player_idx));

  if (ev)  /* since we added a gtk_clist_select_row() there may be no event */
  {
    switch (ev->type)
    {
    case GDK_2BUTTON_PRESS:
      switch (event[row])
      {
      case E_NOEVENT:
      case E_LOW_ON_FUNDS:
      case E_ANARCHY:
      case E_DIPLOMATED:
      case E_TECH_GAIN:
      case E_DESTROYED:
	break;
      case E_GLOBAL_ECO:
      case E_CITY_LOST:
      case E_UNIT_LOST:
      case E_UNIT_WIN:
      case E_MY_DIPLOMAT:
      case E_UNIT_LOST_ATT:
      case E_UNIT_WIN_ATT:
      case E_UPRISING:
	meswin_goto_callback (meswin_goto_command, NULL);
	break;
      default:
	meswin_popcity_callback (meswin_popcity_command, NULL);
	break;
      }
      meswin_visited_item (row);
      break;
    default:
      break;
    }
  }
  gtk_widget_set_sensitive(meswin_goto_command, location_ok?TRUE:FALSE);
  gtk_widget_set_sensitive(meswin_popcity_command, city_ok?TRUE:FALSE);
}

/**************************************************************************
...
**************************************************************************/
void meswin_list_ucallback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(meswin_goto_command, FALSE);
  gtk_widget_set_sensitive(meswin_popcity_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
void meswin_close_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(meswin_dialog_shell);
  meswin_dialog_shell = NULL;
  gtk_style_unref(meswin_visited_style);
  gtk_style_unref(meswin_not_visited_style);
}

/**************************************************************************
...
**************************************************************************/
void meswin_goto_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint   row;

  if (!(selection=GTK_CLIST(meswin_list)->selection))
      return;

  row = GPOINTER_TO_INT(selection->data);

  if(xpos[row]!=-1 && ypos[row]!=-1)
    center_tile_mapcanvas(xpos[row], ypos[row]);
  meswin_visited_item (row);
}

/**************************************************************************
...
**************************************************************************/
void meswin_popcity_callback(GtkWidget *w, gpointer data)
{
  struct city *pcity;
  int x, y;
  GList *selection;
  gint row;

  if (!(selection=GTK_CLIST(meswin_list)->selection))
      return;

  row = GPOINTER_TO_INT(selection->data);

  x = xpos[row];
  y = ypos[row];
  if((x!=-1 && y!=-1) && (pcity=map_get_city(x,y))
     && (pcity->owner == game.player_idx)) {
      if (center_when_popup_city) {
       center_tile_mapcanvas(x,y);
      }
    popup_city_dialog(pcity, 0);
  }
  meswin_visited_item (row);
}
