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
#include <gdk/gdkkeysyms.h>

#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "chatline.h"
#include "cityrep.h"
#include "dialogs.h"
#include "clinet.h"
#include "gui_main.h"
#include "gui_stuff.h"
#include "options.h"

#include "ratesdlg.h"
#include "optiondlg.h"

/******************************************************************/
static GtkWidget *rates_dialog_shell;
static GtkWidget *rates_gov_label;
static GtkWidget *rates_tax_toggle, *rates_lux_toggle, *rates_sci_toggle;
static GtkWidget *rates_tax_label, *rates_lux_label, *rates_sci_label;
static GtkObject *rates_tax_adj, *rates_lux_adj, *rates_sci_adj;

static gulong     rates_tax_sig, rates_lux_sig, rates_sci_sig;
/******************************************************************/

static int rates_tax_value, rates_lux_value, rates_sci_value;


static void rates_changed_callback(GtkAdjustment *adj);


/**************************************************************************
...
**************************************************************************/
static void rates_set_values(int tax, int no_tax_scroll, 
			     int lux, int no_lux_scroll,
			     int sci, int no_sci_scroll)
{
  char buf[64];
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
    my_snprintf(buf, sizeof(buf), "%3d%%", tax);
    if (strcmp(buf, GTK_LABEL(rates_tax_label)->label) != 0)
	gtk_label_set_text(GTK_LABEL(rates_tax_label), buf);
    if(!no_tax_scroll)
    {
	g_signal_handler_block(rates_tax_adj, rates_tax_sig);
	gtk_adjustment_set_value(GTK_ADJUSTMENT(rates_tax_adj), tax/10 );
	g_signal_handler_unblock(rates_tax_adj, rates_tax_sig);
    }
    rates_tax_value=tax;
  }

  if(lux!=rates_lux_value) {
    my_snprintf(buf, sizeof(buf), "%3d%%", lux);
    if (strcmp(buf, GTK_LABEL(rates_lux_label)->label) != 0)
	gtk_label_set_text(GTK_LABEL(rates_lux_label), buf);
    if(!no_lux_scroll)
    {
	g_signal_handler_block(rates_lux_adj, rates_lux_sig);
	gtk_adjustment_set_value(GTK_ADJUSTMENT(rates_lux_adj), lux/10 );
	g_signal_handler_unblock(rates_lux_adj, rates_lux_sig);
    }
    rates_lux_value=lux;
  }

  if(sci!=rates_sci_value) {
    my_snprintf(buf, sizeof(buf), "%3d%%", sci);
    if (strcmp(buf, GTK_LABEL(rates_sci_label)->label) != 0)
	gtk_label_set_text(GTK_LABEL(rates_sci_label),buf);
    if(!no_sci_scroll)
    {
	g_signal_handler_block(rates_sci_adj, rates_sci_sig);
	gtk_adjustment_set_value(GTK_ADJUSTMENT(rates_sci_adj), sci/10 );
	g_signal_handler_unblock(rates_sci_adj, rates_sci_sig);
    }
    rates_sci_value=sci;
  }
}


/**************************************************************************
...
**************************************************************************/
static void rates_changed_callback(GtkAdjustment *adj)
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
static void rates_command_callback(GtkWidget *w, gint response_id)
{
  if (response_id == GTK_RESPONSE_OK) {
    dsend_packet_player_rates(&aconnection, rates_tax_value, rates_lux_value,
			      rates_sci_value);
  }
  gtk_widget_destroy(rates_dialog_shell);
}


/**************************************************************************
...
**************************************************************************/
static void rates_destroy_callback(GtkWidget *widget, gpointer data)
{
  rates_dialog_shell = NULL;
}



/****************************************************************
... 
*****************************************************************/
static GtkWidget *create_rates_dialog(void)
{
  GtkWidget     *shell;
  GtkWidget	*frame, *hbox;

  GtkWidget	*scale;
  
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

  rates_gov_label = gtk_label_new("");
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( shell )->vbox ), rates_gov_label, TRUE, TRUE, 5 );

  frame = gtk_frame_new( _("Tax") );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_tax_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_tax_adj ) );
  gtk_widget_set_size_request(scale, 300, 40);
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_tax_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_tax_label, TRUE, TRUE, 0 );
  gtk_widget_set_size_request(rates_tax_label, 40, -1);

  rates_tax_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_tax_toggle, TRUE, TRUE, 0 );

  frame = gtk_frame_new( _("Luxury") );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_lux_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_lux_adj ) );
  gtk_widget_set_size_request(scale, 300, 40);
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_lux_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_lux_label, TRUE, TRUE, 0 );
  gtk_widget_set_size_request(rates_lux_label, 40, -1);

  rates_lux_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_lux_toggle, TRUE, TRUE, 0 );

  frame = gtk_frame_new( _("Science") );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( shell )->vbox ), frame, TRUE, TRUE, 5 );

  hbox = gtk_hbox_new( FALSE, 10 );
  gtk_container_add( GTK_CONTAINER( frame ), hbox );

  rates_sci_adj = gtk_adjustment_new( 0.0, 0.0, 11.0, 1.0, 1.0, 1.0 );
  scale = gtk_hscale_new( GTK_ADJUSTMENT( rates_sci_adj ) );
  gtk_widget_set_size_request(scale, 300, 40);
  gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
  gtk_scale_set_draw_value( GTK_SCALE( scale ), FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

  rates_sci_label = gtk_label_new("  0%");
  gtk_box_pack_start( GTK_BOX( hbox ), rates_sci_label, TRUE, TRUE, 0 );
  gtk_widget_set_size_request(rates_sci_label, 40, -1);

  rates_sci_toggle = gtk_check_button_new_with_label( _("Lock") );
  gtk_box_pack_start( GTK_BOX( hbox ), rates_sci_toggle, TRUE, TRUE, 0 );


  g_signal_connect(shell, "response",
		   G_CALLBACK(rates_command_callback), NULL);
  g_signal_connect(shell, "destroy",
		   G_CALLBACK(rates_destroy_callback), NULL);

  gtk_widget_show_all( GTK_DIALOG( shell )->vbox );
  gtk_widget_show_all( GTK_DIALOG( shell )->action_area );

  rates_tax_value=-1;
  rates_lux_value=-1;
  rates_sci_value=-1;

  rates_tax_sig =
    g_signal_connect_after(rates_tax_adj, "value_changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_lux_sig =
    g_signal_connect_after(rates_lux_adj, "value_changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_sci_sig =
    g_signal_connect_after(rates_sci_adj, "value_changed",
			   G_CALLBACK(rates_changed_callback), NULL);

  rates_set_values( game.player_ptr->economic.tax, 0,
        	    game.player_ptr->economic.luxury, 0,
        	    game.player_ptr->economic.science, 0 );
  return shell;
}




/****************************************************************
... 
*****************************************************************/
void popup_rates_dialog(void)
{
  char buf[64];

  if (!rates_dialog_shell)
    rates_dialog_shell = create_rates_dialog();

  my_snprintf(buf, sizeof(buf), _("%s max rate: %d%%"),
      get_government_name(game.player_ptr->government),
      get_government_max_rate(game.player_ptr->government));
  gtk_label_set_text(GTK_LABEL(rates_gov_label), buf);
  
  gtk_window_present(GTK_WINDOW(rates_dialog_shell));
}

/**************************************************************************
  Option dialog 
**************************************************************************/

static GtkWidget *option_dialog_shell;

/**************************************************************************
...
**************************************************************************/
static void option_ok_command_callback(GtkWidget *widget, gpointer data)
{
  const char *dp;
  bool b;
  int val;

  client_options_iterate(o) {
    switch (o->type) {
    case COT_BOOL:
      b = *(o->p_bool_value);
      *(o->p_bool_value) = GTK_TOGGLE_BUTTON(o->p_gui_data)->active;
      if (b != *(o->p_bool_value) && o->change_callback) {
	(o->change_callback)(o);
      }
      break;
    case COT_INT:
      val = *(o->p_int_value);
      dp = gtk_entry_get_text(GTK_ENTRY(o->p_gui_data));
      sscanf(dp, "%d", o->p_int_value);
      if (val != *(o->p_int_value) && o->change_callback) {
	(o->change_callback)(o);
      }
      break;
    case COT_STR:
      if (o->p_string_vals) {
	const char* new_value = gtk_entry_get_text(GTK_ENTRY
					(GTK_COMBO(o->p_gui_data)->entry));
	if (strcmp(o->p_string_value, new_value)) {
	  mystrlcpy(o->p_string_value, new_value, o->string_length);
	  if (o->change_callback) {
	    (o->change_callback)(o);
	  }
	}
      } else {
	mystrlcpy(o->p_string_value,
		  gtk_entry_get_text(GTK_ENTRY(o->p_gui_data)),
		  o->string_length);
      }
      break;
    }
  } client_options_iterate_end;

  if (map_scrollbars) {
    gtk_widget_show(map_horizontal_scrollbar);
    gtk_widget_show(map_vertical_scrollbar);
  } else {
    gtk_widget_hide(map_horizontal_scrollbar);
    gtk_widget_hide(map_vertical_scrollbar);
  }
  if (fullscreen_mode) {
    gtk_window_fullscreen(GTK_WINDOW(toplevel));
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(toplevel));
  }
  option_dialog_shell = NULL;
}


/****************************************************************
... 
*****************************************************************/
static void create_option_dialog(void)
{
  GtkWidget *label, *table;
  int i;

  option_dialog_shell = gtk_dialog_new_with_buttons(_("Set local options"),
  	NULL,
	0,
	GTK_STOCK_CLOSE,
	GTK_RESPONSE_CLOSE,
	NULL);
  setup_dialog(option_dialog_shell, toplevel);
  gtk_dialog_set_default_response(GTK_DIALOG(option_dialog_shell),
				  GTK_RESPONSE_CLOSE);
  g_signal_connect(option_dialog_shell, "response",
		   G_CALLBACK(gtk_widget_destroy), NULL);
  g_signal_connect(option_dialog_shell, "destroy",
		   G_CALLBACK(option_ok_command_callback), NULL);
  gtk_window_set_position (GTK_WINDOW(option_dialog_shell), GTK_WIN_POS_MOUSE);

  table = gtk_table_new(num_options, 2, FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(option_dialog_shell)->vbox),
		     table, FALSE, FALSE, 0);

  i = 0;
  client_options_iterate(o) {
    switch (o->type) {
    case COT_BOOL:
      label = gtk_label_new(_(o->description));
      gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
      gtk_table_attach(GTK_TABLE(table), label, 0, 1, i, i+1,
		       GTK_FILL, GTK_FILL | GTK_EXPAND,
		       0, 0);
      o->p_gui_data = (void *)gtk_check_button_new();
      gtk_table_attach(GTK_TABLE(table), o->p_gui_data, 1, 2, i, i+1,
		       GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		       0, 0);
      break;
    case COT_INT:
      label = gtk_label_new(_(o->description));
      gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
      gtk_table_attach(GTK_TABLE(table), label, 0, 1, i, i+1,
		       GTK_FILL, GTK_FILL | GTK_EXPAND,
		       0, 0);
      o->p_gui_data = gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(o->p_gui_data), 5);
      gtk_widget_set_size_request(GTK_WIDGET(o->p_gui_data), 45, -1);
      gtk_table_attach(GTK_TABLE(table), o->p_gui_data, 1, 2, i, i+1,
		       GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		       0, 0);
      break;
    case COT_STR:
      label = gtk_label_new(_(o->description));
      gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
      gtk_table_attach(GTK_TABLE(table), label, 0, 1, i, i+1,
		       GTK_FILL, GTK_FILL | GTK_EXPAND,
		       0, 0);
      if (o->p_string_vals) {
        o->p_gui_data = gtk_combo_new();
      } else {
        o->p_gui_data = gtk_entry_new();
      }
      gtk_widget_set_size_request(GTK_WIDGET(o->p_gui_data), 150, -1);
      gtk_table_attach(GTK_TABLE(table), o->p_gui_data, 1, 2, i, i+1,
		       GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		       0, 0);
      break;
    }
    i++;
  } client_options_iterate_end;

  gtk_widget_show_all( GTK_DIALOG( option_dialog_shell )->vbox );
}

/****************************************************************
... 
*****************************************************************/
void popup_option_dialog(void)
{
  char valstr[64];

  if (!option_dialog_shell) {
    create_option_dialog();
  }

  client_options_iterate(o) {
    switch (o->type) {
    case COT_BOOL:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(o->p_gui_data),
				   *(o->p_bool_value));
      break;
    case COT_INT:
      my_snprintf(valstr, sizeof(valstr), "%d", *(o->p_int_value));
      gtk_entry_set_text(GTK_ENTRY(o->p_gui_data), valstr);
      break;
    case COT_STR:
      if (o->p_string_vals) {
	int i;
	GList *items = NULL;
	const char **vals = (*o->p_string_vals) ();

	for (i = 0; vals[i]; i++) {
	  if (strcmp(vals[i], o->p_string_value) == 0) {
	    continue;
	  }
	  items = g_list_append(items, (gpointer) vals[i]);
	}
	items = g_list_prepend(items, (gpointer) o->p_string_value);
	gtk_combo_set_popdown_strings(GTK_COMBO(o->p_gui_data), items);
      } else {
	gtk_entry_set_text(GTK_ENTRY(o->p_gui_data), o->p_string_value);
      }
      break;
    }
  } client_options_iterate_end;

  gtk_widget_show(option_dialog_shell);
}
