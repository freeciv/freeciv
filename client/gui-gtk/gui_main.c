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

#include <assert.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
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
#include "resources.h"
#include "tilespec.h"

#include "gui_main.h"

#include "freeciv.ico"

const char *client_string = "gui-gtk";

/*void file_quit_cmd_callback( GtkWidget *widget, gpointer data )*/
void game_rates( GtkWidget *widget, gpointer data );

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
GtkWidget *	top_vbox;

GdkGC *		civ_gc;
GdkGC *		fill_bg_gc;
GdkGC *         thin_line_gc, *thick_line_gc;
GdkGC *		fill_tile_gc;
GdkPixmap *	gray50, *gray25, *black50;
GdkGC *		mask_fg_gc;
GdkGC *		mask_bg_gc;
GdkPixmap *	mask_bitmap;

GdkFont *	main_font;
GdkFont *	city_productions_font;

GtkWidget *	main_frame_civ_name;
GtkWidget *	main_label_info;
GtkWidget *	main_label_info_ebox;
GtkText *	main_message_area;

GtkWidget *	econ_label			[10];
GtkWidget *	bulb_label;
GtkWidget *	sun_label;
GtkWidget *	flake_label;
GtkWidget *	government_label;
GtkWidget *	timeout_label;
GtkWidget *	turn_done_button;

GtkWidget *	unit_pixmap;
GtkWidget *     unit_pixmap_button;
GtkWidget *	unit_below_pixmap		[MAX_NUM_UNITS_BELOW];
GtkWidget *     unit_below_pixmap_button        [MAX_NUM_UNITS_BELOW];
GtkWidget *	more_arrow_pixmap;

static gint	gtk_interval_id;

GtkWidget *	unit_info_label, *unit_info_frame;
GtkWidget *	text_scrollbar;
GtkWidget *	inputline;
static int inputline_cache = -1;

GtkWidget *	map_horizontal_scrollbar, *map_vertical_scrollbar;
gint		gdk_input_id;

GdkWindow *	root_window;

/* ids of the units icons in information display: (or 0) */
static int unit_ids[MAX_NUM_UNITS_BELOW];  


enum Display_color_type		display_color_type;

static gint show_info_popup(GtkWidget *w, GdkEventButton *ev);
static gint timer_callback(gpointer data);


/**************************************************************************
  Print extra usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(const char *argv0)
{
  /* add client-specific usage information here */
  fprintf(stderr, _("Report bugs to <%s>.\n"), BUG_EMAIL_ADDRESS);
}

/**************************************************************************
...
**************************************************************************/
static void parse_options(int argc, char **argv)
{
  int i;

  i = 1;
  while (i < argc)
  {
    if (is_option("--help", argv[i]))
    {
      print_usage(argv[0]);
      exit(0);
    }
    i += 1;
  }
}


/**************************************************************************
...
**************************************************************************/
static gint keyboard_handler(GtkWidget *widget, GdkEventKey *event)
{
  int *cache_index = gtk_object_get_data(GTK_OBJECT(inputline), "cache_current");
  GList *cache = gtk_object_get_data(GTK_OBJECT(inputline), "cache");

  if (GTK_WIDGET_HAS_FOCUS(inputline) || !GTK_WIDGET_IS_SENSITIVE(top_vbox)) {
    /* handle the up-arrow keypress to display history */
    if (event->keyval == GDK_Up) {
      if (g_list_length(cache) - 1 != *cache_index) {
	GList *item = g_list_nth(cache, ++(*cache_index));
	gtk_entry_set_text(GTK_ENTRY(inputline), item->data);
      }
      /* run the other signal handlers */
      gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
      /* return TRUE to avoid the focus is lost (propagates to other widgets) */
      return TRUE;
    }

    /* handle the down-arrow keypress to display history */
    if (event->keyval == GDK_Down) {
      if (*cache_index) {
	GList *item = g_list_nth(cache, --(*cache_index));
	gtk_entry_set_text(GTK_ENTRY(inputline), item->data);
      }
      /* run the other signal handlers */
      gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
      /* return TRUE to avoid the focus is lost (propagates to other widgets) */
      return TRUE;
    }

    return FALSE;
  }

  if (is_isometric) {
    switch (event->keyval) {
    case GDK_Up:
    case GDK_8:
    case GDK_KP_8:
    case GDK_KP_Up:		key_move_north_west();		break;
    case GDK_9:
    case GDK_KP_9:
    case GDK_Page_Up:
    case GDK_KP_Page_Up:	key_move_north();		break;
    case GDK_Right:
    case GDK_6:
    case GDK_KP_6:
    case GDK_KP_Right:		key_move_north_east();		break;
    case GDK_3:
    case GDK_KP_3:
    case GDK_Page_Down:
    case GDK_KP_Page_Down:	key_move_east();		break;
    case GDK_Down:
    case GDK_2:
    case GDK_KP_2:
    case GDK_KP_Down:		key_move_south_east();		break;
    case GDK_1:
    case GDK_KP_1:
    case GDK_End:
    case GDK_KP_End:		key_move_south();		break;
    case GDK_Left:
    case GDK_4:
    case GDK_KP_4:
    case GDK_KP_Left:		key_move_south_west();		break;
    case GDK_7:
    case GDK_KP_7:
    case GDK_Home:
    case GDK_KP_Home:		key_move_west();		break;

    case GDK_KP_Begin:
    case GDK_5:
    case GDK_KP_5:		focus_to_next_unit();		break;

    case GDK_Return:
    case GDK_KP_Enter:		key_end_turn();			break;

    case GDK_Escape:		key_cancel_action();		break;

    case GDK_t:			key_city_workers(widget,event);	break;
    default:							return FALSE;
    }
  } else {
    switch (event->keyval) {
    case GDK_Up:
    case GDK_8:
    case GDK_KP_8:
    case GDK_KP_Up:		key_move_north();		break;
    case GDK_9:
    case GDK_KP_9:
    case GDK_Page_Up:
    case GDK_KP_Page_Up:	key_move_north_east();		break;
    case GDK_Right:
    case GDK_6:
    case GDK_KP_6:
    case GDK_KP_Right:		key_move_east();		break;
    case GDK_3:
    case GDK_KP_3:
    case GDK_Page_Down:
    case GDK_KP_Page_Down:	key_move_south_east();		break;
    case GDK_Down:
    case GDK_2:
    case GDK_KP_2:
    case GDK_KP_Down:		key_move_south();		break;
    case GDK_1:
    case GDK_KP_1:
    case GDK_End:
    case GDK_KP_End:		key_move_south_west();		break;
    case GDK_Left:
    case GDK_4:
    case GDK_KP_4:
    case GDK_KP_Left:		key_move_west();		break;
    case GDK_7:
    case GDK_KP_7:
    case GDK_Home:
    case GDK_KP_Home:		key_move_north_west();		break;
    case GDK_KP_Begin:
    case GDK_5:
    case GDK_KP_5:		focus_to_next_unit();		break;

    case GDK_Return:
    case GDK_KP_Enter:		key_end_turn();			break;

    case GDK_Escape:		key_cancel_action();		break;

    case GDK_t:			key_city_workers(widget,event);	break;
    default:							return FALSE;
    }
  }

  gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
  return TRUE;
}

static gint tearoff_delete(GtkWidget *w, GdkEvent *ev, GtkWidget *box)
{
  GtkWidget *p;

  gtk_widget_hide(w);
  p = ((GtkBoxChild *)GTK_BOX(box)->children->data)->widget;
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(p), FALSE);

  p = gtk_object_get_user_data(GTK_OBJECT (w));
  gtk_widget_reparent(box, p);
  return TRUE;
}

static void tearoff_callback (GtkWidget *but, GtkWidget *box)
{
  GtkWidget *w;

  if (GTK_TOGGLE_BUTTON(but)->active)
  {
    w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w), _("Freeciv"));
    gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_MOUSE);
    gtk_signal_connect(GTK_OBJECT(w), "delete_event",
    		       GTK_SIGNAL_FUNC(tearoff_delete), box);

    gtk_object_set_user_data(GTK_OBJECT (w), box->parent);
    gtk_widget_reparent(box, w);
    gtk_widget_show(w);
  }
  else
  {
    w = box->parent;
    tearoff_delete(w, NULL, box);
    gtk_widget_destroy(w);
  }
}

static GtkWidget *detached_widget_new(void)
{
  return gtk_hbox_new(FALSE, 2);
}

static GtkWidget *detached_widget_fill(GtkWidget *ahbox)
{
  GtkWidget *b, *sep, *avbox;

  b = gtk_toggle_button_new();
  gtk_box_pack_start( GTK_BOX( ahbox ), b, FALSE, FALSE, 0 );
  gtk_signal_connect( GTK_OBJECT(b), "clicked",
        	      GTK_SIGNAL_FUNC( tearoff_callback ), ahbox);

  /* cosmetic effects */
  sep = gtk_vseparator_new();
  gtk_container_add( GTK_CONTAINER( b ), sep );

  avbox = gtk_vbox_new( FALSE, 0 );
  gtk_box_pack_start( GTK_BOX( ahbox ), avbox, TRUE, TRUE, 0 );
  return avbox;
}

static void select_pixmap_callback(GtkWidget *w, GdkEvent *ev, int i) 
{
  struct unit *punit;

  if (i == -1)  /* unit currently selected */
    return;
  if (unit_ids[i] == 0) /* no unit displayed at this place */
    return;
  punit=find_unit_by_id(unit_ids[i]);
  if(punit) { /* should always be true at this point */
    if (punit->owner == game.player_idx) {  /* may be non-true if alliance */
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      set_unit_focus(punit);
    }
  }
}

/**************************************************************************
...
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget	      *hbox;
  GtkWidget	      *vbox1;
  GtkWidget           *avbox;
  GtkWidget           *ahbox;
  GtkWidget	      *frame;

  GtkWidget	      *menubar;
  GtkWidget	      *table;
  GtkWidget	      *ebox, *paned;
  GtkStyle	      *text_style;
  int		       i;

  paned = gtk_vpaned_new();
  gtk_container_add(GTK_CONTAINER(toplevel), paned);

  top_vbox = gtk_vbox_new(FALSE, 5);
  gtk_paned_pack1(GTK_PANED(paned), top_vbox, TRUE, FALSE);
  
  setup_menus(toplevel, &menubar);
  gtk_box_pack_start(GTK_BOX(top_vbox), menubar, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(top_vbox), hbox, TRUE, TRUE, 0);

  vbox1 = gtk_vbox_new( FALSE, 3 );
  gtk_box_pack_start( GTK_BOX( hbox ), vbox1, FALSE, FALSE, 0 );

  ahbox = detached_widget_new();
  gtk_container_add(GTK_CONTAINER(vbox1), ahbox);
  avbox = detached_widget_fill(ahbox);

  overview_canvas	      = gtk_drawing_area_new();

  gtk_widget_set_events( overview_canvas, GDK_EXPOSURE_MASK
        			      | GDK_BUTTON_PRESS_MASK );

  gtk_drawing_area_size( GTK_DRAWING_AREA( overview_canvas ), 160, 100 );
  gtk_box_pack_start(GTK_BOX(avbox), overview_canvas, FALSE, FALSE, 0);

  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "expose_event",
        	      (GtkSignalFunc) overview_canvas_expose, NULL );
  gtk_signal_connect( GTK_OBJECT( overview_canvas ), "button_press_event",
        	      (GtkSignalFunc) butt_down_overviewcanvas, NULL );

  ahbox = detached_widget_new();
  gtk_container_add(GTK_CONTAINER(vbox1), ahbox);
  avbox = detached_widget_fill(ahbox);

  {   /* Info on player's civilization */
      GtkWidget *vbox4;

      frame = gtk_frame_new(NULL);
      gtk_box_pack_start(GTK_BOX(avbox), frame, FALSE, FALSE, 0);

      main_frame_civ_name=frame;

      vbox4 = gtk_vbox_new(FALSE, 0);
      gtk_container_add(GTK_CONTAINER(frame), vbox4);

      main_label_info_ebox = gtk_event_box_new ();
      gtk_widget_set_events (main_label_info_ebox, GDK_BUTTON_PRESS_MASK);

      gtk_box_pack_start(GTK_BOX(vbox4), main_label_info_ebox, FALSE, FALSE, 0);

      main_label_info = gtk_label_new("\n\n\n\n");
      gtk_container_add (GTK_CONTAINER (main_label_info_ebox), main_label_info);
      gtk_signal_connect (GTK_OBJECT(main_label_info_ebox),"button_press_event",
			  GTK_SIGNAL_FUNC (show_info_popup), NULL);
  }

  { /* production status */
      GtkWidget *box4;

      /* make a box so the table will be centered */
      box4 = gtk_hbox_new( FALSE, 0 );
      gtk_box_pack_start(GTK_BOX(avbox), box4, FALSE, FALSE, 0);

      table = gtk_table_new(3, 10, FALSE);
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

          econ_label[i] = gtk_pixmap_new(get_citizen_pixmap(i<5?1:2), NULL);
	  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(econ_label[i]), FALSE);
          gtk_container_add( GTK_CONTAINER( ebox ), econ_label[i] );
      }

      bulb_label      = gtk_pixmap_new(sprites.bulb[0]->pixmap, NULL);
      gtk_pixmap_set_build_insensitive(GTK_PIXMAP(bulb_label), FALSE);
      sun_label       = gtk_pixmap_new(sprites.warming[0]->pixmap, NULL);
      gtk_pixmap_set_build_insensitive(GTK_PIXMAP(sun_label), FALSE);
      flake_label     = gtk_pixmap_new(sprites.cooling[0]->pixmap, NULL);
      gtk_pixmap_set_build_insensitive(GTK_PIXMAP(flake_label), FALSE);
      government_label= gtk_pixmap_new(sprites.citizen[7]->pixmap, NULL);
      gtk_pixmap_set_build_insensitive(GTK_PIXMAP(government_label), FALSE);
      timeout_label   = gtk_label_new("");

      for (i=0; i<5; i++)
      {
          frame = gtk_frame_new(NULL);
          gtk_widget_set_usize(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);

	  if (i==4)
            gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 6, 1, 2);
	  else
            gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 1, 1, 2);

          switch (i)
          {
          case 0: gtk_container_add( GTK_CONTAINER( frame ), bulb_label );
              break;
          case 1: gtk_container_add( GTK_CONTAINER( frame ), sun_label );
              break;
          case 2: gtk_container_add( GTK_CONTAINER( frame ), flake_label );
              break;
          case 3: gtk_container_add( GTK_CONTAINER( frame ), government_label );
              break;
          case 4: gtk_container_add( GTK_CONTAINER( frame ), timeout_label );
              break;
          }
      }

      turn_done_button = gtk_button_new_with_label( _("Turn Done") );
      gtk_widget_set_style(turn_done_button, gtk_style_copy(turn_done_button->style));
      gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 10, 2, 3);
  }
 
  { /* selected unit status */
    GtkWidget *ubox;


    unit_info_frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(avbox), unit_info_frame, FALSE, FALSE, 0);
    
    unit_info_label = gtk_label_new("\n\n\n");
    gtk_container_add(GTK_CONTAINER(unit_info_frame), unit_info_label);

    ubox = gtk_hbox_new(FALSE,0);
    gtk_box_pack_start(GTK_BOX(avbox), ubox, FALSE, FALSE, 0);

    table = gtk_table_new( 2, num_units_below, FALSE );
    gtk_box_pack_start(GTK_BOX(ubox),table,FALSE,FALSE,5);

    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 2);

    unit_pixmap=gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    gtk_pixcomm_clear(GTK_PIXCOMM(unit_pixmap), TRUE);
    unit_pixmap_button=gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(unit_pixmap_button),unit_pixmap);
    gtk_table_attach_defaults( GTK_TABLE(table), unit_pixmap_button, 
       0, 1, 0, 1 );
    gtk_signal_connect(GTK_OBJECT(unit_pixmap_button), "button_press_event",
       GTK_SIGNAL_FUNC(select_pixmap_callback),(gpointer)(-1));

    for (i=0; i<num_units_below; i++)
    {
      unit_below_pixmap[i]=gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH,
					   UNIT_TILE_HEIGHT);
      unit_below_pixmap_button[i]=gtk_event_box_new();
      gtk_container_add(GTK_CONTAINER(unit_below_pixmap_button[i]),
        unit_below_pixmap[i]);
      gtk_signal_connect(GTK_OBJECT(unit_below_pixmap_button[i]), 
         "button_press_event",
        GTK_SIGNAL_FUNC(select_pixmap_callback),(gpointer)(i));
      
      gtk_table_attach_defaults(GTK_TABLE(table), unit_below_pixmap_button[i],
    	  i, i+1, 1, 2);
      gtk_widget_set_usize(unit_below_pixmap[i],
			   UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
      gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);
    }

    more_arrow_pixmap=gtk_pixmap_new(sprites.right_arrow->pixmap, NULL);
    gtk_pixmap_set_build_insensitive(GTK_PIXMAP(more_arrow_pixmap), FALSE);
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
        			   |GDK_KEY_PRESS_MASK
                                   |GDK_POINTER_MOTION_MASK);

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
  gtk_signal_connect(GTK_OBJECT(map_canvas), "motion_notify_event",
		     GTK_SIGNAL_FUNC(move_mapcanvas), NULL);

  {   /* the message window */
      GtkWidget *text;

      ahbox = detached_widget_new();
      gtk_paned_pack2(GTK_PANED(paned), ahbox, TRUE, TRUE);
      avbox = detached_widget_fill(ahbox);

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
      gtk_text_set_word_wrap(GTK_TEXT(text), 1);

      main_message_area = GTK_TEXT (text);

      gtk_box_pack_start (GTK_BOX(hbox), text_scrollbar, FALSE, FALSE, 0);
      gtk_widget_realize (text_scrollbar);

      set_output_window_text(
      _("Freeciv is free software and you are welcome to distribute copies of"
      " it\nunder certain conditions; See the \"Copying\" item on the Help"
      " menu.\nNow.. Go give'em hell!") );

      /* the chat line */
      inputline = gtk_entry_new();
      gtk_box_pack_start(GTK_BOX(avbox), inputline, FALSE, FALSE, 0);
      gtk_object_set_data(GTK_OBJECT(inputline), "cache", NULL);
      gtk_object_set_data(GTK_OBJECT(inputline), "cache_current", &inputline_cache);
  }
  gtk_signal_connect(GTK_OBJECT(toplevel), "key_press_event",
		     GTK_SIGNAL_FUNC(keyboard_handler), NULL);
  /*  gtk_key_snooper_install(keyboard_handler, NULL);*/

  gtk_widget_show_all(paned);
  gtk_widget_hide(more_arrow_pixmap);
}

/**************************************************************************
...
**************************************************************************/
void ui_main(int argc, char **argv)
{
  GdkBitmap *icon_bitmap;
  gchar *homedir, *rc_file;

  parse_options(argc, argv);

  /* tell GTK+ library which locale */

#ifdef HAVE_LOCALE_H
  /* Freeciv assumes that all the world uses ISO-8859-1.  This
     assumption is not quite true, but used to work more or less.
     This is a workaround until we can fix the problem properly
     in the next release.  */
#ifdef HAVE_PUTENV
  if (!strcmp(setlocale(LC_CTYPE, (const char *)NULL), "C"))
    putenv("LC_CTYPE=en_US.ISO8859-1");
#endif
#endif

  gtk_set_locale();

  /* GTK withdraw gtk options */
  /* Process GTK arguments */
  gtk_init(&argc, &argv);

  /* Load resources */
  gtk_rc_parse_string(fallback_resources);

  homedir = g_get_home_dir();	/* should i gfree() this also? --vasc */
  rc_file = g_strdup_printf("%s%s%s", homedir, G_DIR_SEPARATOR_S, "freeciv.rc");
  gtk_rc_parse(rc_file);
  g_free (rc_file);


  display_color_type=get_visual();

  toplevel = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize (toplevel);
  gtk_widget_set_name(toplevel, "Freeciv");

  root_window=toplevel->window;
  init_color_system();

  gtk_signal_connect( GTK_OBJECT(toplevel),"delete_event",
      GTK_SIGNAL_FUNC(gtk_main_quit),NULL );

  gtk_window_set_title (GTK_WINDOW (toplevel), _("Freeciv"));

  icon_bitmap = gdk_bitmap_create_from_data( root_window,
        				     freeciv_bits,
        				     freeciv_width, freeciv_height );
  gdk_window_set_icon( root_window, NULL, icon_bitmap, icon_bitmap );

  civ_gc = gdk_gc_new(root_window);

  if (!(main_font=gdk_font_load(city_names_font)))
  {
      freelog(LOG_FATAL, "failed loading font: %s", city_names_font);
      exit(1);
  }

  if (!(city_productions_font = gdk_font_load(city_productions_font_name))) {
    freelog(LOG_FATAL, "failed loading font: %s",
	    city_productions_font_name);
    exit(1);
  }

  fill_bg_gc = gdk_gc_new(root_window);
  if (is_isometric) {
    thin_line_gc = gdk_gc_new(root_window);
    thick_line_gc = gdk_gc_new(root_window);
    gdk_gc_set_line_attributes(thin_line_gc,
			       1,
			       GDK_LINE_SOLID,
			       GDK_CAP_NOT_LAST,
			       GDK_JOIN_MITER);
    gdk_gc_set_line_attributes(thick_line_gc,
			       2,
			       GDK_LINE_SOLID,
			       GDK_CAP_NOT_LAST,
			       GDK_JOIN_MITER);
  }

  fill_tile_gc = gdk_gc_new(root_window);
  gdk_gc_set_fill(fill_tile_gc, GDK_STIPPLED);

  {
    unsigned char d1[]={0x03,0x0c,0x03,0x0c};
    unsigned char d2[]={0x08,0x02,0x08,0x02};
    unsigned char d3[]={0xAA,0x55,0xAA,0x55};
    gray50=gdk_bitmap_create_from_data(root_window,d1,4,4);
    gray25=gdk_bitmap_create_from_data(root_window,d2,4,4);
    black50 = gdk_bitmap_create_from_data(root_window,d3,4,4);
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

  tilespec_load_tiles();

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

  single_tile_pixmap_width = UNIT_TILE_WIDTH;
  single_tile_pixmap_height = UNIT_TILE_HEIGHT;
  single_tile_pixmap	      = gdk_pixmap_new(root_window, 
					       single_tile_pixmap_width,
					       single_tile_pixmap_height, -1);

  load_options();

  set_client_state(CLIENT_PRE_GAME_STATE);

  gtk_main();
}

/**************************************************************************
...
**************************************************************************/
static void get_net_input(gpointer data, gint fid, GdkInputCondition condition)
{
  input_from_server(fid);
}

/**************************************************************************
...
**************************************************************************/
static void set_wait_for_writable_socket(struct connection *pc,
					 int socket_writable)
{
  static int previous_state = 0;

  if (previous_state == socket_writable)
    return;
  freelog(LOG_DEBUG, "set_wait_for_writable_socket(%d)", socket_writable);
  gdk_input_remove(gdk_input_id);
  gdk_input_id =
      gdk_input_add(aconnection.sock,
		    GDK_INPUT_READ | (socket_writable ? GDK_INPUT_WRITE :
				      0) | GDK_INPUT_EXCEPTION,
		    get_net_input, NULL);
  previous_state = socket_writable;
}

/**************************************************************************
 This function is called after the client succesfully
 has connected to the server
**************************************************************************/
void add_net_input(int sock)
{
  gdk_input_id = gdk_input_add(sock,
			       GDK_INPUT_READ | GDK_INPUT_EXCEPTION,
			       get_net_input, NULL);
  aconnection.notify_of_writable_data = set_wait_for_writable_socket;
}

/**************************************************************************
 This function is called if the client disconnects
 from the server
**************************************************************************/
void remove_net_input(void)
{
  gdk_input_remove(gdk_input_id);
  gdk_window_set_cursor(root_window, 0);
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
static gint show_info_popup(GtkWidget *w, GdkEventButton *ev)
{
  if(ev->button==1) {
    GtkWidget *p;
    char buf[512];
    
    my_snprintf(buf, sizeof(buf),
	    _("%s People\nYear: %s\nGold: %d\nNet Income: %d\n"
	      "Tax:%d Lux:%d Sci:%d\nResearching %s: %d/%d"),
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
void sound_bell(void)
{
  gdk_beep();
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
      if(seconds_to_turndone > 0)
	seconds_to_turndone--;
      else
	seconds_to_turndone = 0;
    }
    
    flip=!flip;
  }
  return TRUE;
}

/**************************************************************************
  Set one of the unit icons in information area based on punit.
  Use punit==NULL to clear icon.
  Index 'idx' is -1 for "active unit", or 0 to (num_units_below-1) for
  units below.  Also updates unit_ids[idx] for idx>=0.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  GtkWidget *w;
  
  assert(idx>=-1 && idx<num_units_below);
  if (idx == -1) {
    w = unit_pixmap;
  } else {
    w = unit_below_pixmap[idx];
    unit_ids[idx] = punit ? punit->id : 0;
  }
  
  if (punit) {
    gtk_pixcomm_clear(GTK_PIXCOMM(w), FALSE);
    put_unit_gpixmap(punit, GTK_PIXCOMM(w));
  } else {
    gtk_pixcomm_clear(GTK_PIXCOMM(w), TRUE);
  }
  
}

/**************************************************************************
  Set the "more arrow" for the unit icons to on(1) or off(0).
  Maintains a static record of current state to avoid unnecessary redraws.
  Note initial state should match initial gui setup (off).
**************************************************************************/
void set_unit_icons_more_arrow(int onoff)
{
  static int showing = 0;
  if (onoff==1 && !showing) {
    gtk_widget_show(more_arrow_pixmap);
    showing = 1;
  }
  else if(onoff==0 && showing) {
    gtk_widget_hide(more_arrow_pixmap);
    showing = 0;
  }
}
