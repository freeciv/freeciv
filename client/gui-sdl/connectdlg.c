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
                          connectdlg.c  -  description
                             -------------------
    begin                : Mon Jul 1 2002
    copyright            : (C) 2002 by Rafa³ Bursig
    email                : Rafa³ Bursig <bursig@poczta.fm>
 **********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
#include "support.h"
#include "version.h"

#include "civclient.h"
#include "chatline.h"
#include "clinet.h"
#include "colors.h"
#include "dialogs.h"

#include "gui_mem.h"

#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"

#include "gui_id.h"
#include "gui_main.h"

#include "connectdlg.h"

static SDL_TimerID autoconnect_timer_id;

static int connect_callback(struct GUI *pWidget);
static int convert_portnr_callback(struct GUI *pWidget);
static int convert_playername_callback(struct GUI *pWidget);
static int convert_servername_callback(struct GUI *pWidget);

/**************************************************************************
  ...
**************************************************************************/
static int connect_callback(struct GUI *pWidget)
{
  char errbuf[512];
  /*struct GUI *pTmp = NULL; */

  if (connect_to_server(player_name, server_host, server_port,
			errbuf, sizeof(errbuf)) != -1) {
    /* destroy connect dlg. widgets */
    del_widget_from_gui_list(pWidget);
    del_ID_from_gui_list(ID_META_SERVERS_BUTTON);
    del_ID_from_gui_list(ID_CANCEL_BUTTON);
    del_ID_from_gui_list(ID_PLAYER_NAME_EDIT);
    del_ID_from_gui_list(ID_SERVER_NAME_EDIT);
    del_ID_from_gui_list(ID_PORT_EDIT);

    /* setup Option Icon */
    pSellected_Widget =
	get_widget_pointer_form_main_list(ID_CLIENT_OPTIONS);
    pSellected_Widget->size.x = 5;
    pSellected_Widget->size.y = 5;
    set_wstate(pSellected_Widget, WS_NORMAL);
    clear_wflag(pSellected_Widget, WF_HIDDEN);

#ifdef UNUSED
    SDL_Delay(100);

    /* check game status */
    pTmp = get_widget_pointer_form_main_list(ID_WAITING_LABEL);

    if (pTmp) {			/* game not started */

      /* draw intro screen */
      draw_intro_gfx();

      /* set x,y to Options icon and draw to screen */
      draw_icon(pSellected_Widget, 5, 5);

      refresh_widget_background(pTmp);
      redraw_label(pTmp);

      refresh_fullscreen();
    }
#endif

    pSellected_Widget = NULL;

    return -1;
  } else {
    append_output_window(errbuf);

    /* button up */
    unsellect_widget_action();
    set_wstate(pWidget, WS_SELLECTED);
    pSellected_Widget = pWidget;
    redraw_tibutton(pWidget);
    refresh_rect(pWidget->size);
  }
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int meta_severs_callback(struct GUI *pWidget)
{
  return -1;
}



/**************************************************************************
  ...
**************************************************************************/
static int convert_playername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sz_strlcpy(player_name, tmp);
  FREE(tmp);
  return -1;
}

/**************************************************************************
...
**************************************************************************/
static int convert_servername_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sz_strlcpy(server_host, tmp);
  FREE(tmp);
  return -1;
}

/**************************************************************************
  ...
**************************************************************************/
static int convert_portnr_callback(struct GUI *pWidget)
{
  char *tmp = convert_to_chars(pWidget->string16->text);
  sscanf(tmp, "%d", &server_port);
  FREE(tmp);
  return -1;
}

/**************************************************************************
  Provide an interface for connecting to a FreeCiv server.
**************************************************************************/
void gui_server_connect(void)
{
  char pCharPort[6];
  struct GUI *pUser = NULL, *pServer = NULL, *pPort = NULL;
  SDL_String16 *pPlayer_name = NULL;
  SDL_String16 *pServer_name = NULL;
  SDL_String16 *pPort_nr = NULL;

  int start_x;			/* = 215; */
  int start_y = 185;

  int start_button_y;
  int start_button_connect_x;	/* = 165; */
  int start_button_meta_x;	/* = 270; */
  int start_button_cancel_x;	/* = 415; */

  pPlayer_name = create_str16_from_char(_("Player Name :"), 10);
  pPlayer_name->forecol.r = 255;
  pPlayer_name->forecol.g = 255;
  pPlayer_name->forecol.b = 255;
  pServer_name = create_str16_from_char(_("Freeciv Serwer :"), 10);
  pServer_name->forecol.r = 255;
  pServer_name->forecol.g = 255;
  pServer_name->forecol.b = 255;
  pPort_nr = create_str16_from_char(_("Port :"), 10);
  pPort_nr->forecol.r = 255;
  pPort_nr->forecol.g = 255;
  pPort_nr->forecol.b = 255;


  disable(ID_CLIENT_OPTIONS);

  /* ====================== INIT =============================== */
  my_snprintf(pCharPort, sizeof(pCharPort), "%d", server_port);

  add_to_gui_list(ID_CONNECT_BUTTON,
		  create_themeicon_button_from_chars(pTheme->OK_Icon,
						     _("Connect"), 14, 0));

  set_action(ID_CONNECT_BUTTON, connect_callback);
  enable(ID_CONNECT_BUTTON);

  add_to_gui_list(ID_META_SERVERS_BUTTON,
		  create_themeicon_button_from_chars(pTheme->META_Icon,
						     _("Metaserver"), 14,
						     0));

  set_action(ID_META_SERVERS_BUTTON, meta_severs_callback);
  /*enable( ID_META_SERVERS_BUTTON ); */

  add_to_gui_list(ID_CANCEL_BUTTON,
		  create_themeicon_button_from_chars(pTheme->CANCEL_Icon,
						     _("Cancel"), 14, 0));

  enable(ID_CANCEL_BUTTON);

  add_to_gui_list(ID_PLAYER_NAME_EDIT,
		  create_edit_from_chars(NULL, player_name, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT));

  set_action(ID_PLAYER_NAME_EDIT, convert_playername_callback);
  enable(ID_PLAYER_NAME_EDIT);

  add_to_gui_list(ID_SERVER_NAME_EDIT,
		  create_edit_from_chars(NULL, server_host, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT));

  set_action(ID_SERVER_NAME_EDIT, convert_servername_callback);
  enable(ID_SERVER_NAME_EDIT);

  add_to_gui_list(ID_PORT_EDIT,
		  create_edit_from_chars(NULL, pCharPort, 14, 210,
					 WF_DRAW_THEME_TRANSPARENT));

  set_action(ID_PORT_EDIT, convert_portnr_callback);
  enable(ID_PORT_EDIT);

  pUser = get_widget_pointer_form_main_list(ID_PLAYER_NAME_EDIT);
  pServer = get_widget_pointer_form_main_list(ID_SERVER_NAME_EDIT);
  pPort = get_widget_pointer_form_main_list(ID_PORT_EDIT);


  /*refresh_fullscreen(); */

  /* ==================== Draw first time ===================== */
  start_x = (Main.screen->w - pUser->size.w) / 2;
  start_y = (Main.screen->h - pServer->size.h) / 2 - 15 - pUser->size.h;

  write_text16(Main.screen, start_x + 5, start_y - 13, pPlayer_name);
  draw_edit(pUser, start_x, start_y);
  write_text16(Main.screen, start_x + 5, start_y - 13 + 15 +
	       pUser->size.h, pServer_name);
  draw_edit(pServer, start_x, start_y + 15 + pUser->size.h);
  write_text16(Main.screen, start_x + 5, start_y - 13 + 30 +
	       pUser->size.h + pServer->size.h, pPort_nr);
  draw_edit(pPort, start_x,
	    start_y + 30 + pUser->size.h + pServer->size.h);

  start_button_y = pPort->size.y + pPort->size.h + 80;

  start_button_meta_x = (Main.screen->w -
			 get_widget_pointer_form_main_list
			 (ID_META_SERVERS_BUTTON)->size.w) / 2;

  start_button_connect_x = start_button_meta_x -
      get_widget_pointer_form_main_list(ID_CONNECT_BUTTON)->size.w - 20;

  start_button_cancel_x = start_button_meta_x +
      get_widget_pointer_form_main_list(ID_META_SERVERS_BUTTON)->size.w +
      20;

  draw_tibutton(get_widget_pointer_form_main_list(ID_CONNECT_BUTTON),
		start_button_connect_x, start_button_y);

  draw_frame_around_widget(get_widget_pointer_form_main_list
			   (ID_CONNECT_BUTTON));

  draw_tibutton(get_widget_pointer_form_main_list(ID_META_SERVERS_BUTTON),
		start_button_meta_x, start_button_y);

  draw_frame_around_widget(get_widget_pointer_form_main_list
			   (ID_META_SERVERS_BUTTON));

  draw_tibutton(get_widget_pointer_form_main_list(ID_CANCEL_BUTTON),
		start_button_cancel_x, start_button_y);

  draw_frame_around_widget(get_widget_pointer_form_main_list
			   (ID_CANCEL_BUTTON));

  refresh_fullscreen();

  /* ==================== Free Local Var ===================== */
  FREESTRING16(pPlayer_name);
  FREESTRING16(pServer_name);
  FREESTRING16(pPort_nr);
}

/**************************************************************************
  Make an attempt to autoconnect to the server.  
**************************************************************************/
static Uint32 try_to_autoconnect(Uint32 interval, void *parm)
{
  char errbuf[512];
  static int count = 0;
  static int warning_shown = 0;

  count++;

  if (count >= MAX_AUTOCONNECT_ATTEMPTS) {
    freelog(LOG_FATAL,
	    _("Failed to contact server \"%s\" at port "
	      "%d as \"%s\" after %d attempts"),
	    server_host, server_port, player_name, count);

    exit(EXIT_FAILURE);
  }

  switch (try_to_connect(player_name, errbuf, sizeof(errbuf))) {
  case 0:			/* Success! 
				   if (autoconnect_timer_id)
				   SDL_RemoveTimer(autoconnect_timer_id); */
    return 0;			/*  Tells GTK not to call this
				   function again */
  case ECONNREFUSED:		/* Server not available (yet) */
    if (!warning_shown) {
      freelog(LOG_NORMAL, _("Connection to server refused. "
			    "Please start the server."));
      append_output_window(_("Connection to server refused. "
			     "Please start the server."));
      warning_shown = 1;
    }
    return interval;		/*  Tells GTK to keep calling this function */
  default:			/* All other errors are fatal */
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, errbuf);
    exit(EXIT_FAILURE);		/* Suppresses a gcc warning */
  }
  return interval;
}

/**************************************************************************
  Start trying to autoconnect to civserver.
  Calls get_server_address(), then arranges for
  autoconnect_callback(), which calls try_to_connect(), to be called
  roughly every AUTOCONNECT_INTERVAL milliseconds, until success,
  fatal error or user intervention.
**************************************************************************/
void server_autoconnect()
{
  char buf[512];

  my_snprintf(buf, sizeof(buf),
	      _("Auto-connecting to server \"%s\" at port %d "
		"as \"%s\" every %d.%d second(s) for %d times"),
	      server_host, server_port, player_name,
	      AUTOCONNECT_INTERVAL / 1000, AUTOCONNECT_INTERVAL % 1000,
	      MAX_AUTOCONNECT_ATTEMPTS);
  append_output_window(buf);

  if (get_server_address(server_host, server_port, buf, sizeof(buf)) < 0) {
    freelog(LOG_FATAL,
	    _("Error contacting server \"%s\" at port %d "
	      "as \"%s\":\n %s\n"),
	    server_host, server_port, player_name, buf);


    exit(EXIT_FAILURE);
  }
  if (try_to_autoconnect(AUTOCONNECT_INTERVAL, NULL)) {
    autoconnect_timer_id = SDL_AddTimer(AUTOCONNECT_INTERVAL,
					try_to_autoconnect, NULL);

  }
}
