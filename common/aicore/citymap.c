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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "city.h"
#include "game.h"
#ifdef DEBUG
#include "log.h"
#endif
#include "map.h"
#include "mem.h"
#include "support.h"
#include "unit.h"
#include "unittype.h"

#include "citymap.h"

/* CITYMAP - reserve space for cities
 *
 * The citymap is a large int double array that corresponds to
 * the freeciv main map. For each tile, it stores three different 
 * and exclusive values in a single int: A positive int tells you
 * how many cities can use this tile (a crowdedness inidicator). A 
 * value of zero indicates that the tile is presently unused and 
 * available. A negative value means that this tile is occupied 
 * and reserved by some city or unit: in this case the value gives
 * the negative of the ID of the city or unit that has reserved the
 * tile.
 *
 * Code that uses the citymap should modify its behaviour based on
 * positive values encountered, and never attempt to steal a tile
 * which has a negative value.
 */

static int *citymap;

#define LOG_CITYMAP LOG_DEBUG

/**************************************************************************
  Initialize citymap by reserving worked tiles and establishing the
  crowdedness of (virtual) cities.
**************************************************************************/
void citymap_turn_init(struct player *pplayer)
{
  /* The citymap is reinitialized at the start of ever turn.  This includes
   * a call to realloc, which only really matters if this is the first turn
   * of the game (but it's easier than a separate function to do this). */
  citymap = fc_realloc(citymap, MAX_MAP_INDEX * sizeof(*citymap));
  memset(citymap, 0, MAX_MAP_INDEX * sizeof(*citymap));

  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->tile, ptile) {
        if (ptile->worked) {
          citymap[ptile->index] = -(ptile->worked->id);
        } else {
	  citymap[ptile->index]++;
        }
      } map_city_radius_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_CITIES)
        && punit->ai.ai_role == AIUNIT_BUILD_CITY) {
      map_city_radius_iterate(punit->goto_tile, ptile) {
        if (citymap[ptile->index] >= 0) {
          citymap[ptile->index]++;
        }
      } map_city_radius_iterate_end;
      citymap[punit->goto_tile->index] = -(punit->id);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
  This function reserves a single tile for a (possibly virtual) city with
  a settler's or a city's id. Then it 'crowds' tiles that this city can 
  use to make them less attractive to other cities we may consider making.
**************************************************************************/
void citymap_reserve_city_spot(struct tile *ptile, int id)
{
#ifdef DEBUG
  freelog(LOG_CITYMAP, "id %d reserving (%d, %d), was %d", 
          id, TILE_XY(ptile), citymap[ptile->index]);
  assert(citymap[ptile->index] >= 0);
#endif

  /* Tiles will now be "reserved" by actual workers, so free excess
   * reservations. Also mark tiles for city overlapping, or 
   * 'crowding'. */
  map_city_radius_iterate(ptile, ptile1) {
    if (citymap[ptile1->index] == -id) {
      citymap[ptile1->index] = 0;
    }
    if (citymap[ptile1->index] >= 0) {
      citymap[ptile1->index]++;
    }
  } map_city_radius_iterate_end;
  citymap[ptile->index] = -(id);
}

/**************************************************************************
  Reverse any reservations we have made in the surrounding area.
**************************************************************************/
void citymap_free_city_spot(struct tile *ptile, int id)
{
  map_city_radius_iterate(ptile, ptile1) {
    if (citymap[ptile1->index] == -(id)) {
      citymap[ptile1->index] = 0;
    } else if (citymap[ptile1->index] > 0) {
      citymap[ptile1->index]--;
    }
  } map_city_radius_iterate_end;
}

/**************************************************************************
  Reserve additional tiles as desired (eg I would reserve best available
  food tile in addition to adjacent tiles)
**************************************************************************/
void citymap_reserve_tile(struct tile *ptile, int id)
{
#ifdef DEBUG
  assert(!citymap_is_reserved(ptile));
#endif

  citymap[ptile->index] = -id;
}

/**************************************************************************
  Returns a positive value if within a city radius, which is 1 x number of
  cities you are within the radius of, or zero or less if not. A negative
  value means this tile is reserved by a city and should not be taken.
**************************************************************************/
int citymap_read(struct tile *ptile)
{
  return citymap[ptile->index];
}

/**************************************************************************
  A tile is reserved if it contains a city or unit id, or a worker is
  assigned to it.
**************************************************************************/
bool citymap_is_reserved(struct tile *ptile)
{
  if (ptile->worked || ptile->city) {
    return TRUE;
  }
  return (citymap[ptile->index] < 0);
}
