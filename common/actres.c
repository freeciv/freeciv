/****************************************************************************
 Freeciv - Copyright (C) 2023 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "actres.h"

static struct actres act_results[ACTRES_LAST] = {
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_ESTABLISH_EMBASSY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_SPY_INVESTIGATE_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_POISON */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_STEAL_GOLD */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_SABOTAGE_CITY */
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC }, /* ACTRES_SPY_TARGETED_SABOTAGE_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_SABOTAGE_CITY_PRODUCTION */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_STEAL_TECH */
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC }, /* ACTRES_SPY_TARGETED_STEAL_TECH */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_INCITE_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRADE_ROUTE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_MARKETPLACE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HELP_WONDER */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_BRIBE_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_SABOTAGE_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_CAPTURE_UNITS */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_FOUND_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_JOIN_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_STEAL_MAPS */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_BOMBARD */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_NUKE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_NUKE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_NUKE_UNITS */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_DESTROY_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_EXPEL_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_DISBAND_UNIT_RECOVER */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_DISBAND_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HOME_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_UPGRADE_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_PARADROP */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_AIRLIFT */
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD },      /* ACTRES_ATTACK */
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE },       /* ACTRES_STRIKE_BUILDING */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_STRIKE_PRODUCTION */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_CONQUER_CITY */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HEAL_UNIT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSFORM_TERRAIN */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_CULTIVATE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_PLANT */
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE },        /* ACTRES_PILLAGE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_FORTIFY */
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE },       /* ACTRES_ROAD */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_CONVERT */
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE },       /* ACTRES_BASE */
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE },       /* ACTRES_MINE */
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE },       /* ACTRES_IRRIGATE */
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE },        /* ACTRES_CLEAN_POLLUTION */
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE },        /* ACTRES_CLEAN_FALLOUT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_ALIGHT */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_UNLOAD */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_DISEMBARK */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_BOARD */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_EMBARK */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_SPREAD_PLAGUE */
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC },    /* ACTRES_SPY_ATTACK */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_CONQUER_EXTRAS */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HUT_ENTER */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HUT_FRIGHTEN */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_UNIT_MOVE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_PARADROP_CONQUER */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_HOMELESS */
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD },      /* ACTRES_WIPE_UNITS */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_SPY_ESCAPE */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE },          /* ACTRES_TRANSPORT_LOAD */
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE }         /* ACTRES_CLEAN */
};

/*********************************************************************//**
  Initialize action results system
*************************************************************************/
void actres_init(void)
{
}

/*********************************************************************//**
  Free resources allocated by the action results system
*************************************************************************/
void actres_free(void)
{
}

/**********************************************************************//**
  Returns the sub target complexity for the action with the specified
  result when it has the specified target kind and sub target kind.
**************************************************************************/
enum act_tgt_compl actres_target_compl_calc(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return ACT_TGT_COMPL_SIMPLE;
  }

  fc_assert_ret_val(action_result_is_valid(result), ACT_TGT_COMPL_SIMPLE);

  return act_results[result].sub_tgt_compl;
}

/**********************************************************************//**
  Get the battle kind that can prevent an action.
**************************************************************************/
enum action_battle_kind actres_get_battle_kind(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return ABK_NONE;
  }

  fc_assert_ret_val(action_result_is_valid(result), ABK_NONE);

  return act_results[result].battle_kind;
}
