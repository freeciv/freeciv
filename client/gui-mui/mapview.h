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
#ifndef FC__MAPVIEW_H
#define FC__MAPVIEW_H

#include "mapview_g.h"

#include "graphics.h"

void create_line_at_mouse_pos(void);

/* Use of these wrapper functions is deprecated. */
#define get_canvas_xy(map_x, map_y, canvas_x, canvas_y) \
  tile_to_canvas_pos(canvas_x, canvas_y, map_x, map_y)

#endif  /* FC__MAPVIEW_H */
