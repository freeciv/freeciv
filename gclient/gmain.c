#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#include <log.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gmain.h>

#include "main.h"

#include <civclient.h>
#include <graphics.h>
#include <colors.h>
#include <game.h>
#include <clinet.h>
#include <connect.h>
#include <mapview.h>
#include <mapctrl.h>

#define BULB_TILES       21*20
#define GOVERNMENT_TILES 21*20+8
#define SUN_TILES        21*20+14



void popup_help_dialog_string( void );

/*void file_quit_cmd_callback( GtkWidget *widget, gpointer data )*/
void game_rates( GtkWidget *widget, gpointer data );

extern SPRITE *		intro_gfx_sprite;
extern SPRITE *		radar_gfx_sprite;






extern char	server_host	[512];
extern char	name		[512];
extern int	server_port;

extern enum client_states	client_state;
extern int			use_solid_color_behind_units;
extern int			sound_bell_at_new_turn;
extern int			smooth_move_units;
extern int			flags_are_transparent;

extern int			seconds_to_turndone;

extern int			last_turn_gold_amount;
extern int			turn_gold_difference;
extern int			did_advance_tech_this_turn;



GtkWidget *	map_canvas;			/* GtkDrawingArea */
GtkWidget *	overview_canvas;		/* GtkDrawingArea */

/* this pixmap acts as a backing store for the map_canvas widget */
GdkPixmap *	map_canvas_store;
int		map_canvas_store_twidth, map_canvas_store_theight;

/* this pixmap acts as a backing store for the overview_canvas widget */
GdkPixmap *	overview_canvas_store;
int		overview_canvas_store_width, overview_canvas_store_height;

/* this pixmap is used when moving units etc */
GdkPixmap *	single_tile_pixmap;
int		single_tile_pixmap_width, single_tile_pixmap_height;


GtkWidget *	drawing;
GtkWidget *	toplevel;

GdkGC *		civ_gc;
GdkGC *		fill_bg_gc;

GdkFont *	main_font;

GtkWidget *	main_frame_civ_name;
GtkLabel *	main_label_info;

GtkText *	main_message_area;

GtkWidget *	econ_label			[10];
GtkWidget *	bulb_label;
GtkWidget *	sun_label;
GtkWidget *	government_label;
GtkWidget *	timeout_label;
GtkWidget *	turn_done_button;

gint		gtk_interval_id;

GtkWidget *	unit_info_label, *unit_info_frame;
GtkWidget *	text_scrollbar;




void append_output_window( char *astring )
{
    gtk_text_freeze( GTK_TEXT( main_message_area ) );
    gtk_text_insert( main_message_area, NULL, NULL, NULL, astring, -1 );
    gtk_text_insert( main_message_area, NULL, NULL, NULL, "\n", -1 );
    gtk_text_thaw( GTK_TEXT( main_message_area ) );

    /* move the scrollbar forward by a ridiculous amount */
    gtk_range_default_vmotion( GTK_RANGE( text_scrollbar ), 0, 10000 );
    return;
}


/**************************************************************************
...
**************************************************************************/
gint timer_callback(gpointer data)
{
  static int flip;
  
  if(get_client_state()==CLIENT_GAME_RUNNING_STATE) {
  
    if(game.player_ptr->is_connected && game.player_ptr->is_alive && 
       !game.player_ptr->turn_done) { 
      int i, is_waiting, is_moving;
      
      for(i=0, is_waiting=0, is_moving=0; i<game.nplayers; i++)
        if(game.players[i].is_alive && game.players[i].is_connected) {
          if(game.players[i].turn_done)
            is_waiting++;
          else
            is_moving++;
        }
      
      if(is_moving==1 && is_waiting) 
        update_turn_done_button(0);  /* stress the slow player! */
    }
    
    blink_active_unit();
    
    if(flip) {
      update_timeout_label();
    if(seconds_to_turndone)
        seconds_to_turndone--;
    }
    
    flip=!flip;
  }
  return TRUE;
}

void end_turn_callback( GtkWidget *widget, gpointer data )
{
    gtk_widget_set_sensitive(turn_done_button, FALSE);
    user_ended_turn();
}





void enable_turn_done_button(void)
{
  gtk_widget_set_sensitive(turn_done_button, TRUE);
/*
  if(sound_bell_at_new_turn)
    XBell(display, 100);
*/
}


/**************************************************************************
...
**************************************************************************/
void inputline_return(GtkWidget *widget, gpointer data)
{
  struct packet_generic_message apacket;
  char *theinput;

  theinput = gtk_entry_get_text( GTK_ENTRY( widget ) );
  
  if(*theinput) {
    strncpy(apacket.message, theinput, MSG_SIZE-NAME_SIZE);
    apacket.message[MSG_SIZE-NAME_SIZE]='\0';
    send_packet_generic_message(&aconnection, PACKET_CHAT_MSG, &apacket);
  }

  gtk_entry_set_text(GTK_ENTRY(widget), "");
}



void keyboard_handler( GtkWidget *widget, GdkEventKey *event )
{
    switch ( event->keyval )
    {
    case GDK_KP_Up:		key_unit_north();		break;
    case GDK_KP_Page_Up:	key_unit_north_east();		break;
    case GDK_KP_Right:		key_unit_east();		break;
    case GDK_KP_Page_Down:	key_unit_south_east();		break;
    case GDK_KP_Down:		key_unit_south();		break;
    case GDK_KP_End:		key_unit_south_west();		break;
    case GDK_KP_Left:		key_unit_west();		break;
    case GDK_KP_Home:		key_unit_north_west();		break;
    }
}


int g_main( int argc, char **argv )
{
    GtkWidget	        *vbox;
    GtkWidget           *hbox;
    GtkWidget           *vbox1;

    GtkWidget           *frame;

    GtkWidget           *menubar;
    GtkAcceleratorTable *accel;

    /* Process GTK arguments */
    gtk_init( &argc, &argv );

    {
	struct passwd *password = getpwuid( getuid() );

	strcpy( name, password->pw_name );
    }

    log_set_level( LOG_NORMAL );

    strcpy( server_host, "localhost" );
    server_port = DEFAULT_SOCK_PORT;

    toplevel = gtk_window_new( GTK_WINDOW_TOPLEVEL );

    gtk_window_set_title( GTK_WINDOW( toplevel ), "FreeCiv" );

    gtk_widget_realize( toplevel );

    init_color_system();

    civ_gc = gdk_gc_new( toplevel->window );

    if ( !( main_font = gdk_font_load( CITY_NAMES_FONT ) ) )
    {
	log_string( LOG_FATAL, "failed loading font: " CITY_NAMES_FONT );
	exit( 1 );
    }

    fill_bg_gc = gdk_gc_new( toplevel->window );

    load_intro_gfx();
    load_tile_gfx();



    vbox = gtk_vbox_new( FALSE, 5 );
    gtk_container_add( GTK_CONTAINER( toplevel ), vbox );
    
    get_main_menu( &menubar, &accel );
    gtk_window_add_accelerator_table( GTK_WINDOW( toplevel ), accel );
    gtk_box_pack_start( GTK_BOX( vbox ), menubar, FALSE, FALSE, 0 );



    hbox = gtk_hbox_new( FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );



    vbox1 = gtk_vbox_new( FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( hbox ), vbox1, FALSE, FALSE, 0 );

    frame = gtk_frame_new( NULL );
    gtk_box_pack_start( GTK_BOX( vbox1 ), frame, FALSE, FALSE, 0 );
    
    overview_canvas		= gtk_drawing_area_new();
    gtk_container_add( GTK_CONTAINER( frame ), overview_canvas );
    gtk_widget_set_usize( overview_canvas, 160, 100 );

    gtk_signal_connect( GTK_OBJECT( overview_canvas ), "expose_event",
			(GtkSignalFunc) overview_canvas_expose, NULL );
    gtk_signal_connect( GTK_OBJECT( overview_canvas ), "button_press_event",
			(GtkSignalFunc) butt_down_overviewcanvas, NULL );
    gtk_widget_set_events( overview_canvas, GDK_EXPOSURE_MASK
					| GDK_LEAVE_NOTIFY_MASK
					| GDK_BUTTON_PRESS_MASK
					| GDK_POINTER_MOTION_MASK
					| GDK_POINTER_MOTION_HINT_MASK );

    map_canvas_store_twidth	= 510/NORMAL_TILE_WIDTH;
    map_canvas_store_theight	= 300/NORMAL_TILE_HEIGHT;
    map_canvas_store		= gdk_pixmap_new( toplevel->window,
				  map_canvas_store_twidth*NORMAL_TILE_WIDTH,
				  map_canvas_store_theight*NORMAL_TILE_HEIGHT,
				  -1 );

    overview_canvas_store_width	= 2*80;
    overview_canvas_store_height= 2*50;

    overview_canvas_store	= gdk_pixmap_new( toplevel->window,
				  overview_canvas_store_width,
				  overview_canvas_store_height, 
				  -1 );

    gdk_gc_set_foreground( fill_bg_gc, &colors_standard[COLOR_STD_WHITE] );
    gdk_draw_rectangle( overview_canvas_store, fill_bg_gc, TRUE, 0, 0,
		overview_canvas_store_width, overview_canvas_store_height );

    single_tile_pixmap		= gdk_pixmap_new( toplevel->window, 
				  NORMAL_TILE_WIDTH,
				  NORMAL_TILE_HEIGHT,
				  -1 );


    {	/* Info on player's civilization */
	GtkWidget *vbox2;
	GtkWidget *label;

	frame = gtk_frame_new( NULL );
	gtk_box_pack_start( GTK_BOX( vbox1 ), frame, TRUE, FALSE, 0 );
	main_frame_civ_name = frame;

	vbox2 = gtk_vbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( frame ), vbox2 );

	label = gtk_label_new( "\n\n\n" );
	gtk_box_pack_start( GTK_BOX( vbox2 ), label, TRUE, FALSE, 0 );
	main_label_info = GTK_LABEL( label );
    }

    { /* production status */
	GtkWidget *box4;
	GtkWidget *table;
	GtkWidget *frame;
	int       i;

	/* make a box so the table will be centered */
	box4 = gtk_hbox_new( FALSE, 0 );
	gtk_box_pack_start(GTK_BOX(vbox1), box4, TRUE, FALSE, 0);

	table = gtk_table_new(10, 2, FALSE);
	gtk_table_set_row_spacing(GTK_TABLE(table), 0, 0);
	gtk_table_set_col_spacing(GTK_TABLE(table), 0, 0);
	gtk_box_pack_start(GTK_BOX(box4), table, TRUE, FALSE, 0);

	for ( i = 0 ; i < 10 ; i++ )
	{
	    frame = gtk_frame_new(NULL);
	    gtk_widget_set_usize(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
	    gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 1, 0, 1);

	    econ_label[i] = gtk_pixmap_new(get_tile_sprite(SUN_TILES)->pixmap,NULL);
	    gtk_container_add( GTK_CONTAINER( frame ), econ_label[i] );
	}

	bulb_label	= gtk_pixmap_new(get_tile_sprite(BULB_TILES)->pixmap,NULL);
	sun_label	= gtk_pixmap_new(get_tile_sprite(SUN_TILES)->pixmap,NULL);
	government_label= gtk_pixmap_new(get_tile_sprite(GOVERNMENT_TILES)->pixmap,NULL);
	timeout_label	= gtk_label_new("");

	for ( i = 6 ; i < 10 ; i++ )
	{
	    frame = gtk_frame_new(NULL);
	    gtk_widget_set_usize(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
	    gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 1, 1, 2);

	    switch ( i )
	    {
	    case 6: gtk_container_add( GTK_CONTAINER( frame ), bulb_label );
		break;
	    case 7: gtk_container_add( GTK_CONTAINER( frame ), sun_label );
		break;
	    case 8: gtk_container_add( GTK_CONTAINER( frame ), government_label );
		break;
	    case 9: gtk_container_add( GTK_CONTAINER( frame ), timeout_label );
		break;
	    }
	}

    turn_done_button = gtk_button_new_with_label( "Turn Done" );
    gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 6, 1, 2);
    gtk_signal_connect( GTK_OBJECT( turn_done_button ), "clicked",
			GTK_SIGNAL_FUNC( end_turn_callback ), NULL );

    }

    { /* selected unit status */
        GtkWidget *box4;

        unit_info_frame = gtk_frame_new(NULL);
        gtk_box_pack_start(GTK_BOX(vbox1), unit_info_frame, TRUE, FALSE, 0);
        
        box4 = gtk_vbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(unit_info_frame), box4);
        
        unit_info_label = gtk_label_new("\n\n");
        gtk_box_pack_start(GTK_BOX(box4), unit_info_label, TRUE, FALSE, 0);
    }

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start( GTK_BOX( hbox ), frame, FALSE, FALSE, 0 );

    map_canvas			= gtk_drawing_area_new();
    gtk_widget_set_usize( map_canvas, 510, 300 );

    gtk_container_add( GTK_CONTAINER( frame ), map_canvas );

    /* Event signals */
    gtk_signal_connect( GTK_OBJECT( map_canvas ), "expose_event",
			(GtkSignalFunc) map_canvas_expose, NULL );
    gtk_signal_connect( GTK_OBJECT( map_canvas ), "button_press_event",
			(GtkSignalFunc) butt_down_mapcanvas, NULL );

    gtk_widget_set_events( map_canvas, GDK_EXPOSURE_MASK
					| GDK_LEAVE_NOTIFY_MASK
					| GDK_BUTTON_PRESS_MASK
					| GDK_POINTER_MOTION_MASK
					| GDK_POINTER_MOTION_HINT_MASK );

    { /* the message window */
	GtkWidget *text;

	hbox = gtk_hbox_new( FALSE, 0 );
	gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

	text = gtk_text_new(NULL, NULL);
	gtk_text_set_editable(GTK_TEXT(text), FALSE);
	gtk_text_set_word_wrap(GTK_TEXT(text), TRUE);
	gtk_box_pack_start( GTK_BOX( hbox ), text, TRUE, TRUE, 0 );

	text_scrollbar = gtk_vscrollbar_new( GTK_TEXT( text )->vadj );

	gtk_widget_realize( text );
	main_message_area = GTK_TEXT(text);

	gtk_box_pack_start( GTK_BOX( hbox ), text_scrollbar, FALSE, FALSE, 0 );

	gtk_widget_realize( text_scrollbar );

	append_output_window(
	"Freeciv is free software and you are welcome to distribute copies of"
	" it\nunder certain conditions; See the \"Copying\" item on the Help"
	" menu.\nNow.. Go give'em hell!" );
    }
    
    { /* the chat line */
	GtkWidget *entry;

	entry = gtk_entry_new();

	gtk_signal_connect(GTK_OBJECT(entry),
    				       "activate",
    				       GTK_SIGNAL_FUNC(inputline_return),
    				       GTK_OBJECT(entry));

	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
	gtk_widget_show(entry);
    }

    gtk_signal_connect( GTK_OBJECT( toplevel ), "key_press_event",
		GTK_SIGNAL_FUNC(keyboard_handler), NULL );

    gtk_widget_show_all( vbox );
    gtk_widget_show( toplevel );

    set_client_state( CLIENT_PRE_GAME_STATE );

    gtk_interval_id = gtk_timeout_add( 500, timer_callback, NULL );

    gtk_main( );
          
    return 0;
}
