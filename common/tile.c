/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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

#include <assert.h>

#include "tile.h"

/****************************************************************************
  Return the player who owns this tile (or NULL if none).
****************************************************************************/
struct player *tile_get_owner(const struct tile *ptile)
{
  return ptile->owner;
}

/****************************************************************************
  Set the owner of a tile (may be NULL).
****************************************************************************/
void tile_set_owner(struct tile *ptile, struct player *owner)
{
  ptile->owner = owner;
}

/****************************************************************************
  Return the city on this tile (or NULL if none).
****************************************************************************/
struct city *tile_get_city(const struct tile *ptile)
{
  return ptile->city;
}

/****************************************************************************
  Set the city on the tile (may be NULL).
****************************************************************************/
void tile_set_city(struct tile *ptile, struct city *pcity)
{
  ptile->city = pcity;
}

/****************************************************************************
  Return the terrain ID of the tile.  Terrains are defined in the ruleset
  (see terrain.h).
****************************************************************************/
Terrain_type_id tile_get_terrain(const struct tile *ptile)
{
  return ptile->terrain;
}

/****************************************************************************
  Set the terrain ID of the tile.  See tile_get_terrain.
****************************************************************************/
void tile_set_terrain(struct tile *ptile, Terrain_type_id ter)
{
  ptile->terrain = ter;
}

/****************************************************************************
  Return the specials of the tile.  See terrain.h.

  Note that this returns a mask of _all_ the specials on the tile.  To
  check a specific special use tile_has_special.
****************************************************************************/
enum tile_special_type tile_get_special(const struct tile *ptile)
{
  return ptile->special;
}

/****************************************************************************
  Returns TRUE iff the given tile has the given special.
****************************************************************************/
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type special)
{
  return contains_special(ptile->special, special);
}

/****************************************************************************
  Add the given special or specials to the tile.

  Note that this does not erase any existing specials already on the tile
  (see tile_clear_special or tile_clear_all_specials for that).  Also note
  the special to be set may be a mask, so you can set more than one
  special at a time (but this is not recommended).
****************************************************************************/
void tile_set_special(struct tile *ptile, enum tile_special_type spe)
{
  ptile->special |= spe;
}

/****************************************************************************
  Clear the given special or specials from the tile.

  This function clears all the specials set in the 'spe' mask from the
  tile's set of specials.  All other specials are unaffected.
****************************************************************************/
void tile_clear_special(struct tile *ptile, enum tile_special_type spe)
{
  ptile->special &= ~spe;
}

/****************************************************************************
  Remove any and all specials from this tile.
****************************************************************************/
void tile_clear_all_specials(struct tile *ptile)
{
  assert((int)S_NO_SPECIAL == 0);
  ptile->special = S_NO_SPECIAL;
}

/****************************************************************************
  Return the continent ID of the tile.  Typically land has a positive
  continent number and ocean has a negative number; no tile should have
  a 0 continent number.
****************************************************************************/
Continent_id tile_get_continent(const struct tile *ptile)
{
  return ptile->continent;
}

/****************************************************************************
  Set the continent ID of the tile.  See tile_get_continent.
****************************************************************************/
void tile_set_continent(struct tile *ptile, Continent_id val)
{
  ptile->continent = val;
}
