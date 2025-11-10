/***********************************************************************
 Freeciv - Copyright (C) 1996-2013 - Freeciv Development Team
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

#include <math.h> /* ceil(), floor() */

/* utility */
#include "astring.h"

/* common */
#include "actions.h"
#include "city.h"
#include "combat.h"
#include "fc_interface.h"
#include "game.h"
#include "map.h"
#include "metaknowledge.h"
#include "movement.h"
#include "oblig_reqs.h"
#include "research.h"
#include "server_settings.h"
#include "specialist.h"
#include "unit.h"


/* Values used to interpret action probabilities.
 *
 * Action probabilities are sent over the network. A change in a value here
 * will therefore change the network protocol.
 *
 * A change in a value here should also update the action probability
 * format documentation in fc_types.h */
/* The lowest possible probability value (0%) */
#define ACTPROB_VAL_MIN 0
/* The highest possible probability value (100%) */
#define ACTPROB_VAL_MAX 200
/* A probability increase of 1% corresponds to this increase. */
#define ACTPROB_VAL_1_PCT (ACTPROB_VAL_MAX / 100)
/* Action probability doesn't apply when min is this. */
#define ACTPROB_VAL_NA 253
/* Action probability unsupported when min is this. */
#define ACTPROB_VAL_NOT_IMPL 254

static struct action *actions[MAX_NUM_ACTIONS];
struct action **_actions = actions;
struct action_auto_perf auto_perfs[MAX_NUM_ACTION_AUTO_PERFORMERS];
static bool actions_initialized = FALSE;

static struct action_enabler_list *action_enablers_by_action[MAX_NUM_ACTIONS];

static struct astring ui_name_str = ASTRING_INIT;

static struct action *
unit_action_new(action_id id,
                enum action_result result,
                bool rare_pop_up,
                bool unitwaittime_controlled,
                enum moves_actor_kind moves_actor,
                const int min_distance,
                const int max_distance,
                bool actor_consuming_always);
static struct action *
player_action_new(action_id id,
                  enum action_result result);
static struct action *
city_action_new(action_id id,
                enum action_result result);

static bool is_enabler_active(const struct action_enabler *enabler,
                              const struct req_context *actor,
                              const struct req_context *target);

static inline bool
action_prob_is_signal(const struct act_prob probability);
static inline bool
action_prob_not_relevant(const struct act_prob probability);
static inline bool
action_prob_not_impl(const struct act_prob probability);

static struct act_prob ap_diplomat_battle(const struct unit *pattacker,
                                          const struct unit *pvictim,
                                          const struct tile *tgt_tile,
                                          const struct action *paction)
  fc__attribute((nonnull(3)));

/* Make sure that an action distance can target the whole map. */
FC_STATIC_ASSERT(MAP_DISTANCE_MAX <= ACTION_DISTANCE_LAST_NON_SIGNAL,
                 action_range_can_not_cover_the_whole_map);

static struct action_list *actlist_by_result[ACTRES_LAST];
static struct action_list *actlist_by_activity[ACTIVITY_LAST];


/**********************************************************************//**
  Hard code the actions.
**************************************************************************/
static void hard_code_actions(void)
{
  actions[ACTION_SPY_POISON] =
      unit_action_new(ACTION_SPY_POISON, ACTRES_SPY_POISON,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_POISON_ESC] =
      unit_action_new(ACTION_SPY_POISON_ESC, ACTRES_SPY_POISON,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_UNIT] =
      unit_action_new(ACTION_SPY_SABOTAGE_UNIT, ACTRES_SPY_SABOTAGE_UNIT,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_UNIT_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_UNIT_ESC, ACTRES_SPY_SABOTAGE_UNIT,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_BRIBE_UNIT] =
      unit_action_new(ACTION_SPY_BRIBE_UNIT, ACTRES_SPY_BRIBE_UNIT,
                      FALSE, TRUE,
                      /* Tries a forced move if the target unit is alone at
                       * its tile and not in a city. Takes all movement if
                       * the forced move fails. */
                      MAK_FORCED,
                      0, 1, FALSE);
  actions[ACTION_SPY_BRIBE_STACK] =
      unit_action_new(ACTION_SPY_BRIBE_STACK, ACTRES_SPY_BRIBE_STACK,
                      FALSE, TRUE,
                      MAK_FORCED,
                      0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_CITY] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY, ACTRES_SPY_SABOTAGE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_CITY_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_ESC, ACTRES_SPY_SABOTAGE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      unit_action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY,
                      ACTRES_SPY_TARGETED_SABOTAGE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_SABOTAGE_CITY_PRODUCTION] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_PRODUCTION,
                      ACTRES_SPY_SABOTAGE_CITY_PRODUCTION,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC] =
      unit_action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC,
                      ACTRES_SPY_TARGETED_SABOTAGE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC] =
      unit_action_new(ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC,
                      ACTRES_SPY_SABOTAGE_CITY_PRODUCTION,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_INCITE_CITY] =
      unit_action_new(ACTION_SPY_INCITE_CITY, ACTRES_SPY_INCITE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_INCITE_CITY_ESC] =
      unit_action_new(ACTION_SPY_INCITE_CITY_ESC, ACTRES_SPY_INCITE_CITY,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_ESTABLISH_EMBASSY] =
      unit_action_new(ACTION_ESTABLISH_EMBASSY,
                      ACTRES_ESTABLISH_EMBASSY,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_ESTABLISH_EMBASSY_STAY] =
      unit_action_new(ACTION_ESTABLISH_EMBASSY_STAY,
                      ACTRES_ESTABLISH_EMBASSY,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_TECH] =
      unit_action_new(ACTION_SPY_STEAL_TECH,
                      ACTRES_SPY_STEAL_TECH,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_TECH_ESC] =
      unit_action_new(ACTION_SPY_STEAL_TECH_ESC,
                      ACTRES_SPY_STEAL_TECH,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_TARGETED_STEAL_TECH] =
      unit_action_new(ACTION_SPY_TARGETED_STEAL_TECH,
                      ACTRES_SPY_TARGETED_STEAL_TECH,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_TARGETED_STEAL_TECH_ESC] =
      unit_action_new(ACTION_SPY_TARGETED_STEAL_TECH_ESC,
                      ACTRES_SPY_TARGETED_STEAL_TECH,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_INVESTIGATE_CITY] =
      unit_action_new(ACTION_SPY_INVESTIGATE_CITY,
                      ACTRES_SPY_INVESTIGATE_CITY,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_INV_CITY_SPEND] =
      unit_action_new(ACTION_INV_CITY_SPEND,
                      ACTRES_SPY_INVESTIGATE_CITY,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_GOLD] =
      unit_action_new(ACTION_SPY_STEAL_GOLD,
                      ACTRES_SPY_STEAL_GOLD,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_STEAL_GOLD_ESC] =
      unit_action_new(ACTION_SPY_STEAL_GOLD_ESC,
                      ACTRES_SPY_STEAL_GOLD,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_SPREAD_PLAGUE] =
      unit_action_new(ACTION_SPY_SPREAD_PLAGUE,
                      ACTRES_SPY_SPREAD_PLAGUE,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_SPY_ESCAPE] =
      unit_action_new(ACTION_SPY_ESCAPE,
                      ACTRES_SPY_ESCAPE,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_TRADE_ROUTE] =
      unit_action_new(ACTION_TRADE_ROUTE, ACTRES_TRADE_ROUTE,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_MARKETPLACE] =
      unit_action_new(ACTION_MARKETPLACE, ACTRES_MARKETPLACE,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_HELP_WONDER] =
      unit_action_new(ACTION_HELP_WONDER, ACTRES_HELP_WONDER,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_CAPTURE_UNITS] =
      unit_action_new(ACTION_CAPTURE_UNITS, ACTRES_CAPTURE_UNITS,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1, 1,
                      FALSE);
  actions[ACTION_FOUND_CITY] =
      unit_action_new(ACTION_FOUND_CITY, ACTRES_FOUND_CITY,
                      TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile.
                       * Reason: The Freeciv code assumes that the city
                       * founding unit is located at the tile were the new
                       * city is founded. */
                      0, 0,
                      TRUE);
  actions[ACTION_JOIN_CITY] =
      unit_action_new(ACTION_JOIN_CITY, ACTRES_JOIN_CITY,
                      TRUE, TRUE,
                      MAK_STAYS, 0, 1, TRUE);
  actions[ACTION_STEAL_MAPS] =
      unit_action_new(ACTION_STEAL_MAPS, ACTRES_STEAL_MAPS,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_STEAL_MAPS_ESC] =
      unit_action_new(ACTION_STEAL_MAPS_ESC, ACTRES_STEAL_MAPS,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_BOMBARD] =
      unit_action_new(ACTION_BOMBARD, ACTRES_BOMBARD,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD2] =
      unit_action_new(ACTION_BOMBARD2, ACTRES_BOMBARD,
                      FALSE, TRUE,
                      MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_2_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD3] =
      unit_action_new(ACTION_BOMBARD3, ACTRES_BOMBARD,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_3_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD4] =
      unit_action_new(ACTION_BOMBARD4, ACTRES_BOMBARD,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_4_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD_LETHAL] =
      unit_action_new(ACTION_BOMBARD_LETHAL, ACTRES_BOMBARD,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_lethal_max_range */
                      1,
                      FALSE);
  actions[ACTION_BOMBARD_LETHAL2] =
      unit_action_new(ACTION_BOMBARD_LETHAL2, ACTRES_BOMBARD,
                      FALSE, TRUE, MAK_STAYS,
                      /* A single domestic unit at the target tile will make
                       * the action illegal. It must therefore be performed
                       * from another tile. */
                      1,
                      /* Overwritten by the ruleset's bombard_lethal_2_max_range */
                      1,
                      FALSE);
  actions[ACTION_SPY_NUKE] =
      unit_action_new(ACTION_SPY_NUKE, ACTRES_SPY_NUKE,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, TRUE);
  actions[ACTION_SPY_NUKE_ESC] =
      unit_action_new(ACTION_SPY_NUKE_ESC, ACTRES_SPY_NUKE,
                      FALSE, TRUE,
                      MAK_ESCAPE, 0, 1, FALSE);
  actions[ACTION_NUKE] =
      unit_action_new(ACTION_NUKE, ACTRES_NUKE,
                      TRUE, TRUE,
                      MAK_STAYS, 0,
                      /* Overwritten by the ruleset's
                       * explode_nuclear_max_range */
                      0,
                      TRUE);
  actions[ACTION_NUKE_CITY] =
      unit_action_new(ACTION_NUKE_CITY, ACTRES_NUKE,
                      TRUE, TRUE,
                      MAK_STAYS, 1, 1, TRUE);
  actions[ACTION_NUKE_UNITS] =
      unit_action_new(ACTION_NUKE_UNITS, ACTRES_NUKE_UNITS,
                      TRUE, TRUE,
                      MAK_STAYS, 1, 1, TRUE);
  actions[ACTION_DESTROY_CITY] =
      unit_action_new(ACTION_DESTROY_CITY, ACTRES_DESTROY_CITY,
                      TRUE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_EXPEL_UNIT] =
      unit_action_new(ACTION_EXPEL_UNIT, ACTRES_EXPEL_UNIT,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_DISBAND_UNIT_RECOVER] =
      unit_action_new(ACTION_DISBAND_UNIT_RECOVER, ACTRES_DISBAND_UNIT_RECOVER,
                      TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      0, 1,
                      TRUE);
  actions[ACTION_DISBAND_UNIT] =
      unit_action_new(ACTION_DISBAND_UNIT,
                      /* Can't be ACTRES_NONE because
                       * action_success_actor_consume() sets unit lost
                       * reason based on action result. */
                      ACTRES_DISBAND_UNIT,
                      TRUE, TRUE,
                      MAK_STAYS, 0, 0, TRUE);
  actions[ACTION_HOME_CITY] =
      unit_action_new(ACTION_HOME_CITY, ACTRES_HOME_CITY,
                      TRUE, FALSE,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_HOMELESS] =
      unit_action_new(ACTION_HOMELESS, ACTRES_HOMELESS,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_UPGRADE_UNIT] =
      unit_action_new(ACTION_UPGRADE_UNIT, ACTRES_UPGRADE_UNIT,
                      TRUE, TRUE, MAK_STAYS,
                      /* Illegal to perform to a target on another tile to
                       * keep the rules exactly as they were for now. */
                      0, 0,
                      FALSE);
  actions[ACTION_PARADROP] =
      unit_action_new(ACTION_PARADROP, ACTRES_PARADROP,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_PARADROP_CONQUER] =
      unit_action_new(ACTION_PARADROP_CONQUER, ACTRES_PARADROP_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_PARADROP_FRIGHTEN] =
      unit_action_new(ACTION_PARADROP_FRIGHTEN, ACTRES_PARADROP,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_PARADROP_FRIGHTEN_CONQUER] =
      unit_action_new(ACTION_PARADROP_FRIGHTEN_CONQUER,
                      ACTRES_PARADROP_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_PARADROP_ENTER] =
      unit_action_new(ACTION_PARADROP_ENTER, ACTRES_PARADROP,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_PARADROP_ENTER_CONQUER] =
      unit_action_new(ACTION_PARADROP_ENTER_CONQUER,
                      ACTRES_PARADROP_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Still limited by each unit type's
                       * paratroopers_range field. */
                      ACTION_DISTANCE_MAX,
                      FALSE);
  actions[ACTION_AIRLIFT] =
      unit_action_new(ACTION_AIRLIFT, ACTRES_AIRLIFT,
                      TRUE, TRUE,
                      MAK_TELEPORT,
                      1,
                      /* Overwritten by the ruleset's airlift_max_range. */
                      ACTION_DISTANCE_UNLIMITED,
                      FALSE);
  actions[ACTION_ATTACK] =
      unit_action_new(ACTION_ATTACK, ACTRES_ATTACK,
                      FALSE, TRUE,
                      /* Tries a forced move if the target unit's tile has
                       * no non-allied units and the occupychance dice roll
                       * tells it to move. */
                      MAK_FORCED,
                      1, 1, FALSE);
  actions[ACTION_ATTACK2] =
      unit_action_new(ACTION_ATTACK2, ACTRES_ATTACK,
                      FALSE, TRUE,
                      /* Tries a forced move if the target unit's tile has
                       * no non-allied units and the occupychance dice roll
                       * tells it to move. */
                      MAK_FORCED,
                      1, 1, FALSE);
  actions[ACTION_SUICIDE_ATTACK] =
      unit_action_new(ACTION_SUICIDE_ATTACK, ACTRES_ATTACK,
                      FALSE, TRUE,
                      MAK_FORCED, 1, 1, TRUE);
  actions[ACTION_SUICIDE_ATTACK2] =
      unit_action_new(ACTION_SUICIDE_ATTACK2, ACTRES_ATTACK,
                      FALSE, TRUE,
                      MAK_FORCED, 1, 1, TRUE);
  actions[ACTION_WIPE_UNITS] =
      unit_action_new(ACTION_WIPE_UNITS, ACTRES_WIPE_UNITS,
                      FALSE, TRUE,
                      MAK_FORCED,
                      1, 1, FALSE);
  actions[ACTION_STRIKE_BUILDING] =
      unit_action_new(ACTION_STRIKE_BUILDING, ACTRES_STRIKE_BUILDING,
                      FALSE, FALSE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_STRIKE_PRODUCTION] =
      unit_action_new(ACTION_STRIKE_PRODUCTION, ACTRES_STRIKE_PRODUCTION,
                      FALSE, FALSE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY_SHRINK] =
      unit_action_new(ACTION_CONQUER_CITY_SHRINK, ACTRES_CONQUER_CITY,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY_SHRINK2] =
      unit_action_new(ACTION_CONQUER_CITY_SHRINK2, ACTRES_CONQUER_CITY,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY_SHRINK3] =
      unit_action_new(ACTION_CONQUER_CITY_SHRINK3, ACTRES_CONQUER_CITY,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_CITY_SHRINK4] =
      unit_action_new(ACTION_CONQUER_CITY_SHRINK4, ACTRES_CONQUER_CITY,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_EXTRAS] =
      unit_action_new(ACTION_CONQUER_EXTRAS, ACTRES_CONQUER_EXTRAS,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_EXTRAS2] =
      unit_action_new(ACTION_CONQUER_EXTRAS2, ACTRES_CONQUER_EXTRAS,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_EXTRAS3] =
      unit_action_new(ACTION_CONQUER_EXTRAS3, ACTRES_CONQUER_EXTRAS,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_CONQUER_EXTRAS4] =
      unit_action_new(ACTION_CONQUER_EXTRAS4, ACTRES_CONQUER_EXTRAS,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HEAL_UNIT] =
      unit_action_new(ACTION_HEAL_UNIT, ACTRES_HEAL_UNIT,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_HEAL_UNIT2] =
      unit_action_new(ACTION_HEAL_UNIT2, ACTRES_HEAL_UNIT,
                      FALSE, TRUE,
                      MAK_STAYS, 0, 1, FALSE);
  actions[ACTION_TRANSFORM_TERRAIN] =
      unit_action_new(ACTION_TRANSFORM_TERRAIN, ACTRES_TRANSFORM_TERRAIN,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSFORM_TERRAIN2] =
      unit_action_new(ACTION_TRANSFORM_TERRAIN2, ACTRES_TRANSFORM_TERRAIN,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CULTIVATE] =
      unit_action_new(ACTION_CULTIVATE, ACTRES_CULTIVATE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CULTIVATE2] =
      unit_action_new(ACTION_CULTIVATE2, ACTRES_CULTIVATE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PLANT] =
      unit_action_new(ACTION_PLANT, ACTRES_PLANT,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PLANT2] =
      unit_action_new(ACTION_PLANT2, ACTRES_PLANT,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PILLAGE] =
      unit_action_new(ACTION_PILLAGE, ACTRES_PILLAGE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_PILLAGE2] =
      unit_action_new(ACTION_PILLAGE2, ACTRES_PILLAGE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CLEAN] =
      unit_action_new(ACTION_CLEAN, ACTRES_CLEAN,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CLEAN2] =
      unit_action_new(ACTION_CLEAN2, ACTRES_CLEAN,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_FORTIFY] =
      unit_action_new(ACTION_FORTIFY, ACTRES_FORTIFY,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_FORTIFY2] =
      unit_action_new(ACTION_FORTIFY2, ACTRES_FORTIFY,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_ROAD] =
      unit_action_new(ACTION_ROAD, ACTRES_ROAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_ROAD2] =
      unit_action_new(ACTION_ROAD2, ACTRES_ROAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_CONVERT] =
      unit_action_new(ACTION_CONVERT, ACTRES_CONVERT,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_BASE] =
      unit_action_new(ACTION_BASE, ACTRES_BASE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_BASE2] =
      unit_action_new(ACTION_BASE2, ACTRES_BASE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_MINE] =
      unit_action_new(ACTION_MINE, ACTRES_MINE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_MINE2] =
      unit_action_new(ACTION_MINE2, ACTRES_MINE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_IRRIGATE] =
      unit_action_new(ACTION_IRRIGATE, ACTRES_IRRIGATE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_IRRIGATE2] =
      unit_action_new(ACTION_IRRIGATE2, ACTRES_IRRIGATE,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_DEBOARD] =
      unit_action_new(ACTION_TRANSPORT_DEBOARD, ACTRES_TRANSPORT_DEBOARD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_BOARD] =
      unit_action_new(ACTION_TRANSPORT_BOARD, ACTRES_TRANSPORT_BOARD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_BOARD2] =
      unit_action_new(ACTION_TRANSPORT_BOARD2, ACTRES_TRANSPORT_BOARD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_BOARD3] =
      unit_action_new(ACTION_TRANSPORT_BOARD3, ACTRES_TRANSPORT_BOARD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_UNLOAD] =
      unit_action_new(ACTION_TRANSPORT_UNLOAD, ACTRES_TRANSPORT_UNLOAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_LOAD] =
      unit_action_new(ACTION_TRANSPORT_LOAD, ACTRES_TRANSPORT_LOAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_LOAD2] =
      unit_action_new(ACTION_TRANSPORT_LOAD2, ACTRES_TRANSPORT_LOAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_LOAD3] =
      unit_action_new(ACTION_TRANSPORT_LOAD3, ACTRES_TRANSPORT_LOAD,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK1] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK1,
                      ACTRES_TRANSPORT_DISEMBARK,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK2] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK2,
                      ACTRES_TRANSPORT_DISEMBARK,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK3] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK3,
                      ACTRES_TRANSPORT_DISEMBARK,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_DISEMBARK4] =
      unit_action_new(ACTION_TRANSPORT_DISEMBARK4,
                      ACTRES_TRANSPORT_DISEMBARK,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_EMBARK] =
      unit_action_new(ACTION_TRANSPORT_EMBARK, ACTRES_TRANSPORT_EMBARK,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_EMBARK2] =
      unit_action_new(ACTION_TRANSPORT_EMBARK2, ACTRES_TRANSPORT_EMBARK,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_EMBARK3] =
      unit_action_new(ACTION_TRANSPORT_EMBARK3, ACTRES_TRANSPORT_EMBARK,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TRANSPORT_EMBARK4] =
      unit_action_new(ACTION_TRANSPORT_EMBARK4, ACTRES_TRANSPORT_EMBARK,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_SPY_ATTACK] =
      unit_action_new(ACTION_SPY_ATTACK, ACTRES_SPY_ATTACK,
                      FALSE, TRUE,
                      MAK_STAYS, 1, 1, FALSE);
  actions[ACTION_HUT_ENTER] =
      unit_action_new(ACTION_HUT_ENTER, ACTRES_HUT_ENTER,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_ENTER2] =
      unit_action_new(ACTION_HUT_ENTER2, ACTRES_HUT_ENTER,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_ENTER3] =
      unit_action_new(ACTION_HUT_ENTER3, ACTRES_HUT_ENTER,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_ENTER4] =
      unit_action_new(ACTION_HUT_ENTER4, ACTRES_HUT_ENTER,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_FRIGHTEN] =
      unit_action_new(ACTION_HUT_FRIGHTEN, ACTRES_HUT_FRIGHTEN,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_FRIGHTEN2] =
      unit_action_new(ACTION_HUT_FRIGHTEN2, ACTRES_HUT_FRIGHTEN,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_FRIGHTEN3] =
      unit_action_new(ACTION_HUT_FRIGHTEN3, ACTRES_HUT_FRIGHTEN,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_HUT_FRIGHTEN4] =
      unit_action_new(ACTION_HUT_FRIGHTEN4, ACTRES_HUT_FRIGHTEN,
                      FALSE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_UNIT_MOVE] =
      unit_action_new(ACTION_UNIT_MOVE, ACTRES_UNIT_MOVE,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_UNIT_MOVE2] =
      unit_action_new(ACTION_UNIT_MOVE2, ACTRES_UNIT_MOVE,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_UNIT_MOVE3] =
      unit_action_new(ACTION_UNIT_MOVE3, ACTRES_UNIT_MOVE,
                      TRUE, TRUE,
                      MAK_REGULAR, 1, 1, FALSE);
  actions[ACTION_TELEPORT] =
      unit_action_new(ACTION_TELEPORT, ACTRES_TELEPORT,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1, 1, FALSE);
  actions[ACTION_TELEPORT2] =
      unit_action_new(ACTION_TELEPORT2, ACTRES_TELEPORT,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1, 1, FALSE);
  actions[ACTION_TELEPORT3] =
      unit_action_new(ACTION_TELEPORT3, ACTRES_TELEPORT,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1, 1, FALSE);
  actions[ACTION_TELEPORT_CONQUER] =
      unit_action_new(ACTION_TELEPORT_CONQUER, ACTRES_TELEPORT_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1,
                      ACTION_DISTANCE_MAX, FALSE);
  actions[ACTION_TELEPORT_FRIGHTEN] =
      unit_action_new(ACTION_TELEPORT_FRIGHTEN,
                      ACTRES_TELEPORT,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1,
                      ACTION_DISTANCE_MAX, FALSE);
  actions[ACTION_TELEPORT_FRIGHTEN_CONQUER] =
      unit_action_new(ACTION_TELEPORT_FRIGHTEN_CONQUER,
                      ACTRES_TELEPORT_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1,
                      ACTION_DISTANCE_MAX, FALSE);
  actions[ACTION_TELEPORT_ENTER] =
      unit_action_new(ACTION_TELEPORT_ENTER,
                      ACTRES_TELEPORT,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1,
                      ACTION_DISTANCE_MAX, FALSE);
  actions[ACTION_TELEPORT_ENTER_CONQUER] =
      unit_action_new(ACTION_TELEPORT_ENTER_CONQUER,
                      ACTRES_TELEPORT_CONQUER,
                      TRUE, TRUE,
                      MAK_TELEPORT, 1,
                      ACTION_DISTANCE_MAX, FALSE);
  actions[ACTION_GAIN_VETERANCY] =
      unit_action_new(ACTION_GAIN_VETERANCY, ACTRES_ENABLER_CHECK,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_ESCAPE] =
      unit_action_new(ACTION_ESCAPE, ACTRES_ENABLER_CHECK,
                      TRUE, FALSE,
                      MAK_STAYS, 0, 0, FALSE);
  actions[ACTION_USER_ACTION1] =
      unit_action_new(ACTION_USER_ACTION1, ACTRES_NONE,
                      FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_USER_ACTION2] =
      unit_action_new(ACTION_USER_ACTION2, ACTRES_NONE,
                      FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_USER_ACTION3] =
      unit_action_new(ACTION_USER_ACTION3, ACTRES_NONE,
                      FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_USER_ACTION4] =
      unit_action_new(ACTION_USER_ACTION4, ACTRES_NONE,
                      FALSE, TRUE,
                      MAK_UNREPRESENTABLE,
                      /* Overwritten by the ruleset */
                      0, 1, FALSE);
  actions[ACTION_COLLECT_RANSOM] =
      unit_action_new(ACTION_COLLECT_RANSOM, ACTRES_COLLECT_RANSOM,
                      FALSE, TRUE,
                      /* Tries a forced move if the target unit's tile has
                       * no non-allied units and the occupychance dice roll
                       * tells it to move. */
                      MAK_FORCED,
                      1, 1, FALSE);

  actions[ACTION_CIVIL_WAR] =
      player_action_new(ACTION_CIVIL_WAR, ACTRES_ENABLER_CHECK);

  actions[ACTION_FINISH_UNIT] =
      city_action_new(ACTION_FINISH_UNIT, ACTRES_ENABLER_CHECK);

  actions[ACTION_FINISH_BUILDING] =
      city_action_new(ACTION_FINISH_BUILDING, ACTRES_ENABLER_CHECK);

  /* The structure even for these need to be created, for
   * the action_id_rule_name() to work on iterations. */

  /*
  actions[ACTION_UNUSED_1]
    = unit_action_new(ACTION_UNUSED_1, ACTRES_NONE,
                      FALSE, FALSE,
                      MAK_UNREPRESENTABLE,
                      0, 0, FALSE);
  */
}

/**********************************************************************//**
  Initialize the actions and the action enablers.
**************************************************************************/
void actions_init(void)
{
  int i, j;

  for (i = 0; i < ACTRES_LAST; i++) {
    actlist_by_result[i] = action_list_new();
  }
  for (i = 0; i < ACTIVITY_LAST; i++) {
    actlist_by_activity[i] = action_list_new();
  }

  /* Hard code the actions */
  hard_code_actions();

  /* Initialize the action enabler list */
  action_iterate_all(act) {
    action_enablers_by_action[act] = action_enabler_list_new();
  } action_iterate_all_end;

  /* Initialize action obligatory hard requirements. */
  oblig_hard_reqs_init();

  /* Obligatory hard requirements are sorted by requirement in the source
   * code. This makes it easy to read, modify and explain it. */
  hard_code_oblig_hard_reqs();

  /* Initialize the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    /* Nothing here. Nothing after this point. */
    auto_perfs[i].cause = AAPC_COUNT;

    /* The criteria to pick *this* auto performer for its cause. */
    requirement_vector_init(&auto_perfs[i].reqs);

    for (j = 0; j < MAX_NUM_ACTIONS; j++) {
      /* Nothing here. Nothing after this point. */
      auto_perfs[i].alternatives[j] = ACTION_NONE;
    }
  }

  /* The actions them self are now initialized. */
  actions_initialized = TRUE;
}

/**********************************************************************//**
  Generate action related data based on the currently loaded ruleset.
  Done before ruleset sanity checking and ruleset compatibility post
  processing.
**************************************************************************/
void actions_rs_pre_san_gen(void)
{
  /* Some obligatory hard requirements needs access to the rest of the
   * ruleset. */
  hard_code_oblig_hard_reqs_ruleset();
}

/**********************************************************************//**
  Free the actions and the action enablers.
**************************************************************************/
void actions_free(void)
{
  int i;

  /* Don't consider the actions to be initialized any longer. */
  actions_initialized = FALSE;

  action_iterate_all(act) {
    action_enabler_list_iterate(action_enablers_by_action[act], enabler) {
      action_enabler_free(enabler);
    } action_enabler_list_iterate_end;

    action_enabler_list_destroy(action_enablers_by_action[act]);

    FC_FREE(actions[act]);
  } action_iterate_all_end;

  /* Free the obligatory hard action requirements. */
  oblig_hard_reqs_free();

  /* Free the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    requirement_vector_free(&auto_perfs[i].reqs);
  }

  astr_free(&ui_name_str);

  for (i = 0; i < ACTRES_LAST; i++) {
    action_list_destroy(actlist_by_result[i]);
    actlist_by_result[i] = nullptr;
  }
  for (i = 0; i < ACTIVITY_LAST; i++) {
    action_list_destroy(actlist_by_activity[i]);
    actlist_by_activity[i] = nullptr;
  }
}

/**********************************************************************//**
  Returns TRUE iff the actions are initialized.

  Doesn't care about action enablers.
**************************************************************************/
bool actions_are_ready(void)
{
  if (!actions_initialized) {
    /* The actions them self aren't initialized yet. */
    return FALSE;
  }

  action_noninternal_iterate(act) {
    if (actions[act]->ui_name[0] == '\0') {
      /* A noninternal action without a UI name exists means that
       * the ruleset haven't loaded yet. The ruleset loading will assign
       * a default name to any actions not named by the ruleset.
       * The client will get this name from the server. */
      return FALSE;
    }
  } action_noninternal_iterate_end;

  /* The actions should be ready for use. */
  return TRUE;
}

/**********************************************************************//**
  Create a new action.
**************************************************************************/
static struct action *action_new(action_id id,
                                 enum action_actor_kind aak,
                                 enum action_result result,
                                 const int min_distance,
                                 const int max_distance,
                                 bool actor_consuming_always)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  action->id = id;
  action->configured = FALSE;

  action->result = result;

  if (result != ACTRES_LAST) {
    enum unit_activity act = actres_activity_result(result);

    action_list_append(actlist_by_result[result], action);

    if (act != ACTIVITY_LAST) {
      action_list_append(actlist_by_activity[act], action);
    }
  }

  /* Not set here */
  BV_CLR_ALL(action->sub_results);

  action->actor_kind = aak;
  action->target_kind = actres_target_kind_default(result);
  action->sub_target_kind = actres_sub_target_kind_default(result);
  action->target_complexity = actres_target_compl_calc(result);

  /* ASTK_NONE implies ACT_TGT_COMPL_SIMPLE and
   * !ASTK_NONE implies !ACT_TGT_COMPL_SIMPLE */
  fc_assert_msg(((action->sub_target_kind == ASTK_NONE
                  && action->target_complexity == ACT_TGT_COMPL_SIMPLE)
                 || (action->sub_target_kind != ASTK_NONE
                     && action->target_complexity != ACT_TGT_COMPL_SIMPLE)),
                "%s contradicts itself regarding sub targets.",
                action_rule_name(action));

  /* The distance between the actor and itself is always 0. */
  fc_assert(action->target_kind != ATK_SELF
            || (min_distance == 0 && max_distance == 0));

  action->min_distance = min_distance;
  action->max_distance = max_distance;

  action->actor_consuming_always = actor_consuming_always;

  /* Loaded from the ruleset. Until generalized actions are ready it has to
   * be defined separately from other action data. */
  action->ui_name[0] = '\0';
  action->quiet = FALSE;
  BV_CLR_ALL(action->blocked_by);

  return action;
}

/**********************************************************************//**
  Create a new action performed by a unit actor.
**************************************************************************/
static struct action *
unit_action_new(action_id id,
                enum action_result result,
                bool rare_pop_up,
                bool unitwaittime_controlled,
                enum moves_actor_kind moves_actor,
                const int min_distance,
                const int max_distance,
                bool actor_consuming_always)
{
  struct action *act = action_new(id, AAK_UNIT, result,
                                  min_distance, max_distance,
                                  actor_consuming_always);

  act->actor.is_unit.rare_pop_up = rare_pop_up;

  act->actor.is_unit.unitwaittime_controlled = unitwaittime_controlled;

  act->actor.is_unit.moves_actor = moves_actor;

  return act;
}

/**********************************************************************//**
  Create a new action performed by a player actor.
**************************************************************************/
static struct action *player_action_new(action_id id,
                                        enum action_result result)
{
  struct action *act = action_new(id, AAK_PLAYER, result,
                                  0, 0, FALSE);

  return act;
}

/**********************************************************************//**
  Create a new action performed by a city actor.
**************************************************************************/
static struct action *city_action_new(action_id id,
                                      enum action_result result)
{
  struct action *act = action_new(id, AAK_CITY, result,
                                  0, 0, FALSE);

  return act;
}

/**********************************************************************//**
  Returns TRUE iff the specified action ID refers to a valid action.
**************************************************************************/
bool action_id_exists(const action_id act_id)
{
  /* Actions are still hard coded. */
  return gen_action_is_valid(act_id) && actions[act_id];
}

/**********************************************************************//**
  Return the action with the given name.

  Returns nullptr if no action with the given name exists.
**************************************************************************/
struct action *action_by_rule_name(const char *name)
{
  /* Actions are still hard coded in the gen_action enum. */
  action_id act_id = gen_action_by_name(name, fc_strcasecmp);

  if (!action_id_exists(act_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action named %s", name);

    return nullptr;
  }

  return action_by_number(act_id);
}

/**********************************************************************//**
  Get the actor kind of an action.
**************************************************************************/
enum action_actor_kind action_get_actor_kind(const struct action *paction)
{
  fc_assert_ret_val_msg(paction, AAK_COUNT, "Action doesn't exist.");

  return paction->actor_kind;
}

/**********************************************************************//**
  Get the target kind of an action.
**************************************************************************/
enum action_target_kind action_get_target_kind(
    const struct action *paction)
{
  fc_assert_ret_val_msg(paction, ATK_COUNT, "Action doesn't exist.");

  return paction->target_kind;
}

/**********************************************************************//**
  Get the sub target kind of an action.
**************************************************************************/
enum action_sub_target_kind action_get_sub_target_kind(
    const struct action *paction)
{
  fc_assert_ret_val_msg(paction, ASTK_COUNT, "Action doesn't exist.");

  return paction->sub_target_kind;
}

/**********************************************************************//**
  Returns TRUE iff the specified action allows the player to provide
  details in addition to actor and target. Returns FALSE if the action
  doesn't support any additional details.
**************************************************************************/
bool action_has_complex_target(const struct action *paction)
{
  fc_assert_ret_val(paction != nullptr, FALSE);

  return paction->target_complexity >= ACT_TGT_COMPL_FLEXIBLE;
}

/**********************************************************************//**
  Returns TRUE iff the specified action REQUIRES the player to provide
  details in addition to actor and target. Returns FALSE if the action
  doesn't support any additional details or if they can be set by Freeciv
  it self.
**************************************************************************/
bool action_requires_details(const struct action *paction)
{
  fc_assert_ret_val(paction != nullptr, FALSE);

  return paction->target_complexity >= ACT_TGT_COMPL_MANDATORY;
}

/**********************************************************************//**
  Returns TRUE iff a unit's ability to perform this action will pop up the
  action selection dialog before the player asks for it only in exceptional
  cases.

  An example of an exceptional case is when the player tries to move a
  unit to a tile it can't move to but can perform this action to.
**************************************************************************/
bool action_id_is_rare_pop_up(action_id act_id)
{
  fc_assert_ret_val_msg((action_id_exists(act_id)),
                        FALSE, "Action %d don't exist.", act_id);
  fc_assert_ret_val(action_id_get_actor_kind(act_id) == AAK_UNIT, FALSE);

  return actions[act_id]->actor.is_unit.rare_pop_up;
}

/**********************************************************************//**
  Returns TRUE iff the specified distance between actor and target is
  sm,aller or equal to the max range accepted by the specified action.
**************************************************************************/
bool action_distance_inside_max(const struct action *action,
                                const int distance)
{
  return (distance <= action->max_distance
          || action->max_distance == ACTION_DISTANCE_UNLIMITED);
}

/**********************************************************************//**
  Returns TRUE iff the specified distance between actor and target is
  within the range acceptable to the specified action.
**************************************************************************/
bool action_distance_accepted(const struct action *action,
                              const int distance)
{
  fc_assert_ret_val(action, FALSE);

  return (distance >= action->min_distance
          && action_distance_inside_max(action, distance));
}

/**********************************************************************//**
  Returns TRUE iff blocked will be illegal if blocker is legal.
**************************************************************************/
bool action_would_be_blocked_by(const struct action *blocked,
                                const struct action *blocker)
{
  fc_assert_ret_val(blocked, FALSE);
  fc_assert_ret_val(blocker, FALSE);

  return BV_ISSET(blocked->blocked_by, action_number(blocker));
}

/**********************************************************************//**
  Get the universal number of the action.
**************************************************************************/
int action_number(const struct action *action)
{
  return action->id;
}

/**********************************************************************//**
  Get the rule name of the action.
**************************************************************************/
const char *action_rule_name(const struct action *action)
{
  /* Rule name is still hard coded. */
  return action_id_rule_name(action->id);
}

/**********************************************************************//**
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.

  This always returns the same static string, just modified according
  to the call. Copy the result if you want it to remain valid over
  another call to this function.
**************************************************************************/
const char *action_name_translation(const struct action *paction)
{
  /* Use action_id_name_translation() to format the UI name. */
  return action_id_name_translation(paction->id);
}

/**********************************************************************//**
  Get the rule name of the action.
**************************************************************************/
const char *action_id_rule_name(action_id act_id)
{
  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return gen_action_name(act_id);
}

/**********************************************************************//**
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_id_name_translation(action_id act_id)
{
  return action_prepare_ui_name(act_id, "", ACTPROB_NA, nullptr);
}

/**********************************************************************//**
  Get the action name with a mnemonic ready to display in the UI.
**************************************************************************/
const char *action_get_ui_name_mnemonic(action_id act_id,
                                        const char *mnemonic)
{
  return action_prepare_ui_name(act_id, mnemonic, ACTPROB_NA, nullptr);
}

/**********************************************************************//**
  Returns a text representation of the action probability prob unless it
  is a signal. Returns nullptr if prob is a signal.

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
static const char *action_prob_to_text(const struct act_prob prob)
{
  static struct astring chance = ASTRING_INIT;

  /* How to interpret action probabilities like prob is documented in
   * fc_types.h */
  if (action_prob_is_signal(prob)) {
    fc_assert(action_prob_not_impl(prob)
              || action_prob_not_relevant(prob));

    /* Unknown because of missing server support or should not exits. */
    return nullptr;
  }

  if (prob.min == prob.max) {
    /* Only one probability in range. */

    /* TRANS: the probability that an action will succeed. Given in
     * percentage. Resolution is 0.5%. */
    astr_set(&chance, _("%.1f%%"), (double)prob.max / ACTPROB_VAL_1_PCT);
  } else {
    /* TRANS: the interval (end points included) where the probability of
     * the action's success is. Given in percentage. Resolution is 0.5%. */
    astr_set(&chance, _("[%.1f%%, %.1f%%]"),
             (double)prob.min / ACTPROB_VAL_1_PCT,
             (double)prob.max / ACTPROB_VAL_1_PCT);
  }

  return astr_str(&chance);
}

/**********************************************************************//**
  Get the UI name ready to show the action in the UI. It is possible to
  add a client specific mnemonic; it is assumed that if the mnemonic
  appears in the action name it can be escaped by doubling.
  Success probability information is interpreted and added to the text.
  A custom text can be inserted before the probability information.

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
const char *action_prepare_ui_name(action_id act_id, const char *mnemonic,
                                   const struct act_prob prob,
                                   const char *custom)
{
  struct astring chance = ASTRING_INIT;

  /* Text representation of the probability. */
  const char *probtxt;

  if (!actions_are_ready()) {
    /* Could be a client who haven't gotten the ruleset yet */

    /* so there shouldn't be any action probability to show */
    fc_assert(action_prob_not_relevant(prob));

    /* but the action should be valid */
    fc_assert_ret_val_msg(action_id_exists(act_id),
                          "Invalid action",
                          "Invalid action %d", act_id);

    /* and no custom text will be inserted */
    fc_assert(custom == nullptr || custom[0] == '\0');

    /* Make the best of what is known */
    astr_set(&ui_name_str, _("%s%s (name may be wrong)"),
             mnemonic, action_id_rule_name(act_id));

    /* Return the guess. */
    return astr_str(&ui_name_str);
  }

  probtxt = action_prob_to_text(prob);

  /* Format the info part of the action's UI name. */
  if (probtxt != nullptr && custom != nullptr) {
    /* TRANS: action UI name's info part with custom info and probability.
     * Hint: you can move the paren handling from this string to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s; %s)"), custom, probtxt);
  } else if (probtxt != nullptr) {
    /* TRANS: action UI name's info part with probability.
     * Hint: you can move the paren handling from this string to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s)"), probtxt);
  } else if (custom != nullptr) {
    /* TRANS: action UI name's info part with custom info.
     * Hint: you can move the paren handling from this string to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s)"), custom);
  } else {
    /* No info part to display. */
    astr_clear(&chance);
  }

  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  /* Escape any instances of the mnemonic in the action's UI format string.
   * (Assumes any mnemonic can be escaped by doubling, and that they are
   * unlikely to appear in a format specifier. True for clients seen so
   * far: Gtk's _ and Qt's &) */
  {
    struct astring fmtstr = ASTRING_INIT;
    const char *ui_name = _(actions[act_id]->ui_name);

    if (mnemonic[0] != '\0') {
      const char *hit;

      fc_assert(!strchr(mnemonic, '%'));
      while ((hit = strstr(ui_name, mnemonic))) {
        astr_add(&fmtstr, "%.*s%s%s", (int)(hit - ui_name), ui_name,
                 mnemonic, mnemonic);
        ui_name = hit + strlen(mnemonic);
      }
    }
    astr_add(&fmtstr, "%s", ui_name);

    /* Use the modified format string */
    astr_set(&ui_name_str, astr_str(&fmtstr), mnemonic,
             astr_str(&chance));

    astr_free(&fmtstr);
  }

  astr_free(&chance);

  return astr_str(&ui_name_str);
}

/**********************************************************************//**
  Explain an action probability in a way suitable for a tool tip for the
  button that starts it.
  @return an explanation of what an action probability means

  The returned string is in statically allocated astring, and thus this
  function is not thread-safe.
**************************************************************************/
const char *action_prob_explain(const struct act_prob prob)
{
  static struct astring tool_tip = ASTRING_INIT;

  if (action_prob_is_signal(prob)) {
    fc_assert(action_prob_not_impl(prob));

    /* Missing server support. No in game action will change this. */
    astr_clear(&tool_tip);
  } else if (prob.min == prob.max) {
    /* TRANS: action probability of success. Given in percentage.
     * Resolution is 0.5%. */
    astr_set(&tool_tip, _("The probability of success is %.1f%%."),
             (double)prob.max / ACTPROB_VAL_1_PCT);
  } else {
    astr_set(&tool_tip,
             /* TRANS: action probability interval (min to max). Given in
              * percentage. Resolution is 0.5%. The string at the end is
              * shown when the interval is wide enough to not be caused by
              * rounding. It explains that the interval is imprecise because
              * the player doesn't have enough information. */
             _("The probability of success is %.1f%%, %.1f%% or somewhere"
               " in between.%s"),
             (double)prob.min / ACTPROB_VAL_1_PCT,
             (double)prob.max / ACTPROB_VAL_1_PCT,
             prob.max - prob.min > 1 ?
               /* TRANS: explanation used in the action probability tooltip
                * above. Preserve leading space. */
               _(" (This is the most precise interval I can calculate "
                 "given the information our nation has access to.)") :
               "");
  }

  return astr_str(&tool_tip);
}

/**********************************************************************//**
  Get the unit type role corresponding to the ability to do the specified
  action.
**************************************************************************/
int action_get_role(const struct action *paction)
{
  fc_assert_msg(AAK_UNIT == action_get_actor_kind(paction),
                "Action %s isn't performed by a unit",
                action_rule_name(paction));

  return paction->id + L_LAST;
}

/**********************************************************************//**
  Create a new action enabler.
**************************************************************************/
struct action_enabler *action_enabler_new(void)
{
  struct action_enabler *enabler;

  enabler = fc_malloc(sizeof(*enabler));
  enabler->rulesave.ruledit_disabled = FALSE;
  enabler->rulesave.comment = nullptr;
  requirement_vector_init(&enabler->actor_reqs);
  requirement_vector_init(&enabler->target_reqs);

  /* Make sure that action doesn't end up as a random value that happens to
   * be a valid action id. */
  enabler->action = ACTION_NONE;

  return enabler;
}

/**********************************************************************//**
  Free resources allocated for the action enabler.
**************************************************************************/
void action_enabler_free(struct action_enabler *enabler)
{
  if (enabler->rulesave.comment != nullptr) {
    free(enabler->rulesave.comment);
  }

  requirement_vector_free(&enabler->actor_reqs);
  requirement_vector_free(&enabler->target_reqs);

  free(enabler);
}

/**********************************************************************//**
  Create a new copy of an existing action enabler.
**************************************************************************/
struct action_enabler *
action_enabler_copy(const struct action_enabler *original)
{
  struct action_enabler *enabler = action_enabler_new();

  enabler->action = original->action;

  requirement_vector_copy(&enabler->actor_reqs, &original->actor_reqs);
  requirement_vector_copy(&enabler->target_reqs, &original->target_reqs);

  return enabler;
}

/**********************************************************************//**
  Add an action enabler to the current ruleset.
**************************************************************************/
void action_enabler_add(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be added. */
  fc_assert_ret(enabler);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret(action_id_exists(enabler_get_action_id(enabler)));

  action_enabler_list_append(
        action_enablers_for_action(enabler_get_action_id(enabler)),
        enabler);
}

/**********************************************************************//**
  Remove an action enabler from the current ruleset.

  Returns TRUE on success.
**************************************************************************/
bool action_enabler_remove(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be removed. */
  fc_assert_ret_val(enabler, FALSE);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_exists(enabler_get_action_id(enabler)), FALSE);

  return action_enabler_list_remove(
        action_enablers_for_action(enabler_get_action_id(enabler)),
        enabler);
}

/**********************************************************************//**
  Get all enablers for an action in the current ruleset.
**************************************************************************/
struct action_enabler_list *
action_enablers_for_action(action_id action)
{
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_exists(action), nullptr);

  return action_enablers_by_action[action];
}

/**********************************************************************//**
  Returns a suggestion to add an obligatory hard requirement to an action
  enabler or nullptr if no hard obligatory reqs were missing. It is the
  responsibility of the caller to free the suggestion when it is done with
  it.

  @param enabler the action enabler to suggest a fix for.
  @param oblig   hard obligatory requirements to check
  @return a problem with fix suggestions or nullptr if no obligatory hard
          requirement problems were detected.
**************************************************************************/
static struct req_vec_problem *
ae_suggest_repair_if_no_oblig(const struct action_enabler *enabler,
                              const struct obligatory_req_vector *oblig)
{
  struct action *paction;

  /* Sanity check: a non existing action enabler is missing but it doesn't
   * miss any obligatory hard requirements. */
  fc_assert_ret_val(enabler, nullptr);

  /* Sanity check: a non existing action doesn't have any obligatory hard
   * requirements. */
  fc_assert_ret_val(action_id_exists(enabler_get_action_id(enabler)), nullptr);
  paction = enabler_get_action(enabler);

  /* No obligatory hard requirements. */
  fc_assert_ret_val(oblig, nullptr);

  obligatory_req_vector_iterate(oblig, obreq) {
    struct req_vec_problem *out;
    int i;
    bool fulfilled = FALSE;

    /* Check each alternative. */
    for (i = 0; i < obreq->contras->alternatives; i++) {
      const struct requirement_vector *ae_vec;

      /* Select action enabler requirement vector. */
      ae_vec = (obreq->contras->alternative[i].is_target
                ? &enabler->target_reqs
                : &enabler->actor_reqs);

      if (does_req_contradicts_reqs(&obreq->contras->alternative[i].req,
                                    ae_vec)
          /* Consider the hard requirement fulfilled since a universal that
           * never is there always will be absent in this ruleset. */
          || (obreq->contras->alternative[i].req.present
              && universal_never_there(
                  &obreq->contras->alternative[i].req.source))) {
        /* It is enough that one alternative accepts the enabler. */
        fulfilled = TRUE;
        break;
      }

      /* Fall back to the next alternative */
    }

    if (fulfilled) {
      /* This obligatory hard requirement isn't a problem. */
      continue;
    }

    /* Missing hard obligatory requirement detected */

    out = req_vec_problem_new(obreq->contras->alternatives,
                              obreq->error_msg,
                              action_rule_name(paction));

    for (i = 0; i < obreq->contras->alternatives; i++) {
      const struct requirement_vector *ae_vec;

      /* Select action enabler requirement vector. */
      ae_vec = (obreq->contras->alternative[i].is_target
                ? &enabler->target_reqs
                : &enabler->actor_reqs);

      /* The suggested fix is to append a requirement that makes the enabler
       * contradict the missing hard obligatory requirement detector. */
      out->suggested_solutions[i].operation = RVCO_APPEND;
      out->suggested_solutions[i].vector_number
          = action_enabler_vector_number(enabler, ae_vec);

      /* Change the requirement from what should conflict to what is
       * wanted. */
      out->suggested_solutions[i].req.present
          = !obreq->contras->alternative[i].req.present;
      out->suggested_solutions[i].req.source
          = obreq->contras->alternative[i].req.source;
      out->suggested_solutions[i].req.range
          = obreq->contras->alternative[i].req.range;
      out->suggested_solutions[i].req.survives
          = obreq->contras->alternative[i].req.survives;
      out->suggested_solutions[i].req.quiet
          = obreq->contras->alternative[i].req.quiet;
    }

    /* Return the first problem found. The next problem will be detected
     * during the next call. */
    return out;
  } obligatory_req_vector_iterate_end;

  /* No obligatory req problems found. */
  return nullptr;
}

/**********************************************************************//**
  Returns a suggestion to add an obligatory hard requirement to an action
  enabler or nullptr if no hard obligatory reqs were missing. It is the
  responsibility of the caller to free the suggestion when it is done with
  it.
  @param enabler the action enabler to suggest a fix for.
  @return a problem with fix suggestions or nullptr if no obligatory hard
          requirement problems were detected.
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_repair_oblig(const struct action_enabler *enabler)
{
  struct action *paction;
  enum action_sub_result sub_res;
  struct req_vec_problem *out;

  /* Sanity check: a non existing action enabler is missing but it doesn't
   * miss any obligatory hard requirements. */
  fc_assert_ret_val(enabler, nullptr);

  /* Sanity check: a non existing action doesn't have any obligatory hard
   * requirements. */
  fc_assert_ret_val(action_id_exists(enabler_get_action_id(enabler)), nullptr);
  paction = enabler_get_action(enabler);

  if (paction->result != ACTRES_NONE) {
    /* A hard coded action result may mean obligatory requirements. */
    out = ae_suggest_repair_if_no_oblig(enabler, oblig_hard_reqs_get(paction->result));
    if (out != nullptr) {
      return out;
    }
  }

  for (sub_res = action_sub_result_begin();
       sub_res != action_sub_result_end();
       sub_res = action_sub_result_next(sub_res)) {
    if (!BV_ISSET(paction->sub_results, sub_res)) {
      /* Not relevant */
      continue;
    }

    /* The action has this sub result. Check its hard requirements. */
    out = ae_suggest_repair_if_no_oblig(enabler,
                                        oblig_hard_reqs_get_sub(sub_res));
    if (out != nullptr) {
      return out;
    }
  }

  /* No obligatory req problems found. */
  return nullptr;
}

/**********************************************************************//**
  Returns the first local DiplRel requirement in the specified requirement
  vector or nullptr if it doesn't have a local DiplRel requirement.
  @param vec the requirement vector to look in
  @return the first local DiplRel requirement.
**************************************************************************/
static struct requirement *
req_vec_first_local_diplrel(const struct requirement_vector *vec)
{
  requirement_vector_iterate(vec, preq) {
    if (preq->source.kind == VUT_DIPLREL
        && preq->range == REQ_RANGE_LOCAL) {
      return preq;
    }
  } requirement_vector_iterate_end;

  return nullptr;
}

/**********************************************************************//**
  Detects a local DiplRel requirement in a tile targeted action without
  an explicit claimed requirement in the target reqs.
  @param enabler the enabler to look at
  @return the problem or nullptr if no problem was found
**************************************************************************/
static struct req_vec_problem *
enabler_tile_tgt_local_diplrel_implies_claimed(
    const struct action_enabler *enabler)
{
  struct req_vec_problem *out;
  struct requirement *local_diplrel;
  struct requirement *claimed_req;
  struct requirement tile_is_claimed;
  struct requirement tile_is_unclaimed;
  struct action *paction = enabler_get_action(enabler);
  struct astring astr;

  if (action_get_target_kind(paction) != ATK_TILE) {
    /* Not tile targeted */
    return nullptr;
  }

  local_diplrel = req_vec_first_local_diplrel(&enabler->actor_reqs);
  if (local_diplrel == nullptr) {
    /* No local diplrel */
    return nullptr;
  }

  /* Tile is unclaimed as a requirement. */
  tile_is_unclaimed.range = REQ_RANGE_TILE;
  tile_is_unclaimed.survives = FALSE;
  tile_is_unclaimed.source.kind = VUT_CITYTILE;
  tile_is_unclaimed.present = FALSE;
  tile_is_unclaimed.source.value.citytile = CITYT_CLAIMED;

  claimed_req = req_vec_first_contradiction_in_vec(&tile_is_unclaimed,
                                                   &enabler->target_reqs);

  if (claimed_req) {
    /* Already clear */
    return nullptr;
  }

  /* Tile is claimed as a requirement. */
  tile_is_claimed.range = REQ_RANGE_TILE;
  tile_is_claimed.survives = FALSE;
  tile_is_claimed.source.kind = VUT_CITYTILE;
  tile_is_claimed.present = TRUE;
  tile_is_claimed.source.value.citytile = CITYT_CLAIMED;

  out = req_vec_problem_new(
          1,
          /* TRANS: ruledit shows this when an enabler for a tile targeted
           * action requires that the actor has a diplomatic relationship to
           * the target but doesn't require that the target tile is claimed.
           * (DiplRel requirements to an unclaimed tile are never fulfilled
           * so this is implicit.) */
          N_("Requirement {%s} of action \"%s\" implies a claimed "
             "tile. No diplomatic relation to Nature."),
          req_to_fstring(local_diplrel, &astr), action_rule_name(paction));

  astr_free(&astr);

  /* The solution is to add the requirement that the tile is claimed */
  out->suggested_solutions[0].req = tile_is_claimed;
  out->suggested_solutions[0].vector_number
      = action_enabler_vector_number(enabler, &enabler->target_reqs);
  out->suggested_solutions[0].operation = RVCO_APPEND;

  return out;
}

/**********************************************************************//**
  Returns the first action enabler specific contradiction in the specified
  enabler or nullptr if no enabler specific contradiction is found.
  @param enabler the enabler to look at
  @return the first problem and maybe a suggested fix
**************************************************************************/
static struct req_vec_problem *
enabler_first_self_contradiction(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;
  struct requirement *local_diplrel;
  struct requirement *unclaimed_req;
  struct requirement tile_is_claimed;
  struct action *paction = enabler_get_action(enabler);
  struct astring astr1;
  struct astring astr2;

  if (action_get_target_kind(paction) != ATK_TILE) {
    /* Not tile targeted */
    return nullptr;
  }

  local_diplrel = req_vec_first_local_diplrel(&enabler->actor_reqs);
  if (local_diplrel == nullptr) {
    /* No local diplrel */
    return nullptr;
  }

  /* Tile is claimed as a requirement. */
  tile_is_claimed.range = REQ_RANGE_TILE;
  tile_is_claimed.survives = FALSE;
  tile_is_claimed.source.kind = VUT_CITYTILE;
  tile_is_claimed.present = TRUE;
  tile_is_claimed.source.value.citytile = CITYT_CLAIMED;

  unclaimed_req = req_vec_first_contradiction_in_vec(&tile_is_claimed,
                                                     &enabler->target_reqs);

  if (unclaimed_req == nullptr) {
    /* No unclaimed req */
    return nullptr;
  }

  out = req_vec_problem_new(
          2,
          /* TRANS: ruledit shows this when an enabler for a tile targeted
           * action requires that the target tile is unclaimed and that the
           * actor has a diplomatic relationship to the target. (DiplRel
           * requirements to an unclaimed tile are never fulfilled.) */
          N_("In enabler for \"%s\": No diplomatic relation to Nature."
             " Requirements {%s} and {%s} contradict each other."),
          action_rule_name(paction),
          req_to_fstring(local_diplrel, &astr1),
          req_to_fstring(unclaimed_req, &astr2));

  astr_free(&astr1);
  astr_free(&astr2);

  /* The first suggestion is to remove the diplrel */
  out->suggested_solutions[0].req = *local_diplrel;
  out->suggested_solutions[0].vector_number
      = action_enabler_vector_number(enabler, &enabler->actor_reqs);
  out->suggested_solutions[0].operation = RVCO_REMOVE;

  /* The 2nd is to remove the requirement that the tile is unclaimed */
  out->suggested_solutions[1].req = *unclaimed_req;
  out->suggested_solutions[1].vector_number
      = action_enabler_vector_number(enabler, &enabler->target_reqs);
  out->suggested_solutions[1].operation = RVCO_REMOVE;

  return out;
}

/**********************************************************************//**
  Returns a suggestion to fix the specified action enabler or nullptr if no
  fix is found to be needed. It is the responsibility of the caller to
  free the suggestion with req_vec_problem_free() when it is done with it.
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_repair(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;

  out = action_enabler_suggest_repair_oblig(enabler);
  if (out != nullptr) {
    return out;
  }

  /* Look for errors in the requirement vectors. */
  out = req_vec_suggest_repair(&enabler->actor_reqs,
                               action_enabler_vector_number,
                               enabler);
  if (out != nullptr) {
    return out;
  }

  out = req_vec_suggest_repair(&enabler->target_reqs,
                               action_enabler_vector_number,
                               enabler);
  if (out != nullptr) {
    return out;
  }

  /* Enabler specific contradictions. */
  out = enabler_first_self_contradiction(enabler);
  if (out != nullptr) {
    return out;
  }

  /* Needed in action not enabled explanation finding. */
  out = enabler_tile_tgt_local_diplrel_implies_claimed(enabler);
  if (out != nullptr) {
    return out;
  }

  /* No problems found. */
  return nullptr;
}

/**********************************************************************//**
  Returns the first action enabler specific clarification possibility in
  the specified enabler or nullptr if no enabler specific contradiction is
  found.
  @param enabler the enabler to look at
  @return the first problem and maybe a suggested fix
**************************************************************************/
static struct req_vec_problem *
enabler_first_clarification(const struct action_enabler *enabler)
{
  struct req_vec_problem *out;

  out = nullptr;

  return out;
}

/**********************************************************************//**
  Returns a suggestion to improve the specified action enabler or nullptr if
  nothing to improve is found to be needed. It is the responsibility of the
  caller to free the suggestion when it is done with it. A possible
  improvement isn't always an error.
  @param enabler the enabler to improve
  @return a suggestion to improve the specified action enabler
**************************************************************************/
struct req_vec_problem *
action_enabler_suggest_improvement(const struct action_enabler *enabler)
{
  struct action *paction;
  struct req_vec_problem *out;

  out = action_enabler_suggest_repair(enabler);
  if (out) {
    /* A bug, not just a potential improvement */
    return out;
  }

  paction = enabler_get_action(enabler);

  /* Look for improvement suggestions to the requirement vectors. */
  out = req_vec_suggest_improvement(&enabler->actor_reqs,
                                    action_enabler_vector_number,
                                    enabler);
  if (out) {
    return out;
  }
  out = req_vec_suggest_improvement(&enabler->target_reqs,
                                    action_enabler_vector_number,
                                    enabler);
  if (out) {
    return out;
  }

  /* Detect unused action enablers. */
  if (action_get_actor_kind(paction) == AAK_UNIT) {
    bool has_user = FALSE;

    unit_type_iterate(pactor) {
      if (action_actor_utype_hard_reqs_ok(paction, pactor)
          && requirement_fulfilled_by_unit_type(pactor,
                                                &(enabler->actor_reqs))) {
        has_user = TRUE;
        break;
      }
    } unit_type_iterate_end;

    if (!has_user) {
      /* TRANS: ruledit warns a user about an unused action enabler */
      out = req_vec_problem_new(0, N_("Action enabler for \"%s\" is never"
                                      " used by any unit."),
                                action_rule_name(paction));
    }
  }
  if (out != nullptr) {
    return out;
  }

  out = enabler_first_clarification(enabler);

  return out;
}

/**********************************************************************//**
  Returns the requirement vector number of the specified requirement
  vector in the specified action enabler.
  @param enabler the action enabler that may own the vector.
  @param vec the requirement vector to number.
  @return the requirement vector number the vector has in this enabler.
**************************************************************************/
req_vec_num_in_item
action_enabler_vector_number(const void *enabler,
                             const struct requirement_vector *vec)
{
  struct action_enabler *ae = (struct action_enabler *)enabler;

  if (vec == &ae->actor_reqs) {
    return 0;
  } else if (vec == &ae->target_reqs) {
    return 1;
  } else {
    return -1;
  }
}

/********************************************************************//**
  Returns a writable pointer to the specified requirement vector in the
  action enabler or nullptr if the action enabler doesn't have a requirement
  vector with that requirement vector number.
  @param enabler the action enabler that may own the vector.
  @param number the item's requirement vector number.
  @return a pointer to the specified requirement vector.
************************************************************************/
struct requirement_vector *
action_enabler_vector_by_number(const void *enabler,
                                req_vec_num_in_item number)
{
  struct action_enabler *ae = (struct action_enabler *)enabler;

  fc_assert_ret_val(number >= 0, nullptr);

  switch (number) {
  case 0:
    return &ae->actor_reqs;
  case 1:
    return &ae->target_reqs;
  default:
    return nullptr;
  }
}

/*********************************************************************//**
  Returns the name of the given requirement vector number n in an action
  enabler or nullptr if enablers don't have a requirement vector with that
  number.
  @param vec the requirement vector to name
  @return the requirement vector name or nullptr.
**************************************************************************/
const char *action_enabler_vector_by_number_name(req_vec_num_in_item vec)
{
  switch (vec) {
  case 0:
    /* TRANS: requirement vector in an action enabler (ruledit) */
    return _("actor_reqs");
  case 1:
    /* TRANS: requirement vector in an action enabler (ruledit) */
    return _("target_reqs");
  default:
    return nullptr;
  }
}

/**********************************************************************//**
  Returns the local building type of a city target.

  target_city can't be nullptr
**************************************************************************/
static const struct impr_type *
tgt_city_local_building(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, nullptr);

  if (target_city->production.kind == VUT_IMPROVEMENT) {
    /* The local building is what the target city currently is building. */
    return target_city->production.value.building;
  } else {
    /* In the current semantic the local building is always the building
     * being built. */
    /* TODO: Consider making the local building the target building for
     * actions that allows specifying a building target. */
    return nullptr;
  }
}

/**********************************************************************//**
  Returns the local unit type of a city target.

  target_city can't be nullptr
**************************************************************************/
static const struct unit_type *
tgt_city_local_utype(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, nullptr);

  if (target_city->production.kind == VUT_UTYPE) {
    /* The local unit type is what the target city currently is
     * building. */
    return target_city->production.value.utype;
  } else {
    /* In the current semantic the local utype is always the type of the
     * unit being built. */
    return nullptr;
  }
}

/**********************************************************************//**
  Returns the target tile for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target tile could therefore be missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_DISBAND_UNIT_RECOVER.
**************************************************************************/
static const struct tile *
blocked_find_target_tile(const struct action *act,
                         const struct unit *actor_unit,
                         const struct tile *target_tile_arg,
                         const struct city *target_city,
                         const struct unit *target_unit)
{
  if (target_tile_arg != nullptr) {
    /* Trust the caller. */
    return target_tile_arg;
  }

  /* Action should always be set */
  fc_assert_ret_val(act, nullptr);

  switch (action_get_target_kind(act)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city, nullptr);
    return city_tile(target_city);
  case ATK_UNIT:
    if (target_unit == nullptr) {
      fc_assert(target_unit != nullptr);
      return nullptr;
    }
    return unit_tile(target_unit);
  case ATK_STACK:
    fc_assert_ret_val(target_unit || target_tile_arg, nullptr);
    if (target_unit) {
      return unit_tile(target_unit);
    }

    fc__fallthrough;
  case ATK_TILE:
  case ATK_EXTRAS:
    fc_assert_ret_val(target_tile_arg, nullptr);
    return target_tile_arg;
  case ATK_SELF:
    if (actor_unit != nullptr) {
      return unit_tile(actor_unit);
    }
    return nullptr;
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %s",
                action_get_target_kind(act), action_rule_name(act));
  return nullptr;
}

/**********************************************************************//**
  Returns the target city for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target city argument could therefore be
  missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_DISBAND_UNIT_RECOVER.
**************************************************************************/
static const struct city *
blocked_find_target_city(const struct action *act,
                         const struct unit *actor_unit,
                         const struct tile *target_tile,
                         const struct city *target_city_arg,
                         const struct unit *target_unit)
{
  if (target_city_arg != nullptr) {
    /* Trust the caller. */
    return target_city_arg;
  }

  /* action should always be set */
  fc_assert_ret_val(act, nullptr);

  switch (action_get_target_kind(act)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city_arg, nullptr);
    return target_city_arg;
  case ATK_UNIT:
    fc_assert_ret_val(target_unit, nullptr);
    fc_assert_ret_val(unit_tile(target_unit), nullptr);
    return tile_city(unit_tile(target_unit));
  case ATK_STACK:
    fc_assert_ret_val(target_unit || target_tile, nullptr);
    if (target_unit) {
      fc_assert_ret_val(unit_tile(target_unit), nullptr);
      return tile_city(unit_tile(target_unit));
    }

    fc__fallthrough;
  case ATK_TILE:
  case ATK_EXTRAS:
    fc_assert_ret_val(target_tile, nullptr);
    return tile_city(target_tile);
  case ATK_SELF:
    if (actor_unit != nullptr) {
      struct tile *ptile = unit_tile(actor_unit);

      if (ptile != nullptr) {
        return tile_city(ptile);
      }
    }
    return nullptr;
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %s",
                action_get_target_kind(act), action_rule_name(act));
  return nullptr;
}

/**********************************************************************//**
  Returns the action that blocks the specified action or nullptr if the
  specified action isn't blocked.

  An action that can block another blocks when it is forced and possible.
**************************************************************************/
struct action *action_is_blocked_by(const struct civ_map *nmap,
                                    const struct action *act,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile_arg,
                                    const struct city *target_city_arg,
                                    const struct unit *target_unit)
{
  const struct tile *target_tile
      = blocked_find_target_tile(act, actor_unit, target_tile_arg,
                                 target_city_arg, target_unit);
  const struct city *target_city
      = blocked_find_target_city(act, actor_unit, target_tile,
                                 target_city_arg, target_unit);

  action_iterate(blocker_id) {
    struct action *blocker = action_by_number(blocker_id);

    if (action_get_actor_kind(blocker) != AAK_UNIT) {
      /* Currently, only unit's actions may block each other */
      continue;
    }

    if (!action_would_be_blocked_by(act, blocker)) {
      /* It doesn't matter if it is legal. It won't block the action. */
      continue;
    }

    switch (action_get_target_kind(blocker)) {
    case ATK_CITY:
      if (!target_city) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_city(nmap, blocker->id,
                                         actor_unit, target_city)) {
        return blocker;
      }
      break;
    case ATK_UNIT:
      if (!target_unit) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_unit(nmap, blocker->id,
                                         actor_unit, target_unit)) {
        return blocker;
      }
      break;
    case ATK_STACK:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_stack(nmap, blocker->id,
                                          actor_unit, target_tile)) {
        return blocker;
      }
      break;
    case ATK_TILE:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_tile(nmap, blocker->id,
                                         actor_unit, target_tile, nullptr)) {
        return blocker;
      }
      break;
    case ATK_EXTRAS:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_extras(nmap, blocker->id,
                                           actor_unit, target_tile, nullptr)) {
        return blocker;
      }
      break;
    case ATK_SELF:
      if (is_action_enabled_unit_on_self(nmap, blocker->id, actor_unit)) {
        return blocker;
      }
      break;
    case ATK_COUNT:
      fc_assert_action(action_id_get_target_kind(blocker->id) != ATK_COUNT,
                       continue);
      break;
    }
  } action_iterate_end;

  /* Not blocked. */
  return nullptr;
}

/**********************************************************************//**
  Returns TRUE if the specified unit type can perform the specified action
  given that an action enabler later will enable it.

  This is done by checking the action result's hard requirements. Hard
  requirements must be TRUE before an action can be done. The reason why
  is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.

  @param paction the action to check the hard reqs for
  @param actor_unittype the unit type that may be able to act
  @param ignore_third_party ignore if potential targets etc exists
  @return TRUE iff the specified unit type can perform the wanted action
          given that an action enabler later will enable it.
**************************************************************************/
static bool
action_actor_utype_hard_reqs_ok_full(const struct action *paction,
                                     const struct unit_type *actor_unittype,
                                     bool ignore_third_party)
{
  switch (paction->result) {
  case ACTRES_JOIN_CITY:
    if (actor_unittype->pop_cost <= 0 && !is_super_specialist(actor_unittype->spec_type)) {
      /* Reason: Must have something to add. */
      return FALSE;
    }
    break;

  case ACTRES_BOMBARD:
    if (actor_unittype->bombard_rate <= 0) {
      /* Reason: Can't bombard if it never fires. */
      return FALSE;
    }

    if (actor_unittype->attack_strength <= 0) {
      /* Reason: Can't bombard without attack strength. */
      return FALSE;
    }

    break;

  case ACTRES_UPGRADE_UNIT:
    if (actor_unittype->obsoleted_by == U_NOT_OBSOLETED) {
      /* Reason: Nothing to upgrade to. */
      return FALSE;
    }
    break;

  case ACTRES_ATTACK:
  case ACTRES_WIPE_UNITS:
  case ACTRES_COLLECT_RANSOM:
    if (actor_unittype->attack_strength <= 0) {
      /* Reason: Can't attack without strength. */
      return FALSE;
    }
    break;

  case ACTRES_CONVERT:
    if (!actor_unittype->converted_to) {
      /* Reason: must be able to convert to something. */
      return FALSE;
    }
    break;

  case ACTRES_TRANSPORT_UNLOAD:
    if (actor_unittype->transport_capacity < 1) {
      /* Reason: can't transport anything to unload. */
      return FALSE;
    }
    break;

  case ACTRES_TRANSPORT_LOAD:
    if (actor_unittype->transport_capacity < 1) {
      /* Reason: can't transport anything to load. */
      return FALSE;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
  case ACTRES_TRANSPORT_DEBOARD:
  case ACTRES_TRANSPORT_DISEMBARK:
    if (!ignore_third_party) {
      bool has_transporter = FALSE;

      unit_type_iterate(utrans) {
        if (can_unit_type_transport(utrans, utype_class(actor_unittype))) {
          has_transporter = TRUE;
          break;
        }
      } unit_type_iterate_end;

      if (!has_transporter) {
        /* Reason: no other unit can transport this unit. */
        return FALSE;
      }
    }
    break;

  case ACTRES_CONQUER_EXTRAS:
    if (!ignore_third_party) {
      bool has_target = FALSE;
      struct unit_class *pclass = utype_class(actor_unittype);

      /* Use cache when it has been initialized */
      if (pclass->cache.native_bases != nullptr) {
        /* Extra being native one is a hard requirement */
        extra_type_list_iterate(pclass->cache.native_bases, pextra) {
          if (!territory_claiming_base(pextra->data.base)) {
            /* Hard requirement */
            continue;
          }

          has_target = TRUE;
          break;
        } extra_type_list_iterate_end;
      } else {
        struct extra_type_list *terr_claimers = extra_type_list_of_terr_claimers();

        extra_type_list_iterate(terr_claimers, pextra) {
          if (!is_native_extra_to_uclass(pextra, pclass)) {
            /* Hard requirement */
            continue;
          }

          has_target = TRUE;
          break;
        } extra_type_list_iterate_end;
      }

      if (!has_target) {
        /* Reason: no extras can be conquered by this unit. */
        return FALSE;
      }
    }
    break;

  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    if (actor_unittype->paratroopers_range <= 0) {
      /* Reason: Can't paradrop 0 tiles. */
      return FALSE;
    }
    break;

  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_SPY_ESCAPE:
  case ACTRES_TRADE_ROUTE:
  case ACTRES_MARKETPLACE:
  case ACTRES_HELP_WONDER:
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_BRIBE_STACK:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_DISBAND_UNIT_RECOVER:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_HOMELESS:
  case ACTRES_AIRLIFT:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_HEAL_UNIT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN:
  case ACTRES_FORTIFY:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_ROAD:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_SPY_ATTACK:
  case ACTRES_UNIT_MOVE:
  case ACTRES_TELEPORT:
  case ACTRES_TELEPORT_CONQUER:
  case ACTRES_ENABLER_CHECK:
  case ACTRES_NONE:
    /* No hard unit type requirements. */
    break;

  ASSERT_UNUSED_ACTRES_CASES;
  }

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if the specified unit type can perform the specified action
  given that an action enabler later will enable it.

  This is done by checking the action result's hard requirements. Hard
  requirements must be TRUE before an action can be done. The reason why
  is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  @param paction the action to check the hard reqs for
  @param actor_unittype the unit type that may be able to act
  @return TRUE iff the specified unit type can perform the wanted action
          given that an action enabler later will enable it.
**************************************************************************/
bool
action_actor_utype_hard_reqs_ok(const struct action *paction,
                                const struct unit_type *actor_unittype)
{
  return action_actor_utype_hard_reqs_ok_full(paction,
                                              actor_unittype, FALSE);
}

/**********************************************************************//**
  Returns TRUE iff the wanted action is possible as far as the actor is
  concerned given that an action enabler later will enable it. Will, unlike
  action_actor_utype_hard_reqs_ok(), check the actor unit's current state.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.


  Passing nullptr for actor is equivalent to passing an empty context.
  This may or may not be legal depending on the action.
**************************************************************************/
static enum fc_tristate
action_hard_reqs_actor(const struct civ_map *nmap,
                       const struct action *paction,
                       const struct req_context *actor,
                       const bool omniscient,
                       const struct city *homecity)
{
  enum action_result result = paction->result;

  if (actor == nullptr) {
    actor = req_context_empty();
  }

  if (!action_actor_utype_hard_reqs_ok_full(paction, actor->unittype,
                                            TRUE)) {
    /* Info leak: The actor player knows the type of their unit. */
    /* The actor unit type can't perform the action because of hard
     * unit type requirements. */
    return TRI_NO;
  }

  switch (result) {
  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if their unit already has paradropped this
     * turn. */
    if (actor->unit->paradropped) {
      return TRI_NO;
    }

    break;

  case ACTRES_AIRLIFT:
    {
      /* Obligatory hard requirement. Checked here too since
       * action_hard_reqs_actor() may be called before any
       * action enablers are checked. */
      if (actor->city == nullptr) {
        /* No city to airlift from. */
        return TRI_NO;
      }

      if (actor->player != city_owner(actor->city)
          && !(game.info.airlifting_style & AIRLIFTING_ALLIED_SRC
               && pplayers_allied(actor->player,
                                  city_owner(actor->city)))) {
        /* Not allowed to airlift from this source. */
        return TRI_NO;
      }

      if (!(omniscient || city_owner(actor->city) == actor->player)) {
        /* Can't check for airlifting capacity. */
        return TRI_MAYBE;
      }

      if (0 >= actor->city->airlift
          && (!(game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)
              || !game.info.airlift_from_always_enabled)) {
        /* The source cannot airlift for this turn (maybe already airlifted
         * or no airport).
         *
         * See also do_airline() in server/unittools.h. */
        return TRI_NO;
      }
    }
    break;

  case ACTRES_CONVERT:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows their unit's cargo and location. */
    if (!unit_can_convert(nmap, actor->unit)) {
      return TRI_NO;
    }
    break;

  case ACTRES_TRANSPORT_BOARD:
  case ACTRES_TRANSPORT_EMBARK:
    if (unit_transported(actor->unit)) {
      if (!can_unit_unload(actor->unit, unit_transport_get(actor->unit))) {
        /* Can't leave current transport. */
        return TRI_NO;
      }
    }
    break;

  case ACTRES_TRANSPORT_DISEMBARK:
    if (!can_unit_unload(actor->unit, unit_transport_get(actor->unit))) {
      /* Keep the old rules about Unreachable and disembarks. */
      return TRI_NO;
    }
    break;

  case ACTRES_HOMELESS:
  case ACTRES_UNIT_MOVE:
  case ACTRES_TELEPORT:
  case ACTRES_TELEPORT_CONQUER:
  case ACTRES_ESTABLISH_EMBASSY:
  case ACTRES_SPY_INVESTIGATE_CITY:
  case ACTRES_SPY_POISON:
  case ACTRES_SPY_STEAL_GOLD:
  case ACTRES_SPY_SPREAD_PLAGUE:
  case ACTRES_SPY_SABOTAGE_CITY:
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTRES_SPY_STEAL_TECH:
  case ACTRES_SPY_TARGETED_STEAL_TECH:
  case ACTRES_SPY_INCITE_CITY:
  case ACTRES_SPY_ESCAPE:
  case ACTRES_TRADE_ROUTE:
  case ACTRES_MARKETPLACE:
  case ACTRES_HELP_WONDER:
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_BRIBE_STACK:
  case ACTRES_SPY_SABOTAGE_UNIT:
  case ACTRES_CAPTURE_UNITS:
  case ACTRES_FOUND_CITY:
  case ACTRES_JOIN_CITY:
  case ACTRES_STEAL_MAPS:
  case ACTRES_BOMBARD:
  case ACTRES_SPY_NUKE:
  case ACTRES_NUKE:
  case ACTRES_NUKE_UNITS:
  case ACTRES_DESTROY_CITY:
  case ACTRES_EXPEL_UNIT:
  case ACTRES_DISBAND_UNIT_RECOVER:
  case ACTRES_DISBAND_UNIT:
  case ACTRES_HOME_CITY:
  case ACTRES_UPGRADE_UNIT:
  case ACTRES_ATTACK:
  case ACTRES_WIPE_UNITS:
  case ACTRES_COLLECT_RANSOM:
  case ACTRES_STRIKE_BUILDING:
  case ACTRES_STRIKE_PRODUCTION:
  case ACTRES_CONQUER_CITY:
  case ACTRES_CONQUER_EXTRAS:
  case ACTRES_HEAL_UNIT:
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN:
  case ACTRES_FORTIFY:
  case ACTRES_ROAD:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
  case ACTRES_TRANSPORT_DEBOARD:
  case ACTRES_TRANSPORT_UNLOAD:
  case ACTRES_TRANSPORT_LOAD:
  case ACTRES_SPY_ATTACK:
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
  case ACTRES_ENABLER_CHECK:
  case ACTRES_NONE:
    /* No hard unit requirements. */
    break;

  ASSERT_UNUSED_ACTRES_CASES;
  }

  return TRI_YES;
}

/**********************************************************************//**
  Returns if the wanted action is possible given that an action enabler
  later will enable it.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.

  This is done by checking the action's hard requirements.
  Hard requirements must be fulfilled before an action can be done.
  The reason why is usually that code dealing with the action assumes that
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.
   * remember that this is called from action_prob(). Should information
     the player don't have access to be used in a test it must check if
     the evaluation can see the thing being tested.

  Passing nullptr for actor or target is equivalent to passing an empty
  context. This may or may not be legal depending on the action.
**************************************************************************/
static enum fc_tristate
is_action_possible(const struct civ_map *nmap,
                   const action_id wanted_action,
                   const struct req_context *actor,
                   const struct req_context *target,
                   const struct extra_type *target_extra,
                   const bool omniscient,
                   const struct city *homecity)
{
  enum fc_tristate out;
  struct action *paction = action_by_number(wanted_action);
  enum action_target_kind tkind = action_get_target_kind(paction);
  const struct tile *atile = nullptr, *ttile = nullptr;

  if (actor == nullptr) {
    actor = req_context_empty();
  } else {
    atile = actor->tile;
  }
  if (target == nullptr) {
    target = req_context_empty();
  } else {
    ttile = target->tile;
  }

  fc_assert_msg((tkind == ATK_CITY && target->city != nullptr)
                || (tkind == ATK_TILE && ttile != nullptr)
                || (tkind == ATK_EXTRAS && ttile != nullptr)
                || (tkind == ATK_UNIT && target->unit != nullptr)
                /* At this level each individual unit is tested. */
                || (tkind == ATK_STACK && target->unit != nullptr)
                || (tkind == ATK_SELF),
                "Missing target!");

  /* Info leak: The player knows where their unit/city is. */
  if (nullptr != atile && nullptr != ttile
      && !action_distance_accepted(paction,
                                   real_map_distance(atile, ttile))) {
    /* The distance between the actor and the target isn't inside the
     * action's accepted range. */
    return TRI_NO;
  }

  switch (tkind) {
  case ATK_UNIT:
    /* The Freeciv code for all actions that is controlled by action
     * enablers and targets a unit assumes that the acting
     * player can see the target unit.
     * Examples:
     * - action_prob_vs_unit()'s quick check that the distance between actor
     *   and target is acceptable would leak distance to target unit if the
     *   target unit can't be seen.
     */
    if (!can_player_see_unit(actor->player, target->unit)) {
      return TRI_NO;
    }
    break;
  case ATK_CITY:
    /* The Freeciv code assumes that the player is aware of the target
     * city's existence. (How can you order an airlift to a city when you
     * don't know its city ID?) */
    if (fc_funcs->player_tile_city_id_get(city_tile(target->city),
                                          actor->player)
        != target->city->id) {
      return TRI_NO;
    }
    break;
  case ATK_STACK:
  case ATK_TILE:
  case ATK_EXTRAS:
  case ATK_SELF:
    /* No special player knowledge checks. */
    break;
  case ATK_COUNT:
    fc_assert(tkind != ATK_COUNT);
    break;
  }

  if (action_is_blocked_by(nmap, paction, actor->unit,
                           ttile, target->city, target->unit)) {
    /* Allows an action to block an other action. If a blocking action is
     * legal the actions it blocks becomes illegal. */
    return TRI_NO;
  }

  /* Actor specific hard requirements. */
  out = action_hard_reqs_actor(nmap, paction, actor, omniscient, homecity);

  if (out == TRI_NO) {
    /* Illegal because of a hard actor requirement. */
    return TRI_NO;
  }

  /* Quick checks for action itself */
  if (paction->result == ACTRES_ATTACK
      || paction->result == ACTRES_WIPE_UNITS
      || paction->result == ACTRES_COLLECT_RANSOM) {
    /* Reason: Keep the old rules. */
    if (!can_unit_attack_tile(actor->unit, paction, ttile)) {
      return TRI_NO;
    }
  }

  /* Hard requirements for results. */
  out = actres_possible(nmap,
                        paction->result, actor,
                        target, target_extra, out, omniscient,
                        homecity);

  if (out == TRI_NO) {
    /* Illegal because of a hard actor requirement. */
    return TRI_NO;
  }

  if (paction->result == ACTRES_NUKE_UNITS) {
    if (unit_attack_units_at_tile_result(actor->unit, paction, ttile)
        != ATT_OK) {
      /* Unreachable. */
      return TRI_NO;
    }
  } else if (paction->result == ACTRES_PARADROP
             || paction->result == ACTRES_PARADROP_CONQUER) {
    if (can_player_see_tile(actor->player, ttile)) {
      /* Check for seen stuff that may kill the actor unit. */

      /* Reason: Keep the old rules. Be merciful. */
      /* Info leak: The player sees the target tile. */
      if (!can_unit_exist_at_tile(nmap, actor->unit, ttile)
          && (!BV_ISSET(paction->sub_results, ACT_SUB_RES_MAY_EMBARK)
              || !unit_could_load_at(actor->unit, ttile))) {
        return TRI_NO;
      }

      /* Reason: Keep the old rules. Be merciful. */
      /* Info leak: The player sees the target tile. */
      if (is_non_attack_city_tile(ttile, actor->player)) {
        return TRI_NO;
      }

      /* Reason: Be merciful. */
      /* Info leak: The player sees all units checked. Invisible units are
       * ignored. */
      unit_list_iterate(ttile->units, pother) {
        if (can_player_see_unit(actor->player, pother)
            && !pplayers_allied(actor->player, unit_owner(pother))) {
          return TRI_NO;
        }
      } unit_list_iterate_end;
    }
  }

  return out;
}

/**********************************************************************//**
  Return TRUE iff the action enabler is active

  actor may be nullptr. This is equivalent to passing an empty context.
  target may be nullptr. This is equivalent to passing an empty context.
**************************************************************************/
static bool is_enabler_active(const struct action_enabler *enabler,
                              const struct req_context *actor,
                              const struct req_context *target)
{
  return are_reqs_active(actor, target, &enabler->actor_reqs, RPT_CERTAIN)
      && are_reqs_active(target, actor, &enabler->target_reqs, RPT_CERTAIN);
}

/**********************************************************************//**
  Returns TRUE if the wanted action is enabled.

  Note that the action may disable it self because of hard requirements
  even if an action enabler returns TRUE.

  Passing nullptr for actor or target is equivalent to passing an empty
  context. This may or may not be legal depending on the action.
**************************************************************************/
static bool is_action_enabled(const struct civ_map *nmap,
                              const action_id wanted_action,
                              const struct req_context *actor,
                              const struct req_context *target,
                              const struct extra_type *target_extra,
                              const struct city *actor_home)
{
  enum fc_tristate possible;

  possible = is_action_possible(nmap, wanted_action, actor, target, target_extra,
                                TRUE, actor_home);

  if (possible != TRI_YES) {
    /* This context is omniscient. Should be yes or no. */
    fc_assert_msg(possible != TRI_MAYBE,
                  "Is omniscient, should get yes or no.");

    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return FALSE;
  }

  return action_enablers_allow(wanted_action, actor, target);
}

/**********************************************************************//**
  Returns whether action enablers would allow action,
  assuming hard requirements do.
**************************************************************************/
bool action_enablers_allow(const action_id wanted_action,
                           const struct req_context *actor,
                           const struct req_context *target)
{
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (is_enabler_active(enabler, actor, target)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_city as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_city_full(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct city *target_city)
{
  const struct impr_type *target_building;
  const struct unit_type *target_utype;

  if (actor_unit == nullptr || target_city == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = unit_owner(actor_unit),
                             .city = tile_city(actor_tile),
                             .tile = actor_tile,
                             .unit = actor_unit,
                             .unittype = unit_type_get(actor_unit),
                           },
                           &(const struct req_context) {
                             .player = city_owner(target_city),
                             .city = target_city,
                             .building = target_building,
                             .tile = city_tile(target_city),
                             .unittype = target_utype,
                           },
                           nullptr,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_city as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_city(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city)
{
  return is_action_enabled_unit_on_city_full(nmap, wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_city);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_unit_full(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct unit *target_unit)
{
  if (actor_unit == nullptr || target_unit == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_UNIT));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = unit_owner(actor_unit),
                             .city = tile_city(actor_tile),
                             .tile = actor_tile,
                             .unit = actor_unit,
                             .unittype = unit_type_get(actor_unit),
                           },
                           &(const struct req_context) {
                             .player = unit_owner(target_unit),
                             .city = tile_city(unit_tile(target_unit)),
                             .tile = unit_tile(target_unit),
                             .unit = target_unit,
                             .unittype = unit_type_get(target_unit),
                           },
                           nullptr,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_unit(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit)
{
  return is_action_enabled_unit_on_unit_full(nmap, wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_unit);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to all units on the
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_stack_full(const struct civ_map *nmap,
                                     const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct city *actor_home,
                                     const struct tile *actor_tile,
                                     const struct tile *target_tile)
{
  const struct req_context *actor_ctxt;

  if (actor_unit == nullptr || target_tile == nullptr
      || unit_list_size(target_tile->units) == 0) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_STACK
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_STACK));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  actor_ctxt = &(const struct req_context) {
    .player = unit_owner(actor_unit),
    .city = tile_city(actor_tile),
    .tile = actor_tile,
    .unit = actor_unit,
    .unittype = unit_type_get(actor_unit),
  };

  unit_list_iterate(target_tile->units, target_unit) {
    if (!is_action_enabled(nmap, wanted_action, actor_ctxt,
                           &(const struct req_context) {
                             .player = unit_owner(target_unit),
                             .city = tile_city(unit_tile(target_unit)),
                             .tile = unit_tile(target_unit),
                             .unit = target_unit,
                             .unittype = unit_type_get(target_unit),
                           },
                           nullptr, actor_home)) {
      /* One unit makes it impossible for all units. */
      return FALSE;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to all units on the
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_stack(const struct civ_map *nmap,
                                     const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct tile *target_tile)
{
  return is_action_enabled_unit_on_stack_full(nmap, wanted_action, actor_unit,
                                              unit_home(actor_unit),
                                              unit_tile(actor_unit),
                                              target_tile);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_tile_full(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  if (actor_unit == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_TILE));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = unit_owner(actor_unit),
                             .city = tile_city(actor_tile),
                             .tile = actor_tile,
                             .unit = actor_unit,
                             .unittype = unit_type_get(actor_unit),
                           },
                           &(const struct req_context) {
                             .player = tile_owner(target_tile),
                             .city = tile_city(target_tile),
                             .tile = target_tile,
                           },
                           target_extra,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_tile(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  return is_action_enabled_unit_on_tile_full(nmap, wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_tile, target_extra);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the extras at
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_extras_full(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct unit *actor_unit,
                                      const struct city *actor_home,
                                      const struct tile *actor_tile,
                                      const struct tile *target_tile,
                                      const struct extra_type *target_extra)
{
  if (actor_unit == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_EXTRAS
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_EXTRAS));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = unit_owner(actor_unit),
                             .city = tile_city(actor_tile),
                             .tile = actor_tile,
                             .unit = actor_unit,
                             .unittype = unit_type_get(actor_unit),
                           },
                           &(const struct req_context) {
                             .player = target_tile->extras_owner,
                             .city = tile_city(target_tile),
                             .tile = target_tile,
                           },
                           target_extra,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to the extras at
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_extras(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct unit *actor_unit,
                                      const struct tile *target_tile,
                                      const struct extra_type *target_extra)
{
  return is_action_enabled_unit_on_extras_full(nmap, wanted_action, actor_unit,
                                               unit_home(actor_unit),
                                               unit_tile(actor_unit),
                                               target_tile, target_extra);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to itself as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
**************************************************************************/
static bool
is_action_enabled_unit_on_self_full(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile)
{
  if (actor_unit == nullptr) {
    /* Can't do an action when the actor is missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        action_id_rule_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        action_id_rule_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_SELF));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = unit_owner(actor_unit),
                             .city = tile_city(actor_tile),
                             .tile = actor_tile,
                             .unit = actor_unit,
                             .unittype = unit_type_get(actor_unit),
                           },
                           nullptr, nullptr,
                           actor_home);
}

/**********************************************************************//**
  Returns TRUE if actor_unit can do wanted_action to itself as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
**************************************************************************/
bool is_action_enabled_unit_on_self(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit)
{
  return is_action_enabled_unit_on_self_full(nmap, wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit));
}

#define ASSERT_PLAYER_ACTION(_atk)                                              \
    fc_assert_ret_val_msg(AAK_PLAYER == action_id_get_actor_kind(wanted_action),\
                          FALSE, "Action %s is performed by %s not %s",         \
                          action_id_rule_name(wanted_action),                   \
                          action_actor_kind_name(                               \
                            action_id_get_actor_kind(wanted_action)),           \
                          action_actor_kind_name(AAK_PLAYER));                  \
    fc_assert_ret_val_msg(_atk                                                  \
                          == action_id_get_target_kind(wanted_action),          \
                          FALSE, "Action %s is against %s not %s",              \
                          action_id_rule_name(wanted_action),                   \
                          action_target_kind_name(                              \
                            action_id_get_target_kind(wanted_action)),          \
                          action_target_kind_name(_atk));

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to themself as far as
  action enablers are concerned.
**************************************************************************/
bool is_action_enabled_player_on_self(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr)
{
  if (actor_plr == nullptr) {
    /* Can't do an action when the actor is missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_SELF);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = actor_plr,
                           },
                           nullptr, nullptr, nullptr);
}

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to target_city as far as
  action enablers are concerned.
**************************************************************************/
bool is_action_enabled_player_on_city(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct city *target_city)
{
  const struct impr_type *target_building;
  const struct unit_type *target_utype;

  if (actor_plr == nullptr || target_city == nullptr) {
    /* Can't do an action when the actor or the target is missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_CITY);

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = actor_plr,
                           },
                           &(const struct req_context) {
                             .player = city_owner(target_city),
                             .city = target_city,
                             .building = target_building,
                             .tile = city_tile(target_city),
                             .unittype = target_utype,
                           }, nullptr, nullptr);
}

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to the extras at
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool
is_action_enabled_player_on_extras(const struct civ_map *nmap,
                                   const action_id wanted_action,
                                   const struct player *actor_plr,
                                   const struct tile *target_tile,
                                   const struct extra_type *target_extra)
{
  if (actor_plr == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_EXTRAS);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = actor_plr,
                           },
                           &(const struct req_context) {
                             .player = target_tile->extras_owner,
                             .city = tile_city(target_tile),
                             .tile = target_tile,
                           },
                           target_extra, nullptr);
}

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to all units at
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_player_on_stack(const struct civ_map *nmap,
                                       const action_id wanted_action,
                                       const struct player *actor_plr,
                                       const struct tile *target_tile,
                                       const struct extra_type *target_extra)
{
  const struct req_context actor_ctxt = {
    .player = actor_plr,
  };
  const struct city *tcity;

  if (actor_plr == nullptr  || target_tile == nullptr
      || unit_list_size(target_tile->units) == 0) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_STACK);

  tcity = tile_city(target_tile);
  unit_list_iterate(target_tile->units, target_unit) {
    if (!is_action_enabled(nmap, wanted_action, &actor_ctxt,
                           &(const struct req_context) {
                             .player = unit_owner(target_unit),
                             .city = tcity,
                             .tile = target_tile,
                             .unit = target_unit,
                             .unittype = unit_type_get(target_unit),
                           },
                           nullptr, nullptr)) {
      /* One unit makes it impossible for all units. */
      return FALSE;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_player_on_tile(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct tile *target_tile,
                                      const struct extra_type *target_extra)
{
  if (actor_plr == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_TILE);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = actor_plr,
                           },
                           &(const struct req_context) {
                             .player = tile_owner(target_tile),
                             .city = tile_city(target_tile),
                             .tile = target_tile,
                           },
                           target_extra, nullptr);
}

/**********************************************************************//**
  Returns TRUE if actor_plr can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_player_on_unit(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct unit *target_unit)
{
  if (actor_plr == nullptr || target_unit == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  ASSERT_PLAYER_ACTION(ATK_UNIT);

  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = actor_plr,
                           },
                           &(const struct req_context) {
                             .player = unit_owner(target_unit),
                             .city = tile_city(unit_tile(target_unit)),
                             .tile = unit_tile(target_unit),
                             .unit = target_unit,
                             .unittype = unit_type_get(target_unit),
                           },
                           nullptr, nullptr);
}

/**********************************************************************//**
  Returns TRUE if actor_city can do wanted_action as far as
  action enablers are concerned.
**************************************************************************/
bool is_action_enabled_city(const struct civ_map *nmap,
                            const action_id wanted_action,
                            const struct city *actor_city)
{
  return is_action_enabled(nmap, wanted_action,
                           &(const struct req_context) {
                             .player = city_owner(actor_city),
                             .city = actor_city,
                             .tile = city_tile(actor_city)
                           },
                           nullptr, nullptr, nullptr);
}

/**********************************************************************//**
  Find out if the action is enabled, may be enabled or isn't enabled given
  what the player owning the actor knowns.

  A player don't always know everything needed to figure out if an action
  is enabled or not. A server side AI with the same limits on its knowledge
  as a human player or a client should use this to figure out what is what.

  Assumes to be called from the point of view of the actor. Its knowledge
  is assumed to be given in the parameters.

  Returns TRI_YES if the action is enabled, TRI_NO if it isn't and
  TRI_MAYBE if the player don't know enough to tell.

  If meta knowledge is missing TRI_MAYBE will be returned.

  target may be nullptr. This is equivalent to passing an empty context.
**************************************************************************/
static enum fc_tristate
action_enabled_local(const action_id wanted_action,
                     const struct req_context *actor,
                     const struct req_context *target)
{
  enum fc_tristate current;
  enum fc_tristate result;

  if (actor == nullptr || actor->player == nullptr) {
    /* Need actor->player for point of view */
    return TRI_MAYBE;
  }

  if (target == nullptr) {
    target = req_context_empty();
  }

  result = TRI_NO;
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    current = fc_tristate_and(mke_eval_reqs(actor->player,
                                            actor, target,
                                            &enabler->actor_reqs,
                                            RPT_CERTAIN),
                              mke_eval_reqs(actor->player,
                                            target, actor,
                                            &enabler->target_reqs,
                                            RPT_CERTAIN));
    if (current == TRI_YES) {
      return TRI_YES;
    } else if (current == TRI_MAYBE) {
      result = TRI_MAYBE;
    }
  } action_enabler_list_iterate_end;

  return result;
}

/**********************************************************************//**
  Find out if the effect value is known

  The knowledge of the actor is assumed to be given in the parameters.

  If meta knowledge is missing TRI_MAYBE will be returned.

  context and other_context may be nullptr. This is equivalent to passing
  empty contexts.
**************************************************************************/
static bool is_effect_val_known(enum effect_type effect_type,
                                const struct player *pov_player,
                                const struct req_context *context,
                                const struct req_context *other_context)
{
  effect_list_iterate(get_effects(effect_type), peffect) {
    if (TRI_MAYBE == mke_eval_reqs(pov_player, context, other_context,
                                   &(peffect->reqs), RPT_CERTAIN)) {
      return FALSE;
    }
  } effect_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Does the target has any techs the actor don't?
**************************************************************************/
static enum fc_tristate
tech_can_be_stolen(const struct player *actor_player,
                   const struct player *target_player)
{
  const struct research *actor_research = research_get(actor_player);
  const struct research *target_research = research_get(target_player);

  if (actor_research != target_research) {
    if (can_see_techs_of_target(actor_player, target_player)) {
      advance_iterate(padvance) {
        Tech_type_id i = advance_number(padvance);

        if (research_invention_state(target_research, i) == TECH_KNOWN
            && research_invention_gettable(actor_research, i,
                                           game.info.tech_steal_allow_holes)
            && (research_invention_state(actor_research, i) == TECH_UNKNOWN
                || (research_invention_state(actor_research, i)
                    == TECH_PREREQS_KNOWN))) {
          return TRI_YES;
        }
      } advance_iterate_end;
    } else {
      return TRI_MAYBE;
    }
  }

  return TRI_NO;
}

/**********************************************************************//**
  The action probability that pattacker will win a diplomatic battle.

  It is assumed that pattacker and pdefender have different owners and that
  the defender can defend in a diplomatic battle.

  See diplomat_success_vs_defender() in server/diplomats.c
**************************************************************************/
static struct act_prob ap_dipl_battle_win(const struct unit *pattacker,
                                          const struct unit *pdefender)
{
  /* Keep unconverted until the end to avoid scaling each step */
  int chance;
  struct act_prob out;

  /* Superspy always win */
  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    /* A defending UTYF_SUPERSPY will defeat every possible attacker. */
    return ACTPROB_IMPOSSIBLE;
  }
  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    /* An attacking UTYF_SUPERSPY will defeat every possible defender
     * except another UTYF_SUPERSPY. */
    return ACTPROB_CERTAIN;
  }

  /* Base chance is 50% */
  chance = 50;

  /* Spy attack bonus */
  if (unit_has_type_flag(pattacker, UTYF_SPY)) {
    chance += 25;
  }

  /* Spy defense bonus */
  if (unit_has_type_flag(pdefender, UTYF_SPY)) {
    chance -= 25;
  }

  /* Veteran attack and defense bonus */
  {
    const struct veteran_level *vatt
      = utype_veteran_level(unit_type_get(pattacker), pattacker->veteran);
    const struct veteran_level *vdef
      = utype_veteran_level(unit_type_get(pdefender), pdefender->veteran);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* Defense bonus. */
  {
    const struct req_context defender_ctxt = {
      .player = tile_owner(pdefender->tile),
      .city = tile_city(pdefender->tile),
      .tile = pdefender->tile,
    };
    if (!is_effect_val_known(EFT_SPY_RESISTANT, unit_owner(pattacker),
                             &defender_ctxt,
                             nullptr)) {
      return ACTPROB_NOT_KNOWN;
    }

    /* Reduce the chance of an attack by EFT_SPY_RESISTANT percent. */
    chance -= chance * get_target_bonus_effects(
                         nullptr,
                         &defender_ctxt,
                         nullptr,
                         EFT_SPY_RESISTANT
                       ) / 100;
  }

  chance = CLIP(0, chance, 100);

  /* Convert to action probability */
  out.min = chance * ACTPROB_VAL_1_PCT;
  out.max = chance * ACTPROB_VAL_1_PCT;

  return out;
}

/**********************************************************************//**
  The action probability that pattacker will win a diplomatic battle.

  See diplomat_infiltrate_tile() in server/diplomats.c
**************************************************************************/
static struct act_prob ap_diplomat_battle(const struct unit *pattacker,
                                          const struct unit *pvictim,
                                          const struct tile *tgt_tile,
                                          const struct action *paction)
{
  struct unit *pdefender;

  if (!can_player_see_hypotetic_units_at(unit_owner(pattacker),
                                         tgt_tile)) {
    /* Don't leak information about unseen defenders. */
    return ACTPROB_NOT_KNOWN;
  }

  pdefender = get_diplomatic_defender(pattacker, pvictim, tgt_tile,
                                      paction);

  if (pdefender) {
    /* There will be a diplomatic battle instead of an action. */
    return ap_dipl_battle_win(pattacker, pdefender);
  };

  /* No diplomatic battle will occur. */
  return ACTPROB_CERTAIN;
}

/**********************************************************************//**
  Returns the action probability for when a target is unseen.
**************************************************************************/
static struct act_prob act_prob_unseen_target(const struct civ_map *nmap,
                                              action_id act_id,
                                              const struct unit *actor_unit)
{
  if (action_maybe_possible_actor_unit(nmap, act_id, actor_unit)) {
    /* Unknown because the target is unseen. */
    return ACTPROB_NOT_KNOWN;
  } else {
    /* The actor it self can't do this. */
    return ACTPROB_IMPOSSIBLE;
  }
}

/**********************************************************************//**
  Returns the action probability of an action not failing its dice roll
  without leaking information.
**************************************************************************/
static struct act_prob
action_prob_pre_action_dice_roll(const struct player *act_player,
                                 const struct unit *act_unit,
                                 const struct city *tgt_city,
                                 const struct player *tgt_player,
                                 const struct action *paction)
{
  if (is_effect_val_known(EFT_ACTION_ODDS_PCT, act_player,
                          &(const struct req_context) {
                            .player = act_player,
                            .city = tgt_city,
                            .unit = act_unit,
                            .unittype = unit_type_get(act_unit),
                          },
                          &(const struct req_context) {
                            .player = tgt_player,
                          })
      && is_effect_val_known(EFT_ACTION_RESIST_PCT, act_player,
                             &(const struct req_context) {
                               .player = tgt_player,
                               .city = tgt_city,
                               .unit = act_unit,
                             },
                             &(const struct req_context) {
                              .player = act_player,
                             })) {
    int unconverted = action_dice_roll_odds(act_player, act_unit, tgt_city,
                                            tgt_player, paction);
    struct act_prob result = { .min = unconverted * ACTPROB_VAL_1_PCT,
                               .max = unconverted * ACTPROB_VAL_1_PCT };

    return result;
  } else {
    /* Could be improved to return a more exact probability in some cases.
     * Example: The player has enough information to know that the
     * probability always will be above 25% and always under 75% because
     * the only effect with unknown requirements that may apply adds (or
     * subtracts) 50% while all the requirements of the other effects that
     * may apply are known. */
    return ACTPROB_NOT_KNOWN;
  }
}

/**********************************************************************//**
  Returns the action probability of an action winning a potential pre
  action battle - like a diplomatic battle - and then not failing its dice
  roll. Shouldn't leak information.
**************************************************************************/
static struct act_prob
action_prob_battle_then_dice_roll(const struct player *act_player,
                                  const struct unit *act_unit,
                                  const struct city *tgt_city,
                                  const struct unit *tgt_unit,
                                  const struct tile *tgt_tile,
                                  const struct player *tgt_player,
                                  const struct action *paction)
{
  struct act_prob battle;
  struct act_prob dice_roll;

  battle = ACTPROB_CERTAIN;
  switch (actres_get_battle_kind(paction->result)) {
  case ABK_NONE:
    /* No pre action battle. */
    break;
  case ABK_DIPLOMATIC:
    battle = ap_diplomat_battle(act_unit, tgt_unit, tgt_tile,
                                paction);
    break;
  case ABK_STANDARD:
    /* Not supported here yet. Implement when users appear. */
    fc_assert(actres_get_battle_kind(paction->result) != ABK_STANDARD);
    break;
  case ABK_COUNT:
    fc_assert(actres_get_battle_kind(paction->result) != ABK_COUNT);
    break;
  }

  dice_roll = action_prob_pre_action_dice_roll(act_player, act_unit,
                                               tgt_city, tgt_player,
                                               paction);

  return action_prob_and(&battle, &dice_roll);
}

/**********************************************************************//**
  An action's probability of success.

  "Success" indicates that the action achieves its goal, not that the
  actor survives. For actions that cost money it is assumed that the
  player has and is willing to spend the money. This is so the player can
  figure out what their odds are before deciding to get the extra money.

  Passing nullptr for actor or target is equivalent to passing an empty
  context. This may or may not be legal depending on the action.
**************************************************************************/
static struct act_prob
action_prob(const struct civ_map *nmap,
            const action_id wanted_action,
            const struct req_context *actor,
            const struct city *actor_home,
            const struct req_context *target,
            const struct extra_type *target_extra)
{
  enum fc_tristate known;
  struct act_prob chance;
  const struct action *paction = action_by_number(wanted_action);

  if (actor == nullptr) {
    actor = req_context_empty();
  }
  if (target == nullptr) {
    target = req_context_empty();
  }

  known = is_action_possible(nmap, wanted_action, actor, target,
                             target_extra,
                             FALSE, actor_home);

  if (known == TRI_NO) {
    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return ACTPROB_IMPOSSIBLE;
  }

  chance = ACTPROB_NOT_IMPLEMENTED;

  known = fc_tristate_and(known,
                          action_enabled_local(wanted_action,
                                               actor, target));

  switch (paction->result) {
  case ACTRES_SPY_POISON:
    /* All uncertainty comes from potential diplomatic battles and the
     * (diplchance server setting and the) Action_Odds_Pct effect controlled
     * dice roll before the action. */
    chance = action_prob_battle_then_dice_roll(actor->player, actor->unit,
                                               target->city, target->unit,
                                               target->tile, target->player,
                                               paction);
    break;
  case ACTRES_SPY_STEAL_GOLD:
    /* TODO */
    break;
  case ACTRES_SPY_SPREAD_PLAGUE:
    /* TODO */
    break;
  case ACTRES_STEAL_MAPS:
    /* TODO */
    break;
  case ACTRES_SPY_SABOTAGE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor->unit, target->unit, target->tile,
                                paction);
    break;
  case ACTRES_SPY_BRIBE_UNIT:
  case ACTRES_SPY_BRIBE_STACK:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor->unit, target->unit, target->tile,
                                paction);
    break;
  case ACTRES_SPY_ATTACK:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor->unit, nullptr, target->tile,
                                paction);
    break;
  case ACTRES_SPY_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTRES_SPY_TARGETED_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:
    /* TODO */
    break;
  case ACTRES_SPY_INCITE_CITY:
    /* TODO */
    break;
  case ACTRES_ESTABLISH_EMBASSY:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_SPY_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor->player,
                                               target->player));

    /* TODO: Calculate actual chance */

    break;
  case ACTRES_SPY_TARGETED_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor->player,
                                               target->player));

    /* TODO: Calculate actual chance */

    break;
  case ACTRES_SPY_INVESTIGATE_CITY:
    /* There is no risk that the city won't get investigated. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_SPY_ESCAPE:
    /* TODO */
    break;
  case ACTRES_TRADE_ROUTE:
    /* TODO */
    break;
  case ACTRES_MARKETPLACE:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HELP_WONDER:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_CAPTURE_UNITS:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_EXPEL_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_BOMBARD:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_FOUND_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_JOIN_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_SPY_NUKE:
    /* All uncertainty comes from potential diplomatic battles and the
     * (diplchance server setting and the) Action_Odds_Pct effect controlled
     * dice roll before the action. */
    chance = action_prob_battle_then_dice_roll(actor->player, actor->unit,
                                               target->city, target->unit,
                                               target->tile,
                                               target->player,
                                               paction);
    break;
  case ACTRES_NUKE:
    /* TODO */
    break;
  case ACTRES_NUKE_UNITS:
    /* TODO */
    break;
  case ACTRES_DESTROY_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_DISBAND_UNIT_RECOVER:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_DISBAND_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HOME_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HOMELESS:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_UPGRADE_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_PARADROP:
  case ACTRES_PARADROP_CONQUER:
    /* TODO */
    break;
  case ACTRES_AIRLIFT:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_ATTACK:
  case ACTRES_COLLECT_RANSOM:
    {
      struct unit *defender_unit = get_defender(nmap, actor->unit,
                                                target->tile, paction);

      if (can_player_see_unit(actor->player, defender_unit)) {
        double unconverted = unit_win_chance(nmap, actor->unit,
                                             defender_unit, paction);

        chance.min = MAX(ACTPROB_VAL_MIN,
                         floor((double)ACTPROB_VAL_MAX * unconverted));
        chance.max = MIN(ACTPROB_VAL_MAX,
                         ceil((double)ACTPROB_VAL_MAX * unconverted));
      } else if (known == TRI_YES) {
        known = TRI_MAYBE;
      }
    }
    break;
  case ACTRES_WIPE_UNITS:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_STRIKE_BUILDING:
    /* TODO: not implemented yet because:
     * - dice roll 100% * Action_Odds_Pct could be handled with
     *   action_prob_pre_action_dice_roll().
     * - sub target building may be missing. May be missing without player
     *   knowledge if it isn't visible. See is_improvement_visible() and
     *   can_player_see_city_internals(). */
    break;
  case ACTRES_STRIKE_PRODUCTION:
    /* All uncertainty comes from the (diplchance server setting and the)
     * Action_Odds_Pct effect controlled dice roll before the action. */
    chance = action_prob_pre_action_dice_roll(actor->player, actor->unit,
                                              target->city, target->player,
                                              paction);
    break;
  case ACTRES_CONQUER_CITY:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_CONQUER_EXTRAS:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HEAL_UNIT:
    /* No battle is fought first. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSFORM_TERRAIN:
  case ACTRES_CULTIVATE:
  case ACTRES_PLANT:
  case ACTRES_PILLAGE:
  case ACTRES_CLEAN:
  case ACTRES_FORTIFY:
  case ACTRES_ROAD:
  case ACTRES_CONVERT:
  case ACTRES_BASE:
  case ACTRES_MINE:
  case ACTRES_IRRIGATE:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_DEBOARD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_BOARD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_EMBARK:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_UNLOAD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_LOAD:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_TRANSPORT_DISEMBARK:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_HUT_ENTER:
  case ACTRES_HUT_FRIGHTEN:
    /* Entering the hut happens with a probability of 100%. What happens
     * next is probably up to dice rolls in Lua. */
    chance = ACTPROB_NOT_IMPLEMENTED;
    break;
  case ACTRES_UNIT_MOVE:
  case ACTRES_TELEPORT:
  case ACTRES_TELEPORT_CONQUER:
    chance = ACTPROB_CERTAIN;
    break;
    /* Not UI action, so chance is meaningless */
  case ACTRES_ENABLER_CHECK:
    chance = ACTPROB_CERTAIN;
    break;
  case ACTRES_NONE:
    /* Accommodate ruleset authors that wishes to roll the dice in Lua.
     * Would be ACTPROB_CERTAIN if not for that. */
    /* TODO: maybe allow the ruleset author to give a probability from
     * Lua? */
    chance = ACTPROB_NOT_IMPLEMENTED;
    break;
  }

  /* Non signal action probabilities should be in range. */
  fc_assert_action((action_prob_is_signal(chance)
                    || chance.max <= ACTPROB_VAL_MAX),
                   chance.max = ACTPROB_VAL_MAX);
  fc_assert_action((action_prob_is_signal(chance)
                    || chance.min >= ACTPROB_VAL_MIN),
                   chance.min = ACTPROB_VAL_MIN);

  switch (known) {
  case TRI_NO:
    return ACTPROB_IMPOSSIBLE;
    break;
  case TRI_MAYBE:
    return ACTPROB_NOT_KNOWN;
    break;
  case TRI_YES:
    return chance;
    break;
  };

  fc_assert_msg(FALSE, "Should be yes, maybe or no");

  return ACTPROB_NOT_IMPLEMENTED;
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
static struct act_prob
action_prob_vs_city_full(const struct civ_map *nmap,
                         const struct unit *actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct city *target_city)
{
  const struct impr_type *target_building;
  const struct unit_type *target_utype;
  const struct action *act = action_by_number(act_id);

  if (actor_unit == nullptr || target_city == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about city position since an unknown city
   * can't be targeted and a city can't move. */
  if (!action_id_distance_accepted(act_id,
          real_map_distance(actor_tile,
                            city_tile(target_city)))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since it must be 100% certain from the
   * player's perspective that the blocking action is legal. */
  if (action_is_blocked_by(nmap, act, actor_unit,
                           city_tile(target_city), target_city, nullptr)) {
    /* Don't offer to perform an action known to be blocked. */
    return ACTPROB_IMPOSSIBLE;
  }

  if (!player_can_see_city_externals(unit_owner(actor_unit), target_city)) {
    /* The invisible city at this tile may, as far as the player knows, not
     * exist anymore. */
    return act_prob_unseen_target(nmap, act_id, actor_unit);
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return action_prob(nmap, act_id,
                     &(const struct req_context) {
                       .player = unit_owner(actor_unit),
                       .city = tile_city(actor_tile),
                       .tile = actor_tile,
                       .unit = actor_unit,
                       .unittype = unit_type_get(actor_unit),
                     },
                     actor_home,
                     &(const struct req_context) {
                       .player = city_owner(target_city),
                       .city = target_city,
                       .building = target_building,
                       .tile = city_tile(target_city),
                       .unittype = target_utype,
                     }, nullptr);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
struct act_prob action_prob_vs_city(const struct civ_map *nmap,
                                    const struct unit *actor_unit,
                                    const action_id act_id,
                                    const struct city *target_city)
{
  return action_prob_vs_city_full(nmap, actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id, target_city);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
static struct act_prob
action_prob_vs_unit_full(const struct civ_map *nmap,
                         const struct unit *actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct unit *target_unit)
{
  if (actor_unit == nullptr || target_unit == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_UNIT));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about unit position since an unseen unit can't
   * be targeted. */
  if (!action_id_distance_accepted(act_id,
          real_map_distance(actor_tile,
                            unit_tile(target_unit)))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(nmap, act_id,
                     &(const struct req_context) {
                       .player = unit_owner(actor_unit),
                       .city = tile_city(actor_tile),
                       .tile = actor_tile,
                       .unit = actor_unit,
                       .unittype = unit_type_get(actor_unit),
                     },
                     actor_home,
                     &(const struct req_context) {
                       .player = unit_owner(target_unit),
                       .city = tile_city(unit_tile(target_unit)),
                       .tile = unit_tile(target_unit),
                       .unit = target_unit,
                       .unittype = unit_type_get(target_unit),
                     },
                     nullptr);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
struct act_prob action_prob_vs_unit(const struct civ_map *nmap,
                                    const struct unit *actor_unit,
                                    const action_id act_id,
                                    const struct unit *target_unit)
{
  return action_prob_vs_unit_full(nmap, actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id,
                                  target_unit);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on all units at the target tile.
**************************************************************************/
static struct act_prob
action_prob_vs_stack_full(const struct civ_map *nmap,
                          const struct unit *actor_unit,
                          const struct city *actor_home,
                          const struct tile *actor_tile,
                          const action_id act_id,
                          const struct tile *target_tile)
{
  struct act_prob prob_all;
  const struct req_context *actor_ctxt;
  const struct action *act = action_by_number(act_id);

  if (actor_unit == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_STACK == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_STACK));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about unit stack position since it is
   * specified as a tile and an unknown tile's position is known. */
  if (!action_id_distance_accepted(act_id,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since the actor player can see the target
   * tile. */
  if (tile_is_seen(target_tile, unit_owner(actor_unit))
      && tile_city(target_tile) != nullptr
      && !utype_can_do_act_if_tgt_citytile(unit_type_get(actor_unit),
                                           act_id,
                                           CITYT_CENTER, TRUE)) {
    /* Don't offer to perform actions that never can target a unit stack in
     * a city. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information since it must be 100% certain from the
   * player's perspective that the blocking action is legal. */
  unit_list_iterate(target_tile->units, target_unit) {
    if (action_is_blocked_by(nmap, act, actor_unit,
                             target_tile, tile_city(target_tile),
                             target_unit)) {
      /* Don't offer to perform an action known to be blocked. */
      return ACTPROB_IMPOSSIBLE;
    }
  } unit_list_iterate_end;

  /* Must be done here since an empty unseen tile will result in
   * ACTPROB_IMPOSSIBLE. */
  if (unit_list_size(target_tile->units) == 0) {
    /* Can't act against an empty tile. */

    if (player_can_trust_tile_has_no_units(unit_owner(actor_unit),
                                           target_tile)) {
      /* Known empty tile. */
      return ACTPROB_IMPOSSIBLE;
    } else {
      /* The player doesn't know that the tile is empty. */
      return act_prob_unseen_target(nmap, act_id, actor_unit);
    }
  }

  if ((action_id_has_result_safe(act_id, ACTRES_ATTACK)
       || action_id_has_result_safe(act_id, ACTRES_WIPE_UNITS)
       || action_id_has_result_safe(act_id, ACTRES_COLLECT_RANSOM))
      && tile_city(target_tile) != nullptr
      && !pplayers_at_war(city_owner(tile_city(target_tile)),
                          unit_owner(actor_unit))) {
    /* Hard coded rule: can't "Bombard", "Suicide Attack", or "Attack"
     * units in non enemy cities. */
    return ACTPROB_IMPOSSIBLE;
  }

  if ((action_id_has_result_safe(act_id, ACTRES_ATTACK)
       || action_id_has_result_safe(act_id, ACTRES_WIPE_UNITS)
       || action_id_has_result_safe(act_id, ACTRES_NUKE_UNITS)
       || action_id_has_result_safe(act_id, ACTRES_COLLECT_RANSOM))
      && !is_native_tile(unit_type_get(actor_unit), target_tile)
      && !can_attack_non_native(unit_type_get(actor_unit))) {
    /* Hard coded rule: can't "Nuke Units", "Wipe Units", "Suicide Attack",
     * or "Attack" units on non native tile without "AttackNonNative" and
     * not "Only_Native_Attack". */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Invisible units at this tile can make the action legal or illegal.
   * Invisible units can be stacked with visible units. The possible
   * existence of invisible units therefore makes the result uncertain. */
  prob_all = (can_player_see_hypotetic_units_at(unit_owner(actor_unit),
                                                target_tile)
              ? ACTPROB_CERTAIN : ACTPROB_NOT_KNOWN);

  actor_ctxt = &(const struct req_context) {
    .player = unit_owner(actor_unit),
    .city = tile_city(actor_tile),
    .tile = actor_tile,
    .unit = actor_unit,
    .unittype = unit_type_get(actor_unit),
  };

  unit_list_iterate(target_tile->units, target_unit) {
    struct act_prob prob_unit;

    if (!can_player_see_unit(unit_owner(actor_unit), target_unit)) {
      /* Only visible units are considered. The invisible units contributed
       * their uncertainty to prob_all above. */
      continue;
    }

    prob_unit = action_prob(nmap, act_id, actor_ctxt, actor_home,
                            &(const struct req_context) {
                              .player = unit_owner(target_unit),
                              .city = tile_city(unit_tile(target_unit)),
                              .tile = unit_tile(target_unit),
                              .unit = target_unit,
                              .unittype = unit_type_get(target_unit),
                            },
                            nullptr);

    if (!action_prob_possible(prob_unit)) {
      /* One unit makes it impossible for all units. */
      return ACTPROB_IMPOSSIBLE;
    } else if (action_prob_not_impl(prob_unit)) {
      /* Not implemented dominates all except impossible. */
      prob_all = ACTPROB_NOT_IMPLEMENTED;
    } else {
      fc_assert_msg(!action_prob_is_signal(prob_unit),
                    "Invalid probability [%d, %d]",
                    prob_unit.min, prob_unit.max);

      if (action_prob_is_signal(prob_all)) {
        /* Special values dominate regular values. */
        continue;
      }

      /* Probability against all target units considered until this moment
       * and the probability against this target unit. */
      prob_all.min = (prob_all.min * prob_unit.min) / ACTPROB_VAL_MAX;
      prob_all.max = (prob_all.max * prob_unit.max) / ACTPROB_VAL_MAX;
      break;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return prob_all;
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on all units at the target tile.
**************************************************************************/
struct act_prob action_prob_vs_stack(const struct civ_map *nmap,
                                     const struct unit *actor_unit,
                                     const action_id act_id,
                                     const struct tile *target_tile)
{
  return action_prob_vs_stack_full(nmap, actor_unit,
                                   unit_home(actor_unit),
                                   unit_tile(actor_unit),
                                   act_id,
                                   target_tile);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target tile.
**************************************************************************/
static struct act_prob
action_prob_vs_tile_full(const struct civ_map *nmap,
                         const struct unit *actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct tile *target_tile,
                         const struct extra_type *target_extra)
{
  if (actor_unit == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_TILE));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about tile position since an unknown tile's
   * position is known. */
  if (!action_id_distance_accepted(act_id,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(nmap, act_id,
                     &(const struct req_context) {
                       .player = unit_owner(actor_unit),
                       .city = tile_city(actor_tile),
                       .tile = actor_tile,
                       .unit = actor_unit,
                       .unittype = unit_type_get(actor_unit),
                     },
                     actor_home,
                     &(const struct req_context) {
                       .player = tile_owner(target_tile),
                       .city = tile_city(target_tile),
                       .tile = target_tile,
                     },
                     target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target tile.
**************************************************************************/
struct act_prob action_prob_vs_tile(const struct civ_map *nmap,
                                    const struct unit *actor_unit,
                                    const action_id act_id,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra)
{
  return action_prob_vs_tile_full(nmap, actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id, target_tile, target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the extras at the target tile.
**************************************************************************/
static struct act_prob
action_prob_vs_extras_full(const struct civ_map *nmap,
                           const struct unit *actor_unit,
                           const struct city *actor_home,
                           const struct tile *actor_tile,
                           const action_id act_id,
                           const struct tile *target_tile,
                           const struct extra_type *target_extra)
{
  if (actor_unit == nullptr || target_tile == nullptr) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_EXTRAS == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_EXTRAS));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* Doesn't leak information about tile position since an unknown tile's
   * position is known. */
  if (!action_id_distance_accepted(act_id,
                                   real_map_distance(actor_tile,
                                                     target_tile))) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(nmap, act_id,
                     &(const struct req_context) {
                       .player = unit_owner(actor_unit),
                       .city = tile_city(actor_tile),
                       .tile = actor_tile,
                       .unit = actor_unit,
                       .unittype = unit_type_get(actor_unit),
                     },
                     actor_home,
                     &(const struct req_context) {
                       .player = target_tile->extras_owner,
                       .city = tile_city(target_tile),
                       .tile = target_tile,
                     },
                     target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the extras at the target tile.
**************************************************************************/
struct act_prob action_prob_vs_extras(const struct civ_map *nmap,
                                      const struct unit *actor_unit,
                                      const action_id act_id,
                                      const struct tile *target_tile,
                                      const struct extra_type *target_extra)
{
  return action_prob_vs_extras_full(nmap, actor_unit,
                                    unit_home(actor_unit),
                                    unit_tile(actor_unit),
                                    act_id, target_tile, target_extra);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on itself.
**************************************************************************/
static struct act_prob
action_prob_self_full(const struct civ_map *nmap,
                      const struct unit *actor_unit,
                      const struct city *actor_home,
                      const struct tile *actor_tile,
                      const action_id act_id)
{
  if (actor_unit == nullptr) {
    /* Can't do the action when the actor is missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  /* No point in checking distance to target. It is always 0. */

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        action_id_rule_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_SELF));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(nmap, act_id,
                     &(const struct req_context) {
                       .player = unit_owner(actor_unit),
                       .city = tile_city(actor_tile),
                       .tile = actor_tile,
                       .unit = actor_unit,
                       .unittype = unit_type_get(actor_unit),
                     },
                     actor_home,
                     nullptr,
                     nullptr);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on itself.
**************************************************************************/
struct act_prob action_prob_self(const struct civ_map *nmap,
                                 const struct unit *actor_unit,
                                 const action_id act_id)
{
  return action_prob_self_full(nmap, actor_unit,
                               unit_home(actor_unit),
                               unit_tile(actor_unit),
                               act_id);
}

/**********************************************************************//**
  Returns the actor unit's probability of successfully performing the
  specified action against the action specific target.
  @param nmap      Map to consult
  @param paction   The action to perform
  @param act_unit  The actor unit
  @param tgt_city  The target for city targeted actions
  @param tgt_unit  The target for unit targeted actions
  @param tgt_tile  The target for tile and unit stack targeted actions
  @param extra_tgt The target for extra sub targeted actions
  @return The action probability of performing the action
**************************************************************************/
struct act_prob action_prob_unit_vs_tgt(const struct civ_map *nmap,
                                        const struct action *paction,
                                        const struct unit *act_unit,
                                        const struct city *tgt_city,
                                        const struct unit *tgt_unit,
                                        const struct tile *tgt_tile,
                                        const struct extra_type *extra_tgt)
{
  /* Assume impossible until told otherwise. */
  struct act_prob prob = ACTPROB_IMPOSSIBLE;

  fc_assert_ret_val(paction, ACTPROB_IMPOSSIBLE);
  fc_assert_ret_val(act_unit, ACTPROB_IMPOSSIBLE);

  switch (action_get_target_kind(paction)) {
  case ATK_STACK:
    if (tgt_tile) {
      prob = action_prob_vs_stack(nmap, act_unit, paction->id, tgt_tile);
    }
    break;
  case ATK_TILE:
    if (tgt_tile) {
      prob = action_prob_vs_tile(nmap, act_unit, paction->id, tgt_tile, extra_tgt);
    }
    break;
  case ATK_EXTRAS:
    if (tgt_tile) {
      prob = action_prob_vs_extras(nmap, act_unit, paction->id,
                                   tgt_tile, extra_tgt);
    }
    break;
  case ATK_CITY:
    if (tgt_city) {
      prob = action_prob_vs_city(nmap, act_unit, paction->id, tgt_city);
    }
    break;
  case ATK_UNIT:
    if (tgt_unit) {
      prob = action_prob_vs_unit(nmap, act_unit, paction->id, tgt_unit);
    }
    break;
  case ATK_SELF:
    prob = action_prob_self(nmap, act_unit, paction->id);
    break;
  case ATK_COUNT:
    log_error("Invalid action target kind");
    break;
  }

  return prob;
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target city given the specified
  game state changes.
**************************************************************************/
struct act_prob action_speculate_unit_on_city(const struct civ_map *nmap,
                                              const action_id act_id,
                                              const struct unit *actor,
                                              const struct city *actor_home,
                                              const struct tile *actor_tile,
                                              const bool omniscient_cheat,
                                              const struct city* target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_city_full(nmap, act_id,
                                            actor, actor_home, actor_tile,
                                            target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    /* FIXME: this branch result depends _directly_ on actor's position.
     * I.e., like, not adjacent, no action. Other branch ignores radius. */
    return action_prob_vs_city_full(nmap, actor, actor_home, actor_tile,
                                    act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target unit given the specified
  game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_unit(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct unit *target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_unit_full(nmap, act_id,
                                            actor, actor_home, actor_tile,
                                            target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_unit_full(nmap, actor, actor_home, actor_tile,
                                    act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target unit stack given the specified
  game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_stack(const struct civ_map *nmap,
                               action_id act_id,
                               const struct unit *actor,
                               const struct city *actor_home,
                               const struct tile *actor_tile,
                               bool omniscient_cheat,
                               const struct tile *target)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_stack_full(nmap, act_id,
                                             actor, actor_home, actor_tile,
                                             target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_stack_full(nmap, actor, actor_home, actor_tile,
                                     act_id, target);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target tile (and, if specified,
  extra) given the specified game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_tile(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct tile *target_tile,
                              const struct extra_type *target_extra)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_tile_full(nmap, act_id,
                                            actor, actor_home, actor_tile,
                                            target_tile, target_extra)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_tile_full(nmap, actor, actor_home, actor_tile,
                                    act_id, target_tile, target_extra);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action to the extras at the target tile (and, if
  specified, specific extra) given the specified game state changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_extras(const struct civ_map *nmap,
                                action_id act_id,
                                const struct unit *actor,
                                const struct city *actor_home,
                                const struct tile *actor_tile,
                                bool omniscient_cheat,
                                const struct tile *target_tile,
                                const struct extra_type *target_extra)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */

  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_extras_full(nmap, act_id,
                                              actor, actor_home, actor_tile,
                                              target_tile, target_extra)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_extras_full(nmap, actor, actor_home, actor_tile,
                                      act_id, target_tile, target_extra);
  }
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on itself given the specified game state
  changes.
**************************************************************************/
struct act_prob
action_speculate_unit_on_self(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat)
{
  /* FIXME: some unit state requirements still depend on the actor unit's
   * current position rather than on actor_tile. Maybe this function should
   * return ACTPROB_NOT_IMPLEMENTED when one of those is detected and no
   * other requirement makes the action ACTPROB_IMPOSSIBLE? */
  if (omniscient_cheat) {
    if (is_action_enabled_unit_on_self_full(nmap, act_id,
                                            actor, actor_home, actor_tile)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_self_full(nmap, actor, actor_home, actor_tile,
                                 act_id);
  }
}

/**********************************************************************//**
  Returns the impossible action probability.
**************************************************************************/
struct act_prob action_prob_new_impossible(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MIN };

  return out;
}

/**********************************************************************//**
  Returns the certain action probability.
**************************************************************************/
struct act_prob action_prob_new_certain(void)
{
  struct act_prob out = { ACTPROB_VAL_MAX, ACTPROB_VAL_MAX };

  return out;
}

/**********************************************************************//**
  Returns the n/a action probability.
**************************************************************************/
struct act_prob action_prob_new_not_relevant(void)
{
  struct act_prob out = { ACTPROB_VAL_NA, ACTPROB_VAL_MIN};

  return out;
}

/**********************************************************************//**
  Returns the "not implemented" action probability.
**************************************************************************/
struct act_prob action_prob_new_not_impl(void)
{
  struct act_prob out = { ACTPROB_VAL_NOT_IMPL, ACTPROB_VAL_MIN };

  return out;
}

/**********************************************************************//**
  Returns the "user don't know" action probability.
**************************************************************************/
struct act_prob action_prob_new_unknown(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MAX };

  return out;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability belongs to an action that
  may be possible.
**************************************************************************/
bool action_prob_possible(const struct act_prob probability)
{
  return (ACTPROB_VAL_MIN < probability.max
          || action_prob_not_impl(probability));
}

/**********************************************************************//**
  Returns TRUE iff the given action probability is certain that its action
  is possible.
**************************************************************************/
bool action_prob_certain(const struct act_prob probability)
{
  return (ACTPROB_VAL_MAX == probability.min
          && ACTPROB_VAL_MAX == probability.max);
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents the lack of
  an action probability.
**************************************************************************/
static inline bool
action_prob_not_relevant(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NA
         && probability.max == ACTPROB_VAL_MIN;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents that support
  for finding this action probability currently is missing from Freeciv.
**************************************************************************/
static inline bool
action_prob_not_impl(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NOT_IMPL
         && probability.max == ACTPROB_VAL_MIN;
}

/**********************************************************************//**
  Returns TRUE iff the given action probability represents a special
  signal value rather than a regular action probability value.
**************************************************************************/
static inline bool
action_prob_is_signal(const struct act_prob probability)
{
  return probability.max < probability.min;
}

/**********************************************************************//**
  Returns TRUE iff ap1 and ap2 are equal.
**************************************************************************/
bool are_action_probabilitys_equal(const struct act_prob *ap1,
                                   const struct act_prob *ap2)
{
  return ap1->min == ap2->min && ap1->max == ap2->max;
}

/**********************************************************************//**
  Compare action probabilities. Prioritize the lowest possible value.
**************************************************************************/
int action_prob_cmp_pessimist(const struct act_prob ap1,
                              const struct act_prob ap2)
{
  struct act_prob my_ap1;
  struct act_prob my_ap2;

  /* The action probabilities are real. */
  fc_assert(!action_prob_not_relevant(ap1));
  fc_assert(!action_prob_not_relevant(ap2));

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(ap1)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap1));

    my_ap1 = ACTPROB_NOT_KNOWN;
  } else {
    my_ap1 = ap1;
  }

  if (action_prob_is_signal(ap2)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap2));

    my_ap2 = ACTPROB_NOT_KNOWN;
  } else {
    my_ap2 = ap2;
  }

  /* The action probabilities now have a comparison friendly form. */
  fc_assert(!action_prob_is_signal(my_ap1));
  fc_assert(!action_prob_is_signal(my_ap2));

  /* Do the comparison. Start with min. Continue with max. */
  if (my_ap1.min < my_ap2.min) {
    return -1;
  } else if (my_ap1.min > my_ap2.min) {
    return 1;
  } else if (my_ap1.max < my_ap2.max) {
    return -1;
  } else if (my_ap1.max > my_ap2.max) {
    return 1;
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Returns double in the range [0-1] representing the minimum of the given
  action probability.
**************************************************************************/
double action_prob_to_0_to_1_pessimist(const struct act_prob ap)
{
  struct act_prob my_ap;

  /* The action probability is real. */
  fc_assert(!action_prob_not_relevant(ap));

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(ap)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(ap));

    my_ap = ACTPROB_NOT_KNOWN;
  } else {
    my_ap = ap;
  }

  /* The action probability now has a math friendly form. */
  fc_assert(!action_prob_is_signal(my_ap));

  return (double)my_ap.min / (double) ACTPROB_VAL_MAX;
}

/**********************************************************************//**
  Returns ap1 and ap2 - as in both ap1 and ap2 happening.
  Said in math that is: P(A) * P(B)
**************************************************************************/
struct act_prob action_prob_and(const struct act_prob *ap1,
                                const struct act_prob *ap2)
{
  struct act_prob my_ap1;
  struct act_prob my_ap2;
  struct act_prob out;

  /* The action probabilities are real. */
  fc_assert(ap1 && !action_prob_not_relevant(*ap1));
  fc_assert(ap2 && !action_prob_not_relevant(*ap2));

  if (action_prob_is_signal(*ap1)
      && are_action_probabilitys_equal(ap1, ap2)) {
    /* Keep the information rather than converting the signal to
     * ACTPROB_NOT_KNOWN. */

    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    out.min = ap1->min;
    out.max = ap2->max;

    return out;
  }

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(*ap1)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    my_ap1.min = ACTPROB_VAL_MIN;
    my_ap1.max = ACTPROB_VAL_MAX;
  } else {
    my_ap1.min = ap1->min;
    my_ap1.max = ap1->max;
  }

  if (action_prob_is_signal(*ap2)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap2));

    my_ap2.min = ACTPROB_VAL_MIN;
    my_ap2.max = ACTPROB_VAL_MAX;
  } else {
    my_ap2.min = ap2->min;
    my_ap2.max = ap2->max;
  }

  /* The action probabilities now have a math friendly form. */
  fc_assert(!action_prob_is_signal(my_ap1));
  fc_assert(!action_prob_is_signal(my_ap2));

  /* Do the math. */
  out.min = (my_ap1.min * my_ap2.min) / ACTPROB_VAL_MAX;
  out.max = (my_ap1.max * my_ap2.max) / ACTPROB_VAL_MAX;

  /* Cap at 100%. */
  out.min = MIN(out.min, ACTPROB_VAL_MAX);
  out.max = MIN(out.max, ACTPROB_VAL_MAX);

  return out;
}

/**********************************************************************//**
  Returns ap1 with ap2 as fall back in cases where ap1 doesn't happen.
  Said in math that is: P(A) + P(A') * P(B)

  This is useful to calculate the probability of doing action A or, when A
  is impossible, falling back to doing action B.
**************************************************************************/
struct act_prob action_prob_fall_back(const struct act_prob *ap1,
                                      const struct act_prob *ap2)
{
  struct act_prob my_ap1;
  struct act_prob my_ap2;
  struct act_prob out;

  /* The action probabilities are real. */
  fc_assert(ap1 && !action_prob_not_relevant(*ap1));
  fc_assert(ap2 && !action_prob_not_relevant(*ap2));

  if (action_prob_is_signal(*ap1)
      && are_action_probabilitys_equal(ap1, ap2)) {
    /* Keep the information rather than converting the signal to
     * ACTPROB_NOT_KNOWN. */

    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    out.min = ap1->min;
    out.max = ap2->max;

    return out;
  }

  /* Convert any signals to ACTPROB_NOT_KNOWN. */
  if (action_prob_is_signal(*ap1)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap1));

    my_ap1.min = ACTPROB_VAL_MIN;
    my_ap1.max = ACTPROB_VAL_MAX;
  } else {
    my_ap1.min = ap1->min;
    my_ap1.max = ap1->max;
  }

  if (action_prob_is_signal(*ap2)) {
    /* Assert that it is OK to convert the signal. */
    fc_assert(action_prob_not_impl(*ap2));

    my_ap2.min = ACTPROB_VAL_MIN;
    my_ap2.max = ACTPROB_VAL_MAX;
  } else {
    my_ap2.min = ap2->min;
    my_ap2.max = ap2->max;
  }

  /* The action probabilities now have a math friendly form. */
  fc_assert(!action_prob_is_signal(my_ap1));
  fc_assert(!action_prob_is_signal(my_ap2));

  /* Do the math. */
  out.min = my_ap1.min + (((ACTPROB_VAL_MAX - my_ap1.min) * my_ap2.min)
                          / ACTPROB_VAL_MAX);
  out.max = my_ap1.max + (((ACTPROB_VAL_MAX - my_ap1.max) * my_ap2.max)
                          / ACTPROB_VAL_MAX);

  /* Cap at 100%. */
  out.min = MIN(out.min, ACTPROB_VAL_MAX);
  out.max = MIN(out.max, ACTPROB_VAL_MAX);

  return out;
}

/**********************************************************************//**
  Returns the initial odds of an action not failing its dice roll.
**************************************************************************/
int action_dice_roll_initial_odds(const struct action *paction)
{
  switch (actres_dice_type(paction->result)) {
  case DRT_DIPLCHANCE:
    if (BV_ISSET(game.info.diplchance_initial_odds, paction->id)) {
      /* Take the initial odds from the diplchance setting. */
      return server_setting_value_int_get(
            server_setting_by_name("diplchance"));
    }
    fc__fallthrough;
  case DRT_CERTAIN:
    return 100;
  case DRT_NONE:
    break;
  }

  /* The odds of the action not being stopped by its dice roll when the dice
   * isn't thrown is 100%. ACTION_ODDS_PCT_DICE_ROLL_NA is above 100% */
  return ACTION_ODDS_PCT_DICE_ROLL_NA;
}

/**********************************************************************//**
  Returns the odds of an action not failing its dice roll.
**************************************************************************/
int action_dice_roll_odds(const struct player *act_player,
                          const struct unit *act_unit,
                          const struct city *tgt_city,
                          const struct player *tgt_player,
                          const struct action *paction)
{
  int odds = action_dice_roll_initial_odds(paction);
  const struct unit_type *actu_type = unit_type_get(act_unit);

  fc_assert_action_msg(odds >= 0 && odds <= 100,
                       odds = 100,
                       "Bad initial odds for action number %d."
                       " Does it roll the dice at all?",
                       paction->id);

  /* Let the Action_Odds_Pct effect modify the odds. The advantage of doing
   * it this way instead of rolling twice is that Action_Odds_Pct can
   * increase the odds. */
  odds = odds
    + ((odds
        * get_target_bonus_effects(nullptr,
                                   &(const struct req_context) {
                                     .player = act_player,
                                     .city = tgt_city,
                                     .unit = act_unit,
                                     .unittype = actu_type,
                                     .action = paction,
                                   },
                                   &(const struct req_context) {
                                     .player = tgt_player,
                                   },
                                   EFT_ACTION_ODDS_PCT))
       / 100)
    - ((odds
        * get_target_bonus_effects(nullptr,
                                   &(const struct req_context) {
                                     .player = tgt_player,
                                     .city = tgt_city,
                                     .unit = act_unit,
                                     .unittype = actu_type,
                                     .action = paction,
                                   },
                                   &(const struct req_context) {
                                     .player = act_player,
                                   },
                                   EFT_ACTION_RESIST_PCT))
       / 100);


  /* Odds are between 0% and 100%. */
  return CLIP(0, odds, 100);
}

/**********************************************************************//**
  Will a player with the government gov be immune to the action act?
**************************************************************************/
bool action_immune_government(struct government *gov, action_id act)
{
  struct action *paction = action_by_number(act);

  /* Always immune since its not enabled. Doesn't count. */
  if (!action_is_in_use(paction)) {
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(act), enabler) {
    if (requirement_fulfilled_by_government(gov, &(enabler->target_reqs))) {
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**********************************************************************//**
  Returns TRUE if the wanted action can be done to the target.

  target may be nullptr. This is equivalent to passing an empty context.
**************************************************************************/
static bool is_target_possible(const action_id wanted_action,
                               const struct player *actor_player,
                               const struct req_context *target)
{
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (are_reqs_active(target,
                        &(const struct req_context) {
                          .player = actor_player,
                        },
                        &enabler->target_reqs, RPT_POSSIBLE)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the wanted action can be done to the target city.
**************************************************************************/
bool is_action_possible_on_city(action_id act_id,
                                const struct player *actor_player,
                                const struct city* target_city)
{
  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        FALSE, "Action %s is against %s not cities",
                        action_id_rule_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)));

  return is_target_possible(act_id, actor_player,
                            &(const struct req_context) {
                              .player = city_owner(target_city),
                              .city = target_city,
                              .tile = city_tile(target_city),
                            });
}

/**********************************************************************//**
  Returns TRUE if the wanted action (as far as the player knows) can be
  performed right now by the specified actor unit if an approriate target
  is provided.
**************************************************************************/
bool action_maybe_possible_actor_unit(const struct civ_map *nmap,
                                      const action_id act_id,
                                      const struct unit *actor_unit)
{
  const struct player *actor_player = unit_owner(actor_unit);
  const struct req_context actor_ctxt = {
    .player = actor_player,
    .city = tile_city(unit_tile(actor_unit)),
    .tile = unit_tile(actor_unit),
    .unit = actor_unit,
    .unittype = unit_type_get(actor_unit),
  };
  const struct action *paction = action_by_number(act_id);

  enum fc_tristate result;

  fc_assert_ret_val(actor_unit, FALSE);

  if (!utype_can_do_action(actor_unit->utype, act_id)) {
    /* The unit type can't perform the action. */
    return FALSE;
  }

  result = action_hard_reqs_actor(nmap, paction, &actor_ctxt, FALSE,
                                  unit_home(actor_unit));

  if (result == TRI_NO) {
    /* The hard requirements aren't fulfilled. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(act_id),
                              enabler) {
    const enum fc_tristate current
        = mke_eval_reqs(actor_player, &actor_ctxt, nullptr,
                        &enabler->actor_reqs,
                        /* Needed since no player to evaluate DiplRel
                         * requirements against. */
                        RPT_POSSIBLE);

    if (current == TRI_YES
        || current == TRI_MAYBE) {
      /* The ruleset requirements may be fulfilled. */
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  /* No action enabler allows this action. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the specified action can't be done now but would have
  been legal if the unit had full movement.
**************************************************************************/
bool action_mp_full_makes_legal(const struct unit *actor,
                                const action_id act_id)
{
  fc_assert(action_id_exists(act_id) || act_id == ACTION_ANY);

  /* Check if full movement points may enable the specified action. */
  return !utype_may_act_move_frags(unit_type_get(actor),
                                   act_id,
                                   actor->moves_left)
      && utype_may_act_move_frags(unit_type_get(actor),
                                  act_id,
                                  unit_move_rate(actor));
}

/**********************************************************************//**
  Returns TRUE iff the specified action enabler may be active for an actor
  of the specified unit type in the current ruleset.
  Note that the answer may be "no" even if this function returns TRUE. It
  may just be unable to detect it.
  @param ae        the action enabler to check
  @param act_utype the candidate actor unit type
  @returns TRUE if the enabler may be active for act_utype
**************************************************************************/
bool action_enabler_utype_possible_actor(const struct action_enabler *ae,
                                         const struct unit_type *act_utype)
{
  const struct action *paction = enabler_get_action(ae);
  struct universal actor_univ = { .kind = VUT_UTYPE,
                                  .value.utype = act_utype };

  fc_assert_ret_val(paction != nullptr, FALSE);
  fc_assert_ret_val(action_get_actor_kind(paction) == AAK_UNIT, FALSE);
  fc_assert_ret_val(act_utype != nullptr, FALSE);

  return (action_actor_utype_hard_reqs_ok(paction, act_utype)
          && !req_vec_is_impossible_to_fulfill(&ae->actor_reqs)
          && universal_fulfills_requirements(FALSE, &ae->actor_reqs,
                                             &actor_univ));
}

/**********************************************************************//**
  Returns TRUE iff the specified action enabler may have an actor that it
  may be enabled for in the current ruleset. An enabler can't be enabled if
  no potential actor fulfills both its action's hard requirements and its
  own actor requirement vector, actor_reqs.
  Note that the answer may be "no" even if this function returns TRUE. It
  may just be unable to detect it.
  @param ae        the action enabler to check
  @returns TRUE if the enabler may be enabled at all
**************************************************************************/
bool action_enabler_possible_actor(const struct action_enabler *ae)
{
  const struct action *paction = enabler_get_action(ae);

  switch (action_get_actor_kind(paction)) {
  case AAK_UNIT:
    unit_type_iterate(putype) {
      if (action_enabler_utype_possible_actor(ae, putype)) {
        /* A possible actor unit type has been found. */
        return TRUE;
      }
    } unit_type_iterate_end;

    /* No actor detected. */
    return FALSE;
  case AAK_CITY:
  case AAK_PLAYER:
    /* Currently can't detect */
    return TRUE;
  case AAK_COUNT:
    fc_assert(action_get_actor_kind(paction) != AAK_COUNT);
    break;
  }

  /* No actor detected. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE iff the specified action has an actor that fulfills its
  hard requirements in the current ruleset.
  @param paction the action to check
  @returns TRUE if the action's hard requirement may be fulfilled in
                the current ruleset.
**************************************************************************/
static bool action_has_possible_actor_hard_reqs(struct action *paction)
{
  switch (action_get_actor_kind(paction)) {
  case AAK_UNIT:
    unit_type_iterate(putype) {
      if (action_actor_utype_hard_reqs_ok(paction, putype)) {
        return TRUE;
      }
    } unit_type_iterate_end;
    break;
  case AAK_CITY:
  case AAK_PLAYER:
    /* No ruleset hard reqs atm */
    return TRUE;
  case AAK_COUNT:
    fc_assert(action_get_actor_kind(paction) != AAK_COUNT);
    break;
  }

  /* No actor detected. */
  return FALSE;
}

/**********************************************************************//**
  Returns TRUE if the specified action may be enabled in the current
  ruleset.

  @param paction the action to check if is in use.
  @return TRUE if the action could be enabled in the current ruleset.
**************************************************************************/
bool action_is_in_use(struct action *paction)
{
  struct action_enabler_list *enablers;

  if (!action_has_possible_actor_hard_reqs(paction)) {
    /* Hard requirements not fulfilled. */
    return FALSE;
  }

  enablers = action_enablers_for_action(paction->id);

  action_enabler_list_re_iterate(enablers, ae) {
    /* If this iteration finds any entries, action is enabled. */
    return TRUE;
  } action_enabler_list_re_iterate_end;

  /* No non deleted action enabler. */
  return FALSE;
}

/**********************************************************************//**
  Is the action for freeciv's internal use only?

  @param  paction   The action to check
  @return           Whether action is for internal use only
**************************************************************************/
bool action_is_internal(struct action *paction)
{
  return paction != nullptr
    && action_has_result(paction, ACTRES_ENABLER_CHECK);
}

/**********************************************************************//**
  Is action by id for freeciv's internal use only?

  @param  act       Id of the action to check
  @return           Whether action is for internal use only
**************************************************************************/
bool action_id_is_internal(action_id act)
{
  return action_is_internal(action_by_number(act));
}

/**********************************************************************//**
  Returns action auto performer rule slot number num so it can be filled.
**************************************************************************/
struct action_auto_perf *action_auto_perf_slot_number(const int num)
{
  fc_assert_ret_val(num >= 0, nullptr);
  fc_assert_ret_val(num < MAX_NUM_ACTION_AUTO_PERFORMERS, nullptr);

  return &auto_perfs[num];
}

/**********************************************************************//**
  Returns action auto performer rule number num.

  Used in action_auto_perf_iterate()

  WARNING: If the cause of the returned action performer rule is
  AAPC_COUNT it means that it is unused.
**************************************************************************/
const struct action_auto_perf *action_auto_perf_by_number(const int num)
{
  return action_auto_perf_slot_number(num);
}

/**********************************************************************//**
  Is there any action enablers of the given type not blocked by universals?
**************************************************************************/
bool action_univs_not_blocking(const struct action *paction,
                               struct universal *actor_uni,
                               struct universal *target_uni)
{
  action_enabler_list_iterate(action_enablers_for_action(paction->id),
                              enab) {
    if ((actor_uni == nullptr
         || universal_fulfills_requirements(FALSE, &(enab->actor_reqs),
                                            actor_uni))
        && (target_uni == nullptr
            || universal_fulfills_requirements(FALSE, &(enab->target_reqs),
                                               target_uni))) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Terminate an action array of the specified size.
  @param act_array the array to end
  @param size the number of elements to include in the list
**************************************************************************/
void action_array_end(action_id *act_array, int size)
{
  fc_assert_ret(size <= MAX_NUM_ACTIONS);

  if (size < MAX_NUM_ACTIONS) {
    /* An action array is terminated by ACTION_NONE */
    act_array[size] = ACTION_NONE;
  }
}

/**********************************************************************//**
  Add all actions with the specified result to the specified action array
  starting at the specified position.
  @param act_array the array to add the actions to
  @param position index in act_array that is updated as action are added
  @param result all actions with this result are added.
**************************************************************************/
void action_array_add_all_by_result(action_id *act_array,
                                   int *position,
                                   enum action_result result)
{
  action_iterate(act) {
    struct action *paction = action_by_number(act);
    if (paction->result == result) {
      /* Assume one result for each action. */
      fc_assert_ret(*position < MAX_NUM_ACTIONS);

      act_array[(*position)++] = paction->id;
    }
  } action_iterate_end;
}

/**********************************************************************//**
  Return default ui_name for the action
**************************************************************************/
const char *action_ui_name_default(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
    /* TRANS: _Poison City (3% chance of success). */
    return N_("%sPoison City%s");
  case ACTION_SPY_POISON_ESC:
    /* TRANS: _Poison City and Escape (3% chance of success). */
    return N_("%sPoison City and Escape%s");
  case ACTION_SPY_SABOTAGE_UNIT:
    /* TRANS: S_abotage Enemy Unit (3% chance of success). */
    return N_("S%sabotage Enemy Unit%s");
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
    /* TRANS: S_abotage Enemy Unit and Escape (3% chance of success). */
    return N_("S%sabotage Enemy Unit and Escape%s");
  case ACTION_SPY_BRIBE_UNIT:
    /* TRANS: Bribe Enemy _Unit (3% chance of success). */
    return N_("Bribe Enemy %sUnit%s");
  case ACTION_SPY_BRIBE_STACK:
    /* TRANS: Bribe Enemy _Stack (3% chance of success). */
    return N_("Bribe Enemy %sStack%s");
  case ACTION_SPY_SABOTAGE_CITY:
    /* TRANS: _Sabotage City (3% chance of success). */
    return N_("%sSabotage City%s");
  case ACTION_SPY_SABOTAGE_CITY_ESC:
    /* TRANS: _Sabotage City and Escape (3% chance of success). */
    return N_("%sSabotage City and Escape%s");
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    /* TRANS: Industria_l Sabotage (3% chance of success). */
    return N_("Industria%sl Sabotage%s");
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
    /* TRANS: Industria_l Sabotage Production (3% chance of success). */
    return N_("Industria%sl Sabotage Production%s");
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
    /* TRANS: Industria_l Sabotage and Escape (3% chance of success). */
    return N_("Industria%sl Sabotage and Escape%s");
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
    /* TRANS: Industria_l Sabotage Production and Escape (3% chance of success). */
    return N_("Industria%sl Sabotage Production and Escape%s");
  case ACTION_SPY_INCITE_CITY:
    /* TRANS: Incite a Re_volt (3% chance of success). */
    return N_("Incite a Re%svolt%s");
  case ACTION_SPY_INCITE_CITY_ESC:
    /* TRANS: Incite a Re_volt and Escape (3% chance of success). */
    return N_("Incite a Re%svolt and Escape%s");
  case ACTION_ESTABLISH_EMBASSY:
    /* TRANS: Establish _Embassy (100% chance of success). */
    return N_("Establish %sEmbassy%s");
  case ACTION_ESTABLISH_EMBASSY_STAY:
    /* TRANS: Becom_e Ambassador (100% chance of success). */
    return N_("Becom%se Ambassador%s");
  case ACTION_SPY_STEAL_TECH:
    /* TRANS: Steal _Technology (3% chance of success). */
    return N_("Steal %sTechnology%s");
  case ACTION_SPY_STEAL_TECH_ESC:
    /* TRANS: Steal _Technology and Escape (3% chance of success). */
    return N_("Steal %sTechnology and Escape%s");
  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* TRANS: In_dustrial Espionage (3% chance of success). */
    return N_("In%sdustrial Espionage%s");
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
    /* TRANS: In_dustrial Espionage and Escape (3% chance of success). */
    return N_("In%sdustrial Espionage and Escape%s");
  case ACTION_SPY_INVESTIGATE_CITY:
    /* TRANS: _Investigate City (100% chance of success). */
    return N_("%sInvestigate City%s");
  case ACTION_INV_CITY_SPEND:
    /* TRANS: _Investigate City (spends the unit) (100% chance of
     * success). */
    return N_("%sInvestigate City (spends the unit)%s");
  case ACTION_SPY_STEAL_GOLD:
    /* TRANS: Steal _Gold (100% chance of success). */
    return N_("Steal %sGold%s");
  case ACTION_SPY_STEAL_GOLD_ESC:
    /* TRANS: Steal _Gold and Escape (100% chance of success). */
    return N_("Steal %sGold and Escape%s");
  case ACTION_SPY_SPREAD_PLAGUE:
    /* TRANS: Spread _Plague (100% chance of success). */
    return N_("Spread %sPlague%s");
  case ACTION_STEAL_MAPS:
    /* TRANS: Steal _Maps (100% chance of success). */
    return N_("Steal %sMaps%s");
  case ACTION_STEAL_MAPS_ESC:
    /* TRANS: Steal _Maps and Escape (100% chance of success). */
    return N_("Steal %sMaps and Escape%s");
  case ACTION_TRADE_ROUTE:
    /* TRANS: Establish Trade _Route (100% chance of success). */
    return N_("Establish Trade %sRoute%s");
  case ACTION_MARKETPLACE:
    /* TRANS: Enter _Marketplace (100% chance of success). */
    return N_("Enter %sMarketplace%s");
  case ACTION_HELP_WONDER:
    /* TRANS: Help _build Wonder (100% chance of success). */
    return N_("Help %sbuild Wonder%s");
  case ACTION_CAPTURE_UNITS:
    /* TRANS: _Capture Units (100% chance of success). */
    return N_("%sCapture Units%s");
  case ACTION_EXPEL_UNIT:
    /* TRANS: _Expel Unit (100% chance of success). */
    return N_("%sExpel Unit%s");
  case ACTION_FOUND_CITY:
    /* TRANS: _Found City (100% chance of success). */
    return N_("%sFound City%s");
  case ACTION_JOIN_CITY:
    /* TRANS: _Join City (100% chance of success). */
    return N_("%sJoin City%s");
  case ACTION_BOMBARD:
    /* TRANS: B_ombard (100% chance of success). */
    return N_("B%sombard%s");
  case ACTION_BOMBARD2:
    /* TRANS: B_ombard 2 (100% chance of success). */
    return N_("B%sombard 2%s");
  case ACTION_BOMBARD3:
    /* TRANS: B_ombard 3 (100% chance of success). */
    return N_("B%sombard 3%s");
  case ACTION_BOMBARD4:
    /* TRANS: B_ombard 4 (100% chance of success). */
    return N_("B%sombard 4%s");
  case ACTION_BOMBARD_LETHAL:
  case ACTION_BOMBARD_LETHAL2:
    /* TRANS: Lethal B_ombard (100% chance of success). */
    return N_("Lethal B%sombard%s");
  case ACTION_SPY_NUKE:
    /* TRANS: Suitcase _Nuke (100% chance of success). */
    return N_("Suitcase %sNuke%s");
  case ACTION_SPY_NUKE_ESC:
    /* TRANS: Suitcase _Nuke and Escape (100% chance of success). */
    return N_("Suitcase %sNuke and Escape%s");
  case ACTION_NUKE:
    /* TRANS: Explode _Nuclear (100% chance of success). */
    return N_("Explode %sNuclear%s");
  case ACTION_NUKE_CITY:
    /* TRANS: _Nuke City (100% chance of success). */
    return N_("%sNuke City%s");
  case ACTION_NUKE_UNITS:
    /* TRANS: _Nuke Units (100% chance of success). */
    return N_("%sNuke Units%s");
  case ACTION_DESTROY_CITY:
    /* TRANS: Destroy _City (100% chance of success). */
    return N_("Destroy %sCity%s");
  case ACTION_DISBAND_UNIT_RECOVER:
    /* TRANS: Dis_band recovering production (100% chance of success). */
    return N_("Dis%sband recovering production%s");
  case ACTION_DISBAND_UNIT:
    /* TRANS: Dis_band without recovering production (100% chance of success). */
    return N_("Dis%sband without recovering production%s");
  case ACTION_HOME_CITY:
    /* TRANS: Set _Home City (100% chance of success). */
    return N_("Set %sHome City%s");
  case ACTION_HOMELESS:
    /* TRANS: Make _Homeless (100% chance of success). */
    return N_("Make %sHomeless%s");
  case ACTION_UPGRADE_UNIT:
    /* TRANS: _Upgrade Unit (100% chance of success). */
    return N_("%sUpgrade Unit%s");
  case ACTION_PARADROP:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_PARADROP_CONQUER:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_PARADROP_FRIGHTEN:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_PARADROP_ENTER:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_PARADROP_ENTER_CONQUER:
    /* TRANS: Drop _Paratrooper (100% chance of success). */
    return N_("Drop %sParatrooper%s");
  case ACTION_AIRLIFT:
    /* TRANS: _Airlift to City (100% chance of success). */
    return N_("%sAirlift to City%s");
  case ACTION_ATTACK:
  case ACTION_ATTACK2:
    /* TRANS: _Attack (100% chance of success). */
    return N_("%sAttack%s");
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
    /* TRANS: _Suicide Attack (100% chance of success). */
    return N_("%sSuicide Attack%s");
  case ACTION_WIPE_UNITS:
    /* TRANS: _Wipe Units (100% chance of success). */
    return N_("%sWipe Units%s");
  case ACTION_COLLECT_RANSOM:
    /* TRANS: Collect _Ransom (100% chance of success). */
    return N_("Collect %sRansom%s");
  case ACTION_STRIKE_BUILDING:
    /* TRANS: Surgical Str_ike Building (100% chance of success). */
    return N_("Surgical Str%sike Building%s");
  case ACTION_STRIKE_PRODUCTION:
    /* TRANS: Surgical Str_ike Production (100% chance of success). */
    return N_("Surgical Str%sike Production%s");
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
    /* TRANS: _Conquer City (100% chance of success). */
    return N_("%sConquer City%s");
  case ACTION_CONQUER_CITY_SHRINK2:
    /* TRANS: _Conquer City 2 (100% chance of success). */
    return N_("%sConquer City 2%s");
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
    /* TRANS: _Conquer Extras (100% chance of success). */
    return N_("%sConquer Extras%s");
  case ACTION_CONQUER_EXTRAS2:
    /* TRANS: _Conquer Extras 2 (100% chance of success). */
    return N_("%sConquer Extras 2%s");
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
    /* TRANS: Heal _Unit (3% chance of success). */
    return N_("Heal %sUnit%s");
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
    /* TRANS: _Transform Terrain (3% chance of success). */
    return N_("%sTransform Terrain%s");
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
    /* TRANS: Transform by _Cultivating (3% chance of success). */
    return N_("Transform by %sCultivating%s");
  case ACTION_PLANT:
  case ACTION_PLANT2:
    /* TRANS: Transform by _Planting (3% chance of success). */
    return N_("Transform by %sPlanting%s");
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
    /* TRANS: Pilla_ge (100% chance of success). */
    return N_("Pilla%sge%s");
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
    /* TRANS: Clean (100% chance of success). */
    return N_("%sClean%s");
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
    /* TRANS: _Fortify (100% chance of success). */
    return N_("%sFortify%s");
  case ACTION_ROAD:
  case ACTION_ROAD2:
    /* TRANS: Build _Road (100% chance of success). */
    return N_("Build %sRoad%s");
  case ACTION_CONVERT:
    /* TRANS: _Convert Unit (100% chance of success). */
    return N_("%sConvert Unit%s");
  case ACTION_BASE:
  case ACTION_BASE2:
    /* TRANS: _Build Base (100% chance of success). */
    return N_("%sBuild Base%s");
  case ACTION_MINE:
  case ACTION_MINE2:
    /* TRANS: Build _Mine (100% chance of success). */
    return N_("Build %sMine%s");
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
    /* TRANS: Build _Irrigation (100% chance of success). */
    return N_("Build %sIrrigation%s");
  case ACTION_TRANSPORT_DEBOARD:
    /* TRANS: _Deboard (100% chance of success). */
    return N_("%sDeboard%s");
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
    /* TRANS: _Board (100% chance of success). */
    return N_("%sBoard%s");
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
    /* TRANS: _Embark (100% chance of success). */
    return N_("%sEmbark%s");
  case ACTION_TRANSPORT_UNLOAD:
    /* TRANS: _Unload (100% chance of success). */
    return N_("%sUnload%s");
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
    /* TRANS: _Load (100% chance of success). */
    return N_("%sLoad%s");
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
    /* TRANS: _Disembark (100% chance of success). */
    return N_("%sDisembark%s");
  case ACTION_TRANSPORT_DISEMBARK2:
    /* TRANS: _Disembark 2 (100% chance of success). */
    return N_("%sDisembark 2%s");
  case ACTION_SPY_ATTACK:
    /* TRANS: _Eliminate Diplomat (100% chance of success). */
    return N_("%sEliminate Diplomat%s");
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
    /* TRANS: Enter _Hut (100% chance of success). */
    return N_("Enter %sHut%s");
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
    /* TRANS: Frighten _Hut (100% chance of success). */
    return N_("Frighten %sHut%s");
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
    /* TRANS: Regular _Move (100% chance of success). */
    return N_("Regular %sMove%s");
  case ACTION_TELEPORT:
  case ACTION_TELEPORT2:
  case ACTION_TELEPORT3:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_TELEPORT_CONQUER:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_TELEPORT_FRIGHTEN:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_TELEPORT_ENTER:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_TELEPORT_ENTER_CONQUER:
    /* TRANS: _Teleport (100% chance of success). */
    return N_("%sTeleport%s");
  case ACTION_SPY_ESCAPE:
    /* TRANS: _Escape To Nearest City (100% chance of success). */
    return N_("%sEscape To Nearest City%s");
  case ACTION_USER_ACTION1:
    /* TRANS: _User Action 1 (100% chance of success). */
    return N_("%sUser Action 1%s");
  case ACTION_USER_ACTION2:
    /* TRANS: _User Action 2 (100% chance of success). */
    return N_("%sUser Action 2%s");
  case ACTION_USER_ACTION3:
    /* TRANS: _User Action 3 (100% chance of success). */
    return N_("%sUser Action 3%s");
  case ACTION_USER_ACTION4:
    /* TRANS: _User Action 4 (100% chance of success). */
    return N_("%sUser Action 4%s");
  case ACTION_GAIN_VETERANCY:
    return N_("%sGain Veterancy%s");
  case ACTION_ESCAPE:
    return N_("%sEscape%s");
  case ACTION_CIVIL_WAR:
    return N_("%sCivil War%s");
  case ACTION_FINISH_UNIT:
    return N_("Finish %sUnit%s");
  case ACTION_FINISH_BUILDING:
    return N_("Finish %sBuilding%s");
  case ACTION_COUNT:
    fc_assert(act != ACTION_COUNT);
    break;
  }

  return nullptr;
}

/**********************************************************************//**
  Return min range ruleset variable name for the action or nullptr if min
  range can't be set in the ruleset.

  TODO: Make actions generic and put min_range in a field of the action.
**************************************************************************/
const char *action_min_range_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_BRIBE_STACK:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT_RECOVER:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_ATTACK2:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
  case ACTION_WIPE_UNITS:
  case ACTION_COLLECT_RANSOM:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK2:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_BOMBARD4:
  case ACTION_BOMBARD_LETHAL:
  case ACTION_BOMBARD_LETHAL2:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
  case ACTION_SPY_ESCAPE:
  case ACTION_GAIN_VETERANCY:
  case ACTION_ESCAPE:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
    /* Min range is not ruleset changeable */
    return nullptr;
  case ACTION_NUKE:
    return "explode_nuclear_min_range";
  case ACTION_NUKE_CITY:
    return "nuke_city_min_range";
  case ACTION_NUKE_UNITS:
    return "nuke_units_min_range";
  case ACTION_TELEPORT:
    return "teleport_min_range";
  case ACTION_TELEPORT2:
    return "teleport_2_min_range";
  case ACTION_TELEPORT3:
    return "teleport_3_min_range";
  case ACTION_TELEPORT_CONQUER:
    return "teleport_conquer_min_range";
  case ACTION_TELEPORT_FRIGHTEN:
    return "teleport_frighten_min_range";
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
    return "teleport_frighten_conquer_min_range";
  case ACTION_TELEPORT_ENTER:
    return "teleport_enter_min_range";
  case ACTION_TELEPORT_ENTER_CONQUER:
    return "teleport_enter_conquer_min_range";
  case ACTION_USER_ACTION1:
    return "user_action_1_min_range";
  case ACTION_USER_ACTION2:
    return "user_action_2_min_range";
  case ACTION_USER_ACTION3:
    return "user_action_3_min_range";
  case ACTION_USER_ACTION4:
    return "user_action_4_min_range";
  case ACTION_COUNT:
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);

  return nullptr;
}

/**********************************************************************//**
  Return max range ruleset variable name for the action or nullptr if max
  range can't be set in the ruleset.

  TODO: make actions generic and put max_range in a field of the action.
**************************************************************************/
const char *action_max_range_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_BRIBE_STACK:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_ATTACK:
  case ACTION_ATTACK2:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
  case ACTION_WIPE_UNITS:
  case ACTION_COLLECT_RANSOM:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK2:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
  case ACTION_SPY_ESCAPE:
  case ACTION_GAIN_VETERANCY:
  case ACTION_ESCAPE:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
    /* Max range is not ruleset changeable */
    return nullptr;
  case ACTION_HELP_WONDER:
    return "help_wonder_max_range";
  case ACTION_DISBAND_UNIT_RECOVER:
    return "disband_unit_recover_max_range";
  case ACTION_BOMBARD:
    return "bombard_max_range";
  case ACTION_BOMBARD2:
    return "bombard_2_max_range";
  case ACTION_BOMBARD3:
    return "bombard_3_max_range";
  case ACTION_BOMBARD4:
    return "bombard_4_max_range";
  case ACTION_BOMBARD_LETHAL:
    return "bombard_lethal_max_range";
  case ACTION_BOMBARD_LETHAL2:
    return "bombard_lethal_2_max_range";
  case ACTION_NUKE:
    return "explode_nuclear_max_range";
  case ACTION_NUKE_CITY:
    return "nuke_city_max_range";
  case ACTION_NUKE_UNITS:
    return "nuke_units_max_range";
  case ACTION_AIRLIFT:
    return "airlift_max_range";
  case ACTION_TELEPORT:
    return "teleport_max_range";
  case ACTION_TELEPORT2:
    return "teleport_2_max_range";
  case ACTION_TELEPORT3:
    return "teleport_3_max_range";
  case ACTION_TELEPORT_CONQUER:
    return "teleport_conquer_max_range";
  case ACTION_TELEPORT_FRIGHTEN:
    return "teleport_frighten_max_range";
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
    return "teleport_frighten_conquer_max_range";
  case ACTION_TELEPORT_ENTER:
    return "teleport_enter_max_range";
  case ACTION_TELEPORT_ENTER_CONQUER:
    return "teleport_enter_conquer_max_range";
  case ACTION_USER_ACTION1:
    return "user_action_1_max_range";
  case ACTION_USER_ACTION2:
    return "user_action_2_max_range";
  case ACTION_USER_ACTION3:
    return "user_action_3_max_range";
  case ACTION_USER_ACTION4:
    return "user_action_4_max_range";
  case ACTION_COUNT:
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);

  return nullptr;
}

/**********************************************************************//**
  Return target kind ruleset variable name for the action or nullptr if
  target kind can't be set in the ruleset.

  TODO: make actions generic and put target_kind in a field of the action.
**************************************************************************/
const char *action_target_kind_ruleset_var_name(int act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_BRIBE_STACK:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_NUKE_UNITS:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT_RECOVER:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_ATTACK2:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
  case ACTION_WIPE_UNITS:
  case ACTION_COLLECT_RANSOM:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK2:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_BOMBARD4:
  case ACTION_BOMBARD_LETHAL:
  case ACTION_BOMBARD_LETHAL2:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
  case ACTION_TELEPORT:
  case ACTION_TELEPORT2:
  case ACTION_TELEPORT3:
  case ACTION_TELEPORT_CONQUER:
  case ACTION_TELEPORT_FRIGHTEN:
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
  case ACTION_TELEPORT_ENTER:
  case ACTION_TELEPORT_ENTER_CONQUER:
  case ACTION_SPY_ESCAPE:
  case ACTION_GAIN_VETERANCY:
  case ACTION_ESCAPE:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
    /* Target kind is not ruleset changeable */
    return nullptr;
  case ACTION_NUKE:
    return "explode_nuclear_target_kind";
  case ACTION_NUKE_CITY:
    return "nuke_city_target_kind";
  case ACTION_PILLAGE:
    return "pillage_target_kind";
  case ACTION_PILLAGE2:
    return "pillage_2_target_kind";
  case ACTION_USER_ACTION1:
    return "user_action_1_target_kind";
  case ACTION_USER_ACTION2:
    return "user_action_2_target_kind";
  case ACTION_USER_ACTION3:
    return "user_action_3_target_kind";
  case ACTION_USER_ACTION4:
    return "user_action_4_target_kind";
  case ACTION_COUNT:
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);

  return nullptr;
}

/**********************************************************************//**
  Return actor consuming always ruleset variable name for the action or
  nullptr if actor consuming always can't be set in the ruleset.

  TODO: make actions generic and put actor consuming always in a field of
  the action.
**************************************************************************/
const char *action_actor_consuming_always_ruleset_var_name(action_id act)
{
  switch ((enum gen_action)act) {
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_BRIBE_STACK:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT_RECOVER:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_AIRLIFT:
  case ACTION_ATTACK:
  case ACTION_ATTACK2:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
  case ACTION_WIPE_UNITS:
  case ACTION_COLLECT_RANSOM:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK2:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_BOMBARD4:
  case ACTION_BOMBARD_LETHAL:
  case ACTION_BOMBARD_LETHAL2:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
  case ACTION_TELEPORT:
  case ACTION_TELEPORT2:
  case ACTION_TELEPORT3:
  case ACTION_TELEPORT_CONQUER:
  case ACTION_TELEPORT_FRIGHTEN:
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
  case ACTION_TELEPORT_ENTER:
  case ACTION_TELEPORT_ENTER_CONQUER:
  case ACTION_SPY_ESCAPE:
  case ACTION_GAIN_VETERANCY:
  case ACTION_ESCAPE:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
    /* Actor consuming always is not ruleset changeable */
    return nullptr;
  case ACTION_FOUND_CITY:
    return "found_city_consuming_always";
  case ACTION_NUKE:
    return "explode_nuclear_consuming_always";
  case ACTION_NUKE_CITY:
    return "nuke_city_consuming_always";
  case ACTION_NUKE_UNITS:
    return "nuke_units_consuming_always";
  case ACTION_SPY_SPREAD_PLAGUE:
    return "spread_plague_actor_consuming_always";
  case ACTION_USER_ACTION1:
    return "user_action_1_actor_consuming_always";
  case ACTION_USER_ACTION2:
    return "user_action_2_actor_consuming_always";
  case ACTION_USER_ACTION3:
    return "user_action_3_actor_consuming_always";
  case ACTION_USER_ACTION4:
    return "user_action_4_actor_consuming_always";
  case ACTION_COUNT:
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  fc_assert(act >= 0 && act < ACTION_COUNT);

  return nullptr;
}

/**********************************************************************//**
  Return action blocked by ruleset variable name for the action or
  nullptr if actor consuming always can't be set in the ruleset.

  TODO: make actions generic and put blocked by actions in a field of
  the action.
**************************************************************************/
const char *action_blocked_by_ruleset_var_name(const struct action *act)
{
  fc_assert_ret_val(act != nullptr, nullptr);

  switch ((enum gen_action)action_number(act)) {
  case ACTION_MARKETPLACE:
    return "enter_marketplace_blocked_by";
  case ACTION_BOMBARD:
    return "bombard_blocked_by";
  case ACTION_BOMBARD2:
    return "bombard_2_blocked_by";
  case ACTION_BOMBARD3:
    return "bombard_3_blocked_by";
  case ACTION_BOMBARD4:
    return "bombard_4_blocked_by";
  case ACTION_BOMBARD_LETHAL:
    return "bombard_lethal_blocked_by";
  case ACTION_BOMBARD_LETHAL2:
    return "bombard_lethal_2_blocked_by";
  case ACTION_NUKE:
    return "explode_nuclear_blocked_by";
  case ACTION_NUKE_CITY:
    return "nuke_city_blocked_by";
  case ACTION_NUKE_UNITS:
    return "nuke_units_blocked_by";
  case ACTION_ATTACK:
    return "attack_blocked_by";
  case ACTION_ATTACK2:
    return "attack_2_blocked_by";
  case ACTION_SUICIDE_ATTACK:
    return "suicide_attack_blocked_by";
  case ACTION_SUICIDE_ATTACK2:
    return "suicide_attack_2_blocked_by";
  case ACTION_WIPE_UNITS:
    return "wipe_units_blocked_by";
  case ACTION_COLLECT_RANSOM:
    return "collect_ransom_blocked_by";
  case ACTION_CONQUER_CITY_SHRINK:
    return "conquer_city_shrink_blocked_by";
  case ACTION_CONQUER_CITY_SHRINK2:
    return "conquer_city_shrink_2_blocked_by";
  case ACTION_CONQUER_CITY_SHRINK3:
    return "conquer_city_shrink_3_blocked_by";
  case ACTION_CONQUER_CITY_SHRINK4:
    return "conquer_city_shrink_4_blocked_by";
  case ACTION_UNIT_MOVE:
    return "move_blocked_by";
  case ACTION_UNIT_MOVE2:
    return "move_2_blocked_by";
  case ACTION_UNIT_MOVE3:
    return "move_3_blocked_by";
  case ACTION_TELEPORT:
    return "teleport_blocked_by";
  case ACTION_TELEPORT2:
    return "teleport_2_blocked_by";
  case ACTION_TELEPORT3:
    return "teleport_3_blocked_by";
  case ACTION_TELEPORT_CONQUER:
    return "teleport_conquer_blocked_by";
  case ACTION_TELEPORT_FRIGHTEN:
    return "teleport_frighten_blocked_by";
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
    return "teleport_frighten_conquer_blocked_by";
  case ACTION_TELEPORT_ENTER:
    return "teleport_enter_blocked_by";
  case ACTION_TELEPORT_ENTER_CONQUER:
    return "teleport_enter_conquer_blocked_by";
  case ACTION_SPY_ESCAPE:
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_BRIBE_STACK:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT_RECOVER:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_AIRLIFT:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_GAIN_VETERANCY:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
  case ACTION_ESCAPE:
  case ACTION_USER_ACTION1:
  case ACTION_USER_ACTION2:
  case ACTION_USER_ACTION3:
  case ACTION_USER_ACTION4:
    /* blocked_by is not ruleset changeable */
    return nullptr;
  case ACTION_COUNT:
    fc_assert_ret_val(action_number(act) != ACTION_COUNT, nullptr);
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  return nullptr;
}

/**********************************************************************//**
  Return action post success forced action ruleset variable name for the
  action or nullptr if it can't be set in the ruleset.
**************************************************************************/
const char *
action_post_success_forced_ruleset_var_name(const struct action *act)
{
  fc_assert_ret_val(act != nullptr, nullptr);

  if (!(action_has_result(act, ACTRES_SPY_BRIBE_UNIT)
        || action_has_result(act, ACTRES_SPY_BRIBE_STACK)
        || action_has_result(act, ACTRES_ATTACK)
        || action_has_result(act, ACTRES_WIPE_UNITS)
        || action_has_result(act, ACTRES_COLLECT_RANSOM))) {
    /* No support in the action performer function */
    return nullptr;
  }

  switch ((enum gen_action)action_number(act)) {
  case ACTION_SPY_BRIBE_UNIT:
    return "bribe_unit_post_success_forced_actions";
  case ACTION_SPY_BRIBE_STACK:
    return "bribe_stack_post_success_forced_actions";
  case ACTION_ATTACK:
    return "attack_post_success_forced_actions";
  case ACTION_ATTACK2:
    return "attack_2_post_success_forced_actions";
  case ACTION_WIPE_UNITS:
    return "wipe_units_post_success_forced_actions";
  case ACTION_COLLECT_RANSOM:
    return "collect_ransom_post_success_forced_actions";
  case ACTION_MARKETPLACE:
  case ACTION_BOMBARD:
  case ACTION_BOMBARD2:
  case ACTION_BOMBARD3:
  case ACTION_BOMBARD4:
  case ACTION_BOMBARD_LETHAL:
  case ACTION_BOMBARD_LETHAL2:
  case ACTION_NUKE:
  case ACTION_NUKE_CITY:
  case ACTION_NUKE_UNITS:
  case ACTION_SUICIDE_ATTACK:
  case ACTION_SUICIDE_ATTACK2:
  case ACTION_CONQUER_CITY_SHRINK:
  case ACTION_CONQUER_CITY_SHRINK2:
  case ACTION_CONQUER_CITY_SHRINK3:
  case ACTION_CONQUER_CITY_SHRINK4:
  case ACTION_SPY_POISON:
  case ACTION_SPY_POISON_ESC:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT_ESC:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_ESC:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC:
  case ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_INCITE_CITY_ESC:
  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_ESTABLISH_EMBASSY_STAY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_STEAL_TECH_ESC:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH_ESC:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_INV_CITY_SPEND:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_STEAL_GOLD_ESC:
  case ACTION_STEAL_MAPS:
  case ACTION_STEAL_MAPS_ESC:
  case ACTION_TRADE_ROUTE:
  case ACTION_HELP_WONDER:
  case ACTION_CAPTURE_UNITS:
  case ACTION_EXPEL_UNIT:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_SPY_NUKE:
  case ACTION_SPY_NUKE_ESC:
  case ACTION_DESTROY_CITY:
  case ACTION_DISBAND_UNIT_RECOVER:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_HOMELESS:
  case ACTION_UPGRADE_UNIT:
  case ACTION_PARADROP:
  case ACTION_PARADROP_CONQUER:
  case ACTION_PARADROP_FRIGHTEN:
  case ACTION_PARADROP_FRIGHTEN_CONQUER:
  case ACTION_PARADROP_ENTER:
  case ACTION_PARADROP_ENTER_CONQUER:
  case ACTION_AIRLIFT:
  case ACTION_STRIKE_BUILDING:
  case ACTION_STRIKE_PRODUCTION:
  case ACTION_HEAL_UNIT:
  case ACTION_HEAL_UNIT2:
  case ACTION_TRANSFORM_TERRAIN:
  case ACTION_TRANSFORM_TERRAIN2:
  case ACTION_CULTIVATE:
  case ACTION_CULTIVATE2:
  case ACTION_PLANT:
  case ACTION_PLANT2:
  case ACTION_PILLAGE:
  case ACTION_PILLAGE2:
  case ACTION_CLEAN:
  case ACTION_CLEAN2:
  case ACTION_FORTIFY:
  case ACTION_FORTIFY2:
  case ACTION_ROAD:
  case ACTION_ROAD2:
  case ACTION_CONVERT:
  case ACTION_BASE:
  case ACTION_BASE2:
  case ACTION_MINE:
  case ACTION_MINE2:
  case ACTION_IRRIGATE:
  case ACTION_IRRIGATE2:
  case ACTION_TRANSPORT_DEBOARD:
  case ACTION_TRANSPORT_BOARD:
  case ACTION_TRANSPORT_BOARD2:
  case ACTION_TRANSPORT_BOARD3:
  case ACTION_TRANSPORT_EMBARK:
  case ACTION_TRANSPORT_EMBARK2:
  case ACTION_TRANSPORT_EMBARK3:
  case ACTION_TRANSPORT_EMBARK4:
  case ACTION_TRANSPORT_UNLOAD:
  case ACTION_TRANSPORT_LOAD:
  case ACTION_TRANSPORT_LOAD2:
  case ACTION_TRANSPORT_LOAD3:
  case ACTION_TRANSPORT_DISEMBARK1:
  case ACTION_TRANSPORT_DISEMBARK2:
  case ACTION_TRANSPORT_DISEMBARK3:
  case ACTION_TRANSPORT_DISEMBARK4:
  case ACTION_SPY_SPREAD_PLAGUE:
  case ACTION_SPY_ATTACK:
  case ACTION_CONQUER_EXTRAS:
  case ACTION_CONQUER_EXTRAS2:
  case ACTION_CONQUER_EXTRAS3:
  case ACTION_CONQUER_EXTRAS4:
  case ACTION_HUT_ENTER:
  case ACTION_HUT_ENTER2:
  case ACTION_HUT_ENTER3:
  case ACTION_HUT_ENTER4:
  case ACTION_HUT_FRIGHTEN:
  case ACTION_HUT_FRIGHTEN2:
  case ACTION_HUT_FRIGHTEN3:
  case ACTION_HUT_FRIGHTEN4:
  case ACTION_UNIT_MOVE:
  case ACTION_UNIT_MOVE2:
  case ACTION_UNIT_MOVE3:
  case ACTION_TELEPORT:
  case ACTION_TELEPORT2:
  case ACTION_TELEPORT3:
  case ACTION_TELEPORT_CONQUER:
  case ACTION_TELEPORT_FRIGHTEN:
  case ACTION_TELEPORT_FRIGHTEN_CONQUER:
  case ACTION_TELEPORT_ENTER:
  case ACTION_TELEPORT_ENTER_CONQUER:
  case ACTION_SPY_ESCAPE:
  case ACTION_GAIN_VETERANCY:
  case ACTION_ESCAPE:
  case ACTION_CIVIL_WAR:
  case ACTION_FINISH_UNIT:
  case ACTION_FINISH_BUILDING:
  case ACTION_USER_ACTION1:
  case ACTION_USER_ACTION2:
  case ACTION_USER_ACTION3:
  case ACTION_USER_ACTION4:
    /* Not ruleset changeable */
    return nullptr;
  case ACTION_COUNT:
    fc_assert_ret_val(action_number(act) != ACTION_COUNT, nullptr);
    break;

  ASSERT_UNUSED_ACTION_CASES;
  }

  return nullptr;
}

/**********************************************************************//**
  Is the action ever possible? Currently just checks that there's any
  action enablers for the action.
**************************************************************************/
bool action_ever_possible(action_id action)
{
  return action_enabler_list_size(action_enablers_for_action(action)) > 0;
}

/**********************************************************************//**
  Specenum callback to update old enum names to current ones.
**************************************************************************/
const char *gen_action_name_update_cb(const char *old_name)
{
  if (is_ruleset_compat_mode()) {
  }

  return old_name;
}

const char *atk_helpnames[ATK_COUNT] =
{
  N_("individual cities"), /* ATK_CITY   */
  N_("individual units"),  /* ATK_UNIT   */
  N_("unit stacks"),       /* ATK_STACK  */
  N_("tiles"),             /* ATK_TILE   */
  N_("tile extras"),       /* ATK_EXTRAS */
  N_("itself")             /* ATK_SELF   */
};

/**********************************************************************//**
  Return description of the action target kind suitable to use
  in the helptext.
**************************************************************************/
const char *action_target_kind_help(enum action_target_kind kind)
{
  fc_assert(kind >= 0 && kind < ATK_COUNT);

  return _(atk_helpnames[kind]);
}

/************************************************************************//**
  Returns action list by result.
****************************************************************************/
struct action_list *action_list_by_result(enum action_result result)
{
  fc_assert(result < ACTRES_LAST);

  return actlist_by_result[result];
}

/************************************************************************//**
  Returns action list by activity.
****************************************************************************/
struct action_list *action_list_by_activity(enum unit_activity activity)
{
  fc_assert(activity < ACTIVITY_LAST);

  return actlist_by_activity[activity];
}
