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
#include <time.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "log.h"
#include "game.h"
#include "map.h"
#include "shared.h"
#include "version.h"

#include "chatline.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg.h"
#include "control.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "helpdata.h"		/* boot_help_texts() */
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "spaceshipdlg.h"

#include "gui_main.h"

#include "freeciv.ico"

#define NOTIFY_DIALOG_FONT	"-b&h-lucidatypewriter-bold-r-normal-*-12-*-*-*-*-*-*-*"
#define FIXED_10_BFONT		"-b&h-lucidatypewriter-bold-r-normal-*-10-*-*-*-*-*-*-*"

/*void file_quit_cmd_callback( GtkWidget *widget, gpointer data )*/
void game_rates( GtkWidget *widget, gpointer data );

extern SPRITE *		intro_gfx_sprite;
extern SPRITE *		radar_gfx_sprite;



extern char	server_host	[];
extern char	name		[];
extern int	server_port;
extern char *	tile_set_dir;

extern enum client_states	client_state;

extern int			seconds_to_turndone;

extern int			last_turn_gold_amount;
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
GtkStyle *	city_dialog_style;

GdkWindow *	root_window;


enum Display_color_type		display_color_type;

extern int	num_units_below;

#ifdef UNUSED  /* used in commented-out code */
static gint show_info_popup(GtkWidget *w, GdkEventButton *ev);
#endif

static gint timer_callback(gpointer data);


/**************************************************************************
  Print the usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(const char *argv0)
{
  fprintf(stderr, "Usage: %s [option ...]\nValid options are:\n", argv0);
  fprintf(stderr, "  -h, --help\t\tPrint a summary of the options.\n");
  fprintf(stderr, "  -l, --log=FILE\tUse FILE as logfile.\n");
  fprintf(stderr, "  -N, --Name=NAME\tUse NAME.\n");
  fprintf(stderr, "  -p, --port=PORT\tConnect to PORT.\n");
  fprintf(stderr, "  -s, --server=SERVER\tConnect to the server SERVER.\n");
#ifdef DEBUG
  fprintf(stderr, "  -d, --debug=LEVEL\tSet debug log LEVEL (0,1,2,3,"
	                                          "or 3:file1,min,max:...)\n");
#else
  fprintf(stderr, "  -d, --debug=LEVEL\tSet debug log LEVEL (0,1,2).\n");
#endif
  fprintf(stderr, "  -t, --tiles=DIR\tLook in directory DIR for the tiles.\n");
  fprintf(stderr, "  -v, --version\t\tPrint the version number.\n");
}

/**************************************************************************
...
**************************************************************************/
static void parse_options(int argc, char **argv)
{
  char logfile[512]="";
  char *option=NULL; /* really use as a pointer */
  int loglevel;
  int i;

  /* set default argument values */
  loglevel=LOG_NORMAL;

  {
    struct passwd *password=getpwuid(getuid());

    if (password)
      strcpy(name, password->pw_name);
    else {
      /* freelog not yet initialised here --dwp */
      fprintf(stderr, "Your getpwuid call failed.  Please report this.");
      strcpy(name, "operator 00000");
      sprintf(name+9, "%05i",(int)getuid());
   }
  }

  server_port=DEFAULT_SOCK_PORT;

  strcpy(server_host, "localhost");

  i = 1;
  while (i < argc)
  {
    if (is_option("--help", argv[i]))
      {
      print_usage(argv[0]);
      exit(0);
      }
    else if (is_option("--version",argv[i]))
      {
      fprintf(stderr, "%s\n", FREECIV_NAME_VERSION);
      exit(0);
      }
    else if ((option = get_option("--log",argv,&i,argc)) != NULL)
      strcpy(logfile,option);
    else if ((option = get_option("--name",argv,&i,argc)) != NULL)
      strcpy(name, option);
    else if ((option = get_option("--port",argv,&i,argc)) != NULL)
      server_port=atoi(option);
    else if ((option = get_option("--server",argv,&i,argc)) != NULL)
      strcpy(server_host, option);
    else if ((option = get_option("--debug",argv,&i,argc)) != NULL)
    {
      loglevel=log_parse_level_str(option);
      if (loglevel==-1) {
        exit(1);
      }
    }
    else if ((option = get_option("--tiles",argv,&i,argc)) != NULL)
      tile_set_dir=option;
    else freelog(LOG_VERBOSE,"Unrecognized option %s\n",argv[i]);
    i += 1;
  }

  log_init(logfile, loglevel, NULL);
}


/**************************************************************************
...
**************************************************************************/
static gint keyboard_handler(GtkWidget *widget, GdkEventKey *event)
{
    if (GTK_WIDGET_HAS_FOCUS(inputline))
	return FALSE;

    switch (event->keyval)
    {
    case GDK_8:
    case GDK_KP_8:
    case GDK_KP_Up:		key_unit_north();		break;
    case GDK_9:
    case GDK_KP_9:
    case GDK_KP_Page_Up:	key_unit_north_east();		break;
    case GDK_6:
    case GDK_KP_6:
    case GDK_KP_Right:		key_unit_east();		break;
    case GDK_3:
    case GDK_KP_3:
    case GDK_KP_Page_Down:	key_unit_south_east();		break;
    case GDK_2:
    case GDK_KP_2:
    case GDK_KP_Down:		key_unit_south();		break;
    case GDK_1:
    case GDK_KP_1:
    case GDK_KP_End:		key_unit_south_west();		break;
    case GDK_4:
    case GDK_KP_4:
    case GDK_KP_Left:		key_unit_west();		break;
    case GDK_7:
    case GDK_KP_7:
    case GDK_KP_Home:		key_unit_north_west();		break;
    case GDK_KP_Begin:
    case GDK_5:
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

static void tearoff_callback (GtkWidget *but, GtkWidget *box)
{
    GtkWidget *w, *p;

    if (GTK_TOGGLE_BUTTON (but)->active)
    {
	w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_MOUSE);

	gtk_object_set_user_data (GTK_OBJECT (w), box->parent);
	gtk_widget_reparent (box, w);
	gtk_widget_show (w);
    }
    else
    {
        w = box->parent;
	p = gtk_object_get_user_data (GTK_OBJECT (w));
	gtk_widget_reparent (box, p);
	gtk_widget_destroy (w);
    }
}

static GtkWidget *detached_widget_new(GtkWidget **avbox)
{
    GtkWidget *ahbox, *b, *sep;

    ahbox = gtk_hbox_new( FALSE, 2 );

    b = gtk_toggle_button_new();
    gtk_box_pack_start( GTK_BOX( ahbox ), b, FALSE, FALSE, 0 );
    gtk_signal_connect( GTK_OBJECT(b), "clicked",
			GTK_SIGNAL_FUNC( tearoff_callback ), ahbox);

    /* cosmetic effects */
    sep = gtk_vseparator_new();
    gtk_container_add( GTK_CONTAINER( b ), sep );

    *avbox = gtk_vbox_new( FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( ahbox ), *avbox, TRUE, TRUE, 0 );
    return ahbox;
}

/**************************************************************************
...
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget	      *vbox;
  GtkWidget	      *hbox;
  GtkWidget	      *vbox1;
  GtkWidget           *avbox;
  GtkWidget	      *frame;

  GtkWidget	      *menubar;
  GtkWidget	      *table;
  GtkWidget	      *ebox, *paned;
  GtkStyle	      *text_style;
  int		       i;

  vbox = gtk_vbox_new( FALSE, 5 );
  gtk_container_add( GTK_CONTAINER( toplevel ), vbox );
  
  setup_menus( toplevel, &menubar );
  gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

  paned = gtk_vpaned_new();
  gtk_box_pack_start( GTK_BOX( vbox ), paned, TRUE, TRUE, 0 );

  hbox = gtk_hbox_new( FALSE, 0 );
  gtk_paned_pack1(GTK_PANED(paned), hbox, TRUE, FALSE);

  vbox1 = gtk_vbox_new( FALSE, 3 );
  gtk_box_pack_start( GTK_BOX( hbox ), vbox1, FALSE, FALSE, 0 );

  gtk_container_add(GTK_CONTAINER(vbox1), detached_widget_new (&avbox));

  overview_canvas	      = gtk_drawing_area_new();

  gtk_widget_set_events( overview_canvas, GDK_EXPOSURE_MASK
        			      | GDK_BUTTON_PRESS_MASK );

  gtk_drawing_area_size( GTK_DRAWING_AREA( overview_canvas ), 160, 100 );
  gtk_box_pack_start(GTK_BOX(avbox), overview_canvas, TRUE, TRUE, 0);

  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "expose_event",
        	      (GtkSignalFunc) overview_canvas_expose, NULL );
  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "button_press_event",
        	      (GtkSignalFunc) butt_down_overviewcanvas, NULL );

  gtk_container_add(GTK_CONTAINER(vbox1), detached_widget_new (&avbox));

  {   /* Info on player's civilization */
      GtkWidget *vbox4;

      frame = gtk_frame_new(NULL);
      gtk_box_pack_start(GTK_BOX(avbox), frame, FALSE, FALSE, 0);

      main_frame_civ_name=frame;

      vbox4 = gtk_vbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(frame), vbox4);
/*
      main_label_info_ebox = gtk_event_box_new ();
      gtk_widget_set_events (main_label_info_ebox, GDK_BUTTON_PRESS_MASK);

      gtk_box_pack_start(GTK_BOX(vbox4), main_label_info_ebox, FALSE, FALSE, 0);
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
      gtk_box_pack_start(GTK_BOX(avbox), box4, FALSE, FALSE, 0);

      table = gtk_table_new(10, 2, FALSE);
      gtk_table_set_row_spacing(GTK_TABLE(table), 0, 0);
      gtk_table_set_col_spacing(GTK_TABLE(table), 0, 0);
      gtk_box_pack_start(GTK_BOX(box4), table, TRUE, FALSE, 0);

      for (i=0 ; i<10 ; i++)
      {
          ebox = gtk_event_box_new();
          gtk_widget_set_events( ebox, GDK_BUTTON_PRESS_MASK );

          gtk_table_attach_defaults(GTK_TABLE(table), ebox, i, i + 1, 0, 1);

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
  gtk_widget_set_style(turn_done_button, gtk_style_copy(turn_done_button->style));
  gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 5, 1, 2);
  }
 
  { /* selected unit status */
    GtkWidget *ubox;


    unit_info_frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(avbox), unit_info_frame, FALSE, FALSE, 0);
    
    unit_info_label = gtk_label_new("\n\n\n");
    gtk_container_add(GTK_CONTAINER(unit_info_frame), unit_info_label);

    ubox = gtk_hbox_new(FALSE,0);
    gtk_box_pack_start(GTK_BOX(avbox), ubox, FALSE, FALSE, 0);

    table = gtk_table_new( 2, 5, FALSE );
    gtk_box_pack_start(GTK_BOX(ubox),table,FALSE,FALSE,5);

    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    unit_pixmap=gtk_pixcomm_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
    gtk_table_attach_defaults( GTK_TABLE(table), unit_pixmap, 0, 1, 0, 1 );
    gtk_pixcomm_clear(GTK_PIXCOMM(unit_pixmap), TRUE);

    for (i=0; i<num_units_below; i++)
    {
      unit_below_pixmap[i]=gtk_pixcomm_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      gtk_table_attach_defaults(GTK_TABLE(table), unit_below_pixmap[i],
    	  i, i+1, 1, 2);
      gtk_widget_set_usize(unit_below_pixmap[i],
            NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);
      gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);
    }

    more_arrow_pixmap=gtk_pixmap_new(get_tile_sprite(RIGHT_ARROW_TILE)->pixmap, NULL);
    gtk_table_attach_defaults(GTK_TABLE(table), more_arrow_pixmap, 4, 5, 1, 2);
  }

  table = gtk_table_new( 2, 2, FALSE );
  gtk_box_pack_start( GTK_BOX( hbox ), table, TRUE, TRUE, 0 );

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
      GtkWidget *text;

      gtk_paned_pack2(GTK_PANED(paned), detached_widget_new (&avbox),
								TRUE, TRUE);

      hbox = gtk_hbox_new(FALSE, 0);
      gtk_box_pack_start(GTK_BOX(avbox), hbox, TRUE, TRUE, 0);

      text = gtk_text_new(NULL, NULL);
      gtk_box_pack_start(GTK_BOX(hbox), text, TRUE, TRUE, 0);
      gtk_widget_set_usize(text, 600, 100);
      gtk_text_set_editable (GTK_TEXT (text), FALSE);

      text_scrollbar = gtk_vscrollbar_new (GTK_TEXT(text)->vadj);
      gtk_widget_realize (text);

      /* hack to make insensitive text readable */
      text_style = gtk_style_copy (text->style);
      text_style->base[GTK_STATE_INSENSITIVE]=
					text_style->base[GTK_STATE_NORMAL];
      text_style->text[GTK_STATE_INSENSITIVE]=
					text_style->text[GTK_STATE_NORMAL];
      gtk_widget_set_style (text, text_style);

      main_message_area = GTK_TEXT (text);

      gtk_box_pack_start (GTK_BOX(hbox), text_scrollbar, FALSE, FALSE, 0);
      gtk_widget_realize (text_scrollbar);

      append_output_window(
      "Freeciv is free software and you are welcome to distribute copies of"
      " it\nunder certain conditions; See the \"Copying\" item on the Help"
      " menu.\nNow.. Go give'em hell!" );

      /* the chat line */
      inputline = gtk_entry_new();
      gtk_box_pack_start( GTK_BOX( avbox ), inputline, FALSE, FALSE, 0 );
  }
  gtk_signal_connect(GTK_OBJECT(toplevel), "key_press_event",
      GTK_SIGNAL_FUNC(keyboard_handler), NULL);
/*  gtk_key_snooper_install(keyboard_handler, NULL);*/

  gtk_widget_show_all(vbox);
}

/**************************************************************************
...
**************************************************************************/
void ui_main(int argc, char **argv)
{
  GdkBitmap	      *icon_bitmap;


  parse_options(argc, argv);
  /* GTK withdraw gtk options */
  /* Process GTK arguments */
  gtk_init(&argc, &argv);

  boot_help_texts();	       /* after log_init */

  display_color_type=get_visual();

  toplevel = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize (toplevel);

  root_window=toplevel->window;
  init_color_system();

  notify_dialog_style=gtk_style_new();
  gdk_font_unref (notify_dialog_style->font);
  notify_dialog_style->font=gdk_font_load (NOTIFY_DIALOG_FONT);
  gdk_font_ref (notify_dialog_style->font);

  city_dialog_style=gtk_style_new();
  gdk_font_unref (city_dialog_style->font);
  city_dialog_style->font=gdk_font_load (FIXED_10_BFONT);
  gdk_font_ref (city_dialog_style->font);

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
  load_cursors();

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

  load_options();

  set_client_state(CLIENT_PRE_GAME_STATE);

  gtk_main();
}

/**************************************************************************
...
**************************************************************************/
void remove_net_input(void)
{
  gdk_input_remove(gdk_input_id);
}

#ifdef UNUSED  /* used in show_info_popup() below */
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
#endif /* UNUSED */

#ifdef UNUSED  /* used in commented-out code */
/**************************************************************************
...
**************************************************************************/
static gint show_info_popup(GtkWidget *w, GdkEventButton *ev)
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
#endif /* UNUSED */



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
static gint timer_callback(gpointer data)
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
