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
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

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


#include "repodlgs.h"

#include "gui_main.h"

/*#include "freeciv.ico"*/

#define TIMERS
#define THREADS

#ifndef THREADS
#define TICK_CALL
#define SOCKET_TIMER_INTERVAL	100
#endif

#ifdef THREADS
#include <SDL/SDL_thread.h>
#endif

#define UNITS_TIMER_INTERVAL 150	/* milliseconds */

const char *client_string = "gui-sdl";

enum USER_EVENT_ID {
  EVENT_ERROR = 0,
  NET = 1,
  ANIM = 2,
  TRY_AUTO_CONNECT = 3,
  SHOW_WIDGET_INFO_LABBEL = 4
};

Uint32 SDL_Client_Flags = 0;

#ifdef TIMERS
static SDL_TimerID game_timer_id;
static SDL_TimerID autoconnect_timer_id;
#endif

#ifdef THREADS
static SDL_Thread *pThread = NULL;
static SDL_sem *pNetLock = NULL;
#endif

static SDL_Event *pNet_User_Event = NULL;
static SDL_Event *pAnim_User_Event = NULL;
static SDL_Event *pInfo_User_Event = NULL;

Uint32 widget_info_cunter = 0;

char *pDataPath = NULL;

/* ================================ Private ============================ */
static void print_usage(const char *argv0);
static void parse_options(int argc, char **argv);

static void gui_main_loop(void);

static int net_socket = -1;

#ifndef THREADS
static Uint32 socket_timer(Uint32 interval, void *socket);
#endif

#ifdef TIMERS
static Uint32 game_timer_callback(Uint32 interval, void *param);
#endif

static int optiondlg_callback(struct GUI *p);

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
  Find path to Data directory.
**************************************************************************/
static void set_data_path(void)
{

  char *pStart = datafilename("freeciv.rc");
  char *pEnd = strstr(pStart, "freeciv.rc");
  size_t len = pEnd - pStart;

  pDataPath = CALLOC(len + 1, sizeof(char));

  memcpy(pDataPath, pStart, len);
}

/**************************************************************************
  Main SDL client loop.
**************************************************************************/
static void gui_main_loop(void)
{
  Uint16 ID = 0;
  int RSHIFT = 0;
  struct GUI *pWidget = NULL;

#ifdef TICK_CALL
  Uint32 t1 = 0, t2;
#endif

  int schot_nr = 0;
  char schot[32];
  
#ifdef THREADS
  pNetLock = SDL_CreateSemaphore(0);
#endif
  
  while (!ID) {
#ifdef TICK_CALL
    /* ========================================= */

    /*
     *      I use this metod becouse I can't debug when 
     *      use thread to call input_from_server(net_socket) function.
     *      In real game I return to thread !
     */
    t2 = SDL_GetTicks();
    if ((t2 - t1) > SOCKET_TIMER_INTERVAL) {
      socket_timer(0, NULL);	/* tmp */
      t1 = SDL_GetTicks();
    }
#endif
    /* ========================================= */

    SDL_WaitEvent(&Main.event);
    switch (Main.event.type) {
    case SDL_QUIT:
      return;

    case SDL_USEREVENT:
      switch(Main.event.user.code) {
	case NET:
          input_from_server(net_socket);
#ifdef THREADS    
          SDL_SemPost(pNetLock);
#endif
	break;
	case ANIM:
	  real_blink_active_unit();
	break;
	case SHOW_WIDGET_INFO_LABBEL:
	  draw_widget_info_label();
	break;
	case TRY_AUTO_CONNECT:
	  if (try_to_autoconnect()) {
#ifdef TIMERS
	    SDL_RemoveTimer(autoconnect_timer_id);
#endif
	    pInfo_User_Event->user.code = SHOW_WIDGET_INFO_LABBEL;
	    widget_info_cunter = 0;
	    break;
	  }
	  widget_info_cunter = 1;
	break;
	default:
	break;
      }    
    break;
    
    case SDL_KEYUP:
      /* find if Right Shift is released */
      if (Main.event.key.keysym.sym == SDLK_RSHIFT) {
	RSHIFT &= 0;
      }
      break;
    case SDL_KEYDOWN:

      pWidget = MainWidgetListKeyScaner(Main.event.key.keysym);

      if (pWidget) {

	ID = widget_pressed_action(pWidget);
	unsellect_widget_action();
      } else if ((RSHIFT) && (Main.event.key.keysym.sym == SDLK_RETURN)) {
	/* input */
	popup_input_line();
	
      } else {
	if (!(SDL_Client_Flags & CF_OPTION_OPEN) &&
	    (map_event_handler(Main.event.key.keysym))) {
	  switch (Main.event.key.keysym.sym) {

	  case SDLK_RETURN:
	  case SDLK_KP_ENTER:
	    key_end_turn();
	    break;

	  case SDLK_RSHIFT:
	    /* Right Shift is Pressed */
	    RSHIFT = 1;
	    break;
	  case SDLK_UP:
	  case SDLK_KP8:
	    key_unit_move(DIR8_NORTH);
	    break;

	  case SDLK_PAGEUP:
	  case SDLK_KP9:
	    key_unit_move(DIR8_NORTHEAST);
	    break;

	  case SDLK_RIGHT:
	  case SDLK_KP6:
	    key_unit_move(DIR8_EAST);
	    break;

	  case SDLK_PAGEDOWN:
	  case SDLK_KP3:
	    key_unit_move(DIR8_SOUTHEAST);
	    break;

	  case SDLK_DOWN:
	  case SDLK_KP2:
	    key_unit_move(DIR8_SOUTH);
	    break;

	  case SDLK_END:
	  case SDLK_KP1:
	    key_unit_move(DIR8_SOUTHWEST);
	    break;

	  case SDLK_LEFT:
	  case SDLK_KP4:
	    key_unit_move(DIR8_WEST);
	    break;

	  case SDLK_HOME:
	  case SDLK_KP7:
	    key_unit_move(DIR8_NORTHWEST);
	    break;

	  case SDLK_KP5:
	    advance_unit_focus();
	    break;

	  case SDLK_ESCAPE:
	    key_cancel_action();
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

    case SDL_MOUSEMOTION:

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
  
#ifdef THREADS
  SDL_DestroySemaphore(pNetLock);
  pNetLock = NULL;
#endif  
  /*del_main_list();*/
}

#ifndef THREADS
/**************************************************************************
  ...
**************************************************************************/
static Uint32 socket_timer(Uint32 interval, void *socket)
{
  struct timeval tv;
  fd_set civfdset;

  while (net_socket >= 0) {
    FD_ZERO(&civfdset);
    FD_SET(net_socket, &civfdset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(FD_SETSIZE, &civfdset, NULL, NULL, &tv)) {
      if (FD_ISSET(net_socket, &civfdset)) {
	input_from_server(net_socket);
      }
    } else {
      break;
    }
  }

  return interval;
}
#endif

#ifdef THREADS
/**************************************************************************
  ...
**************************************************************************/
static int socket_thread(void *socket)
{
  struct timeval tv;
  fd_set civfdset;
  int result;
  
  while (net_socket >= 0) {
    
    FD_ZERO(&civfdset);
    FD_SET(net_socket, &civfdset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    
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
	
	SDL_SemWait(pNetLock);

      }
    }
  } /* while */
  
  return 0;
}
#endif

#ifdef TIMERS
/**************************************************************************
 this is called every TIMER_INTERVAL milliseconds whilst we are in 
 gui_main_loop() (which is all of the time) TIMER_INTERVAL needs to be .5s
**************************************************************************/
static Uint32 game_timer_callback(Uint32 interval, void *param)
{
#if 0  
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

    real_blink_active_unit();

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
#else
  
  if(widget_info_cunter) {
    if(widget_info_cunter > 10) {
      SDL_PushEvent(pInfo_User_Event);
      widget_info_cunter = 0;
    } else {
      widget_info_cunter++;
      SDL_PushEvent(pAnim_User_Event);
    }
  } else {
    SDL_PushEvent(pAnim_User_Event);
  }

#endif
    
  return interval;
}

static Uint32 try_autoconnect_timer_callback(Uint32 interval, void *param)
{
  if(widget_info_cunter) {
    SDL_PushEvent(pInfo_User_Event);
    widget_info_cunter = 0;
  }
  return interval;
}

#endif

/**************************************************************************
  ...
**************************************************************************/
static int optiondlg_callback(struct GUI *pButton)
{
  set_wstate(pButton, WS_DISABLED);
  real_redraw_icon(pButton, Main.gui);
  flush_rect(pButton->size);

  popup_optiondlg();

  return -1;
}


void add_autoconnect_to_timer(void)
{
#ifdef TIMERS
  autoconnect_timer_id = SDL_AddTimer(AUTOCONNECT_INTERVAL,
				try_autoconnect_timer_callback , NULL );
#endif
  widget_info_cunter = 1;
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
  Uint32 iSDL_Flags = SDL_INIT_VIDEO, iScreenFlags = 0;

  iSDL_Flags |= SDL_INIT_NOPARACHUTE;
  
#ifdef TIMERS
  iSDL_Flags |= SDL_INIT_TIMER;
#endif

#ifdef THREADS  
  iSDL_Flags |= SDL_INIT_EVENTTHREAD;
#endif
  
  init_sdl(iSDL_Flags);
  
  freelog(LOG_NORMAL, _("Using Video Output: %s"),
	  SDL_VideoDriverName(device, sizeof(device)));

  set_data_path();

#if 1
  iScreenFlags = SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE;
#else  
  iScreenFlags = SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN;
#endif
  
  set_video_mode(640, 480, iScreenFlags);

  SDL_WM_SetCaption( "SDLClient of Freeciv", "FreeCiv" );

  /* create label beackground */
  pTmp = create_surf(350, 50, SDL_SWSURFACE);
  pBgd = SDL_DisplayFormatAlpha(pTmp);
  FREESURFACE(pTmp);
  
  SDL_FillRect(pBgd, NULL, SDL_MapRGBA(pBgd->format, 255, 255, 255, 128));
  SDL_SetAlpha(pBgd, 0x0, 0x0);
  
  load_intro_gfx();

  draw_intro_gfx();

  pInit_String = create_themelabel(pBgd,
	create_str16_from_char(_("Initializing Client"), 20), 350, 50,
				   WF_FREE_THEME);

  draw_label(pInit_String,
	     (Main.screen->w - pInit_String->size.w) / 2,
	     (Main.screen->h - pInit_String->size.h) / 2);

  dirty_all();
  flush_dirty();
  
  FREE(pInit_String->string16->text);
  pInit_String->string16->text =
      convert_to_utf16(_("Waiting for the beginning of the game"));

  init_gui_list(ID_WAITING_LABEL, pInit_String);
  
}

/**************************************************************************
  The main loop for the UI.  This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
void ui_main(int argc, char *argv[])
{
  int i;
  SDL_Event __Net_User_Event;
  SDL_Event __Anim_User_Event;
  SDL_Event __Info_User_Event;
  
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
  
  smooth_move_unit_steps = 8;
  
  parse_options(argc, argv);

  tilespec_load_tiles();

  load_cursors();
  tilespec_setup_theme();
  
  tilespec_setup_city_icons();

  pSellected_Widget = create_themeicon(pTheme->Options_Icon,
				       (WF_WIDGET_HAS_INFO_LABEL |
					WF_DRAW_THEME_TRANSPARENT));

  pSellected_Widget->action = optiondlg_callback;
  pSellected_Widget->string16 = create_str16_from_char(_("Options"), 12);

  add_to_gui_list(ID_CLIENT_OPTIONS, pSellected_Widget);
  pSellected_Widget = NULL;

  /* clear double call */
  for(i=0; i<E_LAST; i++) {
    if (messages_where[i] & MW_MESSAGES)
    {
      messages_where[i] &= ~MW_OUTPUT;
    }
  }
  
  popup_meswin_dialog();
  
  set_output_window_text(_("SDLClient welcome you..."));

  set_output_window_text(_("Freeciv is free software and you are welcome "
			   "to distribute copies of "
			   "it under certain conditions;"));
  set_output_window_text(_("See the \"Copying\" item on the Help"
			   " menu."));
  set_output_window_text(_("Now.. Go give'em hell!"));

  Init_Input_Edit();

  Init_MapView();

  create_units_order_widgets();

  setup_auxiliary_tech_icons();

  SDL_FillRect(Main.gui,
	  &get_widget_pointer_form_main_list(ID_WAITING_LABEL)->size, 0x0 );
	  
  dirty_all();
  flush_dirty();

#ifdef TIMERS
  game_timer_id = SDL_AddTimer(UNITS_TIMER_INTERVAL,
					  game_timer_callback , NULL );
#endif


  set_client_state(CLIENT_PRE_GAME_STATE);

  gui_main_loop();
  
#ifdef TIMERS
  SDL_RemoveTimer(game_timer_id);
#endif

  free_auxiliary_tech_icons();
  tilespec_unload_theme();
  unload_cursors();

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

#if 0
void enable_turn_done_button(void)
{
  printf("enable_turn_done_button\n");
}
#endif

/**************************************************************************
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.
**************************************************************************/
void add_net_input(int sock)
{
  freelog(LOG_NORMAL, "Connection UP (%d)", sock);
  
  net_socket = sock;
  pAnim_User_Event->user.code = ANIM;
  
#ifdef THREADS
  
  pThread = SDL_CreateThread(socket_thread, NULL);
  
#endif

  
  return;
}

/**************************************************************************
  Stop waiting for any server network data.  See add_net_input().
**************************************************************************/
void remove_net_input(void)
{
  net_socket = (-1);
  freelog(LOG_DEBUG, "Connection DOWN... ");
  pAnim_User_Event->user.code = EVENT_ERROR;
#ifdef THREADS  
    SDL_WaitThread(pThread, NULL);
#endif
  
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
