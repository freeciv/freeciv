/********************************************************************** 
 Freeciv - Copyright (C) 2002 - The Freeciv Project
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

#include "map.h"
#include "shared.h"

#include "tilespec.h"           /* is_isometric */

#include "climap.h"

/************************************************************************
 A tile's "known" field is used by the server to store whether _each_
 player knows the tile.  Client-side, it's used as an enum known_type
 to track whether the tile is known/fogged/unknown.

 Judicious use of this function also makes things very convenient for
 civworld, since it uses both client and server-style storage; since it
 uses the stock tilespec.c file, this function serves as a wrapper.

*************************************************************************/
enum known_type tile_get_known(const struct tile *ptile)
{
  return (enum known_type) ptile->known;
}

/**************************************************************************
  Convert the given GUI direction into a map direction.

  GUI directions correspond to the current viewing interface, so that
  DIR8_NORTH is up on the mapview.  map directions correspond to the
  underlying map tiles, so that DIR8_NORTH means moving with a vector of
  (0,-1).  Neither necessarily corresponds to "north" on the underlying
  world (once iso-maps are possible).

  See also map_to_gui_dir().
**************************************************************************/
enum direction8 gui_to_map_dir(enum direction8 gui_dir)
{
  if (is_isometric) {
    return dir_ccw(gui_dir);
  } else {
    return gui_dir;
  }
}

/**************************************************************************
  Convert the given GUI direction into a map direction.

  See also gui_to_map_dir().
**************************************************************************/
enum direction8 map_to_gui_dir(enum direction8 map_dir)
{
  if (is_isometric) {
    return dir_cw(map_dir);
  } else {
    return map_dir;
  }
}
