/***********************************************************************
 Freeciv - Copyright (C) 2023 The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__ACTRES_H
#define FC__ACTRES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* common */
#include "fc_types.h"
#include "world_object.h"

struct req_context;

/* If 'enum action_result' has currently unused values that should
 * not be used in 'switch - cases', put those cases here. E.g.:
 *
 *#define ASSERT_UNUSED_ACTRES_CASES              \
 *   case ACTRES_UNUSED_1:                        \
 *     fc_assert_msg(FALSE, "ACTRES_UNUSED_1");   \
 *     break;                                     \
  *   case ACTRES_UNUSED_2:                       \
 *     fc_assert_msg(FALSE, "ACTRES_UNUSED_2");   \
 *     break;
 */
#define ASSERT_UNUSED_ACTRES_CASES            \

/* When making changes to this, update also atk_helpnames at actions.c */
#define SPECENUM_NAME action_target_kind
#define SPECENUM_VALUE0 ATK_CITY
#define SPECENUM_VALUE0NAME "City"
#define SPECENUM_VALUE1 ATK_UNIT
#define SPECENUM_VALUE1NAME "Unit"
#define SPECENUM_VALUE2 ATK_STACK
#define SPECENUM_VALUE2NAME "Stack"
#define SPECENUM_VALUE3 ATK_TILE
#define SPECENUM_VALUE3NAME "Tile"
#define SPECENUM_VALUE4 ATK_EXTRAS
#define SPECENUM_VALUE4NAME "Extras"
/* No target except the actor itself. */
#define SPECENUM_VALUE5 ATK_SELF
#define SPECENUM_VALUE5NAME "Self"
#define SPECENUM_COUNT ATK_COUNT
#include "specenum_gen.h"

/* Values used in the network protocol. */
#define SPECENUM_NAME action_sub_target_kind
#define SPECENUM_VALUE0 ASTK_NONE
#define SPECENUM_VALUE0NAME N_("nothing")
#define SPECENUM_VALUE1 ASTK_BUILDING
#define SPECENUM_VALUE1NAME N_("buildings in")
#define SPECENUM_VALUE2 ASTK_TECH
#define SPECENUM_VALUE2NAME N_("techs from")
#define SPECENUM_VALUE3 ASTK_EXTRA
#define SPECENUM_VALUE3NAME N_("extras on")
#define SPECENUM_VALUE4 ASTK_EXTRA_NOT_THERE
#define SPECENUM_VALUE4NAME N_("create extras on")
#define SPECENUM_VALUE5 ASTK_SPECIALIST
#define SPECENUM_VALUE5NAME N_("specialist")
#define SPECENUM_COUNT ASTK_COUNT
#include "specenum_gen.h"

/* Action target complexity */
#define SPECENUM_NAME act_tgt_compl
/* The action's target is just the primary target. (Just the tile, unit,
 * city, etc). */
#define SPECENUM_VALUE0 ACT_TGT_COMPL_SIMPLE
#define SPECENUM_VALUE0NAME N_("simple")
/* The action's target is complex because its target is the primary target
 * and a sub target. (Examples: Tile + Extra and City + Building.) The
 * player is able to specify details about this action but the server will
 * fill in missing details so a client can choose to not specify the sub
 * target. */
#define SPECENUM_VALUE1 ACT_TGT_COMPL_FLEXIBLE
#define SPECENUM_VALUE1NAME N_("flexible")
/* The action's target is complex because its target is the primary target
 * and a sub target. (Examples: Tile + Extra and City + Building.) The
 * player is required to specify details about this action because the
 * server won't fill inn the missing details when unspecified. A client must
 * therefore specify the sub target of this action. */
#define SPECENUM_VALUE2 ACT_TGT_COMPL_MANDATORY
#define SPECENUM_VALUE2NAME N_("mandatory")
#include "specenum_gen.h"

/* A battle is against a defender that tries to stop the action where the
 * defender is in danger. A dice roll without a defender risking anything,
 * like the roll controlled by EFT_ACTION_ODDS_PCT, isn't a battle. */
#define SPECENUM_NAME action_battle_kind
#define SPECENUM_VALUE0 ABK_NONE
#define SPECENUM_VALUE0NAME N_("no battle")
#define SPECENUM_VALUE1 ABK_STANDARD
#define SPECENUM_VALUE1NAME N_("battle")
#define SPECENUM_VALUE2 ABK_DIPLOMATIC
#define SPECENUM_VALUE2NAME N_("diplomatic battle")
#define SPECENUM_COUNT ABK_COUNT
#include "specenum_gen.h"

enum dice_roll_type { DRT_NONE, DRT_DIPLCHANCE, DRT_CERTAIN };

struct actres {
  enum act_tgt_compl sub_tgt_compl;
  enum action_battle_kind battle_kind;
  bool hostile;
  enum unit_activity activity;
  enum dice_roll_type dice;
  enum extra_cause ecause; /* Could deduct this from 'activity', but we should merge
                            * activities completely with actions in the future */
  enum extra_rmcause ermcause;
  enum action_target_kind def_tgt_kind;
};

void actres_init(void);
void actres_free(void);

enum act_tgt_compl actres_target_compl_calc(enum action_result result);
enum action_battle_kind actres_get_battle_kind(enum action_result result);
bool actres_is_hostile(enum action_result result);
enum unit_activity actres_activity_result(enum action_result result);
int actres_get_act_time(enum action_result result,
                        const struct unit *actor_unit,
                        const struct tile *tgt_tile,
                        const struct extra_type *tgt_extra);
enum dice_roll_type actres_dice_type(enum action_result result);
bool actres_creates_extra(enum action_result result,
                          const struct extra_type *pextra);
bool actres_removes_extra(enum action_result result,
                          const struct extra_type *pextra);

enum fc_tristate actres_possible(const struct civ_map *nmap,
                                 enum action_result result,
                                 const struct req_context *actor,
                                 const struct req_context *target,
                                 const struct extra_type *target_extra,
                                 enum fc_tristate def,
                                 bool omniscient,
                                 const struct city *homecity);

int actres_min_range_default(enum action_result result);
int actres_max_range_default(enum action_result result);
bool actres_legal_target_kind(enum action_result result,
                              enum action_target_kind tgt_kind);
enum action_target_kind
actres_target_kind_default(enum action_result result);
enum action_sub_target_kind
actres_sub_target_kind_default(enum action_result result);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__ACTRES_H */
