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
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <SDL/SDL.h>

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "map.h"

#include "gui_mem.h"

#include "shared.h"
#include "support.h"
#include "version.h"

#include "gui_string.h"
#include "gui_stuff.h"		/* gui */
#include "gui_id.h"

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

#include "timing.h"

#include "helpdata.h"		/* boot_help_texts() */
#include "mapctrl.h"
#include "mapview.h"
#include "menu.h"
#include "optiondlg.h"
#include "options.h"
#include "spaceshipdlg.h"
#include "resources.h"
#include "tilespec.h"
#include "gui_tilespec.h"
#include "messagewin_g.h"
#include "citydlg.h"
#include "cityrep.h"

#include "repodlgs.h"

#include "gui_main.h"

/*#include "freeciv.ico"*/

#define UNITS_TIMER_INTERVAL 125	/* milliseconds */

const char *client_string = "gui-sdl";

enum USER_EVENT_ID {
  EVENT_ERROR = 0,
  NET = 1,
  ANIM = 2,
  TRY_AUTO_CONNECT = 3,
  SHOW_WIDGET_INFO_LABBEL = 4,
  FLUSH = 5
};

Uint32 SDL_Client_Flags = 0;
Uint32 widget_info_counter = 0;

extern bool draw_goto_patrol_lines;
SDL_Event *pFlush_User_Event = NULL;
bool is_unit_move_blocked;

/* ================================ Private ============================ */
static int net_socket = -1;
static bool autoconnect = FALSE;
static SDL_Event *pNet_User_Event = NULL;
static SDL_Event *pAnim_User_Event = NULL;
static SDL_Event *pInfo_User_Event = NULL;

static void print_usage(const char *argv0);
static void parse_options(int argc, char **argv);
static void game_focused_unit_anim(void);
static void gui_main_loop(void);

/* =========================================================== */

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
  Main SDL client loop.
**************************************************************************/
static void gui_main_loop(void)
{
  Uint16 ID = 0;
  bool RSHIFT = FALSE;
  struct GUI *pWidget = NULL;
  struct timeval tv;
  fd_set civfdset;
  int result;
  Uint32 t1 , t2;
  int schot_nr = 0;
  char schot[32];
  struct unit *pUnit;
  struct city *pCity;
    
  LSHIFT = FALSE;
  LCTRL = FALSE;
  LALT = FALSE;
  is_unit_move_blocked = FALSE;
  
  t1 = SDL_GetTicks();
  while (!ID) {
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
    if ((t2 - t1) > UNITS_TIMER_INTERVAL) {
      if(widget_info_counter || autoconnect) {
        if(widget_info_counter > 10) {
          SDL_PushEvent(pInfo_User_Event);
          widget_info_counter = 0;
        } else {
          widget_info_counter++;
          SDL_PushEvent(pAnim_User_Event);
        }
      } else {
        SDL_PushEvent(pAnim_User_Event);
      }
      t1 = SDL_GetTicks();
    }
  
    /* ========================================= */
    
    while(SDL_PollEvent(&Main.event) == 1) {
      switch (Main.event.type) {
      case SDL_QUIT:
        return;
    
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
	  case SDLK_LALT:
	    LALT = FALSE;
	  break;
	  default:
	  break;
	}
        break;
      case SDL_KEYDOWN:

        pWidget = MainWidgetListKeyScaner(Main.event.key.keysym);

        if (pWidget) {

	  ID = widget_pressed_action(pWidget);
	  unsellect_widget_action();
        } else if (RSHIFT && (Main.event.key.keysym.sym == SDLK_RETURN)) {
	  /* input */
	  popup_input_line();
	
        } else {
	  if (!(SDL_Client_Flags & CF_OPTION_OPEN) &&
	    (map_event_handler(Main.event.key.keysym))) {
	    switch (Main.event.key.keysym.sym) {

	    case SDLK_RETURN:
	    case SDLK_KP_ENTER:
	      if((pUnit = get_unit_in_focus()) != NULL && 
		(pCity = map_get_tile(pUnit->x, pUnit->y)->city) != NULL &&
	         city_owner(pCity) == game.player_ptr) {
		  popup_city_dialog(pCity, FALSE);
	      } else {
	        disable_focus_animation();
	        key_end_turn();
	      }
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
	    
	    case SDLK_LALT:
	      /* Left ALT is Pressed */
	      LALT = TRUE;
	    break;
	    
	    case SDLK_UP:
	    case SDLK_KP8:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_NORTH);
	      }
	    break;

	    case SDLK_PAGEUP:
	    case SDLK_KP9:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_NORTHEAST);
	      }
	    break;

	    case SDLK_RIGHT:
	    case SDLK_KP6:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_EAST);
	      }
	    break;

	    case SDLK_PAGEDOWN:
	    case SDLK_KP3:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_SOUTHEAST);
	      }
	    break;

	    case SDLK_DOWN:
	    case SDLK_KP2:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_SOUTH);
	      }
	    break;

	    case SDLK_END:
	    case SDLK_KP1:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_SOUTHWEST);
	      }
	    break;

	    case SDLK_LEFT:
	    case SDLK_KP4:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_WEST);
	      }
	    break;

	    case SDLK_HOME:
	    case SDLK_KP7:
	      if(!is_unit_move_blocked) {
	        key_unit_move(DIR8_NORTHWEST);
	      }
	    break;

	    case SDLK_KP5:
	      advance_unit_focus();
	    break;

	    case SDLK_ESCAPE:
	      key_cancel_action();
	      draw_goto_patrol_lines = FALSE;
	    break;

	    case SDLK_c:
	      request_center_focus_unit();
	    break;

	    case SDLK_PRINT:
	      freelog(LOG_NORMAL, "Make screenshot nr. %d", schot_nr);
	      my_snprintf(schot, sizeof(schot), "schot0%d.bmp",
			schot_nr++);
	      SDL_SaveBMP(Main.screen, schot);
	    break;

	    case SDLK_F2:
	      popup_activeunits_report_dialog(FALSE);
	    break;
	    
	    case SDLK_F1:
              popup_city_report_dialog(FALSE);
	    break;

	    case SDLK_F7:
              send_report_request(REPORT_WONDERS_OF_THE_WORLD);
            break;
	    
            case SDLK_F8:
              send_report_request(REPORT_TOP_5_CITIES);
            break;
	    
	    case SDLK_F11:
              send_report_request(REPORT_DEMOGRAPHIC);
            break;
	    
	    default:
	    break;
	  }
	}
      }
      break;

      case SDL_MOUSEBUTTONDOWN:

        pWidget = MainWidgetListScaner(&Main.event.motion);

        if (pWidget) {
	  ID = widget_pressed_action(pWidget);
        } else {
	  button_down_on_map(&Main.event.button);
        }
      break;

      case SDL_USEREVENT:
        switch(Main.event.user.code) {
	  case NET:
            input_from_server(net_socket);
	  break;
	  case ANIM:
	    game_focused_unit_anim();
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
	  default:
	  break;
        }    
      break;
      
      case SDL_MOUSEMOTION:

        if(draw_goto_patrol_lines) {
	  update_line(Main.event.motion.x, Main.event.motion.y);
        }
      
        pWidget = MainWidgetListScaner(&Main.event.motion);
        if (pWidget) {
	  widget_sellected_action(pWidget);
        } else {
	  unsellect_widget_action();
        }
        ID = 0;

      break;
      }
    }
  }
  
  /*del_main_list();*/
}

/**************************************************************************
 this is called every TIMER_INTERVAL milliseconds whilst we are in 
 gui_main_loop() (which is all of the time) TIMER_INTERVAL needs to be .5s
**************************************************************************/
static void game_focused_unit_anim(void)
{
  static int flip;

  if (get_client_state() == CLIENT_GAME_RUNNING_STATE) {

    if (game.player_ptr->is_connected && game.player_ptr->is_alive
	&& !game.player_ptr->turn_done) {
      int i, is_waiting, is_moving;

      for (i = 0, is_waiting = 0, is_moving = 0; i < game.nplayers; i++)
	if (game.players[i].is_alive && game.players[i].is_connected) {
	  if (game.players[i].turn_done) {
	    is_waiting++;
	  } else {
	    is_moving++;
	  }
	}

      if (is_moving == 1 && is_waiting) {
	update_turn_done_button(0);	/* stress the slow player! */
      }
    }

    if(is_isometric) {
      real_blink_active_unit();
    } else {
      blink_active_unit();
    }

    if (flip) {
      update_timeout_label();
      if (seconds_to_turndone > 0) {
	seconds_to_turndone--;
      } else {
	seconds_to_turndone = 0;
      }
    }

    flip = !flip;
  }
    
  return;
}


void add_autoconnect_to_timer(void)
{
  autoconnect = TRUE;
  pInfo_User_Event->user.code = TRY_AUTO_CONNECT;
}

/* ============ Freeciv native game function =========== */

/**************************************************************************
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void ui_init(void)
{
  char device[20];
  struct GUI *pInit_String = NULL;
  SDL_Surface *pBgd, *pTmp;
  Uint32 iSDL_Flags = SDL_INIT_VIDEO;

  iSDL_Flags |= SDL_INIT_NOPARACHUTE;
  
#if 0
 /* events in other thread ( only linux and BeOS ) */  
  iSDL_Flags |= SDL_INIT_EVENTTHREAD;
#endif
  
  /* auto center new windows in X enviroment */
  putenv((char *)"SDL_VIDEO_CENTERED=yes");
  
  init_sdl(iSDL_Flags);
  
  freelog(LOG_NORMAL, _("Using Video Output: %s"),
	  SDL_VideoDriverName(device, sizeof(device)));
    
  pBgd = load_surf(datafilename("misc/intro.png"));
  
  if(pBgd && SDL_GetVideoInfo()->wm_available) {
    set_video_mode(pBgd->w, pBgd->h, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
#if 0    
    /*
     * call this for other that X enviroments - currently not full supported.
     */
    center_main_window_on_screen();
#endif
    SDL_BlitSurface(pBgd, NULL, Main.map, NULL);
    putframe(Main.map, 0, 0, Main.map->w - 1, Main.map->h - 1,
    			SDL_MapRGB(Main.map->format, 255, 255, 255));
    FREESURFACE(pBgd);
    SDL_WM_SetCaption("SDLClient of Freeciv", "FreeCiv");
  } else {
    set_video_mode(640, 480, SDL_SWSURFACE | SDL_ANYFORMAT);
    SDL_FillRect(Main.map, NULL, SDL_MapRGB(Main.map->format, 0, 0, 128));   
  }

  /* create label beackground */
  pTmp = create_surf(350, 50, SDL_SWSURFACE);
  pBgd = SDL_DisplayFormatAlpha(pTmp);
  FREESURFACE(pTmp);
  
  SDL_FillRect(pBgd, NULL, SDL_MapRGBA(pBgd->format, 255, 255, 255, 128));
  putframe(pBgd, 0, 0, pBgd->w - 1, pBgd->h - 1, 0xFF000000);
  SDL_SetAlpha(pBgd, 0x0, 0x0);
 
  
  pInit_String = create_iconlabel(pBgd, Main.gui,
	create_str16_from_char(_("Initializing Client"), 20),
				   WF_ICON_CENTER|WF_FREE_THEME);
  draw_label(pInit_String,
	     (Main.screen->w - pInit_String->size.w) / 2,
	     (Main.screen->h - pInit_String->size.h) / 2);

  flush_all();
  
  FREE(pInit_String->string16->text);
  pInit_String->string16->text =
      convert_to_utf16(_("Waiting for the beginning of the game"));

  init_gui_list(ID_WAITING_LABEL, pInit_String);
  
}

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
  
  smooth_move_unit_steps = 8;
  update_city_text_in_refresh_tile = FALSE;
    
  parse_options(argc, argv);

  tilespec_load_tiles();
  
  load_cursors();
  tilespec_setup_theme();
  tilespec_setup_anim();
  tilespec_setup_city_icons();
  finish_loading_sprites();
  
  init_options_button();
  
  clear_double_messages_call();
    
  create_units_order_widgets();

  setup_auxiliary_tech_icons();
  
#if 1
  set_video_mode(640, 480, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE);
#if 0    
    /*
     * call this for other that X enviroments - currently not full supported.
     */
    center_main_window_on_screen();
#endif
#else  
  set_video_mode(800, 600, SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
#endif
    
  /* SDL_WM_SetCaption("SDLClient of Freeciv", "FreeCiv"); */
  
  draw_intro_gfx();

  mapview_canvas.tile_width = (mapview_canvas.width - 1)
	  / NORMAL_TILE_WIDTH + 1;
  mapview_canvas.tile_height = (mapview_canvas.height - 1)
	  / NORMAL_TILE_HEIGHT + 1;

  flush_all();

  /* this need correct Main.screen size */
  Init_Input_Edit();
  Init_MapView();


  set_client_state(CLIENT_PRE_GAME_STATE);

  gui_main_loop();

  free_auxiliary_tech_icons();
  tilespec_unload_theme();
  tilespec_free_anim();
  unload_cursors();
  free_font_system();
  
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
  rebuild_focus_anim_frames();
}

/**************************************************************************
  Don't show Focused Unit Animation.
**************************************************************************/
void disable_focus_animation(void)
{
  pAnim_User_Event->user.code = EVENT_ERROR;
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
}

/**************************************************************************
  Set one of the unit icons in the information area based on punit.
  NULL will be pased to clear the icon. idx==-1 will be passed to
  indicate this is the active unit, or idx in [0..num_units_below-1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void set_unit_icon(int idx, struct unit *punit)
{
  freelog(LOG_DEBUG, "set_unit_icon : PORT ME");
}

/**************************************************************************
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate and
  deactivate the arrow.
**************************************************************************/
void set_unit_icons_more_arrow(bool onoff)
{
  freelog(LOG_DEBUG, "set_unit_icons_more_arrow : PORT ME");
}

/**************************************************************************
 Update the connected users list at pregame state.
**************************************************************************/
void update_conn_list_dialog(void)
{
  /* PORTME */
}
