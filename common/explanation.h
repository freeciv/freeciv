/***********************************************************************
 Freeciv - Copyright (C) 2014-2020 - The Freeciv Project contributors.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC_EXPLANATION_H
#define FC_EXPLANATION_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* A category of reasons why an action isn't enabled. */
enum ane_kind {
  /* Explanation: wrong actor unit. */
  ANEK_ACTOR_UNIT,
  /* Explanation: no action target. */
  ANEK_MISSING_TARGET,
  /* Explanation: the action is redundant vs this target. */
  ANEK_BAD_TARGET,
  /* Explanation: bad actor terrain. */
  ANEK_BAD_TERRAIN_ACT,
  /* Explanation: bad target terrain. */
  ANEK_BAD_TERRAIN_TGT,
  /* Explanation: being transported. */
  ANEK_IS_TRANSPORTED,
  /* Explanation: not being transported. */
  ANEK_IS_NOT_TRANSPORTED,
  /* Explanation: transports a cargo unit. */
  ANEK_IS_TRANSPORTING,
  /* Explanation: doesn't transport a cargo unit. */
  ANEK_IS_NOT_TRANSPORTING,
  /* Explanation: actor unit has a home city. */
  ANEK_ACTOR_HAS_HOME_CITY,
  /* Explanation: actor unit has no a home city. */
  ANEK_ACTOR_HAS_NO_HOME_CITY,
  /* Explanation: must declare war first. */
  ANEK_NO_WAR,
  /* Explanation: must break peace first. */
  ANEK_PEACE,
  /* Explanation: can't be done to domestic targets. */
  ANEK_DOMESTIC,
  /* Explanation: can't be done to foreign targets. */
  ANEK_FOREIGN,
  /* Explanation: can't be done with non allied units at target tile. */
  ANEK_TGT_NON_ALLIED_UNITS_ON_TILE,
  /* Explanation: this nation can't act. */
  ANEK_NATION_ACT,
  /* Explanation: this nation can't be targeted. */
  ANEK_NATION_TGT,
  /* Explanation: not enough MP left. */
  ANEK_LOW_MP,
  /* Explanation: can't be done to city centers. */
  ANEK_IS_CITY_CENTER,
  /* Explanation: can't be done to non city centers. */
  ANEK_IS_NOT_CITY_CENTER,
  /* Explanation: can't be done to claimed target tiles. */
  ANEK_TGT_IS_CLAIMED,
  /* Explanation: can't be done to unclaimed target tiles. */
  ANEK_TGT_IS_UNCLAIMED,
  /* Explanation: can't be done because target is too near. */
  ANEK_DISTANCE_NEAR,
  /* Explanation: can't be done because target is too far away. */
  ANEK_DISTANCE_FAR,
  /* Explanation: can't be done to targets that far from the coast line. */
  ANEK_TRIREME_MOVE,
  /* Explanation: can't be done because the actor can't exit its
   * transport. */
  ANEK_DISEMBARK_ACT,
  /* Explanation: actor can't reach unit at target. */
  ANEK_TGT_UNREACHABLE,
  /* Explanation: the action is disabled in this scenario. */
  ANEK_SCENARIO_DISABLED,
  /* Explanation: too close to a city. */
  ANEK_CITY_TOO_CLOSE_TGT,
  /* Explanation: the target city is too big. */
  ANEK_CITY_TOO_BIG,
  /* Explanation: the target city's population limit banned the action. */
  ANEK_CITY_POP_LIMIT,
  /* Explanation: the specified city don't have the needed capacity. */
  ANEK_CITY_NO_CAPACITY,
  /* Explanation: the target unit can't switch sides because it is unique
   * and the actor player already has one. */
  ANEK_TGT_IS_UNIQUE_ACT_HAS,
  /* Explanation: the target tile is unknown. */
  ANEK_TGT_TILE_UNKNOWN,
  /* Explanation: the actor player can't afford performing this action. */
  ANEK_ACT_NOT_ENOUGH_MONEY,
  /* Explanation: the action is blocked by another action. */
  ANEK_ACTION_BLOCKS,
  /* Explanation: the target unit is not wipable. */
  ANEK_NOT_WIPABLE,
  /* Explanation not detected. */
  ANEK_UNKNOWN,
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_EXPLANATION_H */
