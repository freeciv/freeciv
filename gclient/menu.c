#include <strings.h>

#include <gtk/gtk.h>

#include <help.h>
#include <civclient.h>
#include <climisc.h>
#include <repodlgs.h>
#include <mapctrl.h>

	void	help_string		( GtkWidget *widget, gpointer data );
	void	game_rates		( GtkWidget *widget, gpointer data );



static	void	menus_remove_accel	( GtkWidget *widget, gchar *signame,
					 gchar *path );
static	gint	menus_install_accel	( GtkWidget *widget, gchar *signame,
					 gchar key, gchar modifiers,
					 gchar *path );
	void	menus_init		( void );
	void	menus_create		( GtkMenuEntry *entries, int nentries );


/*
 * This is the GtkMenuEntry structure used to create new menus.  The
 * first member is the menu definition string.  The second, the
 * default accelerator key used to access this menu function with
 * the keyboard.  The third is the callback function to call when
 * this menu item is selected (by the accelerator key, or with the
 * mouse) The last member is the data to pass to your callback function.
 */

enum MenuID {
  MENU_END_OF_LIST=0,

  MENU_GAME_FIND_CITY,
  MENU_GAME_OPTIONS,
  MENU_GAME_MSG_OPTIONS,
  MENU_GAME_RATES,   
  MENU_GAME_REVOLUTION,
  MENU_GAME_PLAYERS,
  MENU_GAME_MESSAGES,
  MENU_GAME_SERVER_OPTIONS,
  MENU_GAME_OUTPUT_LOG,
  MENU_GAME_QUIT,
  
  MENU_ORDER_AUTO,
  MENU_ORDER_MINE,
  MENU_ORDER_IRRIGATE,
  MENU_ORDER_FORTRESS,
  MENU_ORDER_CITY,
  MENU_ORDER_ROAD,
  MENU_ORDER_POLLUTION,
  MENU_ORDER_FORTIFY,
  MENU_ORDER_SENTRY,
  MENU_ORDER_PILLAGE,
  MENU_ORDER_HOMECITY,
  MENU_ORDER_WAIT,
  MENU_ORDER_UNLOAD,
  MENU_ORDER_GOTO,
  MENU_ORDER_GOTO_CITY,
  MENU_ORDER_DISBAND,
  MENU_ORDER_DONE,

  MENU_REPORT_CITY,
  MENU_REPORT_SCIENCE,
  MENU_REPORT_TRADE,
  MENU_REPORT_ACTIVE_UNITS,
  MENU_REPORT_DEMOGRAPHIC,
  MENU_REPORT_TOP_CITIES,
  MENU_REPORT_WOW,

  MENU_HELP_CONTROLS,
  MENU_HELP_PLAYING,
  MENU_HELP_IMPROVEMENTS,
  MENU_HELP_UNITS,
  MENU_HELP_TECH,
  MENU_HELP_WONDERS,
  MENU_HELP_COPYING,
  MENU_HELP_ABOUT
};

void reports_menu_callback( GtkWidget *widget, gpointer data )
{
  int pane_num = (int)data;

  switch(pane_num) {
/*
   case MENU_REPORT_CITY:
    popup_city_report_dialog(0);
    break;
*/
   case MENU_REPORT_SCIENCE:
    popup_science_dialog(0);
    break;
/*
   case MENU_REPORT_TRADE:
    popup_trade_report_dialog(0);
    break;
   case MENU_REPORT_ACTIVE_UNITS:
    popup_activeunits_report_dialog(0);
    break;
*/
  }
}

static	GtkMenuEntry	menu_items	[]	=
{
    {	"<Main>/Game/Find City",		NULL,		NULL	},
    {	"<Main>/Game/Options",			NULL,		NULL	},
    {	"<Main>/Game/Msg Options",		NULL,		NULL	},
    {	"<Main>/Game/Rates",			NULL,
	game_rates							},
    {	"<Main>/Game/Revolution",		NULL,		NULL	},
    {	"<Main>/Game/Players",			NULL,		NULL	},
    {	"<Main>/Game/Messages",			NULL,		NULL	},
    {	"<Main>/Game/Server Options",		NULL,
	(GtkMenuCallback)send_report_request,
	(gpointer)REPORT_SERVER_OPTIONS					},
    {	"<Main>/Game/Export Log",		NULL,
	log_output_window						},
    {	"<Main>/Game/<separator>",		NULL,		NULL	},
    {	"<Main>/Game/Quit",			"<control>Q",
	(GtkMenuCallback)gtk_main_quit					},

    {	"<Main>/Orders/Auto Settler",		"a",
	key_unit_auto							},
    {	"<Main>/Orders/Build City",		"b",		NULL	},
    {	"<Main>/Orders/Build Irrigation",	"i",
	key_unit_irrigate						},
    {	"<Main>/Orders/Build Fortress",		"<shift>f",	NULL	},
    {	"<Main>/Orders/Change to Forest",	"m",		NULL	},
    {	"<Main>/Orders/Build Road",		"r",
	key_unit_road							},
    {	"<Main>/Orders/Clean Pollution",	"p",
	key_unit_clean_pollution					},
    {	"<Main>/Orders/Make Homecity",		"h",
	key_unit_homecity						},
    {	"<Main>/Orders/Fortify",		"f",
	key_unit_fortify						},
    {	"<Main>/Orders/Sentry",			"s",
	key_unit_sentry							},
    {	"<Main>/Orders/Unload",			"u",
	key_unit_unload							},
    {	"<Main>/Orders/Wait",			"w",
	key_unit_wait							},
    {	"<Main>/Orders/Go to",			"g",
	key_unit_goto							},
    {	"<Main>/Orders/Airlift to City",	"l",		NULL	},
    {	"<Main>/Orders/Disband Unit",		"<shift>d",
	key_unit_disband						},
    {	"<Main>/Orders/Pillage",		"<shift>p",
	key_unit_pillage						},
    {	"<Main>/Orders/Done",			" ",
	key_unit_done							},
    {	"<Main>/Reports/City Report",		NULL,		NULL	},
    {	"<Main>/Reports/Science Report",	NULL,
	(GtkMenuCallback)reports_menu_callback,
	(gpointer)MENU_REPORT_SCIENCE					},
    {	"<Main>/Reports/Trade Report",		NULL,		NULL	},
    {	"<Main>/Reports/Active Units",		NULL,		NULL	},
    {	"<Main>/Reports/Demographic",		NULL,
	(GtkMenuCallback)send_report_request,
	(gpointer)REPORT_DEMOGRAPHIC					},
    {	"<Main>/Reports/Top 5 Cities",		NULL,
	(GtkMenuCallback)send_report_request,
	(gpointer)REPORT_TOP_5_CITIES					},
    {	"<Main>/Reports/Wonders of the World",	NULL,
	(GtkMenuCallback)send_report_request,
	(gpointer)REPORT_WONDERS_OF_THE_WORLD				},

    {	"<Main>/Help/Help Controls",		NULL,		help_string,
						HELP_CONTROLS_ITEM	},
    {	"<Main>/Help/Help Playing",		NULL,		help_string,
						HELP_PLAYING_ITEM	},
    {	"<Main>/Help/Help Improvements",	NULL,		help_string,
						HELP_IMPROVEMENTS_ITEM	},
    {	"<Main>/Help/Help Units",		NULL,		help_string,
						HELP_UNITS_ITEM		},
    {	"<Main>/Help/Help Technology",		NULL,		help_string,
						HELP_TECHS_ITEM		},
    {	"<Main>/Help/Help Wonders",		NULL,		help_string,
						HELP_WONDERS_ITEM	},
    {	"<Main>/Help/Copying",			NULL,		help_string,
						HELP_COPYING_ITEM	},
    {	"<Main>/Help/About",			NULL,		help_string,
						HELP_ABOUT_ITEM		}
};



/* calculate the number of menu_item's */
static int	nitems	= sizeof( menu_items )/sizeof( menu_items[0] );

static int              initialize	= TRUE;
static GtkMenuFactory  *factory		= NULL;
static GtkMenuFactory  *subfactory		[1];
static GHashTable      *entry_ht	= NULL;



void get_main_menu( GtkWidget **menubar, GtkAcceleratorTable **table )
{
    if ( initialize )
	menus_init();

    if ( menubar )
	*menubar = subfactory[0]->widget;
    if ( table )
	*table = subfactory[0]->table;
    return;
}

void menus_init( void )
{
    if ( initialize )
    {
	initialize = FALSE;

	factory = gtk_menu_factory_new( GTK_MENU_FACTORY_MENU_BAR );
	subfactory[0] = gtk_menu_factory_new( GTK_MENU_FACTORY_MENU_BAR );

	gtk_menu_factory_add_subfactory( factory, subfactory[0], "<Main>" );
	menus_create( menu_items, nitems );
    }
    return;
}

void menus_create( GtkMenuEntry *entries, int nentries )
{
    char *accelerator;
    int   i;
    
    if ( initialize )
	menus_init();
    
    if ( entry_ht )
	for ( i = 0; i < nentries; i++ )
	{
	    accelerator = g_hash_table_lookup( entry_ht, entries[i].path );

	    if ( !accelerator )
		continue;

	    if ( accelerator[0] == '\0' )
	    	entries[i].accelerator = NULL;
	    else
	    	entries[i].accelerator = accelerator;
	}

    gtk_menu_factory_add_entries( factory, entries, nentries );
    
    for ( i = 0; i < nentries; i++ )
	if ( entries[i].widget )
	{
	    gtk_signal_connect( GTK_OBJECT( entries[i].widget ),
				"install_accelerator",
				(GtkSignalFunc) menus_install_accel,
				entries[i].path );
	    gtk_signal_connect( GTK_OBJECT( entries[i].widget ),
				"remove_accelerator",
				(GtkSignalFunc) menus_remove_accel,
				entries[i].path );
	}
    return;
}

static gint menus_install_accel( GtkWidget *widget, gchar *signame, gchar key,
				gchar modifiers, gchar *path )
{
    char  accel	[64];
    char *t1;
    char  t2	[2];

    accel[0] = '\0';

    if ( modifiers & GDK_CONTROL_MASK )
	strcat( accel, "<control>" );

    if ( modifiers & GDK_SHIFT_MASK )
	strcat( accel, "<shift>" );

    if ( modifiers & GDK_MOD1_MASK )
	strcat( accel, "<alt>" );

    t2[0] = key;
    t2[1] = '\0';
    strcat( accel, t2 );
    
    if ( entry_ht )
    {
	t1 = g_hash_table_lookup( entry_ht, path );
	g_free( t1 );
    }
    else
	entry_ht = g_hash_table_new( g_str_hash, g_str_equal );

    g_hash_table_insert( entry_ht, path, g_strdup( accel ) );

    return TRUE;
}

static void menus_remove_accel( GtkWidget *widget, gchar *signame, gchar *path )
{
    char *t;
    
    if ( !entry_ht )
	return;

    t = g_hash_table_lookup( entry_ht, path );
    g_free( t );

    g_hash_table_insert( entry_ht, path, g_strdup( "" ) );
    return;
}

void menus_set_sensitive( char *path, int sensitive )
{
    GtkMenuPath *menu_path;
    
    if ( initialize )
	menus_init();
    
    menu_path = gtk_menu_factory_find( factory, path );

    if ( menu_path )
	gtk_widget_set_sensitive( menu_path->widget, sensitive );
    else
	g_warning( "Can't set sensitivity for non existant menu %s.", path );
    return;
}
