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
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "log.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "events.h"
#include "fcintl.h"
#include "government.h"
#include "packets.h"
#include "player.h"

/* client */
#include "client_main.h"
#include "options.h"

/* client/gui-gtk-3.0 */
#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "ratesdlg.h"

#include "optiondlg.h"

/******************************************************************/
static GtkWidget *rates_dialog_shell;
static GtkWidget *rates_gov_label;
static GtkWidget *rates_tax_toggle, *rates_lux_toggle, *rates_sci_toggle;
static GtkWidget *rates_tax_label, *rates_lux_label, *rates_sci_label;
static GtkWidget *rates_tax_scale, *rates_lux_scale, *rates_sci_scale;

static gulong     rates_tax_sig, rates_lux_sig, rates_sci_sig;
/******************************************************************/

static int rates_tax_value, rates_lux_value, rates_sci_value;


static void rates_changed_callback(GtkWidget *range);


/**************************************************************************
  Set tax values to display
**************************************************************************/
static void rates_set_values(int tax, int no_tax_scroll, 
			     int lux, int no_lux_scroll,
			     int sci, int no_sci_scroll)
{
  char buf[64];
  int tax_lock, lux_lock, sci_lock;
  int maxrate;
  
  tax_lock	= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rates_tax_toggle));
  lux_lock	= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rates_lux_toggle));
  sci_lock	= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rates_sci_toggle));

  if (NULL != client.conn.playing) {
    maxrate = get_player_bonus(client.conn.playing, EFT_MAX_RATES);
  } else {
    maxrate = 100;
  }
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

  if (tax!=rates_tax_value) {
    fc_snprintf(buf, sizeof(buf), "%3d%%", tax);
    if (strcmp(buf, gtk_label_get_text(GTK_LABEL(rates_tax_label))) != 0)
	gtk_label_set_text(GTK_LABEL(rates_tax_label), buf);
    if(!no_tax_scroll)
    {
	g_signal_handler_block(rates_tax_scale, rates_tax_sig);
	gtk_range_set_value(GTK_RANGE(rates_tax_scale), tax/10 );
	g_signal_handler_unblock(rates_tax_scale, rates_tax_sig);
    }
    rates_tax_value=tax;
  }

  if(lux!=rates_lux_value) {
    fc_snprintf(buf, sizeof(buf), "%3d%%", lux);
    if (strcmp(buf, gtk_label_get_text(GTK_LABEL(rates_lux_label))) != 0)
	gtk_label_set_text(GTK_LABEL(rates_lux_label), buf);
    if(!no_lux_scroll)
    {
	g_signal_handler_block(rates_lux_scale, rates_lux_sig);
	gtk_range_set_value(GTK_RANGE(rates_lux_scale), lux/10 );
	g_signal_handler_unblock(rates_lux_scale, rates_lux_sig);
    }
    rates_lux_value=lux;
  }

  if(sci!=rates_sci_value) {
    fc_snprintf(buf, sizeof(buf), "%3d%%", sci);
    if (strcmp(buf, gtk_label_get_text(GTK_LABEL(rates_sci_label))) != 0)
	gtk_label_set_text(GTK_LABEL(rates_sci_label),buf);
    if(!no_sci_scroll)
    {
	g_signal_handler_block(rates_sci_scale, rates_sci_sig);
	gtk_range_set_value(GTK_RANGE(rates_sci_scale), sci/10 );
	g_signal_handler_unblock(rates_sci_scale, rates_sci_sig);
    }
    rates_sci_value=sci;
  }
}


/**************************************************************************
  User changes rates
**************************************************************************/
static void rates_changed_callback(GtkWidget *range)
{
  int percent=gtk_range_get_value(GTK_RANGE(range));

  if(range==rates_tax_scale) {
    int tax_value;

    tax_value=10*percent;
    tax_value=MIN(tax_value, 100);
    rates_set_values(tax_value,1, rates_lux_value,0, rates_sci_value,0);
  }
  else if(range==rates_lux_scale) {
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
  User has responded to rates dialog
**************************************************************************/
static void rates_command_callback(GtkWidget *w, gint response_id)
{
  if (response_id == GTK_RESPONSE_OK) {
    dsend_packet_player_rates(&client.conn, rates_tax_value, rates_lux_value,
			      rates_sci_value);
  }
  gtk_widget_destroy(rates_dialog_shell);
}


/****************************************************************
  Create rates dialog
*****************************************************************/
static GtkWidget *create_rates_dialog(void)
{
  GtkWidget     *shell, *content;
  GtkWidget	*frame, *hgrid;
  int i;

  if (!can_client_issue_orders()) {
    return NULL;
  }
  
  shell = gtk_dialog_new_with_buttons(_("Select tax, luxury and science rates"),
  	NULL,
	0,
	GTK_STOCK_CANCEL,
	GTK_RESPONSE_CANCEL,
	GTK_STOCK_OK,
	GTK_RESPONSE_OK,
	NULL);
  setup_dialog(shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(shell), GTK_RESPONSE_OK);
  gtk_window_set_position(GTK_WINDOW(shell), GTK_WIN_POS_MOUSE);
  content = gtk_dialog_get_content_area(GTK_DIALOG(shell));

  rates_gov_label = gtk_label_new("");
  gtk_box_pack_start( GTK_BOX( content ), rates_gov_label, TRUE, TRUE, 5 );

  frame = gtk_frame_new( _("Tax") );
  gtk_box_pack_start( GTK_BOX( content ), frame, TRUE, TRUE, 5 );

  hgrid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 10);
  gtk_container_add(GTK_CONTAINER(frame), hgrid);

  rates_tax_scale =
    gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 10, 1);
  gtk_range_set_increments(GTK_RANGE(rates_tax_scale), 1, 1);
  for (i = 0; i <= 10; i++) {
    gtk_scale_add_mark(GTK_SCALE(rates_tax_scale), i, GTK_POS_TOP, NULL);
  }
  gtk_widget_set_size_request(rates_tax_scale, 300, 40);
  gtk_scale_set_digits(GTK_SCALE(rates_tax_scale), 0);
  gtk_scale_set_draw_value(GTK_SCALE(rates_tax_scale), FALSE);
  gtk_container_add(GTK_CONTAINER(hgrid), rates_tax_scale);

  rates_tax_label = gtk_label_new("  0%");
  gtk_container_add(GTK_CONTAINER(hgrid), rates_tax_label);
  gtk_widget_set_size_request(rates_tax_label, 40, -1);

  rates_tax_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_container_add(GTK_CONTAINER(hgrid), rates_tax_toggle);

  frame = gtk_frame_new( _("Luxury") );
  gtk_box_pack_start( GTK_BOX( content ), frame, TRUE, TRUE, 5 );

  hgrid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 10);
  gtk_container_add(GTK_CONTAINER(frame), hgrid);

  rates_lux_scale =
    gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 10, 1);
  gtk_range_set_increments(GTK_RANGE(rates_lux_scale), 1, 1);
  for (i = 0; i <= 10; i++) {
    gtk_scale_add_mark(GTK_SCALE(rates_lux_scale), i, GTK_POS_TOP, NULL);
  }
  gtk_widget_set_size_request(rates_lux_scale, 300, 40);
  gtk_scale_set_digits(GTK_SCALE(rates_lux_scale), 0);
  gtk_scale_set_draw_value(GTK_SCALE(rates_lux_scale), FALSE);
  gtk_container_add(GTK_CONTAINER(hgrid), rates_lux_scale);

  rates_lux_label = gtk_label_new("  0%");
  gtk_container_add(GTK_CONTAINER(hgrid), rates_lux_label);
  gtk_widget_set_size_request(rates_lux_label, 40, -1);

  rates_lux_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_container_add(GTK_CONTAINER(hgrid), rates_lux_toggle);

  frame = gtk_frame_new( _("Science") );
  gtk_box_pack_start( GTK_BOX( content ), frame, TRUE, TRUE, 5 );

  hgrid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(hgrid), 10);
  gtk_container_add(GTK_CONTAINER(frame), hgrid);

  rates_sci_scale =
    gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 10, 1);
  gtk_range_set_increments(GTK_RANGE(rates_sci_scale), 1, 1);
  for (i = 0; i <= 10; i++) {
    gtk_scale_add_mark(GTK_SCALE(rates_sci_scale), i, GTK_POS_TOP, NULL);
  }
  gtk_widget_set_size_request(rates_sci_scale, 300, 40);
  gtk_scale_set_digits(GTK_SCALE(rates_sci_scale), 0);
  gtk_scale_set_draw_value(GTK_SCALE(rates_sci_scale), FALSE);
  gtk_container_add(GTK_CONTAINER(hgrid), rates_sci_scale);

  rates_sci_label = gtk_label_new("  0%");
  gtk_container_add(GTK_CONTAINER(hgrid), rates_sci_label);
  gtk_widget_set_size_request(rates_sci_label, 40, -1);

  rates_sci_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_container_add(GTK_CONTAINER(hgrid), rates_sci_toggle);


  g_signal_connect(shell, "response",
		   G_CALLBACK(rates_command_callback), NULL);
  g_signal_connect(shell, "destroy",
		   G_CALLBACK(gtk_widget_destroyed), &rates_dialog_shell);

  gtk_widget_show_all(content);
  gtk_widget_show_all(gtk_dialog_get_action_area(GTK_DIALOG(shell)));

  rates_tax_value=-1;
  rates_lux_value=-1;
  rates_sci_value=-1;

  rates_tax_sig =
    g_signal_connect_after(rates_tax_scale, "value-changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_lux_sig =
    g_signal_connect_after(rates_lux_scale, "value-changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_sci_sig =
    g_signal_connect_after(rates_sci_scale, "value-changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_set_values(client.conn.playing->economic.tax, 0,
		   client.conn.playing->economic.luxury, 0,
		   client.conn.playing->economic.science, 0);
  return shell;
}


/****************************************************************
  Popup rates dialog
*****************************************************************/
void popup_rates_dialog(void)
{
  char buf[64];

  if (!can_client_issue_orders()) {
    return;
  }

  if (!rates_dialog_shell) {
    rates_dialog_shell = create_rates_dialog();
  }
  if (!rates_dialog_shell) {
    return;
  }

  fc_snprintf(buf, sizeof(buf), _("%s max rate: %d%%"),
      government_name_for_player(client.conn.playing),
      get_player_bonus(client.conn.playing, EFT_MAX_RATES));
  gtk_label_set_text(GTK_LABEL(rates_gov_label), buf);
  gtk_range_set_fill_level(GTK_RANGE(rates_tax_scale),
                           get_player_bonus(client.conn.playing,
                                            EFT_MAX_RATES)/10);
  gtk_range_set_fill_level(GTK_RANGE(rates_lux_scale),
                           get_player_bonus(client.conn.playing,
                                            EFT_MAX_RATES)/10);
  gtk_range_set_fill_level(GTK_RANGE(rates_sci_scale),
                           get_player_bonus(client.conn.playing,
                                            EFT_MAX_RATES)/10);

  gtk_window_present(GTK_WINDOW(rates_dialog_shell));
}
