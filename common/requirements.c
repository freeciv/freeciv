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
#include <fc_config.h>
#endif

/* utility */
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "support.h"

/* common */
#include "government.h"
#include "improvement.h"
#include "map.h"
#include "road.h"
#include "specialist.h"

#include "requirements.h"

/**************************************************************************
  Parse requirement type (kind) and value strings into a universal
  structure.  Passing in a NULL type is considered VUT_NONE (not an error).

  Pass this some values like "Building", "Factory".
  FIXME: ensure that every caller checks error return!
**************************************************************************/
struct universal universal_by_rule_name(const char *kind,
					const char *value)
{
  struct universal source;

  source.kind = universals_n_by_name(kind, fc_strcasecmp);
  if (!universals_n_is_valid(source.kind)) {
    return source;
  }

  /* Finally scan the value string based on the type of the source. */
  switch (source.kind) {
  case VUT_NONE:
    return source;
  case VUT_ADVANCE:
    source.value.advance = advance_by_rule_name(value);
    if (source.value.advance != NULL) {
      return source;
    }
    break;
  case VUT_GOVERNMENT:
    source.value.govern = government_by_rule_name(value);
    if (source.value.govern != NULL) {
      return source;
    }
    break;
  case VUT_IMPROVEMENT:
    source.value.building = improvement_by_rule_name(value);
    if (source.value.building != NULL) {
      return source;
    }
    break;
  case VUT_SPECIAL:
    source.value.special = special_by_rule_name(value);
    if (source.value.special != S_LAST) {
      return source;
    }
    break;
  case VUT_TERRAIN:
    source.value.terrain = terrain_by_rule_name(value);
    if (source.value.terrain != T_UNKNOWN) {
      return source;
    }
    break;
  case VUT_TERRFLAG:
    source.value.terrainflag
      = terrain_flag_id_by_name(value, fc_strcasecmp);
    if (terrain_flag_id_is_valid(source.value.terrainflag)) {
      return source;
    }
    break;
  case VUT_RESOURCE:
    source.value.resource = resource_by_rule_name(value);
    if (source.value.resource != NULL) {
      return source;
    }
    break;
  case VUT_NATION:
    source.value.nation = nation_by_rule_name(value);
    if (source.value.nation != NO_NATION_SELECTED) {
      return source;
    }
    break;
  case VUT_UTYPE:
    source.value.utype = unit_type_by_rule_name(value);
    if (source.value.utype) {
      return source;
    }
    break;
  case VUT_UTFLAG:
    source.value.unitflag = unit_type_flag_id_by_name(value, fc_strcasecmp);
    if (unit_type_flag_id_is_valid(source.value.unitflag)) {
      return source;
    }
    break;
  case VUT_UCLASS:
    source.value.uclass = unit_class_by_rule_name(value);
    if (source.value.uclass) {
      return source;
    }
    break;
  case VUT_UCFLAG:
    source.value.unitclassflag
      = unit_class_flag_id_by_name(value, fc_strcasecmp);
    if (unit_class_flag_id_is_valid(source.value.unitclassflag)) {
      return source;
    }
    break;
  case VUT_OTYPE:
    source.value.outputtype = output_type_by_identifier(value);
    if (source.value.outputtype != O_LAST) {
      return source;
    }
    break;
  case VUT_SPECIALIST:
    source.value.specialist = specialist_by_rule_name(value);
    if (source.value.specialist) {
      return source;
    }
  case VUT_MINSIZE:
    source.value.minsize = atoi(value);
    if (source.value.minsize > 0) {
      return source;
    }
    break;
  case VUT_AI_LEVEL:
    source.value.ai_level = ai_level_by_name(value);
    if (source.value.ai_level != AI_LEVEL_LAST) {
      return source;
    }
    break;
  case VUT_TERRAINCLASS:
    source.value.terrainclass
      = terrain_class_by_name(value, fc_strcasecmp);
    if (terrain_class_is_valid(source.value.terrainclass)) {
      return source;
    }
    break;
  case VUT_BASE:
    source.value.base = base_type_by_rule_name(value);
    if (source.value.base != NULL) {
      return source;
    }
    break;
  case VUT_ROAD:
    source.value.road = road_type_by_rule_name(value);
    if (source.value.road != NULL) {
      return source;
    }
    break;
  case VUT_MINYEAR:
    source.value.minyear = atoi(value);
    return source;
  case VUT_TERRAINALTER:
    source.value.terrainalter
      = terrain_alteration_by_name(value, fc_strcasecmp);
    if (terrain_alteration_is_valid(source.value.terrainalter)) {
      return source;
    }
    break;
  case VUT_CITYTILE:
    source.value.citytile = citytile_by_rule_name(value);
    if (source.value.citytile != CITYT_LAST) {
      return source;
    }
    break;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  source.kind = universals_n_invalid();
  return source;
}

/**************************************************************************
  Combine values into a universal structure.  This is for serialization
  and is the opposite of universal_extraction().
  FIXME: ensure that every caller checks error return!
**************************************************************************/
struct universal universal_by_number(const enum universals_n kind,
				     const int value)
{
  struct universal source;

  source.kind = kind;

  switch (source.kind) {
  case VUT_NONE:
    return source;
  case VUT_ADVANCE:
    source.value.advance = advance_by_number(value);
    if (source.value.advance != NULL) {
      return source;
    }
    break;
  case VUT_GOVERNMENT:
    source.value.govern = government_by_number(value);
    if (source.value.govern != NULL) {
      return source;
    }
    break;
  case VUT_IMPROVEMENT:
    source.value.building = improvement_by_number(value);
    if (source.value.building != NULL) {
      return source;
    }
    break;
  case VUT_SPECIAL:
    source.value.special = value;
    return source;
  case VUT_TERRAIN:
    source.value.terrain = terrain_by_number(value);
    if (source.value.terrain != NULL) {
      return source;
    }
    break;
  case VUT_TERRFLAG:
    source.value.terrainflag = value;
    return source;
  case VUT_RESOURCE:
    source.value.resource = resource_by_number(value);
    if (source.value.resource != NULL) {
      return source;
    }
    break;
  case VUT_NATION:
    source.value.nation = nation_by_number(value);
    if (source.value.nation != NULL) {
      return source;
    }
    break;
  case VUT_UTYPE:
    source.value.utype = utype_by_number(value);
    if (source.value.utype != NULL) {
      return source;
    }
    break;
  case VUT_UTFLAG:
    source.value.unitflag = value;
    return source;
  case VUT_UCLASS:
    source.value.uclass = uclass_by_number(value);
    if (source.value.uclass != NULL) {
      return source;
    }
    break;
  case VUT_UCFLAG:
    source.value.unitclassflag = value;
    return source;
  case VUT_OTYPE:
    source.value.outputtype = value;
    return source;
  case VUT_SPECIALIST:
    source.value.specialist = specialist_by_number(value);
    return source;
  case VUT_MINSIZE:
    source.value.minsize = value;
    return source;
  case VUT_AI_LEVEL:
    source.value.ai_level = value;
    return source;
  case VUT_TERRAINCLASS:
    source.value.terrainclass = value;
    return source;
  case VUT_BASE:
    source.value.base = base_by_number(value);
    return source;
  case VUT_ROAD:
    source.value.road = road_by_number(value);
    return source;
  case VUT_MINYEAR:
    source.value.minyear = value;
    return source;
  case VUT_TERRAINALTER:
    source.value.terrainalter = value;
    return source;
  case VUT_CITYTILE:
    source.value.citytile = value;
    return source;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  source.kind = universals_n_invalid();
  return source;
}

/**************************************************************************
  Extract universal structure into its components for serialization;
  the opposite of universal_by_number().
**************************************************************************/
void universal_extraction(const struct universal *source,
			  int *kind, int *value)
{
  *kind = source->kind;
  *value = universal_number(source);
}

/**************************************************************************
  Return the universal number of the constituent.
**************************************************************************/
int universal_number(const struct universal *source)
{
  switch (source->kind) {
  case VUT_NONE:
    return 0;
  case VUT_ADVANCE:
    return advance_number(source->value.advance);
  case VUT_GOVERNMENT:
    return government_number(source->value.govern);
  case VUT_IMPROVEMENT:
    return improvement_number(source->value.building);
  case VUT_SPECIAL:
    return source->value.special;
  case VUT_TERRAIN:
    return terrain_number(source->value.terrain);
  case VUT_TERRFLAG:
    return source->value.terrainflag;
  case VUT_RESOURCE:
    return resource_number(source->value.resource);
  case VUT_NATION:
    return nation_number(source->value.nation);
  case VUT_UTYPE:
    return utype_number(source->value.utype);
  case VUT_UTFLAG:
    return source->value.unitflag;
  case VUT_UCLASS:
    return uclass_number(source->value.uclass);
  case VUT_UCFLAG:
    return source->value.unitclassflag;
  case VUT_OTYPE:
    return source->value.outputtype;
  case VUT_SPECIALIST:
    return specialist_number(source->value.specialist);
  case VUT_MINSIZE:
    return source->value.minsize;
  case VUT_AI_LEVEL:
    return source->value.ai_level;
  case VUT_TERRAINCLASS:
    return source->value.terrainclass;
  case VUT_BASE:
    return base_number(source->value.base);
  case VUT_ROAD:
    return road_number(source->value.road);
  case VUT_MINYEAR:
    return source->value.minyear;
  case VUT_TERRAINALTER:
    return source->value.terrainalter;
  case VUT_CITYTILE:
    return source->value.citytile;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  fc_assert_msg(FALSE, "universal_number(): invalid source kind %d.",
                source->kind);
  return 0;
}

/****************************************************************************
  Parse a requirement type and value string into a requrement structure.
  Returns the invalid element for enum universal_n on error. Passing in a
  NULL type is considered VUT_NONE (not an error).

  Pass this some values like "Building", "Factory".
****************************************************************************/
struct requirement req_from_str(const char *type, const char *range,
				bool survives, bool negated,
				const char *value)
{
  struct requirement req;
  bool invalid = TRUE;

  req.source = universal_by_rule_name(type, value);

  /* Scan the range string to find the range.  If no range is given a
   * default fallback is used rather than giving an error. */
  req.range = req_range_by_name(range, fc_strcasecmp);
  if (!req_range_is_valid(req.range)) {
    switch (req.source.kind) {
    case VUT_NONE:
    case VUT_COUNT:
      break;
    case VUT_IMPROVEMENT:
    case VUT_SPECIAL:
    case VUT_TERRAIN:
    case VUT_TERRFLAG:
    case VUT_RESOURCE:
    case VUT_UTYPE:
    case VUT_UTFLAG:
    case VUT_UCLASS:
    case VUT_UCFLAG:
    case VUT_OTYPE:
    case VUT_SPECIALIST:
    case VUT_TERRAINCLASS:
    case VUT_BASE:
    case VUT_ROAD:
    case VUT_TERRAINALTER:
    case VUT_CITYTILE:
      req.range = REQ_RANGE_LOCAL;
      break;
    case VUT_MINSIZE:
      req.range = REQ_RANGE_CITY;
      break;
    case VUT_GOVERNMENT:
    case VUT_ADVANCE:
    case VUT_NATION:
    case VUT_AI_LEVEL:
      req.range = REQ_RANGE_PLAYER;
      break;
    case VUT_MINYEAR:
      req.range = REQ_RANGE_WORLD;
      break;
    }
  }

  req.survives = survives;
  req.negated = negated;

  /* These checks match what combinations are supported inside
   * is_req_active(). */
  switch (req.source.kind) {
  case VUT_SPECIAL:
  case VUT_TERRAIN:
  case VUT_RESOURCE:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_BASE:
  case VUT_ROAD:
    invalid = (req.range != REQ_RANGE_LOCAL
               && req.range != REQ_RANGE_CADJACENT
	       && req.range != REQ_RANGE_ADJACENT);
    break;
  case VUT_ADVANCE:
    invalid = (req.range < REQ_RANGE_PLAYER);
    break;
  case VUT_GOVERNMENT:
  case VUT_AI_LEVEL:
    invalid = (req.range != REQ_RANGE_PLAYER);
    break;
  case VUT_IMPROVEMENT:
    invalid = ((req.range == REQ_RANGE_WORLD
		&& !is_great_wonder(req.source.value.building))
	       || (req.range > REQ_RANGE_CITY
		   && !is_wonder(req.source.value.building)));
    break;
  case VUT_MINSIZE:
    invalid = (req.range != REQ_RANGE_CITY);
    break;
  case VUT_NATION:
    invalid = (req.range != REQ_RANGE_PLAYER
	       && req.range != REQ_RANGE_WORLD);
    break;
  case VUT_UTYPE:
  case VUT_UTFLAG:
  case VUT_UCLASS:
  case VUT_UCFLAG:
  case VUT_OTYPE:
  case VUT_SPECIALIST:
  case VUT_TERRAINALTER: /* XXX could in principle support C/ADJACENT */
    invalid = (req.range != REQ_RANGE_LOCAL);
    break;
  case VUT_CITYTILE:
    invalid = (req.range != REQ_RANGE_LOCAL
               && req.range != REQ_RANGE_CADJACENT
               && req.range != REQ_RANGE_ADJACENT);
    break;
  case VUT_MINYEAR:
    invalid = (req.range != REQ_RANGE_WORLD);
    break;
  case VUT_NONE:
    invalid = FALSE;
    break;
  case VUT_COUNT:
    break;
  }

  if (invalid) {
    log_error("Invalid requirement %s | %s | %s | %s | %s",
              type, range, survives ? "survives" : "",
              negated ? "negated" : "", value);
    req.source.kind = universals_n_invalid();
  }

  return req;
}

/****************************************************************************
  Set the values of a req from serializable integers.  This is the opposite
  of req_get_values.
****************************************************************************/
struct requirement req_from_values(int type, int range,
				   bool survives, bool negated,
				   int value)
{
  struct requirement req;

  req.source = universal_by_number(type, value);
  req.range = range;
  req.survives = survives;
  req.negated = negated;
  return req;
}

/****************************************************************************
  Return the value of a req as a serializable integer.  This is the opposite
  of req_set_value.
****************************************************************************/
void req_get_values(const struct requirement *req,
		    int *type, int *range,
		    bool *survives, bool *negated,
		    int *value)
{
  universal_extraction(&req->source, type, value);
  *range = req->range;
  *survives = req->survives;
  *negated = req->negated;
}

/****************************************************************************
  Returns TRUE if req1 and req2 are equal.
****************************************************************************/
bool are_requirements_equal(const struct requirement *req1,
			    const struct requirement *req2)
{
  return (are_universals_equal(&req1->source, &req2->source)
	  && req1->range == req2->range
	  && req1->survives == req2->survives
	  && req1->negated == req2->negated);
}

/****************************************************************************
  Returns the number of total world buildings (this includes buildings
  that have been destroyed).
****************************************************************************/
static int num_world_buildings_total(const struct impr_type *building)
{
  if (is_great_wonder(building)) {
    return (great_wonder_is_built(building)
            || great_wonder_is_destroyed(building) ? 1 : 0);
  } else {
    /* TRANS: Obscure ruleset error. */
    log_error(_("World-ranged requirements are only supported "
                "for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type in the world.
****************************************************************************/
static int num_world_buildings(const struct impr_type *building)
{
  if (is_great_wonder(building)) {
    return (great_wonder_is_built(building) ? 1 : 0);
  } else {
    /* TRANS: Obscure ruleset error. */
    log_error(_("World-ranged requirements are only supported "
                "for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns whether a building of a certain type has ever been built by
  pplayer, even if it has subsequently been destroyed.

  Note: the implementation of this is no different in principle from
  num_world_buildings_total(), but the semantics are different because
  unlike a great wonder, a small wonder could be destroyed and rebuilt
  many times, requiring return of values >1, but there's no record kept
  to support that. Fortunately, the only current caller doesn't need the
  exact number.
****************************************************************************/
static bool player_has_ever_built(const struct player *pplayer,
                                  const struct impr_type *building)
{
  if (is_wonder(building)) {
    return (wonder_is_built(pplayer, building)
            || wonder_is_lost(pplayer, building) ? TRUE : FALSE);
  } else {
    /* TRANS: Obscure ruleset error. */
    log_error(_("Player-ranged requirements are only supported "
                "for wonders."));
    return FALSE;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type owned by plr.
****************************************************************************/
static int num_player_buildings(const struct player *pplayer,
				const struct impr_type *building)
{
  if (is_wonder(building)) {
    return (wonder_is_built(pplayer, building) ? 1 : 0);
  } else {
    /* TRANS: Obscure ruleset error. */
    log_error(_("Player-ranged requirements are only supported "
                "for wonders."));
    return 0;
  }
}

/****************************************************************************
  Returns the number of buildings of a certain type on a continent.
****************************************************************************/
static int num_continent_buildings(const struct player *pplayer,
				   int continent,
				   const struct impr_type *building)
{
  if (is_wonder(building)) {
    const struct city *pcity;

    pcity = city_from_wonder(pplayer, building);
    if (pcity && pcity->tile && tile_continent(pcity->tile) == continent) {
      return 1;
    }
  } else {
    /* TRANS: Obscure ruleset error. */
    log_error(_("Island-ranged requirements are only supported "
                "for wonders."));
  }
  return 0;
}

/****************************************************************************
  Returns the number of buildings of a certain type in a city.
****************************************************************************/
static int num_city_buildings(const struct city *pcity,
			      const struct impr_type *building)
{
  return (city_has_building(pcity, building) ? 1 : 0);
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
static int count_buildings_in_range(const struct player *target_player,
				    const struct city *target_city,
				    const struct impr_type *target_building,
				    enum req_range range,
				    bool survives,
				    const struct impr_type *source)
{
  if (improvement_obsolete(target_player, source)) {
    return 0;
  }

  if (survives) {
    if (range == REQ_RANGE_WORLD) {
      return num_world_buildings_total(source);
    } else if (range == REQ_RANGE_PLAYER) {
      return player_has_ever_built(target_player, source);
    } else {
      /* There is no sources cache for this. */
      /* TRANS: Obscure ruleset error. */
      log_error(_("Surviving requirements are only supported "
                  "at world and player ranges."));
      return 0;
    }
  }

  switch (range) {
  case REQ_RANGE_WORLD:
    return num_world_buildings(source);
  case REQ_RANGE_PLAYER:
    return target_player ? num_player_buildings(target_player, source) : 0;
  case REQ_RANGE_CONTINENT:
    if (target_player && target_city) {
      int continent = tile_continent(target_city->tile);

      return num_continent_buildings(target_player, continent, source);
    } else {
      /* At present, "Continent" effects can affect only
       * cities and units in cities. */
      return 0;
    }
  case REQ_RANGE_CITY:
    return target_city ? num_city_buildings(target_city, source) : 0;
  case REQ_RANGE_LOCAL:
    if (target_building && target_building == source) {
      return num_city_buildings(target_city, source);
    } else {
      /* TODO: other local targets */
      return 0;
    }
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    return 0;
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return 0;
}

/****************************************************************************
  Is there a source tech within range of the target?
****************************************************************************/
static bool is_tech_in_range(const struct player *target_player,
                             enum req_range range,
                             Tech_type_id tech,
                             enum req_problem_type prob_type)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
    /* If target_player is NULL and prob_type RPT_POSSIBLE, then it will
     * consider the advance is in range. */
    if (NULL != target_player) {
      return TECH_KNOWN == player_invention_state(target_player, tech);
    } else {
      return RPT_POSSIBLE == prob_type;
    }
  case REQ_RANGE_WORLD:
    return game.info.global_advances[tech];
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a source special within range of the target?
****************************************************************************/
static bool is_special_in_range(const struct tile *target_tile,
				enum req_range range, bool survives,
				enum tile_special_type special)

{
  switch (range) {
  case REQ_RANGE_LOCAL:
    return target_tile && tile_has_special(target_tile, special);
  case REQ_RANGE_CADJACENT:
    return target_tile && is_special_card_near(target_tile, special, TRUE);
  case REQ_RANGE_ADJACENT:
    return target_tile && is_special_near_tile(target_tile, special, TRUE);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a source tile within range of the target?
****************************************************************************/
static bool is_terrain_in_range(const struct tile *target_tile,
				enum req_range range, bool survives,
				const struct terrain *pterrain)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain. */
    return pterrain && tile_terrain(target_tile) == pterrain;
  case REQ_RANGE_CADJACENT:
    return pterrain && is_terrain_card_near(target_tile, pterrain, TRUE);
  case REQ_RANGE_ADJACENT:
    return pterrain && is_terrain_near_tile(target_tile, pterrain, TRUE);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a source tile within range of the target?
****************************************************************************/
static bool is_resource_in_range(const struct tile *target_tile,
				 enum req_range range, bool survives,
				 const struct resource *pres)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain. */
    return pres && tile_resource(target_tile) == pres;
  case REQ_RANGE_CADJACENT:
    return pres && is_resource_card_near(target_tile, pres, TRUE);
  case REQ_RANGE_ADJACENT:
    return pres && is_resource_near_tile(target_tile, pres, TRUE);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a source terrain class within range of the target?
****************************************************************************/
static bool is_terrain_class_in_range(const struct tile *target_tile,
                                      enum req_range range, bool survives,
                                      enum terrain_class class)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain of correct class. */
    return (terrain_type_terrain_class(tile_terrain(target_tile)) == class);
  case REQ_RANGE_CADJACENT:
    return is_terrain_class_card_near(target_tile, class);
  case REQ_RANGE_ADJACENT:
    return is_terrain_class_near_tile(target_tile, class);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a terrain with the given flag within range of the target?
****************************************************************************/
static bool is_terrainflag_in_range(const struct tile *target_tile,
                                    enum req_range range, bool survives,
                                    enum terrain_flag_id terrflag)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain with correct flag. */
    return terrain_has_flag(tile_terrain(target_tile), terrflag);
  case REQ_RANGE_CADJACENT:
    return is_terrain_flag_card_near(target_tile, terrflag);
  case REQ_RANGE_ADJACENT:
    return is_terrain_flag_near_tile(target_tile, terrflag);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a source base type within range of the target?
****************************************************************************/
static bool is_base_type_in_range(const struct tile *target_tile,
                                  enum req_range range, bool survives,
                                  struct base_type *pbase)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has base of requested type. */
    return tile_has_base(target_tile, pbase);
  case REQ_RANGE_CADJACENT:
    return is_base_card_near(target_tile, pbase);
  case REQ_RANGE_ADJACENT:
    return is_base_near_tile(target_tile, pbase);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}


/****************************************************************************
  Is there a source road type within range of the target?
****************************************************************************/
static bool is_road_type_in_range(const struct tile *target_tile,
                                  enum req_range range, bool survives,
                                  struct road_type *proad)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has road of requested type. */
    return tile_has_road(target_tile, proad);
  case REQ_RANGE_CADJACENT:
    return is_road_card_near(target_tile, proad);
  case REQ_RANGE_ADJACENT:
    return is_road_near_tile(target_tile, proad);
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a terrain which can support the specified infrastructure
  within range of the target?
****************************************************************************/
static bool is_terrain_alter_possible_in_range(const struct tile *target_tile,
                                           enum req_range range, bool survives,
                                           enum terrain_alteration alteration)
{
  if (!target_tile) {
    return FALSE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    return terrain_can_support_alteration(tile_terrain(target_tile),
                                          alteration);
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT: /* XXX Could in principle support ADJACENT. */
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a nation within range of the target?
****************************************************************************/
static bool is_nation_in_range(const struct player *target_player,
			       enum req_range range, bool survives,
			       const struct nation_type *nation,
                               enum req_problem_type prob_type)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
   if (target_player == NULL) {
      return prob_type == RPT_POSSIBLE;
    }
    return nation_of_player(target_player) == nation;
  case REQ_RANGE_WORLD:
    return (NULL != nation->player
            && (survives || nation->player->is_alive));
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/****************************************************************************
  Is there a unit of the given type within range of the target?
****************************************************************************/
static bool is_unittype_in_range(const struct unit_type *target_unittype,
				 enum req_range range, bool survives,
				 struct unit_type *punittype)
{
  /* If no target_unittype is given, we allow the req to be met.  This is
   * to allow querying of certain effect types (like the presence of city
   * walls) without actually knowing the target unit. */
  return (range == REQ_RANGE_LOCAL
	  && (!target_unittype
	      || target_unittype == punittype));
}

/****************************************************************************
  Is there a unit with the given flag within range of the target?
****************************************************************************/
static bool is_unitflag_in_range(const struct unit_type *target_unittype,
				 enum req_range range, bool survives,
				 enum unit_type_flag_id unitflag,
                                 enum req_problem_type prob_type)
{
  /* If no target_unittype is given, we allow the req to be met.  This is
   * to allow querying of certain effect types (like the presence of city
   * walls) without actually knowing the target unit. */
  if (range != REQ_RANGE_LOCAL) {
    return FALSE;
  }
  if (!target_unittype) {
    /* Unknow means TRUE  for RPT_POSSIBLE
     *              FALSE for RPT_CERTAIN
     */
    return prob_type == RPT_POSSIBLE;
  }

  return utype_has_flag(target_unittype, unitflag);
}

/****************************************************************************
  Is there a unit with the given flag within range of the target?
****************************************************************************/
static bool is_unitclass_in_range(const struct unit_type *target_unittype,
				  enum req_range range, bool survives,
				  struct unit_class *pclass)
{
  /* If no target_unittype is given, we allow the req to be met.  This is
   * to allow querying of certain effect types (like the presence of city
   * walls) without actually knowing the target unit. */
  return (range == REQ_RANGE_LOCAL
	  && (!target_unittype
	      || utype_class(target_unittype) == pclass));
}

/****************************************************************************
  Is there a unit with the given flag within range of the target?
****************************************************************************/
static bool is_unitclassflag_in_range(const struct unit_type *target_unittype,
				      enum req_range range, bool survives,
				      enum unit_class_flag_id ucflag)
{
  /* If no target_unittype is given, we allow the req to be met.  This is
   * to allow querying of certain effect types (like the presence of city
   * walls) without actually knowing the target unit. */
  return (range == REQ_RANGE_LOCAL
	  && (!target_unittype
	      || uclass_has_flag(utype_class(target_unittype), ucflag)));
}

/****************************************************************************
  Is center of given city in tile. If city is NULL, any city will do.
****************************************************************************/
static bool is_city_in_tile(const struct tile *ptile,
			    const struct city *pcity)
{
  if (pcity == NULL) {
    return tile_city(ptile) != NULL;
  } else {
    return is_city_center(pcity, ptile);
  }
}

/****************************************************************************
  Is center of given city in range. If city is NULL, any city will do.
****************************************************************************/
static bool is_citytile_in_range(const struct tile *target_tile,
				 const struct city *target_city,
				 enum req_range range,
				 enum citytile_type citytile)
{
  if (target_tile) {
    if (citytile == CITYT_CENTER) {
      switch (range) {
      case REQ_RANGE_LOCAL:
	return is_city_in_tile(target_tile, target_city);
      case REQ_RANGE_CADJACENT:
        cardinal_adjc_iterate(target_tile, adjc_tile) {
          if (is_city_in_tile(adjc_tile, target_city)) {
            return TRUE;
          }
        } cardinal_adjc_iterate_end;

        return FALSE;
      case REQ_RANGE_ADJACENT:
        adjc_iterate(target_tile, adjc_tile) {
          if (is_city_in_tile(adjc_tile, target_city)) {
            return TRUE;
          }
        } adjc_iterate_end;

        return FALSE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
	break;
      }

      fc_assert_msg(FALSE, "Invalid range %d for citytile.", range);

      return FALSE;
    } else {
      /* Not implemented */
      log_error("is_req_active(): citytile %d not supported.",
		citytile);
      return FALSE;
    }
  } else {
    return FALSE;
  }
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
bool is_req_active(const struct player *target_player,
		   const struct city *target_city,
		   const struct impr_type *target_building,
		   const struct tile *target_tile,
		   const struct unit_type *target_unittype,
		   const struct output_type *target_output,
		   const struct specialist *target_specialist,
		   const struct requirement *req,
                   const enum   req_problem_type prob_type)
{
  bool eval = FALSE;

  /* Note the target may actually not exist.  In particular, effects that
   * have a VUT_SPECIAL, VUT_RESOURCE, or VUT_TERRAIN may often be passed
   * to this function with a city as their target.  In this case the
   * requirement is simply not met. */
  switch (req->source.kind) {
  case VUT_NONE:
    eval = TRUE;
    break;
  case VUT_ADVANCE:
    /* The requirement is filled if the player owns the tech. */
    eval = is_tech_in_range(target_player, req->range,
                            advance_number(req->source.value.advance),
                            prob_type);
    break;
  case VUT_GOVERNMENT:
    /* The requirement is filled if the player is using the government. */
    if (target_player == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = (government_of_player(target_player) == req->source.value.govern);
    }
    break;
  case VUT_IMPROVEMENT:
    /* The requirement is filled if there's at least one of the building
     * in the city.  (This is a slightly nonstandard use of
     * count_sources_in_range.) */
    eval = (count_buildings_in_range(target_player, target_city,
				     target_building,
				     req->range, req->survives,
				     req->source.value.building) > 0);
    break;
  case VUT_SPECIAL:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_special_in_range(target_tile,
                                 req->range, req->survives,
                                 req->source.value.special);
    }
    break;
  case VUT_TERRAIN:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_terrain_in_range(target_tile,
                                 req->range, req->survives,
                                 req->source.value.terrain);
    }
    break;
  case VUT_TERRFLAG:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_terrainflag_in_range(target_tile,
                                     req->range, req->survives,
                                     req->source.value.terrainflag);
    }
    break;
  case VUT_RESOURCE:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_resource_in_range(target_tile,
                                  req->range, req->survives,
                                  req->source.value.resource);
    }
    break;
  case VUT_NATION:
    eval = is_nation_in_range(target_player, req->range, req->survives,
                              req->source.value.nation, prob_type);
    break;
  case VUT_UTYPE:
    if (target_unittype == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_unittype_in_range(target_unittype,
                                  req->range, req->survives,
                                  req->source.value.utype);
    }
    break;
  case VUT_UTFLAG:
    eval = is_unitflag_in_range(target_unittype,
				req->range, req->survives,
				req->source.value.unitflag,
                                prob_type);
    break;
  case VUT_UCLASS:
    if (target_unittype == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_unitclass_in_range(target_unittype,
                                   req->range, req->survives,
                                   req->source.value.uclass);
    }
    break;
  case VUT_UCFLAG:
    if (target_unittype == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_unitclassflag_in_range(target_unittype,
                                       req->range, req->survives,
                                       req->source.value.unitclassflag);
    }
    break;
  case VUT_OTYPE:
    eval = (target_output
	    && target_output->index == req->source.value.outputtype);
    break;
  case VUT_SPECIALIST:
    eval = (target_specialist
	    && target_specialist == req->source.value.specialist);
    break;
  case VUT_MINSIZE:
    if (target_city == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = (city_size_get(target_city) >= req->source.value.minsize);
    }
    break;
  case VUT_AI_LEVEL:
    if (target_player == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = target_player->ai_controlled
        && target_player->ai_common.skill_level == req->source.value.ai_level;
    }
    break;
  case VUT_TERRAINCLASS:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_terrain_class_in_range(target_tile,
                                       req->range, req->survives,
                                       req->source.value.terrainclass);
    }
    break;
  case VUT_BASE:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_base_type_in_range(target_tile,
                                   req->range, req->survives,
                                   req->source.value.base);
    }
    break;
 case VUT_ROAD:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_road_type_in_range(target_tile,
                                   req->range, req->survives,
                                   req->source.value.road);
    }
    break;
  case VUT_MINYEAR:
    eval = game.info.year >= req->source.value.minyear;
    break;
  case VUT_TERRAINALTER:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_terrain_alter_possible_in_range(target_tile,
                                                req->range, req->survives,
                                                req->source.value.terrainalter);
    }
    break;
  case VUT_CITYTILE:
    if (target_tile == NULL) {
      eval = (prob_type == RPT_POSSIBLE);
    } else {
      eval = is_citytile_in_range(target_tile, target_city,
                                  req->range,
                                  req->source.value.citytile);
    }
    break;
  case VUT_COUNT:
    log_error("is_req_active(): invalid source kind %d.", req->source.kind);
    return FALSE;
  }

  if (req->negated) {
    return !eval;
  } else {
    return eval;
  }
}

/****************************************************************************
  Checks the requirement(s) to see if they are active on the given target.

  target gives the type of the target
  (player,city,building,tile) give the exact target

  reqs gives the requirement vector.
  The function returns TRUE only if all requirements are active.

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
****************************************************************************/
bool are_reqs_active(const struct player *target_player,
		     const struct city *target_city,
		     const struct impr_type *target_building,
		     const struct tile *target_tile,
		     const struct unit_type *target_unittype,
		     const struct output_type *target_output,
		     const struct specialist *target_specialist,
		     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type)
{
  requirement_vector_iterate(reqs, preq) {
    if (!is_req_active(target_player, target_city, target_building,
		       target_tile, target_unittype, target_output,
		       target_specialist,
		       preq, prob_type)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;
  return TRUE;
}

/****************************************************************************
  Return TRUE if this is an "unchanging" requirement.  This means that
  if a target can't meet the requirement now, it probably won't ever be able
  to do so later.  This can be used to do requirement filtering when checking
  if a target may "eventually" become available.

  Note this isn't absolute.  Returning TRUE here just means that the
  requirement probably can't be met.  In some cases (particularly terrains)
  it may be wrong.
*****************************************************************************/
bool is_req_unchanging(const struct requirement *req)
{
  switch (req->source.kind) {
  case VUT_NATION:
  case VUT_NONE:
  case VUT_OTYPE:
  case VUT_SPECIALIST:	/* Only so long as it's at local range only */
  case VUT_AI_LEVEL:
  case VUT_CITYTILE:
    return TRUE;
  case VUT_ADVANCE:
  case VUT_GOVERNMENT:
  case VUT_IMPROVEMENT:
  case VUT_MINSIZE:
  case VUT_UTYPE:	/* Not sure about this one */
  case VUT_UTFLAG:	/* Not sure about this one */
  case VUT_UCLASS:	/* Not sure about this one */
  case VUT_UCFLAG:	/* Not sure about this one */
  case VUT_ROAD:
    return FALSE;
  case VUT_SPECIAL:
  case VUT_TERRAIN:
  case VUT_RESOURCE:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_TERRAINALTER:
  case VUT_BASE:
    /* Terrains, specials and bases aren't really unchanging; in fact they're
     * practically guaranteed to change.  We return TRUE here for historical
     * reasons and so that the AI doesn't get confused (since the AI
     * doesn't know how to meet special and terrain requirements). */
    return TRUE;
  case VUT_MINYEAR:
    /* Once year is reached, it does not change again */
    return req->source.value.minyear > game.info.year;
  case VUT_COUNT:
    break;
  }
  fc_assert_msg(FALSE, "Invalid source kind %d.", req->source.kind);
  return TRUE;
}

/****************************************************************************
  Return TRUE iff the two sources are equivalent.  Note this isn't the
  same as an == or memcmp check.
*****************************************************************************/
bool are_universals_equal(const struct universal *psource1,
			  const struct universal *psource2)
{
  if (psource1->kind != psource2->kind) {
    return FALSE;
  }
  switch (psource1->kind) {
  case VUT_NONE:
    return TRUE;
  case VUT_ADVANCE:
    return psource1->value.advance == psource2->value.advance;
  case VUT_GOVERNMENT:
    return psource1->value.govern == psource2->value.govern;
  case VUT_IMPROVEMENT:
    return psource1->value.building == psource2->value.building;
  case VUT_SPECIAL:
    return psource1->value.special == psource2->value.special;
  case VUT_TERRAIN:
    return psource1->value.terrain == psource2->value.terrain;
  case VUT_TERRFLAG:
    return psource1->value.terrainflag == psource2->value.terrainflag;
  case VUT_RESOURCE:
    return psource1->value.resource == psource2->value.resource;
  case VUT_NATION:
    return psource1->value.nation == psource2->value.nation;
  case VUT_UTYPE:
    return psource1->value.utype == psource2->value.utype;
  case VUT_UTFLAG:
    return psource1->value.unitflag == psource2->value.unitflag;
  case VUT_UCLASS:
    return psource1->value.uclass == psource2->value.uclass;
  case VUT_UCFLAG:
    return psource1->value.unitclassflag == psource2->value.unitclassflag;
  case VUT_OTYPE:
    return psource1->value.outputtype == psource2->value.outputtype;
  case VUT_SPECIALIST:
    return psource1->value.specialist == psource2->value.specialist;
  case VUT_MINSIZE:
    return psource1->value.minsize == psource2->value.minsize;
  case VUT_AI_LEVEL:
    return psource1->value.ai_level == psource2->value.ai_level;
  case VUT_TERRAINCLASS:
    return psource1->value.terrainclass == psource2->value.terrainclass;
  case VUT_BASE:
    return psource1->value.base == psource2->value.base;
  case VUT_ROAD:
    return psource1->value.road == psource2->value.road;
  case VUT_MINYEAR:
    return psource1->value.minyear == psource2->value.minyear;
  case VUT_TERRAINALTER:
    return psource1->value.terrainalter == psource2->value.terrainalter;
  case VUT_CITYTILE:
    return psource1->value.citytile == psource2->value.citytile;
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource1->kind);
  return FALSE;
}

/****************************************************************************
  Return the (untranslated) rule name of the universal.
  You don't have to free the return pointer.
*****************************************************************************/
const char *universal_rule_name(const struct universal *psource)
{
  static char buffer[10];

  switch (psource->kind) {
  case VUT_NONE:
    return "(none)";
  case VUT_CITYTILE:
    if (psource->value.citytile == CITYT_CENTER) {
      return "Center";
    } else {
      return "(none)";
    }
  case VUT_MINYEAR:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minyear);

    return buffer;
  case VUT_ADVANCE:
    return advance_rule_name(psource->value.advance);
  case VUT_GOVERNMENT:
    return government_rule_name(psource->value.govern);
  case VUT_IMPROVEMENT:
    return improvement_rule_name(psource->value.building);
  case VUT_SPECIAL:
    return special_rule_name(psource->value.special);
  case VUT_TERRAIN:
    return terrain_rule_name(psource->value.terrain);
  case VUT_TERRFLAG:
    return terrain_flag_id_name(psource->value.terrainflag);
  case VUT_RESOURCE:
    return resource_rule_name(psource->value.resource);
  case VUT_NATION:
    return nation_rule_name(psource->value.nation);
  case VUT_UTYPE:
    return utype_rule_name(psource->value.utype);
  case VUT_UTFLAG:
    return unit_type_flag_id_name(psource->value.unitflag);
  case VUT_UCLASS:
    return uclass_rule_name(psource->value.uclass);
  case VUT_UCFLAG:
    return unit_class_flag_id_name(psource->value.unitclassflag);
  case VUT_OTYPE:
    return get_output_name(psource->value.outputtype);
  case VUT_SPECIALIST:
    return specialist_rule_name(psource->value.specialist);
  case VUT_MINSIZE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minsize);

    return buffer;
  case VUT_AI_LEVEL:
    return ai_level_name(psource->value.ai_level);
  case VUT_TERRAINCLASS:
    return terrain_class_name(psource->value.terrainclass);
  case VUT_BASE:
    return base_rule_name(psource->value.base);
  case VUT_ROAD:
    return road_rule_name(psource->value.road);
  case VUT_TERRAINALTER:
    return terrain_alteration_name(psource->value.terrainalter);
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource->kind);
  return NULL;
}

/****************************************************************************
  Make user-friendly text for the source.  The text is put into a user
  buffer which is also returned.
*****************************************************************************/
const char *universal_name_translation(const struct universal *psource,
				       char *buf, size_t bufsz)
{
  buf[0] = '\0'; /* to be safe. */
  switch (psource->kind) {
  case VUT_NONE:
    /* TRANS: missing value */
    fc_strlcat(buf, _("(none)"), bufsz);
    return buf;
  case VUT_ADVANCE:
    fc_strlcat(buf, advance_name_translation(psource->value.advance), bufsz);
    return buf;
  case VUT_GOVERNMENT:
    fc_strlcat(buf, government_name_translation(psource->value.govern),
               bufsz);
    return buf;
  case VUT_IMPROVEMENT:
    fc_strlcat(buf, improvement_name_translation(psource->value.building),
               bufsz);
    return buf;
  case VUT_SPECIAL:
    fc_strlcat(buf, special_name_translation(psource->value.special), bufsz);
    return buf;
  case VUT_TERRAIN:
    fc_strlcat(buf, terrain_name_translation(psource->value.terrain), bufsz);
    return buf;
  case VUT_RESOURCE:
    fc_strlcat(buf, resource_name_translation(psource->value.resource), bufsz);
    return buf;
  case VUT_NATION:
    fc_strlcat(buf, nation_adjective_translation(psource->value.nation),
               bufsz);
    return buf;
  case VUT_UTYPE:
    fc_strlcat(buf, utype_name_translation(psource->value.utype), bufsz);
    return buf;
  case VUT_UTFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" units"),
                 /* flag names are never translated */
                 unit_type_flag_id_name(psource->value.unitflag));
    return buf;
  case VUT_UCLASS:
    cat_snprintf(buf, bufsz, _("%s units"),
		 uclass_name_translation(psource->value.uclass));
    return buf;
  case VUT_UCFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" units"),
                 /* flag names are never translated */
                 unit_class_flag_id_name(psource->value.unitclassflag));
    return buf;
  case VUT_OTYPE:
    /* FIXME */
    fc_strlcat(buf, get_output_name(psource->value.outputtype), bufsz);
    return buf;
  case VUT_SPECIALIST:
    fc_strlcat(buf, specialist_plural_translation(psource->value.specialist),
               bufsz);
    return buf;
  case VUT_MINSIZE:
    cat_snprintf(buf, bufsz, _("Size %d"),
		 psource->value.minsize);
    return buf;
  case VUT_AI_LEVEL:
    /* TRANS: "Hard AI" */
    cat_snprintf(buf, bufsz, _("%s AI"),
                 ai_level_name(psource->value.ai_level)); /* FIXME */
    return buf;
  case VUT_TERRAINCLASS:
    /* TRANS: "Land terrain" */
    cat_snprintf(buf, bufsz, _("%s terrain"),
                 terrain_class_name_translation(psource->value.terrainclass));
    return buf;
  case VUT_TERRFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" terrain"),
                 /* flag names are never translated */
                 terrain_flag_id_name(psource->value.terrainflag));
    return buf;
  case VUT_BASE:
    /* TRANS: "Fortress base" */
    cat_snprintf(buf, bufsz, _("%s base"),
                 base_name_translation(psource->value.base));
    return buf;
  case VUT_ROAD:
    /* TRANS: Road type requirement: "Road" / "Railroad" / "Maglev" ... */
    cat_snprintf(buf, bufsz, Q_("?road:%s"),
                 road_name_translation(psource->value.road));
    return buf;
  case VUT_MINYEAR:
    cat_snprintf(buf, bufsz, _("After %s"),
                 textyear(psource->value.minyear));
    return buf;
  case VUT_TERRAINALTER:
    /* TRANS: "Irrigation possible" */
    cat_snprintf(buf, bufsz, _("%s possible"),
                 terrain_alteration_name_translation(psource->value.terrainalter));
    return buf;
  case VUT_CITYTILE:
    fc_strlcat(buf, _("City center tile"), bufsz);
    return buf;
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource->kind);
  return buf;
}

/****************************************************************************
  Return untranslated name of the universal source name.
*****************************************************************************/
const char *universal_type_rule_name(const struct universal *psource)
{
  return universals_n_name(psource->kind);
}

/**************************************************************************
  Return the number of shields it takes to build this universal.
**************************************************************************/
int universal_build_shield_cost(const struct universal *target)
{
  switch (target->kind) {
  case VUT_IMPROVEMENT:
    return impr_build_shield_cost(target->value.building);
  case VUT_UTYPE:
    return utype_build_shield_cost(target->value.utype);
  default:
    break;
  }
  return FC_INFINITY;
}
