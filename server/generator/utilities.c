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
  assert(!placed_map_is_initialized());                              
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



#define pmap(ptile) (placed_map[(ptile)->index])

/* Checks if land has not yet been placed on pmap at (x, y) */
bool not_placed(const struct tile *ptile)
{
  return !pmap(ptile);
}

/* set has placed or not placed position in the pmap */
void map_set_placed(struct tile *ptile)
{
  pmap(ptile) = TRUE;
}

void map_unset_placed(struct tile *ptile)
{
  pmap(ptile) = FALSE;
}

/**************************************************************************** 
  set all oceanics tiles in placed_map
****************************************************************************/
void set_all_ocean_tiles_placed(void) 
{
  whole_map_iterate(ptile) {
    if (is_ocean(map_get_terrain(ptile))) {
      map_set_placed(ptile);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Set all nearby tiles as placed on pmap. 
****************************************************************************/
void set_placed_near_pos(struct tile *ptile, int dist)
{
  square_iterate(ptile, dist, tile1) {
    map_set_placed(tile1);
  } square_iterate_end;
}

/**************************************************************************
  Change the values of the integer map, so that they contain ranking of each 
  tile scaled to [0 .. int_map_max].
  The lowest 20% of tiles will have values lower than 0.2 * int_map_max.

  If filter is non-null then it only tiles for which filter(ptile, data) is
  TRUE will be considered.
**************************************************************************/
void adjust_int_map_filtered(int *int_map, int int_map_max, void *data,
			     bool (*filter)(const struct tile *ptile,
					    const void *data))
{
  int minval = 0, maxval = 0, total = 0;
  bool first = TRUE;

  /* Determine minimum and maximum value. */
  whole_map_iterate_filtered(ptile, data, filter) {
    if (first) {
      minval = int_map[ptile->index];
      maxval = int_map[ptile->index];
    } else {
      maxval = MAX(maxval, int_map[ptile->index]);
      minval = MIN(minval, int_map[ptile->index]);
    }
    first = FALSE;
    total++;
  } whole_map_iterate_filtered_end;

  if (total == 0) {
    return;
  }

  {
    int const size = 1 + maxval - minval;
    int i, count = 0, frequencies[size];

    INITIALIZE_ARRAY(frequencies, size, 0);

    /* Translate value so the minimum value is 0
       and count the number of occurencies of all values to initialize the 
       frequencies[] */
    whole_map_iterate_filtered(ptile, data, filter) {
      int_map[ptile->index] -= minval;
      frequencies[int_map[ptile->index]]++;
    } whole_map_iterate_filtered_end;

    /* create the linearize function as "incremental" frequencies */
    for(i =  0; i < size; i++) {
      count += frequencies[i]; 
      frequencies[i] = (count * int_map_max) / total;
    }

    /* apply the linearize function */
    whole_map_iterate_filtered(ptile, data, filter) {
      int_map[ptile->index] = frequencies[int_map[ptile->index]];
    } whole_map_iterate_filtered_end;
  }
}

bool is_normal_nat_pos(int x, int y)
{
  NATIVE_TO_MAP_POS(&x, &y, x, y);
  return is_normal_map_pos(x, y);
}

/****************************************************************************
 * Apply a Gaussian difusion filtre on the map
 * the size of the map is MAX_MAP_INDEX and the map is indexed by 
 * native_pos_to_index function
 * if zeroes_at_edges is set, any unreal position on difusion has 0 value
 * if zeroes_at_edges in unset the unreal position are not counted.
 ****************************************************************************/
 void smooth_int_map(int *int_map, bool zeroes_at_edges)
{
  float weight[5] =  {0.35,  0.5 ,1 , 0.5, 0.35};
  float total_weight = 2.70;
  bool axe = TRUE;
  int alt_int_map[MAX_MAP_INDEX];
  int *target_map, *source_map;

  assert(int_map != NULL);

  target_map = alt_int_map;
  source_map = int_map;

  do {
    whole_map_iterate(ptile) {
      int  N = 0, D = 0;

      iterate_axe(tile1, i, ptile, 2, axe) {
	D += weight[i + 2];
	N += weight[i + 2] * source_map[tile1->index];
      } iterate_axe_end;
      if(zeroes_at_edges) {
	D = total_weight;
      }
      target_map[ptile->index] = N / D;
    } whole_map_iterate_end;

    if (MAP_IS_ISOMETRIC) {
      weight[0] = weight[4] = 0.5;
      weight[1] = weight[3] = 0.7;
      total_weight = 3.4;  
    }

    axe = !axe;

    source_map = alt_int_map;
    target_map = int_map;

  } while (!axe);
}
