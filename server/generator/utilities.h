/********************************************************************** 
 Freeciv Generator - Copyright (C) 2004 - Marcelo J. Burda
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__UTILITIES_H
#define FC__UTILITIES_H

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
  {                                                                         

#define do_in_map_pos_end                                                   \
  }                                                                         \
}

void adjust_int_map(int *int_map, int int_map_max);
void create_placed_map(void);
void destroy_placed_map(void);
void map_set_placed(int x, int y);
void map_unset_placed(int x, int y);
bool not_placed(int x, int y);
bool placed_map_is_initialized(void);
void set_all_ocean_tiles_placed(void) ;
void set_placed_near_pos(int map_x, int map_y, int dist);


#endif  /* FC__UTILITIES_H */
