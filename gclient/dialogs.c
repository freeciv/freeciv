#include <ctype.h>
#include <strings.h>

#include <gtk/gtk.h>

#include <packets.h>
#include <player.h>
#include <clinet.h>
#include <main.h>

extern GtkWidget *toplevel;
extern struct connection aconnection;

/******************************************************************/
GtkWidget	*races_dialog_shell,
		*races_toggles_form;
GtkWidget	*races_ok_command;		/* ok button */
GtkWidget	*races_toggles		[14],	/* toggle race */
		*races_name;			/* leader name */


int races_buttons_get_current(void);

void create_races_dialog	(void);
void races_buttons_callback	( GtkWidget *w, gpointer data );
void races_toggles_callback	( GtkWidget *w, gpointer data );

int race_selected;


int unit_select_ids[100];
int unit_select_no;

int is_showing_government_dialog;

int caravan_city_id;
int caravan_unit_id;

int diplomat_id;
int diplomat_target_id;

struct city *pcity_caravan_dest;
struct unit *punit_caravan;



/****************************************************************
...
*****************************************************************/
void notify_command_callback(GtkWidget *w, GtkWidget *t)
{
  gtk_widget_destroy( t );
  gtk_widget_set_sensitive( toplevel, TRUE );
}



/****************************************************************
...
*****************************************************************/
void popup_notify_dialog(char *headline, char *lines)
{
  GtkWidget *notify_dialog_shell, *notify_command;
  GtkWidget *notify_label, *notify_headline;
  static GtkStyle   text_style;
  
  notify_dialog_shell = gtk_dialog_new();
  
  gtk_window_set_title( GTK_WINDOW( notify_dialog_shell ), "Notify dialog" );
  
  gtk_container_border_width( GTK_CONTAINER(GTK_DIALOG(notify_dialog_shell)->vbox), 5 );

  notify_headline = gtk_label_new( headline);   
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_headline, TRUE, TRUE, 0 );


  text_style = *gtk_widget_get_style( notify_headline );
  text_style.font = gdk_font_load( "-*-courier-medium-r-*-*-12-*-*-*-*-*-*-*" );
  gtk_widget_set_style( notify_headline, &text_style );
  gtk_label_set_justify( GTK_LABEL( notify_headline ), GTK_JUSTIFY_LEFT );

  notify_label = gtk_label_new( lines );   
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->vbox ),
	notify_label, TRUE, TRUE, 0 );


  gtk_widget_set_style( notify_label, &text_style );
  gtk_label_set_justify( GTK_LABEL( notify_label ), GTK_JUSTIFY_LEFT );

  notify_command = gtk_button_new_with_label( "Close" );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG(notify_dialog_shell)->action_area ),
	notify_command, TRUE, TRUE, 0 );
  GTK_WIDGET_SET_FLAGS( notify_command, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( notify_command );

  gtk_signal_connect( GTK_OBJECT( notify_command ), "clicked",
	GTK_SIGNAL_FUNC( notify_command_callback ), notify_dialog_shell );

  
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(notify_dialog_shell)->action_area );
  gtk_widget_show( notify_dialog_shell );

  gtk_widget_set_sensitive( toplevel, FALSE );
}

/****************************************************************
popup the dialog 10% inside the main-window 
*****************************************************************/
void popup_races_dialog(void)
{
  gtk_widget_set_sensitive( toplevel, FALSE );

  create_races_dialog();

  gtk_widget_show(races_dialog_shell);
}

/****************************************************************
...
*****************************************************************/
void popdown_races_dialog(void)
{
  gtk_widget_set_sensitive( toplevel, TRUE );
  gtk_widget_destroy(races_dialog_shell);
}


/****************************************************************
...
*****************************************************************/
void create_races_dialog(void)
{
  int     i;
  GSList *group = NULL;

  races_dialog_shell = gtk_dialog_new();
  
  gtk_window_set_title( GTK_WINDOW( races_dialog_shell ), "Select race and name" );
  races_toggles_form = gtk_table_new( 2, 7, FALSE );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	races_toggles_form, FALSE, FALSE, 0 );

  for(i=0; i<R_LAST; i++) {

    races_toggles[i]= gtk_radio_button_new_with_label( group, get_race_name(i) );

    gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_toggles[i] ), FALSE );

    group = gtk_radio_button_group( GTK_RADIO_BUTTON( races_toggles[i] ) );

    gtk_table_attach_defaults( GTK_TABLE(races_toggles_form), races_toggles[i],
			i%2,i%2+1,i/2,i/2+1 );
  }


  races_name = gtk_entry_new();

  gtk_entry_set_text(GTK_ENTRY(races_name), default_race_leader_names[0]);

  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->vbox ),
	races_name, FALSE, FALSE, 0 );
  GTK_WIDGET_SET_FLAGS( races_name, GTK_CAN_DEFAULT );
  gtk_widget_grab_default( races_name );

  races_ok_command = gtk_button_new_with_label( "Ok" );
  GTK_WIDGET_SET_FLAGS( races_ok_command, GTK_CAN_DEFAULT );
  gtk_box_pack_start( GTK_BOX( GTK_DIALOG( races_dialog_shell )->action_area ),
	races_ok_command, TRUE, TRUE, 0 );

  for(i=0; i<14; i++)
	gtk_signal_connect( GTK_OBJECT( races_toggles[i] ), "toggled",
	    GTK_SIGNAL_FUNC( races_toggles_callback ), NULL );

  gtk_toggle_button_set_state( GTK_TOGGLE_BUTTON( races_toggles[0] ), TRUE );

  gtk_signal_connect( GTK_OBJECT( races_ok_command ), "clicked",
			GTK_SIGNAL_FUNC( races_buttons_callback ), NULL );

  gtk_widget_show_all( GTK_DIALOG(races_dialog_shell)->vbox );
  gtk_widget_show_all( GTK_DIALOG(races_dialog_shell)->action_area );
}

/****************************************************************
...
*****************************************************************/
/*
void races_dialog_returnkey(Widget w, XEvent *event, String *params,
			    Cardinal *num_params)
{
  x_simulate_button_click(XtNameToWidget(XtParent(w), "racesokcommand"));
}
*/

/**************************************************************************
...
**************************************************************************/
void races_toggles_set_sensitive(int bits)
{
  int i, selected, mybits;

  mybits=bits;

  for(i=0; i<R_LAST; i++) {
    if(mybits&1)
      gtk_widget_set_sensitive( races_toggles[i], FALSE );
    else
      gtk_widget_set_sensitive( races_toggles[i], TRUE );
    mybits>>=1;
  }

  if((selected=races_buttons_get_current())==-1)
     return;
}



/**************************************************************************
...
**************************************************************************/
void races_toggles_callback( GtkWidget *w, gpointer data )
{
  int i;

  for(i=0; i<R_LAST; i++)
    if(w==races_toggles[i]) {
      gtk_entry_set_text(GTK_ENTRY(races_name), default_race_leader_names[i]);

      race_selected = i;
      return;
    }
}

/**************************************************************************
...
**************************************************************************/
int races_buttons_get_current(void)
{
  return race_selected;
}


/**************************************************************************
...
**************************************************************************/
void races_buttons_callback( GtkWidget *w, gpointer data )
{
  int selected;
  char *s;

  struct packet_alloc_race packet;

  if((selected=races_buttons_get_current())==-1) {
    append_output_window("You must select a race.");
    return;
  }

  s = gtk_entry_get_text(GTK_ENTRY(races_name));

  /* perform a minimum of sanity test on the name */
  packet.race_no=selected;
  strncpy(packet.name, (char*)s, NAME_SIZE);
  packet.name[NAME_SIZE-1]='\0';
  
  if(!get_sane_name(packet.name)) {
    append_output_window("You must type a legal name.");
    return;
  }

  packet.name[0]=toupper(packet.name[0]);

  send_packet_alloc_race(&aconnection, &packet);
}

/**************************************************************************
  Destroys its widget.  Usefull for a popdown callback on pop-ups that
  won't get resused.
**************************************************************************/
void destroy_me_callback( GtkWidget *w, gpointer data)
{
  gtk_widget_destroy(w);
}
