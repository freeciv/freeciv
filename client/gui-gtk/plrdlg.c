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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <game.h>
#include <player.h>
#include <plrdlg.h>
#include <inteldlg.h>
#include <packets.h>
#include <clinet.h>
#include <chatline.h>
#include <gui_stuff.h>
#include <spaceshipdlg.h>


extern GdkWindow *root_window;
extern GtkWidget *toplevel;
extern struct player_race races[];

GtkWidget *players_dialog_shell;
GtkWidget *players_list;
GtkWidget *players_close_command;
GtkWidget *players_int_command;
GtkWidget *players_meet_command;
GtkWidget *players_sship_command;

void create_players_dialog(void);
void players_button_callback(GtkWidget *w, gpointer data);
void players_meet_callback(GtkWidget *w, gpointer data);
void players_intel_callback(GtkWidget *w, gpointer data);
void players_list_callback(GtkWidget *w, gint row, gint column);
void players_list_ucallback(GtkWidget *w, gint row, gint column);
void players_sship_callback(GtkWidget *w, gpointer data);


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
  gchar *titles	[ 6] = { "Name", "Race", "Embassy", "State", "Host", "Idle" };
  int    i;
  GtkAccelGroup *accel=gtk_accel_group_new();

  players_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(players_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(players_button_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(players_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(players_dialog_shell), "Players");

  players_list=gtk_clist_new_with_titles(6, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(players_list));

  for(i=0; i<6; i++)
    gtk_clist_set_column_auto_resize (GTK_CLIST (players_list), i, TRUE);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->vbox),
	players_list, TRUE, TRUE, 0);

  players_close_command=gtk_accelbutton_new("C_lose", accel);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_close_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_close_command, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(players_close_command);

  players_int_command=gtk_accelbutton_new("_Intelligence", accel);
  gtk_widget_set_sensitive(players_int_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_int_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_int_command, GTK_CAN_DEFAULT);

  players_meet_command=gtk_accelbutton_new("_Meet", accel);
  gtk_widget_set_sensitive(players_meet_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_meet_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_meet_command, GTK_CAN_DEFAULT);

  players_sship_command=gtk_accelbutton_new("_Spaceship", accel);
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
    int i;
    char *row[6];
    
    gtk_clist_freeze(GTK_CLIST(players_list));
    gtk_clist_clear(GTK_CLIST(players_list));

    for(i=0; i<game.nplayers; i++) {
      char idlebuf[32], statebuf[32], namebuf[32];
      
      if(game.players[i].nturns_idle>3)
	sprintf(idlebuf, "(idle %d turns)", game.players[i].nturns_idle-1);
      else
	idlebuf[0]='\0';
      
      if(game.players[i].is_alive) {
	if(game.players[i].is_connected) {
	  if(game.players[i].turn_done)
	    strcpy(statebuf, "done");
	  else
	    strcpy(statebuf, "moving");
	}
	else
	  statebuf[0]='\0';
      }
      else
	strcpy(statebuf, "R.I.P");

      if(game.players[i].ai.control)
	sprintf(namebuf,"*%-15s", game.players[i].name);
      else
	sprintf(namebuf,"%-16s", game.players[i].name);

      row[0] = namebuf;
      row[1] = races[game.players[i].race].name;
      row[2] = player_has_embassy(game.player_ptr, &game.players[i]) ? "X":" ";
      row[3] = statebuf;
      row[4] = game.players[i].addr;
      row[5] = idlebuf;

      gtk_clist_append(GTK_CLIST(players_list), row);
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
    if(game.players[row].spaceship.state != SSHIP_NONE)
      gtk_widget_set_sensitive(players_sship_command, TRUE);
    else
      gtk_widget_set_sensitive(players_sship_command, FALSE);

    if(player_has_embassy(game.player_ptr, &game.players[row])) {
      if(game.players[row].is_connected &&
	 game.players[row].is_alive)
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

  if(!(selection=GTK_CLIST(players_list)->selection))
    return;

  row=(gint)selection->data;

  if(player_has_embassy(game.player_ptr, &game.players[row])) {
    struct packet_diplomacy_info pa;
  
    pa.plrno0=game.player_idx;
    pa.plrno1=row;
    send_packet_diplomacy_info(&aconnection, PACKET_DIPLOMACY_INIT_MEETING,
        		       &pa);
  }
  else {
    append_output_window("Game: You need an embassy to establish a diplomatic meeting.");
  }
}

/**************************************************************************
...
**************************************************************************/
void players_intel_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint row;

  if(!(selection=GTK_CLIST(players_list)->selection))
      return;

  row=(gint)selection->data;

  if(player_has_embassy(game.player_ptr, &game.players[row]))
    popup_intel_dialog(&game.players[row]);
}

/**************************************************************************
...
**************************************************************************/
void players_sship_callback(GtkWidget *w, gpointer data)
{
  GList *selection;
  gint row;

  if(!(selection=GTK_CLIST(players_list)->selection))
      return;

  row=(gint)selection->data;

  if(player_has_embassy(game.player_ptr, &game.players[row]))
    popup_spaceship_dialog(&game.players[row]);
}
