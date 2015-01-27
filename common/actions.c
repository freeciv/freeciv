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

/* utility */
#include "astring.h"

/* common */
#include "actions.h"
#include "city.h"
#include "game.h"
#include "unit.h"
#include "research.h"
#include "tile.h"

static struct action *actions[ACTION_COUNT];
static struct action_enabler_list *action_enablers_by_action[ACTION_COUNT];

static struct action *action_new(enum gen_action id,
                                 enum action_target_kind target_kind,
                                 bool hostile);

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

/**************************************************************************
  Initialize the actions and the action enablers.
**************************************************************************/
void actions_init(void)
{
  /* Hard code the actions */
  actions[ACTION_SPY_POISON] = action_new(ACTION_SPY_POISON, ATK_CITY,
      TRUE);
  actions[ACTION_SPY_SABOTAGE_UNIT] =
      action_new(ACTION_SPY_SABOTAGE_UNIT, ATK_UNIT,
                 TRUE);
  actions[ACTION_SPY_BRIBE_UNIT] =
      action_new(ACTION_SPY_BRIBE_UNIT, ATK_UNIT,
                 TRUE);
  actions[ACTION_SPY_SABOTAGE_CITY] =
      action_new(ACTION_SPY_SABOTAGE_CITY, ATK_CITY,
                 TRUE);
  actions[ACTION_SPY_TARGETED_SABOTAGE_CITY] =
      action_new(ACTION_SPY_TARGETED_SABOTAGE_CITY, ATK_CITY,
                 TRUE);
  actions[ACTION_SPY_INCITE_CITY] =
      action_new(ACTION_SPY_INCITE_CITY, ATK_CITY,
                 TRUE);
  actions[ACTION_ESTABLISH_EMBASSY] =
      action_new(ACTION_ESTABLISH_EMBASSY, ATK_CITY,
                 FALSE);
  actions[ACTION_SPY_STEAL_TECH] =
      action_new(ACTION_SPY_STEAL_TECH, ATK_CITY,
                 TRUE);
  actions[ACTION_SPY_TARGETED_STEAL_TECH] =
      action_new(ACTION_SPY_TARGETED_STEAL_TECH, ATK_CITY,
                 TRUE);
  actions[ACTION_SPY_INVESTIGATE_CITY] =
      action_new(ACTION_SPY_INVESTIGATE_CITY, ATK_CITY,
                 TRUE);
  actions[ACTION_SPY_STEAL_GOLD] =
      action_new(ACTION_SPY_STEAL_GOLD, ATK_CITY,
                 TRUE);
  actions[ACTION_TRADE_ROUTE] =
      action_new(ACTION_TRADE_ROUTE, ATK_CITY,
                 FALSE);
  actions[ACTION_MARKETPLACE] =
      action_new(ACTION_MARKETPLACE, ATK_CITY,
                 FALSE);
  actions[ACTION_HELP_WONDER] =
      action_new(ACTION_HELP_WONDER, ATK_CITY,
                 FALSE);

  /* Initialize the action enabler list */
  action_iterate(act) {
    action_enablers_by_action[act] = action_enabler_list_new();
  } action_iterate_end;
}

/**************************************************************************
  Free the actions and the action enablers.
**************************************************************************/
void actions_free(void)
{
  action_iterate(act) {
    action_enabler_list_iterate(action_enablers_by_action[act], enabler) {
      requirement_vector_free(&enabler->actor_reqs);
      requirement_vector_free(&enabler->target_reqs);
      free(enabler);
    } action_enabler_list_iterate_end;

    action_enabler_list_destroy(action_enablers_by_action[act]);

    free(actions[act]);
  } action_iterate_end;
}

/**************************************************************************
  Create a new action.
**************************************************************************/
static struct action *action_new(enum gen_action id,
                                 enum action_target_kind target_kind,
                                 bool hostile)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  action->id = id;
  action->actor_kind = AAK_UNIT;
  action->target_kind = target_kind;
  action->hostile = hostile;

  /* The ui_name is loaded from the ruleset. Until generalized actions
   * are ready it has to be defined seperatly from other action data. */

  return action;
}

/**************************************************************************
  Return the action with the given id.
**************************************************************************/
struct action *action_by_number(int action_id)
{
  fc_assert_msg(actions[action_id], "Action %d don't exist.", action_id);

  return actions[action_id];
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

  /* How to interpret action probabilities like prob is documented in
   * actions.h */
  switch (prob) {
  case ACTPROB_NOT_KNOWN:
    /* Unknown because the player don't have the required knowledge to
     * determine the probability of success for this action. */

    /* TRANS: the chance of a diplomat action succeeding is unknown. */
    probtxt = _("?%");

    break;
  case ACTPROB_NOT_IMPLEMENTED:
    /* Unknown because of missing server support. */
    probtxt = NULL;

    break;
  case ACTPROB_NA:
    /* Should not exist */
    probtxt = NULL;

    break;
  case ACTPROB_IMPOSSIBLE:
    /* ACTPROB_IMPOSSIBLE is a 0% probability of success */
  default:
    /* Should be in the range 0 (0%) to 200 (100%) */
    fc_assert_msg(prob < 201,
                  "Diplomat action probability out of range");

    /* TRANS: the probability that a diplomat action will succeed. */
    astr_set(&chance, _("%.1f%%"), (double)prob / 2);
    probtxt = astr_str(&chance);

    break;
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

  switch (act_prob) {
  case ACTPROB_NOT_KNOWN:
    /* Missing in game knowledge. An in game action can change this. */
    astr_set(&tool_tip,
             _("Starting to do this may currently be impossible."));
    break;
  case ACTPROB_NOT_IMPLEMENTED:
    /* Missing server support. No in game action will change this. */
    astr_clear(&tool_tip);
    break;
  default:
    {
      /* The unit is 0.5% chance of success. */
      const double converted = (double)act_prob / 2.0;

      astr_set(&tool_tip, _("The probability of success is %.1f%%."),
               converted);
    }
    break;
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
  Add an action enabler.
**************************************************************************/
void action_enabler_add(struct action_enabler *enabler)
{
  action_enabler_list_append(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**************************************************************************
  Some actions have hard requirements that can be expressed as normal
  requirement vectors. Append those to the action enabler so the action
  struct won't need those fields.

  Reconsider this choice if many enablers for each action should become
  common.
**************************************************************************/
void action_enabler_append_hard(struct action_enabler *enabler)
{
  if (enabler->action != ACTION_TRADE_ROUTE
      && enabler->action != ACTION_MARKETPLACE
      && enabler->action != ACTION_HELP_WONDER) {
    /* The Freeciv code assumes that all spy actions have foreign targets.
     * TODO: Move this restriction to the ruleset to prepare for false flag
     * operations. */
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           TRUE, "Is foreign"));
  }
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
  Returns TRUE if the wanted action is possible given that an action
  enabler later will enable it.

  This is done by checking the action's hard requirements. Hard requirements
  must be TRUE before an action can be done. The reason why is usually that
  code dealing with the action assumes that the requirements are true. A
  requirement may also end up here if it can't be expressed in a requirment
  vector or if its abstence makes the action pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirment in a comment.
   * remember that this is called from action_prob(). Should information
     the player don't have access to be used in a test the way this
     function is used must change.
**************************************************************************/
static bool is_action_possible(const enum gen_action wanted_action,
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
  fc_assert_msg((action_get_target_kind(wanted_action) == ATK_CITY
                 && target_city != NULL)
                || (action_get_target_kind(wanted_action) == ATK_UNIT
                    && target_unit != NULL),
                "Missing target!");

  if (action_get_actor_kind(wanted_action) == AAK_UNIT) {
    /* The Freeciv code for all actions controlled by enablers assumes that
     * an acting unit is on the same tile as its target or on the tile next
     * to it. */
    if (real_map_distance(actor_tile, target_tile) > 1) {
      return FALSE;
    }
  }

  if (action_get_target_kind(wanted_action) == ATK_UNIT) {
    /* The Freeciv code for all actions with a unit target that is
     * controlled by action enablers assumes that the acting player can see
     * the target unit. */
    if (!can_player_see_unit(actor_player, target_unit)) {
      return FALSE;
    }
  }

  if (wanted_action == ACTION_ESTABLISH_EMBASSY) {
    /* Why this is a hard requirement: There is currently no point in
     * establishing an embassy when a real embassy already exists.
     * (Possible exception: crazy hack using the Lua callback
     * action_started_callback() to make establish embassy do something
     * else even if the UI still call the action Establish Embassy) */
    /* Info leak: The actor player known who he has a real embassy to. */
    if (player_has_real_embassy(actor_player, target_player)) {
      return FALSE;
    }
  }

  if (wanted_action == ACTION_SPY_TARGETED_STEAL_TECH) {
    /* Reason: The Freeciv code don't support selecting a target tech
     * unless it is known that the victim player has it. */
    /* Info leak: The actor player knowns who's techs he can see. */
    if (!can_see_techs_of_target(actor_player, target_player)) {
      return FALSE;
    }
  }

  if (wanted_action == ACTION_SPY_STEAL_GOLD) {
    /* If actor_unit can do the action the actor_player can see how much
     * gold target_player have. Not requireing it is therefore pointless.
     */
    if (target_player->economic.gold <= 0) {
      return FALSE;
    }
  }

  if (wanted_action == ACTION_TRADE_ROUTE ||
      wanted_action == ACTION_MARKETPLACE) {
    const struct city *actor_homecity;

    /* It isn't possible to establish a trade route from a non existing
     * city. The Freeciv code assumes this applies to Enter Marketplace
     * too. */
    if (!(actor_homecity = game_city_by_number(actor_unit->homecity))) {
      return FALSE;
    }

    /* Can't establish a trade route or enter the market place if the
     * cities can't trade at all. */
    /* TODO: Should this restriction (and the above restriction that the
     * actor unit must have a home city) be kept for Enter Marketplace? */
    if (!can_cities_trade(actor_homecity, target_city)) {
      return FALSE;
    }

    /* There are more restrictions on establishing a trade route than on
     * entering the market place. */
    if (wanted_action == ACTION_TRADE_ROUTE &&
        !can_establish_trade_route(actor_homecity, target_city)) {
      return FALSE;
    }
  }

  if (wanted_action == ACTION_HELP_WONDER) {
    /* It is only possible to help the production of a wonder. */
    /* Info leak: It is already known when a foreign city is building a
     * wonder. */
    /* TODO: Do this rule belong in the ruleset? */
    if (!(VUT_IMPROVEMENT == target_city->production.kind
        && is_wonder(target_city->production.value.building))) {
      return FALSE;
    }

    /* It is only possible to help the production if the production needs
     * the help. (If not it would be possible to add shields for a non
     * wonder if it is build after a wonder) */
    /* Info leak: No new information is sent with the old rules. When the
     * ruleset is changed to make helping foreign wonders legal the
     * information that a wonder have been hurried (bought, helped) leaks.
     * That a foreign wonder will be ready next turn (from work) is already
     * known. That it will be finished because of help is not. */
    if (!(target_city->shield_stock
          < impr_build_shield_cost(
            target_city->production.value.building))) {
      return FALSE;
    }
  }

  return TRUE;
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
                         actor_output, actor_specialist,
                         &enabler->actor_reqs, RPT_CERTAIN)
      && are_reqs_active(target_player, actor_player, target_city,
                         target_building, target_tile,
                         target_unit, target_unittype,
                         target_output, target_specialist,
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
  if (!is_action_possible(wanted_action,
                          actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist)) {
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

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), NULL, NULL,
                           unit_tile(actor_unit),
                           actor_unit, unit_type(actor_unit),
                           NULL, NULL,
                           city_owner(target_city), target_city, NULL,
                           city_tile(target_city), NULL, NULL, NULL, NULL);
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

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), NULL, NULL,
                           unit_tile(actor_unit),
                           actor_unit, unit_type(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit),
                           target_unit, unit_type(target_unit),
                           NULL, NULL);
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
                                    &enabler->actor_reqs),
                      mke_eval_reqs(actor_player, target_player,
                                    actor_player, target_city,
                                    target_building, target_tile,
                                    target_unit, target_output,
                                    target_specialist,
                                    &enabler->target_reqs));
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
                                   &(peffect->reqs))) {
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

  It is assumed that pattacker and pdefender have different owners.

  See diplomat_infiltrate_tile() in server/diplomats.c
**************************************************************************/
static action_probability ap_diplomat_battle(const struct unit *pattacker,
                                             const struct unit *pdefender)
{
  struct city *pcity;

  /* Keep unconverted until the end to avoid scaling each step */
  int chance;

  /* Superspy always win */
  if (unit_has_type_flag(pattacker, UTYF_SUPERSPY)) {
    return 200;
  }
  if (unit_has_type_flag(pdefender, UTYF_SUPERSPY)) {
    return ACTPROB_IMPOSSIBLE;
  }

  /* This target is defenseless */
  if (!unit_has_type_flag(pdefender, UTYF_DIPLOMAT)) {
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
        utype_veteran_level(unit_type(pattacker), pattacker->veteran);
    const struct veteran_level *vdef =
        utype_veteran_level(unit_type(pdefender), pdefender->veteran);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* City and base defense bonus */
  pcity = tile_city(pdefender->tile);
  if (pcity) {
    if (!is_effect_val_known(EFT_SPY_RESISTANT, unit_owner(pattacker),
                             city_owner(pcity),  NULL, pcity, NULL,
                             city_tile(pcity), NULL, NULL, NULL)) {
      return ACTPROB_NOT_KNOWN;
    }

    chance -= chance * get_city_bonus(tile_city(pdefender->tile),
                                      EFT_SPY_RESISTANT) / 100;
  } else {
    if (tile_has_base_flag_for_unit(pdefender->tile, unit_type(pdefender),
                                    BF_DIPLOMAT_DEFENSE)) {
      chance -= chance * 25 / 100;
    }
  }

  /* Convert to action probability */
  return chance * 2;
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
  int known;
  int chance;

  const struct unit_type *actor_unittype;
  const struct unit_type *target_unittype;

  if (actor_unit == NULL) {
    actor_unittype = NULL;
  } else {
    actor_unittype = unit_type(actor_unit);
  }

  if (target_unit == NULL) {
    target_unittype = NULL;
  } else {
    target_unittype = unit_type(target_unit);
  }

  if (!is_action_possible(wanted_action,
                          actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist)) {
    /* The action enablers are irrelevant since the action it self is
     * impossible. */
    return ACTPROB_IMPOSSIBLE;
  }

  chance = ACTPROB_NOT_IMPLEMENTED;

  known = action_enabled_local(wanted_action,
                               actor_player, actor_city,
                               actor_building, actor_tile, actor_unit,
                               actor_output, actor_specialist,
                               target_player, target_city,
                               target_building, target_tile, target_unit,
                               target_output, target_specialist);

  switch (wanted_action) {
  case ACTION_SPY_POISON:
    /* TODO */
    break;
  case ACTION_SPY_STEAL_GOLD:
    /* TODO */
    break;
  case ACTION_SPY_SABOTAGE_UNIT:
    /* Hard coded limit: The victim unit is alone at the tile */
    chance = ap_diplomat_battle(actor_unit, target_unit);
    break;
  case ACTION_SPY_BRIBE_UNIT:
    /* Hard coded limit: The target unit is alone at its tile.
     * It won't fight a diplomatic battle. */
    chance = 200;
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
                        "Should be yes, mabe or no");
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
action_probability action_prob_vs_city(struct unit* actor_unit,
                                       int action_id,
                                       struct city* target_city)
{
  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(action_id),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_CITY));

  return action_prob(action_id,
                     unit_owner(actor_unit), NULL, NULL,
                     unit_tile(actor_unit), actor_unit,
                     NULL, NULL,
                     city_owner(target_city), target_city, NULL,
                     city_tile(target_city), NULL, NULL, NULL);
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
action_probability action_prob_vs_unit(struct unit* actor_unit,
                                       int action_id,
                                       struct unit* target_unit)
{
  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_get_actor_kind(action_id),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(action_id),
                        action_actor_kind_name(
                          action_get_actor_kind(action_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_get_target_kind(action_id),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(action_id),
                        action_target_kind_name(
                          action_get_target_kind(action_id)),
                        action_target_kind_name(ATK_UNIT));

  return action_prob(action_id,
                     unit_owner(actor_unit), NULL, NULL,
                     unit_tile(actor_unit), actor_unit,
                     NULL, NULL,
                     unit_owner(target_unit),
                     tile_city(unit_tile(target_unit)), NULL,
                     unit_tile(target_unit),
                     target_unit, NULL, NULL);
}

/**************************************************************************
  Returns TRUE iff the given action probability belongs to an action that
  may be possible.
**************************************************************************/
bool action_prob_possible(action_probability probability)
{
  return ACTPROB_IMPOSSIBLE != probability;
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
                        target_output, target_specialist,
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
