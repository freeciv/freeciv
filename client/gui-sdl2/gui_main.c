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

/***********************************************************************
                          gui_main.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "fc_prehdrs.h"

#include <errno.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "netintf.h"

/* common */
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "clinet.h"
#include "editgui_g.h"
#include "gui_properties.h"
#include "spaceshipdlg_g.h"
#include "tilespec.h"
#include "update_queue.h"
#include "zoom.h"

/* gui-sdl2 */
#include "chatline.h"
#include "citydlg.h"
#include "cityrep.h"
#include "diplodlg.h"
#include "graphics.h"
#include "gui_id.h"
#include "gui_mouse.h"
#include "gui_tilespec.h"
#include "inteldlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "messagewin.h"
#include "optiondlg.h"
#include "repodlgs.h"
#include "themespec.h"
#include "widget.h"

#include "gui_main.h"

#define UNITS_TIMER_INTERVAL      128 /* milliseconds */
#define MAP_SCROLL_TIMER_INTERVAL 500

const char *client_string = "gui-sdl2";

/* The real GUI character encoding is UTF-16 which is not supported by
 * fciconv code at this time. Conversion between UTF-8 and UTF-16 is done
 * in gui_iconv.c */
const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

Uint32 sdl2_client_flags = 0;

Uint32 widget_info_counter = 0;
int MOVE_STEP_X = DEFAULT_MOVE_STEP;
int MOVE_STEP_Y = DEFAULT_MOVE_STEP;
extern bool draw_goto_patrol_lines;
bool is_unit_move_blocked;
bool LSHIFT;
bool RSHIFT;
bool LCTRL;
bool RCTRL;
bool LALT;
static int city_names_font_size = 10;
static int city_productions_font_size = 10;
int *client_font_sizes[FONT_COUNT] = {
  &city_names_font_size,       /* FONT_CITY_NAME */
  &city_productions_font_size, /* FONT_CITY_PROD */
  &city_productions_font_size  /* FONT_REQTREE_TEXT; not used yet */
};

static unsigned font_size_parameter = 0;

/* ================================ Private ============================ */
static int net_socket = -1;
static bool autoconnect = FALSE;
static bool is_map_scrolling = FALSE;
static enum direction8 scroll_dir;

static struct finger_behavior finger_behavior;
static struct mouse_button_behavior button_behavior;

static SDL_Event __net_user_event;
static SDL_Event __anim_user_event;
static SDL_Event __info_user_event;
static SDL_Event __flush_user_event;
static SDL_Event __map_scroll_user_event;

static SDL_Event *net_user_event = NULL;
static SDL_Event *anim_user_event = NULL;
static SDL_Event *info_user_event = NULL;
static SDL_Event *flush_user_event = NULL;
static SDL_Event *map_scroll_user_event = NULL;

static void print_usage(void);
static bool parse_options(int argc, char **argv);
static bool check_scroll_area(int x, int y);

int user_event_type;

enum USER_EVENT_ID {
  EVENT_ERROR = 0,
  NET,
  ANIM,
  TRY_AUTO_CONNECT,
  SHOW_WIDGET_INFO_LABEL,
  FLUSH,
  MAP_SCROLL,
  EXIT_FROM_EVENT_LOOP
};

struct callback {
  void (*callback)(void *data);
  void *data;
};

#define SPECLIST_TAG callback
#define SPECLIST_TYPE struct callback
#include "speclist.h"

struct callback_list *callbacks = NULL;

/* =========================================================== */

/**********************************************************************//**
  Print extra usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(void)
{
  /* Add client-specific usage information here */
  fc_fprintf(stderr,
             _("  -f,  --fullscreen\tStart Client in Fullscreen mode\n"));
  fc_fprintf(stderr,
             _("  -F,  --Font SIZE\tUse SIZE as the base font size\n"));
  fc_fprintf(stderr, _("  -s,  --swrenderer\tUse SW renderer\n"));
  fc_fprintf(stderr, _("  -t,  --theme THEME\tUse GUI theme THEME\n"));

  /* TRANS: No full stop after the URL, could cause confusion. */
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

/**********************************************************************//**
  Search for gui-specific command-line options.
**************************************************************************/
static bool parse_options(int argc, char **argv)
{
  int i = 1;
  char *option = NULL;

  while (i < argc) {
    if (is_option("--help", argv[i])) {
      print_usage();

      return FALSE;
    } else if (is_option("--fullscreen", argv[i])) {
      GUI_SDL_OPTION(fullscreen) = TRUE;
    } else if (is_option("--swrenderer", argv[i])) {
      sdl2_client_flags |= CF_SWRENDERER;
    } else if ((option = get_option_malloc("--Font", argv, &i, argc, FALSE))) {
      if (!str_to_uint(option, &font_size_parameter)) {
        fc_fprintf(stderr, _("Invalid font size %s"), option);
        exit(EXIT_FAILURE);
      }
      free(option);
    } else if ((option = get_option_malloc("--theme", argv, &i, argc, FALSE))) {
      sz_strlcpy(GUI_SDL_OPTION(default_theme_name), option);
      free(option);
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[i]);

      exit(EXIT_FAILURE);
    }

    i++;
  }

  return TRUE;
}

/**********************************************************************//**
  Main handler for key presses
**************************************************************************/
static widget_id main_key_down_handler(SDL_Keysym key, void *data)
{
  static struct widget *pwidget;

  if ((pwidget = find_next_widget_for_key(NULL, key)) != NULL) {
    return widget_pressed_action(pwidget);
  } else {
    if (key.sym == SDLK_TAB) {
      /* Input */
      popup_input_line();
    } else {
      if (map_event_handler(key)
          && C_S_RUNNING == client_state()) {
        switch (key.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          if (LSHIFT || RSHIFT) {
            disable_focus_animation();
            key_end_turn();
          } else {
            struct unit *punit;
            struct city *pcity;

            if (NULL != (punit = head_of_units_in_focus())
                && (pcity = tile_city(unit_tile(punit))) != NULL
                && city_owner(pcity) == client.conn.playing) {
              popup_city_dialog(pcity);
            }
          }
          return ID_ERROR;

        case SDLK_F2:
          units_report_dialog_popup(FALSE);
          return ID_ERROR;

        case SDLK_F4:
          city_report_dialog_popup(FALSE);
          return ID_ERROR;

        case SDLK_F7:
          send_report_request(REPORT_WONDERS_OF_THE_WORLD);
          return ID_ERROR;

        case SDLK_F8:
          send_report_request(REPORT_TOP_CITIES);
          return ID_ERROR;

        case SDLK_F9:
          if (meswin_dialog_is_open()) {
            meswin_dialog_popdown();
          } else {
            meswin_dialog_popup(TRUE);
          }
          flush_dirty();
          return ID_ERROR;

        case SDLK_F11:
          send_report_request(REPORT_DEMOGRAPHIC);
          return ID_ERROR;

        case SDLK_F12:
          popup_spaceship_dialog(client.conn.playing);
          return ID_ERROR;

        case SDLK_ASTERISK:
          send_report_request(REPORT_ACHIEVEMENTS);
          return ID_ERROR;

        default:
          return ID_ERROR;
        }
      }
    }
  }

  return ID_ERROR;
}

/**********************************************************************//**
  Main key release handler.
**************************************************************************/
static widget_id main_key_up_handler(SDL_Keysym key, void *data)
{
  if (selected_widget) {
    unselect_widget_action();
  }

  return ID_ERROR;
}
/**********************************************************************//**
  Main finger down handler.
**************************************************************************/
static widget_id main_finger_down_handler(SDL_TouchFingerEvent *touch_event,
                                          void *data)
{
  struct widget *pwidget;
  /* Touch event coordinates are normalized (0...1). */
  int x = touch_event->x * main_window_width();
  int y = touch_event->y * main_window_height();

  if ((pwidget = find_next_widget_at_pos(NULL, x, y)) != NULL) {
    if (get_wstate(pwidget) != FC_WS_DISABLED) {
      return widget_pressed_action(pwidget);
    }
  } else {
    /* No visible widget at this position; map pressed. */
    if (!finger_behavior.counting) {
      /* Start counting. */
      finger_behavior.counting = TRUE;
      finger_behavior.finger_down_ticks = SDL_GetTicks();
      finger_behavior.event = *touch_event;
      finger_behavior.hold_state = MB_HOLD_SHORT;
      finger_behavior.ptile = canvas_pos_to_tile(x, y, mouse_zoom);
    }
  }

  return ID_ERROR;
}

/**********************************************************************//**
  Main finger release handler.
**************************************************************************/
static widget_id main_finger_up_handler(SDL_TouchFingerEvent *touch_event,
                                        void *data)
{
  /* Touch event coordinates are normalized (0...1). */
  int x = touch_event->x * main_window_width();
  int y = touch_event->y * main_window_height();

  /* Screen wasn't pressed over a widget. */
  if (finger_behavior.finger_down_ticks
      && !find_next_widget_at_pos(NULL, x, y)) {
    finger_behavior.event = *touch_event;
    finger_up_on_map(&finger_behavior);
  }

  finger_behavior.counting = FALSE;
  finger_behavior.finger_down_ticks = 0;

  is_map_scrolling = FALSE;

  return ID_ERROR;
}

/**********************************************************************//**
  Main mouse click handler.
**************************************************************************/
static widget_id main_mouse_button_down_handler(SDL_MouseButtonEvent *button_event,
                                                void *data)
{
  struct widget *pwidget;

  if ((pwidget = find_next_widget_at_pos(NULL,
                                         button_event->x,
                                         button_event->y)) != NULL) {
    if (get_wstate(pwidget) != FC_WS_DISABLED) {
      return widget_pressed_action(pwidget);
    }
  } else {
    /* No visible widget at this position -> map click */
#ifdef UNDER_CE
    if (!check_scroll_area(button_event->x, button_event->y)) {
#endif
    if (!button_behavior.counting) {
      /* Start counting */
      button_behavior.counting = TRUE;
      button_behavior.button_down_ticks = SDL_GetTicks();
      *button_behavior.event = *button_event;
      button_behavior.hold_state = MB_HOLD_SHORT;
      button_behavior.ptile = canvas_pos_to_tile(button_event->x, button_event->y,
                                                 mouse_zoom);
    }
#ifdef UNDER_CE
    }
#endif
  }

  return ID_ERROR;
}

/**********************************************************************//**
  Main mouse button release handler.
**************************************************************************/
static widget_id main_mouse_button_up_handler(SDL_MouseButtonEvent *button_event,
                                              void *data)
{
  if (button_behavior.button_down_ticks /* Button wasn't pressed over a widget */
      && !find_next_widget_at_pos(NULL, button_event->x, button_event->y)) {
    *button_behavior.event = *button_event;
    button_up_on_map(&button_behavior);
  }

  button_behavior.counting = FALSE;
  button_behavior.button_down_ticks = 0;

  is_map_scrolling = FALSE;

  return ID_ERROR;
}

#ifdef UNDER_CE
  #define SCROLL_MAP_AREA       8
#else
  #define SCROLL_MAP_AREA       1
#endif

/**********************************************************************//**
  Main handler for mouse movement handling.
**************************************************************************/
static widget_id main_mouse_motion_handler(SDL_MouseMotionEvent *motion_event,
                                           void *data)
{
  static struct widget *pwidget;
  struct tile *ptile;

  /* Stop evaluating button hold time when moving to another tile in medium
   * hold state or above */
  if (button_behavior.counting && (button_behavior.hold_state >= MB_HOLD_MEDIUM)) {
    ptile = canvas_pos_to_tile(motion_event->x, motion_event->y,
                               mouse_zoom);
    if (tile_index(ptile) != tile_index(button_behavior.ptile)) {
      button_behavior.counting = FALSE;
    }
  }

  if (draw_goto_patrol_lines) {
    update_line(motion_event->x, motion_event->y);
  }

#ifndef UNDER_CE
  if (GUI_SDL_OPTION(fullscreen)) {
    check_scroll_area(motion_event->x, motion_event->y);
  }
#endif /* UNDER_CE */

  if ((pwidget = find_next_widget_at_pos(NULL,
                                         motion_event->x,
                                         motion_event->y)) != NULL) {
    update_mouse_cursor(CURSOR_DEFAULT);
    if (get_wstate(pwidget) != FC_WS_DISABLED) {
      widget_selected_action(pwidget);
    }
  } else {
    if (selected_widget) {
      unselect_widget_action();
    } else {
      control_mouse_cursor(canvas_pos_to_tile(motion_event->x, motion_event->y,
                                              mouse_zoom));
    }
  }

  draw_mouse_cursor();

  return ID_ERROR;
}

/**********************************************************************//**
  This is called every TIMER_INTERVAL milliseconds whilst we are in
  gui_main_loop() (which is all of the time) TIMER_INTERVAL needs to be .5s
**************************************************************************/
static void update_button_hold_state(void)
{
  /* button pressed */
  if (button_behavior.counting) {
    if (((SDL_GetTicks() - button_behavior.button_down_ticks) >= MB_MEDIUM_HOLD_DELAY)
        && ((SDL_GetTicks() - button_behavior.button_down_ticks) < MB_LONG_HOLD_DELAY)) {

      if (button_behavior.hold_state != MB_HOLD_MEDIUM) {
        button_behavior.hold_state = MB_HOLD_MEDIUM;
        button_down_on_map(&button_behavior);
      }

    } else if (((SDL_GetTicks() - button_behavior.button_down_ticks)
                                                    >= MB_LONG_HOLD_DELAY)) {

      if (button_behavior.hold_state != MB_HOLD_LONG) {
        button_behavior.hold_state = MB_HOLD_LONG;
        button_down_on_map(&button_behavior);
      }
    }
  }

  return;
}

/**********************************************************************//**
  Check if coordinate is in scroll area.
**************************************************************************/
static bool check_scroll_area(int x, int y)
{
  SDL_Rect rect_north = {.x = 0, .y = 0,
    .w = main_data.map->w, .h = SCROLL_MAP_AREA};
  SDL_Rect rect_east = {.x = main_data.map->w - SCROLL_MAP_AREA, .y = 0,
    .w = SCROLL_MAP_AREA, .h = main_data.map->h};
  SDL_Rect rect_south = {.x = 0, .y = main_data.map->h - SCROLL_MAP_AREA,
    .w = main_data.map->w, .h = SCROLL_MAP_AREA};
  SDL_Rect rect_west = {.x = 0, .y = 0,
    .w = SCROLL_MAP_AREA, .h = main_data.map->h};

  if (is_in_rect_area(x, y, &rect_north)) {
    is_map_scrolling = TRUE;
    if (is_in_rect_area(x, y, &rect_west)) {
      scroll_dir = DIR8_NORTHWEST;
    } else if (is_in_rect_area(x, y, &rect_east)) {
      scroll_dir = DIR8_NORTHEAST;
    } else {
      scroll_dir = DIR8_NORTH;
    }
  } else if (is_in_rect_area(x, y, &rect_south)) {
    is_map_scrolling = TRUE;
    if (is_in_rect_area(x, y, &rect_west)) {
      scroll_dir = DIR8_SOUTHWEST;
    } else if (is_in_rect_area(x, y, &rect_east)) {
      scroll_dir = DIR8_SOUTHEAST;
    } else {
      scroll_dir = DIR8_SOUTH;
    }
  } else if (is_in_rect_area(x, y, &rect_east)) {
    is_map_scrolling = TRUE;
    scroll_dir = DIR8_EAST;
  } else if (is_in_rect_area(x, y, &rect_west)) {
    is_map_scrolling = TRUE;
    scroll_dir = DIR8_WEST;
  } else {
    is_map_scrolling = FALSE;
  }

  return is_map_scrolling;
}

/* ============================ Public ========================== */

/**********************************************************************//**
  Instruct event loop to exit.
**************************************************************************/
void force_exit_from_event_loop(void)
{
  SDL_Event event;

  event.type = user_event_type;
  event.user.code = EXIT_FROM_EVENT_LOOP;
  event.user.data1 = NULL;
  event.user.data2 = NULL;

  SDL_PushEvent(&event);
}

/**********************************************************************//**
  Filter out mouse motion events for too small movement to react to.
  This function may run in a separate event thread.
**************************************************************************/
int FilterMouseMotionEvents(void *data, SDL_Event *event)
{
  if (event->type == SDL_MOUSEMOTION) {
    static int x = 0, y = 0;

    if (((MOVE_STEP_X > 0) && (abs(event->motion.x - x) >= MOVE_STEP_X))
        || ((MOVE_STEP_Y > 0) && (abs(event->motion.y - y) >= MOVE_STEP_Y)) ) {
      x = event->motion.x;
      y = event->motion.y;
      return 1;    /* Catch it */
    } else {
      return 0;    /* Drop it, we've handled it */
    }
  }

  return 1;
}

/**********************************************************************//**
  SDL2-client main loop.
**************************************************************************/
widget_id gui_event_loop(void *data,
                         void (*loop_action)(void *data),
                         widget_id (*key_down_handler)(SDL_Keysym key, void *data),
                         widget_id (*key_up_handler)(SDL_Keysym key, void *data),
                         widget_id (*textinput_handler)(const char *text, void *data),
                         widget_id (*finger_down_handler)(SDL_TouchFingerEvent *touch_event,
                                                          void *data),
                         widget_id (*finger_up_handler)(SDL_TouchFingerEvent *touch_event,
                                                        void *data),
                         widget_id (*finger_motion_handler)(SDL_TouchFingerEvent *touch_event,
                                                            void *data),
                         widget_id (*mouse_button_down_handler)(SDL_MouseButtonEvent *button_event,
                                                                void *data),
                         widget_id (*mouse_button_up_handler)(SDL_MouseButtonEvent *button_event,
                                                              void *data),
                         widget_id (*mouse_motion_handler)(SDL_MouseMotionEvent *motion_event,
                                                           void *data))
{
  Uint16 ID;
  static fc_timeval tv;
  static fd_set civfdset;
  Uint32 t_current, t_last_unit_anim, t_last_map_scrolling;
  Uint32 real_timer_next_call;
  static int result;

  ID = ID_ERROR;
  t_last_map_scrolling = t_last_unit_anim = real_timer_next_call = SDL_GetTicks();
  while (ID == ID_ERROR) {
    /* ========================================= */
    /* Net check with 10ms delay event loop */
    if (net_socket >= 0) {
      FD_ZERO(&civfdset);

      if (net_socket >= 0) {
        FD_SET(net_socket, &civfdset);
      }

      tv.tv_sec = 0;
      tv.tv_usec = 10000; /* 10ms*/

      result = fc_select(net_socket + 1, &civfdset, NULL, NULL, &tv);
      if (result < 0) {
        if (errno != EINTR) {
          break;
        } else {
          continue;
        }
      } else {
        if (result > 0) {
          if ((net_socket >= 0) && FD_ISSET(net_socket, &civfdset)) {
            SDL_PushEvent(net_user_event);
          }
        }
      }
    } else { /* If connection is not establish */
      SDL_Delay(10);
    }
    /* ========================================= */

    t_current = SDL_GetTicks();

    if (t_current > real_timer_next_call) {
      real_timer_next_call = t_current + (real_timer_callback() * 1000);
    }

    if ((t_current - t_last_unit_anim) > UNITS_TIMER_INTERVAL) {
      if (autoconnect) {
        widget_info_counter++;
        SDL_PushEvent(anim_user_event);
      } else {
        SDL_PushEvent(anim_user_event);
      }

      t_last_unit_anim = SDL_GetTicks();
    }

    if (is_map_scrolling) {
      if ((t_current - t_last_map_scrolling) > MAP_SCROLL_TIMER_INTERVAL) {
        SDL_PushEvent(map_scroll_user_event);
        t_last_map_scrolling = SDL_GetTicks();
      }
    } else {
      t_last_map_scrolling = SDL_GetTicks();
    }

    if (widget_info_counter > 0) {
      SDL_PushEvent(info_user_event);
      widget_info_counter = 0;
    }

    /* ========================================= */

    if (loop_action) {
      loop_action(data);
    }

    /* ========================================= */

    while (SDL_PollEvent(&main_data.event) == 1) {

      if (main_data.event.type == user_event_type) {
        switch (main_data.event.user.code) {
        case NET:
          input_from_server(net_socket);
          break;
        case ANIM:
          update_button_hold_state();
          animate_mouse_cursor();
          draw_mouse_cursor();
          break;
        case SHOW_WIDGET_INFO_LABEL:
          draw_widget_info_label();
          break;
        case TRY_AUTO_CONNECT:
          if (try_to_autoconnect()) {
            info_user_event->user.code = SHOW_WIDGET_INFO_LABEL;
            autoconnect = FALSE;
          }
          break;
        case FLUSH:
          unqueue_flush();
          break;
        case MAP_SCROLL:
          scroll_mapview(scroll_dir);
          break;
        case EXIT_FROM_EVENT_LOOP:
          return MAX_ID;
          break;
        default:
          break;
        }

      } else {

        switch (main_data.event.type) {

        case SDL_QUIT:
          return MAX_ID;
          break;

        case SDL_KEYUP:
          switch (main_data.event.key.keysym.sym) {
            /* find if Shifts are released */
            case SDLK_RSHIFT:
              RSHIFT = FALSE;
            break;
            case SDLK_LSHIFT:
              LSHIFT = FALSE;
            break;
            case SDLK_LCTRL:
              LCTRL = FALSE;
            break;
            case SDLK_RCTRL:
              RCTRL = FALSE;
            break;
            case SDLK_LALT:
              LALT = FALSE;
            break;
            default:
              if (key_up_handler) {
                ID = key_up_handler(main_data.event.key.keysym, data);
              }
            break;
          }
          break;

        case SDL_KEYDOWN:
          switch (main_data.event.key.keysym.sym) {
#if 0
          case SDLK_PRINT:
            fc_snprintf(schot, sizeof(schot), "fc_%05d.bmp", schot_nr++);
            log_normal(_("Making screenshot %s"), schot);
            SDL_SaveBMP(main_data.screen, schot);
            break;
#endif /* 0 */

          case SDLK_RSHIFT:
            /* Right Shift is Pressed */
            RSHIFT = TRUE;
            break;

          case SDLK_LSHIFT:
            /* Left Shift is Pressed */
            LSHIFT = TRUE;
            break;

          case SDLK_LCTRL:
            /* Left CTRL is Pressed */
            LCTRL = TRUE;
            break;

          case SDLK_RCTRL:
            /* Right CTRL is Pressed */
            RCTRL = TRUE;
            break;

          case SDLK_LALT:
            /* Left ALT is Pressed */
            LALT = TRUE;
            break;

          default:
            if (key_down_handler) {
              ID = key_down_handler(main_data.event.key.keysym, data);
            }
            break;
          }
          break;

        case SDL_TEXTINPUT:
          if (textinput_handler) {
            ID = textinput_handler(main_data.event.text.text, data);
          }
          break;

        case SDL_FINGERDOWN:
          if (finger_down_handler) {
            ID = finger_down_handler(&main_data.event.tfinger, data);
          }
          break;

        case SDL_FINGERUP:
          if (finger_up_handler) {
            ID = finger_up_handler(&main_data.event.tfinger, data);
          }
          break;

        case SDL_FINGERMOTION:
          if (finger_motion_handler) {
            ID = finger_motion_handler(&main_data.event.tfinger, data);
          }
          break;

        case SDL_MOUSEBUTTONDOWN:
          if (mouse_button_down_handler) {
            ID = mouse_button_down_handler(&main_data.event.button, data);
          }
          break;

        case SDL_MOUSEBUTTONUP:
          if (mouse_button_up_handler) {
            ID = mouse_button_up_handler(&main_data.event.button, data);
          }
          break;

        case SDL_MOUSEMOTION:
          if (mouse_motion_handler) {
            ID = mouse_motion_handler(&main_data.event.motion, data);
          }
          break;
        }
      }
    }

    if (ID == ID_ERROR) {
      if (callbacks && callback_list_size(callbacks) > 0) {
        struct callback *cb = callback_list_get(callbacks, 0);

        callback_list_remove(callbacks, cb);
        (cb->callback)(cb->data);
        free(cb);
      }
    }

    update_main_screen();

#ifdef __EMSCRIPTEN__
    emscripten_sleep(100);
#endif
  }

  return ID;
}

/* ============ Freeciv native game function =========== */

/**********************************************************************//**
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void ui_init(void)
{
  Uint32 sdl_flags;

  button_behavior.counting = FALSE;
  button_behavior.button_down_ticks = 0;
  button_behavior.hold_state = MB_HOLD_SHORT;
  button_behavior.event = fc_calloc(1, sizeof(SDL_MouseButtonEvent));

  sdl2_client_flags = 0;
  sdl_flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;

  init_sdl(sdl_flags);
}

/**********************************************************************//**
  Really resize the main window.
**************************************************************************/
static void real_resize_window_callback(void *data)
{
  struct widget *widget;

  if (C_S_RUNNING == client_state()) {
    /* Move units window to bottom-right corner. */
    set_new_unitinfo_window_pos();
    /* Move minimap window to bottom-left corner. */
    set_new_minimap_window_pos();

    /* Move cooling/warming icons to bottom-right corner. */
    widget = get_widget_pointer_from_main_list(ID_WARMING_ICON);
    widget_set_position(widget, (main_window_width() - adj_size(10)
                                 - (widget->size.w * 2)), widget->size.y);

    widget = get_widget_pointer_from_main_list(ID_COOLING_ICON);
    widget_set_position(widget, (main_window_width() - adj_size(10)
                                 - widget->size.w), widget->size.y);

    map_canvas_resized(main_window_width(), main_window_height());
    update_info_label();
    update_unit_info_label(get_units_in_focus());
    center_on_something();      /* With redrawing full map. */
    update_order_widgets();
  } else {
    draw_intro_gfx();
    dirty_all();
  }

  flush_all();
}

/**********************************************************************//**
  Resize the main window after option changed.
**************************************************************************/
static void resize_window_callback(struct option *poption)
{
  update_queue_add(real_resize_window_callback, NULL);
}

/**********************************************************************//**
  Change fullscreen status after option changed.
**************************************************************************/
static void fullscreen_callback(struct option *poption)
{
  SDL_DisplayMode mode;

  if (GUI_SDL_OPTION(fullscreen)) {
    SDL_SetWindowFullscreen(main_data.screen, SDL_WINDOW_FULLSCREEN_DESKTOP);
  } else {
    SDL_SetWindowFullscreen(main_data.screen, 0);
  }

  SDL_GetWindowDisplayMode(main_data.screen, &mode);

  if (!create_surfaces(mode.w, mode.h)) {
    /* Try to revert */
    if (!GUI_SDL_OPTION(fullscreen)) {
      SDL_SetWindowFullscreen(main_data.screen, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
      SDL_SetWindowFullscreen(main_data.screen, 0);
    }
  }

  update_queue_add(real_resize_window_callback, NULL);
}

/**********************************************************************//**
  Extra initializers for client options. Here we make set the callback
  for the specific gui-sdl2 options.
**************************************************************************/
void options_extra_init(void)
{
  struct option *poption;

#define option_var_set_callback(var, callback)                              \
  if ((poption = optset_option_by_name(client_optset,                       \
                                       GUI_SDL_OPTION_STR(var)))) {         \
    option_set_changed_callback(poption, callback);                         \
  } else {                                                                  \
    log_error("Didn't find option %s!", GUI_SDL_OPTION_STR(var));           \
  }

  option_var_set_callback(fullscreen, fullscreen_callback);
  option_var_set_callback(screen, resize_window_callback);
#undef option_var_set_callback
}

/**********************************************************************//**
  Remove double messages caused by message configured to both MW_MESSAGES
  and MW_OUTPUT.
**************************************************************************/
static void clear_double_messages_call(void)
{
  int i;

  /* Clear double call */
  for (i = 0; i <= event_type_max(); i++) {
    if (messages_where[i] & MW_MESSAGES) {
      messages_where[i] &= ~MW_OUTPUT;
    }
  }
}

/**********************************************************************//**
  Entry point for freeciv client program. SDL has macro magic to turn
  this in to function named SDL_main() and it provides actual main()
  itself.
**************************************************************************/
int main(int argc, char **argv)
{
  return client_main(argc, argv, FALSE);
}

/**********************************************************************//**
  Migrate sdl2 client specific options from sdl client options.
**************************************************************************/
static void migrate_options_from_sdl(void)
{
  log_normal(_("Migrating options from sdl to sdl2 client"));

#define MIGRATE_OPTION(opt) gui_options.gui_sdl2_##opt = gui_options.gui_sdl_##opt;

  /* Default theme name is never migrated */
  MIGRATE_OPTION(fullscreen);
  MIGRATE_OPTION(screen);
  MIGRATE_OPTION(do_cursor_animation);
  MIGRATE_OPTION(use_color_cursors);

#undef MIGRATE_OPTION

  gui_options.gui_sdl2_migrated_from_sdl = TRUE;
}

/**********************************************************************//**
  The main loop for the UI. This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
int ui_main(int argc, char *argv[])
{
  Uint32 flags = 0;

  if (parse_options(argc, argv)) {
    if (!gui_options.gui_sdl2_migrated_from_sdl) {
      migrate_options_from_sdl();
    }
    if (!GUI_SDL_OPTION(default_screen_size_set)) {
      if (font_size_parameter > 10) {
        GUI_SDL_OPTION(screen) = VIDEO_MODE(640 * font_size_parameter / 10,
                                            480 * font_size_parameter / 10);
      }

      GUI_SDL_OPTION(default_screen_size_set) = TRUE;
    }

    if (GUI_SDL_OPTION(fullscreen)) {
      flags |= SDL_WINDOW_FULLSCREEN;
    } else {
      flags &= ~SDL_WINDOW_FULLSCREEN;
    }

    log_normal(_("Using Video Output: %s"), SDL_GetCurrentVideoDriver());
    if (!set_video_mode(GUI_SDL_OPTION(screen.width),
                        GUI_SDL_OPTION(screen.height),
                        flags)) {
      return EXIT_FAILURE;
    }

    user_event_type = SDL_RegisterEvents(1);

    SDL_zero(__net_user_event);
    __net_user_event.type = user_event_type;
    __net_user_event.user.code = NET;
    __net_user_event.user.data1 = NULL;
    __net_user_event.user.data2 = NULL;
    net_user_event = &__net_user_event;

    SDL_zero(__anim_user_event);
    __anim_user_event.type = user_event_type;
    __anim_user_event.user.code = EVENT_ERROR;
    __anim_user_event.user.data1 = NULL;
    __anim_user_event.user.data2 = NULL;
    anim_user_event = &__anim_user_event;

    SDL_zero(__info_user_event);
    __info_user_event.type = user_event_type;
    __info_user_event.user.code = SHOW_WIDGET_INFO_LABEL;
    __info_user_event.user.data1 = NULL;
    __info_user_event.user.data2 = NULL;
    info_user_event = &__info_user_event;

    SDL_zero(__flush_user_event);
    __flush_user_event.type = user_event_type;
    __flush_user_event.user.code = FLUSH;
    __flush_user_event.user.data1 = NULL;
    __flush_user_event.user.data2 = NULL;
    flush_user_event = &__flush_user_event;

    SDL_zero(__map_scroll_user_event);
    __map_scroll_user_event.type = user_event_type;
    __map_scroll_user_event.user.code = MAP_SCROLL;
    __map_scroll_user_event.user.data1 = NULL;
    __map_scroll_user_event.user.data2 = NULL;
    map_scroll_user_event = &__map_scroll_user_event;

    is_unit_move_blocked = FALSE;

    sdl2_client_flags |= (CF_DRAW_PLAYERS_NEUTRAL_STATUS
                          |CF_DRAW_PLAYERS_WAR_STATUS
                          |CF_DRAW_PLAYERS_CEASEFIRE_STATUS
                          |CF_DRAW_PLAYERS_PEACE_STATUS
                          |CF_DRAW_PLAYERS_ALLIANCE_STATUS);

    tileset_init(tileset);
    tileset_load_tiles(tileset);
    tileset_use_preferred_theme(tileset);

    load_cursors();

    callbacks = callback_list_new();

    diplomacy_dialog_init();
    intel_dialog_init();

    clear_double_messages_call();

    setup_auxiliary_tech_icons();

    /* This needs correct main_data.screen size */
    init_mapcanvas_and_overview();

    set_client_state(C_S_DISCONNECTED);

    /* Main game loop */
    gui_event_loop(NULL, NULL, main_key_down_handler, main_key_up_handler, NULL,
                   main_finger_down_handler, main_finger_up_handler, NULL,
                   main_mouse_button_down_handler, main_mouse_button_up_handler,
                   main_mouse_motion_handler);
    start_quitting();

#if defined UNDER_CE && defined GUI_SDL2_SMALL_SCREEN
    /* Change back to window mode to restore the title bar */
    set_video_mode(320, 240, SDL_SWSURFACE | SDL_ANYFORMAT);
#endif

    city_map_canvas_free();

    free_mapcanvas_and_overview();

    free_auxiliary_tech_icons();

    diplomacy_dialog_done();
    intel_dialog_done();

    callback_list_destroy(callbacks);
    callbacks = NULL;

    unload_cursors();

    free(button_behavior.event);
    button_behavior.event = NULL;

    meswin_dialog_popdown();

    del_main_list();

    free_font_system();
    theme_free(active_theme);
    active_theme = NULL;

    quit_sdl();
  }

  return EXIT_SUCCESS;
}

/**********************************************************************//**
  Do any necessary UI-specific cleanup
**************************************************************************/
void ui_exit(void)
{
}

/**********************************************************************//**
  Return our GUI type
**************************************************************************/
enum gui_type get_gui_type(void)
{
  return GUI_SDL2;
}

/**********************************************************************//**
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void sound_bell(void)
{
  log_debug("sound_bell : PORT ME");
}

/**********************************************************************//**
  Show Focused Unit Animation.
**************************************************************************/
void enable_focus_animation(void)
{
  anim_user_event->user.code = ANIM;
  sdl2_client_flags |= CF_FOCUS_ANIMATION;
}

/**********************************************************************//**
  Don't show Focused Unit Animation.
**************************************************************************/
void disable_focus_animation(void)
{
  sdl2_client_flags &= ~CF_FOCUS_ANIMATION;
}

/**********************************************************************//**
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.
**************************************************************************/
void add_net_input(int sock)
{
  log_debug("Connection UP (%d)", sock);
  net_socket = sock;
  autoconnect = FALSE;
  enable_focus_animation();
}

/**********************************************************************//**
  Stop waiting for any server network data.  See add_net_input().
**************************************************************************/
void remove_net_input(void)
{
  log_debug("Connection DOWN... ");
  net_socket = (-1);
  disable_focus_animation();
  draw_goto_patrol_lines = FALSE;
  update_mouse_cursor(CURSOR_DEFAULT);
}

/**********************************************************************//**
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
**************************************************************************/
void add_idle_callback(void (callback)(void *), void *data)
{
  if (callbacks != NULL) {
    struct callback *cb = fc_malloc(sizeof(*cb));

    cb->callback = callback;
    cb->data = data;

    callback_list_prepend(callbacks, cb);
  }
}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_tileset_changed(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_refresh(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_popup_properties(const struct tile_list *tiles, int objtype)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_popdown_all(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_notify_object_changed(int objtype, int object_id, bool removal)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void editgui_notify_object_created(int tag, int id)
{}

/**********************************************************************//**
  Updates a gui font style.
**************************************************************************/
void gui_update_font(const char *font_name, const char *font_value)
{
#define CHECK_FONT(client_font, action) \
  do { \
    if (strcmp(#client_font, font_name) == 0) { \
      char *end; \
      long size = strtol(font_value, &end, 10); \
      if (end && *end == '\0' && size > 0) { \
        *client_font_sizes[client_font] = size; \
        action; \
      } \
    } \
  } while (FALSE)

  CHECK_FONT(FONT_CITY_NAME, update_city_descriptions());
  CHECK_FONT(FONT_CITY_PROD, update_city_descriptions());
  /* FONT_REQTREE_TEXT not used yet */

#undef CHECK_FONT
}

/**********************************************************************//**
  Return default font size, from any source.
**************************************************************************/
unsigned default_font_size(struct theme *act_theme)
{
  return font_size_parameter > 0 ? font_size_parameter :
    (GUI_SDL_OPTION(use_theme_font_size) ? theme_default_font_size(act_theme) :
     GUI_SDL_OPTION(font_size));
}

/**********************************************************************//**
  Update font sizes based on theme.
**************************************************************************/
void update_font_from_theme(int theme_font_size)
{
  *client_font_sizes[FONT_CITY_NAME] = theme_font_size;
  *client_font_sizes[FONT_CITY_PROD] = theme_font_size;
}

/**********************************************************************//**
  Insert build information to help
**************************************************************************/
void insert_client_build_info(char *outbuf, size_t outlen)
{
  /* PORTME */
}

/**********************************************************************//**
  Queue a flush event to be handled later by SDL.
**************************************************************************/
bool flush_event(void)
{
  return SDL_PushEvent(flush_user_event) >= 0;
}

/**********************************************************************//**
  Define properties of this gui.
**************************************************************************/
void setup_gui_properties(void)
{
  gui_properties.animations = FALSE;
  gui_properties.views.isometric = TRUE;
  gui_properties.views.overhead = TRUE;
  gui_properties.views.d3 = FALSE;
}
