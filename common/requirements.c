/***********************************************************************
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

#include <stdarg.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "achievements.h"
#include "calendar.h"
#include "citizens.h"
#include "culture.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "movement.h"
#include "player.h"
#include "map.h"
#include "research.h"
#include "road.h"
#include "server_settings.h"
#include "specialist.h"
#include "style.h"

#include "requirements.h"

/************************************************************************
  Container for req_item_found functions
************************************************************************/
typedef enum req_item_found (*universal_found)(const struct requirement *,
                                               const struct universal *);
static universal_found universal_found_function[VUT_COUNT] = {NULL};

/**********************************************************************//**
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

  universal_value_from_str(&source, value);

  return source;
}

/**********************************************************************//**
  Returns TRUE iff the specified activity can appear in an "Activity"
  requirement.
**************************************************************************/
static bool activity_is_valid_in_requirement(enum unit_activity act)
{
  return unit_activity_is_valid(act)
      && is_real_activity(act)
      && act != ACTIVITY_SENTRY
      && act != ACTIVITY_GOTO
      && act != ACTIVITY_EXPLORE;
}

/**********************************************************************//**
  Returns TRUE iff the specified universal legally can appear in a
  requirement.
**************************************************************************/
bool universal_is_legal_in_requirement(const struct universal *univ)
{
  if (univ->kind == VUT_ACTIVITY) {
    return activity_is_valid_in_requirement(univ->value.activity);
  }

  return TRUE;
}

/**********************************************************************//**
  Parse requirement value strings into a universal
  structure.
**************************************************************************/
void universal_value_from_str(struct universal *source, const char *value)
{
  /* Finally scan the value string based on the type of the source. */
  switch (source->kind) {
  case VUT_NONE:
    return;
  case VUT_ADVANCE:
    source->value.advance = advance_by_rule_name(value);
    if (source->value.advance != NULL) {
      return;
    }
    break;
  case VUT_TECHFLAG:
    source->value.techflag
      = tech_flag_id_by_name(value, fc_strcasecmp);
    if (tech_flag_id_is_valid(source->value.techflag)) {
      return;
    }
    break;
  case VUT_GOVERNMENT:
    source->value.govern = government_by_rule_name(value);
    if (source->value.govern != NULL) {
      return;
    }
    break;
  case VUT_ACHIEVEMENT:
    source->value.achievement = achievement_by_rule_name(value);
    if (source->value.achievement != NULL) {
      return;
    }
    break;
  case VUT_STYLE:
    source->value.style = style_by_rule_name(value);
    if (source->value.style != NULL) {
      return;
    }
    break;
  case VUT_IMPROVEMENT:
    source->value.building = improvement_by_rule_name(value);
    if (source->value.building != NULL) {
      return;
    }
    break;
  case VUT_IMPR_GENUS:
    source->value.impr_genus = impr_genus_id_by_name(value, fc_strcasecmp);
    if (impr_genus_id_is_valid(source->value.impr_genus)) {
      return;
    }
    break;
  case VUT_EXTRA:
    source->value.extra = extra_type_by_rule_name(value);
    if (source->value.extra != NULL) {
      return;
    }
    break;
  case VUT_GOOD:
    source->value.good = goods_by_rule_name(value);
    if (source->value.good != NULL) {
      return;
    }
    break;
  case VUT_TERRAIN:
    source->value.terrain = terrain_by_rule_name(value);
    if (source->value.terrain != T_UNKNOWN) {
      return;
    }
    break;
  case VUT_TERRFLAG:
    source->value.terrainflag
      = terrain_flag_id_by_name(value, fc_strcasecmp);
    if (terrain_flag_id_is_valid(source->value.terrainflag)) {
      return;
    }
    break;
  case VUT_NATION:
    source->value.nation = nation_by_rule_name(value);
    if (source->value.nation != NO_NATION_SELECTED) {
      return;
    }
    break;
  case VUT_NATIONGROUP:
    source->value.nationgroup = nation_group_by_rule_name(value);
    if (source->value.nationgroup != NULL) {
      return;
    }
    break;
  case VUT_NATIONALITY:
    source->value.nationality = nation_by_rule_name(value);
    if (source->value.nationality != NO_NATION_SELECTED) {
      return;
    }
    break;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    source->value.diplrel = diplrel_by_rule_name(value);
    if (source->value.diplrel != diplrel_other_invalid()) {
      return;
    }
    break;
  case VUT_UTYPE:
    source->value.utype = unit_type_by_rule_name(value);
    if (source->value.utype) {
      return;
    }
    break;
  case VUT_UTFLAG:
    source->value.unitflag = unit_type_flag_id_by_name(value, fc_strcasecmp);
    if (unit_type_flag_id_is_valid(source->value.unitflag)) {
      return;
    }
    break;
  case VUT_UCLASS:
    source->value.uclass = unit_class_by_rule_name(value);
    if (source->value.uclass) {
      return;
    }
    break;
  case VUT_UCFLAG:
    source->value.unitclassflag
      = unit_class_flag_id_by_name(value, fc_strcasecmp);
    if (unit_class_flag_id_is_valid(source->value.unitclassflag)) {
      return;
    }
    break;
  case VUT_MINVETERAN:
    source->value.minveteran = atoi(value);
    if (source->value.minveteran > 0) {
      return;
    }
    break;
  case VUT_UNITSTATE:
    source->value.unit_state = ustate_prop_by_name(value, fc_strcasecmp);
    if (ustate_prop_is_valid(source->value.unit_state)) {
      return;
    }
    break;
  case VUT_ACTIVITY:
    source->value.activity = unit_activity_by_name(value, fc_strcasecmp);
    if (activity_is_valid_in_requirement(source->value.activity)) {
      return;
    }
    break;
  case VUT_MINMOVES:
    source->value.minmoves = atoi(value);
    if (source->value.minmoves > 0) {
      return;
    }
    break;
  case VUT_MINHP:
    source->value.min_hit_points = atoi(value);
    if (source->value.min_hit_points > 0) {
      return;
    }
    break;
  case VUT_AGE:
    source->value.age = atoi(value);
    if (source->value.age > 0) {
      return;
    }
    break;
  case VUT_MINTECHS:
    source->value.min_techs = atoi(value);
    if (source->value.min_techs > 0) {
      return;
    }
    break;
  case VUT_ACTION:
    source->value.action = action_by_rule_name(value);
    if (source->value.action != NULL) {
      return;
    }
    break;
  case VUT_OTYPE:
    source->value.outputtype = output_type_by_identifier(value);
    if (source->value.outputtype != O_LAST) {
      return;
    }
    break;
  case VUT_SPECIALIST:
    source->value.specialist = specialist_by_rule_name(value);
    if (source->value.specialist) {
      return;
    }
    break;
  case VUT_MINSIZE:
    source->value.minsize = atoi(value);
    if (source->value.minsize > 0) {
      return;
    }
    break;
  case VUT_MINCULTURE:
    source->value.minculture = atoi(value);
    if (source->value.minculture > 0) {
      return;
    }
    break;
  case VUT_MINFOREIGNPCT:
    source->value.minforeignpct = atoi(value);
    if (source->value.minforeignpct > 0) {
      return;
    }
    break;
  case VUT_AI_LEVEL:
    source->value.ai_level = ai_level_by_name(value, fc_strcasecmp);
    if (ai_level_is_valid(source->value.ai_level)) {
      return;
    }
    break;
  case VUT_MAXTILEUNITS:
    source->value.max_tile_units = atoi(value);
    if (0 <= source->value.max_tile_units) {
      return;
    }
    break;
  case VUT_TERRAINCLASS:
    source->value.terrainclass
      = terrain_class_by_name(value, fc_strcasecmp);
    if (terrain_class_is_valid(source->value.terrainclass)) {
      return;
    }
    break;
  case VUT_ROADFLAG:
    source->value.roadflag = road_flag_id_by_name(value, fc_strcasecmp);
    if (road_flag_id_is_valid(source->value.roadflag)) {
      return;
    }
    break;
  case VUT_EXTRAFLAG:
    source->value.extraflag = extra_flag_id_by_name(value, fc_strcasecmp);
    if (extra_flag_id_is_valid(source->value.extraflag)) {
      return;
    }
    break;
  case VUT_MINYEAR:
    source->value.minyear = atoi(value);
    return;
  case VUT_MINCALFRAG:
    /* Rule names are 0-based numbers, not pretty names from ruleset */
    source->value.mincalfrag = atoi(value);
    if (source->value.mincalfrag >= 0) {
      /* More range checking done later, in sanity_check_req_individual() */
      return;
    }
    break;
  case VUT_TOPO:
    source->value.topo_property = topo_flag_by_name(value, fc_strcasecmp);
    if (topo_flag_is_valid(source->value.topo_property)) {
      return;
    }
    break;
  case VUT_SERVERSETTING:
    source->value.ssetval = ssetv_by_rule_name(value);
    if (source->value.ssetval != SSETV_NONE) {
      return;
    }
    break;
  case VUT_TERRAINALTER:
    source->value.terrainalter
      = terrain_alteration_by_name(value, fc_strcasecmp);
    if (terrain_alteration_is_valid(source->value.terrainalter)) {
      return;
    }
    break;
  case VUT_CITYTILE:
    source->value.citytile = citytile_type_by_name(value, fc_strcasecmp);
    if (source->value.citytile != CITYT_LAST) {
      return;
    }
    break;
  case VUT_CITYSTATUS:
    source->value.citystatus = citystatus_type_by_name(value, fc_strcasecmp);
    if (source->value.citystatus != CITYS_LAST) {
      return;
    }
    break;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  source->kind = universals_n_invalid();
}

/**********************************************************************//**
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
    /* Avoid compiler warning about unitialized source.value */
    source.value.advance = NULL;

    return source;
  case VUT_ADVANCE:
    source.value.advance = advance_by_number(value);
    if (source.value.advance != NULL) {
      return source;
    }
    break;
 case VUT_TECHFLAG:
    source.value.techflag = value;
    return source;
  case VUT_GOVERNMENT:
    source.value.govern = government_by_number(value);
    if (source.value.govern != NULL) {
      return source;
    }
    break;
  case VUT_ACHIEVEMENT:
    source.value.achievement = achievement_by_number(value);
    if (source.value.achievement != NULL) {
      return source;
    }
    break;
  case VUT_STYLE:
    source.value.style = style_by_number(value);
    if (source.value.style != NULL) {
      return source;
    }
    break;
  case VUT_IMPROVEMENT:
    source.value.building = improvement_by_number(value);
    if (source.value.building != NULL) {
      return source;
    }
    break;
  case VUT_IMPR_GENUS:
    source.value.impr_genus = value;
    return source;
  case VUT_EXTRA:
    source.value.extra = extra_by_number(value);
    return source;
  case VUT_GOOD:
    source.value.good = goods_by_number(value);
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
  case VUT_NATION:
    source.value.nation = nation_by_number(value);
    if (source.value.nation != NULL) {
      return source;
    }
    break;
  case VUT_NATIONGROUP:
    source.value.nationgroup = nation_group_by_number(value);
    if (source.value.nationgroup != NULL) {
      return source;
    }
    break;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    source.value.diplrel = value;
    if (source.value.diplrel != diplrel_other_invalid()) {
      return source;
    }
    break;
  case VUT_NATIONALITY:
    source.value.nationality = nation_by_number(value);
    if (source.value.nationality != NULL) {
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
  case VUT_MINVETERAN:
    source.value.minveteran = value;
    return source;
  case VUT_UNITSTATE:
    source.value.unit_state = value;
    return source;
  case VUT_ACTIVITY:
    source.value.activity = value;
    return source;
  case VUT_MINMOVES:
    source.value.minmoves = value;
    return source;
  case VUT_MINHP:
    source.value.min_hit_points = value;
    return source;
  case VUT_AGE:
    source.value.age = value;
    return source;
  case VUT_MINTECHS:
    source.value.min_techs = value;
    return source;
  case VUT_ACTION:
    source.value.action = action_by_number(value);
    if (source.value.action != NULL) {
      return source;
    }
    break;
  case VUT_OTYPE:
    source.value.outputtype = value;
    return source;
  case VUT_SPECIALIST:
    source.value.specialist = specialist_by_number(value);
    return source;
  case VUT_MINSIZE:
    source.value.minsize = value;
    return source;
  case VUT_MINCULTURE:
    source.value.minculture = value;
    return source;
  case VUT_MINFOREIGNPCT:
    source.value.minforeignpct = value;
    return source;
  case VUT_AI_LEVEL:
    source.value.ai_level = value;
    return source;
  case VUT_MAXTILEUNITS:
    source.value.max_tile_units = value;
    return source;
  case VUT_TERRAINCLASS:
    source.value.terrainclass = value;
    return source;
  case VUT_ROADFLAG:
    source.value.roadflag = value;
    return source;
  case VUT_EXTRAFLAG:
    source.value.extraflag = value;
    return source;
  case VUT_MINYEAR:
    source.value.minyear = value;
    return source;
  case VUT_MINCALFRAG:
    source.value.mincalfrag = value;
    return source;
  case VUT_TOPO:
    source.value.topo_property = value;
    return source;
  case VUT_SERVERSETTING:
    source.value.ssetval = value;
    return source;
  case VUT_TERRAINALTER:
    source.value.terrainalter = value;
    return source;
  case VUT_CITYTILE:
    source.value.citytile = value;
    return source;
  case VUT_CITYSTATUS:
    source.value.citystatus = value;
    return source;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  source.kind = universals_n_invalid();
  /* Avoid compiler warning about unitialized source.value */
  source.value.advance = NULL;

  return source;
}

/**********************************************************************//**
  Extract universal structure into its components for serialization;
  the opposite of universal_by_number().
**************************************************************************/
void universal_extraction(const struct universal *source,
			  int *kind, int *value)
{
  *kind = source->kind;
  *value = universal_number(source);
}

/**********************************************************************//**
  Return the universal number of the constituent.
**************************************************************************/
int universal_number(const struct universal *source)
{
  switch (source->kind) {
  case VUT_NONE:
    return 0;
  case VUT_ADVANCE:
    return advance_number(source->value.advance);
  case VUT_TECHFLAG:
    return source->value.techflag;
  case VUT_GOVERNMENT:
    return government_number(source->value.govern);
  case VUT_ACHIEVEMENT:
    return achievement_number(source->value.achievement);
  case VUT_STYLE:
    return style_number(source->value.style);
  case VUT_IMPROVEMENT:
    return improvement_number(source->value.building);
  case VUT_IMPR_GENUS:
    return source->value.impr_genus;
  case VUT_EXTRA:
    return extra_number(source->value.extra);
  case VUT_GOOD:
    return goods_number(source->value.good);
  case VUT_TERRAIN:
    return terrain_number(source->value.terrain);
  case VUT_TERRFLAG:
    return source->value.terrainflag;
  case VUT_NATION:
    return nation_number(source->value.nation);
  case VUT_NATIONGROUP:
    return nation_group_number(source->value.nationgroup);
  case VUT_NATIONALITY:
    return nation_number(source->value.nationality);
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    return source->value.diplrel;
  case VUT_UTYPE:
    return utype_number(source->value.utype);
  case VUT_UTFLAG:
    return source->value.unitflag;
  case VUT_UCLASS:
    return uclass_number(source->value.uclass);
  case VUT_UCFLAG:
    return source->value.unitclassflag;
  case VUT_MINVETERAN:
    return source->value.minveteran;
  case VUT_UNITSTATE:
    return source->value.unit_state;
  case VUT_ACTIVITY:
    return source->value.activity;
  case VUT_MINMOVES:
    return source->value.minmoves;
  case VUT_MINHP:
    return source->value.min_hit_points;
  case VUT_AGE:
    return source->value.age;
  case VUT_MINTECHS:
    return source->value.min_techs;
  case VUT_ACTION:
    return action_number(source->value.action);
  case VUT_OTYPE:
    return source->value.outputtype;
  case VUT_SPECIALIST:
    return specialist_number(source->value.specialist);
  case VUT_MINSIZE:
    return source->value.minsize;
  case VUT_MINCULTURE:
    return source->value.minculture;
  case VUT_MINFOREIGNPCT:
    return source->value.minforeignpct;
  case VUT_AI_LEVEL:
    return source->value.ai_level;
  case VUT_MAXTILEUNITS:
    return source->value.max_tile_units;
  case VUT_TERRAINCLASS:
    return source->value.terrainclass;
   case VUT_ROADFLAG:
    return source->value.roadflag;
  case VUT_EXTRAFLAG:
    return source->value.extraflag;
  case VUT_MINYEAR:
    return source->value.minyear;
  case VUT_MINCALFRAG:
    return source->value.mincalfrag;
  case VUT_TOPO:
    return source->value.topo_property;
  case VUT_SERVERSETTING:
    return source->value.ssetval;
  case VUT_TERRAINALTER:
    return source->value.terrainalter;
  case VUT_CITYTILE:
    return source->value.citytile;
  case VUT_CITYSTATUS:
    return source->value.citystatus;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  fc_assert_msg(FALSE, "universal_number(): invalid source kind %d.",
                source->kind);
  return 0;
}


/**********************************************************************//**
  Returns a pointer to a statically-allocated, empty requirement context.
**************************************************************************/
const struct req_context *req_context_empty(void)
{
  static const struct req_context empty = {};
  return &empty;
}


/**********************************************************************//**
  Returns the given requirement as a formatted string ready for printing.
  Does not care about the 'quiet' property.
**************************************************************************/
const char *req_to_fstring(const struct requirement *req)
{
  struct astring printable_req = ASTRING_INIT;

  astr_set(&printable_req, "%s%s %s %s%s",
           req->survives ? "surviving " : "",
           req_range_name(req->range),
           universal_type_rule_name(&req->source),
           req->present ? "" : "!",
           universal_rule_name(&req->source));

  return astr_str(&printable_req);
}

/**********************************************************************//**
  Parse a requirement type and value string into a requirement structure.
  Returns the invalid element for enum universal_n on error. Passing in a
  NULL type is considered VUT_NONE (not an error).

  Pass this some values like "Building", "Factory".
**************************************************************************/
struct requirement req_from_str(const char *type, const char *range,
                                bool survives, bool present, bool quiet,
                                const char *value)
{
  struct requirement req;
  bool invalid;
  const char *error = NULL;

  req.source = universal_by_rule_name(type, value);

  invalid = !universals_n_is_valid(req.source.kind);
  if (invalid) {
    error = "bad type or name";
  } else {
    /* Scan the range string to find the range.  If no range is given a
     * default fallback is used rather than giving an error. */
    req.range = req_range_by_name(range, fc_strcasecmp);
    if (!req_range_is_valid(req.range)) {
      switch (req.source.kind) {
      case VUT_NONE:
      case VUT_COUNT:
        break;
      case VUT_IMPROVEMENT:
      case VUT_IMPR_GENUS:
      case VUT_EXTRA:
      case VUT_TERRAIN:
      case VUT_TERRFLAG:
      case VUT_UTYPE:
      case VUT_UTFLAG:
      case VUT_UCLASS:
      case VUT_UCFLAG:
      case VUT_MINVETERAN:
      case VUT_UNITSTATE:
      case VUT_ACTIVITY:
      case VUT_MINMOVES:
      case VUT_MINHP:
      case VUT_AGE:
      case VUT_ACTION:
      case VUT_OTYPE:
      case VUT_SPECIALIST:
      case VUT_TERRAINCLASS:
      case VUT_ROADFLAG:
      case VUT_EXTRAFLAG:
      case VUT_TERRAINALTER:
      case VUT_CITYTILE:
      case VUT_MAXTILEUNITS:
      case VUT_DIPLREL_TILE_O:
      case VUT_DIPLREL_UNITANY_O:
        req.range = REQ_RANGE_LOCAL;
        break;
      case VUT_MINSIZE:
      case VUT_MINCULTURE:
      case VUT_MINFOREIGNPCT:
      case VUT_NATIONALITY:
      case VUT_CITYSTATUS:
      case VUT_GOOD:
        req.range = REQ_RANGE_CITY;
        break;
      case VUT_GOVERNMENT:
      case VUT_ACHIEVEMENT:
      case VUT_STYLE:
      case VUT_ADVANCE:
      case VUT_TECHFLAG:
      case VUT_NATION:
      case VUT_NATIONGROUP:
      case VUT_DIPLREL:
      case VUT_DIPLREL_TILE:
      case VUT_DIPLREL_UNITANY:
      case VUT_AI_LEVEL:
        req.range = REQ_RANGE_PLAYER;
        break;
      case VUT_MINYEAR:
      case VUT_MINCALFRAG:
      case VUT_TOPO:
      case VUT_MINTECHS:
      case VUT_SERVERSETTING:
        req.range = REQ_RANGE_WORLD;
        break;
      }
    }

    req.survives = survives;
    req.present = present;
    req.quiet = quiet;

    /* These checks match what combinations are supported inside
     * is_req_active(). However, it's only possible to do basic checks,
     * not anything that might depend on the rest of the ruleset which
     * might not have been loaded yet. */
    switch (req.source.kind) {
    case VUT_TERRAIN:
    case VUT_EXTRA:
    case VUT_TERRAINCLASS:
    case VUT_TERRFLAG:
    case VUT_ROADFLAG:
    case VUT_EXTRAFLAG:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT
                 && req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADEROUTE);
      break;
    case VUT_ADVANCE:
    case VUT_TECHFLAG:
    case VUT_ACHIEVEMENT:
    case VUT_MINTECHS:
      invalid = (req.range < REQ_RANGE_PLAYER);
      break;
    case VUT_GOVERNMENT:
    case VUT_AI_LEVEL:
    case VUT_STYLE:
      invalid = (req.range != REQ_RANGE_PLAYER);
      break;
    case VUT_MINSIZE:
    case VUT_MINFOREIGNPCT:
    case VUT_NATIONALITY:
    case VUT_CITYSTATUS:
      invalid = (req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADEROUTE);
      break;
    case VUT_GOOD:
      invalid = (req.range != REQ_RANGE_CITY);
      break;
    case VUT_MINCULTURE:
      invalid = (req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADEROUTE
                 && req.range != REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_TEAM
                 && req.range != REQ_RANGE_ALLIANCE
                 && req.range != REQ_RANGE_WORLD);
      break;
    case VUT_DIPLREL:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_TEAM
                 && req.range != REQ_RANGE_ALLIANCE
                 && req.range != REQ_RANGE_WORLD)
                /* Non local foreign makes no sense. */
                || (req.source.value.diplrel == DRO_FOREIGN
                    && req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_DIPLREL_TILE:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_TEAM
                 && req.range != REQ_RANGE_ALLIANCE)
                /* Non local foreign makes no sense. */
                || (req.source.value.diplrel == DRO_FOREIGN
                    && req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_DIPLREL_TILE_O:
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_DIPLREL_UNITANY:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_TEAM
                 && req.range != REQ_RANGE_ALLIANCE)
                /* Non local foreign makes no sense. */
                || (req.source.value.diplrel == DRO_FOREIGN
                    && req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_DIPLREL_UNITANY_O:
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_NATION:
    case VUT_NATIONGROUP:
      invalid = (req.range != REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_TEAM
                 && req.range != REQ_RANGE_ALLIANCE
                 && req.range != REQ_RANGE_WORLD);
      break;
    case VUT_UTYPE:
    case VUT_UTFLAG:
    case VUT_UCLASS:
    case VUT_UCFLAG:
    case VUT_MINVETERAN:
    case VUT_UNITSTATE:
    case VUT_ACTIVITY:
    case VUT_MINMOVES:
    case VUT_MINHP:
    case VUT_ACTION:
    case VUT_OTYPE:
    case VUT_SPECIALIST:
    case VUT_TERRAINALTER: /* XXX could in principle support C/ADJACENT */
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_CITYTILE:
    case VUT_MAXTILEUNITS:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT);
      break;
    case VUT_MINYEAR:
    case VUT_MINCALFRAG:
    case VUT_TOPO:
    case VUT_SERVERSETTING:
      invalid = (req.range != REQ_RANGE_WORLD);
      break;
    case VUT_AGE:
      /* FIXME: could support TRADEROUTE, TEAM, etc */
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_PLAYER);
      break;
    case VUT_IMPR_GENUS:
      /* TODO: Support other ranges too. */
      invalid = req.range != REQ_RANGE_LOCAL;
      break;
    case VUT_IMPROVEMENT:
      /* Valid ranges depend on the building genus (wonder/improvement),
       * which might not have been loaded from the ruleset yet.
       * So we allow anything here, and do a proper check once ruleset
       * loading is complete, in sanity_check_req_individual(). */
    case VUT_NONE:
      invalid = FALSE;
      break;
    case VUT_COUNT:
      break;
    }
    if (invalid) {
      error = "bad range";
    }
  }

  if (!invalid) {
    /* Check 'survives'. */
    switch (req.source.kind) {
    case VUT_IMPROVEMENT:
      /* See buildings_in_range(). */
      invalid = survives && req.range <= REQ_RANGE_CONTINENT;
      break;
    case VUT_NATION:
    case VUT_ADVANCE:
      invalid = survives && req.range != REQ_RANGE_WORLD;
      break;
    case VUT_IMPR_GENUS:
    case VUT_GOVERNMENT:
    case VUT_TERRAIN:
    case VUT_UTYPE:
    case VUT_UTFLAG:
    case VUT_UCLASS:
    case VUT_UCFLAG:
    case VUT_MINVETERAN:
    case VUT_UNITSTATE:
    case VUT_ACTIVITY:
    case VUT_MINMOVES:
    case VUT_MINHP:
    case VUT_AGE:
    case VUT_ACTION:
    case VUT_OTYPE:
    case VUT_SPECIALIST:
    case VUT_MINSIZE:
    case VUT_MINCULTURE:
    case VUT_MINFOREIGNPCT:
    case VUT_AI_LEVEL:
    case VUT_TERRAINCLASS:
    case VUT_MINYEAR:
    case VUT_MINCALFRAG:
    case VUT_TOPO:
    case VUT_SERVERSETTING:
    case VUT_TERRAINALTER:
    case VUT_CITYTILE:
    case VUT_CITYSTATUS:
    case VUT_TERRFLAG:
    case VUT_NATIONALITY:
    case VUT_ROADFLAG:
    case VUT_EXTRAFLAG:
    case VUT_EXTRA:
    case VUT_GOOD:
    case VUT_TECHFLAG:
    case VUT_ACHIEVEMENT:
    case VUT_NATIONGROUP:
    case VUT_STYLE:
    case VUT_DIPLREL:
    case VUT_DIPLREL_TILE:
    case VUT_DIPLREL_TILE_O:
    case VUT_DIPLREL_UNITANY:
    case VUT_DIPLREL_UNITANY_O:
    case VUT_MAXTILEUNITS:
    case VUT_MINTECHS:
      /* Most requirements don't support 'survives'. */
      invalid = survives;
      break;
    case VUT_NONE:
    case VUT_COUNT:
      break;
    }
    if (invalid) {
      error = "bad 'survives'";
    }
  }

  if (invalid) {
    log_error("Invalid requirement %s | %s | %s | %s | %s: %s",
              type, range, survives ? "survives" : "",
              present ? "present" : "", value, error);
    req.source.kind = universals_n_invalid();
  }

  return req;
}

/**********************************************************************//**
  Set the values of a req from serializable integers.  This is the opposite
  of req_get_values.
**************************************************************************/
struct requirement req_from_values(int type, int range,
				   bool survives, bool present, bool quiet,
				   int value)
{
  struct requirement req;

  req.source = universal_by_number(type, value);
  req.range = range;
  req.survives = survives;
  req.present = present;
  req.quiet = quiet;

  return req;
}

/**********************************************************************//**
  Return the value of a req as a serializable integer.  This is the opposite
  of req_set_value.
**************************************************************************/
void req_get_values(const struct requirement *req,
		    int *type, int *range,
		    bool *survives, bool *present, bool *quiet,
		    int *value)
{
  universal_extraction(&req->source, type, value);
  *range = req->range;
  *survives = req->survives;
  *present = req->present;
  *quiet = req->quiet;
}

/**********************************************************************//**
  Returns TRUE if req1 and req2 are equal.
  Does not care if one is quiet and the other not.
**************************************************************************/
bool are_requirements_equal(const struct requirement *req1,
			    const struct requirement *req2)
{
  return (are_universals_equal(&req1->source, &req2->source)
	  && req1->range == req2->range
	  && req1->survives == req2->survives
	  && req1->present == req2->present);
}

/**********************************************************************//**
  Returns TRUE if req1 and req2 directly negate each other.
**************************************************************************/
static bool are_requirements_opposites(const struct requirement *req1,
                                       const struct requirement *req2)
{
  return (are_universals_equal(&req1->source, &req2->source)
          && req1->range == req2->range
          && req1->survives == req2->survives
          && req1->present != req2->present);
}

/**********************************************************************//**
  Returns TRUE if the specified building requirement contradicts the
  specified building genus requirement.
**************************************************************************/
static bool impr_contra_genus(const struct requirement *impr_req,
                              const struct requirement *genus_req)
{
  /* The input is sane. */
  fc_assert_ret_val(impr_req->source.kind == VUT_IMPROVEMENT, FALSE);
  fc_assert_ret_val(genus_req->source.kind == VUT_IMPR_GENUS, FALSE);

  if (impr_req->range == REQ_RANGE_LOCAL
      && genus_req->range == REQ_RANGE_LOCAL) {
    /* Applies to the same target building. */

    if (impr_req->present && !genus_req->present) {
      /* The target building can't not have the genus it has. */
      return (impr_req->source.value.building->genus
              == genus_req->source.value.impr_genus);
    }

    if (impr_req->present && genus_req->present) {
      /* The target building can't have another genus than it has. */
      return (impr_req->source.value.building->genus
              != genus_req->source.value.impr_genus);
    }
  }

  /* No special knowledge. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the specified nation requirement contradicts the
  specified nation group requirement.
**************************************************************************/
static bool nation_contra_group(const struct requirement *nation_req,
                                const struct requirement *group_req)
{
  /* The input is sane. */
  fc_assert_ret_val(nation_req->source.kind == VUT_NATION, FALSE);
  fc_assert_ret_val(group_req->source.kind == VUT_NATIONGROUP, FALSE);

  if (nation_req->range == REQ_RANGE_PLAYER
      && group_req->range == REQ_RANGE_PLAYER) {
    /* Applies to the same target building. */

    if (nation_req->present && !group_req->present) {
      /* The target nation can't be in the group. */
      return nation_is_in_group(nation_req->source.value.nation,
                                group_req->source.value.nationgroup);
    }
  }

  /* No special knowledge. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if req1 and req2 contradicts each other because a present
  requirement implies the presence of a !present requirement according to
  the knowledge about implications in universal_found_function.
**************************************************************************/
static bool present_implies_not_present(const struct requirement *req1,
                                        const struct requirement *req2)
{
  const struct requirement *absent, *present;

  if (req1->present == req2->present) {
    /* Can't use the knowledge in universal_found_function when both are
     * required to be absent or when both are required to be present.
     * It is no contradiction to require !Spy unit and !Missile unit class.
     * It is no contradiction to require River and Irrigation at the same
     * tile. */
    return FALSE;
  }

  if (req1->present) {
    absent = req2;
    present = req1;
  } else {
    absent = req1;
    present = req2;
  }

  if (!universal_found_function[present->source.kind]) {
    /* No knowledge to exploit. */
    return FALSE;
  }

  if (present->range != absent->range) {
    /* Larger ranges are not always strict supersets of smaller ranges.
     * Example: Traderoute > CAdjacent but something may be in CAdjacent
     * but not in Traderoute. */
    return FALSE;
  }

  return ITF_YES == universal_fulfills_requirement(absent,
                                                   &present->source);
}

/**********************************************************************//**
  Returns TRUE if req1 and req2 contradicts each other.

  TODO: If information about what entity each requirement type will be
  evaluated against is passed it will become possible to detect stuff like
  that an unclaimed tile contradicts all DiplRel requirements against it.
**************************************************************************/
bool are_requirements_contradictions(const struct requirement *req1,
                                     const struct requirement *req2)
{
  if (are_requirements_opposites(req1, req2)) {
    /* The exact opposite. */
    return TRUE;
  }

  if (present_implies_not_present(req1, req2)) {
    return TRUE;
  }

  switch (req1->source.kind) {
  case VUT_IMPROVEMENT:
    if (req2->source.kind == VUT_IMPR_GENUS) {
      return impr_contra_genus(req1, req2);
    }

    /* No special knowledge. */
    return FALSE;
    break;
  case VUT_IMPR_GENUS:
    if (req2->source.kind == VUT_IMPROVEMENT) {
      return impr_contra_genus(req2, req1);
    }

    /* No special knowledge. */
    return FALSE;
    break;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    if (req2->source.kind != req1->source.kind) {
      /* Finding contradictions across requirement kinds aren't supported
       * for DiplRel requirements. */
      return FALSE;
    } else {
      /* Use the special knowledge about DiplRel requirements to find
       * contradictions. */

      bv_diplrel_all_reqs req1_contra;
      int req2_pos;

      req1_contra = diplrel_req_contradicts(req1);
      req2_pos = requirement_diplrel_ereq(req2->source.value.diplrel,
                                          req2->range,
                                          req2->present);

      return BV_ISSET(req1_contra, req2_pos);
    }
    break;
  case VUT_MINMOVES:
    if (req2->source.kind != VUT_MINMOVES) {
      /* Finding contradictions across requirement kinds aren't supported
       * for MinMoveFrags requirements. */
      return FALSE;
    } else if (req1->present == req2->present) {
      /* No contradiction possible. */
      return FALSE;
    } else {
      /* Number of move fragments left can't be larger than the number
       * required to be present and smaller than the number required to not
       * be present when the number required to be present is smaller than
       * the number required to not be present. */
      if (req1->present) {
        return req1->source.value.minmoves >= req2->source.value.minmoves;
      } else {
        return req1->source.value.minmoves <= req2->source.value.minmoves;
      }
    }
    break;
  case VUT_NATION:
    if (req2->source.kind == VUT_NATIONGROUP) {
      return nation_contra_group(req1, req2);
    }

    /* No special knowledge. */
    return FALSE;
    break;
  case VUT_NATIONGROUP:
    if (req2->source.kind == VUT_NATION) {
      return nation_contra_group(req2, req1);
    }

    /* No special knowledge. */
    return FALSE;
    break;
  default:
    /* No special knowledge exists. The requirements aren't the exact
     * opposite of each other per the initial check. */
    return FALSE;
    break;
  }
}

/**********************************************************************//**
  Returns TRUE if the given requirement contradicts the given requirement
  vector.
**************************************************************************/
bool does_req_contradicts_reqs(const struct requirement *req,
                               const struct requirement_vector *vec)
{
  /* If the requirement is contradicted by any requirement in the vector it
   * contradicts the entire requirement vector. */
  requirement_vector_iterate(vec, preq) {
    if (are_requirements_contradictions(req, preq)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  /* Not a singe requirement in the requirement vector is contradicted be
   * the specified requirement. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if players are in the same requirements range.
**************************************************************************/
static inline bool players_in_same_range(const struct player *pplayer1,
                                         const struct player *pplayer2,
                                         enum req_range range)
{
  switch (range) {
  case REQ_RANGE_WORLD:
    return TRUE;
  case REQ_RANGE_ALLIANCE:
    return pplayers_allied(pplayer1, pplayer2);
  case REQ_RANGE_TEAM:
    return players_on_same_team(pplayer1, pplayer2);
  case REQ_RANGE_PLAYER:
    return pplayer1 == pplayer2;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CITY:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

/**********************************************************************//**
  Returns the number of total world buildings (this includes buildings
  that have been destroyed).
**************************************************************************/
static int num_world_buildings_total(const struct impr_type *building)
{
  if (is_great_wonder(building)) {
    return (great_wonder_is_built(building)
            || great_wonder_is_destroyed(building) ? 1 : 0);
  } else {
    log_error("World-ranged requirements are only supported for wonders.");
    return 0;
  }
}

/**********************************************************************//**
  Returns the number of buildings of a certain type in the world.
**************************************************************************/
static int num_world_buildings(const struct impr_type *building)
{
  if (is_great_wonder(building)) {
    return (great_wonder_is_built(building) ? 1 : 0);
  } else {
    log_error("World-ranged requirements are only supported for wonders.");
    return 0;
  }
}

/**********************************************************************//**
  Returns whether a building of a certain type has ever been built by
  pplayer, even if it has subsequently been destroyed.

  Note: the implementation of this is no different in principle from
  num_world_buildings_total(), but the semantics are different because
  unlike a great wonder, a small wonder could be destroyed and rebuilt
  many times, requiring return of values >1, but there's no record kept
  to support that. Fortunately, the only current caller doesn't need the
  exact number.
**************************************************************************/
static bool player_has_ever_built(const struct player *pplayer,
                                  const struct impr_type *building)
{
  if (is_wonder(building)) {
    return (wonder_is_built(pplayer, building)
            || wonder_is_lost(pplayer, building) ? TRUE : FALSE);
  } else {
    log_error("Player-ranged requirements are only supported for wonders.");
    return FALSE;
  }
}

/**********************************************************************//**
  Returns the number of buildings of a certain type owned by plr.
**************************************************************************/
static int num_player_buildings(const struct player *pplayer,
				const struct impr_type *building)
{
  if (is_wonder(building)) {
    return (wonder_is_built(pplayer, building) ? 1 : 0);
  } else {
    log_error("Player-ranged requirements are only supported for wonders.");
    return 0;
  }
}

/**********************************************************************//**
  Returns the number of buildings of a certain type on a continent.
**************************************************************************/
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
    log_error("Island-ranged requirements are only supported for wonders.");
  }
  return 0;
}

/**********************************************************************//**
  Returns the number of buildings of a certain type in a city.
**************************************************************************/
static int num_city_buildings(const struct city *pcity,
			      const struct impr_type *building)
{
  return (city_has_building(pcity, building) ? 1 : 0);
}

/**********************************************************************//**
  Are there any source buildings within range of the target that are not
  obsolete?

  The target gives the type of the target.  The exact target is a player,
  city, or building specified by the target_xxx arguments.

  The range gives the range of the requirement.

  "Survives" specifies whether the requirement allows destroyed sources.
  If set then all source buildings ever built are counted; if not then only
  living buildings are counted.

  source gives the building type of the source in question.
**************************************************************************/
static enum fc_tristate
is_building_in_range(const struct player *target_player,
                     const struct city *target_city,
                     const struct impr_type *target_building,
                     enum req_range range,
                     bool survives,
                     const struct impr_type *source)
{
  /* Check if it's certain that the building is obsolete given the
   * specification we have */
  if (improvement_obsolete(target_player, source, target_city)) {
    return TRI_NO;
  }

  if (survives) {

    /* Check whether condition has ever held, using cached information. */
    switch (range) {
    case REQ_RANGE_WORLD:
      return BOOL_TO_TRISTATE(num_world_buildings_total(source) > 0);
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
      if (target_player == NULL) {
        return TRI_MAYBE;
      }
      players_iterate_alive(plr2) {
        if (players_in_same_range(target_player, plr2, range)
            && player_has_ever_built(plr2, source)) {
          return TRI_YES;
        }
      } players_iterate_alive_end;
      return TRI_NO;
    case REQ_RANGE_PLAYER:
      if (target_player == NULL) {
        return TRI_MAYBE;
      }
      return BOOL_TO_TRISTATE(player_has_ever_built(target_player, source));
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CITY:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
      /* There is no sources cache for this. */
      log_error("Surviving requirements are only supported at "
                "World/Alliance/Team/Player ranges.");
      return TRI_NO;
    case REQ_RANGE_COUNT:
      break;
    }

  } else {

    /* Non-surviving requirement. */
    switch (range) {
    case REQ_RANGE_WORLD:
      return BOOL_TO_TRISTATE(num_world_buildings(source) > 0);
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
      if (target_player == NULL) {
        return TRI_MAYBE;
      }
      players_iterate_alive(plr2) {
        if (players_in_same_range(target_player, plr2, range)
            && num_player_buildings(plr2, source) > 0) {
          return TRI_YES;
        }
      } players_iterate_alive_end;
      return TRI_NO;
    case REQ_RANGE_PLAYER:
      if (target_player == NULL) {
        return TRI_MAYBE;
      }
      return BOOL_TO_TRISTATE(num_player_buildings(target_player, source) > 0);
    case REQ_RANGE_CONTINENT:
      /* At present, "Continent" effects can affect only
       * cities and units in cities. */
      if (target_player && target_city) {
        int continent = tile_continent(target_city->tile);
        return BOOL_TO_TRISTATE(num_continent_buildings(target_player,
                                                        continent, source) > 0);
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_TRADEROUTE:
      if (target_city) {
        if (num_city_buildings(target_city, source) > 0) {
          return TRI_YES;
        } else {
          trade_partners_iterate(target_city, trade_partner) {
            if (num_city_buildings(trade_partner, source) > 0) {
              return TRI_YES;
            }
          } trade_partners_iterate_end;
        }
        return TRI_NO;
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_CITY:
      if (target_city) {
        return BOOL_TO_TRISTATE(num_city_buildings(target_city, source) > 0);
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_LOCAL:
      if (target_building) {
        if (target_building == source) {
          return TRI_YES;
        } else {
          return TRI_NO;
        }
      } else {
        /* TODO: other local targets */
        return TRI_MAYBE;
      }
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
      return TRI_NO;
    case REQ_RANGE_COUNT:
      break;
    }

  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return TRI_NO;
}

/**********************************************************************//**
  Is there a source tech within range of the target?
**************************************************************************/
static enum fc_tristate is_tech_in_range(const struct player *target_player,
                                         enum req_range range, bool survives,
                                         Tech_type_id tech)
{
  if (survives) {
    fc_assert(range == REQ_RANGE_WORLD);
    return BOOL_TO_TRISTATE(game.info.global_advances[tech]);
  }

  /* Not a 'surviving' requirement. */
  switch (range) {
  case REQ_RANGE_PLAYER:
    if (NULL != target_player) {
      return BOOL_TO_TRISTATE(TECH_KNOWN == research_invention_state
                                (research_get(target_player), tech));
    } else {
      return TRI_MAYBE;
    }
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
   if (NULL == target_player) {
     return TRI_MAYBE;
   }
   players_iterate_alive(plr2) {
     if (players_in_same_range(target_player, plr2, range)) {
       if (research_invention_state(research_get(plr2), tech)
           == TECH_KNOWN) {
         return TRI_YES;
       }
     }
   } players_iterate_alive_end;

   return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a tech with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_techflag_in_range(const struct player *target_player,
                                             enum req_range range,
                                             enum tech_flag_id techflag)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
    if (NULL != target_player) {
      return BOOL_TO_TRISTATE(player_knows_techs_with_flag(target_player, techflag));
    } else {
      return TRI_MAYBE;
    }
    break;
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
    if (NULL == target_player) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)
          && player_knows_techs_with_flag(plr2, techflag)) {
        return TRI_YES;
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_WORLD:
    players_iterate(pplayer) {
      if (player_knows_techs_with_flag(pplayer, techflag)) {
        return TRI_YES;
      }
    } players_iterate_end;

    return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is city or player with at least minculture culture in range?
**************************************************************************/
static enum fc_tristate is_minculture_in_range(const struct city *target_city,
                                               const struct player *target_player,
                                               enum req_range range,
                                               int minculture)
{
  switch (range) {
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(city_culture(target_city) >= minculture);
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    if (city_culture(target_city) >= minculture) {
      return TRI_YES;
    } else {
      trade_partners_iterate(target_city, trade_partner) {
        if (city_culture(trade_partner) >= minculture) {
          return TRI_YES;
        }
      } trade_partners_iterate_end;
      return TRI_MAYBE;
    }
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (NULL == target_player) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)) {
        if (player_culture(plr2) >= minculture) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is city with at least min_foreign_pct foreigners in range?
**************************************************************************/
static enum fc_tristate
is_minforeignpct_in_range(const struct city *target_city, enum req_range range,
                          int min_foreign_pct)
{
  int foreign_pct;

  switch (range) {
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    foreign_pct = citizens_nation_foreign(target_city) * 100
      / city_size_get(target_city);
    return BOOL_TO_TRISTATE(foreign_pct >= min_foreign_pct);
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    foreign_pct = citizens_nation_foreign(target_city) * 100
      / city_size_get(target_city);
    if (foreign_pct >= min_foreign_pct) {
      return TRI_YES;
    } else {
      trade_partners_iterate(target_city, trade_partner) {
        foreign_pct = citizens_nation_foreign(trade_partner) * 100
          / city_size_get(trade_partner); 
        if (foreign_pct >= min_foreign_pct) {
          return TRI_YES;
        }
      } trade_partners_iterate_end;
      return TRI_MAYBE;
    }
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a tile with max X units within range of the target?
**************************************************************************/
static enum fc_tristate
is_tile_units_in_range(const struct tile *target_tile, enum req_range range,
                       int max_units)
{
  /* TODO: if can't see V_INVIS -> TRI_MAYBE */
  switch (range) {
  case REQ_RANGE_LOCAL:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(unit_list_size(target_tile->units) <= max_units);
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    if (unit_list_size(target_tile->units) <= max_units) {
      return TRI_YES;
    }
    cardinal_adjc_iterate(&(wld.map), target_tile, adjc_tile) {
      if (unit_list_size(adjc_tile->units) <= max_units) {
        return TRI_YES;
      }
    } cardinal_adjc_iterate_end;
    return TRI_NO;
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    if (unit_list_size(target_tile->units) <= max_units) {
      return TRI_YES;
    }
    adjc_iterate(&(wld.map), target_tile, adjc_tile) {
      if (unit_list_size(adjc_tile->units) <= max_units) {
        return TRI_YES;
      }
    } adjc_iterate_end;
    return TRI_NO;
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a source extra type within range of the target?
**************************************************************************/
static enum fc_tristate is_extra_type_in_range(const struct tile *target_tile,
                                               const struct city *target_city,
                                               enum req_range range, bool survives,
                                               struct extra_type *pextra)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has extra of requested type. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(target_tile, pextra));
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(target_tile, pextra)
                            || is_extra_card_near(target_tile, pextra));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(target_tile, pextra)
                            || is_extra_near_tile(target_tile, pextra));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_extra(ptile, pextra)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;

  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_extra(ptile, pextra)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;
    trade_partners_iterate(target_city, trade_partner) {
      city_tile_iterate(city_map_radius_sq_get(trade_partner),
                        city_tile(trade_partner), ptile) {
        if (tile_has_extra(ptile, pextra)) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;

  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a source goods type within range of the target?
**************************************************************************/
static enum fc_tristate is_goods_type_in_range(const struct tile *target_tile,
                                               const struct city *target_city,
                                               enum req_range range, bool survives,
                                               struct goods_type *pgood)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CITY:
    /* The requirement is filled if the city imports good of requested type. */
    if (!target_city) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(city_receives_goods(target_city, pgood));
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a source tile within range of the target?
**************************************************************************/
static enum fc_tristate is_terrain_in_range(const struct tile *target_tile,
                                            const struct city *target_city,
                                            enum req_range range, bool survives,
                                            const struct terrain *pterrain)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return pterrain && tile_terrain(target_tile) == pterrain;
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(pterrain && is_terrain_card_near(target_tile, pterrain, TRUE));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(pterrain && is_terrain_near_tile(target_tile, pterrain, TRUE));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    if (pterrain != NULL) {
      city_tile_iterate(city_map_radius_sq_get(target_city),
                        city_tile(target_city), ptile) {
        if (tile_terrain(ptile) == pterrain) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    }
    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    if (pterrain != NULL) {
      city_tile_iterate(city_map_radius_sq_get(target_city),
                        city_tile(target_city), ptile) {
        if (tile_terrain(ptile) == pterrain) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
      trade_partners_iterate(target_city, trade_partner) {
        city_tile_iterate(city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          if (tile_terrain(ptile) == pterrain) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      } trade_partners_iterate_end;
    }
    return TRI_NO;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a source terrain class within range of the target?
**************************************************************************/
static enum fc_tristate is_terrain_class_in_range(const struct tile *target_tile,
                                                  const struct city *target_city,
                                                  enum req_range range, bool survives,
                                                  enum terrain_class pclass)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has the terrain of correct class. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(target_tile)) == pclass);
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(target_tile)) == pclass
                            || is_terrain_class_card_near(target_tile, pclass));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(target_tile)) == pclass
                            || is_terrain_class_near_tile(target_tile, pclass));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);
      if (pterrain != T_UNKNOWN
          && terrain_type_terrain_class(pterrain) == pclass) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);
      if (pterrain != T_UNKNOWN
          && terrain_type_terrain_class(pterrain) == pclass) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    trade_partners_iterate(target_city, trade_partner) {
      city_tile_iterate(city_map_radius_sq_get(trade_partner),
                        city_tile(trade_partner), ptile) {
        const struct terrain *pterrain = tile_terrain(ptile);
        if (pterrain != T_UNKNOWN
            && terrain_type_terrain_class(pterrain) == pclass) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a terrain with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_terrainflag_in_range(const struct tile *target_tile,
                                                const struct city *target_city,
                                                enum req_range range, bool survives,
                                                enum terrain_flag_id terrflag)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is fulfilled if the tile has a terrain with
     * correct flag. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(target_tile),
                                             terrflag));
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(target_tile),
                                             terrflag)
                            || is_terrain_flag_card_near(target_tile,
                                                         terrflag));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(target_tile),
                                             terrflag)
                            || is_terrain_flag_near_tile(target_tile,
                                                         terrflag));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);
      if (pterrain != T_UNKNOWN
          && terrain_has_flag(pterrain, terrflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);
      if (pterrain != T_UNKNOWN
          && terrain_has_flag(pterrain, terrflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    trade_partners_iterate(target_city, trade_partner) {
      city_tile_iterate(city_map_radius_sq_get(trade_partner),
                        city_tile(trade_partner), ptile) {
        const struct terrain *pterrain = tile_terrain(ptile);
        if (pterrain != T_UNKNOWN
            && terrain_has_flag(pterrain, terrflag)) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a road with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_roadflag_in_range(const struct tile *target_tile,
                                             const struct city *target_city,
                                             enum req_range range, bool survives,
                                             enum road_flag_id roadflag)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has a road with correct flag. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(target_tile, roadflag));
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(target_tile, roadflag)
                            || is_road_flag_card_near(target_tile, roadflag));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(target_tile, roadflag)
                            || is_road_flag_near_tile(target_tile, roadflag));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_road_flag(ptile, roadflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_road_flag(ptile, roadflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    trade_partners_iterate(target_city, trade_partner) {
      city_tile_iterate(city_map_radius_sq_get(trade_partner),
                        city_tile(trade_partner), ptile) {
        if (tile_has_road_flag(ptile, roadflag)) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there an extra with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_extraflag_in_range(const struct tile *target_tile,
                                              const struct city *target_city,
                                              enum req_range range, bool survives,
                                              enum extra_flag_id extraflag)
{
  switch (range) {
  case REQ_RANGE_LOCAL:
    /* The requirement is filled if the tile has an extra with correct flag. */
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(target_tile, extraflag));
  case REQ_RANGE_CADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(target_tile, extraflag)
                            || is_extra_flag_card_near(target_tile, extraflag));
  case REQ_RANGE_ADJACENT:
    if (!target_tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(target_tile, extraflag)
                            || is_extra_flag_near_tile(target_tile, extraflag));
  case REQ_RANGE_CITY:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_extra_flag(ptile, extraflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (!target_city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(city_map_radius_sq_get(target_city),
                      city_tile(target_city), ptile) {
      if (tile_has_extra_flag(ptile, extraflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    trade_partners_iterate(target_city, trade_partner) {
      city_tile_iterate(city_map_radius_sq_get(trade_partner),
                        city_tile(trade_partner), ptile) {
        if (tile_has_extra_flag(ptile, extraflag)) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a terrain which can support the specified infrastructure
  within range of the target?
**************************************************************************/
static enum fc_tristate is_terrain_alter_possible_in_range(const struct tile *target_tile,
                                                           enum req_range range,
                                                           bool survives,
                                                           enum terrain_alteration alteration)
{
  if (!target_tile) {
    return TRI_MAYBE;
  }

  switch (range) {
  case REQ_RANGE_LOCAL:
    return BOOL_TO_TRISTATE(terrain_can_support_alteration(tile_terrain(target_tile),
                                                           alteration));
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT: /* XXX Could in principle support ADJACENT. */
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a nation within range of the target?
**************************************************************************/
static enum fc_tristate is_nation_in_range(const struct player *target_player,
                                           enum req_range range, bool survives,
                                           const struct nation_type *nation)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(nation_of_player(target_player) == nation);
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)) {
        if (nation_of_player(plr2) == nation) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_WORLD:
    /* NB: if a player is ever removed outright from the game
     * (e.g. via /remove), rather than just dying, this 'survives'
     * requirement will stop being true for their nation.
     * create_command_newcomer() can also cause this to happen. */
    return BOOL_TO_TRISTATE(NULL != nation->player
                            && (survives || nation->player->is_alive));
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a nation group within range of the target?
**************************************************************************/
static enum fc_tristate
is_nation_group_in_range(const struct player *target_player,
                         enum req_range range, bool survives,
                         const struct nation_group *ngroup)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(nation_is_in_group(nation_of_player(target_player),
                                               ngroup));
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)) {
        if (nation_is_in_group(nation_of_player(plr2), ngroup)) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is there a nationality within range of the target?
**************************************************************************/
static enum fc_tristate is_nationality_in_range(const struct city *target_city,
                                                enum req_range range,
                                                const struct nation_type *nationality)
{
  switch (range) {
  case REQ_RANGE_CITY:
    if (target_city == NULL) {
     return TRI_MAYBE;
    }
    citizens_iterate(target_city, slot, count) {
      if (player_slot_get_player(slot)->nation == nationality) {
        return TRI_YES;
      }
    } citizens_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADEROUTE:
    if (target_city == NULL) {
      return TRI_MAYBE;
    }
    citizens_iterate(target_city, slot, count) {
      if (player_slot_get_player(slot)->nation == nationality) {
        return TRI_YES;
      }
    } citizens_iterate_end;

    trade_partners_iterate(target_city, trade_partner) {
      citizens_iterate(trade_partner, slot, count) {
        if (player_slot_get_player(slot)->nation == nationality) {
          return TRI_YES;
        }
      } citizens_iterate_end;
    } trade_partners_iterate_end;

    return TRI_NO;
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is the diplomatic state within range of the target?
**************************************************************************/
static enum fc_tristate is_diplrel_in_range(const struct player *target_player,
                                            const struct player *other_player,
                                            enum req_range range,
                                            int diplrel)
{
  switch (range) {
  case REQ_RANGE_PLAYER:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(is_diplrel_to_other(target_player, diplrel));
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (target_player == NULL) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)) {
        if (is_diplrel_to_other(plr2, diplrel)) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_LOCAL:
    if (target_player == NULL || other_player == NULL) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(is_diplrel_between(target_player, other_player, diplrel));
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADEROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Is the diplomatic state within range of any unit at the target tile?
**************************************************************************/
static enum fc_tristate
is_diplrel_unitany_in_range(const struct tile *target_tile,
                            const struct player *other_player,
                            enum req_range range,
                            int diplrel)
{
  enum fc_tristate out = TRI_NO;

  if (target_tile == NULL) {
    return TRI_MAYBE;
  }

  unit_list_iterate(target_tile->units, target_unit) {
    enum fc_tristate for_target_unit = is_diplrel_in_range(
        unit_owner(target_unit), other_player, range, diplrel);

    out = fc_tristate_or(out, for_target_unit);
  } unit_list_iterate_end;

  return out;
}

/**********************************************************************//**
  Is there a unit of the given type within range of the target?
**************************************************************************/
static enum fc_tristate is_unittype_in_range(const struct unit_type *target_unittype,
                                             enum req_range range, bool survives,
                                             const struct unit_type *punittype)
{
  if (range != REQ_RANGE_LOCAL) {
    return TRI_NO;
  }
  if (!target_unittype) {
    return TRI_MAYBE;
  }

  return BOOL_TO_TRISTATE(target_unittype == punittype);
}

/**********************************************************************//**
  Is there a unit with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_unitflag_in_range(const struct unit_type *target_unittype,
                                             enum req_range range, bool survives,
                                             enum unit_type_flag_id unitflag)
{
  if (range != REQ_RANGE_LOCAL) {
    return TRI_NO;
  }
  if (!target_unittype) {
    return TRI_MAYBE;
  }

  return BOOL_TO_TRISTATE(utype_has_flag(target_unittype, unitflag));
}

/**********************************************************************//**
  Is there a unit with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_unitclass_in_range(const struct unit_type *target_unittype,
                                              enum req_range range, bool survives,
                                              struct unit_class *pclass)
{
  if (range != REQ_RANGE_LOCAL) {
    return TRI_NO;
  }
  if (!target_unittype) {
    return TRI_MAYBE;
  }

  return BOOL_TO_TRISTATE(utype_class(target_unittype) == pclass);
}

/**********************************************************************//**
  Is there a unit with the given flag within range of the target?
**************************************************************************/
static enum fc_tristate is_unitclassflag_in_range(const struct unit_type *target_unittype,
                                                  enum req_range range, bool survives,
                                                  enum unit_class_flag_id ucflag)
{
  if (range != REQ_RANGE_LOCAL) {
    return TRI_NO;
  }
  if (!target_unittype) {
    return TRI_MAYBE;
  }

  return BOOL_TO_TRISTATE(uclass_has_flag(utype_class(target_unittype), ucflag));
}

/**********************************************************************//**
  Is the given property of the unit state true?
**************************************************************************/
static enum fc_tristate is_unit_state(const struct unit *target_unit,
                                      enum req_range range,
                                      bool survives,
                                      enum ustate_prop uprop)
{
  fc_assert_ret_val_msg(range == REQ_RANGE_LOCAL, TRI_NO,
                        "Unsupported range \"%s\"",
                        req_range_name(range));

  /* Could be asked with incomplete data.
   * is_req_active() will handle it based on prob_type. */
  if (target_unit == NULL) {
    return TRI_MAYBE;
  }

  switch (uprop) {
  case USP_TRANSPORTED:
    return BOOL_TO_TRISTATE(target_unit->transporter != NULL);
  case USP_LIVABLE_TILE:
    return BOOL_TO_TRISTATE(
          can_unit_exist_at_tile(&(wld.map), target_unit,
                                 unit_tile(target_unit)));
    break;
  case USP_TRANSPORTING:
    return BOOL_TO_TRISTATE(0 < get_transporter_occupancy(target_unit));
  case USP_HAS_HOME_CITY:
    return BOOL_TO_TRISTATE(target_unit->homecity > 0);
  case USP_NATIVE_TILE:
    return BOOL_TO_TRISTATE(
        is_native_tile(unit_type_get(target_unit), unit_tile(target_unit)));
    break;
  case USP_NATIVE_EXTRA:
    return BOOL_TO_TRISTATE(
        tile_has_native_base(unit_tile(target_unit),
                             unit_type_get(target_unit)));
    break;
  case USP_MOVED_THIS_TURN:
    return BOOL_TO_TRISTATE(target_unit->moved);
  case USP_COUNT:
    fc_assert_msg(uprop != USP_COUNT, "Invalid unit state property.");
    /* Invalid property is unknowable. */
    return TRI_NO;
  }

  /* Should never be reached */
  fc_assert_msg(FALSE, "Unsupported unit property %d", uprop);

  return TRI_NO;
}

/**********************************************************************//**
  Is the unit performing the specified activity?
**************************************************************************/
static enum fc_tristate unit_activity_in_range(const struct unit *punit,
                                               enum req_range range,
                                               enum unit_activity activity)
{
  fc_assert_ret_val_msg(range == REQ_RANGE_LOCAL, TRI_NO,
                        "Unsupported range \"%s\"",
                        req_range_name(range));

  /* Could be asked with incomplete data.
   * is_req_active() will handle it based on prob_type. */
  if (punit == NULL) {
    return TRI_MAYBE;
  }

  switch (punit->activity) {
  case ACTIVITY_IDLE:
  case ACTIVITY_SENTRY:
  case ACTIVITY_GOTO:
  case ACTIVITY_EXPLORE:
    /* Seen as idle. */
    return BOOL_TO_TRISTATE(activity == ACTIVITY_IDLE);
  default:
    /* Handled below. */
    break;
  }

  return BOOL_TO_TRISTATE(punit->activity == activity);
}

/**********************************************************************//**
  Is center of given city in tile. If city is NULL, any city will do.
**************************************************************************/
static bool is_city_in_tile(const struct tile *ptile,
			    const struct city *pcity)
{
  if (pcity == NULL) {
    return tile_city(ptile) != NULL;
  } else {
    return is_city_center(pcity, ptile);
  }
}

/**********************************************************************//**
  Is center of given city in range. If city is NULL, any city will do.
**************************************************************************/
static enum fc_tristate is_citytile_in_range(const struct tile *target_tile,
                                             const struct city *target_city,
                                             enum req_range range,
                                             enum citytile_type citytile)
{
  fc_assert_ret_val(req_range_is_valid(range), TRI_MAYBE);
  if (target_tile == NULL) {
    return TRI_MAYBE;
  }

  switch (citytile) {
  case CITYT_CENTER:
    switch (range) {
    case REQ_RANGE_LOCAL:
      return BOOL_TO_TRISTATE(is_city_in_tile(target_tile, target_city));
    case REQ_RANGE_CADJACENT:
      if (is_city_in_tile(target_tile, target_city)) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (is_city_in_tile(adjc_tile, target_city)) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (is_city_in_tile(target_tile, target_city)) {
        return TRI_YES;
      }
      adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (is_city_in_tile(adjc_tile, target_city)) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_CLAIMED:
    switch (range) {
    case REQ_RANGE_LOCAL:
      return BOOL_TO_TRISTATE(target_tile->owner != NULL);
    case REQ_RANGE_CADJACENT:
      if (target_tile->owner != NULL) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (adjc_tile->owner != NULL) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (target_tile->owner != NULL) {
        return TRI_YES;
      }
      adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (adjc_tile->owner != NULL) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_EXTRAS_OWNED:
    switch (range) {
    case REQ_RANGE_LOCAL:
      return BOOL_TO_TRISTATE(target_tile->extras_owner != NULL);
    case REQ_RANGE_CADJACENT:
      if (target_tile->extras_owner != NULL) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (adjc_tile->extras_owner != NULL) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (target_tile->extras_owner != NULL) {
        return TRI_YES;
      }
      adjc_iterate(&(wld.map), target_tile, adjc_tile) {
        if (adjc_tile->extras_owner != NULL) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADEROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_LAST:
    /* Handled below */
    break;
  }

  /* Not implemented */
  log_error("is_req_active(): citytile %d not supported.",
            citytile);
  return TRI_MAYBE;
}

/**********************************************************************//**
  Is city with specific status in range. If city is NULL, any city will do.
**************************************************************************/
static enum fc_tristate is_citystatus_in_range(const struct city *target_city,
                                               enum req_range range,
                                               enum citystatus_type citystatus)
{
  if (citystatus == CITYS_OWNED_BY_ORIGINAL) {
    switch (range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(city_owner(target_city) == target_city->original);
    case REQ_RANGE_TRADEROUTE:
      {
        bool found = (city_owner(target_city) == target_city->original);

        trade_partners_iterate(target_city, trade_partner) {
          if (city_owner(trade_partner) == trade_partner->original) {
            found = TRUE;
            break;
          }
        } trade_partners_iterate_end;

        return BOOL_TO_TRISTATE(found);
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      break;
    }

    fc_assert_msg(FALSE, "Invalid range %d for citystatus.", range);

    return TRI_MAYBE;
  } else {
    /* Not implemented */
    log_error("is_req_active(): citystatus %d not supported.",
              citystatus);
    return TRI_MAYBE;
  }
}

/**********************************************************************//**
  Has achievement been claimed by someone in range.
**************************************************************************/
static enum fc_tristate
is_achievement_in_range(const struct player *target_player,
                        enum req_range range,
                        const struct achievement *achievement)
{
  if (range == REQ_RANGE_WORLD) {
    return BOOL_TO_TRISTATE(achievement_claimed(achievement));
  } else if (target_player == NULL) {
    return TRI_MAYBE;
  } else if (range == REQ_RANGE_ALLIANCE || range == REQ_RANGE_TEAM) {
    players_iterate_alive(plr2) {
      if (players_in_same_range(target_player, plr2, range)
          && achievement_player_has(achievement, plr2)) {
        return TRI_YES;
      }
    } players_iterate_alive_end;
    return TRI_NO;
  } else if (range == REQ_RANGE_PLAYER) {
    if (achievement_player_has(achievement, target_player)) {
      return TRI_YES;
    } else {
      return TRI_NO;
    }
  }

  fc_assert_msg(FALSE,
                "Illegal range %d for achievement requirement.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Checks the requirement to see if it is active on the given target.

  context gives the target (or targets) to evaluate against
  req gives the requirement itself

  context may be NULL. This is equivalent to passing an empty context.

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
**************************************************************************/
bool is_req_active(const struct req_context *context,
                   const struct player *other_player,
                   const struct requirement *req,
                   const enum   req_problem_type prob_type)
{
  enum fc_tristate eval = TRI_NO;

  if (context == NULL) {
    context = req_context_empty();
  }

  /* Note the target may actually not exist.  In particular, effects that
   * have a VUT_TERRAIN may often be passed
   * to this function with a city as their target.  In this case the
   * requirement is simply not met. */
  switch (req->source.kind) {
  case VUT_NONE:
    eval = TRI_YES;
    break;
  case VUT_ADVANCE:
    /* The requirement is filled if the player owns the tech. */
    eval = is_tech_in_range(context->player, req->range, req->survives,
                            advance_number(req->source.value.advance));
    break;
 case VUT_TECHFLAG:
    eval = is_techflag_in_range(context->player, req->range,
                                req->source.value.techflag);
    break;
  case VUT_GOVERNMENT:
    /* The requirement is filled if the player is using the government. */
    if (context->player == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = BOOL_TO_TRISTATE(government_of_player(context->player)
                              == req->source.value.govern);
    }
    break;
  case VUT_ACHIEVEMENT:
    eval = is_achievement_in_range(context->player, req->range,
                                   req->source.value.achievement);
    break;
  case VUT_STYLE:
    if (context->player == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = BOOL_TO_TRISTATE(context->player->style
                              == req->source.value.style);
    }
    break;
  case VUT_IMPROVEMENT:
    eval = is_building_in_range(context->player, context->city,
                                context->building,
                                req->range, req->survives,
                                req->source.value.building);
    break;
  case VUT_IMPR_GENUS:
    eval = (context->building ? BOOL_TO_TRISTATE(
                                  context->building->genus
                                  == req->source.value.impr_genus)
                              : TRI_MAYBE);
    break;
  case VUT_EXTRA:
    eval = is_extra_type_in_range(context->tile, context->city,
                                  req->range, req->survives,
                                  req->source.value.extra);
    break;
  case VUT_GOOD:
    eval = is_goods_type_in_range(context->tile, context->city,
                                  req->range, req->survives,
                                  req->source.value.good);
    break;
  case VUT_TERRAIN:
    eval = is_terrain_in_range(context->tile, context->city,
                               req->range, req->survives,
                               req->source.value.terrain);
    break;
  case VUT_TERRFLAG:
    eval = is_terrainflag_in_range(context->tile, context->city,
                                   req->range, req->survives,
                                   req->source.value.terrainflag);
    break;
  case VUT_NATION:
    eval = is_nation_in_range(context->player, req->range, req->survives,
                              req->source.value.nation);
    break;
  case VUT_NATIONGROUP:
    eval = is_nation_group_in_range(context->player, req->range,
                                    req->survives,
                                    req->source.value.nationgroup);
    break;
  case VUT_NATIONALITY:
    eval = is_nationality_in_range(context->city, req->range,
                                   req->source.value.nationality);
    break;
  case VUT_DIPLREL:
    eval = is_diplrel_in_range(context->player, other_player, req->range,
                               req->source.value.diplrel);
    break;
  case VUT_DIPLREL_TILE:
    eval = is_diplrel_in_range(context->tile ? tile_owner(context->tile)
                                             : NULL,
                               context->player,
                               req->range,
                               req->source.value.diplrel);
    break;
  case VUT_DIPLREL_TILE_O:
    eval = is_diplrel_in_range(context->tile ? tile_owner(context->tile)
                                             : NULL,
                               other_player,
                               req->range,
                               req->source.value.diplrel);
    break;
  case VUT_DIPLREL_UNITANY:
    eval = is_diplrel_unitany_in_range(context->tile, context->player,
                                       req->range,
                                       req->source.value.diplrel);
    break;
  case VUT_DIPLREL_UNITANY_O:
    eval = is_diplrel_unitany_in_range(context->tile, other_player,
                                       req->range,
                                       req->source.value.diplrel);
    break;
  case VUT_UTYPE:
    if (context->unittype == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_unittype_in_range(context->unittype,
                                  req->range, req->survives,
                                  req->source.value.utype);
    }
    break;
  case VUT_UTFLAG:
    eval = is_unitflag_in_range(context->unittype,
				req->range, req->survives,
				req->source.value.unitflag);
    break;
  case VUT_UCLASS:
    if (context->unittype == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_unitclass_in_range(context->unittype,
                                   req->range, req->survives,
                                   req->source.value.uclass);
    }
    break;
  case VUT_UCFLAG:
    if (context->unittype == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_unitclassflag_in_range(context->unittype,
                                       req->range, req->survives,
                                       req->source.value.unitclassflag);
    }
    break;
  case VUT_MINVETERAN:
    if (context->unit == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval =
        BOOL_TO_TRISTATE(context->unit->veteran
                         >= req->source.value.minveteran);
    }
    break;
  case VUT_UNITSTATE:
    if (context->unit == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_unit_state(context->unit,
                           req->range, req->survives,
                           req->source.value.unit_state);
    }
    break;
  case VUT_ACTIVITY:
    eval = unit_activity_in_range(context->unit,
                                  req->range, req->source.value.activity);
    break;
  case VUT_MINMOVES:
    if (context->unit == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = BOOL_TO_TRISTATE(
            req->source.value.minmoves <= context->unit->moves_left);
    }
    break;
  case VUT_MINHP:
    if (context->unit == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = BOOL_TO_TRISTATE(
            req->source.value.min_hit_points <= context->unit->hp);
    }
    break;
  case VUT_AGE:
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      if (context->unit == NULL || !is_server()) {
        eval = TRI_MAYBE;
      } else {
        eval = BOOL_TO_TRISTATE(
                 req->source.value.age <=
                 game.info.turn - context->unit->server.birth_turn);
      }
      break;
    case REQ_RANGE_CITY:
      if (context->city == NULL) {
        eval = TRI_MAYBE;
      } else {
        eval = BOOL_TO_TRISTATE(
                 req->source.value.age <=
                 game.info.turn - context->city->turn_founded);
      }
      break;
    case REQ_RANGE_PLAYER:
      if (context->player == NULL) {
        eval = TRI_MAYBE;
      } else {
        eval =
          BOOL_TO_TRISTATE(req->source.value.age
                           <= player_age(context->player));
      }
      break;
    default:
      eval = TRI_MAYBE;
      break;
    }
    break;
  case VUT_MINTECHS:
    switch (req->range) {
    case REQ_RANGE_WORLD:
      /* "None" does not count */
      eval = ((game.info.global_advance_count - 1) >= req->source.value.min_techs);
      break;
    case REQ_RANGE_PLAYER:
      if (context->player == NULL) {
        eval = TRI_MAYBE;
      } else {
        /* "None" does not count */
        eval = BOOL_TO_TRISTATE(
                   (research_get(context->player)->techs_researched - 1)
                   >= req->source.value.min_techs
               );
      }
      break;
    default:
      eval = TRI_MAYBE;
    }
    break;
  case VUT_ACTION:
    eval = BOOL_TO_TRISTATE(context->action
                            && action_number(context->action)
                               == action_number(req->source.value.action));
    break;
  case VUT_OTYPE:
    eval = BOOL_TO_TRISTATE(context->output
                            && context->output->index
                               == req->source.value.outputtype);
    break;
  case VUT_SPECIALIST:
    eval = BOOL_TO_TRISTATE(context->specialist
                            && context->specialist
                               == req->source.value.specialist);
    break;
  case VUT_MINSIZE:
    if (context->city == NULL) {
      eval = TRI_MAYBE;
    } else {
      if (req->range == REQ_RANGE_TRADEROUTE) {
        bool found = FALSE;

        if (city_size_get(context->city) >= req->source.value.minsize) {
          eval = TRI_YES;
          break;
        }
        trade_partners_iterate(context->city, trade_partner) {
          if (city_size_get(trade_partner) >= req->source.value.minsize) {
            found = TRUE;
            break;
          }
        } trade_partners_iterate_end;
        eval = BOOL_TO_TRISTATE(found);
      } else {
        eval = BOOL_TO_TRISTATE(city_size_get(context->city)
                                >= req->source.value.minsize);
      }
    }
    break;
  case VUT_MINCULTURE:
    eval = is_minculture_in_range(context->city, context->player,
                                  req->range,
                                  req->source.value.minculture);
    break;
  case VUT_MINFOREIGNPCT:
    eval = is_minforeignpct_in_range(context->city, req->range,
                                     req->source.value.minforeignpct);
    break;
  case VUT_AI_LEVEL:
    if (context->player == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = BOOL_TO_TRISTATE(is_ai(context->player)
                              && context->player->ai_common.skill_level
                                 == req->source.value.ai_level);
    }
    break;
  case VUT_MAXTILEUNITS:
    eval = is_tile_units_in_range(context->tile, req->range,
                                  req->source.value.max_tile_units);
    break;
  case VUT_TERRAINCLASS:
    eval = is_terrain_class_in_range(context->tile, context->city,
                                     req->range, req->survives,
                                     req->source.value.terrainclass);
    break;
  case VUT_ROADFLAG:
    eval = is_roadflag_in_range(context->tile, context->city,
                                 req->range, req->survives,
                                 req->source.value.roadflag);
    break;
  case VUT_EXTRAFLAG:
    eval = is_extraflag_in_range(context->tile, context->city,
                                 req->range, req->survives,
                                 req->source.value.extraflag);
    break;
  case VUT_MINYEAR:
    eval = BOOL_TO_TRISTATE(game.info.year >= req->source.value.minyear);
    break;
  case VUT_MINCALFRAG:
    eval = BOOL_TO_TRISTATE(game.info.fragment_count >= req->source.value.mincalfrag);
    break;
  case VUT_TOPO:
    eval = BOOL_TO_TRISTATE(current_topo_has_flag(req->source.value.topo_property));
    break;
  case VUT_SERVERSETTING:
    eval = BOOL_TO_TRISTATE(ssetv_setting_has_value(
                                req->source.value.ssetval));
    break;
  case VUT_TERRAINALTER:
    if (context->tile == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_terrain_alter_possible_in_range(context->tile,
                                                req->range, req->survives,
                                                req->source.value.terrainalter);
    }
    break;
  case VUT_CITYTILE:
    if (context->tile == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_citytile_in_range(context->tile, context->city,
                                  req->range,
                                  req->source.value.citytile);
    }
    break;
  case VUT_CITYSTATUS:
    if (context->city == NULL) {
      eval = TRI_MAYBE;
    } else {
      eval = is_citystatus_in_range(context->city,
                                    req->range,
                                    req->source.value.citystatus);
    }
    break;
  case VUT_COUNT:
    log_error("is_req_active(): invalid source kind %d.", req->source.kind);
    return FALSE;
  }

  if (eval == TRI_MAYBE) {
    if (prob_type == RPT_POSSIBLE) {
      return TRUE;
    } else {
      return FALSE;
    }
  }
  if (req->present) {
    return (eval == TRI_YES);
  } else {
    return (eval == TRI_NO);
  }
}

/**********************************************************************//**
  Checks the requirement(s) to see if they are active on the given target.

  context gives the target (or targets) to evaluate against

  reqs gives the requirement vector.
  The function returns TRUE only if all requirements are active.

  context may be NULL. This is equivalent to passing an empty context.

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
**************************************************************************/
bool are_reqs_active(const struct req_context *context,
                     const struct player *other_player,
                     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type)
{
  requirement_vector_iterate(reqs, preq) {
    if (!is_req_active(context, other_player, preq, prob_type)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;
  return TRUE;
}

/**********************************************************************//**
  Return TRUE if this is an "unchanging" requirement.  This means that
  if a target can't meet the requirement now, it probably won't ever be able
  to do so later.  This can be used to do requirement filtering when checking
  if a target may "eventually" become available.

  Note this isn't absolute.  Returning TRUE here just means that the
  requirement probably can't be met.  In some cases (particularly terrains)
  it may be wrong.
***************************************************************************/
bool is_req_unchanging(const struct requirement *req)
{
  switch (req->source.kind) {
  case VUT_NONE:
  case VUT_ACTION:
  case VUT_OTYPE:
  case VUT_SPECIALIST:	/* Only so long as it's at local range only */
  case VUT_AI_LEVEL:
  case VUT_CITYTILE:
  case VUT_CITYSTATUS:  /* We don't *want* owner of our city to change */
  case VUT_STYLE:
  case VUT_TOPO:
  case VUT_SERVERSETTING:
    return TRUE;
  case VUT_NATION:
  case VUT_NATIONGROUP:
    return (req->range != REQ_RANGE_ALLIANCE);
  case VUT_ADVANCE:
  case VUT_TECHFLAG:
  case VUT_GOVERNMENT:
  case VUT_ACHIEVEMENT:
  case VUT_IMPROVEMENT:
  case VUT_IMPR_GENUS:
  case VUT_MINSIZE:
  case VUT_MINCULTURE:
  case VUT_MINFOREIGNPCT:
  case VUT_MINTECHS:
  case VUT_NATIONALITY:
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
  case VUT_MAXTILEUNITS:
  case VUT_UTYPE:	/* Not sure about this one */
  case VUT_UTFLAG:	/* Not sure about this one */
  case VUT_UCLASS:	/* Not sure about this one */
  case VUT_UCFLAG:	/* Not sure about this one */
  case VUT_MINVETERAN:
  case VUT_UNITSTATE:
  case VUT_ACTIVITY:
  case VUT_MINMOVES:
  case VUT_MINHP:
  case VUT_AGE:
  case VUT_ROADFLAG:
  case VUT_EXTRAFLAG:
  case VUT_MINCALFRAG:  /* cyclically available */
    return FALSE;
  case VUT_TERRAIN:
  case VUT_EXTRA:
  case VUT_GOOD:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_TERRAINALTER:
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

/**********************************************************************//**
  Returns TRUE iff the requirement vector vec contains the requirement
  req.
**************************************************************************/
bool is_req_in_vec(const struct requirement *req,
                   const struct requirement_vector *vec)
{
  requirement_vector_iterate(vec, preq) {
    if (are_requirements_equal(req, preq)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified requirement vector has a positive
  requirement of the specified requirement type.
  @param reqs the requirement vector to look in
  @param kind the requirement type to look for
**************************************************************************/
bool req_vec_wants_type(const struct requirement_vector *reqs,
                        enum universals_n kind)
{
  requirement_vector_iterate(reqs, preq) {
    if (preq->present && preq->source.kind == kind) {
      return TRUE;
    }
  } requirement_vector_iterate_end;
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified universal is known to never be there.
  Note that this function may return FALSE even when it is impossible for
  the universal to appear in the game if that can't be detected.
  @param source the universal to check the absence of.
  @return TRUE iff the universal never will appear.
**************************************************************************/
bool universal_never_there(const struct universal *source)
{
  switch (source->kind) {
  case VUT_ACTION:
    return !action_is_in_use(source->value.action);
  case VUT_UTFLAG:
    return !utype_flag_is_in_use(source->value.unitflag);
  case VUT_UCFLAG:
    return !uclass_flag_is_in_use(source->value.unitclassflag);
  case VUT_EXTRAFLAG:
    return !extra_flag_is_in_use(source->value.extraflag);
  case VUT_OTYPE:
  case VUT_SPECIALIST:
  case VUT_AI_LEVEL:
  case VUT_CITYTILE:
  case VUT_CITYSTATUS:
  case VUT_STYLE:
  case VUT_TOPO:
  case VUT_SERVERSETTING:
  case VUT_NATION:
  case VUT_NATIONGROUP:
  case VUT_ADVANCE:
  case VUT_TECHFLAG:
  case VUT_GOVERNMENT:
  case VUT_ACHIEVEMENT:
  case VUT_IMPROVEMENT:
  case VUT_IMPR_GENUS:
  case VUT_MINSIZE:
  case VUT_MINCULTURE:
  case VUT_MINFOREIGNPCT:
  case VUT_MINTECHS:
  case VUT_NATIONALITY:
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
  case VUT_MAXTILEUNITS:
  case VUT_UTYPE:
  case VUT_UCLASS:
  case VUT_MINVETERAN:
  case VUT_UNITSTATE:
  case VUT_ACTIVITY:
  case VUT_MINMOVES:
  case VUT_MINHP:
  case VUT_AGE:
  case VUT_ROADFLAG:
  case VUT_MINCALFRAG:
  case VUT_TERRAIN:
  case VUT_EXTRA:
  case VUT_GOOD:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_TERRAINALTER:
  case VUT_MINYEAR:
  case VUT_NONE:
  case VUT_COUNT:
    /* Not implemented. */
    break;
  }

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified requirement is known to be impossible to
  fulfill. Note that this function may return FALSE even when it is
  impossible to fulfill a requirement if it can't detect it.
  @param req the requirement to check the possibility of.
  @return TRUE iff the requirement never can be fulfilled.
**************************************************************************/
bool req_is_impossible_to_fulfill(const struct requirement *req)
{
  /* Not known to be impossible to fulfill */
  return req->present && universal_never_there(&req->source);
}

/**********************************************************************//**
  Returns TRUE iff the specified requirement vector is known to be
  impossible to fulfill. Note that this function may return FALSE even when
  it is impossible to fulfill a requirement if it can't detect it.
  @param reqs the requirement vector to check the possibility of.
  @return TRUE iff the requirement vector never can be fulfilled.
**************************************************************************/
bool req_vec_is_impossible_to_fulfill(const struct requirement_vector *reqs)
{
  requirement_vector_iterate(reqs, preq) {
    if (req_is_impossible_to_fulfill(preq)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  /* Not known to be impossible to fulfill */
  return FALSE;
}

/**********************************************************************//**
  Returns the requirement vector number of the specified requirement
  vector in the specified requirement vector.
  @param parent_item the item that may own the vector.
  @param vec the requirement vector to number.
  @return the requirement vector number the vector has in the parent item.
**************************************************************************/
req_vec_num_in_item
req_vec_vector_number(const void *parent_item,
                      const struct requirement_vector *vec)
{
  if (vec) {
    return 0;
  } else {
    return -1;
  }
}

/********************************************************************//**
  Returns a writable pointer to the specified requirement vector in the
  specified requirement vector or NULL if the parent item doesn't have a
  requirement vector with that requirement vector number.
  @param parent_item the item that should have the requirement vector.
  @param number the item's requirement vector number.
  @return a pointer to the specified requirement vector.
************************************************************************/
struct requirement_vector *
req_vec_by_number(const void *parent_item, req_vec_num_in_item number)
{
  fc_assert_ret_val(number == 0, NULL);
  return (struct requirement_vector *)parent_item;
}

/**********************************************************************//**
  Returns the specified requirement vector change as a translated string
  ready for use in the user interface.
  N.B.: The returned string is static, so every call to this function
  overwrites the previous.
  @param change the requirement vector change
  @param namer a function that returns a description of the vector to
               change for the item the vector belongs to.
  @return the specified requirement vector change
**************************************************************************/
const char *req_vec_change_translation(const struct req_vec_change *change,
                                       const requirement_vector_namer namer)
{
  const char *req_vec_description;
  static char buf[MAX_LEN_NAME * 3];

  fc_assert_ret_val(change, NULL);
  fc_assert_ret_val(req_vec_change_operation_is_valid(change->operation),
                    NULL);

  /* Get rid of the previous. */
  buf[0] = '\0';

  if (namer == NULL) {
    /* TRANS: default description of a requirement vector
     * (used in ruledit) */
    req_vec_description = _("the requirement vector");
  } else {
    req_vec_description = namer(change->vector_number);
  }

  switch (change->operation) {
  case RVCO_REMOVE:
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: remove a requirement from a requirement vector
                 * (in ruledit).
                 * The first %s is the operation.
                 * The second %s is the requirement.
                 * The third %s is a description of the requirement vector,
                 * like "actor_reqs" */
                _("%s %s from %s"),
                req_vec_change_operation_name(change->operation),
                req_to_fstring(&change->req),
                req_vec_description);
    break;
  case RVCO_APPEND:
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: append a requirement to a requirement vector
                 * (in ruledit).
                 * The first %s is the operation.
                 * The second %s is the requirement.
                 * The third %s is a description of the requirement vector,
                 * like "actor_reqs" */
                _("%s %s to %s"),
                req_vec_change_operation_name(change->operation),
                req_to_fstring(&change->req),
                req_vec_description);
    break;
  case RVCO_NOOP:
    fc_snprintf(buf, sizeof(buf),
                /* TRANS: do nothing to a requirement vector (in ruledit).
                 * The first %s is a description of the requirement vector,
                 * like "actor_reqs" */
                _("Do nothing to %s"), req_vec_description);
    break;
  }

  return buf;
}

/**********************************************************************//**
  Returns TRUE iff the specified requirement vector modification was
  successfully applied to the specified target requirement vector.
  @param modification the requirement vector change
  @param getter a function that returns a pointer to the requirement
                vector the change should be applied to given a ruleset
                item and the vectors number in the item.
  @param parent_item the item to apply the change to.
  @return if the specified modification was successfully applied
**************************************************************************/
bool req_vec_change_apply(const struct req_vec_change *modification,
                          requirement_vector_by_number getter,
                          const void *parent_item)
{
  struct requirement_vector *target
      = getter(parent_item, modification->vector_number);
  int i = 0;

  switch (modification->operation) {
  case RVCO_APPEND:
    requirement_vector_append(target, modification->req);
    return TRUE;
  case RVCO_REMOVE:
    requirement_vector_iterate(target, preq) {
      if (are_requirements_equal(&modification->req, preq)) {
        requirement_vector_remove(target, i);
        return TRUE;
      }
      i++;
    } requirement_vector_iterate_end;
    return FALSE;
  case RVCO_NOOP:
    return FALSE;
  }

  return FALSE;
}

/**********************************************************************//**
  Returns a new requirement vector problem with the specified number of
  suggested solutions and the specified description. The suggestions are
  added by the caller.
  @param num_suggested_solutions the number of suggested solutions.
  @param description the description of the problem.
  @param description_translated the translated description of the problem.
  @return the new requirement vector problem.
**************************************************************************/
struct req_vec_problem *
req_vec_problem_new_transl(int num_suggested_solutions,
                           const char *description,
                           const char *description_translated)
{
  struct req_vec_problem *out;
  int i;

  out = fc_malloc(sizeof(*out));

  fc_strlcpy(out->description, description, sizeof(out->description));
  fc_strlcpy(out->description_translated, _(description_translated),
             sizeof(out->description_translated));

  out->num_suggested_solutions = num_suggested_solutions;
  out->suggested_solutions = fc_malloc(out->num_suggested_solutions
                                       * sizeof(struct req_vec_change));
  for (i = 0; i < out->num_suggested_solutions; i++) {
    /* No suggestions are ready yet. */
    out->suggested_solutions[i].operation = RVCO_NOOP;
    out->suggested_solutions[i].vector_number = -1;
    out->suggested_solutions[i].req.source.kind = VUT_NONE;
  }

  return out;
}

/**********************************************************************//**
  Returns a new requirement vector problem with the specified number of
  suggested solutions and the specified description. The suggestions are
  added by the caller.
  @param num_suggested_solutions the number of suggested solutions.
  @param descr the description of the problem as a format string
  @return the new requirement vector problem.
**************************************************************************/
struct req_vec_problem *req_vec_problem_new(int num_suggested_solutions,
                                            const char *descr, ...)
{
  char description[500];
  char description_translated[500];
  va_list ap;

  va_start(ap, descr);
  fc_vsnprintf(description, sizeof(description), descr, ap);
  va_end(ap);

  va_start(ap, descr);
  fc_vsnprintf(description_translated, sizeof(description_translated),
               _(descr), ap);
  va_end(ap);

  return req_vec_problem_new_transl(num_suggested_solutions,
                                    description, description_translated);
}

/**********************************************************************//**
  De-allocates resources associated with the given requirement vector
  problem.
  @param issue the no longer needed problem.
**************************************************************************/
void req_vec_problem_free(struct req_vec_problem *issue)
{
  FC_FREE(issue->suggested_solutions);
  issue->num_suggested_solutions = 0;

  FC_FREE(issue);
}

/**********************************************************************//**
  Returns the first self contradiction found in the specified requirement
  vector with suggested solutions or NULL if no contradiction was found.
  It is the responsibility of the caller to free the suggestion when it is
  done with it.
  @param vec the requirement vector to look in.
  @param get_num function that returns the requirement vector's number in
                 the parent item.
  @param parent_item the item that owns the vector.
  @return the first self contradiction found.
**************************************************************************/
struct req_vec_problem *
req_vec_get_first_contradiction(const struct requirement_vector *vec,
                                requirement_vector_number get_num,
                                const void *parent_item)
{
  int i, j;
  req_vec_num_in_item vec_num;

  if (vec == NULL || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return NULL;
  }

  if (get_num == NULL || parent_item == NULL) {
    vec_num = 0;
  } else {
    vec_num = get_num(parent_item, vec);
  }

  /* Look for contradictions */
  for (i = 0; i < requirement_vector_size(vec); i++) {
    struct requirement *preq = requirement_vector_get(vec, i);
    for (j = 0; j < requirement_vector_size(vec); j++) {
      struct requirement *nreq = requirement_vector_get(vec, j);

      if (are_requirements_contradictions(preq, nreq)) {
        struct req_vec_problem *problem;

        problem = req_vec_problem_new(2,
            N_("Requirements {%s} and {%s} contradict each other."),
            req_to_fstring(preq), req_to_fstring(nreq));

        /* The solution is to remove one of the contradictions. */
        problem->suggested_solutions[0].operation = RVCO_REMOVE;
        problem->suggested_solutions[0].vector_number = vec_num;
        problem->suggested_solutions[0].req = *preq;

        problem->suggested_solutions[1].operation = RVCO_REMOVE;
        problem->suggested_solutions[1].vector_number = vec_num;
        problem->suggested_solutions[1].req = *nreq;

        /* Only the first contradiction is reported. */
        return problem;
      }
    }
  }

  return NULL;
}

/**********************************************************************//**
  Returns a suggestion to fix the specified requirement vector or NULL if
  no fix is found to be needed. It is the responsibility of the caller to
  free the suggestion when it is done with it.
  @param vec the requirement vector to look in.
  @param get_num function that returns the requirement vector's number in
                 the parent item.
  @param parent_item the item that owns the vector.
  @return the first error found.
**************************************************************************/
struct req_vec_problem *
req_vec_suggest_repair(const struct requirement_vector *vec,
                       requirement_vector_number get_num,
                       const void *parent_item)
{
  /* Check for self contradictins. */
  return req_vec_get_first_contradiction(vec, get_num, parent_item);
}

/**********************************************************************//**
  Returns the first universal known to always be absent in the specified
  requirement vector with suggested solutions or NULL if no missing
  universals were found.
  It is the responsibility of the caller to free the suggestion when it is
  done with it.
  @param vec the requirement vector to look in.
  @param get_num function that returns the requirement vector's number in
                 the parent item.
  @param parent_item the item that owns the vector.
  @return the first missing universal found.
**************************************************************************/
struct req_vec_problem *
req_vec_get_first_missing_univ(const struct requirement_vector *vec,
                               requirement_vector_number get_num,
                               const void *parent_item)
{
  int i;
  req_vec_num_in_item vec_num;

  if (vec == NULL || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return NULL;
  }

  if (get_num == NULL || parent_item == NULL) {
    vec_num = 0;
  } else {
    vec_num = get_num(parent_item, vec);
  }

  /* Look for contradictions */
  for (i = 0; i < requirement_vector_size(vec); i++) {
    struct requirement *preq = requirement_vector_get(vec, i);

    if (universal_never_there(&preq->source)) {
      struct req_vec_problem *problem;

      if (preq->present) {
        /* The requirement vector can never be fulfilled. Removing the
         * requirement makes it possible to fulfill it. This is a rule
         * change and shouldn't be "fixed" without thinking. Don't offer any
         * automatic solution to prevent mindless "fixes". */
        /* TRANS: ruledit warns a user about an unused requirement vector
         * that never can be fulfilled because it asks for something that
         * never will be there. */
        req_vec_problem_new(0,
                  N_("Requirement {%s} requires %s but it will never be"
                     " there."),
                  req_to_fstring(preq), universal_rule_name(&preq->source));
        continue;
      }

      problem = req_vec_problem_new(1,
          N_("Requirement {%s} mentions %s but it will never be there."),
          req_to_fstring(preq), universal_rule_name(&preq->source));

      /* The solution is to remove the reference to the missing
       * universal. */
      problem->suggested_solutions[0].operation = RVCO_REMOVE;
      problem->suggested_solutions[0].vector_number = vec_num;
      problem->suggested_solutions[0].req = *preq;

      /* Only the first missing universal is reported. */
      return problem;
    }
  }

  return NULL;
}

/**********************************************************************//**
  Returns the first redundant requirement in the specified requirement
  vector with suggested solutions or NULL if no redundant requirements were
  found.
  It is the responsibility of the caller to free the suggestion when it is
  done with it.
  @param vec the requirement vector to look in.
  @param get_num function that returns the requirement vector's number in
                 the parent item.
  @param parent_item the item that owns the vector.
  @return the first redundant requirement found.
**************************************************************************/
struct req_vec_problem *
req_vec_get_first_redundant_req(const struct requirement_vector *vec,
                                requirement_vector_number get_num,
                                const void *parent_item)
{
  int i, j;
  req_vec_num_in_item vec_num;

  if (vec == NULL || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return NULL;
  }

  if (get_num == NULL || parent_item == NULL) {
    vec_num = 0;
  } else {
    vec_num = get_num(parent_item, vec);
  }

  /* Look for repeated requirements */
  for (i = 0; i < requirement_vector_size(vec) - 1; i++) {
    struct requirement *preq = requirement_vector_get(vec, i);
    for (j = i + 1; j < requirement_vector_size(vec); j++) {
      struct requirement *nreq = requirement_vector_get(vec, j);

      if (are_requirements_equal(preq, nreq)) {
        struct req_vec_problem *problem;

        problem = req_vec_problem_new(2,
            N_("Requirements {%s} and {%s} are the same."),
            req_to_fstring(preq), req_to_fstring(nreq));

        /* The solution is to remove one of the redundant requirements. */
        problem->suggested_solutions[0].operation = RVCO_REMOVE;
        problem->suggested_solutions[0].vector_number = vec_num;
        problem->suggested_solutions[0].req = *preq;

        problem->suggested_solutions[1].operation = RVCO_REMOVE;
        problem->suggested_solutions[1].vector_number = vec_num;
        problem->suggested_solutions[1].req = *nreq;

        /* Only the first redundancy is reported. */
        return problem;
      }
    }
  }

  return NULL;
}

/**********************************************************************//**
  Returns a suggestion to improve the specified requirement vector or NULL
  if nothing to improve is found. It is the responsibility of the caller to
  free the suggestion when it is done with it. A possible improvement isn't
  always an error.
  @param vec the requirement vector to look in.
  @param get_num function that returns the requirement vector's number in
                 the parent item.
  @param parent_item the item that owns the vector.
  @return the first improvement suggestion found.
**************************************************************************/
struct req_vec_problem *
req_vec_suggest_improvement(const struct requirement_vector *vec,
                            requirement_vector_number get_num,
                            const void *parent_item)
{
  struct req_vec_problem *out;

  out = req_vec_suggest_repair(vec, get_num, parent_item);
  if (out != NULL) {
    /* A bug, not just a potential improvement */
    return out;
  }

  /* Check if a universal that never will appear in the game is checked. */
  out = req_vec_get_first_missing_univ(vec, get_num, parent_item);
  if (out != NULL) {
    return out;
  }

  /* Check if a requirement is redundant. */
  out = req_vec_get_first_redundant_req(vec, get_num, parent_item);
  return out;
}

/**********************************************************************//**
  Return TRUE iff the two sources are equivalent.  Note this isn't the
  same as an == or memcmp check.
***************************************************************************/
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
  case VUT_TECHFLAG:
    return psource1->value.techflag == psource2->value.techflag;
  case VUT_GOVERNMENT:
    return psource1->value.govern == psource2->value.govern;
  case VUT_ACHIEVEMENT:
    return psource1->value.achievement == psource2->value.achievement;
  case VUT_STYLE:
    return psource1->value.style == psource2->value.style;
  case VUT_IMPROVEMENT:
    return psource1->value.building == psource2->value.building;
  case VUT_IMPR_GENUS:
    return psource1->value.impr_genus == psource2->value.impr_genus;
  case VUT_EXTRA:
    return psource1->value.extra == psource2->value.extra;
  case VUT_GOOD:
    return psource1->value.good == psource2->value.good;
  case VUT_TERRAIN:
    return psource1->value.terrain == psource2->value.terrain;
  case VUT_TERRFLAG:
    return psource1->value.terrainflag == psource2->value.terrainflag;
  case VUT_NATION:
    return psource1->value.nation == psource2->value.nation;
  case VUT_NATIONGROUP:
    return psource1->value.nationgroup == psource2->value.nationgroup;
  case VUT_NATIONALITY:
    return psource1->value.nationality == psource2->value.nationality;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    return psource1->value.diplrel == psource2->value.diplrel;
  case VUT_UTYPE:
    return psource1->value.utype == psource2->value.utype;
  case VUT_UTFLAG:
    return psource1->value.unitflag == psource2->value.unitflag;
  case VUT_UCLASS:
    return psource1->value.uclass == psource2->value.uclass;
  case VUT_UCFLAG:
    return psource1->value.unitclassflag == psource2->value.unitclassflag;
  case VUT_MINVETERAN:
    return psource1->value.minveteran == psource2->value.minveteran;
  case VUT_UNITSTATE:
    return psource1->value.unit_state == psource2->value.unit_state;
  case VUT_ACTIVITY:
    return psource1->value.activity == psource2->value.activity;
  case VUT_MINMOVES:
    return psource1->value.minmoves == psource2->value.minmoves;
  case VUT_MINHP:
    return psource1->value.min_hit_points == psource2->value.min_hit_points;
  case VUT_AGE:
    return psource1->value.age == psource2->value.age;
  case VUT_MINTECHS:
    return psource1->value.min_techs == psource2->value.min_techs;
  case VUT_ACTION:
    return (action_number(psource1->value.action)
            == action_number(psource2->value.action));
  case VUT_OTYPE:
    return psource1->value.outputtype == psource2->value.outputtype;
  case VUT_SPECIALIST:
    return psource1->value.specialist == psource2->value.specialist;
  case VUT_MINSIZE:
    return psource1->value.minsize == psource2->value.minsize;
  case VUT_MINCULTURE:
    return psource1->value.minculture == psource2->value.minculture;
  case VUT_MINFOREIGNPCT:
    return psource1->value.minforeignpct == psource2->value.minforeignpct;
  case VUT_AI_LEVEL:
    return psource1->value.ai_level == psource2->value.ai_level;
  case VUT_MAXTILEUNITS:
    return psource1->value.max_tile_units == psource2->value.max_tile_units;
  case VUT_TERRAINCLASS:
    return psource1->value.terrainclass == psource2->value.terrainclass;
  case VUT_ROADFLAG:
    return psource1->value.roadflag == psource2->value.roadflag;
  case VUT_EXTRAFLAG:
    return psource1->value.extraflag == psource2->value.extraflag;
  case VUT_MINYEAR:
    return psource1->value.minyear == psource2->value.minyear;
  case VUT_MINCALFRAG:
    return psource1->value.mincalfrag == psource2->value.mincalfrag;
  case VUT_TOPO:
    return psource1->value.topo_property == psource2->value.topo_property;
  case VUT_SERVERSETTING:
    return psource1->value.ssetval == psource2->value.ssetval;
  case VUT_TERRAINALTER:
    return psource1->value.terrainalter == psource2->value.terrainalter;
  case VUT_CITYTILE:
    return psource1->value.citytile == psource2->value.citytile;
  case VUT_CITYSTATUS:
    return psource1->value.citystatus == psource2->value.citystatus;
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource1->kind);
  return FALSE;
}

/**********************************************************************//**
  Return the (untranslated) rule name of the universal.
  You don't have to free the return pointer.
***************************************************************************/
const char *universal_rule_name(const struct universal *psource)
{
  static char buffer[10];

  switch (psource->kind) {
  case VUT_NONE:
    return "(none)";
  case VUT_CITYTILE:
    return citytile_type_name(psource->value.citytile);
  case VUT_CITYSTATUS:
    return citystatus_type_name(psource->value.citystatus);
  case VUT_MINYEAR:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minyear);

    return buffer;
  case VUT_MINCALFRAG:
    /* Rule name is 0-based number, not pretty name from ruleset */
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.mincalfrag);

    return buffer;
  case VUT_TOPO:
    return topo_flag_name(psource->value.topo_property);
  case VUT_SERVERSETTING:
    return ssetv_rule_name(psource->value.ssetval);
  case VUT_ADVANCE:
    return advance_rule_name(psource->value.advance);
  case VUT_TECHFLAG:
    return tech_flag_id_name(psource->value.techflag);
  case VUT_GOVERNMENT:
    return government_rule_name(psource->value.govern);
  case VUT_ACHIEVEMENT:
    return achievement_rule_name(psource->value.achievement);
  case VUT_STYLE:
    return style_rule_name(psource->value.style);
  case VUT_IMPROVEMENT:
    return improvement_rule_name(psource->value.building);
  case VUT_IMPR_GENUS:
    return impr_genus_id_name(psource->value.impr_genus);
  case VUT_EXTRA:
    return extra_rule_name(psource->value.extra);
  case VUT_GOOD:
    return goods_rule_name(psource->value.good);
  case VUT_TERRAIN:
    return terrain_rule_name(psource->value.terrain);
  case VUT_TERRFLAG:
    return terrain_flag_id_name(psource->value.terrainflag);
  case VUT_NATION:
    return nation_rule_name(psource->value.nation);
  case VUT_NATIONGROUP:
    return nation_group_rule_name(psource->value.nationgroup);
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    return diplrel_rule_name(psource->value.diplrel);
  case VUT_NATIONALITY:
    return nation_rule_name(psource->value.nationality);
  case VUT_UTYPE:
    return utype_rule_name(psource->value.utype);
  case VUT_UTFLAG:
    return unit_type_flag_id_name(psource->value.unitflag);
  case VUT_UCLASS:
    return uclass_rule_name(psource->value.uclass);
  case VUT_UCFLAG:
    return unit_class_flag_id_name(psource->value.unitclassflag);
  case VUT_MINVETERAN:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minveteran);

    return buffer;
  case VUT_UNITSTATE:
    return ustate_prop_name(psource->value.unit_state);
  case VUT_ACTIVITY:
    return unit_activity_name(psource->value.activity);
  case VUT_MINMOVES:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minmoves);

    return buffer;
  case VUT_MINHP:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.min_hit_points);

    return buffer;
  case VUT_AGE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.age);

    return buffer;
  case VUT_MINTECHS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.min_techs);

    return buffer;
  case VUT_ACTION:
    return action_rule_name(psource->value.action);
  case VUT_OTYPE:
    return get_output_identifier(psource->value.outputtype);
  case VUT_SPECIALIST:
    return specialist_rule_name(psource->value.specialist);
  case VUT_MINSIZE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minsize);

    return buffer;
  case VUT_MINCULTURE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minculture);

    return buffer;
  case VUT_MINFOREIGNPCT:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minforeignpct);

    return buffer;
  case VUT_AI_LEVEL:
    return ai_level_name(psource->value.ai_level);
  case VUT_MAXTILEUNITS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.max_tile_units);
    return buffer;
  case VUT_TERRAINCLASS:
    return terrain_class_name(psource->value.terrainclass);
  case VUT_ROADFLAG:
    return road_flag_id_name(psource->value.roadflag);
  case VUT_EXTRAFLAG:
    return extra_flag_id_name(psource->value.extraflag);
  case VUT_TERRAINALTER:
    return terrain_alteration_name(psource->value.terrainalter);
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource->kind);
  return NULL;
}

/**********************************************************************//**
  Make user-friendly text for the source.  The text is put into a user
  buffer which is also returned.
  This should be short, as it's used in lists like "Aqueduct+Size 8" when
  explaining a calculated value. It just needs to be enough to remind the
  player of rules they already know, not a complete explanation (use
  insert_requirement() for that).
**************************************************************************/
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
  case VUT_TECHFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" tech"),
                 tech_flag_id_translated_name(psource->value.techflag));
    return buf;
  case VUT_GOVERNMENT:
    fc_strlcat(buf, government_name_translation(psource->value.govern),
               bufsz);
    return buf;
  case VUT_ACHIEVEMENT:
    fc_strlcat(buf, achievement_name_translation(psource->value.achievement),
               bufsz);
    return buf;
  case VUT_STYLE:
    fc_strlcat(buf, style_name_translation(psource->value.style),
               bufsz);
    return buf;
  case VUT_IMPROVEMENT:
    fc_strlcat(buf, improvement_name_translation(psource->value.building),
               bufsz);
    return buf;
  case VUT_IMPR_GENUS:
    fc_strlcat(buf,
               impr_genus_id_translated_name(psource->value.impr_genus),
               bufsz);
    return buf;
  case VUT_EXTRA:
    fc_strlcat(buf, extra_name_translation(psource->value.extra), bufsz);
    return buf;
  case VUT_GOOD:
    fc_strlcat(buf, goods_name_translation(psource->value.good), bufsz);
    return buf;
  case VUT_TERRAIN:
    fc_strlcat(buf, terrain_name_translation(psource->value.terrain), bufsz);
    return buf;
  case VUT_NATION:
    fc_strlcat(buf, nation_adjective_translation(psource->value.nation),
               bufsz);
    return buf;
  case VUT_NATIONGROUP:
    fc_strlcat(buf, nation_group_name_translation(psource->value.nationgroup),
               bufsz);
    return buf;
  case VUT_NATIONALITY:
    cat_snprintf(buf, bufsz, _("%s citizens"),
                 nation_adjective_translation(psource->value.nationality));
    return buf;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    fc_strlcat(buf, diplrel_name_translation(psource->value.diplrel),
               bufsz);
    return buf;
  case VUT_UTYPE:
    fc_strlcat(buf, utype_name_translation(psource->value.utype), bufsz);
    return buf;
  case VUT_UTFLAG:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Unit type flag */
                 Q_("?utflag:\"%s\" units"),
                 unit_type_flag_id_translated_name(
                   psource->value.unitflag));
    return buf;
  case VUT_UCLASS:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Unit class */
                 _("%s units"),
		 uclass_name_translation(psource->value.uclass));
    return buf;
  case VUT_UCFLAG:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Unit class flag */
                 Q_("?ucflag:\"%s\" units"),
                 unit_class_flag_id_translated_name(
                   psource->value.unitclassflag));
    return buf;
  case VUT_MINVETERAN:
    /* FIXME */
    cat_snprintf(buf, bufsz, _("Veteran level >=%d"),
		 psource->value.minveteran);
    return buf;
  case VUT_UNITSTATE:
    switch (psource->value.unit_state) {
    case USP_TRANSPORTED:
      /* TRANS: unit state. (appears in strings like "Missile+Transported") */
      cat_snprintf(buf, bufsz, _("Transported"));
      break;
    case USP_LIVABLE_TILE:
      cat_snprintf(buf, bufsz,
                   /* TRANS: unit state. (appears in strings like
                    * "Missile+On livable tile") */
                   _("On livable tile"));
      break;
    case USP_TRANSPORTING:
      /* TRANS: unit state. (appears in strings like "Missile+Transported") */
      cat_snprintf(buf, bufsz, _("Transporting"));
      break;
    case USP_HAS_HOME_CITY:
      /* TRANS: unit state. (appears in strings like "Missile+Has a home city") */
      cat_snprintf(buf, bufsz, _("Has a home city"));
      break;
    case USP_NATIVE_TILE:
      cat_snprintf(buf, bufsz,
                   /* TRANS: unit state. (appears in strings like
                    * "Missile+On native tile") */
                   _("On native tile"));
      break;
    case USP_NATIVE_EXTRA:
      cat_snprintf(buf, bufsz,
                   /* TRANS: unit state. (appears in strings like
                    * "Missile+In native extra") */
                   _("In native extra"));
      break;
    case USP_MOVED_THIS_TURN:
      /* TRANS: unit state. (appears in strings like
       * "Missile+Has moved this turn") */
      cat_snprintf(buf, bufsz, _("Has moved this turn"));
      break;
    case USP_COUNT:
      fc_assert_msg(psource->value.unit_state != USP_COUNT,
                    "Invalid unit state property.");
      break;
    }
    return buf;
  case VUT_ACTIVITY:
    cat_snprintf(buf, bufsz, _("%s activity"),
                 _(unit_activity_name(psource->value.activity)));
    return buf;
  case VUT_MINMOVES:
    /* TRANS: Minimum unit movement points left for requirement to be met
     * (%s is a string like "1" or "2 1/3") */
    cat_snprintf(buf, bufsz, _("%s MP"),
                 move_points_text(psource->value.minmoves, TRUE));
    return buf;
  case VUT_MINHP:
    /* TRANS: HP = hit points */
    cat_snprintf(buf, bufsz, _("%d HP"),
                 psource->value.min_hit_points);
    return buf;
  case VUT_AGE:
    cat_snprintf(buf, bufsz, _("Age %d"),
                 psource->value.age);
    return buf;
  case VUT_MINTECHS:
    cat_snprintf(buf, bufsz, _("%d Techs"),
                 psource->value.min_techs);
    return buf;
  case VUT_ACTION:
    fc_strlcat(buf, action_name_translation(psource->value.action),
               bufsz);
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
  case VUT_MINCULTURE:
    cat_snprintf(buf, bufsz, _("Culture %d"),
		 psource->value.minculture);
    return buf;
  case VUT_MINFOREIGNPCT:
    cat_snprintf(buf, bufsz, _("%d%% Foreigners"),
		 psource->value.minforeignpct);
    return buf;
  case VUT_AI_LEVEL:
    /* TRANS: "Hard AI" */
    cat_snprintf(buf, bufsz, _("%s AI"),
                 ai_level_translated_name(psource->value.ai_level)); /* FIXME */
    return buf;
  case VUT_MAXTILEUNITS:
    /* TRANS: here <= means 'less than or equal' */
    cat_snprintf(buf, bufsz, PL_("<=%d unit",
                                 "<=%d units", psource->value.max_tile_units),
                 psource->value.max_tile_units);
    return buf;
  case VUT_TERRAINCLASS:
    /* TRANS: Terrain class: "Land terrain" */
    cat_snprintf(buf, bufsz, _("%s terrain"),
                 terrain_class_name_translation(psource->value.terrainclass));
    return buf;
  case VUT_TERRFLAG:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Terrain flag */
                 Q_("?terrflag:\"%s\" terrain"),
                 terrain_flag_id_translated_name(
                   psource->value.terrainflag));
    return buf;
  case VUT_ROADFLAG:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Road flag */
                 Q_("?roadflag:\"%s\" road"),
                 road_flag_id_translated_name(psource->value.roadflag));
    return buf;
  case VUT_EXTRAFLAG:
    cat_snprintf(buf, bufsz,
                 /* TRANS: Extra flag */
                 Q_("?extraflag:\"%s\" extra"),
                 extra_flag_id_translated_name(psource->value.extraflag));
    return buf;
  case VUT_MINYEAR:
    cat_snprintf(buf, bufsz, _("After %s"),
                 textyear(psource->value.minyear));
    return buf;
  case VUT_MINCALFRAG:
    /* TRANS: here >= means 'greater than or equal'.
     * %s identifies a calendar fragment (may be bare number). */
    cat_snprintf(buf, bufsz, _(">=%s"),
                 textcalfrag(psource->value.mincalfrag));
    return buf;
  case VUT_TOPO:
    /* TRANS: topology flag name ("WrapX", "ISO", etc) */
    cat_snprintf(buf, bufsz, _("%s map"),
                 _(topo_flag_name(psource->value.topo_property)));
    return buf;
  case VUT_SERVERSETTING:
    fc_strlcat(buf, ssetv_human_readable(psource->value.ssetval, TRUE),
               bufsz);
    return buf;
  case VUT_TERRAINALTER:
    /* TRANS: "Irrigation possible" */
    cat_snprintf(buf, bufsz, _("%s possible"),
                 Q_(terrain_alteration_name(psource->value.terrainalter)));
    return buf;
  case VUT_CITYTILE:
    switch (psource->value.citytile) {
    case CITYT_CENTER:
      fc_strlcat(buf, _("City center"), bufsz);
      break;
    case CITYT_CLAIMED:
      fc_strlcat(buf, _("Tile claimed"), bufsz);
      break;
    case CITYT_EXTRAS_OWNED:
      fc_strlcat(buf, _("Extras owned"), bufsz);
      break;
    case CITYT_LAST:
      fc_assert(psource->value.citytile != CITYT_LAST);
      fc_strlcat(buf, "error", bufsz);
      break;
    }
    return buf;
  case VUT_CITYSTATUS:
    switch (psource->value.citystatus) {
    case CITYS_OWNED_BY_ORIGINAL:
      fc_strlcat(buf, _("Owned by original"), bufsz);
      break;
    case CITYS_LAST:
      fc_assert(psource->value.citystatus != CITYS_LAST);
      fc_strlcat(buf, "error", bufsz);
      break;
    }
    return buf;
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource->kind);
  return buf;
}

/**********************************************************************//**
  Return untranslated name of the universal source name.
**************************************************************************/
const char *universal_type_rule_name(const struct universal *psource)
{
  return universals_n_name(psource->kind);
}

/**********************************************************************//**
  Return the number of shields it takes to build this universal.
**************************************************************************/
int universal_build_shield_cost(const struct city *pcity,
                                const struct universal *target)
{
  switch (target->kind) {
  case VUT_IMPROVEMENT:
    return impr_build_shield_cost(pcity, target->value.building);
  case VUT_UTYPE:
    return utype_build_shield_cost(pcity, NULL, target->value.utype);
  default:
    break;
  }
  return FC_INFINITY;
}

/**********************************************************************//**
  Replaces all instances of the universal to_replace with replacement in
  the requirement vector reqs and returns TRUE iff any requirements were
  replaced.
**************************************************************************/
bool universal_replace_in_req_vec(struct requirement_vector *reqs,
                                  const struct universal *to_replace,
                                  const struct universal *replacement)
{
  bool changed = FALSE;

  requirement_vector_iterate(reqs, preq) {
    if (universal_is_mentioned_by_requirement(preq, to_replace)) {
      preq->source = *replacement;
      changed = TRUE;
    }
  } requirement_vector_iterate_end;

  return changed;
}

/**********************************************************************//**
  Returns TRUE iff the universal 'psource' is directly mentioned by any of
  the requirements in 'reqs'.
**************************************************************************/
bool universal_is_mentioned_by_requirements(
    const struct requirement_vector *reqs,
    const struct universal *psource)
{
  requirement_vector_iterate(reqs, preq) {
    if (universal_is_mentioned_by_requirement(preq, psource)) {
      return TRUE;
    }
  } requirement_vector_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the presence of any of the specified universals is
  enough to guarantee that the specified requirement vector never will be
  fulfilled.
  @param reqs   the requirement vector that never should be fulfilled
  @param unis   the universals that are present
  @param n_unis the number of universals in unis
  @return if the universals blocks fulfillment of the req vector
**************************************************************************/
bool universals_mean_unfulfilled(struct requirement_vector *reqs,
                                 struct universal *unis,
                                 size_t n_unis)
{
  int i;

  for (i = 0; i < n_unis; i++) {
    if (!universal_fulfills_requirements(FALSE, reqs, &unis[i])) {
      /* This universal makes it impossible to fulfill the specified
       * requirement vector */
      return TRUE;
    }
  }

  /* No specified universal is known to guarantee that the requirement
   * vector never will be fulfilled. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the presence of the specified universals is enough to
  know if the specified requirement vector is fulfilled. This means that
  the requirement vector can't check anything it can't find in the listed
  universals.
  Note that TRUE is returned both when the requirement vector is known to
  be fulfilled and when it is known to be unfulfilled.
  @param reqs   the requirement vector certainty is wanted about
  @param unis   the universals that are present
  @param n_unis the number of universals in unis
  @return TRUE iff the specified universals is everything required to find
               out whether the specified requirement vector is fulfilled or
               not
**************************************************************************/
bool universals_say_everything(struct requirement_vector *reqs,
                               struct universal *unis,
                               size_t n_unis)
{
  requirement_vector_iterate(reqs, preq) {
    int i;
    bool req_mentioned_a_source = FALSE;

    for (i = 0; i < n_unis; i++) {
      switch (universal_fulfills_requirement(preq, &(unis[i]))) {
      case ITF_NO:
      case ITF_YES:
        /* this req matched this source */
        req_mentioned_a_source = TRUE;
        break;
      case ITF_NOT_APPLICABLE:
        /* Not a mention. */
        break;
      }
    }

    if (!req_mentioned_a_source) {
      /* A requirement not relevant to any of the specified universals was
       * found in the requirement vector. */
      return FALSE;
    }
  } requirement_vector_iterate_end;

  /* No requirement not relevant to any of the specified universals was
   * found in the requirement vector. */
  return TRUE;
}

/**********************************************************************//**
  Will the universal 'source' fulfill this requirement?
**************************************************************************/
enum req_item_found
universal_fulfills_requirement(const struct requirement *preq,
                               const struct universal *source)
{
  fc_assert_ret_val_msg(universal_found_function[source->kind],
                        ITF_NOT_APPLICABLE,
                        "No req item found function for %s",
                        universal_type_rule_name(source));

  return (*universal_found_function[source->kind])(preq, source);
}

/**********************************************************************//**
  Will the universal 'source' fulfill the requirements in the list?
  If 'check_necessary' is FALSE: are there no requirements that 'source'
    would actively prevent the fulfilment of?
  If 'check_necessary' is TRUE: does 'source' help the requirements to be
    fulfilled? (NB 'source' might not be the only source of its type that
    would be sufficient; for instance, if 'source' is a specific terrain
    type, we can return TRUE even if the requirement is only for something
    vague like a TerrainClass.)
**************************************************************************/
bool universal_fulfills_requirements(bool check_necessary,
                                     const struct requirement_vector *reqs,
                                     const struct universal *source)
{
  bool necessary = FALSE;

  fc_assert_ret_val_msg(universal_found_function[source->kind],
                        !check_necessary,
                        "No req item found function for %s",
                        universal_type_rule_name(source));

  requirement_vector_iterate(reqs, preq) {
    switch ((*universal_found_function[source->kind])(preq, source)) {
    case ITF_NOT_APPLICABLE:
      continue;
    case ITF_NO:
      if (preq->present) {
        return FALSE;
      }
      break;
    case ITF_YES:
      if (preq->present) {
        necessary = TRUE;
      } else {
        return FALSE;
      }
      break;
    }
  } requirement_vector_iterate_end;

  return (!check_necessary || necessary);
}

/**********************************************************************//**
  Returns TRUE iff the specified universal is relevant to fulfilling the
  specified requirement.
**************************************************************************/
bool universal_is_relevant_to_requirement(const struct requirement *req,
                                          const struct universal *source)
{
  switch (universal_fulfills_requirement(req, source)) {
  case ITF_NOT_APPLICABLE:
    return FALSE;
  case ITF_NO:
  case ITF_YES:
    return TRUE;
  }

  log_error("Unhandled item_found value");
  return FALSE;
}

/**********************************************************************//**
  Find if a nation fulfills a requirement
**************************************************************************/
static enum req_item_found nation_found(const struct requirement *preq,
                                        const struct universal *source)
{
  fc_assert(source->value.nation);

  switch (preq->source.kind) {
  case VUT_NATION:
    return preq->source.value.nation == source->value.nation ? ITF_YES
                                                             : ITF_NO;
  case VUT_NATIONGROUP:
    return nation_is_in_group(source->value.nation,
                              preq->source.value.nationgroup) ? ITF_YES
                                                              : ITF_NO;
  default:
    break;
  }

  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if a government fulfills a requirement
**************************************************************************/
static enum req_item_found government_found(const struct requirement *preq,
                                            const struct universal *source)
{
  fc_assert(source->value.govern);

  if (preq->source.kind == VUT_GOVERNMENT) {
    return preq->source.value.govern == source->value.govern ? ITF_YES
                                                             : ITF_NO;
  }

  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if an improvement fulfills a requirement
**************************************************************************/
static enum req_item_found improvement_found(const struct requirement *preq,
                                             const struct universal *source)
{
  fc_assert(source->value.building);

  /* We only ever return ITF_YES, because requiring a different
   * improvement does not mean that the improvement under consideration
   * cannot fulfill the requirements. This is necessary to allow
   * requirement vectors to specify multiple required improvements. */

  switch (preq->source.kind) {
  case VUT_IMPROVEMENT:
    if (source->value.building == preq->source.value.building) {
      return ITF_YES;
    }
    break;
  case VUT_IMPR_GENUS:
    if (source->value.building->genus == preq->source.value.impr_genus) {
      return ITF_YES;
    }
    break;
  default:
    break;
  }

  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if a unit class fulfills a requirement
**************************************************************************/
static enum req_item_found unit_class_found(const struct requirement *preq,
                                            const struct universal *source)
{
  fc_assert(source->value.uclass);

  switch (preq->source.kind) {
  case VUT_UCLASS:
    return source->value.uclass == preq->source.value.uclass ? ITF_YES
                                                             : ITF_NO;
  case VUT_UCFLAG:
    return uclass_has_flag(source->value.uclass,
                           preq->source.value.unitclassflag) ? ITF_YES
                                                             : ITF_NO;

  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  };
}

/**********************************************************************//**
  Find if a unit type fulfills a requirement
**************************************************************************/
static enum req_item_found unit_type_found(const struct requirement *preq,
                                           const struct universal *source)
{
  fc_assert(source->value.utype);

  switch (preq->source.kind) {
  case VUT_UTYPE:
    return source->value.utype == preq->source.value.utype ? ITF_YES : ITF_NO;
  case VUT_UCLASS:
    return utype_class(source->value.utype) == preq->source.value.uclass
                                            ? ITF_YES : ITF_NO;
  case VUT_UTFLAG:
    return utype_has_flag(source->value.utype,
                          preq->source.value.unitflag) ? ITF_YES : ITF_NO;
  case VUT_UCFLAG:
    return uclass_has_flag(utype_class(source->value.utype),
                           preq->source.value.unitclassflag) ? ITF_YES
                                                             : ITF_NO;
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  };
}

/**********************************************************************//**
  Find if a unit activity fulfills a requirement
**************************************************************************/
static enum req_item_found
unit_activity_found(const struct requirement *preq,
                    const struct universal *source)
{
  fc_assert_ret_val(unit_activity_is_valid(source->value.activity),
                    ITF_NOT_APPLICABLE);

  switch (preq->source.kind) {
  case VUT_ACTIVITY:
    return source->value.activity == preq->source.value.activity ? ITF_YES
                                                                 : ITF_NO;
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  };
}

/**********************************************************************//**
  Find if a terrain type fulfills a requirement
**************************************************************************/
static enum req_item_found terrain_type_found(const struct requirement *preq,
                                              const struct universal *source)
{
  fc_assert(source->value.terrain);

  switch (preq->source.kind) {
  case VUT_TERRAIN:
    return source->value.terrain == preq->source.value.terrain ? ITF_YES : ITF_NO;
  case VUT_TERRAINCLASS:
    return terrain_type_terrain_class(source->value.terrain) == preq->source.value.terrainclass
      ? ITF_YES : ITF_NO;
  case VUT_TERRFLAG:
    return terrain_has_flag(source->value.terrain,
                            preq->source.value.terrainflag) ? ITF_YES : ITF_NO;
  case VUT_TERRAINALTER:
    return (terrain_can_support_alteration(source->value.terrain,
                                           preq->source.value.terrainalter)
            ? ITF_YES : ITF_NO);
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  };
}

/**********************************************************************//**
  Find if a tile state fulfills a requirement
**************************************************************************/
static enum req_item_found city_tile_found(const struct requirement *preq,
                                           const struct universal *source)
{
  fc_assert_ret_val(citytile_type_is_valid(source->value.citytile),
                    ITF_NOT_APPLICABLE);

  switch (preq->source.kind) {
  case VUT_CITYTILE:
    return (source->value.citytile == preq->source.value.citytile
            ? ITF_YES
            /* The presence of one tile state doesn't block another */
            : ITF_NOT_APPLICABLE);
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  };
}

/**********************************************************************//**
  Find if an extra type fulfills a requirement
**************************************************************************/
static enum req_item_found extra_type_found(const struct requirement *preq,
                                            const struct universal *source)
{
  fc_assert(source->value.extra);

  switch (preq->source.kind) {
  case VUT_EXTRA:
    return source->value.extra == preq->source.value.extra ? ITF_YES : ITF_NO;
  case VUT_EXTRAFLAG:
    return extra_has_flag(source->value.extra,
                          preq->source.value.extraflag) ? ITF_YES : ITF_NO;
  case VUT_ROADFLAG:
    {
      struct road_type *r = extra_road_get(source->value.extra);
      return r && road_has_flag(r, preq->source.value.roadflag)
        ? ITF_YES : ITF_NO;
    }
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  }
}

/**********************************************************************//**
  Find if an action fulfills a requirement
**************************************************************************/
static enum req_item_found action_found(const struct requirement *preq,
                                        const struct universal *source)
{
  fc_assert(source->value.action);

  if (preq->source.kind == VUT_ACTION) {
    return preq->source.value.action == source->value.action ? ITF_YES
                                                             : ITF_NO;
  }

  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if a diplrel fulfills a requirement
**************************************************************************/
static enum req_item_found diplrel_found(const struct requirement *preq,
                                         const struct universal *source)
{
  fc_assert_ret_val((source->kind == VUT_DIPLREL
                     || source->kind == VUT_DIPLREL_TILE
                     || source->kind == VUT_DIPLREL_TILE_O
                     || source->kind == VUT_DIPLREL_UNITANY
                     || source->kind == VUT_DIPLREL_UNITANY_O),
                    ITF_NOT_APPLICABLE);

  if (preq->source.kind == source->kind) {
    if (preq->source.value.diplrel == source->value.diplrel) {
      /* The diplrel itself. */
      return ITF_YES;
    }
    if (preq->source.value.diplrel == DRO_FOREIGN
        && source->value.diplrel < DS_LAST) {
      /* All diplstate_type values are to foreigners. */
      return ITF_YES;
    }
    if (preq->source.value.diplrel == DRO_HOSTS_EMBASSY
        && source->value.diplrel == DRO_HOSTS_REAL_EMBASSY) {
      /* A real embassy is an embassy. */
      return ITF_YES;
    }
    if (preq->source.value.diplrel == DRO_HAS_EMBASSY
        && source->value.diplrel == DRO_HAS_REAL_EMBASSY) {
      /* A real embassy is an embassy. */
      return ITF_YES;
    }
    if (preq->source.value.diplrel < DS_LAST
        && source->value.diplrel < DS_LAST
        && preq->range == REQ_RANGE_LOCAL) {
      fc_assert_ret_val(preq->source.value.diplrel != source->value.diplrel,
                        ITF_YES);
      /* Can only have one diplstate_type to a specific player. */
      return ITF_NO;
    }
    /* Can't say this diplrel blocks the other diplrel. */
    return ITF_NOT_APPLICABLE;
  }

  /* Not relevant. */
  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if an output type fulfills a requirement
**************************************************************************/
static enum req_item_found output_type_found(const struct requirement *preq,
                                             const struct universal *source)
{
  switch (preq->source.kind) {
  case VUT_OTYPE:
    return source->value.outputtype == preq->source.value.outputtype ? ITF_YES
                                                                     : ITF_NO;
  default:
    /* Not found and not relevant. */
    return ITF_NOT_APPLICABLE;
  }
}

/**********************************************************************//**
  Find if a unit state property fulfills a requirement.
**************************************************************************/
static enum req_item_found ustate_found(const struct requirement *preq,
                                        const struct universal *source)
{
  if (preq->range != REQ_RANGE_LOCAL) {
    return ITF_NOT_APPLICABLE;
  }

  if (preq->source.kind == VUT_UNITSTATE) {
    switch (source->value.unit_state) {
    case USP_TRANSPORTED:
    case USP_TRANSPORTING:
      /* USP_TRANSPORTED and USP_TRANSPORTING don't exclude each other. */
    case USP_LIVABLE_TILE:
    case USP_NATIVE_TILE:
    /* USP_NATIVE_TILE isn't a strict subset of USP_LIVABLE_TILE. See
     * UTYF_COAST_STRICT. */
    case USP_HAS_HOME_CITY:
    case USP_NATIVE_EXTRA:
    case USP_MOVED_THIS_TURN:
      if (source->value.unit_state == preq->source.value.unit_state) {
        /* The other unit states doesn't contradict */
        return ITF_YES;
      }
      break;
    case USP_COUNT:
      fc_assert_ret_val(source->value.unit_state != USP_COUNT,
                        ITF_NOT_APPLICABLE);
    }
  }

  /* Not found and not relevant. */
  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Initialise universal_found_function array.
**************************************************************************/
void universal_found_functions_init(void)
{
  universal_found_function[VUT_GOVERNMENT] = &government_found;
  universal_found_function[VUT_NATION] = &nation_found;
  universal_found_function[VUT_IMPROVEMENT] = &improvement_found;
  universal_found_function[VUT_UCLASS] = &unit_class_found;
  universal_found_function[VUT_UTYPE] = &unit_type_found;
  universal_found_function[VUT_ACTIVITY] = &unit_activity_found;
  universal_found_function[VUT_TERRAIN] = &terrain_type_found;
  universal_found_function[VUT_CITYTILE] = &city_tile_found;
  universal_found_function[VUT_EXTRA] = &extra_type_found;
  universal_found_function[VUT_OTYPE] = &output_type_found;
  universal_found_function[VUT_ACTION] = &action_found;
  universal_found_function[VUT_DIPLREL] = &diplrel_found;
  universal_found_function[VUT_DIPLREL_TILE] = &diplrel_found;
  universal_found_function[VUT_DIPLREL_TILE_O] = &diplrel_found;
  universal_found_function[VUT_DIPLREL_UNITANY] = &diplrel_found;
  universal_found_function[VUT_DIPLREL_UNITANY_O] = &diplrel_found;
  universal_found_function[VUT_UNITSTATE] = &ustate_found;
}

/**********************************************************************//**
  Returns (the position of) the given requirement's enumerator in the
  enumeration of all possible requirements of its requirement kind.

  Note: Since this isn't used for any requirement type that supports
  surviving requirements those aren't supported. Add support if a user
  appears.
**************************************************************************/
int requirement_kind_ereq(const int value,
                          const enum req_range range,
                          const bool present,
                          const int max_value)
{
  /* The enumerators in each range starts with present for every possible
   * value followed by !present for every possible value. */
  const int pres_start = (present ? 0 : max_value);

  /* The enumerators for every range follows all the positions of the
   * previous range(s). */
  const int range_start = ((max_value - 1) * 2) * range;

  return range_start + pres_start + value;
}
