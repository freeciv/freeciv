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
#include <pwd.h>
#include <unistd.h>
#include <time.h>

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gui_main.h>
#include <gui_stuff.h>
#include <menu.h>
#include <colors.h>
#include <log.h>
#include <graphics.h>
#include <map.h>
#include <mapview.h>
#include <chatline.h>
#include <civclient.h>
#include <clinet.h>
#include <mapctrl.h>
#include <freeciv.ico>
#include <dialogs.h>
#include <game.h>
#include <gotodlg.h>
#include <connectdlg.h>
#include <helpdlg.h>
#include <optiondlg.h>
#include <spaceshipdlg.h>
#include <options.h>

#define NOTIFY_DIALOG_FONT	"-b&h-lucidatypewriter-bold-r-normal-*-12-*-*-*-*-*-*-*"

/*void file_quit_cmd_callback( GtkWidget *widget, gpointer data )*/
void game_rates( GtkWidget *widget, gpointer data );

extern SPRITE *		intro_gfx_sprite;
extern SPRITE *		radar_gfx_sprite;






extern char	server_host	[];
extern char	name		[];
extern int	server_port;
extern char *	tile_set_dir;

extern enum client_states	client_state;
extern int			use_solid_color_behind_units;
extern int			sound_bell_at_new_turn;
extern int			smooth_move_units;
extern int			flags_are_transparent;

extern int			seconds_to_turndone;

extern int			last_turn_gold_amount;
extern int			turn_gold_difference;
extern int			ai_manual_turn_done;
extern int			did_advance_tech_this_turn;



GtkWidget *	map_canvas;			/* GtkDrawingArea */
GtkWidget *	overview_canvas;		/* GtkDrawingArea */

/* this pixmap acts as a backing store for the map_canvas widget */
GdkPixmap *	map_canvas_store;
int		map_canvas_store_twidth=1, map_canvas_store_theight=1;

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
GdkGC *		fill_tile_gc;
GdkPixmap *	gray50, *gray25;
GdkGC *		mask_fg_gc;
GdkGC *		mask_bg_gc;
GdkPixmap *	mask_bitmap;

GdkFont *	main_font;

GtkWidget *	main_frame_civ_name;
GtkWidget *	main_label_info;
/*
GtkWidget *	main_label_info_ebox;
*/
GtkText *	main_message_area;

GtkWidget *	econ_label			[10];
GtkWidget *	bulb_label;
GtkWidget *	sun_label;
GtkWidget *	government_label;
GtkWidget *	timeout_label;
GtkWidget *	turn_done_button;

GtkWidget *	unit_pixmap;
GtkWidget *	unit_below_pixmap		[MAX_NUM_UNITS_BELOW];
GtkWidget *	more_arrow_pixmap;

gint		gtk_interval_id;

GtkWidget *	unit_info_label, *unit_info_frame;
GtkWidget *	text_scrollbar;
GtkWidget *	inputline;

GtkWidget *	map_horizontal_scrollbar, *map_vertical_scrollbar;
gint		gdk_input_id;

GtkStyle *	notify_dialog_style;

GdkWindow *	root_window;


enum Display_color_type		display_color_type;

extern int	num_units_below;

gint show_info_popup(GtkWidget *w, GdkEventButton *ev);
gint timer_callback(gpointer data);



/**************************************************************************
...
**************************************************************************/
void parse_options(int *argc, char **argv[])
{
  static char usage[] = 
    "Usage: %s [option ...]\nValid options are:\n";

  char *logfile=NULL;
  int loglevel;
  
  /* set default argument values */
  loglevel=LOG_NORMAL;

  {
    struct passwd *password=getpwuid(getuid());

    if (password)
      strcpy(name, password->pw_name);
    else {
      freelog(LOG_NORMAL, "Your getpwuid call failed.  Please report this.");
      strcpy(name, "operator 00000");
      sprintf(name+9, "%05i",(int)getuid());
   }
  }

  server_port=DEFAULT_SOCK_PORT;

  strcpy(server_host, "localhost");

  if (argc && argv)
  {
    int i, j, k;
    
    for (i = 1; i < *argc;)
    {
      if (!strcmp ("--help", (*argv)[i]))
      {
	fprintf(stderr, usage, (*argv)[0]);
	fprintf(stderr, "  --help\t\tPrint a summary of the options.\n");
	fprintf(stderr, "  --log=FILE\t\tUse FILE as logfile.\n");
	fprintf(stderr, "  --name=NAME\t\tUse NAME.\n");
	fprintf(stderr, "  --port=PORT\t\tConnect to PORT.\n");
	fprintf(stderr, "  --server=SERVER\tConnect to the server SERVER.\n");
	fprintf(stderr, "  --debug=LEVEL\t\tSet debug log LEVEL (0,1,2).\n");
	fprintf(stderr, "  --tiles=DIR\t\tLook in directory DIR for the tiles.\n");
	fprintf(stderr, "  --version\t\tPrint the version number.\n");
	exit(0);
      }
      else if (!strcmp ("--version", (*argv)[i]))
      {
        fprintf(stderr, "%s\n", FREECIV_NAME_VERSION);
        exit(0);
      }
      else if (!strcmp ("--log",  (*argv)[i]) ||
              !strncmp ("--log=", (*argv)[i], 6))
      {
	char *opt_log = (*argv)[i] + 6;
	
	if (*opt_log == '=')
	  opt_log++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_log = (*argv)[i];
	}
        (*argv)[i] = NULL;

	logfile=opt_log;
      }
      else if (!strcmp ("--name",  (*argv)[i]) ||
              !strncmp ("--name=", (*argv)[i], 7))
      {
	char *opt_name = (*argv)[i] + 6;
	
	if (*opt_name == '=')
	  opt_name++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_name = (*argv)[i];
	}
        (*argv)[i] = NULL;

	strcpy(name, opt_name);
      }
      else if (!strcmp ("--port",  (*argv)[i]) ||
              !strncmp ("--port=", (*argv)[i], 7))
      {
	char *opt_port = (*argv)[i] + 6;
	
	if (*opt_port == '=')
	  opt_port++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_port = (*argv)[i];
	}
        (*argv)[i] = NULL;

	server_port=atoi(opt_port);
      }
      else if (!strcmp ("--server",  (*argv)[i]) ||
              !strncmp ("--server=", (*argv)[i], 9))
      {
	char *opt_server = (*argv)[i] + 8;
	
	if (*opt_server == '=')
	  opt_server++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_server = (*argv)[i];
	}
        (*argv)[i] = NULL;

	strcpy(server_host, opt_server);
      }
      else if (!strcmp ("--debug",  (*argv)[i]) ||
              !strncmp ("--debug=", (*argv)[i], 8))
      {
	char *opt_debug = (*argv)[i] + 7;
	
	if (*opt_debug == '=')
	  opt_debug++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_debug = (*argv)[i];
	}
        (*argv)[i] = NULL;

	loglevel=atoi(opt_debug);
      }
      else if (!strcmp ("--tiles",  (*argv)[i]) ||
              !strncmp ("--tiles=", (*argv)[i], 8))
      {
	char *opt_tiles = (*argv)[i] + 7;
	
	if (*opt_tiles == '=')
	  opt_tiles++;
	else
	{
	  (*argv)[i] = NULL;
	  i += 1;
	  opt_tiles = (*argv)[i];
	}
        (*argv)[i] = NULL;

	tile_set_dir=opt_tiles;
      }
      i += 1;
    }

    for (i = 1; i < *argc; i++)
    {
      for (k = i; k < *argc; k++)
        if ((*argv)[k] != NULL)
          break;
  
      if (k > i)
      {
        k -= i;
        for (j = i + k; j < *argc; j++)
          (*argv)[j-k] = (*argv)[j];
        *argc -= k;
      }
    }
  }
  log_init(logfile, loglevel, NULL);
}


/**************************************************************************
...
**************************************************************************/
gint keyboard_handler(GtkWidget *widget, GdkEventKey *event)
{
    if (GTK_WIDGET_HAS_FOCUS(inputline))
	return FALSE;

    switch (event->keyval)
    {
    case GDK_KP_8:
    case GDK_KP_Up:		key_unit_north();		break;
    case GDK_KP_9:
    case GDK_KP_Page_Up:	key_unit_north_east();		break;
    case GDK_KP_6:
    case GDK_KP_Right:		key_unit_east();		break;
    case GDK_KP_3:
    case GDK_KP_Page_Down:	key_unit_south_east();		break;
    case GDK_KP_2:
    case GDK_KP_Down:		key_unit_south();		break;
    case GDK_KP_1:
    case GDK_KP_End:		key_unit_south_west();		break;
    case GDK_KP_4:
    case GDK_KP_Left:		key_unit_west();		break;
    case GDK_KP_7:
    case GDK_KP_Home:		key_unit_north_west();		break;
    case GDK_KP_Begin:
    case GDK_KP_5:		focus_to_next_unit();		break;

    case GDK_e:			key_end_turn();			break;
    case GDK_Return:		key_end_turn();			break;
    case GDK_KP_Enter:		key_end_turn();			break;

    case GDK_t:			key_city_workers(widget,event);	break;
    default:							return FALSE;
    }

    gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
    return TRUE;
}


/**************************************************************************
...
**************************************************************************/
void setup_widgets(void)
{
  GtkWidget	      *vbox;
  GtkWidget	      *hbox;
  GtkWidget	      *hbox2;
  GtkWidget	      *vbox1;
  GtkWidget	      *vbox2;

  GtkWidget	      *frame;

  GtkWidget	      *menubar;
  GtkWidget	      *table;
  GtkWidget	      *ebox, *paned;
  int		       i;

  vbox = gtk_vbox_new( FALSE, 5 );
  gtk_container_add( GTK_CONTAINER( toplevel ), vbox );
  
  setup_menus( toplevel, &menubar );

  gtk_box_pack_start( GTK_BOX( vbox ), menubar, FALSE, FALSE, 0 );

  paned = gtk_vpaned_new();
  gtk_box_pack_start( GTK_BOX( vbox ), paned, TRUE, TRUE, 0 );

  hbox = gtk_hbox_new( FALSE, 0 );
  gtk_paned_pack1(GTK_PANED(paned), hbox, TRUE, FALSE);

  vbox1 = gtk_vbox_new( FALSE, 0 );
  gtk_box_pack_start( GTK_BOX( hbox ), vbox1, FALSE, FALSE, 0 );

  hbox2 = gtk_hbox_new( FALSE, 0 );
  gtk_box_pack_start( GTK_BOX( vbox1 ), hbox2, FALSE, FALSE, 0 );

  frame = gtk_frame_new( NULL );
  gtk_box_pack_start( GTK_BOX( hbox2 ), frame, FALSE, FALSE, 0 );
  
  overview_canvas	      = gtk_drawing_area_new();
  gtk_container_add( GTK_CONTAINER( frame ), overview_canvas );
  gtk_drawing_area_size( GTK_DRAWING_AREA( overview_canvas ), 160, 100 );

  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "expose_event",
        	      (GtkSignalFunc) overview_canvas_expose, NULL );
  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "button_press_event",
        	      (GtkSignalFunc) butt_down_overviewcanvas, NULL );
  gtk_widget_set_events( overview_canvas, GDK_EXPOSURE_MASK
        			      | GDK_BUTTON_PRESS_MASK );


  {   /* Info on player's civilization */
      GtkWidget *vbox4;

      frame = gtk_frame_new(NULL);
      gtk_box_pack_start(GTK_BOX(vbox1), frame, FALSE, FALSE, 0);
      main_frame_civ_name=frame;

      vbox4 = gtk_vbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(frame), vbox4);
/*
      main_label_info_ebox = gtk_event_box_new ();
      gtk_box_pack_start(GTK_BOX(vbox4), main_label_info_ebox, FALSE, FALSE, 0);

      gtk_widget_set_events (main_label_info_ebox, GDK_BUTTON_PRESS_MASK);
*/
      main_label_info = gtk_label_new("\n\n\n\n");
      gtk_box_pack_start(GTK_BOX(vbox4), main_label_info, FALSE, FALSE, 0);
/*
      gtk_container_add (GTK_CONTAINER (main_label_info_ebox), main_label_info);
      gtk_signal_connect (GTK_OBJECT(main_label_info_ebox),"button_press_event",
			  GTK_SIGNAL_FUNC (show_info_popup), NULL);
*/
  }

  { /* production status */
      GtkWidget *box4;

      /* make a box so the table will be centered */
      box4 = gtk_hbox_new( FALSE, 0 );
      gtk_box_pack_start(GTK_BOX(vbox1), box4, FALSE, FALSE, 5);

      table = gtk_table_new(10, 2, FALSE);
      gtk_table_set_row_spacing(GTK_TABLE(table), 0, 0);
      gtk_table_set_col_spacing(GTK_TABLE(table), 0, 0);
      gtk_box_pack_start(GTK_BOX(box4), table, TRUE, FALSE, 0);

      for (i=0 ; i<10 ; i++)
      {
          ebox = gtk_event_box_new();
          gtk_table_attach_defaults(GTK_TABLE(table), ebox, i, i + 1, 0, 1);

          gtk_widget_set_events( ebox, GDK_BUTTON_PRESS_MASK );
          gtk_signal_connect( GTK_OBJECT( ebox ), "button_press_event",
              GTK_SIGNAL_FUNC(taxrates_callback), (gpointer)i);

          econ_label[i] = gtk_pixmap_new(get_tile_sprite(SUN_TILES)->pixmap,NULL);
          gtk_container_add( GTK_CONTAINER( ebox ), econ_label[i] );
      }

      bulb_label      = gtk_pixmap_new(get_tile_sprite(BULB_TILES)->pixmap,NULL);
      sun_label       = gtk_pixmap_new(get_tile_sprite(SUN_TILES)->pixmap,NULL);
      government_label= gtk_pixmap_new(get_tile_sprite(GOVERNMENT_TILES)->pixmap,NULL);
      timeout_label   = gtk_label_new("");

      for (i=5; i<9; i++)
      {
          frame = gtk_frame_new(NULL);
          gtk_widget_set_usize(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);

	  if (i==8)
            gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 2, 1, 2);
	  else
            gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 1, 1, 2);

          switch (i)
          {
          case 5: gtk_container_add( GTK_CONTAINER( frame ), bulb_label );
              break;
          case 6: gtk_container_add( GTK_CONTAINER( frame ), sun_label );
              break;
          case 7: gtk_container_add( GTK_CONTAINER( frame ), government_label );
              break;
          case 8: gtk_container_add( GTK_CONTAINER( frame ), timeout_label );
              break;
          }
      }

  turn_done_button = gtk_button_new_with_label( "Turn Done" );
  gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 5, 1, 2);
  }

  { /* selected unit status */
    GtkWidget *ubox;

    unit_info_frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox1), unit_info_frame, FALSE, FALSE, 5);
    
    unit_info_label = gtk_label_new("\n\n\n");
    gtk_container_add(GTK_CONTAINER(unit_info_frame), unit_info_label);

    ubox = gtk_hbox_new(FALSE,0);
    gtk_box_pack_start(GTK_BOX(vbox1), ubox, FALSE, FALSE, 0);

    table = gtk_table_new( 2, 5, FALSE );
    gtk_box_pack_start(GTK_BOX(ubox),table,FALSE,FALSE,5);

    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    unit_pixmap=gtk_new_pixmap(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    gtk_table_attach_defaults( GTK_TABLE(table), unit_pixmap, 0, 1, 0, 1 );
    gtk_clear_pixmap(unit_pixmap);

    for (i=0; i<num_units_below; i++)
    {
      unit_below_pixmap[i]=gtk_new_pixmap(NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      gtk_table_attach_defaults(GTK_TABLE(table), unit_below_pixmap[i],
    	  i, i+1, 1, 2);
      gtk_widget_set_usize(unit_below_pixmap[i],
            NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      gtk_clear_pixmap(unit_below_pixmap[i]);
    }

    more_arrow_pixmap=gtk_pixmap_new(get_tile_sprite(RIGHT_ARROW_TILE)->pixmap, NULL);
    gtk_table_attach_defaults(GTK_TABLE(table), more_arrow_pixmap, 4, 5, 1, 2);
  }

  vbox2 = gtk_vbox_new( FALSE, 0 );
  gtk_box_pack_start( GTK_BOX( hbox ), vbox2, TRUE, TRUE, 0 );

  table = gtk_table_new( 2, 2, FALSE );
  gtk_box_pack_start( GTK_BOX( vbox2 ), table, TRUE, TRUE, 0 );

  frame = gtk_frame_new( NULL );
  gtk_table_attach(GTK_TABLE(table), frame,
		0, 1, 0, 1,
		GTK_EXPAND|GTK_SHRINK|GTK_FILL,
		GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0 );

  map_canvas		      = gtk_drawing_area_new();

  gtk_widget_set_events(map_canvas, GDK_EXPOSURE_MASK
        			      |GDK_BUTTON_PRESS_MASK
        			      |GDK_KEY_PRESS_MASK);

  gtk_drawing_area_size(GTK_DRAWING_AREA(map_canvas), 510, 300);
  gtk_container_add(GTK_CONTAINER(frame), map_canvas);

  map_horizontal_scrollbar = gtk_hscrollbar_new(NULL);
  gtk_table_attach(GTK_TABLE(table), map_horizontal_scrollbar, 0, 1, 1, 2,
		GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

  map_vertical_scrollbar = gtk_vscrollbar_new(NULL);
  gtk_table_attach(GTK_TABLE(table), map_vertical_scrollbar, 1, 2, 0, 1,
		0, GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0);

  /* Event signals */
  gtk_signal_connect( GTK_OBJECT( map_canvas ), "expose_event",
        	      GTK_SIGNAL_FUNC( map_canvas_expose ), NULL );
  gtk_signal_connect( GTK_OBJECT( map_canvas ), "button_press_event",
        	      GTK_SIGNAL_FUNC( butt_down_mapcanvas ), NULL );
  gtk_signal_connect( GTK_OBJECT( map_canvas ), "button_press_event",
        	      GTK_SIGNAL_FUNC( butt_down_wakeup ), NULL );

  {   /* the message window */
      GtkWidget *text, *vbox3;

      vbox3 = gtk_vbox_new(FALSE, 5);
      gtk_paned_pack2(GTK_PANED(paned), vbox3, FALSE, FALSE);

      hbox = gtk_hbox_new(FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox3), hbox, TRUE, TRUE, 0);

      text = gtk_widget_new (GTK_TYPE_TEXT,
        		     "GtkWidget::parent", hbox,
        		     "GtkText::word_wrap", TRUE,
        		     NULL);
      gtk_text_set_editable (GTK_TEXT (text), FALSE);

      text_scrollbar = gtk_vscrollbar_new (GTK_TEXT(text)->vadj);

      gtk_widget_realize (text);
      main_message_area = GTK_TEXT (text);

      gtk_box_pack_start (GTK_BOX(hbox), text_scrollbar, FALSE, FALSE, 0);

      gtk_widget_realize (text_scrollbar);

      append_output_window(
      "Freeciv is free software and you are welcome to distribute copies of"
      " it\nunder certain conditions; See the \"Copying\" item on the Help"
      " menu.\nNow.. Go give'em hell!" );

      /* the chat line */
      inputline = gtk_entry_new();
      gtk_box_pack_start( GTK_BOX( vbox3 ), inputline, FALSE, FALSE, 0 );
  }
  gtk_signal_connect(GTK_OBJECT(toplevel), "key_press_event",
      GTK_SIGNAL_FUNC(keyboard_handler), NULL);
/*  gtk_key_snooper_install(keyboard_handler, NULL);*/

  gtk_widget_show_all(vbox);
}


/**************************************************************************
...
**************************************************************************/
int ui_main(int argc, char **argv)
{
  GdkBitmap	      *icon_bitmap;

  parse_options(&argc, &argv);

  /* Process GTK arguments */
  gtk_init(&argc, &argv);

  boot_help_texts();	       /* after log_init */

  display_color_type=get_visual();

  toplevel = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize (toplevel);

  root_window=toplevel->window;
  init_color_system();
  notify_dialog_style=gtk_style_new();
  notify_dialog_style->font=gdk_font_load (NOTIFY_DIALOG_FONT);

  gtk_signal_connect( GTK_OBJECT(toplevel),"delete_event",
      GTK_SIGNAL_FUNC(gtk_main_quit),NULL );

  gtk_window_set_title (GTK_WINDOW (toplevel), "FreeCiv");

  icon_bitmap = gdk_bitmap_create_from_data( root_window,
        				     freeciv_bits,
        				     freeciv_width, freeciv_height );
  gdk_window_set_icon( root_window, NULL, icon_bitmap, icon_bitmap );

  civ_gc = gdk_gc_new(root_window);

  if (!(main_font=gdk_font_load(CITY_NAMES_FONT)))
  {
      freelog(LOG_FATAL, "failed loading font: " CITY_NAMES_FONT);
      exit(1);
  }

  fill_bg_gc = gdk_gc_new(root_window);
  fill_tile_gc = gdk_gc_new(root_window);
  gdk_gc_set_fill(fill_tile_gc, GDK_STIPPLED);

  {
    unsigned char d1[]={0x03,0x0c,0x03,0x0c};
    unsigned char d2[]={0x08,0x02,0x08,0x02};
    gray50=gdk_bitmap_create_from_data(root_window,d1,4,4);
    gray25=gdk_bitmap_create_from_data(root_window,d2,4,4);
  }

  {
    GdkColor pixel;
    
    mask_bitmap=gdk_pixmap_new(root_window, 1, 1, 1);

    mask_fg_gc=gdk_gc_new(mask_bitmap);
    pixel.pixel=1;
    gdk_gc_set_foreground(mask_fg_gc, &pixel);
    gdk_gc_set_function(mask_fg_gc, GDK_OR);

    mask_bg_gc=gdk_gc_new(mask_bitmap);
    pixel.pixel=0;
    gdk_gc_set_foreground(mask_bg_gc, &pixel);
  }

  load_tile_gfx();

  /* 135 below is rough value (could be more intelligent) --dwp */
  num_units_below = 135/(int)NORMAL_TILE_WIDTH;
  num_units_below = MIN(num_units_below,MAX_NUM_UNITS_BELOW);
  num_units_below = MAX(num_units_below,1);
  
  setup_widgets();
  load_intro_gfx();

  gtk_signal_connect(GTK_OBJECT(inputline), "activate",
	GTK_SIGNAL_FUNC(inputline_return), NULL);


  gtk_signal_connect(GTK_OBJECT(turn_done_button), "clicked",
        	      GTK_SIGNAL_FUNC(end_turn_callback), NULL);

  gtk_widget_show(toplevel);

  gtk_interval_id=gtk_timeout_add(500, timer_callback, NULL);

  map_canvas_store	      = gdk_pixmap_new(root_window,
        			map_canvas_store_twidth*NORMAL_TILE_WIDTH,
        			map_canvas_store_theight*NORMAL_TILE_HEIGHT,
        			-1);

  overview_canvas_store_width = 2*80;
  overview_canvas_store_height= 2*50;

  overview_canvas_store       = gdk_pixmap_new(root_window,
        			overview_canvas_store_width,
        			overview_canvas_store_height, 
        			-1);

  gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  gdk_draw_rectangle(overview_canvas_store, fill_bg_gc, TRUE, 0, 0,
              overview_canvas_store_width, overview_canvas_store_height);

  single_tile_pixmap	      = gdk_pixmap_new(root_window, 
        			NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, -1);

  strcpy(our_capability, CAPABILITY);
  if (getenv("FREECIV_CAPS"))
      strcpy(our_capability, getenv("FREECIV_CAPS"));

  load_options();

  /* This seed is not saved anywhere; randoms in the client should
     have cosmetic effects only (eg city name suggestions).  --dwp */
  mysrand(time(NULL));

  set_client_state(CLIENT_PRE_GAME_STATE);

  gtk_main();
  	
  return 0;
}

/**************************************************************************
...
**************************************************************************/
void remove_net_input(void)
{
  gdk_input_remove(gdk_input_id);
}

/**************************************************************************
...
**************************************************************************/
static gint
show_info_button_release(GtkWidget *w, GdkEventButton *event)
{
  gtk_grab_remove(w);
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
gint show_info_popup(GtkWidget *w, GdkEventButton *ev)
{
  if(ev->button==1) {
    GtkWidget *p;
    char buf[512];
    
    sprintf(buf, "%s People\nYear: %s\nGold: %d\nNet Income: %d\nTax:%d Lux:%d Sci:%d\nResearching %s: %d/%d",
	    int_to_text(civ_population(game.player_ptr)),
	    textyear(game.year),
	    game.player_ptr->economic.gold,
	    turn_gold_difference,
	    game.player_ptr->economic.tax,
	    game.player_ptr->economic.luxury,
	    game.player_ptr->economic.science,
	    
	    advances[game.player_ptr->research.researching].name,
	    game.player_ptr->research.researched,
	    research_time(game.player_ptr));
    
    p=gtk_window_new(GTK_WINDOW_POPUP);

    gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", p,
        			   "GtkLabel::label", buf,
				   "GtkWidget::visible", TRUE,
        			   NULL);
    gtk_widget_realize(p);
    
    gtk_widget_popup(p, ev->x_root-p->requisition.width/2,
			ev->y_root-p->requisition.height/2);
    gdk_pointer_grab(p->window, TRUE, GDK_BUTTON_RELEASE_MASK,
		     NULL, NULL, ev->time);
    gtk_grab_add(p);

    gtk_signal_connect_after(GTK_OBJECT(p), "button_release_event",
                             GTK_SIGNAL_FUNC(show_info_button_release), NULL);
  }
  return TRUE;
}



/**************************************************************************
...
**************************************************************************/
void enable_turn_done_button(void)
{
  if(game.player_ptr->ai.control && !ai_manual_turn_done)
    user_ended_turn();
  gtk_widget_set_sensitive(turn_done_button,
		!game.player_ptr->ai.control||ai_manual_turn_done);

  if(sound_bell_at_new_turn)
    gdk_beep();
}


/**************************************************************************
...
**************************************************************************/
void end_turn_callback(GtkWidget *widget, gpointer data)
{
    gtk_widget_set_sensitive(turn_done_button, FALSE);
    user_ended_turn();
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
