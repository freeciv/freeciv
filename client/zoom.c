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

/**************************************************************************
  Set map zoom level.
**************************************************************************/
void set_map_zoom(float new_zoom)
{
  map_zoom = new_zoom;

  map_canvas_resized(mapview.width, mapview.height);
}
