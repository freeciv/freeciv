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
#include "fcintl.h"

#include "map.h"

#include "maphand.h"

#include "mapgen_topology.h"
#include "startpos.h"

struct isledata *islands;

/****************************************************************************
  Return an approximation of the goodness of a tile to a civilization.
****************************************************************************/
static int get_tile_value(int x, int y)
{
  struct tile *ptile = map_get_tile(x, y);
  Terrain_type_id old_terrain;
  enum tile_special_type old_special;
  int value, irrig_bonus, mine_bonus;

  /* Give one point for each food / shield / trade produced. */
  value = (get_food_tile(x, y)
	   + get_shields_tile(x, y)
	   + get_trade_tile(x, y));

  old_terrain = ptile->terrain;
  old_special = ptile->special;

  map_set_special(x, y, S_ROAD);
  map_irrigate_tile(x, y);
  irrig_bonus = (get_food_tile(x, y)
		 + get_shields_tile(x, y)
		 + get_trade_tile(x, y)) - value;

  ptile->terrain = old_terrain;
  ptile->special = old_special;
  map_set_special(x, y, S_ROAD);
  map_mine_tile(x, y);
  mine_bonus = (get_food_tile(x, y)
		 + get_shields_tile(x, y)
		 + get_trade_tile(x, y)) - value;

  ptile->terrain = old_terrain;
  ptile->special = old_special;

  value += MAX(0, MAX(mine_bonus, irrig_bonus)) / 2;

  return value;
}

/**************************************************************************
 Allocate islands array and fill in values.
 Note this is only used for map.generator <= 1 or >= 5, since others
 setups islands and starters explicitly.
**************************************************************************/
static void setup_isledata(void)
{
  int starters = 0;
  int min,  i;
  
  assert(map.num_continents > 0);
  
  /* allocate + 1 so can use continent number as index */
  islands = fc_calloc((map.num_continents + 1), sizeof(struct isledata));

  /* add up all the resources of the map */
  whole_map_iterate(x, y) {
    /* number of different continents seen from (x,y) */
    int seen_conts = 0;
    /* list of seen continents */
    Continent_id conts[CITY_TILES]; 
    int j;
    
    /* add tile's value to each continent that is within city 
     * radius distance */
    map_city_radius_iterate(x, y, x1, y1) {
      /* (x1,y1) is possible location of a future city which will
       * be able to get benefit of the tile (x,y) */
      if (is_ocean(map_get_terrain(x1, y1)) 
	  || map_colatitude(x1, y1) <= 2 * ICE_BASE_LEVEL) { 
	/* Not land, or too cold. */
        continue;
      }
      for (j = 0; j < seen_conts; j++) {
	if (map_get_continent(x1, y1) == conts[j]) {
          /* Continent of (x1,y1) is already in the list */
	  break;
        }
      }
      if (j >= seen_conts) { 
	/* we have not seen this continent yet */
	assert(seen_conts < CITY_TILES);
	conts[seen_conts] = map_get_continent(x1, y1);
	seen_conts++;
      }
    } map_city_radius_iterate_end;
    
    /* Now actually add the tile's value to all these continents */
    for (j = 0; j < seen_conts; j++) {
      islands[conts[j]].goodies += get_tile_value(x, y);
    }
  } whole_map_iterate_end;
  
  /* now divide the number of desired starting positions among
   * the continents so that the minimum number of resources per starting 
   * position is as large as possible */
  
  /* set minimum number of resources per starting position to be value of
   * the best continent */
  min = 0;
  for (i = 1; i <= map.num_continents; i++) {
    if (min < islands[i].goodies) {
      min = islands[i].goodies;
    }
  }
  
  /* place as many starting positions as possible with the current minumum
   * number of resources, if not enough are placed, decrease the minimum */
  while ((starters < game.nplayers) && (min > 0)) {
    int nextmin = 0;
    
    starters = 0;
    for (i = 1; i <= map.num_continents; i++) {
      int value = islands[i].goodies;
      
      starters += value / min;
      if (nextmin < (value / (value / min + 1))) {
        nextmin = value / (value / min + 1);
      }
    }
    
    freelog(LOG_VERBOSE,
	    "%d starting positions allocated with\n"
            "at least %d resouces per starting position; \n",
            starters, min);

    assert(nextmin < min);
    /* This choice of next min guarantees that there will be at least 
     * one more starter on one of the continents */
    min = nextmin;
  }
  
  if (min == 0) {
    freelog(LOG_VERBOSE,
            "If we continue some starting positions will have to have "
            "access to zero resources (as defined in get_tile_value). \n");
    freelog(LOG_FATAL,
            "Cannot create enough starting position and will abort.\n"
            "Please report this bug at " WEBSITE_URL);
    abort();
  } else {
    for (i = 1; i <= map.num_continents; i++) {
      islands[i].starters = islands[i].goodies / min;
    }
  }
}

struct start_filter_data {
  int count; /* Number of existing start positions. */
  int dist; /* Minimum distance between starting positions. */
};

/**************************************************************************
  Return TRUE if (x,y) is a good starting position.

  Bad places:
  - Islands with no room.
  - Non-suitable terrain;
  - On a hut;
  - Too close to another starter on the same continent:
    'dist' is too close (real_map_distance)
    'nr' is the number of other start positions in
    map.start_positions to check for too closeness.
**************************************************************************/
static bool is_valid_start_pos(int x, int y, void *dataptr)
{
  struct start_filter_data *data = dataptr;
  int i;
  Terrain_type_id t = map_get_terrain(x, y);

  if (is_ocean(map_get_terrain(x, y))) {
    return FALSE;
  }

  if (islands[(int)map_get_continent(x, y)].starters == 0) {
    return FALSE;
  }

  /* Only start on certain terrain types. */
  if (!terrain_has_flag(t, TER_STARTER)) {
    return FALSE;
  }
  
  /* Don't start on a hut. */
  if (map_has_special(x, y, S_HUT)) {
    return FALSE;
  }
  
  /* Nobody will start on the poles since they aren't valid terrain. */

  /* Don't start too close to someone else. */
  for (i = 0; i < data->count; i++) {
    int x1 = map.start_positions[i].x;
    int y1 = map.start_positions[i].y;

    if (map_get_continent(x, y) == map_get_continent(x1, y1)
	&& real_map_distance(x, y, x1, y1) < data->dist) {
      return FALSE;
    }
  }

  return TRUE;
}

/**************************************************************************
  where do the different races start on the map? well this function tries
  to spread them out on the different islands.
**************************************************************************/
void create_start_positions(void)
{
  int x, y, k, sum;
  struct start_filter_data data;
  
  if (!islands) {
    /* Isle data is already setup for generators 2, 3, and 4. */
    setup_isledata();
  }

  data.dist = MIN(40, MIN(map.xsize / 2, map.ysize / 2));

  data.count = 0;
  sum = 0;
  for (k = 1; k <= map.num_continents; k++) {
    sum += islands[k].starters;
    if (islands[k].starters != 0) {
      freelog(LOG_VERBOSE, "starters on isle %i", k);
    }
  }
  assert(game.nplayers <= data.count + sum);

  map.start_positions = fc_realloc(map.start_positions,
				   game.nplayers
				   * sizeof(*map.start_positions));
  while (data.count < game.nplayers) {
    if (rand_map_pos_filtered(&x, &y, &data, is_valid_start_pos)) {
      islands[(int)map_get_continent(x, y)].starters--;
      map.start_positions[data.count].x = x;
      map.start_positions[data.count].y = y;
      map.start_positions[data.count].nation = NO_NATION_SELECTED;
      freelog(LOG_DEBUG, "Adding %d,%d as starting position %d.",
	      x, y, data.count);
      data.count++;

    } else {
      
      data.dist--;
      if (data.dist == 0) {
	die(_("The server appears to have gotten into an infinite loop "
	      "in the allocation of starting positions, and will abort.\n"
	      "Maybe the numbers of players/ia is too much for this map.\n"
	      "Please report this bug at %s."), WEBSITE_URL);
      }
    }
  }
  map.num_start_positions = game.nplayers;

  free(islands);
  islands = NULL;
}
