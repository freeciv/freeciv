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
#include "counters.h"
#include "culture.h"
#include "game.h"
#include "government.h"
#include "improvement.h"
#include "movement.h"
#include "nation.h"
#include "player.h"
#include "map.h"
#include "research.h"
#include "road.h"
#include "server_settings.h"
#include "specialist.h"
#include "style.h"
#include "tiledef.h"
#include "victory.h" /* victory_enabled() */

#include "requirements.h"

/************************************************************************
  Container for req_item_found functions
************************************************************************/
typedef enum req_item_found (*universal_found)(const struct requirement *,
                                               const struct universal *);
static universal_found universal_found_function[VUT_COUNT] = {nullptr};

static
enum fc_tristate tri_req_present(const struct civ_map *nmap,
                                 const struct req_context *context,
                                 const struct req_context *other_context,
                                 const struct requirement *req);

/* Function pointer for requirement-type-specific is_req_active handlers */
typedef enum fc_tristate
(*is_req_active_cb)(const struct civ_map *nmap,
                    const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req);

static inline bool are_tiles_in_range(const struct tile *tile1,
                                      const struct tile *tile2,
                                      enum req_range range);

/**********************************************************************//**
  Never changes in local range
  Mostly it's about requirements evaluated from constants and persistent
  ruleset objects passed in the context.
**************************************************************************/
static enum req_unchanging_status
  unchanging_local(const struct civ_map *nmap,
                   enum req_unchanging_status def,
                   const struct req_context *context,
                   const struct requirement *req)
{
  return req->range == REQ_RANGE_LOCAL ? REQUCH_YES : def;
}
#define REQUC_LOCAL unchanging_local

/**********************************************************************//**
  If not present, may appear; but once becomes present, never goes absent
**************************************************************************/
static enum req_unchanging_status
  unchanging_present(const struct civ_map *nmap,
                     enum req_unchanging_status def,
                     const struct req_context *context,
                     const struct requirement *req)
{
  if (TRI_YES != tri_req_present(nmap, context, nullptr, req)) {
    return REQUCH_NO;
  }
  return def;
}
#define REQUC_PRESENT unchanging_present

/**********************************************************************//**
  Equals ..._present(), but never changes in World range
**************************************************************************/
static enum req_unchanging_status
  unchanging_world(const struct civ_map *nmap,
                   enum req_unchanging_status def,
                   const struct req_context *context,
                   const struct requirement *req)
{
  return
    unchanging_present(nmap, req->range == REQ_RANGE_WORLD ? REQUCH_YES : def,
                       context, req);
}
#define REQUC_WORLD unchanging_world

/**********************************************************************//**
  Unchanging except if provided by an ally and not the player themself
  Alliances may break, team members may be destroyed or reassigned
**************************************************************************/
static enum req_unchanging_status
  unchanging_noally(const struct civ_map *nmap,
                    enum req_unchanging_status def,
                    const struct req_context *context,
                    const struct requirement *req)
{
  if (REQ_RANGE_ALLIANCE == req->range
      || REQ_RANGE_TEAM == req->range) {
    struct requirement preq;

    req_copy(&preq, req);
    preq.range = REQ_RANGE_PLAYER;
    if (TRI_YES != tri_req_present(nmap, context, nullptr, &preq)) {
      return REQ_RANGE_TEAM == req->range ? REQUCH_ACT : REQUCH_NO;
    }
  }
  return def;
}
#define REQUC_NALLY unchanging_noally

/**********************************************************************//**
  Special CityTile case handler
**************************************************************************/
static enum req_unchanging_status
  unchanging_citytile(const struct civ_map *nmap,
                      enum req_unchanging_status def,
                      const struct req_context *context,
                      const struct requirement *req)
{
  fc_assert_ret_val(VUT_CITYTILE == req->source.kind, REQUCH_NO);
  if (CITYT_CENTER == req->source.value.citytile
      || (CITYT_BORDERING_TCLASS_REGION != req->source.value.citytile
          && context->city != nullptr && context->tile != nullptr
          && city_tile(context->city) != nullptr
          && are_tiles_in_range(city_tile(context->city), context->tile,
                                req->range))){
    /* Cities don't move, and most reqs are present on city center */
    return REQUCH_YES;
  }
  return def;
}
#define REQUC_CITYTILE unchanging_citytile

/**********************************************************************//**
  Special CityStatus case handler. Changes easily save for owner.
**************************************************************************/
static enum req_unchanging_status
  unchanging_citystatus(const struct civ_map *nmap,
                        enum req_unchanging_status def,
                        const struct req_context *context,
                        const struct requirement *req)
{
  fc_assert_ret_val(VUT_CITYSTATUS == req->source.kind, REQUCH_NO);

  if (REQ_RANGE_CITY == req->range
      && (CITYS_OWNED_BY_ORIGINAL == req->source.value.citystatus
          || CITYS_TRANSFERRED == req->source.value.citystatus)) {
    return REQUCH_CTRL;
  }

  return def;
}
#define REQUC_CITYSTATUS unchanging_citystatus

/**********************************************************************//**
  Special Building case handler.
  Sometimes building is just a constant parameter, and sometimes
  it subjects to wonder building rules. Also, there is obsoletion...
**************************************************************************/
static enum req_unchanging_status
  unchanging_building(const struct civ_map *nmap,
                      enum req_unchanging_status def,
                      const struct req_context *context,
                      const struct requirement *req)
{
  const struct impr_type *b = req->source.value.building;

  fc_assert_ret_val(VUT_IMPROVEMENT == req->source.kind
                    || VUT_SITE == req->source.kind, REQUCH_NO);
  if (REQ_RANGE_LOCAL == req->range) {
    /* Likely, won't be questioned for an obsolete building */
    return REQUCH_YES;
  }

  if (req->source.kind == VUT_IMPROVEMENT
      && improvement_obsolete(context->player, b, context->city)) {
    /* FIXME: Sometimes can unobsolete, but considering it
     * may sometimes put the function on endless recursion */
    return REQUCH_ACT; /* Mostly about techs */
  }
  if (is_great_wonder(b)) {
    if (great_wonder_is_destroyed(b)
        || (!great_wonder_is_available(b)
            && (req->range <= REQ_RANGE_CITY && TRI_YES
                == tri_req_present(nmap, context, nullptr, req)))) {
      /* If the wonder stays somewhere, it may either remain there
       * or be destroyed. If it is destroyed, it is nowhere. */
      return REQUCH_SCRIPTS;
    }
  }
  return def;
}
#define REQUC_IMPR unchanging_building

struct req_def {
  const is_req_active_cb cb;
  enum req_unchanging_status unchanging;
  req_unchanging_cond_cb unchanging_cond;
};

/**********************************************************************//**
  Parse requirement type (kind) and value strings into a universal
  structure. Passing in a nullptr type is considered VUT_NONE (not an error).

  Pass this some values like "Building", "Factory".
  FIXME: Ensure that every caller checks error return!
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
    if (source->value.advance != nullptr) {
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
    if (source->value.govern != nullptr) {
      return;
    }
    break;
  case VUT_GOVFLAG:
    source->value.govflag = gov_flag_id_by_name(value, fc_strcasecmp);
    if (gov_flag_id_is_valid(source->value.govflag)) {
      return;
    }
    break;
  case VUT_ACHIEVEMENT:
    source->value.achievement = achievement_by_rule_name(value);
    if (source->value.achievement != nullptr) {
      return;
    }
    break;
  case VUT_STYLE:
    source->value.style = style_by_rule_name(value);
    if (source->value.style != nullptr) {
      return;
    }
    break;
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    source->value.building = improvement_by_rule_name(value);
    if (source->value.building != nullptr) {
      return;
    }
    break;
  case VUT_IMPR_GENUS:
    source->value.impr_genus = impr_genus_id_by_name(value, fc_strcasecmp);
    if (impr_genus_id_is_valid(source->value.impr_genus)) {
      return;
    }
    break;
  case VUT_IMPR_FLAG:
    source->value.impr_flag = impr_flag_id_by_name(value, fc_strcasecmp);
    if (impr_flag_id_is_valid(source->value.impr_flag)) {
      return;
    }
    break;
  case VUT_PLAYER_FLAG:
    source->value.plr_flag = plr_flag_id_by_name(value, fc_strcasecmp);
    if (plr_flag_id_is_valid(source->value.plr_flag)) {
      return;
    }
    break;
  case VUT_EXTRA:
    source->value.extra = extra_type_by_rule_name(value);
    if (source->value.extra != nullptr) {
      return;
    }
    break;
  case VUT_TILEDEF:
    source->value.tiledef = tiledef_by_rule_name(value);
    if (source->value.tiledef != nullptr) {
      return;
    }
    break;
  case VUT_GOOD:
    source->value.good = goods_by_rule_name(value);
    if (source->value.good != nullptr) {
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
    if (source->value.nationgroup != nullptr) {
      return;
    }
    break;
  case VUT_NATIONALITY:
    source->value.nationality = nation_by_rule_name(value);
    if (source->value.nationality != NO_NATION_SELECTED) {
      return;
    }
    break;
   case VUT_ORIGINAL_OWNER:
    source->value.origowner = nation_by_rule_name(value);
    if (source->value.origowner != NO_NATION_SELECTED) {
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
  case VUT_FORM_AGE:
    source->value.form_age = atoi(value);
    if (source->value.form_age > 0) {
      return;
    }
    break;
  case VUT_MINTECHS:
    source->value.min_techs = atoi(value);
    if (source->value.min_techs > 0) {
      return;
    }
    break;
  case VUT_FUTURETECHS:
    source->value.future_techs = atoi(value);
    if (source->value.future_techs > 0) {
      return;
    }
    break;
  case VUT_MINCITIES:
    source->value.min_cities = atoi(value);
    if (source->value.min_cities > 0) {
      return;
    }
    break;
  case VUT_ACTION:
    source->value.action = action_by_rule_name(value);
    if (source->value.action != nullptr) {
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
  case VUT_MAXTILETOTALUNITS:
    source->value.max_tile_total_units = atoi(value);
    if (0 <= source->value.max_tile_total_units) {
      return;
    }
    break;
  case VUT_MAXTILETOPUNITS:
    source->value.max_tile_top_units = atoi(value);
    if (0 <= source->value.max_tile_top_units) {
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
  case VUT_WRAP:
    source->value.wrap_property = wrap_flag_by_name(value, fc_strcasecmp);
    if (wrap_flag_is_valid(source->value.wrap_property)) {
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
  case VUT_PLAYER_STATE:
    source->value.plrstate = plrstate_type_by_name(value, fc_strcasecmp);
    if (source->value.plrstate != PLRS_LAST) {
      return;
    }
    break;
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    source->value.latitude = atoi(value);
    if (source->value.latitude >= -MAP_MAX_LATITUDE
        && source->value.latitude <= MAP_MAX_LATITUDE) {
      return;
    }
    break;
  case VUT_COUNTER:
   source->value.counter = counter_by_rule_name(value);
   if (source->value.counter != nullptr) {
      return;
   }
   break;
  case VUT_MAX_DISTANCE_SQ:
    source->value.distance_sq = atoi(value);
    if (0 <= source->value.distance_sq) {
      return;
    }
    break;
  case VUT_MAX_REGION_TILES:
    source->value.region_tiles = atoi(value);
    if (0 < source->value.region_tiles) {
      return;
    }
    break;
  case VUT_TILE_REL:
    source->value.tilerel = tilerel_type_by_name(value, fc_strcasecmp);
    if (source->value.tilerel != TREL_COUNT) {
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
  Combine values into a universal structure. This is for serialization
  and is the opposite of universal_extraction().
  FIXME: Ensure that every caller checks error return!
**************************************************************************/
struct universal universal_by_number(const enum universals_n kind,
                                     const int value)
{
  struct universal source;

  source.kind = kind;

  switch (source.kind) {
  case VUT_NONE:
    /* Avoid compiler warning about uninitialized source.value */
    source.value.advance = nullptr;

    return source;
  case VUT_ADVANCE:
    source.value.advance = advance_by_number(value);
    if (source.value.advance != nullptr) {
      return source;
    }
    break;
  case VUT_TECHFLAG:
    source.value.techflag = value;
    return source;
  case VUT_GOVERNMENT:
    source.value.govern = government_by_number(value);
    if (source.value.govern != nullptr) {
      return source;
    }
    break;
  case VUT_GOVFLAG:
    source.value.govflag = value;
    return source;
  case VUT_ACHIEVEMENT:
    source.value.achievement = achievement_by_number(value);
    if (source.value.achievement != nullptr) {
      return source;
    }
    break;
  case VUT_STYLE:
    source.value.style = style_by_number(value);
    if (source.value.style != nullptr) {
      return source;
    }
    break;
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    source.value.building = improvement_by_number(value);
    if (source.value.building != nullptr) {
      return source;
    }
    break;
  case VUT_IMPR_GENUS:
    source.value.impr_genus = value;
    return source;
  case VUT_IMPR_FLAG:
    source.value.impr_flag = value;
    return source;
  case VUT_PLAYER_FLAG:
    source.value.plr_flag = value;
    return source;
  case VUT_EXTRA:
    source.value.extra = extra_by_number(value);
    return source;
  case VUT_TILEDEF:
    source.value.tiledef = tiledef_by_number(value);
    return source;
  case VUT_GOOD:
    source.value.good = goods_by_number(value);
    return source;
  case VUT_TERRAIN:
    source.value.terrain = terrain_by_number(value);
    if (source.value.terrain != nullptr) {
      return source;
    }
    break;
  case VUT_TERRFLAG:
    source.value.terrainflag = value;
    return source;
  case VUT_NATION:
    source.value.nation = nation_by_number(value);
    if (source.value.nation != nullptr) {
      return source;
    }
    break;
  case VUT_NATIONGROUP:
    source.value.nationgroup = nation_group_by_number(value);
    if (source.value.nationgroup != nullptr) {
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
    if (source.value.nationality != nullptr) {
      return source;
    }
    break;
  case VUT_ORIGINAL_OWNER:
    source.value.origowner = nation_by_number(value);
    if (source.value.origowner != nullptr) {
      return source;
    }
    break;
  case VUT_UTYPE:
    source.value.utype = utype_by_number(value);
    if (source.value.utype != nullptr) {
      return source;
    }
    break;
  case VUT_UTFLAG:
    source.value.unitflag = value;
    return source;
  case VUT_UCLASS:
    source.value.uclass = uclass_by_number(value);
    if (source.value.uclass != nullptr) {
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
  case VUT_FORM_AGE:
    source.value.form_age = value;
    return source;
  case VUT_MINTECHS:
    source.value.min_techs = value;
    return source;
  case VUT_FUTURETECHS:
    source.value.future_techs = value;
    return source;
  case VUT_MINCITIES:
    source.value.min_cities = value;
    return source;
  case VUT_ACTION:
    source.value.action = action_by_number(value);
    if (source.value.action != nullptr) {
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
  case VUT_MAXTILETOTALUNITS:
    source.value.max_tile_total_units = value;
    return source;
  case VUT_MAXTILETOPUNITS:
    source.value.max_tile_top_units = value;
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
  case VUT_WRAP:
    source.value.wrap_property = value;
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
  case VUT_PLAYER_STATE:
    source.value.plrstate = value;
    return source;
  case VUT_COUNTER:
    source.value.counter = counter_by_id(value);
    return source;
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    source.value.latitude = value;
    return source;
  case VUT_MAX_DISTANCE_SQ:
    source.value.distance_sq = value;
    return source;
  case VUT_MAX_REGION_TILES:
    source.value.region_tiles = value;
    return source;
  case VUT_TILE_REL:
    source.value.tilerel = value;
    return source;
  case VUT_COUNT:
    break;
  }

  /* If we reach here there's been an error. */
  source.kind = universals_n_invalid();
  /* Avoid compiler warning about uninitialized source.value */
  source.value.advance = nullptr;

  return source;
}

/**********************************************************************//**
  Fill in copy of universal
**************************************************************************/
void universal_copy(struct universal *dst, const struct universal *src)
{
  dst->value = src->value;
  dst->kind = src->kind;
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
  case VUT_GOVFLAG:
    return source->value.govflag;
  case VUT_ACHIEVEMENT:
    return achievement_number(source->value.achievement);
  case VUT_STYLE:
    return style_number(source->value.style);
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    return improvement_number(source->value.building);
  case VUT_IMPR_GENUS:
    return source->value.impr_genus;
  case VUT_IMPR_FLAG:
    return source->value.impr_flag;
  case VUT_PLAYER_FLAG:
    return source->value.plr_flag;
  case VUT_EXTRA:
    return extra_number(source->value.extra);
  case VUT_TILEDEF:
    return tiledef_number(source->value.tiledef);
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
  case VUT_ORIGINAL_OWNER:
    return nation_number(source->value.origowner);
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
  case VUT_FORM_AGE:
    return source->value.form_age;
  case VUT_MINTECHS:
    return source->value.min_techs;
  case VUT_FUTURETECHS:
    return source->value.future_techs;
  case VUT_MINCITIES:
    return source->value.min_cities;
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
  case VUT_MAXTILETOTALUNITS:
    return source->value.max_tile_total_units;
  case VUT_MAXTILETOPUNITS:
    return source->value.max_tile_top_units;
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
  case VUT_WRAP:
    return source->value.wrap_property;
  case VUT_SERVERSETTING:
    return source->value.ssetval;
  case VUT_TERRAINALTER:
    return source->value.terrainalter;
  case VUT_CITYTILE:
    return source->value.citytile;
  case VUT_CITYSTATUS:
    return source->value.citystatus;
  case VUT_PLAYER_STATE:
    return source->value.plrstate;
  case VUT_COUNTER:
    return counter_id(source->value.counter);
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    return source->value.latitude;
  case VUT_MAX_DISTANCE_SQ:
    return source->value.distance_sq;
  case VUT_MAX_REGION_TILES:
    return source->value.region_tiles;
  case VUT_TILE_REL:
    return source->value.tilerel;
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

  astring does not need to be initialized before the call,
  but caller needs to call astr_free() for it once the returned
  string is no longer needed.
**************************************************************************/
const char *req_to_fstring(const struct requirement *req,
                           struct astring *astr)
{
  astr_init(astr);

  astr_set(astr, "%s%s %s %s%s",
           req->survives ? "surviving " : "",
           req_range_name(req->range),
           universal_type_rule_name(&req->source),
           req->present ? "" : "!",
           universal_rule_name(&req->source));

  return astr_str(astr);
}

/**********************************************************************//**
  Parse a requirement type and value string into a requirement structure.
  Returns the invalid element for enum universal_n on error. Passing in
  a nullptr type is considered VUT_NONE (not an error).

  Pass this some values like "Building", "Factory".
**************************************************************************/
struct requirement req_from_str(const char *type, const char *range,
                                bool survives, bool present, bool quiet,
                                const char *value)
{
  struct requirement req;
  bool invalid;
  const char *error = nullptr;

  req.source = universal_by_rule_name(type, value);

  invalid = !universals_n_is_valid(req.source.kind);
  if (invalid) {
    error = "bad type or name";
  } else {
    /* Scan the range string to find the range. If no range is given a
     * default fallback is used rather than giving an error. */
    if (range != nullptr) {
      req.range = req_range_by_name(range, fc_strcasecmp);
      if (!req_range_is_valid(req.range)) {
        invalid = TRUE;
      }
    } else {
      switch (req.source.kind) {
      case VUT_NONE:
      case VUT_COUNT:
        break;
      case VUT_IMPROVEMENT:
      case VUT_SITE:
      case VUT_IMPR_GENUS:
      case VUT_IMPR_FLAG:
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
      case VUT_FORM_AGE:
      case VUT_ACTION:
      case VUT_OTYPE:
      case VUT_SPECIALIST:
      case VUT_DIPLREL_TILE_O:
      case VUT_DIPLREL_UNITANY_O:
        req.range = REQ_RANGE_LOCAL;
        break;
      case VUT_EXTRA:
      case VUT_TILEDEF:
      case VUT_ROADFLAG:
      case VUT_EXTRAFLAG:
        /* Keep old behavior */
        req.range = REQ_RANGE_TILE;
        break;
      case VUT_TERRAIN:
      case VUT_TERRFLAG:
      case VUT_TERRAINCLASS:
      case VUT_TERRAINALTER:
      case VUT_CITYTILE:
      case VUT_MAXTILETOTALUNITS:
      case VUT_MAXTILETOPUNITS:
      case VUT_MINLATITUDE:
      case VUT_MAXLATITUDE:
      case VUT_MAX_DISTANCE_SQ:
        req.range = REQ_RANGE_TILE;
        break;
      case VUT_COUNTER:
      case VUT_MINSIZE:
      case VUT_MINCULTURE:
      case VUT_MINFOREIGNPCT:
      case VUT_NATIONALITY:
      case VUT_ORIGINAL_OWNER:
      case VUT_CITYSTATUS:
      case VUT_GOOD:
        req.range = REQ_RANGE_CITY;
        break;
      case VUT_GOVERNMENT:
      case VUT_GOVFLAG:
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
      case VUT_PLAYER_FLAG:
      case VUT_PLAYER_STATE:
      case VUT_MINCITIES:
      case VUT_FUTURETECHS:
        req.range = REQ_RANGE_PLAYER;
        break;
      case VUT_MINYEAR:
      case VUT_MINCALFRAG:
      case VUT_TOPO:
      case VUT_WRAP:
      case VUT_MINTECHS:
      case VUT_SERVERSETTING:
        req.range = REQ_RANGE_WORLD;
        break;
      case VUT_MAX_REGION_TILES:
        req.range = REQ_RANGE_CONTINENT;
        break;
      case VUT_TILE_REL:
        req.range = REQ_RANGE_TILE;
        if (req.source.value.tilerel == TREL_ONLY_OTHER_REGION) {
          /* Not available at Tile range */
          req.range = REQ_RANGE_ADJACENT;
        }
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
    case VUT_TERRAINCLASS:
    case VUT_TERRFLAG:
      invalid = (req.range != REQ_RANGE_TILE
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT
                 && req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADE_ROUTE);
      break;
    case VUT_EXTRA:
    case VUT_ROADFLAG:
    case VUT_EXTRAFLAG:
      invalid = (req.range > REQ_RANGE_TRADE_ROUTE);
      break;
    case VUT_TILEDEF:
      invalid = (req.range > REQ_RANGE_TRADE_ROUTE
                 || req.range == REQ_RANGE_LOCAL);
      break;
    case VUT_ACHIEVEMENT:
    case VUT_MINTECHS:
    case VUT_FUTURETECHS:
      invalid = (req.range < REQ_RANGE_PLAYER);
      break;
    case VUT_ADVANCE:
    case VUT_TECHFLAG:
      invalid = (req.range < REQ_RANGE_PLAYER
                 && req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_GOVERNMENT:
    case VUT_GOVFLAG:
    case VUT_AI_LEVEL:
    case VUT_STYLE:
    case VUT_MINCITIES:
      invalid = (req.range != REQ_RANGE_PLAYER);
      break;
    case VUT_MINSIZE:
    case VUT_MINFOREIGNPCT:
    case VUT_NATIONALITY:
    case VUT_CITYSTATUS:
      invalid = (req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADE_ROUTE);
      break;
    case VUT_GOOD:
    case VUT_ORIGINAL_OWNER:
      invalid = (req.range != REQ_RANGE_CITY);
      break;
    case VUT_MINCULTURE:
      invalid = (req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_TRADE_ROUTE
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
    case VUT_MINVETERAN:
    case VUT_UNITSTATE:
    case VUT_ACTIVITY:
    case VUT_MINMOVES:
    case VUT_MINHP:
    case VUT_ACTION:
    case VUT_OTYPE:
    case VUT_SPECIALIST:
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_UTYPE:
    case VUT_UTFLAG:
    case VUT_UCLASS:
    case VUT_UCFLAG:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_TILE
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT);
      break;
    case VUT_TERRAINALTER: /* XXX could in principle support C/ADJACENT */
    case VUT_MAX_DISTANCE_SQ:
      invalid = (req.range != REQ_RANGE_TILE);
      break;
    case VUT_CITYTILE:
    case VUT_MAXTILETOTALUNITS:
    case VUT_MAXTILETOPUNITS:
      invalid = (req.range != REQ_RANGE_TILE
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT);
      break;
    case VUT_MINLATITUDE:
    case VUT_MAXLATITUDE:
      invalid = (req.range != REQ_RANGE_TILE
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT
                 && req.range != REQ_RANGE_WORLD)
                /* Avoid redundancy at tile range: no negated requirements
                 * that could be emulated by a present requirement of the
                 * other type */
                || (req.range == REQ_RANGE_TILE && !req.present);
      break;
    case VUT_MINYEAR:
    case VUT_MINCALFRAG:
    case VUT_TOPO:
    case VUT_WRAP:
    case VUT_SERVERSETTING:
      invalid = (req.range != REQ_RANGE_WORLD);
      break;
    case VUT_AGE:
      /* FIXME: Could support TRADE_ROUTE, TEAM, etc */
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_CITY
                 && req.range != REQ_RANGE_PLAYER);
      break;
    case VUT_FORM_AGE:
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_IMPR_GENUS:
      /* TODO: Support other ranges too. */
      invalid = (req.range != REQ_RANGE_LOCAL);
      break;
    case VUT_IMPR_FLAG:
      invalid = (req.range != REQ_RANGE_LOCAL
                 && req.range != REQ_RANGE_TILE
                 && req.range != REQ_RANGE_CITY);
      break;
    case VUT_COUNTER:
      invalid = req.range != REQ_RANGE_CITY;
      break;
    case VUT_PLAYER_FLAG:
    case VUT_PLAYER_STATE:
      invalid = (req.range != REQ_RANGE_PLAYER);
      break;
    case VUT_MAX_REGION_TILES:
      invalid = (req.range != REQ_RANGE_CONTINENT
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_ADJACENT);
      break;
    case VUT_TILE_REL:
      invalid = (req.range != REQ_RANGE_ADJACENT
                 && req.range != REQ_RANGE_CADJACENT
                 && req.range != REQ_RANGE_TILE)
                /* TREL_ONLY_OTHER_REGION not supported at Tile range */
                || (req.source.value.tilerel == TREL_ONLY_OTHER_REGION
                    && req.range == REQ_RANGE_TILE);
      break;
    case VUT_IMPROVEMENT:
    case VUT_SITE:
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
    case VUT_SITE:
      /* See buildings_in_range(). */
      invalid = survives && req.range <= REQ_RANGE_CONTINENT;
      break;
    case VUT_NATION:
    case VUT_ADVANCE:
      invalid = survives && req.range != REQ_RANGE_WORLD;
      break;
    case VUT_COUNTER:
    case VUT_IMPR_GENUS:
    case VUT_IMPR_FLAG:
    case VUT_PLAYER_FLAG:
    case VUT_PLAYER_STATE:
    case VUT_GOVERNMENT:
    case VUT_GOVFLAG:
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
    case VUT_FORM_AGE:
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
    case VUT_WRAP:
    case VUT_SERVERSETTING:
    case VUT_TERRAINALTER:
    case VUT_CITYTILE:
    case VUT_CITYSTATUS:
    case VUT_TERRFLAG:
    case VUT_NATIONALITY:
    case VUT_ORIGINAL_OWNER:
    case VUT_ROADFLAG:
    case VUT_EXTRAFLAG:
    case VUT_EXTRA:
    case VUT_TILEDEF:
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
    case VUT_MAXTILETOTALUNITS:
    case VUT_MAXTILETOPUNITS:
    case VUT_MINTECHS:
    case VUT_FUTURETECHS:
    case VUT_MINCITIES:
    case VUT_MINLATITUDE:
    case VUT_MAXLATITUDE:
    case VUT_MAX_DISTANCE_SQ:
    case VUT_MAX_REGION_TILES:
    case VUT_TILE_REL:
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
  Set the values of a req from serializable integers. This is the opposite
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
  Return the value of a req as a serializable integer. This is the opposite
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
  Fill in copy of the requirement.
**************************************************************************/
void req_copy(struct requirement *dst, const struct requirement *src)
{
  universal_copy(&(dst->source), &(src->source));
  dst->range = src->range;
  dst->survives = src->survives;
  dst->present = src->present;
  dst->quiet = src->quiet;
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
  fc_assert_ret_val(impr_req->source.kind == VUT_IMPROVEMENT
                    || impr_req->source.kind == VUT_SITE, FALSE);
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
  Returns TRUE if the specified building requirement contradicts the
  specified building flag requirement.
**************************************************************************/
static bool impr_contra_flag(const struct requirement *impr_req,
                             const struct requirement *flag_req)
{
  /* The input is sane. */
  fc_assert_ret_val(impr_req->source.kind == VUT_IMPROVEMENT
                    || impr_req->source.kind == VUT_SITE, FALSE);
  fc_assert_ret_val(flag_req->source.kind == VUT_IMPR_FLAG, FALSE);

  if (impr_req->range == REQ_RANGE_LOCAL
      && flag_req->range == REQ_RANGE_LOCAL) {
    /* Applies to the same target building. */

    if (impr_req->present && !flag_req->present) {
      /* The target building can't not have the flag it has. */
      return improvement_has_flag(impr_req->source.value.building,
                                  flag_req->source.value.impr_flag);
    }

    if (impr_req->present && flag_req->present) {
      /* The target building can't have another flag than it has. */
      return !improvement_has_flag(impr_req->source.value.building,
                                   flag_req->source.value.impr_flag);
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
  Returns TRUE if the specified city center or continent requirement
  contradicts the other city tile requirement.
**************************************************************************/
static bool city_center_contra(const struct requirement *cc_req,
                               const struct requirement *ct_req)
{
  /* The input is sane. */
  fc_assert_ret_val(cc_req->source.kind == VUT_CITYTILE
                    && ct_req->source.kind == VUT_CITYTILE, FALSE);

  if (cc_req->source.value.citytile == CITYT_CENTER
      && cc_req->present && cc_req->range <= ct_req->range) {
    switch (ct_req->source.value.citytile) {
    case CITYT_CENTER:
    case CITYT_CLAIMED:
    case CITYT_EXTRAS_OWNED:
    case CITYT_WORKED:
    case CITYT_SAME_CONTINENT:
      /* Should be always on city center */
      return !ct_req->present;
    case CITYT_BORDERING_TCLASS_REGION:
      /* Handled later */
      break;
    case CITYT_LAST:
      /* Error */
      fc_assert_ret_val(ct_req->source.value.citytile != CITYT_LAST, FALSE);
    }
  }
  if ((cc_req->source.value.citytile == CITYT_SAME_CONTINENT
       || cc_req->source.value.citytile == CITYT_CENTER)
      && ct_req->source.value.citytile
         == CITYT_BORDERING_TCLASS_REGION
      && REQ_RANGE_TILE == cc_req->range
      && REQ_RANGE_TILE == ct_req->range) {
    /* Can't coexist */
    return cc_req->present ? ct_req->present : !ct_req->present;
  }

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
     * Example: Trade Route > CAdjacent but something may be in CAdjacent
     * but not in Trade Route. */
    return FALSE;
  }

  return ITF_YES == universal_fulfills_requirement(absent,
                                                   &present->source);
}

/**********************************************************************//**
  Returns TRUE if req2 is always fulfilled when req1 is (i.e. req1 => req2)
**************************************************************************/
bool req_implies_req(const struct requirement *req1,
                     const struct requirement *req2)
{
  struct requirement nreq2;

  req_copy(&nreq2, req2);
  nreq2.present = !nreq2.present;
  return are_requirements_contradictions(req1, &nreq2);
}

/**********************************************************************//**
  Returns TRUE iff two bounds that could each be either an upper or lower
  bound are contradicting each other. This function assumes that of the
  upper and lower bounds, one will be inclusive, the other exclusive.
**************************************************************************/
static inline bool are_bounds_contradictions(int bound1, bool is_upper1,
                                             int bound2, bool is_upper2)
{
  /* If the bounds are on opposite sides, and one is inclusive, the other
   * exclusive, the number of values that satisfy both bounds is exactly
   * their difference, (upper bound) - (lower bound).
   * The bounds contradict each other iff this difference is 0 or less,
   * i.e. iff (upper bound) <= (lower bound) */
  if (is_upper1 && !is_upper2) {
    return bound1 <= bound2;
  } else if (!is_upper1 && is_upper2) {
    return bound1 >= bound2;
  }
  /* Both are upper or both are lower ~> no contradiction possible */
  return FALSE;
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
  case VUT_SITE:
    if (req2->source.kind == VUT_IMPR_GENUS) {
      return impr_contra_genus(req1, req2);
    } else if (req2->source.kind == VUT_IMPR_FLAG) {
      return impr_contra_flag(req1, req2);
    } else if (req2->source.kind == VUT_CITYTILE
               && req2->source.value.citytile == CITYT_CENTER
               && REQ_RANGE_TILE == req2->range
               && REQ_RANGE_TILE == req1->range
               && req1->present) {
      /* A building must be in a city */
      return !req2->present;
    }

    /* No special knowledge. */
    return FALSE;
  case VUT_IMPR_GENUS:
    if (req2->source.kind == VUT_IMPROVEMENT
        || req2->source.kind == VUT_SITE) {
      return impr_contra_genus(req2, req1);
    }

    /* No special knowledge. */
    return FALSE;
  case VUT_IMPR_FLAG:
    if (req2->source.kind == VUT_IMPROVEMENT
        || req2->source.kind == VUT_SITE) {
      return impr_contra_flag(req2, req1);
    }

    /* No special knowledge. */
    return FALSE;
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
    }
    return are_bounds_contradictions(
        req1->source.value.minmoves, !req1->present,
        req2->source.value.minmoves, !req2->present);
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    if (req2->source.kind != VUT_MINLATITUDE
        && req2->source.kind != VUT_MAXLATITUDE) {
      /* Finding contradictions across requirement kinds other than each
       * other is not supported for MinLatitude and MaxLatitude. */
      return FALSE;
    } else {
      /* For a contradiction, we need
       * - a minimum (present MinLatitude or negated MaxLatitude)
       * - a maximum (negated MinLatitude or present MaxLatitude)
       * - the maximum to be less than the minimum
       * - a requirement at the larger range that applies to the entire
       *   range (i.e. a negated requirement, unless the range is Tile)
       *   Otherwise, the two requirements could still be fulfilled
       *   simultaneously by different tiles in the range */

      /* Initial values beyond the boundaries to avoid edge cases */
      int minimum = -MAP_MAX_LATITUDE - 1, maximum = MAP_MAX_LATITUDE + 1;
      enum req_range covered_range = REQ_RANGE_TILE;

#define EXTRACT_INFO(req)                                                 \
      if (req->present) {                                                 \
        if (req->source.kind == VUT_MINLATITUDE) {                        \
          /* present MinLatitude */                                       \
          minimum = MAX(minimum, req->source.value.latitude);             \
        } else {                                                          \
          /* present MaxLatitude */                                       \
          maximum = MIN(maximum, req->source.value.latitude);             \
        }                                                                 \
      } else {                                                            \
        covered_range = MAX(covered_range, req->range);                   \
        if (req->source.kind == VUT_MINLATITUDE) {                        \
          /* negated MinLatitude */                                       \
          maximum = MIN(maximum, req->source.value.latitude - 1);         \
        } else {                                                          \
          /* negated MaxLatitude */                                       \
          minimum = MAX(minimum, req->source.value.latitude + 1);         \
        }                                                                 \
      }

      EXTRACT_INFO(req1);
      EXTRACT_INFO(req2);

#undef EXTRACT_INFO

      return (maximum < minimum
              && covered_range >= req1->range
              && covered_range >= req2->range);
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
  case VUT_CITYTILE:
    if (req2->source.kind == VUT_CITYTILE) {
      return city_center_contra(req1, req2)
        || city_center_contra(req2, req1);
    } else if (req1->source.value.citytile == CITYT_CENTER
               && (req2->source.kind == VUT_IMPROVEMENT
                   || req2->source.kind == VUT_SITE)
               && REQ_RANGE_TILE == req2->range
               && REQ_RANGE_TILE == req1->range
               && req2->present) {
      /* A building must be in a city */
      return !req1->present;
    }

    return FALSE;
  case VUT_MAX_DISTANCE_SQ:
    if (req2->source.kind != VUT_MAX_DISTANCE_SQ) {
      /* Finding contradictions across requirement kinds isn't supported
       * for MaxDistanceSq requirements. */
      return FALSE;
    }
    return are_bounds_contradictions(
        req1->source.value.distance_sq, req1->present,
        req2->source.value.distance_sq, req2->present);
  case VUT_MAX_REGION_TILES:
    if (req2->source.kind != VUT_MAX_REGION_TILES) {
      /* Finding contradictions across requirement kinds isn't supported
       * for MaxRegionTiles requirements. */
      return FALSE;
    } else if (req1->range != req2->range) {
      /* FIXME: Finding contradictions across ranges not yet supported.
       * In particular, a max at a small range and a min at a larger range
       * needs extra work to figure out. */
      return FALSE;
    }
    return are_bounds_contradictions(
        req1->source.value.region_tiles, req1->present,
        req2->source.value.region_tiles, req2->present);
  case VUT_TILE_REL:
    if (req2->source.kind != VUT_TILE_REL) {
      /* Finding contradictions across requirement kinds isn't supported
       * for TileRel requirements. */
      return FALSE;
    }
    if (req1->source.value.tilerel == req2->source.value.tilerel) {
      /* Same requirement at different ranges. Note that same range is
       * already covered by are_requirements_opposites() above. */
      switch (req1->source.value.tilerel) {
        case TREL_SAME_TCLASS:
        case TREL_SAME_REGION:
        case TREL_REGION_SURROUNDED:
          /* Negated req at larger range contradicts present req at
           * smaller range. */
          if (req1->range > req2->range) {
            return !req1->present && req2->present;
          } else {
            return req1->present && !req2->present;
          }
          break;
        case TREL_ONLY_OTHER_REGION:
          /* Present req at larger range contradicts negated req at
           * smaller range */
          if (req1->range > req2->range) {
            return req1->present && !req2->present;
          } else {
            return !req1->present && req2->present;
          }
          break;
        default:
          return FALSE;
      }
    }
    if (req1->source.value.tilerel == TREL_SAME_TCLASS
        && req2->source.value.tilerel == TREL_SAME_REGION) {
      /* Same region at any range implies same terrain class at that range
       * and any larger range ~> contradicts negated */
      return (!req1->present && req2->present
              && (req1->range >= req2->range));
    } else if (req2->source.value.tilerel == TREL_SAME_TCLASS
               && req1->source.value.tilerel == TREL_SAME_REGION) {
      /* Same as above */
      return (req1->present && !req2->present
              && (req1->range <= req2->range));
    } else if (req1->source.value.tilerel == TREL_REGION_SURROUNDED
               || req2->source.value.tilerel == TREL_REGION_SURROUNDED) {
      const struct requirement *surr, *other;
      if (req1->source.value.tilerel == TREL_REGION_SURROUNDED) {
        surr = req1;
        other = req2;
      } else {
        surr = req2;
        other = req1;
      }
      if (surr->present && surr->range == REQ_RANGE_TILE) {
        /* Target tile must be part of a surrounded region
         * ~> not the same terrain class
         * ~> not the same region
         * ~> not touched by a third region */
        switch (other->source.value.tilerel) {
        case TREL_SAME_TCLASS:
        case TREL_SAME_REGION:
          return (other->present && other->range == REQ_RANGE_TILE);
        case TREL_ONLY_OTHER_REGION:
          return (!other->present);
        default:
          break;
        }
      }
    }
    /* No further contradictions we can detect */
    return FALSE;
  default:
    /* No special knowledge exists. The requirements aren't the exact
     * opposite of each other per the initial check. */
    return FALSE;
  }
}

/**********************************************************************//**
  Returns the first requirement in the specified requirement vector that
  contradicts the specified requirement or NULL if no contradiction was
  detected.
  @param req the requirement that may contradict the vector
  @param vec the requirement vector to look in
  @return the first local DiplRel requirement.
**************************************************************************/
struct requirement *
req_vec_first_contradiction_in_vec(const struct requirement *req,
                                   const struct requirement_vector *vec)
{
  /* If the requirement is contradicted by any requirement in the vector it
   * contradicts the entire requirement vector. */
  requirement_vector_iterate(vec, preq) {
    if (are_requirements_contradictions(req, preq)) {
      return preq;
    }
  } requirement_vector_iterate_end;

  /* Not a single requirement in the requirement vector is contradicted to be
   * the specified requirement. */
  return nullptr;
}

/**********************************************************************//**
  Returns TRUE if the given requirement contradicts the given requirement
  vector.
**************************************************************************/
bool does_req_contradicts_reqs(const struct requirement *req,
                               const struct requirement_vector *vec)
{
  return req_vec_first_contradiction_in_vec(req, vec) != nullptr;
}

/**********************************************************************//**
  Returns TRUE if tiles are in given requirements range with each other
**************************************************************************/
static inline bool are_tiles_in_range(const struct tile *tile1,
                                      const struct tile *tile2,
                                      enum req_range range)
{
  switch (range) {
  case REQ_RANGE_ADJACENT:
    if (is_tiles_adjacent(tile1, tile2)) {
      return TRUE;
    }
    fc__fallthrough;
  case REQ_RANGE_TILE:
    return same_pos(tile1, tile2);
  case REQ_RANGE_CADJACENT:
    return map_distance(tile1, tile2) <= 1;
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    /* Invalid */
    fc_assert(FALSE);
  }
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
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CITY:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_TILE:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);
  return FALSE;
}

#define IS_REQ_ACTIVE_VARIANT_ASSERT(_kind)                \
{                                                          \
  fc_assert_ret_val(req != nullptr, TRI_MAYBE);            \
  fc_assert_ret_val(req->source.kind == _kind, TRI_MAYBE); \
  fc_assert(context != nullptr);                           \
  fc_assert(other_context != nullptr);                     \
}

/**********************************************************************//**
  Included for completeness. Determine whether a trivial (none) requirement
  is satisfied in a given context (it always is), ignoring parts of the
  requirement that can be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a none requirement
**************************************************************************/
static enum fc_tristate
is_none_req_active(const struct civ_map *nmap,
                   const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_NONE);

  return TRI_YES;
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
  Determine whether a building requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a building requirement
**************************************************************************/
static enum fc_tristate
is_building_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  const struct impr_type *building;

  /* Can't use this assertion, as both VUT_IMPROVEMENT and VUT_SITE
   * are handled here. */
  /* IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_IMPROVEMENT); */

  building = req->source.value.building;

  /* Check if it's certain that the building is obsolete given the
   * specification we have */
  if (req->source.kind == VUT_IMPROVEMENT
      && improvement_obsolete(context->player, building, context->city)) {
    return TRI_NO;
  }

  if (req->survives) {

    /* Check whether condition has ever held, using cached information. */
    switch (req->range) {
    case REQ_RANGE_WORLD:
      return BOOL_TO_TRISTATE(num_world_buildings_total(building) > 0);
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
      if (context->player == nullptr) {
        return TRI_MAYBE;
      }
      players_iterate_alive(plr2) {
        if (players_in_same_range(context->player, plr2, req->range)
            && player_has_ever_built(plr2, building)) {
          return TRI_YES;
        }
      } players_iterate_alive_end;
      return TRI_NO;
    case REQ_RANGE_PLAYER:
      if (context->player == nullptr) {
        return TRI_MAYBE;
      }
      return BOOL_TO_TRISTATE(player_has_ever_built(context->player,
                              building));
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CITY:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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
    switch (req->range) {
    case REQ_RANGE_WORLD:
      return BOOL_TO_TRISTATE(num_world_buildings(building) > 0);
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
      if (context->player == nullptr) {
        return TRI_MAYBE;
      }
      players_iterate_alive(plr2) {
        if (players_in_same_range(context->player, plr2, req->range)
            && num_player_buildings(plr2, building) > 0) {
          return TRI_YES;
        }
      } players_iterate_alive_end;
      return TRI_NO;
    case REQ_RANGE_PLAYER:
      if (context->player == nullptr) {
        return TRI_MAYBE;
      }
      return BOOL_TO_TRISTATE(num_player_buildings(context->player,
                                                   building)
                              > 0);
    case REQ_RANGE_CONTINENT:
      /* At present, "Continent" effects can affect only
       * cities and units in cities. */
      if (context->player && context->city) {
        int continent = tile_continent(context->city->tile);
        return BOOL_TO_TRISTATE(num_continent_buildings(context->player,
                                                        continent, building)
                                > 0);
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_TRADE_ROUTE:
      if (context->city) {
        if (city_has_building(context->city, building)) {
          return TRI_YES;
        } else {
          enum fc_tristate ret = TRI_NO;

          trade_partners_iterate(context->city, trade_partner) {
            if (trade_partner == nullptr) {
              ret = TRI_MAYBE;
            } else if (city_has_building(trade_partner, building)) {
              return TRI_YES;
            }
          } trade_partners_iterate_end;

          return ret;
        }
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_CITY:
      if (context->city) {
        return BOOL_TO_TRISTATE(city_has_building(context->city, building));
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_LOCAL:
      if (context->building) {
        if (context->building == building) {
          return TRI_YES;
        } else {
          return TRI_NO;
        }
      } else {
        /* TODO: Other local targets */
        return TRI_MAYBE;
      }
    case REQ_RANGE_TILE:
      if (context->tile) {
        const struct city *pcity = tile_city(context->tile);

        if (pcity) {
          return BOOL_TO_TRISTATE(city_has_building(pcity, building));
        } else {
          return TRI_NO;
        }
      } else {
        return TRI_MAYBE;
      }
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_COUNT:
      break;
    }

  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);
  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a building genus requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a building genus requirement
**************************************************************************/
static enum fc_tristate
is_buildinggenus_req_active(const struct civ_map *nmap,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_IMPR_GENUS);

  return (context->building ? BOOL_TO_TRISTATE(
                                context->building->genus
                                == req->source.value.impr_genus)
                            : TRI_MAYBE);
}

/**********************************************************************//**
  Is building flag present in a given city?
**************************************************************************/
static enum fc_tristate is_buildingflag_in_city(const struct city *pcity,
                                                enum impr_flag_id flag)
{
  struct player *owner;

  if (pcity == nullptr) {
    return TRI_MAYBE;
  }

  owner = city_owner(pcity);
  city_built_iterate(pcity, impr) {
    if (improvement_has_flag(impr, flag)
        && !improvement_obsolete(owner, impr, pcity)) {
      return TRI_YES;
    }
  } city_built_iterate_end;

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a building flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a building flag requirement
**************************************************************************/
static enum fc_tristate
is_buildingflag_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_IMPR_FLAG);

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    return (context->building
            ? BOOL_TO_TRISTATE(improvement_has_flag(context->building,
                                                    req->source.value.impr_flag))
            : TRI_MAYBE);
  case REQ_RANGE_CITY:
    return is_buildingflag_in_city(context->city, req->source.value.impr_flag);
  case REQ_RANGE_TILE:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }
    return is_buildingflag_in_city(tile_city(context->tile),
                                   req->source.value.impr_flag);
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a player flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a player flag requirement
**************************************************************************/
static enum fc_tristate
is_plr_flag_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_PLAYER_FLAG);

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    return (context->player != nullptr
            ? BOOL_TO_TRISTATE(player_has_flag(context->player,
                                               req->source.value.plr_flag))
            : TRI_MAYBE);
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a player state requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a player state requirement
**************************************************************************/
static enum fc_tristate
is_plr_state_req_active(const struct civ_map *nmap,
                        const struct req_context *context,
                        const struct req_context *other_context,
                        const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_PLAYER_STATE);

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    return (context->player != nullptr
            ? BOOL_TO_TRISTATE(player_has_state(context->player,
                                                req->source.value.plrstate))
            : TRI_MAYBE);
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a tech requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a tech requirement
**************************************************************************/
static enum fc_tristate
is_tech_req_active(const struct civ_map *nmap,
                   const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req)
{
  Tech_type_id tech;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ADVANCE);

  tech = advance_number(req->source.value.advance);

  if (req->survives) {
    fc_assert(req->range == REQ_RANGE_WORLD);
    return BOOL_TO_TRISTATE(game.info.global_advances[tech]);
  }

  /* Not a 'surviving' requirement. */
  switch (req->range) {
  case REQ_RANGE_PLAYER:
    if (context->player != nullptr) {
      return BOOL_TO_TRISTATE(TECH_KNOWN == research_invention_state
                                (research_get(context->player), tech));
    } else {
      return TRI_MAYBE;
    }
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)) {
        if (research_invention_state(research_get(plr2), tech)
            == TECH_KNOWN) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;

    return TRI_NO;
  case REQ_RANGE_LOCAL:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    if (research_get(context->player)->researching == tech) {
      return TRI_YES;
    }
    return TRI_NO;
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a tech flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a techflag requirement
**************************************************************************/
static enum fc_tristate
is_techflag_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  enum tech_flag_id techflag;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TECHFLAG);

  techflag = req->source.value.techflag;

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    if (context->player != nullptr) {
      return BOOL_TO_TRISTATE(player_knows_techs_with_flag(context->player,
                                                           techflag));
    } else {
      return TRI_MAYBE;
    }
    break;
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)
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
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    if (advance_has_flag(research_get(context->player)->researching, techflag)) {
      return TRI_YES;
    }
    return TRI_NO;
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a minimum culture requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a minculture requirement
**************************************************************************/
static enum fc_tristate
is_minculture_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  int minculture;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINCULTURE);

  minculture = req->source.value.minculture;

  switch (req->range) {
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(city_culture(context->city) >= minculture);
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    if (city_culture(context->city) >= minculture) {
      return TRI_YES;
    } else {
      enum fc_tristate ret = TRI_NO;

      trade_partners_iterate(context->city, trade_partner) {
        if (trade_partner == nullptr) {
          ret = TRI_MAYBE;
        } else if (city_culture(trade_partner) >= minculture) {
          return TRI_YES;
        }
      } trade_partners_iterate_end;

      return ret;
    }
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)) {
        if (player_culture(plr2) >= minculture) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a minimum foreign population requirement is satisfied in
  a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a minforeignpct requirement
**************************************************************************/
static enum fc_tristate
is_minforeignpct_req_active(const struct civ_map *nmap,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement *req)
{
  int min_foreign_pct, foreign_pct;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINFOREIGNPCT);

  min_foreign_pct = req->source.value.minforeignpct;

  switch (req->range) {
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    foreign_pct = citizens_nation_foreign(context->city) * 100
      / city_size_get(context->city);
    return BOOL_TO_TRISTATE(foreign_pct >= min_foreign_pct);
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    foreign_pct = citizens_nation_foreign(context->city) * 100
      / city_size_get(context->city);
    if (foreign_pct >= min_foreign_pct) {
      return TRI_YES;
    } else {
      enum fc_tristate ret = TRI_NO;

      trade_partners_iterate(context->city, trade_partner) {
        if (trade_partner == nullptr) {
          ret = TRI_MAYBE;
        } else {
          foreign_pct = citizens_nation_foreign(trade_partner) * 100
            / city_size_get(trade_partner);
          if (foreign_pct >= min_foreign_pct) {
            return TRI_YES;
          }
        }
      } trade_partners_iterate_end;

      return ret;
    }
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a maximum total units on tile requirement is satisfied in
  a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a maxunitsontile requirement
**************************************************************************/
static enum fc_tristate
is_maxtotalunitsontile_req_active(const struct civ_map *nmap,
                                  const struct req_context *context,
                                  const struct req_context *other_context,
                                  const struct requirement *req)
{
  int max_units;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MAXTILETOTALUNITS);

  max_units = req->source.value.max_tile_total_units;

  /* TODO: If can't see V_INVIS -> TRI_MAYBE */
  switch (req->range) {
  case REQ_RANGE_TILE:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(unit_list_size(context->tile->units) <= max_units);
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    if (unit_list_size(context->tile->units) <= max_units) {
      return TRI_YES;
    }
    cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
      if (unit_list_size(adjc_tile->units) <= max_units) {
        return TRI_YES;
      }
    } cardinal_adjc_iterate_end;
    return TRI_NO;
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    if (unit_list_size(context->tile->units) <= max_units) {
      return TRI_YES;
    }
    adjc_iterate(nmap, context->tile, adjc_tile) {
      if (unit_list_size(adjc_tile->units) <= max_units) {
        return TRI_YES;
      }
    } adjc_iterate_end;
    return TRI_NO;
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}


/**********************************************************************//**
  Determine whether a maximum top units on tile requirement is satisfied in
  a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a maxunitsontile requirement
**************************************************************************/
static enum fc_tristate
is_maxtopunitsontile_req_active(const struct civ_map *nmap,
                                const struct req_context *context,
                                const struct req_context *other_context,
                                const struct requirement *req)
{
  int max_units;
  int count;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MAXTILETOPUNITS);

  max_units = req->source.value.max_tile_top_units;

  /* TODO: If can't see V_INVIS -> TRI_MAYBE */
  switch (req->range) {
  case REQ_RANGE_TILE:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    count = 0;
    unit_list_iterate(context->tile->units, punit) {
      if (!unit_transported(punit)) {
        count++;
      }
    } unit_list_iterate_end;
    return BOOL_TO_TRISTATE(count <= max_units);
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    count = 0;
    unit_list_iterate(context->tile->units, punit) {
      if (!unit_transported(punit)) {
        count++;
      }
    } unit_list_iterate_end;
    if (count <= max_units) {
      return TRI_YES;
    }
    cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
      count = 0;
      unit_list_iterate(adjc_tile->units, punit) {
        if (!unit_transported(punit)) {
          count++;
        }
      } unit_list_iterate_end;
      if (count <= max_units) {
        return TRI_YES;
      }
    } cardinal_adjc_iterate_end;

    return TRI_NO;
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    count = 0;
    unit_list_iterate(context->tile->units, punit) {
      if (!unit_transported(punit)) {
        count++;
      }
    } unit_list_iterate_end;
    if (count <= max_units) {
      return TRI_YES;
    }
    adjc_iterate(nmap, context->tile, adjc_tile) {
      count = 0;
      unit_list_iterate(adjc_tile->units, punit) {
        if (!unit_transported(punit)) {
          count++;
        }
      } unit_list_iterate_end;
      if (count <= max_units) {
        return TRI_YES;
      }
    } adjc_iterate_end;
    return TRI_NO;
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether an extra requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be an extra requirement
**************************************************************************/
static enum fc_tristate
is_extra_req_active(const struct civ_map *nmap,
                    const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req)
{
  const struct extra_type *pextra;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_EXTRA);

  pextra = req->source.value.extra;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->extra) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(context->extra == pextra);
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has extra of requested type. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(context->tile, pextra));
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(context->tile, pextra)
                            || is_extra_card_near(nmap, context->tile, pextra));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra(context->tile, pextra)
                            || is_extra_near_tile(nmap, context->tile, pextra));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_extra(ptile, pextra)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;

  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_extra(ptile, pextra)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          if (tile_has_extra(ptile, pextra)) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;

  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a tiledef requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a tiledef requirement
**************************************************************************/
static enum fc_tristate
is_tiledef_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  const struct tiledef *ptdef;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TILEDEF);

  ptdef = req->source.value.tiledef;

  switch (req->range) {
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has tiledef of requested type. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_matches_tiledef(ptdef, context->tile));
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_matches_tiledef(ptdef, context->tile)
                            || is_tiledef_card_near(nmap, context->tile, ptdef));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_matches_tiledef(ptdef, context->tile)
                            || is_tiledef_near_tile(nmap, context->tile, ptdef));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_matches_tiledef(ptdef, ptile)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;

  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_matches_tiledef(ptdef, ptile)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          if (tile_matches_tiledef(ptdef, ptile)) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;

  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a goods requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a goods requirement
**************************************************************************/
static enum fc_tristate
is_good_req_active(const struct civ_map *nmap,
                   const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req)
{
  const struct goods_type *pgood;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_GOOD);

  pgood = req->source.value.good;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_CITY:
    /* The requirement is filled if the city imports good of requested type. */
    if (!context->city) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(city_receives_goods(context->city, pgood)
                            || (goods_has_flag(pgood, GF_SELF_PROVIDED)
                                && goods_can_be_provided(context->city, pgood,
                                                         nullptr)));
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether an action requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be an action requirement
**************************************************************************/
static enum fc_tristate
is_action_req_active(const struct civ_map *nmap,
                     const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ACTION);

  if (context->action) {
    return BOOL_TO_TRISTATE(action_number(context->action)
                            == action_number(req->source.value.action));
  }

  if (context->unit != nullptr && context->unit->action != ACTION_NONE) {
    log_normal("Unit action %s", action_id_rule_name(context->unit->action));
    return BOOL_TO_TRISTATE(context->unit->action
                            == action_number(req->source.value.action));
  }

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether an output type requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be an output type requirement
**************************************************************************/
static enum fc_tristate
is_outputtype_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_OTYPE);

  return BOOL_TO_TRISTATE(context->output
                          && context->output->index
                             == req->source.value.outputtype);
}

/**********************************************************************//**
  Determine whether a specialist requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a specialist requirement
**************************************************************************/
static enum fc_tristate
is_specialist_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_SPECIALIST);

  return BOOL_TO_TRISTATE(context->specialist
                          && context->specialist
                             == req->source.value.specialist);
}

/**********************************************************************//**
  Determine whether a terrain type requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a terrain requirement
**************************************************************************/
static enum fc_tristate
is_terrain_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  const struct terrain *pterrain;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TERRAIN);

  pterrain = req->source.value.terrain;

  switch (req->range) {
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has the terrain. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return pterrain && tile_terrain(context->tile) == pterrain;
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(pterrain && is_terrain_card_near(nmap, context->tile, pterrain, TRUE));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(pterrain && is_terrain_near_tile(nmap, context->tile, pterrain, TRUE));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    if (pterrain != nullptr) {
      city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                        city_tile(context->city), ptile) {
        if (tile_terrain(ptile) == pterrain) {
          return TRI_YES;
        }
      } city_tile_iterate_end;
    }
    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    if (pterrain != nullptr) {
      enum fc_tristate ret;

      city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                        city_tile(context->city), ptile) {
        if (tile_terrain(ptile) == pterrain) {
          return TRI_YES;
        }
      } city_tile_iterate_end;

      ret = TRI_NO;
      trade_partners_iterate(context->city, trade_partner) {
        if (trade_partner == nullptr) {
          ret = TRI_MAYBE;
        } else {
          city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                            city_tile(trade_partner), ptile) {
            if (tile_terrain(ptile) == pterrain) {
              return TRI_YES;
            }
          } city_tile_iterate_end;
        }
      } trade_partners_iterate_end;

      return ret;
    }

    return TRI_MAYBE;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a terrain class requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a terrain class requirement
**************************************************************************/
static enum fc_tristate
is_terrainclass_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  enum terrain_class pclass;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TERRAINCLASS);

  pclass = req->source.value.terrainclass;

  switch (req->range) {
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has the terrain of correct class. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(context->tile)) == pclass);
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(context->tile)) == pclass
                            || is_terrain_class_card_near(nmap, context->tile, pclass));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_type_terrain_class(tile_terrain(context->tile)) == pclass
                            || is_terrain_class_near_tile(nmap, context->tile, pclass));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);

      if (pterrain != T_UNKNOWN
          && terrain_type_terrain_class(pterrain) == pclass) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);

      if (pterrain != T_UNKNOWN
          && terrain_type_terrain_class(pterrain) == pclass) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          const struct terrain *pterrain = tile_terrain(ptile);

          if (pterrain != T_UNKNOWN
              && terrain_type_terrain_class(pterrain) == pclass) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a terrain flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a terrain flag requirement
**************************************************************************/
static enum fc_tristate
is_terrainflag_req_active(const struct civ_map *nmap,
                          const struct req_context *context,
                          const struct req_context *other_context,
                          const struct requirement *req)
{
  enum terrain_flag_id terrflag;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TERRFLAG);

  terrflag = req->source.value.terrainflag;

  switch (req->range) {
  case REQ_RANGE_TILE:
    /* The requirement is fulfilled if the tile has a terrain with
     * correct flag. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(context->tile),
                                             terrflag));
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(context->tile),
                                             terrflag)
                            || is_terrain_flag_card_near(nmap, context->tile,
                                                         terrflag));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(terrain_has_flag(tile_terrain(context->tile),
                                             terrflag)
                            || is_terrain_flag_near_tile(nmap, context->tile,
                                                         terrflag));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);

      if (pterrain != T_UNKNOWN
          && terrain_has_flag(pterrain, terrflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      const struct terrain *pterrain = tile_terrain(ptile);

      if (pterrain != T_UNKNOWN
          && terrain_has_flag(pterrain, terrflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          const struct terrain *pterrain = tile_terrain(ptile);

          if (pterrain != T_UNKNOWN
              && terrain_has_flag(pterrain, terrflag)) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a road flag requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a roadflag requirement
**************************************************************************/
static enum fc_tristate
is_roadflag_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  enum road_flag_id roadflag;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ROADFLAG);

  roadflag = req->source.value.roadflag;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    {
      if (!context->extra) {
        return TRI_MAYBE;
      }
      struct road_type *r = extra_road_get(context->extra);

      return BOOL_TO_TRISTATE(
          r && road_has_flag(r, roadflag)
      );
    }
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has a road with correct flag. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(context->tile, roadflag));
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(context->tile, roadflag)
                            || is_road_flag_card_near(nmap, context->tile,
                                                      roadflag));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_road_flag(context->tile, roadflag)
                            || is_road_flag_near_tile(nmap, context->tile,
                                                      roadflag));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_road_flag(ptile, roadflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_road_flag(ptile, roadflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          if (tile_has_road_flag(ptile, roadflag)) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether an extra flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be an extraflag requirement
**************************************************************************/
static enum fc_tristate
is_extraflag_req_active(const struct civ_map *nmap,
                        const struct req_context *context,
                        const struct req_context *other_context,
                        const struct requirement *req)
{
  enum extra_flag_id extraflag;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_EXTRAFLAG);

  extraflag = req->source.value.extraflag;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->extra) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(extra_has_flag(context->extra, extraflag));
  case REQ_RANGE_TILE:
    /* The requirement is filled if the tile has an extra with correct flag. */
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(context->tile, extraflag));
  case REQ_RANGE_CADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(context->tile, extraflag)
                            || is_extra_flag_card_near(nmap, context->tile, extraflag));
  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(tile_has_extra_flag(context->tile, extraflag)
                            || is_extra_flag_near_tile(nmap, context->tile, extraflag));
  case REQ_RANGE_CITY:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_extra_flag(ptile, extraflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (!context->city) {
      return TRI_MAYBE;
    }
    city_tile_iterate(nmap, city_map_radius_sq_get(context->city),
                      city_tile(context->city), ptile) {
      if (tile_has_extra_flag(ptile, extraflag)) {
        return TRI_YES;
      }
    } city_tile_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        city_tile_iterate(nmap, city_map_radius_sq_get(trade_partner),
                          city_tile(trade_partner), ptile) {
          if (tile_has_extra_flag(ptile, extraflag)) {
            return TRI_YES;
          }
        } city_tile_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a terrain-alteration-possible (terrainalter) requirement
  is satisfied in a given context, ignoring parts of the requirement that
  can be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a terrainalter requirement
**************************************************************************/
static enum fc_tristate
is_terrainalter_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  enum terrain_alteration alteration;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TERRAINALTER);

  alteration = req->source.value.terrainalter;

  if (!context->tile) {
    return TRI_MAYBE;
  }

  switch (req->range) {
  case REQ_RANGE_TILE:
    return BOOL_TO_TRISTATE(terrain_can_support_alteration(
                                tile_terrain(context->tile), alteration));
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT: /* XXX Could in principle support ADJACENT. */
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a government (gov) requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a gov requirement
**************************************************************************/
static enum fc_tristate
is_gov_req_active(const struct civ_map *nmap,
                  const struct req_context *context,
                  const struct req_context *other_context,
                  const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_GOVERNMENT);

  if (context->player == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(government_of_player(context->player)
                            == req->source.value.govern);
  }
}

/**********************************************************************//**
  Determine whether a government flag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a gov flag requirement
**************************************************************************/
static enum fc_tristate
is_govflag_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_GOVFLAG);

  if (context->player == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(BV_ISSET(government_of_player(context->player)->flags,
                                     req->source.value.govflag));
  }
}

/**********************************************************************//**
  Determine whether a style requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a style requirement
**************************************************************************/
static enum fc_tristate
is_style_req_active(const struct civ_map *nmap,
                    const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_STYLE);

  if (context->player == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(context->player->style
                            == req->source.value.style);
  }
}

/**********************************************************************//**
  Determine whether a minimum technologies requirement is satisfied in a
  given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a mintechs requirement
**************************************************************************/
static enum fc_tristate
is_mintechs_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINTECHS);

  switch (req->range) {
  case REQ_RANGE_WORLD:
    /* "None" does not count */
    return ((game.info.global_advance_count - 1)
            >= req->source.value.min_techs);
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    } else {
      /* "None" does not count */
      return BOOL_TO_TRISTATE(
                  (research_get(context->player)->techs_researched - 1)
                  >= req->source.value.min_techs
              );
    }
  default:
    return TRI_MAYBE;
  }
}

/**********************************************************************//**
  Determine whether a minimum future technologies requirement is satisfied
  in a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a mintechs requirement
**************************************************************************/
static enum fc_tristate
is_futuretechs_req_active(const struct civ_map *nmap,
                          const struct req_context *context,
                          const struct req_context *other_context,
                          const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_FUTURETECHS);

  switch (req->range) {
  case REQ_RANGE_WORLD:
    players_iterate_alive(plr) {
      if (research_get(plr)->future_tech
          >= req->source.value.future_techs) {
        return TRI_YES;
      }
    } players_iterate_alive_end;

    return TRI_NO;
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    } else {
      return BOOL_TO_TRISTATE(research_get(context->player)->future_tech
                              >= req->source.value.future_techs);
    }
  default:
    return TRI_MAYBE;
  }
}

/**********************************************************************//**
  Determine whether a minimum cities count requirement is satisfied in a
  given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a mincities requirement
**************************************************************************/
static enum fc_tristate
is_mincities_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINCITIES);

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    } else {
      /* "None" does not count */
      return BOOL_TO_TRISTATE(
                  city_list_size(context->player->cities)
                  >= req->source.value.min_cities
              );
    }
  default:
    return TRI_MAYBE;
  }
}

/**********************************************************************//**
  Determine whether an AI level requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be an AI level requirement
**************************************************************************/
static enum fc_tristate
is_ai_req_active(const struct civ_map *nmap,
                 const struct req_context *context,
                 const struct req_context *other_context,
                 const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_AI_LEVEL);

  if (context->player == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(is_ai(context->player)
                            && context->player->ai_common.skill_level
                               == req->source.value.ai_level);
  }
}

/**********************************************************************//**
  Determine whether a nation requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a nation requirement
**************************************************************************/
static enum fc_tristate
is_nation_req_active(const struct civ_map *nmap,
                     const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement *req)
{
  const struct nation_type *nation;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_NATION);

  nation = req->source.value.nation;

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(nation_of_player(context->player) == nation);
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)) {
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
    return BOOL_TO_TRISTATE(nation->player != nullptr
                            && (req->survives || nation->player->is_alive));
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a nation group requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a nation group requirement
**************************************************************************/
static enum fc_tristate
is_nationgroup_req_active(const struct civ_map *nmap,
                          const struct req_context *context,
                          const struct req_context *other_context,
                          const struct requirement *req)
{
  const struct nation_group *ngroup;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_NATIONGROUP);

  ngroup = req->source.value.nationgroup;

  switch (req->range) {
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(nation_is_in_group(
        nation_of_player(context->player), ngroup));
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    }
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)) {
        if (nation_is_in_group(nation_of_player(plr2), ngroup)) {
          return TRI_YES;
        }
      }
    } players_iterate_alive_end;
    return TRI_NO;
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a nationality requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a nationality requirement
**************************************************************************/
static enum fc_tristate
is_nationality_req_active(const struct civ_map *nmap,
                          const struct req_context *context,
                          const struct req_context *other_context,
                          const struct requirement *req)
{
  const struct nation_type *nationality;
  enum fc_tristate ret;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_NATIONALITY);

  nationality = req->source.value.nationality;

  switch (req->range) {
  case REQ_RANGE_CITY:
    if (context->city == nullptr) {
     return TRI_MAYBE;
    }
    citizens_iterate(context->city, slot, count) {
      if (player_slot_get_player(slot)->nation == nationality) {
        return TRI_YES;
      }
    } citizens_iterate_end;

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
    if (context->city == nullptr) {
      return TRI_MAYBE;
    }
    citizens_iterate(context->city, slot, count) {
      if (player_slot_get_player(slot)->nation == nationality) {
        return TRI_YES;
      }
    } citizens_iterate_end;

    ret = TRI_NO;
    trade_partners_iterate(context->city, trade_partner) {
      if (trade_partner == nullptr) {
        ret = TRI_MAYBE;
      } else {
        citizens_iterate(trade_partner, slot, count) {
          if (player_slot_get_player(slot)->nation == nationality) {
            return TRI_YES;
          }
        } citizens_iterate_end;
      }
    } trade_partners_iterate_end;

    return ret;
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether an original owner requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be an original owner requirement
**************************************************************************/
static enum fc_tristate
is_originalowner_req_active(const struct civ_map *nmap,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement *req)
{
  const struct nation_type *nation;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ORIGINAL_OWNER);

  nation = req->source.value.origowner;

  switch (req->range) {
  case REQ_RANGE_CITY:
    if (context->city == nullptr || context->city->original == nullptr) {
      return TRI_MAYBE;
    }
    if (player_nation(context->city->original) == nation) {
      return TRI_YES;
    }

    return TRI_NO;
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

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
    if (target_player == nullptr) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(is_diplrel_to_other(target_player, diplrel));
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
    if (target_player == nullptr) {
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
    if (target_player == nullptr || other_player == nullptr) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(is_diplrel_between(target_player, other_player, diplrel));
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid range %d.", range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a diplomatic relationship (diplrel) requirement is
  satisfied in a given context, ignoring parts of the requirement that can
  be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a diplrel requirement
**************************************************************************/
static enum fc_tristate
is_diplrel_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_DIPLREL);

  return is_diplrel_in_range(context->player, other_context->player,
                             req->range, req->source.value.diplrel);
}

/**********************************************************************//**
  Determine whether a tile owner diplomatic relationship (diplrel_tile)
  requirement is satisfied in a given context, ignoring parts of the
  requirement that can be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a diplrel_tile requirement
**************************************************************************/
static enum fc_tristate
is_diplrel_tile_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_DIPLREL_TILE);

  return is_diplrel_in_range(context->tile ? tile_owner(context->tile)
                                           : nullptr,
                             context->player,
                             req->range,
                             req->source.value.diplrel);
}

/**********************************************************************//**
  Determine whether a tile owner / other player diplomatic relationship
  (diplrel_tile_o) requirement is satisfied in a given context, ignoring
  parts of the requirement that can be handled uniformly for all requirement
  types.

  context, other_context and req must not be null,
  and req must be a diplrel_tile_o requirement
**************************************************************************/
static enum fc_tristate
is_diplrel_tile_o_req_active(const struct civ_map *nmap,
                             const struct req_context *context,
                             const struct req_context *other_context,
                             const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_DIPLREL_TILE_O);

  return is_diplrel_in_range(context->tile ? tile_owner(context->tile)
                                           : nullptr,
                             other_context->player,
                             req->range,
                             req->source.value.diplrel);
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

  if (target_tile == nullptr) {
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
  Determine whether an any-unit-owner diplomatic relationship
  (diplrel_unitany) requirement is satisfied in a given context, ignoring
  parts of the requirement that can be handled uniformly for all requirement
  types.

  context, other_context and req must not be null,
  and req must be a diplrel_unitany requirement
**************************************************************************/
static enum fc_tristate
is_diplrel_unitany_req_active(const struct civ_map *nmap,
                              const struct req_context *context,
                              const struct req_context *other_context,
                              const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_DIPLREL_UNITANY);

  return is_diplrel_unitany_in_range(context->tile, context->player,
                                     req->range,
                                     req->source.value.diplrel);
}

/**********************************************************************//**
  Determine whether an any-unit-owner / other player diplomatic relationship
  (diplrel_unitany_o) requirement is satisfied in a given context, ignoring
  parts of the requirement that can be handled uniformly for all requirement
  types.

  context, other_context and req must not be null,
  and req must be a diplrel_unitany_o requirement
**************************************************************************/
static enum fc_tristate
is_diplrel_unitany_o_req_active(const struct civ_map *nmap,
                                const struct req_context *context,
                                const struct req_context *other_context,
                                const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_DIPLREL_UNITANY_O);

  return is_diplrel_unitany_in_range(context->tile, other_context->player,
                                     req->range,
                                     req->source.value.diplrel);
}

/**********************************************************************//**
  Determine whether a unittype requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a unittype requirement
**************************************************************************/
static enum fc_tristate
is_unittype_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  const struct unit_type *punittype;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_UTYPE);

  punittype = req->source.value.utype;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->unittype) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(context->unittype == punittype);
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }

    unit_list_iterate(context->tile->units, punit) {
      if (punit->utype == punittype) {
        return TRI_YES;
      }
    } unit_list_iterate_end;

    if (req->range == REQ_RANGE_TILE) {
      return TRI_NO;
    }

    if (req->range == REQ_RANGE_CADJACENT) {
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (punit->utype == punittype) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } cardinal_adjc_iterate_end;
    } else {
      fc_assert(req->range == REQ_RANGE_ADJACENT);

      adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (punit->utype == punittype) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } adjc_iterate_end;
    }

    return TRI_NO;

  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    fc_assert(FALSE);
    break;
  }

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a unitflag requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a unitflag requirement
**************************************************************************/
static enum fc_tristate
is_unitflag_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  enum unit_type_flag_id unitflag;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_UTFLAG);

  unitflag = req->source.value.unitflag;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->unittype) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(utype_has_flag(context->unittype, unitflag));
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }

    unit_list_iterate(context->tile->units, punit) {
      if (unit_has_type_flag(punit, unitflag)) {
        return TRI_YES;
      }
    } unit_list_iterate_end;

    if (req->range == REQ_RANGE_TILE) {
      return TRI_NO;
    }

    if (req->range == REQ_RANGE_CADJACENT) {
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_has_type_flag(punit, unitflag)) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } cardinal_adjc_iterate_end;
    } else {
      fc_assert(req->range == REQ_RANGE_ADJACENT);

      adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_has_type_flag(punit, unitflag)) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } adjc_iterate_end;
    }

     return TRI_NO;

  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    fc_assert(FALSE);
    break;
  }

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a unitclass requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a unitclass requirement
**************************************************************************/
static enum fc_tristate
is_unitclass_req_active(const struct civ_map *nmap,
                        const struct req_context *context,
                        const struct req_context *other_context,
                        const struct requirement *req)
{
  const struct unit_class *pclass;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_UCLASS);

  pclass = req->source.value.uclass;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->unittype) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(utype_class(context->unittype) == pclass);
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }

    unit_list_iterate(context->tile->units, punit) {
      if (unit_class_get(punit) == pclass) {
        return TRI_YES;
      }
    } unit_list_iterate_end;

    if (req->range == REQ_RANGE_TILE) {
      return TRI_NO;
    }

    if (req->range == REQ_RANGE_CADJACENT) {
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_class_get(punit) == pclass) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } cardinal_adjc_iterate_end;
    } else {
      fc_assert(req->range == REQ_RANGE_ADJACENT);

      adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_class_get(punit) == pclass) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } adjc_iterate_end;
    }

     return TRI_NO;

  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    fc_assert(FALSE);
    break;
  }

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a unitclassflag requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a unitclassflag requirement
**************************************************************************/
static enum fc_tristate
is_unitclassflag_req_active(const struct civ_map *nmap,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement *req)
{
  enum unit_class_flag_id ucflag;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_UCFLAG);

  ucflag = req->source.value.unitclassflag;

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (!context->unittype) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(uclass_has_flag(utype_class(context->unittype), ucflag));
  case REQ_RANGE_TILE:
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }

    unit_list_iterate(context->tile->units, punit) {
      if (unit_has_class_flag(punit, ucflag)) {
        return TRI_YES;
      }
    } unit_list_iterate_end;

    if (req->range == REQ_RANGE_TILE) {
      return TRI_NO;
    }

    if (req->range == REQ_RANGE_CADJACENT) {
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_has_class_flag(punit, ucflag)) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } cardinal_adjc_iterate_end;
    } else {
      fc_assert(req->range == REQ_RANGE_ADJACENT);

      adjc_iterate(nmap, context->tile, adjc_tile) {
        unit_list_iterate(adjc_tile->units, punit) {
          if (unit_has_class_flag(punit, ucflag)) {
            return TRI_YES;
          }
        } unit_list_iterate_end;
      } adjc_iterate_end;
    }

    return TRI_NO;

  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_WORLD:
  case REQ_RANGE_COUNT:
    fc_assert(FALSE);
    break;
  }

  return TRI_NO;
}

/**********************************************************************//**
  Determine whether a unitstate requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a unitstate requirement
**************************************************************************/
static enum fc_tristate
is_unitstate_req_active(const struct civ_map *nmap,
                        const struct req_context *context,
                        const struct req_context *other_context,
                        const struct requirement *req)
{
  enum ustate_prop uprop;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_UNITSTATE);

  uprop = req->source.value.unit_state;

  fc_assert_ret_val_msg(req->range == REQ_RANGE_LOCAL, TRI_NO,
                        "Unsupported range \"%s\"",
                        req_range_name(req->range));

  /* Could be asked with incomplete data.
   * is_req_active() will handle it based on prob_type. */
  if (context->unit == nullptr) {
    return TRI_MAYBE;
  }

  switch (uprop) {
  case USP_TRANSPORTED:
    return BOOL_TO_TRISTATE(context->unit->transporter != nullptr);
  case USP_LIVABLE_TILE:
    return BOOL_TO_TRISTATE(
          can_unit_exist_at_tile(nmap, context->unit,
                                 unit_tile(context->unit)));
    break;
  case USP_TRANSPORTING:
    return BOOL_TO_TRISTATE(0 < get_transporter_occupancy(context->unit));
  case USP_HAS_HOME_CITY:
    return BOOL_TO_TRISTATE(context->unit->homecity > 0);
  case USP_NATIVE_TILE:
    return BOOL_TO_TRISTATE(
        is_native_tile(unit_type_get(context->unit),
                       unit_tile(context->unit)));
    break;
  case USP_NATIVE_EXTRA:
    return BOOL_TO_TRISTATE(
        tile_has_native_base(unit_tile(context->unit),
                             unit_type_get(context->unit)));
    break;
  case USP_MOVED_THIS_TURN:
    return BOOL_TO_TRISTATE(context->unit->moved);
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
  Determine whether an activity requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be an activity requirement
**************************************************************************/
static enum fc_tristate
is_activity_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  enum unit_activity activity;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ACTIVITY);

  activity = req->source.value.activity;

  fc_assert_ret_val_msg(req->range == REQ_RANGE_LOCAL, TRI_NO,
                        "Unsupported range \"%s\"",
                        req_range_name(req->range));

  if (context->unit == nullptr) {
    /* FIXME: Excluding ACTIVITY_IDLE here is a bit ugly, but done because
     *        it's the zero value that context has by default - so many callers
     *        who meant not to set specific activity actually have ACTIVITY_IDLE
     *        instead of ACTIVITY_LAST */
    if (context->activity != ACTIVITY_LAST && context->activity != ACTIVITY_IDLE) {
      return BOOL_TO_TRISTATE(activity == context->activity);
    }

    /* Could be asked with incomplete data.
     * is_req_active() will handle it based on prob_type. */
    return TRI_MAYBE;
  }

  switch (context->unit->activity) {
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

  return BOOL_TO_TRISTATE(context->unit->activity == activity);
}

/**********************************************************************//**
  Determine whether a minimum veteran level (minveteran) requirement is
  satisfied in a given context, ignoring parts of the requirement that can
  be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a minveteran requirement
**************************************************************************/
static enum fc_tristate
is_minveteran_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINVETERAN);

  if (context->unit == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(context->unit->veteran
                            >= req->source.value.minveteran);
  }
}

/**********************************************************************//**
  Determine whether a minimum movement fragments (minmovefrags) requirement
  is satisfied in a given context, ignoring parts of the requirement that
  can be handled uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a minmovefrags requirement
**************************************************************************/
static enum fc_tristate
is_minmovefrags_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINMOVES);

  if (context->unit == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(req->source.value.minmoves
                            <= context->unit->moves_left);
  }
}

/**********************************************************************//**
  Determine whether a minimum hitpoints requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a minhitpoints requirement
**************************************************************************/
static enum fc_tristate
is_minhitpoints_req_active(const struct civ_map *nmap,
                           const struct req_context *context,
                           const struct req_context *other_context,
                           const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINHP);

  if (context->unit == nullptr) {
    return TRI_MAYBE;
  } else {
    return BOOL_TO_TRISTATE(req->source.value.min_hit_points
                            <= context->unit->hp);
  }
}

/**********************************************************************//**
  Determine whether an age requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be an age requirement
**************************************************************************/
static enum fc_tristate
is_age_req_active(const struct civ_map *nmap,
                  const struct req_context *context,
                  const struct req_context *other_context,
                  const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_AGE);

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (context->unit == nullptr || !is_server()) {
      return TRI_MAYBE;
    } else {
      return BOOL_TO_TRISTATE(
                req->source.value.age <=
                game.info.turn - context->unit->birth_turn);
    }
    break;
  case REQ_RANGE_CITY:
    if (context->city == nullptr) {
      return TRI_MAYBE;
    } else {
      return BOOL_TO_TRISTATE(
                req->source.value.age <=
                game.info.turn - context->city->turn_founded);
    }
    break;
  case REQ_RANGE_PLAYER:
    if (context->player == nullptr) {
      return TRI_MAYBE;
    } else {
      return BOOL_TO_TRISTATE(req->source.value.age
                              <= player_age(context->player));
    }
    break;
  default:
    return TRI_MAYBE;
    break;
  }
}

/**********************************************************************//**
  Determine whether a form age requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a form age requirement
**************************************************************************/
static enum fc_tristate
is_form_age_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_FORM_AGE);

  switch (req->range) {
  case REQ_RANGE_LOCAL:
    if (context->unit == nullptr || !is_server()) {
      return TRI_MAYBE;
    } else {
      return BOOL_TO_TRISTATE(
                req->source.value.form_age <=
                game.info.turn - context->unit->current_form_turn);
    }
    break;
  default:
    return TRI_MAYBE;
    break;
  }
}

/**********************************************************************//**
  Determine whether the given continent or ocean might be surrounded by a
  specific desired surrounder.
**************************************************************************/
static inline enum fc_tristate
does_region_surrounder_match(Continent_id cont, Continent_id surrounder)
{
  Continent_id actual_surrounder;
  bool whole_known;

  if (cont > 0) {
    actual_surrounder = get_island_surrounder(cont);
    whole_known = is_whole_continent_known(cont);

    if (actual_surrounder > 0) {
      return TRI_NO;
    }
  } else if (cont < 0) {
    actual_surrounder = get_lake_surrounder(cont);
    whole_known = is_whole_ocean_known(-cont);

    if (actual_surrounder < 0) {
      return TRI_NO;
    }
  } else {
    return TRI_MAYBE;
  }

  if (actual_surrounder == 0 || surrounder == 0) {
    return TRI_MAYBE;
  } else if (actual_surrounder != surrounder) {
    return TRI_NO;
  } else if (!whole_known) {
    return TRI_MAYBE;
  } else {
    return TRI_YES;
  }
}

/**********************************************************************//**
  Determine whether a tile relationship requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a tile relationship requirement
**************************************************************************/
static enum fc_tristate
is_tile_rel_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TILE_REL);

  if (context->tile == nullptr || other_context->tile == nullptr) {
    /* Note: For some values, we might be able to give a definitive
     * TRI_NO answer even if one of the tiles is missing, but that's
     * probably not worth the added effort. */
    return TRI_MAYBE;
  }

  switch (req->source.value.tilerel) {
  case TREL_SAME_TCLASS:
    if (tile_terrain(other_context->tile) == T_UNKNOWN) {
      return TRI_MAYBE;
    }
    fc_assert_ret_val_msg((req->range == REQ_RANGE_TILE
                           || req->range == REQ_RANGE_CADJACENT
                           || req->range == REQ_RANGE_ADJACENT),
                          TRI_MAYBE,
                          "Invalid range %d for tile relation \"%s\" req",
                          req->range, tilerel_type_name(TREL_SAME_TCLASS));
    {
      enum terrain_class cls = terrain_type_terrain_class(
          tile_terrain(other_context->tile));
      bool seen_unknown = FALSE;
      const struct terrain *terr;

      if ((terr = tile_terrain(context->tile)) == T_UNKNOWN) {
        seen_unknown = TRUE;
      } else if (terrain_type_terrain_class(terr) == cls) {
        return TRUE;
      }

      range_adjc_iterate(nmap, context->tile, req->range, adj_tile) {
        if ((terr = tile_terrain(adj_tile)) == T_UNKNOWN) {
          seen_unknown = TRUE;
        } else if (terrain_type_terrain_class(terr) == cls) {
          return TRUE;
        }
      } range_adjc_iterate_end;

      if (seen_unknown) {
        return TRI_MAYBE;
      } else {
        return TRI_NO;
      }
    }
    break;
  case TREL_SAME_REGION:
    if (tile_continent(other_context->tile) == 0) {
      return TRI_MAYBE;
    }
    fc_assert_ret_val_msg((req->range == REQ_RANGE_TILE
                           || req->range == REQ_RANGE_CADJACENT
                           || req->range == REQ_RANGE_ADJACENT),
                          TRI_MAYBE,
                          "Invalid range %d for tile relation \"%s\" req",
                          req->range, tilerel_type_name(TREL_SAME_REGION));

    if (tile_continent(context->tile)
        == tile_continent(other_context->tile)) {
      return TRI_YES;
    } else {
      bool seen_unknown = (tile_continent(context->tile) == 0);
      Continent_id cont = tile_continent(other_context->tile);

      range_adjc_iterate(nmap, context->tile, req->range, adj_tile) {
        Continent_id adj_cont = tile_continent(adj_tile);

        if (adj_cont == cont) {
          return TRI_YES;
        } else if (adj_cont == 0) {
          seen_unknown = TRUE;
        }
      } range_adjc_iterate_end;

      if (seen_unknown) {
        return TRI_MAYBE;
      } else {
        return TRI_NO;
      }
    }
    break;
  case TREL_ONLY_OTHER_REGION:
    if (tile_continent(context->tile) == 0
        || tile_continent(other_context->tile) == 0) {
      /* Note: We could still give a definitive TRI_NO answer if there are
       * too many different adjacent continents, but that's probably not
       * worth the added effort. */
      return TRI_MAYBE;
    }
    fc_assert_ret_val_msg((req->range == REQ_RANGE_CADJACENT
                           || req->range == REQ_RANGE_ADJACENT),
                          TRI_MAYBE,
                          "Invalid range %d for tile relation \"%s\" req",
                          req->range,
                          tilerel_type_name(TREL_ONLY_OTHER_REGION));

    {
      bool seen_unknown = FALSE;
      Continent_id cont = tile_continent(context->tile);
      Continent_id other_cont = tile_continent(other_context->tile);

      range_adjc_iterate(nmap, context->tile, req->range, adj_tile) {
        Continent_id adj_cont = tile_continent(adj_tile);

        if (adj_cont == 0) {
          seen_unknown = TRUE;
        } else if (adj_cont != cont && adj_cont != other_cont) {
          return TRI_NO;
        }
      } range_adjc_iterate_end;

      if (seen_unknown) {
        return TRI_MAYBE;
      } else {
        return TRI_YES;
      }
    }
    break;
  case TREL_REGION_SURROUNDED:
    fc_assert_ret_val_msg((req->range == REQ_RANGE_TILE
                           || req->range == REQ_RANGE_CADJACENT
                           || req->range == REQ_RANGE_ADJACENT),
                          TRI_MAYBE,
                          "Invalid range %d for tile relation \"%s\" req",
                          req->range,
                          tilerel_type_name(TREL_REGION_SURROUNDED));

    {
      bool seen_maybe = FALSE;
      Continent_id wanted = tile_continent(other_context->tile);

      switch (does_region_surrounder_match(tile_continent(context->tile),
                                           wanted)) {
        case TRI_YES:
          return TRI_YES;
        case TRI_MAYBE:
          seen_maybe = TRUE;
          break;
        default:
          break;
      }

      range_adjc_iterate(nmap, context->tile, req->range, adj_tile) {
        switch (does_region_surrounder_match(tile_continent(adj_tile),
                                             wanted)) {
          case TRI_YES:
            return TRI_YES;
          case TRI_MAYBE:
            seen_maybe = TRUE;
            break;
          default:
            break;
        }
      } range_adjc_iterate_end;

      if (seen_maybe) {
        return TRI_MAYBE;
      } else {
        return TRI_NO;
      }
    }
    break;
  default:
    break;
  }

  fc_assert_msg(FALSE,
                "Illegal value %d for tile relationship requirement.",
                req->source.value.tilerel);
  return TRI_MAYBE;
}

/**********************************************************************//**
  Is center of given city in tile. If city is nullptr, any city will do.
**************************************************************************/
static bool is_city_in_tile(const struct tile *ptile,
                            const struct city *pcity)
{
  if (pcity == nullptr) {
    return tile_city(ptile) != nullptr;
  } else {
    return is_city_center(pcity, ptile);
  }
}

/**********************************************************************//**
  Determine whether a citytile requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a citytile requirement
**************************************************************************/
static enum fc_tristate
is_citytile_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  enum citytile_type citytile;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_CITYTILE);

  citytile = req->source.value.citytile;

  fc_assert_ret_val(req_range_is_valid(req->range), TRI_MAYBE);
  if (context->tile == nullptr) {
    return TRI_MAYBE;
  }

  switch (citytile) {
  case CITYT_CENTER:
    switch (req->range) {
    case REQ_RANGE_TILE:
      return BOOL_TO_TRISTATE(is_city_in_tile(context->tile,
                                              context->city));
    case REQ_RANGE_CADJACENT:
      if (is_city_in_tile(context->tile, context->city)) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        if (is_city_in_tile(adjc_tile, context->city)) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (is_city_in_tile(context->tile, context->city)) {
        return TRI_YES;
      }
      adjc_iterate(nmap, context->tile, adjc_tile) {
        if (is_city_in_tile(adjc_tile, context->city)) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_CLAIMED:
    switch (req->range) {
    case REQ_RANGE_TILE:
      return BOOL_TO_TRISTATE(context->tile->owner != nullptr);
    case REQ_RANGE_CADJACENT:
      if (context->tile->owner != nullptr) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->owner != nullptr) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (context->tile->owner != nullptr) {
        return TRI_YES;
      }
      adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->owner != nullptr) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_EXTRAS_OWNED:
    switch (req->range) {
    case REQ_RANGE_TILE:
      return BOOL_TO_TRISTATE(context->tile->extras_owner != nullptr);
    case REQ_RANGE_CADJACENT:
      if (context->tile->extras_owner != nullptr) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->extras_owner != nullptr) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (context->tile->extras_owner != nullptr) {
        return TRI_YES;
      }
      adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->extras_owner != nullptr) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_WORKED:
    switch (req->range) {
    case REQ_RANGE_TILE:
      return BOOL_TO_TRISTATE(context->tile->worked != nullptr);
    case REQ_RANGE_CADJACENT:
      if (context->tile->worked != nullptr) {
        return TRI_YES;
      }
      cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->worked != nullptr) {
          return TRI_YES;
        }
      } cardinal_adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_ADJACENT:
      if (context->tile->worked != nullptr) {
        return TRI_YES;
      }
      adjc_iterate(nmap, context->tile, adjc_tile) {
        if (adjc_tile->worked != nullptr) {
          return TRI_YES;
        }
      } adjc_iterate_end;

      return TRI_NO;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
      break;
    }

    return TRI_MAYBE;
  case CITYT_SAME_CONTINENT:
    {
      Continent_id cc;
      const struct tile *target_tile = context->tile, *cc_tile;

      if (!context->city) {
        return TRI_MAYBE;
      }
      cc_tile = city_tile(context->city);
      if (!cc_tile) {
        /* Unplaced virtual city */
        return TRI_MAYBE;
      }
      cc = tile_continent(cc_tile);
      /* Note: No special treatment of 0 == cc here*/
      switch (req->range) {
      case REQ_RANGE_TILE:
        return BOOL_TO_TRISTATE(tile_continent(target_tile) == cc);
      case REQ_RANGE_CADJACENT:
        if (tile_continent(target_tile) == cc) {
          return TRI_YES;
        }
        cardinal_adjc_iterate(nmap, target_tile, adjc_tile) {
          if (tile_continent(adjc_tile) == cc) {
            return TRI_YES;
          }
        } cardinal_adjc_iterate_end;

        return TRI_NO;
      case REQ_RANGE_ADJACENT:
        if (tile_continent(target_tile) == cc) {
          return TRI_YES;
        }
        adjc_iterate(nmap, target_tile, adjc_tile) {
          if (tile_continent(adjc_tile) == cc) {
            return TRI_YES;
          }
        } adjc_iterate_end;

        return TRI_NO;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
        break;
      }
    }

    return TRI_MAYBE;
  case CITYT_BORDERING_TCLASS_REGION:
    {
      int n = 0;
      Continent_id adjc_cont[8], cc;
      bool ukt = FALSE;
      const struct tile *target_tile = context->tile, *cc_tile;

      if (!context->city) {
        return TRI_MAYBE;
      }
      cc_tile = city_tile(context->city);
      if (!cc_tile) {
        /* Unplaced virtual city */
        return TRI_MAYBE;
      }
      cc = tile_continent(cc_tile);
      if (!cc) {
        /* Don't know the city center terrain class.
         * Maybe, the city floats? Even if the rules prohibit it... */
        return TRI_MAYBE;
      }
      adjc_iterate(nmap, cc_tile, adjc_tile) {
        Continent_id tc = tile_continent(adjc_tile);

        if (0 != tc) {
          bool seen = FALSE;
          int i = n;

          if (tc == cc) {
            continue;
          }
          while (--i >= 0) {
            if (adjc_cont[i] == tc) {
              seen = TRUE;
              break;
            }
          }
          if (seen) {
            continue;
          }
          adjc_cont[n++] = tile_continent(adjc_tile);
        } else {
          /* Likely, it's a black tile in client and we don't know
           * We possibly can calculate, but keep it simple. */
          ukt = TRUE;
        }
      } adjc_iterate_end;
      if (0 == n) {
        return ukt ? TRI_MAYBE : TRI_NO;
      }

      switch (req->range) {
      case REQ_RANGE_TILE:
        {
          Continent_id tc = tile_continent(target_tile);

          if (cc == tc) {
            return TRI_NO;
          }
          if (0 == tc || ukt) {
            return TRI_MAYBE;
          }
          for (int i = 0; i < n; i++) {
            if (tc == adjc_cont[i]) {
              return TRI_YES;
            }
          }
        }

        return TRI_NO;
      case REQ_RANGE_ADJACENT:
        if (ukt) {
          /* If ALL the tiles in range are on cc, we can say it's false */
          square_iterate(nmap, target_tile, 1, adjc_tile) {
            if (tile_continent(adjc_tile) != cc) {
             return TRI_MAYBE;
            }
          } square_iterate_end;

          return TRI_NO;
        } else {
          square_iterate(nmap, target_tile, 1, adjc_tile) {
            Continent_id tc = tile_continent(adjc_tile);

            if (0 == tc) {
              return TRI_MAYBE;
            }
            for (int i = 0; i < n; i++) {
              if (tc == adjc_cont[i]) {
                return TRI_YES;
              }
            }
          } square_iterate_end;
        }

        return TRI_NO;
      case REQ_RANGE_CADJACENT:
        if (ukt) {
          /* If ALL the tiles in range are on cc, we can say it's false */
          circle_iterate(nmap, target_tile, 1, cadjc_tile) {
            if (tile_continent(cadjc_tile) != cc) {
              return TRI_MAYBE;
            }
          } circle_iterate_end;
        } else {
          circle_iterate(nmap, target_tile, 1, cadjc_tile) {
            Continent_id tc = tile_continent(cadjc_tile);

            if (0 == tc) {
              return TRI_MAYBE;
            }
            for (int i = 0; i < n; i++) {
              if (tc == adjc_cont[i]) {
                return TRI_YES;
              }
            }
          } circle_iterate_end;
        }

        return TRI_NO;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        fc_assert_msg(FALSE, "Invalid range %d for citytile.", req->range);
        break;
      }
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
  Determine whether a city status requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a city status requirement
**************************************************************************/
static enum fc_tristate
is_citystatus_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  enum citystatus_type citystatus;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_CITYSTATUS);

  citystatus = req->source.value.citystatus;

  if (context->city == nullptr) {
    return TRI_MAYBE;
  }

  switch (citystatus) {
  case CITYS_OWNED_BY_ORIGINAL:
    switch (req->range) {
    case REQ_RANGE_CITY:
      if (context->city->original == nullptr) {
        return TRI_MAYBE;
      }
      return BOOL_TO_TRISTATE(city_owner(context->city) == context->city->original);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (city_owner(context->city) == context->city->original) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr || trade_partner->original == nullptr) {
            ret = TRI_MAYBE;
          } else if (city_owner(trade_partner) == trade_partner->original) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus OwnedByOriginal.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_STARVED:
    switch (req->range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(context->city->had_famine);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (context->city->had_famine) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr) {
            ret = TRI_MAYBE;
          } else if (trade_partner->had_famine) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus Starved.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_DISORDER:
    switch (req->range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(context->city->anarchy > 0);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (context->city->anarchy > 0) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr) {
            ret = TRI_MAYBE;
          } else if (trade_partner->anarchy > 0) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus Disorder.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_CELEBRATION:
    switch (req->range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(context->city->rapture > 0);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (context->city->rapture > 0) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr) {
            ret = TRI_MAYBE;
          } else if (trade_partner->rapture > 0) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus Celebration.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_TRANSFERRED:
    switch (req->range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(context->city->acquire_t != CACQ_FOUNDED);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (context->city->acquire_t != CACQ_FOUNDED) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr) {
            ret = TRI_MAYBE;
          } else if (trade_partner->acquire_t != CACQ_FOUNDED) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus Transferred.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_CAPITALCONNECTED:
    if (!is_server()) {
      /* Client has no idea. */
      return TRI_MAYBE;
    }

    switch (req->range) {
    case REQ_RANGE_CITY:
      return BOOL_TO_TRISTATE(context->city->server.aarea != nullptr
                              && context->city->server.aarea->capital);
    case REQ_RANGE_TRADE_ROUTE:
      {
        enum fc_tristate ret;

        if (context->city->server.aarea != nullptr
            && context->city->server.aarea->capital) {
          return TRI_YES;
        }

        ret = TRI_NO;
        trade_partners_iterate(context->city, trade_partner) {
          if (trade_partner == nullptr) {
            ret = TRI_MAYBE;
          } else if (trade_partner->server.aarea != nullptr
                     && trade_partner->server.aarea->capital) {
            return TRI_YES;
          }
        } trade_partners_iterate_end;

        return ret;
      }
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
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

    fc_assert_msg(FALSE, "Invalid range %d for citystatus CapitalConnected.",
                         req->range);

    return TRI_MAYBE;

  case CITYS_LAST:
    break;
  }

  /* Not implemented */
  log_error("is_req_active(): citystatus %d not supported.",
            citystatus);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a minimum size requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a minsize requirement
**************************************************************************/
static enum fc_tristate
is_minsize_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINSIZE);

  if (context->city == nullptr) {
    return TRI_MAYBE;
  } else {
    if (req->range == REQ_RANGE_TRADE_ROUTE) {
      enum fc_tristate ret;

      if (city_size_get(context->city) >= req->source.value.minsize) {
        return TRI_YES;
      }

      ret = TRI_NO;
      trade_partners_iterate(context->city, trade_partner) {
        if (trade_partner == nullptr) {
          ret = TRI_MAYBE;
        } else if (city_size_get(trade_partner) >= req->source.value.minsize) {
          return TRI_YES;
        }
      } trade_partners_iterate_end;

      return ret;
    } else {
      return BOOL_TO_TRISTATE(city_size_get(context->city)
                              >= req->source.value.minsize);
    }
  }
}

/**********************************************************************//**
  Determine whether a counter requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a counter requirement
**************************************************************************/
static enum fc_tristate
is_counter_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  const struct counter *count;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_COUNTER);

  count = req->source.value.counter;

  if (context->city == nullptr) {
    return TRI_MAYBE;
  }
  return BOOL_TO_TRISTATE(count->checkpoint <=
                          context->city->counter_values[
                              counter_index(count)]);
}

/**********************************************************************//**
  Determine whether an achievement requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be an achievement requirement
**************************************************************************/
static enum fc_tristate
is_achievement_req_active(const struct civ_map *nmap,
                          const struct req_context *context,
                          const struct req_context *other_context,
                          const struct requirement *req)
{
  const struct achievement *achievement;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_ACHIEVEMENT);

  achievement = req->source.value.achievement;

  if (req->range == REQ_RANGE_WORLD) {
    return BOOL_TO_TRISTATE(achievement_claimed(achievement));
  } else if (context->player == nullptr) {
    return TRI_MAYBE;
  } else if (req->range == REQ_RANGE_ALLIANCE
             || req->range == REQ_RANGE_TEAM) {
    players_iterate_alive(plr2) {
      if (players_in_same_range(context->player, plr2, req->range)
          && achievement_player_has(achievement, plr2)) {
        return TRI_YES;
      }
    } players_iterate_alive_end;
    return TRI_NO;
  } else if (req->range == REQ_RANGE_PLAYER) {
    if (achievement_player_has(achievement, context->player)) {
      return TRI_YES;
    } else {
      return TRI_NO;
    }
  }

  fc_assert_msg(FALSE, "Invalid range %d.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a minimum or maximum latitude requirement is satisfied
  in a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a minlatitude or maxlatitude requirement
**************************************************************************/
static enum fc_tristate
is_latitude_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  int min = -MAP_MAX_LATITUDE, max = MAP_MAX_LATITUDE;

  fc_assert_ret_val(req != nullptr, TRI_MAYBE);
  fc_assert(context != nullptr);

  switch (req->source.kind) {
  case VUT_MINLATITUDE:
    min = req->source.value.latitude;
    break;
  case VUT_MAXLATITUDE:
    max = req->source.value.latitude;
    break;
  default:
    fc_assert(req->source.kind == VUT_MINLATITUDE
              || req->source.kind == VUT_MAXLATITUDE);
    break;
  }

  switch (req->range) {
  case REQ_RANGE_WORLD:
    return BOOL_TO_TRISTATE(min <= MAP_MAX_REAL_LATITUDE(wld.map)
                            && max >= MAP_MIN_REAL_LATITUDE(wld.map));

  case REQ_RANGE_TILE:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    } else {
      int tile_lat = map_signed_latitude(context->tile);

      return BOOL_TO_TRISTATE(min <= tile_lat && max >= tile_lat);
    }

  case REQ_RANGE_CADJACENT:
    if (context->tile == nullptr) {
      return TRI_MAYBE;
    }

    cardinal_adjc_iterate(nmap, context->tile, adjc_tile) {
      int tile_lat = map_signed_latitude(adjc_tile);

      if (min <= tile_lat && max >= tile_lat) {
        return TRI_YES;
      }
    } cardinal_adjc_iterate_end;
    return TRI_NO;

  case REQ_RANGE_ADJACENT:
    if (!context->tile) {
      return TRI_MAYBE;
    }

    adjc_iterate(nmap, context->tile, adjc_tile) {
      int tile_lat = map_signed_latitude(adjc_tile);

      if (min <= tile_lat && max >= tile_lat) {
        return TRI_YES;
      }
    } adjc_iterate_end;
    return TRI_NO;

  case REQ_RANGE_CITY:
  case REQ_RANGE_TRADE_ROUTE:
  case REQ_RANGE_CONTINENT:
  case REQ_RANGE_PLAYER:
  case REQ_RANGE_TEAM:
  case REQ_RANGE_ALLIANCE:
  case REQ_RANGE_LOCAL:
  case REQ_RANGE_COUNT:
    break;
  }

  fc_assert_msg(FALSE,
                "Illegal range %d for latitude requirement.", req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a maximum squared distance requirement is satisfied in
  a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a max squared distance requirement
**************************************************************************/
static enum fc_tristate
is_max_distance_sq_req_active(const struct civ_map *nmap,
                              const struct req_context *context,
                              const struct req_context *other_context,
                              const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MAX_DISTANCE_SQ);

  switch (req->range) {
  case REQ_RANGE_TILE:
    if (context->tile == nullptr || other_context->tile == nullptr) {
      return TRI_MAYBE;
    }
    return BOOL_TO_TRISTATE(
      sq_map_distance(context->tile, other_context->tile)
      <= req->source.value.distance_sq
    );
  default:
    break;
  }

  fc_assert_msg(FALSE,
                "Illegal range %d for max squared distance requirement.",
                req->range);

  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a maximum tiles of same region requirement is satisfied
  in a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a max region tiles requirement
**************************************************************************/
static enum fc_tristate
is_max_region_tiles_req_active(const struct civ_map *nmap,
                               const struct req_context *context,
                               const struct req_context *other_context,
                               const struct requirement *req)
{
  int max_tiles, min_tiles = 1;

  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MAX_REGION_TILES);

  switch (req->range) {
  case REQ_RANGE_CADJACENT:
  case REQ_RANGE_ADJACENT:
    if (context->tile == nullptr) {
      /* The tile itself is included in the range */
      max_tiles = 1 + ((req->range == REQ_RANGE_CADJACENT)
                      ? nmap->num_cardinal_dirs
                      : nmap->num_valid_dirs);

      break;
    } else {
      Continent_id cont = tile_continent(context->tile);

      /* Count how many adjacent tiles there actually are as we go along */
      max_tiles = 1;

      range_adjc_iterate(nmap, context->tile, req->range, adj_tile) {
        Continent_id adj_cont = tile_continent(adj_tile);

        if (adj_cont == 0 || cont == 0) {
          max_tiles++;
        } else if (adj_cont == cont) {
          min_tiles++;
          max_tiles++;
        }
      } range_adjc_iterate_end;
    }
    break;
  case REQ_RANGE_CONTINENT:
    {
      Continent_id cont = context->tile ? tile_continent(context->tile) : 0;

      fc_assert_ret_val(cont <= nmap->num_continents, TRI_MAYBE);
      fc_assert_ret_val(-cont <= nmap->num_oceans, TRI_MAYBE);

      /* Note: We could come up with a better upper bound by subtracting
       * all other continent/ocean sizes, or all except the largest if we
       * don't know the tile.
       * We could even do a flood-fill count of the unknown area bordered
       * by known tiles of the continent.
       * Probably not worth the effort though. */
      max_tiles = nmap->xsize * nmap->ysize;

      if (cont > 0) {
        min_tiles = nmap->continent_sizes[cont];
        if (is_whole_continent_known(cont)) {
          max_tiles = min_tiles;
        }
      } else if (cont < 0) {
        min_tiles = nmap->ocean_sizes[-cont];
        if (is_whole_ocean_known(-cont)) {
          max_tiles = min_tiles;
        }
      }
    }
    break;
  default:
    fc_assert_msg(FALSE,
                  "Illegal range %d for max region tiles requirement.",
                  req->range);
    return TRI_MAYBE;
  }

  if (min_tiles > req->source.value.region_tiles) {
    return TRI_NO;
  } else if (max_tiles <= req->source.value.region_tiles) {
    return TRI_YES;
  }
  return TRI_MAYBE;
}

/**********************************************************************//**
  Determine whether a minimum year requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a minyear requirement
**************************************************************************/
static enum fc_tristate
is_minyear_req_active(const struct civ_map *nmap,
                      const struct req_context *context,
                      const struct req_context *other_context,
                      const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINYEAR);

  return BOOL_TO_TRISTATE(game.info.year >= req->source.value.minyear);
}

/**********************************************************************//**
  Determine whether a minimum calendar fragment requirement is satisfied in
  a given context, ignoring parts of the requirement that can be handled
  uniformly for all requirement types.

  context, other_context and req must not be null,
  and req must be a mincalfrag requirement
**************************************************************************/
static enum fc_tristate
is_mincalfrag_req_active(const struct civ_map *nmap,
                         const struct req_context *context,
                         const struct req_context *other_context,
                         const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_MINCALFRAG);

  return BOOL_TO_TRISTATE(game.info.fragment_count
                          >= req->source.value.mincalfrag);
}

/**********************************************************************//**
  Determine whether a topology requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a topology requirement
**************************************************************************/
static enum fc_tristate
is_topology_req_active(const struct civ_map *nmap,
                       const struct req_context *context,
                       const struct req_context *other_context,
                       const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_TOPO);

  return BOOL_TO_TRISTATE(
      current_topo_has_flag(req->source.value.topo_property));
}

/**********************************************************************//**
  Determine whether a wrap requirement is satisfied in a given context,
  ignoring parts of the requirement that can be handled uniformly for all
  requirement types.

  context, other_context and req must not be null,
  and req must be a wrap requirement
**************************************************************************/
static enum fc_tristate
is_wrap_req_active(const struct civ_map *nmap,
                   const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_WRAP);

  return BOOL_TO_TRISTATE(
      current_wrap_has_flag(req->source.value.wrap_property));
}

/**********************************************************************//**
  Determine whether a server setting requirement is satisfied in a given
  context, ignoring parts of the requirement that can be handled uniformly
  for all requirement types.

  context, other_context and req must not be null,
  and req must be a server setting requirement
**************************************************************************/
static enum fc_tristate
is_serversetting_req_active(const struct civ_map *nmap,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement *req)
{
  IS_REQ_ACTIVE_VARIANT_ASSERT(VUT_SERVERSETTING);

  return BOOL_TO_TRISTATE(ssetv_setting_has_value(
                              req->source.value.ssetval));
}

/* Not const for potential ruleset-related adjustment */
static struct req_def req_definitions[VUT_COUNT] = {
  [VUT_NONE] = {is_none_req_active, REQUCH_YES},

  /* Alphabetical order of enum constant */
  [VUT_ACHIEVEMENT] = {is_achievement_req_active, REQUCH_YES, REQUC_PRESENT},
  [VUT_ACTION] = {is_action_req_active, REQUCH_YES},
  [VUT_ACTIVITY] = {is_activity_req_active, REQUCH_NO},
  [VUT_ADVANCE] = {is_tech_req_active, REQUCH_NO},
  [VUT_AGE] = {is_age_req_active, REQUCH_YES, REQUC_PRESENT},
  [VUT_FORM_AGE] = {is_form_age_req_active, REQUCH_YES, REQUC_PRESENT},
  [VUT_AI_LEVEL] = {is_ai_req_active, REQUCH_HACK},
  [VUT_CITYSTATUS] = {is_citystatus_req_active, REQUCH_NO, REQUC_CITYSTATUS},
  [VUT_CITYTILE] = {is_citytile_req_active, REQUCH_NO, REQUC_CITYTILE},
  [VUT_COUNTER] = {is_counter_req_active, REQUCH_NO},
  [VUT_DIPLREL] = {is_diplrel_req_active, REQUCH_NO},
  [VUT_DIPLREL_TILE] = {is_diplrel_tile_req_active, REQUCH_NO},
  [VUT_DIPLREL_TILE_O] = {is_diplrel_tile_o_req_active, REQUCH_NO},
  [VUT_DIPLREL_UNITANY] = {is_diplrel_unitany_req_active, REQUCH_NO},
  [VUT_DIPLREL_UNITANY_O] = {is_diplrel_unitany_o_req_active, REQUCH_NO},
  [VUT_EXTRA] = {is_extra_req_active, REQUCH_NO, REQUC_LOCAL},
  [VUT_TILEDEF] = {is_tiledef_req_active, REQUCH_NO, REQUC_LOCAL},
  [VUT_EXTRAFLAG] = {is_extraflag_req_active, REQUCH_NO, REQUC_LOCAL},
  [VUT_FUTURETECHS] = {is_futuretechs_req_active, REQUCH_ACT, REQUC_WORLD},
  [VUT_GOOD] = {is_good_req_active, REQUCH_NO},
  [VUT_GOVERNMENT] = {is_gov_req_active, REQUCH_NO},
  [VUT_GOVFLAG] = {is_govflag_req_active, REQUCH_NO},
  [VUT_IMPROVEMENT] = {is_building_req_active, REQUCH_NO, REQUC_IMPR},
  [VUT_SITE] = {is_building_req_active, REQUCH_NO, REQUC_IMPR},
  [VUT_IMPR_GENUS] = {is_buildinggenus_req_active, REQUCH_YES},
  [VUT_IMPR_FLAG] = {is_buildingflag_req_active, REQUCH_YES},
  [VUT_PLAYER_FLAG] = {is_plr_flag_req_active, REQUCH_NO},
  [VUT_PLAYER_STATE] = {is_plr_state_req_active, REQUCH_NO},
  [VUT_MAX_DISTANCE_SQ] = {is_max_distance_sq_req_active, REQUCH_YES},
  [VUT_MAX_REGION_TILES] = {is_max_region_tiles_req_active, REQUCH_NO},
  [VUT_MAXLATITUDE] = {is_latitude_req_active, REQUCH_YES},
  [VUT_MAXTILETOTALUNITS] = {is_maxtotalunitsontile_req_active, REQUCH_NO},
  [VUT_MAXTILETOPUNITS] = {is_maxtopunitsontile_req_active, REQUCH_NO},
  [VUT_MINCALFRAG] = {is_mincalfrag_req_active, REQUCH_NO},
  [VUT_MINCULTURE] = {is_minculture_req_active, REQUCH_NO},
  [VUT_MINFOREIGNPCT] = {is_minforeignpct_req_active, REQUCH_NO},
  [VUT_MINHP] = {is_minhitpoints_req_active, REQUCH_NO},
  [VUT_MINLATITUDE] = {is_latitude_req_active, REQUCH_NO},
  [VUT_MINMOVES] = {is_minmovefrags_req_active, REQUCH_NO},
  [VUT_MINSIZE] = {is_minsize_req_active, REQUCH_NO},
  [VUT_MINTECHS] = {is_mintechs_req_active, REQUCH_ACT, REQUC_WORLD},
  [VUT_MINCITIES] = {is_mincities_req_active, REQUCH_NO},
  [VUT_MINVETERAN] = {is_minveteran_req_active, REQUCH_SCRIPTS, REQUC_PRESENT},
  [VUT_MINYEAR] = {is_minyear_req_active, REQUCH_HACK, REQUC_PRESENT},
  [VUT_NATION] = {is_nation_req_active, REQUCH_HACK, REQUC_NALLY},
  [VUT_NATIONALITY] = {is_nationality_req_active, REQUCH_NO},
  [VUT_NATIONGROUP] = {is_nationgroup_req_active, REQUCH_HACK, REQUC_NALLY},
  [VUT_ORIGINAL_OWNER] = {is_originalowner_req_active, REQUCH_HACK},
  [VUT_OTYPE] = {is_outputtype_req_active, REQUCH_YES},
  [VUT_ROADFLAG] = {is_roadflag_req_active, REQUCH_NO, REQUC_LOCAL},
  [VUT_SERVERSETTING] = {is_serversetting_req_active, REQUCH_HACK},
  [VUT_SPECIALIST] = {is_specialist_req_active, REQUCH_YES},
  [VUT_STYLE] = {is_style_req_active, REQUCH_HACK},
  [VUT_TECHFLAG] = {is_techflag_req_active, REQUCH_NO},
  [VUT_TERRAIN] = {is_terrain_req_active, REQUCH_NO},
  [VUT_TERRAINALTER] = {is_terrainalter_req_active, REQUCH_NO},
  [VUT_TERRAINCLASS] = {is_terrainclass_req_active, REQUCH_NO},
  [VUT_TERRFLAG] = {is_terrainflag_req_active, REQUCH_NO},
  [VUT_TILE_REL] = {is_tile_rel_req_active, REQUCH_NO},
  [VUT_TOPO] = {is_topology_req_active, REQUCH_YES},
  [VUT_WRAP] = {is_wrap_req_active, REQUCH_YES},
  [VUT_UCFLAG] = {is_unitclassflag_req_active, REQUCH_YES},
  [VUT_UCLASS] = {is_unitclass_req_active, REQUCH_YES},
  [VUT_UNITSTATE] = {is_unitstate_req_active, REQUCH_NO},
  [VUT_UTFLAG] = {is_unitflag_req_active, REQUCH_YES},
  [VUT_UTYPE] = {is_unittype_req_active, REQUCH_YES}
};

/**********************************************************************//**
  Checks the requirement to see if it is active on the given target.

  context gives the target (or targets) to evaluate against
  req gives the requirement itself

  context and other_context may be nullptr. This is equivalent to passing
  empty contexts.

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
**************************************************************************/
bool is_req_active(const struct req_context *context,
                   const struct req_context *other_context,
                   const struct requirement *req,
                   const enum   req_problem_type prob_type)
{
  const struct civ_map *nmap = &(wld.map);
  enum fc_tristate eval = tri_req_present(nmap, context, other_context,
                                          req);

  if (eval == TRI_MAYBE) {
    if (prob_type == RPT_POSSIBLE) {
      return TRUE;
    } else {
      return FALSE;
    }
  }
  return req->present ? (eval != TRI_NO) : (eval != TRI_YES);
}

/**********************************************************************//**
  Applies the standard evaluation of req in context, ignoring req->present.

  context and other_context may be nullptr. This is equivalent to passing
  empty contexts.

  Fields of context that are nullptr are considered unspecified
  and will produce TRI_MAYBE if req needs them to evaluate.
**************************************************************************/
static
enum fc_tristate tri_req_present(const struct civ_map *nmap,
                                 const struct req_context *context,
                                 const struct req_context *other_context,
                                 const struct requirement *req)
{
  if (!context) {
    context = req_context_empty();
  }
  if (!other_context) {
    other_context = req_context_empty();
  }

  if (req->source.kind >= VUT_COUNT) {
    log_error("tri_req_present(): invalid source kind %d.",
              req->source.kind);
    return TRI_NO;
  }

  fc_assert_ret_val(req_definitions[req->source.kind].cb != nullptr, TRI_NO);

  return req_definitions[req->source.kind].cb(nmap, context,
                                              other_context, req);
}

/**********************************************************************//**
  Evaluates req in context to fc_tristate.

  context and other_context may be nullptr. This is equivalent to passing
  empty contexts.

  Fields of context that are nullptr are considered unspecified
  and will produce TRI_MAYBE if req needs them to evaluate.
**************************************************************************/
enum fc_tristate tri_req_active(const struct req_context *context,
                                const struct req_context *other_context,
                                const struct requirement *req)
{
  const struct civ_map *nmap = &(wld.map);
  enum fc_tristate eval = tri_req_present(nmap, context, other_context, req);

  if (!req->present) {
    if (TRI_NO == eval) {
      return TRI_YES;
    }
    if (TRI_YES == eval) {
      return TRI_NO;
    }
  }

  return eval;
}

/**********************************************************************//**
  Checks the requirement(s) to see if they are active on the given target.

  context gives the target (or targets) to evaluate against

  reqs gives the requirement vector.
  The function returns TRUE only if all requirements are active.

  context and other_context may be nullptr. This is equivalent to passing
  empty contexts.

  Make sure you give all aspects of the target when calling this function:
  for instance if you have TARGET_CITY pass the city's owner as the target
  player as well as the city itself as the target city.
**************************************************************************/
bool are_reqs_active(const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement_vector *reqs,
                     const enum   req_problem_type prob_type)
{
  requirement_vector_iterate(reqs, preq) {
    if (!is_req_active(context, other_context, preq, prob_type)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Like are_reqs_active() but checks only requirements that have
  one of the ranges between min_range and max_range.
**************************************************************************/
bool are_reqs_active_ranges(const enum req_range min_range,
                            const enum req_range max_range,
                            const struct req_context *context,
                            const struct req_context *other_context,
                            const struct requirement_vector *reqs,
                            const enum   req_problem_type prob_type)
{
  requirement_vector_iterate(reqs, preq) {
    if (preq->range >= min_range && preq->range <= max_range) {
      if (!is_req_active(context, other_context, preq, prob_type)) {
        return FALSE;
      }
    }
  } requirement_vector_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  For requirements changing with time, will they be active for the target
  after pass in period turns if nothing else changes?
  Since year and calfrag changing is effect dependent, the result
  may appear not precise. (Does not consider research progress etc.)
**************************************************************************/
enum fc_tristate
tri_req_active_turns(int pass, int period,
                     const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement *req)
{
  /* FIXME: Doubles code from calendar.c */
  int ypt = get_world_bonus(EFT_TURN_YEARS);
  int fpt = get_world_bonus(EFT_TURN_FRAGMENTS);
  int fragment = game.info.fragment_count;
  int fragment1 = fragment; /* If fragments don't advance */
  int year_inc, year_inc1;
  const int slowdown = (victory_enabled(VC_SPACERACE)
        ? get_world_bonus(EFT_SLOW_DOWN_TIMELINE) : 0);
  bool present, present1;

  fc_assert(pass >= 0 && period >= 0);
  if (slowdown >= 3) {
    if (ypt > 1) {
      ypt = 1;
    }
  } else if (slowdown >= 2) {
    if (ypt > 2) {
      ypt = 2;
    }
  } else if (slowdown >= 1) {
    if (ypt > 5) {
      ypt = 5;
    }
  }
  year_inc = ypt * pass;
  year_inc1 = year_inc + ypt * period;
  if (game.calendar.calendar_fragments) {
    int fragment_years;

    fragment += fpt * pass;
    fragment_years = fragment / game.calendar.calendar_fragments;
    year_inc += fragment_years;
    fragment -= fragment_years * game.calendar.calendar_fragments;
    fragment1 = fragment + fpt * period;
    fragment_years += fragment1 / game.calendar.calendar_fragments;
    year_inc1 += fragment_years;
    fragment1 -= fragment_years * game.calendar.calendar_fragments;
  }
  if (game.calendar.calendar_skip_0 && game.info.year < 0) {
    if (year_inc + game.info.year >= 0) {
      year_inc++;
      year_inc1++;
    } else if (year_inc1 + game.info.year >= 0) {
      year_inc1++;
    }
  }

  switch (req->source.kind) {
  case VUT_AGE:
    switch (req->range) {
    case REQ_RANGE_LOCAL:
      if (context->unit == nullptr || !is_server()) {
        return TRI_MAYBE;
      } else {
        int ua = game.info.turn + pass - context->unit->birth_turn;

        present = req->source.value.age <= ua;
        present1 = req->source.value.age <= ua + period;
      }
      break;
    case REQ_RANGE_CITY:
      if (context->city == nullptr) {
        return TRI_MAYBE;
      } else {
        int ca = game.info.turn + pass - context->city->turn_founded;

        present = req->source.value.age <= ca;
        present1 = req->source.value.age <= ca + period;
      }
      break;
    case REQ_RANGE_PLAYER:
      if (context->player == nullptr) {
        return TRI_MAYBE;
      } else {
        present = req->source.value.age
            <= player_age(context->player) + pass;
        present1 = req->source.value.age
            <= player_age(context->player) + pass + period;
      }
      break;
    default:
      return TRI_MAYBE;
    }
    break;
  case VUT_FORM_AGE:
    if (context->unit == nullptr || !is_server()) {
      return TRI_MAYBE;
    } else {
      int ua = game.info.turn + pass - context->unit->current_form_turn;

      present = req->source.value.form_age <= ua;
      present1 = req->source.value.form_age <= ua + period;
    }
    break;
  case VUT_MINYEAR:
    present = game.info.year + year_inc >= req->source.value.minyear;
    present1 = game.info.year + year_inc1 >= req->source.value.minyear;
    break;
  case VUT_MINCALFRAG:
    if (game.calendar.calendar_fragments <= period) {
      /* Hope that the requirement is valid and fragments advance fine */
      return TRI_YES;
    }
    present = fragment >= req->source.value.mincalfrag;
    present1 = fragment1 >= req->source.value.mincalfrag;
    break;
  default:
    /* No special handling invented */
    return tri_req_active(context, other_context, req);
  }
  return BOOL_TO_TRISTATE(req->present
                          ? present || present1 : !(present && present1));
}

/**********************************************************************//**
  A tester callback for tri_reqs_cb_active() that uses the default
  requirement calculation except for requirements in data[n_data] array
  and ones implied by them or contradicting them
**************************************************************************/
enum fc_tristate default_tester_cb
   (const struct req_context *context,
    const struct req_context *other_context,
    const struct requirement *req,
    void *data, int n_data)
{
  int i;

  fc_assert_ret_val(data || n_data == 0, TRI_NO);

  for (i = 0; i < n_data; i++) {
    if (are_requirements_contradictions(&((struct requirement *) data)[i],
                                        req)) {
      return TRI_NO;
    } else if (req_implies_req(&((struct requirement *) data)[i], req)) {
      return TRI_YES;
    }
  }

  return tri_req_active(context, other_context, req);
}

/**********************************************************************//**
  Test requirements in reqs with tester according to (data, n_data)
  and give the resulting tristate.
  If maybe_reqs is not nullptr, copies requirements that are evaluated
  to TRI_MAYBE into it (stops as soon as one evaluates to TRI_NO).
**************************************************************************/
enum fc_tristate
  tri_reqs_cb_active(const struct req_context *context,
                     const struct req_context *other_context,
                     const struct requirement_vector *reqs,
                     struct requirement_vector *maybe_reqs,
                     req_tester_cb tester,
                     void *data, int n_data)
{
  bool active = TRUE;
  bool certain = TRUE;

  fc_assert_ret_val(tester != nullptr, TRI_NO);

  requirement_vector_iterate(reqs, preq) {
    switch(tester(context, other_context, preq,
                  data, n_data)) {
    case TRI_NO:
      active = FALSE;
      certain = TRUE;
      break;
    case TRI_YES:
      break;
    case TRI_MAYBE:
      certain = FALSE;
      if (maybe_reqs) {
        requirement_vector_append(maybe_reqs, *preq);
      }
      break;
    default:
      fc_assert(FALSE);
      active = FALSE;
    }
    if (!active) {
      break;
    }
  } requirement_vector_iterate_end;

  return certain ? (active ? TRI_YES : TRI_NO) : TRI_MAYBE;
}

/**********************************************************************//**
  Gives a suggestion may req ever evaluate to another value with given
  context. (The other player is not supplied since it has no value
  for changeability of any requirement for now.)

  Note this isn't absolute. Result other than REQUCH_NO here just means
  that the requirement _probably_ can't change its value afterwards.
***************************************************************************/
enum req_unchanging_status
  is_req_unchanging(const struct req_context *context,
                    const struct requirement *req)
{
  enum req_unchanging_status s;
  const struct civ_map *nmap = &(wld.map);

  fc_assert_ret_val(req, REQUCH_NO);
  fc_assert_ret_val_msg(universals_n_is_valid(req->source.kind), REQUCH_NO,
                        "Invalid source kind %d.", req->source.kind);
  s = req_definitions[req->source.kind].unchanging;

  if (req->survives) {
    /* Special case for surviving requirements */
    /* Buildings may obsolete even here */
    if (VUT_IMPROVEMENT == req->source.kind) {
      const struct impr_type *b = req->source.value.building;

      if (can_improvement_go_obsolete(b)) {
        if (improvement_obsolete(context->player, b, context->city)) {
          /* FIXME: Sometimes can unobsolete, but considering it
           * may sometimes put the function on endless recursion */
          return REQUCH_ACT; /* Mostly about techs */
        } else {
          /* NOTE: May obsoletion reqs be unchanging? Hardly but why not. */
          return REQUCH_NO;
        }
      }
    }
    s = unchanging_present(nmap, s, context, req);
    if (s != REQUCH_NO) {
      return unchanging_noally(nmap, s, context, req);
    }
  } else {
    req_unchanging_cond_cb cond
      = req_definitions[req->source.kind].unchanging_cond;

    if (cond) {
      return cond(nmap, s, context, req);
    }
  }

  return s;
}

/**********************************************************************//**
  Returns whether this requirement is unfulfilled and probably won't be ever
***************************************************************************/
enum req_unchanging_status
  is_req_preventing(const struct req_context *context,
                    const struct req_context *other_context,
                    const struct requirement *req,
                    enum req_problem_type prob_type)
{
  enum req_unchanging_status u = is_req_unchanging(context, req);

  if (REQUCH_NO != u) {
    /* Presence is precalculated */
    bool auto_present = (req->survives
         && !(VUT_IMPROVEMENT == req->source.kind
              && can_improvement_go_obsolete(req->source.value.building)))
      || REQUC_PRESENT == req_definitions[req->source.kind].unchanging_cond
      || REQUC_WORLD == req_definitions[req->source.kind].unchanging_cond;

    if (auto_present ? req->present
        : is_req_active(context, other_context, req, RPT_POSSIBLE)) {
      /* Unchanging but does not block */
      return REQUCH_NO;
    }
  }

  return u;
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
  case VUT_MINLATITUDE:
    return source->value.latitude > MAP_MAX_REAL_LATITUDE(wld.map);
  case VUT_MAXLATITUDE:
    return source->value.latitude < MAP_MIN_REAL_LATITUDE(wld.map);
  case VUT_COUNTER:
  case VUT_OTYPE:
  case VUT_SPECIALIST:
  case VUT_AI_LEVEL:
  case VUT_CITYTILE:
  case VUT_CITYSTATUS:
  case VUT_STYLE:
  case VUT_TOPO:
  case VUT_WRAP:
  case VUT_SERVERSETTING:
  case VUT_NATION:
  case VUT_NATIONGROUP:
  case VUT_ADVANCE:
  case VUT_TECHFLAG:
  case VUT_GOVERNMENT:
  case VUT_GOVFLAG:
  case VUT_ACHIEVEMENT:
  case VUT_IMPROVEMENT:
  case VUT_SITE:
  case VUT_IMPR_GENUS:
  case VUT_IMPR_FLAG:
  case VUT_PLAYER_FLAG:
  case VUT_PLAYER_STATE:
  case VUT_MINSIZE:
  case VUT_MINCULTURE:
  case VUT_MINFOREIGNPCT:
  case VUT_MINTECHS:
  case VUT_FUTURETECHS:
  case VUT_MINCITIES:
  case VUT_NATIONALITY:
  case VUT_ORIGINAL_OWNER: /* As long as midgame player creation or civil war possible */
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
  case VUT_MAXTILETOTALUNITS:
  case VUT_MAXTILETOPUNITS:
  case VUT_UTYPE:
  case VUT_UCLASS:
  case VUT_MINVETERAN:
  case VUT_UNITSTATE:
  case VUT_ACTIVITY:
  case VUT_MINMOVES:
  case VUT_MINHP:
  case VUT_AGE:
  case VUT_FORM_AGE:
  case VUT_ROADFLAG:
  case VUT_MINCALFRAG:
  case VUT_TERRAIN:
  case VUT_EXTRA:
  case VUT_TILEDEF:
  case VUT_GOOD:
  case VUT_TERRAINCLASS:
  case VUT_TERRFLAG:
  case VUT_TERRAINALTER:
  case VUT_MINYEAR:
  case VUT_MAX_DISTANCE_SQ:
  case VUT_MAX_REGION_TILES:
  case VUT_TILE_REL:
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
  specified requirement vector or nullptr if the parent item doesn't have
  a requirement vector with that requirement vector number.

  @param parent_item the item that should have the requirement vector.
  @param number the item's requirement vector number.
  @return a pointer to the specified requirement vector.
************************************************************************/
struct requirement_vector *
req_vec_by_number(const void *parent_item, req_vec_num_in_item number)
{
  fc_assert_ret_val(number == 0, nullptr);

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
  struct astring astr;

  fc_assert_ret_val(change, nullptr);
  fc_assert_ret_val(req_vec_change_operation_is_valid(change->operation),
                    nullptr);

  /* Get rid of the previous. */
  buf[0] = '\0';

  if (namer == nullptr) {
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
                req_to_fstring(&change->req, &astr),
                req_vec_description);
    astr_free(&astr);
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
                req_to_fstring(&change->req, &astr),
                req_vec_description);
    astr_free(&astr);
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
  vector with suggested solutions or nullptr if no contradiction was found.
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

  if (vec == nullptr || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return nullptr;
  }

  if (get_num == nullptr || parent_item == nullptr) {
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
        struct astring astr;
        struct astring nastr;

        problem = req_vec_problem_new(2,
            N_("Requirements {%s} and {%s} contradict each other."),
            req_to_fstring(preq, &astr), req_to_fstring(nreq, &nastr));

        astr_free(&astr);
        astr_free(&nastr);

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

  return nullptr;
}

/**********************************************************************//**
  Returns a suggestion to fix the specified requirement vector or nullptr if
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
  requirement vector with suggested solutions or nullptr if no missing
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
  struct req_vec_problem *problem = nullptr;

  if (vec == nullptr || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return nullptr;
  }

  if (get_num == nullptr || parent_item == nullptr) {
    vec_num = 0;
  } else {
    vec_num = get_num(parent_item, vec);
  }

  /* Look for contradictions */
  for (i = 0; i < requirement_vector_size(vec); i++) {
    struct requirement *preq = requirement_vector_get(vec, i);

    if (universal_never_there(&preq->source)) {
      struct astring astr;

      if (preq->present) {
        /* The requirement vector can never be fulfilled. Removing the
         * requirement makes it possible to fulfill it. This is a rule
         * change and shouldn't be "fixed" without thinking. Don't offer any
         * automatic solution to prevent mindless "fixes". */
        /* TRANS: Ruledit warns a user about an unused requirement vector
         * that never can be fulfilled because it asks for something that
         * never will be there. */
        if (problem == nullptr) {
          problem = req_vec_problem_new(0,
                       N_("Requirement {%s} requires %s but it will never be"
                          " there."),
                       req_to_fstring(preq, &astr), universal_rule_name(&preq->source));
          astr_free(&astr);
        }

        /* Continue to check if other problems have a solution proposal,
         * and prefer to return those. */
        continue;
      }

      if (problem != nullptr) {
        /* Free previous one (one with no solution proposals) */
        req_vec_problem_free(problem);
      }

      problem = req_vec_problem_new(1,
          N_("Requirement {%s} mentions %s but it will never be there."),
          req_to_fstring(preq, &astr), universal_rule_name(&preq->source));

      astr_free(&astr);

      /* The solution is to remove the reference to the missing
       * universal. */
      problem->suggested_solutions[0].operation = RVCO_REMOVE;
      problem->suggested_solutions[0].vector_number = vec_num;
      problem->suggested_solutions[0].req = *preq;

      /* Only the first missing universal is reported. */
      return problem;
    }
  }

  return problem;
}

/**********************************************************************//**
  Returns the first redundant requirement in the specified requirement
  vector with suggested solutions or nullptr if no redundant requirements were
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

  if (vec == nullptr || requirement_vector_size(vec) == 0) {
    /* No vector. */
    return nullptr;
  }

  if (get_num == nullptr || parent_item == nullptr) {
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
        struct astring astr;
        struct astring nastr;

        problem = req_vec_problem_new(2,
            N_("Requirements {%s} and {%s} are the same."),
            req_to_fstring(preq, &astr), req_to_fstring(nreq, &nastr));

        astr_free(&astr);
        astr_free(&nastr);

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

  return nullptr;
}

/**********************************************************************//**
  Returns a suggestion to improve the specified requirement vector or nullptr
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
  if (out != nullptr) {
    /* A bug, not just a potential improvement */
    return out;
  }

  /* Check if a universal that never will appear in the game is checked. */
  out = req_vec_get_first_missing_univ(vec, get_num, parent_item);
  if (out != nullptr) {
    return out;
  }

  /* Check if a requirement is redundant. */
  out = req_vec_get_first_redundant_req(vec, get_num, parent_item);
  return out;
}

/**********************************************************************//**
  Return TRUE iff the two sources are equivalent. Note this isn't the
  same as an == or memcmp() check.
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
  case VUT_COUNTER:
    return psource1->value.counter == psource2->value.counter;
  case VUT_ADVANCE:
    return psource1->value.advance == psource2->value.advance;
  case VUT_TECHFLAG:
    return psource1->value.techflag == psource2->value.techflag;
  case VUT_GOVERNMENT:
    return psource1->value.govern == psource2->value.govern;
  case VUT_GOVFLAG:
    return psource1->value.govflag == psource2->value.govflag;
  case VUT_ACHIEVEMENT:
    return psource1->value.achievement == psource2->value.achievement;
  case VUT_STYLE:
    return psource1->value.style == psource2->value.style;
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    return psource1->value.building == psource2->value.building;
  case VUT_IMPR_GENUS:
    return psource1->value.impr_genus == psource2->value.impr_genus;
  case VUT_IMPR_FLAG:
    return psource1->value.impr_flag == psource2->value.impr_flag;
  case VUT_PLAYER_FLAG:
    return psource1->value.plr_flag == psource2->value.plr_flag;
  case VUT_PLAYER_STATE:
    return psource1->value.plrstate == psource2->value.plrstate;
  case VUT_EXTRA:
    return psource1->value.extra == psource2->value.extra;
  case VUT_TILEDEF:
    return psource1->value.tiledef == psource2->value.tiledef;
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
  case VUT_ORIGINAL_OWNER:
    return psource1->value.origowner == psource2->value.origowner;
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
  case VUT_FORM_AGE:
    return psource1->value.form_age == psource2->value.form_age;
  case VUT_MINTECHS:
    return psource1->value.min_techs == psource2->value.min_techs;
  case VUT_FUTURETECHS:
    return psource1->value.future_techs == psource2->value.future_techs;
  case VUT_MINCITIES:
    return psource1->value.min_cities == psource2->value.min_cities;
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
  case VUT_MAXTILETOTALUNITS:
    return psource1->value.max_tile_total_units == psource2->value.max_tile_total_units;
  case VUT_MAXTILETOPUNITS:
    return psource1->value.max_tile_top_units == psource2->value.max_tile_top_units;
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
  case VUT_WRAP:
    return psource1->value.wrap_property == psource2->value.wrap_property;
  case VUT_SERVERSETTING:
    return psource1->value.ssetval == psource2->value.ssetval;
  case VUT_TERRAINALTER:
    return psource1->value.terrainalter == psource2->value.terrainalter;
  case VUT_CITYTILE:
    return psource1->value.citytile == psource2->value.citytile;
  case VUT_CITYSTATUS:
    return psource1->value.citystatus == psource2->value.citystatus;
  case VUT_TILE_REL:
    return psource1->value.tilerel == psource2->value.tilerel;
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    return psource1->value.latitude == psource2->value.latitude;
  case VUT_MAX_DISTANCE_SQ:
    return psource1->value.distance_sq == psource2->value.distance_sq;
  case VUT_MAX_REGION_TILES:
    return psource1->value.region_tiles == psource2->value.region_tiles;
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
  case VUT_COUNTER:
    return counter_rule_name(psource->value.counter);
  case VUT_CITYTILE:
    return citytile_type_name(psource->value.citytile);
  case VUT_CITYSTATUS:
    return citystatus_type_name(psource->value.citystatus);
  case VUT_TILE_REL:
    return tilerel_type_name(psource->value.tilerel);
  case VUT_MINYEAR:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.minyear);

    return buffer;
  case VUT_MINCALFRAG:
    /* Rule name is 0-based number, not pretty name from ruleset */
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.mincalfrag);

    return buffer;
  case VUT_TOPO:
    return topo_flag_name(psource->value.topo_property);
  case VUT_WRAP:
    return wrap_flag_name(psource->value.wrap_property);
  case VUT_SERVERSETTING:
    return ssetv_rule_name(psource->value.ssetval);
  case VUT_ADVANCE:
    return advance_rule_name(psource->value.advance);
  case VUT_TECHFLAG:
    return tech_flag_id_name(psource->value.techflag);
  case VUT_GOVERNMENT:
    return government_rule_name(psource->value.govern);
  case VUT_GOVFLAG:
    return gov_flag_id_name(psource->value.govflag);
  case VUT_ACHIEVEMENT:
    return achievement_rule_name(psource->value.achievement);
  case VUT_STYLE:
    return style_rule_name(psource->value.style);
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    return improvement_rule_name(psource->value.building);
  case VUT_IMPR_GENUS:
    return impr_genus_id_name(psource->value.impr_genus);
  case VUT_IMPR_FLAG:
    return impr_flag_id_name(psource->value.impr_flag);
  case VUT_PLAYER_FLAG:
    return plr_flag_id_name(psource->value.plr_flag);
  case VUT_PLAYER_STATE:
    return plrstate_type_name(psource->value.plrstate);
  case VUT_EXTRA:
    return extra_rule_name(psource->value.extra);
  case VUT_TILEDEF:
    return tiledef_rule_name(psource->value.tiledef);
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
  case VUT_ORIGINAL_OWNER:
    return nation_rule_name(psource->value.origowner);
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
  case VUT_FORM_AGE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.form_age);

    return buffer;
  case VUT_MINTECHS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.min_techs);

    return buffer;
  case VUT_FUTURETECHS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.future_techs);

    return buffer;
  case VUT_MINCITIES:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.min_cities);

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
  case VUT_MAXTILETOTALUNITS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.max_tile_total_units);
    return buffer;
  case VUT_MAXTILETOPUNITS:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.max_tile_top_units);
    return buffer;
  case VUT_TERRAINCLASS:
    return terrain_class_name(psource->value.terrainclass);
  case VUT_ROADFLAG:
    return road_flag_id_name(psource->value.roadflag);
  case VUT_EXTRAFLAG:
    return extra_flag_id_name(psource->value.extraflag);
  case VUT_TERRAINALTER:
    return terrain_alteration_name(psource->value.terrainalter);
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.latitude);

    return buffer;
  case VUT_MAX_DISTANCE_SQ:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.distance_sq);

    return buffer;
  case VUT_MAX_REGION_TILES:
    fc_snprintf(buffer, sizeof(buffer), "%d", psource->value.region_tiles);

    return buffer;
  case VUT_COUNT:
    break;
  }

  fc_assert_msg(FALSE, "Invalid source kind %d.", psource->kind);

  return nullptr;
}

/**********************************************************************//**
  Make user-friendly text for the source. The text is put into a user
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
  case VUT_COUNTER:
    fc_strlcat(buf, counter_name_translation(psource->value.counter), bufsz);
    return buf;
  case VUT_TECHFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" tech"),
                 tech_flag_id_translated_name(psource->value.techflag));
    return buf;
  case VUT_GOVERNMENT:
    fc_strlcat(buf, government_name_translation(psource->value.govern),
               bufsz);
    return buf;
  case VUT_GOVFLAG:
    cat_snprintf(buf, bufsz, _("\"%s\" gov"),
                 gov_flag_id_translated_name(psource->value.govflag));
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
  case VUT_SITE:
    {
      char local_buf[1024];

      fc_snprintf(local_buf, sizeof(local_buf), _("%s site"),
                  improvement_name_translation(psource->value.building));
      fc_strlcat(buf, local_buf, bufsz);
    }

    return buf;
  case VUT_IMPR_GENUS:
    fc_strlcat(buf,
               impr_genus_id_translated_name(psource->value.impr_genus),
               bufsz);
    return buf;
  case VUT_IMPR_FLAG:
    fc_strlcat(buf,
               impr_flag_id_translated_name(psource->value.impr_flag),
               bufsz);
    return buf;
  case VUT_PLAYER_FLAG:
    fc_strlcat(buf,
               plr_flag_id_translated_name(psource->value.plr_flag),
               bufsz);
    return buf;
  case VUT_PLAYER_STATE:
    fc_strlcat(buf,
               plrstate_type_translated_name(psource->value.plrstate),
               bufsz);
    return buf;
  case VUT_EXTRA:
    fc_strlcat(buf, extra_name_translation(psource->value.extra), bufsz);
    return buf;
  case VUT_TILEDEF:
    fc_strlcat(buf, tiledef_name_translation(psource->value.tiledef), bufsz);
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
  case VUT_ORIGINAL_OWNER:
    /* TRANS: Keep short. City founding nation. */
    cat_snprintf(buf, bufsz, _("%s original owner"),
                 nation_adjective_translation(psource->value.origowner));
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
                 Q_(unit_activity_name(psource->value.activity)));
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
  case VUT_FORM_AGE:
    cat_snprintf(buf, bufsz, _("Form age %d"),
                 psource->value.form_age);
    return buf;
  case VUT_MINTECHS:
    cat_snprintf(buf, bufsz, _("%d Techs"),
                 psource->value.min_techs);
    return buf;
  case VUT_FUTURETECHS:
    cat_snprintf(buf, bufsz, _("%d Future techs"),
                 psource->value.future_techs);
    return buf;
  case VUT_MINCITIES:
    cat_snprintf(buf, bufsz, _("%d Cities"),
                 psource->value.min_cities);
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
  case VUT_MAXTILETOTALUNITS:
    /* TRANS: here <= means 'less than or equal' */
    cat_snprintf(buf, bufsz, PL_("<=%d total unit",
                                 "<=%d total units",
                                 psource->value.max_tile_total_units),
                 psource->value.max_tile_total_units);
    return buf;
  case VUT_MAXTILETOPUNITS:
    /* TRANS: here <= means 'less than or equal' */
    cat_snprintf(buf, bufsz, PL_("<=%d unit",
                                 "<=%d units", psource->value.max_tile_top_units),
                 psource->value.max_tile_top_units);
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
    /* TRANS: topology flag name ("Hex", "ISO") */
    cat_snprintf(buf, bufsz, _("%s map"),
                 _(topo_flag_name(psource->value.topo_property)));
    return buf;
  case VUT_WRAP:
    /* TRANS: wrap flag name ("WrapX", "WrapY") */
    cat_snprintf(buf, bufsz, _("%s map"),
                 _(wrap_flag_name(psource->value.wrap_property)));
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
    case CITYT_WORKED:
      fc_strlcat(buf, _("Worked tile"), bufsz);
      break;
    case CITYT_SAME_CONTINENT:
      fc_strlcat(buf, _("Same continent tile"), bufsz);
      break;
    case CITYT_BORDERING_TCLASS_REGION:
      /* TRANS: Short for "a tile of other terrain class mass near city" */
      fc_strlcat(buf, _("Port reachable tile"), bufsz);
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
    case CITYS_STARVED:
      fc_strlcat(buf, _("Starved"), bufsz);
      break;
    case CITYS_DISORDER:
      fc_strlcat(buf, _("Disorder"), bufsz);
      break;
    case CITYS_CELEBRATION:
      fc_strlcat(buf, _("Celebration"), bufsz);
      break;
    case CITYS_TRANSFERRED:
      /* TRANS: CityStatus value - city has changed hands */
      fc_strlcat(buf, _("Transferred"), bufsz);
      break;
    case CITYS_CAPITALCONNECTED:
      fc_strlcat(buf, _("CapitalConnected"), bufsz);
      break;
    case CITYS_LAST:
      fc_assert(psource->value.citystatus != CITYS_LAST);
      fc_strlcat(buf, "error", bufsz);
      break;
    }
    return buf;
  case VUT_TILE_REL:
    switch (psource->value.tilerel) {
    case TREL_SAME_TCLASS:
      fc_strlcat(buf, _("Same terrain class"), bufsz);
      break;
    case TREL_SAME_REGION:
      fc_strlcat(buf, _("Same continent/ocean"), bufsz);
      break;
    case TREL_ONLY_OTHER_REGION:
      fc_strlcat(buf, _("Only other continent/ocean"), bufsz);
      break;
    case TREL_REGION_SURROUNDED:
      fc_strlcat(buf, _("Lake/island surrounded"), bufsz);
      break;
    case TREL_COUNT:
      fc_assert(psource->value.tilerel != TREL_COUNT);
      fc_strlcat(buf, "error", bufsz);
      break;
    }
    return buf;
  case VUT_MINLATITUDE:
    /* TRANS: here >= means 'greater than or equal'. */
    cat_snprintf(buf, bufsz, _("Latitude >= %d"),
                 psource->value.latitude);
    return buf;
  case VUT_MAXLATITUDE:
    /* TRANS: here <= means 'less than or equal'. */
    cat_snprintf(buf, bufsz, _("Latitude <= %d"),
                 psource->value.latitude);
    return buf;
  case VUT_MAX_DISTANCE_SQ:
    /* TRANS: here <= means 'less than or equal'. */
    cat_snprintf(buf, bufsz, _("Squared distance <= %d"),
                 psource->value.distance_sq);
    return buf;
  case VUT_MAX_REGION_TILES:
    cat_snprintf(buf, bufsz, _("%d or fewer region tiles"),
                 psource->value.region_tiles);
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
  case VUT_SITE:
    return impr_build_shield_cost(pcity, target->value.building);
  case VUT_UTYPE:
    return utype_build_shield_cost(pcity, nullptr, target->value.utype);
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
    would actively prevent the fulfillment of?
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
  } else if (preq->source.kind == VUT_GOVFLAG) {
    return BV_ISSET(source->value.govern->flags, preq->source.value.govflag)
      ? ITF_YES : ITF_NO;
  }

  return ITF_NOT_APPLICABLE;
}

/**********************************************************************//**
  Find if number of cities fulfills a requirement
**************************************************************************/
static enum req_item_found mincities_found(const struct requirement *preq,
                                           const struct universal *source)
{
  fc_assert(source->value.min_cities);

  if (preq->source.kind == VUT_MINCITIES) {
    return preq->source.value.min_cities <= source->value.min_cities ? ITF_YES
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
  case VUT_SITE:
    if (source->value.building == preq->source.value.building) {
      return ITF_YES;
    }
    break;
  case VUT_IMPR_GENUS:
    if (source->value.building->genus == preq->source.value.impr_genus) {
      return ITF_YES;
    }
    break;
  case VUT_IMPR_FLAG:
    if (improvement_has_flag(source->value.building,
                             preq->source.value.impr_flag)) {
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

  TODO: Handle simple VUT_TILEDEF cases.
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
  universal_found_function[VUT_SITE] = &improvement_found;
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
  universal_found_function[VUT_MINCITIES] = &mincities_found;
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
