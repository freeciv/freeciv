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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "events.h"
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"

#include "chatline.h"
#include "citydlg.h"
#include "clinet.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "mapview.h"
#include "options.h"

#include "messagewin.h"


#define N_MSG_VIEW 24		/* max before scrolling happens */

#ifdef UNUSED
/**************************************************************************
  Turn off updating of message window
**************************************************************************/
void meswin_update_delay_on(void)
{
  freelog(LOG_DEBUG, "meswin_update_delay_on : PORT ME");
}

/**************************************************************************
  Turn on updating of message window
**************************************************************************/
void meswin_update_delay_off(void)
{
  /* dissconect_from_server call this */
  freelog(LOG_DEBUG, "meswin_update_delay_off : PORT ME");
}
#endif

/**************************************************************************
  Popup (or raise) the message dialog; typically triggered by F10.
**************************************************************************/
void popup_meswin_dialog(void)
{
  freelog(LOG_DEBUG, "popup_meswin_dialog : PORT ME");
}

/**************************************************************************
  Return whether the message dialog is open.
**************************************************************************/
bool is_meswin_open(void)
{
  return FALSE;
/*    return meswin_dialog_shell != NULL;*/
}

#ifdef UNUSED
/**************************************************************************
  ...
**************************************************************************/
void clear_notify_window(void)
{
  /* call by set_client_state(...) */
  freelog(LOG_DEBUG, "clear_notify_window : PORT ME");
}


/**************************************************************************
  ...
**************************************************************************/
void add_notify_window(struct packet_generic_message *packet)
{
  freelog(LOG_DEBUG, "add_notify_window : PORT ME");
}
#endif

/**************************************************************************
  Do the work of updating (populating) the message dialog.
**************************************************************************/
void real_update_meswin_dialog(void)
{
  freelog(LOG_DEBUG, "update_meswin_dialog : PORT ME");
}
