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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "packets.h"
#include "nation.h"
#include "player.h"
#include "support.h"

#include "chatline.h"
#include "climisc.h"
#include "clinet.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "inteldlg.h"
#include "spaceshipdlg.h"
#include "colors.h"

#include "plrdlg.h"

/**************************************************************************
  Update all information in the player list dialog.
**************************************************************************/
void update_players_dialog(void)
{
  freelog(LOG_DEBUG, "update_players_dialog : PORT ME");
}

#ifdef UNUSED
/**************************************************************************
  Toggle/set updating of player dialog.
**************************************************************************/
void plrdlg_update_delay_set(bool delay)
{
  freelog(LOG_DEBUG, "plrdlg_update_delay_set : %d PORT ME", delay);
}
#endif

/**************************************************************************
  Popup (or raise) the player list dialog.
**************************************************************************/
void popup_players_dialog(void)
{
  freelog(LOG_DEBUG, "popup_players_dialog : PORT ME");
}
