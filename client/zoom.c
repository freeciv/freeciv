/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Team
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
#include <fc_config.h>
#endif

/* client */
#include "mapview_common.h"

#include "zoom.h"


float map_zoom = 1.0;
bool zoom_enabled = FALSE;

static float zoom_steps[] = {
  -1.0, 0.10, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, -1.0
};

/**************************************************************************
  Set map zoom level.
**************************************************************************/
void zoom_set(float new_zoom)
{
  zoom_enabled = TRUE;
  map_zoom = new_zoom;

  map_canvas_resized(mapview.width, mapview.height);
}

/**************************************************************************
  Set map zoom level to exactly one.
**************************************************************************/
void zoom_1_0(void)
{
  zoom_enabled = FALSE;
  map_zoom = 1.0;

  map_canvas_resized(mapview.width, mapview.height);
}

/**************************************************************************
  Zoom level one step up
**************************************************************************/
void zoom_step_up(void)
{
  int i;

  /* Even if below previous step, close enough is considered to be in
   * previous step so that change is not miniscule */
  for (i = 1 ;
       zoom_steps[i] < map_zoom * 1.05 && zoom_steps[i] > 0 ;
       i++ ) {
    /* empty */
  }

  if (zoom_steps[i] > 0) {
    if (zoom_steps[i] > 0.99 && zoom_steps[i] < 1.01) {
      zoom_1_0();
    } else {
      zoom_set(zoom_steps[i]);
    }
  }
}

/**************************************************************************
  Zoom level one step down
**************************************************************************/
void zoom_step_down(void)
{
  int i;

  /* Even if above previous step, close enough is considered to be in
   * previous step so that change is not miniscule */
  for (i = ARRAY_SIZE(zoom_steps) - 2 ;
       zoom_steps[i] * 1.05 > map_zoom && zoom_steps[i] > 0 ;
       i-- ) {
    /* empty */
  }

  if (zoom_steps[i] > 0) {
    if (zoom_steps[i] > 0.99 && zoom_steps[i] < 1.01) {
      zoom_1_0();
    } else {
      zoom_set(zoom_steps[i]);
    }
  }
}
