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
#include "audio.h"
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "colors.h"
#include "connectdlg_common.h"
#include "control.h"
#include "editor.h"
#include "gui_properties.h"
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
#include "helpdlg.h"
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

const char *client_string = GUI_NAME_FULL;

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

static GdkPaintable *empty_unit_paintable = NULL;
static GtkWidget *unit_pic_table;
static GtkWidget *unit_pic;
static GtkWidget *unit_below_pic[MAX_NUM_UNITS_BELOW];
static GtkWidget *more_arrow;
static GtkWidget *more_arrow_container;

static int unit_id_top;
static int unit_ids[MAX_NUM_UNITS_BELOW];  /* ids of the units icons in
                                            * information display: (or 0) */
GtkTextView *main_message_area;
GtkTextBuffer *message_buffer = NULL;
static GtkWidget *allied_chat_toggle_button;

static gint timer_id;                               /*       Ditto        */
static GIOChannel *srv_channel;
static guint srv_id;
gint cur_x, cur_y;

static bool gui_up = FALSE;

static bool audio_paused = FALSE;
static bool client_focus = TRUE;

static GtkApplication *fc_app;

static struct video_mode vmode = { -1, -1 };

static void set_g_log_callbacks(void);

static gboolean show_info_popup(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer data);

static void end_turn_callback(GtkWidget *w, gpointer data);
static gboolean get_net_input(GIOChannel *source, GIOCondition condition,
                              gpointer data);
static void set_wait_for_writable_socket(struct connection *pc,
                                         bool socket_writable);

static void print_usage(void);
static void activate_gui(GtkApplication *app, gpointer data);
static bool parse_options(int argc, char **argv);
static gboolean toplevel_key_press_handler(GtkEventControllerKey *controller,
                                           guint keyval,
                                           guint keycode,
                                           GdkModifierType state,
                                           gpointer data);
static gboolean mouse_scroll_mapcanvas(GtkEventControllerScroll *controller,
                                       gdouble dx, gdouble dy,
                                       gpointer data);

static void tearoff_callback(GtkWidget *b, gpointer data);
static GtkWidget *detached_widget_new(void);
static GtkWidget *detached_widget_fill(GtkWidget *tearbox);

static gboolean select_unit_pic_callback(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer data);
static gboolean select_more_arrow_callback(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer data);
static gboolean quit_dialog_callback(void);

static void allied_chat_button_toggled(GtkToggleButton *button,
                                       gpointer user_data);

static void free_unit_table(void);

static void adjust_default_options(void);

static float zoom_steps_custom[] = {
  -1.0, 0.13, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, -1.0
};

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

  if (gui_options.silent_when_not_in_focus) {
    if (!audio_paused && !client_focus) {
      audio_pause();
      audio_paused = TRUE;
    } else if (audio_paused && client_focus) {
      audio_resume();
      audio_paused = FALSE;
    }
  }

  timer_id = g_timeout_add(seconds * 1000, timer_callback, NULL);

  return FALSE;
}

/**********************************************************************//**
  Print extra usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(void)
{
  /* Add client-specific usage information here */
  fc_fprintf(stderr,
             _("gtk4-client gui-specific options are:\n"));

  fc_fprintf(stderr,
             _("-r, --resolution WIDTHxHEIGHT\tAssume given resolution "
               "screen\n"));
  fc_fprintf(stderr,
             /* TRANS: Keep word 'default' untranslated */
             _("-z, --zoom LEVEL\tSet zoom level; use value 'default' "
               "to reset\n\n"));

  /* TRANS: No full stop after the URL, could cause confusion. */
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

/**********************************************************************//**
  Search for gui-specific command line options.
**************************************************************************/
static bool parse_options(int argc, char **argv)
{
  int i = 1;

  while (i < argc) {
    char *option = NULL;

    if (is_option("--help", argv[i])) {
      print_usage();

      return FALSE;
    } else if ((option = get_option_malloc("--zoom", argv, &i, argc, FALSE))) {
      char *endptr;

      if (strcmp("default", option)) {
        gui_options.zoom_set = TRUE;
        gui_options.zoom_default_level = strtof(option, &endptr);
      } else {
        gui_options.zoom_set = FALSE;
      }
      free(option);
    } else if ((option = get_option_malloc("--resolution", argv, &i, argc, FALSE))) {
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

  return TRUE;
}

/**********************************************************************//**
  Focus on widget. Returns whether focus was really changed.
**************************************************************************/
static void toplevel_focus(GtkWidget *w, GtkDirectionType arg,
                           gpointer data)
{
  switch (arg) {
    case GTK_DIR_TAB_FORWARD:
    case GTK_DIR_TAB_BACKWARD:

      if (!gtk_widget_get_can_focus(w)) {
        return;
      }

      if (!gtk_widget_is_focus(w)) {
        gtk_widget_grab_focus(w);
        return;
      }
      break;

    default:
      break;
  }
}

/**********************************************************************//**
  When the chatline text view is resized, scroll it to the bottom. This
  prevents users from accidentally missing messages when the chatline
  gets scrolled up a small amount and stops scrolling down automatically.
**************************************************************************/
void main_message_area_resize(void *data)
{
  if (get_current_client_page() == PAGE_GAME) {
    static int old_width = 0, old_height = 0;
    int width = gtk_widget_get_width(GTK_WIDGET(main_message_area));
    int height = gtk_widget_get_height(GTK_WIDGET(main_message_area));

    if (width != old_width
        || height != old_height) {
      chatline_scroll_to_bottom(TRUE);
      old_width = width;
      old_height = height;
    }

    add_idle_callback(main_message_area_resize, NULL);
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
static gboolean key_press_map_canvas(guint keyval, GdkModifierType state)
{
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

  if (state & GDK_SHIFT_MASK) {
    bool volchange = FALSE;

    switch (keyval) {
    case GDK_KEY_plus:
    case GDK_KEY_KP_Add:
      gui_options.sound_effects_volume += 10;
      volchange = TRUE;
      break;

    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
      gui_options.sound_effects_volume -= 10;
      volchange = TRUE;
      break;

    default:
      break;
    }

    if (volchange) {
      struct option *poption = optset_option_by_name(client_optset,
                                                     "sound_effects_volume");

      gui_options.sound_effects_volume = CLIP(0,
                                              gui_options.sound_effects_volume,
                                              100);
      option_changed(poption);

      return TRUE;
    }
  } else if (!(state & GDK_CONTROL_MASK)) {
    switch (keyval) {
    case GDK_KEY_plus:
    case GDK_KEY_KP_Add:
      zoom_step_up();
      return TRUE;

    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
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
static gboolean toplevel_key_press_handler(GtkEventControllerKey *controller,
                                           guint keyval,
                                           guint keycode,
                                           GdkModifierType state,
                                           gpointer data)
{
  if (inputline_has_focus()) {
    return FALSE;
  }

  if (keyval == GDK_KEY_apostrophe) {
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
    }
  }

  if (!gtk_widget_get_mapped(top_vbox)
      || !can_client_change_view()) {
    return FALSE;
  }

  if (editor_is_active()) {
    if (handle_edit_key_press(keyval, state)) {
      return TRUE;
    }
  }

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
    return key_press_map_canvas(keyval, state);
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

  set_mapview_scroll_pos(scroll_x, scroll_y, mouse_zoom);

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

  control_mouse_cursor(canvas_pos_to_tile(cur_x, cur_y, mouse_zoom));

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
  Freeciv window has lost focus
**************************************************************************/
gboolean fc_lost_focus(GtkEventControllerFocus *controller,
                       gpointer data)
{
  client_focus = FALSE;

  return FALSE;
}

/**********************************************************************//**
  Freeciv window has gained focus
**************************************************************************/
gboolean fc_gained_focus(GtkEventControllerFocus *controller,
                         gpointer data)
{
  client_focus = TRUE;

  return FALSE;
}

/**********************************************************************//**
  Reattach tearoff to its normal parent and destroy the detach window.
**************************************************************************/
static void tearoff_reattach(GtkWidget *box)
{
  GtkWidget *aparent, *cparent;

  aparent = g_object_get_data(G_OBJECT(box), "aparent");
  cparent = gtk_widget_get_parent(box);

  if (aparent != cparent) {
    move_from_container_to_container(box, cparent, aparent);

    gtk_window_destroy(GTK_WINDOW(cparent));
  }
}

/**********************************************************************//**
  User requested close of the detach window.
**************************************************************************/
static void tearoff_close(GtkWidget *w, gpointer data)
{
  tearoff_reattach(GTK_WIDGET(data));
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
    g_signal_connect(w, "close-request", G_CALLBACK(tearoff_close), box);

    g_object_set_data(G_OBJECT(box), "aparent", gtk_widget_get_parent(box));

    temp_hide = g_object_get_data(G_OBJECT(box), "hide-over-reparent");
    if (temp_hide != nullptr) {
      gtk_widget_set_visible(temp_hide, FALSE);
    }

    move_from_container_to_container(box, old_parent, w);

    gtk_widget_set_visible(w, TRUE);

    if (temp_hide != nullptr) {
      gtk_widget_set_visible(temp_hide, TRUE);
    }
  } else {
    tearoff_reattach(box);
  }
}

/**********************************************************************//**
  Create the container for the widget that's able to be detached.
**************************************************************************/
static GtkWidget *detached_widget_new(void)
{
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

  return hbox;
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
  gtk_style_context_add_provider_for_display(
                                 gtk_widget_get_display(toplevel),
                                 GTK_STYLE_PROVIDER(detach_button_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_widget_add_css_class(b, "detach_button");
  gtk_widget_set_tooltip_text(b, _("Detach/Attach the pane."));

  gtk_box_append(GTK_BOX(tearbox), b);
  g_signal_connect(b, "toggled", G_CALLBACK(tearoff_callback), tearbox);

  fillbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_box_append(GTK_BOX(tearbox), fillbox);

  return fillbox;
}

/**********************************************************************//**
  Called to build the unit_below pixmap table. This is the table on the
  left of the screen that shows all of the inactive units in the current
  tile.

  It may be called again if the tileset changes.
**************************************************************************/
static void populate_unit_pic_table(void)
{
  int i, width;
  GtkWidget *table = unit_pic_table;
  GdkPixbuf *pix;
  int ttw;
  GtkEventController *controller;

  /* Get width of the overview window */
  width = (overview_canvas_store_width > GUI_GTK_OVERVIEW_MIN_XSIZE)
    ? overview_canvas_store_width
    : GUI_GTK_OVERVIEW_MIN_XSIZE;

  ttw = tileset_tile_width(tileset);

  if (GUI_GTK_OPTION(small_display_layout)) {
    /* We want arrow to appear if there is other units in addition
       to active one in tile. Active unit is not counted, so there
       can be 0 other units to not to display arrow. */
    num_units_below = 1 - 1;
  } else {
    num_units_below = width / ttw;
    num_units_below = CLIP(1, num_units_below, MAX_NUM_UNITS_BELOW);
  }

  /* Top row: the active unit. */
  /* Note, we ref this and other widgets here so that we can unref them
   * in reset_unit_table. */
  unit_pic = gtk_picture_new();
  g_object_ref(unit_pic);
  gtk_widget_set_size_request(unit_pic, ttw, -1);
  gtk_grid_attach(GTK_GRID(table), unit_pic, 0, 0, 1, 1);

  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(select_unit_pic_callback),
                   GINT_TO_POINTER(-1));
  gtk_widget_add_controller(unit_pic, controller);

  if (!GUI_GTK_OPTION(small_display_layout)) {
    /* Bottom row: other units in the same tile. */
    for (i = 0; i < num_units_below; i++) {
      unit_below_pic[i] = gtk_picture_new();
      g_object_ref(unit_below_pic[i]);
      gtk_widget_set_size_request(unit_below_pic[i], ttw, -1);

      controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
      g_signal_connect(controller, "pressed",
                       G_CALLBACK(select_unit_pic_callback),
                       GINT_TO_POINTER(i));
      gtk_widget_add_controller(unit_below_pic[i], controller);

      gtk_grid_attach(GTK_GRID(table), unit_below_pic[i],
                      i, 1, 1, 1);
    }
  }

  /* Create arrow (popup for all units on the selected tile) */
  pix = sprite_get_pixbuf(get_arrow_sprite(tileset, ARROW_RIGHT));
  more_arrow = gtk_image_new_from_pixbuf(pix);
  g_object_ref(more_arrow);

  controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(controller, "pressed",
                   G_CALLBACK(select_more_arrow_callback),
                   NULL);
  gtk_widget_add_controller(more_arrow, controller);

  /* An extra layer so that we can hide the clickable button but keep
   * an explicit size request to avoid the layout jumping around */
  more_arrow_container = gtk_frame_new(NULL);
  gtk_widget_set_halign(more_arrow_container, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(more_arrow_container, GTK_ALIGN_CENTER);
  g_object_ref(more_arrow_container);
  gtk_frame_set_child(GTK_FRAME(more_arrow_container), more_arrow);
  gtk_widget_set_size_request(more_arrow_container,
                              gdk_pixbuf_get_width(pix), -1);
  g_object_unref(G_OBJECT(pix));

  if (!GUI_GTK_OPTION(small_display_layout)) {
    /* Display on bottom row. */
    gtk_grid_attach(GTK_GRID(table), more_arrow_container,
                    num_units_below, 1, 1, 1);
  } else {
    /* Display on top row (there is no bottom row). */
    gtk_grid_attach(GTK_GRID(table), more_arrow_container,
                    1, 0, 1, 1);
  }

  gtk_widget_set_visible(table, TRUE);
}

/**********************************************************************//**
  Free unit image table.
**************************************************************************/
static void free_unit_table(void)
{
  if (unit_pic != NULL) {
    gtk_grid_remove(GTK_GRID(unit_pic_table), unit_pic);
    g_object_unref(unit_pic);
    if (!GUI_GTK_OPTION(small_display_layout)) {
      int i;

      for (i = 0; i < num_units_below; i++) {
        gtk_grid_remove(GTK_GRID(unit_pic_table),
                        unit_below_pic[i]);
        g_object_unref(unit_below_pic[i]);
      }
    }
    gtk_grid_remove(GTK_GRID(unit_pic_table),
                    more_arrow_container);
    g_object_unref(more_arrow);
    g_object_unref(more_arrow_container);
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
   * populate_unit_pic_table(). */
  free_unit_table();

  populate_unit_pic_table();

  /* We have to force a redraw of the units. And we explicitly have
   * to force a redraw of the focus unit, which is normally only
   * redrawn when the focus changes. We also have to force the 'more'
   * arrow to go away, both by expicitly hiding it and telling it to
   * do so (this will be reset immediately afterwards if necessary,
   * but we have to make the *internal* state consistent). */
  gtk_widget_set_visible(more_arrow, FALSE);
  set_unit_icons_more_arrow(FALSE);
  if (get_num_units_in_focus() == 1) {
    set_unit_icon(-1, head_of_units_in_focus());
  } else {
    set_unit_icon(-1, NULL);
  }
  update_unit_pix_label(get_units_in_focus());
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
gboolean terminate_signal_processing(GtkEventControllerFocus *controller,
                                     gpointer data)
{
  return TRUE;
}

/**********************************************************************//**
  Update tooltip of the Turn Done button
**************************************************************************/
void update_turn_done_tooltip(void)
{
  struct option *opt = optset_option_by_name(server_optset, "fixedlength");

  if (opt != NULL && option_bool_get(opt)) {
    gtk_widget_set_tooltip_text(turn_done_button,
                                _("Fixed length turns"));
  } else {
    char buf[256];

    fc_snprintf(buf, sizeof(buf), "%s:\n%s",
                _("Turn Done"), _("Shift+Return"));
    gtk_widget_set_tooltip_text(turn_done_button, buf);
  }
}

/**********************************************************************//**
  Do the heavy lifting for the widget setup.
**************************************************************************/
static void setup_widgets(void)
{
  GtkWidget *page, *hgrid, *hgrid2, *label;
  GtkWidget *frame, *table, *table2, *paned, *sw, *text;
  GtkWidget *button, *view, *mainbox, *vbox, *right_vbox = NULL;
  int i;
  GtkWidget *notebook, *statusbar;
  GtkWidget *dtach_lowbox = NULL;
  struct sprite *spr;
  int right_row = 0;
  int top_row = 0;
  int grid_col = 0;
  int grid2_col = 0;
  GtkGesture *mc_gesture;
  GtkEventController *mc_controller;
  GtkWidget *ebar;

  message_buffer = gtk_text_buffer_new(NULL);

  notebook = gtk_notebook_new();

  toplevel_tabs = notebook;
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_window_set_child(GTK_WINDOW(toplevel), mainbox);
  gtk_box_append(GTK_BOX(mainbox), notebook);
  statusbar = create_statusbar();
  gtk_box_append(GTK_BOX(mainbox), statusbar);

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
  gtk_widget_set_margin_bottom(ingame_votebar, 2);
  gtk_widget_set_margin_end(ingame_votebar, 2);
  gtk_widget_set_margin_start(ingame_votebar, 2);
  gtk_widget_set_margin_top(ingame_votebar, 2);

  /* *** Everything in the top *** */

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

  /* This holds the overview canvas, production info, etc. */
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  /* Put vbox to the left of anything else in hgrid -- right_vbox is either
   * the chat/messages pane, or NULL which is OK */
  gtk_grid_attach_next_to(GTK_GRID(hgrid), vbox, right_vbox,
                          GTK_POS_LEFT, 1, 1);
  grid_col++;

  /* Overview canvas */
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

  mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_frame_set_child(GTK_FRAME(frame), mainbox);
  gtk_widget_set_hexpand(mainbox, TRUE);

  label = gtk_label_new(NULL);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(label, 2);
  gtk_widget_set_margin_end(label, 2);
  gtk_widget_set_margin_top(label, 2);
  gtk_widget_set_margin_bottom(label, 2);

  mc_controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
  g_signal_connect(mc_controller, "pressed",
                   G_CALLBACK(show_info_popup), frame);
  gtk_widget_add_controller(label, mc_controller);
  gtk_box_append(GTK_BOX(mainbox), label);
  main_label_info = label;

  /* Production status */
  table = gtk_grid_new();
  gtk_widget_set_halign(table, GTK_ALIGN_CENTER);
  gtk_grid_set_column_homogeneous(GTK_GRID(table), TRUE);
  gtk_box_append(GTK_BOX(avbox), table);

  /* Citizens for taxrates */
  table2 = gtk_grid_new();
  gtk_grid_attach(GTK_GRID(table), table2, 0, 0, 10, 1);
  econ_widget = table2;

  for (i = 0; i < 10; i++) {
    GtkEventController *controller;

    spr = i < 5 ? get_tax_sprite(tileset, O_SCIENCE) : get_tax_sprite(tileset, O_GOLD);
    econ_label[i] = picture_new_from_surface(spr->surface);

    g_object_set_data(G_OBJECT(econ_label[i]), "rate_button", GUINT_TO_POINTER(i));
    controller = GTK_EVENT_CONTROLLER(gtk_gesture_click_new());
    g_signal_connect(controller, "pressed",
                     G_CALLBACK(taxrates_callback), NULL);
    gtk_widget_add_controller(econ_label[i], controller);
    mc_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mc_gesture), 3);
    controller = GTK_EVENT_CONTROLLER(mc_gesture);
    g_signal_connect(controller, "pressed",
		     G_CALLBACK(reverse_taxrates_callback), NULL);
    gtk_widget_add_controller(econ_label[i], controller);
    gtk_grid_attach(GTK_GRID(table2), econ_label[i], i, 0, 1, 1);
  }

  /* Science, environmental, govt, timeout */
  spr = client_research_sprite();
  if (spr != NULL) {
    bulb_label = picture_new_from_surface(spr->surface);
  } else {
    bulb_label = gtk_picture_new();
  }

  spr = client_warming_sprite();
  if (spr != NULL) {
    sun_label = picture_new_from_surface(spr->surface);
  } else {
    sun_label = gtk_picture_new();
  }

  spr = client_cooling_sprite();
  if (spr != NULL) {
    flake_label = picture_new_from_surface(spr->surface);
  } else {
    flake_label = gtk_picture_new();
  }

  spr = client_government_sprite();
  if (spr != NULL) {
    government_label = picture_new_from_surface(spr->surface);
  } else {
    government_label = gtk_picture_new();
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


  /* Turn done */
  turn_done_button = gtk_button_new_with_label(_("Turn Done"));

  gtk_grid_attach(GTK_GRID(table), turn_done_button, 0, 2, 10, 1);

  g_signal_connect(turn_done_button, "clicked",
                   G_CALLBACK(end_turn_callback), NULL);
  update_turn_done_tooltip();

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
  gtk_widget_set_margin_bottom(table, 5);
  gtk_widget_set_margin_end(table, 5);
  gtk_widget_set_margin_start(table, 5);
  gtk_widget_set_margin_top(table, 5);
  gtk_grid_attach(GTK_GRID(hgrid2), table, grid2_col++, 0, 1, 1);

  gtk_grid_set_row_spacing(GTK_GRID(table), 2);
  gtk_grid_set_column_spacing(GTK_GRID(table), 2);

  unit_pic_table = table;

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

  mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(mainbox), map_widget);

  gtk_box_append(GTK_BOX(mainbox), editgui_get_editbar()->widget);
  ebar = editgui_get_editbar()->widget;
  gtk_widget_set_margin_bottom(ebar, 4);
  gtk_widget_set_margin_end(ebar, 4);
  gtk_widget_set_margin_start(ebar, 4);
  gtk_widget_set_margin_top(ebar, 4);

  label = gtk_label_new(Q_("?noun:View"));
  gtk_notebook_append_page(GTK_NOTEBOOK(top_notebook), mainbox, label);

  frame = gtk_frame_new(NULL);
  gtk_grid_attach(GTK_GRID(map_widget), frame, 0, 0, 1, 1);

  map_canvas = gtk_drawing_area_new();
  gtk_widget_set_hexpand(map_canvas, TRUE);
  gtk_widget_set_vexpand(map_canvas, TRUE);
  gtk_widget_set_size_request(map_canvas, 300, 300);
  gtk_widget_set_can_focus(map_canvas, TRUE);

  gtk_widget_set_focusable(map_canvas, TRUE);

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

  mc_controller = gtk_event_controller_key_new();
  g_signal_connect(mc_controller, "key-pressed",
                   G_CALLBACK(toplevel_key_press_handler), NULL);
  gtk_widget_add_controller(toplevel, mc_controller);

  /* *** The message window -- this is a detachable widget *** */

  if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_MERGED) {
    bottom_hpaned = paned;
    right_notebook = bottom_notebook = top_notebook;
  } else {
    GtkWidget *hpaned;

    dtach_lowbox = detached_widget_new();
    gtk_paned_set_end_child(GTK_PANED(paned), dtach_lowbox);
    avbox = detached_widget_fill(dtach_lowbox);

    mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (!GUI_GTK_OPTION(small_display_layout)) {
      gtk_box_append(GTK_BOX(mainbox), ingame_votebar);
    }
    gtk_box_append(GTK_BOX(avbox), mainbox);

    if (GUI_GTK_OPTION(small_display_layout)) {
      hpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    } else {
      hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    }
    gtk_box_append(GTK_BOX(mainbox), hpaned);
    gtk_widget_set_margin_bottom(hpaned, 4);
    gtk_widget_set_margin_end(hpaned, 4);
    gtk_widget_set_margin_start(hpaned, 4);
    gtk_widget_set_margin_top(hpaned, 4);
    bottom_hpaned = hpaned;

    bottom_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(bottom_notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(bottom_notebook), TRUE);
    gtk_paned_set_start_child(GTK_PANED(hpaned), bottom_notebook);

    right_notebook = gtk_notebook_new();
    g_object_ref(right_notebook);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(right_notebook), GTK_POS_TOP);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(right_notebook), TRUE);
    if (GUI_GTK_OPTION(message_chat_location) == GUI_GTK_MSGCHAT_SPLIT) {
      gtk_paned_set_end_child(GTK_PANED(hpaned), right_notebook);
    }
  }

  mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  sw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(sw),
                                    TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_box_append(GTK_BOX(mainbox), sw);

  label = gtk_label_new(_("Chat"));
  gtk_notebook_append_page(GTK_NOTEBOOK(bottom_notebook), mainbox, label);

  text = gtk_text_view_new_with_buffer(message_buffer);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_widget_set_vexpand(text, TRUE);
  set_message_buffer_view_link_handlers(text);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), text);

  gtk_widget_set_name(text, "chatline");

  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
  gtk_widget_realize(text);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 5);

  main_message_area = GTK_TEXT_VIEW(text);
  if (dtach_lowbox != NULL) {
    g_object_set_data(G_OBJECT(dtach_lowbox), "hide-over-reparent", main_message_area);
  }

  chat_welcome_message(TRUE);

  /* The chat line */
  view = inputline_toolkit_view_new();
  gtk_box_append(GTK_BOX(mainbox), view);
  gtk_widget_set_margin_bottom(view, 3);
  gtk_widget_set_margin_end(view, 3);
  gtk_widget_set_margin_start(view, 3);
  gtk_widget_set_margin_top(view, 3);

  button = gtk_check_button_new_with_label(_("Allies Only"));
  gtk_widget_set_focus_on_click(button, FALSE);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(button),
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

  gtk_widget_set_visible(gtk_window_get_child(GTK_WINDOW(toplevel)),
                         TRUE);

  if (GUI_GTK_OPTION(enable_tabs)) {
    meswin_dialog_popup(FALSE);
  }

  gtk_notebook_set_current_page(GTK_NOTEBOOK(top_notebook), 0);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom_notebook), 0);

  if (!GUI_GTK_OPTION(map_scrollbars)) {
    gtk_widget_set_visible(map_horizontal_scrollbar, FALSE);
    gtk_widget_set_visible(map_vertical_scrollbar, FALSE);
  }
}

/**********************************************************************//**
  g_log callback to log with freelog
**************************************************************************/
static void g_log_to_freelog_cb(const gchar *log_domain,
                                GLogLevelFlags log_level,
                                const gchar *message,
                                gpointer user_data)
{
  enum log_level fllvl = LOG_ERROR;

  switch (log_level) {
  case G_LOG_LEVEL_DEBUG:
    fllvl = LOG_DEBUG;
    break;
  case G_LOG_LEVEL_WARNING:
    fllvl = LOG_WARN;
    break;
  default:
    break;
  }

  if (log_domain != NULL) {
    log_base(fllvl, "%s: %s", log_domain, message);
  } else {
    log_base(fllvl, "%s", message);
  }
}

/**********************************************************************//**
  g_log callback to log with freelog
**************************************************************************/
static GLogWriterOutput g_log_writer_to_freelog_cb(GLogLevelFlags log_level,
                                                   const GLogField *fields,
                                                   gsize n_fields,
                                                   gpointer user_data)
{
  /* No need to have formatter of our own - let's use glib's default one. */
  gchar *out = g_log_writer_format_fields(log_level, fields, n_fields, FALSE);

  g_log_to_freelog_cb(NULL, log_level, out, NULL);

  return G_LOG_WRITER_HANDLED;
}

/**********************************************************************//**
  Set up g_log callback for a single domain.
**************************************************************************/
static void set_g_log_callback_domain(const char *domain)
{
  g_log_set_handler(domain,
                    G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_LEVEL_WARNING
                    | G_LOG_LEVEL_MASK
                    | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                    g_log_to_freelog_cb, NULL);
}

/**********************************************************************//**
  Set up g_log callbacks.
**************************************************************************/
static void set_g_log_callbacks(void)
{
  /* Old API, still used by many log producers */
  g_log_set_default_handler(g_log_to_freelog_cb, NULL);

  set_g_log_callback_domain("Gtk");
  set_g_log_callback_domain("Gdk");
  set_g_log_callback_domain("Glib");

  /* glib >= 2.50 API */
  g_log_set_writer_func(g_log_writer_to_freelog_cb, NULL, NULL);
}

/**********************************************************************//**
  Called from main().
**************************************************************************/
void ui_init(void)
{
  log_set_callback(log_callback_utf8);
  set_g_log_callbacks();
  set_frame_by_frame_animation();
  zoom_phase_set(FALSE);
  zoom_set_steps(zoom_steps_custom);
}

/**********************************************************************//**
  Entry point for whole freeciv client program.
**************************************************************************/
int main(int argc, char **argv)
{
  return client_main(argc, argv, FALSE);
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
  MIGRATE_OPTION(helpdlg_xsize);
  MIGRATE_OPTION(helpdlg_ysize);
  MIGRATE_OPTION(optionsdlg_xsize);
  MIGRATE_OPTION(optionsdlg_ysize);
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
int ui_main(int argc, char **argv)
{
  if (parse_options(argc, argv)) {
    /* The locale has already been set in init_nls() and the windows-specific
     * locale logic in gtk_init() causes problems with zh_CN (see PR#39475) */
    gtk_disable_setlocale();

    if (!gtk_init_check()) {
      log_fatal(_("Failed to open graphical mode."));
      return EXIT_FAILURE;
    }

    menus_set_initial_toggle_values();

    gui_up = TRUE;
    fc_app = gtk_application_new(NULL, 0);
    g_signal_connect(fc_app, "activate", G_CALLBACK(activate_gui), NULL);
    g_application_run(G_APPLICATION(fc_app), 0, NULL);
    gui_up = FALSE;

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

    /* We have extra ref for unit_info_box that has protected
     * it from getting destroyed when editinfobox_refresh()
     * moves widgets around. Free that extra ref here. */
    g_object_unref(unit_info_box);
    if (empty_unit_paintable != NULL) {
      g_object_unref(empty_unit_paintable);
    }

    editgui_free();
    gtk_window_destroy(GTK_WINDOW(toplevel));
    message_buffer = NULL; /* Result of destruction of everything */
    tileset_free_tiles(tileset);
  }

  return EXIT_SUCCESS;
}

/**********************************************************************//**
  Run the gui
**************************************************************************/
static void activate_gui(GtkApplication *app, gpointer data)
{
  PangoFontDescription *toplevel_font_name;
  guint sig;
  GtkEventController *controller;
  char window_name[1024];

  toplevel = gtk_application_window_new(app);
  if (vmode.width > 0 && vmode.height > 0) {
    gtk_window_set_default_size(GTK_WINDOW(toplevel),
                                vmode.width, vmode.height);
  }

  controller = GTK_EVENT_CONTROLLER(gtk_event_controller_focus_new());
  g_signal_connect(controller, "enter",
                   G_CALLBACK(fc_gained_focus), NULL);
  g_signal_connect(controller, "leave",
                   G_CALLBACK(fc_lost_focus), NULL);
  gtk_widget_add_controller(toplevel, controller);

  gtk_widget_realize(toplevel);
  gtk_widget_set_name(toplevel, "Freeciv");

  help_system_init();

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

  fc_snprintf(window_name, sizeof(window_name), _("Freeciv (%s)"), GUI_NAME_SHORT);
  gtk_window_set_title(GTK_WINDOW(toplevel), window_name);

  g_signal_connect(toplevel, "close-request",
                   G_CALLBACK(quit_dialog_callback), NULL);

  /* Disable GTK cursor key focus movement */
  sig = g_signal_lookup("move-focus", GTK_TYPE_WIDGET);
  g_signal_handlers_disconnect_matched(toplevel, G_SIGNAL_MATCH_ID, sig,
                                       0, 0, 0, 0);
  g_signal_connect(toplevel, "move-focus", G_CALLBACK(toplevel_focus), NULL);

  options_iterate(client_optset, poption) {
    if (OT_FONT == option_type(poption)) {
      /* Force to call the appropriate callback. */
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

  /* Keep the icon of the executable on Windows (see PR#36491) */
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

  gtk_widget_set_visible(toplevel, TRUE);

  /* Assumes toplevel showing */
  set_client_state(C_S_DISCONNECTED);

  /* Assumes client_state is set */
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
    w = unit_pic;
    unit_id_top = punit ? punit->id : 0;
  } else {
    w = unit_below_pic[idx];
    unit_ids[idx] = punit ? punit->id : 0;
  }

  if (!w) {
    return;
  }

  if (punit) {
    put_unit_picture(punit, GTK_PICTURE(w), -1);
  } else {
    if (empty_unit_paintable == NULL) {
      /* FIXME: Use proper icon height instead of hardcoded 50 */
      empty_unit_paintable = gdk_paintable_new_empty(tileset_tile_width(tileset), 50);

      /* Add ref to avoid it getting destroyed along any single parent widget. */
      g_object_ref(empty_unit_paintable);
    }
    gtk_picture_set_paintable(GTK_PICTURE(w), empty_unit_paintable);
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

  if (more_arrow == nullptr) {
    return;
  }

  if (onoff && !showing) {
    gtk_widget_set_visible(more_arrow, TRUE);
    showing = TRUE;
  } else if (!onoff && showing) {
    gtk_widget_set_visible(more_arrow, FALSE);
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
  Callback for clicking a unit icon underneath unit info box.
  these are the units on the same tile as the focus unit.
**************************************************************************/
static gboolean select_unit_pic_callback(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer data)
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

  if (unit_ids[i] == 0) { /* No unit displayed at this place */
    return TRUE;
  }

  punit = game_unit_by_number(unit_ids[i]);
  if (NULL != punit && unit_owner(punit) == client_player()) {
    /* Unit shouldn't be NULL but may be owned by an ally. */
    unit_focus_set(punit);
  }

  return TRUE;
}

/**********************************************************************//**
  Callback for clicking a unit icon underneath unit info box.
  these are the units on the same tile as the focus unit.
**************************************************************************/
static gboolean select_more_arrow_callback(GtkGestureClick *gesture, int n_press,
                                           double x, double y, gpointer data)
{
  struct unit *punit = game_unit_by_number(unit_id_top);

  if (punit) {
    unit_select_dialog_popup(unit_tile(punit));
  }

  return TRUE;
}

/**********************************************************************//**
  On close of the info popup
**************************************************************************/
static void info_popup_closed(GtkWidget *self, gpointer data)
{
  gtk_widget_unparent(self);
}

/**********************************************************************//**
  Popup info box
**************************************************************************/
static gboolean show_info_popup(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer data)
{
  GtkWidget *p;
  GtkWidget *child;
  GtkWidget *frame = GTK_WIDGET(data);

  p = gtk_popover_new();

  gtk_widget_set_parent(p, frame);
  child = gtk_label_new(get_info_label_text_popup());
  gtk_popover_set_child(GTK_POPOVER(p), child);
  g_signal_connect(p, "closed",
                   G_CALLBACK(info_popup_closed), NULL);
  gtk_popover_popup(GTK_POPOVER(p));

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
  This function is called after the client has successfully
  connected to the server
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
      disconnect_from_server(FALSE);
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
  A wrapper for the callback called through add_idle_callback().
**************************************************************************/
static gboolean idle_callback_wrapper(gpointer data)
{
  struct callback *cb = data;

  (cb->callback)(cb->data);
  free(cb);

  return FALSE;
}

/**********************************************************************//**
  Enqueue a callback to be called during an idle moment. The 'callback'
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
  Add idle callback for updating animations.
**************************************************************************/
void animation_idle_cb(void *data)
{
  if (get_current_client_page() == PAGE_GAME) {
    update_animation();
    add_idle_callback(animation_idle_cb, NULL);
  }
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
  fc_assert_ret(GTK_IS_CHECK_BUTTON(button));

  gtk_check_button_set_active(GTK_CHECK_BUTTON(button),
                              option_bool_get(poption));
}

/**********************************************************************//**
  Option callback for the 'fullscreen' gtk-gui option.
**************************************************************************/
void fullscreen_opt_refresh(struct option *poption)
{
  if (GUI_GTK_OPTION(fullscreen)) {
    gtk_window_fullscreen(GTK_WINDOW(toplevel));
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(toplevel));
  }
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
  for the specific gui-gtk-4.0 options.
**************************************************************************/
void options_extra_init(void)
{
  struct option *poption;

#define option_var_set_callback(var, callback)                              \
  if ((poption = optset_option_by_name(client_optset,                       \
                                       GUI_GTK_OPTION_STR(var)))) {         \
    option_set_changed_callback(poption, callback);                         \
  } else {                                                                  \
    log_error("Didn't find option %s!", GUI_GTK_OPTION_STR(var));           \
  }

  option_var_set_callback(allied_chat_only,
                          allied_chat_only_callback);
  option_var_set_callback(fullscreen,
                          fullscreen_opt_refresh);

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
  fc_assert_ret(button != nullptr);
  fc_assert_ret(GTK_IS_CHECK_BUTTON(button));

  /* Hide the "Allies Only" button for local games. */
  if (is_server_running()) {
    gtk_widget_set_visible(button, FALSE);
  } else {
    gtk_widget_set_visible(button, TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button),
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

/**********************************************************************//**
  Return the client GtkApplication
**************************************************************************/
GtkApplication *gui_app(void)
{
  return fc_app;
}

/**********************************************************************//**
  Define properties of this gui.
**************************************************************************/
void setup_gui_properties(void)
{
  gui_properties.animations = TRUE;
  gui_properties.views.isometric = TRUE;
  gui_properties.views.overhead = TRUE;
#ifdef GTK3_3D_ENABLED
  gui_properties.views.d3 = TRUE;
#else  /* GTK3_3D_ENABLED */
  gui_properties.views.d3 = FALSE;
#endif /* GTK3_3D_ENABLED */
}
