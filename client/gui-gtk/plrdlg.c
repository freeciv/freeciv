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
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "clinet.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"

#include "plrdlg.h"


extern GdkWindow *root_window;
extern GtkWidget *toplevel;

static GtkWidget *players_dialog_shell;
static GtkWidget *players_list;
static GtkWidget *players_close_command;
static GtkWidget *players_int_command;
static GtkWidget *players_meet_command;
static GtkWidget *players_sship_command;

static int list_index_to_player_index[MAX_NUM_PLAYERS];


static void create_players_dialog(void);
static void players_button_callback(GtkWidget *w, gpointer data);
static void players_meet_callback(GtkWidget *w, gpointer data);
static void players_intel_callback(GtkWidget *w, gpointer data);
static void players_list_callback(GtkWidget *w, gint row, gint column);
static void players_list_ucallback(GtkWidget *w, gint row, gint column);
static void players_sship_callback(GtkWidget *w, gpointer data);


/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_players_dialog(void)
{
  if(!players_dialog_shell){
    create_players_dialog();
    gtk_set_relative_position(toplevel, players_dialog_shell, 25, 25);

    gtk_widget_show(players_dialog_shell);
  }
}


/****************************************************************
...
*****************************************************************/
void create_players_dialog(void)
{
  static gchar *titles_[6] = { N_("Name"), N_("Race"), N_("Embassy"),
			       N_("State"), N_("Host"), N_("Idle") };
  static gchar **titles;
  int    i;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!titles) titles = intl_slist(6, titles_);

  players_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(players_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(players_button_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(players_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(players_dialog_shell), _("Players"));

  players_list=gtk_clist_new_with_titles(6, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(players_list));

  for(i=0; i<6; i++)
    gtk_clist_set_column_auto_resize (GTK_CLIST (players_list), i, TRUE);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->vbox),
	players_list, TRUE, TRUE, 0);

  players_close_command=gtk_accelbutton_new(_("C_lose"), accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_close_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_close_command, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(players_close_command);

  players_int_command=gtk_accelbutton_new(_("_Intelligence"), accel);
  gtk_widget_set_sensitive(players_int_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_int_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_int_command, GTK_CAN_DEFAULT);

  players_meet_command=gtk_accelbutton_new(_("_Meet"), accel);
  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_meet_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_meet_command, GTK_CAN_DEFAULT);

  players_sship_command=gtk_accelbutton_new(_("_Spaceship"), accel);
  gtk_widget_set_sensitive(players_sship_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_sship_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_sship_command, GTK_CAN_DEFAULT);

  gtk_signal_connect(GTK_OBJECT(players_list), "select_row",
		GTK_SIGNAL_FUNC(players_list_callback), NULL);
  gtk_signal_connect(GTK_OBJECT(players_list), "unselect_row",
		GTK_SIGNAL_FUNC(players_list_ucallback), NULL);
  
  gtk_signal_connect(GTK_OBJECT(players_close_command), "clicked",
		GTK_SIGNAL_FUNC(players_button_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(players_meet_command), "clicked",
		GTK_SIGNAL_FUNC(players_meet_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(players_int_command), "clicked",
		GTK_SIGNAL_FUNC(players_intel_callback), NULL);
  
  gtk_signal_connect(GTK_OBJECT(players_sship_command), "clicked",
		GTK_SIGNAL_FUNC(players_sship_callback), NULL);

  gtk_widget_show_all(GTK_DIALOG(players_dialog_shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(players_dialog_shell)->action_area);

  gtk_widget_add_accelerator(players_close_command, "clicked",
	accel, GDK_Escape, 0, 0);

  update_players_dialog();
}


/**************************************************************************
...
**************************************************************************/
void update_players_dialog(void)
{
   if(players_dialog_shell) {
    int i,j;
    char *row[6];

    gtk_clist_freeze(GTK_CLIST(players_list));
    gtk_clist_clear(GTK_CLIST(players_list));

    for(i=0,j=0; i<game.nplayers; i++) {
      char idlebuf[32], statebuf[32], namebuf[32];
      
      if(is_barbarian(&game.players[i]))
        continue;
      if(game.players[i].nturns_idle>3)
	sprintf(idlebuf, _("(idle %d turns)"), game.players[i].nturns_idle-1);
      else
	idlebuf[0]='\0';
      
      if(game.players[i].is_alive) {
	if(game.players[i].is_connected) {
	  if(game.players[i].turn_done)
	    strcpy(statebuf, _("done"));
	  else
	    strcpy(statebuf, _("moving"));
	}
	else
	  statebuf[0]='\0';
      }
      else
	strcpy(statebuf, _("R.I.P"));

      if(game.players[i].ai.control)
	sprintf(namebuf,"*%-15s", game.players[i].name);
      else
	sprintf(namebuf,"%-16s", game.players[i].name);

      row[0] = namebuf;
      row[1] = get_nation_name(game.players[i].nation);
      row[2] = player_has_embassy(game.player_ptr, &game.players[i]) ? "X":" ";
      row[3] = statebuf;
      row[4] = game.players[i].addr;
      row[5] = idlebuf;

      gtk_clist_append(GTK_CLIST(players_list), row);

      list_index_to_player_index[j] = i;
      j++;
    }
    
    gtk_clist_thaw(GTK_CLIST(players_list));
    gtk_widget_show_all(players_list);
  }
}

/**************************************************************************
...
**************************************************************************/
void players_list_callback(GtkWidget *w, gint row, gint column)
{
  int player_index = list_index_to_player_index[row];
  struct player *pplayer = &game.players[player_index];

    if(pplayer->spaceship.state != SSHIP_NONE)
      gtk_widget_set_sensitive(players_sship_command, TRUE);
    else
      gtk_widget_set_sensitive(players_sship_command, FALSE);

    if(pplayer->is_alive && player_has_embassy(game.player_ptr, pplayer)) {
      if(pplayer->is_connected)
	gtk_widget_set_sensitive(players_meet_command, TRUE);
      else
	gtk_widget_set_sensitive(players_meet_command, FALSE);
      gtk_widget_set_sensitive(players_int_command, TRUE);
      return;
    }

  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_widget_set_sensitive(players_int_command, FALSE);
}

void players_list_ucallback(GtkWidget *w, gint row, gint column)
{
  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_widget_set_sensitive(players_int_command, FALSE);
}

/**************************************************************************
...
**************************************************************************/
void players_button_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(players_dialog_shell);
  players_dialog_shell=0;
}

/**************************************************************************
...
**************************************************************************/
void players_meet_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint row;
  int player_index;

  if(!(selection=GTK_CLIST(players_list)->selection))
    return;

  row=(gint)selection->data;
  player_index = list_index_to_player_index[row];

  if(player_has_embassy(game.player_ptr, &game.players[player_index])) {
    struct packet_diplomacy_info pa;
  
    pa.plrno0=game.player_idx;
    pa.plrno1=player_index;
    send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_INIT_MEETING,
        		       &pa);
  }
  else {
    append_output_window(_("Game: You need an embassy to establish a diplomatic meeting."));
  }
}

/**************************************************************************
...
**************************************************************************/
void players_intel_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint row;
  int player_index;

  if(!(selection=GTK_CLIST(players_list)->selection))
      return;

  row=(gint)selection->data;
  player_index = list_index_to_player_index[row];

  if(player_has_embassy(game.player_ptr, &game.players[player_index]))
    popup_intel_dialog(&game.players[player_index]);
}

/**************************************************************************
...
**************************************************************************/
void players_sship_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint row;
  int player_index;

  if(!(selection=GTK_CLIST(players_list)->selection))
      return;

  row=(gint)selection->data;
  player_index = list_index_to_player_index[row];

  if(player_has_embassy(game.player_ptr, &game.players[player_index]))
    popup_spaceship_dialog(&game.players[player_index]);
}
