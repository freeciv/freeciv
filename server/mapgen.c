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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "maphand.h" /* assign_continent_numbers(), MAP_NCONT */
#include "mem.h"
#include "rand.h"
#include "shared.h"
#include "srv_main.h"

#include "mapgen.h"

/* Provide a block to convert from native to map coordinates.  For instance
 *   do_in_map_pos(mx, my, xn, yn) {
 *     map_set_terrain(mx, my, T_OCEAN);
 *   } do_in_map_pos_end;
 * Note that changing the value of the map coordinates won't change the native
 * coordinates.
 */
#define do_in_map_pos(map_x, map_y, nat_x, nat_y)                           \
{                                                                           \
  int map_x, map_y;                                                         \
  NATIVE_TO_MAP_POS(&map_x, &map_y, nat_x, nat_y);                          \
  {                                                                         \

#define do_in_map_pos_end                                                   \
  }                                                                         \
}

/* Wrapper for easy access.  It's a macro so it can be a lvalue. */
#define hmap(x, y) (height_map[map_pos_to_index(x, y)])
#define hnat(x, y) (height_map[native_pos_to_index((x), (y))])
#define rmap(x, y) (river_map[map_pos_to_index(x, y)])
#define pmap(x, y) (placed_map[map_pos_to_index(x, y)])

static void make_huts(int number);
static void add_specials(int prob);
static void mapgenerator1(void);
static void mapgenerator2(void);
static void mapgenerator3(void);
static void mapgenerator4(void);
static void mapgenerator5(void);
static void smooth_map(void);
static void adjust_hmap(void);
static void adjust_terrain_param(void);

#define RIVERS_MAXTRIES 32767
enum river_map_type {RS_BLOCKED = 0, RS_RIVER = 1};

/* Array needed to mark tiles as blocked to prevent a river from
   falling into itself, and for storing rivers temporarly.
   A value of 1 means blocked.
   A value of 2 means river.                            -Erik Sigra */
static int *river_map;

/*
 * Height map information
 *
 *   height_map[] stores the height of each tile
 *   hmap_max_level is the maximum height (heights will range from
 *     [0,hmap_max_level).
 *   hmap_shore_level is the level of ocean.  Any tile at this height or
 *     above is land; anything below is ocean.
 *   hmap_mount_level is the level of mountains and hills.  Any tile above
 *     this height will usually be a mountain or hill.
 */
static int *height_map;
static const int hmap_max_level = 1000;
static int hmap_shore_level, hmap_mountain_level;

static int forests=0;

struct isledata {
  int goodies;
  int starters;
};
static struct isledata *islands;

/* this is the maximal temperature at equators returned by map_latitude */
#define MAX_TEMP 1000

/* An estimate of the linear (1-dimensional) size of the map. */
#define SQSIZE MAX(1, sqrt(map.xsize * map.ysize / 1000))

/* used to create the poles and for separating them.  In a
 * mercator projection map we don't want the poles to be too big. */
#define ICE_BASE_LEVEL						\
  ((!topo_has_flag(TF_WRAPX) || !topo_has_flag(TF_WRAPY))	\
   /* 5% for little maps; 2% for big ones */			\
   ? MAX_TEMP * (3 + 2 * SQSIZE) / (100 * SQSIZE)		\
   : 5 * MAX_TEMP / 100  /* 5% for all maps */)

/****************************************************************************
  Used to initialize an array 'a' of size 'size' with value 'val' in each
  element.
****************************************************************************/
#define INITIALIZE_ARRAY(array, size, value)				    \
  {									    \
    int _ini_index;							    \
    									    \
    for (_ini_index = 0; _ini_index < (size); _ini_index++) {		    \
      (array)[_ini_index] = (value);					    \
    }									    \
  }

/****************************************************************************
  It can be used to many things, placed terrains on lands, placed huts, etc
****************************************************************************/
static bool *placed_map;

/****************************************************************************
  Create a clean pmap
****************************************************************************/
static void create_placed_map(void)
{
 placed_map = fc_malloc (sizeof(bool) * MAX_MAP_INDEX);
 INITIALIZE_ARRAY(placed_map, MAX_MAP_INDEX, FALSE );
}

/**************************************************************************** 
  Free the pmap
****************************************************************************/
static void remove_placed_map(void)
{
  free(placed_map);
  placed_map = NULL;
}

/* Checks if land has not yet been placed on pmap at (x, y) */
#define not_placed(x, y) (!pmap((x), (y)))

/* set has placed or not placed position in the pmap */
#define map_set_placed(x, y) (pmap((x), (y)) = TRUE)
#define map_unset_placed(x, y) (pmap((x), (y)) = FALSE)

/****************************************************************************
  Returns the temperature of this map position.  This is a value in the
  range of 0 to MAX_TEMP (inclusive).
****************************************************************************/
static int map_temperature(int map_x, int map_y)
{
  double x, y;
  
  if (map.alltemperate) {
    /* An all-temperate map has "average" temperature everywhere.
     *
     * TODO: perhaps there should be a random temperature variation. */
    return MAX_TEMP / 2;
  }

  do_in_natural_pos(ntl_x, ntl_y, map_x, map_y) {
    if (!topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
      /* A FLAT (unwrapped) map 
       *
       * We assume this is a partial planetary map.  A polar zone is placed
       * at the top and the equator is at the bottom.  The user can specify
       * all-temperate to avoid this. */
      return MAX_TEMP * ntl_y / (NATURAL_HEIGHT - 1);
    }

    /* Otherwise a wrapping map is assumed to be a global planetary map. */

    /* we fold the map to get the base symetries
     *
     * ...... 
     * :c__c:
     * :____:
     * :____:
     * :c__c:
     * ......
     *
     * C are the corners.  In all cases the 4 corners have equal temperature.
     * So we fold the map over in both directions and determine
     * x and y vars in the range [0.0, 1.0].
     *
     * ...>x 
     * :C_
     * :__
     * V
     * y
     *
     * And now this is what we have - just one-quarter of the map.
     */
    x = ((ntl_x > (NATURAL_WIDTH / 2 - 1)
	 ? NATURAL_WIDTH - 1.0 - (double)ntl_x
	 : (double)ntl_x)
	 / (NATURAL_WIDTH / 2 - 1));
    y = ((ntl_y > (NATURAL_HEIGHT / 2 - 1)
	  ? NATURAL_HEIGHT - 1.0 - (double)ntl_y
	  : (double)ntl_y)
	 / (NATURAL_HEIGHT / 2 - 1));
  } do_in_natural_pos_end;

  if (topo_has_flag(TF_WRAPX) && !topo_has_flag(TF_WRAPY)) {
    /* In an Earth-like topology the polar zones are at north and south.
     * This is equivalent to a Mercator projection. */
    return MAX_TEMP * y;
  }
  
  if (!topo_has_flag(TF_WRAPX) && topo_has_flag(TF_WRAPY)) {
    /* In a Uranus-like topology the polar zones are at east and west.
     * This isn't really the way Uranus is; it's the way Earth would look
     * if you tilted your head sideways.  It's still a Mercator
     * projection. */
    return MAX_TEMP * x;
  }

  /* Otherwise we have a torus topology.  We set it up as an approximation
   * of a sphere with two circular polar zones and a square equatorial
   * zone.  In this case north and south are not constant directions on the
   * map because we have to use a more complicated (custom) projection.
   *
   * Generators 2 and 5 work best if the center of the map is free.  So
   * we want to set up the map with the poles (N,S) along the sides and the
   * equator (/,\) in between.
   *
   * ........
   * :\ NN /:
   * : \  / :
   * :S \/ S:
   * :S /\ S:
   * : /  \ :
   * :/ NN \:
   * ''''''''
   */

  /* Remember that we've already folded the map into fourths:
   *
   * ....
   * :\ N
   * : \ 
   * :S \
   *
   * Now flip it along the X direction to get this:
   *
   * ....
   * :N /
   * : / 
   * :/ S
   */
  x = 1.0 - x;

  /* Since the north and south poles are equivalent, we can fold along the
   * diagonal.  This leaves us with 1/8 of the map
   *
   * .....
   * :P /
   * : / 
   * :/
   *
   * where P is the polar regions and / is the equator. */
  if (x + y > 1.0) {
    x = 1.0 - x;
    y = 1.0 - y;
  }

  /* This projection makes poles with a shape of a quarter-circle along
   * "P" and the equator as a straight line along "/".
   *
   * This is explained more fully at
   * http://rt.freeciv.org/Ticket/Display.html?id=8624. */
  return MAX_TEMP * (1.5 * (x * x * y + x * y * y) 
		     - 0.5 * (x * x * x + y * y * y) 
		     + 1.5 * (x * x + y * y));
}

struct DataFilter {
  int T_min, T_max;
};

/****************************************************************************
  A filter function to be passed to rand_map_pos_filtered().  See
  rand_map_pos_temperature for more explanation.
****************************************************************************/
static bool temperature_filter(int map_x, int map_y, void *data)
{
  struct DataFilter *filter = data;
  const int T = map_temperature(map_x, map_y);

  return (T >= filter->T_min) && (T <= filter->T_max) 
	  && not_placed(map_x, map_y) ;
}

/****************************************************************************
  Return random map coordinates which have temperature within the selected
  range and which are not yet placed on pmap. Returns FALSE if there is no 
  such position.
****************************************************************************/
static bool rand_map_pos_temperature(int *map_x, int *map_y,
				     int T_min, int T_max)
{
  struct DataFilter filter;

  /* We could short-circuit the logic here (for instance, for non-temperate
   * requests on a temperate map).  However this is unnecessary and isn't
   * extensible.  So instead we just go straight to the rand_map_pos_filtered
   * call. */

  filter.T_min = T_min;
  filter.T_max = T_max;
  return rand_map_pos_filtered(map_x, map_y, &filter, temperature_filter);
}

/****************************************************************************
  Return TRUE if the map in a city radius is SINGULAR.  This is used to
  avoid putting (non-polar) land near the edge of the map.
****************************************************************************/
static bool near_singularity(int map_x, int map_y)
{
  return is_singular_map_pos(map_x, map_y, CITY_MAP_RADIUS);
}

/**************************************************************************
  make_mountains() will convert all squares that are higher than thill to
  mountains and hills. Notice that thill will be adjusted according to
  the map.mountains value, so increase map.mountains and you'll get more 
  hills and mountains (and vice versa).
**************************************************************************/
static void make_mountains(void)
{
  /* Calculate the mountain level.  map.mountains specifies the percentage
   * of land that is turned into hills and mountains. */
  hmap_mountain_level = ((hmap_max_level - hmap_shore_level)
			 * (100 - map.mountains)) / 100 + hmap_shore_level;

  whole_map_iterate(x, y) {
    if (hmap(x, y) > hmap_mountain_level
	&& !is_ocean(map_get_terrain(x, y))) { 
      /* Randomly place hills and mountains on "high" tiles.  But don't
       * put hills near the poles (they're too green). */
      if (myrand(100) > 75 
	  || map_temperature(x, y) <= MAX_TEMP / 10) {
	map_set_terrain(x, y, T_MOUNTAINS);
	map_set_placed(x, y);
      } else if (myrand(100) > 25) {
	map_set_terrain(x, y, T_HILLS);
	map_set_placed(x, y);
      }
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Add arctic and tundra squares in the arctic zone (that is, the coolest
  10% of the map).  We also texture the pole (adding arctic, tundra, and
  mountains).  This is used in generators 2-4.
****************************************************************************/
static void make_polar(void)
{
  struct tile *ptile;
  int T;

  whole_map_iterate(map_x, map_y) {
    T = map_temperature(map_x, map_y);	/* temperature parameter */
    ptile = map_get_tile(map_x, map_y);
    if (T < ICE_BASE_LEVEL) { /* get the coldest part of the map */
      if (ptile->terrain != T_MOUNTAINS) {
	ptile->terrain = T_ARCTIC;
	map_set_placed(map_x, map_y);
      }
    } else if ((T <= 1.5 * ICE_BASE_LEVEL) 
	       && (ptile->terrain == T_OCEAN) ) {  
      ptile->terrain = T_ARCTIC;
      map_set_placed(map_x, map_y);
    } else if (T <= 2 * ICE_BASE_LEVEL) {
      if (ptile->terrain == T_OCEAN) {
	if (myrand(10) > 5) {
	  ptile->terrain = T_ARCTIC;
	  map_set_placed(map_x, map_y);
	} else if (myrand(10) > 6) {
	  ptile->terrain = T_TUNDRA;
	  map_set_placed(map_x, map_y);
	}
      } else if (myrand(10) > 0 && ptile->terrain != T_MOUNTAINS) {
	ptile->terrain = T_TUNDRA;
	map_set_placed(map_x, map_y);
      }
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Place untextured land at the poles.  This is used by generators 1 and 5.
  The land is textured by make_tundra, make_arctic, and make_mountains.
****************************************************************************/
static void make_polar_land(void)
{
  struct tile *ptile;
  int T;

  whole_map_iterate(map_x, map_y) {
    T = map_temperature(map_x, map_y);	/* temperature parameter */
    ptile = map_get_tile(map_x, map_y);
    if (T < 1.5 * ICE_BASE_LEVEL) {
      ptile->terrain = T_UNKNOWN;
      map_unset_placed(map_x, map_y);
    } else if ((T <= 2 * ICE_BASE_LEVEL) && myrand(10) > 4 ) { 
      ptile->terrain = T_UNKNOWN;
      map_unset_placed(map_x, map_y);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Create tundra in cold zones.  Used by generators 1 and 5.  This must be
  called after make_arctic since it will fill all remaining untextured
  areas (we don't want any grassland left on the poles).
****************************************************************************/
static void make_tundra(void)
{
  whole_map_iterate(x, y) {
    int T = map_temperature(x, y);

    if (not_placed(x,y) 
	&& (2 * ICE_BASE_LEVEL > T || myrand(MAX_TEMP/5) > T)) {
      map_set_terrain(x, y, T_TUNDRA);
      map_set_placed(x, y);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Create arctic in cold zones.  Used by generators 1 and 5.
****************************************************************************/
static void make_arctic(void)
{
  whole_map_iterate(x, y) {
    int T = map_temperature(x, y);

    if (not_placed(x,y) 
	&& myrand(15 * MAX_TEMP / 100) > T -  ICE_BASE_LEVEL 
	&& T <= 3 * ICE_BASE_LEVEL) {
      map_set_terrain(x, y, T_ARCTIC);
      map_set_placed(x, y);
    }
  } whole_map_iterate_end;
}

/**************************************************************************
  Recursively generate deserts.

  Deserts tend to clump up so we recursively place deserts nearby as well.
  "Diff" is the recursion stopper and is reduced when the recursive call
  is made.  It is reduced more if the desert wants to grow in north or
  south (so we end up with "wide" deserts).

  base_T is the temperature of the original desert.
**************************************************************************/
static void make_desert(int x, int y, int height, int diff, int base_T)
{
  const int DeltaT = MAX_TEMP / (3 * SQSIZE);

  if (abs(hmap(x, y) - height) < diff 
      && not_placed(x,y)) {
    map_set_terrain(x, y, T_DESERT);
    map_set_placed(x, y);
    cardinal_adjc_iterate(x, y, x1, y1) {
      make_desert(x1, y1, height,
		  diff - 1 - abs(map_temperature(x1, y1) - base_T) / DeltaT,
		  base_T);
     
    } cardinal_adjc_iterate_end;
  }
}

/**************************************************************************
  a recursive function that adds forest to the current location and try
  to spread out to the neighbours, it's called from make_forests until
  enough forest has been planted. diff is again the block function.
  if we're close to equator it will with 50% chance generate jungle instead
**************************************************************************/
static void make_forest(int map_x, int map_y, int height, int diff)
{
  int T = map_temperature(map_x, map_y);
  if (not_placed(map_x, map_y)) {
    if (T > 8 * MAX_TEMP / 10 
	&& myrand(1000) > 500 - 300 * (T * 1000 / MAX_TEMP - 800)) {
      map_set_terrain(map_x, map_y, T_JUNGLE);
    } else {
      map_set_terrain(map_x, map_y, T_FOREST);
    }
    map_set_placed(map_x, map_y);
    if (abs(hmap(map_x, map_y) - height) < diff) {
      cardinal_adjc_iterate(map_x, map_y, x1, y1) {
	if (myrand(10) > 5) {
	  make_forest(x1, y1, height, diff - 5);
	}
      } cardinal_adjc_iterate_end;
    }
    forests++;
  }
}

/**************************************************************************
  makeforest calls make_forest with random unplaced locations until there
  has been made enough forests. (the map.forestsize value controls this) 
**************************************************************************/
static void make_forests(void)
{
  int x, y;
  int forestsize = (map_num_tiles() * map.forestsize) / 1000;

  forests = 0;

  do {
    /* Place one forest clump anywhere. */
    if (rand_map_pos_temperature(&x, &y,
				 MAX_TEMP / 10, MAX_TEMP)) {
      make_forest(x, y, hmap(x, y), 25);
    } else { 
      /* If rand_map_pos_temperature returns FALSE we may as well stop
       * looking. */
      break;
    }

    /* Now add another tropical forest clump (70-100% temperature). */
    if (rand_map_pos_temperature(&x, &y,
				 7 *MAX_TEMP / 10, MAX_TEMP)) {
      make_forest(x, y, hmap(x, y), 25);
    }

    /* And add a cold forest clump (10%-30% temperature). */
    if (rand_map_pos_temperature(&x, &y,
	  	 1 * MAX_TEMP / 10, 3 * MAX_TEMP / 10)) {
      make_forest(x, y, hmap(x, y), 25);
    }
  } while (forests < forestsize);
}

/**************************************************************************
  swamps, is placed on low lying locations, that will typically be close to
  the shoreline. They're put at random (where there is grassland)
  and with 50% chance each of it's neighbour squares will be converted to
  swamp aswell
**************************************************************************/
static void make_swamps(void)
{
  int x, y, swamps;
  int forever = 0;
  const int swamps_to_be_placed 
      = MAX_MAP_INDEX *  map.swampsize * map.landpercent / 10000;
  const int hmap_swamp_level = ((hmap_max_level - hmap_shore_level)
			  * 2 * map.swampsize) / 100 + hmap_shore_level;

  for (swamps = 0; swamps < swamps_to_be_placed; ) {
    forever++;
    if (forever > 1000) {
      return;
    }
    rand_map_pos(&x, &y);
    if (not_placed(x, y)
	&& hmap(x, y) < hmap_swamp_level) {
      map_set_terrain(x, y, T_SWAMP);
      map_set_placed(x, y);
      cardinal_adjc_iterate(x, y, x1, y1) {
 	if (myrand(10) > 5 && not_placed(x1, y1)) { 
	  map_set_terrain(x1, y1, T_SWAMP);
	  map_set_placed(x1, y1);
	  swamps++;
	}
      } cardinal_adjc_iterate_end;
      swamps++;
      forever = 0;
    }
  }
}

/*************************************************************************
  Make deserts until we have enough of them.
**************************************************************************/
static void make_deserts(void)
{
  int x, y, i = map.deserts, j = 0;

  /* "i" is the number of desert clumps made; "j" is the number of tries. */
  /* TODO: j is no longer needed */
  while (i > 0 && j < 100 * map.deserts) {
    j++;

    /* Choose a random coordinate between 20 and 30 degrees north/south
     * (deserts tend to be between 15 and 35; make_desert will expand
     * them). */
    if (rand_map_pos_temperature(&x, &y,
	   65 * MAX_TEMP / 100, 80 * MAX_TEMP / 100)){
      make_desert(x, y, hmap(x, y), 50, map_temperature(x, y));
      i--;
    } else {
      /* If rand_map_pos_temperature returns FALSE we may as well stop
       * looking. */
      break;
    }
  }
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_blocked(int x, int y)
{
  if (TEST_BIT(rmap(x, y), RS_BLOCKED))
    return 1;

  /* any un-blocked? */
  cardinal_adjc_iterate(x, y, x1, y1) {
    if (!TEST_BIT(rmap(x1, y1), RS_BLOCKED))
      return 0;
  } cardinal_adjc_iterate_end;

  return 1; /* none non-blocked |- all blocked */
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_rivergrid(int x, int y)
{
  return (count_special_near_tile(x, y, TRUE, S_RIVER) > 1) ? 1 : 0;
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_highlands(int x, int y)
{
  return (((map_get_terrain(x, y) == T_HILLS) ? 1 : 0) +
	  ((map_get_terrain(x, y) == T_MOUNTAINS) ? 2 : 0));
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_ocean(int x, int y)
{
  /* This number must always be >= 0.  6 is the maximum number of
   * cardinal directions. */
  return 6 - count_ocean_near_tile(x, y, TRUE);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_river(int x, int y)
{
  /* This number must always be >= 0.  6 is the maximum number of
   * cardinal directions. */
  return 6 - count_special_near_tile(x, y, TRUE, S_RIVER);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_highlands(int x, int y)
{
  return (count_terrain_near_tile(x, y, TRUE, T_HILLS)
	  + 2 * count_terrain_near_tile(x, y, TRUE, T_MOUNTAINS));
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_swamp(int x, int y)
{
  return (map_get_terrain(x, y) != T_SWAMP) ? 1 : 0;
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_swamp(int x, int y)
{
  /* This number must always be >= 0.  6 is the maximum number of
   * cardinal directions. */
  return 6 - count_terrain_near_tile(x, y, TRUE, T_SWAMP);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_height_map(int x, int y)
{
  return hmap(x, y);
}

/*********************************************************************
 Called from make_river. Marks all directions as blocked.  -Erik Sigra
*********************************************************************/
static void river_blockmark(int x, int y)
{
  freelog(LOG_DEBUG, "Blockmarking (%d, %d) and adjacent tiles.",
	  x, y);

  rmap(x, y) |= (1u << RS_BLOCKED);

  cardinal_adjc_iterate(x, y, x1, y1) {
    rmap(x1, y1) |= (1u << RS_BLOCKED);
  } cardinal_adjc_iterate_end;
}

struct test_func {
  int (*func)(int, int);
  bool fatal;
};

#define NUM_TEST_FUNCTIONS 9
static struct test_func test_funcs[NUM_TEST_FUNCTIONS] = {
  {river_test_blocked,            TRUE},
  {river_test_rivergrid,          TRUE},
  {river_test_highlands,          FALSE},
  {river_test_adjacent_ocean,     FALSE},
  {river_test_adjacent_river,     FALSE},
  {river_test_adjacent_highlands, FALSE},
  {river_test_swamp,              FALSE},
  {river_test_adjacent_swamp,     FALSE},
  {river_test_height_map,         FALSE}
};

/********************************************************************
 Makes a river starting at (x, y). Returns 1 if it succeeds.
 Return 0 if it fails. The river is stored in river_map.
 
 How to make a river path look natural
 =====================================
 Rivers always flow down. Thus rivers are best implemented on maps
 where every tile has an explicit height value. However, Freeciv has a
 flat map. But there are certain things that help the user imagine
 differences in height between tiles. The selection of direction for
 rivers should confirm and even amplify the user's image of the map's
 topology.
 
 To decide which direction the river takes, the possible directions
 are tested in a series of test until there is only 1 direction
 left. Some tests are fatal. This means that they can sort away all
 remaining directions. If they do so, the river is aborted. Here
 follows a description of the test series.
 
 * Falling into itself: fatal
     (river_test_blocked)
     This is tested by looking up in the river_map array if a tile or
     every tile surrounding the tile is marked as blocked. A tile is
     marked as blocked if it belongs to the current river or has been
     evaluated in a previous iteration in the creation of the current
     river.
     
     Possible values:
     0: Is not falling into itself.
     1: Is falling into itself.
     
 * Forming a 4-river-grid: optionally fatal
     (river_test_rivergrid)
     A minimal 4-river-grid is formed when an intersection in the map
     grid is surrounded by 4 river tiles. There can be larger river
     grids consisting of several overlapping minimal 4-river-grids.
     
     Possible values:
     0: Is not forming a 4-river-grid.
     1: Is forming a 4-river-grid.

 * Highlands:
     (river_test_highlands)
     Rivers must not flow up in mountains or hills if there are
     alternatives.
     
     Possible values:
     0: Is not hills and not mountains.
     1: Is hills.
     2: Is mountains.

 * Adjacent ocean:
     (river_test_adjacent_ocean)
     Rivers must flow down to coastal areas when possible:

     Possible values:
     n: 4 - adjacent_terrain_tiles4(...)

 * Adjacent river:
     (river_test_adjacent_river)
     Rivers must flow down to areas near other rivers when possible:

     Possible values:
     n: 6 - count_river_near_tile(...) (should be small after the
                                        river-grid test)
					
 * Adjacent highlands:
     (river_test_adjacent_highlands)
     Rivers must not flow towards highlands if there are alternatives. 
     
 * Swamps:
     (river_test_swamp)
     Rivers must flow down in swamps when possible.
     
     Possible values:
     0: Is swamps.
     1: Is not swamps.
     
 * Adjacent swamps:
     (river_test_adjacent_swamp)
     Rivers must flow towards swamps when possible.

 * height_map:
     (river_test_height_map)
     Rivers must flow in the direction which takes it to the tile with
     the lowest value on the height_map.
     
     Possible values:
     n: height_map[...]
     
 If these rules haven't decided the direction, the random number
 generator gets the desicion.                              -Erik Sigra
*********************************************************************/
static bool make_river(int x, int y)
{
  /* Comparison value for each tile surrounding the current tile.  It is
   * the suitability to continue a river to the tile in that direction;
   * lower is better.  However rivers may only run in cardinal directions;
   * the other directions are ignored entirely. */
  int rd_comparison_val[8];

  bool rd_direction_is_valid[8];
  int num_valid_directions, func_num, direction;

  while (TRUE) {
    /* Mark the current tile as river. */
    rmap(x, y) |= (1u << RS_RIVER);
    freelog(LOG_DEBUG,
	    "The tile at (%d, %d) has been marked as river in river_map.\n",
	    x, y);

    /* Test if the river is done. */
    /* We arbitrarily make rivers end at the poles. */
    if (count_special_near_tile(x, y, TRUE, S_RIVER) != 0
	|| count_ocean_near_tile(x, y, TRUE) != 0
        || (map_get_terrain(x, y) == T_ARCTIC 
	    && map_temperature(x, y) < 8 * MAX_TEMP / 100)) { 

      freelog(LOG_DEBUG,
	      "The river ended at (%d, %d).\n", x, y);
      return TRUE;
    }

    /* Else choose a direction to continue the river. */
    freelog(LOG_DEBUG,
	    "The river did not end at (%d, %d). Evaluating directions...\n",
	    x, y);

    /* Mark all available cardinal directions as available. */
    memset(rd_direction_is_valid, 0, sizeof(rd_direction_is_valid));
    cardinal_adjc_dir_iterate(x, y, x1, y1, dir) {
      rd_direction_is_valid[dir] = TRUE;
    } cardinal_adjc_dir_iterate_end;

    /* Test series that selects a direction for the river. */
    for (func_num = 0; func_num < NUM_TEST_FUNCTIONS; func_num++) {
      int best_val = -1;

      /* first get the tile values for the function */
      cardinal_adjc_dir_iterate(x, y, x1, y1, dir) {
	if (rd_direction_is_valid[dir]) {
	  rd_comparison_val[dir] = (test_funcs[func_num].func) (x1, y1);
	  if (best_val == -1) {
	    best_val = rd_comparison_val[dir];
	  } else {
	    best_val = MIN(rd_comparison_val[dir], best_val);
	  }
	}
      } cardinal_adjc_dir_iterate_end;
      assert(best_val != -1);

      /* should we abort? */
      if (best_val > 0 && test_funcs[func_num].fatal) {
	return FALSE;
      }

      /* mark the less attractive directions as invalid */
      cardinal_adjc_dir_iterate(x, y, x1, y1, dir) {
	if (rd_direction_is_valid[dir]) {
	  if (rd_comparison_val[dir] != best_val) {
	    rd_direction_is_valid[dir] = FALSE;
	  }
	}
      } cardinal_adjc_dir_iterate_end;
    }

    /* Directions evaluated with all functions. Now choose the best
       direction before going to the next iteration of the while loop */
    num_valid_directions = 0;
    cardinal_adjc_dir_iterate(x, y, x1, y1, dir) {
      if (rd_direction_is_valid[dir]) {
	num_valid_directions++;
      }
    } cardinal_adjc_dir_iterate_end;

    if (num_valid_directions == 0) {
      return FALSE; /* river aborted */
    }

    /* One or more valid directions: choose randomly. */
    freelog(LOG_DEBUG, "mapgen.c: Had to let the random number"
	    " generator select a direction for a river.");
    direction = myrand(num_valid_directions);
    freelog(LOG_DEBUG, "mapgen.c: direction: %d", direction);

    /* Find the direction that the random number generator selected. */
    cardinal_adjc_dir_iterate(x, y, x1, y1, dir) {
      if (rd_direction_is_valid[dir]) {
	if (direction > 0) {
	  direction--;
	} else {
	  river_blockmark(x, y);
	  x = x1;
	  y = y1;
	  break;
	}
      }
    } cardinal_adjc_dir_iterate_end;
    assert(direction == 0);

  } /* end while; (Make a river.) */
}

/**************************************************************************
  Calls make_river until there are enough river tiles on the map. It stops
  when it has tried to create RIVERS_MAXTRIES rivers.           -Erik Sigra
**************************************************************************/
static void make_rivers(void)
{
  int x, y; /* The coordinates. */

  /* Formula to make the river density similar om different sized maps. Avoids
     too few rivers on large maps and too many rivers on small maps. */
  int desirable_riverlength =
    map.riverlength *
    /* This 10 is a conversion factor to take into account the fact that this
     * river code was written when map.riverlength had a maximum value of 
     * 1000 rather than the current 100 */
    10 *
    /* The size of the map (poles don't count). */
    map_num_tiles() * (map.alltemperate ? 1.0 : 0.90) *
    /* Rivers need to be on land only. */
    map.landpercent /
    /* Adjustment value. Tested by me. Gives no rivers with 'set
       rivers 0', gives a reasonable amount of rivers with default
       settings and as many rivers as possible with 'set rivers 100'. */
    0xD000; /* (= 53248 in decimal) */

  /* The number of river tiles that have been set. */
  int current_riverlength = 0;

  int i; /* Loop variable. */

  /* Counts the number of iterations (should increase with 1 during
     every iteration of the main loop in this function).
     Is needed to stop a potentially infinite loop. */
  int iteration_counter = 0;

  river_map = fc_malloc(sizeof(int) * map.xsize * map.ysize);

  /* The main loop in this function. */
  while (current_riverlength < desirable_riverlength
	 && iteration_counter < RIVERS_MAXTRIES) {

    rand_map_pos(&x, &y);

    /* Check if it is suitable to start a river on the current tile.
     */
    if (
	/* Don't start a river on ocean. */
	!is_ocean(map_get_terrain(x, y))

	/* Don't start a river on river. */
	&& !map_has_special(x, y, S_RIVER)

	/* Don't start a river on a tile is surrounded by > 1 river +
	   ocean tile. */
	&& (count_special_near_tile(x, y, TRUE, S_RIVER)
	    + count_ocean_near_tile(x, y, TRUE) <= 1)

	/* Don't start a river on a tile that is surrounded by hills or
	   mountains unless it is hard to find somewhere else to start
	   it. */
	&& (count_terrain_near_tile(x, y, TRUE, T_HILLS)
	    + count_terrain_near_tile(x, y, TRUE, T_MOUNTAINS) < 4
	    || iteration_counter == RIVERS_MAXTRIES / 10 * 5)

	/* Don't start a river on hills unless it is hard to find
	   somewhere else to start it. */
	&& (map_get_terrain(x, y) != T_HILLS
	    || iteration_counter == RIVERS_MAXTRIES / 10 * 6)

	/* Don't start a river on mountains unless it is hard to find
	   somewhere else to start it. */
	&& (map_get_terrain(x, y) != T_MOUNTAINS
	    || iteration_counter == RIVERS_MAXTRIES / 10 * 7)

	/* Don't start a river on arctic unless it is hard to find
	   somewhere else to start it. */
	&& (map_get_terrain(x, y) != T_ARCTIC
	    || iteration_counter == RIVERS_MAXTRIES / 10 * 8)

	/* Don't start a river on desert unless it is hard to find
	   somewhere else to start it. */
	&& (map_get_terrain(x, y) != T_DESERT
	    || iteration_counter == RIVERS_MAXTRIES / 10 * 9)) {

      /* Reset river_map before making a new river. */
      for (i = 0; i < map.xsize * map.ysize; i++) {
	river_map[i] = 0;
      }

      freelog(LOG_DEBUG,
	      "Found a suitable starting tile for a river at (%d, %d)."
	      " Starting to make it.",
	      x, y);

      /* Try to make a river. If it is OK, apply it to the map. */
      if (make_river(x, y)) {
	whole_map_iterate(x1, y1) {
	  if (TEST_BIT(rmap(x1, y1), RS_RIVER)) {
	    Terrain_type_id t = map_get_terrain(x1, y1);

	    if (!terrain_has_flag(t, TER_CAN_HAVE_RIVER)) {
	      /* We have to change the terrain to put a river here. */
	      t = get_flag_terrain(TER_CAN_HAVE_RIVER);
	      map_set_terrain(x1, y1, t);
	    }
	    map_set_special(x1, y1, S_RIVER);
	    current_riverlength++;
	    freelog(LOG_DEBUG, "Applied a river to (%d, %d).", x1, y1);
	  }
	} whole_map_iterate_end;
      }
      else {
	freelog(LOG_DEBUG,
		"mapgen.c: A river failed. It might have gotten stuck in a helix.");
      }
    } /* end if; */
    iteration_counter++;
    freelog(LOG_DEBUG,
	    "current_riverlength: %d; desirable_riverlength: %d; iteration_counter: %d",
	    current_riverlength, desirable_riverlength, iteration_counter);
  } /* end while; */
  free(river_map);
  river_map = NULL;
}

/**************************************************************************
  make_plains converts 50% of the remaining terrains to plains and 50%
  grassland,
**************************************************************************/
static void make_plains(void)
{
  whole_map_iterate(x, y) {
    if (not_placed(x, y)) {
      if(myrand(100) > 50) {
	  map_set_terrain(x, y, T_GRASSLAND);
      } else {
	  map_set_terrain(x, y, T_PLAINS);
      }
      map_set_placed(x, y);
    }
  } whole_map_iterate_end;
}
/****************************************************************************
  Return TRUE if the terrain at the given map position is "clean".  This
  means that all the terrain for 2 squares around it is either grassland or
  plains.
****************************************************************************/
static bool terrain_is_clean(int map_x, int map_y)
{
  square_iterate(map_x, map_y, 2, x1, y1) {
    if (map_get_terrain(x1, y1) != T_GRASSLAND
	&& map_get_terrain(x1, y1) != T_PLAINS) {
      return FALSE;
    }
  } square_iterate_end;

  return TRUE;
}

/**************************************************************************
  we don't want huge areas of grass/plains, 
 so we put in a hill here and there, where it gets too 'clean' 
**************************************************************************/
static void make_fair(void)
{
  whole_map_iterate(map_x, map_y) {
    if (terrain_is_clean(map_x, map_y)) {
      if (!map_has_special(map_x, map_y, S_RIVER)) {
	map_set_terrain(map_x, map_y, T_HILLS);
      }
      cardinal_adjc_iterate(map_x, map_y, x1, y1) {
	if (myrand(100) > 66
	    && !is_ocean(map_get_terrain(x1, y1))
	    && !map_has_special(x1, y1, S_RIVER)) {
	  map_set_terrain(x1, y1, T_HILLS);
	}
      } cardinal_adjc_iterate_end;
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Lower the land near the polar region to avoid too much land there.

  See also renomalize_hmap_poles
****************************************************************************/
static void normalize_hmap_poles(void)
{
  whole_map_iterate(x, y) {
    if (near_singularity(x, y)) {
      hmap(x, y) = 0;
    } else if (map_temperature(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_temperature(x, y) / (2.5 * ICE_BASE_LEVEL);
    } else if (map.separatepoles 
	       && map_temperature(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 0.1;
    } else if (map_temperature(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_temperature(x, y) / (2.5 * ICE_BASE_LEVEL);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Invert the effects of normalize_hmap_poles so that we have accurate heights
  for texturing the poles.
****************************************************************************/
static void renormalize_hmap_poles(void)
{
  whole_map_iterate(x, y) {
    if (hmap(x, y) == 0 || map_temperature(x, y) == 0) {
      /* Nothing. */
    } else if (map_temperature(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) / map_temperature(x, y);
    } else if (map.separatepoles 
	       && map_temperature(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 10;
    } else if (map_temperature(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) /  map_temperature(x, y);
    }
  } whole_map_iterate_end;
}

/**************************************************************************
  make land simply does it all based on a generated heightmap
  1) with map.landpercent it generates a ocean/grassland map 
  2) it then calls the above functions to generate the different terrains
**************************************************************************/
static void make_land(void)
{
  create_placed_map(); /* here it means land terrains to be placed */
  adjust_hmap();
  normalize_hmap_poles();
  hmap_shore_level = (hmap_max_level * (100 - map.landpercent)) / 100;
  whole_map_iterate(x, y) {
    map_set_terrain(x, y, T_UNKNOWN); /* set as oceans count is used */
    if (hmap(x, y) < hmap_shore_level) {
      map_set_terrain(x, y, T_OCEAN);
      map_set_placed(x, y); /* placed, not a land target */
    }
  } whole_map_iterate_end;

  renormalize_hmap_poles();
  make_polar_land(); /* make extra land at poles */
  make_mountains();
  make_arctic();
  make_tundra();
  make_swamps();
  make_forests();
  make_deserts();
  make_plains();
  make_fair();
  make_rivers();
  remove_placed_map();
  assign_continent_numbers();
}

/**************************************************************************
  Returns if this is a 1x1 island
**************************************************************************/
static bool is_tiny_island(int x, int y) 
{
  Terrain_type_id t = map_get_terrain(x, y);

  if (is_ocean(t) || t == T_ARCTIC) {
    /* The arctic check is needed for iso-maps: the poles may not have
     * any cardinally adjacent land tiles, but that's okay. */
    return FALSE;
  }

  cardinal_adjc_iterate(x, y, x1, y1) {
    if (!is_ocean(map_get_terrain(x1, y1))) {
      return FALSE;
    }
  } cardinal_adjc_iterate_end;

  return TRUE;
}

/**************************************************************************
  Removes all 1x1 islands (sets them to ocean).
**************************************************************************/
static void remove_tiny_islands(void)
{
  whole_map_iterate(x, y) {
    if (is_tiny_island(x, y)) {
      map_set_terrain(x, y, T_OCEAN);
      map_clear_special(x, y, S_RIVER);
      map_set_continent(x, y, 0);
    }
  } whole_map_iterate_end;
}

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
	  || map_temperature(x1, y1) <= 2 * ICE_BASE_LEVEL) { 
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

/****************************************************************************
  Set the map xsize and ysize based on a base size and ratio (in natural
  coordinates).
****************************************************************************/
static void set_sizes(double size, int Xratio, int Yratio)
{
  /* Some code in generator assumes even dimension, so this is set to 2.
   * Future topologies may also require even dimensions. */
  const int even = 2;

  /* In iso-maps we need to double the map.ysize factor, since xsize is
   * in native coordinates which are compressed 2x in the X direction. */ 
  const int iso = (topo_has_flag(TF_ISO) || topo_has_flag(TF_HEX)) ? 2 : 1;

  /* We have:
   *
   *   1000 * size = xsize * ysize
   *
   * And to satisfy the ratios and other constraints we set
   *
   *   xsize = i_size * xratio * even
   *   ysize = i_size * yratio * even * iso
   *
   * For any value of "i_size".  So with some substitution
   *
   *   1000 * size = i_size * i_size * xratio * yratio * even * even * iso
   *   i_size = sqrt(1000 * size / (xratio * yratio * even * even * iso))
   * 
   * Make sure to round off i_size to preserve exact wanted ratios,
   * that may be importante for some topologies.
   */
  const int i_size
    = sqrt((float)(1000 * size)
	   / (float)(Xratio * Yratio * iso * even * even)) + 0.49;

  /* Now build xsize and ysize value as described above. */
  map.xsize = Xratio * i_size * even;
  map.ysize = Yratio * i_size * even * iso;

  /* Now make sure the size isn't too large for this ratio.  If it is
   * then decrease the size and try again. */
  if (MAX(MAP_WIDTH, MAP_HEIGHT) > MAP_MAX_LINEAR_SIZE ) {
    assert(size > 0.1);
    set_sizes(size - 0.1, Xratio, Yratio);
    return;
  }

  /* If the ratio is too big for some topology the simplest way to avoid
   * this error is to set the maximum size smaller for all topologies! */
  if (map.size > size + 0.9) {
    /* Warning when size is set uselessly big */ 
    freelog(LOG_ERROR,
	    "Requested size of %d is too big for this topology.",
	    map.size);
  }
  freelog(LOG_VERBOSE,
	  "Creating a map of size %d x %d = %d tiles (%d requested).",
	  map.xsize, map.ysize, map.xsize * map.ysize, map.size * 1000);
}

/*
 * The default ratios for known topologies
 *
 * The factor Xratio * Yratio determines the accuracy of the size.
 * Small ratios work better than large ones; 3:2 is not the same as 6:4
 */
#define AUTO_RATIO_FLAT           {1, 1}
#define AUTO_RATIO_CLASSIC        {3, 2} 
#define AUTO_RATIO_URANUS         {2, 3} 
#define AUTO_RATIO_TORUS          {1, 1}

/*************************************************************************** 
  This function sets sizes in a topology-specific way then calls
  map_init_topology.
***************************************************************************/
static void generator_init_topology(void)
{
  /* Changing or reordering the topo_flag enum will break this code. */
  const int default_ratios[4][2] =
      {AUTO_RATIO_FLAT, AUTO_RATIO_CLASSIC,
       AUTO_RATIO_URANUS, AUTO_RATIO_TORUS};
  const int id = 0x3 & map.topology_id;
  
  assert(TF_WRAPX == 0x1 && TF_WRAPY == 0x2);

  /* Set map.xsize and map.ysize based on map.size. */
  set_sizes(map.size, default_ratios[id][0], default_ratios[id][1]);

  /* Then initialise all topoloicals parameters */
  map_init_topology(TRUE);
}

/**************************************************************************
  See stdinhand.c for information on map generation methods.

FIXME: Some continent numbers are unused at the end of this function, fx
       removed completely by remove_tiny_islands.
       When this function is finished various data is written to "islands",
       indexed by continent numbers, so a simple renumbering would not
       work...
**************************************************************************/
void map_fractal_generate(void)
{
  /* save the current random state: */
  RANDOM_STATE rstate = get_myrand_state();
 
  if (map.seed==0)
    map.seed = (myrand(MAX_UINT32) ^ time(NULL)) & (MAX_UINT32 >> 1);

  mysrand(map.seed);

  /* don't generate tiles with mapgen==0 as we've loaded them from file */
  /* also, don't delete (the handcrafted!) tiny islands in a scenario */
  if (map.generator != 0) {
    generator_init_topology();  /* initialize map.xsize and map.ysize, etc */
    map_allocate();
    adjust_terrain_param();
    /* if one mapgenerator fails, it will choose another mapgenerator */
    /* with a lower number to try again */
    if (map.generator == 5 )
      mapgenerator5();
    if (map.generator == 4 )
      mapgenerator4();
    if (map.generator == 3 )
      mapgenerator3();
    if( map.generator == 2 )
      mapgenerator2();
    if( map.generator == 1 )
      mapgenerator1();
    if (!map.tinyisles) {
      remove_tiny_islands();
    }
  } else {
      assign_continent_numbers();
  }

  if(!map.have_specials) /* some scenarios already provide specials */
    add_specials(map.riches); /* hvor mange promiller specials oensker vi*/

  if (!map.have_huts)
    make_huts(map.huts); /* Vi vil have store promiller; man kan aldrig faa
			    for meget oel! */

  /* restore previous random state: */
  set_myrand_state(rstate);
}

/**************************************************************************
 Convert terrain parameters from the server into percents for the generators
**************************************************************************/
static void adjust_terrain_param(void)
{
  int total;
  int polar = 5; /* FIXME: convert to a server option */

  total = map.mountains + map.deserts + map.forestsize + map.swampsize 
    + map.grasssize;

  if (total != 100 - polar) {
    map.forestsize = map.forestsize * (100 - polar) / total;
    map.swampsize = map.swampsize * (100 - polar) / total;
    map.mountains = map.mountains * (100 - polar) / total;
    map.deserts = map.deserts * (100 - polar) / total;
    map.grasssize = 100 - map.forestsize - map.swampsize - map.mountains 
      - polar - map.deserts;
  }
}

/**************************************************************************
  Change the values of the height map, so that they contain ranking of each 
  tile scaled to [0 .. hmap_max_level].
  The lowest 20% of tiles will have values lower than 0.2 * hmap_max_level.

  slope can globally be estimated as
        hmap_max_level * sqrt(number_of_islands) / linear_size_of_map
**************************************************************************/
static void adjust_hmap(void)
{
  int minval = hnat(0, 0), maxval = minval;

  /* Determine minimum and maximum heights. */
  whole_map_iterate(x, y) {
    maxval = MAX(maxval, hmap(x, y));
    minval = MIN(minval, hmap(x, y));
  } whole_map_iterate_end;

  {
    int const size = 1 + maxval - minval;
    int i, count = 0, frequencies[size];

    INITIALIZE_ARRAY(frequencies, size, 0);

    /* Translate heights so the minimum height is 0
       and count the number of occurencies of all values to initialize the 
       frequencies[] */
    whole_map_iterate(x, y) {
      hmap(x, y) = (hmap(x, y) - minval);
      frequencies[hmap(x, y)]++;
    } whole_map_iterate_end;

    /* create the linearize function as "incremental" frequencies */
    for(i =  0; i < size; i++) {
      count += frequencies[i]; 
      frequencies[i] = (count * hmap_max_level) / MAX_MAP_INDEX;
    }

    /* apply the linearize function */
    whole_map_iterate(x, y) {
      hmap(x, y) = frequencies[hmap(x, y)];
    } whole_map_iterate_end; 
  }
}

/**************************************************************************
  mapgenerator1, highlevel function, that calls all the previous functions
**************************************************************************/
static void mapgenerator1(void)
{
  int i;
  height_map = fc_malloc (sizeof(int) * MAX_MAP_INDEX);

  INITIALIZE_ARRAY(height_map, MAX_MAP_INDEX, myrand(40) );

  for (i=0;i<1500;i++) {
    int x, y;

    rand_map_pos(&x, &y);

    if (near_singularity(x, y)
	|| map_temperature(x, y) <= ICE_BASE_LEVEL / 2) { 
      /* Avoid land near singularities or at the poles. */
      hmap(x, y) -= myrand(5000);
    } else { 
      hmap(x, y) += myrand(5000);
    }
    if ((i % 100) == 0) {
      smooth_map(); 
    }
  }

  smooth_map(); 
  smooth_map(); 
  smooth_map(); 

  make_land();
  free(height_map);
  height_map = NULL;
}

/**************************************************************************
  smooth_map should be viewed as a corrosion function on the map, it
  levels out the differences in the heightmap.
**************************************************************************/
static void smooth_map(void)
{
  /* We make a new height map and then copy it back over the old one.
   * Care must be taken so that the new height map uses the exact same
   * storage structure as the real one - it must be the same size and
   * use the same indexing. The advantage of the new array is there's
   * no feedback from overwriting in-use values.
   */
  int *new_hmap = fc_malloc(sizeof(int) * map.xsize * map.ysize);
  
  whole_map_iterate(x, y) {
    /* double-count this tile */
    int height_sum = hmap(x, y) * 2;

    /* weight of counted tiles */
    int counter = 2;

    adjc_iterate(x, y, x2, y2) {
      /* count adjacent tile once */
      height_sum += hmap(x2, y2);
      counter++;
    } adjc_iterate_end;

    /* random factor: -30..30 */
    height_sum += myrand(61) - 30;

    if (height_sum < 0)
      height_sum = 0;
    new_hmap[map_pos_to_index(x, y)] = height_sum / counter;
  } whole_map_iterate_end;

  memcpy(height_map, new_hmap, sizeof(int) * map.xsize * map.ysize);
  free(new_hmap);
}

/****************************************************************************
  Set all nearby tiles as placed on pmap. This helps avoiding huts being 
  too close
****************************************************************************/
static void set_placed_close_hut(int map_x, int map_y)
{
  square_iterate(map_x, map_y, 3, x1, y1) {
    map_set_placed(x1, y1);
  } square_iterate_end;
}

/****************************************************************************
  Return TRUE if a safe tile is in a radius of 1.  This function is used to
  test where to place specials on the sea.
****************************************************************************/
static bool near_safe_tiles(int x_ct, int y_ct)
{
  square_iterate(x_ct, y_ct, 1, map_x, map_y) {
    if (!terrain_has_flag(map_get_terrain(map_x, map_y), TER_UNSAFE_COAST)) {
      return TRUE;
    }	
  } square_iterate_end;

  return FALSE;
}

/**************************************************************************
  this function spreads out huts on the map, a position can be used for a
  hut if there isn't another hut close and if it's not on the ocean.
**************************************************************************/
static void make_huts(int number)
{
  int x, y, count = 0;

  create_placed_map(); /* here it means placed huts */

  while (number * map_num_tiles() >= 2000 && count++ < map_num_tiles() * 2) {

    /* Add a hut.  But not on a polar area, on an ocean, or too close to
     * another hut. */
    if (rand_map_pos_temperature(&x, &y,
				 2 * ICE_BASE_LEVEL, MAX_TEMP)) {
      if (is_ocean(map_get_terrain(x, y))) {
	map_set_placed(x, y); /* not good for a hut */
      } else {
	number--;
	map_set_special(x, y, S_HUT);
	set_placed_close_hut(x, y);
	    /* Don't add to islands[].goodies because islands[] not
	       setup at this point, except for generator>1, but they
	       have pre-set starters anyway. */
      }
    }
  }
  remove_placed_map();
}

/****************************************************************************
  Return TRUE iff there's a special (i.e., SPECIAL_1 or SPECIAL_2) within
  1 tile of the given map position.
****************************************************************************/
static bool is_special_close(int map_x, int map_y)
{
  square_iterate(map_x, map_y, 1, x1, y1) {
    if (map_has_special(x1, y1, S_SPECIAL_1)
	|| map_has_special(x1, y1, S_SPECIAL_2)) {
      return TRUE;
    }
  } square_iterate_end;

  return FALSE;
}

/****************************************************************************
  Add specials to the map with given probability (out of 1000).
****************************************************************************/
static void add_specials(int prob)
{
  Terrain_type_id ttype;

  whole_map_iterate(x, y)  {
    ttype = map_get_terrain(x, y);
    if (!is_ocean(ttype)
	&& !is_special_close(x, y) 
	&& myrand(1000) < prob) {
      if (tile_types[ttype].special_1_name[0] != '\0'
	  && (tile_types[ttype].special_2_name[0] == '\0'
	      || (myrand(100) < 50))) {
	map_set_special(x, y, S_SPECIAL_1);
      } else if (tile_types[ttype].special_2_name[0] != '\0') {
	map_set_special(x, y, S_SPECIAL_2);
      }
    } else if (is_ocean(ttype) && near_safe_tiles(x,y) 
	       && myrand(1000) < prob && !is_special_close(x, y)) {
      if (tile_types[ttype].special_1_name[0] != '\0'
	  && (tile_types[ttype].special_2_name[0] == '\0'
	      || (myrand(100) < 50))) {
        map_set_special(x, y, S_SPECIAL_1);
      } else if (tile_types[ttype].special_2_name[0] != '\0') {
	map_set_special(x, y, S_SPECIAL_2);
      }
    }
  } whole_map_iterate_end;
  
  map.have_specials = TRUE;
}

/**************************************************************************
  common variables for generator 2, 3 and 4
**************************************************************************/
struct gen234_state {
  int isleindex, n, e, s, w;
  long int totalmass;
};

static bool map_pos_is_cold(int x, int y)
{  
  return map_temperature(x, y) <= 2 * MAX_TEMP/ 10;
}

/**************************************************************************
Returns a random position in the rectangle denoted by the given state.
**************************************************************************/
static void get_random_map_position_from_state(int *x, int *y,
					       const struct gen234_state
					       *const pstate)
{
  int xn, yn;

  assert((pstate->e - pstate->w) > 0);
  assert((pstate->e - pstate->w) < map.xsize);
  assert((pstate->s - pstate->n) > 0);
  assert((pstate->s - pstate->n) < map.ysize);

  xn = pstate->w + myrand(pstate->e - pstate->w);
  yn = pstate->n + myrand(pstate->s - pstate->n);

  NATIVE_TO_MAP_POS(x, y, xn, yn);
  if (!normalize_map_pos(x, y)) {
    die("Invalid map operation.");
  }
}

/**************************************************************************
  fill an island with up four types of terrains, rivers have extra code
**************************************************************************/
static void fill_island(int coast, long int *bucket,
			int warm0_weight, int warm1_weight,
			int cold0_weight, int cold1_weight,
			Terrain_type_id warm0,
			Terrain_type_id warm1,
			Terrain_type_id cold0,
			Terrain_type_id cold1,
			const struct gen234_state *const pstate)
{
  int x, y, i, k, capac;
  long int failsafe;

  if (*bucket <= 0 ) return;
  capac = pstate->totalmass;
  i = *bucket / capac;
  i++;
  *bucket -= i * capac;

  k= i;
  failsafe = i * (pstate->s - pstate->n) * (pstate->e - pstate->w);
  if(failsafe<0){ failsafe= -failsafe; }

  if(warm0_weight+warm1_weight+cold0_weight+cold1_weight<=0)
    i= 0;

  while (i > 0 && (failsafe--) > 0) {
    get_random_map_position_from_state(&x, &y, pstate);

    if (map_get_continent(x, y) == pstate->isleindex &&
	not_placed(x, y)) {

      /* the first condition helps make terrain more contiguous,
	 the second lets it avoid the coast: */
      if ( ( i*3>k*2 
	     || is_terrain_near_tile(x,y,warm0) 
	     || is_terrain_near_tile(x,y,warm1) 
	     || myrand(100)<50 
	     || is_terrain_near_tile(x,y,cold0) 
	     || is_terrain_near_tile(x,y,cold1) 
	     )
	   &&( !is_cardinally_adj_to_ocean(x, y) || myrand(100) < coast )) {
	if (map_pos_is_cold(x, y)) {
	  map_set_terrain(x, y, (myrand(cold0_weight
					+ cold1_weight) < cold0_weight) 
			  ? cold0 : cold1);
	  map_set_placed(x, y);
	} else {
	  map_set_terrain(x, y, (myrand(warm0_weight
					+ warm1_weight) < warm0_weight) 
			  ? warm0 : warm1);
	  map_set_placed(x, y);
	}
      }
      if (!not_placed(x,y)) i--;
    }
  }
}

/**************************************************************************
  fill an island with rivers
**************************************************************************/
static void fill_island_rivers(int coast, long int *bucket,
			       const struct gen234_state *const pstate)
{
  int x, y, i, k, capac;
  long int failsafe;

  if (*bucket <= 0 ) {
    return;
  }
  capac = pstate->totalmass;
  i = *bucket / capac;
  i++;
  *bucket -= i * capac;

  k = i;
  failsafe = i * (pstate->s - pstate->n) * (pstate->e - pstate->w);
  if (failsafe < 0) {
    failsafe = -failsafe;
  }

  while (i > 0 && (failsafe--) > 0) {
    get_random_map_position_from_state(&x, &y, pstate);
    if (map_get_continent(x, y) == pstate->isleindex
	&& not_placed(x, y)) {

      /* the first condition helps make terrain more contiguous,
	 the second lets it avoid the coast: */
      if ((i * 3 > k * 2 
	   || count_special_near_tile(x, y, FALSE, S_RIVER) > 0
	   || myrand(100) < 50)
	  && (!is_cardinally_adj_to_ocean(x, y) || myrand(100) < coast)) {
	if (is_water_adjacent_to_tile(x, y)
	    && count_ocean_near_tile(x, y, FALSE) < 4
            && count_special_near_tile(x, y, FALSE, S_RIVER) < 3) {
	  map_set_special(x, y, S_RIVER);
	  i--;
	}
      }
    }
  }
}

/****************************************************************************
  Return TRUE if the ocean position is near land.  This is used in the
  creation of islands, so it differs logically from near_safe_tiles().
****************************************************************************/
static bool is_near_land(center_x, center_y)
{
  /* Note this function may sometimes be called on land tiles. */
  adjc_iterate(center_x, center_y, x_itr, y_itr) {
    if (!is_ocean(map_get_terrain(x_itr, y_itr))) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

static long int checkmass;

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static bool place_island(struct gen234_state *pstate)
{
  int xn, yn, xon, yon, i=0;

  {
    int xo, yo;

    rand_map_pos(&xo, &yo);
    MAP_TO_NATIVE_POS(&xon, &yon, xo, yo);
  }

  /* this helps a lot for maps with high landmass */
  for (yn = pstate->n, xn = pstate->w;
       yn < pstate->s && xn < pstate->e;
       yn++, xn++) {
    int map_x, map_y;

    NATIVE_TO_MAP_POS(&map_x, &map_y,
		      xn + xon - pstate->w, yn + yon - pstate->n);

    if (!normalize_map_pos(&map_x, &map_y)) {
      return FALSE;
    }
    if (hnat(xn, yn) != 0 && is_near_land(map_x, map_y)) {
      return FALSE;
    }
  }
		       
  for (yn = pstate->n; yn < pstate->s; yn++) {
    for (xn = pstate->w; xn < pstate->e; xn++) {
      int map_x, map_y;

      NATIVE_TO_MAP_POS(&map_x, &map_y,
			xn + xon - pstate->w, yn + yon - pstate->n);

      if (!normalize_map_pos(&map_x, &map_y)) {
	return FALSE;
      }
      if (hnat(xn, yn) != 0 && is_near_land(map_x, map_y)) {
	return FALSE;
      }
    }
  }

  for (yn = pstate->n; yn < pstate->s; yn++) {
    for (xn = pstate->w; xn < pstate->e; xn++) {
      if (hnat(xn, yn) != 0) {
	int map_x, map_y;
	bool is_real;

	NATIVE_TO_MAP_POS(&map_x, &map_y,
			  xn + xon - pstate->w, yn + yon - pstate->n);

	is_real = normalize_map_pos(&map_x, &map_y);
	assert(is_real);

	checkmass--; 
	if(checkmass<=0) {
	  freelog(LOG_ERROR, "mapgen.c: mass doesn't sum up.");
	  return i != 0;
	}

        map_set_terrain(map_x, map_y, T_UNKNOWN);
	map_unset_placed(map_x, map_y);

	map_set_continent(map_x, map_y, pstate->isleindex);
        i++;
      }
    }
  }
  pstate->s += yon - pstate->n;
  pstate->e += xon - pstate->w;
  pstate->n = yon;
  pstate->w = xon;
  return i != 0;
}

/****************************************************************************
  Returns the number of cardinally adjacent tiles have a non-zero elevation.
****************************************************************************/
static int count_card_adjc_elevated_tiles(int x, int y)
{
  int count = 0;

  cardinal_adjc_iterate(x, y, x1, y1) {
    if (hmap(x1, y1) != 0) {
      count++;
    }
  } cardinal_adjc_iterate_end;

  return count;
}

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static bool create_island(int islemass, struct gen234_state *pstate)
{
  int x, y, xn, yn, i;
  long int tries=islemass*(2+islemass/20)+99;
  bool j;

  memset(height_map, '\0', sizeof(int) * map.xsize * map.ysize);
  xn = map.xsize / 2;
  yn = map.ysize / 2;
  hnat(xn, yn) = 1;
  pstate->n = yn - 1;
  pstate->w = xn - 1;
  pstate->s = yn + 2;
  pstate->e = xn + 2;
  i = islemass - 1;
  while (i > 0 && tries-->0) {
    get_random_map_position_from_state(&x, &y, pstate);
    MAP_TO_NATIVE_POS(&xn, &yn, x, y);
    if ((!near_singularity(x, y) || myrand(50) < 25 ) 
	&& hmap(x, y) == 0 && count_card_adjc_elevated_tiles(x, y) > 0) {
      hmap(x, y) = 1;
      i--;
      if (yn >= pstate->s - 1 && pstate->s < map.ysize - 2) {
	pstate->s++;
      }
      if (xn >= pstate->e - 1 && pstate->e < map.xsize - 2) {
	pstate->e++;
      }
      if (yn <= pstate->n && pstate->n > 2) {
	pstate->n--;
      }
      if (xn <= pstate->w && pstate->w > 2) {
	pstate->w--;
      }
    }
    if (i < islemass / 10) {
      for (yn = pstate->n; yn < pstate->s; yn++) {
	for (xn = pstate->w; xn < pstate->e; xn++) {
	  NATIVE_TO_MAP_POS(&x, &y, xn, yn);
	  if (hnat(xn, yn) == 0 && i > 0
	      && count_card_adjc_elevated_tiles(x, y) == 4) {
	    hnat(xn, yn) = 1;
            i--; 
          }
	}
      }
    }
  }
  if(tries<=0) {
    freelog(LOG_ERROR, "create_island ended early with %d/%d.",
	    islemass-i, islemass);
  }
  
  tries = map_num_tiles() / 4;	/* on a 40x60 map, there are 2400 places */
  while (!(j = place_island(pstate)) && (--tries) > 0) {
    /* nothing */
  }
  return j;
}

/*************************************************************************/

/**************************************************************************
  make an island, fill every tile type except plains
  note: you have to create big islands first.
  Return TRUE if successful.
  min_specific_island_size is a percent value.
***************************************************************************/
static bool make_island(int islemass, int starters,
			struct gen234_state *pstate,
			int min_specific_island_size)
{
  /* int may be only 2 byte ! */
  static long int tilefactor, balance, lastplaced;
  static long int riverbuck, mountbuck, desertbuck, forestbuck, swampbuck;

  int i;

  if (islemass == 0) {
    /* this only runs to initialise static things, not to actually
     * create an island. */
    balance = 0;
    pstate->isleindex = map.num_continents + 1;	/* 0= none, poles, then isles */

    checkmass = pstate->totalmass;

    /* caveat: this should really be sent to all players */
    if (pstate->totalmass > 3000)
      freelog(LOG_NORMAL, _("High landmass - this may take a few seconds."));

    i = map.riverlength + map.mountains
		+ map.deserts + map.forestsize + map.swampsize;
    i = i <= 90 ? 100 : i * 11 / 10;
    tilefactor = pstate->totalmass / i;
    riverbuck = -(long int) myrand(pstate->totalmass);
    mountbuck = -(long int) myrand(pstate->totalmass);
    desertbuck = -(long int) myrand(pstate->totalmass);
    forestbuck = -(long int) myrand(pstate->totalmass);
    swampbuck = -(long int) myrand(pstate->totalmass);
    lastplaced = pstate->totalmass;
  } else {

    /* makes the islands this big */
    islemass = islemass - balance;

    /* don't create continents without a number */
    if (pstate->isleindex >= MAP_NCONT) {
      return FALSE;
    }

    if (islemass > lastplaced + 1 + lastplaced / 50) {
      /* don't create big isles we can't place */
      islemass = lastplaced + 1 + lastplaced / 50;
    }

    /* isle creation does not perform well for nonsquare islands */
    if (islemass > (map.ysize - 6) * (map.ysize - 6)) {
      islemass = (map.ysize - 6) * (map.ysize - 6);
    }

    if (islemass > (map.xsize - 2) * (map.xsize - 2)) {
      islemass = (map.xsize - 2) * (map.xsize - 2);
    }

    i = islemass;
    if (i <= 0) {
      return FALSE;
    }
    islands[pstate->isleindex].starters = starters;
    assert(starters>=0);
    freelog(LOG_VERBOSE, "island %i", pstate->isleindex);

    /* keep trying to place an island, and decrease the size of
     * the island we're trying to create until we succeed.
     * If we get too small, return an error. */
    while (!create_island(i, pstate)) {
      if (i < islemass * min_specific_island_size / 100) {
	return FALSE;
      }
      i--;
    }
    i++;
    lastplaced= i;
    if(i*10>islemass){
      balance = i - islemass;
    }else{
      balance = 0;
    }

    freelog(LOG_VERBOSE, "ini=%d, plc=%d, bal=%ld, tot=%ld",
	    islemass, i, balance, checkmass);

    i *= tilefactor;

    riverbuck += map.riverlength * i;
    fill_island_rivers(1, &riverbuck, pstate);

    mountbuck += map.mountains * i;
    fill_island(20, &mountbuck,
		3,1, 3,1,
		T_HILLS, T_MOUNTAINS, T_HILLS, T_MOUNTAINS,
		pstate);
    desertbuck += map.deserts * i;
    fill_island(40, &desertbuck,
		map.deserts, map.deserts, map.deserts, map.deserts,
		T_DESERT, T_DESERT, T_DESERT, T_TUNDRA,
		pstate);
    forestbuck += map.forestsize * i;
    fill_island(60, &forestbuck,
		map.forestsize, map.swampsize, map.forestsize, map.swampsize,
		T_FOREST, T_JUNGLE, T_FOREST, T_TUNDRA,
		pstate);
    swampbuck += map.swampsize * i;
    fill_island(80, &swampbuck,
		map.swampsize, map.swampsize, map.swampsize, map.swampsize,
		T_SWAMP, T_SWAMP, T_SWAMP, T_SWAMP,
		pstate);

    pstate->isleindex++;
    map.num_continents++;
  }
  return TRUE;
}

/**************************************************************************
  fill ocean and make polar
**************************************************************************/
static void initworld(struct gen234_state *pstate)
{
  int i;
  height_map = fc_malloc(sizeof(int) * map.ysize * map.xsize);
  islands = fc_malloc((MAP_NCONT+1)*sizeof(struct isledata));
  create_placed_map(); /* land tiles which aren't placed yet */
  whole_map_iterate(x, y) {
    map_set_terrain(x, y, T_OCEAN);
    map_set_continent(x, y, 0);
    map_set_placed(x, y); /* not a land tile */
    map_clear_all_specials(x, y);
    map_set_owner(x, y, NULL);
  } whole_map_iterate_end;
  if (!map.alltemperate) {
    make_polar();
    /* Set poles numbers.  After the map is generated continents will 
     * be renumbered. */
    assign_continent_numbers(); 
  }
  make_island(0, 0, pstate, 0);
  for(i = 0; i <= map.num_continents; i++ ) {
      islands[i].starters = 0;
  }
}  

/* This variable is the Default Minimum Specific Island Size, 
 * ie the smallest size we'll typically permit our island, as a % of
 * the size we wanted. So if we ask for an island of size x, the island 
 * creation will return if it would create an island smaller than
 *  x * DMSIS / 100 */
#define DMSIS 10

/**************************************************************************
  island base map generators
**************************************************************************/
static void mapgenerator2(void)
{
  long int totalweight;
  struct gen234_state state;
  struct gen234_state *pstate = &state;
  int i;
  bool done = FALSE;
  int spares= 1; 
  /* constant that makes up that an island actually needs additional space */

  /* put 70% of land in big continents, 
   *     20% in medium, and 
   *     10% in small. */ 
  int bigfrac = 70, midfrac = 20, smallfrac = 10;

  if (map.landpercent > 85) {
    map.generator = 1;
    return;
  }

  pstate->totalmass = ((map.ysize - 6 - spares) * map.landpercent 
                       * (map.xsize - spares)) / 100;
  totalweight = 100 * game.nplayers;

  while (!done && bigfrac > midfrac) {
    done = TRUE;
    initworld(pstate);
    
    /* Create one big island for each player. */
    for (i = game.nplayers; i > 0; i--) {
      if (!make_island(bigfrac * pstate->totalmass / totalweight,
                      1, pstate, 95)) {
	/* we couldn't make an island at least 95% as big as we wanted,
	 * and since we're trying hard to be fair, we need to start again,
	 * with all big islands reduced slightly in size.
	 * Take the size reduction from the big islands and add it to the 
	 * small islands to keep overall landmass unchanged.
	 * Note that the big islands can get very small if necessary, and
	 * the smaller islands will not exist if we can't place them 
         * easily. */
	freelog(LOG_VERBOSE,
		"Island too small, trying again with all smaller islands.\n");
	midfrac += bigfrac * 0.01;
	smallfrac += bigfrac * 0.04;
	bigfrac *= 0.95;
	done = FALSE;	
	break;
      }
    }
  }

  if (bigfrac <= midfrac) {
    /* We could never make adequately big islands. */
    freelog(LOG_NORMAL, _("Falling back to generator %d."), 1);
    map.generator = 1;
    return;
  }

  /* Now place smaller islands, but don't worry if they're small,
   * or even non-existent. One medium and one small per player. */
  for (i = game.nplayers; i > 0; i--) {
    make_island(midfrac * pstate->totalmass / totalweight, 0, pstate, DMSIS);
  }
  for (i = game.nplayers; i > 0; i--) {
    make_island(smallfrac * pstate->totalmass / totalweight, 0, pstate, DMSIS);
  }

  make_plains();  
  remove_placed_map();
  free(height_map);
  height_map = NULL;

  if(checkmass>map.xsize+map.ysize+totalweight) {
    freelog(LOG_VERBOSE, "%ld mass left unplaced", checkmass);
  }
}

/**************************************************************************
On popular demand, this tries to mimick the generator 3 as best as possible.
**************************************************************************/
static void mapgenerator3(void)
{
  int spares= 1;
  int j=0;
  
  long int islandmass,landmass, size;
  long int maxmassdiv6=20;
  int bigislands;
  struct gen234_state state;
  struct gen234_state *pstate = &state;

  if ( map.landpercent > 80) {
    map.generator = 2;
    return;
  }

  pstate->totalmass =
      ((map.ysize - 6 - spares) * map.landpercent * (map.xsize - spares)) /
      100;

  bigislands= game.nplayers;

  landmass= ( map.xsize * (map.ysize-6) * map.landpercent )/100;
  /* subtracting the arctics */
  if( landmass>3*map.ysize+game.nplayers*3 ){
    landmass-= 3*map.ysize;
  }


  islandmass= (landmass)/(3*bigislands);
  if(islandmass<4*maxmassdiv6 )
    islandmass= (landmass)/(2*bigislands);
  if(islandmass<3*maxmassdiv6 && game.nplayers*2<landmass )
    islandmass= (landmass)/(bigislands);

  if (map.xsize < 40 || map.ysize < 40 || map.landpercent > 80) { 
      freelog(LOG_NORMAL, _("Falling back to generator %d."), 2); 
      map.generator = 2;
      return; 
    }

  if(islandmass<2)
    islandmass= 2;
  if(islandmass>maxmassdiv6*6)
    islandmass= maxmassdiv6*6;/* !PS: let's try this */

  initworld(pstate);

  while (pstate->isleindex - 2 <= bigislands && checkmass > islandmass
	 && ++j < 500) {
    make_island(islandmass, 1, pstate, DMSIS);
  }

  if(j==500){
    freelog(LOG_NORMAL, _("Generator 3 didn't place all big islands."));
  }
  
  islandmass= (islandmass*11)/8;
  /*!PS: I'd like to mult by 3/2, but starters might make trouble then*/
  if(islandmass<2)
    islandmass= 2;


  while (pstate->isleindex <= MAP_NCONT - 20 && checkmass > islandmass
	 && ++j < 1500) {
      if(j<1000)
	size = myrand((islandmass+1)/2+1)+islandmass/2;
      else
	size = myrand((islandmass+1)/2+1);
      if(size<2) size=2;

      make_island(size, (pstate->isleindex - 2 <= game.nplayers) ? 1 : 0,
		  pstate, DMSIS);
  }

  make_plains();  
  remove_placed_map();
  free(height_map);
  height_map = NULL;
    
  if(j==1500) {
    freelog(LOG_NORMAL, _("Generator 3 left %li landmass unplaced."), checkmass);
  } else if (checkmass > map.xsize + map.ysize) {
    freelog(LOG_VERBOSE, "%ld mass left unplaced", checkmass);
  }

}

/**************************************************************************
...
**************************************************************************/
static void mapgenerator4(void)
{
  int bigweight=70;
  int spares= 1;
  int i;
  long int totalweight;
  struct gen234_state state;
  struct gen234_state *pstate = &state;


  /* no islands with mass >> sqr(min(xsize,ysize)) */

  if ( game.nplayers<2 || map.landpercent > 80) {
    map.generator = 3;
    return;
  }

  if(map.landpercent>60)
    bigweight=30;
  else if(map.landpercent>40)
    bigweight=50;
  else
    bigweight=70;

  spares= (map.landpercent-5)/30;

  pstate->totalmass =
      ((map.ysize - 6 - spares) * map.landpercent * (map.xsize - spares)) /
      100;

  /*!PS: The weights NEED to sum up to totalweight (dammit) */
  totalweight = (30 + bigweight) * game.nplayers;

  initworld(pstate);

  i = game.nplayers / 2;
  if ((game.nplayers % 2) == 1) {
    make_island(bigweight * 3 * pstate->totalmass / totalweight, 3, 
		pstate, DMSIS);
  } else {
    i++;
  }
  while ((--i) > 0) {
    make_island(bigweight * 2 * pstate->totalmass / totalweight, 2,
		pstate, DMSIS);
  }
  for (i = game.nplayers; i > 0; i--) {
    make_island(20 * pstate->totalmass / totalweight, 0, pstate, DMSIS);
  }
  for (i = game.nplayers; i > 0; i--) {
    make_island(10 * pstate->totalmass / totalweight, 0, pstate, DMSIS);
  }
  make_plains();  
  remove_placed_map();
  free(height_map);
  height_map = NULL;

  if(checkmass>map.xsize+map.ysize+totalweight) {
    freelog(LOG_VERBOSE, "%ld mass left unplaced", checkmass);
  }
}

#undef DMSIS

/**************************************************************************
  Recursive function which does the work for generator 5.

  All (x0,y0) and (x1,y1) are in native coordinates.
**************************************************************************/
static void gen5rec(int step, int x0, int y0, int x1, int y1)
{
  int val[2][2];
  int x1wrap = x1; /* to wrap correctly */ 
  int y1wrap = y1; 

  /* All x and y values are native. */

  if (((y1 - y0 <= 0) || (x1 - x0 <= 0)) 
      || ((y1 - y0 == 1) && (x1 - x0 == 1))) {
    return;
  }

  if (x1 == map.xsize)
    x1wrap = 0;
  if (y1 == map.ysize)
    y1wrap = 0;

  val[0][0] = hnat(x0, y0);
  val[0][1] = hnat(x0, y1wrap);
  val[1][0] = hnat(x1wrap, y0);
  val[1][1] = hnat(x1wrap, y1wrap);

  /* set midpoints of sides to avg of side's vertices plus a random factor */
  /* unset points are zero, don't reset if set */
#define set_midpoints(X, Y, V)						\
  do_in_map_pos(map_x, map_y, (X), (Y)) {                               \
    if (!near_singularity(map_x, map_y)					\
	&& map_temperature(map_x, map_y) >  ICE_BASE_LEVEL/2		\
	&& hnat((X), (Y)) == 0) {					\
      hnat((X), (Y)) = (V);						\
    }									\
  } do_in_map_pos_end;

  set_midpoints((x0 + x1) / 2, y0,
		(val[0][0] + val[1][0]) / 2 + myrand(step) - step / 2);
  set_midpoints((x0 + x1) / 2,  y1wrap,
		(val[0][1] + val[1][1]) / 2 + myrand(step) - step / 2);
  set_midpoints(x0, (y0 + y1)/2,
		(val[0][0] + val[0][1]) / 2 + myrand(step) - step / 2);  
  set_midpoints(x1wrap,  (y0 + y1) / 2,
		(val[1][0] + val[1][1]) / 2 + myrand(step) - step / 2);  

  /* set middle to average of midpoints plus a random factor, if not set */
  set_midpoints((x0 + x1) / 2, (y0 + y1) / 2,
		((val[0][0] + val[0][1] + val[1][0] + val[1][1]) / 4
		 + myrand(step) - step / 2));

#undef set_midpoints

  /* now call recursively on the four subrectangles */
  gen5rec(2 * step / 3, x0, y0, (x1 + x0) / 2, (y1 + y0) / 2);
  gen5rec(2 * step / 3, x0, (y1 + y0) / 2, (x1 + x0) / 2, y1);
  gen5rec(2 * step / 3, (x1 + x0) / 2, y0, x1, (y1 + y0) / 2);
  gen5rec(2 * step / 3, (x1 + x0) / 2, (y1 + y0) / 2, x1, y1);
}

/**************************************************************************
Generator 5 makes earthlike worlds with one or more large continents and
a scattering of smaller islands. It does so by dividing the world into
blocks and on each block raising or lowering the corners, then the 
midpoints and middle and so on recursively.  Fiddling with 'xdiv' and 
'ydiv' will change the size of the initial blocks and, if the map does not 
wrap in at least one direction, fiddling with 'avoidedge' will change the 
liklihood of continents butting up to non-wrapped edges.

  All X and Y values used in this function are in native coordinates.
**************************************************************************/
static void mapgenerator5(void)
{
  const bool xnowrap = !topo_has_flag(TF_WRAPX);
  const bool ynowrap = !topo_has_flag(TF_WRAPY);

  /* 
   * How many blocks should the x and y directions be divided into
   * initially. 
   */
  const int xdiv = 6;		
  const int ydiv = 5;

  int xdiv2 = xdiv + (xnowrap ? 1 : 0);
  int ydiv2 = ydiv + (ynowrap ? 1 : 0);

  int xmax = map.xsize - (xnowrap ? 1 : 0);
  int ymax = map.ysize - (ynowrap ? 1 : 0);
  int xn, yn;
  /* just need something > log(max(xsize, ysize)) for the recursion */
  int step = map.xsize + map.ysize; 
  /* edges are avoided more strongly as this increases */
  int avoidedge = (50 - map.landpercent) * step / 100 + step / 3; 

  height_map = fc_malloc(sizeof(int) * MAX_MAP_INDEX);

 /* initialize map */
  INITIALIZE_ARRAY(height_map, MAX_MAP_INDEX, 0);

  /* set initial points */
  for (xn = 0; xn < xdiv2; xn++) {
    for (yn = 0; yn < ydiv2; yn++) {
      do_in_map_pos(x, y, (xn * xmax / xdiv), (yn * ymax / ydiv)) {
	/* set initial points */
	hmap(x, y) = myrand(2 * step) - (2 * step) / 2;

	if (near_singularity(x, y)) {
	  /* avoid edges (topological singularities) */
	  hmap(x, y) -= avoidedge;
	}

	if (map_temperature(x, y) <= ICE_BASE_LEVEL / 2 ) {
	  /* separate poles and avoid too much land at poles */
	  hmap(x, y) -= myrand(avoidedge);
	}
      } do_in_map_pos_end;
    }
  }

  /* calculate recursively on each block */
  for (xn = 0; xn < xdiv; xn++) {
    for (yn = 0; yn < ydiv; yn++) {
      gen5rec(step, xn * xmax / xdiv, yn * ymax / ydiv, 
	      (xn + 1) * xmax / xdiv, (yn + 1) * ymax / ydiv);
    }
  }

  /* put in some random fuzz */
  whole_map_iterate(x, y) {
    hmap(x, y) = 8 * hmap(x, y) + myrand(4) - 2;
  } whole_map_iterate_end;

  make_land();
  free(height_map);
  height_map = NULL;
}
