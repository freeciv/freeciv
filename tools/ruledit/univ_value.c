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
#include "game.h"
#include "requirements.h"
#include "specialist.h"
#include "tech.h"
#include "traderoutes.h"

#include "univ_value.h"

/************************************************************************
  Initialize universal value with a value suitable for the kind.

  Returns TRUE iff there's any values universal kind can have with
  current ruleset.
************************************************************************/
bool universal_value_initial(struct universal *src)
{
  /* TODO: Implement really */
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
  case VUT_GOVERNMENT:
    src->value.govern = game.government_during_revolution;
    return TRUE;
  case VUT_IMPROVEMENT:
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
  case VUT_TERRAINALTER:
    src->value.terrainalter = TA_CAN_IRRIGATE;
    return TRUE;
  case VUT_CITYTILE:
    src->value.citytile = CITYT_CENTER;
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
  case VUT_BASEFLAG:
    src->value.baseflag = BF_NOT_AGGRESSIVE;
    return TRUE;
  case VUT_ROADFLAG:
    src->value.roadflag = RF_REQUIRES_BRIDGE;
    return TRUE;
  case VUT_EXTRA:
    if (game.control.num_extra_types <= 0) {
      return FALSE;
    }
    src->value.extra = extra_by_number(0);
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
    src->value.diplrel = DS_WAR;
    return TRUE;
  case VUT_MAXTILEUNITS:
    src->value.max_tile_units = 0;
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
  case VUT_UNITSTATE:
    src->value.unit_state = USP_TRANSPORTED;
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
  case VUT_NATIONGROUP:
    if (nation_group_count() <= 0) {
      return FALSE;
    }
    src->value.nationgroup = nation_group_by_number(0);
    return TRUE;
  case VUT_TOPO:
    src->value.topo_property = TF_ISO;
    return TRUE;
  case VUT_IMPR_GENUS:
    src->value.impr_genus = IG_IMPROVEMENT;
    return TRUE;
  case VUT_ACTION:
    src->value.action = action_by_number(0);
    return TRUE;
  case VUT_MINTECHS:
    src->value.min_techs = 0;
    return TRUE;
  case VUT_EXTRAFLAG:
    src->value.extraflag = EF_NATIVE_TILE;
    return TRUE;
  case VUT_COUNT:
    fc_assert(src->kind != VUT_COUNT);
    return FALSE;
  }

  return FALSE;
}
