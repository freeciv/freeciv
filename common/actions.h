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
#include "actres.h"
#include "fc_types.h"
#include "map_types.h"
#include "requirements.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* If 'enum gen_action' has currently unused values that should
 * not be used in 'switch - cases', put those cases here. E.g.:
 *
 *#define ASSERT_UNUSED_ACTION_CASES              \
 *   case ACTION_UNUSED_1:                        \
 *     fc_assert_msg(FALSE, "ACTION_UNUSED_1");   \
 *     break;                                     \
  *   case ACTION_UNUSED_2:                       \
 *     fc_assert_msg(FALSE, "ACTION_UNUSED_2");   \
 *     break;
 */
#define ASSERT_UNUSED_ACTION_CASES            \

/* Used in the network protocol. */
#define SPECENUM_NAME action_actor_kind
#define SPECENUM_VALUE0 AAK_UNIT
#define SPECENUM_VALUE0NAME N_("a unit")
#define SPECENUM_VALUE1 AAK_CITY
#define SPECENUM_VALUE1NAME N_("a city")
#define SPECENUM_VALUE2 AAK_PLAYER
#define SPECENUM_VALUE2NAME N_("a player")
#define SPECENUM_COUNT AAK_COUNT
#include "specenum_gen.h"

const char *gen_action_name_update_cb(const char *old_name);

#include "actions_enums_gen.h"

/* Fake action id used in searches to signal "any action at all". */
#define ACTION_ANY ACTION_COUNT

/* Fake action id used to signal the absence of any actions. */
#define ACTION_NONE ACTION_COUNT

/* Used in the network protocol. */
#define MAX_NUM_ACTIONS ACTION_COUNT
#define NUM_ACTIONS MAX_NUM_ACTIONS

/* Describes how a unit successfully performing an action will move it. */
#define SPECENUM_NAME moves_actor_kind
#define SPECENUM_VALUE0 MAK_STAYS
#define SPECENUM_VALUE0NAME N_("stays")
#define SPECENUM_VALUE1 MAK_REGULAR
#define SPECENUM_VALUE1NAME N_("regular")
#define SPECENUM_VALUE2 MAK_TELEPORT
#define SPECENUM_VALUE2NAME N_("teleport")
#define SPECENUM_VALUE3 MAK_ESCAPE
#define SPECENUM_VALUE3NAME N_("escape")
#define SPECENUM_VALUE4 MAK_FORCED
#define SPECENUM_VALUE4NAME N_("forced")
#define SPECENUM_VALUE5 MAK_UNREPRESENTABLE
#define SPECENUM_VALUE5NAME N_("unrepresentable")
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

struct action
{
  action_id id;
  bool configured;

  enum action_result result;
  bv_action_sub_results sub_results;

  enum action_actor_kind actor_kind;
  enum action_target_kind target_kind;
  enum action_sub_target_kind sub_target_kind;

  /* Sub target policy. */
  enum act_tgt_compl target_complexity;

  /* Limits on the distance on the map between the actor and the target.
   * The action is legal iff the distance is min_distance, max_distance or
   * a value in between. */
  int min_distance, max_distance;

  /* The name of the action shown in the UI */
  char ui_name[MAX_LEN_NAME];

  /* Suppress automatic help text generation about what enables and/or
   * disables this action. */
  bool quiet;

  /* Actions that blocks this action. The action will be illegal if any
   * bloking action is legal. */
  bv_actions blocked_by;

  /* Successfully performing this action will always consume the actor.
   * Don't set this for actions that consumes the unit in some cases
   * (depending on luck, the presence of a flag, etc) but not in other
   * cases. */
  bool actor_consuming_always;

  union {
    struct {
      /* A unit's ability to perform this action will pop up the action
       * selection dialog before the player asks for it only in exceptional
       * cases.
       *
       * The motivation for setting rare_pop_up is to minimize player
       * annoyance and mistakes. Getting a pop up every time a unit moves is
       * annoying. An unexpected offer to do something that in many cases is
       * destructive can lead the player's muscle memory to perform the
       * wrong action. */
      bool rare_pop_up;

      /* The unitwaittime setting blocks this action when done too soon. */
      bool unitwaittime_controlled;

      /* How successfully performing the specified action always will move
       * the actor unit of the specified type. */
      enum moves_actor_kind moves_actor;
    } is_unit;
  } actor;
};

struct action_enabler
{
  action_id action;
  struct requirement_vector actor_reqs;
  struct requirement_vector target_reqs;

  struct {
    /* Only relevant for ruledit and other rulesave users. Indicates that
     * this action enabler is deleted and shouldn't be saved. */
    bool ruledit_disabled;

    char *comment;
  } rulesave;
};

#define action_has_result(_act_, _res_) ((_act_)->result == (_res_))

#define enabler_get_action(_enabler_) action_by_number(_enabler_->action)
#define enabler_get_action_id(_enabler_) (_enabler_->action)

#define SPECLIST_TAG action_enabler
#define SPECLIST_TYPE struct action_enabler
#include "speclist.h"
#define action_enabler_list_iterate(action_enabler_list, aenabler) \
  TYPED_LIST_ITERATE(struct action_enabler, action_enabler_list, aenabler)
#define action_enabler_list_iterate_end LIST_ITERATE_END

#define action_enabler_list_re_iterate(action_enabler_list, aenabler) \
  action_enabler_list_iterate(action_enabler_list, aenabler) {        \
    if (!aenabler->rulesave.ruledit_disabled) {

#define action_enabler_list_re_iterate_end                            \
    }                                                                 \
  } action_enabler_list_iterate_end

#define action_iterate_all(_act_)                                         \
{                                                                         \
  action_id _act_;                                                        \
  for (_act_ = 0; _act_ < NUM_ACTIONS; _act_++) {

#define action_iterate_all_end                                            \
  }                                                                       \
}

/* Filter out unused action slots, in versions where those exist */
#define action_iterate(_act_)                                             \
{                                                                         \
  action_iterate_all(_act_) {

#define action_iterate_end                                                \
  } action_iterate_all_end;                                               \
}

/* Get 'struct action_id_list' and related functions: */
#define SPECLIST_TAG action
#define SPECLIST_TYPE struct action
#include "speclist.h"

#define action_list_iterate(_list_, _act_) \
  TYPED_LIST_ITERATE(struct action, _list_, _act_)
#define action_list_iterate_end LIST_ITERATE_END

struct action_list *action_list_by_result(enum action_result result);
struct action_list *action_list_by_activity(enum unit_activity activity);

/* TODO: Turn this to an iteration over precalculated list */
#define action_noninternal_iterate(_act_)              \
{                                                      \
  action_iterate(_act_) {                              \
    if (!action_id_is_internal(_act_)) {

#define action_noninternal_iterate_end                 \
    }                                                  \
  } action_iterate_end;                                \
}

#define action_by_result_iterate(_paction_, _result_)                     \
{                                                                         \
  action_list_iterate(action_list_by_result(_result_), _paction_) {       \

#define action_by_result_iterate_end                                      \
  } action_list_iterate_end;                                              \
}

#define action_by_activity_iterate(_paction_, _activity_)                 \
{                                                                         \
  action_list_iterate(action_list_by_activity(_activity_), _paction_) {

#define action_by_activity_iterate_end                                    \
  } action_list_iterate_end;                                              \
}

#define action_array_iterate(_act_array_, _act_id_)                         \
{                                                                         \
  int _pos_;                                                              \
                                                                          \
  for (_pos_ = 0; _pos_ < NUM_ACTIONS; _pos_++) {                         \
    const action_id _act_id_ = _act_array_[_pos_];                         \
                                                                          \
    if (_act_id_ == ACTION_NONE) {                                        \
      /* No more actions in this list. */                                 \
      break;                                                              \
    }

#define action_array_iterate_end                              \
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
#define SPECENUM_VALUE0NAME "Unit Upkeep"
/* A unit moved to an adjacent tile (auto attack). */
#define SPECENUM_VALUE1 AAPC_UNIT_MOVED_ADJ
#define SPECENUM_VALUE1NAME "Moved Adjacent"
/* An action was successfully performed and the (action specific) conditions
 * for forcing a post action move are fulfilled. */
#define SPECENUM_VALUE2 AAPC_POST_ACTION
#define SPECENUM_VALUE2NAME "After Successful Action"
/* The city that made the unit's current tile native is gone. Evaluated
 * against an adjacent tile. */
#define SPECENUM_VALUE3 AAPC_CITY_GONE
#define SPECENUM_VALUE3NAME "City Gone"
/* The unit's stack has been defeated and is scheduled for execution but the
 * unit has the CanEscape unit type flag.
 * Evaluated against an adjacent tile. */
#define SPECENUM_VALUE4 AAPC_UNIT_STACK_DEATH
#define SPECENUM_VALUE4NAME "Unit Stack Dead"
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
  action_array_iterate(_autoperf_->alternatives, _act_id_)

#define action_auto_perf_actions_iterate_end                              \
  action_array_iterate_end

/* Hard coded location of action auto performers. Used for conversion while
 * action auto performers aren't directly exposed to the ruleset.
 * Remember to update also MAX_NUM_ACTION_AUTO_PERFORMERS when changing these. */
#define ACTION_AUTO_UPKEEP_FOOD          0
#define ACTION_AUTO_UPKEEP_GOLD          1
#define ACTION_AUTO_UPKEEP_SHIELD        2
#define ACTION_AUTO_MOVED_ADJ            3
#define ACTION_AUTO_POST_BRIBE_UNIT      4
#define ACTION_AUTO_POST_BRIBE_STACK     5
#define ACTION_AUTO_POST_ATTACK          6
#define ACTION_AUTO_POST_ATTACK2         7
#define ACTION_AUTO_POST_COLLECT_RANSOM  8
#define ACTION_AUTO_ESCAPE_CITY          9
#define ACTION_AUTO_ESCAPE_STACK        10
#define ACTION_AUTO_POST_WIPE_UNITS     11

/* Initialization */
void actions_init(void);
void actions_rs_pre_san_gen(void);
void actions_free(void);

bool actions_are_ready(void);

bool action_id_exists(const action_id act_id);

extern struct action **_actions;
/**********************************************************************//**
  Return the action with the given id.

  Returns nullptr if no action with the given id exists.
**************************************************************************/
static inline struct action *action_by_number(action_id act_id)
{
  if (!gen_action_is_valid((enum gen_action)act_id)) {
    return nullptr;
  }

  /* We return nullptr if there's nullptr there, no need to special case it */
  return _actions[act_id];
}

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

int action_number(const struct action *action);

#define action_id(_act_) (_act_->id)

#define action_has_result_safe(paction, result)                           \
  (paction && action_has_result(paction, result))
#define action_id_has_result_safe(act_id, result)                         \
  (action_by_number(act_id)                                               \
   && action_has_result(action_by_number(act_id), result))

bool action_has_complex_target(const struct action *paction);
#define action_id_has_complex_target(act_id)                              \
  action_has_complex_target(action_by_number(act_id))
bool action_requires_details(const struct action *paction);
#define action_id_requires_details(act_id)                                \
  action_requires_details(action_by_number(act_id))

#define action_id_get_act_time(act_id, actor_unit, tgt_tile, tgt_extra)   \
  actres_get_act_time(action_by_number(act_id)->result,                   \
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

int action_get_role(const struct action *paction);
#define action_id_get_role(act_id)                                        \
  action_get_role(action_by_number(act_id))

#define action_get_activity(_pact_)                                       \
  actres_activity_result(_pact_->result)
#define action_id_get_activity(act_id)                                    \
  action_get_activity(action_by_number(act_id))

const char *action_rule_name(const struct action *action);
const char *action_id_rule_name(action_id act_id);

const char *action_name_translation(const struct action *paction);
const char *action_id_name_translation(action_id act_id);
const char *action_get_ui_name_mnemonic(action_id act_id,
                                        const char *mnemonic);
const char *action_prepare_ui_name(action_id act_id, const char *mnemonic,
                                   const struct act_prob prob,
                                   const char *custom);
const char *action_ui_name_default(int act);

const char *action_min_range_ruleset_var_name(int act);
const char *action_max_range_ruleset_var_name(int act);

const char *action_target_kind_ruleset_var_name(int act);
const char *action_target_kind_help(enum action_target_kind kind);

const char *action_actor_consuming_always_ruleset_var_name(action_id act);

const char *action_blocked_by_ruleset_var_name(const struct action *act);

const char *
action_post_success_forced_ruleset_var_name(const struct action *act);

bool action_ever_possible(action_id action);

struct action_enabler_list *
action_enablers_for_action(action_id action);

struct action_enabler *action_enabler_new(void);
void action_enabler_free(struct action_enabler *enabler);
struct action_enabler *
action_enabler_copy(const struct action_enabler *original);
void action_enabler_add(struct action_enabler *enabler);
bool action_enabler_remove(struct action_enabler *enabler);

struct req_vec_problem *
action_enabler_suggest_repair_oblig(const struct action_enabler *enabler);
struct req_vec_problem *
action_enabler_suggest_repair(const struct action_enabler *enabler);
struct req_vec_problem *
action_enabler_suggest_improvement(const struct action_enabler *enabler);

req_vec_num_in_item
action_enabler_vector_number(const void *enabler,
                             const struct requirement_vector *vec);
struct requirement_vector *
action_enabler_vector_by_number(const void *enabler,
                                req_vec_num_in_item vec);
const char *action_enabler_vector_by_number_name(req_vec_num_in_item vec);

bool action_enabler_utype_possible_actor(const struct action_enabler *ae,
                                         const struct unit_type *act_utype);
bool action_enabler_possible_actor(const struct action_enabler *ae);

struct action *action_is_blocked_by(const struct civ_map *nmap,
                                    const struct action *act,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct city *target_city,
                                    const struct unit *target_unit);

bool is_action_enabled_unit_on_city(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city);

bool is_action_enabled_unit_on_unit(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit);

bool is_action_enabled_unit_on_stack(const struct civ_map *nmap,
                                     const action_id wanted_action,
                                     const struct unit *actor_unit,
                                     const struct tile *target_tile);

bool is_action_enabled_unit_on_tile(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile,
                                    const struct extra_type *target_extra);

bool is_action_enabled_unit_on_extras(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct unit *actor_unit,
                                      const struct tile *target,
                                      const struct extra_type *tgt_extra);

bool is_action_enabled_unit_on_self(const struct civ_map *nmap,
                                    const action_id wanted_action,
                                    const struct unit *actor_unit);

bool is_action_enabled_player_on_self(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr);
bool is_action_enabled_player_on_city(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct city *target_city);
bool
is_action_enabled_player_on_extras(const struct civ_map *nmap,
                                   const action_id wanted_action,
                                   const struct player *actor_plr,
                                   const struct tile *target_tile,
                                   const struct extra_type *target_extra);
bool is_action_enabled_player_on_stack(const struct civ_map *nmap,
                                       const action_id wanted_action,
                                       const struct player *actor_plr,
                                       const struct tile *target_tile,
                                       const struct extra_type *target_extra);
bool is_action_enabled_player_on_tile(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct tile *target_tile,
                                      const struct extra_type *target_extra);
bool is_action_enabled_player_on_unit(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct player *actor_plr,
                                      const struct unit *target_unit);

bool is_action_enabled_city(const struct civ_map *nmap,
                            const action_id wanted_action,
                            const struct city *actor_city);

struct act_prob action_prob_vs_city(const struct civ_map *nmap,
                                    const struct unit *actor,
                                    const action_id act_id,
                                    const struct city *victim);

struct act_prob action_prob_vs_unit(const struct civ_map *nmap,
                                    const struct unit *actor,
                                    const action_id act_id,
                                    const struct unit *victim);

struct act_prob action_prob_vs_stack(const struct civ_map *nmap,
                                     const struct unit* actor,
                                     const action_id act_id,
                                     const struct tile* victims);

struct act_prob action_prob_vs_tile(const struct civ_map *nmap,
                                    const struct unit *actor,
                                    const action_id act_id,
                                    const struct tile *victims,
                                    const struct extra_type *target_extra);

struct act_prob action_prob_vs_extras(const struct civ_map *nmap,
                                      const struct unit *actor,
                                      const action_id act_id,
                                      const struct tile *target,
                                      const struct extra_type *tgt_extra);

struct act_prob action_prob_self(const struct civ_map *nmap,
                                 const struct unit *actor,
                                 const action_id act_id);

struct act_prob action_prob_unit_vs_tgt(const struct civ_map *nmap,
                                        const struct action *paction,
                                        const struct unit *act_unit,
                                        const struct city *tgt_city,
                                        const struct unit *tgt_unit,
                                        const struct tile *tgt_tile,
                                        const struct extra_type *sub_tgt);

struct act_prob
action_speculate_unit_on_city(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct city* target);

struct act_prob
action_speculate_unit_on_unit(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct unit *target);

struct act_prob
action_speculate_unit_on_stack(const struct civ_map *nmap,
                               action_id act_id,
                               const struct unit *actor,
                               const struct city *actor_home,
                               const struct tile *actor_tile,
                               bool omniscient_cheat,
                               const struct tile *target);

struct act_prob
action_speculate_unit_on_tile(const struct civ_map *nmap,
                              action_id act_id,
                              const struct unit *actor,
                              const struct city *actor_home,
                              const struct tile *actor_tile,
                              bool omniscient_cheat,
                              const struct tile *target_tile,
                              const struct extra_type *target_extra);

struct act_prob
action_speculate_unit_on_extras(const struct civ_map *nmap,
                                action_id act_id,
                                const struct unit *actor,
                                const struct city *actor_home,
                                const struct tile *actor_tile,
                                bool omniscient_cheat,
                                const struct tile *target_tile,
                                const struct extra_type *target_extra);

struct act_prob
action_speculate_unit_on_self(const struct civ_map *nmap,
                              action_id act_id,
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

struct act_prob action_prob_and(const struct act_prob *ap1,
                                const struct act_prob *ap2);

struct act_prob action_prob_fall_back(const struct act_prob *ap1,
                                      const struct act_prob *ap2);

const char *action_prob_explain(const struct act_prob prob);

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
action_actor_utype_hard_reqs_ok(const struct action *result,
                                const struct unit_type *actor_unittype);

/* Reasoning about actions */
bool action_univs_not_blocking(const struct action *paction,
                               struct universal *actor_uni,
                               struct universal *target_uni);
#define action_id_univs_not_blocking(act_id, act_uni, tgt_uni)            \
  action_univs_not_blocking(action_by_number(act_id), act_uni, tgt_uni)

bool action_immune_government(struct government *gov, action_id act);

bool action_enablers_allow(const action_id wanted_action,
                           const struct req_context *actor,
                           const struct req_context *target);
bool is_action_possible_on_city(action_id act_id,
                                const struct player *actor_player,
                                const struct city *target_city);

bool action_maybe_possible_actor_unit(const struct civ_map *nmap,
                                      const action_id wanted_action,
                                      const struct unit *actor_unit);

bool action_mp_full_makes_legal(const struct unit *actor,
                                const action_id act_id);

bool action_is_in_use(struct action *paction);

bool action_is_internal(struct action *paction);
bool action_id_is_internal(action_id act);

/* Action lists */
void action_array_end(action_id *act_array, int size);
void action_array_add_all_by_result(action_id *act_array,
                                   int *position,
                                   enum action_result result);

/* Action auto performers */
const struct action_auto_perf *action_auto_perf_by_number(const int num);
struct action_auto_perf *action_auto_perf_slot_number(const int num);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_ACTIONS_H */
