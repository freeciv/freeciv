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

#include "log.h"

#include "citydlg_common.h"
#include "tilespec.h"		/* for is_isometric */

/**************************************************************************
This converts a city coordinate position to citymap canvas coordinates
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
void city_pos_to_canvas_pos(int city_x, int city_y, int *canvas_x, int *canvas_y)
{
  if (is_isometric) {
    int diff_xy;

    /* The line at y=0 isometric has constant x+y=1(tiles) */
    diff_xy = (city_x + city_y) - (1);
    *canvas_y = diff_xy/2 * NORMAL_TILE_HEIGHT + (diff_xy%2) * (NORMAL_TILE_HEIGHT/2);

    /* The line at x=0 isometric has constant x-y=-3(tiles) */
    diff_xy = city_x - city_y;
    *canvas_x = (diff_xy + 3) * NORMAL_TILE_WIDTH/2;
  } else {
    *canvas_x = city_x * NORMAL_TILE_WIDTH;
    *canvas_y = city_y * NORMAL_TILE_HEIGHT;
  }
}

/**************************************************************************
This converts a citymap canvas position to a city coordinate position
(either isometric or overhead).  It should be in cityview.c instead.
**************************************************************************/
void canvas_pos_to_city_pos(int canvas_x, int canvas_y, int *map_x, int *map_y)
{
  if (is_isometric) {
    *map_x = -2;
    *map_y = 2;

    /* first find an equivalent position on the left side of the screen. */
    *map_x += canvas_x / NORMAL_TILE_WIDTH;
    *map_y -= canvas_x / NORMAL_TILE_WIDTH;
    canvas_x %= NORMAL_TILE_WIDTH;

    /* Then move op to the top corner. */
    *map_x += canvas_y / NORMAL_TILE_HEIGHT;
    *map_y += canvas_y / NORMAL_TILE_HEIGHT;
    canvas_y %= NORMAL_TILE_HEIGHT;

    assert(NORMAL_TILE_WIDTH == 2 * NORMAL_TILE_HEIGHT);
    canvas_y *= 2;		/* now we have a square. */
    if (canvas_x + canvas_y > NORMAL_TILE_WIDTH / 2)
      (*map_x)++;
    if (canvas_x + canvas_y > 3 * NORMAL_TILE_WIDTH / 2)
      (*map_x)++;
    if (canvas_x - canvas_y > NORMAL_TILE_WIDTH / 2)
      (*map_y)--;
    if (canvas_y - canvas_x > NORMAL_TILE_WIDTH / 2)
      (*map_y)++;
  } else {
    *map_x = canvas_x / NORMAL_TILE_WIDTH;
    *map_y = canvas_y / NORMAL_TILE_HEIGHT;
  }
  freelog(LOG_DEBUG, "canvas_pos_to_city_pos(pos=(%d,%d))=(%d,%d)", canvas_x, canvas_y, *map_x, *map_y);
}
