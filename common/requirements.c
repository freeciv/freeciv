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

/* Names of source types.  These must correspond to enum req_source_type in
 * requirements.h.  Do not change these unless you know what you're doing! */
static const char *req_source_type_names[] = {
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

/**************************************************************************
  Parse a requirement type and value string into a requirement source
  structure.  Passing in a NULL type is considered REQ_NONE (not an error).

  Pass this some values like "Building", "Factory".
**************************************************************************/
struct req_source req_source_from_str(const char *type, const char *value)
{
  struct req_source source;
  const struct government *pgov;

  assert(ARRAY_SIZE(req_source_type_names) == REQ_LAST);
  if (type) {
    for (source.type = 0;
	 source.type < ARRAY_SIZE(req_source_type_names);
	 source.type++) {
      if (0 == mystrcasecmp(req_source_type_names[source.type], type)) {
	break;
      }
    }
  } else {
    source.type = REQ_NONE;
  }

  /* Finally scan the value string based on the type of the source. */
  switch (source.type) {
  case REQ_NONE:
    return source;
  case REQ_TECH:
    source.value.tech = find_tech_by_name(value);
    if (source.value.tech != A_LAST) {
      return source;
    }
    break;
  case REQ_GOV:
    pgov = find_government_by_name(value);
    if (pgov != NULL) {
      source.value.gov = pgov->index;
      return source;
    }
    break;
  case REQ_BUILDING:
    source.value.building = find_improvement_by_name(value);
    if (source.value.building != B_LAST) {
      return source;
    }
    break;
  case REQ_SPECIAL:
    source.value.special = get_special_by_name(value);
    if (source.value.special != S_NO_SPECIAL) {
      return source;
    }
    break;
  case REQ_TERRAIN:
    source.value.terrain = get_terrain_by_name(value);
    if (source.value.terrain != T_UNKNOWN) {
      return source;
    }
    break;
  case REQ_LAST:
    break;
  }

  /* If we reach here there's been an error. */
  source.type = REQ_LAST;
  return source;
}

/**************************************************************************
  Parse some integer values into a req source.  This is for serialization
  of req sources and is the opposite of req_source_get_values().
**************************************************************************/
struct req_source req_source_from_values(int type, int value)
{
  struct req_source source;

  source.type = type;

  switch (source.type) {
  case REQ_NONE:
    return source;
  case REQ_TECH:
    source.value.tech = value;
    return source;
  case REQ_GOV:
    source.value.gov = value;
    return source;
  case REQ_BUILDING:
    source.value.building = value;
    return source;
  case REQ_SPECIAL:
    source.value.special = value;
    return source;
  case REQ_TERRAIN:
    source.value.terrain = value;
    return source;
  case REQ_LAST:
    return source;
  }

  source.type = REQ_LAST;
  assert(0);
  return source;
}

/**************************************************************************
  Look at a req source and return some integer values describing it.  This
  is for serialization of req sources and is the opposite of
  req_source_from_values().
**************************************************************************/
void req_source_get_values(struct req_source *source, int *type, int *value)
{
  *type = source->type;

  switch (source->type) {
  case REQ_NONE:
    *value = 0;
    return;
  case REQ_TECH:
    *value = source->value.tech;
    return;
  case REQ_GOV:
    *value = source->value.gov;
    return;
  case REQ_BUILDING:
    *value = source->value.building;
    return;
  case REQ_SPECIAL:
    *value = source->value.special;
    return;
  case REQ_TERRAIN:
    *value = source->value.terrain;
    return;
  case REQ_LAST:
    break;
  }

  assert(0);
  *value = 0;
}

/****************************************************************************
  Parse a requirement type and value string into a requrement structure.
  Returns REQ_LAST on error.  Passing in a NULL type is considered REQ_NONE
  (not an error).

  Pass this some values like "Building", "Factory".
****************************************************************************/
struct requirement req_from_str(const char *type,
				const char *range, bool survives,
				const char *value)
{
  struct requirement req;

  req.source = req_source_from_str(type, value);

  /* Scan the range string to find the range.  If no range is given a
   * default fallback is used rather than giving an error. */
  req.range = req_range_from_str(range);
  if (req.range == REQ_RANGE_LAST) {
    switch (req.source.type) {
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
  return req;
}

/****************************************************************************
  Set the values of a req from serializable integers.  This is the opposite
  of req_get_values.
****************************************************************************/
struct requirement req_from_values(int type, int range,
				   bool survives, int value)
{
  struct requirement req;

  req.source = req_source_from_values(type, value);
  req.range = range;
  req.survives = survives;
  return req;
}

/****************************************************************************
  Return the value of a req as a serializable integer.  This is the opposite
  of req_set_value.
****************************************************************************/
void req_get_values(struct requirement *req,
		    int *type, int *range, bool *survives, int *value)
{
  req_source_get_values(&req->source, type, value);
  *range = req->range;
  *survives = req->survives;
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
  if (is_great_wonder(building)) {
    return (great_wonder_was_built(building) ? 1 : 0);
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
  if (is_great_wonder(id)) {
    return (find_city_from_great_wonder(id) ? 1 : 0);
  } else {
    freelog(LOG_ERROR,
	    /* TRANS: Obscure ruleset error. */
	    _("World-ranged requirements are only supported for wonders."));
    return 0;
  }
}

/**************************************************************************
  Returns the player city with the given wonder.
**************************************************************************/
static struct city *player_find_city_from_wonder(const struct player *plr,
						 Impr_Type_id id)
{
  int city_id;
  struct city *pcity;

  if (is_great_wonder(id)) {
    city_id = game.great_wonders[id];
  } else if (is_small_wonder(id)) {
    city_id = plr->small_wonders[id];
  } else {
    return NULL;
  }

  pcity = find_city_by_id(city_id);
  if (pcity && (city_owner(pcity) == plr)) {
    return pcity;
  } else {
    return NULL;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type owned by plr.
****************************************************************************/
static int num_player_buildings(const struct player *pplayer,
				Impr_Type_id building)
{
  if (is_wonder(building)) {
    return (player_find_city_from_wonder(pplayer, building) ? 1 : 0);
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

    pcity = player_find_city_from_wonder(pplayer, building);
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
  switch (req->source.type) {
  case REQ_NONE:
    return TRUE;
  case REQ_TECH:
    /* The requirement is filled if the player owns the tech. */
    return (target_player
	    && get_invention(target_player,
			     req->source.value.tech) == TECH_KNOWN);
  case REQ_GOV:
    /* The requirement is filled if the player is using the government. */
    return (target_player
	    && (target_player->government == req->source.value.gov));
  case REQ_BUILDING:
    /* The requirement is filled if there's at least one of the building
     * in the city.  (This is a slightly nonstandard use of
     * count_sources_in_range.) */
    return (count_buildings_in_range(target, target_player, target_city,
				     target_building,
				     req->range, req->survives,
				     req->source.value.building) > 0);
  case REQ_SPECIAL:
    /* The requirement is filled if the tile has the special. */
    return (target_tile
	    && tile_has_special(target_tile, req->source.value.special));
  case REQ_TERRAIN:
    /* The requirement is filled if the tile has the terrain. */
    return (target_tile
	    && (target_tile->terrain  == req->source.value.terrain));
  case REQ_LAST:
    break;
  }

  assert(0);
  return FALSE;
}
