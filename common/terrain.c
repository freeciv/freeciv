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

#include "fcintl.h"

#include "game.h"
#include "map.h"
#include "mem.h"		/* free */
#include "rand.h"
#include "shared.h"
#include "support.h"
#include "terrain.h"

struct tile_type tile_types[MAX_NUM_TERRAINS];

/***************************************************************
...
***************************************************************/
struct tile_type *get_tile_type(Terrain_type_id type)
{
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
  assert(type < T_COUNT);
  return tile_types[type].terrain_name;
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
    if (tile_get_terrain(adjc_tile) == t) {
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
  Return the number of adjacent tiles that have the given terrain property.
****************************************************************************/
int count_terrain_property_near_tile(const struct tile *ptile,
				     bool cardinal_only, bool percentage,
				     enum mapgen_terrain_property prop)
{
  int count = 0, total = 0;

  variable_adjc_iterate(ptile, adjc_tile, cardinal_only) {
    if (get_tile_type(adjc_tile->terrain)->property[prop] > 0) {
      count++;
    }
    total++;
  } variable_adjc_iterate_end;

  if (percentage) {
    count = count * 100 / total;
  }
  return count;
}

/* Names of specials.
 * (These must correspond to enum tile_special_type.)
 */
static const char *tile_special_type_names[] =
{
  N_("Special1"),
  N_("Road"),
  N_("Irrigation"),
  N_("Railroad"),
  N_("Mine"),
  N_("Pollution"),
  N_("Hut"),
  N_("Fortress"),
  N_("Special2"),
  N_("River"),
  N_("Farmland"),
  N_("Airbase"),
  N_("Fallout")
};

/****************************************************************************
  Return the special with the given name, or S_NO_SPECIAL.

  FIXME: should be find_special_by_name().
****************************************************************************/
enum tile_special_type get_special_by_name(const char *name)
{
  int i;
  enum tile_special_type st = 1;

  for (i = 0; i < ARRAY_SIZE(tile_special_type_names); i++) {
    if (0 == strcmp(name, tile_special_type_names[i])) {
      return st;
    }
      
    st <<= 1;
  }

  return S_NO_SPECIAL;
}

/****************************************************************************
  Return the name of the given special.
****************************************************************************/
const char *get_special_name(enum tile_special_type type)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(tile_special_type_names); i++) {
    if ((type & 0x1) == 1) {
      return _(tile_special_type_names[i]);
    }
    type >>= 1;
  }

  return NULL;
}

/***************************************************************
 Returns TRUE iff the given special is found in the given set.
***************************************************************/
bool contains_special(enum tile_special_type set,
		      enum tile_special_type to_test_for)
{
  enum tile_special_type masked = set & to_test_for;

  assert(0 == (int) S_NO_SPECIAL);

  /*
   * contains_special should only be called with one S_* in
   * to_test_for.
   */
  assert(masked == S_NO_SPECIAL || masked == to_test_for);

  return masked == to_test_for;
}

/****************************************************************************
  Returns TRUE iff any tile adjacent to (map_x,map_y) has the given special.
****************************************************************************/
bool is_special_near_tile(const struct tile *ptile, enum tile_special_type spe)
{
  adjc_iterate(ptile, adjc_tile) {
    if (tile_has_special(adjc_tile, spe)) {
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
    if (tile_has_special(adjc_tile, spe)) {
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
    if (terrain_has_flag(tile_get_terrain(adjc_tile), flag)) {
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
    if (terrain_has_flag(tile_get_terrain(adjc_tile), flag)) {
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
  Return a (static) string with special(s) name(s):
    eg: "Mine"
    eg: "Road/Farmland"
  This only includes "infrastructure", i.e., man-made specials.
****************************************************************************/
const char *get_infrastructure_text(enum tile_special_type spe)
{
  static char s[256];
  char *p;
  
  s[0] = '\0';

  /* Since railroad requires road, Road/Railroad is redundant */
  if (contains_special(spe, S_RAILROAD)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Railroad"));
  } else if (contains_special(spe, S_ROAD)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Road"));
  }

  /* Likewise for farmland on irrigation */
  if (contains_special(spe, S_FARMLAND)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Farmland"));
  } else if (contains_special(spe, S_IRRIGATION)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Irrigation"));
  }

  if (contains_special(spe, S_MINE)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Mine"));
  }

  if (contains_special(spe, S_FORTRESS)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Fortress"));
  }

  if (contains_special(spe, S_AIRBASE)) {
    cat_snprintf(s, sizeof(s), "%s/", _("Airbase"));
  }

  p = s + strlen(s) - 1;
  if (*p == '/') {
    *p = '\0';
  }

  return s;
}

/****************************************************************************
  Return the prerequesites needed before building the given infrastructure.
****************************************************************************/
enum tile_special_type get_infrastructure_prereq(enum tile_special_type spe)
{
  enum tile_special_type prereq = S_NO_SPECIAL;

  if (contains_special(spe, S_RAILROAD)) {
    prereq |= S_ROAD;
  }
  if (contains_special(spe, S_FARMLAND)) {
    prereq |= S_IRRIGATION;
  }

  return prereq;
}

/****************************************************************************
  Returns the highest-priority (best) infrastructure (man-made special) to
  be pillaged from the terrain set.  May return S_NO_SPECIAL if nothing
  better is available.
****************************************************************************/
enum tile_special_type get_preferred_pillage(enum tile_special_type pset)
{
  if (contains_special(pset, S_FARMLAND)) {
    return S_FARMLAND;
  }
  if (contains_special(pset, S_IRRIGATION)) {
    return S_IRRIGATION;
  }
  if (contains_special(pset, S_MINE)) {
    return S_MINE;
  }
  if (contains_special(pset, S_FORTRESS)) {
    return S_FORTRESS;
  }
  if (contains_special(pset, S_AIRBASE)) {
    return S_AIRBASE;
  }
  if (contains_special(pset, S_RAILROAD)) {
    return S_RAILROAD;
  }
  if (contains_special(pset, S_ROAD)) {
    return S_ROAD;
  }
  return S_NO_SPECIAL;
}
