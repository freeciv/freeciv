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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* common */
#include "achievements.h"
#include "counters.h"
#include "game.h"
#include "government.h"
#include "nation.h"
#include "requirements.h"
#include "server_settings.h"
#include "specialist.h"
#include "tech.h"
#include "tiledef.h"
#include "traderoutes.h"

/* server */
#include "rssanity.h"
#include "settings.h"

#include "univ_value.h"

/********************************************************************//**
  Initialize universal value with a value suitable for the kind.

  Returns TRUE iff there's any values universal kind can have with
  current ruleset.
************************************************************************/
bool universal_value_initial(struct universal *src)
{
  switch (src->kind) {
  case VUT_NONE:
    /* Value of None should never be used */
    return TRUE;
  case VUT_ADVANCE:
    if (game.control.num_tech_types <= 0) {
      return FALSE;
    }
    src->value.advance = advance_by_number(A_NONE);
    return TRUE;
  case VUT_COUNTER:
    if (counters_get_city_counters_count() <= 0) {
      return FALSE;
    }
    src->value.counter = counter_by_index(0, CTGT_CITY);
    return TRUE;
  case VUT_GOVERNMENT:
    src->value.govern = game.government_during_revolution;
    return TRUE;
  case VUT_GOVFLAG:
    src->value.govflag = 0;
    return TRUE;
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    if (game.control.num_impr_types <= 0) {
      return FALSE;
    }
    src->value.building = improvement_by_number(0);
    return TRUE;
  case VUT_TERRAIN:
    src->value.terrain = terrain_by_number(0);
    return TRUE;
  case VUT_NATION:
    if (game.control.nation_count <= 0) {
      return FALSE;
    }
    src->value.nation = nation_by_number(0);
    return TRUE;
  case VUT_UTYPE:
    if (game.control.num_unit_types <= 0) {
      return FALSE;
    }
    src->value.utype = utype_by_number(0);
    return TRUE;
  case VUT_UTFLAG:
    src->value.unitflag = (enum unit_type_flag_id)0;
    return TRUE;
  case VUT_UCLASS:
    if (game.control.num_unit_classes <= 0) {
      return FALSE;
    }
    src->value.uclass = uclass_by_number(0);
    return TRUE;
  case VUT_UCFLAG:
    src->value.unitclassflag = (enum unit_class_flag_id)0;
    return TRUE;
  case VUT_OTYPE:
    src->value.outputtype = (enum output_type_id)0;
    return TRUE;
  case VUT_SPECIALIST:
    if (game.control.num_specialist_types <= 0) {
      return FALSE;
    }
    src->value.specialist = specialist_by_number(0);
    return TRUE;
  case VUT_MINSIZE:
    src->value.minsize = 0;
    return TRUE;
  case VUT_AI_LEVEL:
    src->value.ai_level = AI_LEVEL_CHEATING;
    return TRUE;
  case VUT_TERRAINCLASS:
    src->value.terrainclass = TC_LAND;
    return TRUE;
  case VUT_MINYEAR:
    src->value.minyear = 0;
    return TRUE;
  case VUT_MINCALFRAG:
    src->value.mincalfrag = 0;
    return TRUE;
  case VUT_TERRAINALTER:
    src->value.terrainalter = TA_CAN_IRRIGATE;
    return TRUE;
  case VUT_CITYTILE:
    src->value.citytile = CITYT_CENTER;
    return TRUE;
  case VUT_CITYSTATUS:
    src->value.citystatus = CITYS_OWNED_BY_ORIGINAL;
    return TRUE;
  case VUT_GOOD:
    if (game.control.num_goods_types <= 0) {
      return FALSE;
    }
    src->value.good = goods_by_number(0);
    return TRUE;
  case VUT_TERRFLAG:
    src->value.terrainflag = TER_NO_BARBS;
    return TRUE;
  case VUT_NATIONALITY:
    if (game.control.nation_count <= 0) {
      return FALSE;
    }
    src->value.nationality = nation_by_number(0);
    return TRUE;
  case VUT_ORIGINAL_OWNER:
    if (game.control.nation_count <= 0) {
      return FALSE;
    }
    src->value.origowner = nation_by_number(0);
    return TRUE;
  case VUT_ROADFLAG:
    src->value.roadflag = RF_RIVER;
    return TRUE;
  case VUT_EXTRA:
    if (game.control.num_extra_types <= 0) {
      return FALSE;
    }
    src->value.extra = extra_by_number(0);
    return TRUE;
  case VUT_TILEDEF:
    if (game.control.num_tiledef_types <= 0) {
      return FALSE;
    }
    src->value.tiledef = tiledef_by_number(0);
    return TRUE;
  case VUT_TECHFLAG:
    src->value.techflag = TF_BONUS_TECH;
    return TRUE;
  case VUT_ACHIEVEMENT:
    if (game.control.num_achievement_types <= 0) {
      return FALSE;
    }
    src->value.achievement = achievement_by_number(0);
    return TRUE;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    src->value.diplrel = DS_WAR;
    return TRUE;
  case VUT_MAXTILETOTALUNITS:
    src->value.max_tile_total_units = 0;
    return TRUE;
  case VUT_MAXTILETOPUNITS:
    src->value.max_tile_top_units = 0;
    return TRUE;
  case VUT_STYLE:
    if (game.control.num_styles <= 0) {
      return FALSE;
    }
    src->value.style = style_by_number(0);
    return TRUE;
  case VUT_MINCULTURE:
    src->value.minculture = 0;
    return TRUE;
  case VUT_MINFOREIGNPCT:
    src->value.minforeignpct = 0;
    return TRUE;
  case VUT_UNITSTATE:
    src->value.unit_state = USP_TRANSPORTED;
    return TRUE;
  case VUT_ACTIVITY:
    src->value.activity = ACTIVITY_IDLE;
    return TRUE;
  case VUT_MINMOVES:
    src->value.minmoves = 0;
    return TRUE;
  case VUT_MINVETERAN:
    src->value.minveteran = 0;
    return TRUE;
  case VUT_MINHP:
    src->value.min_hit_points = 0;
    return TRUE;
  case VUT_AGE:
    src->value.age = 0;
    return TRUE;
  case VUT_FORM_AGE:
    src->value.form_age = 0;
    return TRUE;
  case VUT_NATIONGROUP:
    if (nation_group_count() <= 0) {
      return FALSE;
    }
    src->value.nationgroup = nation_group_by_number(0);
    return TRUE;
  case VUT_TOPO:
    src->value.topo_property = TF_ISO;
    return TRUE;
  case VUT_WRAP:
    src->value.wrap_property = WRAP_X;
    return TRUE;
  case VUT_SERVERSETTING:
    src->value.ssetval
        = ssetv_from_values(server_setting_by_name("killstack"), TRUE);
    return TRUE;
  case VUT_IMPR_GENUS:
    src->value.impr_genus = IG_IMPROVEMENT;
    return TRUE;
  case VUT_IMPR_FLAG:
    src->value.impr_flag = IF_VISIBLE_BY_OTHERS;
    return TRUE;
  case VUT_PLAYER_FLAG:
    src->value.plr_flag = PLRF_AI;
    return TRUE;
  case VUT_PLAYER_STATE:
    src->value.plrstate = PLRS_BARBARIAN;
    return TRUE;
  case VUT_ACTION:
    src->value.action = action_by_number(0);
    return TRUE;
  case VUT_MINTECHS:
    src->value.min_techs = 0;
    return TRUE;
  case VUT_FUTURETECHS:
    src->value.future_techs = 0;
    return TRUE;
  case VUT_MINCITIES:
    src->value.min_cities = 0;
    return TRUE;
  case VUT_EXTRAFLAG:
    src->value.extraflag = EF_NATIVE_TILE;
    return TRUE;
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
    src->value.latitude = 0;
    return TRUE;
  case VUT_MAX_DISTANCE_SQ:
    src->value.distance_sq = 0;
    return TRUE;
  case VUT_MAX_REGION_TILES:
    src->value.region_tiles = 0;
    return TRUE;
  case VUT_TILE_REL:
    src->value.tilerel = TREL_SAME_REGION;
    return TRUE;
  case VUT_COUNT:
    fc_assert(src->kind != VUT_COUNT);
    return FALSE;
  }

  return FALSE;
}

/********************************************************************//**
  Call cb() for each value possible for the universal kind.
************************************************************************/
void universal_kind_values(struct universal *univ,
                           univ_kind_values_cb cb, void *data)
{
  int i;

  switch (univ->kind) {
  case VUT_NONE:
    break;
  case VUT_ADVANCE:
    advance_re_active_iterate(padv) {
      cb(advance_rule_name(padv), univ->value.advance == padv, data);
    } advance_re_active_iterate_end;
    break;
  case VUT_COUNTER:
    city_counters_iterate(pcount) {
      cb(counter_rule_name(pcount), univ->value.counter == pcount, data);
    } city_counters_iterate_end;
    break;
  case VUT_GOVERNMENT:
    governments_re_active_iterate(pgov) {
      cb(government_rule_name(pgov), univ->value.govern == pgov, data);
    } governments_re_active_iterate_end;
    break;
  case VUT_GOVFLAG:
    for (i = 0; i < GOVF_LAST_USER_FLAG; i++) {
      cb(gov_flag_id_name(i), univ->value.govflag == i, data);
    }
    break;
  case VUT_IMPROVEMENT:
  case VUT_SITE:
    improvement_re_active_iterate(pimpr) {
      cb(improvement_rule_name(pimpr), univ->value.building == pimpr, data);
    } improvement_re_active_iterate_end;
    break;
  case VUT_TERRAIN:
    terrain_re_active_iterate(pterr) {
      cb(terrain_rule_name(pterr), univ->value.terrain == pterr, data);
    } terrain_re_active_iterate_end;
    break;
  case VUT_NATION:
    nations_iterate(pnat) {
      cb(nation_rule_name(pnat), univ->value.nation == pnat, data);
    } nations_iterate_end;
    break;
  case VUT_UTYPE:
    unit_type_re_active_iterate(putype) {
      cb(utype_rule_name(putype), univ->value.utype == putype, data);
    } unit_type_re_active_iterate_end;
    break;
  case VUT_UCLASS:
    unit_class_re_active_iterate(pclass) {
      cb(uclass_rule_name(pclass), univ->value.uclass == pclass, data);
    } unit_class_re_active_iterate_end;
    break;
  case VUT_OTYPE:
    output_type_iterate(otype) {
      cb(get_output_name(otype), univ->value.outputtype == otype, data);
    } output_type_iterate_end;
    break;
  case VUT_GOOD:
    goods_type_re_active_iterate(pgood) {
      cb(goods_rule_name(pgood), univ->value.good == pgood, data);
    } goods_type_re_active_iterate_end;
    break;
  case VUT_NATIONALITY:
    nations_iterate(pnat) {
      cb(nation_rule_name(pnat), univ->value.nationality == pnat, data);
    } nations_iterate_end;
    break;
  case VUT_ORIGINAL_OWNER:
    nations_iterate(pnat) {
      cb(nation_rule_name(pnat), univ->value.origowner == pnat, data);
    } nations_iterate_end;
    break;
  case VUT_EXTRA:
    extra_type_re_active_iterate(pextra) {
      cb(extra_rule_name(pextra), univ->value.extra == pextra, data);
    } extra_type_re_active_iterate_end;
    break;
  case VUT_TILEDEF:
    tiledef_iterate(ptd) {
      cb(tiledef_rule_name(ptd), univ->value.tiledef == ptd, data);
    } tiledef_iterate_end;
    break;
  case VUT_STYLE:
    styles_re_active_iterate(pstyle) {
      cb(style_rule_name(pstyle), univ->value.style == pstyle, data);
    } styles_re_active_iterate_end;
    break;
  case VUT_AI_LEVEL:
    for (i = 0; i < AI_LEVEL_COUNT; i++) {
      cb(ai_level_name(i), univ->value.ai_level == i, data);
    }
    break;
  case VUT_SPECIALIST:
    specialist_type_re_active_iterate(pspe) {
      cb(specialist_rule_name(pspe), univ->value.specialist == pspe, data);
    } specialist_type_re_active_iterate_end;
    break;
  case VUT_TERRAINCLASS:
    for (i = 0; i < TC_COUNT; i++) {
      cb(terrain_class_name(i), univ->value.terrainclass == i, data);
    }
    break;
  case VUT_UTFLAG:
    for (i = 0; i < UTYF_LAST_USER_FLAG; i++) {
      cb(unit_type_flag_id_name(i), univ->value.unitflag == i, data);
    }
    break;
  case VUT_UCFLAG:
    for (i = 0; i < UCF_COUNT; i++) {
      cb(unit_class_flag_id_name(i), univ->value.unitclassflag == i, data);
    }
    break;
  case VUT_TERRFLAG:
    for (i = 0; i < TER_USER_LAST; i++) {
      cb(terrain_flag_id_name(i), univ->value.terrainflag == i, data);
    }
    break;
  case VUT_ROADFLAG:
    for (i = 0; i < RF_COUNT; i++) {
      cb(road_flag_id_name(i), univ->value.roadflag == i, data);
    }
    break;
  case VUT_TECHFLAG:
    for (i = 0; i < TF_COUNT; i++) {
      cb(tech_flag_id_name(i), univ->value.techflag == i, data);
    }
    break;
  case VUT_EXTRAFLAG:
    for (i = 0; i < EF_COUNT; i++) {
      cb(extra_flag_id_name(i), univ->value.extraflag == i, data);
    }
    break;
  case VUT_TERRAINALTER:
    for (i = 0; i < TA_COUNT; i++) {
      cb(terrain_alteration_name(i), univ->value.terrainalter == i, data);
    }
    break;
  case VUT_CITYTILE:
    for (i = 0; i < CITYT_LAST; i++) {
      cb(citytile_type_name(i), univ->value.citytile == i, data);
    }
    break;
  case VUT_CITYSTATUS:
    for (i = 0; i < CITYS_LAST; i++) {
      cb(citystatus_type_name(i), univ->value.citystatus == i, data);
    }
    break;
  case VUT_TILE_REL:
    for (i = 0; i < TREL_COUNT; i++) {
      cb(tilerel_type_name(i), univ->value.tilerel == i, data);
    }
    break;
  case VUT_ACHIEVEMENT:
    achievements_re_active_iterate(pach) {
      cb(achievement_rule_name(pach), univ->value.achievement == pach, data);
    } achievements_re_active_iterate_end;
    break;
  case VUT_DIPLREL:
  case VUT_DIPLREL_TILE:
  case VUT_DIPLREL_TILE_O:
  case VUT_DIPLREL_UNITANY:
  case VUT_DIPLREL_UNITANY_O:
    for (i = 0; i < DS_LAST; i++) {
      cb(diplstate_type_name(i), univ->value.diplrel == i, data);
    }
    for (; i < DRO_LAST; i++) {
      cb(diplrel_other_name(i), univ->value.diplrel == i, data);
    }
    break;
  case VUT_UNITSTATE:
    for (i = 0; i < USP_COUNT; i++) {
      cb(ustate_prop_name(i), univ->value.unit_state == i, data);
    }
    break;
  case VUT_ACTIVITY:
    activity_type_iterate(act) {
      if (!universal_is_legal_in_requirement(
            &(struct universal){.kind = VUT_ACTIVITY,
                                .value = {.activity = act}})) {
        /* This activity is excluded from appearing in requirement
         * vectors. */
        continue;
      }
      cb(unit_activity_name(act), univ->value.activity == act, data);
    } activity_type_iterate_end;
    break;
  case VUT_NATIONGROUP:
    nation_groups_iterate(pgroup) {
      cb(nation_group_rule_name(pgroup), univ->value.nationgroup == pgroup, data);
    } nation_groups_iterate_end;
    break;
  case VUT_TOPO:
    for (i = 0; i < TOPO_FLAG_BITS; i++) {
      cb(topo_flag_name(1 << i), univ->value.topo_property == 1 << i, data);
    }
    break;
  case VUT_WRAP:
    for (i = 0; i < WRAP_FLAG_BITS; i++) {
      cb(wrap_flag_name(1 << i), univ->value.wrap_property == 1 << i, data);
    }
    break;
  case VUT_SERVERSETTING:
    for (i = 0;
         /* Only binary settings with the value TRUE are currently
          * supported. */
         i < settings_number();
         i++) {
      if (sanity_check_server_setting_value_in_req(i)) {
        cb(ssetv_rule_name(i),
           univ->value.ssetval == ssetv_from_values(i, TRUE), data);
      }
    }
    break;
  case VUT_IMPR_GENUS:
    genus_iterate(genus) {
      cb(impr_genus_id_name(genus), univ->value.impr_genus == genus, data);
    } genus_iterate_end;
    break;
  case VUT_IMPR_FLAG:
    for (i = 0; i < IF_COUNT; i++) {
      cb(impr_flag_id_name(i), univ->value.impr_flag == i, data);
    }
    break;
  case VUT_PLAYER_FLAG:
    for (i = 0; i < PLRF_COUNT; i++) {
      cb(plr_flag_id_name(i), univ->value.plr_flag == i, data);
    }
    break;
  case VUT_PLAYER_STATE:
    for (i = 0; i < PLRS_LAST; i++) {
      cb(plrstate_type_name(i), univ->value.plrstate == i, data);
    }
    break;
  case VUT_ACTION:
    action_iterate(act) {
      struct action *pact = action_by_number(act);

      cb(action_rule_name(pact), univ->value.action == pact, data);
    } action_iterate_end;
    break;
  case VUT_MINSIZE:
  case VUT_MINYEAR:
  case VUT_MINCALFRAG:
  case VUT_MAXTILETOTALUNITS:
  case VUT_MAXTILETOPUNITS:
  case VUT_MINCULTURE:
  case VUT_MINFOREIGNPCT:
  case VUT_MINMOVES:
  case VUT_MINVETERAN:
  case VUT_MINHP:
  case VUT_AGE:
  case VUT_FORM_AGE:
  case VUT_MINTECHS:
  case VUT_FUTURETECHS:
  case VUT_MINCITIES:
  case VUT_MINLATITUDE:
  case VUT_MAXLATITUDE:
  case VUT_MAX_DISTANCE_SQ:
  case VUT_MAX_REGION_TILES:
    /* Requirement types having numerical value */
    cb(nullptr, FALSE, data);
    break;
  case VUT_COUNT:
    fc_assert(univ->kind != VUT_COUNT);
    break;
  }
}
