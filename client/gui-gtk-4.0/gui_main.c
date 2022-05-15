/***********************************************************************
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
#include <fc_config.h>
#endif

#ifdef AUDIO_SDL
/* Though it would happily compile without this include,
 * it is needed for sound to work.
 * It defines "main" macro to rename our main() so that
 * it can install SDL's own. */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* PLAIN_INCLUDE */
#endif /* AUDIO_SDL */

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "dataio.h"
#include "featured_text.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "unitlist.h"
#include "version.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg_common.h"
#include "control.h"
#include "editor.h"
#include "options.h"
#include "text.h"
#include "tilespec.h"
#include "zoom.h"

/* client/gui-gtk-4.0 */
#include "chatline.h"
#include "citizensinfo.h"
#include "connectdlg.h"
#include "cma_fe.h"
#include "dialogs.h"
#include "diplodlg.h"
#include "editgui.h"
#include "gotodlg.h"
#include "graphics.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "inteldlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "pages.h"
#include "plrdlg.h"
#include "luaconsole.h"
#include "spaceshipdlg.h"
#include "repodlgs.h"
#include "voteinfo_bar.h"

#include "gui_main.h"

const char *client_string = "gui-gtk-4.0";

GtkWidget *map_canvas;                  /* GtkDrawingArea */
GtkWidget *map_horizontal_scrollbar;
GtkWidget *map_vertical_scrollbar;

GtkWidget *overview_canvas;             /* GtkDrawingArea */
GtkWidget *overview_scrolled_window;    /* GtkScrolledWindow */
/* The two values below define the width and height of the map overview. The
 * first set of values (2*62, 2*46) define the size for a netbook display. For
 * bigger displays the values are doubled (default). */
#define OVERVIEW_CANVAS_STORE_WIDTH_NETBOOK  (2 * 64)
#define OVERVIEW_CANVAS_STORE_HEIGHT_NETBOOK (2 * 46)
#define OVERVIEW_CANVAS_STORE_WIDTH \
  (2 * OVERVIEW_CANVAS_STORE_WIDTH_NETBOOK)
#define OVERVIEW_CANVAS_STORE_HEIGHT \
  (2 * OVERVIEW_CANVAS_STORE_HEIGHT_NETBOOK)
int overview_canvas_store_width = OVERVIEW_CANVAS_STORE_WIDTH;
int overview_canvas_store_height = OVERVIEW_CANVAS_STORE_HEIGHT;

GtkWidget *toplevel;
GtkWidget *toplevel_tabs;
GtkWidget *top_vbox;
GtkWidget *top_notebook, *bottom_notebook, *right_notebook;
GtkWidget *map_widget;
static GtkWidget *bottom_hpaned;

PangoFontDescription *city_names_style = NULL;
PangoFontDescription *city_productions_style = NULL;
PangoFontDescription *reqtree_text_style = NULL;

GtkWidget *main_frame_civ_name;
GtkWidget *main_label_info;

GtkWidget *avbox, *ahbox, *conn_box;
GtkWidget* scroll_panel;

GtkWidget *econ_label[10];
GtkWidget *bulb_label;
GtkWidget *sun_label;
GtkWidget *flake_label;
GtkWidget *government_label;
GtkWidget *timeout_label;
GtkWidget *turn_done_button;

GtkWidget *unit_info_label;
GtkWidget *unit_info_box;
GtkWidget *unit_info_frame;

GtkWidget *econ_widget;

const char *const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

static GtkWidget *main_menubar;
static GtkWidget *unit_image_table;
static GtkWidget *unit_image;
static GtkWidget *unit_below_image[MAX_NUM_UNITS_BELOW];
static GtkWidget *more_arrow_pixmap;
static GtkWidget *more_arrow_pixmap_container;

static int unit_id_top;
static int unit_ids[MAX_NUM_UNITS_BELOW];  /* ids of the units icons in
                                            * information display: (or 0) */
GtkTextView *main_message_area;
GtkTextBuffer *message_buffer = NULL;
static GtkWidget *allied_chat_toggle_button;

static enum Display_color_type display_color_type;  /* practically unused */
static gint timer_id;                               /*       ditto        */
static GIOChannel *srv_channel;
static guint srv_id;
gint cur_x, cur_y;

static bool gui_up = FALSE;

static GtkApplication *fc_app;

static struct video_mode vmode = { -1, -1 };

static gboolean show_info_popup(GtkWidget *w, GdkEvent *ev, gpointer data);

static void end_turn_callback(GtkWidget *w, gpointer data);
static gboolean get_net_input(GIOChannel *source, GIOCondition condition,
                              gpointer data);
static void set_wait_for_writable_socket(struct connection *pc,
                                         bool socket_writable);

static void print_usage(void);
static void activate_gui(GtkApplication *app, gpointer data);
static void parse_options(int argc, char **argv);
static gboolean toplevel_key_press_handler(GtkWidget *w, GdkEvent *ev, gpointer data);
static gboolean mouse_scroll_mapcanvas(GtkEventControllerScroll *controller,
                                       gdouble dx, gdouble dy,
                                       gpointer data);

static void tearoff_callback(GtkWidget *b, gpointer data);
static GtkWidget *detached_widget_new(void);
static GtkWidget *detached_widget_fill(GtkWidget *tearbox);

static gboolean select_unit_image_callback(GtkWidget *w, GdkEvent *ev,
                                           gpointer data);
static gboolean select_more_arrow_pixmap_callback(GtkWidget *w, GdkEvent *ev,
                                                  gpointer data);
static gboolean quit_dialog_callback(void);

static void allied_chat_button_toggled(GtkToggleButton *button,
                                       gpointer user_data);

static void free_unit_table(void);

static void adjust_default_options(void);

/**********************************************************************//**
  Callback for freelog
**************************************************************************/
static void log_callback_utf8(enum log_level level, const char *message,
                              bool file_too)
{
  if (!file_too || level <= LOG_FATAL) {
    fc_fprintf(stderr, "%d: %s\n", level, message);
  }
}

/**********************************************************************//**
  Called while in gtk_main() (which is all of the time)
  TIMER_INTERVAL is now set by real_timer_callback()
**************************************************************************/
static gboolean timer_callback(gpointer data)
{
  double seconds = real_timer_callback();

  timer_id = g_timeout_add(seconds * 1000, timer_callback, NULL);

  return FALSE;
}

/**********************************************************************//**
  Print extra usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(void)
{
  /* add client-specific usage information here */
  fc_fprintf(stderr,
             _("gtk4-client gui-specific options are:\n"));

  fc_fprintf(stderr,
             _("-r, --resolution WIDTHxHEIGHT\tAssume given resolution "
               "screen\n"));

  fc_fprintf(stderr, "\n");

  /* TRANS: No full stop after the URL, could cause confusion. */
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

/**********************************************************************//**
  Search for gui-specific command line options.
**************************************************************************/
static void parse_options(int argc, char **argv)
{
  int i = 1;

  while (i < argc) {
    char *option = NULL;

    if (is_option("--help", argv[i])) {
      print_usage();
      exit(EXIT_SUCCESS);
    } else if ((option = get_option_malloc("--resolution", argv, &i, argc,
                                           FALSE))) {
      if (!string_to_video_mode(option, &vmode)) {
        fc_fprintf(stderr, _("Illegal video mode '%s'\n"), option);
        exit(EXIT_FAILURE);
      }
      free(option);
    } else {
      fc_fprintf(stderr, _("Unknown command-line option \"%s\".\n"),
                 argv[i]);
      exit(EXIT_FAILURE);
    }

    i++;
  }
}

/**********************************************************************//**
  Focus on widget. Returns whether focus was really changed.
**************************************************************************/
static gboolean toplevel_focus(GtkWidget *w, GtkDirectionType arg)
{
  switch (arg) {
    case GTK_DIR_TAB_FORWARD:
    case GTK_DIR_TAB_BACKWARD:

      if (!gtk_widget_get_can_focus(w)) {
	return FALSE;
      }

      if (!gtk_widget_is_focus(w)) {
	gtk_widget_grab_focus(w);
	return TRUE;
      }
      break;

    default:
      break;
  }
  return FALSE;
}

/**********************************************************************//**
  When the chatline text view is resized, scroll it to the bottom. This
  prevents users from accidentally missing messages when the chatline
  gets scrolled up a small amount and stops scrolling down automatically.
**************************************************************************/
static void main_message_area_resize(GtkWidget *widget, int width, int height,
                                     gpointer data)
{
  static int old_width = 0, old_height = 0;

  if (width != old_width
      || height != old_height) {
    chatline_scroll_to_bottom(TRUE);
    old_width = width;
    old_height = height;
  }
}

/**********************************************************************//**
  Focus on map canvas
**************************************************************************/
gboolean map_canvas_focus(void)
{
  gtk_window_present(GTK_WINDOW(toplevel));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(top_notebook), 0);
  gtk_widget_grab_focus(map_canvas);

  return TRUE;
}

/**********************************************************************//**
  Handle keypress events when map canvas is in focus
**************************************************************************/
static gboolean key_press_map_canvas(GtkWidget *w, GdkEvent *ev,
                                     gpointer data)
{
  GdkModifierType state;
  guint keyval;

  state = gdk_event_get_modifier_state(ev);
  keyval = gdk_key_event_get_keyval(ev);
  if ((state & GDK_SHIFT_MASK)) {
    switch (keyval) {

    case GDK_KEY_Left:
      scroll_mapview(DIR8_WEST);
      return TRUE;

    case GDK_KEY_Right:
      scroll_mapview(DIR8_EAST);
      return TRUE;

    case GDK_KEY_Up:
      scroll_mapview(DIR8_NORTH);
      return TRUE;

    case GDK_KEY_Down:
      scroll_mapview(DIR8_SOUTH);
      return TRUE;

    case GDK_KEY_Home:
      key_center_capital();
      return TRUE;

    case GDK_KEY_Page_Up:
      g_signal_emit_by_name(main_message_area, "move_cursor",
	                          GTK_MOVEMENT_PAGES, -1, FALSE);
      return TRUE;

    case GDK_KEY_Page_Down:
      g_signal_emit_by_name(main_message_area, "move_cursor",
	                          GTK_MOVEMENT_PAGES, 1, FALSE);
      return TRUE;

    default:
      break;
    }
  } else if (!(state & GDK_CONTROL_MASK)) {
    switch (keyval) {
    default:
      break;
    }
  }

  if (!(state & GDK_CONTROL_MASK)) {
    switch (keyval) {
    case GDK_KEY_plus:
      zoom_step_up();
      return TRUE;

    case GDK_KEY_minus:
      zoom_step_down();
      return TRUE;

    default:
      break;
    }
  }

  /* Return here if observer */
  if (client_is_observer()) {
    return FALSE;
  }

  fc_assert(MAX_NUM_BATTLEGROUPS == 4);

  if ((state & GDK_CONTROL_MASK)) {
    switch (keyval) {

    case GDK_KEY_F1:
      key_unit_assign_battlegroup(0, (state & GDK_SHIFT_MASK));
      return TRUE;

    case GDK_KEY_F2:
      key_unit_assign_battlegroup(1, (state & GDK_SHIFT_MASK));
      return TRUE;

    case GDK_KEY_F3:
      key_unit_assign_battlegroup(2, (state & GDK_SHIFT_MASK));
      return TRUE;

    case GDK_KEY_F4:
      key_unit_assign_battlegroup(3, (state & GDK_SHIFT_MASK));
      return TRUE;

    default:
      break;
    };
  } else if ((state & GDK_SHIFT_MASK)) {
    switch (keyval) {

    case GDK_KEY_F1:
      key_unit_select_battlegroup(0, FALSE);
      return TRUE;

    case GDK_KEY_F2:
      key_unit_select_battlegroup(1, FALSE);
      return TRUE;

    case GDK_KEY_F3:
      key_unit_select_battlegroup(2, FALSE);
      return TRUE;

    case GDK_KEY_F4:
      key_unit_select_battlegroup(3, FALSE);
      return TRUE;

    default:
      break;
    };
  }

  switch (keyval) {

  case GDK_KEY_KP_Up:
  case GDK_KEY_KP_8:
  case GDK_KEY_Up:
  case GDK_KEY_8:
    key_unit_move(DIR8_NORTH);
    return TRUE;

  case GDK_KEY_KP_Page_Up:
  case GDK_KEY_KP_9:
  case GDK_KEY_Page_Up:
  case GDK_KEY_9:
    key_unit_move(DIR8_NORTHEAST);
    return TRUE;

  case GDK_KEY_KP_Right:
  case GDK_KEY_KP_6:
  case GDK_KEY_Right:
  case GDK_KEY_6:
    key_unit_move(DIR8_EAST);
    return TRUE;

  case GDK_KEY_KP_Page_Down:
  case GDK_KEY_KP_3:
  case GDK_KEY_Page_Down:
  case GDK_KEY_3:
    key_unit_move(DIR8_SOUTHEAST);
    return TRUE;

  case GDK_KEY_KP_Down:
  case GDK_KEY_KP_2:
  case GDK_KEY_Down:
  case GDK_KEY_2:
    key_unit_move(DIR8_SOUTH);
    return TRUE;

  case GDK_KEY_KP_End:
  case GDK_KEY_KP_1:
  case GDK_KEY_End:
  case GDK_KEY_1:
    key_unit_move(DIR8_SOUTHWEST);
    return TRUE;

  case GDK_KEY_KP_Left:
  case GDK_KEY_KP_4:
  case GDK_KEY_Left:
  case GDK_KEY_4:
    key_unit_move(DIR8_WEST);
    return TRUE;

  case GDK_KEY_KP_Home:
  case GDK_KEY_KP_7:
  case GDK_KEY_Home:
  case GDK_KEY_7:
    key_unit_move(DIR8_NORTHWEST);
    return TRUE;

  case GDK_KEY_KP_Begin:
  case GDK_KEY_KP_5:
  case GDK_KEY_5:
    key_recall_previous_focus_unit();
    return TRUE;

  case GDK_KEY_Escape:
    key_cancel_action();
    return TRUE;

  case GDK_KEY_b:
    if (tiles_hilited_cities) {
      buy_production_in_selected_cities();
      return TRUE;
    }
    break;

  default:
    break;
  };

  return FALSE;
}

/**********************************************************************//**
  Handle a keyboard key press made in the client's toplevel window.
**************************************************************************/
static gboolean toplevel_key_press_handler(GtkWidget *w, GdkEvent *ev,
                                           gpointer data)
{
  guint keyval;
  GdkModifierType state;

  if (inputline_has_focus()) {
    return FALSE;
  }

  keyval = gdk_key_event_get_keyval(ev);
  switch (keyval) {

  case GDK_KEY_apostrophe:
    /* Allow this even if not in main map view; chatline is present on
     * some other pages too */

    /* Make the chatline visible if it's not currently.
     * FIXME: should find the correct window, even when detached, from any
     * other window; should scroll to the bottom automatically showing the
     * latest text from other players; MUST NOT make spurious text windows
     * at the bottom of other dialogs. */
    if (gtk_widget_get_mapped(top_vbox)) {
      /* The main game view is visible. May need to switch notebook. */
      if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_MERGED) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(top_notebook), 1);
      } else {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom_notebook), 0);
      }
    }

    /* If the chatline is (now) visible, focus it. */
    if (inputline_is_visible()) {
      inputline_grab_focus();
      return TRUE;
    } else {
      break;
    }

  default:
    break;
  }

  if (!gtk_widget_get_mapped(top_vbox)
      || !can_client_change_view()) {
    return FALSE;
  }

  if (editor_is_active()) {
    if (handle_edit_key_press(ev)) {
      return TRUE;
    }
  }

  state = gdk_event_get_modifier_state(ev);
  if (state & GDK_SHIFT_MASK) {
    switch (keyval) {

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      key_end_turn();
      return TRUE;

    default:
      break;
    }
  }

  if (0 == gtk_notebook_get_current_page(GTK_NOTEBOOK(top_notebook))) {
    /* 0 means the map view is focused. */
    return key_press_map_canvas(w, ev, data);
  }

#if 0
  /* We are focused some other dialog, tab, or widget. */
  if ((state & GDK_CONTROL_MASK)) {
  } else if ((state & GDK_SHIFT_MASK)) {
  } else {
    switch (keyval) {

    case GDK_KEY_F4:
      map_canvas_focus();
      return TRUE;

    default:
      break;
    };
  }
#endif /* 0 */

  return FALSE;
}

/**********************************************************************//**
  Mouse/touchpad scrolling over the mapview
**************************************************************************/
static gboolean mouse_scroll_mapcanvas(GtkEventControllerScroll *controller,
                                       gdouble dx, gdouble dy,
                                       gpointer data)
{
  int scroll_x, scroll_y, xstep, ystep;
  gdouble e_x, e_y;
  GdkModifierType state;

  if (!can_client_change_view()) {
    return FALSE;
  }

  get_mapview_scroll_pos(&scroll_x, &scroll_y);
  get_mapview_scroll_step(&xstep, &ystep);

  scroll_y += ystep * dy;
  scroll_x += xstep * dx;

  set_mapview_scroll_pos(scroll_x, scroll_y);

  /* Emulating mouse move now */
  if (!gtk_widget_has_focus(map_canvas)) {
    gtk_widget_grab_focus(map_canvas);
  }

  gdk_event_get_position(gtk_event_controller_get_current_event(
                                      GTK_EVENT_CONTROLLER(controller)),
                         &e_x, &e_y);
  update_line(e_x, e_y);
  state = gtk_event_controller_get_current_event_state(
                                      GTK_EVENT_CONTROLLER(controller));
  if (rbutton_down && (state & GDK_BUTTON3_MASK)) {
    update_selection_rectangle(e_x, e_y);
  }

  if (keyboardless_goto_button_down && hover_state == HOVER_NONE) {
    maybe_activate_keyboardless_goto(cur_x, cur_y);
  }

  control_mouse_cursor(canvas_pos_to_tile(cur_x, cur_y));

  return TRUE;
}

/**********************************************************************//**
  Move widget from parent to parent
**************************************************************************/
static void move_from_container_to_container(GtkWidget *wdg,
                                             GtkWidget *old_wdg,
                                             GtkWidget *new_wdg)
{
  g_object_ref(wdg); /* Make sure reference count stays above 0
                      * during the transition to new parent. */

  if (GTK_IS_PANED(old_wdg)) {
    gtk_paned_set_end_child(GTK_PANED(old_wdg), NULL);
  } else if (GTK_IS_WINDOW(old_wdg)) {
    gtk_window_set_child(GTK_WINDOW(old_wdg), NULL);
  } else {
    fc_assert(GTK_IS_BOX(old_wdg));

    gtk_box_remove(GTK_BOX(old_wdg), wdg);
  }

  if (GTK_IS_PANED(new_wdg)) {
    gtk_paned_set_end_child(GTK_PANED(new_wdg), wdg);
  } else if (GTK_IS_WINDOW(new_wdg)) {
    gtk_window_set_child(GTK_WINDOW(new_wdg), wdg);
  } else {
    fc_assert(GTK_IS_BOX(new_wdg));

    gtk_box_append(GTK_BOX(new_wdg), wdg);
  }

  g_object_unref(wdg);
}

/**********************************************************************//**
  Reattaches the detached widget when the user destroys it.
**************************************************************************/
static void tearoff_destroy(GtkWidget *w, gpointer data)
{
  GtkWidget *p, *b, *box;
  GtkWidget *old_parent;

  box = GTK_WIDGET(data);
  old_parent = gtk_widget_get_parent(box);
  p = g_object_get_data(G_OBJECT(w), "parent");
  b = g_object_get_data(G_OBJECT(w), "toggle");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), FALSE);

  gtk_widget_hide(w);

  move_from_container_to_container(box, old_parent, p);
}

/**********************************************************************//**
  Callback for the toggle button in the detachable widget: causes the
  widget to detach or reattach.
**************************************************************************/
static void tearoff_callback(GtkWidget *b, gpointer data)
{
  GtkWidget *box = GTK_WIDGET(data);
  GtkWidget *old_parent = gtk_widget_get_parent(box);

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))) {
    GtkWidget *w;
    GtkWidget *temp_hide;

    w = gtk_window_new();
    setup_dialog(w, toplevel);
    gtk_widget_set_name(w, "Freeciv");
    gtk_window_set_title(GTK_WINDOW(w), _("Freeciv"));
    g_signal_connect(w, "destroy", G_CALLBACK(tearoff_destroy), box);

    g_object_set_data(G_OBJECT(w), "parent", gtk_widget_get_parent(box));
    g_object_set_data(G_OBJECT(w), "toggle", b);

    temp_hide = g_object_get_data(G_OBJECT(box), "hide-over-reparent");
    if (temp_hide != NULL) {
      gtk_widget_hide(temp_hide);
    }

    move_from_container_to_container(box, old_parent, w);

    gtk_widget_show(w);

    if (temp_hide != NULL) {
      gtk_widget_show(temp_hide);
    }
  } else {
    if (GTK_IS_BOX(old_parent)) {
      gtk_box_remove(GTK_BOX(old_parent), box);
    } else {
      fc_assert(GTK_IS_PANED(old_parent));

      gtk_paned_set_end_child(GTK_PANED(old_parent), NULL);
    }
  }
}

/**********************************************************************//**
  Create the container for the widget that's able to be detached.
**************************************************************************/
static GtkWidget *detached_widget_new(void)
{
  GtkWidget *hgrid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

  return hgrid;
}

/**********************************************************************//**
  Creates the toggle button necessary to detach and reattach the widget
  and returns a vbox in which you fill your goodies.
**************************************************************************/
static GtkWidget *detached_widget_fill(GtkWidget *tearbox)
{
  GtkWidget *b, *fillbox;
  static GtkCssProvider *detach_button_provider = NULL;

  if (detach_button_provider == NULL) {
    detach_button_provider = gtk_css_provider_new();

    /* These toggle buttons run vertically down the side of many UI
     * elements, so they need to be thin horizontally. */
    gtk_css_provider_load_from_data(detach_button_provider,
                                    ".detach_button {\n"
                                    "  padding: 0px 0px 0px 0px;\n"
                                    "  min-width: 6px;\n"
                                    "}",
                                    -1);
  }

  b = gtk_toggle_button_new();
  gtk_style_context_add_provider(gtk_widget_get_style_context(b),
                                 GTK_STYLE_PROVIDER(detach_button_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_class(gtk_widget_get_style_context(b),
                              "detach_button");

  gtk_box_append(GTK_BOX(tearbox), b);
  g_signal_connect(b, "toggled", G_CALLBACK(tearoff_callback), tearbox);

  fillbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_box_append(GTK_BOX(tearbox), fillbox);

  return fillbox;
}

/**********************************************************************//**
  Called to build the unit_below pixmap table.  This is the table on the
  left of the screen that shows all of the inactive units in the current
  tile.

  It may be called again if the tileset changes.
**************************************************************************/
static void populate_unit_image_table(void)
{
  int i, width;
  GtkWidget *table = unit_image_table;
  GdkPixbuf *pix;

  /* get width of the overview window */
  width = (overview_canvas_store_width > GUI_GTK_OVERVIEW_MIN_XSIZE) ? overview_canvas_store_width
                                               : GUI_GTK_OVERVIEW_MIN_XSIZE;

  if (GUI_GTK_OPTION(small_display_layout)) {
    /* We want arrow to appear if there is other units in addition
       to active one in tile. Active unit is not counted, so there
       can be 0 other units to not to display arrow. */
    num_units_below = 1 - 1;
  } else {
    num_units_below = width / (int) tileset_tile_width(tileset);
    num_units_below = CLIP(1, num_units_below, MAX_NUM_UNITS_BELOW);
  }

  /* Top row: the active unit. */
  /* Note, we ref this and other widgets here so that we can unref them
   * in reset_unit_table. */
  unit_image = gtk_image_new();
  g_object_ref(unit_image);
  gtk_widget_set_size_request(unit_image, tileset_tile_width(tileset), -1);
  gtk_grid_attach(GTK_GRID(table), unit_image, 0, 0, 1, 1);
  g_signal_connect(unit_image, "button_press_event",
                   G_CALLBACK(select_unit_image_callback),
                   GINT_TO_POINTER(-1));

  if (!GUI_GTK_OPTION(small_display_layout)) {
    /* Bottom row: other units in the same tile. */
    for (i = 0; i < num_units_below; i++) {
      unit_below_image[i] = gtk_image_new();
      g_object_ref(unit_below_image[i]);
      gtk_widget_set_size_request(unit_below_image[i],
                                  tileset_tile_width(tileset), -1);
      g_signal_connect(unit_below_image[i],
                       "button_press_event",
                       G_CALLBACK(select_unit_image_callback),
                       GINT_TO_POINTER(i));

      gtk_grid_attach(GTK_GRID(table), unit_below_image[i],
                      i, 1, 1, 1);
    }
  }

  /* create arrow (popup for all units on the selected tile) */
  pix = sprite_get_pixbuf(get_arrow_sprite(tileset, ARROW_RIGHT));
  more_arrow_pixmap = gtk_image_new_from_pixbuf(pix);
  g_object_ref(more_arrow_pixmap);
  g_signal_connect(more_arrow_pixmap,
                   "button_press_event",
                   G_CALLBACK(select_more_arrow_pixmap_callback), NULL);
  /* An extra layer so that we can hide the clickable button but keep
   * an explicit size request to avoid the layout jumping around */
  more_arrow_pixmap_container = gtk_frame_new(NULL);
  gtk_widget_set_halign(more_arrow_pixmap_container, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(more_arrow_pixmap_container, GTK_ALIGN_CENTER);
  g_object_ref(more_arrow_pixmap_container);
  gtk_frame_set_child(GTK_FRAME(more_arrow_pixmap_container),
                      more_arrow_pixmap);
  gtk_widget_set_size_request(more_arrow_pixmap_container,
                              gdk_pixbuf_get_width(pix), -1);
  g_object_unref(G_OBJECT(pix));

  if (!GUI_GTK_OPTION(small_display_layout)) {
    /* Display on bottom row. */
    gtk_grid_attach(GTK_GRID(table), more_arrow_pixmap_container,
                    MAX_NUM_UNITS_BELOW, 1, 1, 1);
  } else {
    /* Display on top row (there is no bottom row). */
    gtk_grid_attach(GTK_GRID(table), more_arrow_pixmap_container,
                    MAX_NUM_UNITS_BELOW, 0, 1, 1);
  }

  gtk_widget_show(table);
}

/**********************************************************************//**
  Free unit image table.
**************************************************************************/
static void free_unit_table(void)
{
  if (unit_image) {
    gtk_grid_remove(GTK_GRID(unit_image_table), unit_image);
    g_object_unref(unit_image);
    if (!GUI_GTK_OPTION(small_display_layout)) {
      int i;

      for (i = 0; i < num_units_below; i++) {
        gtk_grid_remove(GTK_GRID(unit_image_table),
                        unit_below_image[i]);
        g_object_unref(unit_below_image[i]);
      }
    }
    gtk_grid_remove(GTK_GRID(unit_image_table),
                    more_arrow_pixmap_container);
    g_object_unref(more_arrow_pixmap);
    g_object_unref(more_arrow_pixmap_container);
  }
}

/**********************************************************************//**
  Called when the tileset is changed to reset the unit pixmap table.
**************************************************************************/
void reset_unit_table(void)
{
  /* Unreference all of the widgets that we're about to reallocate, thus
   * avoiding a memory leak. Remove them from the container first, just
   * to be safe. Note, the widgets are ref'd in
   * populatate_unit_image_table. */
  free_unit_table();

  populate_unit_image_table();

  /* We have to force a redraw of the units.  And we explicitly have
   * to force a redraw of the focus unit, which is normally only
   * redrawn when the focus changes. We also have to force the 'more'
   * arrow to go away, both by expicitly hiding it and telling it to
   * do so (this will be reset immediately afterwards if necessary,
   * but we have to make the *internal* state consistent). */
  gtk_widget_hide(more_arrow_pixmap);
  set_unit_icons_more_arrow(FALSE);
  if (get_num_units_in_focus() == 1) {
    set_unit_icon(-1, head_of_units_in_focus());
  } else {
    set_unit_icon(-1, NULL);
  }
  update_unit_pix_label(get_units_in_focus());
}

/**********************************************************************//**
  Enable/Disable the game page menu bar.
**************************************************************************/
void enable_menus(bool enable)
{
  if (enable) {
    main_menubar = setup_menus(toplevel);
    /* Ensure the menus are really created before performing any operations
     * on them. */
    while (g_main_context_pending(NULL)) {
      g_main_context_iteration(NULL, FALSE);
    }
    gtk_grid_attach_next_to(GTK_GRID(top_vbox), main_menubar, NULL, GTK_POS_TOP, 1, 1);
    menus_init();
    gtk_widget_show(main_menubar);
  } else {
#ifdef MENUS_GTK3
    gtk_widget_destroy(main_menubar);
#endif
  }
}

/**********************************************************************//**
  Workaround for a crash that occurs when a button release event is
  emitted for a notebook with no pages. See PR#40743.
  FIXME: Remove this hack once gtk_notebook_button_release() in
  gtk/gtknotebook.c checks for NULL notebook->cur_page.
**************************************************************************/
static gboolean right_notebook_button_release(GtkWidget *widget,
                                              GdkEvent *event)
{
  if (gdk_event_get_event_type(event) != GDK_BUTTON_RELEASE) {
    return FALSE;
  }

  if (!GTK_IS_NOTEBOOK(widget)
      || -1 == gtk_notebook_get_current_page(GTK_NOTEBOOK(widget))) {
    /* Make sure the default gtk handler
     * does NOT get called in this case. */
    return TRUE;
  }

  return FALSE;
}

/**********************************************************************//**
  Override background color for canvases
**************************************************************************/
#if 0
static void setup_canvas_color_for_state(GtkStateFlags state)
{
  gtk_widget_override_background_color(GTK_WIDGET(overview_canvas), state,
                                       &get_color(tileset, COLOR_OVERVIEW_UNKNOWN)->color);
  gtk_widget_override_background_color(GTK_WIDGET(map_canvas), state,
                                       &get_color(tileset, COLOR_OVERVIEW_UNKNOWN)->color);
}
#endif

/**********************************************************************//**
  Callback that just returns TRUE.
**************************************************************************/
bool terminate_signal_processing(void)
{
  return TRUE;
}

/**********************************************************************//**
  Do the heavy lifting for the widget setup.
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget *page, *hgrid, *hgrid2, *label;
  GtkWidget *frame, *table, *table2, *paned, *hpaned, *sw, *text;
  GtkWidget *button, *view, *vgrid, *vbox, *right_vbox = NULL;
  int i;
  char buf[256];
  GtkWidget *notebook, *statusbar;
  GtkWidget *dtach_lowbox = NULL;
  struct sprite *spr;
  int grid_row = 0;
  int right_row = 0;
  int top_row = 0;
  int grid_col = 0;
  int grid2_col = 0;
  GtkGesture *mc_gesture;
  GtkEventController *mc_controller;

  message_buffer = gtk_text_buffer_new(NULL);

  notebook = gtk_notebook_new();

  /* stop mouse wheel notebook page switching. */
  g_signal_connect(notebook, "scroll_event",
                   G_CALLBACK(terminate_signal_processing), NULL);

  toplevel_tabs = notebook;
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  vgrid = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(vgrid), 4);
  gtk_window_set_child(GTK_WINDOW(toplevel), vgrid);
  gtk_grid_attach(GTK_GRID(vgrid), notebook, 0, grid_row++, 1, 1);
  statusbar = create_statusbar();
  gtk_grid_attach(GTK_GRID(vgrid), statusbar, 0, grid_row++, 1, 1);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
      create_main_page(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
      create_start_page(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
      create_scenario_page(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
      create_load_page(), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
      create_network_page(), NULL);

  editgui_create_widgets();

  ingame_votebar = voteinfo_bar_new(FALSE);
  g_object_set(ingame_votebar, "margin", 2, NULL);

  /* *** everything in the top *** */

  page = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(page),
                                    TRUE);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, NULL);

  top_vbox = gtk_grid_new();
  gtk_orientable_set_orientation(GTK_ORIENTABLE(top_vbox),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing(GTK_GRID(top_vbox), 5);
  hgrid = gtk_grid_new();

  if (GUI_GTK_OPTION(small_display_layout)) {
    /* The window is divided into two horizontal panels: overview +
     * civinfo + unitinfo, main view + message window. */
    right_vbox = gtk_grid_new();
    right_row = 0;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(right_vbox),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_grid_attach(GTK_GRID(hgrid), right_vbox, grid_col++, 0, 1, 1);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), top_vbox);
    gtk_grid_attach(GTK_GRID(top_vbox), hgrid, 0, top_row++, 1, 1);
    gtk_grid_attach(GTK_GRID(right_vbox), paned, 0, right_row++, 1, 1);
    gtk_grid_attach(GTK_GRID(right_vbox), ingame_votebar, 0, right_row++, 1, 1);

    /* Overview size designed for small displays (netbooks). */
    overview_canvas_store_width = OVERVIEW_CANVAS_STORE_WIDTH_NETBOOK;
    overview_canvas_store_height = OVERVIEW_CANVAS_STORE_HEIGHT_NETBOOK;
  } else {
    /* The window is divided into two vertical panes: overview +
     * + civinfo + unitinfo + main view, message window. */
    paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), paned);
    gtk_paned_set_start_child(GTK_PANED(paned), top_vbox);
    gtk_grid_attach(GTK_GRID(top_vbox), hgrid, 0, top_row++, 1, 1);

    /* Overview size designed for big displays (desktops). */
    overview_canvas_store_width = OVERVIEW_CANVAS_STORE_WIDTH;
    overview_canvas_store_height = OVERVIEW_CANVAS_STORE_HEIGHT;
  }

  /* this holds the overview canvas, production info, etc. */
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_grid_set_column_spacing(GTK_GRID(vbox), 3);
  /* Put vgrid to the left of anything else in hgrid -- right_vbox is either
   * the chat/messages pane, or NULL which is OK */
  gtk_grid_attach_next_to(GTK_GRID(hgrid), vbox, right_vbox,
                          GTK_POS_LEFT, 1, 1);
  grid_col++;

  /* overview canvas */
  ahbox = detached_widget_new();
  gtk_widget_set_hexpand(ahbox, FALSE);
  gtk_widget_set_vexpand(ahbox, FALSE);
  gtk_box_append(GTK_BOX(vbox), ahbox);
  avbox = detached_widget_fill(ahbox);

  overview_scrolled_window = gtk_scrolled_window_new();
  gtk_widget_set_margin_start(overview_scrolled_window, 1);
  gtk_widget_set_margin_end(overview_scrolled_window, 1);
  gtk_widget_set_margin_top(overview_scrolled_window, 1);
  gtk_widget_set_margin_bottom(overview_scrolled_window, 1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (overview_scrolled_window),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  overview_canvas = gtk_drawing_area_new();
  gtk_widget_set_halign(overview_canvas, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(overview_canvas, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(overview_canvas, overview_canvas_store_width,
		              overview_canvas_store_height);
  gtk_widget_set_size_request(overview_scrolled_window, overview_canvas_store_width,
		              overview_canvas_store_height);
  gtk_widget_set_hexpand(overview_canvas, TRUE);
  gtk_widget_set_vexpand(overview_canvas, TRUE);

  gtk_box_append(GTK_BOX(avbox), overview_scrolled_window);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(overview_scrolled_window),
                                overview_canvas);

  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(overview_canvas),
                                 overview_canvas_draw, NULL, NULL);

  mc_controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(left_butt_down_overviewcanvas), NULL);
  gtk_widget_add_controller(overview_canvas, mc_controller);
  mc_gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mc_gesture), 3);
  mc_controller = GTK_EVENT_CONTROLLER(mc_gesture);
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(right_butt_down_overviewcanvas), NULL);
  gtk_widget_add_controller(overview_canvas, mc_controller);
  mc_controller = gtk_event_controller_motion_new();
  g_signal_connect(mc_controller, "motion",
                   G_CALLBACK(move_overviewcanvas), NULL);
  gtk_widget_add_controller(overview_canvas, mc_controller);

  /* The rest */

  ahbox = detached_widget_new();
  gtk_box_append(GTK_BOX(vbox), ahbox);
  gtk_widget_set_hexpand(ahbox, FALSE);
  avbox = detached_widget_fill(ahbox);
  gtk_widget_set_vexpand(avbox, TRUE);
  gtk_widget_set_valign(avbox, GTK_ALIGN_FILL);

  /* Info on player's civilization, when game is running. */
  frame = gtk_frame_new("");
  gtk_box_append(GTK_BOX(avbox), frame);

  main_frame_civ_name = frame;

  vgrid = gtk_grid_new();
  grid_row = 0;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_frame_set_child(GTK_FRAME(frame), vgrid);
  gtk_widget_set_hexpand(vgrid, TRUE);

  label = gtk_label_new(NULL);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(label, 2);
  gtk_widget_set_margin_end(label, 2);
  gtk_widget_set_margin_top(label, 2);
  gtk_widget_set_margin_bottom(label, 2);
  g_signal_connect(label, "button_press_event",
                   G_CALLBACK(show_info_popup), NULL);
  gtk_grid_attach(GTK_GRID(vgrid), label, 0, grid_row++, 1, 1);
  main_label_info = label;

  /* Production status */
  table = gtk_grid_new();
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_grid_set_column_homogeneous(GTK_GRID(table), TRUE);
  gtk_box_append(GTK_BOX(avbox), table);

  /* citizens for taxrates */
  table2 = gtk_grid_new();
  gtk_grid_attach(GTK_GRID(table), table2, 0, 0, 10, 1);
  econ_widget = table2;

  for (i = 0; i < 10; i++) {
    GtkEventController *controller;

    spr = i < 5 ? get_tax_sprite(tileset, O_SCIENCE) : get_tax_sprite(tileset, O_GOLD);
    econ_label[i] = image_new_from_surface(spr->surface);

    g_object_set_data(G_OBJECT(econ_label[i]), "rate_button", GUINT_TO_POINTER(i));
    controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    g_signal_connect(controller, "pressed",
                     G_CALLBACK(taxrates_callback), NULL);
    gtk_widget_add_controller(econ_label[i], controller);
    gtk_grid_attach(GTK_GRID(table2), econ_label[i], i, 0, 1, 1);
  }

  /* science, environmental, govt, timeout */
  spr = client_research_sprite();
  if (spr != NULL) {
    bulb_label = image_new_from_surface(spr->surface);
  } else {
    bulb_label = gtk_image_new();
  }

  spr = client_warming_sprite();
  if (spr != NULL) {
    sun_label = image_new_from_surface(spr->surface);
  } else {
    sun_label = gtk_image_new();
  }

  spr = client_cooling_sprite();
  if (spr != NULL) {
    flake_label = image_new_from_surface(spr->surface);
  } else {
    flake_label = gtk_image_new();
  }

  spr = client_government_sprite();
  if (spr != NULL) {
    government_label = image_new_from_surface(spr->surface);
  } else {
    government_label = gtk_image_new();
  }

  for (i = 0; i < 4; i++) {
    GtkWidget *w;

    switch (i) {
    case 0:
      w = bulb_label;
      break;
    case 1:
      w = sun_label;
      break;
    case 2:
      w = flake_label;
      break;
    default:
    case 3:
      w = government_label;
      break;
    }

    gtk_widget_set_halign(w, GTK_ALIGN_START);
    gtk_widget_set_valign(w, GTK_ALIGN_START);
    gtk_widget_set_margin_start(w, 0);
    gtk_widget_set_margin_end(w, 0);
    gtk_widget_set_margin_top(w, 0);
    gtk_widget_set_margin_bottom(w, 0);
    gtk_grid_attach(GTK_GRID(table), w, i, 1, 1, 1);
  }

  timeout_label = gtk_label_new("");

  frame = gtk_frame_new(NULL);
  gtk_grid_attach(GTK_GRID(table), frame, 4, 1, 6, 1);
  gtk_frame_set_child(GTK_FRAME(frame), timeout_label);


  /* turn done */
  turn_done_button = gtk_button_new_with_label(_("Turn Done"));

  gtk_grid_attach(GTK_GRID(table), turn_done_button, 0, 2, 10, 1);

  g_signal_connect(turn_done_button, "clicked",
                   G_CALLBACK(end_turn_callback), NULL);

  fc_snprintf(buf, sizeof(buf), "%s:\n%s",
              _("Turn Done"), _("Shift+Return"));
  gtk_widget_set_tooltip_text(turn_done_button, buf);

  /* Selected unit status */

  /* If you turn this to something else than GtkBox, also adjust
   * editgui.c replace_widget() code that removes and adds widgets from it. */
  unit_info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(unit_info_box, FALSE);
  gtk_box_append(GTK_BOX(avbox), unit_info_box);

  /* In edit mode the unit_info_box widget is replaced by the
   * editinfobox, so we need to add a ref here so that it is
   * not destroyed when removed from its container.
   * See editinfobox_refresh() call to replace_widget() */
  g_object_ref(unit_info_box);

  unit_info_frame = gtk_frame_new("");
  gtk_box_append(GTK_BOX(unit_info_box), unit_info_frame);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw), TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_frame_set_child(GTK_FRAME(unit_info_frame), sw);

  label = gtk_label_new(NULL);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(label, 2);
  gtk_widget_set_margin_end(label, 2);
  gtk_widget_set_margin_top(label, 2);
  gtk_widget_set_margin_bottom(label, 2);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), label);
  unit_info_label = label;

  hgrid2 = gtk_grid_new();
  gtk_box_append(GTK_BOX(unit_info_box), hgrid2);

  table = gtk_grid_new();
  g_object_set(table, "margin", 5, NULL);
  gtk_grid_attach(GTK_GRID(hgrid2), table, grid2_col++, 0, 1, 1);

  gtk_grid_set_row_spacing(GTK_GRID(table), 2);
  gtk_grid_set_column_spacing(GTK_GRID(table), 2);

  unit_image_table = table;

  /* Map canvas, editor toolbar, and scrollbars */

  /* The top notebook containing the map view and dialogs. */

  top_notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(top_notebook), GTK_POS_BOTTOM);
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(top_notebook), TRUE);


  if (GUI_GTK_OPTION(small_display_layout)) {
    gtk_paned_set_start_child(GTK_PANED(paned), top_notebook);
  } else if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_MERGED) {
    right_vbox = gtk_grid_new();
    right_row = 0;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(right_vbox),
                                   GTK_ORIENTATION_VERTICAL);

    gtk_grid_attach(GTK_GRID(right_vbox), top_notebook, 0, right_row++, 1, 1);
    gtk_grid_attach(GTK_GRID(right_vbox), ingame_votebar, 0, right_row++, 1, 1);
    gtk_grid_attach(GTK_GRID(hgrid), right_vbox, grid_col++, 0, 1, 1);
  } else {
    gtk_grid_attach(GTK_GRID(hgrid), top_notebook, grid_col++, 0, 1, 1);
  }

  map_widget = gtk_grid_new();

  vgrid = gtk_grid_new();
  grid_row = 0;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_grid_attach(GTK_GRID(vgrid), map_widget, 0, grid_row++, 1, 1);

  gtk_grid_attach(GTK_GRID(vgrid), editgui_get_editbar()->widget,
                  0, grid_row++, 1, 1);
  g_object_set(editgui_get_editbar()->widget, "margin", 4, NULL);

  label = gtk_label_new(Q_("?noun:View"));
  gtk_notebook_append_page(GTK_NOTEBOOK(top_notebook), vgrid, label);

  frame = gtk_frame_new(NULL);
  gtk_grid_attach(GTK_GRID(map_widget), frame, 0, 0, 1, 1);

  map_canvas = gtk_drawing_area_new();
  gtk_widget_set_hexpand(map_canvas, TRUE);
  gtk_widget_set_vexpand(map_canvas, TRUE);
  gtk_widget_set_size_request(map_canvas, 300, 300);
  gtk_widget_set_can_focus(map_canvas, TRUE);

#if 0
  setup_canvas_color_for_state(GTK_STATE_FLAG_NORMAL);
  setup_canvas_color_for_state(GTK_STATE_FLAG_ACTIVE);
  setup_canvas_color_for_state(GTK_STATE_FLAG_PRELIGHT);
  setup_canvas_color_for_state(GTK_STATE_FLAG_SELECTED);
  setup_canvas_color_for_state(GTK_STATE_FLAG_INSENSITIVE);
  setup_canvas_color_for_state(GTK_STATE_FLAG_INCONSISTENT);
  setup_canvas_color_for_state(GTK_STATE_FLAG_FOCUSED);
  setup_canvas_color_for_state(GTK_STATE_FLAG_BACKDROP);
#endif /* 0 */

  gtk_frame_set_child(GTK_FRAME(frame), map_canvas);

  map_horizontal_scrollbar =
      gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, NULL);
  gtk_grid_attach(GTK_GRID(map_widget), map_horizontal_scrollbar, 0, 1, 1, 1);

  map_vertical_scrollbar =
      gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
  gtk_grid_attach(GTK_GRID(map_widget), map_vertical_scrollbar, 1, 0, 1, 1);

  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(map_canvas), map_canvas_draw,
                                 NULL, NULL);

  mc_controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(left_butt_down_mapcanvas), NULL);
  g_signal_connect(mc_controller, "released",
                   G_CALLBACK(left_butt_up_mapcanvas), NULL);
  gtk_widget_add_controller(map_canvas, mc_controller);
  mc_gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mc_gesture), 3);
  mc_controller = GTK_EVENT_CONTROLLER(mc_gesture);
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(right_butt_down_mapcanvas), NULL);
  g_signal_connect(mc_controller, "released",
                   G_CALLBACK(right_butt_up_mapcanvas), NULL);
  gtk_widget_add_controller(map_canvas, mc_controller);
  mc_gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mc_gesture), 2);
  mc_controller = GTK_EVENT_CONTROLLER(mc_gesture);
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(middle_butt_down_mapcanvas), NULL);
  gtk_widget_add_controller(map_canvas, mc_controller);
  mc_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(mc_controller, "scroll",
                   G_CALLBACK(mouse_scroll_mapcanvas), NULL);
  gtk_widget_add_controller(map_canvas, mc_controller);
  mc_controller = gtk_event_controller_motion_new();
  g_signal_connect(mc_controller, "motion",
                   G_CALLBACK(move_mapcanvas), NULL);
  g_signal_connect(mc_controller, "leave",
                   G_CALLBACK(leave_mapcanvas), NULL);
  gtk_widget_add_controller(map_canvas, mc_controller);

  g_signal_connect(map_canvas, "resize",
                   G_CALLBACK(map_canvas_resize), NULL);

  g_signal_connect(toplevel, "key_press_event",
                   G_CALLBACK(toplevel_key_press_handler), NULL);

  /* *** The message window -- this is a detachable widget *** */

  if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_MERGED) {
    bottom_hpaned = hpaned = paned;
    right_notebook = bottom_notebook = top_notebook;
  } else {
    dtach_lowbox = detached_widget_new();
    gtk_paned_set_end_child(GTK_PANED(paned), dtach_lowbox);
    avbox = detached_widget_fill(dtach_lowbox);

    vgrid = gtk_grid_new();
    grid_row = 0;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                   GTK_ORIENTATION_VERTICAL);
    if (!GUI_GTK_OPTION(small_display_layout)) {
      gtk_grid_attach(GTK_GRID(vgrid), ingame_votebar, 0, grid_row++, 1, 1);
    }
    gtk_box_append(GTK_BOX(avbox), vgrid);

    if (GUI_GTK_OPTION(small_display_layout)) {
      hpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    } else {
      hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    }
    gtk_grid_attach(GTK_GRID(vgrid), hpaned, 0, grid_row++, 1, 1);
    g_object_set(hpaned, "margin", 4, NULL);
    bottom_hpaned = hpaned;

    bottom_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(bottom_notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(bottom_notebook), TRUE);
    gtk_paned_set_start_child(GTK_PANED(hpaned), bottom_notebook);

    right_notebook = gtk_notebook_new();
    g_object_ref(right_notebook);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(right_notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(right_notebook), TRUE);
    g_signal_connect(right_notebook, "button-release-event",
                     G_CALLBACK(right_notebook_button_release), NULL);
    if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_SPLIT) {
      gtk_paned_set_end_child(GTK_PANED(hpaned), right_notebook);
    }
  }

  vgrid = gtk_grid_new();
  grid_row = 0;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(vgrid),
                                 GTK_ORIENTATION_VERTICAL);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw),
				    TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
  				 GTK_POLICY_ALWAYS);
  gtk_grid_attach(GTK_GRID(vgrid), sw, 0, grid_row++, 1, 1);

  label = gtk_label_new(_("Chat"));
  gtk_notebook_append_page(GTK_NOTEBOOK(bottom_notebook), vgrid, label);

  text = gtk_text_view_new_with_buffer(message_buffer);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  set_message_buffer_view_link_handlers(text);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), text);
  g_signal_connect(text, "resize",
                   G_CALLBACK(main_message_area_resize), NULL);

  gtk_widget_set_name(text, "chatline");

  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_widget_realize(text);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);

  main_message_area = GTK_TEXT_VIEW(text);
  if (dtach_lowbox != NULL) {
    g_object_set_data(G_OBJECT(dtach_lowbox), "hide-over-reparent", main_message_area);
  }

  chat_welcome_message(TRUE);

  /* the chat line */
  view = inputline_toolkit_view_new();
  gtk_grid_attach(GTK_GRID(vgrid), view, 0, grid_row++, 1, 1);
  g_object_set(view, "margin", 3, NULL);

  button = gtk_check_button_new_with_label(_("Allies Only"));
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                               GUI_GTK_OPTION(allied_chat_only));
  g_signal_connect(button, "toggled",
                   G_CALLBACK(allied_chat_button_toggled), NULL);
  inputline_toolkit_view_append_button(view, button);
  allied_chat_toggle_button = button;

  button = gtk_button_new_with_label(_("Clear links"));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(link_marks_clear_all), NULL);
  inputline_toolkit_view_append_button(view, button);

  /* Other things to take care of */

  gtk_widget_show(gtk_window_get_child(GTK_WINDOW(toplevel)));

  if (GUI_GTK_OPTION(enable_tabs)) {
    meswin_dialog_popup(FALSE);
  }

  gtk_notebook_set_current_page(GTK_NOTEBOOK(top_notebook), 0);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom_notebook), 0);

  if (!GUI_GTK_OPTION(map_scrollbars)) {
    gtk_widget_hide(map_horizontal_scrollbar);
    gtk_widget_hide(map_vertical_scrollbar);
  }
}

/**********************************************************************//**
  Called from main().
**************************************************************************/
void ui_init(void)
{
  log_set_callback(log_callback_utf8);
  set_frame_by_frame_animation();
}

/**********************************************************************//**
  Entry point for whole freeciv client program.
**************************************************************************/
int main(int argc, char **argv)
{
  return client_main(argc, argv);
}

/**********************************************************************//**
  Migrate gtk3 client specific options from gtk2 client options.
**************************************************************************/
static void migrate_options_from_gtk2(void)
{
  log_normal(_("Migrating options from gtk2 to gtk3 client"));

#define MIGRATE_OPTION(opt) gui_options.gui_gtk3_##opt = gui_options.gui_gtk2_##opt;
#define MIGRATE_STR_OPTION(opt) \
  strncpy(gui_options.gui_gtk3_##opt, gui_options.gui_gtk2_##opt,      \
          sizeof(gui_options.gui_gtk3_##opt));

  /* Default theme name is never migrated */
  /* 'fullscreen', 'small_display_layout', and 'message_chat_location'
   * not migrated, as (unlike Gtk2), Gtk3-client tries to pick better
   * defaults for these in fresh installations based on screen size (see
   * adjust_default_options()); so user is probably better served by
   * getting these adaptive defaults than whatever they had for Gtk2.
   * Since 'fullscreen' isn't migrated, we don't need to worry about
   * preserving gui_gtk2_migrated_from_2_5 either. */
  MIGRATE_OPTION(map_scrollbars);
  MIGRATE_OPTION(dialogs_on_top);
  MIGRATE_OPTION(show_task_icons);
  MIGRATE_OPTION(enable_tabs);
  MIGRATE_OPTION(show_chat_message_time);
  MIGRATE_OPTION(new_messages_go_to_top);
  MIGRATE_OPTION(show_message_window_buttons);
  MIGRATE_OPTION(metaserver_tab_first);
  MIGRATE_OPTION(allied_chat_only);
  MIGRATE_OPTION(mouse_over_map_focus);
  MIGRATE_OPTION(chatline_autocompletion);
  MIGRATE_OPTION(citydlg_xsize);
  MIGRATE_OPTION(citydlg_ysize);
  MIGRATE_OPTION(popup_tech_help);

  MIGRATE_STR_OPTION(font_city_label);
  MIGRATE_STR_OPTION(font_notify_label);
  MIGRATE_STR_OPTION(font_spaceship_label);
  MIGRATE_STR_OPTION(font_help_label);
  MIGRATE_STR_OPTION(font_help_link);
  MIGRATE_STR_OPTION(font_help_text);
  MIGRATE_STR_OPTION(font_chatline);
  MIGRATE_STR_OPTION(font_beta_label);
  MIGRATE_STR_OPTION(font_small);
  MIGRATE_STR_OPTION(font_comment_label);
  MIGRATE_STR_OPTION(font_city_names);
  MIGRATE_STR_OPTION(font_city_productions);
  MIGRATE_STR_OPTION(font_reqtree_text);

#undef MIGRATE_OPTION
#undef MIGRATE_STR_OPTION

  gui_options.gui_gtk3_migrated_from_gtk2 = TRUE;
}

/**********************************************************************//**
  Migrate gtk3.22 client specific options from gtk3 client options.
**************************************************************************/
static void migrate_options_from_gtk3(void)
{
  log_normal(_("Migrating options from gtk3 to gtk3.22 client"));

#define MIGRATE_OPTION(opt) gui_options.gui_gtk3_22_##opt = gui_options.gui_gtk3_##opt;
#define MIGRATE_STR_OPTION(opt) \
  strncpy(gui_options.gui_gtk3_22_##opt, gui_options.gui_gtk3_##opt,      \
          sizeof(gui_options.gui_gtk3_22_##opt));

  /* Default theme name is never migrated */

  /* Simulate gui-gtk3's migrate_options_from_2_5() */
  if (!gui_options.gui_gtk3_migrated_from_2_5) {
    log_normal(_("Migrating gtk3-client options from freeciv-2.5 options."));
    gui_options.gui_gtk3_fullscreen = gui_options.migrate_fullscreen;
    gui_options.gui_gtk3_migrated_from_2_5 = TRUE;
  }

  MIGRATE_OPTION(fullscreen);
  MIGRATE_OPTION(map_scrollbars);
  MIGRATE_OPTION(dialogs_on_top);
  MIGRATE_OPTION(show_task_icons);
  MIGRATE_OPTION(enable_tabs);
  MIGRATE_OPTION(show_chat_message_time);
  MIGRATE_OPTION(new_messages_go_to_top);
  MIGRATE_OPTION(show_message_window_buttons);
  MIGRATE_OPTION(metaserver_tab_first);
  MIGRATE_OPTION(allied_chat_only);
  MIGRATE_OPTION(message_chat_location);
  MIGRATE_OPTION(small_display_layout);
  MIGRATE_OPTION(mouse_over_map_focus);
  MIGRATE_OPTION(chatline_autocompletion);
  MIGRATE_OPTION(citydlg_xsize);
  MIGRATE_OPTION(citydlg_ysize);
  MIGRATE_OPTION(popup_tech_help);

  MIGRATE_STR_OPTION(font_city_label);
  MIGRATE_STR_OPTION(font_notify_label);
  MIGRATE_STR_OPTION(font_spaceship_label);
  MIGRATE_STR_OPTION(font_help_label);
  MIGRATE_STR_OPTION(font_help_link);
  MIGRATE_STR_OPTION(font_help_text);
  MIGRATE_STR_OPTION(font_chatline);
  MIGRATE_STR_OPTION(font_beta_label);
  MIGRATE_STR_OPTION(font_small);
  MIGRATE_STR_OPTION(font_comment_label);
  MIGRATE_STR_OPTION(font_city_names);
  MIGRATE_STR_OPTION(font_city_productions);
  MIGRATE_STR_OPTION(font_reqtree_text);

#undef MIGRATE_OPTION
#undef MIGRATE_STR_OPTION

  gui_options.gui_gtk3_22_migrated_from_gtk3 = TRUE;
}

/**********************************************************************//**
  Migrate gtk4 client specific options from gtk3.22 client options.
**************************************************************************/
static void migrate_options_from_gtk3_22(void)
{
  log_normal(_("Migrating options from gtk3.22 to gtk4 client"));

#define MIGRATE_OPTION(opt) GUI_GTK_OPTION(opt) = gui_options.gui_gtk3_22_##opt;
#define MIGRATE_STR_OPTION(opt) \
  strncpy(GUI_GTK_OPTION(opt), gui_options.gui_gtk3_22_##opt,      \
          sizeof(GUI_GTK_OPTION(opt)));

  /* Default theme name is never migrated */
  MIGRATE_OPTION(fullscreen);
  MIGRATE_OPTION(map_scrollbars);
  MIGRATE_OPTION(dialogs_on_top);
  MIGRATE_OPTION(show_task_icons);
  MIGRATE_OPTION(enable_tabs);
  MIGRATE_OPTION(show_chat_message_time);
  MIGRATE_OPTION(new_messages_go_to_top);
  MIGRATE_OPTION(show_message_window_buttons);
  MIGRATE_OPTION(metaserver_tab_first);
  MIGRATE_OPTION(allied_chat_only);
  MIGRATE_OPTION(message_chat_location);
  MIGRATE_OPTION(small_display_layout);
  MIGRATE_OPTION(mouse_over_map_focus);
  MIGRATE_OPTION(chatline_autocompletion);
  MIGRATE_OPTION(citydlg_xsize);
  MIGRATE_OPTION(citydlg_ysize);
  MIGRATE_OPTION(popup_tech_help);

  MIGRATE_STR_OPTION(font_city_label);
  MIGRATE_STR_OPTION(font_notify_label);
  MIGRATE_STR_OPTION(font_spaceship_label);
  MIGRATE_STR_OPTION(font_help_label);
  MIGRATE_STR_OPTION(font_help_link);
  MIGRATE_STR_OPTION(font_help_text);
  MIGRATE_STR_OPTION(font_chatline);
  MIGRATE_STR_OPTION(font_beta_label);
  MIGRATE_STR_OPTION(font_small);
  MIGRATE_STR_OPTION(font_comment_label);
  MIGRATE_STR_OPTION(font_city_names);
  MIGRATE_STR_OPTION(font_city_productions);
  MIGRATE_STR_OPTION(font_reqtree_text);

#undef MIGRATE_OPTION
#undef MIGRATE_STR_OPTION

  GUI_GTK_OPTION(migrated_from_gtk3_22) = TRUE;
}

/**********************************************************************//**
  Called from client_main(), is what it's named.
**************************************************************************/
void ui_main(int argc, char **argv)
{
  parse_options(argc, argv);

  /* the locale has already been set in init_nls() and the windows-specific
   * locale logic in gtk_init() causes problems with zh_CN (see PR#39475) */
  gtk_disable_setlocale();

  gtk_init();

  gui_up = TRUE;
  fc_app = gtk_application_new(NULL, 0);
  g_signal_connect(fc_app, "activate", G_CALLBACK(activate_gui), NULL);
  g_application_run(G_APPLICATION(fc_app), 0, NULL);
  gui_up = FALSE;

  /* We have extra ref for unit_info_box that has protected
   * it from getting destroyed when editinfobox_refresh()
   * moves widgets around. Free that extra ref here. */
  g_object_unref(unit_info_box);

  destroy_server_scans();
  free_mapcanvas_and_overview();
  spaceship_dialog_done();
  intel_dialog_done();
  citizens_dialog_done();
  luaconsole_dialog_done();
  happiness_dialog_done();
  diplomacy_dialog_done();
  cma_fe_done();
  free_unit_table();
  editgui_free();
  gtk_window_destroy(GTK_WINDOW(toplevel));
  message_buffer = NULL; /* Result of destruction of everything */
  tileset_free_tiles(tileset);
}

/**********************************************************************//**
  Run the gui
**************************************************************************/
static void activate_gui(GtkApplication *app, gpointer data)
{
  PangoFontDescription *toplevel_font_name;
  guint sig;

  toplevel = gtk_application_window_new(app);
  if (vmode.width > 0 && vmode.height > 0) {
    gtk_window_set_default_size(GTK_WINDOW(toplevel),
                                vmode.width, vmode.height);
  }

  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv");

  dlg_tab_provider_prepare();

  if (gui_options.first_boot) {
    adjust_default_options();
    /* We're using fresh defaults for this version of this client,
     * so prevent any future migrations from other clients / versions */
    GUI_GTK_OPTION(migrated_from_gtk3_22) = TRUE;
    /* Avoid also marking previous Gtk clients as migrated, so that
     * they can have their own run of their adjust_default_options() if
     * they are ever run (as a side effect of Gtk2->Gtk3 migration). */
  } else {
    if (!GUI_GTK_OPTION(migrated_from_gtk3_22)) {
      if (!gui_options.gui_gtk3_22_migrated_from_gtk3) {
        if (!gui_options.gui_gtk3_migrated_from_gtk2) {
          migrate_options_from_gtk2();
          /* We want a fresh look at screen-size-related options after Gtk2 */
          adjust_default_options();
          /* We don't ever want to consider pre-2.6 fullscreen option again
           * (even for gui-gtk3) */
          gui_options.gui_gtk3_migrated_from_2_5 = TRUE;
        }
        migrate_options_from_gtk3();
      }
      migrate_options_from_gtk3_22();
    }
  }

  if (GUI_GTK_OPTION(fullscreen)) {
    gtk_window_fullscreen(GTK_WINDOW(toplevel));
  }

  gtk_window_set_title(GTK_WINDOW(toplevel), _("Freeciv"));

  g_signal_connect(toplevel, "close-request",
                   G_CALLBACK(quit_dialog_callback), NULL);

  /* Disable GTK cursor key focus movement */
  sig = g_signal_lookup("focus", GTK_TYPE_WIDGET);
  g_signal_handlers_disconnect_matched(toplevel, G_SIGNAL_MATCH_ID, sig,
                                       0, 0, 0, 0);
  g_signal_connect(toplevel, "focus", G_CALLBACK(toplevel_focus), NULL);


  display_color_type = get_visual();

  options_iterate(client_optset, poption) {
    if (OT_FONT == option_type(poption)) {
      /* Force to call the appropriated callback. */
      option_changed(poption);
    }
  } options_iterate_end;

  toplevel_font_name = pango_context_get_font_description(
                           gtk_widget_get_pango_context(toplevel));

  if (NULL == city_names_style) {
    city_names_style = pango_font_description_copy(toplevel_font_name);
    log_error("city_names_style should have been set by options.");
  }
  if (NULL == city_productions_style) {
    city_productions_style = pango_font_description_copy(toplevel_font_name);
    log_error("city_productions_style should have been set by options.");
  }
  if (NULL == reqtree_text_style) {
    reqtree_text_style = pango_font_description_copy(toplevel_font_name);
    log_error("reqtree_text_style should have been set by options.");
  }

  tileset_init(tileset);
  tileset_load_tiles(tileset);

  /* keep the icon of the executable on Windows (see PR#36491) */
#ifndef FREECIV_MSWINDOWS
  {
    /* Only call this after tileset_load_tiles is called. */
    gtk_window_set_icon_name(GTK_WINDOW(toplevel), "freeciv");
  }
#endif /* FREECIV_MSWINDOWS */

  setup_widgets();
  load_cursors();
  cma_fe_init();
  diplomacy_dialog_init();
  luaconsole_dialog_init();
  happiness_dialog_init();
  citizens_dialog_init();
  intel_dialog_init();
  spaceship_dialog_init();
  chatline_init();
  init_mapcanvas_and_overview();

  tileset_use_preferred_theme(tileset);

  gtk_widget_show(toplevel);

  /* assumes toplevel showing */
  set_client_state(C_S_DISCONNECTED);

  /* assumes client_state is set */
  timer_id = g_timeout_add(TIMER_INTERVAL, timer_callback, NULL);
}

/**********************************************************************//**
  Return whether gui is currently running.
**************************************************************************/
bool is_gui_up(void)
{
  return gui_up;
}

/**********************************************************************//**
  Do any necessary UI-specific cleanup
**************************************************************************/
void ui_exit(void)
{
  if (message_buffer != NULL) {
    g_object_unref(message_buffer);
    message_buffer = NULL;
  }
}

/**********************************************************************//**
  Return our GUI type
**************************************************************************/
enum gui_type get_gui_type(void)
{
  return GUI_GTK4;
}

/**********************************************************************//**
  Obvious...
**************************************************************************/
void sound_bell(void)
{
  gdk_display_beep(gdk_display_get_default());
}

/**********************************************************************//**
  Set one of the unit icons in information area based on punit.
  Use punit == NULL to clear icon.
  Index 'idx' is -1 for "active unit", or 0 to (num_units_below - 1) for
  units below.  Also updates unit_ids[idx] for idx >= 0.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  GtkWidget *w;

  fc_assert_ret(idx >= -1 && idx < num_units_below);

  if (idx == -1) {
    w = unit_image;
    unit_id_top = punit ? punit->id : 0;
  } else {
    w = unit_below_image[idx];
    unit_ids[idx] = punit ? punit->id : 0;
  }

  if (!w) {
    return;
  }

  if (punit) {
    put_unit_image(punit, GTK_IMAGE(w), -1);
  } else {
    gtk_image_clear(GTK_IMAGE(w));
  }
}

/**********************************************************************//**
  Set the "more arrow" for the unit icons to on(1) or off(0).
  Maintains a static record of current state to avoid unnecessary redraws.
  Note initial state should match initial gui setup (off).
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
  static bool showing = FALSE;

  if (!more_arrow_pixmap) {
    return;
  }

  if (onoff && !showing) {
    gtk_widget_show(more_arrow_pixmap);
    showing = TRUE;
  } else if (!onoff && showing) {
    gtk_widget_hide(more_arrow_pixmap);
    showing = FALSE;
  }
}

/**********************************************************************//**
  Called when the set of units in focus (get_units_in_focus()) changes.
  Standard updates like update_unit_info_label() are handled in the platform-
  independent code; we use this to keep the goto/airlift dialog up to date,
  if it's visible.
**************************************************************************/
void real_focus_units_changed(void)
{
  goto_dialog_focus_units_changed();
}

/**********************************************************************//**
 callback for clicking a unit icon underneath unit info box.
 these are the units on the same tile as the focus unit.
**************************************************************************/
static gboolean select_unit_image_callback(GtkWidget *w, GdkEvent *ev,
                                           gpointer data)
{
  int i = GPOINTER_TO_INT(data);
  struct unit *punit;

  if (i == -1) {
    punit = game_unit_by_number(unit_id_top);
    if (punit && unit_is_in_focus(punit)) {
      /* Clicking on the currently selected unit will center it. */
      center_tile_mapcanvas(unit_tile(punit));
    }
    return TRUE;
  }

  if (unit_ids[i] == 0) /* no unit displayed at this place */
    return TRUE;

  punit = game_unit_by_number(unit_ids[i]);
  if (NULL != punit && unit_owner(punit) == client.conn.playing) {
    /* Unit shouldn't be NULL but may be owned by an ally. */
    unit_focus_set(punit);
  }

  return TRUE;
}

/**********************************************************************//**
  Callback for clicking a unit icon underneath unit info box.
  these are the units on the same tile as the focus unit.
**************************************************************************/
static gboolean select_more_arrow_pixmap_callback(GtkWidget *w, GdkEvent *ev,
                                                  gpointer data)
{
  struct unit *punit = game_unit_by_number(unit_id_top);

  if (punit) {
    unit_select_dialog_popup(unit_tile(punit));
  }

  return TRUE;
}

/**********************************************************************//**
  Popup info box
**************************************************************************/
static gboolean show_info_popup(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  guint button;

  button = gdk_button_event_get_button(ev);
  if (button == 1) {
    GtkWidget *p;
    GtkWidget *child;

    p = gtk_window_new();
    gtk_widget_set_margin_start(p, 4);
    gtk_widget_set_margin_end(p, 4);
    gtk_widget_set_margin_top(p, 4);
    gtk_widget_set_margin_bottom(p, 4);
    gtk_window_set_transient_for(GTK_WINDOW(p), GTK_WINDOW(toplevel));

    child = gtk_label_new(get_info_label_text_popup());
    gtk_window_set_child(GTK_WINDOW(p), child);
    gtk_widget_show(p);
  }

  return TRUE;
}

/**********************************************************************//**
  User clicked "Turn Done" button
**************************************************************************/
static void end_turn_callback(GtkWidget *w, gpointer data)
{
    gtk_widget_set_sensitive(turn_done_button, FALSE);
    user_ended_turn();
}

/**********************************************************************//**
  Read input from server socket
**************************************************************************/
static gboolean get_net_input(GIOChannel *source, GIOCondition condition,
                              gpointer data)
{
  input_from_server(g_io_channel_unix_get_fd(source));

  return TRUE;
}

/**********************************************************************//**
  Set socket writability state
**************************************************************************/
static void set_wait_for_writable_socket(struct connection *pc,
					 bool socket_writable)
{
  static bool previous_state = FALSE;

  fc_assert_ret(pc == &client.conn);

  if (previous_state == socket_writable)
    return;

  log_debug("set_wait_for_writable_socket(%d)", socket_writable);

  g_source_remove(srv_id);
  srv_id = g_io_add_watch(srv_channel,
                          G_IO_IN | (socket_writable ? G_IO_OUT : 0) | G_IO_ERR,
                          get_net_input,
                          NULL);

  previous_state = socket_writable;
}

/**********************************************************************//**
  This function is called after the client succesfully
  has connected to the server
**************************************************************************/
void add_net_input(int sock)
{
#ifdef FREECIV_MSWINDOWS
  srv_channel = g_io_channel_win32_new_socket(sock);
#else
  srv_channel = g_io_channel_unix_new(sock);
#endif
  srv_id = g_io_add_watch(srv_channel,
                          G_IO_IN | G_IO_ERR,
                          get_net_input,
                          NULL);
  client.conn.notify_of_writable_data = set_wait_for_writable_socket;
}

/**********************************************************************//**
  This function is called if the client disconnects
  from the server
**************************************************************************/
void remove_net_input(void)
{
  g_source_remove(srv_id);
  g_io_channel_unref(srv_channel);
  gtk_widget_set_cursor(toplevel, NULL);
}

/**********************************************************************//**
  This is the response callback for the dialog with the message:
  Are you sure you want to quit?
**************************************************************************/
static void quit_dialog_response(GtkWidget *dialog, gint response)
{
  gtk_window_destroy(GTK_WINDOW(dialog));
  if (response == GTK_RESPONSE_YES) {
    start_quitting();
    if (client.conn.used) {
      disconnect_from_server();
    }
    quit_gtk_main();
  }
}

/**********************************************************************//**
  Exit gtk main loop.
**************************************************************************/
void quit_gtk_main(void)
{
  /* Quit gtk main loop. After this it will return to finish
   * ui_main() */

  g_application_quit(G_APPLICATION(fc_app));
}

/**********************************************************************//**
  Popups the dialog with the message:
  Are you sure you want to quit?
**************************************************************************/
void popup_quit_dialog(void)
{
  static GtkWidget *dialog;

  if (!dialog) {
    dialog = gtk_message_dialog_new(NULL,
	0,
	GTK_MESSAGE_WARNING,
	GTK_BUTTONS_YES_NO,
	_("Are you sure you want to quit?"));
    setup_dialog(dialog, toplevel);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(quit_dialog_response), NULL);
    g_signal_connect(dialog, "destroy",
                     G_CALLBACK(widget_destroyed), &dialog);
  }

  gtk_window_present(GTK_WINDOW(dialog));
}

/**********************************************************************//**
  Popups the quit dialog.
**************************************************************************/
static gboolean quit_dialog_callback(void)
{
  popup_quit_dialog();
  /* Stop emission of event. */
  return TRUE;
}

struct callback {
  void (*callback)(void *data);
  void *data;
};

/**********************************************************************//**
  A wrapper for the callback called through add_idle_callback.
**************************************************************************/
static gboolean idle_callback_wrapper(gpointer data)
{
  struct callback *cb = data;

  (cb->callback)(cb->data);
  free(cb);

  return FALSE;
}

/**********************************************************************//**
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
**************************************************************************/
void add_idle_callback(void (callback)(void *), void *data)
{
  struct callback *cb = fc_malloc(sizeof(*cb));

  cb->callback = callback;
  cb->data = data;
  g_idle_add(idle_callback_wrapper, cb);
}

/**********************************************************************//**
  Option callback for the 'allied_chat_only' gtk-gui option.
  This updates the state of the associated toggle button.
**************************************************************************/
static void allied_chat_only_callback(struct option *poption)
{
  GtkWidget *button;

  button = allied_chat_toggle_button;
  fc_assert_ret(button != NULL);
  fc_assert_ret(GTK_IS_TOGGLE_BUTTON(button));

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                               option_bool_get(poption));
}

/**********************************************************************//**
  Change the city names font.
**************************************************************************/
static void apply_city_names_font(struct option *poption)
{
  gui_update_font_full(option_font_target(poption),
                       option_font_get(poption),
                       &city_names_style);
  update_city_descriptions();
}

/**********************************************************************//**
  Change the city productions font.
**************************************************************************/
static void apply_city_productions_font(struct option *poption)
{
  gui_update_font_full(option_font_target(poption),
                       option_font_get(poption),
                       &city_productions_style);
  update_city_descriptions();
}

/**********************************************************************//**
  Change the city productions font.
**************************************************************************/
static void apply_reqtree_text_font(struct option *poption)
{
  gui_update_font_full(option_font_target(poption),
                       option_font_get(poption),
                       &reqtree_text_style);
  science_report_dialog_redraw();
}

/**********************************************************************//**
  Extra initializers for client options. Here we make set the callback
  for the specific gui-gtk-3.22 options.
**************************************************************************/
void options_extra_init(void)
{

  struct option *poption;

#define option_var_set_callback(var, callback)                              \
  if ((poption = optset_option_by_name(client_optset, GUI_GTK_OPTION_STR(var)))) { \
    option_set_changed_callback(poption, callback);                         \
  } else {                                                                  \
    log_error("Didn't find option %s!", GUI_GTK_OPTION_STR(var));      \
  }

  option_var_set_callback(allied_chat_only,
                          allied_chat_only_callback);

  option_var_set_callback(font_city_names,
                          apply_city_names_font);
  option_var_set_callback(font_city_productions,
                          apply_city_productions_font);
  option_var_set_callback(font_reqtree_text,
                          apply_reqtree_text_font);
#undef option_var_set_callback
}

/**********************************************************************//**
  Set the chatline buttons to reflect the state of the game and current
  client options. This function should be called on game start.
**************************************************************************/
void refresh_chat_buttons(void)
{
  GtkWidget *button;

  button = allied_chat_toggle_button;
  fc_assert_ret(button != NULL);
  fc_assert_ret(GTK_IS_TOGGLE_BUTTON(button));

  /* Hide the "Allies Only" button for local games. */
  if (is_server_running()) {
    gtk_widget_hide(button);
  } else {
    gtk_widget_show(button);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                                 GUI_GTK_OPTION(allied_chat_only));
  }
}

/**********************************************************************//**
  Handle a toggle of the "Allies Only" chat button.
**************************************************************************/
static void allied_chat_button_toggled(GtkToggleButton *button,
                                       gpointer user_data)
{
  GUI_GTK_OPTION(allied_chat_only) = gtk_toggle_button_get_active(button);
}

/**********************************************************************//**
  Insert build information to help
**************************************************************************/
void insert_client_build_info(char *outbuf, size_t outlen)
{
  cat_snprintf(outbuf, outlen, _("\nBuilt against gtk %d.%d.%d, using %d.%d.%d"
                                 "\nBuilt against glib %d.%d.%d, using %d.%d.%d"),
               GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION,
               gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
               GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
               glib_major_version, glib_minor_version, glib_micro_version);
}

/**********************************************************************//**
  Return dimensions of primary monitor, if any
  (in 'application pixels')
**************************************************************************/
static bool monitor_size(GdkRectangle *rect_p)
{
  GdkDisplay *display;
  GdkMonitor *monitor;

  display = gdk_display_get_default();
  if (!display) {
    return FALSE;
  }

  monitor = g_list_model_get_item(gdk_display_get_monitors(display), 0);

  if (!monitor) {
    return FALSE;
  }

  gdk_monitor_get_geometry(monitor, rect_p);

  return TRUE;
}

/**********************************************************************//**
  Return width of the primary monitor
**************************************************************************/
int screen_width(void)
{
  GdkRectangle rect;

  if (vmode.width > 0) {
    return vmode.width;
  }

  if (monitor_size(&rect)) {
    return rect.width;
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Return height of the primary monitor
**************************************************************************/
int screen_height(void)
{
  GdkRectangle rect;

  if (vmode.height > 0) {
    return vmode.height;
  }

  if (monitor_size(&rect)) {
    return rect.height;
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Give resolution requested by user, if any.
**************************************************************************/
struct video_mode *resolution_request_get(void)
{
  if (vmode.width > 0 && vmode.height > 0) {
    return &vmode;
  }

  return NULL;
}

/**********************************************************************//**
  Make dynamic adjustments to first-launch default options.
**************************************************************************/
static void adjust_default_options(void)
{
  int scr_height = screen_height();

  if (scr_height > 0) {
    /* Adjust these options only if we do know the screen height. */

    if (scr_height <= 480) {
      /* Freeciv is practically unusable outside fullscreen mode in so
       * small display */
      log_verbose("Changing default to fullscreen due to very small screen");
      GUI_GTK_OPTION(fullscreen) = TRUE;
    }
    if (scr_height < 1024) {
      /* This is a small display */
      log_verbose("Defaulting to small widget layout due to small screen");
      GUI_GTK_OPTION(small_display_layout) = TRUE;
      log_verbose("Defaulting to merged messages/chat due to small screen");
      GUI_GTK_OPTION(message_chat_location) = GUI_GTK_MSGCHAT_MERGED;
    }
  }
}
