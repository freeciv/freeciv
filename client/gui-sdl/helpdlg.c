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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "genlist.h"
#include "government.h"

#include "gui_mem.h"

#include "shared.h"
#include "tech.h"
#include "unit.h"
#include "map.h"
#include "support.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "helpdata.h"
#include "tilespec.h"

#include "helpdlg.h"

#define TECH_TREE_DEPTH         20
#define TECH_TREE_EXPANDED_DEPTH 2

/**************************************************************************
  Popup the help dialog to get help on the given string topic.  Note that
  the toppic may appear in multiple sections of the help (it may be both
  an improvement and a unit, for example).

  The string will be untranslated.
**************************************************************************/
void popup_help_dialog_string(const char *item)
{
  popup_help_dialog_typed(_(item), HELP_ANY);
}

/**************************************************************************
  Popup the help dialog to display help on the given string topic from
  the given section.

  The string will be translated.
**************************************************************************/
void popup_help_dialog_typed(const char *item, enum help_page_type eHPT)
{
  freelog(LOG_DEBUG, "popup_help_dialog_typed : PORT ME");
}

/**************************************************************************
  Close the help dialog.
**************************************************************************/
void popdown_help_dialog(void)
{
  freelog(LOG_DEBUG, "popdown_help_dialog : PORT ME");
}
