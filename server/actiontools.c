/***********************************************************************
 Freeciv - Copyright (C) 1996-2015 - Freeciv Development Team
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
#include "rand.h"

/* common */
#include "actions.h"

/* server */
#include "aiiface.h"
#include "nation.h"
#include "notify.h"
#include "plrhand.h"
#include "unithand.h"
#include "unittools.h"

#include "actiontools.h"

typedef void (*action_notify)(struct player *,
                              const struct action *,
                              struct player *,
                              struct player *,
                              const struct tile *,
                              const char *);

/**********************************************************************//**
  Wipe an actor if the action it successfully performed consumed it.
**************************************************************************/
static void action_success_actor_consume(struct action *paction,
                                         int actor_id, struct unit *actor)
{
  if (unit_is_alive(actor_id)
      && utype_is_consumed_by_action(paction, unit_type_get(actor))) {
    if (action_has_result(paction, ACTRES_DISBAND_UNIT)
        || action_has_result(paction, ACTRES_DISBAND_UNIT_RECOVER)) {
      wipe_unit(actor, ULR_DISBANDED, NULL);
    } else if (action_has_result(paction, ACTRES_NUKE)
               || action_has_result(paction, ACTRES_NUKE_UNITS)) {
      wipe_unit(actor, ULR_DETONATED, NULL);
    } else if (action_has_result(paction, ACTRES_ATTACK)
               || action_has_result(paction, ACTRES_WIPE_UNITS)) {
      wipe_unit(actor, ULR_MISSILE, NULL);
    } else {
      wipe_unit(actor, ULR_USED, NULL);
    }
  }
}

/**********************************************************************//**
  Pay the movement point cost of success.
**************************************************************************/
static void action_success_pay_mp(struct action *paction,
                                  int actor_id, struct unit *actor)
{
  if (unit_is_alive(actor_id)) {
    int spent_mp = unit_pays_mp_for_action(paction, actor);
    actor->moves_left = MAX(0, actor->moves_left - spent_mp);
    send_unit_info(NULL, actor);
  }
}

/**********************************************************************//**
  Pay the movement point price of being the target of an action.
**************************************************************************/
void action_success_target_pay_mp(struct action *paction,
                                  int target_id, struct unit *target)
{
  if (unit_is_alive(target_id)) {
    int spent_mp = get_target_bonus_effects(
        NULL,
        &(const struct req_context) {
          .player = unit_owner(target),
          .city = unit_tile(target) ? tile_city(unit_tile(target)) : NULL,
          .tile = unit_tile(target),
          .unit = target,
          .unittype = unit_type_get(target),
          .action = paction,
        },
        NULL,
        EFT_ACTION_SUCCESS_TARGET_MOVE_COST);

    target->moves_left = MAX(0, target->moves_left - spent_mp);
    send_unit_info(NULL, target);
  }
}

/**********************************************************************//**
  Make the actor that successfully performed the action pay the price.
**************************************************************************/
void action_success_actor_price(struct action *paction,
                                int actor_id, struct unit *actor)
{
  action_success_actor_consume(paction, actor_id, actor);
  action_success_pay_mp(paction, actor_id, actor);
}

/**********************************************************************//**
  Give the victim a casus belli against the offender.
**************************************************************************/
static void action_give_casus_belli(struct player *offender,
                                    struct player *victim_player,
                                    const bool int_outrage)
{
  if (int_outrage) {
    /* This action is seen as a reason for any other player, no matter who
     * the victim was, to declare war on the actor. It could be used to
     * label certain actions atrocities in rule sets where international
     * outrage over an action fits the setting. */

    players_iterate(oplayer) {
      if (oplayer != offender) {
        player_diplstate_get(oplayer, offender)->has_reason_to_cancel = 2;
        player_update_last_war_action(oplayer);
      }
    } players_iterate_end;
  } else if (victim_player && offender != victim_player) {
    /* If an unclaimed tile is nuked there is no victim to give casus
     * belli. If an actor nukes their own tile, they are more than willing
     * to forgive themself. */

    /* Give the victim player a casus belli. */
    player_diplstate_get(victim_player, offender)->has_reason_to_cancel =
        2;
    player_update_last_war_action(victim_player);
  }
  player_update_last_war_action(offender);
}

/**********************************************************************//**
  Take care of any consequences (like casus belli) of the given action
  when the situation was as specified.

  victim_player can be NULL
**************************************************************************/
static void action_consequence_common(const struct action *paction,
                                      struct player *offender,
                                      const struct unit_type *offender_utype,
                                      struct player *victim_player,
                                      const struct tile *victim_tile,
                                      const char *victim_link,
                                      const action_notify notify_actor,
                                      const action_notify notify_victim,
                                      const action_notify notify_global,
                                      const enum effect_type eft)
{
  enum casus_belli_range cbr;

  cbr = casus_belli_range_for(offender, offender_utype, victim_player,
                              eft, paction, victim_tile);

  if (cbr >= CBR_VICTIM_ONLY) {
    /* In this situation the specified action provides a casus belli
     * against the actor. */

    /* International outrage: This isn't just between the offender and the
     * victim. */
    const bool int_outrage = (cbr == CBR_INTERNATIONAL_OUTRAGE);

    /* Notify the involved players by sending them a message. */
    notify_actor(offender, paction, offender, victim_player,
                 victim_tile, victim_link);
    notify_victim(victim_player, paction, offender, victim_player,
                  victim_tile, victim_link);

    if (int_outrage) {
      /* Every other player gets a casus belli against the actor. Tell each
       * players about it. */
      players_iterate(oplayer) {
        notify_global(oplayer, paction, offender,
                      victim_player, victim_tile, victim_link);
      } players_iterate_end;
    }

    /* Give casus belli. */
    action_give_casus_belli(offender, victim_player, int_outrage);

    /* Notify players controlled by the built in AI. */
    call_incident(INCIDENT_ACTION, cbr, paction, offender, victim_player);

    /* Update the clients. */
    send_player_all_c(offender, NULL);

    if (victim_player != NULL && victim_player != offender) {
      /* The actor player was just sent. */
      /* An action against an ownerless tile is victimless. */
      send_player_all_c(victim_player, NULL);
    }
  }
}

/**********************************************************************//**
  Notify the actor that the failed action gave the victim a casus belli
  against the actor.
**************************************************************************/
static void notify_actor_caught(struct player *receiver,
                                const struct action *paction,
                                struct player *offender,
                                struct player *victim_player,
                                const struct tile *victim_tile,
                                const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to themself. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke ... San Francisco */
                  _("You have caused an diplomatic incident getting caught"
                    " trying to do %s to %s."),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_STACK:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Bribe Enemy Unit ... American ... Partisan */
                  _("You have caused an diplomatic incident getting caught"
                    " trying to do %s to %s %s."),
                  action_name_translation(paction),
                  nation_adjective_for_player(victim_player),
                  victim_link);
    break;
  case ATK_TILE:
  case ATK_EXTRAS:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Explode Nuclear ... (54, 26) */
                  _("You have caused an diplomatic incident getting caught"
                    " trying to do %s at %s."),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_SELF:
    /* Special actor notice not needed. Actor is victim. */
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**********************************************************************//**
  Notify the victim that the failed action gave the victim a
  casus belli against the actor.
**************************************************************************/
static void notify_victim_caught(struct player *receiver,
                                 const struct action *paction,
                                 struct player *offender,
                                 struct player *victim_player,
                                 const struct tile *victim_tile,
                                 const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to themself. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke ... San Francisco */
                  _("The %s have caused an diplomatic incident getting caught"
                    " trying to do %s to %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_STACK:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Bribe Enemy Unit ... Partisan */
                  _("The %s have caused an diplomatic incident getting caught"
                    " trying to do %s to your %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_TILE:
  case ATK_EXTRAS:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Explode Nuclear ... (54, 26) */
                  _("The %s have caused an diplomatic incident getting caught"
                    " trying to do %s at %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_SELF:
    /* Special victim notice not needed. Actor is victim. */
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**********************************************************************//**
  Notify the world that the failed action gave the everyone a casus belli
  against the actor.
**************************************************************************/
static void notify_global_caught(struct player *receiver,
                                 const struct action *paction,
                                 struct player *offender,
                                 struct player *victim_player,
                                 const struct tile *victim_tile,
                                 const char *victim_link)
{
  if (receiver == offender) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke */
                  _("Getting caught while trying to do %s gives "
                    "everyone a casus belli against you."),
                  action_name_translation(paction));
  } else if (receiver == victim_player) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke ... Europeans */
                  _("Getting caught while trying to do %s to you gives "
                    "everyone a casus belli against the %s."),
                  action_name_translation(paction),
                  nation_plural_for_player(offender));
  } else if (victim_player == NULL) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke */
                  _("You now have a casus belli against the %s. "
                    "They got caught trying to do %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction));
  } else {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke ... Americans */
                  _("You now have a casus belli against the %s. "
                    "They got caught trying to do %s to the %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  nation_plural_for_player(victim_player));
  }
}

/**********************************************************************//**
  Take care of any consequences (like casus belli) of getting caught while
  trying to perform the given action.

  victim_player can be NULL
**************************************************************************/
void action_consequence_caught(const struct action *paction,
                               struct player *offender,
                               const struct unit_type *offender_utype,
                               struct player *victim_player,
                               const struct tile *victim_tile,
                               const char *victim_link)
{
  action_consequence_common(paction, offender, offender_utype,
                            victim_player, victim_tile, victim_link,
                            notify_actor_caught,
                            notify_victim_caught,
                            notify_global_caught,
                            EFT_CASUS_BELLI_CAUGHT);
}

/**********************************************************************//**
  Notify the actor that the performed action gave the victim a casus belli
  against the actor.
**************************************************************************/
static void notify_actor_success(struct player *receiver,
                                 const struct action *paction,
                                 struct player *offender,
                                 struct player *victim_player,
                                 const struct tile *victim_tile,
                                 const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to themself. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke ... San Francisco */
                  _("You have caused an diplomatic incident doing %s to %s."),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_STACK:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRAND: Bribe Enemy Unit ... American ... Partisan */
                  _("You have caused an diplomatic incident doing %s to %s %s."),
                  action_name_translation(paction),
                  nation_adjective_for_player(victim_player),
                  victim_link);
    break;
  case ATK_TILE:
  case ATK_EXTRAS:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Explode Nuclear ... (54, 26) */
                  _("You have caused an diplomatic incident doing %s at %s."),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_SELF:
    /* Special actor notice not needed. Actor is victim. */
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**********************************************************************//**
  Notify the victim that the performed action gave the victim a casus
  belli against the actor.
**************************************************************************/
static void notify_victim_success(struct player *receiver,
                                  const struct action *paction,
                                  struct player *offender,
                                  struct player *victim_player,
                                  const struct tile *victim_tile,
                                  const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to themself. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(paction)) {
  case ATK_CITY:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke ... San Francisco */
                  _("The %s have caused an diplomatic incident doing %s to %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_STACK:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Bribe Enemy Unit ... Partisan */
                  _("The %s have caused an diplomatic incident doing "
                    "%s to your %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_TILE:
  case ATK_EXTRAS:
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Explode Nuclear ... (54, 26) */
                  _("The %s have caused an diplomatic incident doing %s at %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  victim_link);
    break;
  case ATK_SELF:
    /* Special victim notice not needed. Actor is victim. */
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**********************************************************************//**
  Notify the world that the performed action gave the everyone a casus
  belli against the actor.
**************************************************************************/
static void notify_global_success(struct player *receiver,
                                  const struct action *paction,
                                  struct player *offender,
                                  struct player *victim_player,
                                  const struct tile *victim_tile,
                                  const char *victim_link)
{
  if (receiver == offender) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke */
                  _("Doing %s gives everyone a casus belli against you."),
                  action_name_translation(paction));
  } else if (receiver == victim_player) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Suitcase Nuke ... Europeans */
                  _("Doing %s to you gives everyone a casus belli against "
                    "the %s."),
                  action_name_translation(paction),
                  nation_plural_for_player(offender));
  } else if (victim_player == NULL) {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke */
                  _("You now have a casus belli against the %s. "
                    "They did %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction));
  } else {
    notify_player(receiver, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  /* TRANS: Europeans ... Suitcase Nuke ... Americans */
                  _("You now have a casus belli against the %s. "
                    "They did %s to the %s."),
                  nation_plural_for_player(offender),
                  action_name_translation(paction),
                  nation_plural_for_player(victim_player));
  }
}

/**********************************************************************//**
  Take care of any consequences (like casus belli) of successfully
  performing the given action.

  victim_player can be NULL
**************************************************************************/
void action_consequence_success(const struct action *paction,
                                struct player *offender,
                                const struct unit_type *offender_utype,
                                struct player *victim_player,
                                const struct tile *victim_tile,
                                const char *victim_link)
{
  action_consequence_common(paction, offender, offender_utype,
                            victim_player, victim_tile, victim_link,
                            notify_actor_success,
                            notify_victim_success,
                            notify_global_success,
                            EFT_CASUS_BELLI_SUCCESS);
}

/**************************************************************************
  Take care of any consequences (like casus belli) of successfully
  completing the given action.

  victim_player can be NULL
**************************************************************************/
void action_consequence_complete(const struct action *paction,
                                 struct player *offender,
                                 const struct unit_type *offender_utype,
                                 struct player *victim_player,
                                 const struct tile *victim_tile,
                                 const char *victim_link)
{
  action_consequence_common(paction, offender, offender_utype,
                            victim_player, victim_tile, victim_link,
                            notify_actor_success,
                            notify_victim_success,
                            notify_global_success,
                            EFT_CASUS_BELLI_COMPLETE);
}

/**********************************************************************//**
  Returns TRUE iff, from the point of view of the owner of the actor unit,
  it looks like the actor unit may be able to do any action to the target
  city.

  If the owner of the actor unit doesn't have the knowledge needed to know
  for sure if the unit can act TRUE will be returned.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
static bool may_unit_act_vs_city(struct unit *actor, struct city *target,
                                 bool accept_all_actions)
{
  const struct civ_map *nmap = &(wld.map);

  if (actor == NULL || target == NULL) {
    /* Can't do any actions if actor or target are missing. */
    return FALSE;
  }

  action_iterate(act) {
    if (!(action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_CITY)) {
      /* Not a relevant action. */
      continue;
    }

    if (action_id_is_rare_pop_up(act) && !accept_all_actions) {
      /* Not relevant since not accepted here. */
      continue;
    }

    if (action_prob_possible(action_prob_vs_city(nmap, actor, act, target))) {
      /* The actor unit may be able to do this action to the target
       * city. */
      return TRUE;
    }
  } action_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Find a city to target for an action on the specified tile.

  Returns NULL if no proper target is found.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
struct city *action_tgt_city(struct unit *actor, struct tile *target_tile,
                             bool accept_all_actions)
{
  struct city *target = tile_city(target_tile);

  if (target && may_unit_act_vs_city(actor, target, accept_all_actions)) {
    /* It may be possible to act against this city. */
    return target;
  }

  return NULL;
}

/**********************************************************************//**
  Returns TRUE iff, from the point of view of the owner of the actor unit,
  it looks like the actor unit may be able to do any action to the target
  unit.

  If the owner of the actor unit doesn't have the knowledge needed to know
  for sure if the unit can act TRUE will be returned.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
static bool may_unit_act_vs_unit(struct unit *actor, struct unit *target,
                                 bool accept_all_actions)
{
  const struct civ_map *nmap = &(wld.map);

  if (actor == NULL || target == NULL) {
    /* Can't do any actions if actor or target are missing. */
    return FALSE;
  }

  action_iterate(act) {
    if (!(action_id_get_actor_kind(act) == AAK_UNIT
        && action_id_get_target_kind(act) == ATK_UNIT)) {
      /* Not a relevant action. */
      continue;
    }

    if (action_id_is_rare_pop_up(act) && !accept_all_actions) {
      /* Not relevant since not accepted here. */
      continue;
    }

    if (action_prob_possible(action_prob_vs_unit(nmap, actor, act, target))) {
      /* The actor unit may be able to do this action to the target
       * unit. */
      return TRUE;
    }
  } action_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Find a unit to target for an action at the specified tile.

  Returns the first unit found at the tile that the actor may act against
  or NULL if no proper target is found.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
struct unit *action_tgt_unit(struct unit *actor, struct tile *target_tile,
                             bool accept_all_actions)
{
  unit_list_iterate(target_tile->units, target) {
    if (may_unit_act_vs_unit(actor, target, accept_all_actions)) {
      return target;
    }
  } unit_list_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Returns the tile iff it, from the point of view of the owner of the
  actor unit, looks like a target tile.

  Returns NULL if the player knows that the actor unit can't do any
  action (that specifies its target as a tile) to the tile.

  If the owner of the actor unit doesn't have the knowledge needed to know
  for sure if the unit can act the tile will be returned.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
struct tile *action_tgt_tile(struct unit *actor,
                             struct tile *target,
                             const struct extra_type *target_extra,
                             bool accept_all_actions)
{
  const struct civ_map *nmap = &(wld.map);

  if (actor == NULL || target == NULL) {
    /* Can't do any actions if actor or target are missing. */
    return NULL;
  }

  action_iterate(act) {
    struct act_prob prob;

    if (action_id_get_actor_kind(act) != AAK_UNIT) {
      /* Not a relevant action. */
      continue;
    }

    if (action_id_is_rare_pop_up(act) && !accept_all_actions) {
      /* Not relevant since not accepted here. */
      continue;
    }

    switch (action_id_get_target_kind(act)) {
    case ATK_TILE:
      prob = action_prob_vs_tile(nmap, actor, act, target, target_extra);
      break;
    case ATK_EXTRAS:
      prob = action_prob_vs_extras(nmap, actor, act, target, target_extra);
      break;
    case ATK_STACK:
      prob = action_prob_vs_stack(nmap, actor, act, target);
      break;
    case ATK_CITY:
    case ATK_UNIT:
    case ATK_SELF:
      /* Target not specified by tile. */
      continue;
    case ATK_COUNT:
      /* Invalid target kind */
      fc_assert(action_id_get_target_kind(act) != ATK_COUNT);
      continue;
    }

    if (action_prob_possible(prob)) {
      /* The actor unit may be able to do this action to the target
       * tile. */
      return target;
    }
  } action_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Returns TRUE iff, from the point of view of the owner of the actor unit,
  it looks like the actor unit may be able to do any action to the target
  extra located at the target tile.

  If the owner of the actor unit doesn't have the knowledge needed to know
  for sure if the unit can act TRUE will be returned.

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
static bool may_unit_act_vs_tile_extra(const struct unit *actor,
                                       const struct tile *tgt_tile,
                                       const struct extra_type *tgt_extra,
                                       bool accept_all_actions)
{
  const struct civ_map *nmap = &(wld.map);

  if (actor == NULL || tgt_tile == NULL || tgt_extra == NULL) {
    /* Can't do any actions if actor or target are missing. */
    return FALSE;
  }

  action_iterate(act) {
    if (!(action_id_get_actor_kind(act) == AAK_UNIT
          && (action_id_get_target_kind(act) == ATK_TILE
              || action_id_get_target_kind(act) == ATK_EXTRAS)
          && action_id_has_complex_target(act))) {
      /* Not a relevant action. */
      continue;
    }

    if (action_id_is_rare_pop_up(act) && !accept_all_actions) {
      /* Not relevant since not accepted here. */
      continue;
    }

    switch (action_id_get_target_kind(act)) {
    case ATK_TILE:
      if (action_prob_possible(action_prob_vs_tile(nmap, actor, act,
                                                   tgt_tile, tgt_extra))) {
        /* The actor unit may be able to do this action to the target
         * extra. */
        return TRUE;
      }
      break;
    case ATK_EXTRAS:
      if (action_prob_possible(action_prob_vs_extras(nmap, actor, act,
                                                     tgt_tile,
                                                     tgt_extra))) {
        /* The actor unit may be able to do this action to the target
         * extra. */
        return TRUE;
      }
      break;
    case ATK_CITY:
    case ATK_UNIT:
    case ATK_STACK:
    case ATK_SELF:
      /* Not supported. */
      break;
    case ATK_COUNT:
      fc_assert(action_id_get_target_kind(act) != ATK_COUNT);
      break;
    }
  } action_iterate_end;

  return FALSE;
}

/**********************************************************************//**
  Find an extra to target for an action at the specified tile.

  Returns the first extra found that the actor may act against at the tile
  or NULL if no proper target is found. (Note that some actions requires
  the absence of an extra since they result in its creation while other
  requires its presence.)

  If the only action(s) that can be performed against a target has the
  rare_pop_up property the target will only be considered valid if the
  accept_all_actions argument is TRUE.
**************************************************************************/
struct extra_type *action_tgt_tile_extra(const struct unit *actor,
                                         const struct tile *target_tile,
                                         bool accept_all_actions)
{
  extra_type_re_active_iterate(target) {
    if (may_unit_act_vs_tile_extra(actor, target_tile, target,
                                   accept_all_actions)) {
      return target;
    }
  } extra_type_re_active_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Find an sub target for the specified action.
**************************************************************************/
int action_sub_target_id_for_action(const struct action *paction,
                                    struct unit *actor_unit)
{
  const struct tile *tgt_tile = unit_tile(actor_unit);
  const struct civ_map *nmap = &(wld.map);

  fc_assert_ret_val(paction->target_complexity == ACT_TGT_COMPL_FLEXIBLE,
                    NO_TARGET);

  switch (action_get_sub_target_kind(paction)) {
  case ASTK_NONE:
    /* Should not be reached */
    fc_assert_ret_val(action_get_sub_target_kind(paction) != ASTK_NONE,
                      NO_TARGET);
    break;
  case ASTK_BUILDING:
    /* Implement if a building sub targeted action becomes flexible */
    fc_assert_ret_val(paction->target_complexity == ACT_TGT_COMPL_FLEXIBLE,
                      NO_TARGET);
    break;
  case ASTK_TECH:
    /* Implement if a tech sub targeted action becomes flexible */
    fc_assert_ret_val(paction->target_complexity == ACT_TGT_COMPL_FLEXIBLE,
                      NO_TARGET);
    break;
  case ASTK_EXTRA:
  case ASTK_EXTRA_NOT_THERE:
    if (action_has_result(paction, ACTRES_PILLAGE)) {
      /* Special treatment for "Pillage" */
      struct extra_type *pextra = NULL;
      enum unit_activity activity = action_get_activity(paction);

      unit_assign_specific_activity_target(actor_unit, &activity,
                                           action_number(paction), &pextra);

      if (pextra != NULL) {
        return extra_number(pextra);
      }
    }
    extra_type_re_active_iterate(tgt_extra) {
      if (action_prob_possible(action_prob_vs_tile(nmap, actor_unit, paction->id,
                                                   tgt_tile, tgt_extra))) {
        /* The actor unit may be able to do this action to the target
         * extra. */
        return extra_number(tgt_extra);
      }
    } extra_type_re_active_iterate_end;
    break;
  case ASTK_SPECIALIST:
    /* Implement if a specialist sub targeted action becomes flexible */
    fc_assert_ret_val(paction->target_complexity == ACT_TGT_COMPL_FLEXIBLE,
                      NO_TARGET);
    break;
  case ASTK_COUNT:
    /* Should not exist. */
    fc_assert_ret_val(action_get_sub_target_kind(paction) != ASTK_COUNT,
                      NO_TARGET);
    break;
  }

  return NO_TARGET;
}

/**********************************************************************//**
  Returns the action auto performer that the specified cause can force the
  specified actor to perform. Returns NULL if no such action auto performer
  exists.
**************************************************************************/
const struct action_auto_perf *
action_auto_perf_unit_sel(const enum action_auto_perf_cause cause,
                          const struct unit *actor,
                          const struct player *other_player,
                          const struct output_type *eval_output,
                          const struct action *eval_action)
{
  const struct req_context actor_ctxt = {
    .player = unit_owner(actor),
    .tile = unit_tile(actor),
    .unit = actor,
    .unittype = unit_type_get(actor),
    .output = eval_output,
    .action = eval_action,
  };

  action_auto_perf_by_cause_iterate(cause, autoperformer) {
    if (are_reqs_active(&actor_ctxt,
                        &(const struct req_context) {
                          .player = other_player,
                        },
                        &autoperformer->reqs, RPT_CERTAIN)) {
      /* Select this action auto performer. */
      return autoperformer;
    }
  } action_auto_perf_by_cause_iterate_end;

  /* Can't even try to force an action. */
  return NULL;
}

#define action_auto_perf_acquire_targets(_target_extra_)                   \
  tgt_city = (target_city ? target_city                                    \
                          : action_tgt_city(actor, unit_tile(actor),       \
                                            TRUE));                        \
  tgt_tile = (target_tile ? target_tile                                    \
                          : action_tgt_tile(actor, unit_tile(actor),       \
                                            _target_extra_,                \
                                            TRUE));                        \
  tgt_unit = (target_unit ? target_unit                                    \
                          : action_tgt_unit(actor, unit_tile(actor),       \
                                            TRUE));

/**********************************************************************//**
  Make the specified actor unit perform an action because of cause.

  Returns the action the actor unit was forced to perform.
  Returns NULL if that didn't happen.

  Note that the return value doesn't say anything about survival.
**************************************************************************/
const struct action *
action_auto_perf_unit_do(const enum action_auto_perf_cause cause,
                         struct unit *actor,
                         const struct player *other_player,
                         const struct output_type *eval_output,
                         const struct action *eval_action,
                         const struct tile *target_tile,
                         const struct city *target_city,
                         const struct unit *target_unit,
                         const struct extra_type *target_extra)
{
  int actor_id;

  const struct city *tgt_city;
  const struct tile *tgt_tile;
  const struct unit *tgt_unit;
  const struct civ_map *nmap = &(wld.map);

  const struct action_auto_perf *autoperf
    = action_auto_perf_unit_sel(cause, actor, other_player,
                                eval_output, eval_action);

  if (!autoperf) {
    /* No matching Action Auto Performer. */
    return NULL;
  }

  actor_id = actor->id;

  /* Acquire the targets. */
  action_auto_perf_acquire_targets(target_extra);

  action_auto_perf_actions_iterate(autoperf, act) {
    if (action_id_get_actor_kind(act) == AAK_UNIT) {
      /* This action can be done by units. */

#define perform_action_to(act, actor, tgtid, tgt_extra)                   \
  if (unit_perform_action(unit_owner(actor),                              \
                          actor->id, tgtid, tgt_extra,                    \
                          NULL, act, ACT_REQ_RULES)) {                    \
    return action_by_number(act);                                         \
  }

      switch (action_id_get_target_kind(act)) {
      case ATK_STACK:
        if (tgt_tile
            && is_action_enabled_unit_on_stack(nmap, act, actor, tgt_tile)) {
          perform_action_to(act, actor, tgt_tile->index, EXTRA_NONE);
        }
        break;
      case ATK_TILE:
        if (tgt_tile
            && is_action_enabled_unit_on_tile(nmap, act, actor,
                                              tgt_tile, target_extra)) {
          perform_action_to(act, actor, tgt_tile->index,
                            target_extra ? extra_number(target_extra) : -1);
        }
        break;
      case ATK_EXTRAS:
        if (tgt_tile
            && is_action_enabled_unit_on_extras(nmap, act, actor,
                                                tgt_tile, target_extra)) {
          perform_action_to(act, actor, tgt_tile->index,
                            target_extra ? extra_number(target_extra) : -1);
        }
        break;
      case ATK_CITY:
        if (tgt_city
            && is_action_enabled_unit_on_city(nmap, act, actor, tgt_city)) {
          perform_action_to(act, actor, tgt_city->id, EXTRA_NONE)
        }
        break;
      case ATK_UNIT:
        if (tgt_unit
            && is_action_enabled_unit_on_unit(nmap, act, actor, tgt_unit)) {
          perform_action_to(act, actor, tgt_unit->id, EXTRA_NONE);
        }
        break;
      case ATK_SELF:
        if (actor
            && is_action_enabled_unit_on_self(nmap, act, actor)) {
          perform_action_to(act, actor, actor->id, EXTRA_NONE);
        }
        break;
      case ATK_COUNT:
        fc_assert(action_id_get_target_kind(act) != ATK_COUNT);
      }

      if (!unit_is_alive(actor_id)) {
        /* The unit is gone. Maybe it was killed in Lua? */
        return NULL;
      }
    }
  } action_auto_perf_actions_iterate_end;

  return NULL;
}

/**********************************************************************//**
  Returns the probability for the specified actor unit to be forced to
  perform an action by the specified cause.
**************************************************************************/
struct act_prob
action_auto_perf_unit_prob(const enum action_auto_perf_cause cause,
                           struct unit *actor,
                           const struct player *other_player,
                           const struct output_type *eval_output,
                           const struct action *eval_action,
                           const struct tile *target_tile,
                           const struct city *target_city,
                           const struct unit *target_unit,
                           const struct extra_type *target_extra)
{
  struct act_prob out;

  const struct city *tgt_city;
  const struct tile *tgt_tile;
  const struct unit *tgt_unit;
  const struct civ_map *nmap = &(wld.map);

  const struct action_auto_perf *autoperf
    = action_auto_perf_unit_sel(cause, actor, other_player,
                                eval_output, eval_action);

  if (!autoperf) {
    /* No matching Action Auto Performer. */
    return ACTPROB_IMPOSSIBLE;
  }

  out = ACTPROB_IMPOSSIBLE;

  /* Acquire the targets. */
  action_auto_perf_acquire_targets(target_extra);

  action_auto_perf_actions_iterate(autoperf, act) {
    struct act_prob current = ACTPROB_IMPOSSIBLE;

    if (action_id_get_actor_kind(act) == AAK_UNIT) {
      /* This action can be done by units. */

      switch (action_id_get_target_kind(act)) {
      case ATK_STACK:
        if (tgt_tile
            && is_action_enabled_unit_on_stack(nmap, act, actor, tgt_tile)) {
          current = action_prob_vs_stack(nmap, actor, act, tgt_tile);
        }
        break;
      case ATK_TILE:
        if (tgt_tile
            && is_action_enabled_unit_on_tile(nmap, act, actor,
                                              tgt_tile, target_extra)) {
          current = action_prob_vs_tile(nmap, actor, act, tgt_tile, target_extra);
        }
        break;
      case ATK_EXTRAS:
        if (tgt_tile
            && is_action_enabled_unit_on_extras(nmap, act, actor,
                                                tgt_tile, target_extra)) {
          current = action_prob_vs_extras(nmap, actor, act,
                                          tgt_tile, target_extra);
        }
        break;
      case ATK_CITY:
        if (tgt_city
            && is_action_enabled_unit_on_city(nmap, act, actor, tgt_city)) {
          current = action_prob_vs_city(nmap, actor, act, tgt_city);
        }
        break;
      case ATK_UNIT:
        if (tgt_unit
            && is_action_enabled_unit_on_unit(nmap, act, actor, tgt_unit)) {
          current = action_prob_vs_unit(nmap, actor, act, tgt_unit);
        }
        break;
      case ATK_SELF:
        if (actor
            && is_action_enabled_unit_on_self(nmap, act, actor)) {
          current = action_prob_self(nmap, actor, act);
        }
        break;
      case ATK_COUNT:
        fc_assert(action_id_get_target_kind(act) != ATK_COUNT);
      }
    }

    out = action_prob_fall_back(&out, &current);
  } action_auto_perf_actions_iterate_end;

  return out;
}

/************************************************************************//**
  Returns TRUE iff the spy/diplomat was caught outside of a diplomatic
  battle.
****************************************************************************/
bool action_failed_dice_roll(const struct player *act_player,
                             const struct unit *act_unit,
                             const struct city *tgt_city,
                             const struct player *tgt_player,
                             const struct action *paction)
{
  int odds = action_dice_roll_odds(act_player, act_unit,
                                   tgt_city, tgt_player,
                                   paction);

  /* Roll the dice. */
  return fc_rand(100) >= odds;
}
