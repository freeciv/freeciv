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
 * Note: that the map position is declared as const and can't be changed
 * inside the block.
 */
#define do_in_map_pos(ptile, nat_x, nat_y)				    \
{                                                                           \
  struct tile *ptile = native_pos_to_tile((nat_x), (nat_y));		    \
  {

#define do_in_map_pos_end                                                   \
  }                                                                         \
}

/***************************************************************************
 iterate axe iterate on selected axe ( x if Xaxe is TRUE) over a intervale
 of -dist to dist arround the tile indexed by index0
 this iterator create 2 vars:
 index : the map index of the iterate pointed tile
 i : the position in the intervale of iteration (from -dist to dist)
 index0, dist, Xaxe are side effect safe.
 ***************************************************************************/
#define iterate_axe(iter_tile, i, center_tile, dist, Xaxe)		\
  {									\
    const int ___dist = (dist);						\
    const struct tile *_center_tile = (center_tile);			\
    const bool ___Xaxe = (Xaxe);					\
    int i, ___x, ___y;							\
    struct tile *iter_tile;						\
									\
    for (i = -___dist; i <= ___dist; i++) {				\
      ___x = _center_tile->nat_x + (___Xaxe ? i : 0);			\
      ___y = _center_tile->nat_y + (___Xaxe ? 0 : i);			\
      iter_tile = native_pos_to_tile(___x, ___y);			\
      if (!iter_tile) {							\
	continue;							\
      }

#define iterate_axe_end \
    } \
} 
#define whole_map_iterate_filtered(ptile, pdata, pfilter)                   \
{									    \
  bool (*_filter)(const struct tile *ptile, const void *data) = (pfilter);  \
  const void *_data = (pdata);						    \
									    \
  whole_map_iterate(ptile) {                                                \
    if (_filter && !(_filter)(ptile, _data)) {				    \
      continue;                                                             \
    }

#define whole_map_iterate_filtered_end					    \
  } whole_map_iterate_end						    \
}

bool is_normal_nat_pos(int x, int y);

/* int maps tools */
void adjust_int_map_filtered(int *int_map, int int_map_max, void *data,
				   bool (*filter)(const struct tile *ptile,
						  const void *data));
#define adjust_int_map(int_map, int_map_max) \
  adjust_int_map_filtered(int_map, int_map_max, (void *)NULL, \
	     (bool (*)(const struct tile *ptile, const void *data) )NULL)
void smooth_int_map(int *int_map, bool zeroes_at_edges);

/* placed_map tool*/
void create_placed_map(void);
void destroy_placed_map(void);
void map_set_placed(struct tile *ptile);
void map_unset_placed(struct tile *ptile);
bool not_placed(const struct tile *ptile);
bool placed_map_is_initialized(void);
void set_all_ocean_tiles_placed(void) ;
void set_placed_near_pos(struct tile *ptile, int dist);



#endif  /* FC__UTILITIES_H */
