/********************************************************************** 
 Freeciv - Copyright (C) 2003 - The Freeciv Project
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

#include "map.h"
#include "mem.h"		/* free */
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "terrain.h"

static struct tile_type tile_types[MAX_NUM_TERRAINS];

/***************************************************************
...
***************************************************************/
struct tile_type *get_tile_type(Terrain_type_id type)
{
  if (type < 0 || type >= T_COUNT) {
    /* HACK: return a dummy tile for out-of-range requests.  This is
     * designed specifically to fix this problem in 2.0. */
    static struct tile_type t_void;

#if 0 /* Currently this assertion triggers all the time. */
    assert(type >= 0 && type < T_COUNT);
#endif
    return &t_void;
  }
  return &tile_types[type];
}

/****************************************************************************
  Return the terrain type matching the name, or T_UNKNOWN if none matches.
****************************************************************************/
Terrain_type_id get_terrain_by_name(const char *name)
{
  Terrain_type_id tt;

  for (tt = T_FIRST; tt < T_COUNT; tt++) {
    if (0 == strcmp (tile_types[tt].terrain_name, name)) {
      return tt;
    }
  }

  return T_UNKNOWN;
}

/***************************************************************
...
***************************************************************/
const char *get_terrain_name(Terrain_type_id type)
{
  return get_tile_type(type)->terrain_name;
}

/****************************************************************************
  Return the terrain flag matching the given string, or TER_LAST if there's
  no match.
****************************************************************************/
enum terrain_flag_id terrain_flag_from_str(const char *s)
{
  enum terrain_flag_id flag;
  const char *flag_names[] = {
    /* Must match terrain flags in terrain.h. */
    "NoBarbs",
    "NoPollution",
    "NoCities",
    "Starter",
    "CanHaveRiver",
    "UnsafeCoast",
    "Unsafe",
    "Oceanic"
  };

  assert(ARRAY_SIZE(flag_names) == TER_COUNT);

  for (flag = TER_FIRST; flag < TER_LAST; flag++) {
    if (mystrcasecmp(flag_names[flag], s) == 0) {
      return flag;
    }
  }

  return TER_LAST;
}

/****************************************************************************
  Return a random terrain that has the specified flag.
****************************************************************************/
Terrain_type_id get_flag_terrain(enum terrain_flag_id flag)
{
  bool has_flag[T_COUNT];
  int count = 0;

  terrain_type_iterate(t) {
    if ((has_flag[t] = terrain_has_flag(t, flag))) {
      count++;
    }
  } terrain_type_iterate_end;

  count = myrand(count);
  terrain_type_iterate(t) {
    if (has_flag[t]) {
      if (count == 0) {
       return t;
      }
      count--;
    }
  } terrain_type_iterate_end;

  die("Reached end of get_flag_terrain!");
  return T_NONE;
}

/****************************************************************************
  Free memory which is associated with terrain types.
****************************************************************************/
void tile_types_free(void)
{
  terrain_type_iterate(t) {
    struct tile_type *p = get_tile_type(t);

    free(p->helptext);
    p->helptext = NULL;
  } terrain_type_iterate_end;
}

/****************************************************************************
  This iterator behaves like adjc_iterate or cardinal_adjc_iterate depending
  on the value of card_only.
****************************************************************************/
#define variable_adjc_iterate(center_tile, itr_tile, card_only)		    \
{									    \
  enum direction8 *_dirlist;						    \
  int _total;								    \
  									    \
  if (card_only) {							    \
    _dirlist = map.cardinal_dirs;					    \
    _total = map.num_cardinal_dirs;					    \
  } else {								    \
    _dirlist = map.valid_dirs;						    \
    _total = map.num_valid_dirs;					    \
  }									    \
  									    \
  adjc_dirlist_iterate(center_tile, itr_tile, _dir, _dirlist, _total) {

#define variable_adjc_iterate_end		                            \
  } adjc_dirlist_iterate_end;						    \
}


/****************************************************************************
  Returns TRUE iff any adjacent tile contains the given terrain.
****************************************************************************/
bool is_terrain_near_tile(const struct tile *ptile, Terrain_type_id t)
{
  adjc_iterate(ptile, adjc_tile) {
    if (adjc_tile->terrain == t) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return the number of adjacent tiles that have the given terrain.
****************************************************************************/
int count_terrain_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    Terrain_type_id t)
{
  int count = 0, total = 0;

  variable_adjc_iterate(ptile, adjc_tile, cardinal_only) {
    if (map_get_terrain(adjc_tile) == t) {
      count++;
    }
    total++;
  } variable_adjc_iterate_end;

  if (percentage) {
    count = count * 100 / total;
  }
  return count;
}

/****************************************************************************
  Returns TRUE iff any tile adjacent to (map_x,map_y) has the given special.
****************************************************************************/
bool is_special_near_tile(const struct tile *ptile, enum tile_special_type spe)
{
  adjc_iterate(ptile, adjc_tile) {
    if (map_has_special(adjc_tile, spe)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Returns the number of adjacent tiles that have the given map special.
****************************************************************************/
int count_special_near_tile(const struct tile *ptile,
			    bool cardinal_only, bool percentage,
			    enum tile_special_type spe)
{
  int count = 0, total = 0;

  variable_adjc_iterate(ptile, adjc_tile, cardinal_only) {
    if (map_has_special(adjc_tile, spe)) {
      count++;
    }
    total++;
  } variable_adjc_iterate_end;

  if (percentage) {
    count = count * 100 / total;
  }
  return count;
}

/****************************************************************************
  Returns TRUE iff any adjacent tile contains terrain with the given flag.
****************************************************************************/
bool is_terrain_flag_near_tile(const struct tile *ptile,
			       enum terrain_flag_id flag)
{
  adjc_iterate(ptile, adjc_tile) {
    if (terrain_has_flag(map_get_terrain(adjc_tile), flag)) {
      return TRUE;
    }
  } adjc_iterate_end;

  return FALSE;
}

/****************************************************************************
  Return the number of adjacent tiles that have terrain with the given flag.
****************************************************************************/
int count_terrain_flag_near_tile(const struct tile *ptile,
				 bool cardinal_only, bool percentage,
				 enum terrain_flag_id flag)
{
  int count = 0, total = 0;

  variable_adjc_iterate(ptile, adjc_tile, cardinal_only) {
    if (terrain_has_flag(map_get_terrain(adjc_tile), flag)) {
      count++;
    }
    total++;
  } variable_adjc_iterate_end;

  if (percentage) {
    count = count * 100 / total;
  }
  return count;
}
