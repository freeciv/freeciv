/**********************************************************************
 Freeciv - Copyright (C) 2003 - Per I. Mathisen
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__CITYMAP_H
#define FC__CITYMAP_H

extern int citymap[MAP_MAX_WIDTH * MAP_MAX_HEIGHT]; /* FIXME */

void citymap_turn_init(struct player *pplayer);
void citymap_reserve_city_spot(struct tile *ptile, int id);
void citymap_free_city_spot(struct tile *ptile, int id);
void citymap_reserve_tile(struct tile *ptile, int id);

/**************************************************************************
  Returns a positive value if within a city radius, which is 1 x number of
  cities you are within the radius of, or zero or less if not. A negative
  value means this tile is reserved by a city and should not be taken.
**************************************************************************/
static inline int citymap_read(struct tile *ptile)
{
  return citymap[ptile->index];
}

/**************************************************************************
  A tile is reserved if it contains a city or unit id, or a worker is
  assigned to it.
**************************************************************************/
static inline bool citymap_is_reserved(struct tile *ptile)
{
  if (ptile->worked || ptile->city) {
    return TRUE;
  }
  return (citymap[ptile->index] < 0);
}

#endif
