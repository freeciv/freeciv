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

#ifndef FC__MAPVIEW_COMMON_H
#define FC__MAPVIEW_COMMON_H

#include "colors_g.h"

void refresh_tile_mapcanvas(int x, int y, int write_to_screen);
enum color_std get_grid_color(int x1, int y1, int x2, int y2);

#endif /* FC__MAPVIEW_COMMON_H */
