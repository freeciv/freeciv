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

#include <SDL/SDL.h>

#include "city.h"
#include "fcintl.h"
#include "game.h"
#include "government.h"

#include "gui_mem.h"

#include "support.h"

#include "graphics.h"
#include "gui_main.h"
#include "gui_string.h"
#include "gui_stuff.h"
#include "happiness.h"
#include "tilespec.h"

/* semi-arbitrary number that controls the width of the happiness widget */
#define HAPPINESS_PIX_WIDTH 23

#define NUM_HAPPINESS_MODIFIERS 5

enum { CITIES, LUXURIES, BUILDINGS, UNITS, WONDERS };
