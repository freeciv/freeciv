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
#include "government.h"
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
#include "gui_main.h"
#include "gui_stuff.h"
#include "helpdata.h"                   /* boot_help_texts() */
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "spaceshipdlg.h"
#include "resources.h"
#include "tilespec.h"


#include "freeciv.ico"

const char *client_string = "gui-gtk-2.0";

GtkWidget *map_canvas;                  /* GtkDrawingArea */
GtkWidget *map_horizontal_scrollbar;
GtkWidget *map_vertical_scrollbar;
GdkPixmap *map_canvas_store;            /* this pixmap acts as a backing store 
                                         * for the map_canvas widget */
int map_canvas_store_twidth = 1;
int map_canvas_store_theight = 1;

GtkWidget *overview_canvas;             /* GtkDrawingArea */
GdkPixmap *overview_canvas_store;       /* this pixmap acts as a backing store 
                                         * for the overview_canvas widget */
int overview_canvas_store_width = 2 * 80;
int overview_canvas_store_height = 2 * 50;

GdkPixmap *single_tile_pixmap;          /* this pixmap is used when 
                                         * moving units etc */
int single_tile_pixmap_width;
int single_tile_pixmap_height;

GtkWidget *toplevel;
GtkWidget *top_vbox;
GdkWindow *root_window;

PangoFontDescription *main_font;
PangoFontDescription *city_productions_font;

GdkGC *civ_gc;
GdkGC *mask_fg_gc;
GdkGC *mask_bg_gc;
GdkGC *fill_bg_gc;
GdkGC *fill_tile_gc;
GdkGC *thin_line_gc;
GdkGC *thick_line_gc;
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

static GtkWidget *unit_pixmap;
static GtkWidget *unit_pixmap_button;
static GtkWidget *unit_below_pixmap[MAX_NUM_UNITS_BELOW];
static GtkWidget *unit_below_pixmap_button[MAX_NUM_UNITS_BELOW];
static GtkWidget *more_arrow_pixmap;

static int unit_ids[MAX_NUM_UNITS_BELOW];  /* ids of the units icons in 
                                            * information display: (or 0) */
GtkTextView *main_message_area;
static GtkWidget *inputline;

static enum Display_color_type display_color_type;  /* practically unused */
static gint timer_id;                               /*       ditto        */
static gint gdk_input_id;


static gboolean show_info_button_release(GtkWidget *w, GdkEventButton *ev, gpointer data);
static gboolean show_info_popup(GtkWidget *w, GdkEventButton *ev, gpointer data);

static void end_turn_callback(GtkWidget *w, gpointer data);
static void get_net_input(gpointer data, gint fid, GdkInputCondition condition);
static void set_wait_for_writable_socket(struct connection *pc,
                                         bool socket_writable);

static void print_usage(const char *argv0);
static void parse_options(int argc, char **argv);
static gboolean keyboard_handler(GtkWidget *w, GdkEventKey *ev, gpointer data);

static void tearoff_callback(GtkToggleButton *b, gpointer data);
static GtkWidget *detached_widget_new(void);
static GtkWidget *detached_widget_fill(GtkWidget *ahbox);

static gboolean select_unit_pixmap_callback(GtkWidget *w, GdkEvent *ev, 
					    gpointer data);
static gint timer_callback(gpointer data);

static char *network_charset = NULL;


/**************************************************************************
Network string charset conversion functions.
**************************************************************************/
gchar *ntoh_str(const gchar *netstr)
{
  return g_convert(netstr, -1, "UTF-8", network_charset, NULL, NULL, NULL);
}

/**************************************************************************
...
**************************************************************************/
static unsigned char *put_conv(unsigned char *dst, const char *src)
{
  gsize len;
  gchar *out = g_convert(src, -1, network_charset, "UTF-8", NULL, &len, NULL);

  if (out) {
    memcpy(dst, out, len);
    g_free(out);
    dst[len] = '\0';

    return dst + len + 1;
  } else {
    dst[0] = '\0';

    return dst + 1;
  }
}

/**************************************************************************
 Returns FALSE if the destination isn't large enough or the source was
 bad.
**************************************************************************/
static bool iget_conv(char *dst, size_t ndst, const unsigned char *src,
		      size_t nsrc)
{
  gsize len;			/* length to copy, not including null */
  gchar *out = g_convert(src, nsrc, "UTF-8", network_charset, NULL, &len, NULL);
  bool ret = TRUE;

  if (!out) {
    dst[0] = '\0';
    return FALSE;
  }

  if (ndst > 0 && len >= ndst) {
    ret = FALSE;
    len = ndst - 1;
  }

  memcpy(dst, out, len);
  dst[len] = '\0';
  g_free(out);

  return ret;
}

/**************************************************************************
Local log callback functions.
**************************************************************************/
static void fprintf_utf8(FILE *stream, const char *format, ...)
{
  va_list ap;
  const gchar *charset;
  gchar *s;

  va_start(ap, format);
  s = g_strdup_vprintf(format, ap);
  va_end(ap);

  if (!g_get_charset(&charset)) {
    GError *error = NULL;
    gchar  *s2;

    s2 = g_convert(s, -1, charset, "UTF-8", NULL, NULL, &error);

    if (error) {
      fprintf(stream, "fprintf_utf8: %s\n", error->message);
      g_error_free(error);
    } else {
      g_free(s);
      s = s2;
    }
  }
  fputs(s, stream);
  fflush(stream);
  g_free(s);
}

/**************************************************************************
...
**************************************************************************/
static void log_callback_utf8(int level, char *message)
{
  fprintf_utf8(stderr, "%d: %s\n", level, message);
}

/**************************************************************************
  Print extra usage information, including one line help on each option,
  to stderr. 
**************************************************************************/
static void print_usage(const char *argv0)
{
  /* add client-specific usage information here */
  fprintf_utf8(stderr, _("Report bugs to <%s>.\n"), BUG_EMAIL_ADDRESS);
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
static gboolean inputline_focus(GtkWidget *w, GdkEventFocus *ev, gpointer data)
{
  if (GPOINTER_TO_INT(data) != 0) {
    gtk_window_remove_accel_group(GTK_WINDOW(toplevel), toplevel_accel);
  } else {
    gtk_window_add_accel_group(GTK_WINDOW(toplevel), toplevel_accel);
  }
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static gboolean keyboard_handler(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
  /* inputline history code */
  if (GTK_WIDGET_HAS_FOCUS(inputline) || !GTK_WIDGET_IS_SENSITIVE(top_vbox)) {
    void *data = NULL;
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

    if (data)
      gtk_entry_set_text(GTK_ENTRY(inputline), data);

    if (keypress)
      g_signal_stop_emission_by_name(w, "key_press_event");

    return keypress;
  }

  if (is_isometric) {
    switch (ev->keyval) {
      case GDK_Up:
      case GDK_KP_Up:
      case GDK_8:
      case GDK_KP_8: 		key_move_north_west();	break;

      case GDK_Page_Up:
      case GDK_KP_Page_Up:
      case GDK_9:
      case GDK_KP_9: 		key_move_north();	break;

      case GDK_Right:
      case GDK_KP_Right:
      case GDK_6:
      case GDK_KP_6: 		key_move_north_east();	break;

      case GDK_Page_Down:
      case GDK_KP_Page_Down:
      case GDK_3:
      case GDK_KP_3: 		key_move_east();	break;

      case GDK_Down:
      case GDK_KP_Down:
      case GDK_2:
      case GDK_KP_2: 		key_move_south_east();	break;

      case GDK_End:
      case GDK_KP_End:
      case GDK_1:
      case GDK_KP_1: 		key_move_south();	break;

      case GDK_Left:
      case GDK_KP_Left:
      case GDK_4:
      case GDK_KP_4: 		key_move_south_west();	break;

      case GDK_Home:
      case GDK_KP_Home:
      case GDK_7:
      case GDK_KP_7: 		key_move_west();	break;
 
      case GDK_KP_Begin:
      case GDK_5:
      case GDK_KP_5:
        focus_to_next_unit();
        break;
  
      case GDK_Return:
      case GDK_KP_Enter:
        key_end_turn();
        break;
  
      case GDK_Escape:
        key_cancel_action();
        break;
  
      case GDK_t:
        key_city_workers(w, ev);
        break;

      default:
        return FALSE;
    }
  } else {
    switch (ev->keyval) {
      case GDK_Up:
      case GDK_KP_Up:
      case GDK_8:
      case GDK_KP_8: 		key_move_north();	break;

      case GDK_Page_Up:
      case GDK_KP_Page_Up:
      case GDK_9:
      case GDK_KP_9: 		key_move_north_east();	break;

      case GDK_Right:
      case GDK_KP_Right:
      case GDK_6:
      case GDK_KP_6: 		key_move_east();	break;

      case GDK_Page_Down:
      case GDK_KP_Page_Down:
      case GDK_3:
      case GDK_KP_3: 		key_move_south_east();	break;

      case GDK_Down:
      case GDK_KP_Down:
      case GDK_2:
      case GDK_KP_2: 		key_move_south();	break;

      case GDK_End:
      case GDK_KP_End:
      case GDK_1:
      case GDK_KP_1: 		key_move_south_west();	break;

      case GDK_Left:
      case GDK_KP_Left:
      case GDK_4:
      case GDK_KP_4: 		key_move_west();	break;

      case GDK_Home:
      case GDK_KP_Home:		
      case GDK_7:
      case GDK_KP_7: 		key_move_north_west();	break;

      case GDK_KP_Begin:
      case GDK_Return:
      case GDK_KP_Enter:
        key_end_turn();
        break;
  
      case GDK_5:
      case GDK_KP_5: 
        focus_to_next_unit(); 
        break;
  
      case GDK_Escape:
        key_cancel_action();
        break;
  
      case GDK_t:
        key_city_workers(w, ev);
        break;

      default:
        return FALSE;
    }
  }

  g_signal_stop_emission_by_name(w, "key_press_event");
  return TRUE;
}

/**************************************************************************
 reattaches the detached widget when the user closes it via 
 the window manager.
**************************************************************************/
static void tearoff_destroy(GtkWidget *w, gpointer data)
{
  GtkWidget *p, *box = GTK_WIDGET(data);

  p = g_object_get_data(G_OBJECT(w), "prev_parent");
  gtk_widget_reparent(box, p);
}

/**************************************************************************
 callback for the toggle button in the detachable widget: causes the
 widget to detach or reattach.
**************************************************************************/
static void tearoff_callback(GtkToggleButton *b, gpointer data)
{
  GtkWidget *box = GTK_WIDGET(data);
  GtkWidget *w;

  if (gtk_toggle_button_get_active(b)) {
    w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(w, "Freeciv");
    gtk_window_set_title(GTK_WINDOW(w), _("Freeciv"));
    gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_MOUSE);
    g_signal_connect(w, "destroy", G_CALLBACK(tearoff_destroy), box);

    g_object_set_data(G_OBJECT(w), "prev_parent", box->parent);
    gtk_widget_reparent(box, w);
    gtk_widget_show(w);
  } else {
    w = box->parent;
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
  g_signal_connect(b, "toggled", G_CALLBACK(tearoff_callback), ahbox);

  /* cosmetic effects */
  sep = gtk_vseparator_new();
  gtk_container_add(GTK_CONTAINER(b), sep);

  avbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ahbox), avbox, TRUE, TRUE, 0);
  return avbox;
}

/**************************************************************************
 do the heavy lifting for the widget setup.
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget *box, *ebox, *hbox, *vbox;
  GtkWidget *avbox, *ahbox;
  GtkWidget *frame, *table, *paned, *menubar, *sw, *text;
  GtkStyle *style;
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

  gtk_widget_add_events(overview_canvas, GDK_EXPOSURE_MASK
        			       | GDK_BUTTON_PRESS_MASK );

  gtk_widget_set_size_request(overview_canvas, 160, 100);
  gtk_box_pack_start(GTK_BOX(avbox), overview_canvas, FALSE, FALSE, 0);

  g_signal_connect(overview_canvas, "expose_event",
        	   G_CALLBACK(overview_canvas_expose), NULL);

  g_signal_connect(overview_canvas, "button_press_event",
        	   G_CALLBACK(butt_down_overviewcanvas), NULL);

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
  gtk_widget_add_events(ebox, GDK_BUTTON_PRESS_MASK);

  gtk_box_pack_start(GTK_BOX(vbox), ebox, FALSE, FALSE, 0);

  main_label_info = gtk_label_new("\n\n\n\n");
  gtk_container_add(GTK_CONTAINER(ebox), main_label_info);
  g_signal_connect(ebox, "button_press_event",
                   G_CALLBACK(show_info_popup), NULL);

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
    ebox = gtk_event_box_new();
    gtk_widget_add_events(ebox, GDK_BUTTON_PRESS_MASK);

    gtk_table_attach_defaults(GTK_TABLE(table), ebox, i, i + 1, 0, 1);

    g_signal_connect(ebox, "button_press_event",
                     G_CALLBACK(taxrates_callback), GINT_TO_POINTER(i));

    econ_label[i] = gtk_image_new_from_pixmap(get_citizen_pixmap(i < 5 ? 1 : 2),
					      NULL);
    gtk_container_add(GTK_CONTAINER(ebox), econ_label[i]);
  }

  /* science, environmental, govt, timeout */
  bulb_label = gtk_image_new_from_pixmap(sprites.bulb[0]->pixmap, NULL);
  sun_label = gtk_image_new_from_pixmap(sprites.warming[0]->pixmap, NULL);
  flake_label = gtk_image_new_from_pixmap(sprites.cooling[0]->pixmap, NULL);
  government_label = gtk_image_new_from_pixmap(sprites.citizen[7]->pixmap,
					       NULL);

  for (i = 0; i < 4; i++) {
    GtkWidget *w;
    
    switch (i) {
      case 0: w = bulb_label;	      break;
      case 1: w = sun_label;	      break;
      case 2: w = flake_label;        break;
      default:
      case 3: w = government_label;   break;
    }

    gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.0);
    gtk_misc_set_padding(GTK_MISC(w), 0, 0);
    gtk_widget_set_size_request(w, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
    gtk_table_attach_defaults(GTK_TABLE(table), w, i, i + 1, 1, 2);
  }

  timeout_label = gtk_label_new("");

  frame = gtk_frame_new(NULL);
  gtk_widget_set_size_request(frame, SMALL_TILE_WIDTH, SMALL_TILE_HEIGHT);
  gtk_table_attach_defaults(GTK_TABLE(table), frame, 4, 10, 1, 2);
  gtk_container_add(GTK_CONTAINER(frame), timeout_label);


  /* turn done */
  turn_done_button = gtk_button_new_with_label(_("Turn Done"));

  /* the turn done button must have its own style. otherwise when we flash
     the turn done button other widgets may flash too. */
  if (!(style = gtk_rc_get_style(turn_done_button))) {
    style = turn_done_button->style;
  }
  gtk_widget_set_style(turn_done_button, gtk_style_copy(style));

  gtk_table_attach_defaults(GTK_TABLE(table), turn_done_button, 0, 10, 2, 3);

  g_signal_connect(turn_done_button, "clicked",
                   G_CALLBACK(end_turn_callback), NULL);
 
  /* Selected unit status */

  unit_info_frame = gtk_frame_new(NULL);
  gtk_box_pack_start(GTK_BOX(avbox), unit_info_frame, FALSE, FALSE, 0);
    
  unit_info_label = gtk_label_new("\n\n\n");
  gtk_container_add(GTK_CONTAINER(unit_info_frame), unit_info_label);

  box = gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(avbox), box, FALSE, FALSE, 0);

  table = gtk_table_new(2, num_units_below, FALSE);
  gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 5);

  gtk_table_set_row_spacings(GTK_TABLE(table), 2);
  gtk_table_set_col_spacings(GTK_TABLE(table), 2);

  unit_pixmap = gtk_pixcomm_new(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
  unit_pixmap_button = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(unit_pixmap_button), unit_pixmap);
  gtk_table_attach_defaults(GTK_TABLE(table), unit_pixmap_button, 0, 1, 0, 1);
  g_signal_connect(unit_pixmap_button, "button_press_event",
                   G_CALLBACK(select_unit_pixmap_callback), 
                   GINT_TO_POINTER(-1));

  for(i = 0; i < num_units_below; i++) {
    unit_below_pixmap[i] = gtk_pixcomm_new(UNIT_TILE_WIDTH, UNIT_TILE_HEIGHT);
    unit_below_pixmap_button[i] = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(unit_below_pixmap_button[i]),
                      unit_below_pixmap[i]);
    g_signal_connect(unit_below_pixmap_button[i],
                     "button_press_event",
                      G_CALLBACK(select_unit_pixmap_callback),
                      GINT_TO_POINTER(i));
      
    gtk_table_attach_defaults(GTK_TABLE(table), unit_below_pixmap_button[i],
                              i, i + 1, 1, 2);
  }

  more_arrow_pixmap = gtk_image_new_from_pixmap(sprites.right_arrow->pixmap,
						NULL);
  gtk_table_attach_defaults(GTK_TABLE(table), more_arrow_pixmap, 4, 5, 1, 2);

  /* Map canvas and scrollbars */

  table = gtk_table_new(2, 2, FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 0);

  frame = gtk_frame_new(NULL);
  gtk_table_attach(GTK_TABLE(table), frame, 0, 1, 0, 1,
                   GTK_EXPAND|GTK_SHRINK|GTK_FILL,
                   GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0);

  map_canvas = gtk_drawing_area_new();
  
  for (i = 0; i < 5; i++) {
    gtk_widget_modify_bg(GTK_WIDGET(map_canvas), i,
			 colors_standard[COLOR_STD_BLACK]);
  }

  gtk_widget_add_events(map_canvas, GDK_EXPOSURE_MASK
                                   |GDK_BUTTON_PRESS_MASK
                                   |GDK_KEY_PRESS_MASK
                                   |GDK_POINTER_MOTION_MASK);

  gtk_widget_set_size_request(map_canvas, 510, 300);
  gtk_container_add(GTK_CONTAINER(frame), map_canvas);

  map_horizontal_scrollbar = gtk_hscrollbar_new(NULL);
  gtk_table_attach(GTK_TABLE(table), map_horizontal_scrollbar, 0, 1, 1, 2,
                   GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

  map_vertical_scrollbar = gtk_vscrollbar_new(NULL);
  gtk_table_attach(GTK_TABLE(table), map_vertical_scrollbar, 1, 2, 0, 1,
                   0, GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0);

  g_signal_connect(map_canvas, "expose_event",
                   G_CALLBACK(map_canvas_expose), NULL);

  g_signal_connect(map_canvas, "configure_event",
                   G_CALLBACK(map_canvas_configure), NULL);

  g_signal_connect(map_canvas, "motion_notify_event",
                   G_CALLBACK(move_mapcanvas), NULL);

  g_signal_connect(map_canvas, "button_press_event",
                   G_CALLBACK(butt_down_mapcanvas), NULL);

  g_signal_connect(toplevel, "key_press_event",
                   G_CALLBACK(keyboard_handler), NULL);

  g_signal_connect(map_canvas, "button_press_event",
                   G_CALLBACK(butt_down_wakeup), NULL);

  /* *** The message window -- this is a detachable widget *** */

  ahbox = detached_widget_new();
  gtk_paned_pack2(GTK_PANED(paned), ahbox, TRUE, TRUE);
  avbox = detached_widget_fill(ahbox);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(avbox), sw, TRUE, TRUE, 0);
  gtk_widget_set_size_request(sw, 600, 100);

  text = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_container_add(GTK_CONTAINER(sw), text);

  gtk_widget_set_name(text, "chatline");

  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_widget_realize(text);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);

  main_message_area = GTK_TEXT_VIEW(text);

  set_output_window_text(
      _("Freeciv is free software and you are welcome to distribute copies of"
      " it\nunder certain conditions; See the \"Copying\" item on the Help"
      " menu.\nNow.. Go give'em hell!") );

  /* the chat line */
  inputline = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(avbox), inputline, FALSE, FALSE, 0);

  g_signal_connect(inputline, "activate", G_CALLBACK(inputline_return), NULL);

  g_signal_connect(inputline, "focus_in_event",
		   G_CALLBACK(inputline_focus), GINT_TO_POINTER(1));

  g_signal_connect(inputline, "focus_out_event",
		   G_CALLBACK(inputline_focus), GINT_TO_POINTER(0));


  /* Other things to take care of */

  gtk_widget_show_all(paned);
  gtk_widget_hide(more_arrow_pixmap);
}

/**************************************************************************
 called from main().
**************************************************************************/
void ui_init(void)
{
  gchar *s;
  char *net_charset;

#ifdef ENABLE_NLS
  bind_textdomain_codeset(PACKAGE, "UTF-8");
#endif

  log_set_callback(log_callback_utf8);

  /* set networking string conversion callbacks */
  if ((net_charset = getenv("FREECIV_NETWORK_ENCODING"))) {
    network_charset = mystrdup(net_charset);
  } else {
    const gchar *charset;

    g_get_charset(&charset);
    network_charset = mystrdup(charset);
  }

  set_put_conv_callback(put_conv);
  set_iget_conv_callback(iget_conv);

  /* convert inputs */
  s = g_locale_to_utf8(player_name, -1, NULL, NULL, NULL);
  sz_strlcpy(player_name, s);
  g_free(s);

  /* this is silly, but i don't want the UI to barf on erroneous input */
  s = g_locale_to_utf8(metaserver, -1, NULL, NULL, NULL);
  sz_strlcpy(metaserver, s);
  g_free(s);

  s = g_locale_to_utf8(server_host, -1, NULL, NULL, NULL);
  sz_strlcpy(server_host, s);
  g_free(s);
}

/**************************************************************************
 called from main(), is what it's named.
**************************************************************************/
void ui_main(int argc, char **argv)
{
  GdkBitmap *icon_bitmap;
  GtkStyle *has_resources;
  PangoLanguage *lang;

  parse_options(argc, argv);

  /* GTK withdraw gtk options. Process GTK arguments */
  gtk_init(&argc, &argv);
  
  lang = gtk_get_default_language();
  freelog(LOG_NORMAL, "LANGUAGE=\"%s\"", pango_language_to_string(lang));

  /* Load resources */
  has_resources = gtk_rc_get_style_by_paths(gtk_settings_get_default(),
					    "Freeciv*", NULL, G_TYPE_NONE);
  if (!has_resources) {
    freelog(LOG_DEBUG, "Using fallback resources - which is OK");
    gtk_rc_parse_string(fallback_resources);
  }

  toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv");
  root_window = toplevel->window;

  gtk_window_set_title(GTK_WINDOW (toplevel), _("Freeciv"));

  g_signal_connect(toplevel, "delete_event", G_CALLBACK(gtk_main_quit), NULL);

  display_color_type = get_visual();
  init_color_system();

  icon_bitmap = gdk_bitmap_create_from_data(root_window, freeciv_bits,
                                            freeciv_width, freeciv_height);
  gdk_window_set_icon(root_window, NULL, icon_bitmap, icon_bitmap);

  civ_gc = gdk_gc_new(root_window);

  /* font names shouldn't be in spec files! */
  main_font = pango_font_description_from_string("Sans Bold 13");
  city_productions_font = pango_font_description_from_string("Serif 11");

  fill_bg_gc = gdk_gc_new(root_window);

  if (is_isometric) {
    thin_line_gc = gdk_gc_new(root_window);
    thick_line_gc = gdk_gc_new(root_window);
    gdk_gc_set_line_attributes(thin_line_gc, 1,
			       GDK_LINE_SOLID,
			       GDK_CAP_NOT_LAST,
			       GDK_JOIN_MITER);
    gdk_gc_set_line_attributes(thick_line_gc, 2,
			       GDK_LINE_SOLID,
			       GDK_CAP_NOT_LAST,
			       GDK_JOIN_MITER);
  }

  fill_tile_gc = gdk_gc_new(root_window);
  gdk_gc_set_fill(fill_tile_gc, GDK_STIPPLED);

  {
    unsigned char d1[] = {0x03, 0x0c, 0x03, 0x0c};
    unsigned char d2[] = {0x08, 0x02, 0x08, 0x02};
    unsigned char d3[] = {0xAA, 0x55, 0xAA, 0x55};

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

  /* 135 below is rough value (could be more intelligent) --dwp */
  num_units_below = 135 / (int) NORMAL_TILE_WIDTH;
  num_units_below = MIN(num_units_below, MAX_NUM_UNITS_BELOW);
  num_units_below = MAX(num_units_below, 1);
  
  setup_widgets();
  load_intro_gfx();
  load_cursors();

  genlist_init(&history_list);
  history_pos = -1;

  gtk_widget_show(toplevel);

  timer_id = gtk_timeout_add(TIMER_INTERVAL, timer_callback, NULL);

  overview_canvas_store = gdk_pixmap_new(root_window,
                                         overview_canvas_store_width,
                                         overview_canvas_store_height, -1);

  gdk_gc_set_foreground(fill_bg_gc, colors_standard[COLOR_STD_WHITE]);
  gdk_draw_rectangle(overview_canvas_store, fill_bg_gc, TRUE, 0, 0,
                     overview_canvas_store_width, overview_canvas_store_height);

  single_tile_pixmap_width = UNIT_TILE_WIDTH;
  single_tile_pixmap_height = UNIT_TILE_HEIGHT;
  single_tile_pixmap = gdk_pixmap_new(root_window, 
                                      single_tile_pixmap_width,
                                      single_tile_pixmap_height, -1);

  set_client_state(CLIENT_PRE_GAME_STATE);

  gtk_main();
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

  gtk_pixcomm_freeze(GTK_PIXCOMM(w));

  if (punit) {
    put_unit_gpixmap(punit, GTK_PIXCOMM(w));
  } else {
    gtk_pixcomm_clear(GTK_PIXCOMM(w));
  }
  
  gtk_pixcomm_thaw(GTK_PIXCOMM(w));
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
static gboolean select_unit_pixmap_callback(GtkWidget *w, GdkEvent *ev, 
                                        gpointer data) 
{
  int i = GPOINTER_TO_INT(data);
  struct unit *punit;

  if (i == -1)  /* unit currently selected */
    return TRUE;

  if (unit_ids[i] == 0) /* no unit displayed at this place */
    return TRUE;

  punit = find_unit_by_id(unit_ids[i]);
  if(punit) { /* should always be true at this point */
    if (punit->owner == game.player_idx) {  /* may be non-true if alliance */
      request_new_unit_activity(punit, ACTIVITY_IDLE);
      set_unit_focus(punit);
    }
  }

  return TRUE;
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
static gboolean show_info_button_release(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
  gtk_grab_remove(w);
  gdk_pointer_ungrab(GDK_CURRENT_TIME);
  gtk_widget_destroy(w);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static gboolean show_info_popup(GtkWidget *w, GdkEventButton *ev, gpointer data)
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
	    turn_gold_difference,
	    game.player_ptr->economic.tax,
	    game.player_ptr->economic.luxury,
	    game.player_ptr->economic.science,
	    
	    advances[game.player_ptr->research.researching].name,
	    game.player_ptr->research.bulbs_researched,
	    total_bulbs_required(game.player_ptr),
	    get_government_name(game.player_ptr->government));
    
    p = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_app_paintable(p, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(p), 4);
    gtk_window_set_position(GTK_WINDOW(p), GTK_WIN_POS_MOUSE);

    gtk_widget_new(GTK_TYPE_LABEL, "GtkWidget::parent", p,
        			   "GtkLabel::label", buf,
				   "GtkWidget::visible", TRUE,
        			   NULL);
    gtk_widget_show(p);

    gdk_pointer_grab(p->window, TRUE, GDK_BUTTON_RELEASE_MASK,
		     NULL, NULL, ev->time);
    gtk_grab_add(p);

    g_signal_connect_after(p, "button_release_event",
                           G_CALLBACK(show_info_button_release), NULL);
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
		           !game.player_ptr->ai.control || ai_manual_turn_done);
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
