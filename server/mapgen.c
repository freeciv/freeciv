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
#include "mem.h"
#include "rand.h"
#include "shared.h"

#include "mapgen.h"

/* Wrapper for easy access.  It's a macro so it can be a lvalue. */
#define hmap(x,y) (height_map[map_inx(x, y)])

static void make_huts(int number);
static void add_specials(int prob);
static void mapgenerator1(void);
static void mapgenerator2(void);
static void mapgenerator3(void);
static void mapgenerator4(void);
static void smooth_map(void);
static void adjust_map(int minval);

#define RIVERS_MAXTRIES 32767
enum river_map_type {RS_BLOCKED = 1, RS_RIVER = 2};

/* Array needed to mark tiles as blocked to prevent a river from
   falling into itself, and for storing rivers temporarly.
   A value of 1 means blocked.
   A value of 2 means river.                            -Erik Sigra */
static int *river_map;

static int *height_map;
static int maxval=0;
static int forests=0;

struct isledata {
  int x,y;                        /* upper left corner of the islands */
  int goodies;
  int starters;
};
static struct isledata *islands;

/* this is used for generator>1 */
#define MAP_NCONT 255

/**************************************************************************
  make_mountains() will convert all squares that are higher than thill to
  mountains and hills. Notice that thill will be adjusted according to
  the map.mountains value, so increase map.mountains and you'll get more 
  hills and mountains (and vice versa).
**************************************************************************/
static void make_mountains(int thill)
{
  int mount;
  int j;
  for (j=0;j<10;j++) {
    mount=0;
    whole_map_iterate(x, y) {
      if (hmap(x, y)>thill) 
	mount++;
    } whole_map_iterate_end;
    if (mount<((map.xsize*map.ysize)*map.mountains)/1000) 
      thill*=95;
    else 
      thill*=105;
    thill/=100;
  }
  
  whole_map_iterate(x, y) {
    if (hmap(x, y)>thill &&map_get_terrain(x,y)!=T_OCEAN) { 
      if (myrand(100)>75) 
	map_set_terrain(x, y, T_MOUNTAINS);
      else if (myrand(100)>25) 
	map_set_terrain(x, y, T_HILLS);
    }
  } whole_map_iterate_end;
}

/**************************************************************************
 add arctic and tundra squares in the arctic zone. 
 (that is the top 10%, and low 10% of the map)
**************************************************************************/
static void make_polar(void)
{
  int y,x;

  for (y=0;y<map.ysize/10;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((hmap(x, y)+(map.ysize/10-y*25)>myrand(maxval) &&
	   map_get_terrain(x,y)==T_GRASSLAND) || y==0) { 
	if (y<2)
	  map_set_terrain(x, y, T_ARCTIC);
	else
	  map_set_terrain(x, y, T_TUNDRA);
	  
      } 
    }
  }
  for (y=map.ysize*9/10;y<map.ysize;y++) {
    for (x=0;x<map.xsize;x++) {
      if ((hmap(x, y)+(map.ysize/10-(map.ysize-y)*25)>myrand(maxval) &&
	   map_get_terrain(x, y)==T_GRASSLAND) || y==map.ysize-1) {
	if (y>map.ysize-3)
	  map_set_terrain(x, y, T_ARCTIC);
	else
	  map_set_terrain(x, y, T_TUNDRA);
      }
    }
  }

  /* only arctic and tundra allowed at the poles (first and last two lines,
     as defined in make_passable() ), to be consistent with generator>1. 
     turn every land tile on the second lines that is not arctic into tundra,
     since the first lines has already been set to all arctic above. */
  for (x=0;x<map.xsize;x++) {
    if (map_get_terrain(x, 1)!=T_ARCTIC &&
	map_get_terrain(x, 1)!=T_OCEAN)
      map_set_terrain(x, 1, T_TUNDRA);
    if (map_get_terrain(x, map.ysize-2)!=T_ARCTIC && 
	map_get_terrain(x, map.ysize-2)!=T_OCEAN)
      map_set_terrain(x, map.ysize-2, T_TUNDRA);
  }
}

/**************************************************************************
  recursively generate deserts, i use the heights of the map, to make the
  desert unregulary shaped, diff is the recursion stopper, and will be reduced
  more if desert wants to grow in the y direction, so we end up with 
  "wide" deserts. 
**************************************************************************/
static void make_desert(int x, int y, int height, int diff) 
{
  if (abs(hmap(x, y)-height)<diff && map_get_terrain(x, y)==T_GRASSLAND) {
    map_set_terrain(x, y, T_DESERT);
    cartesian_adjacent_iterate(x, y, x1, y1) {
      if (x != x1)
	make_desert(x1, y1, height, diff-1);
      else /* y != y1 */
	make_desert(x1, y1, height, diff-3);
    } cartesian_adjacent_iterate_end;
  }
}

/**************************************************************************
  a recursive function that adds forest to the current location and try
  to spread out to the neighbours, it's called from make_forests until
  enough forest has been planted. diff is again the block function.
  if we're close to equator it will with 50% chance generate jungle instead
**************************************************************************/
static void make_forest(int x, int y, int height, int diff)
{
  if (y==0 || y==map.ysize-1)
    return;

  if (map_get_terrain(x, y)==T_GRASSLAND) {
    if (y>map.ysize*42/100 && y<map.ysize*58/100 && myrand(100)>50)
      map_set_terrain(x, y, T_JUNGLE);
    else 
      map_set_terrain(x, y, T_FOREST);
      if (abs(hmap(x, y)-height)<diff) {
	cartesian_adjacent_iterate(x, y, x1, y1) {
	  if (myrand(10)>5) make_forest(x1, y1, height, diff-5);
	} cartesian_adjacent_iterate_end;
      }
    forests++;
  }
}

/**************************************************************************
  makeforest calls make_forest with random grassland locations until there
  has been made enough forests. (the map.forestsize value controls this) 
**************************************************************************/
static void make_forests(void)
{
  int x,y;
  int forestsize=25;
  forestsize=(map.xsize*map.ysize*map.forestsize)/1000;
   do {
    x=myrand(map.xsize);
    y=myrand(map.ysize);
    if (map_get_terrain(x, y)==T_GRASSLAND) {
      make_forest(x,y, hmap(x, y), 25);
    }
    if (myrand(100)>75) {
      y=(myrand(map.ysize*2/10))+map.ysize*4/10;
      x=myrand(map.xsize);
      if (map_get_terrain(x, y)==T_GRASSLAND) {
	make_forest(x,y, hmap(x, y), 25);
      }
    }
  } while (forests<forestsize);
}

/**************************************************************************
  swamps, is placed on low lying locations, that will typically be close to
  the shoreline. They're put at random (where there is grassland)
  and with 50% chance each of it's neighbour squares will be converted to
  swamp aswell
**************************************************************************/
static void make_swamps(void)
{
  int x,y,swamps;
  int forever=0;
  for (swamps=0;swamps<map.swampsize;) {
    forever++;
    if (forever>1000) return;
    y=myrand(map.ysize);
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_GRASSLAND && hmap(x, y)<(maxval*60)/100) {
      map_set_terrain(x, y, T_SWAMP);
      cartesian_adjacent_iterate(x, y, x1, y1) {
 	if (myrand(10)>5 && map_get_terrain(x1, y1) != T_OCEAN) 
 	  map_set_terrain(x1, y1, T_SWAMP);
 	/* maybe this should increment i too? */
      } cartesian_adjacent_iterate_end;
      swamps++;
    }
  }
}

/*************************************************************************
  make_deserts calls make_desert until we have enough deserts actually
  there is no map setting for how many we want, what happends is that
  we choose a random coordinate between 20 and 30 degrees north and south 
  (deserts tend to be between 15 and 35, make_desert will expand them) and 
  if it's a grassland square we call make_desert with this coordinate, we 
  try this 500 times for each region: north and south.
**************************************************************************/
static void make_deserts(void)
{
  int x,y,i,j;
  i=map.deserts;
  j=0;
  while (i && j<500) {
    j++;

    y=myrand(map.ysize*10/180)+map.ysize*110/180;
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_GRASSLAND) {
      make_desert(x,y, hmap(x, y), 50);
      i--;
    }
    y=myrand(map.ysize*10/180)+map.ysize*60/180;
    x=myrand(map.xsize);
    if (map_get_terrain(x, y)==T_GRASSLAND) {
      make_desert(x,y, hmap(x, y), 50);
      i--;
    }
  }
}

/*********************************************************************
 Returns the number of adjacent river tiles of a tile. This can be 0
 to 4.                                                     -Erik Sigra
*********************************************************************/
static int adjacent_river_tiles4(int x, int y)
{
  int num_adjacent  = 0;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) == T_RIVER
	|| map_get_special(x1, y1) & S_RIVER)
      num_adjacent++;
  } cartesian_adjacent_iterate_end;

  return num_adjacent;
}

/*********************************************************************
 Returns the number of adjacent tiles of a tile with the terrain
 terrain. This can be 0 to 4.                              -Erik Sigra
*********************************************************************/
static int adjacent_terrain_tiles4(int x, int y,
				   enum tile_terrain_type terrain)
{
  int num_adjacent  = 0;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) == terrain)
      num_adjacent++;
  } cartesian_adjacent_iterate_end;

  return num_adjacent;
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_blocked(int x, int y)
{
  if (river_map[y*map.xsize + x] & RS_BLOCKED)
    return 1;

  /* any un-blocked? */
  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (!(river_map[(y1)*map.xsize + x1] & RS_BLOCKED))
      return 0;
  } cartesian_adjacent_iterate_end;

  return 1; /* none non-blocked |- all blocked */
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_rivergrid(int x, int y)
{
  return adjacent_river_tiles4(x, y) > 1;
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_highlands(int x, int y)
{    
  return (map_get_terrain(x, y) == T_HILLS) +
    2 * (map_get_terrain(x, y) == T_MOUNTAINS);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_ocean(int x, int y)
{
  return 4 - adjacent_terrain_tiles4(x, y, T_OCEAN);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_river(int x, int y)
{
  return 4 - adjacent_river_tiles4(x, y);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_highlands(int x, int y)
{
  return
    adjacent_terrain_tiles4(x, y, T_HILLS) +
    2 * adjacent_terrain_tiles4(x, y , T_MOUNTAINS);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_swamp(int x, int y)
{
  return map_get_terrain(x, y) != T_SWAMP;
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_adjacent_swamp(int x, int y)
{
  return 4 - adjacent_terrain_tiles4(x, y, T_SWAMP);
}

/*********************************************************************
 Help function used in make_river(). See the help there.
*********************************************************************/
static int river_test_height_map(int x, int y)
{
  return height_map[y*map.xsize + x];
}

/*********************************************************************
 Called from make_river. Marks all directions as blocked.  -Erik Sigra
*********************************************************************/
static void river_blockmark(int x, int y)
{
  freelog(LOG_DEBUG, "Blockmarking (%d, %d) and adjacent tiles.",
	  x, y);

  river_map[y*map.xsize + x] |= RS_BLOCKED;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    river_map[y1*map.xsize + x1] |= RS_BLOCKED;
  } cartesian_adjacent_iterate_end;
}

struct test_func {
  int (*func)(int, int);
  int fatal;
};

#define NUM_TEST_FUNCTIONS 9
struct test_func test_funcs[NUM_TEST_FUNCTIONS] = {
  {river_test_blocked,            1},
  {river_test_rivergrid,          1},
  {river_test_highlands,          0},
  {river_test_adjacent_ocean,     0},
  {river_test_adjacent_river,     0},
  {river_test_adjacent_highlands, 0},
  {river_test_swamp,              0},
  {river_test_adjacent_swamp,     0},
  {river_test_height_map,         0}
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
     n: 4 - adjacent_river_tiles4(...) (should be < 2 after the
                                        4-river-grid test)
					
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
static int make_river(int x, int y)
{
  /* The comparison values of the 4 tiles surrounding the current
     tile. It is the suitability to continue a river to that tile that
     is being compared. Lower is better.                  -Erik Sigra */
  static int rd_comparison_val[4];

  int rd_direction_is_valid[4];
  int num_valid_directions, dir, func_num, direction;

  while (1) {
    /* Mark the current tile as river. */
    river_map[y*map.xsize + x] |= RS_RIVER;
    freelog(LOG_DEBUG,
	    "The tile at (%d, %d) has been marked as river in river_map.\n",
	    x, y);

    /* Test if the river is done. */
    if (adjacent_river_tiles4(x, y) ||
	adjacent_terrain_tiles4(x, y, T_OCEAN)) {
      freelog(LOG_DEBUG,
	      "The river ended at (%d, %d).\n", x, y);
      return 1;
    }

    /* Else choose a direction to continue the river. */
    freelog(LOG_DEBUG,
	    "The river did not end at (%d, %d). Evaluating directions...\n", x, y);

    /* Mark all directions as available. */
    for (dir = 0; dir < 4; dir++)
      rd_direction_is_valid[dir] = 1;

    /* Test series that selects a direction for the river. */
    for (func_num = 0; func_num < NUM_TEST_FUNCTIONS; func_num++) {
      int best_val = -1;
      /* first get the tile values for the function */
      for (dir = 0; dir < 4; dir++) {
	int x1 = x + CAR_DIR_DX[dir];
	int y1 = y + CAR_DIR_DY[dir];
	if (normalize_map_pos(&x1, &y1)
	    && rd_direction_is_valid[dir]) {
	  rd_comparison_val[dir] = (test_funcs[func_num].func) (x1, y1);
	  if (best_val == -1) {
	    best_val = rd_comparison_val[dir];
	  } else {
	    best_val = MIN(rd_comparison_val[dir], best_val);
	  }
	}
      }
      assert(best_val != -1);

      /* should we abort? */
      if (best_val > 0 && test_funcs[func_num].fatal) return 0;

      /* mark the less attractive directions as invalid */
      for (dir = 0; dir < 4; dir++) {
	int x1 = x + CAR_DIR_DX[dir];
	int y1 = y + CAR_DIR_DY[dir];
	if (normalize_map_pos(&x1, &y1)
	    && rd_direction_is_valid[dir]) {
	  if (rd_comparison_val[dir] != best_val)
	    rd_direction_is_valid[dir] = 0;
	}
      }
    }

    /* Directions evaluated with all functions. Now choose the best
       direction before going to the next iteration of the while loop */
    num_valid_directions = 0;
    for (dir = 0; dir < 4; dir++)
      if (rd_direction_is_valid[dir])
	num_valid_directions++;

    switch (num_valid_directions) {
    case 0:
      return 0; /* river aborted */
    case 1:
      for (dir = 0; dir < 4; dir++) {
	int x1 = x + CAR_DIR_DX[dir];
	int y1 = y + CAR_DIR_DY[dir];
	if (normalize_map_pos(&x1, &y1)
	    && rd_direction_is_valid[dir]) {
	  river_blockmark(x, y);
	  x = x1;
	  y = y1;
	}
      }
      break;
    default:
      /* More than one possible direction; Let the random number
	 generator select the direction. */
      freelog(LOG_DEBUG, "mapgen.c: Had to let the random number"
	      " generator select a direction for a river.");
      direction = myrand(num_valid_directions - 1);
      freelog(LOG_DEBUG, "mapgen.c: direction: %d", direction);

      /* Find the direction that the random number generator selected. */
      for (dir = 0; dir < 4; dir++) {
	int x1 = x + CAR_DIR_DX[dir];
	int y1 = y + CAR_DIR_DY[dir];
	if (normalize_map_pos(&x1, &y1)
	    && rd_direction_is_valid[dir]) {
	  if (direction > 0) direction--;
	  else {
	    river_blockmark(x, y);
	    x = x1;
	    y = y1;
	    break;
	  }
	}
      }
      break;
    } /* end switch (rd_number_of_directions()) */

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
    /* The size of the map (poles don't count). */
    map.xsize * (map.ysize - 2) *
    /* Rivers need to be on land only. */
    map.landpercent /
    /* Adjustment value. Tested by me. Gives no rivers with 'set
       rivers 0', gives a reasonable amount of rivers with default
       settings and as many rivers as possible with 'set rivers 1000'. */
    0xD000;

  /* The number of river tiles that have been set. */
  int current_riverlength = 0;

  int i; /* Loop variable. */

  /* Counts the number of iterations (should increase with 1 during
     every iteration of the main loop in this function).
     Is needed to stop a potentially infinite loop. */
  int iteration_counter = 0;

  river_map = fc_malloc(sizeof(int)*map.xsize*map.ysize);

  /* The main loop in this function. */
  while (current_riverlength < desirable_riverlength &&
	 iteration_counter < RIVERS_MAXTRIES) {

    /* Don't start any rivers at the poles. */
    y = myrand(map.ysize - 2) + 1; 

    /* Any x-coordinate is valid. */
    x = myrand(map.xsize);
 
    /* Check if it is suitable to start a river on the current tile.
     */
    if (
	/* Don't start a river on ocean. */
	map_get_terrain(x, y) != T_OCEAN &&

	/* Don't start a river on river. */
	map_get_terrain(x, y) != T_RIVER &&
	!(map_get_special(x, y) & S_RIVER) &&

	/* Don't start a river on a tile is surrounded by > 1 river +
	   ocean tile. */
	adjacent_river_tiles4(x, y) +
	adjacent_terrain_tiles4(x, y, T_OCEAN) <= 1 &&

	/* Don't start a river on a tile that is surrounded by hills or
	   mountains unless it is hard to find somewhere else to start
	   it. */
	(adjacent_terrain_tiles4(x, y, T_HILLS) +
	 adjacent_terrain_tiles4(x, y, T_MOUNTAINS) < 4 ||
	 iteration_counter == RIVERS_MAXTRIES/10 * 5) &&

	/* Don't start a river on hills unless it is hard to find
	   somewhere else to start it. */
	(map_get_terrain(x, y) != T_HILLS ||
	 iteration_counter == RIVERS_MAXTRIES/10 * 6) &&

	/* Don't start a river on mountains unless it is hard to find
	   somewhere else to start it. */
	(map_get_terrain(x, y) != T_MOUNTAINS ||
	 iteration_counter == RIVERS_MAXTRIES/10 * 7) &&

	/* Don't start a river on arctic unless it is hard to find
	   somewhere else to start it. */
	(map_get_terrain(x, y) != T_ARCTIC ||
	 iteration_counter == RIVERS_MAXTRIES/10 * 8) &&

	/* Don't start a river on desert unless it is hard to find
	   somewhere else to start it. */
	(map_get_terrain(x, y) != T_DESERT ||
	 iteration_counter == RIVERS_MAXTRIES/10 * 9)){


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
	  if (river_map[y1*map.xsize + x1] & RS_RIVER) {
	    if (terrain_control.river_style == R_AS_TERRAIN) {
	      map_set_terrain(x1, y1, T_RIVER); /* Civ1 river style. */
	    } else if (terrain_control.river_style == R_AS_SPECIAL) {
	      map_set_special(x1, y1, S_RIVER); /* Civ2 river style. */
	    }
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
}

/**************************************************************************
  make_plains converts 50% of the remaining grassland to plains, this should
  maybe be lowered to 30% or done in batches, like the swamps?
**************************************************************************/
static void make_plains(void)
{
  whole_map_iterate(x, y) {
    if (map_get_terrain(x, y) == T_GRASSLAND && myrand(100) > 50)
      map_set_terrain(x, y, T_PLAINS);
  } whole_map_iterate_end;
}

/**************************************************************************
  we want the map to be sailable east-west at least at north and south pole 
  and make it a bit jagged at the edge as well.
  So this procedure converts the second line and the second last line to
  ocean, and 50% of the 3rd and 3rd last line to ocean. 
**************************************************************************/
static void make_passable(void)
{
  int x;
  
  for (x=0;x<map.xsize;x++) {
    map_set_terrain(x, 2, T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,1,T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,3,T_OCEAN);
    map_set_terrain(x, map.ysize-3, T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,map.ysize-2,T_OCEAN);
    if (myrand(100)>50) map_set_terrain(x,map.ysize-4,T_OCEAN);
  } 
  
}

/**************************************************************************
  we don't want huge areas of grass/plains, 
 so we put in a hill here and there, where it gets too 'clean' 
**************************************************************************/
static void make_fair(void)
{
  int x,y;
  for (y=2;y<map.ysize-2;y++) {
    for (x=0;x<map.xsize;x++) {
      if (terrain_is_clean(x,y)) {
	if (map_get_terrain(x, y) != T_RIVER &&
	    !(map_get_special(x, y) & S_RIVER)) {
	  map_set_terrain(map_adjust_x(x), y, T_HILLS);
	}
	cartesian_adjacent_iterate(x, y, x1, y1) {
	  if (myrand(100) > 66 &&
	      map_get_terrain(x1, y1) != T_OCEAN
	      && map_get_terrain(x1, y1) != T_RIVER
	      && !(map_get_special(x1, y1) & S_RIVER)) {
	    map_set_terrain(x1, y1, T_HILLS);
	  }	  
	} cartesian_adjacent_iterate_end;
      }
    }
  }
}

/**************************************************************************
  make land simply does it all based on a generated heightmap
  1) with map.landpercent it generates a ocean/grassland map 
  2) it then calls the above functions to generate the different terrains
**************************************************************************/
static void make_land(void)
{
  int tres=(maxval*map.landpercent)/100;
  int count=0;
  int total=(map.xsize*map.ysize*map.landpercent)/100;
  int forever=0;
  do {
    forever++;
    if (forever>50) break; /* loop elimination */
    count=0;
    whole_map_iterate(x, y) {
      if (hmap(x, y) < tres)
	map_set_terrain(x, y, T_OCEAN);
      else {
	map_set_terrain(x, y, T_GRASSLAND);
	count++;
      }
    } whole_map_iterate_end;
    if (count>total)
      tres*=11;
    else
      tres*=9;
    tres/=10;
  } while (abs(total-count)> maxval/40);
  make_passable();
  make_mountains(maxval*8/10);
  make_forests();
  make_swamps();
  make_deserts();
  make_plains();
  make_polar();
  make_fair();
  make_rivers();
}

/**************************************************************************
  Returns if this is a 1x1 island
**************************************************************************/
static int is_tiny_island(int x, int y) 
{
  if (map_get_terrain(x,y) == T_OCEAN) return 0;

  cartesian_adjacent_iterate(x, y, x1, y1) {
    if (map_get_terrain(x1, y1) != T_OCEAN) return 0;
  } cartesian_adjacent_iterate_end;

  return 1;
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
 Number this tile and recursive adjacent tiles with specified
 continent number, by flood-fill algorithm.
 Returns 1 if tile successfully assigned this number.
**************************************************************************/
static int assign_continent_flood(int x, int y, int nr)
{
  if (y<0 || y>=map.ysize)              return 0;
  if (map_get_continent(x, y))          return 0;
  if (map_get_terrain(x, y) == T_OCEAN) return 0;

  map_set_continent(x, y, nr);

  adjc_iterate(x, y, x1, y1) {
    assign_continent_flood(x1, y1, nr);
  } adjc_iterate_end;

  return 1;
}

/**************************************************************************
 Assign continent numbers to all tiles.
 Numbers 1 and 2 are reserved for polar continents if
 map.generator != 0; otherwise are not special.
 Also sets map.num_continents (note 0 is ocean, and continents
 have numbers 1 to map.num_continents _inclusive_).
 Note this is not used by generators>1 at map creation
 time, as these assign their own continent numbers.
**************************************************************************/
void assign_continent_numbers(void)
{
  int isle = 1;

  whole_map_iterate(x, y) {
    map_set_continent(x, y, 0);
  } whole_map_iterate_end;

  if (map.generator != 0) {
    assign_continent_flood(0, 0, 1);
    assign_continent_flood(0, map.ysize-1, 2);
    isle = 3;
  }
      
  whole_map_iterate(x, y) {
    if (!map_get_continent(x, y) && map_get_terrain(x, y) != T_OCEAN) {
      assign_continent_flood(x, y, isle++);
    }
  } whole_map_iterate_end;
  map.num_continents = isle-1;
  freelog(LOG_VERBOSE, "Map has %d continents", map.num_continents);
}

/**************************************************************************
 Allocate islands array and fill in values.
 Note this is only use for map.generator<=1, since others
 setups islands and starters explicitly.
**************************************************************************/
static void setup_isledata(void)
{
  int x;
  int good, mingood, maxgood;
  int riches;
  int starters;
  int isles, oldisles, goodisles;
  int guard1=0;
  int firstcont;
  int i;

  assert(map.num_continents>0);
  
  /* allocate + 1 so can use continent number as index */
  islands = fc_malloc((map.num_continents+1)*sizeof(struct isledata));

  /* initialize: */
  for(i=0; i<=map.num_continents; i++) {
    islands[i].x = islands[i].y = -1;             /* flag */
    islands[i].goodies = islands[i].starters = 0;
  }

  /* get x and y positions: (top left) */
  whole_map_iterate(x, y) {
    int cont = map_get_continent(x, y);
    if (islands[cont].x == -1) {
      islands[cont].x = x;
      islands[cont].y = y;
    }
  } whole_map_iterate_end;

  /* Add up the goodies: for useable ocean, add value to continent
     for _every_ adjacent land tile.  This is IMO not very good,
     because it adds potentially many times, and usable sea of
     distance 2 from land is ignored, but this re-produces the
     results of the previous method which used flood_fill().  --dwp
     This is also the correct place to add S_HUT bonus.
  */
  whole_map_iterate(x, y) {
    int cont = map_get_continent(x, y);
    if (cont) {
      islands[cont].goodies += is_good_tile(x, y);
      if (map_get_special(x, y) & S_HUT) {
	islands[cont].goodies += 0;	/* 3; *//* regression testing */
      }
    } else {
      assert(map_get_terrain(x, y) == T_OCEAN);
      /* no need to check is_sea_usable(x,y), because will
         only use for adjacent land (cont1>0) below */
      {
	int goodval = is_good_tile(x, y);
	square_iterate(x, y, 1, x1, y1) {
	  int cont1 = map_get_continent(x1, y1);
	  if (cont1 > 0) {
	    islands[cont1].goodies += goodval;
	  }
	}
	square_iterate_end;
      }
    }
  } whole_map_iterate_end;
  
  /* the arctic and the antarctic are continents 1 and 2 for generator>0*/
  if (map.generator>0) {
    firstcont = 3;
  } else {
    firstcont = 1;
  }
  isles = map.num_continents+1;
 
  riches=0;
  for (x=firstcont; x<isles; x++) {
      riches+=islands[x].goodies;
  }

  starters= 100; oldisles= isles+1; goodisles= isles;
  while( starters>game.nplayers && oldisles>goodisles ){
    freelog(LOG_VERBOSE, "goodisles=%i", goodisles);
    oldisles= goodisles;
    maxgood= 1;
    mingood= riches;
    good=0; 
    goodisles=0;
    starters= 0;


    /* goody goody */
    for (x=firstcont;x<isles;x++) {	  
      islands[x].starters=0;
      if ( islands[x].goodies > (riches+oldisles-1)/oldisles ) 
	{ 
	  good+=islands[x].goodies; 
	  goodisles++; 
	  if(mingood>islands[x].goodies)
	    mingood= islands[x].goodies;
	  if(maxgood<islands[x].goodies)
	    maxgood= islands[x].goodies;
	}
    }
  
    if(mingood+1<maxgood/game.nplayers)
      mingood= maxgood/game.nplayers; 

    if(goodisles>game.nplayers){
      /* bloody goodies */   
      for (x=firstcont;x<isles;x++) {
	if (( islands[x].goodies*4 > 3*(riches+oldisles-1)/oldisles )
	    &&!(islands[x].goodies > (riches+oldisles-1)/oldisles)
	    )
	  { 
	    good+=islands[x].goodies; 
	    goodisles++; 
	    if(mingood>islands[x].goodies)
	      mingood= islands[x].goodies;
	  }
      }
  
 
      /* starters are loosers */
      for (x=firstcont;x<isles;x++) {
	if (( islands[x].goodies*4 > 3*(riches+oldisles-1)/oldisles )
	    &&!(islands[x].goodies > (riches+oldisles-1)/oldisles)) {
	  freelog(LOG_VERBOSE, "islands[x].goodies=%i",islands[x].goodies);
	  islands[x].starters= (islands[x].goodies+guard1)/mingood; 
	  if(!islands[x].starters) {
	    islands[x].starters+= 1;/* ?PS: may not be enough, guard1(tm) */
	  }
	  starters+= islands[x].starters;
	}
      }
    }

    /* starters are winners */
    for (x=firstcont;x<isles;x++) {
      if (islands[x].goodies > (riches+oldisles-1)/oldisles) {
	assert(!islands[x].starters);
	freelog(LOG_VERBOSE, "islands[x].goodies=%i", islands[x].goodies);
	islands[x].starters = (islands[x].goodies+guard1)/mingood; 
	if(!islands[x].starters) {
	  islands[x].starters+= 1;/* ?PS: may not be enough, guard1(tm) */
	}
	starters+= islands[x].starters;
      }
    }

    riches= good;
    if(starters<game.nplayers){
      starters= game.nplayers+1;
      goodisles=oldisles; 
      oldisles= goodisles+1;
      riches= (4*riches+3)/3;
      if(mingood/game.nplayers>5) {
	guard1+= mingood/game.nplayers;
      } else {
	guard1+=5;
      }
      freelog(LOG_DEBUG,
	      _("Map generator: not enough start positions, fixing."));
    }
  }
  freelog(LOG_DEBUG, _("The map has %i starting positions on %i isles."),
	  starters, goodisles);
}

/**************************************************************************
  where do the different races start on the map? well this function tries
  to spread them out on the different islands.

  FIXME: MAXTRIES used to be 1.000.000, but has been raised to 10.000.000
         because a set of values hit the limit. At some point we want to
         make a better solution.
**************************************************************************/
#define MAXTRIES 10000000
void create_start_positions(void)
{
  int nr=0;
  int dist=40;
  int x, y, j=0, k, sum;
  int counter = 0;

  if (islands==NULL)		/* already setup for generator>1 */
    setup_isledata();

  if(dist>= map.xsize/2)
    dist= map.xsize/2;
  if(dist>= map.ysize/2)
    dist= map.ysize/2;

  sum=0;
  for (k=0; k<=map.num_continents; k++) {
    sum += islands[k].starters;
    if (islands[k].starters!=0) {
      freelog(LOG_VERBOSE, "starters on isle %i", k);
    }
  }
  assert(game.nplayers<=nr+sum);

  while (nr<game.nplayers) {
    x=myrand(map.xsize);
    y=myrand(map.ysize); 
    if (islands[(int)map_get_continent(x, y)].starters) {
      j++;
      if (!is_starter_close(x, y, nr, dist)) {
	islands[(int)map_get_continent(x, y)].starters--;
	map.start_positions[nr].x=x;
	map.start_positions[nr].y=y;
	nr++;
      }else{
	if (j>900-dist*9) {
 	  if(dist>1)
	    dist--;	  	  
	  j=0;
	}
      }
    }
    counter++;
    if (counter > MAXTRIES) {
      freelog(LOG_FATAL,
	      "The server appears to have gotten into an infinite loop "
	      "in the allocation of starting positions, and will abort.\n"
	      "Please report this bug at " WEBSITE_URL);
      abort();
    }
  }
  map.num_start_positions = game.nplayers;

  free(islands);
  islands = NULL;
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
    map_allocate();
    /* if one mapgenerator fails, it will choose another mapgenerator */
    /* with a lower number to try again */
    if (map.generator == 4 )
      mapgenerator4();
    if (map.generator == 3 )
      mapgenerator3();
    if( map.generator == 2 )
      mapgenerator2();
    if( map.generator == 1 )
      mapgenerator1();
    remove_tiny_islands();
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
 readjust terrain counts so that it makes sense for mapgen 1, 2, 3 and 4
 idea: input is the number of terrain
 mapgen 1 needs custom parameters, 
 mapgen 2 and 3 and 4 use percents, currently.
 Ultimately, I hope all parameters below will be weights,
 with the defaults set to a percentage;
 Placing deserts and swamps may cause some problems.

 This function needs to be called from the server everytime a
 a parameter changes.
 It will be called again at game start, too.
**************************************************************************/
void adjust_terrain_param(void)
{
  int total;

  /*!PS: I don't have the time to have several test runs */
  /* to find a mapping from percents to generator 1 settings. */

  if(map.generator==1){
    /*map.riverlength*= 10; */
    /*I leave this out, people will get too upset 
      if they have to change all their scripts */
    return;
  }

  map.riverlength/= 10;/* left in */

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;

  if(total>100){
    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.forestsize= map.forestsize*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.riverlength= map.riverlength*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.swampsize= map.swampsize*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.mountains= map.mountains*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + map.grasssize;
    map.deserts= map.deserts*100/total;

    total = map.riverlength + map.mountains + map.deserts 
    + map.forestsize + map.swampsize + 0;
    map.grasssize= 100 - total;
  }
  /* if smaller than 100, rest goes implicitly to grass */

  map.riverlength*= 10;
}

/**************************************************************************
  since the generated map will always have a positive number as minimum height
  i reduce the height so the lowest height is zero, this makes calculations
  easier
**************************************************************************/
static void adjust_map(int minval)
{
  whole_map_iterate(x, y) {
    hmap(x, y) -= minval;
  } whole_map_iterate_end;
}

/**************************************************************************
  mapgenerator1, highlevel function, that calls all the previous functions
**************************************************************************/
static void mapgenerator1(void)
{
  int i;
  int minval=5000000;
  height_map=fc_malloc (sizeof(int)*map.xsize*map.ysize);

  adjust_terrain_param();
  
  whole_map_iterate(x, y) {
    hmap(x, y) = myrand(40) + ((500 - abs(map.ysize / 2 - y)) / 10);
  } whole_map_iterate_end;

  for (i=0;i<1500;i++) {
    height_map[myrand(map.ysize*map.xsize)]+=myrand(5000);
    if (!(i%100)) {
      smooth_map(); 
    }
  }

  smooth_map(); 
  smooth_map(); 
  smooth_map(); 

  whole_map_iterate(x, y) {
    if (hmap(x, y) > maxval)
      maxval = hmap(x, y);
    if (hmap(x, y) < minval)
      minval = hmap(x, y);
  } whole_map_iterate_end;

  maxval-=minval;
  adjust_map(minval);

  make_land();
  free(height_map);
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
    new_hmap[map_inx(x, y)] = height_sum / counter;
  } whole_map_iterate_end;

  memcpy(height_map, new_hmap, sizeof(int) * map.xsize * map.ysize);
  free(new_hmap);
}

/**************************************************************************
  this function spreads out huts on the map, a position can be used for a
  hut if there isn't another hut close and if it's not on the ocean.
**************************************************************************/
static void make_huts(int number)
{
  int x,y,l;
  int count=0;
  while ((number*map.xsize*map.ysize)/2000 && count++<map.xsize*map.ysize*2) {
    x=myrand(map.xsize);
    y=myrand(map.ysize);
    l=myrand(6);
    if (map_get_terrain(x, y)!=T_OCEAN && 
	( map_get_terrain(x, y)!=T_ARCTIC || l<3 )
	) {
      if (!is_hut_close(x,y)) {
	number--;
	map_get_tile(x,y)->special|=S_HUT;
	/* Don't add to islands[].goodies because islands[] not
	   setup at this point, except for generator>1, but they
	   have pre-set starters anyway. */
      }
    }
  }
}

static void add_specials(int prob)
{
  int x,y;
  enum tile_terrain_type ttype;
  for (y=1;y<map.ysize-1;y++) {
    for (x=0;x<map.xsize; x++) {
      ttype = map_get_terrain(x, y);
      if ((ttype==T_OCEAN && is_coastline(x,y)) || (ttype!=T_OCEAN)) {
	if (myrand(1000)<prob) {
	  if (!is_special_close(x,y)) {
	    if (tile_types[ttype].special_1_name[0] &&
		(!(tile_types[ttype].special_2_name[0]) || (myrand(100)<50))) {
	      map_get_tile(x,y)->special|=S_SPECIAL_1;
	    }
	    else if (tile_types[ttype].special_2_name[0]) {
	      map_get_tile(x,y)->special|=S_SPECIAL_2;
	    }
	  }
	}
      }
    }
  }
  map.have_specials = 1;
}

/**************************************************************************
  globals for generator 2 & 3
**************************************************************************/
static int isleindex, n, e, s, w;
static long int totalmass; /* better a global than a duplicate formula */

static int is_cold(int x, int y){
  return ( y * 5 < map.ysize || y * 5 > map.ysize * 4 );
}

/**************************************************************************
  fill an island with up four types of terrains, rivers have extra code
**************************************************************************/
static void fill_island(int coast, long int *bucket,
			int warm0_weight, int warm1_weight, 
			int cold0_weight, int cold1_weight, 
		enum tile_terrain_type warm0, enum tile_terrain_type warm1,
		enum tile_terrain_type cold0, enum tile_terrain_type cold1)
{
  int x, y, i, k, capac;
  long int failsafe;

  if (*bucket <= 0 ) return;
  capac = totalmass;
  i = *bucket / capac;
  i++;
  *bucket -= i * capac;

  k= i;
  failsafe= i*(s-n)*(e-w);
  if(failsafe<0){ failsafe= -failsafe; }

  if(warm0_weight+warm1_weight+cold0_weight+cold1_weight<=0)
    i= 0;

  while (i && failsafe--) {
    y = myrand(s - n) + n;
    x = myrand(e - w) + w;
    if (map_get_continent(x,y) == isleindex &&
		map_get_terrain(x,y) == T_GRASSLAND) {

      /* the first condition helps make terrain more contiguous,
	 the second lets it avoid the coast: */
      if ( ( i*3>k*2 
	     || is_terrain_near_tile(x,y,warm0) 
	     || is_terrain_near_tile(x,y,warm1) 
	     || myrand(100)<50 
	     || is_terrain_near_tile(x,y,cold0) 
	     || is_terrain_near_tile(x,y,cold1) 
	     )
	   &&( !is_at_coast(x, y) || myrand(100) < coast )) {
        if (cold1 != T_RIVER) {
          if ( is_cold(x,y) )
            map_set_terrain(x, y, (myrand(cold0_weight+cold1_weight)<cold0_weight) 
			    ? cold0 : cold1);
          else
            map_set_terrain(x, y, (myrand(warm0_weight+warm1_weight)<warm0_weight) 
			    ? warm0 : warm1);
        } else {
          if (is_water_adjacent_to_tile(x, y) &&
	      count_terrain_near_tile(x, y, T_OCEAN) < 4 &&
	      count_terrain_near_tile(x, y, T_RIVER) < 3)
	    map_set_terrain(x, y, T_RIVER);
	}
      }
      if (map_get_terrain(x,y) != T_GRASSLAND) i--;
    }
  }
}

/**************************************************************************
  fill an island with rivers, when river style is R_AS_SPECIAL
**************************************************************************/
static void fill_island_rivers(int coast, long int *bucket)
{
  int x, y, i, k, capac;
  long int failsafe;

  if (*bucket <= 0 ) return;
  capac = totalmass;
  i = *bucket / capac;
  i++;
  *bucket -= i * capac;

  k= i;
  failsafe= i*(s-n)*(e-w);
  if(failsafe<0){ failsafe= -failsafe; }

  while (i && failsafe--) {
    y = myrand(s - n) + n;
    x = myrand(e - w) + w;
    if (map_get_continent(x,y) == isleindex &&
		map_get_terrain(x,y) == T_GRASSLAND) {

      /* the first condition helps make terrain more contiguous,
	 the second lets it avoid the coast: */
      if ( ( i*3>k*2 
	     || count_special_near_tile(x, y, S_RIVER) > 0
	     || myrand(100)<50 
	     )
	   &&( !is_at_coast(x, y) || myrand(100) < coast )) {
	if (is_water_adjacent_to_tile(x, y) &&
	    count_terrain_near_tile(x, y, T_OCEAN) < 4 &&
            count_special_near_tile(x, y, S_RIVER) < 3) {
	  map_set_special(x, y, S_RIVER);
	  i--;
	}
      }
    }
  }
}

/*************************************************************************/

static long int checkmass;

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static int place_island(void)
{
  int x, y, xo, yo, i=0;
  yo = myrand(map.ysize);
  xo = myrand(map.xsize);

  /* this helps a lot for maps with high landmass */
  for (y = n, x = w ; y < s && x < e ; y++, x++) {
    int map_x = x + xo - w;
    int map_y = y + yo - n;
    if (!normalize_map_pos(&map_x, &map_y))
      return 0;
    if (hmap(x, y) && is_coastline(map_x, map_y))
      return 0;
  }
		       
  for (y = n ; y < s ; y++)
    for (x = w ; x < e ; x++) {
      int map_x = x + xo - w;
      int map_y = y + yo - n;
      if (!normalize_map_pos(&map_x, &map_y))
	return 0;
      if (hmap(x, y) && is_coastline(map_x, map_y))
	return 0;
    }

  for (y = n ; y < s ; y++) 
    for (x = w ; x < e ; x++) {
      if (hmap(x, y)) {
	int map_x = x + xo - w;
	int map_y = y + yo - n;
	int is_real;

	is_real = normalize_map_pos(&map_x, &map_y);
	assert(is_real);

	checkmass--; 
	if(checkmass<=0) {
	  freelog(LOG_ERROR, "mapgen.c: mass doesn't sum up.");
	  return i;
	}

        map_set_terrain(map_x, map_y, T_GRASSLAND);
        map_set_continent(map_x, map_y, isleindex);
        i++;
      }
    }
  s += yo - n;
  e += xo - w;
  n = yo;
  w = xo;
  return i;
}

/**************************************************************************
  finds a place and drop the island created when called with islemass != 0
**************************************************************************/
static int create_island(int islemass)
{
  int x, y, i;
  long int tries=islemass*(2+islemass/20)+99;

  memset(height_map, '\0', sizeof(int) * map.xsize * map.ysize);
  y = map.ysize / 2;
  x = map.xsize / 2;
  hmap(x, y) = 1;
  n = y - 1; w = x - 1;
  s = y + 2; e = x + 2;
  i = islemass - 1;
  while (i && tries-->0) {
    y = myrand(s - n) + n;
    x = myrand(e - w) + w;
    if ((!hmap(x, y)) && (
		hmap(x+1, y) || hmap(x-1, y) ||
		hmap(x, y+1) || hmap(x, y-1) )) {
      hmap(x, y) = 1;
      i--;
      if (y >= s - 1 && s < map.ysize - 2) s++;
      if (x >= e - 1 && e < map.xsize - 2) e++;
      if (y <= n && n > 2) n--;
      if (x <= w && w > 2) w--;
    }
    if (i < islemass / 10) {
      for (y = n ; y < s ; y++)
        for (x = w ; x < e ; x++)
          if ((!hmap(x, y)) && i && (
		hmap(x+1, y) && hmap(x-1, y) &&
		hmap(x, y+1) && hmap(x, y-1) )) {
            hmap(x, y) = 1;
            i--; 
          }
    }
  }
  if(tries<=0) {
    freelog(LOG_ERROR, "create_island ended early with %d/%d.",
	    islemass-i, islemass);
  }
  
  tries= map.xsize*(long int)map.ysize/4;/* on a 40x60 map, there are 2400 places */
  while (!(i = place_island()) && --tries);
  return i;
}

/*************************************************************************/

static long int totalweight;

/**************************************************************************
  make an island, fill every tile type except plains
  note: you have to create big islands first.
**************************************************************************/
static void make_island(int islemass, int starters)
{
  static long int tilefactor, balance, lastplaced;/* int may be only 2 byte ! */
  static long int riverbuck, mountbuck, desertbuck, forestbuck, swampbuck;

  int i;

  if (!islemass) {
					/* setup initial static parameters */
    balance = 0;
    isleindex = 3; /* 0= none, 1= arctic, 2= antarctic */

    checkmass= totalmass;

    /* caveat: this should really be sent to all players */
    if(totalmass>3000)
      freelog(LOG_NORMAL, _("High landmass - this may take a few seconds."));

    i = (map.riverlength / 10) + map.mountains
		+ map.deserts + map.forestsize + map.swampsize;
    i = i <= 90 ? 100 : i * 11 / 10;
    tilefactor = totalmass / i;
    riverbuck = -(long int)myrand(totalmass);
    mountbuck = -(long int)myrand(totalmass);
    desertbuck = -(long int)myrand(totalmass);
    forestbuck = -(long int)myrand(totalmass);
    swampbuck = -(long int)myrand(totalmass);
    lastplaced = totalmass;
  } else {

   /* makes the islands here */
    islemass = islemass - balance;

    /* don't create continents without a number */
    if(isleindex>=MAP_NCONT)
      return;

    if(islemass>lastplaced+1+lastplaced/50)/*don't create big isles we can't place*/
      islemass= lastplaced+1+lastplaced/50;

    /* isle creation does not perform well for nonsquare islands */
    if(islemass>(map.ysize-6)*(map.ysize-6))
      islemass= (map.ysize-6)*(map.ysize-6);

    if(islemass>(map.xsize-2)*(map.xsize-2))
      islemass= (map.xsize-2)*(map.xsize-2);

    i = islemass;
    if (i <= 0) return;
    islands[isleindex].starters = starters;

    freelog(LOG_VERBOSE, "island %i",isleindex);

    while (!create_island(i--) && i*10>islemass );
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
    if (terrain_control.river_style==R_AS_TERRAIN) {
      riverbuck += map.riverlength / 10 * i;
      fill_island(1, &riverbuck,
		 1,1,1,1,
		 T_RIVER, T_RIVER, T_RIVER, T_RIVER);
    }
    if (terrain_control.river_style==R_AS_SPECIAL) {
      riverbuck += map.riverlength / 10 * i;
      fill_island_rivers(1, &riverbuck);
    }
    mountbuck += map.mountains * i;
    fill_island(20, &mountbuck,
		3,1, 3,1,
		T_HILLS, T_MOUNTAINS, T_HILLS, T_MOUNTAINS);
    desertbuck += map.deserts * i;
    fill_island(40, &desertbuck,
		map.deserts, map.deserts, map.deserts, map.deserts,
		T_DESERT, T_DESERT, T_DESERT, T_TUNDRA);
    forestbuck += map.forestsize * i;
    fill_island(60, &forestbuck,
		map.forestsize, map.swampsize, map.forestsize, map.swampsize,
		T_FOREST, T_JUNGLE, T_FOREST, T_TUNDRA);
    swampbuck += map.swampsize * i;
    fill_island(80, &swampbuck,
		map.swampsize, map.swampsize, map.swampsize, map.swampsize,
		T_SWAMP, T_SWAMP, T_SWAMP, T_SWAMP);

    isleindex++;
    map.num_continents++;
  }
}

/**************************************************************************
  fill ocean and make polar
**************************************************************************/
static void initworld(void)
{
  int x, y;
  
  height_map = fc_malloc(sizeof(int) * map.ysize * map.xsize);
  islands = fc_malloc((MAP_NCONT+1)*sizeof(struct isledata));
  
  for (y = 0 ; y < map.ysize ; y++) 
    for (x = 0 ; x < map.xsize ; x++) {
      map_set_terrain(x, y, T_OCEAN);
      map_set_continent(x, y, 0);
    }
  for (x = 0 ; x < map.xsize; x++) {
    map_set_terrain(x, 0, myrand(9) ? T_ARCTIC : T_TUNDRA);
    map_set_continent(x, 0, 1);
    if (!myrand(9)) {
      map_set_terrain(x, 1, myrand(9) ? T_TUNDRA : T_ARCTIC);
      map_set_continent(x, 1, 1);
    }
    map_set_terrain(x, map.ysize-1, myrand(9) ? T_ARCTIC : T_TUNDRA);
    map_set_continent(x, map.ysize-1, 2);
    if (!myrand(9)) {
      map_set_terrain(x, map.ysize-2, myrand(9) ? T_TUNDRA : T_ARCTIC);
      map_set_continent(x, map.ysize-2, 2);
    }
  }
  map.num_continents = 2;
  make_island(0, 0);
  islands[2].starters = 0;
  islands[1].starters = 0;
  islands[0].starters = 0;
}  

/**************************************************************************
  island base map generators
**************************************************************************/
static void mapgenerator2(void)
{
  int i;
  int spares= 1; 
  /* constant that makes up that an island actually needs additional space */

  if (map.landpercent > 85) {
    map.generator = 1;
    return;
  }

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;

  /*!PS: The weights NEED to sum up to totalweight (dammit) */
  /* copying the flow of the make_island loops is the safest way */
  totalweight= 0;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 70;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 20;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 10;

  initworld();

  for (i = game.nplayers ; i ; i--)
    make_island(70*totalmass/totalweight, 1);
  for (i = game.nplayers ; i ; i--)
    make_island(20*totalmass/totalweight, 0);
  for (i = game.nplayers ; i ; i--)
    make_island(10*totalmass/totalweight, 0);
  make_plains();  
  free(height_map);

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

  if ( map.landpercent > 80) {
    map.generator = 2;
    return;
  }

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;


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

  if( map.xsize < 40 || map.ysize < 40 || map.landpercent>80 )
    { freelog(LOG_NORMAL,_("Falling back to generator 2.")); mapgenerator2(); return; }

  if(islandmass<2)
    islandmass= 2;
  if(islandmass>maxmassdiv6*6)
    islandmass= maxmassdiv6*6;/* !PS: let's try this */

  initworld();

  while( isleindex-2<=bigislands && checkmass>islandmass && ++j<500 ) {
    make_island(islandmass,1);
  }

  if(j==500){
    freelog(LOG_NORMAL, _("Generator 3 didn't place all big islands."));
  }
  
  islandmass= (islandmass*11)/8;
  /*!PS: I'd like to mult by 3/2, but starters might make trouble then*/
  if(islandmass<2)
    islandmass= 2;


  while( isleindex<=MAP_NCONT-20 && checkmass>islandmass && ++j< 1500 ) {
      if(j<1000)
	size = myrand((islandmass+1)/2+1)+islandmass/2;
      else
	size = myrand((islandmass+1)/2+1);
      if(size<2) size=2;

      make_island(size, (isleindex-2<=game.nplayers)?1:0 );
  }

  make_plains();  
  free(height_map);
    
  if(j==1500) {
    freelog(LOG_NORMAL, _("Generator 3 left %li landmass unplaced."), checkmass);
  } else if(checkmass>map.xsize+map.ysize+totalweight) {
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

  /* no islands with mass >> sqr(min(xsize,ysize)) */

  i = game.nplayers / 2;
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

  adjust_terrain_param();
  totalmass = ( (map.ysize-6-spares) * map.landpercent * (map.xsize-spares) ) / 100;

  /*!PS: The weights NEED to sum up to totalweight (dammit) */
  totalweight= 0;
  if (game.nplayers % 2)
    totalweight+= bigweight*3;
  else
    i++;
  while (--i)
    totalweight+= bigweight*2;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 20;
  for (i = game.nplayers ; i ; i--)
    totalweight+= 10;

  initworld();

  i = game.nplayers / 2;
  if (game.nplayers % 2)
    make_island(bigweight*3, 3);
  else
    i++;
  while (--i)
    make_island(bigweight*2*totalmass/totalweight, 2);
  for (i = game.nplayers ; i ; i--)
    make_island(20*totalmass/totalweight, 0);
  for (i = game.nplayers ; i ; i--)
    make_island(10*totalmass/totalweight, 0);
  make_plains();  
  free(height_map);

  if(checkmass>map.xsize+map.ysize+totalweight) {
    freelog(LOG_VERBOSE, "%ld mass left unplaced", checkmass);
  }
}
