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
#include <ctype.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "government.h"
#include "packets.h"
#include "player.h"
#include "shared.h"
#include "support.h"

#include "clinet.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "mapview.h"

#include "inteldlg.h"

/**************************************************************************
  Popup an intelligence dialog for the given player.
**************************************************************************/
void popup_intel_dialog(struct player *p)
{
  freelog(LOG_DEBUG, "popup_intel_dialog : PORT ME");
}
