/********************************************************************** 
 Freeciv - Copyright (C) 1996 - 2004 The Freeciv Project Team 
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
#include "temperature_map.h"
#include "utilities.h"

struct islands_data_type {
  Continent_id id;
  int size;
  int goodies;
  int starters;
  int total;
};
static struct islands_data_type *islands;
static int *islands_index;

/****************************************************************************
  Return an approximation of the goodness of a tile to a civilization.
****************************************************************************/
static int get_tile_value(struct tile *ptile)
{
  Terrain_type_id old_terrain;
  enum tile_special_type old_special;
  int value, irrig_bonus, mine_bonus;

  /* Give one point for each food / shield / trade produced. */
  value = (get_food_tile(ptile)
	   + get_shields_tile(ptile)
	   + get_trade_tile(ptile));

  old_terrain = ptile->terrain;
  old_special = ptile->special;

  map_set_special(ptile, S_ROAD);
  map_irrigate_tile(ptile);
  irrig_bonus = (get_food_tile(ptile)
		 + get_shields_tile(ptile)
		 + get_trade_tile(ptile)) - value;

  ptile->terrain = old_terrain;
  ptile->special = old_special;
  map_set_special(ptile, S_ROAD);
  map_mine_tile(ptile);
  mine_bonus = (get_food_tile(ptile)
		+ get_shields_tile(ptile)
		+ get_trade_tile(ptile)) - value;

  ptile->terrain = old_terrain;
  ptile->special = old_special;

  value += MAX(0, MAX(mine_bonus, irrig_bonus)) / 2;

  return value;
}

struct start_filter_data {
  int count;			/* Number of existing start positions. */
  int min_value;
  int *value;
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
static bool is_valid_start_pos(const struct tile *ptile, const void *dataptr)
{
  const struct start_filter_data *pdata = dataptr;
  int i;
  struct islands_data_type *island;
  int cont_size, cont = map_get_continent(ptile);

  /* Only start on certain terrain types. */  
  if (pdata->value[ptile->index] < pdata->min_value) {
      return FALSE;
  } 

  assert(cont > 0);
  if (islands[islands_index[cont]].starters == 0) {
    return FALSE;
  }

  /* Don't start on a hut. */
  if (map_has_special(ptile, S_HUT)) {
    return FALSE;
  }

  /* A longstanding bug allowed starting positions to exist on poles,
   * sometimes.  This hack prevents it by setting a fixed distance from
   * the pole (dependent on map temperature) that a start pos must be.
   * Cold and frozen tiles are not allowed for start pos placement. */
  if (tmap_is(ptile, TT_NHOT)) {
    return FALSE;
  }

  /* Don't start too close to someone else. */
  cont_size = get_continent_size(cont);
  island = islands + islands_index[cont];
  for (i = 0; i < pdata->count; i++) {
    struct tile *tile1 = map.start_positions[i].tile;

    if ((map_get_continent(ptile) == map_get_continent(tile1)
	 && (real_map_distance(ptile, tile1) * 1000 / pdata->min_value
	     <= (sqrt(cont_size / island->total))))
	|| (real_map_distance(ptile, tile1) * 1000 / pdata->min_value < 5)) {
      return FALSE;
    }
  }
  return TRUE;
}

/*************************************************************************
 help function for qsort
 *************************************************************************/
static int compare_islands(const void *A_, const void *B_)
{
  const struct islands_data_type *A = A_, *B = B_;

  return B->goodies - A->goodies;
}

/****************************************************************************
  Initialize islands data.
****************************************************************************/
static void initialize_isle_data(void)
{
  int nr;

  islands = fc_malloc((map.num_continents + 1) * sizeof(*islands));
  islands_index = fc_malloc((map.num_continents + 1)
			    * sizeof(*islands_index));

  /* islands[0] is unused. */
  for (nr = 1; nr <= map.num_continents; nr++) {
    islands[nr].id = nr;
    islands[nr].size = get_continent_size(nr);
    islands[nr].goodies = 0;
    islands[nr].starters = 0;
    islands[nr].total = 0;
  }
}

/****************************************************************************
  A function that filters for TER_STARTER tiles.
****************************************************************************/
static bool filter_starters(const struct tile *ptile, const void *data)
{
  return terrain_has_flag(map_get_terrain(ptile), TER_STARTER);
}

/**************************************************************************
  where do the different nations start on the map? well this function tries
  to spread them out on the different islands.

  MT_SIGLE: one player per isle
  MT_2or3: 2 players per isle (maybe one isle with 3)
  MT_ALL: all players in asingle isle
  MT_VARIABLE: at least 2 player per isle
  
  Returns true on success
**************************************************************************/
bool create_start_positions(enum start_mode mode)
{
  struct tile *ptile;
  int k, sum;
  struct start_filter_data data;
  int tile_value_aux[MAX_MAP_INDEX], tile_value[MAX_MAP_INDEX];
  int min_goodies_per_player = 2000;
  int total_goodies = 0;
  /* this is factor is used to maximize land used in extreme little maps */
  float efactor =  game.nplayers / map.size / 4; 
  bool failure = FALSE;
  bool is_tmap = temperature_is_initialized();

  if (!is_tmap) {
    /* The temperature map has already been destroyed by the time start
     * positions have been placed.  We check for this and then create a
     * false temperature map. This is used in the tmap_is() call above.
     * We don't create a "real" map here because that requires the height
     * map and other information which has already been destroyed. */
    create_tmap(FALSE);
  }

  /* Unsafe terrains separate continents, otherwise small areas of green
   * near the poles could be populated by a civilization if that pole
   * was part of a larger continent. */
  assign_continent_numbers(TRUE);

  /* If the default is given, just use MT_VARIABLE. */
  if (mode == MT_DEFAULT) {
    mode = MT_VARIABLE;
  }

  /* get the tile value */
  whole_map_iterate(ptile) {
    tile_value_aux[ptile->index] = get_tile_value(ptile);
  } whole_map_iterate_end;

  /* select the best tiles */
  whole_map_iterate(ptile) {
    int this_tile_value = tile_value_aux[ptile->index];
    int lcount = 0, bcount = 0;

    map_city_radius_iterate(ptile, ptile1) {
      if (this_tile_value > tile_value_aux[ptile1->index]) {
	lcount++;
      } else if (this_tile_value < tile_value_aux[ptile1->index]) {
	bcount++;
      }
    } map_city_radius_iterate_end;
    if (lcount <= bcount) {
      this_tile_value = 0;
    }
    tile_value[ptile->index] = 100 * this_tile_value;
  } whole_map_iterate_end;
  /* get an average value */
  smooth_int_map(tile_value, TRUE);

  initialize_isle_data();

  /* oceans are not good for starters; discard them */
  whole_map_iterate(ptile) {
    if (!filter_starters(ptile, NULL)) {
      tile_value[ptile->index] = 0;
    } else {
      islands[map_get_continent(ptile)].goodies += tile_value[ptile->index];
      total_goodies += tile_value[ptile->index];
    }
  } whole_map_iterate_end;

  /* evaluate the best places on the map */
  adjust_int_map_filtered(tile_value, 1000, NULL, filter_starters);
 
  /* Sort the islands so the best ones come first.  Note that islands[0] is
   * unused so we just skip it. */
  qsort(islands + 1, map.num_continents,
	sizeof(*islands), compare_islands);

  /* If we can't place starters according to the first choice, change the
   * choice. */
  if (mode == MT_SINGLE && map.num_continents < game.nplayers + 3) {
    mode = MT_2or3;
  }

  if (mode == MT_2or3 && map.num_continents < game.nplayers / 2 + 4) {
    mode = MT_VARIABLE;
  }

  if (mode == MT_ALL 
      && (islands[1].goodies < game.nplayers * min_goodies_per_player
	  || islands[1].goodies < total_goodies * (0.5 + 0.8 * efactor)
	  / (1 + efactor))) {
    mode = MT_VARIABLE;
  }

  /* the variable way is the last posibility */
  if (mode == MT_VARIABLE) {
    min_goodies_per_player = total_goodies * (0.65 + 0.8 * efactor) 
      / (1 + efactor)  / game.nplayers;
  }

  { 
    int nr, to_place = game.nplayers, first = 1;

    /* inizialize islands_index */
    for (nr = 1; nr <= map.num_continents; nr++) {
      islands_index[islands[nr].id] = nr;
    }

    /* searh for best first island for fairness */    
    if ((mode == MT_SINGLE) || (mode == MT_2or3)) {
      float var_goodies, best = HUGE_VAL;
      int num_islands
	= (mode == MT_SINGLE) ? game.nplayers : (game.nplayers / 2);

      for (nr = 1; nr <= 1 + map.num_continents - num_islands; nr++) {
	if (islands[nr + num_islands - 1].goodies < min_goodies_per_player) {
	  break;
	}
	var_goodies
	    = (islands[nr].goodies - islands[nr + num_islands - 1].goodies)
	    / (islands[nr + num_islands - 1].goodies);

	if (var_goodies < best * 0.9) {
	  best = var_goodies;
	  first = nr;
	}
      }
    }

    /* set starters per isle */
    if (mode == MT_ALL) {
      islands[1].starters = to_place;
      islands[1].total = to_place;
      to_place = 0;
    }
    for (nr = 1; nr <= map.num_continents; nr++) {
      if (mode == MT_SINGLE && to_place > 0 && nr >= first) {
	islands[nr].starters = 1;
	islands[nr].total = 1;
	to_place--;
      }
      if (mode == MT_2or3 && to_place > 0 && nr >= first) {
	islands[nr].starters = 2 + (nr == 1 ? (game.nplayers % 2) : 0);
	to_place -= islands[nr].total = islands[nr].starters;
      }

      if (mode == MT_VARIABLE && to_place > 0) {
	islands[nr].starters = MAX(1, islands[nr].goodies 
				   / min_goodies_per_player);
	to_place -= islands[nr].total = islands[nr].starters;
      }
    }
  }

  data.count = 0;
  data.value = tile_value;
  data.min_value = 900;
  sum = 0;
  for (k = 1; k <= map.num_continents; k++) {
    sum += islands[islands_index[k]].starters;
    if (islands[islands_index[k]].starters != 0) {
      freelog(LOG_VERBOSE, "starters on isle %i", k);
    }
  }
  assert(game.nplayers <= data.count + sum);

  /* now search for the best place and set start_positions */
  map.start_positions = fc_realloc(map.start_positions,
				   game.nplayers
				   * sizeof(*map.start_positions));
  while (data.count < game.nplayers) {
    if ((ptile = rand_map_pos_filtered(&data, is_valid_start_pos))) {
      islands[islands_index[(int) map_get_continent(ptile)]].starters--;
      map.start_positions[data.count].tile = ptile;
      map.start_positions[data.count].nation = NO_NATION_SELECTED;
      freelog(LOG_DEBUG,
	      "Adding %d,%d as starting position %d, %d goodies on islands.",
	      TILE_XY(ptile), data.count,
	      islands[islands_index[(int) map_get_continent(ptile)]].goodies);
      data.count++;

    } else {
      data.min_value *= 0.9;
      if (data.min_value <= 10) {
	freelog(LOG_ERROR,
	        _("The server appears to have gotten into an infinite loop "
	          "in the allocation of starting positions.\n"
	          "Maybe the numbers of players/ia is too much for this map.\n"
	          "Please report this bug at %s."), WEBSITE_URL);
	failure = TRUE;
	break;
      }
    }
  }
  map.num_start_positions = game.nplayers;

  free(islands);
  free(islands_index);
  islands = NULL;
  islands_index = NULL;

  if (!is_tmap) {
    destroy_tmap();
  }

  return !failure;
}
