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
 * how many cities that can use it, a crowdedness indicator. A 
 * value of zero indicates that the tile is presently unused and 
 * available. A negative value means that this tile is occupied 
 * and reserved by some city or unit.
 *
 * Code that uses the citymap should modify its behaviour based on
 * positive values encountered, and never attempt to steal a tile
 * which has a negative value.
 */

int citymap[MAP_MAX_WIDTH * MAP_MAX_HEIGHT];

#define LOG_CITYMAP LOG_DEBUG

/**************************************************************************
  Initialize citymap by reserving worked tiles and establishing the
  crowdedness of (virtual) cities.
**************************************************************************/
void citymap_turn_init(struct player *pplayer)
{
  memset(citymap, 0, sizeof(citymap));
  players_iterate(pplayer) {
    city_list_iterate(pplayer->cities, pcity) {
      map_city_radius_iterate(pcity->tile, ptile) {
        if (ptile->worked) {
          citymap[ptile->index] = -(ptile->worked->id);
        } else {
          citymap[ptile->index] = citymap[ptile->index]++;
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
