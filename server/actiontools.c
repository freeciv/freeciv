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
  Take care of any consequences (like casus belli) of getting caught while
  trying to perform the given action.
**************************************************************************/
void action_consequence_caught(const int action_id,
                               struct player *offender,
                               struct player *victim_player,
                               const struct tile *victim_tile,
                               const char *victim_link)
{
  if (offender == victim_player) {
    /* The player is more than willing to forgive him self. */
    return;
  }

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

    /* Customize message based on action type. */
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

    /* Give the victim player a casus belli. */
    player_diplstate_get(victim_player, offender)->has_reason_to_cancel = 2;

    /* Notify players controlled by the built in AI. */
    call_incident(INCIDENT_DIPLOMAT, offender, victim_player);

    send_player_all_c(offender, NULL);
    send_player_all_c(victim_player, NULL);
  }
}

/**************************************************************************
  Take care of any consequences (like casus belli) of successfully
  performing the given action.
**************************************************************************/
void action_consequence_success(const int action_id,
                                struct player *offender,
                                struct player *victim_player,
                                const struct tile *victim_tile,
                                const char *victim_link)
{
  if (offender == victim_player) {
    /* The player is more than willing to forgive him self. */
    return;
  }

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

    /* Customize message based on action type. */
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

    /* Give the victim player a casus belli. */
    player_diplstate_get(victim_player, offender)->has_reason_to_cancel = 2;

    /* Notify players controlled by the built in AI. */
    call_incident(INCIDENT_DIPLOMAT, offender, victim_player);

    send_player_all_c(offender, NULL);
    send_player_all_c(victim_player, NULL);
  }
}
