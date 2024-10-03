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
                          gui_main.h  -  description
                             -------------------
    begin                : Sep 6 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
***********************************************************************/

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

/* SDL2 */
#ifdef SDL2_PLAIN_INCLUDE
#include <SDL.h>
#else  /* SDL2_PLAIN_INCLUDE */
#include <SDL2/SDL.h>
#endif /* SDL2_PLAIN_INCLUDE */

/* client/include */
#include "gui_main_g.h"

struct theme;

#define GUI_SDL_OPTION(optname) gui_options.gui_sdl2_##optname
#define GUI_SDL_OPTION_STR(optname) "gui_sdl2_" #optname

/* Enable this to adjust sizes for 320x240 resolution */
/* #define GUI_SDL2_SMALL_SCREEN */

/* SDL2 client Flags */
#define CF_NONE				0
#define CF_ORDERS_WIDGETS_CREATED	(1<<0)
#define CF_MAP_UNIT_W_CREATED		(1<<1)
#define CF_UNITINFO_SHOWN		(1<<2)
#define CF_OVERVIEW_SHOWN		(1<<3)
#define CF_GAME_JUST_STARTED		(1<<6)

#define CF_FOCUS_ANIMATION		(1<<9)
#define CF_CHANGED_PROD			(1<<10)
#define CF_CHANGED_CITY_NAME		(1<<11)
#define CF_CITY_STATUS_SPECIAL		(1<<12)
#define CF_CHANGE_TAXRATE_LUX_BLOCK	(1<<13)
#define CF_CHANGE_TAXRATE_SCI_BLOCK	(1<<14)
#define CF_DRAW_CITY_GRID		(1<<17)
#define CF_DRAW_CITY_WORKER_GRID	(1<<18)
#define CF_DRAW_PLAYERS_WAR_STATUS	(1<<19)
#define CF_DRAW_PLAYERS_CEASEFIRE_STATUS	(1<<20)
#define CF_DRAW_PLAYERS_PEACE_STATUS	(1<<21)
#define CF_DRAW_PLAYERS_ALLIANCE_STATUS	(1<<22)
#define CF_DRAW_PLAYERS_NEUTRAL_STATUS	(1<<23)
#define CF_SWRENDERER                   (1<<24)

/* Mouse button behavior */
#define MB_MEDIUM_HOLD_DELAY  500         /* Medium hold:  500ms */
#define MB_LONG_HOLD_DELAY   2000         /* Long hold:   2000ms */

/* Predicate for detecting basic widget activation events. */
#define PRESSED_EVENT(event) (                                              \
  (event).type == SDL_KEYDOWN                                               \
  || (event).type == SDL_FINGERDOWN                                         \
  || ((event).type == SDL_MOUSEBUTTONDOWN                                   \
      && (event).button.button == SDL_BUTTON_LEFT))

enum mouse_button_hold_state {
  MB_HOLD_SHORT,
  MB_HOLD_MEDIUM,
  MB_HOLD_LONG
};

struct finger_behavior {
    bool counting;
    Uint32 finger_down_ticks;
    enum mouse_button_hold_state hold_state;
    SDL_TouchFingerEvent event;
    struct tile *ptile;
};

struct mouse_button_behavior {
  bool counting;
  Uint32 button_down_ticks;
  enum mouse_button_hold_state hold_state;
  SDL_MouseButtonEvent *event;
  struct tile *ptile;
};

extern struct widget *selected_widget;
extern Uint32 sdl2_client_flags;
extern bool LSHIFT;
extern bool RSHIFT;
extern bool LCTRL;
extern bool RCTRL;
extern bool LALT;
extern int *client_font_sizes[]; /* indexed by enum client_font */

void force_exit_from_event_loop(void);
void enable_focus_animation(void);
void disable_focus_animation(void);


#define DEFAULT_MOVE_STEP 5
extern int MOVE_STEP_X, MOVE_STEP_Y;
int FilterMouseMotionEvents(void *data, SDL_Event *event);

Uint16 gui_event_loop(void *data, void (*loop_action)(void *data),
                      Uint16 (*key_down_handler)(SDL_Keysym key, void *data),
                      Uint16 (*key_up_handler)(SDL_Keysym key, void *data),
                      Uint16 (*textinput_handler)(const char *text, void *data),
                      Uint16 (*finger_down_handler)(SDL_TouchFingerEvent *touch_event, void *data),
                      Uint16 (*finger_up_handler)(SDL_TouchFingerEvent *touch_event, void *data),
                      Uint16 (*finger_motion_handler)(SDL_TouchFingerEvent *touch_event,
                                                      void *data),
                      Uint16 (*mouse_button_down_handler)(SDL_MouseButtonEvent *button_event,
                                                          void *data),
                      Uint16 (*mouse_button_up_handler)(SDL_MouseButtonEvent *button_event,
                                                        void *data),
                      Uint16 (*mouse_motion_handler)(SDL_MouseMotionEvent *motion_event,
                                                     void *data));

unsigned default_font_size(struct theme *act_theme);
void update_font_from_theme(int theme_font_size);

bool flush_event(void);

/* Shrink sizes for 320x240 screen */
#ifdef GUI_SDL2_SMALL_SCREEN
  #define adj_size(size) ((size) / 2)
#else
  #define adj_size(size) (size)
#endif

#endif /* FC__GUI_MAIN_H */
