/**********************************************************************
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
#define SPECENUM_VALUE0NAME N_("units")
#define SPECENUM_COUNT AAK_COUNT
#include "specenum_gen.h"

/* Used in the network protocol. */
#define SPECENUM_NAME action_target_kind
#define SPECENUM_VALUE0 ATK_CITY
#define SPECENUM_VALUE0NAME N_("cities")
#define SPECENUM_VALUE1 ATK_UNIT
#define SPECENUM_VALUE1NAME N_("units")
#define SPECENUM_COUNT ATK_COUNT
#include "specenum_gen.h"

/* Used in the network protocol. */
#define SPECENUM_NAME gen_action
#define SPECENUM_VALUE0 ACTION_SPY_POISON
#define SPECENUM_VALUE0NAME N_("Poison City")
#define SPECENUM_VALUE1 ACTION_SPY_SABOTAGE_UNIT
#define SPECENUM_VALUE1NAME N_("Sabotage Unit")
#define SPECENUM_VALUE2 ACTION_SPY_BRIBE_UNIT
#define SPECENUM_VALUE2NAME N_("Bribe Unit")
#define SPECENUM_VALUE3 ACTION_SPY_SABOTAGE_CITY
#define SPECENUM_VALUE3NAME N_("Sabotage City")
#define SPECENUM_VALUE4 ACTION_SPY_TARGETED_SABOTAGE_CITY
#define SPECENUM_VALUE4NAME N_("Targeted Sabotage City")
#define SPECENUM_VALUE5 ACTION_SPY_INCITE_CITY
#define SPECENUM_VALUE5NAME N_("Incite City")
#define SPECENUM_VALUE6 ACTION_ESTABLISH_EMBASSY
#define SPECENUM_VALUE6NAME N_("Establish Embassy")
#define SPECENUM_VALUE7 ACTION_SPY_STEAL_TECH
#define SPECENUM_VALUE7NAME N_("Steal Tech")
#define SPECENUM_VALUE8 ACTION_SPY_TARGETED_STEAL_TECH
#define SPECENUM_VALUE8NAME N_("Targeted Steal Tech")
#define SPECENUM_VALUE9 ACTION_SPY_INVESTIGATE_CITY
#define SPECENUM_VALUE9NAME N_("Investigate City")
#define SPECENUM_COUNT ACTION_COUNT
#include "specenum_gen.h"

/* Used to signal that the unit should move in stead of trying to act. */
#define ACTION_MOVE ACTION_COUNT

/*
 * Action probability
 *
 * An action probability is the probability that an action will be
 * successfull under the given cicumstances. It goes from 0% to 100%.
 * Alternatively it can signal a special case.
 *
 * Values from 0 up to and including 200 are to be understood as the chance
 * of success measured in half percentag points. The value 3 indicates that
 * the chance is 1.5%. The value 10 indicates that the chance is 5%. The
 * probability may have been rounded to the nearest half percentage point.
 *
 * Values above 200 (100%) up to and including 255 are special cases. All
 * special cases should be declared and documented below. A value in this
 * range should be considered a bug if it isn't. If a special value for
 * internal use is needed please avoid the range from and including 0 up
 * to and including 255.
 *
 */
typedef int action_probability;

/*
 * ACTPROB_IMPOSSIBLE is another way of saying that the probability is 0%.
 */
#define ACTPROB_IMPOSSIBLE 0

/*
 * The special value ACTPROB_NA indicates that no probability should exist.
 */
#define ACTPROB_NA 253

/*
 * The special value ACTPROB_NOT_IMPLEMENTED indicates that support
 * for finding this probability currently is missing.
 */
#define ACTPROB_NOT_IMPLEMENTED 254

/*
 * The special value ACTPROB_NOT_KNOWN indicates that the player don't know
 * enough to find out. It is caused by the probability depending on a rule
 * that depends on game state the player don't have access to. It may be
 * possible for the player to later gain access to this game state.
 */
#define ACTPROB_NOT_KNOWN 255

struct action
{
  enum gen_action id;
  enum action_actor_kind actor_kind;
  enum action_target_kind target_kind;

  /* The name of the action shown in the UI */
  char ui_name[MAX_LEN_NAME];
};

struct action_enabler
{
  enum gen_action action;
  struct requirement_vector actor_reqs;
  struct requirement_vector target_reqs;
};

#define SPECLIST_TAG action_enabler
#define SPECLIST_TYPE struct action_enabler
#include "speclist.h"
#define action_enabler_list_iterate(action_enabler_list, aenabler) \
  TYPED_LIST_ITERATE(struct action_enabler, action_enabler_list, aenabler)
#define action_enabler_list_iterate_end LIST_ITERATE_END

#define action_iterate(_act_)                          \
{                                                      \
  int _act_;                                           \
  for (_act_ = 0; _act_ < ACTION_COUNT; _act_++) {

#define action_iterate_end                             \
  }                                                    \
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

/* Initialization */
void actions_init(void);
void actions_free(void);

enum action_actor_kind action_get_actor_kind(int action_id);
enum action_target_kind action_get_target_kind(int action_id);

const char *action_get_ui_name(int action_id);
const char *action_prepare_ui_name(int action_id, const char* mnemonic,
                                   const action_probability prob);

struct action_enabler_list *
action_enablers_for_action(enum gen_action action);

struct action_enabler *action_enabler_new(void);
void action_enabler_add(struct action_enabler *enabler);
void action_enabler_append_hard(struct action_enabler *enabler);

bool is_action_enabled_unit_on_city(const enum gen_action wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city);

bool is_action_enabled_unit_on_unit(const enum gen_action wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit);

action_probability action_prob_vs_city(struct unit* actor, int action_id,
                                       struct city* victim);

action_probability action_prob_vs_unit(struct unit* actor, int action_id,
                                       struct unit* victim);

/* Reasoning about actions */
bool action_immune_government(struct government *gov, int act);

bool is_action_possible_on_city(const enum gen_action action_id,
                                const struct player *actor_player,
                                const struct city* target_city);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC_ACTIONS_H */
