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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL/SDL.h>

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "genlist.h"
#include "map.h"

#include "gui_mem.h"

#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "colors.h"
#include "dialogs.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "helpdlg.h"
#include "inputdlg.h"
#include "mapctrl.h"
#include "mapview.h"
#include "repodlgs.h"
#include "spaceship.h"
#include "tilespec.h"
#include "climisc.h"

#include "spaceshipdlg.h"

/**************************************************************************
  Refresh (update) the spaceship dialog for the given player.
**************************************************************************/
void refresh_spaceship_dialog(struct player *pplayer)
{
  freelog(LOG_DEBUG, "refresh_spaceship_dialog : PORT ME");
}

/**************************************************************************
  Popup (or raise) the spaceship dialog for the given player.
**************************************************************************/
void popup_spaceship_dialog(struct player *pplayer)
{
  freelog(LOG_DEBUG, "popup_spaceship_dialog : PORT ME");
}

/**************************************************************************
  Close the spaceship dialog for the given player.
**************************************************************************/
void popdown_spaceship_dialog(struct player *pplayer)
{
  freelog(LOG_DEBUG, "popdown_spaceship_dialog : PORT ME");
}
