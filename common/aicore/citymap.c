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

static int citymap[MAP_MAX_WIDTH][MAP_MAX_HEIGHT];

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
      map_city_radius_iterate(pcity->x, pcity->y, x1, y1) {
        struct tile *ptile = map_get_tile(x1, y1);

        if (ptile->worked) {
          citymap[x1][y1] = -(ptile->worked->id);
        } else {
          citymap[x1][y1] = citymap[x1][y1]++;
        }
      } map_city_radius_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;
  unit_list_iterate(pplayer->units, punit) {
    if (unit_flag(punit, F_CITIES)
        && punit->ai.ai_role == AIUNIT_BUILD_CITY) {
      map_city_radius_iterate(goto_dest_x(punit), goto_dest_y(punit), x1, y1) {
        if (citymap[x1][y1] >= 0) {
          citymap[x1][y1]++;
        }
      } map_city_radius_iterate_end;
      citymap[goto_dest_x(punit)][goto_dest_y(punit)] = -(punit->id);
    }
  } unit_list_iterate_end;
}

/**************************************************************************
  Returns a positive value if within a city radius, which is 1 x number of
  cities you are within the radius of, or zero or less if not. A negative
  value means this tile is reserved by a city and should not be taken.
**************************************************************************/
int citymap_read(int x, int y)
{
#ifdef DEBUG
  assert(is_normal_map_pos(x, y));
#endif

  return citymap[x][y];
}

/**************************************************************************
  A tile is reserved if it contains a city or unit id, or a worker is
  assigned to it.
**************************************************************************/
bool citymap_is_reserved(int x, int y)
{
  struct tile *ptile;

#ifdef DEBUG
  assert(is_normal_map_pos(x, y));
#endif

  ptile = map_get_tile(x, y);
  if (ptile->worked || ptile->city) {
    return TRUE;
  }
  return (citymap[x][y] < 0);
}

/**************************************************************************
  This function reserves a single tile for a (possibly virtual) city with
  a settler's or a city's id. Then it 'crowds' tiles that this city can 
  use to make them less attractive to other cities we may consider making.
**************************************************************************/
void citymap_reserve_city_spot(int x, int y, int id)
{
#ifdef DEBUG
  assert(is_normal_map_pos(x, y));
  freelog(LOG_CITYMAP, "id %d reserving (%d, %d), was %d", 
          id, x, y, citymap[x][y]);
  assert(citymap[x][y] >= 0);
#endif

  /* Tiles will now be "reserved" by actual workers, so free excess
   * reservations. Also mark tiles for city overlapping, or 
   * 'crowding'. */
  map_city_radius_iterate(x, y, x1, y1) {
    if (citymap[x1][y1] == -id) {
      citymap[x1][y1] = 0;
    }
    if (citymap[x1][y1] >= 0) {
      citymap[x1][y1]++;
    }
  } map_city_radius_iterate_end;
  citymap[x][y] = -(id);
}

/**************************************************************************
  Reverse any reservations we have made in the surrounding area.
**************************************************************************/
void citymap_free_city_spot(int x, int y, int id)
{
#ifdef DEBUG
  assert(is_normal_map_pos(x, y));
#endif

  map_city_radius_iterate(x, y, x1, y1) {
    if (citymap[x1][y1] == -(id)) {
      citymap[x1][y1] = 0;
    } else if (citymap[x1][y1] > 0) {
      citymap[x1][y1]--;
    }
  } map_city_radius_iterate_end;
}

/**************************************************************************
  Reserve additional tiles as desired (eg I would reserve best available
  food tile in addition to adjacent tiles)
**************************************************************************/
void citymap_reserve_tile(int x, int y, int id)
{
#ifdef DEBUG
  assert(is_normal_map_pos(x, y));
  assert(!citymap_is_reserved(x,y));
#endif

  citymap[x][y] = -id;
}
