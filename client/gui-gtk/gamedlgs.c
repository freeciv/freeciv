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

#include <game.h>
#include <gui_stuff.h>
#include <events.h>
#include <player.h>
#include <shared.h>
#include <packets.h>
#include <dialogs.h>
#include <chatline.h>
#include <messagedlg.h>
#include <cityrep.h>
#include <log.h>

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
void rates_set_values(int tax, int no_tax_scroll, 
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
void rates_ok_command_callback(GtkWidget *widget, gpointer data)
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
void rates_cancel_command_callback(GtkWidget *widget, gpointer data)
{
  gtk_widget_set_sensitive(toplevel, TRUE);
  gtk_widget_destroy(rates_dialog_shell);
}



/****************************************************************
... 
*****************************************************************/
void create_rates_dialog( void )
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







/******************************************************************/
extern int use_solid_color_behind_units;
extern int sound_bell_at_new_turn;
extern int smooth_move_units;
extern int flags_are_transparent;
extern int ai_popup_windows;
extern int ai_manual_turn_done;
extern int auto_center_on_unit;
extern int wakeup_focus;
extern int draw_diagonal_roads;
extern int center_when_popup_city;

/******************************************************************/

/* definitions for options save/restore */

typedef struct {
  int *var;
  char *name;
} opt_def;

#define GEN_OPT(var) { &var, #var }

static opt_def opts[]= {
  GEN_OPT(use_solid_color_behind_units),
  GEN_OPT(sound_bell_at_new_turn),
  GEN_OPT(smooth_move_units),
  GEN_OPT(flags_are_transparent),
  GEN_OPT(ai_popup_windows),
  GEN_OPT(ai_manual_turn_done),
  GEN_OPT(auto_center_on_unit),
  GEN_OPT(wakeup_focus),
  GEN_OPT(draw_diagonal_roads),
  GEN_OPT(center_when_popup_city),
  { NULL, NULL }
};

/******************************************************************/
GtkWidget *option_dialog_shell;
GtkWidget *option_bg_toggle;
GtkWidget *option_bell_toggle;
GtkWidget *option_move_toggle;
GtkWidget *option_flag_toggle;
GtkWidget *option_aipopup_toggle;
GtkWidget *option_aiturndone_toggle;
GtkWidget *option_autocenter_toggle;
GtkWidget *option_wakeup_focus_toggle;
GtkWidget *option_diagonal_roads_toggle;
GtkWidget *option_cenpop_toggle;

/**************************************************************************
...
**************************************************************************/
void option_ok_command_callback(GtkWidget *widget, gpointer data)
{
  gtk_widget_set_sensitive(toplevel, TRUE);

  use_solid_color_behind_units = GTK_TOGGLE_BUTTON(option_bg_toggle)->active;
  sound_bell_at_new_turn = GTK_TOGGLE_BUTTON(option_bell_toggle)->active;
  smooth_move_units = GTK_TOGGLE_BUTTON(option_move_toggle)->active;
  flags_are_transparent = GTK_TOGGLE_BUTTON(option_flag_toggle)->active;
  ai_popup_windows = GTK_TOGGLE_BUTTON(option_aipopup_toggle)->active;
  ai_manual_turn_done = GTK_TOGGLE_BUTTON(option_aiturndone_toggle)->active;
  auto_center_on_unit = GTK_TOGGLE_BUTTON(option_autocenter_toggle)->active;
  wakeup_focus = GTK_TOGGLE_BUTTON(option_wakeup_focus_toggle)->active;
  draw_diagonal_roads = GTK_TOGGLE_BUTTON(option_diagonal_roads_toggle)->active;
  center_when_popup_city = GTK_TOGGLE_BUTTON(option_cenpop_toggle)->active;

  gtk_widget_destroy(option_dialog_shell);
}

/****************************************************************
 The "options" file handles actual "options", and also message
 options, and city report settings
*****************************************************************/

/****************************************************************
...								
*****************************************************************/
FILE *open_option_file(char *mode)
{
  char name_buffer[256];
  char output_buffer[256];
  char *name;
  FILE *f;

  name = getenv("FREECIV_OPT");

  if (!name) {
    name = getenv("HOME");
    if (!name) {
      append_output_window("Cannot find your home directory");
      return NULL;
    }
    strncpy(name_buffer, name, 230);
    name_buffer[230] = '\0';
    strcat(name_buffer, "/.civclientrc");
    name = name_buffer;
  }
  
  freelog(LOG_DEBUG, "settings file is %s", name);

  f = fopen(name, mode);

  if(mode[0]=='w') {
    if (f) {
      sprintf(output_buffer, "Settings file is ");
      strncat(output_buffer, name, 255-strlen(output_buffer));
    } else {
      sprintf(output_buffer, "Cannot write to file ");
      strncat(output_buffer, name, 255-strlen(output_buffer));
    }
    output_buffer[255] = '\0';
    append_output_window(output_buffer);
  }
  
  return f;
}

/****************************************************************
... 
*****************************************************************/
void load_options(void)
{
  char buffer[256];
  char orig_buffer[256];
  char *s;
  FILE *option_file;
  opt_def *o;
  int val, ind;

  option_file = open_option_file("r");
  if (option_file==NULL) {
    /* fail silently */
    return;
  }

  while (fgets(buffer,255,option_file)) {
    buffer[255] = '\0';
    strcpy(orig_buffer, buffer);    /* save original for error messages */
    
    /* handle comments */
    if ((s = strstr(buffer, "#"))) {
      *s = '\0';
    }

    /* skip blank lines */
    for (s=buffer; *s && isspace(*s); s++) ;
    if(!*s) continue;

    /* ignore [client] header */
    if (*s == '[') continue;

    /* parse value */
    s = strstr(buffer, "=");
    if (s == NULL || sscanf(s+1, "%d", &val) != 1) {
      append_output_window("Parse error while loading option file: input is:");
      append_output_window(orig_buffer);
      continue;
    }

    /* parse variable names */
    if ((s = strstr(buffer, "message_where_"))) {
      if (sscanf(s+14, "%d", &ind) == 1) {
       messages_where[ind] = val;
       goto next_line;
      }
    }

    for (o=opts; o->name; o++) {
      if (strstr(buffer, o->name)) {
       *(o->var) = val;
       goto next_line;
      }
    }
    
    if ((s = strstr(buffer, "city_report_"))) {
      s += 12;
      for (ind=1; ind<num_city_report_spec(); ind++) {
       if (strstr(s, city_report_spec_tagname(ind))) {
	 *(city_report_spec_show_ptr(ind)) = val;
	 goto next_line;
       }
      }
    }
    
    append_output_window("Unknown variable found in option file: input is:");
    append_output_window(orig_buffer);

  next_line:
    {} /* placate Solaris cc/xmkmf/makedepend */
  }

  fclose(option_file);
}

/****************************************************************
... 
*****************************************************************/
void save_options(void)
{
  FILE *option_file;
  opt_def* o;
  int i;

  option_file = open_option_file("w");
  if (option_file==NULL) {
    append_output_window("Cannot save settings.");
    return;
  }

  fprintf(option_file, "# settings file for freeciv client version %s\n#\n",
	 VERSION_STRING);
  
  fprintf(option_file, "[client]\n");

  for (o=opts; o->name; o++) {
    fprintf(option_file, "%s = %d\n", o->name, *(o->var));
  }
  for (i=0; i<E_LAST; i++) {
    fprintf(option_file, "message_where_%2.2d = %d  # %s\n",
	   i, messages_where[i], message_text[i]);
  }
  for (i=1; i<num_city_report_spec(); i++) {
    fprintf(option_file, "city_report_%s = %d\n",
	   city_report_spec_tagname(i), *(city_report_spec_show_ptr(i)));
  }

  fclose(option_file);
  
  append_output_window("Saved settings.");
}


/****************************************************************
... 
*****************************************************************/
void create_option_dialog(void)
{
  GtkWidget *button;
  GtkAccelGroup *accel=gtk_accel_group_new();

  option_dialog_shell = gtk_dialog_new();
  gtk_signal_connect( GTK_OBJECT(option_dialog_shell),"delete_event",
      GTK_SIGNAL_FUNC(deleted_callback),NULL );
  gtk_window_set_position (GTK_WINDOW(option_dialog_shell), GTK_WIN_POS_MOUSE);
  gtk_accel_group_attach(accel, GTK_OBJECT(option_dialog_shell));

  gtk_window_set_title( GTK_WINDOW( option_dialog_shell ), "Set local options" );

  option_bg_toggle = gtk_check_button_new_with_label( "Solid unit background color" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_bg_toggle, TRUE, TRUE, 0 );
  option_bell_toggle = gtk_check_button_new_with_label( "Sound bell at new turn" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_bell_toggle, TRUE, TRUE, 0 );
  option_move_toggle = gtk_check_button_new_with_label( "Smooth unit moves" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_move_toggle, TRUE, TRUE, 0 );
  option_flag_toggle = gtk_check_button_new_with_label( "Flags are transparent" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_flag_toggle, TRUE, TRUE, 0 );

  option_aipopup_toggle = gtk_check_button_new_with_label( "Popup dialogs in AI Mode" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_aipopup_toggle, TRUE, TRUE, 0 );
  option_aiturndone_toggle = gtk_check_button_new_with_label( "Manual Turn Done in AI Mode" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_aiturndone_toggle, TRUE, TRUE, 0 );
  option_autocenter_toggle = gtk_check_button_new_with_label( "Auto Center on Units" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_autocenter_toggle, TRUE, TRUE, 0 );
  option_wakeup_focus_toggle = gtk_check_button_new_with_label( "Focus on Awakened Units" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_wakeup_focus_toggle, TRUE, TRUE, 0 );
  option_diagonal_roads_toggle = gtk_check_button_new_with_label( "Draw Diagonal Roads/Rails" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_diagonal_roads_toggle, TRUE, TRUE, 0 );
  option_cenpop_toggle = gtk_check_button_new_with_label( "Center map when Popup city" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( option_dialog_shell )->vbox ), option_cenpop_toggle, TRUE, TRUE, 0 );

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
  create_option_dialog();

  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_bg_toggle),
	use_solid_color_behind_units);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_bell_toggle),
	sound_bell_at_new_turn);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_move_toggle),
	smooth_move_units);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_flag_toggle),
	flags_are_transparent);
  
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_aipopup_toggle),
	ai_popup_windows);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_aiturndone_toggle),
	ai_manual_turn_done);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_autocenter_toggle),
	auto_center_on_unit);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_wakeup_focus_toggle),
	wakeup_focus);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_diagonal_roads_toggle),
	draw_diagonal_roads);
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(option_cenpop_toggle),
	center_when_popup_city);
/*  gtk_set_relative_position(toplevel, option_dialog_shell, 25, 25);*/
  gtk_widget_show(option_dialog_shell);
  gtk_widget_set_sensitive(toplevel, FALSE);
}
