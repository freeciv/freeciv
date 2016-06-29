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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <math.h> /* ceil */

/* utility */
#include "astring.h"

/* common */
#include "actions.h"
#include "city.h"
#include "combat.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "unit.h"
#include "research.h"
#include "tile.h"

static struct action *actions[ACTION_COUNT];
struct action_auto_perf auto_perfs[MAX_NUM_ACTION_AUTO_PERFORMERS];
static bool actions_initialized = FALSE;

static struct action_enabler_list *action_enablers_by_action[ACTION_COUNT];

static struct action *action_new(enum gen_action id,
                                 enum action_target_kind target_kind,
                                 bool hostile, bool requires_details,
                                 bool rare_pop_up,
                                 const int min_distance,
                                 const int max_distance);

static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
                              const struct unit *actor_unit,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
                              const struct unit *target_unit,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist);

static inline bool action_prob_is_signal(action_probability probability);
static inline bool action_prob_not_relevant(action_probability probability);
static inline bool action_prob_unknown(action_probability probability);
static inline bool action_prob_not_impl(action_probability probability);

/**************************************************************************
  Initialize the actions and the action enablers.
**************************************************************************/
void actions_init(void)
{
  int i, j;

  /* Hard code the actions */
  actions[ACTION_SPY_POISON] = action_new(ACTION_SPY_POISON, ATK_CITY,
                                          TRUE, FALSE, FALSE,
                                          0, 1);
  actions[ACTION_SPY_SABOTAGE_UNIT] =
      action_new(ACTION_SPY_SABOTAGE_UNIT, ATK_UNIT,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_BRIBE_UNIT] =
      action_new(ACTION_SPY_BRIBE_UNIT, ATK_UNIT,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_SABOTAGE_CITY] =
      action_new(ACTION_SPY_SABOTAGE_CITY, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY, ATK_CITY,
                 TRUE, TRUE, FALSE,
                 0, 1);
  actions[ACTION_SPY_INCITE_CITY] =
      action_new(ACTION_SPY_INCITE_CITY, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_ESTABLISH_EMBASSY] =
      action_new(ACTION_ESTABLISH_EMBASSY, ATK_CITY,
                 FALSE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_STEAL_TECH] =
      action_new(ACTION_SPY_STEAL_TECH, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_TARGETED_STEAL_TECH] =
      action_new(ACTION_SPY_TARGETED_STEAL_TECH, ATK_CITY,
                 TRUE, TRUE, FALSE,
                 0, 1);
  actions[ACTION_SPY_INVESTIGATE_CITY] =
      action_new(ACTION_SPY_INVESTIGATE_CITY, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_SPY_STEAL_GOLD] =
      action_new(ACTION_SPY_STEAL_GOLD, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_TRADE_ROUTE] =
      action_new(ACTION_TRADE_ROUTE, ATK_CITY,
                 FALSE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_MARKETPLACE] =
      action_new(ACTION_MARKETPLACE, ATK_CITY,
                 FALSE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_HELP_WONDER] =
      action_new(ACTION_HELP_WONDER, ATK_CITY,
                 FALSE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_CAPTURE_UNITS] =
      action_new(ACTION_CAPTURE_UNITS, ATK_UNITS,
                 TRUE, FALSE, FALSE,
                 /* A single domestic unit at the target tile will make the
                  * action illegal. It must therefore be performed from
                  * another tile. */
                 1, 1);
  actions[ACTION_FOUND_CITY] =
      action_new(ACTION_FOUND_CITY, ATK_TILE,
                 FALSE, FALSE, TRUE,
                 /* Illegal to perform to a target on another tile.
                  * Reason: The Freeciv code assumes that the city founding
                  * unit is located at the tile were the new city is
                  * founded. */
                 0, 0);
  actions[ACTION_JOIN_CITY] =
      action_new(ACTION_JOIN_CITY, ATK_CITY,
                 FALSE, FALSE, TRUE,
                 0, 1);
  actions[ACTION_STEAL_MAPS] =
      action_new(ACTION_STEAL_MAPS, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_BOMBARD] =
      action_new(ACTION_BOMBARD,
                 /* FIXME: Target is actually Units + City */
                 ATK_UNITS,
                 TRUE, FALSE, FALSE,
                 /* A single domestic unit at the target tile will make the
                  * action illegal. It must therefore be performed from
                  * another tile. */
                 1, 1);
  actions[ACTION_SPY_NUKE] =
      action_new(ACTION_SPY_NUKE, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_NUKE] =
      action_new(ACTION_NUKE,
                 /* FIXME: Target is actually Tile + Units + City */
                 ATK_TILE,
                 TRUE, FALSE, TRUE,
                 0, 1);
  actions[ACTION_DESTROY_CITY] =
      action_new(ACTION_DESTROY_CITY, ATK_CITY,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_EXPEL_UNIT] =
      action_new(ACTION_EXPEL_UNIT, ATK_UNIT,
                 TRUE, FALSE, FALSE,
                 0, 1);
  actions[ACTION_RECYCLE_UNIT] =
      action_new(ACTION_RECYCLE_UNIT, ATK_CITY,
                 FALSE, FALSE, TRUE,
                 /* Illegal to perform to a target on another tile to
                  * keep the rules exactly as they were for now. */
                 0, 0);
  actions[ACTION_DISBAND_UNIT] =
      action_new(ACTION_DISBAND_UNIT, ATK_SELF,
                 FALSE, FALSE, TRUE,
                 0, 0);
  actions[ACTION_HOME_CITY] =
      action_new(ACTION_HOME_CITY, ATK_CITY,
                 FALSE, FALSE, TRUE,
                 /* Illegal to perform to a target on another tile to
                  * keep the rules exactly as they were for now. */
                 0, 0);
  actions[ACTION_UPGRADE_UNIT] =
      action_new(ACTION_UPGRADE_UNIT, ATK_CITY,
                 FALSE, FALSE, TRUE,
                 /* Illegal to perform to a target on another tile to
                  * keep the rules exactly as they were for now. */
                 0, 0);
  actions[ACTION_PARADROP] =
      action_new(ACTION_PARADROP, ATK_TILE,
                 FALSE, FALSE, TRUE,
                 0, UNIT_MAX_PARADROP_RANGE);
  actions[ACTION_AIRLIFT] =
      action_new(ACTION_AIRLIFT, ATK_CITY,
                 FALSE, FALSE, TRUE,
                 1, MAP_MAX_LINEAR_SIZE);
  actions[ACTION_ATTACK] =
      action_new(ACTION_ATTACK,
                 /* FIXME: Target is actually City and, depending on the
                  * unreachable_protects setting, each unit at the target
                  * tile (Units) or any unit at the target tile. */
                 ATK_TILE,
                 TRUE, FALSE, FALSE,
                 1, 1);

  /* Initialize the action enabler list */
  action_iterate(act) {
    action_enablers_by_action[act] = action_enabler_list_new();
  } action_iterate_end;

  /* Initialize the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    /* Nothing here. Nothing after this point. */
    auto_perfs[i].cause = AAPC_COUNT;

    /* The criteria to pick *this* auto performer for its cause. */
    requirement_vector_init(&auto_perfs[i].reqs);

    for (j = 0; j < ACTION_COUNT; j++) {
      /* Nothing here. Nothing after this point. */
      auto_perfs[i].alternatives[j] = ACTION_COUNT;
    }
  }

  /* The actions them self are now initialized. */
  actions_initialized = TRUE;
}

/**************************************************************************
  Free the actions and the action enablers.
**************************************************************************/
void actions_free(void)
{
  int i;

  /* Don't consider the actions to be initialized any longer. */
  actions_initialized = FALSE;

  action_iterate(act) {
    action_enabler_list_iterate(action_enablers_by_action[act], enabler) {
      requirement_vector_free(&enabler->actor_reqs);
      requirement_vector_free(&enabler->target_reqs);
      free(enabler);
    } action_enabler_list_iterate_end;

    action_enabler_list_destroy(action_enablers_by_action[act]);

    free(actions[act]);
  } action_iterate_end;

  /* Free the action auto performers. */
  for (i = 0; i < MAX_NUM_ACTION_AUTO_PERFORMERS; i++) {
    requirement_vector_free(&auto_perfs[i].reqs);
  }
}

/**************************************************************************
  Returns TRUE iff the actions are initialized.

  Doesn't care about action enablers.
**************************************************************************/
bool actions_are_ready(void)
{
  if (!actions_initialized) {
    /* The actions them self aren't initialized yet. */
    return FALSE;
  }

  action_iterate(act) {
    if (actions[act]->ui_name[0] == '\0') {
      /* An action without a UI name exists means that the ruleset haven't
       * loaded yet. The ruleset loading will assign a default name to
       * any actions not named by the ruleset. The client will get this
       * name from the server. */
      return FALSE;
    }
  } action_iterate_end;

  /* The actions should be ready for use. */
  return TRUE;
}

/**************************************************************************
  Create a new action.
**************************************************************************/
static struct action *action_new(enum gen_action id,
                                 enum action_target_kind target_kind,
                                 bool hostile, bool requires_details,
                                 bool rare_pop_up,
                                 const int min_distance,
                                 const int max_distance)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  action->id = id;
  action->actor_kind = AAK_UNIT;
  action->target_kind = target_kind;

  action->hostile = hostile;
  action->requires_details = requires_details;
  action->rare_pop_up = rare_pop_up;

  /* The distance between the actor and itself is always 0. */
  fc_assert(target_kind != ATK_SELF
            || (min_distance == 0 && max_distance == 0));

  action->min_distance = min_distance;
  action->max_distance = max_distance;

  /* Loaded from the ruleset. Until generalized actions are ready it has to
   * be defined seperatly from other action data. */
  action->ui_name[0] = '\0';
  action->quiet = FALSE;
  BV_CLR_ALL(action->blocked_by);

  return action;
}

/**************************************************************************
  Returns TRUE iff the specified action ID refers to a valid action.
**************************************************************************/
bool action_id_is_valid(const int action_id)
{
  return gen_action_is_valid(action_id);
}

/**************************************************************************
  Return the action with the given id.

  Returns NULL if no action with the given id exists.
**************************************************************************/
struct action *action_by_number(int action_id)
{
  if (!action_id_is_valid(action_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action numbered %d", action_id);

    return NULL;
  }

  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id];
}

/**************************************************************************
  Return the action with the given name.

  Returns NULL if no action with the given name exists.
**************************************************************************/
struct action *action_by_rule_name(const char *name)
{
  /* Actions are still hard coded in the gen_action enum. */
  int action_id = gen_action_by_name(name, fc_strcasecmp);

  if (!action_id_is_valid(action_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action named %s", name);

    return NULL;
  }

  return action_by_number(action_id);
}

/**************************************************************************
  Get the actor kind of an action.
**************************************************************************/
enum action_actor_kind action_get_actor_kind(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id]->actor_kind;
}

/**************************************************************************
  Get the target kind of an action.
**************************************************************************/
enum action_target_kind action_get_target_kind(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id]->target_kind;
}

/**************************************************************************
  Returns TRUE iff the specified action is hostile.
**************************************************************************/
bool action_is_hostile(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id]->hostile;
}

/**************************************************************************
  Returns TRUE iff the specified action REQUIRES the player to provide
  details in addition to actor and target. Returns FALSE if the action
  doesn't support any additional details or if they can be set by Freeciv
  it self.
**************************************************************************/
bool action_requires_details(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id]->requires_details;
}

/**************************************************************************
  Returns TRUE iff a unit's ability to perform this action will pop up the
  action selection dialog before the player asks for it only in exceptional
  cases.

  An example of an exceptional case is when the player tries to move a
  unit to a tile it can't move to but can perform this action to.
**************************************************************************/
bool action_id_is_rare_pop_up(int action_id)
{
  fc_assert_ret_val_msg((action_id_is_valid(action_id)
                         && actions[action_id]),
                        FALSE, "Action %d don't exist.", action_id);

  return actions[action_id]->rare_pop_up;
}

/**************************************************************************
  Returns TRUE iff the specified distance between actor and target is
  within the range acceptable to the specified action.
**************************************************************************/
bool action_distance_accepted(const struct action *action,
                              const int distance)
{
  fc_assert_ret_val(action, FALSE);

  return (distance >= action->min_distance
          && distance <= action->max_distance);
}

/**************************************************************************
  Returns TRUE iff blocked will be illegal if blocker is legal.
**************************************************************************/
bool action_would_be_blocked_by(const struct action *blocked,
                                const struct action *blocker)
{
  fc_assert_ret_val(blocked, FALSE);
  fc_assert_ret_val(blocker, FALSE);

  return BV_ISSET(blocked->blocked_by, action_number(blocker));
}

/**************************************************************************
  Get the universal number of the action.
**************************************************************************/
int action_number(const struct action *action)
{
  return action->id;
}

/**************************************************************************
  Get the rule name of the action.
**************************************************************************/
const char *action_rule_name(const struct action *action)
{
  /* Rule name is still hard coded. */
  return action_get_rule_name(action->id);
}

/**************************************************************************
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_name_translation(const struct action *action)
{
  /* Use action_get_ui_name() to format the UI name. */
  return action_get_ui_name(action->id);
}

/**************************************************************************
  Get the rule name of the action.
**************************************************************************/
const char *action_get_rule_name(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return gen_action_name(action_id);
}

/**************************************************************************
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_get_ui_name(int action_id)
{
  return action_prepare_ui_name(action_id, "", ACTPROB_NA, NULL);
}

/**************************************************************************
  Get the action name with a mnemonic ready to display in the UI.
**************************************************************************/
const char *action_get_ui_name_mnemonic(int action_id,
                                        const char* mnemonic)
{
  return action_prepare_ui_name(action_id, mnemonic, ACTPROB_NA, NULL);
}

/**************************************************************************
  Get the UI name ready to show the action in the UI. It is possible to
  add a client specific mnemonic. Success probability information is
  interpreted and added to the text. A custom text can be inserted before
  the probability information.
**************************************************************************/
const char *action_prepare_ui_name(int action_id, const char* mnemonic,
                                   const action_probability prob,
                                   const char* custom)
{
  static struct astring str = ASTRING_INIT;
  static struct astring chance = ASTRING_INIT;

  /* Text representation of the probability. */
  const char* probtxt;

  if (!actions_are_ready()) {
    /* Could be a client who haven't gotten the ruleset yet */

    /* so there shouldn't be any action probability to show */
    fc_assert(action_prob_not_relevant(prob));

    /* but the action should be valid */
    fc_assert_ret_val_msg(action_id_is_valid(action_id),
                          "Invalid action",
                          "Invalid action %d", action_id);

    /* and no custom text will be inserted */
    fc_assert(custom == NULL || custom[0] == '\0');

    /* Make the best of what is known */
    astr_set(&str, _("%s%s (name may be wrong)"),
             mnemonic, gen_action_name(action_id));

    /* Return the guess. */
    return astr_str(&str);
  }

  /* How to interpret action probabilities like prob is documented in
   * fc_types.h */
  if (action_prob_is_signal(prob)) {
    if (action_prob_unknown(prob)) {
      /* Unknown because the player don't have the required knowledge to
       * determine the probability of success for this action. */

      /* TRANS: the chance of an action succeeding is unknown. */
      probtxt = _("?%");
    } else {
      fc_assert(action_prob_not_impl(prob)
                || action_prob_not_relevant(prob));

      /* Unknown because of missing server support or should not exits. */
      probtxt = NULL;
    }
  } else {
    /* TRANS: the probability that an action will succeed. */
    astr_set(&chance, _("%.1f%%"), (double)prob / 2);
    probtxt = astr_str(&chance);
  }

  /* Format the info part of the action's UI name. */
  if (probtxt != NULL && custom != NULL) {
    /* TRANS: action UI name's info part with custom info and probabilty. */
    astr_set(&chance, _(" (%s; %s)"), custom, probtxt);
  } else if (probtxt != NULL) {
    /* TRANS: action UI name's info part with probabilty. */
    astr_set(&chance, _(" (%s)"), probtxt);
  } else if (custom != NULL) {
    /* TRANS: action UI name's info part with custom info. */
    astr_set(&chance, _(" (%s)"), custom);
  } else {
    /* No info part to display. */
    astr_clear(&chance);
  }

  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  astr_set(&str, _(actions[action_id]->ui_name), mnemonic,
           astr_str(&chance));

  return astr_str(&str);
}

/**************************************************************************
  Get information about starting the action in the current situation.
  Suitable for a tool tip for the button that starts it.
**************************************************************************/
const char *action_get_tool_tip(const int action_id,
                                const action_probability act_prob)
{
  static struct astring tool_tip = ASTRING_INIT;

  if (action_prob_is_signal(act_prob)) {
    if (action_prob_unknown(act_prob)) {
      /* Missing in game knowledge. An in game action can change this. */
      astr_set(&tool_tip,
               _("Starting to do this may currently be impossible."));
    } else {
      fc_assert(action_prob_not_impl(act_prob));

      /* Missing server support. No in game action will change this. */
      astr_clear(&tool_tip);
    }
  } else {
    /* The unit is 0.5% chance of success. */
    const double converted = (double)act_prob / 2.0;

    astr_set(&tool_tip, _("The probability of success is %.1f%%."),
             converted);
  }

  return astr_str(&tool_tip);
}

/**************************************************************************
  Get the unit type role corresponding to the ability to do action.
**************************************************************************/
int action_get_role(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  fc_assert_msg(AAK_UNIT == action_get_actor_kind(action_id),
                "Action %s isn't performed by a unit",
                action_get_rule_name(action_id));

  return action_id + L_LAST;
}

/**************************************************************************
  Create a new action enabler.
**************************************************************************/
struct action_enabler *action_enabler_new(void)
{
  struct action_enabler *enabler;

  enabler = fc_malloc(sizeof(*enabler));
  requirement_vector_init(&enabler->actor_reqs);
  requirement_vector_init(&enabler->target_reqs);

  return enabler;
}

/**************************************************************************
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

/**************************************************************************
  Add an action enabler.
**************************************************************************/
void action_enabler_add(struct action_enabler *enabler)
{
  action_enabler_list_append(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**************************************************************************
  Get all enablers for an action.
**************************************************************************/
struct action_enabler_list *
action_enablers_for_action(enum gen_action action)
{
  return action_enablers_by_action[action];
}

/**************************************************************************
  Returns TRUE iff the specified player knows (has seen) the specified
  tile.
**************************************************************************/
static bool plr_knows_tile(const struct player *plr,
                           const struct tile *ttile)
{
  return plr && ttile && (tile_get_known(ttile, plr) != TILE_UNKNOWN);
}

/**************************************************************************
  Returns TRUE iff the specified player can see the specified tile.
**************************************************************************/
static bool plr_sees_tile(const struct player *plr,
                          const struct tile *ttile)
{
  return plr && ttile && (tile_get_known(ttile, plr) == TILE_KNOWN_SEEN);
}

/**************************************************************************
  Returns the local building type of a city target.

  target_city can't be NULL
**************************************************************************/
static struct impr_type *
tgt_city_local_building(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, NULL);

  if (target_city->production.kind == VUT_IMPROVEMENT) {
    /* The local building is what the target city currently is building. */
    return target_city->production.value.building;
  } else {
    /* In the current semantic the local building is always the building
     * being built. */
    /* TODO: Consider making the local building the target building for
     * actions that allows specifying a building target. */
    return NULL;
  }
}

/**************************************************************************
  Returns the local unit type of a city target.

  target_city can't be NULL
**************************************************************************/
static struct unit_type *
tgt_city_local_utype(const struct city *target_city)
{
  /* Only used with city targets */
  fc_assert_ret_val(target_city, NULL);

  if (target_city->production.kind == VUT_UTYPE) {
    /* The local unit type is what the target city currently is
     * building. */
    return target_city->production.value.utype;
  } else {
    /* In the current semantic the local utype is always the type of the
     * unit being built. */
    return NULL;
  }
}

/**************************************************************************
  Returns the action that blocks regular attacks or NULL if they aren't
  blocked.

  An action that can block regular attacks blocks them when the action is
  forced and possible.

  TODO: Make city invasion action enabler controlled and delete this
  function.
**************************************************************************/
struct action *action_blocks_attack(const struct unit *actor_unit,
                                    const struct tile *target_tile)
{
  if (game.info.force_capture_units
       && is_action_enabled_unit_on_units(ACTION_CAPTURE_UNITS,
                                          actor_unit, target_tile)) {
    /* Capture unit is possible.
     * The ruleset forbids regular attacks when it is. */
    return action_by_number(ACTION_CAPTURE_UNITS);
  }

  if (game.info.force_bombard
       && is_action_enabled_unit_on_units(ACTION_BOMBARD,
                                          actor_unit, target_tile)) {
    /* Bomard units is possible.
     * The ruleset forbids regular attacks when it is. */
    return action_by_number(ACTION_BOMBARD);
  }

  if (game.info.force_explode_nuclear
      && is_action_enabled_unit_on_tile(ACTION_NUKE,
                                        actor_unit, target_tile)) {
    /* Explode nuclear is possible.
     * The ruleset forbids regular attacks when it is. */
    return action_by_number(ACTION_NUKE);
  }

  if (is_action_enabled_unit_on_tile(ACTION_ATTACK,
                                     actor_unit, target_tile)) {
    /* Regular attack is possible so can't occupy city. */
    return action_by_number(ACTION_ATTACK);
  }

  /* Nothing is blocking a regular attack. */
  return NULL;
}

/**************************************************************************
  Returns the target tile for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target tile could therefore be missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_RECYCLE_UNIT.
**************************************************************************/
static const struct tile *
blocked_find_target_tile(const int action_id,
                         const struct unit *actor_unit,
                         const struct tile *target_tile_arg,
                         const struct city *target_city,
                         const struct unit *target_unit)
{
  if (target_tile_arg != NULL) {
    /* Trust the caller. */
    return target_tile_arg;
  }

  switch (action_get_target_kind(action_id)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city, NULL);
    return city_tile(target_city);
  case ATK_UNIT:
    fc_assert_ret_val(target_unit, NULL);
    return unit_tile(target_unit);
  case ATK_UNITS:
    fc_assert_ret_val(target_unit || target_tile_arg, NULL);
    if (target_unit) {
      return unit_tile(target_unit);
    }
    /* Fall through. */
  case ATK_TILE:
    fc_assert_ret_val(target_tile_arg, NULL);
    return target_tile_arg;
  case ATK_SELF:
    fc_assert_ret_val(actor_unit, NULL);
    return unit_tile(actor_unit);
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %d",
                action_get_target_kind(action_id), action_id);
  return NULL;
}

/**************************************************************************
  Returns the target city for actions that may block the specified action.
  This is needed because some actions can be blocked by an action with a
  different target kind. The target city argument could therefore be
  missing.

  Example: The ATK_SELF action ACTION_DISBAND_UNIT can be blocked by the
  ATK_CITY action ACTION_RECYCLE_UNIT.
**************************************************************************/
static const struct city *
blocked_find_target_city(const int action_id,
                         const struct unit *actor_unit,
                         const struct tile *target_tile,
                         const struct city *target_city_arg,
                         const struct unit *target_unit)
{
  if (target_city_arg != NULL) {
    /* Trust the caller. */
    return target_city_arg;
  }

  switch (action_get_target_kind(action_id)) {
  case ATK_CITY:
    fc_assert_ret_val(target_city_arg, NULL);
    return target_city_arg;
  case ATK_UNIT:
    fc_assert_ret_val(target_unit, NULL);
    fc_assert_ret_val(unit_tile(target_unit), NULL);
    return tile_city(unit_tile(target_unit));
  case ATK_UNITS:
    fc_assert_ret_val(target_unit || target_tile, NULL);
    if (target_unit) {
      fc_assert_ret_val(unit_tile(target_unit), NULL);
      return tile_city(unit_tile(target_unit));
    }
    /* Fall through. */
  case ATK_TILE:
    fc_assert_ret_val(target_tile, NULL);
    return tile_city(target_tile);
  case ATK_SELF:
    fc_assert_ret_val(actor_unit, NULL);
    fc_assert_ret_val(unit_tile(actor_unit), NULL);
    return tile_city(unit_tile(actor_unit));
  case ATK_COUNT:
    /* Handled below. */
    break;
  }

  fc_assert_msg(FALSE, "Bad action target kind %d for action %d",
                action_get_target_kind(action_id), action_id);
  return NULL;
}

/**************************************************************************
  Returns the action that blocks the specified action or NULL if the
  specified action isn't blocked.

  An action that can block another blocks when it is forced and possible.
**************************************************************************/
struct action *action_is_blocked_by(const int action_id,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile_arg,
                                    const struct city *target_city_arg,
                                    const struct unit *target_unit)
{


  const struct tile *target_tile
      = blocked_find_target_tile(action_id, actor_unit, target_tile_arg,
                                 target_city_arg, target_unit);
  const struct city *target_city
      = blocked_find_target_city(action_id, actor_unit, target_tile,
                                 target_city_arg, target_unit);

  action_iterate(blocker_id) {
    fc_assert_action(action_get_actor_kind(blocker_id) == AAK_UNIT,
                     continue);

    if (!action_id_would_be_blocked_by(action_id, blocker_id)) {
      /* It doesn't matter if it is legal. It won't block the action. */
      continue;
    }

    switch (action_get_target_kind(blocker_id)) {
    case ATK_CITY:
      if (!target_city) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_city(blocker_id,
                                         actor_unit, target_city)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_UNIT:
      if (!target_unit) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_unit(blocker_id,
                                         actor_unit, target_unit)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_UNITS:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_units(blocker_id,
                                          actor_unit, target_tile)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_TILE:
      if (!target_tile) {
        /* Can't be enabled. No target. */
        continue;
      }
      if (is_action_enabled_unit_on_tile(blocker_id,
                                         actor_unit, target_tile)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_SELF:
      if (is_action_enabled_unit_on_self(blocker_id, actor_unit)) {
        return action_by_number(blocker_id);
      }
      break;
    case ATK_COUNT:
      fc_assert_action(action_get_target_kind(blocker_id) != ATK_COUNT,
                       continue);
      break;
    }
  } action_iterate_end;

  /* Not blocked. */
  return NULL;
}

/**************************************************************************
  Returns TRUE if the specified unit type can perform the wanted action
  given that an action enabler later will enable it.

  This is done by checking the action's hard requirements. Hard
  requirements must be TRUE before an action can be done. The reason why
  is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.
**************************************************************************/
bool
action_actor_utype_hard_reqs_ok(const enum gen_action wanted_action,
                                const struct unit_type *actor_unittype)
{
  switch (wanted_action) {
  case ACTION_JOIN_CITY:
    if (utype_pop_value(actor_unittype) <= 0) {
      /* Reason: Must have population to add. */
      return FALSE;
    }
    break;

  case ACTION_BOMBARD:
    if (actor_unittype->bombard_rate <= 0) {
      /* Reason: Can't bombard if it never fires. */
      return FALSE;
    }
    break;

  case ACTION_UPGRADE_UNIT:
    if (actor_unittype->obsoleted_by == U_NOT_OBSOLETED) {
      /* Reason: Nothing to upgrade to. */
      return FALSE;
    }
    break;

  case ACTION_ATTACK:
    if (actor_unittype->attack_strength <= 0) {
      /* Reason: Can't attack without strength. */
      return FALSE;
    }
    break;

  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_SPY_POISON:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
  case ACTION_HELP_WONDER:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_CAPTURE_UNITS:
  case ACTION_FOUND_CITY:
  case ACTION_STEAL_MAPS:
  case ACTION_SPY_NUKE:
  case ACTION_NUKE:
  case ACTION_DESTROY_CITY:
  case ACTION_EXPEL_UNIT:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_PARADROP:
  case ACTION_AIRLIFT:
    /* No hard unit type requirements. */
    break;

  case ACTION_COUNT:
    fc_assert_ret_val(wanted_action != ACTION_COUNT, FALSE);
    break;
  }

  return TRUE;
}

/**************************************************************************
  Returns TRUE iff the wanted action is possible as far as the actor is
  concerned given that an action enabler later will enable it. Will, unlike
  action_actor_utype_hard_reqs_ok(), check the actor unit's current state.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.
**************************************************************************/
static enum fc_tristate
action_hard_reqs_actor(const enum gen_action wanted_action,
                       const struct player *actor_player,
                       const struct city *actor_city,
                       const struct impr_type *actor_building,
                       const struct tile *actor_tile,
                       const struct unit *actor_unit,
                       const struct unit_type *actor_unittype,
                       const struct output_type *actor_output,
                       const struct specialist *actor_specialist,
                       const bool omniscient)
{
  if (!action_actor_utype_hard_reqs_ok(wanted_action, actor_unittype)) {
    /* Info leak: The actor player knows the type of his unit. */
    /* The actor unit type can't perform the action because of hard
     * unit type requirements. */
    return TRI_NO;
  }

  switch (wanted_action) {
  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
    /* It isn't possible to establish a trade route from a non existing
     * city. The Freeciv code assumes this applies to Enter Marketplace
     * too. */
    /* Info leak: The actor player knowns his unit's home city. */
    if (!game_city_by_number(actor_unit->homecity)) {
      return TRI_NO;
    }

    break;

  case ACTION_PARADROP:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if his unit already has paradropped this
     * turn. */
    if (actor_unit->paradropped) {
      return TRI_NO;
    }

    /* Reason: Support the paratroopers_mr_req unit type field. */
    /* Info leak: The player knows how many move fragments his unit has
     * left. */
    if (actor_unit->moves_left < actor_unittype->paratroopers_mr_req) {
      return TRI_NO;
    }

    break;

  case ACTION_ATTACK:
    /* Reason: Keep the old marines rules. */
    /* Info leak: The player knows if his unit is at a native tile. */
    if (!is_native_tile(actor_unittype, actor_tile)
        && !can_attack_from_non_native(actor_unittype)) {
      return TRI_NO;
    }
    break;

  case ACTION_ESTABLISH_EMBASSY:
  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_SPY_POISON:
  case ACTION_SPY_STEAL_GOLD:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_TARGETED_STEAL_TECH:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_HELP_WONDER:
  case ACTION_SPY_BRIBE_UNIT:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_CAPTURE_UNITS:
  case ACTION_FOUND_CITY:
  case ACTION_JOIN_CITY:
  case ACTION_STEAL_MAPS:
  case ACTION_BOMBARD:
  case ACTION_SPY_NUKE:
  case ACTION_NUKE:
  case ACTION_DESTROY_CITY:
  case ACTION_EXPEL_UNIT:
  case ACTION_RECYCLE_UNIT:
  case ACTION_DISBAND_UNIT:
  case ACTION_HOME_CITY:
  case ACTION_UPGRADE_UNIT:
  case ACTION_AIRLIFT:
    /* No hard unit type requirements. */
    break;

  case ACTION_COUNT:
    fc_assert_ret_val(wanted_action != ACTION_COUNT, TRI_NO);
    break;
  }

  return TRI_YES;
}

/**************************************************************************
  Returns if the wanted action is possible given that an action enabler
  later will enable it.

  Can return maybe when not omniscient. Should always return yes or no when
  omniscient.

  This is done by checking the action's hard requirements. Hard
  requirements must be fulfilled before an action can be done. The reason
  why is usually that code dealing with the action assumes that the
  requirements are true. A requirement may also end up here if it can't be
  expressed in a requirement vector or if its absence makes the action
  pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirment in a comment.
   * remember that this is called from action_prob(). Should information
     the player don't have access to be used in a test it must check if
     the evaluation can see the thing being tested.
**************************************************************************/
static enum fc_tristate
is_action_possible(const enum gen_action wanted_action,
                   const struct player *actor_player,
                   const struct city *actor_city,
                   const struct impr_type *actor_building,
                   const struct tile *actor_tile,
                   const struct unit *actor_unit,
                   const struct unit_type *actor_unittype,
                   const struct output_type *actor_output,
                   const struct specialist *actor_specialist,
                   const struct player *target_player,
                   const struct city *target_city,
                   const struct impr_type *target_building,
                   const struct tile *target_tile,
                   const struct unit *target_unit,
                   const struct unit_type *target_unittype,
                   const struct output_type *target_output,
                   const struct specialist *target_specialist,
                   const bool omniscient)
{
  bool can_see_tgt_unit;
  bool can_see_tgt_tile;
  enum fc_tristate out;

  fc_assert_msg((action_get_target_kind(wanted_action) == ATK_CITY
                 && target_city != NULL)
                || (action_get_target_kind(wanted_action) == ATK_TILE
                    && target_tile != NULL)
                || (action_get_target_kind(wanted_action) == ATK_UNIT
                    && target_unit != NULL)
                || (action_get_target_kind(wanted_action) == ATK_UNITS
                    /* At this level each individual unit is tested. */
                    && target_unit != NULL)
                || (action_get_target_kind(wanted_action) == ATK_SELF),
                "Missing target!");

  /* Only check requirement against the target unit if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about his odds isn't. */
  can_see_tgt_unit = (omniscient || (target_unit
                                     && can_player_see_unit(actor_player,
                                                            target_unit)));

  /* Only check requirement against the target tile if the actor can see it
   * or if the evaluator is omniscient. The game checking the rules is
   * omniscient. The player asking about his odds isn't. */
  can_see_tgt_tile = (omniscient
                      || plr_sees_tile(actor_player, target_tile));

  /* Info leak: The player knows where his unit is. */
  if (action_get_target_kind(wanted_action) != ATK_SELF) {
    if (!action_id_distance_accepted(wanted_action,
                                    real_map_distance(actor_tile,
                                                      target_tile))) {
      /* The distance between the actor and the target isn't inside the
       * action's accepted range. */
      return TRI_NO;
    }
  }

  if (action_get_target_kind(wanted_action) == ATK_UNIT) {
    /* The Freeciv code for all actions that is controlled by action
     * enablers and targets a unit assumes that the acting
     * player can see the target unit. */
    if (!can_player_see_unit(actor_player, target_unit)) {
      return TRI_NO;
    }
  }

  if (action_is_blocked_by(wanted_action, actor_unit,
                           target_tile, target_city, target_unit)) {
    /* Allows an action to block an other action. If a blocking action is
     * legal the actions it blocks becomes illegal. */
    return TRI_NO;
  }

  /* Actor specific hard requirements. */
  out = action_hard_reqs_actor(wanted_action,
                               actor_player, actor_city, actor_building,
                               actor_tile, actor_unit, actor_unittype,
                               actor_output, actor_specialist,
                               omniscient);

  if (out == TRI_NO) {
    /* Illegal because of a hard actor requirement. */
    return TRI_NO;
  }

  /* Hard requirements for individual actions. */
  switch (wanted_action) {
  case ACTION_CAPTURE_UNITS:
  case ACTION_SPY_BRIBE_UNIT:
    /* Why this is a hard requirement: Can't transfer a unique unit if the
     * actor player already has one. */
    /* Info leak: This is only checked for when the actor player can see
     * the target unit. Since the target unit is seen its type is known.
     * The fact that a city hiding the unseen unit is occupied is known. */

    if (!can_see_tgt_unit) {
      /* An omniscient player can see the target unit. */
      fc_assert(!omniscient);

      return TRI_MAYBE;
    }

    if (utype_player_already_has_this_unique(actor_player,
                                             target_unittype)) {
      return TRI_NO;
    }

    /* FIXME: Capture Unit may want to look for more than one unique unit
     * of the same kind at the target tile. Currently caught by sanity
     * check in do_capture_units(). */

    break;

  case ACTION_ESTABLISH_EMBASSY:
    /* Why this is a hard requirement: There is currently no point in
     * establishing an embassy when a real embassy already exists.
     * (Possible exception: crazy hack using the Lua callback
     * action_started_callback() to make establish embassy do something
     * else even if the UI still call the action Establish Embassy) */
    /* Info leak: The actor player known who he has a real embassy to. */
    if (player_has_real_embassy(actor_player, target_player)) {
      return TRI_NO;
    }

    break;

  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* Reason: The Freeciv code don't support selecting a target tech
     * unless it is known that the victim player has it. */
    /* Info leak: The actor player knowns who's techs he can see. */
    if (!can_see_techs_of_target(actor_player, target_player)) {
      return TRI_NO;
    }

    break;

  case ACTION_SPY_STEAL_GOLD:
    /* If actor_unit can do the action the actor_player can see how much
     * gold target_player have. Not requireing it is therefore pointless.
     */
    if (target_player->economic.gold <= 0) {
      return TRI_NO;
    }

    break;

  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
    {
      const struct city *actor_homecity;

      actor_homecity = game_city_by_number(actor_unit->homecity);

      /* Checked in action_hard_reqs_actor() */
      fc_assert_ret_val(actor_homecity != NULL, TRI_NO);

      /* Can't establish a trade route or enter the market place if the
       * cities can't trade at all. */
      /* TODO: Should this restriction (and the above restriction that the
       * actor unit must have a home city) be kept for Enter Marketplace? */
      if (!can_cities_trade(actor_homecity, target_city)) {
        return TRI_NO;
      }

      /* There are more restrictions on establishing a trade route than on
       * entering the market place. */
      if (wanted_action == ACTION_TRADE_ROUTE &&
          !can_establish_trade_route(actor_homecity, target_city)) {
        return TRI_NO;
      }
    }

    break;

  case ACTION_HELP_WONDER:
    /* It is only possible to help the production if the production needs
     * the help. (If not it would be possible to add shields for something
     * that can't legally receive help if it is build later) */
    /* Info leak: The player knows that the production in his own city has
     * been hurried (bought or helped). The information isn't revealed when
     * asking for action probabilities since omniscient is FALSE. */
    if (!omniscient
        && !can_player_see_city_internals(actor_player, target_city)) {
      return TRI_MAYBE;
    }

    if (!(target_city->shield_stock
          < city_production_build_shield_cost(target_city))) {
      return TRI_NO;
    }

    break;

  case ACTION_RECYCLE_UNIT:
    /* FIXME: The next item may be forbidden from receiving help. But
     * forbidding the player from recycling a unit because the production
     * has enough, like Help Wonder does, would be a rule change. */

    break;

  case ACTION_FOUND_CITY:
    if (game.scenario.prevent_new_cities) {
      /* Reason: allow scenarios to disable city founding. */
      /* Info leak: the setting is public knowledge. */
      return TRI_NO;
    }

    if (can_see_tgt_tile && tile_city(target_tile)) {
      /* Reason: a tile can have 0 or 1 cities. */
      return TRI_NO;
    }

    switch (city_build_here_test(target_tile, actor_unit)) {
    case CB_OK:
      /* If the player knows this is checked below. */
      break;
    case CB_BAD_CITY_TERRAIN:
    case CB_BAD_UNIT_TERRAIN:
    case CB_BAD_BORDERS:
      if (can_see_tgt_tile) {
        /* Known to be blocked. Target tile is seen. */
        return TRI_NO;
      }
      break;
    case CB_NO_MIN_DIST:
      if (omniscient) {
        /* No need to check again. */
        return TRI_NO;
      } else {
        square_iterate(target_tile, game.info.citymindist - 1, otile) {
          if (tile_city(otile) != NULL
              && plr_sees_tile(actor_player, otile)) {
            /* Known to be blocked by citymindist */
            return TRI_NO;
          }
        } square_iterate_end;
      }
      break;
    }

    /* The player may not have enough information to be certain. */

    if (!can_see_tgt_tile) {
      /* Need to know if target tile already has a city, has TER_NO_CITIES
       * terrain, is non native to the actor or is owned by a foreigner. */
      return TRI_MAYBE;
    }

    if (!omniscient) {
      /* The player may not have enough information to find out if
       * citymindist blocks or not. This doesn't depend on if it blocks. */
      square_iterate(target_tile, game.info.citymindist - 1, otile) {
        if (!plr_sees_tile(actor_player, otile)) {
          /* Could have a city that blocks via citymindist. Even if this
           * tile has TER_NO_CITIES terrain the player don't know that it
           * didn't change and had a city built on it. */
          return TRI_MAYBE;
        }
      } square_iterate_end;
    }

    break;

  case ACTION_JOIN_CITY:
    {
      int new_pop;

      if (!omniscient
          && !mke_can_see_city_externals(actor_player, target_city)) {
        return TRI_MAYBE;
      }

      new_pop = city_size_get(target_city) + unit_pop_value(actor_unit);

      if (new_pop > game.info.add_to_size_limit) {
        /* Reason: Make the add_to_size_limit setting work. */
        return TRI_NO;
      }

      if (!city_can_grow_to(target_city, new_pop)) {
        /* Reason: respect city size limits. */
        /* Info leak: when it is legal to join a foreign city is legal and
         * the EFT_SIZE_UNLIMIT effect or the EFT_SIZE_ADJ effect depends on
         * something the actor player don't have access to.
         * Example: depends on a building (like Aqueduct) that isn't
         * VisibleByOthers. */
        return TRI_NO;
      }
    }

    break;

  case ACTION_BOMBARD:
    /* TODO: Move to the ruleset. */
    if (!pplayers_at_war(unit_owner(target_unit), actor_player)) {
      return TRI_NO;
    }

    /* FIXME: Target of Bombard should be city and units. */
    if (tile_city(target_tile)
        && !pplayers_at_war(city_owner(tile_city(target_tile)),
                            actor_player)) {
      return TRI_NO;
    }

    break;

  case ACTION_NUKE:
    if (actor_tile != target_tile) {
      /* The old rules only restricted other tiles. Keep them for now. */

      struct city *tcity;

      if (actor_unit->moves_left <= 0) {
        return TRI_NO;
      }

      if (!(tcity = tile_city(target_tile))
          && !unit_list_size(target_tile->units)) {
        return TRI_NO;
      }

      if (tcity && !pplayers_at_war(city_owner(tcity), actor_player)) {
        return TRI_NO;
      }

      if (is_non_attack_unit_tile(target_tile, actor_player)) {
        return TRI_NO;
      }

      if (!tcity
          && (unit_attack_units_at_tile_result(actor_unit, target_tile)
              != ATT_OK)) {
        return TRI_NO;
      }
    }

    break;

  case ACTION_HOME_CITY:
    /* Reason: can't change to what is. */
    /* Info leak: The player knows his unit's current home city. */
    if (actor_unit->homecity == target_city->id) {
      /* This is already the unit's home city. */
      return TRI_NO;
    }

    break;

  case ACTION_UPGRADE_UNIT:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows his unit's type. He knows if he can
     * build the unit type upgraded to. If the upgrade happens in a foreign
     * city that fact may leak. This can be seen as a price for changing
     * the rules to allow upgrading in a foreign city.
     * The player knows how much gold he has. If the Upgrade_Price_Pct
     * effect depends on information he don't have that information may
     * leak. The player knows the location of his unit. He knows if the
     * tile has a city and if the unit can exist there outside a transport.
     * The player knows his unit's cargo. By knowing their number and type
     * he can predict if there will be room for them in the unit upgraded
     * to as long as he knows what unit type his unit will end up as. */
    if (unit_upgrade_test(actor_unit, FALSE) != UU_OK) {
      return TRI_NO;
    }

    break;

  case ACTION_PARADROP:
    /* Reason: Keep the old rules. */
    /* Info leak: The player knows if he knows the target tile. */
    if (!plr_knows_tile(actor_player, target_tile)) {
      return TRI_NO;
    }

    break;

  case ACTION_AIRLIFT:
    /* Reason: Keep the old rules. */
    /* Info leak: same as test_unit_can_airlift_to() */
    switch (test_unit_can_airlift_to(omniscient ? NULL : actor_player,
                                     actor_unit, target_city)) {
    case AR_OK:
      return TRI_YES;
    case AR_OK_SRC_UNKNOWN:
    case AR_OK_DST_UNKNOWN:
      return TRI_MAYBE;
    case AR_NO_MOVES:
    case AR_WRONG_UNITTYPE:
    case AR_OCCUPIED:
    case AR_NOT_IN_CITY:
    case AR_BAD_SRC_CITY:
    case AR_BAD_DST_CITY:
    case AR_SRC_NO_FLIGHTS:
    case AR_DST_NO_FLIGHTS:
      return TRI_NO;
    }

    break;

  case ACTION_ATTACK:
    /* Reason: must have a unit to attack. */
    if (unit_list_size(target_tile->units) <= 0) {
      return TRI_NO;
    }

    /* Reason: Keep the old rules. */
    if (!can_unit_attack_tile(actor_unit, target_tile)) {
      return TRI_NO;
    }
    break;

  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_SPY_POISON:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_SABOTAGE_UNIT:
  case ACTION_STEAL_MAPS:
  case ACTION_SPY_NUKE:
  case ACTION_DESTROY_CITY:
  case ACTION_EXPEL_UNIT:
  case ACTION_DISBAND_UNIT:
    /* No known hard coded requirements. */
    break;
  case ACTION_COUNT:
    fc_assert(action_id_is_valid(wanted_action));
    break;
  }

  return out;
}

/**************************************************************************
  Return TRUE iff the action enabler is active
**************************************************************************/
static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
                              const struct unit *actor_unit,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
                              const struct unit *target_unit,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist)
{
  return are_reqs_active(actor_player, target_player, actor_city,
                         actor_building, actor_tile,
                         actor_unit, actor_unittype,
                         actor_output, actor_specialist, NULL,
                         &enabler->actor_reqs, RPT_CERTAIN)
      && are_reqs_active(target_player, actor_player, target_city,
                         target_building, target_tile,
                         target_unit, target_unittype,
                         target_output, target_specialist, NULL,
                         &enabler->target_reqs, RPT_CERTAIN);
}

/**************************************************************************
  Returns TRUE if the wanted action is enabled.

  Note that the action may disable it self because of hard requirements
  even if an action enabler returns TRUE.
**************************************************************************/
static bool is_action_enabled(const enum gen_action wanted_action,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
                              const struct unit *actor_unit,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
                              const struct unit *target_unit,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist)
{
  enum fc_tristate possible;

  possible = is_action_possible(wanted_action,
                                actor_player, actor_city,
                                actor_building, actor_tile,
                                actor_unit, actor_unittype,
                                actor_output, actor_specialist,
                                target_player, target_city,
                                target_building, target_tile,
                                target_unit, target_unittype,
                                target_output, target_specialist,
                                TRUE);

  if (possible != TRI_YES) {
    /* This context is omniscient. Should be yes or no. */
    fc_assert_msg(possible != TRI_MAYBE,
                  "Is omniscient, should get yes or no.");

    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (is_enabler_active(enabler, actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to target_city as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_city(const enum gen_action wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *target_city)
{
  struct tile *actor_tile = unit_tile(actor_unit);
  struct impr_type *target_building;
  struct unit_type *target_utype;

  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_CITY));

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           city_owner(target_city), target_city,
                           target_building, city_tile(target_city),
                           NULL, target_utype, NULL, NULL);
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_unit(const enum gen_action wanted_action,
                                    const struct unit *actor_unit,
                                    const struct unit *target_unit)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_UNIT));

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit),
                           target_unit, unit_type_get(target_unit),
                           NULL, NULL);
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to all units on the
  target_tile as far as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_units(const enum gen_action wanted_action,
                                     const struct unit *actor_unit,
                                     const struct tile *target_tile)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_tile == NULL
      || unit_list_size(target_tile->units) == 0) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNITS == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_UNITS));

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  unit_list_iterate(target_tile->units, target_unit) {
    if (!is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit),
                           target_unit, unit_type_get(target_unit),
                           NULL, NULL)) {
      /* One unit makes it impossible for all units. */
      return FALSE;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return TRUE;
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to the target_tile as far
  as action enablers are concerned.

  See note in is_action_enabled for why the action may still be disabled.
**************************************************************************/
bool is_action_enabled_unit_on_tile(const enum gen_action wanted_action,
                                    const struct unit *actor_unit,
                                    const struct tile *target_tile)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_tile == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_TILE));

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           tile_owner(target_tile), NULL, NULL,
                           target_tile, NULL, NULL, NULL, NULL);
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to itself as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
**************************************************************************/
bool is_action_enabled_unit_on_self(const enum gen_action wanted_action,
                                    const struct unit *actor_unit)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL) {
    /* Can't do an action when the actor is missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_SELF));

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

/**************************************************************************
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
**************************************************************************/
static enum fc_tristate
action_enabled_local(const enum gen_action wanted_action,
                     const struct player *actor_player,
                     const struct city *actor_city,
                     const struct impr_type *actor_building,
                     const struct tile *actor_tile,
                     const struct unit *actor_unit,
                     const struct output_type *actor_output,
                     const struct specialist *actor_specialist,
                     const struct player *target_player,
                     const struct city *target_city,
                     const struct impr_type *target_building,
                     const struct tile *target_tile,
                     const struct unit *target_unit,
                     const struct output_type *target_output,
                     const struct specialist *target_specialist)
{
  enum fc_tristate current;
  enum fc_tristate result;

  result = TRI_NO;
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    current = tri_and(mke_eval_reqs(actor_player, actor_player,
                                    target_player, actor_city,
                                    actor_building, actor_tile,
                                    actor_unit, actor_output,
                                    actor_specialist,
                                    &enabler->actor_reqs, RPT_CERTAIN),
                      mke_eval_reqs(actor_player, target_player,
                                    actor_player, target_city,
                                    target_building, target_tile,
                                    target_unit, target_output,
                                    target_specialist,
                                    &enabler->target_reqs, RPT_CERTAIN));
    if (current == TRI_YES) {
      return TRI_YES;
    } else if (current == TRI_MAYBE) {
      result = TRI_MAYBE;
    }
  } action_enabler_list_iterate_end;

  return result;
}

/**************************************************************************
  Find out if the effect value is known

  The knowledge of the actor is assumed to be given in the parameters.

  If meta knowledge is missing TRI_MAYBE will be returned.
**************************************************************************/
static bool is_effect_val_known(enum effect_type effect_type,
                                const struct player *pow_player,
                                const struct player *target_player,
                                const struct player *other_player,
                                const struct city *target_city,
                                const struct impr_type *target_building,
                                const struct tile *target_tile,
                                const struct unit *target_unit,
                                const struct output_type *target_output,
                                const struct specialist *target_specialist)
{
  effect_list_iterate(get_effects(effect_type), peffect) {
    if (TRI_MAYBE == mke_eval_reqs(pow_player, target_player,
                                   other_player, target_city,
                                   target_building, target_tile,
                                   target_unit, target_output,
                                   target_specialist,
                                   &(peffect->reqs), RPT_CERTAIN)) {
      return FALSE;
    }
  } effect_list_iterate_end;

  return TRUE;
}

/**************************************************************************
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
      advance_iterate(A_FIRST, padvance) {
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

/**************************************************************************
  The action probability that pattacker will win a diplomatic battle.

  It is assumed that pattacker and pdefender have different owners and that
  the defender can defend in a diplomatic battle.

  See diplomat_success_vs_defender() in server/diplomats.c
**************************************************************************/
static action_probability ap_dipl_battle_win(const struct unit *pattacker,
                                             const struct unit *pdefender)
{
  /* Keep unconverted until the end to avoid scaling each step */
  int chance;

  /* Superspy always win */
  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    /* A defending UTYF_SUPERSPY will defeat every possible attacker. */
    return ACTPROB_IMPOSSIBLE;
  }
  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    /* An attacking UTYF_SUPERSPY will defeat every possible defender
     * except another UTYF_SUPERSPY. */
    return 200;
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
    const struct veteran_level *vatt =
        utype_veteran_level(unit_type_get(pattacker), pattacker->veteran);
    const struct veteran_level *vdef =
        utype_veteran_level(unit_type_get(pdefender), pdefender->veteran);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* Defense bonus. */
  {
    if (!is_effect_val_known(EFT_SPY_RESISTANT, unit_owner(pattacker),
                             tile_owner(pdefender->tile),  NULL,
                             tile_city(pdefender->tile), NULL,
                             pdefender->tile, NULL, NULL, NULL)) {
      return ACTPROB_NOT_KNOWN;
    }

    /* Reduce the chance of an attack by EFT_SPY_RESISTANT percent. */
    chance -= chance
              * get_target_bonus_effects(NULL,
                                         tile_owner(pdefender->tile), NULL,
                                         tile_city(pdefender->tile), NULL,
                                         pdefender->tile, NULL, NULL, NULL,
                                         NULL, NULL,
                                         EFT_SPY_RESISTANT) / 100;
  }

  /* Convert to action probability */
  return chance * 2;
}

/**************************************************************************
  The action probability that pattacker will win a diplomatic battle.

  See diplomat_infiltrate_tile() in server/diplomats.c
**************************************************************************/
static action_probability ap_diplomat_battle(const struct unit *pattacker,
                                             const struct unit *pvictim)
{
  unit_list_iterate(unit_tile(pvictim)->units, punit) {
    if (unit_owner(punit) == unit_owner(pattacker)) {
      /* Won't defend against its owner. */
      continue;
    }

    if (punit == pvictim
        && !unit_has_type_flag(punit, UTYF_SUPERSPY)) {
      /* The victim unit is defenseless unless it's a SuperSpy.
       * Rationalization: A regular diplomat don't mind being bribed. A
       * SuperSpy is high enough up the chain that accepting a bribe is
       * against his own interests. */
      continue;
    }

    if (!(unit_has_type_flag(punit, UTYF_DIPLOMAT)
        || unit_has_type_flag(punit, UTYF_SUPERSPY))) {
      /* The unit can't defend. */
      continue;
    }

    /* There will be a diplomatic battle in stead of an action. */
    return ap_dipl_battle_win(pattacker, punit);
  } unit_list_iterate_end;

  /* No diplomatic battle will occur. */
  return 200;
}

/**************************************************************************
  An action's probability of success.

  "Success" indicates that the action achieves its goal, not that the
  actor survives. For actions that cost money it is assumed that the
  player has and is willing to spend the money. This is so the player can
  figure out what his odds are before deciding to get the extra money.
**************************************************************************/
static action_probability
action_prob(const enum gen_action wanted_action,
            const struct player *actor_player,
            const struct city *actor_city,
            const struct impr_type *actor_building,
            const struct tile *actor_tile,
            const struct unit *actor_unit,
            const struct unit_type *actor_unittype_p,
            const struct output_type *actor_output,
            const struct specialist *actor_specialist,
            const struct player *target_player,
            const struct city *target_city,
            const struct impr_type *target_building,
            const struct tile *target_tile,
            const struct unit *target_unit,
            const struct unit_type *target_unittype_p,
            const struct output_type *target_output,
            const struct specialist *target_specialist)
{
  int known;
  action_probability chance;

  const struct unit_type *actor_unittype;
  const struct unit_type *target_unittype;

  if (actor_unittype_p == NULL && actor_unit != NULL) {
    actor_unittype = unit_type_get(actor_unit);
  } else {
    actor_unittype = actor_unittype_p;
  }

  if (target_unittype_p == NULL && target_unit != NULL) {
    target_unittype = unit_type_get(target_unit);
  } else {
    target_unittype = target_unittype_p;
  }

  known = is_action_possible(wanted_action,
                             actor_player, actor_city,
                             actor_building, actor_tile,
                             actor_unit, actor_unittype,
                             actor_output, actor_specialist,
                             target_player, target_city,
                             target_building, target_tile,
                             target_unit, target_unittype,
                             target_output, target_specialist,
                             FALSE);

  if (known == TRI_NO) {
    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return ACTPROB_IMPOSSIBLE;
  }

  chance = ACTPROB_NOT_IMPLEMENTED;

  known = tri_and(known,
                  action_enabled_local(wanted_action,
                                       actor_player, actor_city,
                                       actor_building, actor_tile,
                                       actor_unit,
                                       actor_output, actor_specialist,
                                       target_player, target_city,
                                       target_building, target_tile,
                                       target_unit,
                                       target_output, target_specialist));

  switch (wanted_action) {
  case ACTION_SPY_POISON:
    /* TODO */
    break;
  case ACTION_SPY_STEAL_GOLD:
    /* TODO */
    break;
  case ACTION_STEAL_MAPS:
    /* TODO */
    break;
  case ACTION_SPY_SABOTAGE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit);
    break;
  case ACTION_SPY_BRIBE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit);;
    break;
  case ACTION_SPY_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
    /* TODO */
    break;
  case ACTION_SPY_INCITE_CITY:
    /* TODO */
    break;
  case ACTION_ESTABLISH_EMBASSY:
    chance = 200;
    break;
  case ACTION_SPY_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = tri_and(known,
                    tech_can_be_stolen(actor_player, target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = tri_and(known,
                    tech_can_be_stolen(actor_player, target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTION_SPY_INVESTIGATE_CITY:
    /* There is no risk that the city won't get investigated. */
    chance = 200;
    break;
  case ACTION_TRADE_ROUTE:
    /* TODO */
    break;
  case ACTION_MARKETPLACE:
    /* Possible when not blocked by is_action_possible() */
    chance = 200;
    break;
  case ACTION_HELP_WONDER:
    /* Possible when not blocked by is_action_possible() */
    chance = 200;
    break;
  case ACTION_CAPTURE_UNITS:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_EXPEL_UNIT:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_BOMBARD:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_FOUND_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = 200;
    break;
  case ACTION_JOIN_CITY:
    /* Possible when not blocked by is_action_possible() */
    chance = 200;
    break;
  case ACTION_SPY_NUKE:
    /* TODO */
    break;
  case ACTION_NUKE:
    /* TODO */
    break;
  case ACTION_DESTROY_CITY:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_RECYCLE_UNIT:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_DISBAND_UNIT:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_HOME_CITY:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_UPGRADE_UNIT:
    /* No battle is fought first. */
    chance = 200;
    break;
  case ACTION_PARADROP:
    /* TODO */
    break;
  case ACTION_AIRLIFT:
    /* TODO */
    break;
  case ACTION_ATTACK:
    {
      struct unit *defender_unit = get_defender(actor_unit,
                                   target_tile);

      if (can_player_see_unit(actor_player, defender_unit)) {
        double unconverted = unit_win_chance(actor_unit, defender_unit);

        /* Action is seen as disabled by anyone that relies on action
         * probability if it is rounded down to 0%. */
        chance = ceil((double)200 * unconverted);
      } else if (known == TRI_YES) {
        known = TRI_MAYBE;
      }
    }
    break;
  case ACTION_COUNT:
    fc_assert(FALSE);
    break;
  }

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

  fc_assert_ret_val_msg(FALSE, ACTPROB_NOT_IMPLEMENTED,
                        "Should be yes, maybe or no");
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
action_probability action_prob_vs_city(const struct unit* actor_unit,
                                       const int action_id,
                                       const struct city* target_city)
{
  struct tile *actor_tile = unit_tile(actor_unit);
  struct impr_type *target_building;
  struct unit_type *target_utype;

  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_CITY));

  if (!unit_can_do_action(actor_unit, action_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  target_building = tgt_city_local_building(target_city);
  target_utype = tgt_city_local_utype(target_city);

  return action_prob(action_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL,
                     city_owner(target_city), target_city,
                     target_building, city_tile(target_city),
                     NULL, target_utype, NULL, NULL);
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
action_probability action_prob_vs_unit(const struct unit* actor_unit,
                                       const int action_id,
                                       const struct unit* target_unit)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_get_target_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_UNIT));

  if (!unit_can_do_action(actor_unit, action_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(action_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL,
                     unit_owner(target_unit),
                     tile_city(unit_tile(target_unit)), NULL,
                     unit_tile(target_unit),
                     target_unit, NULL, NULL, NULL);
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on all units at the target tile.
**************************************************************************/
action_probability action_prob_vs_units(const struct unit* actor_unit,
                                        const int action_id,
                                        const struct tile* target_tile)
{
  action_probability prob_all;
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_tile == NULL
      || unit_list_size(target_tile->units) == 0) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNITS == action_get_target_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_UNITS));

  if (!unit_can_do_action(actor_unit, action_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  prob_all = 200;
  unit_list_iterate(target_tile->units, target_unit) {
    action_probability prob_unit;

    prob_unit = action_prob(action_id,
                            unit_owner(actor_unit),
                            tile_city(actor_tile),
                            NULL, actor_tile, actor_unit, NULL,
                            NULL, NULL,
                            unit_owner(target_unit),
                            tile_city(unit_tile(target_unit)), NULL,
                            unit_tile(target_unit),
                            target_unit, NULL,
                            NULL, NULL);

    if (!action_prob_possible(prob_unit)) {
      /* One unit makes it impossible for all units. */
      return ACTPROB_IMPOSSIBLE;
    } else if (action_prob_is_signal(prob_unit)) {
      if (action_prob_not_impl(prob_unit)) {
        /* Not implemented dominates all except impossible. */
        prob_all = ACTPROB_NOT_IMPLEMENTED;
      } else {
        fc_assert(action_prob_unknown(prob_unit));

        if (!action_prob_not_impl(prob_all)) {
          /* Not known dominates all except not implemented and
           * impossible. */
          prob_all = ACTPROB_NOT_KNOWN;
        }
      }
    } else {
      fc_assert_msg(prob_unit <= 200, "Invalid probability %d", prob_unit);

      if (200 < prob_all) {
        /* Special values dominate regular values. */
        continue;
      }

      /* Probability against all target units considered until this moment
       * and the probability against this target unit. */
      prob_all = (prob_all * prob_unit) / 200;
      break;
    }
  } unit_list_iterate_end;

  /* Not impossible for any of the units at the tile. */
  return prob_all;
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target tile.
**************************************************************************/
action_probability action_prob_vs_tile(const struct unit* actor_unit,
                                       const int action_id,
                                       const struct tile* target_tile)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_tile == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_TILE == action_get_target_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_TILE));

  if (!unit_can_do_action(actor_unit, action_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(action_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL,
                     tile_owner(target_tile), NULL, NULL,
                     target_tile, NULL, NULL, NULL, NULL);
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on itself.
**************************************************************************/
action_probability action_prob_self(const struct unit* actor_unit,
                                    const int action_id)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL) {
    /* Can't do the action when the actor is missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_SELF == action_get_target_kind(action_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_SELF));

  if (!unit_can_do_action(actor_unit, action_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(action_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit, NULL,
                     NULL, NULL,
                     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

/**************************************************************************
  Returns the impossible action probability.
**************************************************************************/
action_probability action_prob_new_impossible(void)
{
  return 0;
}

/**************************************************************************
  Returns the n/a action probability.
**************************************************************************/
action_probability action_prob_new_not_relevant(void)
{
  return 253;
}

/**************************************************************************
  Returns the "not implemented" action probability.
**************************************************************************/
action_probability action_prob_new_not_impl(void)
{
  return 254;
}

/**************************************************************************
  Returns the "user don't know" action probability.
**************************************************************************/
action_probability action_prob_new_unknown(void)
{
  return 255;
}

/**************************************************************************
  Returns TRUE iff the given action probability belongs to an action that
  may be possible.
**************************************************************************/
bool action_prob_possible(action_probability probability)
{
  return ACTPROB_IMPOSSIBLE != probability && ACTPROB_NA != probability;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents the lack of
  an action probability.
**************************************************************************/
static inline bool action_prob_not_relevant(action_probability probability)
{
  return ACTPROB_NA == probability;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents that support
  for finding this action probability currently is missing from Freeciv.
**************************************************************************/
static inline bool action_prob_not_impl(action_probability probability)
{
  return ACTPROB_NOT_IMPLEMENTED == probability;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents that the player
  currently doesn't have enough information to find the real value.

 It is caused by the probability depending on a rule that depends on game
 state the player don't have access to. It may be possible for the player
 to later gain access to this game state.
**************************************************************************/
static inline bool action_prob_unknown(action_probability probability)
{
  return ACTPROB_NOT_KNOWN == probability;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents a special
  signal value rather than a regular action probability value.
**************************************************************************/
static inline bool action_prob_is_signal(action_probability probability)
{
  return probability < 0 || probability > 200;
}

/**************************************************************************
  Will a player with the government gov be immune to the action act?
**************************************************************************/
bool action_immune_government(struct government *gov, int act)
{
  /* Always immune since its not enabled. Doesn't count. */
  if (action_enabler_list_size(action_enablers_for_action(act)) == 0) {
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(act), enabler) {
    if (requirement_fulfilled_by_government(gov, &(enabler->target_reqs))) {
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  Returns TRUE if the specified action never can be performed when the
  situation requirement is fulfilled for the actor.
**************************************************************************/
bool action_blocked_by_situation_act(struct action *action,
                                     const struct requirement *situation)
{
  action_enabler_list_iterate(action_enablers_for_action(action->id),
                              enabler) {
    if (!does_req_contradicts_reqs(situation, &enabler->actor_reqs)) {
      return FALSE;
    }
  } action_enabler_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  Returns TRUE if the wanted action can be done to the target.
**************************************************************************/
static bool is_target_possible(const enum gen_action wanted_action,
			       const struct player *actor_player,
			       const struct player *target_player,
			       const struct city *target_city,
			       const struct impr_type *target_building,
			       const struct tile *target_tile,
                               const struct unit *target_unit,
			       const struct unit_type *target_unittype,
			       const struct output_type *target_output,
			       const struct specialist *target_specialist)
{
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (are_reqs_active(target_player, actor_player, target_city,
                        target_building, target_tile,
                        target_unit, target_unittype,
                        target_output, target_specialist, NULL,
                        &enabler->target_reqs, RPT_POSSIBLE)) {
      return TRUE;
    }
  } action_enabler_list_iterate_end;

  return FALSE;
}

/**************************************************************************
  Returns TRUE if the wanted action can be done to the target city.
**************************************************************************/
bool is_action_possible_on_city(const enum gen_action action_id,
                                const struct player *actor_player,
                                const struct city* target_city)
{
  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(action_id),
                        FALSE, "Action %s is against %s not cities",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)));

  return is_target_possible(action_id, actor_player,
                            city_owner(target_city), target_city, NULL,
                            city_tile(target_city), NULL, NULL,
                            NULL, NULL);
}

/**************************************************************************
  Returns TRUE if the wanted action (as far as the player knows) can be
  performed right now by the specified actor unit if an approriate target
  is provided.
**************************************************************************/
bool action_maybe_possible_actor_unit(const int action_id,
                                      const struct unit* actor_unit)
{
  const struct player *actor_player = unit_owner(actor_unit);
  const struct tile *actor_tile = unit_tile(actor_unit);
  const struct city *actor_city = tile_city(actor_tile);
  const struct unit_type *actor_unittype = unit_type_get(actor_unit);

  enum fc_tristate result;

  fc_assert_ret_val(actor_unit, FALSE);

  if (!utype_can_do_action(actor_unit->utype, action_id)) {
    /* The unit type can't perform the action. */
    return FALSE;
  }

  result = action_hard_reqs_actor(action_id,
                                  actor_player, actor_city, NULL,
                                  actor_tile, actor_unit, actor_unittype,
                                  NULL, NULL, FALSE);

  if (result == TRI_NO) {
    /* The hard requirements aren't fulfilled. */
    return FALSE;
  }

  action_enabler_list_iterate(action_enablers_for_action(action_id),
                              enabler) {
    const enum fc_tristate current
        = mke_eval_reqs(actor_player,
                        actor_player, NULL, actor_city, NULL, actor_tile,
                        actor_unit, NULL, NULL,
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

/**************************************************************************
  Returns action auto performer rule slot number num so it can be filled.
**************************************************************************/
struct action_auto_perf *action_auto_perf_slot_number(const int num)
{
  fc_assert_ret_val(num >= 0, NULL);
  fc_assert_ret_val(num < MAX_NUM_ACTION_AUTO_PERFORMERS, NULL);

  return &auto_perfs[num];
}

/**************************************************************************
  Returns action auto performer rule number num.

  Used in action_auto_perf_iterate()

  WARNING: If the cause of the returned action performer rule is
  AAPC_COUNT it means that it is unused.
**************************************************************************/
const struct action_auto_perf *action_auto_perf_by_number(const int num)
{
  return action_auto_perf_slot_number(num);
}
