/********************************************************************** 
   Copyright (C) 2004 - Marcelo J. Burda,
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

#include "map.h"

#include "utilities.h"

/****************************************************************************
 Map that contains, according to circumstances, information on whether
 we have already placed terrain (special, hut) here.
****************************************************************************/
static bool *placed_map;

/**************************************************************************
 return TRUE if initialized
*************************************************************************/ 
bool placed_map_is_initialized(void)
{
  return placed_map != NULL;
}

/****************************************************************************
  Create a clean pmap
****************************************************************************/
void create_placed_map(void)                               
{                                                          
  assert( !placed_map_is_initialized());                              
  placed_map = fc_malloc (sizeof(bool) * MAX_MAP_INDEX);   
  INITIALIZE_ARRAY(placed_map, MAX_MAP_INDEX, FALSE );     
}

/**************************************************************************** 
  Free the pmap
****************************************************************************/
void destroy_placed_map(void)   
{                              
  assert(placed_map_is_initialized()); 
  free(placed_map);            
  placed_map = NULL;           
}



#define pmap(x, y) (placed_map[map_pos_to_index(x, y)])

/* Checks if land has not yet been placed on pmap at (x, y) */
bool not_placed(int x, int y)
{
  return !pmap((x), (y));
}

/* set has placed or not placed position in the pmap */
void map_set_placed(int x, int y)
{
  pmap((x), (y)) = TRUE;
}

void map_unset_placed(int x, int y)
{
  pmap((x), (y)) = FALSE;
}

/**************************************************************************** 
  set all oceanics tiles in placed_map
****************************************************************************/
void set_all_ocean_tiles_placed(void) 
{                               
  whole_map_iterate(x, y) {     
    if (is_ocean(map_get_terrain(x,y))) { 
      map_set_placed(x, y);     
    }                           
  } whole_map_iterate_end;      
}

/****************************************************************************
  Set all nearby tiles as placed on pmap. 
****************************************************************************/
void set_placed_near_pos(int x, int y, int dist)
{
  square_iterate(x, y, dist, x1, y1) {
    map_set_placed(x1, y1);
  } square_iterate_end;
}

/**************************************************************************
  Change the values of the integer map, so that they contain ranking of each 
  tile scaled to [0 .. int_map_max].
  The lowest 20% of tiles will have values lower than 0.2 * int_map_max.

**************************************************************************/
void adjust_int_map(int *int_map, int int_map_max)
{
  int i, minval = *int_map, maxval = minval;

  /* Determine minimum and maximum value. */
  for (i = 0; i < MAX_MAP_INDEX; i++) {
    maxval = MAX(maxval, int_map[i]);
    minval = MIN(minval, int_map[i]);
  }

  {
    int const size = 1 + maxval - minval;
    int i, count = 0, frequencies[size];

    INITIALIZE_ARRAY(frequencies, size, 0);

    /* Translate value so the minimum value is 0
       and count the number of occurencies of all values to initialize the 
       frequencies[] */
    for (i = 0; i < MAX_MAP_INDEX; i++) {
      int_map[i] = (int_map[i] - minval);
      frequencies[int_map[i]]++;
    }

    /* create the linearize function as "incremental" frequencies */
    for(i =  0; i < size; i++) {
      count += frequencies[i]; 
      frequencies[i] = (count * int_map_max) / MAX_MAP_INDEX;
    }

    /* apply the linearize function */
    for (i = 0; i < MAX_MAP_INDEX; i++) {
      int_map[i] = frequencies[int_map[i]];
    }
  }
}
