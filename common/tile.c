/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
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

#include "log.h"
#include "support.h"

#include "tile.h"

/****************************************************************************
  Return the player who owns this tile (or NULL if none).
****************************************************************************/
struct player *tile_get_owner(const struct tile *ptile)
{
  return ptile->owner;
}

/****************************************************************************
  Set the owner of a tile (may be NULL).
****************************************************************************/
void tile_set_owner(struct tile *ptile, struct player *owner)
{
  ptile->owner = owner;
}

/****************************************************************************
  Return the city on this tile (or NULL if none).
****************************************************************************/
struct city *tile_get_city(const struct tile *ptile)
{
  return ptile->city;
}

/****************************************************************************
  Set the city on the tile (may be NULL).
****************************************************************************/
void tile_set_city(struct tile *ptile, struct city *pcity)
{
  ptile->city = pcity;
}

/****************************************************************************
  Return the terrain ID of the tile.  Terrains are defined in the ruleset
  (see terrain.h).
****************************************************************************/
struct terrain *tile_get_terrain(const struct tile *ptile)
{
  return ptile->terrain;
}

/****************************************************************************
  Set the terrain ID of the tile.  See tile_get_terrain.
****************************************************************************/
void tile_set_terrain(struct tile *ptile, struct terrain *pterrain)
{
  ptile->terrain = pterrain;
  if (NULL != pterrain
   && NULL != ptile->resource
   && terrain_has_resource(pterrain, ptile->resource)) {
    /* cannot use set_special() for internal values */
    BV_SET(ptile->special, S_RESOURCE_VALID);
  } else {
    BV_CLR(ptile->special, S_RESOURCE_VALID);
  }
}

/****************************************************************************
  Return the specials of the tile.  See terrain.h.

  Note that this returns a mask of _all_ the specials on the tile.  To
  check a specific special use tile_has_special.
****************************************************************************/
bv_special tile_get_special(const struct tile *ptile)
{
  return ptile->special;
}

/****************************************************************************
  Returns TRUE iff the given tile has the given special.
****************************************************************************/
bool tile_has_special(const struct tile *ptile,
		      enum tile_special_type special)
{
  return contains_special(ptile->special, special);
}

/****************************************************************************
  Returns TRUE iff the given tile has any specials.
****************************************************************************/
bool tile_has_any_specials(const struct tile *ptile)
{
  return contains_any_specials(ptile->special);
}

/****************************************************************************
  Returns base at tile or NULL if no base
****************************************************************************/
struct base_type *tile_get_base(const struct tile *ptile)
{
  return base_of_bv_special(ptile->special);
}

/****************************************************************************
  Adds base to tile.
  FIXME: Currently this asserts that tile contains no old base.
         Instead should remove old base and return bool indicating that.
****************************************************************************/
void tile_add_base(struct tile *ptile, const struct base_type *pbase)
{
  assert(pbase != NULL);

  switch (base_number(pbase)) {
  case BASE_FORTRESS:
    assert(!tile_has_special(ptile, S_AIRBASE));
    tile_set_special(ptile, S_FORTRESS);
    break;
  case BASE_AIRBASE:
    assert(!tile_has_special(ptile, S_FORTRESS));
    tile_set_special(ptile, S_AIRBASE);
    break;
  default:
    freelog(LOG_ERROR, "tile_set_base(): impossible base type %d.",
            base_number(pbase));
    break;
  };
}

/****************************************************************************
  Removes base from tile if such exist
****************************************************************************/
void tile_remove_base(struct tile *ptile)
{
  tile_clear_special(ptile, S_FORTRESS);
  tile_clear_special(ptile, S_AIRBASE);
}

/****************************************************************************
  Check if tile contains base providing effect
****************************************************************************/
bool tile_has_base_flag(const struct tile *ptile, enum base_flag_id flag)
{
  struct base_type *pbase;

  pbase = tile_get_base(ptile);

  if (pbase != NULL) {
    /* Some base at tile, check its flags */
    return base_has_flag(pbase, flag);
  }

  /* No base at tile */
  return FALSE;
}

/****************************************************************************
  Check if tile contains base providing effect for unit
****************************************************************************/
bool tile_has_base_flag_for_unit(const struct tile *ptile,
                                 const struct unit_type *punittype,
                                 enum base_flag_id flag)
{
  struct base_type *pbase;

  pbase = tile_get_base(ptile);

  if (pbase != NULL) {
    /* Some base at tile, check its flags */
    return base_has_flag_for_utype(pbase, flag, punittype);
  }

  /* No base at tile */
  return FALSE;
}

/****************************************************************************
  Check if tile contains base native for unit
****************************************************************************/
bool tile_has_native_base(const struct tile *ptile,
                          const struct unit_type *punittype)
{
  struct base_type *pbase;

  pbase = tile_get_base(ptile);

  if (pbase != NULL) {
    /* Some base at tile, check if it's native */
    return is_native_base_to_utype(pbase, punittype);
  }

  /* No base at tile */
  return FALSE;
}

/****************************************************************************
  Add the given special or specials to the tile.

  Note that this does not erase any existing specials already on the tile
  (see tile_clear_special or tile_clear_all_specials for that).  Also note
  the special to be set may be a mask, so you can set more than one
  special at a time (but this is not recommended).
****************************************************************************/
void tile_set_special(struct tile *ptile, enum tile_special_type spe)
{
  set_special(&ptile->special, spe);
}

/****************************************************************************
  Return the resource at the specified tile.
****************************************************************************/
const struct resource *tile_get_resource(const struct tile *ptile)
{
  return ptile->resource;
}

/****************************************************************************
  Set the given resource at the specified tile.
****************************************************************************/
void tile_set_resource(struct tile *ptile, struct resource *presource)
{
  ptile->resource = presource;
  if (NULL != ptile->terrain
   && NULL != presource
   && terrain_has_resource(ptile->terrain, presource)) {
    /* cannot use set_special() for internal values */
    BV_SET(ptile->special, S_RESOURCE_VALID);
  } else {
    BV_CLR(ptile->special, S_RESOURCE_VALID);
  }
}

/****************************************************************************
  Clear the given special or specials from the tile.

  This function clears all the specials set in the 'spe' mask from the
  tile's set of specials.  All other specials are unaffected.
****************************************************************************/
void tile_clear_special(struct tile *ptile, enum tile_special_type spe)
{
  clear_special(&ptile->special, spe);
}

/****************************************************************************
  Remove any and all specials from this tile.
****************************************************************************/
void tile_clear_all_specials(struct tile *ptile)
{
  clear_all_specials(&ptile->special);
}

/****************************************************************************
  Return the continent ID of the tile.  Typically land has a positive
  continent number and ocean has a negative number; no tile should have
  a 0 continent number.
****************************************************************************/
Continent_id tile_get_continent(const struct tile *ptile)
{
  return ptile->continent;
}

/****************************************************************************
  Set the continent ID of the tile.  See tile_get_continent.
****************************************************************************/
void tile_set_continent(struct tile *ptile, Continent_id val)
{
  ptile->continent = val;
}

/****************************************************************************
  Return a known_type enumeration value for the tile.

  Note that the client only knows known data about game.player_ptr.
****************************************************************************/
enum known_type tile_get_known(const struct tile *ptile,
			       const struct player *pplayer)
{
  if (!BV_ISSET(ptile->tile_known, player_index(pplayer))) {
    return TILE_UNKNOWN;
  } else if (!BV_ISSET(ptile->tile_seen[V_MAIN], player_index(pplayer))) {
    return TILE_KNOWN_FOGGED;
  } else {
    return TILE_KNOWN;
  }
}

/****************************************************************************
  Time to complete the given activity on the given tile.
****************************************************************************/
int tile_activity_time(enum unit_activity activity, const struct tile *ptile)
{
  /* Make sure nobody uses old activities */
  assert(activity != ACTIVITY_FORTRESS && activity != ACTIVITY_AIRBASE);

  /* ACTIVITY_BASE not handled here */
  assert(activity != ACTIVITY_BASE);

  switch (activity) {
  case ACTIVITY_POLLUTION:
    return ptile->terrain->clean_pollution_time * ACTIVITY_FACTOR;
  case ACTIVITY_ROAD:
    return ptile->terrain->road_time * ACTIVITY_FACTOR;
  case ACTIVITY_MINE:
    return ptile->terrain->mining_time * ACTIVITY_FACTOR;
  case ACTIVITY_IRRIGATE:
    return ptile->terrain->irrigation_time * ACTIVITY_FACTOR;
  case ACTIVITY_RAILROAD:
    return ptile->terrain->rail_time * ACTIVITY_FACTOR;
  case ACTIVITY_TRANSFORM:
    return ptile->terrain->transform_time * ACTIVITY_FACTOR;
  case ACTIVITY_FALLOUT:
    return ptile->terrain->clean_fallout_time * ACTIVITY_FACTOR;
  default:
    return 0;
  }
}

/****************************************************************************
  Time to complete the given activity on the given tile.
****************************************************************************/
int tile_activity_base_time(const struct tile *ptile,
                            enum base_type_id base)
{
  if (base == BASE_AIRBASE) {
    return base_by_number(BASE_AIRBASE)->build_time * ACTIVITY_FACTOR;
  } else {
    return base_by_number(BASE_FORTRESS)->build_time * ACTIVITY_FACTOR;
  }

  return 0;
}

/****************************************************************************
  Clear all infrastructure (man-made specials) from the tile.
****************************************************************************/
static void tile_clear_infrastructure(struct tile *ptile)
{
  int i;

  for (i = 0; infrastructure_specials[i] != S_LAST; i++) {
    tile_clear_special(ptile, infrastructure_specials[i]);
  }
}

/****************************************************************************
  Clear all "dirtiness" (pollution and fallout) from the tile.
****************************************************************************/
static void tile_clear_dirtiness(struct tile *ptile)
{
  tile_clear_special(ptile, S_POLLUTION);
  tile_clear_special(ptile, S_FALLOUT);
}

/****************************************************************************
  Change the terrain to the given type.  This does secondary tile updates to
  the tile (as will happen when mining/irrigation/transforming changes the
  tile's terrain).
****************************************************************************/
void tile_change_terrain(struct tile *ptile, struct terrain *pterrain)
{
  tile_set_terrain(ptile, pterrain);
  if (is_ocean(pterrain)) {
    tile_clear_infrastructure(ptile);
    tile_clear_dirtiness(ptile);

    /* The code can't handle these specials in ocean. */
    tile_clear_special(ptile, S_RIVER);
    tile_clear_special(ptile, S_HUT);
  }

  /* Clear mining/irrigation if resulting terrain type cannot support
   * that feature. */
  
  if (pterrain->mining_result != pterrain) {
    tile_clear_special(ptile, S_MINE);
  }

  if (pterrain->irrigation_result != pterrain) {
    tile_clear_special(ptile, S_IRRIGATION);
    tile_clear_special(ptile, S_FARMLAND);
  }
}

/****************************************************************************
  Add the special to the tile.  This does secondary tile updates to
  the tile.
****************************************************************************/
void tile_add_special(struct tile *ptile, enum tile_special_type special)
{
  tile_set_special(ptile, special);

  switch (special) {
  case S_FARMLAND:
    tile_add_special(ptile, S_IRRIGATION);
    /* Fall through to irrigation */
  case S_IRRIGATION:
    tile_clear_special(ptile, S_MINE);
    break;
  case S_RAILROAD:
    tile_add_special(ptile, S_ROAD);
    break;
  case S_MINE:
    tile_clear_special(ptile, S_IRRIGATION);
    tile_clear_special(ptile, S_FARMLAND);
    break;

  case S_ROAD:
  case S_POLLUTION:
  case S_HUT:
  case S_FORTRESS:
  case S_RIVER:
  case S_AIRBASE:
  case S_FALLOUT:
  default:
    break;
  }
}

/****************************************************************************
  Remove the special from the tile.  This does secondary tile updates to
  the tile.
****************************************************************************/
void tile_remove_special(struct tile *ptile, enum tile_special_type special)
{
  tile_clear_special(ptile, special);

  switch (special) {
  case S_IRRIGATION:
    tile_clear_special(ptile, S_FARMLAND);
    break;
  case S_ROAD:
    tile_clear_special(ptile, S_RAILROAD);
    break;

  case S_RAILROAD:
  case S_MINE:
  case S_POLLUTION:
  case S_HUT:
  case S_FORTRESS:
  case S_RIVER:
  case S_FARMLAND:
  case S_AIRBASE:
  case S_FALLOUT:
  default:
    break;
  }
}

/****************************************************************************
  Build irrigation on the tile.  This may change the specials of the tile
  or change the terrain type itself.
****************************************************************************/
static void tile_irrigate(struct tile *ptile)
{
  if (ptile->terrain == ptile->terrain->irrigation_result) {
    if (tile_has_special(ptile, S_IRRIGATION)) {
      tile_add_special(ptile, S_FARMLAND);
    } else {
      tile_add_special(ptile, S_IRRIGATION);
    }
  } else if (ptile->terrain->irrigation_result) {
    tile_change_terrain(ptile, ptile->terrain->irrigation_result);
  }
}

/****************************************************************************
  Build a mine on the tile.  This may change the specials of the tile
  or change the terrain type itself.
****************************************************************************/
static void tile_mine(struct tile *ptile)
{
  if (ptile->terrain == ptile->terrain->mining_result) {
    tile_set_special(ptile, S_MINE);
    tile_clear_special(ptile, S_FARMLAND);
    tile_clear_special(ptile, S_IRRIGATION);
  } else if (ptile->terrain->mining_result) {
    tile_change_terrain(ptile, ptile->terrain->mining_result);
  }
}

/****************************************************************************
  Transform (ACTIVITY_TRANSFORM) the tile.  This usually changes the tile's
  terrain type.
****************************************************************************/
static void tile_transform(struct tile *ptile)
{
  if (ptile->terrain->transform_result != T_NONE) {
    tile_change_terrain(ptile, ptile->terrain->transform_result);
  }
}

/****************************************************************************
  Apply an activity (Activity_type_id, e.g., ACTIVITY_TRANSFORM) to a tile.
  Return false if there was a error or if the activity is not implemented
  by this function.
****************************************************************************/
bool tile_apply_activity(struct tile *ptile, Activity_type_id act) 
{
  /* FIXME: for irrigate, mine, and transform we always return TRUE
   * even if the activity fails. */
  switch(act) {
  case ACTIVITY_POLLUTION:
  case ACTIVITY_FALLOUT: 
    tile_clear_dirtiness(ptile);
    return TRUE;
    
  case ACTIVITY_MINE:
    tile_mine(ptile);
    return TRUE;

  case ACTIVITY_IRRIGATE: 
    tile_irrigate(ptile);
    return TRUE;

  case ACTIVITY_ROAD: 
    if (!is_ocean(ptile->terrain)
	&& !tile_has_special(ptile, S_ROAD)) {
      tile_set_special(ptile, S_ROAD);
      return TRUE;
    }
    return FALSE;

  case ACTIVITY_RAILROAD:
    if (!is_ocean(ptile->terrain)
	&& !tile_has_special(ptile, S_RAILROAD)
	&& tile_has_special(ptile, S_ROAD)) {
      tile_set_special(ptile, S_RAILROAD);
      return TRUE;
    }
    return FALSE;

  case ACTIVITY_TRANSFORM:
    tile_transform(ptile);
    return TRUE;
    
  case ACTIVITY_FORTRESS:
  case ACTIVITY_PILLAGE: 
  case ACTIVITY_AIRBASE:   
  case ACTIVITY_BASE:
    /* do nothing  - not implemented */
    return FALSE;

  case ACTIVITY_IDLE:
  case ACTIVITY_FORTIFIED:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_FORTIFYING:
  case ACTIVITY_PATROL_UNUSED:
  case ACTIVITY_LAST:
    /* do nothing - these activities have no effect
       on terrain type or tile specials */
    return FALSE;
  }
  assert(0);
  return FALSE;
}

/****************************************************************************
  Add one entry about pollution situation to buffer.
  Return if there has been any pollution (even prior calling this)
****************************************************************************/
static bool tile_info_pollution(char *buf, int bufsz,
                                const struct tile *ptile,
                                enum tile_special_type special,
                                bool prevp, bool linebreak)
{
  if (tile_has_special(ptile, special)) {
    if (!prevp) {
      if (linebreak) {
        mystrlcat(buf, "\n[", bufsz);
      } else {
        mystrlcat(buf, " [", bufsz);
      }
    } else {
      mystrlcat(buf, "/", bufsz);
    }

    mystrlcat(buf, special_name_translation(special), bufsz);

    return TRUE;
  }

  return prevp;
}

/****************************************************************************
  Return a (static) string with tile name describing terrain and specials.

  Examples:
    "Hills"
    "Hills (Coals)"
    "Hills (Coals) [Pollution]"
****************************************************************************/
const char *tile_get_info_text(const struct tile *ptile, int linebreaks)
{
  static char s[256];
  bool pollution;
  bool lb = FALSE;
  int bufsz = sizeof(s);

  sz_strlcpy(s, terrain_name_translation(ptile->terrain));
  if (linebreaks & TILE_LB_TERRAIN_RIVER) {
    /* Linebreak needed before next text */
    lb = TRUE;
  }

  if (tile_has_special(ptile, S_RIVER)) {
    if (lb) {
      sz_strlcat(s, "\n");
      lb = FALSE;
    } else {
      sz_strlcat(s, "/");
    }
    sz_strlcat(s, special_name_translation(S_RIVER));
  }
  if (linebreaks & TILE_LB_RIVER_RESOURCE) {
    /* New linebreak requested */
    lb = TRUE;
  }

  if (tile_resource_is_valid(ptile)) {
    if (lb) {
      sz_strlcat(s, "\n");
      lb = FALSE;
    } else {
      sz_strlcat(s, " ");
    }
    cat_snprintf(s, sizeof(s), "(%s)",
		 resource_name_translation(ptile->resource));
  }
  if (linebreaks & TILE_LB_RESOURCE_POLL) {
    /* New linebreak requested */
    lb = TRUE;
  }

  pollution = FALSE;
  pollution = tile_info_pollution(s, bufsz, ptile, S_POLLUTION, pollution, lb);
  pollution = tile_info_pollution(s, bufsz, ptile, S_FALLOUT, pollution, lb);
  if (pollution) {
    sz_strlcat(s, "]");
  }

  return s;
}
