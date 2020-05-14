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

#ifndef FC_ACTIONS_H
#define FC_ACTIONS_H

/* common */
#include "fc_types.h"
#include "metaknowledge.h"
#include "requirements.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPECENUM_NAME action_actor_kind
#define SPECENUM_VALUE0 AAK_UNIT
#define SPECENUM_VALUE0NAME N_("a unit")
#define SPECENUM_COUNT AAK_COUNT
#include "specenum_gen.h"

#define SPECENUM_NAME action_target_kind
#define SPECENUM_VALUE0 ATK_CITY
#define SPECENUM_VALUE0NAME N_("individual cities")
#define SPECENUM_VALUE1 ATK_UNIT
#define SPECENUM_VALUE1NAME N_("individual units")
#define SPECENUM_VALUE2 ATK_UNITS
#define SPECENUM_VALUE2NAME N_("unit stacks")
#define SPECENUM_VALUE3 ATK_TILE
#define SPECENUM_VALUE3NAME N_("tiles")
/* No target except the actor itself. */
#define SPECENUM_VALUE4 ATK_SELF
#define SPECENUM_VALUE4NAME N_("itself")
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
#define SPECENUM_COUNT ASTK_COUNT
#include "specenum_gen.h"

/* Values used in the network protocol. */
/* Names used in file formats but not normally shown to users. */
#define SPECENUM_NAME gen_action
#define SPECENUM_VALUE0 ACTION_ESTABLISH_EMBASSY
#define SPECENUM_VALUE0NAME "Establish Embassy"
#define SPECENUM_VALUE1 ACTION_ESTABLISH_EMBASSY_STAY
#define SPECENUM_VALUE1NAME "Establish Embassy Stay"
#define SPECENUM_VALUE2 ACTION_SPY_INVESTIGATE_CITY
#define SPECENUM_VALUE2NAME "Investigate City"
#define SPECENUM_VALUE3 ACTION_INV_CITY_SPEND
#define SPECENUM_VALUE3NAME "Investigate City Spend Unit"
#define SPECENUM_VALUE4 ACTION_SPY_POISON
#define SPECENUM_VALUE4NAME "Poison City"
#define SPECENUM_VALUE5 ACTION_SPY_POISON_ESC
#define SPECENUM_VALUE5NAME "Poison City Escape"
#define SPECENUM_VALUE6 ACTION_SPY_STEAL_GOLD
#define SPECENUM_VALUE6NAME "Steal Gold"
#define SPECENUM_VALUE7 ACTION_SPY_STEAL_GOLD_ESC
#define SPECENUM_VALUE7NAME "Steal Gold Escape"
#define SPECENUM_VALUE8 ACTION_SPY_SABOTAGE_CITY
#define SPECENUM_VALUE8NAME "Sabotage City"
#define SPECENUM_VALUE9 ACTION_SPY_SABOTAGE_CITY_ESC
#define SPECENUM_VALUE9NAME "Sabotage City Escape"
#define SPECENUM_VALUE10 ACTION_SPY_TARGETED_SABOTAGE_CITY
#define SPECENUM_VALUE10NAME "Targeted Sabotage City"
#define SPECENUM_VALUE11 ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC
#define SPECENUM_VALUE11NAME "Targeted Sabotage City Escape"
#define SPECENUM_VALUE12 ACTION_SPY_SABOTAGE_CITY_PRODUCTION
#define SPECENUM_VALUE12NAME "Sabotage City Production"
#define SPECENUM_VALUE13 ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC
#define SPECENUM_VALUE13NAME "Sabotage City Production Escape"
#define SPECENUM_VALUE14 ACTION_SPY_STEAL_TECH
#define SPECENUM_VALUE14NAME "Steal Tech"
#define SPECENUM_VALUE15 ACTION_SPY_STEAL_TECH_ESC
#define SPECENUM_VALUE15NAME "Steal Tech Escape Expected"
#define SPECENUM_VALUE16 ACTION_SPY_TARGETED_STEAL_TECH
#define SPECENUM_VALUE16NAME "Targeted Steal Tech"
#define SPECENUM_VALUE17 ACTION_SPY_TARGETED_STEAL_TECH_ESC
#define SPECENUM_VALUE17NAME "Targeted Steal Tech Escape Expected"
#define SPECENUM_VALUE18 ACTION_SPY_INCITE_CITY
#define SPECENUM_VALUE18NAME "Incite City"
#define SPECENUM_VALUE19 ACTION_SPY_INCITE_CITY_ESC
#define SPECENUM_VALUE19NAME "Incite City Escape"
#define SPECENUM_VALUE20 ACTION_TRADE_ROUTE
#define SPECENUM_VALUE20NAME "Establish Trade Route"
#define SPECENUM_VALUE21 ACTION_MARKETPLACE
#define SPECENUM_VALUE21NAME "Enter Marketplace"
#define SPECENUM_VALUE22 ACTION_HELP_WONDER
#define SPECENUM_VALUE22NAME "Help Wonder"
#define SPECENUM_VALUE23 ACTION_SPY_BRIBE_UNIT
#define SPECENUM_VALUE23NAME "Bribe Unit"
#define SPECENUM_VALUE24 ACTION_SPY_BRIBE_UNITS
#define SPECENUM_VALUE24NAME "Bribe Units"
#define SPECENUM_VALUE25 ACTION_SPY_SABOTAGE_UNIT
#define SPECENUM_VALUE25NAME "Sabotage Unit"
#define SPECENUM_VALUE26 ACTION_SPY_SABOTAGE_UNIT_ESC
#define SPECENUM_VALUE26NAME "Sabotage Unit Escape"
#define SPECENUM_VALUE27 ACTION_CAPTURE_UNITS
#define SPECENUM_VALUE27NAME "Capture Units"
#define SPECENUM_VALUE28 ACTION_FOUND_CITY
#define SPECENUM_VALUE28NAME "Found City"
#define SPECENUM_VALUE29 ACTION_JOIN_CITY
#define SPECENUM_VALUE29NAME "Join City"
#define SPECENUM_VALUE30 ACTION_STEAL_MAPS
#define SPECENUM_VALUE30NAME "Steal Maps"
#define SPECENUM_VALUE31 ACTION_STEAL_MAPS_ESC
#define SPECENUM_VALUE31NAME "Steal Maps Escape"
#define SPECENUM_VALUE32 ACTION_BOMBARD
#define SPECENUM_VALUE32NAME "Bombard"
#define SPECENUM_VALUE33 ACTION_BOMBARD2
#define SPECENUM_VALUE33NAME "Bombard 2"
#define SPECENUM_VALUE34 ACTION_BOMBARD3
#define SPECENUM_VALUE34NAME "Bombard 3"
#define SPECENUM_VALUE35 ACTION_SPY_NUKE
#define SPECENUM_VALUE35NAME "Suitcase Nuke"
#define SPECENUM_VALUE36 ACTION_SPY_NUKE_ESC
#define SPECENUM_VALUE36NAME "Suitcase Nuke Escape"
#define SPECENUM_VALUE37 ACTION_NUKE
#define SPECENUM_VALUE37NAME "Explode Nuclear"
#define SPECENUM_VALUE38 ACTION_NUKE_CITY
#define SPECENUM_VALUE38NAME "Nuke City"
#define SPECENUM_VALUE39 ACTION_NUKE_UNITS
#define SPECENUM_VALUE39NAME "Nuke Units"
#define SPECENUM_VALUE40 ACTION_DESTROY_CITY
#define SPECENUM_VALUE40NAME "Destroy City"
#define SPECENUM_VALUE41 ACTION_EXPEL_UNIT
#define SPECENUM_VALUE41NAME "Expel Unit"
#define SPECENUM_VALUE42 ACTION_RECYCLE_UNIT
#define SPECENUM_VALUE42NAME "Recycle Unit"
#define SPECENUM_VALUE43 ACTION_DISBAND_UNIT
#define SPECENUM_VALUE43NAME "Disband Unit"
#define SPECENUM_VALUE44 ACTION_HOME_CITY
#define SPECENUM_VALUE44NAME "Home City"
#define SPECENUM_VALUE45 ACTION_UPGRADE_UNIT
#define SPECENUM_VALUE45NAME "Upgrade Unit"
#define SPECENUM_VALUE46 ACTION_PARADROP
#define SPECENUM_VALUE46NAME "Paradrop Unit"
#define SPECENUM_VALUE47 ACTION_AIRLIFT
#define SPECENUM_VALUE47NAME "Airlift Unit"
#define SPECENUM_VALUE48 ACTION_ATTACK
#define SPECENUM_VALUE48NAME "Attack"
#define SPECENUM_VALUE49 ACTION_SUICIDE_ATTACK
#define SPECENUM_VALUE49NAME "Suicide Attack"
#define SPECENUM_VALUE50 ACTION_STRIKE_BUILDING
#define SPECENUM_VALUE50NAME "Surgical Strike Building"
#define SPECENUM_VALUE51 ACTION_STRIKE_PRODUCTION
#define SPECENUM_VALUE51NAME "Surgical Strike Production"
#define SPECENUM_VALUE52 ACTION_CONQUER_CITY
#define SPECENUM_VALUE52NAME "Conquer City"
#define SPECENUM_VALUE53 ACTION_CONQUER_CITY2
#define SPECENUM_VALUE53NAME "Conquer City 2"
#define SPECENUM_VALUE54 ACTION_HEAL_UNIT
#define SPECENUM_VALUE54NAME "Heal Unit"
#define SPECENUM_VALUE55 ACTION_TRANSFORM_TERRAIN
#define SPECENUM_VALUE55NAME "Transform Terrain"
#define SPECENUM_VALUE56 ACTION_CULTIVATE
#define SPECENUM_VALUE56NAME "Cultivate"
#define SPECENUM_VALUE57 ACTION_PLANT
#define SPECENUM_VALUE57NAME "Plant"
#define SPECENUM_VALUE58 ACTION_PILLAGE
#define SPECENUM_VALUE58NAME "Pillage"
#define SPECENUM_VALUE59 ACTION_FORTIFY
#define SPECENUM_VALUE59NAME "Fortify"
#define SPECENUM_VALUE60 ACTION_ROAD
#define SPECENUM_VALUE60NAME "Build Road"
#define SPECENUM_VALUE61 ACTION_CONVERT
#define SPECENUM_VALUE61NAME "Convert Unit"
#define SPECENUM_VALUE62 ACTION_BASE
#define SPECENUM_VALUE62NAME "Build Base"
#define SPECENUM_VALUE63 ACTION_MINE
#define SPECENUM_VALUE63NAME "Build Mine"
#define SPECENUM_VALUE64 ACTION_IRRIGATE
#define SPECENUM_VALUE64NAME "Build Irrigation"
#define SPECENUM_VALUE65 ACTION_CLEAN_POLLUTION
#define SPECENUM_VALUE65NAME "Clean Pollution"
#define SPECENUM_VALUE66 ACTION_CLEAN_FALLOUT
#define SPECENUM_VALUE66NAME "Clean Fallout"
#define SPECENUM_VALUE67 ACTION_TRANSPORT_ALIGHT
#define SPECENUM_VALUE67NAME "Transport Alight"
#define SPECENUM_VALUE68 ACTION_TRANSPORT_UNLOAD
#define SPECENUM_VALUE68NAME "Transport Unload"
#define SPECENUM_VALUE69 ACTION_TRANSPORT_DISEMBARK1
#define SPECENUM_VALUE69NAME "Transport Disembark"
#define SPECENUM_VALUE70 ACTION_TRANSPORT_DISEMBARK2
#define SPECENUM_VALUE70NAME "Transport Disembark 2"
#define SPECENUM_VALUE71 ACTION_TRANSPORT_BOARD
#define SPECENUM_VALUE71NAME "Transport Board"
#define SPECENUM_VALUE72 ACTION_TRANSPORT_EMBARK
#define SPECENUM_VALUE72NAME "Transport Embark"
#define SPECENUM_VALUE73 ACTION_SPY_SPREAD_PLAGUE
#define SPECENUM_VALUE73NAME "Spread Plague"
#define SPECENUM_VALUE74 ACTION_SPY_ATTACK
#define SPECENUM_VALUE74NAME "Spy Attack"
#define SPECENUM_VALUE75 ACTION_USER_ACTION1
#define SPECENUM_VALUE75NAME "User Action 1"
#define SPECENUM_VALUE76 ACTION_USER_ACTION2
#define SPECENUM_VALUE76NAME "User Action 2"
#define SPECENUM_VALUE77 ACTION_USER_ACTION3
#define SPECENUM_VALUE77NAME "User Action 3"
#define SPECENUM_BITVECTOR bv_actions
#define SPECENUM_COUNT ACTION_COUNT
#include "specenum_gen.h"

/* Fake action id used in searches to signal "any action at all". */
#define ACTION_ANY ACTION_COUNT

/* Fake action id used to signal the absence of any actions. */
#define ACTION_NONE ACTION_COUNT

/* Used in the network protocol. */
#define MAX_NUM_ACTIONS ACTION_COUNT
#define NUM_ACTIONS MAX_NUM_ACTIONS

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

/* Who ordered the action to be performed? */
#define SPECENUM_NAME action_requester
/* The player ordered it directly. */
#define SPECENUM_VALUE0 ACT_REQ_PLAYER
#define SPECENUM_VALUE0NAME N_("the player")
/* The game it self because the rules requires it. */
#define SPECENUM_VALUE1 ACT_REQ_RULES
#define SPECENUM_VALUE1NAME N_("the game rules")
/* A server side autonomous agent working for the player. */
#define SPECENUM_VALUE2 ACT_REQ_SS_AGENT
#define SPECENUM_VALUE2NAME N_("a server agent")
/* Number of action requesters. */
#define SPECENUM_COUNT ACT_REQ_COUNT
#include "specenum_gen.h"

/* The last action distance value that is interpreted as an actual
 * distance rather than as a signal value.
 *
 * It is specified literally rather than referring to MAP_DISTANCE_MAX
 * because Freeciv-web's MAP_DISTANCE_MAX differs from the regular Freeciv
 * server's MAP_DISTANCE_MAX. A static assertion in actions.c makes sure
 * that it can cover the whole map. */
#define ACTION_DISTANCE_LAST_NON_SIGNAL 128016
/* No action max distance to target limit. */
#define ACTION_DISTANCE_UNLIMITED (ACTION_DISTANCE_LAST_NON_SIGNAL + 1)
/* No action max distance can be bigger than this. */
#define ACTION_DISTANCE_MAX ACTION_DISTANCE_UNLIMITED

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

struct action
{
  action_id id;
  enum action_actor_kind actor_kind;
  enum action_target_kind target_kind;
  enum action_sub_target_kind sub_target_kind;

  bool hostile; /* TODO: Should this be a scale in stead? */

  /* Sub target policy. */
  enum act_tgt_compl target_complexity;

  /* A unit's ability to perform this action will pop up the action
   * selection dialog before the player asks for it only in exceptional
   * cases.
   *
   * The motivation for setting rare_pop_up is to minimize player
   * annoyance and mistakes. Getting a pop up every time a unit moves is
   * annoying. An unexpected offer to do something that in many cases is
   * destructive can lead the player's muscle memory to perform the wrong
   * action. */
  bool rare_pop_up;

  /* Limits on the distance on the map between the actor and the target.
   * The action is legal iff the distance is min_distance, max_distance or
   * a value in between. */
  int min_distance, max_distance;

  /* The name of the action shown in the UI */
  char ui_name[MAX_LEN_NAME];

  /* Suppress automatic help text generation about what enables and/or
   * disables this action. */
  bool quiet;

  /* The unitwaittime setting blocks this action when done too soon. */
  bool unitwaittime_controlled;

  /* Actions that blocks this action. The action will be illegal if any
   * bloking action is legal. */
  bv_actions blocked_by;

  /* Successfully performing this action will always consume the actor.
   * Don't set this for actions that consumes the unit in some cases
   * (depending on luck, the presence of a flag, etc) but not in other
   * cases. */
  bool actor_consuming_always;
};

struct action_enabler
{
  bool disabled;
  action_id action;
  struct requirement_vector actor_reqs;
  struct requirement_vector target_reqs;
};

#define enabler_get_action(_enabler_) action_by_number(_enabler_->action)

#define SPECLIST_TAG action_enabler
#define SPECLIST_TYPE struct action_enabler
#include "speclist.h"
#define action_enabler_list_iterate(action_enabler_list, aenabler) \
  TYPED_LIST_ITERATE(struct action_enabler, action_enabler_list, aenabler)
#define action_enabler_list_iterate_end LIST_ITERATE_END

#define action_iterate(_act_)                                             \
{                                                                         \
  action_id _act_;                                                        \
  for (_act_ = 0; _act_ < NUM_ACTIONS; _act_++) {

#define action_iterate_end                             \
  }                                                    \
}

#define action_list_iterate(_act_list_, _act_id_)                         \
{                                                                         \
  int _pos_;                                                              \
                                                                          \
  for (_pos_ = 0; _pos_ < NUM_ACTIONS; _pos_++) {                         \
    const action_id _act_id_ = _act_list_[_pos_];                         \
                                                                          \
    if (_act_id_ == ACTION_NONE) {                                        \
      /* No more actions in this list. */                                 \
      break;                                                              \
    }

#define action_list_iterate_end                              \
  }                                                                       \
}

#define action_enablers_iterate(_enabler_)               \
{                                                        \
  action_iterate(_act_) {                                \
    action_enabler_list_iterate(                         \
      action_enablers_for_action(_act_), _enabler_) {

#define action_enablers_iterate_end                      \
    } action_enabler_list_iterate_end;                   \
  } action_iterate_end;                                  \
}

/* The reason why an action should be auto performed. */
#define SPECENUM_NAME action_auto_perf_cause
/* Can't pay the unit's upkeep. */
/* (Can be triggered by food, shield or gold upkeep) */
#define SPECENUM_VALUE0 AAPC_UNIT_UPKEEP
#define SPECENUM_VALUE0NAME N_("Unit Upkeep")
/* A unit moved to an adjacent tile (auto attack). */
#define SPECENUM_VALUE1 AAPC_UNIT_MOVED_ADJ
#define SPECENUM_VALUE1NAME N_("Moved Adjacent")
/* Number of forced action auto performer causes. */
#define SPECENUM_COUNT AAPC_COUNT
#include "specenum_gen.h"

/* An Action Auto Performer rule makes an actor try to perform an action
 * without being ordered to do so by the player controlling it.
 * - the first auto performer that matches the cause and fulfills the reqs
 *   is selected.
 * - the actions listed by the selected auto performer is tried in order
 *   until an action is successful, all actions have been tried or the
 *   actor disappears.
 * - if no action inside the selected auto performer is legal no action is
 *   performed. The system won't try to select another auto performer.
 */
struct action_auto_perf
{
  /* The reason for trying to auto perform an action. */
  enum action_auto_perf_cause cause;

  /* Must be fulfilled if the game should try to force an action from this
   * action auto performer. */
  struct requirement_vector reqs;

  /* Auto perform the first legal action in this list.
   * The list is terminated by ACTION_NONE. */
  action_id alternatives[MAX_NUM_ACTIONS];
};

#define action_auto_perf_iterate(_act_perf_)                              \
{                                                                         \
  int _ap_num_;                                                           \
                                                                          \
  for (_ap_num_ = 0;                                                      \
       _ap_num_ < MAX_NUM_ACTION_AUTO_PERFORMERS                          \
       && (action_auto_perf_by_number(_ap_num_)->cause                    \
           != AAPC_COUNT);                                                \
       _ap_num_++) {                                                      \
    const struct action_auto_perf *_act_perf_                             \
              = action_auto_perf_by_number(_ap_num_);

#define action_auto_perf_iterate_end                                      \
  }                                                                       \
}

#define action_auto_perf_by_cause_iterate(_cause_, _act_perf_)            \
action_auto_perf_iterate(_act_perf_) {                                    \
  if (_act_perf_->cause != _cause_) {                                     \
    continue;                                                             \
  }

#define action_auto_perf_by_cause_iterate_end                             \
} action_auto_perf_iterate_end

#define action_auto_perf_actions_iterate(_autoperf_, _act_id_)            \
  action_list_iterate(_autoperf_->alternatives, _act_id_)

#define action_auto_perf_actions_iterate_end                              \
  action_list_iterate_end

/* Hard coded location of action auto performers. Used for conversion while
 * action auto performers aren't directly exposed to the ruleset. */
#define ACTION_AUTO_UPKEEP_FOOD   0
#define ACTION_AUTO_UPKEEP_GOLD   1
#define ACTION_AUTO_UPKEEP_SHIELD 2
#define ACTION_AUTO_MOVED_ADJ     3

/* Initialization */
void actions_init(void);
void actions_rs_pre_san_gen(void);
void actions_free(void);

bool actions_are_ready(void);

bool action_id_exists(const action_id act_id);

struct action *action_by_number(action_id act_id);
struct action *action_by_rule_name(const char *name);

enum action_actor_kind action_get_actor_kind(const struct action *paction);
#define action_id_get_actor_kind(act_id)                                  \
  action_get_actor_kind(action_by_number(act_id))
enum action_target_kind action_get_target_kind(
    const struct action *paction);
#define action_id_get_target_kind(act_id)                                 \
  action_get_target_kind(action_by_number(act_id))
enum action_sub_target_kind action_get_sub_target_kind(
    const struct action *paction);
#define action_id_get_sub_target_kind(act_id)                             \
  action_get_sub_target_kind(action_by_number(act_id))

enum action_battle_kind action_get_battle_kind(const struct action *pact);

int action_number(const struct action *action);

bool action_has_result(const struct action *paction,
                       enum gen_action result);
#define action_has_result_safe(paction, result)                           \
  (paction && action_has_result(paction, result))
#define action_id_has_result_safe(act_id, result)                         \
  (action_by_number(act_id)                                               \
   && action_has_result(action_by_number(act_id), result))

bool action_is_hostile(action_id act_id);

bool action_id_has_complex_target(action_id act_id);
bool action_requires_details(action_id act_id);

int action_get_act_time(const struct action *paction,
                        const struct unit *actor_unit,
                        const struct tile *tgt_tile,
                        struct extra_type *tgt_extra);
#define action_id_get_act_time(act_id, actor_unit, tgt_tile, tgt_extra)    \
  action_get_act_time(action_by_number(act_id),                            \
                      actor_unit, tgt_tile, tgt_extra)

bool action_id_is_rare_pop_up(action_id act_id);

bool action_distance_accepted(const struct action *action,
                              const int distance);
#define action_id_distance_accepted(act_id, distance)                     \
  action_distance_accepted(action_by_number(act_id), distance)

bool action_distance_inside_max(const struct action *action,
                                const int distance);
#define action_id_distance_inside_max(act_id, distance)                   \
  action_distance_inside_max(action_by_number(act_id), distance)

bool action_would_be_blocked_by(const struct action *blocked,
                                const struct action *blocker);
#define action_id_would_be_blocked_by(blocked_id, blocker_id)             \
  action_would_be_blocked_by(action_by_number(blocked_id),                \
                             action_by_number(blocker_id))

int action_get_role(const struct action *paction);
#define action_id_get_role(act_id)                                        \
  action_get_role(action_by_number(act_id))

enum unit_activity action_get_activity(const struct action *paction);
#define action_id_get_activity(act_id)                                    \
  action_get_activity(action_by_number(act_id))

const char *action_rule_name(const struct action *action);
const char *action_id_rule_name(action_id act_id);

const char *action_name_translation(const struct action *action);
const char *action_id_name_translation(action_id act_id);
const char *action_get_ui_name_mnemonic(action_id act_id,
                                        const char* mnemonic);
const char *action_prepare_ui_name(action_id act_id, const char* mnemonic,
                                   const struct act_prob prob,
                                   const char *custom);
const char *action_get_tool_tip(const action_id act_id,
                                const struct act_prob prob);

const char *action_ui_name_ruleset_var_name(int act);
const char *action_ui_name_default(int act);

const char *action_min_range_ruleset_var_name(int act);
int action_min_range_default(int act);
const char *action_max_range_ruleset_var_name(int act);
int action_max_range_default(int act);

const char *action_target_kind_ruleset_var_name(int act);
const char *action_actor_consuming_always_ruleset_var_name(action_id act);

struct action_enabler_list *
action_enablers_for_action(action_id action);

struct action_enabler *action_enabler_new(void);
void action_enabler_close(struct action_enabler *enabler);
struct action_enabler *
action_enabler_copy(const struct action_enabler *original);
void action_enabler_add(struct action_enabler *enabler);
bool action_enabler_remove(struct action_enabler *enabler);

const char *
action_enabler_obligatory_reqs_missing(struct action_enabler *enabler);
bool action_enabler_obligatory_reqs_add(struct action_enabler *enabler);

bool univs_have_action_enabler(action_id action,
                               struct universal *actor_uni,
                               struct universal *target_uni);

struct action *action_is_blocked_by(const action_id act_id,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct city *target_city,
                                    const struct unit *target_unit);

bool is_action_enabled_unit_on_city(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city);

bool is_action_enabled_unit_on_unit(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit);

bool is_action_enabled_unit_on_units(const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct tile *target_tile);

bool is_action_enabled_unit_on_tile(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra);

bool is_action_enabled_unit_on_self(const action_id wanted_action,
                                    const struct unit *actor_unit);

struct act_prob action_prob_vs_city(const struct unit* actor,
                                    const action_id act_id,
                                    const struct city* victim);

struct act_prob action_prob_vs_unit(const struct unit* actor,
                                    const action_id act_id,
                                    const struct unit* victim);

struct act_prob action_prob_vs_units(const struct unit* actor,
                                     const action_id act_id,
                                     const struct tile* victims);

struct act_prob action_prob_vs_tile(const struct unit *actor,
                                    const action_id act_id,
                                    const struct tile *victims,
                                    const struct extra_type *target_extra);

struct act_prob action_prob_self(const struct unit *actor,
                                 const action_id act_id);

struct act_prob
action_speculate_unit_on_city(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct city* target);

struct act_prob
action_speculate_unit_on_unit(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct unit *target);

struct act_prob
action_speculate_unit_on_units(action_id act_id,
                               const struct unit *actor,
                               const struct city *actor_home,
                               const struct tile *actor_tile,
                               bool omniscient_cheat,
                               const struct tile *target);

struct act_prob
action_speculate_unit_on_tile(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct tile *target_tile,
                              const struct extra_type *target_extra);

struct act_prob
action_speculate_unit_on_self(action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat);

bool action_prob_possible(const struct act_prob probability);

bool action_prob_certain(const struct act_prob probability);

bool are_action_probabilitys_equal(const struct act_prob *ap1,
                                   const struct act_prob *ap2);

int action_prob_cmp_pessimist(const struct act_prob ap1,
                              const struct act_prob ap2);

double action_prob_to_0_to_1_pessimist(const struct act_prob ap);

struct act_prob action_prob_fall_back(const struct act_prob *ap1,
                                      const struct act_prob *ap2);

struct act_prob action_prob_new_impossible(void);
struct act_prob action_prob_new_not_relevant(void);
struct act_prob action_prob_new_not_impl(void);
struct act_prob action_prob_new_unknown(void);
struct act_prob action_prob_new_certain(void);

/* Special action probability values. Documented in fc_types.h's
 * definition of struct act_prob. */
#define ACTPROB_IMPOSSIBLE action_prob_new_impossible()
#define ACTPROB_CERTAIN action_prob_new_certain()
#define ACTPROB_NA action_prob_new_not_relevant()
#define ACTPROB_NOT_IMPLEMENTED action_prob_new_not_impl()
#define ACTPROB_NOT_KNOWN action_prob_new_unknown()

/* ACTION_ODDS_PCT_DICE_ROLL_NA must be above 100%. */
#define ACTION_ODDS_PCT_DICE_ROLL_NA 110
int action_dice_roll_initial_odds(const struct action *paction);
int action_dice_roll_odds(const struct player *act_player,
                          const struct unit *act_unit,
                          const struct city *tgt_city,
                          const struct player *tgt_player,
                          const struct action *paction);

bool
action_actor_utype_hard_reqs_ok(const action_id wanted_action,
                                const struct unit_type *actor_unittype);

/* Reasoning about actions */
bool action_immune_government(struct government *gov, action_id act);

bool action_blocked_by_situation_act(const struct action *paction,
                                     const struct requirement *situation);
#define action_id_blocked_by_situation_act(act_id, situation)             \
  action_blocked_by_situation_act(action_by_number(act_id), situation)

bool action_blocked_by_situation_tgt(const struct action *paction,
                                     const struct requirement *situation);
#define action_id_blocked_by_situation_tgt(act_id, situation)             \
  action_blocked_by_situation_tgt(action_by_number(act_id), situation)

bool is_action_possible_on_city(action_id act_id,
                                const struct player *actor_player,
                                const struct city* target_city);

bool action_maybe_possible_actor_unit(const action_id wanted_action,
                                      const struct unit* actor_unit);

bool action_mp_full_makes_legal(const struct unit *actor,
                                const action_id act_id);

/* Action auto performers */
const struct action_auto_perf *action_auto_perf_by_number(const int num);
struct action_auto_perf *action_auto_perf_slot_number(const int num);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_ACTIONS_H */
