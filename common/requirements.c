/********************************************************************** 
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
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
#include "log.h"
#include "support.h"

#include "government.h"
#include "improvement.h"
#include "map.h"
#include "requirements.h"

/* Names of requirement types.  These must correspond to enum req_type in
 * requirements.h.  Do not change these unless you know what you're doing! */
static const char *req_type_names[] = {
  "None",
  "Tech",
  "Gov",
  "Building",
  "Special",
  "Terrain"
};

/* Names of requirement ranges. These must correspond to enum req_range in
 * requirements.h.  Do not change these unless you know what you're doing! */
static const char *req_range_names[REQ_RANGE_LAST] = {
  "Local",
  "City",
  "Continent",
  "Player",
  "World"
};

/**************************************************************************
  Convert a range name to an enumerated value.

  The check is case insensitive and returns REQ_RANGE_LAST if no match
  is found.
**************************************************************************/
enum req_range req_range_from_str(const char *str)
{
  enum req_range range;

  assert(ARRAY_SIZE(req_range_names) == REQ_RANGE_LAST);

  for (range = 0; range < REQ_RANGE_LAST; range++) {
    if (0 == mystrcasecmp(req_range_names[range], str)) {
      return range;
    }
  }

  return REQ_RANGE_LAST;
}

/****************************************************************************
  Parse a requirement type and value string into a requrement structure.
  Returns REQ_LAST on error.

  Pass this some values like "Building", "Factory".
****************************************************************************/
struct requirement req_from_str(const char *type,
				const char *range, bool survives,
				const char *value)
{
  struct requirement req;
  const struct government *pgov;

  assert(ARRAY_SIZE(req_type_names) == REQ_LAST);
  for (req.type = 0; req.type < ARRAY_SIZE(req_type_names); req.type++) {
    if (0 == mystrcasecmp(req_type_names[req.type], type)) {
      break;
    }
  }

  /* Scan the range string to find the range.  If no range is given a
   * default fallback is used rather than giving an error. */
  req.range = req_range_from_str(range);
  if (req.range == REQ_RANGE_LAST) {
    switch (req.type) {
    case REQ_NONE:
    case REQ_LAST:
      break;
    case REQ_BUILDING:
    case REQ_SPECIAL:
    case REQ_TERRAIN:
      req.range = REQ_RANGE_CITY;
      break;
    case REQ_GOV:
    case REQ_TECH:
      req.range = REQ_RANGE_PLAYER;
      break;
    }
  }

  req.survives = survives;

  /* Finally scan the value string based on the type of the req. */
  switch (req.type) {
  case REQ_NONE:
    return req;
  case REQ_TECH:
    req.value.tech = find_tech_by_name(value);
    if (req.value.tech != A_LAST) {
      return req;
    }
    break;
  case REQ_GOV:
    pgov = find_government_by_name(value);
    if (pgov != NULL) {
      req.value.gov = pgov->index;
      return req;
    }
    break;
  case REQ_BUILDING:
    req.value.building = find_improvement_by_name(value);
    if (req.value.building != B_LAST) {
      return req;
    }
    break;
  case REQ_SPECIAL:
    req.value.special = get_special_by_name(value);
    if (req.value.special != S_NO_SPECIAL) {
      return req;
    }
    break;
  case REQ_TERRAIN:
    req.value.terrain = get_terrain_by_name(value);
    if (req.value.terrain != T_UNKNOWN) {
      return req;
    }
    break;
  case REQ_LAST:
    break;
  }

  /* If we reach here there's been an error. */
  req.type = REQ_LAST;
  return req;
}

/****************************************************************************
  Return the value of a req as a serializable integer.  This is the opposite
  of req_set_value.
****************************************************************************/
void req_get_values(struct requirement *req,
		    int *type, int *range, bool *survives, int *value)
{
  *type = req->type;
  *range = req->range;
  *survives = req->survives;

  switch (req->type) {
  case REQ_NONE:
    *value = 0;
    return;
  case REQ_TECH:
    *value = req->value.tech;
    return;
  case REQ_GOV:
    *value = req->value.gov;
    return;
  case REQ_BUILDING:
    *value = req->value.building;
    return;
  case REQ_SPECIAL:
    *value = req->value.special;
    return;
  case REQ_TERRAIN:
    *value = req->value.terrain;
    return;
  case REQ_LAST:
    break;
  }

  assert(0);
  *value = 0;
}

/****************************************************************************
  Set the values of a req from serializable integers.  This is the opposite
  of req_get_values.
****************************************************************************/
struct requirement req_from_values(int type, int range,
				   bool survives, int value)
{
  struct requirement req;

  req.type = type;
  req.range = range;
  req.survives = survives;

  switch (req.type) {
  case REQ_NONE:
    return req;
  case REQ_TECH:
    req.value.tech = value;
    return req;
  case REQ_GOV:
    req.value.gov = value;
    return req;
  case REQ_BUILDING:
    req.value.building = value;
    return req;
  case REQ_SPECIAL:
    req.value.special = value;
    return req;
  case REQ_TERRAIN:
    req.value.terrain = value;
    return req;
  case REQ_LAST:
    return req;
  }

  req.type = REQ_LAST;
  assert(0);
  return req;
}

/****************************************************************************
  Is this target possible for the range involved?

  For instance a city-ranged requirement can't have a player as it's target.
  See the comment at enum target_type.
****************************************************************************/
static bool is_target_possible(enum target_type target,
			       enum req_range range)
{
  switch (target) {
  case TARGET_PLAYER:
    return (range >= REQ_RANGE_PLAYER);
  case TARGET_CITY:
    return (range >= REQ_RANGE_CITY);
  case TARGET_BUILDING:
    return (range >= REQ_RANGE_LOCAL);
  }
  assert(0);
  return FALSE;
}

/****************************************************************************
  Returns the number of total world buildings (this includes buildings
  that have been destroyed).
****************************************************************************/
static int num_world_buildings_total(Impr_Type_id building)
{
  if (is_wonder(building)) {
    return (game.global_wonders[building] != 0) ? 1 : 0;
  } else {
    freelog(LOG_ERROR,
	    /* TRANS: Obscure ruleset error. */
	    _("World-ranged requirements are only supported for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type in the world.
****************************************************************************/
static int num_world_buildings(Impr_Type_id id)
{
  if (is_wonder(id)) {
    return find_city_by_id(game.global_wonders[id]) ? 1 : 0;
  } else {
    freelog(LOG_ERROR,
	    /* TRANS: Obscure ruleset error. */
	    _("World-ranged requirements are only supported for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type owned by plr.
****************************************************************************/
static int num_player_buildings(const struct player *pplayer,
				Impr_Type_id building)
{
  if (is_wonder(building)) {
    if (player_find_city_by_id(pplayer, game.global_wonders[building])) {
      return 1;
    } else {
      return 0;
    }
  } else {
    freelog(LOG_ERROR,
	    /* TRANS: Obscure ruleset error. */
	    _("Player-ranged requirements are only supported for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type on a continent.
****************************************************************************/
static int num_continent_buildings(const struct player *pplayer,
				   int continent, Impr_Type_id building)
{
  if (is_wonder(building)) {
    const struct city *pcity;

    pcity = player_find_city_by_id(pplayer, game.global_wonders[building]);
    if (pcity && map_get_continent(pcity->tile) == continent) {
      return 1;
    }
  } else {
    freelog(LOG_ERROR,
	    /* TRANS: Obscure ruleset error. */
	    _("Island-ranged requirements are only supported for wonders."));
  }
  return 0;
}

/****************************************************************************
  Returns the number of buildings of a certain type in a city.
****************************************************************************/
static int num_city_buildings(const struct city *pcity, Impr_Type_id id)
{
  return (city_got_building(pcity, id) ? 1 : 0);
}

/****************************************************************************
  How many of the source building are there within range of the target?

  The target gives the type of the target.  The exact target is a player,
  city, or building specified by the target_xxx arguments.

  The range gives the range of the requirement.

  "Survives" specifies whether the requirement allows destroyed sources.
  If set then all source buildings ever built are counted; if not then only
  living buildings are counted.

  source gives the building type of the source in question.

  Note that this function does a lookup into the source caches to find
  the number of available sources.  However not all source caches exist: if
  the cache doesn't exist then we return 0.
****************************************************************************/
int count_buildings_in_range(enum target_type target,
			     const struct player *target_player,
			     const struct city *target_city,
			     Impr_Type_id target_building,
			     enum req_range range, bool survives,
			     Impr_Type_id source)
{
  if (!is_target_possible(target, range)) {
    return 0;
  }

  if (improvement_obsolete(target_player, source)) {
    return 0;
  }

  if (survives) {
    if (range == REQ_RANGE_WORLD) {
      return num_world_buildings_total(source);
    } else {
      /* There is no sources cache for this. */
      freelog(LOG_ERROR,
	      /* TRANS: Obscure ruleset error. */
	      _("Surviving requirements are only "
		"supported at world range."));
      return 0;
    }
  }

  switch (range) {
  case REQ_RANGE_WORLD:
    return num_world_buildings(source);
  case REQ_RANGE_PLAYER:
    return num_player_buildings(target_player, source);
  case REQ_RANGE_CONTINENT:
    {
      int continent = map_get_continent(target_city->tile);

      return num_continent_buildings(target_player, continent, source);
    }
  case REQ_RANGE_CITY:
    return num_city_buildings(target_city, source);
  case REQ_RANGE_LOCAL:
    if (target_building == source) {
      return num_city_buildings(target_city, source);
    } else {
      return 0;
    }
  case REQ_RANGE_LAST:
    break;
  }
  assert(0);
  return 0;
}

/****************************************************************************
  Checks the requirement to see if it is active on the given target.

  target gives the type of the target
  (player,city,building,tile) give the exact target
  req gives the requirement itself

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
****************************************************************************/
bool is_req_active(enum target_type target,
		   const struct player *target_player,
		   const struct city *target_city,
		   Impr_Type_id target_building,
		   const struct tile *target_tile,
		   const struct requirement *req)
{
  /* Note the target may actually not exist.  In particular, effects that
   * have a REQ_SPECIAL or REQ_TERRAIN may often be passed to this function
   * with a city as their target.  In this case the requirement is simply
   * not met. */
  switch (req->type) {
  case REQ_NONE:
    return TRUE;
  case REQ_TECH:
    /* The requirement is filled if the player owns the tech. */
    return (target_player
	    && get_invention(target_player, req->value.tech) == TECH_KNOWN);
  case REQ_GOV:
    /* The requirement is filled if the player is using the government. */
    return target_player && (target_player->government == req->value.gov);
  case REQ_BUILDING:
    /* The requirement is filled if there's at least one of the building
     * in the city.  (This is a slightly nonstandard use of
     * count_sources_in_range.) */
    return (count_buildings_in_range(target, target_player, target_city,
				     target_building, REQ_RANGE_CITY, FALSE,
				     req->value.building) > 0);
  case REQ_SPECIAL:
    /* The requirement is filled if the tile has the special. */
    return target_tile && tile_has_special(target_tile, req->value.special);
  case REQ_TERRAIN:
    /* The requirement is filled if the tile has the terrain. */
    return target_tile && (target_tile->terrain  == req->value.terrain);
  case REQ_LAST:
    break;
  }

  assert(0);
  return FALSE;
}
