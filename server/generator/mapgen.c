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

#include "height_map.h"
#include "mapgen_topology.h"
#include "startpos.h"
#include "temperature_map.h"
#include "utilities.h"


/* Old-style terrain enumeration: deprecated. */
enum {
  T_ARCTIC, T_DESERT, T_FOREST, T_GRASSLAND, T_HILLS, T_JUNGLE,
  T_MOUNTAINS, T_OCEAN, T_PLAINS, T_SWAMP, T_TUNDRA,
};

/* Wrappers for easy access.  They are a macros so they can be a lvalues.*/
#define rmap(x, y) (river_map[map_pos_to_index(x, y)])

static void make_huts(int number);
static void add_specials(int prob);
static void mapgenerator1(void);
static void mapgenerator2(void);
static void mapgenerator3(void);
static void mapgenerator4(void);
static void mapgenerator5(void);
static void smooth_map(void);
static void adjust_terrain_param(void);

#define RIVERS_MAXTRIES 32767
enum river_map_type {RS_BLOCKED = 0, RS_RIVER = 1};

/* Array needed to mark tiles as blocked to prevent a river from
   falling into itself, and for storing rivers temporarly.
   A value of 1 means blocked.
   A value of 2 means river.                            -Erik Sigra */
static int *river_map;

#define HAS_POLES (map.temperature < 70 && !map.alltemperate  )

/* these are the old parameters of terrains types do in %
   TODO: this deppend of the hardcoded terrains
   this has to work from a terrains rules-set */
static int forest_pct = 0, desert_pct = 0, swamp_pct = 0, mountain_pct = 0,
    jungle_pct = 0, river_pct = 0;
 
/****************************************************************************
 * Conditions used mainly in rand_map_pos_characteristic()
 ****************************************************************************/
/* WETNESS */

/* necessary condition of deserts placement */
#define map_pos_is_dry(x, y)						\
  (map_colatitude((x), (y)) <= DRY_MAX_LEVEL				\
   && map_colatitude((x), (y)) > DRY_MIN_LEVEL				\
   && count_ocean_near_tile((x), (y), FALSE, TRUE) <= 35)
typedef enum { WC_ALL = 200, WC_DRY, WC_NDRY } wetness_c;

/* MISCELANEOUS (OTHER CONDITIONS) */

/* necessary condition of swamp placement */
static int hmap_low_level = 0;
#define ini_hmap_low_level() \
{ \
hmap_low_level = (4 * swamp_pct  * \
     (hmap_max_level - hmap_shore_level)) / 100 + hmap_shore_level; \
}
/* should be used after having hmap_low_level initialized */
#define map_pos_is_low(x, y) ((hmap((x), (y)) < hmap_low_level))

typedef enum { MC_NONE, MC_LOW, MC_NLOW } miscellaneous_c;

/***************************************************************************
 These functions test for conditions used in rand_map_pos_characteristic 
***************************************************************************/

/***************************************************************************
  Checks if the given location satisfy some wetness condition
***************************************************************************/
static bool test_wetness(int x, int y, wetness_c c)
{
  switch(c) {
  case WC_ALL:
    return TRUE;
  case WC_DRY:
    return map_pos_is_dry(x, y);
  case WC_NDRY:
    return !map_pos_is_dry(x, y);
  }
  assert(0);
  return FALSE;
}

/***************************************************************************
  Checks if the given location satisfy some miscellaneous condition
***************************************************************************/
static bool test_miscellaneous(int x, int y, miscellaneous_c c)
{
  switch(c) {
  case MC_NONE:
    return TRUE;
  case MC_LOW:
    return map_pos_is_low(x, y);
  case MC_NLOW:
    return !map_pos_is_low(x, y);
  }
  assert(0);
  return FALSE;
}

/***************************************************************************
  Passed as data to rand_map_pos_filtered() by rand_map_pos_characteristic()
***************************************************************************/
struct DataFilter {
  wetness_c wc;
  temperature_type tc;
  miscellaneous_c mc;
};

/****************************************************************************
  A filter function to be passed to rand_map_pos_filtered().  See
  rand_map_pos_characteristic for more explanation.
****************************************************************************/
static bool condition_filter(int map_x, int map_y, void *data)
{
  struct DataFilter *filter = data;

  return  not_placed(map_x, map_y) 
       && tmap_is(map_x, map_y, filter->tc) 
       && test_wetness(map_x, map_y, filter->wc) 
       && test_miscellaneous(map_x, map_y, filter->mc) ;
}

/****************************************************************************
  Return random map coordinates which have some conditions and which are
  not yet placed on pmap.
  Returns FALSE if there is no such position.
****************************************************************************/
static bool rand_map_pos_characteristic(int *map_x, int *map_y,
					wetness_c wc,
					temperature_type tc,
					miscellaneous_c mc )
{
  struct DataFilter filter;

  filter.wc = wc;
  filter.tc = tc;
  filter.mc = mc;
  return rand_map_pos_filtered(map_x, map_y, &filter, condition_filter);
}

/**************************************************************************
  we don't want huge areas of grass/plains, 
  so we put in a hill here and there, where it gets too 'clean' 

  Return TRUE if the terrain at the given map position is "clean".  This
  means that all the terrain for 2 squares around it is not mountain or hill.
****************************************************************************/
static bool terrain_is_too_flat(int map_x, int map_y,int thill, int my_height)
{
  int higher_than_me = 0;
  square_iterate(map_x, map_y, 2, x1, y1) {
    if (hmap(x1, y1) > thill) {
      return FALSE;
    }
    if (hmap(x1, y1) > my_height) {
      if (map_distance(map_x, map_y, x1, y1) == 1) {
	return FALSE;
      }
      if (++higher_than_me > 2) {
	return FALSE;
      }
    }
  } square_iterate_end;

  if ((thill - hmap_shore_level) * higher_than_me  > 
      (my_height - hmap_shore_level) * 4) {
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
  we don't want huge areas of hill/mountains, 
  so we put in a plains here and there, where it gets too 'heigh' 

  Return TRUE if the terrain at the given map position is too heigh.
****************************************************************************/
static bool terrain_is_too_high(int map_x, int map_y,int thill, int my_height)
{
  square_iterate(map_x, map_y, 1, x1, y1) {
    if (hmap(x1, y1) + (hmap_max_level - hmap_mountain_level) / 5 < thill) {
      return FALSE;
    }
  } square_iterate_end;
  return TRUE;
}

/**************************************************************************
  make_relief() will convert all squares that are higher than thill to
  mountains and hills. Note that thill will be adjusted according to
  the map.steepness value, so increasing map.mountains will result in
  more hills and mountains.
**************************************************************************/
static void make_relief(void)
{
  /* Calculate the mountain level.  map.mountains specifies the percentage
   * of land that is turned into hills and mountains. */
  hmap_mountain_level = ((hmap_max_level - hmap_shore_level)
			 * (100 - map.steepness)) / 100 + hmap_shore_level;

  whole_map_iterate(x, y) {
    if (not_placed(x,y) &&
	((hmap_mountain_level < hmap(x, y) && 
	  (myrand(10) > 5 
	   || !terrain_is_too_high(x, y, hmap_mountain_level, hmap(x, y))))
	 || terrain_is_too_flat(x, y, hmap_mountain_level, hmap(x, y)))) {
      /* Randomly place hills and mountains on "high" tiles.  But don't
       * put hills near the poles (they're too green). */
      if (myrand(100) > 70 || tmap_is(x, y, TT_NHOT)) {
	map_set_terrain(x, y, T_MOUNTAINS);
	map_set_placed(x, y);
      } else {
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
  whole_map_iterate(map_x, map_y) {  
    if (tmap_is(map_x, map_y, TT_FROZEN )
	||
	(tmap_is(map_x, map_y, TT_COLD ) &&
	 (myrand(10) > 7) &&
	 is_temperature_type_near(map_x, map_y, TT_FROZEN))) { 
      map_set_terrain(map_x, map_y, T_ARCTIC);
    }
  } whole_map_iterate_end;
}

/*************************************************************************
 if separatepoles is set, return false if this tile has to keep ocean
************************************************************************/
static bool ok_for_separate_poles(int x, int y) 
{
  if (!map.separatepoles) {
    return TRUE;
  }
  adjc_iterate(x, y, x1, y1) {
    if(!is_ocean(map_get_terrain(x1, y1))
       && map_get_continent(x1, y1 ) != 0) {
      return FALSE;
    }
  } adjc_iterate_end;
  return TRUE;
}

/****************************************************************************
  Place untextured land at the poles.  This is used by generators 1 and 5.
****************************************************************************/
static void make_polar_land(void)
{
  assign_continent_numbers();
  whole_map_iterate(map_x, map_y) {
    if ((tmap_is(map_x, map_y, TT_FROZEN ) &&
	ok_for_separate_poles(map_x, map_y))
	||
	(tmap_is(map_x, map_y, TT_COLD ) &&
	 (myrand(10) > 7) &&
	 is_temperature_type_near(map_x, map_y, TT_FROZEN) &&
	 ok_for_separate_poles(map_x, map_y))) {
	map_set_terrain(map_x, map_y, T_UNKNOWN);
	map_set_continent(map_x, map_y, 0);
    } 
  } whole_map_iterate_end;
}

/**************************************************************************
  Recursively generate terrains.
**************************************************************************/
static void place_terrain(int x, int y, int diff, 
                           Terrain_type_id ter, int *to_be_placed,
			   wetness_c        wc,
			   temperature_type tc,
			   miscellaneous_c  mc)
{
  if (*to_be_placed <= 0) {
    return;
  }
  assert(not_placed(x, y));
  map_set_terrain(x, y, ter);
  map_set_placed(x, y);
  (*to_be_placed)--;
  
  cardinal_adjc_iterate(x, y, x1, y1) {
    int Delta = abs(map_colatitude(x1, y1) - map_colatitude(x, y)) / L_UNIT
	+ abs(hmap(x1, y1) - (hmap(x, y))) /  H_UNIT;
    if (not_placed(x1, y1) 
	&& tmap_is(x1, y1, tc) 
	&& test_wetness(x1, y1, wc)
 	&& test_miscellaneous(x1, y1, mc)
	&& Delta < diff 
	&& myrand(10) > 4) {
	place_terrain(x1, y1, diff - 1 - Delta, ter, to_be_placed, wc, tc, mc);
    }
  } cardinal_adjc_iterate_end;
}

/**************************************************************************
  a simple function that adds plains grassland or tundra to the 
  current location.
**************************************************************************/
static void make_plain(int x, int y, int *to_be_placed )
{
  /* in cold place we get tundra instead */
  if (tmap_is(x, y, TT_FROZEN)) {
    map_set_terrain(x, y, T_ARCTIC); 
  } else if (tmap_is(x, y, TT_COLD)) {
    map_set_terrain(x, y, T_TUNDRA); 
  } else {
    if (myrand(100) > 50) {
      map_set_terrain(x, y, T_GRASSLAND);
    } else {
      map_set_terrain(x, y, T_PLAINS);
    }
  }
  map_set_placed(x, y);
  (*to_be_placed)--;
}

/**************************************************************************
  make_plains converts all not yet placed terrains to plains (tundra, grass) 
  used by generators 2-4
**************************************************************************/
static void make_plains(void)
{
  int to_be_placed;
  whole_map_iterate(x, y) {
    if (not_placed(x, y)) {
      to_be_placed = 1;
      make_plain(x, y, &to_be_placed);
    }
  } whole_map_iterate_end;
}
/**************************************************************************
 This place randomly a cluster of terrains with some characteristics
 **************************************************************************/
#define PLACE_ONE_TYPE(count, alternate, ter, wc, tc, mc, weight) \
  if((count) > 0) {                                       \
    int x, y; \
    /* Place some terrains */                             \
    if (rand_map_pos_characteristic(&x, &y, (wc), (tc), (mc))) {  \
      place_terrain(x, y, (weight), (ter), &(count), (wc),(tc), (mc));   \
    } else {                                                             \
      /* If rand_map_pos_temperature returns FALSE we may as well stop*/ \
      /* looking for this time and go to alternate type. */              \
      (alternate) += (count); \
      (count) = 0;            \
    }                         \
  }

/**************************************************************************
  make_terrains calls make_forest, make_dessert,etc  with random free 
  locations until there  has been made enough.
 Comment: funtions as make_swamp, etc. has to have a non 0 probability
 to place one terrains in called position. Else make_terrains will get
 in a infinite loop!
**************************************************************************/
static void make_terrains(void)
{
  int total = 0;
  int forests_count = 0;
  int jungles_count = 0;
  int deserts_count = 0;
  int alt_deserts_count = 0;
  int plains_count = 0;
  int swamps_count = 0;

  whole_map_iterate(x, y) {
    if(not_placed(x,y)) {
     total++;
    }
  } whole_map_iterate_end;

  forests_count = total * forest_pct / (100 - mountain_pct);
  jungles_count = total * jungle_pct / (100 - mountain_pct);
 
  deserts_count = total * desert_pct / (100 - mountain_pct); 
  swamps_count = total * swamp_pct  / (100 - mountain_pct);

  /* grassland, tundra,arctic and plains is counted in plains_count */
  plains_count = total - forests_count - deserts_count
      - swamps_count - jungles_count;

  /* the placement loop */
  do {
    
    PLACE_ONE_TYPE(forests_count , plains_count, T_FOREST,
		   WC_ALL, TT_NFROZEN, MC_NONE, 60);
    PLACE_ONE_TYPE(jungles_count, forests_count , T_JUNGLE,
		   WC_ALL, TT_TROPICAL, MC_NONE, 50);
    PLACE_ONE_TYPE(swamps_count, forests_count , T_SWAMP,
		   WC_NDRY, TT_HOT, MC_LOW, 50);
    PLACE_ONE_TYPE(deserts_count, alt_deserts_count , T_DESERT,
		   WC_DRY, TT_NFROZEN, MC_NLOW, 80);
    PLACE_ONE_TYPE(alt_deserts_count, plains_count , T_DESERT,
		   WC_ALL, TT_NFROZEN, MC_NLOW, 40);
 
  /* make the plains and tundras */
    if (plains_count > 0) {
      int x, y;

      /* Don't use any restriction here ! */
      if (rand_map_pos_characteristic(&x, &y,  WC_ALL, TT_ALL, MC_NONE)){
	make_plain(x,y, &plains_count);
      } else {
	/* If rand_map_pos_temperature returns FALSE we may as well stop
	 * looking for plains. */
	plains_count = 0;
      }
    }
  } while (forests_count > 0 || jungles_count > 0 
	   || deserts_count > 0 || alt_deserts_count > 0 
	   || plains_count > 0 || swamps_count > 0 );
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
  return (count_special_near_tile(x, y, TRUE, FALSE, S_RIVER) > 1) ? 1 : 0;
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
  return 100 - count_ocean_near_tile(x, y, TRUE, TRUE);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_river(int x, int y)
{
  return 100 - count_special_near_tile(x, y, TRUE, TRUE, S_RIVER);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_highlands(int x, int y)
{
  return (count_terrain_near_tile(x, y, TRUE, TRUE, T_HILLS)
	  + 2 * count_terrain_near_tile(x, y, TRUE, TRUE, T_MOUNTAINS));
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
  return 100 - count_terrain_near_tile(x, y, TRUE, TRUE, T_SWAMP);
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

     Possible values: 0-100

 * Adjacent river:
     (river_test_adjacent_river)
     Rivers must flow down to areas near other rivers when possible:

     Possible values: 0-100
					
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
    if (count_special_near_tile(x, y, TRUE, TRUE, S_RIVER) > 0
	|| count_ocean_near_tile(x, y, TRUE, TRUE) > 0
        || (map_get_terrain(x, y) == T_ARCTIC 
	    && map_colatitude(x, y) < 0.8 * COLD_LEVEL)) { 

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
    river_pct *
      /* The size of the map (poles counted in river_pct). */
      map_num_tiles() *
      /* Rivers need to be on land only. */
      map.landpercent /
      /* Adjustment value. Tested by me. Gives no rivers with 'set
	 rivers 0', gives a reasonable amount of rivers with default
	 settings and as many rivers as possible with 'set rivers 100'. */
      5325;

  /* The number of river tiles that have been set. */
  int current_riverlength = 0;

  int i; /* Loop variable. */

  /* Counts the number of iterations (should increase with 1 during
     every iteration of the main loop in this function).
     Is needed to stop a potentially infinite loop. */
  int iteration_counter = 0;

  create_placed_map(); /* needed bu rand_map_characteristic */
  set_all_ocean_tiles_placed();

  river_map = fc_malloc(sizeof(int) * MAX_MAP_INDEX);

  /* The main loop in this function. */
  while (current_riverlength < desirable_riverlength
	 && iteration_counter < RIVERS_MAXTRIES) {

    if (!rand_map_pos_characteristic(&x, &y, 
                                     WC_ALL, TT_NFROZEN, MC_NLOW)) {
	break; /* mo more spring places */
    }

    /* Check if it is suitable to start a river on the current tile.
     */
    if (
	/* Don't start a river on ocean. */
	!is_ocean(map_get_terrain(x, y))

	/* Don't start a river on river. */
	&& !map_has_special(x, y, S_RIVER)

	/* Don't start a river on a tile is surrounded by > 1 river +
	   ocean tile. */
	&& (count_special_near_tile(x, y, TRUE, FALSE, S_RIVER)
	    + count_ocean_near_tile(x, y, TRUE, FALSE) <= 1)

	/* Don't start a river on a tile that is surrounded by hills or
	   mountains unless it is hard to find somewhere else to start
	   it. */
	&& (count_terrain_near_tile(x, y, TRUE, TRUE, T_HILLS)
	    + count_terrain_near_tile(x, y, TRUE, TRUE, T_MOUNTAINS) < 90
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
	    map_set_placed(x1, y1);
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
  destroy_placed_map();
  river_map = NULL;
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
    } else if (map_colatitude(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_colatitude(x, y) / (2.5 * ICE_BASE_LEVEL);
    } else if (map.separatepoles 
	       && map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 0.1;
    } else if (map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= map_colatitude(x, y) / (2.5 * ICE_BASE_LEVEL);
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
    if (hmap(x, y) == 0 || map_colatitude(x, y) == 0) {
      /* Nothing. */
    } else if (map_colatitude(x, y) < 2 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) / map_colatitude(x, y);
    } else if (map.separatepoles 
	       && map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= 10;
    } else if (map_colatitude(x, y) <= 2.5 * ICE_BASE_LEVEL) {
      hmap(x, y) *= (2.5 * ICE_BASE_LEVEL) /  map_colatitude(x, y);
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
  adjust_int_map(height_map, hmap_max_level);
  if (HAS_POLES) {
    normalize_hmap_poles();
  }
  hmap_shore_level = (hmap_max_level * (100 - map.landpercent)) / 100;
  ini_hmap_low_level();
  whole_map_iterate(x, y) {
    map_set_terrain(x, y, T_UNKNOWN); /* set as oceans count is used */
    if (hmap(x, y) < hmap_shore_level) {
      map_set_terrain(x, y, T_OCEAN);
    }
  } whole_map_iterate_end;
  if (HAS_POLES) {
    renormalize_hmap_poles();
  } 

  create_tmap(TRUE); /* base temperature map, need hmap and oceans */
  
  if (HAS_POLES) { /* this is a hack to terrains set with not frizzed oceans*/
    make_polar_land(); /* make extra land at poles*/
  }

  create_placed_map(); /* here it means land terrains to be placed */
  set_all_ocean_tiles_placed();
  make_relief(); /* base relief on map */
  make_terrains(); /* place all exept mountains and hill */
  destroy_placed_map();

  make_rivers(); /* use a new placed_map. destroy older before call */

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

/**************************************************************************
  See stdinhand.c for information on map generation methods.

FIXME: Some continent numbers are unused at the end of this function, fx
       removed completely by remove_tiny_islands.
       When this function is finished various data is written to "islands",
       indexed by continent numbers, so a simple renumbering would not
       work...

  If "autosize" is specified then mapgen will automatically size the map
  based on the map.size server parameter and the specified topology.  If
  not map.xsize and map.ysize will be used.
**************************************************************************/
void map_fractal_generate(bool autosize)
{
  /* save the current random state: */
  RANDOM_STATE rstate = get_myrand_state();
 
  if (map.seed==0)
    map.seed = (myrand(MAX_UINT32) ^ time(NULL)) & (MAX_UINT32 >> 1);

  mysrand(map.seed);

  /* don't generate tiles with mapgen==0 as we've loaded them from file */
  /* also, don't delete (the handcrafted!) tiny islands in a scenario */
  if (map.generator != 0) {
    generator_init_topology(autosize);
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

  if(!temperature_is_initialized()) {
    create_tmap(FALSE);
  }

  if(!map.have_specials) /* some scenarios already provide specials */
    add_specials(map.riches); /* hvor mange promiller specials oensker vi*/

  if (!map.have_huts)
    make_huts(map.huts); /* Vi vil have store promiller; man kan aldrig faa
			    for meget oel! */

  /* restore previous random state: */
  set_myrand_state(rstate);
  destroy_tmap();
}

/**************************************************************************
 Convert parameters from the server into terrains percents parameters for
 the generators
**************************************************************************/
static void adjust_terrain_param(void)
{
  int polar = 2 * ICE_BASE_LEVEL * map.landpercent / MAX_COLATITUDE ;
  float factor =(100.0 - polar - map.steepness * 0.8 ) / 10000;


  mountain_pct = factor * map.steepness * 90;
  /* 40 % if wetness == 50 & */
  forest_pct = factor * (map.wetness * 60 + 1000) ; 
  jungle_pct = forest_pct * (MAX_COLATITUDE - TROPICAL_LEVEL)
     /  (MAX_COLATITUDE * 2);
  forest_pct -= jungle_pct;
  /* 3 - 11 % */
  river_pct = (100 - polar) * (3 + map.wetness / 12) / 100;
  /* 6 %  if wetness == 50 && temperature == 50 */
  swamp_pct = factor * (map.wetness * 6 + map.temperature * 6);
  desert_pct = factor * (map.temperature * 10 + (100 - map.wetness) * 10) ;
  
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
	|| map_colatitude(x, y) <= ICE_BASE_LEVEL / 2) { 
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
    if (rand_map_pos_characteristic(&x, &y, WC_ALL, TT_NFROZEN, MC_NONE)) {
      if (is_ocean(map_get_terrain(x, y))) {
	map_set_placed(x, y); /* not good for a hut */
      } else {
	number--;
	map_set_special(x, y, S_HUT);
	set_placed_near_pos(x, y, 3);
	    /* Don't add to islands[].goodies because islands[] not
	       setup at this point, except for generator>1, but they
	       have pre-set starters anyway. */
      }
    }
  }
  destroy_placed_map();
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
	if (map_colatitude(x, y) < COLD_LEVEL) {
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
	   || count_special_near_tile(x, y, FALSE, TRUE, S_RIVER) > 0
	   || myrand(100) < 50)
	  && (!is_cardinally_adj_to_ocean(x, y) || myrand(100) < coast)) {
	if (is_water_adjacent_to_tile(x, y)
	    && count_ocean_near_tile(x, y, FALSE, TRUE) < 50
            && count_special_near_tile(x, y, FALSE, TRUE, S_RIVER) < 35) {
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

    i = river_pct + mountain_pct
		+ desert_pct + forest_pct + swamp_pct;
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

    riverbuck += river_pct * i;
    fill_island_rivers(1, &riverbuck, pstate);

    mountbuck += mountain_pct * i;
    fill_island(20, &mountbuck,
		3,1, 3,1,
		T_HILLS, T_MOUNTAINS, T_HILLS, T_MOUNTAINS,
		pstate);
    desertbuck += desert_pct * i;
    fill_island(40, &desertbuck,
		1, 1, 1, 1,
		T_DESERT, T_DESERT, T_DESERT, T_TUNDRA,
		pstate);
    forestbuck += forest_pct * i;
    fill_island(60, &forestbuck,
		forest_pct, swamp_pct, forest_pct, swamp_pct,
		T_FOREST, T_JUNGLE, T_FOREST, T_TUNDRA,
		pstate);
    swampbuck += swamp_pct * i;
    fill_island(80, &swampbuck,
		1, 1, 1, 1,
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
  create_tmap(FALSE);
  whole_map_iterate(x, y) {
    map_set_terrain(x, y, T_OCEAN);
    map_set_continent(x, y, 0);
    map_set_placed(x, y); /* not a land tile */
    map_clear_all_specials(x, y);
    map_set_owner(x, y, NULL);
  } whole_map_iterate_end;
  if (HAS_POLES) {
    make_polar();
  }
  
  /* Set poles numbers.  After the map is generated continents will 
   * be renumbered. */
  assign_continent_numbers(); 

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

  assert(!placed_map_is_initialized());

  while (!done && bigfrac > midfrac) {
    done = TRUE;

    if (placed_map_is_initialized()) {
      destroy_placed_map();
    }

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

    /* init world created this map, destroy it before abort */
    destroy_placed_map();
    free(height_map);
    height_map = NULL;
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
  destroy_placed_map();
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
  destroy_placed_map();
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
  destroy_placed_map();
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
	&& map_colatitude(map_x, map_y) >  ICE_BASE_LEVEL/2		\
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

	if (map_colatitude(x, y) <= ICE_BASE_LEVEL / 2 ) {
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
