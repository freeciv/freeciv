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
#ifndef __COLORS_H
#define __COLORS_H

#include <gtk/gtk.h>

#define MAX_COLORS 256

enum { 
  COLOR_STD_BLACK, COLOR_STD_WHITE, COLOR_STD_RED,
  COLOR_STD_YELLOW, COLOR_STD_CYAN,
  COLOR_STD_GROUND, COLOR_STD_OCEAN,
  COLOR_STD_RACE0, COLOR_STD_RACE1, COLOR_STD_RACE2,
  COLOR_STD_RACE3, COLOR_STD_RACE4, COLOR_STD_RACE5,
  COLOR_STD_RACE6, COLOR_STD_RACE7, COLOR_STD_RACE8,
  COLOR_STD_RACE9, COLOR_STD_RACE10, COLOR_STD_RACE11,
  COLOR_STD_RACE12, COLOR_STD_RACE13,

  COLOR_STD_LAST
};


enum Display_color_type
{	BW_DISPLAY, GRAYSCALE_DISPLAY, COLOR_DISPLAY	};


enum Display_color_type	get_visual		(void);
void			init_color_system	(void);
void			color_error		(void);

GdkColor colors_standard		[COLOR_STD_LAST];

#endif

