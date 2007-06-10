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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gtk/gtkinvisible.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "fciconv.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "shared.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "control.h"
#include "freeciv.ico"
#include "helpdata.h"                   /* boot_help_texts() */
#include "options.h"
#include "tilespec.h"

#include "chatline.h"
#include "colors.h"
#include "connectdlg.h"
#include "dialogs.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "resources.h"
#include "spaceshipdlg.h"

#include "gui_main.h"

const char *client_string = "gui-gtk";

GtkWidget *map_canvas;                  /* GtkDrawingArea */
GtkWidget *map_horizontal_scrollbar;
GtkWidget *map_vertical_scrollbar;

GtkWidget *overview_canvas;             /* GtkDrawingArea */

GtkWidget *toplevel;
GtkWidget *top_vbox;
GdkWindow *root_window;

GdkFont *main_fontset;
GdkFont *prod_fontset;

GdkGC *civ_gc;
GdkGC *mask_fg_gc;
GdkGC *mask_bg_gc;
GdkGC *fill_bg_gc;
GdkGC *fill_tile_gc;
GdkGC *thin_line_gc;
GdkGC *thick_line_gc;
GdkGC *border_line_gc;
GdkPixmap *gray50, *gray25, *black50;
GdkPixmap *mask_bitmap;

GtkWidget *main_frame_civ_name;
GtkWidget *main_label_info;

GtkWidget *econ_label[10];
GtkWidget *bulb_label;
GtkWidget *sun_label;
GtkWidget *flake_label;
GtkWidget *government_label;
GtkWidget *timeout_label;
GtkWidget *turn_done_button;

GtkWidget *unit_info_label;
GtkWidget *unit_info_frame;

static GtkWidget *unit_pixmap_table;
static GtkWidget *unit_pixmap;
static GtkWidget *unit_pixmap_button;
static GtkWidget *unit_below_pixmap[MAX_NUM_UNITS_BELOW];
static GtkWidget *unit_below_pixmap_button[MAX_NUM_UNITS_BELOW];
static GtkWidget *more_arrow_pixmap;

static int unit_ids[MAX_NUM_UNITS_BELOW];  /* ids of the units icons in 
                                            * information display: (or 0) */
GtkText   *main_message_area;
GtkWidget *text_scrollbar;
static GtkWidget *inputline;

static enum Display_color_type display_color_type;  /* practically unused */
static gint timer_id;                               /*       ditto        */
static gint gdk_input_id;

client_option gui_options[] = {
  GEN_BOOL_OPTION(meta_accelerators, N_("Use Alt/Meta for accelerators")),
  GEN_BOOL_OPTION(map_scrollbars, N_("Show Map Scrollbars")),
  GEN_BOOL_OPTION(keyboardless_goto, N_("Keyboardless goto")),
};
const int num_gui_options = ARRAY_SIZE(gui_options);

static gint show_info_button_release(GtkWidget *w, GdkEventButton *ev);
static gint show_info_popup(GtkWidget *w, GdkEventButton *ev);

static void end_turn_callback(GtkWidget *w, gpointer data);
static void get_net_input(gpointer data, gint fid, GdkInputCondition condition);
static void set_wait_for_writable_socket(struct connection *pc,
                                         bool socket_writable);

static void print_usage(const char *argv0);
static void parse_options(int argc, char **argv);
static gint keyboard_handler(GtkWidget *w, GdkEventKey *ev);

static gint tearoff_delete(GtkWidget *w, GdkEvent *ev, GtkWidget *box);
static void tearoff_callback(GtkWidget *but, GtkWidget *box);
static GtkWidget *detached_widget_new(void);
static GtkWidget *detached_widget_fill(GtkWidget *ahbox);

static void select_unit_pixmap_callback(GtkWidget *w, GdkEvent *ev, 
                                        gpointer data);
static gint timer_callback(gpointer data);

/**************************************************************************
  Print extra usage information, including one line help on each option,
  to stderr. 
**************************************************************************/
static void print_usage(const char *argv0)
{
  /* add client-specific usage information here */
  fc_fprintf(stderr, _("Report bugs at %s.\n"), BUG_URL);
}

/**************************************************************************
 search for command line options. right now, it's just help
 semi-useless until we have options that aren't the same across all clients.
**************************************************************************/
static void parse_options(int argc, char **argv)
{
  int i = 1;

  while (i < argc) {
    if (is_option("--help", argv[i])) {
      print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    }
    i++;
  }
}

/**************************************************************************
 handles main window keyboard events.
**************************************************************************/
static gint keyboard_handler(GtkWidget *w, GdkEventKey *ev)
{
  /* inputline history code */
  if (GTK_WIDGET_HAS_FOCUS(inputline) || !GTK_WIDGET_IS_SENSITIVE(top_vbox)) {
    const char *data = NULL;
    gint keypress = FALSE;

    if (ev->keyval == GDK_Up) {
      keypress = TRUE;

      if (history_pos < genlist_size(&history_list) - 1)
        history_pos++;

      data = genlist_get(&history_list, history_pos);
    }

    if (ev->keyval == GDK_Down) {
      keypress = TRUE;

      if (history_pos >= 0)
        history_pos--;

      if (history_pos >= 0) {
        data = genlist_get(&history_list, history_pos);
      } else {
        data = "";
      }
    }

    if (ev->keyval == GDK_Page_Up) {
      GtkAdjustment *adj;
      gint nval;

      keypress = TRUE;

      adj = gtk_range_get_adjustment(GTK_RANGE(text_scrollbar));
      nval = adj->value - adj->page_increment;
      gtk_adjustment_set_value(adj, nval);
    }

    if(ev->keyval == GDK_Page_Down) {
      GtkAdjustment *adj;
      gint nval;

      keypress = TRUE;

      adj = gtk_range_get_adjustment(GTK_RANGE(text_scrollbar));
      nval = adj->value + adj->page_increment;
      gtk_adjustment_set_value(adj, nval);
    }
		
    if (data)
      gtk_entry_set_text(GTK_ENTRY(inputline), data);

    if (keypress)
      gtk_signal_emit_stop_by_name(GTK_OBJECT(w), "key_press_event");

    return keypress;
  }

  if (!client_is_observer()) {
    if ((ev->state & GDK_SHIFT_MASK)) {
      switch (ev->keyval) {
	case GDK_Left:
	  scroll_mapview(DIR8_WEST);
	  break;

	case GDK_Right:
	  scroll_mapview(DIR8_EAST);
	  break;

	case GDK_Up:
	  scroll_mapview(DIR8_NORTH);
	  break;

	case GDK_Down:
	  scroll_mapview(DIR8_SOUTH);
	  break;

	case GDK_Home:
	  key_center_capital();
	  break;

	case GDK_Return:
	case GDK_KP_Enter:
	  key_end_turn();
	  break;
    
	default:
	  break;
      }
    }

    switch (ev->keyval) {
      case GDK_KP_Up:
      case GDK_8:
      case GDK_KP_8:
	key_unit_move(DIR8_NORTH);
	break;

      case GDK_KP_Page_Up:
      case GDK_9:
      case GDK_KP_9:
	key_unit_move(DIR8_NORTHEAST);
	break;

      case GDK_KP_Right:
      case GDK_6:
      case GDK_KP_6:
	key_unit_move(DIR8_EAST);
	break;

      case GDK_KP_Page_Down:
      case GDK_3:
      case GDK_KP_3:
	key_unit_move(DIR8_SOUTHEAST);
	break;

      case GDK_KP_Down:
      case GDK_2:
      case GDK_KP_2:
	key_unit_move(DIR8_SOUTH);
	break;

      case GDK_KP_End:
      case GDK_1:
      case GDK_KP_1:
	key_unit_move(DIR8_SOUTHWEST);
	break;

      case GDK_KP_Left:
      case GDK_4:
      case GDK_KP_4:
	key_unit_move(DIR8_WEST);
	break;

      case GDK_KP_Home:		
      case GDK_7:
      case GDK_KP_7:
	key_unit_move(DIR8_NORTHWEST);
	break;

      case GDK_5:
      case GDK_KP_5: 
      case GDK_KP_Begin:
	key_recall_previous_focus_unit();
	break;
  
      case GDK_Escape:
	key_cancel_action();
	break;
  
      case GDK_t:
	key_city_workers(w, ev);
	break;

      case GDK_KP_Divide:
	key_quickselect(SELECT_SEA);
	break;

      case GDK_KP_Multiply:
	key_quickselect(SELECT_LAND);
	break;

      default:
	return FALSE;
    }
  }

  gtk_signal_emit_stop_by_name(GTK_OBJECT(w), "key_press_event");
  return TRUE;
}

/**************************************************************************
 reattaches the detached widget when the user closes it via 
 the window manager.
**************************************************************************/
static gint tearoff_delete(GtkWidget *w, GdkEvent *ev, GtkWidget *box)
{
  GtkWidget *p;

  gtk_widget_hide(w);
  p = ((GtkBoxChild *) GTK_BOX(box)->children->data)->widget;
  gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(p), FALSE);

  p = gtk_object_get_user_data(GTK_OBJECT(w));
  gtk_widget_reparent(box, p);
  return TRUE;
}

/**************************************************************************
 callback for the toggle button in the detachable widget: causes the
 widget to detach or reattach.
**************************************************************************/
static void tearoff_callback(GtkWidget *but, GtkWidget *box)
{
  GtkWidget *w;

  if (GTK_TOGGLE_BUTTON(but)->active) {
    w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w), _("Freeciv"));
    gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_MOUSE);
    gtk_signal_connect(GTK_OBJECT(w), "delete_event",
    		       GTK_SIGNAL_FUNC(tearoff_delete), box);

    gtk_object_set_user_data(GTK_OBJECT(w), box->parent);
    gtk_widget_reparent(box, w);
    gtk_widget_show(w);
  } else {
    w = box->parent;
    tearoff_delete(w, NULL, box);
    gtk_widget_destroy(w);
  }
}

/**************************************************************************
 create the container for the widget that's able to be detached
**************************************************************************/
static GtkWidget *detached_widget_new(void)
{
  return gtk_hbox_new(FALSE, 2);
}

/**************************************************************************
 creates the toggle button necessary to detach and reattach the widget
 and returns a vbox in which you fill your goodies.
**************************************************************************/
static GtkWidget *detached_widget_fill(GtkWidget *ahbox)
{
  GtkWidget *b, *sep, *avbox;

  b = gtk_toggle_button_new();
  gtk_box_pack_start(GTK_BOX(ahbox), b, FALSE, FALSE, 0);
  gtk_signal_connect(GTK_OBJECT(b), "clicked",
        	      GTK_SIGNAL_FUNC(tearoff_callback), ahbox);

  /* cosmetic effects */
  sep = gtk_vseparator_new();
  gtk_container_add(GTK_CONTAINER(b), sep);

  avbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ahbox), avbox, TRUE, TRUE, 0);
  return avbox;
}

/**************************************************************************
  Called to build the unit_below pixmap table.  This is the table on the
  left of the screen that shows all of the inactive units in the current
  tile.

  It may be called again if the tileset changes.
**************************************************************************/
static void populate_unit_pixmap_table(void)
{
  int i;
  GtkWidget *table = unit_pixmap_table;
 
  /* 135 below is rough value (could be more intelligent) --dwp */
  num_units_below = 135 / (int) NORMAL_TILE_WIDTH;
  num_units_below = CLIP(1, num_units_below, MAX_NUM_UNITS_BELOW);

  gtk_table_resize(GTK_TABLE(table), 2, num_units_below);

  /* Note, we ref this and other widgets here so that we can unref them
   * in reset_unit_table. */
  unit_pixmap = gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH, 
                                UNIT_TILE_HEIGHT);
  gtk_widget_ref(unit_pixmap);
  gtk_pixcomm_clear(GTK_PIXCOMM(unit_pixmap), TRUE);
  unit_pixmap_button = gtk_event_box_new();
  gtk_widget_ref(unit_pixmap_button);
  gtk_container_add(GTK_CONTAINER(unit_pixmap_button), unit_pixmap);
  gtk_table_attach_defaults(GTK_TABLE(table), unit_pixmap_button, 0, 1, 0, 1);
  gtk_signal_connect(GTK_OBJECT(unit_pixmap_button), "button_press_event",
                     GTK_SIGNAL_FUNC(select_unit_pixmap_callback), 
                     GINT_TO_POINTER(-1));

  for(i = 0; i < num_units_below; i++) {
    unit_below_pixmap[i] = gtk_pixcomm_new(root_window, UNIT_TILE_WIDTH,
                                           UNIT_TILE_HEIGHT);
    gtk_widget_ref(unit_below_pixmap[i]);
    unit_below_pixmap_button[i] = gtk_event_box_new();
    gtk_widget_ref(unit_below_pixmap_button[i]);
    gtk_container_add(GTK_CONTAINER(unit_below_pixmap_button[i]),
                      unit_below_pixmap[i]);
    gtk_signal_connect(GTK_OBJECT(unit_below_pixmap_button[i]),
                       "button_press_event",
                        GTK_SIGNAL_FUNC(select_unit_pixmap_callback),
                        GINT_TO_POINTER(i));
      
    gtk_table_attach_defaults(GTK_TABLE(table), unit_below_pixmap_button[i],
                              i, i + 1, 1, 2);
    gtk_widget_set_usize(unit_below_pixmap[i],
                         UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    gtk_pixcomm_clear(GTK_PIXCOMM(unit_below_pixmap[i]), TRUE);
  }

  more_arrow_pixmap = gtk_pixmap_new(sprites.right_arrow->pixmap, NULL);
  gtk_widget_ref(more_arrow_pixmap);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(more_arrow_pixmap), FALSE);
  gtk_table_attach_defaults(GTK_TABLE(table), more_arrow_pixmap, 4, 5, 1, 2);

  gtk_widget_show_all(table);
}

/**************************************************************************
  Called when the tileset is changed to reset the unit pixmap table.
**************************************************************************/
void reset_unit_table(void)
{
  int i;

  /* Unreference all of the widgets that we're about to reallocate, thus
   * avoiding a memory leak. Remove them from the container first, just
   * to be safe. Note, the widgets are ref'd in
   * populatate_unit_pixmap_table. */
  gtk_container_remove(GTK_CONTAINER(unit_pixmap_table),
		       unit_pixmap_button);
  gtk_widget_unref(unit_pixmap);
  gtk_widget_unref(unit_pixmap_button);
  for (i = 0; i < num_units_below; i++) {
    gtk_container_remove(GTK_CONTAINER(unit_pixmap_table),
			 unit_below_pixmap_button[i]);
    gtk_widget_unref(unit_below_pixmap[i]);
    gtk_widget_unref(unit_below_pixmap_button[i]);
  }
  gtk_container_remove(GTK_CONTAINER(unit_pixmap_table),
		       more_arrow_pixmap);
  gtk_widget_unref(more_arrow_pixmap);

  populate_unit_pixmap_table();

  /* We have to force a redraw of the units.  And we explicitly have
   * to force a redraw of the focus unit, which is normally only
   * redrawn when the focus changes. We also have to force the 'more'
   * arrow to go away, both by explicitly hiding it and telling it to
   * do so (this will be reset immediately afterwards if necessary,
   * but we have to make the *internal* state consistent). */
  gtk_widget_hide(more_arrow_pixmap);
  set_unit_icons_more_arrow(FALSE);
  set_unit_icon(-1, get_unit_in_focus());
  update_unit_pix_label(get_unit_in_focus());
}

/**************************************************************************
 do the heavy lifting for the widget setup.
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget *box, *ebox, *hbox, *vbox;
  GtkWidget *avbox, *ahbox;
  GtkWidget *frame, *table, *paned, *menubar, *text;
  GtkStyle  *text_style, *style;
  int i;

  /* the window is divided into two panes. "top" and "message window" */ 
  paned = gtk_vpaned_new();
  gtk_container_add(GTK_CONTAINER(toplevel), paned);

  /* *** everything in the top *** */

  top_vbox = gtk_vbox_new(FALSE, 5);
  gtk_paned_pack1(GTK_PANED(paned), top_vbox, TRUE, FALSE);
  
  setup_menus(toplevel, &menubar);
  gtk_box_pack_start(GTK_BOX(top_vbox), menubar, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(top_vbox), hbox, TRUE, TRUE, 0);

  /* this holds the overview canvas, production info, etc. */
  vbox = gtk_vbox_new(FALSE, 3);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);


  /* overview canvas */
  ahbox = detached_widget_new();
  gtk_container_add(GTK_CONTAINER(vbox), ahbox);
  avbox = detached_widget_fill(ahbox);

  overview_canvas = gtk_drawing_area_new();

  gtk_widget_set_events(overview_canvas, (GDK_EXPOSURE_MASK
					  | GDK_BUTTON_PRESS_MASK
					  | GDK_POINTER_MOTION_MASK));

  gtk_drawing_area_size(GTK_DRAWING_AREA(overview_canvas), 160, 100);
  gtk_box_pack_start(GTK_BOX(avbox), overview_canvas, FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(overview_canvas), "expose_event",
        	      (GtkSignalFunc) overview_canvas_expose, NULL);

  gtk_signal_connect(GTK_OBJECT(overview_canvas), "button_press_event",
        	      (GtkSignalFunc) butt_down_overviewcanvas, NULL);

  gtk_signal_connect(GTK_OBJECT(overview_canvas), "motion_notify_event",
		     GTK_SIGNAL_FUNC(move_overviewcanvas), NULL);

  /* The Rest */

  ahbox = detached_widget_new();
  gtk_container_add(GTK_CONTAINER(vbox), ahbox);
  avbox = detached_widget_fill(ahbox);

  /* Info on player's civilization */
  frame = gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(avbox), frame, FALSE, FALSE, 0);

  main_frame_civ_name = frame;

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  ebox = gtk_event_box_new();
  gtk_widget_set_events(ebox, GDK_BUTTON_PRESS_MASK);

  gtk_box_pack_start(GTK_BOX(vbox), ebox, FALSE, FALSE, 0);

  main_label_info = gtk_label_new("\n\n\n\n");
  gtk_container_add(GTK_CONTAINER(ebox), main_label_info);
  gtk_signal_connect(GTK_OBJECT(ebox),"button_press_event",
                     GTK_SIGNAL_FUNC(show_info_popup), NULL);

  /* Production status */

  /* make a box so the table will be centered */
  box = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(avbox), box, FALSE, FALSE, 0);

  table = gtk_table_new(3, 10, FALSE);
  gtk_table_set_row_spacing(GTK_TABLE(table), 0, 0);
  gtk_table_set_col_spacing(GTK_TABLE(table), 0, 0);
  gtk_box_pack_start(GTK_BOX(box), table, TRUE, FALSE, 0);

  /* citizens for taxrates */
  for (i = 0; i < 10; i++) {
    struct Sprite *s = i < 5 ? sprites.tax_science : sprites.tax_gold;

    ebox = gtk_event_box_new();
    gtk_widget_set_events(ebox, GDK_BUTTON_PRESS_MASK);

    gtk_table_attach_defaults(GTK_TABLE(table), ebox, i, i + 1, 0, 1);

    gtk_signal_connect(GTK_OBJECT(ebox), "button_press_event",
                       GTK_SIGNAL_FUNC(taxrates_callback), GINT_TO_POINTER(i));

    econ_label[i] = gtk_pixmap_new(s->pixmap, NULL);
    gtk_pixmap_set_build_insensitive(GTK_PIXMAP(econ_label[i]), FALSE);
    gtk_container_add(GTK_CONTAINER(ebox), econ_label[i]);
  }

  /* science, environmental, govt, timeout */
  bulb_label = gtk_pixmap_new(sprites.bulb[0]->pixmap, NULL);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(bulb_label), FALSE);

  sun_label  = gtk_pixmap_new(sprites.warming[0]->pixmap, NULL);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(sun_label), FALSE);

  flake_label = gtk_pixmap_new(sprites.cooling[0]->pixmap, NULL);
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(flake_label), FALSE);

  {
    /* HACK: the UNHAPPY citizen is used for the government
     * when we don't know any better. */
    struct citizen_type c = {.type = CITIZEN_UNHAPPY};
    struct Sprite *sprite = get_citizen_sprite(c, 0, NULL);

    government_label = gtk_pixmap_new(sprite->pixmap, NULL);
  }
  gtk_pixmap_set_build_insensitive(GTK_PIXMAP(government_label), FALSE);

  timeout_label = gtk_label_new("");

  for (i = 0; i < 5; i++) {
    frame = gtk_frame_new(NULL);
    gtk_widget_set_usize(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);

    if (i == 4) {
      gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 6, 1, 2);
    } else {
      gtk_table_attach_defaults(GTK_TABLE(table), frame, i, i + 1, 1, 2);
    }

    switch (i) {
      case 0: gtk_container_add(GTK_CONTAINER(frame), bulb_label);       break;
      case 1: gtk_container_add(GTK_CONTAINER(frame), sun_label);        break;
      case 2: gtk_container_add(GTK_CONTAINER(frame), flake_label);      break;
      case 3: gtk_container_add(GTK_CONTAINER(frame), government_label); break;
      case 4: gtk_container_add(GTK_CONTAINER(frame), timeout_label);    break;
    }
  }

  /* turn done */
  turn_done_button = gtk_button_new_with_label(_("Turn Done"));

  /* the turn done button must have its own style. otherwise when we flash
     the turn done button other widgets may flash too. */
  style = turn_done_button->style;
  gtk_widget_set_style(turn_done_button, gtk_style_copy(style));

  gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 10, 2, 3);

  gtk_signal_connect(GTK_OBJECT(turn_done_button), "clicked",
                     GTK_SIGNAL_FUNC(end_turn_callback), NULL);
 
  /* Selected unit status */

  unit_info_frame = gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(avbox), unit_info_frame, FALSE, FALSE, 0);
    
  unit_info_label = gtk_label_new("\n\n\n");
  gtk_container_add(GTK_CONTAINER(unit_info_frame), unit_info_label);

  box = gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(avbox), box, FALSE, FALSE, 0);

  table = gtk_table_new(0, 0, FALSE);
  gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 5);

  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 2);

  unit_pixmap_table = table;
  populate_unit_pixmap_table();

  /* Map canvas and scrollbars */

  table = gtk_table_new(2, 2, FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 0);

  frame = gtk_frame_new(NULL);
  gtk_table_attach(GTK_TABLE(table), frame, 0, 1, 0, 1,
                   GTK_EXPAND|GTK_SHRINK|GTK_FILL,
                   GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0);

  map_canvas = gtk_drawing_area_new();

  gtk_widget_set_events(map_canvas, GDK_EXPOSURE_MASK
                                   |GDK_BUTTON_PRESS_MASK
                                   |GDK_BUTTON_RELEASE_MASK
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

  gtk_signal_connect(GTK_OBJECT(map_canvas), "expose_event",
                     GTK_SIGNAL_FUNC(map_canvas_expose), NULL);

  gtk_signal_connect(GTK_OBJECT(map_canvas), "motion_notify_event",
                     GTK_SIGNAL_FUNC(move_mapcanvas), NULL);

  gtk_signal_connect(GTK_OBJECT(map_canvas), "button_press_event",
                     GTK_SIGNAL_FUNC(butt_down_mapcanvas), NULL);

  gtk_signal_connect(GTK_OBJECT(map_canvas), "button_release_event",
                     GTK_SIGNAL_FUNC(butt_release_mapcanvas), NULL);

  gtk_signal_connect(GTK_OBJECT(toplevel), "key_press_event",
                     GTK_SIGNAL_FUNC(keyboard_handler), NULL);

  /* *** The message window -- this is a detachable widget *** */

  ahbox = detached_widget_new();
  gtk_paned_pack2(GTK_PANED(paned), ahbox, TRUE, TRUE);
  avbox = detached_widget_fill(ahbox);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(avbox), hbox, TRUE, TRUE, 0);

  text = gtk_text_new(NULL, NULL);
  gtk_box_pack_start(GTK_BOX(hbox), text, TRUE, TRUE, 0);
  gtk_widget_set_usize(text, 600, 100);

  gtk_text_set_editable(GTK_TEXT(text), FALSE);
  gtk_text_set_word_wrap(GTK_TEXT(text), 1);
  gtk_widget_realize(text);
  main_message_area = GTK_TEXT(text);

  /* hack to make insensitive text readable */
  text_style = gtk_style_copy(text->style);
  text_style->base[GTK_STATE_INSENSITIVE] = text_style->base[GTK_STATE_NORMAL];
  text_style->text[GTK_STATE_INSENSITIVE] = text_style->text[GTK_STATE_NORMAL];
  gtk_widget_set_style(text, text_style);

  text_scrollbar = gtk_vscrollbar_new(GTK_TEXT(text)->vadj);
  gtk_box_pack_start(GTK_BOX(hbox), text_scrollbar, FALSE, FALSE, 0);
  gtk_widget_realize(text_scrollbar);

  set_output_window_text(
      _("Freeciv is free software and you are welcome to distribute copies of"
      " it\nunder certain conditions; See the \"Copying\" item on the Help"
      " menu.\nNow.. Go give'em hell!") );

  /* the chat line */
  inputline = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(avbox), inputline, FALSE, FALSE, 0);

  gtk_signal_connect(GTK_OBJECT(inputline), "activate",
                     GTK_SIGNAL_FUNC(inputline_return), NULL);


  /* Other things to take care of */

  gtk_widget_show_all(paned);
  gtk_widget_hide(more_arrow_pixmap);

  if (!map_scrollbars) {
    gtk_widget_hide(map_horizontal_scrollbar);
    gtk_widget_hide(map_vertical_scrollbar);
  }
}

/**************************************************************************
 called from main().
**************************************************************************/
void ui_init(void)
{
  init_character_encodings(NULL, TRUE);
}

/**************************************************************************
 called from main(), is what it's named.
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
# ifdef HAVE_PUTENV
    if(strcmp(setlocale(LC_CTYPE, (const char *)NULL), "C") == 0)
      putenv((char *) "LC_CTYPE=en_US.ISO8859-1");
# endif
#endif

  gtk_set_locale();

  /* GTK withdraw gtk options. Process GTK arguments */
  gtk_init(&argc, &argv);

  /* Load resources */
  gtk_rc_parse_string(fallback_resources);

  homedir = g_get_home_dir();	/* should i gfree() this also? --vasc */
  rc_file = g_strdup_printf("%s%s%s", homedir, G_DIR_SEPARATOR_S, "freeciv.rc");
  gtk_rc_parse(rc_file);
  g_free (rc_file);



  toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv");
  root_window = toplevel->window;

  gtk_window_set_title(GTK_WINDOW (toplevel), _("Freeciv"));

  gtk_signal_connect(GTK_OBJECT(toplevel), "delete_event",
                     GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

  display_color_type = get_visual();
  init_color_system();

  icon_bitmap = gdk_bitmap_create_from_data(root_window, freeciv_bits,
                                            freeciv_width, freeciv_height);
  gdk_window_set_icon(root_window, NULL, icon_bitmap, icon_bitmap);

  civ_gc = gdk_gc_new(root_window);

  {
    GtkWidget *w;
    GtkStyle *style;
    
    /* Hack to get fonts from resources... */
    w = gtk_invisible_new();
    gtk_widget_set_name(w, "city names");
    gtk_container_add(GTK_CONTAINER(toplevel), w);
    style = gtk_rc_get_style(w);

    main_fontset = style->font;
    gdk_font_ref(main_fontset);
    gtk_widget_destroy(w);

    w = gtk_invisible_new();
    gtk_widget_set_name(w, "city productions");
    gtk_container_add(GTK_CONTAINER(toplevel), w);
    style = gtk_rc_get_style(w);

    prod_fontset = style->font;
    gdk_font_ref(prod_fontset);
    gtk_widget_destroy(w);
  }

  fill_bg_gc = gdk_gc_new(root_window);

  /* These are used in isometric view only, but are always created because
   * the tileset can change at runtime. */
  thin_line_gc = gdk_gc_new(root_window);
  thick_line_gc = gdk_gc_new(root_window);
  border_line_gc = gdk_gc_new(root_window);
  gdk_gc_set_line_attributes(thin_line_gc, 1,
			     GDK_LINE_SOLID,
			     GDK_CAP_NOT_LAST,
			     GDK_JOIN_MITER);
  gdk_gc_set_line_attributes(thick_line_gc, 2,
			     GDK_LINE_SOLID,
			     GDK_CAP_NOT_LAST,
			     GDK_JOIN_MITER);
  gdk_gc_set_line_attributes(border_line_gc, BORDER_WIDTH,
			     GDK_LINE_ON_OFF_DASH,
			     GDK_CAP_NOT_LAST,
			     GDK_JOIN_MITER);

  fill_tile_gc = gdk_gc_new(root_window);
  gdk_gc_set_fill(fill_tile_gc, GDK_STIPPLED);

  {
    char d1[] = {0x03, 0x0c, 0x03, 0x0c};
    char d2[] = {0x08, 0x02, 0x08, 0x02};
    char d3[] = {0xAA, 0x55, 0xAA, 0x55};

    gray50 = gdk_bitmap_create_from_data(root_window, d1, 4, 4);
    gray25 = gdk_bitmap_create_from_data(root_window, d2, 4, 4);
    black50 = gdk_bitmap_create_from_data(root_window, d3, 4, 4);
  }

  {
    GdkColor pixel;
    
    mask_bitmap = gdk_pixmap_new(root_window, 1, 1, 1);

    mask_fg_gc = gdk_gc_new(mask_bitmap);
    pixel.pixel = 1;
    gdk_gc_set_foreground(mask_fg_gc, &pixel);
    gdk_gc_set_function(mask_fg_gc, GDK_OR);

    mask_bg_gc = gdk_gc_new(mask_bitmap);
    pixel.pixel = 0;
    gdk_gc_set_foreground(mask_bg_gc, &pixel);
  }

  tilespec_load_tiles();

  setup_widgets();
  load_intro_gfx();
  load_cursors();

  genlist_init(&history_list);
  history_pos = -1;

  gtk_widget_show(toplevel);

  timer_id = gtk_timeout_add(TIMER_INTERVAL, timer_callback, NULL);

  init_mapcanvas_and_overview();


  set_client_state(CLIENT_PRE_GAME_STATE);

  gtk_main();

  free_color_system();
  tilespec_free_tiles();
}

/**************************************************************************
 Update the connected users list at pregame state.
***************************************************************************/
void update_conn_list_dialog(void)
{
  /* PORTME */
}

/**************************************************************************
 obvious...
**************************************************************************/
void sound_bell(void)
{
  gdk_beep();
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
  
  assert(idx >= -1 && idx < num_units_below);

  if (idx == -1) {
    w = unit_pixmap;
  } else {
    w = unit_below_pixmap[idx];
    unit_ids[idx] = punit ? punit->id : 0;
  }

  if (get_client_state() == CLIENT_GAME_OVER_STATE) {
    gtk_pixcomm_clear(GTK_PIXCOMM(w), TRUE);
    return;
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
void set_unit_icons_more_arrow(bool onoff)
{
  static bool showing = FALSE;

  if (onoff && !showing) {
    gtk_widget_show(more_arrow_pixmap);
    showing = TRUE;
  }
  else if(!onoff && showing) {
    gtk_widget_hide(more_arrow_pixmap);
    showing = FALSE;
  }
}

/**************************************************************************
 callback for clicking a unit icon underneath unit info box.
 these are the units on the same tile as the focus unit.
**************************************************************************/
static void select_unit_pixmap_callback(GtkWidget *w, GdkEvent *ev, 
                                        gpointer data) 
{
  int i = GPOINTER_TO_INT(data);
  struct unit *punit;

  if (i == -1)  /* unit currently selected */
    return;

  if (unit_ids[i] == 0) /* no unit displayed at this place */
    return;

  punit = find_unit_by_id(unit_ids[i]);
  if(punit) { /* should always be true at this point */
    if (punit->owner == game.player_idx) {  /* may be non-true if alliance */
      set_unit_focus(punit);
    }
  }
}

/**************************************************************************
 this is called every TIMER_INTERVAL milliseconds whilst we are in 
 gtk_main() (which is all of the time) TIMER_INTERVAL needs to be .5s
**************************************************************************/
static gint timer_callback(gpointer data)
{
  real_timer_callback();
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static gint show_info_button_release(GtkWidget *w, GdkEventButton *ev)
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
  if(ev->button == 1) {
    GtkWidget *p;
    char buf[512];
    
    my_snprintf(buf, sizeof(buf),
	    _("%s People\nYear: %s Turn: %d\nGold: %d\nNet Income: %d\n"
	      "Tax:%d Lux:%d Sci:%d\nResearching %s: %d/%d\nGovernment: %s"),
	    population_to_text(civ_population(game.player_ptr)),
	    textyear(game.year), game.turn,
	    game.player_ptr->economic.gold,
	    player_get_expected_income(game.player_ptr),
	    game.player_ptr->economic.tax,
	    game.player_ptr->economic.luxury,
	    game.player_ptr->economic.science,

	    get_tech_name(game.player_ptr,
			  game.player_ptr->research.researching),
	    game.player_ptr->research.bulbs_researched,
	    total_bulbs_required(game.player_ptr),
	    get_government_name(game.player_ptr->government));
    
    p = gtk_window_new(GTK_WINDOW_POPUP);

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
 user clicked "Turn Done" button
**************************************************************************/
static void end_turn_callback(GtkWidget *w, gpointer data)
{
    gtk_widget_set_sensitive(turn_done_button, FALSE);
    user_ended_turn();
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
					 bool socket_writable)
{
  static bool previous_state = FALSE;

  assert(pc == &aconnection);

  if (previous_state == socket_writable)
    return;

  freelog(LOG_DEBUG, "set_wait_for_writable_socket(%d)", socket_writable);
  gdk_input_remove(gdk_input_id);
  gdk_input_id = gdk_input_add(aconnection.sock, GDK_INPUT_READ 
                               | (socket_writable ? GDK_INPUT_WRITE : 0)
                               | GDK_INPUT_EXCEPTION, get_net_input, NULL);
  previous_state = socket_writable;
}

/**************************************************************************
 This function is called after the client succesfully
 has connected to the server
**************************************************************************/
void add_net_input(int sock)
{
  gdk_input_id = gdk_input_add(sock, GDK_INPUT_READ | GDK_INPUT_EXCEPTION,
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
  gdk_window_set_cursor(root_window, NULL);
}
