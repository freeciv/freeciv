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
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_ESTABLISH_EMBASSY */
    FALSE},
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_SPY_INVESTIGATE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_POISON */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_STEAL_GOLD */
    TRUE},
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_CITY */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC, /* ACTRES_SPY_TARGETED_SABOTAGE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_CITY_PRODUCTION */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_STEAL_TECH */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_DIPLOMATIC, /* ACTRES_SPY_TARGETED_STEAL_TECH */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_INCITE_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRADE_ROUTE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_MARKETPLACE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HELP_WONDER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_BRIBE_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SABOTAGE_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CAPTURE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_FOUND_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_JOIN_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_STEAL_MAPS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_BOMBARD */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_NUKE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_NUKE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_NUKE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DESTROY_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_EXPEL_UNIT */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DISBAND_UNIT_RECOVER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_DISBAND_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HOME_CITY */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_UPGRADE_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PARADROP */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_AIRLIFT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD,      /* ACTRES_ATTACK */
    TRUE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_STRIKE_BUILDING */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_STRIKE_PRODUCTION */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONQUER_CITY */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HEAL_UNIT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSFORM_TERRAIN */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CULTIVATE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PLANT */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_PILLAGE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_FORTIFY */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_ROAD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONVERT */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_BASE */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_MINE */
    FALSE },
  { ACT_TGT_COMPL_MANDATORY, ABK_NONE,       /* ACTRES_IRRIGATE */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN_POLLUTION */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN_FALLOUT */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_DEBOARD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_UNLOAD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_DISEMBARK */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_BOARD */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_EMBARK */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_SPREAD_PLAGUE */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_DIPLOMATIC,    /* ACTRES_SPY_ATTACK */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_CONQUER_EXTRAS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HUT_ENTER */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HUT_FRIGHTEN */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_UNIT_MOVE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_PARADROP_CONQUER */
    FALSE }, /* TODO: should this be hostile? */
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_HOMELESS */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_STANDARD,      /* ACTRES_WIPE_UNITS */
    TRUE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_SPY_ESCAPE */
    FALSE },
  { ACT_TGT_COMPL_SIMPLE, ABK_NONE,          /* ACTRES_TRANSPORT_LOAD */
    FALSE },
  { ACT_TGT_COMPL_FLEXIBLE, ABK_NONE,        /* ACTRES_CLEAN */
    FALSE }
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

/**********************************************************************//**
  Returns TRUE iff the specified action result indicates hostile action.
**************************************************************************/
bool actres_is_hostile(enum action_result result)
{
  if (result == ACTRES_NONE) {
    return FALSE;
  }

  return act_results[result].hostile;
}
