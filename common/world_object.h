/***********************************************************************
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
#ifndef FC__WORLD_OBJECT_H
#define FC__WORLD_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "map_types.h"


/* struct city_hash. */
#define SPECHASH_TAG city
#define SPECHASH_INT_KEY_TYPE
#define SPECHASH_IDATA_TYPE struct city *
#include "spechash.h"

/* struct unit_hash. */
#define SPECHASH_TAG unit
#define SPECHASH_INT_KEY_TYPE
#define SPECHASH_IDATA_TYPE struct unit *
#include "spechash.h"

struct world
{
  struct civ_map map;
  struct city_hash *cities;
  struct unit_hash *units;
};

extern struct world wld; /* In game.c */


#define MAP_IS_ISOMETRIC (CURRENT_TOPOLOGY & (TF_ISO + TF_HEX))

#define CURRENT_TOPOLOGY (wld.map.topology_id)
#define CURRENT_WRAP (wld.map.wrap_id)

/* Number of index coordinates (for sanity checks and allocations) */
#define MAP_INDEX_SIZE (wld.map.xsize * wld.map.ysize)

/* Width and height of the map, in native coordinates. */
#define MAP_NATIVE_WIDTH wld.map.xsize
#define MAP_NATIVE_HEIGHT wld.map.ysize

/* Width and height of the map, in natural coordinates. */
#define MAP_NATURAL_WIDTH (MAP_IS_ISOMETRIC ? 2 * wld.map.xsize : wld.map.xsize)
#define MAP_NATURAL_HEIGHT wld.map.ysize

#define MAP_CARDINAL_DIRS wld.map.cardinal_dirs
#define MAP_NUM_CARDINAL_DIRS wld.map.num_cardinal_dirs
#define MAP_VALID_DIRS wld.map.valid_dirs
#define MAP_NUM_VALID_DIRS wld.map.num_valid_dirs
#define MAP_ITERATE_OUTWARDS_INDICES wld.map.iterate_outwards_indices
#define MAP_NUM_ITERATE_OUTWARDS_INDICES wld.map.num_iterate_outwards_indices

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__WORLD_OBJECT_H */
