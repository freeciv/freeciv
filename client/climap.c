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

#include "climap.h"

/***************************************************************
 pplayer is only used in the server
***************************************************************/
bool map_get_known(int x, int y, struct player *pplayer)
{
  return tile_get_known(x, y) != TILE_UNKNOWN;
}

/***************************************************************
 pplayer is only used in the server
***************************************************************/
unsigned short map_get_continent(int x, int y, struct player *pplayer)
{
  return map_get_tile(x, y)->continent;
}

/***************************************************************
 pplayer is only used in the server
***************************************************************/
void map_set_continent(int x, int y, struct player *pplayer, int val)
{
  map_get_tile(x, y)->continent = val;
}

/************************************************************************
 A tile's "known" field is used by the server to store whether _each_
 player knows the tile.  Client-side, it's used as an enum known_type
 to track whether the tile is known/fogged/unknown.

 Judicious use of this function also makes things very convenient for
 civworld, since it uses both client and server-style storage; since it
 uses the stock tilespec.c file, this function serves as a wrapper.

*************************************************************************/
enum known_type tile_get_known(int x, int y)
{
  return (enum known_type) map_get_tile(x, y)->known;
}
