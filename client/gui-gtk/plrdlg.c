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
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"

#include "plrdlg.h"

static GtkWidget *players_dialog_shell;
static GtkWidget *players_list;
static GtkWidget *players_close_command;
static GtkWidget *players_int_command;
static GtkWidget *players_meet_command;
static GtkWidget *players_war_command;
static GtkWidget *players_vision_command;
static GtkWidget *players_sship_command;

static int list_index_to_player_index[MAX_NUM_PLAYERS];

static void create_players_dialog(void);
static void players_button_callback(GtkWidget *w, gpointer data);
static void players_meet_callback(GtkWidget *w, gpointer data);
static void players_war_callback(GtkWidget *w, gpointer data);
static void players_vision_callback(GtkWidget *w, gpointer data);
static void players_intel_callback(GtkWidget *w, gpointer data);
static void players_list_callback(GtkWidget *w, gint row, gint column);
static void players_list_ucallback(GtkWidget *w, gint row, gint column);
static void players_sship_callback(GtkWidget *w, gpointer data);

#define NUM_COLOUMS 9

static int delay_plrdlg_update=0;

/******************************************************************
 Turn off updating of player dialog
*******************************************************************/
void plrdlg_update_delay_on(void)
{
  delay_plrdlg_update=1;
}

/******************************************************************
 Turn on updating of player dialog
*******************************************************************/
void plrdlg_update_delay_off(void)
{
  delay_plrdlg_update=0;
}


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
  static gchar *titles_[NUM_COLOUMS] = { N_("Name"), N_("Nation"), N_("Embassy"),
					 N_("Dipl.State"), N_("Vision"),
					 N_("Reputation"), N_("State"),
					 N_("Host"), N_("Idle") };
  static gchar **titles;
  int    i;
  GtkAccelGroup *accel=gtk_accel_group_new();

  if (!titles) titles = intl_slist(NUM_COLOUMS, titles_);

  players_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(players_dialog_shell),"delete_event",
        GTK_SIGNAL_FUNC(players_button_callback),NULL );
  gtk_accel_group_attach(accel, GTK_OBJECT(players_dialog_shell));

  gtk_window_set_title(GTK_WINDOW(players_dialog_shell), _("Players"));

  players_list=gtk_clist_new_with_titles(NUM_COLOUMS, titles);
  gtk_clist_column_titles_passive(GTK_CLIST(players_list));

  for(i=0; i<NUM_COLOUMS; i++)
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

  players_war_command=gtk_accelbutton_new(_("_Cancel Treaty"), accel);
  gtk_widget_set_sensitive(players_war_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_war_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_war_command, GTK_CAN_DEFAULT);

  players_vision_command=gtk_accelbutton_new(_("_Withdraw vision"), accel);
  gtk_widget_set_sensitive(players_vision_command, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(players_dialog_shell)->action_area),
	players_vision_command, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(players_vision_command, GTK_CAN_DEFAULT);

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

  gtk_signal_connect(GTK_OBJECT(players_war_command), "clicked",
		GTK_SIGNAL_FUNC(players_war_callback), NULL);

  gtk_signal_connect(GTK_OBJECT(players_vision_command), "clicked",
		GTK_SIGNAL_FUNC(players_vision_callback), NULL);

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
   if(players_dialog_shell && !delay_plrdlg_update) {
    int i,j;
    char *row[NUM_COLOUMS];
    const struct player_diplstate *pds;

    gtk_clist_freeze(GTK_CLIST(players_list));
    gtk_clist_clear(GTK_CLIST(players_list));

    for(i=0,j=0; i<game.nplayers; i++) {
      char idlebuf[32], statebuf[32], namebuf[32], dsbuf[32], repbuf[32];      

      /* skip barbarians */
      if(is_barbarian(&game.players[i]))
        continue;
      /* text for idleness */
      if(game.players[i].nturns_idle>3) {
	my_snprintf(idlebuf, sizeof(idlebuf), _("(idle %d turns)"),
		    game.players[i].nturns_idle-1);
      } else {
	idlebuf[0]='\0';
      }

      /* text for state */
      if(game.players[i].is_alive) {
	if(game.players[i].is_connected) {
	  if(game.players[i].turn_done)
	    sz_strlcpy(statebuf, _("done"));
	  else
	    sz_strlcpy(statebuf, _("moving"));
	}
	else
	  statebuf[0]='\0';
      }
      else
	sz_strlcpy(statebuf, _("R.I.P"));

      /* text for name, plus AI marker */ 
      if(game.players[i].ai.control)
	my_snprintf(namebuf, sizeof(namebuf), "*%-15s", game.players[i].name);
      else
	my_snprintf(namebuf, sizeof(namebuf), "%-16s", game.players[i].name);

      /* text for diplstate type and turns -- not applicable if this is me */
      if (i == game.player_idx) {
	strcpy(dsbuf, "-");
      } else {
	pds = player_get_diplstate(game.player_idx, i);
	if (pds->type == DS_CEASEFIRE) {
	  my_snprintf(dsbuf, sizeof(dsbuf), "%s (%d)",
		      diplstate_text(pds->type), pds->turns_left);
	} else {
	  my_snprintf(dsbuf, sizeof(dsbuf), "%s",
		      diplstate_text(pds->type));
	}
      }

      /* text for reputation */
      my_snprintf(repbuf, sizeof(repbuf),
		  reputation_text(game.players[i].reputation));

      /* assemble the whole lot */
      row[0] = namebuf;
      row[1] = get_nation_name(game.players[i].nation);
      row[2] = get_embassy_status(game.player_ptr, &game.players[i]);
      row[3] = dsbuf;
      row[4] = get_vision_status(game.player_ptr, &game.players[i]);
      row[5] = repbuf;
      row[6] = statebuf;
      row[7] = (char*)player_addr_hack(&game.players[i]);  /* Fixme */
      row[8] = idlebuf;

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

  switch(player_get_diplstate(game.player_idx, player_index)->type) {
  case DS_WAR: case DS_NO_CONTACT:
    gtk_widget_set_sensitive(players_war_command, FALSE);
    break;
  default:
    gtk_widget_set_sensitive(players_war_command, game.player_idx != player_index);
  }

  if (game.player_ptr->gives_shared_vision & (1<<pplayer->player_no))
    gtk_widget_set_sensitive(players_vision_command, TRUE);
  else
    gtk_widget_set_sensitive(players_vision_command, FALSE);

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
void players_war_callback(GtkWidget *w, gpointer data)
{
  GList *selection = GTK_CLIST(players_list)->selection;
  gint row;
  int player_index;

  if (!selection)
    return;
  else {
    struct packet_generic_integer pa;    

    row = (gint)selection->data;
    player_index = list_index_to_player_index[row];

    pa.value = player_index;
    send_packet_generic_integer(&aconnection, PACKET_PLAYER_CANCEL_PACT,
				&pa);
  }
}

/**************************************************************************
...
**************************************************************************/
void players_vision_callback(GtkWidget *w, gpointer data)
{
  GList *selection = GTK_CLIST(players_list)->selection;
  gint row;
  int player_index;

  if (!selection)
    return;
  else {
    struct packet_generic_integer pa;    

    row = (gint)selection->data;
    player_index = list_index_to_player_index[row];

    pa.value = player_index;
    send_packet_generic_integer(&aconnection, PACKET_PLAYER_REMOVE_VISION,
				&pa);
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

  popup_spaceship_dialog(&game.players[player_index]);
}
