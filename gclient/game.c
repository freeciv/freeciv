#include <gtk/gtk.h>

void game_rates( GtkWidget *widget, gpointer data )
{
    GtkWidget     *window;

    GtkWidget     *vbox;
    GtkWidget     *hbox;
    GtkWidget     *frame;

    GtkWidget     *scale;
    GtkWidget     *button;
    GtkObject     *adjustment;
    
    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );

    gtk_window_set_title( GTK_WINDOW( window ), "Select tax, luxury and science rates" );

    vbox = gtk_vbox_new( FALSE, 5 );
    gtk_container_add( GTK_CONTAINER( window ), vbox );

    frame = gtk_frame_new( "Tax (%)" );
    gtk_box_pack_start( GTK_BOX( vbox ), frame, TRUE, TRUE, 5 );

    hbox = gtk_hbox_new( FALSE, 10 );
    gtk_container_add( GTK_CONTAINER( frame ), hbox );

    adjustment = gtk_adjustment_new( 0, 0, 100, 1, 10, 0 );
    scale = gtk_hscale_new( GTK_ADJUSTMENT( adjustment ) );
    gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
    gtk_range_set_update_policy( GTK_RANGE( scale ), GTK_UPDATE_DELAYED );
    gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
    gtk_scale_set_draw_value( GTK_SCALE( scale ), TRUE );
    gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

    button = gtk_check_button_new_with_label( "Lock" );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );

    frame = gtk_frame_new( "Luxury (%)" );
    gtk_box_pack_start( GTK_BOX( vbox ), frame, TRUE, TRUE, 5 );

    hbox = gtk_hbox_new( FALSE, 10 );
    gtk_container_add( GTK_CONTAINER( frame ), hbox );

    adjustment = gtk_adjustment_new( 0, 0, 100, 1, 10, 0 );
    scale = gtk_hscale_new( GTK_ADJUSTMENT( adjustment ) );
    gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
    gtk_range_set_update_policy( GTK_RANGE( scale ), GTK_UPDATE_DELAYED );
    gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
    gtk_scale_set_draw_value( GTK_SCALE( scale ), TRUE );
    gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

    button = gtk_check_button_new_with_label( "Lock" );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );

    frame = gtk_frame_new( "Science (%)" );
    gtk_box_pack_start( GTK_BOX( vbox ), frame, TRUE, TRUE, 5 );

    hbox = gtk_hbox_new( FALSE, 10 );
    gtk_container_add( GTK_CONTAINER( frame ), hbox );

    adjustment = gtk_adjustment_new( 0, 0, 100, 1, 10, 0 );
    scale = gtk_hscale_new( GTK_ADJUSTMENT( adjustment ) );
    gtk_widget_set_usize( GTK_WIDGET( scale ), 300, 40 );
    gtk_range_set_update_policy( GTK_RANGE( scale ), GTK_UPDATE_DELAYED );
    gtk_scale_set_digits( GTK_SCALE( scale ), 0 );
    gtk_scale_set_draw_value( GTK_SCALE( scale ), TRUE );
    gtk_box_pack_start( GTK_BOX( hbox ), scale, TRUE, TRUE, 0 );

    button = gtk_check_button_new_with_label( "Lock" );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );

    hbox = gtk_hbox_new( FALSE, 5 );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, TRUE, TRUE, 0 );

    button = gtk_button_new_with_label( "Ok" );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );
    GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
    gtk_widget_grab_default( button );

    button = gtk_button_new_with_label( "Cancel" );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );
    GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );

    gtk_widget_show_all( vbox );

    gtk_widget_show( window );
    return;
}
