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
#ifndef FC__COLORS_H
#define FC__COLORS_H

#include "colors_g.h"

enum color_mui {
COLOR_MUI_TECHKNOWN = COLOR_STD_LAST,
COLOR_MUI_TECHREACHABLE, COLOR_MUI_TECHUNKNOWN,
COLOR_MUI_LAST };

extern long colors_rgb[COLOR_MUI_LAST];
extern long colors_pen[COLOR_MUI_LAST];

#define GetColorRGB(c)    colors_rgb[c]
#define GetColorPen(c)    colors_pen[c]

#endif  /* FC__COLORS_H */
