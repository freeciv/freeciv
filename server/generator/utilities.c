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
  int minval = *int_map, maxval = minval;

  /* Determine minimum and maximum value. */
  whole_map_iterate_index(j) {
    maxval = MAX(maxval, int_map[j]);
    minval = MIN(minval, int_map[j]);
  } whole_map_iterate_index_end;

  {
    int const size = 1 + maxval - minval;
    int i, count = 0, frequencies[size];

    INITIALIZE_ARRAY(frequencies, size, 0);

    /* Translate value so the minimum value is 0
       and count the number of occurencies of all values to initialize the 
       frequencies[] */
    whole_map_iterate_index(j) {
      int_map[j] = (int_map[j] - minval);
      frequencies[int_map[j]]++;
    } whole_map_iterate_index_end ;

    /* create the linearize function as "incremental" frequencies */
    for(i =  0; i < size; i++) {
      count += frequencies[i]; 
      frequencies[i] = (count * int_map_max) / MAX_MAP_INDEX;
    }

    /* apply the linearize function */
    whole_map_iterate_index(j) {
      int_map[j] = frequencies[int_map[j]];
    } whole_map_iterate_index_end;
  }
}
bool normalize_nat_pos(int *x, int  *y) 
{
    int map_x, map_y;
    bool return_value;

    NATIVE_TO_MAP_POS(&map_x, &map_y, *x, *y);
    return_value = normalize_map_pos(&map_x, &map_y);
    MAP_TO_NATIVE_POS(x, y, map_x, map_y);

    return return_value;
}

bool is_normal_nat_pos(int x, int y)
{
  do_in_map_pos(map_x, map_y, x, y) {
    return is_normal_map_pos(map_x, map_y);
  } do_in_map_pos_end;
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
  assert(int_map != NULL);
  float weight[5] =  {0.35,  0.5 ,1 , 0.5, 0.35};
  float total_weight = 2.70;
  bool axe = TRUE;
  int alt_int_map[MAX_MAP_INDEX];
  int *target_map, *source_map;

  target_map = alt_int_map;
  source_map = int_map;

  do {
    whole_map_iterate_index( j ) {
      int  N = 0, D = 0;
      iterate_axe(j1, i, j, 2, axe) {
	D += weight[i + 2];
	N += weight[i + 2] * source_map[j1];
      } iterate_axe_end;
      if(zeroes_at_edges) {
	D = total_weight;
      }
      target_map[j] = N / D;
    } whole_map_iterate_index_end;

    if (topo_has_flag(TF_ISO) || topo_has_flag(TF_HEX)) {
    weight[0] = weight[4] = 0.5;
    weight[1] = weight[3] = 0.7;
    total_weight = 3.4;  
  }

  axe = !axe;

  source_map = alt_int_map;
  target_map = int_map;

  } while ( !axe );
}

