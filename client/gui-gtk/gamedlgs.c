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

#include "events.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"

#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gui_stuff.h"
#include "options.h"

#include "ratesdlg.h"
#include "optiondlg.h"

extern	GtkWidget *toplevel;
extern struct connection aconnection;


/******************************************************************/
GtkWidget	*rates_dialog_shell;
GtkWidget	*rates_gov_label;
GtkWidget	*rates_tax_toggle, *rates_lux_toggle, *rates_sci_toggle;
GtkWidget	*rates_tax_label, *rates_lux_label, *rates_sci_label;
GtkObject	*rates_tax_adj, *rates_lux_adj, *rates_sci_adj;

guint		 rates_tax_sig, rates_lux_sig, rates_sci_sig;
/******************************************************************/

int rates_tax_value, rates_lux_value, rates_sci_value;


void rates_changed_callback(GtkAdjustment *adj);


/**************************************************************************
...
**************************************************************************/
static void rates_set_values(int tax, int no_tax_scroll, 
			     int lux, int no_lux_scroll,
			     int sci, int no_sci_scroll)
{
  char buf[16];
  int tax_lock, lux_lock, sci_lock;
  int maxrate;
  
  tax_lock	= GTK_TOGGLE_BUTTON(rates_tax_toggle)->active;
  lux_lock	= GTK_TOGGLE_BUTTON(rates_lux_toggle)->active;
  sci_lock	= GTK_TOGGLE_BUTTON(rates_sci_toggle)->active;

  maxrate=get_government_max_rate(game.player_ptr->government);
  /* This's quite a simple-minded "double check".. */
  tax=MIN(tax, maxrate);
  lux=MIN(lux, maxrate);
  sci=MIN(sci, maxrate);
  
  if(tax+sci+lux!=100)
  {
    if((tax!=rates_tax_value))
    {
      if(!lux_lock)
	lux=MIN(MAX(100-tax-sci, 0), maxrate);
      if(!sci_lock)
	sci=MIN(MAX(100-tax-lux, 0), maxrate);
    }
    else if((lux!=rates_lux_value))
    {
      if(!tax_lock)
	tax=MIN(MAX(100-lux-sci, 0), maxrate);
      if(!sci_lock)
	sci=MIN(MAX(100-lux-tax, 0), maxrate);
    }
    else if((sci!=rates_sci_value))
    {
      if(!lux_lock)
	lux=MIN(MAX(100-tax-sci, 0), maxrate);
      if(!tax_lock)
	tax=MIN(MAX(100-lux-sci, 0), maxrate);
    }
    
    if(tax+sci+lux!=100) {
      tax=rates_tax_value;
      lux=rates_lux_value;
      sci=rates_sci_value;

      rates_tax_value=-1;
      rates_lux_value=-1;
      rates_sci_value=-1;

      no_tax_scroll=0;
      no_lux_scroll=0;
      no_sci_scroll=0;
    }

  }

  if(tax!=rates_tax_value) {
    sprintf(buf,"%3d%%",tax);
    if ( strcmp( buf, GTK_LABEL(rates_tax_label)->label) )
	gtk_set_label(rates_tax_label,buf);
    if(!no_tax_scroll)
    {
	gtk_signal_handler_block(GTK_OBJECT(rates_tax_adj),rates_tax_sig);
	gtk_adjustment_set_value( GTK_ADJUSTMENT(rates_tax_adj), tax/10 );
	gtk_signal_handler_unblock(GTK_OBJECT(rates_tax_adj),rates_tax_sig);
    }
    rates_tax_value=tax;
  }

  if(lux!=rates_lux_value) {
    sprintf(buf,"%3d%%",lux);
    if ( strcmp( buf, GTK_LABEL(rates_lux_label)->label) )
	gtk_set_label(rates_lux_label,buf);
    if(!no_lux_scroll)
    {
	gtk_signal_handler_block(GTK_OBJECT(rates_lux_adj),rates_lux_sig);
	gtk_adjustment_set_value( GTK_ADJUSTMENT(rates_lux_adj), lux/10 );
	gtk_signal_handler_unblock(GTK_OBJECT(rates_lux_adj),rates_lux_sig);
    }
    rates_lux_value=lux;
  }

  if(sci!=rates_sci_value) {
    sprintf(buf,"%3d%%",sci);
    if ( strcmp( buf, GTK_LABEL(rates_sci_label)->label) )
	gtk_set_label(rates_sci_label,buf);
    if(!no_sci_scroll)
    {
	gtk_signal_handler_block(GTK_OBJECT(rates_sci_adj),rates_sci_sig);
	gtk_adjustment_set_value( GTK_ADJUSTMENT(rates_sci_adj), sci/10 );
	gtk_signal_handler_unblock(GTK_OBJECT(rates_sci_adj),rates_sci_sig);
    }
    rates_sci_value=sci;
  }
}


/**************************************************************************
...
**************************************************************************/
void rates_changed_callback(GtkAdjustment *adj)
{
  int percent=adj->value;

  if(adj==GTK_ADJUSTMENT(rates_tax_adj)) {
    int tax_value;

    tax_value=10*percent;
    tax_value=MIN(tax_value, 100);
    rates_set_values(tax_value,1, rates_lux_value,0, rates_sci_value,0);
  }
  else if(adj==GTK_ADJUSTMENT(rates_lux_adj)) {
    int lux_value;

    lux_value=10*percent;
    lux_value=MIN(lux_value, 100);
    rates_set_values(rates_tax_value,0, lux_value,1, rates_sci_value,0);
  }
  else {
    int sci_value;

    sci_value=10*percent;
    sci_value=MIN(sci_value, 100);
    rates_set_values(rates_tax_value,0, rates_lux_value,0, sci_value,1);
  }
}


/**************************************************************************
...
**************************************************************************/
static void rates_ok_command_callback(GtkWidget *widget, gpointer data)
{
  struct packet_player_request packet;
  
  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(rates_dialog_shell);

  packet.tax=rates_tax_value;
  packet.science=rates_sci_value;
  packet.luxury=rates_lux_value;
  send_packet_player_request(&aconnection, &packet, PACKET_PLAYER_RATES);
}


/**************************************************************************
...
**************************************************************************/
static void rates_cancel_command_callback(GtkWidget *widget, gpointer data)
{
  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(rates_dialog_shell);
}



/****************************************************************
... 
*****************************************************************/
static void create_rates_dialog(void)
{
  GtkWidget	*frame, *hbox;

  GtkWidget	*scale;
  GtkWidget	*button;
  GtkAccelGroup *accel=gtk_accel_group_new();
  
  rates_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(rates_dialog_shell),"delete_event",
      GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_window_set_position (GTK_WINDOW(rates_dialog_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(rates_dialog_shell));

  gtk_window_set_title( GTK_WINDOW( rates_dialog_shell ), "Select tax, luxury and science rates" );

  rates_gov_label = gtk_label_new("");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->vbox ), rates_gov_label, TRUE, TRUE, 5 );

  frame = gtk_frame_new( "Tax" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_tax_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_tax_adj ) );
  gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_tax_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_tax_label, TRUE, TRUE, 0 );
  gtk_widget_set_usize( GTK_WIDGET( rates_tax_label ), 40,0 );

  rates_tax_toggle = gtk_check_button_new_with_label( "Lock" );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_tax_toggle, TRUE, TRUE, 0 );

  frame = gtk_frame_new( "Luxury" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_lux_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_lux_adj ) );
  gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_lux_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_lux_label, TRUE, TRUE, 0 );
  gtk_widget_set_usize( GTK_WIDGET( rates_lux_label ), 40,0 );

  rates_lux_toggle = gtk_check_button_new_with_label( "Lock" );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_lux_toggle, TRUE, TRUE, 0 );

  frame = gtk_frame_new( "Science" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_sci_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_sci_adj ) );
  gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_sci_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_sci_label, TRUE, TRUE, 0 );
  gtk_widget_set_usize( GTK_WIDGET( rates_sci_label ), 40,0 );

  rates_sci_toggle = gtk_check_button_new_with_label( "Lock" );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_sci_toggle, TRUE, TRUE, 0 );



  button = gtk_button_new_with_label( "Ok" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->action_area ), button, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( button );
  gtk_signal_connect(GTK_OBJECT(button),"clicked",
      GTK_SIGNAL_FUNC(rates_ok_command_callback), NULL);
  gtk_widget_add_accelerator(button, "clicked",
    accel, GDK_Escape, 0, GTK_ACCEL_VISIBLE);

  button = gtk_button_new_with_label( "Cancel" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( rates_dialog_shell )->action_area ), button, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
  gtk_signal_connect(GTK_OBJECT(button),"clicked",
      GTK_SIGNAL_FUNC(rates_cancel_command_callback), NULL);

  gtk_widget_show_all( GTK_DIALOG( rates_dialog_shell )->vbox );
  gtk_widget_show_all( GTK_DIALOG( rates_dialog_shell )->action_area );

  rates_tax_value=-1;
  rates_lux_value=-1;
  rates_sci_value=-1;

  rates_tax_sig =
      gtk_signal_connect_after( GTK_OBJECT( rates_tax_adj ), "value_changed",
        GTK_SIGNAL_FUNC( rates_changed_callback ), NULL );

  rates_lux_sig =
      gtk_signal_connect_after( GTK_OBJECT( rates_lux_adj ), "value_changed",
        GTK_SIGNAL_FUNC( rates_changed_callback ), NULL );

  rates_sci_sig =
      gtk_signal_connect_after( GTK_OBJECT( rates_sci_adj ), "value_changed",
        GTK_SIGNAL_FUNC( rates_changed_callback ), NULL );

  rates_set_values( game.player_ptr->economic.tax, 0,
        	    game.player_ptr->economic.luxury, 0,
        	    game.player_ptr->economic.science, 0 );
  return;
}




/****************************************************************
... 
*****************************************************************/
void popup_rates_dialog( void )
{
    char buf[50];

    gtk_widget_set_sensitive(toplevel, FALSE );
    create_rates_dialog();

    sprintf(buf, "%s max rate: %d%%",
	get_government_name(game.player_ptr->government),
	get_government_max_rate(game.player_ptr->government));
    gtk_set_label(rates_gov_label, buf);
   
    gtk_widget_show( rates_dialog_shell );
}

/**************************************************************************
  Option dialog 
**************************************************************************/

GtkWidget *option_dialog_shell;

/**************************************************************************
...
**************************************************************************/
static void option_ok_command_callback(GtkWidget *widget, gpointer data)
{
  client_option *o;

  for (o=options; o->name; ++o) {
    *(o->p_value) = GTK_TOGGLE_BUTTON(o->p_gui_data)->active;
  }

  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(option_dialog_shell);
}


/****************************************************************
... 
*****************************************************************/
static void create_option_dialog(void)
{
  GtkWidget *button;
  GtkAccelGroup *accel=gtk_accel_group_new();
  client_option *o;

  option_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(option_dialog_shell),"delete_event",
      GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_window_set_position (GTK_WINDOW(option_dialog_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(option_dialog_shell));

  gtk_window_set_title( GTK_WINDOW( option_dialog_shell ), "Set local options" );


  for (o=options; o->name; ++o) {
    o->p_gui_data = (void *)gtk_check_button_new_with_label (o->description);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG(option_dialog_shell)->vbox), GTK_WIDGET(o->p_gui_data), TRUE, TRUE, 0);
  }

  button = gtk_button_new_with_label( "Close" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->action_area ), button, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( button );
  gtk_signal_connect(GTK_OBJECT(button),"clicked",
      GTK_SIGNAL_FUNC(option_ok_command_callback), NULL);
  gtk_widget_add_accelerator(button, "clicked",
	accel, GDK_Escape, 0, GTK_ACCEL_VISIBLE);

  gtk_widget_show_all( GTK_DIALOG( option_dialog_shell )->vbox );
  gtk_widget_show_all( GTK_DIALOG( option_dialog_shell )->action_area );
}

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  client_option *o;

  create_option_dialog();

  for (o=options; o->name; ++o) {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(o->p_gui_data), *(o->p_value));
  }

/*  gtk_set_relative_position(toplevel, option_dialog_shell, 25, 25);*/
  gtk_widget_show(option_dialog_shell);
  gtk_widget_set_sensitive(toplevel, FALSE);
}

