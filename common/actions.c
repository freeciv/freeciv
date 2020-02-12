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

/* utility */
#include "astring.h"

/* common */
#include "actions.h"
#include "city.h"
#include "game.h"
#include "map.h"
#include "movement.h"
#include "unit.h"
#include "research.h"
#include "tile.h"

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

static struct action *actions[ACTION_COUNT];
static bool actions_initialized = FALSE;

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

static inline bool
action_prob_is_signal(const struct act_prob probability);
static inline bool
action_prob_not_relevant(const struct act_prob probability);
static inline bool
action_prob_not_impl(const struct act_prob probability);

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

  /* The actions them self are now initialized. */
  actions_initialized = TRUE;
}

/**************************************************************************
  Free the actions and the action enablers.
**************************************************************************/
void actions_free(void)
{
  /* Don't consider the actions to be initialized any longer. */
  actions_initialized = FALSE;

  action_iterate(act) {
    action_enabler_list_iterate(action_enablers_by_action[act], enabler) {
      requirement_vector_free(&enabler->actor_reqs);
      requirement_vector_free(&enabler->target_reqs);
      free(enabler);
    } action_enabler_list_iterate_end;

    action_enabler_list_destroy(action_enablers_by_action[act]);

    FC_FREE(actions[act]);
  } action_iterate_end;
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
                                 bool hostile)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  action->id = id;
  action->actor_kind = AAK_UNIT;
  action->target_kind = target_kind;
  action->hostile = hostile;

  /* Loaded from the ruleset. Until generalized actions are ready it has to
   * be defined seperatly from other action data. */
  action->ui_name[0] = '\0';
  action->quiet = FALSE;

  return action;
}

/**************************************************************************
  Returns TRUE iff the specified action ID refers to a valid action.
**************************************************************************/
bool action_id_is_valid(const int act_id)
{
  return gen_action_is_valid(act_id);
}

/**************************************************************************
  Return the action with the given id.

  Returns NULL if no action with the given id exists.
**************************************************************************/
struct action *action_by_number(int act_id)
{
  if (!action_id_is_valid(act_id)) {
    /* Nothing to return. */

    log_verbose("Asked for non existing action numbered %d", act_id);

    return NULL;
  }

  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return actions[act_id];
}

/**************************************************************************
  Get the actor kind of an action.
**************************************************************************/
enum action_actor_kind action_get_actor_kind(const struct action *paction)
{
  fc_assert_ret_val_msg(paction, AAK_COUNT, "Action doesn't exist.");

  return paction->actor_kind;
}

/**************************************************************************
  Get the target kind of an action.
**************************************************************************/
enum action_target_kind action_get_target_kind(
    const struct action *paction)
{
  fc_assert_ret_val_msg(paction, ATK_COUNT, "Action doesn't exist.");

  return paction->target_kind;
}

/**************************************************************************
  Returns TRUE iff the specified action is hostile.
**************************************************************************/
bool action_is_hostile(int act_id)
{
  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return actions[act_id]->hostile;
}

/**************************************************************************
  Get the rule name of the action.
**************************************************************************/
const char *action_id_rule_name(int act_id)
{
  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  return gen_action_name(act_id);
}

/**************************************************************************
  Get the action name used when displaying the action in the UI. Nothing
  is added to the UI name.
**************************************************************************/
const char *action_id_name_translation(int act_id)
{
  return action_prepare_ui_name(act_id, "", ACTPROB_NA, NULL);
}

/**************************************************************************
  Get the action name with a mnemonic ready to display in the UI.
**************************************************************************/
const char *action_get_ui_name_mnemonic(int act_id,
                                        const char* mnemonic)
{
  return action_prepare_ui_name(act_id, mnemonic, ACTPROB_NA, NULL);
}

/**************************************************************************
  Get the UI name ready to show the action in the UI. It is possible to
  add a client specific mnemonic; it is assumed that if the mnemonic
  appears in the action name it can be escaped by doubling.
  Success probability information is interpreted and added to the text.
  A custom text can be inserted before the probability information.
**************************************************************************/
const char *action_prepare_ui_name(int act_id, const char* mnemonic,
                                   const struct act_prob prob,
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
    fc_assert_ret_val_msg(action_id_is_valid(act_id),
                          "Invalid action",
                          "Invalid action %d", act_id);

    /* and no custom text will be inserted */
    fc_assert(custom == NULL || custom[0] == '\0');

    /* Make the best of what is known */
    astr_set(&str, _("%s%s (name may be wrong)"),
             mnemonic, gen_action_name(act_id));

    /* Return the guess. */
    return astr_str(&str);
  }

  /* How to interpret action probabilities like prob is documented in
   * fc_types.h */
  if (action_prob_is_signal(prob)) {
    fc_assert(action_prob_not_impl(prob)
              || action_prob_not_relevant(prob));

    /* Unknown because of missing server support or should not exits. */
    probtxt = NULL;
  } else {
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
    probtxt = astr_str(&chance);
  }

  /* Format the info part of the action's UI name. */
  if (probtxt != NULL && custom != NULL) {
    /* TRANS: action UI name's info part with custom info and probability.
     * Hint: you can move the paren handling from this sting to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s; %s)"), custom, probtxt);
  } else if (probtxt != NULL) {
    /* TRANS: action UI name's info part with probability.
     * Hint: you can move the paren handling from this sting to the action
     * names if you need to add extra information (like a mnemonic letter
     * that doesn't appear in the action UI name) to it. In that case you
     * must do so for all strings with this comment and for every action
     * name. To avoid a `()` when no UI name info part is added you have
     * to add the extra information to every action name or remove the
     * surrounding parens. */
    astr_set(&chance, _(" (%s)"), probtxt);
  } else if (custom != NULL) {
    /* TRANS: action UI name's info part with custom info.
     * Hint: you can move the paren handling from this sting to the action
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
    astr_set(&str, astr_str(&fmtstr), mnemonic,
             astr_str(&chance));

    astr_free(&fmtstr);
  }

  return astr_str(&str);
}

/**************************************************************************
  Get information about starting the action in the current situation.
  Suitable for a tool tip for the button that starts it.
**************************************************************************/
const char *action_get_tool_tip(const int act_id,
                                const struct act_prob prob)
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

/**************************************************************************
  Get the unit type role corresponding to the ability to do action.
**************************************************************************/
int action_get_role(int act_id)
{
  fc_assert_msg(actions[act_id], "Action %d don't exist.", act_id);

  fc_assert_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                "Action %s isn't performed by a unit",
                action_id_rule_name(act_id));

  return act_id + L_LAST;
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

  /* Make sure that action doesn't end up as a random value that happens to
   * be a valid action id. */
  enabler->action = ACTION_NONE;

  return enabler;
}

/**************************************************************************
  Add an action enabler to the current ruleset.
**************************************************************************/
void action_enabler_add(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be added. */
  fc_assert_ret(enabler);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret(action_id_is_valid(enabler->action));

  action_enabler_list_append(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**************************************************************************
  Remove an action enabler from the current ruleset.

  Returns TRUE on success.
**************************************************************************/
bool action_enabler_remove(struct action_enabler *enabler)
{
  /* Sanity check: a non existing action enabler can't be removed. */
  fc_assert_ret_val(enabler, FALSE);
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_is_valid(enabler->action), FALSE);

  return action_enabler_list_remove(
        action_enablers_for_action(enabler->action),
        enabler);
}

/**************************************************************************
  Get all enablers for an action in the current ruleset.
**************************************************************************/
struct action_enabler_list *
action_enablers_for_action(enum gen_action action)
{
  /* Sanity check: a non existing action doesn't have enablers. */
  fc_assert_ret_val(action_id_is_valid(action), NULL);

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
  Returns TRUE if the wanted action is possible given that an action
  enabler later will enable it.

  This is done by checking the action's hard requirements. Hard requirements
  must be TRUE before an action can be done. The reason why is usually that
  code dealing with the action assumes that the requirements are true. A
  requirement may also end up here if it can't be expressed in a requirement
  vector or if its abstence makes the action pointless.

  When adding a new hard requirement here:
   * explain why it is a hard requirement in a comment.
   * remember that this is called from action_prob(). Should information
     the player don't have access to be used in a test it must check if
     this evaluation is omniscient.
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
                               const struct specialist *target_specialist,
                               const bool omniscient,
                               const struct city *homecity)
{
  fc_assert_msg((action_id_get_target_kind(wanted_action) == ATK_CITY
                 && target_city != NULL)
                || (action_id_get_target_kind(wanted_action) == ATK_UNIT
                    && target_unit != NULL),
                "Missing target!");

  if (action_id_get_actor_kind(wanted_action) == AAK_UNIT) {
    /* The Freeciv code for all actions controlled by enablers assumes that
     * an acting unit is on the same tile as its target or on the tile next
     * to it. */
    if (real_map_distance(actor_tile, target_tile) > 1) {
      return FALSE;
    }
  }

  switch (action_id_get_target_kind(wanted_action)) {
  case ATK_UNIT:
    /* The Freeciv code for all actions that is controlled by action
     * enablers and targets a unit assumes that the acting
     * player can see the target unit. */
    if (!can_player_see_unit(actor_player, target_unit)) {
      return FALSE;
    }
    break;
  case ATK_CITY:
    /* The Freeciv code assumes that the player is aware of the target
     * city's existence. (How can you order an airlift to a city when you
     * don't know its city ID?) */
    if (!(plr_knows_tile(actor_player, city_tile(target_city)))) {
      return TRI_NO;
    }
    break;
  case ATK_COUNT:
    fc_assert(action_id_get_target_kind(wanted_action) != ATK_COUNT);
    break;
  }

  if (wanted_action == ACTION_ESTABLISH_EMBASSY
      || wanted_action == ACTION_SPY_INVESTIGATE_CITY
      || wanted_action == ACTION_SPY_STEAL_GOLD
      || wanted_action == ACTION_SPY_STEAL_TECH
      || wanted_action == ACTION_SPY_TARGETED_STEAL_TECH
      || wanted_action == ACTION_SPY_INCITE_CITY
      || wanted_action == ACTION_SPY_BRIBE_UNIT) {
    /* Why this is a hard requirement: There is currently no point in
     * allowing the listed actions against domestic targets.
     * (Possible counter argument: crazy hack involving the Lua callback
     * action_started_callback() to use an action to do something else. */
    /* Info leak: The actor player knows what targets he owns. */
    /* TODO: Move this restriction to the ruleset as a part of false flag
     * operation support. */
    if (actor_player == target_player) {
      return FALSE;
    }
  }

  /* Hard requirements for individual actions. */
  switch (wanted_action) {
  case ACTION_SPY_BRIBE_UNIT:
    /* Why this is a hard requirement: Can't transfer a unique unit if the
     * actor player already has one. */
    if (utype_player_already_has_this_unique(actor_player,
                                             target_unittype)) {
      return FALSE;
    }

    break;

  case ACTION_ESTABLISH_EMBASSY:
    /* Why this is a hard requirement: There is currently no point in
     * establishing an embassy when a real embassy already exists.
     * (Possible exception: crazy hack using the Lua callback
     * action_started_callback() to make establish embassy do something
     * else even if the UI still call the action Establish Embassy) */
    /* Info leak: The actor player known who he has a real embassy to. */
    if (player_has_real_embassy(actor_player, target_player)) {
      return FALSE;
    }

    break;

  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* Reason: The Freeciv code don't support selecting a target tech
     * unless it is known that the victim player has it. */
    /* Info leak: The actor player knowns who's techs he can see. */
    if (!can_see_techs_of_target(actor_player, target_player)) {
      return FALSE;
    }

    break;

  case ACTION_SPY_STEAL_GOLD:
    /* If actor_unit can do the action the actor_player can see how much
     * gold target_player have. Not requireing it is therefore pointless.
     */
    if (target_player->economic.gold <= 0) {
      return FALSE;
    }

    break;

  case ACTION_TRADE_ROUTE:
  case ACTION_MARKETPLACE:
    {
      /* It isn't possible to establish a trade route from a non existing
       * city. The Freeciv code assumes this applies to Enter Marketplace
       * too. */
      if (homecity == NULL) {
        return FALSE;
      }

      /* Can't establish a trade route or enter the market place if the
       * cities can't trade at all. */
      /* TODO: Should this restriction (and the above restriction that the
       * actor unit must have a home city) be kept for Enter Marketplace? */
      if (!can_cities_trade(homecity, target_city)) {
        return FALSE;
      }

      /* Allow a ruleset to forbid units from entering the marketplace if a
       * trade route can be established in stead. */
      if (wanted_action == ACTION_MARKETPLACE
          && game.info.force_trade_route
          && is_action_enabled_unit_on_city(ACTION_TRADE_ROUTE,
                                            actor_unit, target_city)) {
        return FALSE;
      }

      /* There are more restrictions on establishing a trade route than on
       * entering the market place. */
      if (wanted_action == ACTION_TRADE_ROUTE &&
          !can_establish_trade_route(homecity, target_city)) {
        return FALSE;
      }
    }

    break;

  case ACTION_HELP_WONDER:
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

    break;

  case ACTION_SPY_INVESTIGATE_CITY:
  case ACTION_SPY_POISON:
  case ACTION_SPY_SABOTAGE_CITY:
  case ACTION_SPY_TARGETED_SABOTAGE_CITY:
  case ACTION_SPY_STEAL_TECH:
  case ACTION_SPY_INCITE_CITY:
  case ACTION_SPY_SABOTAGE_UNIT:
    /* No known hard coded requirements. */
    break;
  case ACTION_COUNT:
    fc_assert(action_id_is_valid(wanted_action));
    break;
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
                              const struct specialist *target_specialist,
                              const struct city *homecity)
{
  if (!is_action_possible(wanted_action,
                          actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist,
                          TRUE, homecity)) {
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
static bool
is_action_enabled_unit_on_city_full(const action_id wanted_action,
                                    const struct unit *actor_unit,
                                    const struct city *actor_home,
                                    const struct tile *actor_tile,
                                    const struct city *target_city)
{
  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return FALSE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, FALSE);

  if (!unit_can_do_action(actor_unit, wanted_action)) {
    /* No point in continuing. */
    return FALSE;
  }

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), tile_city(actor_tile),
                           NULL, actor_tile,
                           actor_unit, unit_type_get(actor_unit),
                           NULL, NULL,
                           city_owner(target_city), target_city, NULL,
                           city_tile(target_city), NULL, NULL, NULL, NULL,
                           actor_home);
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
  return is_action_enabled_unit_on_city_full(wanted_action, actor_unit,
                                             unit_home(actor_unit),
                                             unit_tile(actor_unit),
                                             target_city);
}

/**************************************************************************
  Returns TRUE if actor_unit can do wanted_action to target_unit as far as
  action enablers are concerned.

  See note in is_action_enabled() for why the action still may be
  disabled.
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

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(wanted_action),
                        FALSE, "Action %s is performed by %s not %s",
                        gen_action_name(wanted_action),
                        action_actor_kind_name(
                          action_id_get_actor_kind(wanted_action)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT
                        == action_id_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not %s",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_id_get_target_kind(wanted_action)),
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
                           NULL, NULL,
                           unit_home(actor_unit));
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
    current = fc_tristate_and(mke_eval_reqs(actor_player, actor_player,
                                            target_player, actor_city,
                                            actor_building, actor_tile,
                                            actor_unit, actor_output,
                                            actor_specialist,
                                            &enabler->actor_reqs,
                                            RPT_CERTAIN),
                              mke_eval_reqs(actor_player, target_player,
                                            actor_player, target_city,
                                            target_building, target_tile,
                                            target_unit, target_output,
                                            target_specialist,
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
static struct act_prob ap_dipl_battle_win(const struct unit *pattacker,
                                          const struct unit *pdefender)
{
  struct city *pcity;

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
    const struct veteran_level *vatt =
        utype_veteran_level(unit_type_get(pattacker), pattacker->veteran);
    const struct veteran_level *vdef =
        utype_veteran_level(unit_type_get(pdefender), pdefender->veteran);

    chance += vatt->power_fact - vdef->power_fact;
  }

  /* City and base defense bonuses */
  if (tile_has_base_flag_for_unit(pdefender->tile, unit_type_get(pdefender),
                                  BF_DIPLOMAT_DEFENSE)) {
    chance -= chance * 25 / 100;
  }

  pcity = tile_city(pdefender->tile);
  if (pcity) {
    if (!is_effect_val_known(EFT_SPY_RESISTANT, unit_owner(pattacker),
                             city_owner(pcity),  NULL, pcity, NULL,
                             city_tile(pcity), NULL, NULL, NULL)) {
      return ACTPROB_NOT_KNOWN;
    }

    chance -= chance * get_city_bonus(tile_city(pdefender->tile),
                                      EFT_SPY_RESISTANT) / 100;
  }

  /* Convert to action probability */
  out.min = chance * ACTPROB_VAL_1_PCT;
  out.max = chance * ACTPROB_VAL_1_PCT;

  return out;
}

/**************************************************************************
  The action probability that pattacker will win a diplomatic battle.

  See diplomat_infiltrate_tile() in server/diplomats.c
**************************************************************************/
static struct act_prob ap_diplomat_battle(const struct unit *pattacker,
                                          const struct unit *pvictim,
                                          const struct tile *tgt_tile)
{
  fc_assert_ret_val(tgt_tile, ACTPROB_NOT_KNOWN);

  unit_list_iterate(tgt_tile->units, punit) {
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
  return ACTPROB_CERTAIN;
}

/**************************************************************************
  An action's probability of success.

  "Success" indicates that the action achieves its goal, not that the
  actor survives. For actions that cost money it is assumed that the
  player has and is willing to spend the money. This is so the player can
  figure out what his odds are before deciding to get the extra money.
**************************************************************************/
static struct act_prob
action_prob(const enum gen_action wanted_action,
            const struct player *actor_player,
            const struct city *actor_city,
            const struct impr_type *actor_building,
            const struct tile *actor_tile,
            const struct unit *actor_unit,
            const struct output_type *actor_output,
            const struct specialist *actor_specialist,
            const struct city *actor_home,
            const struct player *target_player,
            const struct city *target_city,
            const struct impr_type *target_building,
            const struct tile *target_tile,
            const struct unit *target_unit,
            const struct output_type *target_output,
            const struct specialist *target_specialist)
{
  int known;
  struct act_prob chance;
  const struct unit_type *actor_unittype;
  const struct unit_type *target_unittype;

  if (actor_unit == NULL) {
    actor_unittype = NULL;
  } else {
    actor_unittype = unit_type_get(actor_unit);
  }

  if (target_unit == NULL) {
    target_unittype = NULL;
  } else {
    target_unittype = unit_type_get(target_unit);
  }

  if (!is_action_possible(wanted_action,
                          actor_player, actor_city,
                          actor_building, actor_tile,
                          actor_unit, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile,
                          target_unit, target_unittype,
                          target_output, target_specialist,
                          FALSE, actor_home)) {
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
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit, target_tile);
    break;
  case ACTION_SPY_BRIBE_UNIT:
    /* All uncertainty comes from potential diplomatic battles. */
    chance = ap_diplomat_battle(actor_unit, target_unit, target_tile);
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
    chance = ACTPROB_CERTAIN;
    break;
  case ACTION_SPY_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor_player,
                                               target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTION_SPY_TARGETED_STEAL_TECH:
    /* Do the victim have anything worth taking? */
    known = fc_tristate_and(known,
                            tech_can_be_stolen(actor_player,
                                               target_player));

    /* TODO: Calculate actual chance */

    break;
  case ACTION_SPY_INVESTIGATE_CITY:
    /* There is no risk that the city won't get investigated. */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTION_TRADE_ROUTE:
    /* TODO */
    break;
  case ACTION_MARKETPLACE:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTION_HELP_WONDER:
    /* Possible when not blocked by is_action_possible() */
    chance = ACTPROB_CERTAIN;
    break;
  case ACTION_COUNT:
    fc_assert(wanted_action != ACTION_COUNT);
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

  fc_assert_ret_val_msg(FALSE, ACTPROB_NOT_IMPLEMENTED,
                        "Should be yes, maybe or no");
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
static struct act_prob
action_prob_vs_city_full(const struct unit* actor_unit,
                         const struct city *actor_home,
                         const struct tile *actor_tile,
                         const action_id act_id,
                         const struct city* target_city)
{
  if (actor_unit == NULL || target_city == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_CITY));

  fc_assert_ret_val(actor_tile, ACTPROB_IMPOSSIBLE);

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit,
                     NULL, NULL, actor_home,
                     city_owner(target_city), target_city, NULL,
                     city_tile(target_city), NULL, NULL, NULL);
}

/**************************************************************************
  Get the actor unit's probability of successfully performing the chosen
  action on the target city.
**************************************************************************/
struct act_prob action_prob_vs_city(const struct unit* actor_unit,
                                    const action_id act_id,
                                    const struct city* target_city)
{
  return action_prob_vs_city_full(actor_unit,
                                  unit_home(actor_unit),
                                  unit_tile(actor_unit),
                                  act_id, target_city);
}

/**********************************************************************//**
  Get the actor unit's probability of successfully performing the chosen
  action on the target unit.
**************************************************************************/
struct act_prob action_prob_vs_unit(const struct unit* actor_unit,
                                    const int act_id,
                                    const struct unit* target_unit)
{
  struct tile *actor_tile = unit_tile(actor_unit);

  if (actor_unit == NULL || target_unit == NULL) {
    /* Can't do an action when actor or target are missing. */
    return ACTPROB_IMPOSSIBLE;
  }

  fc_assert_ret_val_msg(AAK_UNIT == action_id_get_actor_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is performed by %s not %s",
                        gen_action_name(act_id),
                        action_actor_kind_name(
                          action_id_get_actor_kind(act_id)),
                        action_actor_kind_name(AAK_UNIT));

  fc_assert_ret_val_msg(ATK_UNIT == action_id_get_target_kind(act_id),
                        ACTPROB_IMPOSSIBLE,
                        "Action %s is against %s not %s",
                        gen_action_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)),
                        action_target_kind_name(ATK_UNIT));

  if (!unit_can_do_action(actor_unit, act_id)) {
    /* No point in continuing. */
    return ACTPROB_IMPOSSIBLE;
  }

  return action_prob(act_id,
                     unit_owner(actor_unit), tile_city(actor_tile),
                     NULL, actor_tile, actor_unit,
                     NULL, NULL,
                     unit_home(actor_unit),
                     unit_owner(target_unit),
                     tile_city(unit_tile(target_unit)), NULL,
                     unit_tile(target_unit),
                     target_unit, NULL, NULL);
}

/**********************************************************************//**
  Returns a speculation about the actor unit's probability of successfully
  performing the chosen action on the target city given the specified
  state changes.
**************************************************************************/
struct act_prob action_speculate_unit_on_city(const action_id act_id,
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
    if (is_action_enabled_unit_on_city_full(act_id,
                                            actor, actor_home, actor_tile,
                                            target)) {
      return ACTPROB_CERTAIN;
    } else {
      return ACTPROB_IMPOSSIBLE;
    }
  } else {
    return action_prob_vs_city_full(actor, actor_home, actor_tile,
                                    act_id, target);
  }
}

/**************************************************************************
  Returns the impossible action probability.
**************************************************************************/
struct act_prob action_prob_new_impossible(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MIN };

  return out;
}

/**************************************************************************
  Returns the certain action probability.
**************************************************************************/
struct act_prob action_prob_new_certain(void)
{
  struct act_prob out = { ACTPROB_VAL_MAX, ACTPROB_VAL_MAX };

  return out;
}

/**************************************************************************
  Returns the n/a action probability.
**************************************************************************/
struct act_prob action_prob_new_not_relevant(void)
{
  struct act_prob out = { ACTPROB_VAL_NA, ACTPROB_VAL_MIN};

  return out;
}

/**************************************************************************
  Returns the "not implemented" action probability.
**************************************************************************/
struct act_prob action_prob_new_not_impl(void)
{
  struct act_prob out = { ACTPROB_VAL_NOT_IMPL, ACTPROB_VAL_MIN };

  return out;
}

/**************************************************************************
  Returns the "user don't know" action probability.
**************************************************************************/
struct act_prob action_prob_new_unknown(void)
{
  struct act_prob out = { ACTPROB_VAL_MIN, ACTPROB_VAL_MAX };

  return out;
}

/**************************************************************************
  Returns TRUE iff the given action probability belongs to an action that
  may be possible.
**************************************************************************/
bool action_prob_possible(const struct act_prob probability)
{
  return (ACTPROB_VAL_MIN < probability.max
          || action_prob_not_impl(probability));
}

/**************************************************************************
  Returns TRUE iff the given action probability is certain that its action
  is possible.
**************************************************************************/
bool action_prob_certain(const struct act_prob probability)
{
  return (ACTPROB_VAL_MAX == probability.min
          && ACTPROB_VAL_MAX == probability.max);
}

/**************************************************************************
  Returns TRUE iff the given action probability represents the lack of
  an action probability.
**************************************************************************/
static inline bool
action_prob_not_relevant(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NA
         && probability.max == ACTPROB_VAL_MIN;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents that support
  for finding this action probability currently is missing from Freeciv.
**************************************************************************/
static inline bool
action_prob_not_impl(const struct act_prob probability)
{
  return probability.min == ACTPROB_VAL_NOT_IMPL
         && probability.max == ACTPROB_VAL_MIN;
}

/**************************************************************************
  Returns TRUE iff the given action probability represents a special
  signal value rather than a regular action probability value.
**************************************************************************/
static inline bool
action_prob_is_signal(const struct act_prob probability)
{
  return probability.max < probability.min;
}

/**************************************************************************
  Returns TRUE iff ap1 and ap2 are equal.
**************************************************************************/
bool are_action_probabilitys_equal(const struct act_prob *ap1,
                                   const struct act_prob *ap2)
{
  return ap1->min == ap2->min && ap1->max == ap2->max;
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
bool is_action_possible_on_city(const enum gen_action act_id,
                                const struct player *actor_player,
                                const struct city* target_city)
{
  fc_assert_ret_val_msg(ATK_CITY == action_id_get_target_kind(act_id),
                        FALSE, "Action %s is against %s not cities",
                        gen_action_name(act_id),
                        action_target_kind_name(
                          action_id_get_target_kind(act_id)));

  return is_target_possible(act_id, actor_player,
                            city_owner(target_city), target_city, NULL,
                            city_tile(target_city), NULL, NULL,
                            NULL, NULL);
}

/**************************************************************************
  Returns TRUE if the specified action can't be done now but would have
  been legal if the unit had full movement.
**************************************************************************/
bool action_mp_full_makes_legal(const struct unit *actor,
                                const int act_id)
{
  fc_assert(action_id_is_valid(act_id) || act_id == ACTION_ANY);

  /* Check if full movement points may enable the specified action. */
  return !utype_may_act_move_frags(unit_type_get(actor),
                                   act_id,
                                   actor->moves_left)
      && utype_may_act_move_frags(unit_type_get(actor),
                                  act_id,
                                  unit_move_rate(actor));
}
