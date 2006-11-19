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

/**********************************************************************
                          gui_main.c  -  description
                             -------------------
    begin                : Sun Jun 30 2002
    copyright            : (C) 2002 by Rafał Bursig
    email                : Rafał Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include <SDL/SDL.h>

/* utility */
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"

/* common */
#include "unitlist.h"

/* client */
#include "civclient.h"
#include "clinet.h"
#include "tilespec.h"

/* gui-sdl */
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
#include "spaceshipdlg.h"
#include "widget.h"

#include "gui_main.h"

#define UNITS_TIMER_INTERVAL 128	/* milliseconds */
#define MAP_SCROLL_TIMER_INTERVAL 500

const char *client_string = "gui-sdl";

/* The real GUI character encoding is UTF-16 which is not supported by
 * fciconv code at this time. Conversion between UTF-8 and UTF-16 is done
 * in gui_iconv.c */
const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

Uint32 SDL_Client_Flags = 0;

bool gui_sdl_fullscreen = FALSE;

/* default screen resolution */
#ifdef SMALL_SCREEN
int gui_sdl_screen_width = 320;
int gui_sdl_screen_height = 240;
#else
int gui_sdl_screen_width = 640;
int gui_sdl_screen_height = 480;
#endif

Uint32 widget_info_counter = 0;
int MOVE_STEP_X = DEFAULT_MOVE_STEP;
int MOVE_STEP_Y = DEFAULT_MOVE_STEP;
extern bool draw_goto_patrol_lines;
SDL_Event *pFlush_User_Event = NULL;
bool is_unit_move_blocked;
bool LSHIFT;
bool RSHIFT;
bool LCTRL;
bool RCTRL;
bool LALT;
bool do_focus_animation = TRUE;
int city_names_font_size = 12;
int city_productions_font_size = 12;

/* ================================ Private ============================ */
static int net_socket = -1;
static bool autoconnect = FALSE;
static bool is_map_scrolling = FALSE;
static enum direction8 scroll_dir;

static struct mouse_button_behavior button_behavior;
  
static SDL_Event *pNet_User_Event = NULL;
static SDL_Event *pAnim_User_Event = NULL;
static SDL_Event *pInfo_User_Event = NULL;
static SDL_Event *pMap_Scroll_User_Event = NULL;

static void print_usage(const char *argv0);
static void parse_options(int argc, char **argv);
static int check_scroll_area(int x, int y);
      
enum USER_EVENT_ID {
  EVENT_ERROR = 0,
  NET = 1,
  ANIM = 2,
  TRY_AUTO_CONNECT = 3,
  SHOW_WIDGET_INFO_LABBEL = 4,
  FLUSH = 5,
  MAP_SCROLL = 6,
  EXIT_FROM_EVENT_LOOP = 7
};

client_option gui_options[] = {
  GEN_BOOL_OPTION(gui_sdl_fullscreen, N_("Full Screen"), 
                  N_("If this option is set the client will use the "
                     "whole screen area for drawing"),
                  COC_INTERFACE),
  GEN_INT_OPTION(gui_sdl_screen_width, N_("Screen width"),
                 N_("This option saves the width of the selected screen "
                    "resolution"),
                 COC_INTERFACE),
  GEN_INT_OPTION(gui_sdl_screen_height, N_("Screen height"),
                 N_("This option saves the height of the selected screen "
                    "resolution"),
                 COC_INTERFACE),
  GEN_STR_OPTION(gui_sdl_theme_name, N_("Theme"),
		 N_("By changing this option you change the active theme. "
		    "This is the same as using the -- --theme command-line "
		    "parameter."),
		 COC_GRAPHICS,
		 get_theme_list, themespec_reread_callback),
};

const int num_gui_options = ARRAY_SIZE(gui_options);

struct callback {
  void (*callback)(void *data);
  void *data;
};

#define SPECLIST_TAG callback
#define SPECLIST_TYPE struct callback
#include "speclist.h"

struct callback_list *callbacks;

/* =========================================================== */

/****************************************************************************
  Called by the tileset code to set the font size that should be used to
  draw the city names and productions.
****************************************************************************/
void set_city_names_font_sizes(int my_city_names_font_size,
			       int my_city_productions_font_size)
{
  city_names_font_size = my_city_names_font_size;
  city_productions_font_size = my_city_productions_font_size;
}

/**************************************************************************
  Print extra usage information, including one line help on each option,
  to stderr. 
**************************************************************************/
static void print_usage(const char *argv0)
{
  /* add client-specific usage information here */
  fc_fprintf(stderr, _("Report bugs to <%s>.\n"), BUG_EMAIL_ADDRESS);
  fc_fprintf(stderr,
	     _("  -f,  --fullscreen\tStart Client in Fulscreen mode\n"));
  fc_fprintf(stderr, _("  -e,  --eventthread\tInit Event Subsystem in "
		       "other thread (only Linux and BeOS)\n"));
  fc_fprintf(stderr, _("  -t,  --theme THEME\tUse GUI theme THEME\n"));
}

/**************************************************************************
 search for command line options. right now, it's just help
 semi-useless until we have options that aren't the same across all clients.
**************************************************************************/
static void parse_options(int argc, char **argv)
{
  int i = 1;
  char *option = NULL;
    
  while (i < argc) {
    if (is_option("--help", argv[i])) {
      print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    } else if (is_option("--fullscreen",argv[i])) {
      gui_sdl_fullscreen = TRUE;
    } else if (is_option("--eventthread",argv[i])) {
      /* init events in other thread ( only linux and BeOS ) */  
      SDL_InitSubSystem(SDL_INIT_EVENTTHREAD);
    } else if ((option = get_option_malloc("--theme", argv, &i, argc))) {
      sz_strlcpy(gui_sdl_theme_name, option);
    }
    i++;
  }
  
}

/**************************************************************************
...
**************************************************************************/
static Uint16 main_key_down_handler(SDL_keysym Key, void *pData)
{
  static struct widget *pWidget;
  if ((pWidget = MainWidgetListKeyScaner(Key)) != NULL) {
    return widget_pressed_action(pWidget);
  } else {
    if (RSHIFT && (Key.sym == SDLK_RETURN)) {
      /* input */
      popup_input_line();
    } else {
      if (map_event_handler(Key)
		&& get_client_state() == CLIENT_GAME_RUNNING_STATE) {
        switch (Key.sym) {
	  case SDLK_RETURN:
	  case SDLK_KP_ENTER:
	  {
	    struct unit *pUnit;
	    struct city *pCity;
	    if((pUnit = unit_list_get(get_units_in_focus(), 0)) != NULL && 
	      (pCity = pUnit->tile->city) != NULL &&
	      city_owner(pCity) == game.player_ptr) {
	      popup_city_dialog(pCity);
	    } else {
	      disable_focus_animation();
	      key_end_turn();
	    }
	  }
	  return ID_ERROR;

	  case SDLK_F1:
            popup_city_report_dialog(FALSE);
	  return ID_ERROR;
	    
	  case SDLK_F2:
	    popup_activeunits_report_dialog(FALSE);
	  return ID_ERROR;
	   
	  case SDLK_F7:
            send_report_request(REPORT_WONDERS_OF_THE_WORLD);
          return ID_ERROR;
	    
          case SDLK_F8:
            send_report_request(REPORT_TOP_5_CITIES);
          return ID_ERROR;
	    
	  case SDLK_F10:
            if(is_meswin_open()) {
              popdown_meswin_dialog();
            } else {
              popup_meswin_dialog(true);
            }
	    flush_dirty();
          return ID_ERROR;
	    	        
	  case SDLK_F11:
            send_report_request(REPORT_DEMOGRAPHIC);
          return ID_ERROR;
	    
	  case SDLK_F12:
            popup_spaceship_dialog(game.player_ptr);
          return ID_ERROR;
	  
	  default:
	  return ID_ERROR;
	}
      }
    }
  }
  
  return ID_ERROR;
}

/**************************************************************************
...
**************************************************************************/
static Uint16 main_key_up_handler(SDL_keysym Key, void *pData)
{
  if(pSellected_Widget) {
    unsellect_widget_action();
  }
  return ID_ERROR;
}

/**************************************************************************
...
**************************************************************************/
static Uint16 main_mouse_button_down_handler(SDL_MouseButtonEvent *pButtonEvent, void *pData)
{
  static struct widget *pWidget;
  if ((pWidget = MainWidgetListScaner(pButtonEvent->x, pButtonEvent->y)) != NULL) {
    return widget_pressed_action(pWidget);
  } else {
#ifdef UNDER_CE
    if (!check_scroll_area(pButtonEvent->x, pButtonEvent->y)) {
#endif        
    if (!button_behavior.button_down_ticks) {
      /* start counting */
      button_behavior.button_down_ticks = SDL_GetTicks();   
      *button_behavior.event = *pButtonEvent;
      button_behavior.hold_state = MB_HOLD_SHORT;
      button_behavior.ptile = canvas_pos_to_tile(pButtonEvent->x, pButtonEvent->y);
    }
#ifdef UNDER_CE
    }
#endif    
  }
  return ID_ERROR;
}

static Uint16 main_mouse_button_up_handler(SDL_MouseButtonEvent *pButtonEvent, void *pData)
{
  if (button_behavior.button_down_ticks /* button wasn't pressed over a widget */
     && !MainWidgetListScaner(pButtonEvent->x, pButtonEvent->y)) {
    *button_behavior.event = *pButtonEvent;
    button_up_on_map(&button_behavior);
  }

  button_behavior.button_down_ticks = 0;  
  
#ifdef UNDER_CE
  is_map_scrolling = FALSE;
#endif
  
  return ID_ERROR;
}

#define SCROLL_MAP_AREA		8

/**************************************************************************
...
**************************************************************************/
static Uint16 main_mouse_motion_handler(SDL_MouseMotionEvent *pMotionEvent, void *pData)
{
  static struct widget *pWidget;
  struct tile *ptile;

  /* stop evaluating button hold time when moving to another tile in medium
   * hold state or above */
  if (button_behavior.hold_state >= MB_HOLD_MEDIUM) {
    ptile = canvas_pos_to_tile(pMotionEvent->x, pMotionEvent->y);
    if ((ptile->x != button_behavior.ptile->x)
        || (ptile->y != button_behavior.ptile->y)) {
      button_behavior.button_down_ticks = 0;
    }
  }
  
  if(draw_goto_patrol_lines) {
    update_line(pMotionEvent->x, pMotionEvent->y);
  }
      
  if ((pWidget = MainWidgetListScaner(pMotionEvent->x, pMotionEvent->y)) != NULL) {
    update_mouse_cursor(CURSOR_DEFAULT);
    widget_sellected_action(pWidget);
  } else {
    if (pSellected_Widget) {
      unsellect_widget_action();
    } else {
      if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {
        handle_mouse_cursor(canvas_pos_to_tile(pMotionEvent->x, pMotionEvent->y));
#ifndef UNDER_CE
        check_scroll_area(pMotionEvent->x, pMotionEvent->y);
#endif          
      }
    }
  }

  draw_mouse_cursor();
  
  return ID_ERROR;
}

/**************************************************************************
 this is called every TIMER_INTERVAL milliseconds whilst we are in 
 gui_main_loop() (which is all of the time) TIMER_INTERVAL needs to be .5s
**************************************************************************/
static void update_button_hold_state(void)
{
  /* button pressed */
  if (button_behavior.button_down_ticks) {
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

static int check_scroll_area(int x, int y) {
  static SDL_Rect rect;
          
  rect.x = rect.y = 0;
  rect.w = SCROLL_MAP_AREA;
  rect.h = Main.map->h;

  if (is_in_rect_area(x, y, rect)) {
    is_map_scrolling = TRUE;
    if (scroll_dir != DIR8_WEST) {
      scroll_dir = DIR8_WEST;
    }
  } else {
    rect.x = Main.map->w - SCROLL_MAP_AREA;
    if (is_in_rect_area(x, y, rect)) {
      is_map_scrolling = TRUE;
      if (scroll_dir != DIR8_EAST) {
        scroll_dir = DIR8_EAST;
      }
    } else {
      rect.x = rect.y = 0;
      rect.w = Main.map->w;
      rect.h = SCROLL_MAP_AREA;
      if (is_in_rect_area(x, y, rect)) {
        is_map_scrolling = TRUE;
        if (scroll_dir != DIR8_NORTH) {
          scroll_dir = DIR8_NORTH;
        }
      } else {
        rect.y = Main.map->h - SCROLL_MAP_AREA;
        if (is_in_rect_area(x, y, rect)) {
          is_map_scrolling = TRUE;
   	  if (scroll_dir != DIR8_SOUTH) {
            scroll_dir = DIR8_SOUTH;
  	  }
        } else {
	  is_map_scrolling = FALSE;
	}
      } 
    }
  }
  
  return is_map_scrolling;
}

/* ============================ Public ========================== */

/**************************************************************************
...
**************************************************************************/
void force_exit_from_event_loop(void)
{
  SDL_Event Event;
  
  Event.type = SDL_USEREVENT;
  Event.user.code = EXIT_FROM_EVENT_LOOP;
  Event.user.data1 = NULL;
  Event.user.data2 = NULL;
  
  SDL_PushEvent(&Event);
  
}

/* This function may run in a separate event thread */
int FilterMouseMotionEvents(const SDL_Event *event)
{
  if (event->type == SDL_MOUSEMOTION) {
    static int x = 0, y = 0;
    if((MOVE_STEP_X && (event->motion.x - x > MOVE_STEP_X
      			|| x - event->motion.x > MOVE_STEP_X)) ||
      (MOVE_STEP_Y && (event->motion.y - y > MOVE_STEP_Y
    			|| y - event->motion.y > MOVE_STEP_Y))) {
      x = event->motion.x;
      y = event->motion.y;
      return(1);    /* Catch it */
    } else {
      return(0);    /* Drop it, we've handled it */
    }
  }
  return(1);
}

/**************************************************************************
...
**************************************************************************/
Uint16 gui_event_loop(void *pData,
	void (*loop_action)(void *pData),
	Uint16 (*key_down_handler)(SDL_keysym Key, void *pData),
        Uint16 (*key_up_handler)(SDL_keysym Key, void *pData),
	Uint16 (*mouse_button_down_handler)(SDL_MouseButtonEvent *pButtonEvent, void *pData),
        Uint16 (*mouse_button_up_handler)(SDL_MouseButtonEvent *pButtonEvent, void *pData),
        Uint16 (*mouse_motion_handler)(SDL_MouseMotionEvent *pMotionEvent, void *pData))
{
  Uint16 ID;
  static struct timeval tv;
  static fd_set civfdset;
  Uint32 t1, t2, t3;
  Uint32 real_timer_next_call;
  static int result, schot_nr = 0;
  static char schot[32];

  ID = ID_ERROR;
  t3 = t1 = real_timer_next_call = SDL_GetTicks();
  while (ID == ID_ERROR) {
    /* ========================================= */
    /* net check with 10ms delay event loop */
    if (net_socket >= 0) {
      FD_ZERO(&civfdset);
      FD_SET(net_socket, &civfdset);
      tv.tv_sec = 0;
      tv.tv_usec = 10000;/* 10ms*/
    
      result = select(net_socket + 1, &civfdset, NULL, NULL, &tv);
      if(result < 0) {
        if (errno != EINTR) {
	  break;
        } else {
	  continue;
        }
      } else {
        if (result > 0 && FD_ISSET(net_socket, &civfdset)) {
	  SDL_PushEvent(pNet_User_Event);
	}
      }
    } else { /* if connection is not establish */
      SDL_Delay(10);
    }
    /* ========================================= */
    
    t2 = SDL_GetTicks();
    
    if (t2 > real_timer_next_call) {
      real_timer_next_call = t2 + (real_timer_callback() * 1000);
    }
    
    if ((t2 - t1) > UNITS_TIMER_INTERVAL) {
      if (autoconnect) {
        widget_info_counter++;
        SDL_PushEvent(pAnim_User_Event);
      } else {
        SDL_PushEvent(pAnim_User_Event);
      }
            
      if (is_map_scrolling && (t2 - t3) > MAP_SCROLL_TIMER_INTERVAL) {
	SDL_PushEvent(pMap_Scroll_User_Event);
	t3 = SDL_GetTicks();
      }
      
      t1 = SDL_GetTicks();
    }

    if (widget_info_counter > 0) {
      SDL_PushEvent(pInfo_User_Event);
      widget_info_counter = 0;
    }
    
    /* ========================================= */
    
    if(loop_action) {
      loop_action(pData);
    }
    
    /* ========================================= */
    
    while(SDL_PollEvent(&Main.event) == 1) {
      switch (Main.event.type) {
      case SDL_QUIT:
	abort();
      break;
    
      case SDL_KEYUP:
        switch (Main.event.key.keysym.sym) {
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
	    if(key_up_handler) {
	      ID = key_up_handler(Main.event.key.keysym, pData);
	    }
	  break;
	}
        break;
      case SDL_KEYDOWN:
	switch(Main.event.key.keysym.sym) {
	  case SDLK_PRINT:
	    freelog(LOG_NORMAL, "Make screenshot nr. %d", schot_nr);
	    my_snprintf(schot, sizeof(schot), "schot0%d.bmp", schot_nr++);
	    SDL_SaveBMP(Main.screen, schot);
	  break;
	  
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
	    if(key_down_handler) {
	      ID = key_down_handler(Main.event.key.keysym, pData);
	    }
	  break;
	}
      break;
      case SDL_MOUSEBUTTONDOWN:
        if(mouse_button_down_handler) {
	  ID = mouse_button_down_handler(&Main.event.button, pData);
	}	
      break;
      case SDL_MOUSEBUTTONUP:
	if(mouse_button_up_handler) {
	  ID = mouse_button_up_handler(&Main.event.button, pData);
	}
      break;
      case SDL_MOUSEMOTION:
	if(mouse_motion_handler) {
	  ID = mouse_motion_handler(&Main.event.motion, pData);
	}	
      break;
      case SDL_USEREVENT:
        switch(Main.event.user.code) {
	  case NET:
            input_from_server(net_socket);
	  break;
	  case ANIM:
	    update_button_hold_state();
	    animate_mouse_cursor();
            draw_mouse_cursor();
	  break;
	  case SHOW_WIDGET_INFO_LABBEL:
	    draw_widget_info_label();
	  break;
	  case TRY_AUTO_CONNECT:
	    if (try_to_autoconnect()) {
	      pInfo_User_Event->user.code = SHOW_WIDGET_INFO_LABBEL;
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
	  default:
	  break;
        }    
      break;
	
      }
    }
    
    if (ID == ID_ERROR) {
      if (callbacks && callback_list_size(callbacks) > 0) {
        struct callback *cb = callback_list_get(callbacks, 0);
        callback_list_unlink(callbacks, cb);
        (cb->callback)(cb->data);
        free(cb);
      }
    }
  }
  
  return ID;
}

/* ============ Freeciv native game function =========== */

/**************************************************************************
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void ui_init(void)
{
  char device[20];
/*  struct widget *pInit_String = NULL;*/
  SDL_Surface *pBgd;
  Uint32 iSDL_Flags;

  button_behavior.button_down_ticks = 0;
  button_behavior.hold_state = MB_HOLD_SHORT;
  button_behavior.event = fc_calloc(1, sizeof(SDL_MouseButtonEvent));
  
  SDL_Client_Flags = 0;
  iSDL_Flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
  
  /* auto center new windows in X enviroment */
  putenv((char *)"SDL_VIDEO_CENTERED=yes");
  
  init_sdl(iSDL_Flags);
  
  freelog(LOG_NORMAL, _("Using Video Output: %s"),
	  SDL_VideoDriverName(device, sizeof(device)));
  
  /* create splash screen */  
  pBgd = adj_surf(load_surf(datafilename("misc/intro.png")));
  
  if(pBgd && SDL_GetVideoInfo()->wm_available) {
    set_video_mode(pBgd->w, pBgd->h, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
#if 0    
    /*
     * call this for other that X enviroments - currently not supported.
     */
    center_main_window_on_screen();
#endif
    alphablit(pBgd, NULL, Main.map, NULL);
    putframe(Main.map, 0, 0, Main.map->w - 1, Main.map->h - 1,
    			SDL_MapRGB(Main.map->format, 255, 255, 255));
    FREESURFACE(pBgd);
    SDL_WM_SetCaption("SDLClient of Freeciv", "FreeCiv");
  } else {
    
#ifndef SMALL_SCREEN
    set_video_mode(640, 480, SDL_SWSURFACE | SDL_ANYFORMAT);
#else
    set_video_mode(320, 240, SDL_SWSURFACE | SDL_ANYFORMAT);
#endif    
    
    if(pBgd) {
      blit_entire_src(pBgd, Main.map, (Main.map->w - pBgd->w) / 2,
    				      (Main.map->h - pBgd->h) / 2);
      FREESURFACE(pBgd);
    } else {
      SDL_FillRect(Main.map, NULL, SDL_MapRGB(Main.map->format, 0, 0, 128));
      SDL_WM_SetCaption("SDLClient of Freeciv", "FreeCiv");
    }
  }
  
#if 0
  /* create label beackground */
  pBgd = create_surf_alpha(adj_size(350), adj_size(50), SDL_SWSURFACE);
  
  SDL_FillRect(pBgd, NULL, SDL_MapRGBA(pBgd->format, 255, 255, 255, 128));
  putframe(pBgd, 0, 0, pBgd->w - 1, pBgd->h - 1, SDL_MapRGB(pBgd->format, 0, 0, 0));
 
  pInit_String = create_iconlabel(pBgd, Main.gui,
	create_str16_from_char(_("Initializing Client"), adj_font(20)),
				   WF_ICON_CENTER|WF_FREE_THEME);
  pInit_String->string16->style |= SF_CENTER;

  draw_label(pInit_String,
	     (Main.screen->w - pInit_String->size.w) / 2,
	     (Main.screen->h - pInit_String->size.h) / 2);

  flush_all();
  
  copy_chars_to_string16(pInit_String->string16,
  			_("Waiting for the beginning of the game"));

#endif    

  flush_all();
}

/**************************************************************************
...
**************************************************************************/
static void clear_double_messages_call(void)
{
  int i;
  /* clear double call */
  for(i=0; i<E_LAST; i++) {
    if (messages_where[i] & MW_MESSAGES)
    {
      messages_where[i] &= ~MW_OUTPUT;
    }
  }
}

/**************************************************************************
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  SDL_Event __Net_User_Event;
  SDL_Event __Anim_User_Event;
  SDL_Event __Info_User_Event;
  SDL_Event __Flush_User_Event;
  SDL_Event __pMap_Scroll_User_Event;
  
  parse_options(argc, argv);
  
  __Net_User_Event.type = SDL_USEREVENT;
  __Net_User_Event.user.code = NET;
  __Net_User_Event.user.data1 = NULL;
  __Net_User_Event.user.data2 = NULL;
  pNet_User_Event = &__Net_User_Event;

  __Anim_User_Event.type = SDL_USEREVENT;
  __Anim_User_Event.user.code = EVENT_ERROR;
  __Anim_User_Event.user.data1 = NULL;
  __Anim_User_Event.user.data2 = NULL;
  pAnim_User_Event = &__Anim_User_Event;
  
  __Info_User_Event.type = SDL_USEREVENT;
  __Info_User_Event.user.code = SHOW_WIDGET_INFO_LABBEL;
  __Info_User_Event.user.data1 = NULL;
  __Info_User_Event.user.data2 = NULL;
  pInfo_User_Event = &__Info_User_Event;

  __Flush_User_Event.type = SDL_USEREVENT;
  __Flush_User_Event.user.code = FLUSH;
  __Flush_User_Event.user.data1 = NULL;
  __Flush_User_Event.user.data2 = NULL;
  pFlush_User_Event = &__Flush_User_Event;

  __pMap_Scroll_User_Event.type = SDL_USEREVENT;
  __pMap_Scroll_User_Event.user.code = MAP_SCROLL;
  __pMap_Scroll_User_Event.user.data1 = NULL;
  __pMap_Scroll_User_Event.user.data2 = NULL;
  pMap_Scroll_User_Event = &__pMap_Scroll_User_Event;
  
  is_unit_move_blocked = FALSE;
  
  SDL_Client_Flags |= (CF_DRAW_PLAYERS_NEUTRAL_STATUS|
  		       CF_DRAW_PLAYERS_WAR_STATUS|
                       CF_DRAW_PLAYERS_CEASEFIRE_STATUS|
                       CF_DRAW_PLAYERS_PEACE_STATUS|
                       CF_DRAW_PLAYERS_ALLIANCE_STATUS);
                       
  tileset_load_tiles(tileset);
  tileset_use_prefered_theme(tileset);
      
  load_cursors();  

  callbacks = callback_list_new();
  
  diplomacy_dialog_init();
  intel_dialog_init();

  clear_double_messages_call();
    
  setup_auxiliary_tech_icons();
  
  if (gui_sdl_fullscreen) {
    #ifdef SMALL_SCREEN
      #ifdef UNDER_CE
        /* set 320x240 fullscreen */
        set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
                       SDL_SWSURFACE | SDL_ANYFORMAT | SDL_FULLSCREEN);
      #else
        /* small screen on desktop -> don't set 320x240 fullscreen mode */
        set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
                       SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
      #endif
    #else
      set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
                     SDL_SWSURFACE | SDL_ANYFORMAT | SDL_FULLSCREEN);
    #endif
    
  } else {
    
    #ifdef SMALL_SCREEN
      #ifdef UNDER_CE    
      set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
                     SDL_SWSURFACE | SDL_ANYFORMAT);
      #else
      set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
                     SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
      #endif
    #else
    set_video_mode(gui_sdl_screen_width, gui_sdl_screen_height,
      SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
    #endif
    
#if 0    
    /*
     * call this for other that X enviroments - currently not supported.
     */
    center_main_window_on_screen();
#endif
  }

  /* SDL_WM_SetCaption("SDLClient of Freeciv", "FreeCiv"); */

  /* this need correct Main.screen size */
  init_mapcanvas_and_overview();    
  
  set_client_state(CLIENT_PRE_GAME_STATE);

  /* Main game loop */
  gui_event_loop(NULL, NULL, main_key_down_handler, main_key_up_handler,
  		 main_mouse_button_down_handler, main_mouse_button_up_handler,
		 main_mouse_motion_handler);
                 
}

/**************************************************************************
  Do any necessary UI-specific cleanup
**************************************************************************/
void ui_exit()
{
  
#if defined UNDER_CE && defined SMALL_SCREEN
  /* change back to window mode to restore the title bar */
  set_video_mode(320, 240, SDL_SWSURFACE | SDL_ANYFORMAT);
#endif
  
  free_auxiliary_tech_icons();
  free_intro_radar_sprites();
  
  diplomacy_dialog_done();
  intel_dialog_done();  

  callback_list_unlink_all(callbacks);
  free(callbacks);
  
  unload_cursors();

  FC_FREE(button_behavior.event);

  popdown_meswin_dialog();
        
  del_main_list();
  
  free_font_system();
  theme_free(theme);

  quit_sdl();
}

/**************************************************************************
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void sound_bell(void)
{
  freelog(LOG_DEBUG, "sound_bell : PORT ME");
}

/**************************************************************************
  Show Focused Unit Animation.
**************************************************************************/
void enable_focus_animation(void)
{
  pAnim_User_Event->user.code = ANIM;
  SDL_Client_Flags |= CF_FOCUS_ANIMATION;
}

/**************************************************************************
  Don't show Focused Unit Animation.
**************************************************************************/
void disable_focus_animation(void)
{
  SDL_Client_Flags &= ~CF_FOCUS_ANIMATION;
}

/**************************************************************************
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.
**************************************************************************/
void add_net_input(int sock)
{
  freelog(LOG_DEBUG, "Connection UP (%d)", sock);
  net_socket = sock;
  autoconnect = FALSE;
  enable_focus_animation();
}

/**************************************************************************
  Stop waiting for any server network data.  See add_net_input().
**************************************************************************/
void remove_net_input(void)
{
  net_socket = (-1);
  freelog(LOG_DEBUG, "Connection DOWN... ");
  disable_focus_animation();
  draw_goto_patrol_lines = FALSE;
  update_mouse_cursor(CURSOR_DEFAULT);
}

/**************************************************************************
  Called to monitor a GGZ socket.
**************************************************************************/
void add_ggz_input(int sock)
{
  /* PORTME */
}

/**************************************************************************
  Called on disconnection to remove monitoring on the GGZ socket.  Only
  call this if we're actually in GGZ mode.
**************************************************************************/
void remove_ggz_input(void)
{
  /* PORTME */
}

/****************************************************************************
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
****************************************************************************/
void add_idle_callback(void (callback)(void *), void *data)
{
  struct callback *cb = fc_malloc(sizeof(*cb));

  cb->callback = callback;
  cb->data = data;

  callback_list_prepend(callbacks, cb);
}
