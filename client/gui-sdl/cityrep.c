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
#include <SDL/SDL_ttf.h>

#include "city.h"
#include "fcintl.h"
#include "log.h"
#include "game.h"
#include "packets.h"
#include "shared.h"
#include "support.h"
#include "unit.h"

#include "chatline.h"
#include "citydlg.h"
#include "cityrepdata.h"
#include "clinet.h"
#include "graphics.h"
#include "gui_string.h"
#include "gui_stuff.h"

#include "gui_main.h"

#include "mapview.h"
#include "optiondlg.h"
#include "options.h"
#include "repodlgs.h"
#include "climisc.h"

#include "cityrep.h"
/*#include "cma_fec.h"*/

#define NEG_VAL(x)  ((x)<0 ? (x) : (-x))
#define CMA_NONE	(-1)
#define CMA_CUSTOM	(-2)

/**************************************************************************
  Pop up or brings forward the city report dialog.  It may or may not
  be modal.
**************************************************************************/
void popup_city_report_dialog(bool make_modal)
{
  freelog(LOG_DEBUG, "popup_city_report_dialog : PORT ME");
}

/**************************************************************************
  Update (refresh) the entire city report dialog.
**************************************************************************/
void city_report_dialog_update(void)
{
  freelog(LOG_DEBUG, "city_report_dialog_update : PORT ME");
}

/**************************************************************************
  Update the city report dialog for a single city.
**************************************************************************/
void city_report_dialog_update_city(struct city *pcity)
{
  freelog(LOG_DEBUG, "city_report_dialog_update_city : PORT ME");
}
