/**********************************************************************
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

/* common */
#include "actions.h"

/* server */
#include "aiiface.h"
#include "actiontools.h"
#include "notify.h"
#include "plrhand.h"

/**************************************************************************
  Give the victim a casus belli against the offender.
**************************************************************************/
static void action_give_casus_belli(struct player *offender,
                                    struct player *victim_player)
{
  if (victim_player && offender != victim_player) {
    /* If an unclaimed tile is nuked there is no victim to give casus
     * belli. If an actor nukes his own tile he is more than willing to
     * forgive him self. */

    /* Give the victim player a casus belli. */
    player_diplstate_get(victim_player, offender)->has_reason_to_cancel =
        2;
  }
}

/**************************************************************************
  Returns the kind of diplomatic incident an action may cause.
**************************************************************************/
static enum incident_type action_to_incident(const int action_id)
{
  if (action_id == ACTION_NUKE
      || action_id == ACTION_SPY_NUKE) {
    return INCIDENT_NUCLEAR;
  } else {
    /* FIXME: Some actions are neither nuclear nor diplomat. */
    return INCIDENT_DIPLOMAT;
  }
}

/**************************************************************************
  Notify the players controlled by the built in AI.
**************************************************************************/
static void action_notify_ai(const int action_id,
                             struct player *offender,
                             struct player *victim_player)
{
  const int incident = action_to_incident(action_id);

  /* Notify the victim player. */
  call_incident(incident, offender, victim_player);

  if (incident == INCIDENT_NUCLEAR) {
    /* Tell the world. */
    if (offender == victim_player) {
      players_iterate(oplayer) {
        if (victim_player != oplayer) {
          call_incident(INCIDENT_NUCLEAR_SELF, offender, oplayer);
        }
      } players_iterate_end;
    } else {
      players_iterate(oplayer) {
        if (victim_player != oplayer) {
          call_incident(INCIDENT_NUCLEAR_NOT_TARGET, offender, oplayer);
        }
      } players_iterate_end;
    }
  }
}

/**************************************************************************
  Notify the actor and the victim that the failed action gave the victim a
  casus belli against the actor.
**************************************************************************/
static void action_notify_caught(const int action_id,
                                 struct player *offender,
                                 struct player *victim_player,
                                 const struct tile *victim_tile,
                                 const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to him self. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(action_id)) {
  case ATK_CITY:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident getting caught"
                    " trying to do %s to %s."),
                  gen_action_translated_name(action_id),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident getting caught"
                    " trying to do %s to %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_UNITS:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident getting caught"
                    " trying to do %s to %s %s."),
                  gen_action_translated_name(action_id),
                  nation_adjective_for_player(victim_player),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident getting caught"
                    " trying to do %s to your %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_TILE:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident getting caught"
                    " trying to do %s at %s."),
                  gen_action_translated_name(action_id),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident getting caught"
                    " trying to do %s at %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**************************************************************************
  Take care of any consequences (like casus belli) of getting caught while
  trying to perform the given action.

  victim_player can be NULL
**************************************************************************/
void action_consequence_caught(const int action_id,
                               struct player *offender,
                               struct player *victim_player,
                               const struct tile *victim_tile,
                               const char *victim_link)
{
  if (0 < get_target_bonus_effects(NULL,
                                   offender, victim_player,
                                   tile_city(victim_tile),
                                   NULL,
                                   victim_tile,
                                   NULL, NULL,
                                   NULL, NULL,
                                   action_by_number(action_id),
                                   EFT_CASUS_BELLI_CAUGHT)) {
    /* Getting caught trying to do this action gives the victim a
     * casus belli against the actor. */

    /* Notify all players by sending them a message. */
    action_notify_caught(action_id, offender, victim_player,
                         victim_tile, victim_link);

    /* Give casus belli. */
    action_give_casus_belli(offender, victim_player);

    /* Notify players controlled by the built in AI. */
    action_notify_ai(action_id, offender, victim_player);

    /* Update the clients. */
    send_player_all_c(offender, NULL);

    if (victim_player != NULL && victim_player != offender) {
      /* The actor player was just sent. */
      /* An action against an ownerless tile is victimless. */
      send_player_all_c(victim_player, NULL);
    }
  }
}

/**************************************************************************
  Notify the actor and the victim that the performed action gave the
  victim a casus belli against the actor.
**************************************************************************/
static void action_notify_success(const int action_id,
                                  struct player *offender,
                                  struct player *victim_player,
                                  const struct tile *victim_tile,
                                  const char *victim_link)
{
  if (!victim_player || offender == victim_player) {
    /* There is no victim or the actor did this to him self. */
    return;
  }

  /* Custom message based on action type. */
  switch (action_get_target_kind(action_id)) {
  case ATK_CITY:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident doing %s to %s."),
                  gen_action_translated_name(action_id),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident doing %s to %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_UNIT:
  case ATK_UNITS:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident doing %s to %s %s."),
                  gen_action_translated_name(action_id),
                  nation_adjective_for_player(victim_player),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident doing "
                    "%s to your %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_TILE:
    notify_player(offender, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("You have caused an incident doing %s at %s."),
                  gen_action_translated_name(action_id),
                  victim_link);
    notify_player(victim_player, victim_tile,
                  E_DIPLOMATIC_INCIDENT, ftc_server,
                  _("The %s have caused an incident doing %s at %s."),
                  nation_plural_for_player(offender),
                  gen_action_translated_name(action_id),
                  victim_link);
    break;
  case ATK_COUNT:
    fc_assert(ATK_COUNT != ATK_COUNT);
    break;
  }
}

/**************************************************************************
  Take care of any consequences (like casus belli) of successfully
  performing the given action.

  victim_player can be NULL
**************************************************************************/
void action_consequence_success(const int action_id,
                                struct player *offender,
                                struct player *victim_player,
                                const struct tile *victim_tile,
                                const char *victim_link)
{
  if (0 < get_target_bonus_effects(NULL,
                                   offender, victim_player,
                                   tile_city(victim_tile),
                                   NULL,
                                   victim_tile,
                                   NULL, NULL,
                                   NULL, NULL,
                                   action_by_number(action_id),
                                   EFT_CASUS_BELLI_SUCCESS)) {
    /* Successfully doing this action gives the victim a casus belli
     * against the actor. */

    /* Notify all players by sending them a message. */
    action_notify_success(action_id, offender, victim_player,
                          victim_tile, victim_link);

    /* Give casus belli. */
    action_give_casus_belli(offender, victim_player);

    /* Notify players controlled by the built in AI. */
    action_notify_ai(action_id, offender, victim_player);

    /* Update the clients. */
    send_player_all_c(offender, NULL);

    if (victim_player != NULL && victim_player != offender) {
      /* The actor player was just sent. */
      /* An action against an ownerless tile is victimless. */
      send_player_all_c(victim_player, NULL);
    }
  }
}
