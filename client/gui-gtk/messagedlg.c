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
#include <stdarg.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <game.h>
#include <player.h>
#include <mapview.h>
#include <messagedlg.h>
#include <shared.h>
#include <packets.h>
#include <events.h>
#include <optiondlg.h>
#include <dialogs.h>
#include <gui_stuff.h>

extern GtkWidget *toplevel;

extern struct connection aconnection;

unsigned int messages_where[E_LAST];

char *message_text[E_LAST]={
  "Low Funds		    ",  	       /* E_LOW_ON_FUNDS */
  "Pollution		    ",
  "Global Warming	    ",
  "Civil Disorder	    ",
  "City Celebrating	    ",
  "City Normal  	    ",
  "City Growth  	    ",
  "City Needs Aqueduct      ",
  "Famine in City	    ",
  "City Captured/Destroyed  ",
  "Building Unavailable Item",
  "Wonder Started	    ",
  "Wonder Finished	    ",
  "Improvement Built	    ",
  "New Improvement Selected ",
  "Forced Improvement Sale  ",
  "Production Upgraded      ",
  "Unit Built		    ",
  "Unit Defender Destroyed  ",
  "Unit Defender Survived   ",
  "Collapse to Anarchy      ",
  "Diplomat Actions - Enemy ",
  "Tech from Great Library  ",
  "Player Destroyed	    ",         /* E_DESTROYED */
  "Improvement Bought	    ",         /* E_IMP_BUY */
  "Improvement Sold	    ",         /* E_IMP_SOLD */
  "Unit Bought  	    ",         /* E_UNIT_BUY */
  "Wonder Stopped	    ",         /* E_WONDER_STOPPED */
  "City Needs Aq Being Built",         /* E_CITY_AQ_BUILDING */
  "Diplomat Actions - Own   ",  	/* E_MY_DIPLOMAT */
  "Unit Attack Failed	    ",  	/* E_UNIT_LOST_ATT */
  "Unit Attack Succeeded    ",  	/* E_UNIT_WIN_ATT */
  "Suggest Growth Throttling",          /* E_CITY_GRAN_THROTTLE */
};


/****************************************************************
... 
*****************************************************************/
void init_messages_where(void)
{
  int out_only[] = {E_IMP_BUY, E_IMP_SOLD, E_UNIT_BUY, E_MY_DIPLOMAT,
		   E_UNIT_LOST_ATT, E_UNIT_WIN_ATT};
  int i;

  for(i=0; i<E_LAST; i++) {
    messages_where[i] = MW_OUTPUT | MW_MESSAGES;
  }
  for(i=0; i<sizeof(out_only)/sizeof(int); i++) {
    messages_where[out_only[i]] = MW_OUTPUT;
  }
}

/*************************************************************************/
GtkWidget *create_messageopt_dialog(void);
void messageopt_ok_command_callback(GtkWidget *w, gpointer data);
void messageopt_cancel_command_callback(GtkWidget *w, gpointer data);
static GtkWidget *messageopt_toggles[E_LAST][NUM_MW];

/**************************************************************************
Comparison function for qsort; i1 and i2 are pointers to integers which
index message_text[].
**************************************************************************/
int compar_message_texts(const void *i1, const void *i2)
{
  int j1 = *(const int*)i1;
  int j2 = *(const int*)i2;
  
  return strcmp(message_text[j1], message_text[j2]);
}

/**************************************************************************
... 
**************************************************************************/
void popup_messageopt_dialog(void)
{
  GtkWidget *shell;
  int i, j, state;

  shell=create_messageopt_dialog();

  /* Doing this here makes the "No"'s centered consistently */
  for(i=0; i<E_LAST; i++) {
    for(j=0; j<NUM_MW; j++) {
      state = messages_where[i] & (1<<j);
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(messageopt_toggles[i][j]),
	state);
    }
  }
  
/*  gtk_set_relative_position(toplevel, shell, 15, 0);*/
  gtk_window_set_position (GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  gtk_widget_show(shell);
  gtk_widget_set_sensitive(toplevel, FALSE);
}

/**************************************************************************
...
**************************************************************************/
GtkWidget *create_messageopt_dialog(void)
{
  GtkWidget *shell, *form, *explanation, *ok, *cancel, *col1, *col2;
  GtkWidget *colhead1, *colhead2;
  GtkWidget *label;
  GtkWidget *toggle=0;
  int i, j;
  int sorted[E_LAST];
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  shell=gtk_dialog_new();
  gtk_window_set_title(GTK_WINDOW(shell), "Message Options");
  gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(shell)->vbox), 5);
  gtk_accel_group_attach(accel, GTK_OBJECT(shell));

  explanation=gtk_label_new("Where to Display Messages\nOut = Output window,"
		      " Mes = Messages window, Pop = Popup individual window");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), explanation,
		FALSE, FALSE, 0);

  form=gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->vbox), form, FALSE, FALSE, 0);

  colhead1=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(form), colhead1, TRUE, TRUE, 0);

  col1=gtk_table_new(E_LAST/2+2, NUM_MW+1, FALSE);
  gtk_container_add(GTK_CONTAINER(colhead1), col1);

  colhead2=gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(form), colhead2, TRUE, TRUE, 0);

  col2=gtk_table_new(E_LAST/2+2, NUM_MW+1, FALSE);
  gtk_container_add(GTK_CONTAINER(colhead2), col2);

  for(i=0; i<E_LAST; i++)  {
    sorted[i]=i;
  }
  qsort(sorted, E_LAST, sizeof(int), compar_message_texts);
  
  label = gtk_label_new("Out:");
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 1, 2, 0, 1);
  label = gtk_label_new("Mes:");
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 2, 3, 0, 1);
  label = gtk_label_new("Pop:");
  gtk_table_attach_defaults(GTK_TABLE(col1), label, 3, 4, 0, 1);
  label = gtk_label_new("Out:");
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 1, 2, 0, 1);
  label = gtk_label_new("Mes:");
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 2, 3, 0, 1);
  label = gtk_label_new("Pop:");
  gtk_table_attach_defaults(GTK_TABLE(col2), label, 3, 4, 0, 1);

  for(i=0; i<E_LAST; i++)  {
    int line = (i%E_LAST);
    int is_col1 = i<(E_LAST/2);

    label = gtk_label_new(message_text[sorted[i]]);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
    gtk_table_attach_defaults(GTK_TABLE(is_col1?col1:col2), label,
               0, 1, line+1, line+2);
    for(j=0; j<NUM_MW; j++) {
      toggle = gtk_check_button_new();
      gtk_table_attach_defaults(GTK_TABLE(is_col1?col1:col2), toggle,
               j+1, j+2, line+1, line+2);
      messageopt_toggles[sorted[i]][j]=toggle;
    }
  }

  ok=gtk_button_new_with_label("Ok");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), ok,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(ok);
  gtk_widget_add_accelerator(ok, "clicked", accel, GDK_Escape, 0,
	GTK_ACCEL_VISIBLE);

  cancel=gtk_button_new_with_label("Cancel");
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(shell)->action_area), cancel,
	TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
  
  gtk_signal_connect(GTK_OBJECT(ok), "clicked",
	messageopt_ok_command_callback, shell);
  gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
	messageopt_cancel_command_callback, shell);
  gtk_signal_connect(GTK_OBJECT(shell), "delete_event",
        GTK_SIGNAL_FUNC(messageopt_cancel_command_callback), shell);

  gtk_widget_show_all(GTK_DIALOG(shell)->vbox);
  gtk_widget_show_all(GTK_DIALOG(shell)->action_area);

  return shell;
}

/**************************************************************************
...
**************************************************************************/
void messageopt_cancel_command_callback(GtkWidget *w, gpointer data)
{
  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy((GtkWidget *)data);
}

/**************************************************************************
...
**************************************************************************/
void messageopt_ok_command_callback(GtkWidget *w, gpointer data)
{
  int i, j;

  gtk_widget_set_sensitive(toplevel, TRUE);

  for(i=0;i<E_LAST;i++)  {
    messages_where[i] = 0;
    for(j=0; j<NUM_MW; j++) {
      if (GTK_TOGGLE_BUTTON(messageopt_toggles[i][j])->active)
        messages_where[i] |= (1<<j);
    }
  }

  gtk_widget_destroy((GtkWidget *)data);
}
