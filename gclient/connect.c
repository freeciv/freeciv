#include <stdio.h>
#include <stdlib.h>

#include <clinet.h>
#include <chatline.h>

#include <gtk/gtk.h>

extern char name[];
extern char server_host[];
extern int  server_port;

GtkWidget *iname, *ihost, *iport;

static GtkWidget *dialog;


void connect_callback( GtkWidget *widget, gpointer data )
{
    char errbuf	[512];

    strcpy( name, gtk_entry_get_text( GTK_ENTRY( iname ) ) );
    strcpy( server_host, gtk_entry_get_text( GTK_ENTRY( ihost ) ) );
    server_port = atoi( gtk_entry_get_text( GTK_ENTRY( iport ) ) );
    
    if ( connect_to_server( name, server_host, server_port, errbuf ) != -1 )
    {
	gtk_widget_destroy( dialog );
	/*XtSetSensitive(toplevel, True);*/
	return;
    }
    
    append_output_window( errbuf );
    return;
}

void gui_server_connect( void )
{
    GtkWidget *button;
    GtkWidget *frame;
    char       buf		[256];

    dialog = gtk_dialog_new();
    
    gtk_window_set_title( GTK_WINDOW( dialog ), "Freeciv Server Selection" );

    frame = gtk_frame_new( "Name" );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( dialog )->vbox ), frame, TRUE, TRUE, 0 );

    iname = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY( iname ), name );
    gtk_container_add( GTK_CONTAINER( frame ), iname );

    frame = gtk_frame_new( "Host" );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( dialog )->vbox ), frame, TRUE, TRUE, 0 );

    ihost = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY( ihost ), server_host );
    gtk_container_add( GTK_CONTAINER( frame ), ihost );

    frame = gtk_frame_new( "Port" );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( dialog )->vbox ), frame, TRUE, TRUE, 0 );

    sprintf( buf, "%d", server_port );

    iport = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY( iport ), buf );
    gtk_container_add( GTK_CONTAINER( frame ), iport );

    button = gtk_button_new_with_label( "Connect" );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( dialog )->action_area ), button, TRUE, TRUE, 0 );
    GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
    gtk_widget_grab_default( button );

    gtk_signal_connect( GTK_OBJECT( button ), "clicked",
			GTK_SIGNAL_FUNC( connect_callback ), NULL );

    button = gtk_button_new_with_label( "Quit" );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( dialog )->action_area ), button, TRUE, TRUE, 0 );
    GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );

    gtk_signal_connect( GTK_OBJECT( button ), "clicked",
			GTK_SIGNAL_FUNC( gtk_main_quit ), NULL );

    gtk_widget_show_all( GTK_DIALOG( dialog )->vbox );
    gtk_widget_show_all( GTK_DIALOG( dialog )->action_area );

    gtk_widget_show( dialog );
    return;
}
