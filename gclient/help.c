#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <climisc.h>
#include <log.h>
#include <shared.h>
#include <unit.h>
#include <city.h>
#include <tech.h>
#include <game.h>
#include <help.h>
#include <colors.h>

#include <graphics.h>



typedef struct	help_item		HELP_ITEM;



struct help_item
{
    HELP_ITEM  *next;
    char       *topic;
    char       *text;
    int         indent;
};




/*
 * Globals.
 */
HELP_ITEM *		head		= NULL;
HELP_ITEM *		tail		= NULL;
GtkWidget *		help_clist	= NULL;
GtkWidget *		help_frame	= NULL;
GtkWidget *		help_text	= NULL;
GtkWidget *		unit_tile	= NULL;
GtkWidget *		help_box	= NULL;
GtkWidget *		help_table	= NULL;
GtkWidget *		help_label	[4][5];

char *	help_label_name		[4][5]	=
{
    { "Cost:",		"", "",	"Attack:",	"" },
    { "Defense:",	"", "",	"Move:"	,	"" },
    { "FirePower:",	"", "",	"Hitpoints:",	"" },
    { "Requirement:",	"", "",	"Obsolete by:",	"" }
};


HELP_ITEM *find_help_item_position( int pos )
{
    HELP_ITEM *pitem;

    for ( pitem = head; pitem && pos != 0; pitem = pitem->next, pos-- )
	;

    return pitem;
}

void boot_help_texts( void )
{
    FILE       *fs;
    char        buf	[512];
    char       *p;
    char        text	[64000];
    HELP_ITEM  *pitem;
  
    if ( !( fs = fopen( datafilename( "helpdata.txt" ), "r" ) ) )
    {
	log_string( LOG_NORMAL, "failed reading help-texts" );
	return;
    }

    for ( ; ; )
    {
	fgets( buf, 512, fs );
	buf[ strlen( buf ) - 1 ] = '\0';		/* eat newline */

	if ( !strncmp( buf, "%%", 2 ) )
	    continue;

	if ( !( p = strchr( buf, '#' ) ) )
	    break;

	pitem		= (HELP_ITEM *) malloc( sizeof( HELP_ITEM ) );
	pitem->indent	= p - buf;
	pitem->topic	= mystrdup( p + 1 );
	pitem->next	= NULL;
    
	text[0]		= '\0';

	for ( ; ; )
	{
	    fgets( buf, 512, fs );

	    if ( !strncmp( buf, "%%", 2 ) )
		continue;

	    if ( !strcmp( buf, "---\n" ) )
		break;

	    strcat( text, buf );
	} 
    
	pitem->text	= mystrdup( text );
  
	if ( !head )
	{
	    head	= pitem;
	    tail	= pitem;
	}
	else
	{
	    tail->next	= pitem;
	    tail	= pitem;
	}
    
    }
  
    fclose( fs );
    return;
}

void set_title_topic( char *topic )
{
    if( !strcmp( topic, "Freeciv" ) || !strcmp( topic, "About" ) )
	gtk_frame_set_label( GTK_FRAME( help_frame ), FREECIV_NAME_VERSION );
    else
	gtk_frame_set_label( GTK_FRAME( help_frame ), topic );
    return;
}

void help_update_dialog( HELP_ITEM *pitem )
{
    int   i, j;
    char *top;

    if ( !pitem )
	return;

    for	( top = pitem->topic; *top == ' '; top++ )
	;

    set_title_topic( top );

    gtk_widget_hide_all( help_box );

    gtk_text_freeze( GTK_TEXT( help_text ) );
    gtk_editable_delete_text( GTK_EDITABLE( help_text ), 0, -1 );

    for ( i = 0; i < A_LAST; ++i )
    {
	char buf	[4096];

	if ( strcmp( top, advances[i].name ) )
	    continue;

	strcpy( buf, pitem->text );

	for ( j = 0; j < B_LAST; ++j )
	{
	    if ( i != improvement_types[j].tech_requirement )
		continue;
	    sprintf( buf+strlen( buf ), "Allows %s.\n",
						improvement_types[j].name );
	}
      
	for ( j = 0; j < U_LAST; ++j )
	{
	    if ( i != get_unit_type(j)->tech_requirement )
		continue;
	    sprintf( buf+strlen( buf ), "Allows %s.\n",
						get_unit_type(j)->name );
	}

	for ( j = 0; j < A_LAST; ++j )
	{
	    if ( i == advances[j].req[0] )
	    {
		if ( advances[j].req[1] == A_NONE )
		    sprintf( buf+strlen( buf ), "Allows %s.\n",
			advances[j].name );
		else
		    sprintf( buf+strlen( buf ), "Allows %s (with %s).\n",
			advances[j].name, advances[advances[j].req[1]].name );
	    }
	    if ( i == advances[j].req[1] )
	    {
		sprintf( buf+strlen( buf ), "Allows %s (with %s).\n", 
			advances[j].name, advances[advances[j].req[0]].name );
	    }
	}

	gtk_text_insert( GTK_TEXT( help_text ), NULL, NULL, NULL, buf, -1 );
	gtk_text_thaw( GTK_TEXT( help_text ) );
	gtk_widget_show( help_text );
	gtk_widget_show( help_box );
	return;
    }

    for ( i = 0; i < U_LAST; ++i )
    {
	char       buf		[64];

	if ( strcmp( top, get_unit_type( i )->name ) )
	    continue;

	gtk_pixmap_set( GTK_PIXMAP( unit_tile ), create_overlay_unit(i), NULL );
	gtk_widget_show( unit_tile );


	sprintf( buf, "%d ", get_unit_type(i)->build_cost );
	gtk_label_set( GTK_LABEL( help_label[0][1] ), buf );
	sprintf( buf, "%d ", get_unit_type(i)->attack_strength );
	gtk_label_set( GTK_LABEL( help_label[0][4] ), buf );
	sprintf( buf, "%d ", get_unit_type(i)->defense_strength );
	gtk_label_set( GTK_LABEL( help_label[1][1] ), buf );
	sprintf( buf, "%d ", get_unit_type(i)->move_rate/3 );
	gtk_label_set( GTK_LABEL( help_label[1][4] ), buf );
	sprintf( buf, "%d ", get_unit_type(i)->firepower );
	gtk_label_set( GTK_LABEL( help_label[2][1] ), buf );
	sprintf( buf, "%d ", get_unit_type(i)->hp );
	gtk_label_set( GTK_LABEL( help_label[2][4] ), buf );

	if ( get_unit_type(i)->tech_requirement == A_LAST )
	    gtk_label_set( GTK_LABEL( help_label[3][1] ), "None" );
	else
	{
	    gtk_label_set( GTK_LABEL( help_label[3][1] ),
			advances[get_unit_type(i)->tech_requirement].name );
	}
	if ( get_unit_type(i)->obsoleted_by == -1 )
	    gtk_label_set( GTK_LABEL( help_label[3][4] ), "None" );
	else
	{
	    gtk_label_set( GTK_LABEL( help_label[3][4] ),
		get_unit_type(get_unit_type(i)->obsoleted_by)->name );
	}

	gtk_widget_show_all( help_table );

	gtk_text_insert( GTK_TEXT( help_text ), NULL, NULL, NULL, pitem->text, -1 );
	gtk_text_thaw( GTK_TEXT( help_text ) );
	gtk_widget_show( help_text );

	gtk_widget_show( help_box );
	return;
    }

    gtk_text_insert( GTK_TEXT( help_text ), NULL, NULL, NULL, pitem->text, -1 );
    gtk_text_thaw( GTK_TEXT( help_text ) );
    gtk_widget_show( help_text );
    gtk_widget_show( help_box );
    return;
}


void select_help_item_string( char *item )
{
    HELP_ITEM *pitem;
    int        row;

    for ( pitem = head, row = 0; pitem; pitem = pitem->next, row++ )
	if ( !strcmp( item, pitem->topic ) )	break;

    if ( !pitem )
	return;

    gtk_clist_select_row( GTK_CLIST( help_clist ), row, 0 );
    gtk_clist_moveto( GTK_CLIST( help_clist ), row, 0, 0, 0 );
    return;
}




void selected_topic( GtkCList *clist, gint row, gint column,
							GdkEventButton *event )
{
    HELP_ITEM *pitem;

    for ( pitem = head; pitem && row--; pitem = pitem->next )
	;

    if ( !pitem )
	return;

    help_update_dialog( pitem );
    return;
}










void help_string( GtkWidget *widget, gpointer data )
{
    static GtkWidget *window	= NULL;
    static GtkWidget *mbox;
    static GtkWidget *hbox;
    static GtkWidget *button;
    static GtkStyle   text_style;
    static GtkStyle   red_style;
           char      *item	= (char *)data;
    
           char      *row	[1];
    
           HELP_ITEM *pitem;
           int        i, j;


    if ( !window )
    {
	window = gtk_window_new( GTK_WINDOW_TOPLEVEL );

	gtk_signal_connect( GTK_OBJECT( window ), "destroy",
			GTK_SIGNAL_FUNC( gtk_widget_destroyed ), &window );

	gtk_window_set_title( GTK_WINDOW( window ), "FreeCiv Help Browser" );
	gtk_container_border_width( GTK_CONTAINER( window ), 5 );

	mbox = gtk_vbox_new( FALSE, 5 );
	gtk_container_add( GTK_CONTAINER( window ), mbox );
	
	hbox = gtk_hbox_new( FALSE, 5 );
	gtk_box_pack_start( GTK_BOX( mbox ), hbox, TRUE, TRUE, 0 );

	help_clist = gtk_clist_new( 1 );
	gtk_box_pack_start( GTK_BOX( hbox ), help_clist, TRUE, TRUE, 0 );

	gtk_clist_set_policy( GTK_CLIST( help_clist ),
				GTK_POLICY_ALWAYS, GTK_POLICY_AUTOMATIC );
	gtk_widget_set_usize( help_clist, 180, 350 );

	gtk_signal_connect( GTK_OBJECT( help_clist ), "select_row",
			    GTK_SIGNAL_FUNC( selected_topic ), NULL );

	for ( pitem = head; pitem; pitem = pitem->next )
	{
	    row[0] = pitem->topic;
	    i = gtk_clist_append( GTK_CLIST( help_clist ), row );

	    gtk_clist_set_shift( GTK_CLIST( help_clist ), i, 0, 0, pitem->indent );
	}

	help_frame = gtk_frame_new( "" );
	gtk_box_pack_start( GTK_BOX( hbox ), help_frame, TRUE, TRUE, 0 );
	gtk_widget_set_usize( help_frame, 520, 350 );

	help_box = gtk_vbox_new( FALSE, 5 );
	gtk_container_add( GTK_CONTAINER( help_frame ), help_box );


	unit_tile = gtk_pixmap_new( create_overlay_unit( 0 ), NULL );
	gtk_box_pack_start( GTK_BOX( help_box ), unit_tile, FALSE, FALSE, 0 );



	help_table = gtk_table_new( 5, 4, FALSE );
	gtk_box_pack_start( GTK_BOX( help_box ), help_table, FALSE, FALSE, 0 );

	for ( i = 0; i < 5; i++ )
	    for ( j = 0; j < 4; j++ )
	    {
		help_label[j][i]  = gtk_label_new( help_label_name[j][i] );
		gtk_table_attach_defaults( GTK_TABLE( help_table ),
					help_label[j][i], i, i+1, j, j+1 );
	    }

	red_style = *gtk_widget_get_style( help_label[0][0] );
	red_style.font = gdk_font_load( "-*-times-bold-r-*-*-14-*-*-*-*-*-*-*" );
	red_style.fg[GTK_STATE_NORMAL] = colors_standard[COLOR_STD_RED];

	for ( i = 0; i < 5; i++ )
	    for ( j = 0; j < 4; j++ )
		gtk_widget_set_style( help_label[j][i], &red_style );

	help_text = gtk_text_new( NULL, NULL );
	gtk_text_set_editable( GTK_TEXT( help_text ), FALSE );
	gtk_box_pack_start( GTK_BOX( help_box ), help_text, TRUE, TRUE, 0 );

	text_style = *gtk_widget_get_style( help_text );
	text_style.font = gdk_font_load( "-*-courier-medium-r-*-*-12-*-*-*-*-*-*-*" );
	gtk_widget_set_style( help_text, &text_style );
	gtk_widget_realize( help_text );


	button = gtk_button_new_with_label( "close" );
	gtk_box_pack_start( GTK_BOX( mbox ), button, TRUE, TRUE, 0 );
	GTK_WIDGET_SET_FLAGS( button, GTK_CAN_DEFAULT );
	gtk_widget_grab_default( button );

	gtk_signal_connect_object( GTK_OBJECT( button ), "clicked",
		GTK_SIGNAL_FUNC( gtk_widget_destroy ), GTK_OBJECT( window ) );
    }

    gtk_widget_hide( window );
    gtk_widget_show_all( mbox );
    select_help_item_string( item );
    gtk_widget_show( window );
    return;
}
