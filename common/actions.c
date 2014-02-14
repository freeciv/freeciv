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

/* common */
#include "actions.h"
#include "city.h"
#include "unit.h"
#include "tile.h"

static struct action *actions[ACTION_COUNT];
static struct action_enabler_list *action_enablers_by_action[ACTION_COUNT];

static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist);

/**************************************************************************
  Initialize the actions and the action enablers.
**************************************************************************/
void actions_init(void)
{
  action_iterate(act) {
    actions[act] = action_new();

    actions[act]->id = act;

    if (act == ACTION_SPY_SABOTAGE_UNIT ||
        act == ACTION_SPY_BRIBE_UNIT) {
      actions[act]->target_kind = ATK_UNIT;
    } else {
      actions[act]->target_kind = ATK_CITY;
    }

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
struct action *action_new(void)
{
  struct action *action;

  action = fc_malloc(sizeof(*action));

  return action;
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
  /* All actions that currently use action enablers are spy actions. */
  requirement_vector_append(&enabler->actor_reqs,
                            req_from_str("UnitFlag", "Local", FALSE,
                                         TRUE, "Diplomat"));

  if (enabler->action == ACTION_ESTABLISH_EMBASSY) {
    requirement_vector_append(&enabler->actor_reqs,
                              req_from_str("DiplRel", "Local", FALSE,
                                           FALSE, "Has real embassy"));
  }

  /* The Freeciv code assumes that the victim is alone at its tile. */
  if (enabler->action == ACTION_SPY_BRIBE_UNIT
      || enabler->action == ACTION_SPY_SABOTAGE_UNIT) {
    requirement_vector_append(&enabler->target_reqs,
                              req_from_str("MaxUnitsOnTile", "Local",
                                           FALSE, TRUE, "1"));
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
  Return TRUE iff the action enabler is active
**************************************************************************/
static bool is_enabler_active(const struct action_enabler *enabler,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist)
{
  return are_reqs_active(actor_player, target_player, actor_city,
                         actor_building, actor_tile, actor_unittype,
                         actor_output, actor_specialist,
                         &enabler->actor_reqs, RPT_CERTAIN)
      && are_reqs_active(target_player, actor_player, target_city,
                         target_building, target_tile, target_unittype,
                         target_output, target_specialist,
                         &enabler->target_reqs, RPT_CERTAIN);
}

/**************************************************************************
  Returns TRUE if the wanted action is enabled by an action enabler.

  Note that the action may disable it self by doing its own tests after
  this returns TRUE. This is because some actions have preconditions
  that can't be expressed in an action enabler's requirement vector.
  Should a precondition become expressible in an action enabler's
  requirement vector please move it.
**************************************************************************/
static bool is_action_enabled(const enum gen_action wanted_action,
			      const struct player *actor_player,
			      const struct city *actor_city,
			      const struct impr_type *actor_building,
			      const struct tile *actor_tile,
			      const struct unit_type *actor_unittype,
			      const struct output_type *actor_output,
			      const struct specialist *actor_specialist,
			      const struct player *target_player,
			      const struct city *target_city,
			      const struct impr_type *target_building,
			      const struct tile *target_tile,
			      const struct unit_type *target_unittype,
			      const struct output_type *target_output,
			      const struct specialist *target_specialist)
{
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    if (is_enabler_active(enabler, actor_player, actor_city,
                          actor_building, actor_tile, actor_unittype,
                          actor_output, actor_specialist,
                          target_player, target_city,
                          target_building, target_tile, target_unittype,
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
  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not cities",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)));

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), NULL, NULL,
                           unit_tile(actor_unit), unit_type(actor_unit),
                           NULL, NULL,
                           city_owner(target_city), target_city, NULL,
                           city_tile(target_city), NULL, NULL, NULL);
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
  fc_assert_ret_val_msg(ATK_UNIT == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not units",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)));

  return is_action_enabled(wanted_action,
                           unit_owner(actor_unit), NULL, NULL,
                           unit_tile(actor_unit), unit_type(actor_unit),
                           NULL, NULL,
                           unit_owner(target_unit),
                           tile_city(unit_tile(target_unit)), NULL,
                           unit_tile(target_unit), unit_type(target_unit),
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

  Returns MKE_TRUE if the action is enabled, MKE_FALSE if it isn't and
  MKE_UNCERTAIN if the player don't know enough to tell.

  If meta knowledge is missing MKE_UNCERTAIN will be returned.
**************************************************************************/
static enum mk_eval_result
action_enabled_local(const enum gen_action wanted_action,
                     const struct player *actor_player,
                     const struct city *actor_city,
                     const struct impr_type *actor_building,
                     const struct tile *actor_tile,
                     const struct unit_type *actor_unittype,
                     const struct output_type *actor_output,
                     const struct specialist *actor_specialist,
                     const struct player *target_player,
                     const struct city *target_city,
                     const struct impr_type *target_building,
                     const struct tile *target_tile,
                     const struct unit_type *target_unittype,
                     const struct output_type *target_output,
                     const struct specialist *target_specialist)
{
  enum mk_eval_result current;
  enum mk_eval_result result;

  result = MKE_FALSE;
  action_enabler_list_iterate(action_enablers_for_action(wanted_action),
                              enabler) {
    current = mke_and(mke_eval_reqs(actor_player, actor_player,
                                    target_player, actor_city,
                                    actor_building, actor_tile,
                                    actor_unittype, actor_output,
                                    actor_specialist,
                                    &enabler->actor_reqs),
                      mke_eval_reqs(actor_player, target_player,
                                    actor_player, target_city,
                                    target_building, target_tile,
                                    target_unittype, target_output,
                                    target_specialist,
                                    &enabler->target_reqs));
    if (current == MKE_TRUE) {
      return MKE_TRUE;
    } else if (current == MKE_UNCERTAIN) {
      result = MKE_UNCERTAIN;
    }
  } action_enabler_list_iterate_end;

  return result;
}

/**************************************************************************
  Find out if the action is enabled, may be enabled or isn't enabled given
  what the actor units owner knowns when done by actor_unit on target_city.
**************************************************************************/
enum mk_eval_result
action_enabled_unit_on_city_local(const enum gen_action wanted_action,
                                  const struct unit *actor_unit,
                                  const struct city *target_city)
{
  fc_assert_ret_val_msg(ATK_CITY == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not cities",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)));

  return action_enabled_local(wanted_action,
                              unit_owner(actor_unit), NULL, NULL,
                              unit_tile(actor_unit), unit_type(actor_unit),
                              NULL, NULL,
                              city_owner(target_city), target_city, NULL,
                              city_tile(target_city), NULL, NULL, NULL);
}

/**************************************************************************
  Find out if the action is enabled, may be enabled or isn't enabled given
  what the actor units owner knowns when done by actor_unit on target_unit.
**************************************************************************/
enum mk_eval_result
action_enabled_unit_on_unit_local(const enum gen_action wanted_action,
                                  const struct unit *actor_unit,
                                  const struct unit *target_unit)
{
  fc_assert_ret_val_msg(ATK_UNIT == action_get_target_kind(wanted_action),
                        FALSE, "Action %s is against %s not units",
                        gen_action_name(wanted_action),
                        action_target_kind_name(
                          action_get_target_kind(wanted_action)));

  return action_enabled_local(wanted_action,
                              unit_owner(actor_unit), NULL, NULL,
                              unit_tile(actor_unit), unit_type(actor_unit),
                              NULL, NULL,
                              unit_owner(target_unit),
                              tile_city(unit_tile(target_unit)), NULL,
                              unit_tile(target_unit),
                              unit_type(target_unit), NULL, NULL);
}
